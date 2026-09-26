#include <string.h>

#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_flash.h"
#if CONFIG_SPIRAM
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#endif
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "espos.h"
#include "espos_config.h"
#include "espos_event.h"
#include "espos_health.h"
#include "espos_net.h"
#include "espos_sk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "board.h"
#include "button_hw.h"
#include "button_logic.h"
#include "device_config.h"
#include "eth_w5500.h"
#include "espos_wifi.h"
#include "indicator.h"
#include "input_hw.h"
#include "input_sense.h"
#include "n2k_bridge.h"
#include "relay_ctrl.h"
#include "relay_hw.h"
#include "sk_bridge.h"
#include "sk_espos.h"
#include "web_ui.h"

static const char *TAG = "app";

// Where boards look for updates unless someone sets another source. The
// release workflow keeps it current (USER_MANUAL.md section 3.3).
#define OTA_MANIFEST_URL "https://raw.githubusercontent.com/BoatHacks/signalk-espOS-8relay/ota/manifest.json"

#define TICK_MS 10
#define IO_STALL_MS 2000        // raise espOS's taskStalled alarm after this
#define IO_RESTART_US 3000000LL // restart if the I/O loop is silent this long

static TaskHandle_t s_io_task;
static volatile int64_t s_io_alive_us;
static button_state_t s_button;
static button_hw_t s_button_hw;
static bool s_button_hw_ok;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ------------------------------------------------ wiring between modules

static esp_err_t input_override(uint8_t relay, input_action_t action)
{
    if (action == INPUT_ACTION_TOGGLE) {
        return relay_ctrl_toggle(relay, RELAY_SRC_INPUT);
    }
    return relay_ctrl_set(relay, action == INPUT_ACTION_ON, RELAY_SRC_INPUT);
}

static esp_err_t sk_set_relay(uint8_t relay, bool on)
{
    return relay_ctrl_set(relay, on, RELAY_SRC_SK);
}

static esp_err_t n2k_set_relay(uint8_t relay, bool on)
{
    return relay_ctrl_set(relay, on, RELAY_SRC_N2K);
}

static esp_err_t web_set_relay(uint8_t relay, bool on)
{
    return relay_ctrl_set(relay, on, RELAY_SRC_WEB);
}

// Short, stable names: the relay page shows them and the log prints them.
static const char *source_name(relay_source_t src)
{
    switch (src) {
    case RELAY_SRC_SK: return "signalk";
    case RELAY_SRC_N2K: return "nmea2000";
    case RELAY_SRC_INPUT: return "input";
    case RELAY_SRC_PULSE_END: return "pulse";
    case RELAY_SRC_FAILSAFE: return "failsafe";
    case RELAY_SRC_WEB: return "web";
    case RELAY_SRC_MAX_ON: return "maxOn";
    }
    return "unknown";
}

// The relay page's status line.
static void web_status(web_ui_status_t *out)
{
    snprintf(out->version, sizeof(out->version), "%s", esp_app_get_description()->version);

    espos_net_status_t net = {0};
    espos_net_get_status(&net);
    snprintf(out->hostname, sizeof(out->hostname), "%s", net.hostname);
    out->net_up = net.up;
    snprintf(out->net_iface, sizeof(out->net_iface), "%s", espos_net_if_str(net.iface));
    snprintf(out->ip, sizeof(out->ip), "%s", net.ip);

    espos_sk_ws_status_t sk = {0};
    espos_sk_ws_get_status(&sk);
    out->sk_enabled = sk.enabled;
    out->sk_connected = sk.connected;
    espos_sk_server_t srv = {0};
    if (espos_sk_get_server(&srv) == ESP_OK && srv.host[0]) {
        snprintf(out->sk_server, sizeof(out->sk_server), "%s:%u", srv.host, srv.port);
    }

    n2k_bridge_status_t n2k = {0};
    n2k_bridge_get_status(&n2k);
    out->n2k_started = n2k.started;
    out->n2k_address = n2k.address;
    out->n2k_traffic = n2k.traffic;
}

static void on_relay_change(uint8_t channel, bool on, relay_source_t src, uint8_t mask, void *arg)
{
    ESP_LOGI(TAG, "relay %u %s by %s", channel, on ? "on" : "off", source_name(src));
    web_ui_relay_changed(channel, source_name(src));
    sk_bridge_relay_changed(channel, on);
    n2k_bridge_state_changed();
}

static void on_input_change(uint8_t channel, bool on, uint8_t mask, void *arg)
{
    sk_bridge_input_changed(channel, on);
    n2k_bridge_state_changed();
}

static void on_sk_stream(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    sk_bridge_stream_changed(id == ESPOS_EVENT_SK_STREAM_CONNECTED);
}

// Per changed key, on the writer's task: just wake the I/O task, which
// reloads once however many keys changed.
static void on_config_change(const char *ns, const char *key, void *arg)
{
    if (s_io_task && strcmp(ns, DEVICE_CONFIG_NS) == 0) {
        xTaskNotifyGive(s_io_task);
    }
    // The captive portal's own page does not turn the station back on when
    // a network is saved (confirmed on hardware, plan 14) -- there is a
    // separate "station enabled" box a person must also tick, easy to miss
    // on a board they are trying to recover after losing network access.
    // Do it here instead, but only while the portal is actually open: a
    // network slot edited from the normal settings page, with the station
    // deliberately off (e.g. an Ethernet-only setup), must not be flipped
    // back on as a side effect.
    if (strcmp(ns, "wifi") == 0 && strncmp(key, "ssid", 4) == 0) {
        espos_wifi_status_t wst = {0};
        bool sta_enabled = true;
        if (espos_wifi_get_status(&wst) == ESP_OK && wst.sm.portal_active &&
            espos_config_get_bool("wifi", "sta_enabled", &sta_enabled) == ESP_OK && !sta_enabled) {
            ESP_LOGI(TAG, "network saved while the portal was open: turning the station back on");
            espos_config_set_bool("wifi", "sta_enabled", true);
        }
    }
}

// BOOT button actions (plan 14, issue #7). Both restart so the relays take
// their fail-safe/boot rules (SPEC.md §3.2) rather than being left in
// whatever state a settings change happened to catch them in.
static void do_reopen_portal(void)
{
    ESP_LOGW(TAG, "BOOT button: reopening the setup access point");
    // espOS 0.10.3 has no public "start the portal now" call; with the
    // station off, it opens its portal instead of trying to connect.
    espos_config_set_bool("wifi", "sta_enabled", false);
    esp_restart();
}

static void do_factory_reset(void)
{
    ESP_LOGW(TAG, "BOOT button: factory reset");
    // Not covered by espos_config_factory_reset(): the relays' persisted
    // `hold` state (its own NVS namespace, not espos_config) and the
    // SignalK token (factory means everything -- the board must be
    // approved on the server again).
    relay_hw_clear_hold_state();
    espos_sk_forget_token();
    espos_config_factory_reset();
    esp_restart();
}

static void button_poll(void)
{
    if (!s_button_hw_ok) {
        return;
    }
    button_feedback_t feedback;
    const button_action_t action =
        button_logic_update(&s_button, s_button_hw.read(s_button_hw.ctx), now_ms(), &feedback);

    static const indicator_override_t override_of[] = {
        [BUTTON_FEEDBACK_NONE] = INDICATOR_OVERRIDE_NONE,
        [BUTTON_FEEDBACK_PORTAL] = INDICATOR_OVERRIDE_PORTAL,
        [BUTTON_FEEDBACK_RESET] = INDICATOR_OVERRIDE_RESET,
    };
    indicator_set_override(override_of[feedback]);

    switch (action) {
    case BUTTON_ACTION_PORTAL: do_reopen_portal(); break;
    case BUTTON_ACTION_RESET: do_factory_reset(); break;
    case BUTTON_ACTION_NONE: break;
    }
}

// Relay pulses, flash saves, input polling and the SignalK-loss timer.
// If this loop stalls, pulses stop ending and the fail-safe stops firing, so
// the device restarts within IO_RESTART_US (io_supervisor) and relays take
// their boot state. espOS's task watchdog (30 s) is the backstop.
static void io_task(void *arg)
{
    ESP_ERROR_CHECK(espos_health_watch_task("io", IO_STALL_MS));
    for (;;) {
        espos_health_kick();
        s_io_alive_us = esp_timer_get_time();
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TICK_MS)) > 0) {
            device_config_t cfg;
            if (device_config_load(&cfg) == ESP_OK) {
                relay_ctrl_update_config(&cfg);
                input_sense_update_config(&cfg);
                sk_bridge_update_config(&cfg);
                indicator_update_config(&cfg);
                web_ui_update_config(&cfg);
            }
        }
        relay_ctrl_tick();
        input_sense_poll();
        button_poll();
        sk_bridge_tick();
    }
}

static void io_supervisor(void *arg)
{
    if (esp_timer_get_time() - s_io_alive_us > IO_RESTART_US) {
        ESP_LOGE(TAG, "I/O loop stalled; restarting so relays return to their boot state");
        esp_restart();
    }
}

#if CONFIG_SPIRAM
// cJSON builds trees of many small allocations, which the 16 KB PSRAM
// threshold keeps in internal RAM. espOS parses and re-prints its whole
// settings schema (~47 KB) that way on every settings page load, which ran
// internal RAM out (ESP_ERR_NO_MEM). JSON is never touched from interrupts,
// so all of it can live in PSRAM; internal RAM is the fallback. The hooks
// are global: they cover espOS's JSON as well as ours.
static void *json_malloc(size_t size)
{
    return heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void json_to_psram(void)
{
    cJSON_Hooks hooks = {.malloc_fn = json_malloc, .free_fn = heap_caps_free};
    cJSON_InitHooks(&hooks);
}
#endif

// What esptool's chip report says, in the boot log: which PSRAM (if any) is
// inside the chip package decides the PSRAM mode a build must use, and a
// wrong mode stops the chip at boot.
static void log_chip_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t psram_cap = 0, psram_vendor = 0, flash_cap = 0, pkg = 0;
    esp_efuse_read_field_blob(ESP_EFUSE_PSRAM_CAP, &psram_cap, ESP_EFUSE_PSRAM_CAP[0]->bit_count + 1);
    esp_efuse_read_field_blob(ESP_EFUSE_PSRAM_VENDOR, &psram_vendor, 2);
    esp_efuse_read_field_blob(ESP_EFUSE_FLASH_CAP, &flash_cap, 3);
    esp_efuse_read_field_blob(ESP_EFUSE_PKG_VERSION, &pkg, 3);
    uint32_t flash_bytes = 0;
    esp_flash_get_size(NULL, &flash_bytes);

    // eFuse encodings from ESP-IDF's esp32s3 esp_efuse_table.csv.
    static const char *const psram_mb[] = {"none", "8 MB", "2 MB", "16 MB", "4 MB"};
    static const char *const vendor[] = {"none", "AP 3.3 V", "AP 1.8 V", "?"};
    static const char *const flash_mb[] = {"none", "8 MB", "4 MB"};
    const char *mode = "no PSRAM in the chip package (a separate PSRAM chip can't be seen from here)";
    if (psram_cap == 1 || psram_cap == 3) {
        mode = "octal (ESP32-S3R8/R8V/R16V)";
    } else if (psram_cap == 2 || psram_cap == 4) {
        mode = "quad (ESP32-S3R2)";
    }
    ESP_LOGI(TAG, "chip: model %d rev v%d.%d, %d cores, package %lu, features 0x%lx%s%s", (int)chip.model,
             chip.revision / 100, chip.revision % 100, chip.cores, (unsigned long)pkg, (unsigned long)chip.features,
             (chip.features & CHIP_FEATURE_EMB_PSRAM) ? " (embedded PSRAM)" : "",
             (chip.features & CHIP_FEATURE_EMB_FLASH) ? " (embedded flash)" : "");
    ESP_LOGI(TAG, "chip: in-package PSRAM %s, vendor %s -> %s",
             psram_cap < 5 ? psram_mb[psram_cap] : "?", vendor[psram_vendor & 3], mode);
#if CONFIG_SPIRAM
    char psram[32];
    if (esp_psram_is_initialized()) {
        snprintf(psram, sizeof(psram), "on, %u MB in use", (unsigned)(esp_psram_get_size() >> 20));
    } else {
        snprintf(psram, sizeof(psram), "on, but not found");
    }
#else
    const char *psram = "off";
#endif
    ESP_LOGI(TAG, "chip: in-package flash %s, flash chip %lu MB; PSRAM in this build: %s",
             flash_cap < 3 ? flash_mb[flash_cap] : "?", (unsigned long)(flash_bytes >> 20), psram);
}

// espOS ships with no manifest URL. Fill in this project's, once: an empty
// URL is also how someone opts out of manifest checks, so after the first
// boot with this firmware an emptied URL stays empty.
static void default_ota_manifest(void)
{
    nvs_handle_t h;
    if (nvs_open("app", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    uint8_t done = 0;
    if (nvs_get_u8(h, "ota_url_set", &done) != ESP_OK || !done) {
        char url[8] = "";
        espos_config_get_str("ota", "manifest_url", url, sizeof(url), NULL);
        if (url[0] == '\0' && espos_config_set_str("ota", "manifest_url", OTA_MANIFEST_URL) == ESP_OK) {
            ESP_LOGI(TAG, "update manifest: %s", OTA_MANIFEST_URL);
        }
        nvs_set_u8(h, "ota_url_set", 1);
        nvs_commit(h);
    }
    nvs_close(h);
}

// Runs after espOS has the config store up and before any networking, so
// relays reach their boot state (SPEC.md section 3.2) as early as possible:
// after a warm reset, default-safe relays would otherwise stay on until the
// network is up.
static esp_err_t start_io(void *arg)
{
    device_config_t *cfg = arg;
    ESP_ERROR_CHECK(device_config_load(cfg));

    // The I/O task and the relay/input listeners below call into the
    // SignalK bridge long before sk_bridge_start(), which needs the network.
    ESP_ERROR_CHECK(sk_bridge_init());

    relay_ctrl_hw_t hw;
    ESP_ERROR_CHECK(relay_hw_create(&hw));
    // A dead expander is reported through espOS health; keep booting so the
    // device stays reachable for diagnosis and updates.
    if (relay_ctrl_init(&hw, cfg) != ESP_OK) {
        ESP_LOGE(TAG, "relay expander did not respond; relays unavailable");
    }
    // Every relay's state now is its start-up state (off, or held).
    for (uint8_t ch = 1; ch <= BOARD_CHANNELS; ch++) {
        web_ui_relay_changed(ch, "boot");
    }
    ESP_ERROR_CHECK(relay_ctrl_add_listener(on_relay_change, NULL));

    input_sense_hw_t in_hw;
    ESP_ERROR_CHECK(input_hw_create(&in_hw));
    // Overrides are applied on the first settled reading, after the relays'
    // own boot state above.
    ESP_ERROR_CHECK(input_sense_init(&in_hw, cfg, input_override));
    ESP_ERROR_CHECK(input_sense_add_listener(on_input_change, NULL));

    if (button_hw_create(&s_button_hw) == ESP_OK) {
        // A button already held here (e.g. still being released from the
        // bootloader check) is ignored until it is first seen released, so
        // it can never trigger an action from a state it did not choose.
        button_logic_init(&s_button, s_button_hw.read(s_button_hw.ctx));
        s_button_hw_ok = true;
    } else {
        ESP_LOGE(TAG, "BOOT button unavailable");
    }

    s_io_alive_us = esp_timer_get_time();
    xTaskCreate(io_task, "io", 4096, NULL, 5, &s_io_task);
    const esp_timer_create_args_t sup = {.callback = io_supervisor, .name = "io_sup"};
    esp_timer_handle_t sup_timer;
    ESP_ERROR_CHECK(esp_timer_create(&sup, &sup_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(sup_timer, 500 * 1000));
    return ESP_OK;
}

void app_main(void)
{
#if CONFIG_SPIRAM
    json_to_psram();  // before espOS parses anything
#endif
    log_chip_info();
    static device_config_t cfg;
    espos_start_opts_t opts = ESPOS_START_OPTS_DEFAULT;
    opts.app_name = "signalk-espOS-8relay";
    opts.board = BOARD_NAME;
    opts.before_network = start_io;
    opts.arg = &cfg;
    ESP_ERROR_CHECK(espos_start(&opts));

    default_ota_manifest();

    // Bank ids only change on restart, so checking once at boot is enough.
    device_config_report_health(&cfg);
    ESP_ERROR_CHECK(espos_config_subscribe(on_config_change, NULL));

    static const sk_bridge_io_t sk_io = {
        .set_relay = sk_set_relay,
        .relay_mask = relay_ctrl_get_mask,
        .inputs_ready = input_sense_ready,
        .input_mask = input_sense_get_mask,
        .sk_lost = relay_ctrl_sk_lost,
        .now_ms = now_ms,
    };
    ESP_ERROR_CHECK(espos_event_subscribe(ESPOS_EVENT_SK_STREAM_CONNECTED, on_sk_stream, NULL));
    ESP_ERROR_CHECK(espos_event_subscribe(ESPOS_EVENT_SK_STREAM_DISCONNECTED, on_sk_stream, NULL));
    ESP_ERROR_CHECK(sk_bridge_start(&sk_espos_api, &sk_io, &cfg));

    static const n2k_bridge_io_t n2k_io = {
        .set_relay = n2k_set_relay,
        .relay_mask = relay_ctrl_get_mask,
        .inputs_ready = input_sense_ready,
        .input_mask = input_sense_get_mask,
    };
    // NMEA 2000 failing must not stop SignalK control.
    if (n2k_bridge_start(&n2k_io, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "NMEA 2000 unavailable");
    }

    static const web_ui_io_t web_io = {
        .set_relay = web_set_relay,
        .relay_mask = relay_ctrl_get_mask,
        .inputs_ready = input_sense_ready,
        .input_mask = input_sense_get_mask,
        .get_status = web_status,
        .test_buzzer = indicator_test_buzzer,
    };
    // The relay page is a convenience; SignalK and NMEA 2000 don't need it.
    if (web_ui_start(&web_io, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "relay page unavailable");
    }

    // The LED and buzzer only report; the device works without them.
    if (indicator_start(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "status LED/buzzer unavailable");
    }

    if (cfg.eth_enabled) {
        // Ethernet failing must not stop the device: it still works over WiFi.
        esp_err_t err = eth_w5500_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Ethernet unavailable (%s); continuing on WiFi", esp_err_to_name(err));
        }
    }
}

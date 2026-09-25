#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "espos.h"
#include "espos_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board.h"
#include "device_config.h"
#include "eth_w5500.h"
#include "relay_ctrl.h"
#include "relay_hw.h"

static const char *TAG = "app";

#define RELAY_TICK_MS 10

static TaskHandle_t s_relay_task;

// Per changed key, on the writer's task: just wake the relay task, which
// reloads once however many keys changed.
static void on_config_change(const char *ns, const char *key, void *arg)
{
    if (s_relay_task && strcmp(ns, DEVICE_CONFIG_NS) == 0) {
        xTaskNotifyGive(s_relay_task);
    }
}

static void relay_task(void *arg)
{
    for (;;) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RELAY_TICK_MS)) > 0) {
            device_config_t cfg;
            if (device_config_load(&cfg) == ESP_OK) {
                relay_ctrl_update_config(&cfg);
            }
        }
        relay_ctrl_tick();
    }
}

// Runs after espOS has the config store up and before any networking, so
// relays reach their boot state (SPEC.md section 3.2) as early as possible:
// after a warm reset, default-safe relays would otherwise stay on until the
// network is up.
static esp_err_t start_relays(void *arg)
{
    device_config_t *cfg = arg;
    ESP_ERROR_CHECK(device_config_load(cfg));
    relay_ctrl_hw_t hw;
    ESP_ERROR_CHECK(relay_hw_create(&hw));
    // A dead expander is reported through espOS health; keep booting so the
    // device stays reachable for diagnosis and updates.
    if (relay_ctrl_init(&hw, cfg) != ESP_OK) {
        ESP_LOGE(TAG, "relay expander did not respond; relays unavailable");
    }
    xTaskCreate(relay_task, "relays", 4096, NULL, 5, &s_relay_task);
    return ESP_OK;
}

void app_main(void)
{
    static device_config_t cfg;
    espos_start_opts_t opts = ESPOS_START_OPTS_DEFAULT;
    opts.app_name = "signalk-espOS-8relay";
    opts.board = BOARD_NAME;
    opts.before_network = start_relays;
    opts.arg = &cfg;
    ESP_ERROR_CHECK(espos_start(&opts));

    // Bank ids only change on restart, so checking once at boot is enough.
    device_config_report_health(&cfg);
    ESP_ERROR_CHECK(espos_config_subscribe(on_config_change, NULL));

    if (cfg.eth_enabled) {
        // Ethernet failing must not stop the device: it still works over WiFi.
        esp_err_t err = eth_w5500_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Ethernet unavailable (%s); continuing on WiFi", esp_err_to_name(err));
        }
    }
}

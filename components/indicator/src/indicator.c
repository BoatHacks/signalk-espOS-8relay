#include "indicator.h"

#include <stdatomic.h>
#include <string.h>

#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "espos_health.h"
#include "espos_net.h"
#include "espos_sk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "indicator_logic.h"
#include "led_strip.h"
#include "tone.h"
#include "tone_buzzer.h"
#include "tone_player.h"

static const char *TAG = "indicator";

#define LOOP_MS 10
#define MORSE_UNIT_MS 80  // about 15 words per minute
#define MORSE_PAUSE_MS 5000
#define MAX_SEGS 128  // "IN 1 2 3 4 5 6 7 8" needs about 90

#define BUZZER_TIMER LEDC_TIMER_0
#define BUZZER_CHANNEL LEDC_CHANNEL_0

static struct {
    led_strip_handle_t led;
    tone_buzzer_t buzzer;  // driven by indicator_task() only, once started
    atomic_uchar brightness;
    atomic_bool buzzer_enabled;
    atomic_bool sk_relevant;
    atomic_ushort freq_hz;       // buzzer_freq_hz; applied by the task
    atomic_bool test_requested;  // indicator_test_buzzer() -> task
    atomic_bool busy;            // an alarm, a test or a chirp is sounding
    atomic_bool started;
    atomic_int override;         // indicator_override_t; NONE = 0, show status as usual

    // Input alarms on the buzzer (plan 10 follow-up): the inputs that read
    // on (from input_sense's listener, so only once they have settled) and
    // the ones whose alarm is set to buzz.
    atomic_uchar input_mask;
    atomic_uchar input_buzz_mask;

    // Event chirps (plan 19, issue #14). `tones`/`n_tones` are written by
    // indicator_update_config() (the I/O task) and read by indicator_task();
    // s_tone_mux protects both. Everything else here is a plain index,
    // updated the same way but small enough to be a single atomic.
    atomic_bool event_enabled;  // buzzer_on_event
    tone_t tones[TONE_MAX_TONES];
    size_t n_tones;
    atomic_int boot_idx, portal_idx, reset_idx;  // -1 = no tone assigned
    atomic_int relay_on_idx[BOARD_CHANNELS];
    atomic_int relay_off_idx[BOARD_CHANNELS];
    atomic_int pulse_start_idx[BOARD_CHANNELS];
    atomic_int pulse_stop_idx[BOARD_CHANNELS];
    atomic_int input_on_idx[BOARD_CHANNELS];
    atomic_int input_off_idx[BOARD_CHANNELS];
    atomic_int chirp_request;  // index into tones[] a caller wants played, -1 = none

    // Tones page "Play" button (plan 19 follow-up): an arbitrary one-shot
    // RTTTL string, not looked up from the library. `preview` is guarded by
    // s_tone_mux like `tones` above.
    tone_t preview;
    atomic_bool preview_requested;
} s;

static portMUX_TYPE s_tone_mux = portMUX_INITIALIZER_UNLOCKED;

static void show(indicator_rgb_t c)
{
    led_strip_set_pixel(s.led, 0, c.r, c.g, c.b);
    led_strip_refresh(s.led);
}

static void indicator_task(void *arg)
{
    indicator_rgb_t shown = {1, 1, 1};  // differs from every real colour's first frame
    bool alarm_was = false;
    uint8_t alarm_inputs_was = 0;
    int64_t alarm_start_us = 0;
    static morse_seg_t segs[MAX_SEGS];  // off the stack: this task is the only user
    size_t n_segs = 0;
    bool testing = false;
    int64_t test_start_us = 0;
    uint32_t test_len_ms = 0;
    // An event chirp or a Tones-page preview: never both at once, so they
    // share one player. Static: a tone_t is ~300 bytes, this task's stack
    // is small, and nothing else touches it.
    static tone_player_t player;
    static tone_t next;

    for (;;) {
        const espos_health_state_t health = espos_health_worst();
        espos_sk_ws_status_t sk = {0};
        espos_sk_ws_get_status(&sk);
        const indicator_state_t state =
            indicator_state((int)health, atomic_load(&s.sk_relevant), sk.connected);

        const indicator_override_t override = (indicator_override_t)atomic_load(&s.override);
        const indicator_rgb_t c = override != INDICATOR_OVERRIDE_NONE
                                       ? indicator_override_color(override, atomic_load(&s.brightness),
                                                                   (uint32_t)(esp_timer_get_time() / 1000))
                                       : indicator_color(state, atomic_load(&s.brightness));
        if (memcmp(&c, &shown, sizeof(c)) != 0) {
            show(c);
            shown = c;
        }

        // An input alarm (plan 10's buzzer follow-up) sounds whether or not
        // the buzzer is enabled for health alarms, and wins over one: it is
        // the alarm someone set up for a physical danger (a bilge float
        // switch), while a health alarm still shows on the LED.
        const uint8_t input_alarms = atomic_load(&s.input_mask) & atomic_load(&s.input_buzz_mask);
        const bool alarm = input_alarms != 0 || (state == INDICATOR_ALARM && atomic_load(&s.buzzer_enabled));

        // Priority, highest first (plan 19): the BOOT-button override (LED
        // only, checked above) > alarm > test tone > event chirp. A chirp is
        // skipped entirely, never queued, while the override or alarm is
        // active; one already playing is cut off if the alarm starts. Both
        // the override and a real alarm collapse to the same TONE_PRIORITY_HIGH
        // tier -- this device has no need to distinguish between them for
        // chirp gating.
        const tone_priority_t active_priority =
            override != INDICATOR_OVERRIDE_NONE || alarm ? TONE_PRIORITY_HIGH : TONE_PRIORITY_NONE;
        const bool suppress_chirp = !tone_priority_allowed(TONE_PRIORITY_CHIRP, active_priority);
        if (suppress_chirp) {
            atomic_store(&s.chirp_request, -1);
            atomic_store(&s.preview_requested, false);
            tone_player_stop(&player);
        }

        if (alarm) {
            testing = false;  // a real alarm takes over from a test
        } else if (!testing && !player.playing && atomic_exchange(&s.test_requested, false)) {
            espos_net_status_t net = {0};
            char text[16];
            espos_net_get_status(&net);
            indicator_alarm_text(net.up ? net.ip : NULL, text, sizeof(text));
            n_segs = morse_encode(text, segs, MAX_SEGS);
            test_start_us = esp_timer_get_time();
            test_len_ms = morse_duration_ms(segs, n_segs, MORSE_UNIT_MS);
            testing = true;
            ESP_LOGI(TAG, "buzzer test: \"%s\" at %u Hz", text, (unsigned)atomic_load(&s.freq_hz));
        }
        if (alarm && (!alarm_was || input_alarms != alarm_inputs_was)) {
            // Rebuilt when the alarm starts or the set of alarmed inputs
            // changes. For a health alarm the address is read when it starts,
            // so the message stays the same for the whole alarm.
            char text[INDICATOR_INPUT_ALARM_TEXT_MAX];
            if (input_alarms) {
                indicator_input_alarm_text(input_alarms, text, sizeof(text));
            } else {
                espos_net_status_t net = {0};
                espos_net_get_status(&net);
                indicator_alarm_text(net.up ? net.ip : NULL, text, sizeof(text));
            }
            n_segs = morse_encode(text, segs, MAX_SEGS);
            alarm_start_us = esp_timer_get_time();
            ESP_LOGW(TAG, "alarm: buzzing \"%s\"", text);
        }
        alarm_was = alarm;
        alarm_inputs_was = input_alarms;

        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (!suppress_chirp && !testing && !player.playing) {
            const int req = atomic_exchange(&s.chirp_request, -1);
            if (req >= 0) {
                taskENTER_CRITICAL(&s_tone_mux);
                const bool have = (size_t)req < s.n_tones;
                if (have) {
                    next = s.tones[req];
                }
                taskEXIT_CRITICAL(&s_tone_mux);
                if (have && tone_player_start(&player, &next, now_ms)) {
                    ESP_LOGI(TAG, "chirp: \"%s\"", next.name);
                }
            }
        }

        // Tones page "Play" button: same one-shot mechanism as a chirp, but
        // the tone comes straight from the request, not a library lookup.
        if (!suppress_chirp && !testing && !player.playing && atomic_exchange(&s.preview_requested, false)) {
            taskENTER_CRITICAL(&s_tone_mux);
            next = s.preview;
            taskEXIT_CRITICAL(&s_tone_mux);
            if (tone_player_start(&player, &next, now_ms)) {
                ESP_LOGI(TAG, "preview: \"%s\"", next.name);
            }
        }

        bool tone = false;
        // Frequency changes are applied here, between frames, never in the
        // middle of another task's call: the config's buzzer_freq_hz unless
        // a chirp note (which carries its own pitch) is sounding right now.
        uint16_t want_freq = atomic_load(&s.freq_hz);
        if (alarm) {
            const uint32_t t_ms = (uint32_t)((esp_timer_get_time() - alarm_start_us) / 1000);
            tone = morse_tone_at(segs, n_segs, MORSE_UNIT_MS, MORSE_PAUSE_MS, t_ms);
        } else if (testing) {
            const uint32_t t_ms = (uint32_t)((esp_timer_get_time() - test_start_us) / 1000);
            if (t_ms >= test_len_ms) {
                testing = false;  // one pass only
            } else {
                tone = morse_tone_at(segs, n_segs, MORSE_UNIT_MS, 0, t_ms);
            }
        } else {
            tone = tone_player_step(&player, now_ms, &want_freq);  // one pass only
        }
        tone_buzzer_set_freq(&s.buzzer, want_freq);
        atomic_store(&s.busy, alarm || testing || player.playing);
        tone_buzzer_set(&s.buzzer, tone);
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}

void indicator_update_config(const device_config_t *cfg)
{
    atomic_store(&s.brightness, cfg->led_brightness);
    atomic_store(&s.buzzer_enabled, cfg->buzzer_on_alarm);
    atomic_store(&s.sk_relevant, cfg->publish_switches_tree || cfg->publish_controls_tree);
    atomic_store(&s.freq_hz, cfg->buzzer_freq_hz);
    atomic_store(&s.event_enabled, cfg->buzzer_on_event);
    uint8_t buzz = 0;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        if (cfg->inputs[i].alarm != INPUT_ALARM_OFF && cfg->inputs[i].alarm_buzz) {
            buzz |= (uint8_t)(1u << i);
        }
    }
    atomic_store(&s.input_buzz_mask, buzz);

    // `tone_t` is too big (RTTTL_MAX_NOTES notes each) for a whole
    // array of them to be a stack local -- indicator_update_config() runs on
    // the I/O task's small stack -- so the parse buffer is static instead.
    // Safe without its own lock: this function is only ever called from one
    // task at a time (main_task once at boot, then only the I/O task).
    static tone_t tones[TONE_MAX_TONES];
    const size_t n = tone_library_parse(cfg->tone_patterns, tones, TONE_MAX_TONES);
    taskENTER_CRITICAL(&s_tone_mux);
    memcpy(s.tones, tones, n * sizeof(tones[0]));
    s.n_tones = n;
    taskEXIT_CRITICAL(&s_tone_mux);

    atomic_store(&s.boot_idx, tone_library_find(tones, n, cfg->boot_tone));
    atomic_store(&s.portal_idx, tone_library_find(tones, n, cfg->portal_tone));
    atomic_store(&s.reset_idx, tone_library_find(tones, n, cfg->factory_reset_tone));
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        atomic_store(&s.relay_on_idx[i], tone_library_find(tones, n, cfg->relays[i].on_tone));
        atomic_store(&s.relay_off_idx[i], tone_library_find(tones, n, cfg->relays[i].off_tone));
        atomic_store(&s.pulse_start_idx[i], tone_library_find(tones, n, cfg->relays[i].pulse_start_tone));
        atomic_store(&s.pulse_stop_idx[i], tone_library_find(tones, n, cfg->relays[i].pulse_stop_tone));
        atomic_store(&s.input_on_idx[i], tone_library_find(tones, n, cfg->inputs[i].on_tone));
        atomic_store(&s.input_off_idx[i], tone_library_find(tones, n, cfg->inputs[i].off_tone));
    }
}

void indicator_set_inputs(uint8_t mask)
{
    atomic_store(&s.input_mask, mask);
}

void indicator_set_override(indicator_override_t override)
{
    atomic_store(&s.override, (int)override);
}

static void request_chirp(int idx)
{
    if (idx < 0 || !atomic_load(&s.started) || !atomic_load(&s.event_enabled)) {
        return;
    }
    atomic_store(&s.chirp_request, idx);
}

// Length of tones[idx] in ms, or 0 if request_chirp(idx) would drop it.
static uint32_t chirp_duration_ms(int idx)
{
    if (idx < 0 || !atomic_load(&s.started) || !atomic_load(&s.event_enabled)) {
        return 0;
    }
    uint32_t ms = 0;
    taskENTER_CRITICAL(&s_tone_mux);
    if ((size_t)idx < s.n_tones) {
        ms = rtttl_duration_ms(s.tones[idx].notes, s.tones[idx].n_notes);
    }
    taskEXIT_CRITICAL(&s_tone_mux);
    return ms;
}

// Logged unconditionally, the instant each entry point below is called --
// distinct from the "chirp:"/"preview:"/"buzzer test:" lines in
// indicator_task() above, which only fire once a request actually starts
// sounding. A call can be silently dropped downstream (buzzer_on_event off,
// no tone assigned to that slot, another chirp/alarm/test already has the
// buzzer), which is correct behavior, but that would otherwise look
// identical to "nothing called indicator at all" from the serial log alone.
static const char *event_name(indicator_event_t event)
{
    switch (event) {
    case INDICATOR_EVENT_BOOT: return "boot";
    case INDICATOR_EVENT_PORTAL: return "portal";
    case INDICATOR_EVENT_FACTORY_RESET: return "factory-reset";
    }
    return "?";
}

uint32_t indicator_play_event(indicator_event_t event)
{
    ESP_LOGI(TAG, "indicator_play_event(%s)", event_name(event));
    int idx = -1;
    switch (event) {
    case INDICATOR_EVENT_BOOT: idx = atomic_load(&s.boot_idx); break;
    case INDICATOR_EVENT_PORTAL: idx = atomic_load(&s.portal_idx); break;
    case INDICATOR_EVENT_FACTORY_RESET: idx = atomic_load(&s.reset_idx); break;
    }
    request_chirp(idx);
    return chirp_duration_ms(idx);
}

void indicator_play_relay_tone(uint8_t channel, bool on)
{
    ESP_LOGI(TAG, "indicator_play_relay_tone(%u, %s)", channel, on ? "on" : "off");
    if (channel < 1 || channel > BOARD_CHANNELS) {
        return;
    }
    request_chirp(atomic_load(on ? &s.relay_on_idx[channel - 1] : &s.relay_off_idx[channel - 1]));
}

void indicator_play_relay_pulse_tone(uint8_t channel, bool start)
{
    ESP_LOGI(TAG, "indicator_play_relay_pulse_tone(%u, %s)", channel, start ? "start" : "stop");
    if (channel < 1 || channel > BOARD_CHANNELS) {
        return;
    }
    request_chirp(atomic_load(start ? &s.pulse_start_idx[channel - 1] : &s.pulse_stop_idx[channel - 1]));
}

void indicator_play_input_tone(uint8_t channel, bool on)
{
    ESP_LOGI(TAG, "indicator_play_input_tone(%u, %s)", channel, on ? "on" : "off");
    if (channel < 1 || channel > BOARD_CHANNELS) {
        return;
    }
    request_chirp(atomic_load(on ? &s.input_on_idx[channel - 1] : &s.input_off_idx[channel - 1]));
}

esp_err_t indicator_test_buzzer(void)
{
    ESP_LOGI(TAG, "indicator_test_buzzer()");
    if (!atomic_load(&s.started) || atomic_load(&s.busy) || atomic_load(&s.test_requested)) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&s.test_requested, true);
    return ESP_OK;
}

esp_err_t indicator_play_rtttl(const char *rtttl)
{
    ESP_LOGI(TAG, "indicator_play_rtttl(\"%s\")", rtttl ? rtttl : "");
    tone_t tone = {0};
    tone.n_notes = rtttl_parse(rtttl, tone.notes, RTTTL_MAX_NOTES);
    if (tone.n_notes == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!atomic_load(&s.started) || atomic_load(&s.busy) || atomic_load(&s.preview_requested)) {
        return ESP_ERR_INVALID_STATE;
    }
    taskENTER_CRITICAL(&s_tone_mux);
    s.preview = tone;
    taskEXIT_CRITICAL(&s_tone_mux);
    atomic_store(&s.preview_requested, true);
    return ESP_OK;
}

esp_err_t indicator_start(const device_config_t *cfg)
{
    atomic_store(&s.chirp_request, -1);  // BSS zero-inits to 0, a valid tone index
    indicator_update_config(cfg);

    const led_strip_config_t strip = {
        .strip_gpio_num = BOARD_RGB_LED,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
    };
    const led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip, &rmt, &s.led);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LED: %s", esp_err_to_name(err));
        return err;
    }

    const tone_buzzer_config_t buzzer = {
        .gpio = BOARD_BUZZER,
        .timer = BUZZER_TIMER,
        .channel = BUZZER_CHANNEL,
        .freq_hz = atomic_load(&s.freq_hz),
    };
    err = tone_buzzer_init(&s.buzzer, &buzzer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "buzzer: %s", esp_err_to_name(err));
        return err;
    }

    // Lowest priority of the firmware's tasks: this only shows state. 3072
    // was too tight once event chirps (plan 19) added two tone_t locals
    // (~300 bytes each) on top of the Morse segment buffer -- caused a real
    // stack overflow (TG1WDT-style corruption, caught on hardware playing a
    // relay chirp). Those buffers are static now; 4096 is kept as margin.
    if (xTaskCreate(indicator_task, "indicator", 4096, NULL, 2, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    atomic_store(&s.started, true);
    return ESP_OK;
}

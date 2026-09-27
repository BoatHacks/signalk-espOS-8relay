#include "indicator.h"

#include <stdatomic.h>
#include <string.h>

#include "board.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "espos_health.h"
#include "espos_net.h"
#include "espos_sk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "indicator_logic.h"
#include "led_strip.h"

static const char *TAG = "indicator";

#define LOOP_MS 10
#define MORSE_UNIT_MS 80  // about 15 words per minute
#define MORSE_PAUSE_MS 5000
#define MAX_SEGS 64

#define BUZZER_TIMER LEDC_TIMER_0
#define BUZZER_CHANNEL LEDC_CHANNEL_0
#define BUZZER_RES LEDC_TIMER_10_BIT
#define BUZZER_DUTY_ON 512  // 50 %

static struct {
    led_strip_handle_t led;
    atomic_uchar brightness;
    atomic_bool buzzer_enabled;
    atomic_bool sk_relevant;
    atomic_ushort freq_hz;       // buzzer_freq_hz; applied by the task
    atomic_bool test_requested;  // indicator_test_buzzer() -> task
    atomic_bool busy;            // an alarm, a test or a chirp is sounding
    atomic_bool started;
    atomic_int override;         // indicator_override_t; NONE = 0, show status as usual

    // Event chirps (plan 19, issue #14). `tones`/`n_tones` are written by
    // indicator_update_config() (the I/O task) and read by indicator_task();
    // s_tone_mux protects both. Everything else here is a plain index,
    // updated the same way but small enough to be a single atomic.
    atomic_bool event_enabled;  // buzzer_on_event
    indicator_tone_t tones[INDICATOR_MAX_TONES];
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
    indicator_tone_t preview;
    atomic_bool preview_requested;
} s;

static portMUX_TYPE s_tone_mux = portMUX_INITIALIZER_UNLOCKED;

static void set_tone(bool on)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, on ? BUZZER_DUTY_ON : 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL);
}

static void show(indicator_rgb_t c)
{
    led_strip_set_pixel(s.led, 0, c.r, c.g, c.b);
    led_strip_refresh(s.led);
}

static void indicator_task(void *arg)
{
    indicator_rgb_t shown = {1, 1, 1};  // differs from every real colour's first frame
    bool alarm_was = false;
    bool tone_was = false;
    int64_t alarm_start_us = 0;
    morse_seg_t segs[MAX_SEGS];
    size_t n_segs = 0;
    bool testing = false;
    int64_t test_start_us = 0;
    uint32_t test_len_ms = 0;
    uint16_t applied_freq = atomic_load(&s.freq_hz);
    bool chirping = false;
    int64_t chirp_start_us = 0;
    uint32_t chirp_len_ms = 0;
    indicator_tone_t current_chirp = {0};
    bool previewing = false;
    int64_t preview_start_us = 0;
    uint32_t preview_len_ms = 0;
    indicator_tone_t current_preview = {0};

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

        const bool alarm = state == INDICATOR_ALARM && atomic_load(&s.buzzer_enabled);

        // Priority, highest first (plan 19): the BOOT-button override (LED
        // only, checked above) > alarm > test tone > event chirp. A chirp is
        // skipped entirely, never queued, while the override or alarm is
        // active; one already playing is cut off if the alarm starts.
        const bool suppress_chirp = !indicator_chirp_allowed(override, alarm);
        if (suppress_chirp) {
            atomic_store(&s.chirp_request, -1);
            chirping = false;
            atomic_store(&s.preview_requested, false);
            previewing = false;
        }

        if (alarm) {
            testing = false;  // a real alarm takes over from a test
        } else if (!testing && !chirping && !previewing && atomic_exchange(&s.test_requested, false)) {
            espos_net_status_t net = {0};
            char text[16];
            espos_net_get_status(&net);
            indicator_alarm_text(net.up ? net.ip : NULL, text, sizeof(text));
            n_segs = morse_encode(text, segs, MAX_SEGS);
            test_start_us = esp_timer_get_time();
            test_len_ms = morse_duration_ms(segs, n_segs, MORSE_UNIT_MS);
            testing = true;
            ESP_LOGI(TAG, "buzzer test: \"%s\" at %u Hz", text, (unsigned)applied_freq);
        }
        if (alarm && !alarm_was) {
            // The address is read when the alarm starts, so the message stays
            // the same for the whole alarm.
            espos_net_status_t net = {0};
            char text[16];
            espos_net_get_status(&net);
            indicator_alarm_text(net.up ? net.ip : NULL, text, sizeof(text));
            n_segs = morse_encode(text, segs, MAX_SEGS);
            alarm_start_us = esp_timer_get_time();
            ESP_LOGW(TAG, "alarm: buzzing \"%s\"", text);
        }
        alarm_was = alarm;

        if (!suppress_chirp && !testing && !chirping && !previewing) {
            const int req = atomic_exchange(&s.chirp_request, -1);
            if (req >= 0) {
                taskENTER_CRITICAL(&s_tone_mux);
                const bool have = (size_t)req < s.n_tones;
                if (have) {
                    current_chirp = s.tones[req];
                }
                taskEXIT_CRITICAL(&s_tone_mux);
                if (have && current_chirp.n_notes > 0) {
                    chirping = true;
                    chirp_start_us = esp_timer_get_time();
                    chirp_len_ms = rtttl_duration_ms(current_chirp.notes, current_chirp.n_notes);
                    ESP_LOGI(TAG, "chirp: \"%s\"", current_chirp.name);
                }
            }
        }

        // Tones page "Play" button: same one-shot mechanism as a chirp, but
        // the tone comes straight from the request, not a library lookup.
        if (!suppress_chirp && !testing && !chirping && !previewing &&
            atomic_exchange(&s.preview_requested, false)) {
            taskENTER_CRITICAL(&s_tone_mux);
            current_preview = s.preview;
            taskEXIT_CRITICAL(&s_tone_mux);
            if (current_preview.n_notes > 0) {
                previewing = true;
                preview_start_us = esp_timer_get_time();
                preview_len_ms = rtttl_duration_ms(current_preview.notes, current_preview.n_notes);
                ESP_LOGI(TAG, "preview: \"%s\"", current_preview.name);
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
        } else if (chirping) {
            const uint32_t t_ms = (uint32_t)((esp_timer_get_time() - chirp_start_us) / 1000);
            if (t_ms >= chirp_len_ms) {
                chirping = false;  // one pass only
            } else {
                uint16_t note_freq = 0;
                if (rtttl_tone_at(current_chirp.notes, current_chirp.n_notes, t_ms, &note_freq)) {
                    tone = true;
                    want_freq = note_freq;
                }
            }
        } else if (previewing) {
            const uint32_t t_ms = (uint32_t)((esp_timer_get_time() - preview_start_us) / 1000);
            if (t_ms >= preview_len_ms) {
                previewing = false;  // one pass only
            } else {
                uint16_t note_freq = 0;
                if (rtttl_tone_at(current_preview.notes, current_preview.n_notes, t_ms, &note_freq)) {
                    tone = true;
                    want_freq = note_freq;
                }
            }
        }
        if (want_freq != applied_freq && want_freq != 0 &&
            ledc_set_freq(LEDC_LOW_SPEED_MODE, BUZZER_TIMER, want_freq) == ESP_OK) {
            applied_freq = want_freq;
        }
        atomic_store(&s.busy, alarm || testing || chirping || previewing);
        if (tone != tone_was) {
            set_tone(tone);
            tone_was = tone;
        }
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

    // `indicator_tone_t` is too big (RTTTL_MAX_NOTES notes each) for a whole
    // array of them to be a stack local -- indicator_update_config() runs on
    // the I/O task's small stack -- so the parse buffer is static instead.
    // Safe without its own lock: this function is only ever called from one
    // task at a time (main_task once at boot, then only the I/O task).
    static indicator_tone_t tones[INDICATOR_MAX_TONES];
    const size_t n = indicator_parse_tones(cfg->tone_patterns, tones, INDICATOR_MAX_TONES);
    taskENTER_CRITICAL(&s_tone_mux);
    memcpy(s.tones, tones, n * sizeof(tones[0]));
    s.n_tones = n;
    taskEXIT_CRITICAL(&s_tone_mux);

    atomic_store(&s.boot_idx, indicator_find_tone(tones, n, cfg->boot_tone));
    atomic_store(&s.portal_idx, indicator_find_tone(tones, n, cfg->portal_tone));
    atomic_store(&s.reset_idx, indicator_find_tone(tones, n, cfg->factory_reset_tone));
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        atomic_store(&s.relay_on_idx[i], indicator_find_tone(tones, n, cfg->relays[i].on_tone));
        atomic_store(&s.relay_off_idx[i], indicator_find_tone(tones, n, cfg->relays[i].off_tone));
        atomic_store(&s.pulse_start_idx[i], indicator_find_tone(tones, n, cfg->relays[i].pulse_start_tone));
        atomic_store(&s.pulse_stop_idx[i], indicator_find_tone(tones, n, cfg->relays[i].pulse_stop_tone));
        atomic_store(&s.input_on_idx[i], indicator_find_tone(tones, n, cfg->inputs[i].on_tone));
        atomic_store(&s.input_off_idx[i], indicator_find_tone(tones, n, cfg->inputs[i].off_tone));
    }
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

void indicator_play_event(indicator_event_t event)
{
    switch (event) {
    case INDICATOR_EVENT_BOOT: request_chirp(atomic_load(&s.boot_idx)); break;
    case INDICATOR_EVENT_PORTAL: request_chirp(atomic_load(&s.portal_idx)); break;
    case INDICATOR_EVENT_FACTORY_RESET: request_chirp(atomic_load(&s.reset_idx)); break;
    }
}

void indicator_play_relay_tone(uint8_t channel, bool on)
{
    if (channel < 1 || channel > BOARD_CHANNELS) {
        return;
    }
    request_chirp(atomic_load(on ? &s.relay_on_idx[channel - 1] : &s.relay_off_idx[channel - 1]));
}

void indicator_play_relay_pulse_tone(uint8_t channel, bool start)
{
    if (channel < 1 || channel > BOARD_CHANNELS) {
        return;
    }
    request_chirp(atomic_load(start ? &s.pulse_start_idx[channel - 1] : &s.pulse_stop_idx[channel - 1]));
}

void indicator_play_input_tone(uint8_t channel, bool on)
{
    if (channel < 1 || channel > BOARD_CHANNELS) {
        return;
    }
    request_chirp(atomic_load(on ? &s.input_on_idx[channel - 1] : &s.input_off_idx[channel - 1]));
}

esp_err_t indicator_test_buzzer(void)
{
    if (!atomic_load(&s.started) || atomic_load(&s.busy) || atomic_load(&s.test_requested)) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&s.test_requested, true);
    return ESP_OK;
}

esp_err_t indicator_play_rtttl(const char *rtttl)
{
    indicator_tone_t tone = {0};
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

    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BUZZER_RES,
        .timer_num = BUZZER_TIMER,
        .freq_hz = atomic_load(&s.freq_hz),
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel = {
        .gpio_num = BOARD_BUZZER,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BUZZER_CHANNEL,
        .timer_sel = BUZZER_TIMER,
        .duty = 0,
    };
    err = ledc_timer_config(&timer);
    if (err == ESP_OK) {
        err = ledc_channel_config(&channel);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "buzzer: %s", esp_err_to_name(err));
        return err;
    }

    // Lowest priority of the firmware's tasks: this only shows state. 3072
    // was too tight once event chirps (plan 19) added two indicator_tone_t
    // locals (current_chirp/current_preview, ~300 bytes each) on top of the
    // existing Morse segment buffer -- caused a real stack overflow
    // (TG1WDT-style corruption, caught on hardware playing a relay chirp).
    if (xTaskCreate(indicator_task, "indicator", 4096, NULL, 2, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    atomic_store(&s.started, true);
    return ESP_OK;
}

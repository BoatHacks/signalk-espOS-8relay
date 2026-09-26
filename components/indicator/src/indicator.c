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
    atomic_bool busy;            // an alarm or a test is sounding
    atomic_bool started;
    atomic_int override;         // indicator_override_t; NONE = 0, show status as usual
} s;

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

        // A frequency change is applied here, between frames, never in the
        // middle of another task's call.
        const uint16_t freq = atomic_load(&s.freq_hz);
        if (freq != applied_freq && ledc_set_freq(LEDC_LOW_SPEED_MODE, BUZZER_TIMER, freq) == ESP_OK) {
            applied_freq = freq;
        }

        const bool alarm = state == INDICATOR_ALARM && atomic_load(&s.buzzer_enabled);
        if (alarm) {
            testing = false;  // a real alarm takes over from a test
        } else if (!testing && atomic_exchange(&s.test_requested, false)) {
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
        bool tone = false;
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
        }
        atomic_store(&s.busy, alarm || testing);
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
}

void indicator_set_override(indicator_override_t override)
{
    atomic_store(&s.override, (int)override);
}

esp_err_t indicator_test_buzzer(void)
{
    if (!atomic_load(&s.started) || atomic_load(&s.busy) || atomic_load(&s.test_requested)) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&s.test_requested, true);
    return ESP_OK;
}

esp_err_t indicator_start(const device_config_t *cfg)
{
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

    // Lowest priority of the firmware's tasks: this only shows state.
    if (xTaskCreate(indicator_task, "indicator", 3072, NULL, 2, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    atomic_store(&s.started, true);
    return ESP_OK;
}

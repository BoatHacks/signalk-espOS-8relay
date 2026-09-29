#include "tone_buzzer.h"

#define BUZZER_MODE LEDC_LOW_SPEED_MODE
#define BUZZER_RES LEDC_TIMER_10_BIT
#define BUZZER_DUTY_ON 512  // 50 %

esp_err_t tone_buzzer_init(tone_buzzer_t *b, const tone_buzzer_config_t *cfg)
{
    b->cfg = *cfg;
    b->applied_hz = cfg->freq_hz;
    b->on = false;
    const ledc_timer_config_t timer = {
        .speed_mode = BUZZER_MODE,
        .duty_resolution = BUZZER_RES,
        .timer_num = cfg->timer,
        .freq_hz = cfg->freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel = {
        .gpio_num = cfg->gpio,
        .speed_mode = BUZZER_MODE,
        .channel = cfg->channel,
        .timer_sel = cfg->timer,
        .duty = 0,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err == ESP_OK) {
        err = ledc_channel_config(&channel);
    }
    return err;
}

void tone_buzzer_set(tone_buzzer_t *b, bool on)
{
    if (on == b->on) {
        return;
    }
    ledc_set_duty(BUZZER_MODE, b->cfg.channel, on ? BUZZER_DUTY_ON : 0);
    ledc_update_duty(BUZZER_MODE, b->cfg.channel);
    b->on = on;
}

void tone_buzzer_set_freq(tone_buzzer_t *b, uint16_t freq_hz)
{
    if (freq_hz == 0 || freq_hz == b->applied_hz) {
        return;
    }
    if (ledc_set_freq(BUZZER_MODE, b->cfg.timer, freq_hz) == ESP_OK) {
        b->applied_hz = freq_hz;
    }
}

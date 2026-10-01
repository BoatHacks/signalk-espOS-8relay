// A passive buzzer on one LEDC channel (device builds only): square wave at
// 50 % duty, on or off, at a pitch that can change between notes. Pairs with
// tone_player.h. Not thread-safe: drive it from one task.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/ledc.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int gpio;
    ledc_timer_t timer;
    ledc_channel_t channel;
    uint16_t freq_hz;  // starting pitch
} tone_buzzer_config_t;

typedef struct {
    tone_buzzer_config_t cfg;
    uint16_t applied_hz;
    bool on;
} tone_buzzer_t;

// Configures the timer and channel (low-speed mode, 10-bit duty), silent.
esp_err_t tone_buzzer_init(tone_buzzer_t *b, const tone_buzzer_config_t *cfg);

// Sounds or silences the buzzer; a no-op when already in that state.
void tone_buzzer_set(tone_buzzer_t *b, bool on);

// Changes the pitch; a no-op for 0 or the pitch already applied. Left as it
// was if the timer can't produce `freq_hz`.
void tone_buzzer_set_freq(tone_buzzer_t *b, uint16_t freq_hz);

#ifdef __cplusplus
}
#endif

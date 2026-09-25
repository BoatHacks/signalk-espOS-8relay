// What the status LED and buzzer should do, without touching hardware.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Worst first: an alarm hides a warning, a warning hides a missing SignalK
// connection.
typedef enum {
    INDICATOR_OK,
    INDICATOR_NO_SIGNALK,
    INDICATOR_WARN,
    INDICATOR_ALARM,
} indicator_state_t;

typedef struct {
    uint8_t r, g, b;
} indicator_rgb_t;

// `health`: espOS's worst health state (0 normal, 1 warn, 2 alarm).
// `sk_relevant`: false when no SignalK tree is published, so a missing
// connection isn't worth showing.
indicator_state_t indicator_state(int health, bool sk_relevant, bool sk_connected);

// Green, blue, amber or red, scaled to `brightness_pct` (0 = off).
indicator_rgb_t indicator_color(indicator_state_t state, uint8_t brightness_pct);

// One stretch of tone or silence, in Morse units.
typedef struct {
    bool on;
    uint8_t units;
} morse_seg_t;

// Morse for A-Z, 0-9 and spaces (other characters are skipped): dot 1 unit,
// dash 3, gap inside a letter 1, between letters 3, between words 7.
// Returns the number of segments written (at most `max`).
size_t morse_encode(const char *text, morse_seg_t *out, size_t max);

// The alarm message: "ESP", then the last octet of `ip` ("ESP 42" for
// 192.168.1.42). Without a usable address, "ESP AP": join the device's
// setup access point.
void indicator_alarm_text(const char *ip, char *out, size_t size);

// Length of one pass of the message, without the pause.
uint32_t morse_duration_ms(const morse_seg_t *segs, size_t n, uint32_t unit_ms);

// Whether the tone is on at `t_ms` into a message that repeats with
// `pause_ms` of silence after each round.
bool morse_tone_at(const morse_seg_t *segs, size_t n, uint32_t unit_ms, uint32_t pause_ms, uint32_t t_ms);

#ifdef __cplusplus
}
#endif

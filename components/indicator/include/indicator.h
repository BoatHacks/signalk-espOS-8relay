// Status LED and alarm buzzer (device builds only). The LED shows espOS
// health and the SignalK connection; the buzzer, if enabled, sounds "ESP"
// and the last octet of the device's IP address in Morse while a health
// alarm is active, or "ESP AP" when it has no address.
#pragma once

#include "device_config.h"
#include "esp_err.h"
#include "indicator_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t indicator_start(const device_config_t *cfg);

// Event chirps (plan 19, issue #14): a short one-shot RTTTL tone, gated by
// `buzzer_on_event` and skipped entirely (never queued) while the BOOT
// button's override or a real alarm is active/starts. Callable from any
// task; no-op before indicator_start().
typedef enum {
    INDICATOR_EVENT_BOOT,
    INDICATOR_EVENT_PORTAL,
    INDICATOR_EVENT_FACTORY_RESET,
} indicator_event_t;

// Returns the requested tone's length in ms, or 0 if none will be requested
// (buzzer_on_event off, no tone assigned, or before indicator_start()). A
// non-zero length doesn't promise the tone is heard: it can still be skipped
// or cut off by the override, an alarm, or another chirp already playing.
uint32_t indicator_play_event(indicator_event_t event);
void indicator_play_relay_tone(uint8_t channel, bool on);
void indicator_play_input_tone(uint8_t channel, bool on);

// Momentary relays only: a pulse starting or ending, using the relay's
// pulse_start_tone/pulse_stop_tone instead of its on_tone/off_tone.
void indicator_play_relay_pulse_tone(uint8_t channel, bool start);

// Every input's settled state (bit 0 = input 1), from input_sense's
// listener. While an input whose alarm is set to buzz (`input<n>_alm_buzz`)
// reads on, the buzzer sounds "IN <n>" in Morse, whether or not the buzzer
// is enabled for health alarms. Callable from any task.
void indicator_set_inputs(uint8_t mask);

// The BOOT button (plan 14) drives this while held: NONE shows espOS
// health/SignalK status as usual, anything else takes over the LED (not the
// buzzer) until set back to NONE. Callable from any task.
void indicator_set_override(indicator_override_t override);

// Brightness, buzzer switch and frequency, tree toggles; callable from any
// task.
void indicator_update_config(const device_config_t *cfg);

// Play the alarm pattern once, whether or not the buzzer is enabled for
// alarms. ESP_ERR_INVALID_STATE while an alarm or another test is sounding,
// or before indicator_start().
esp_err_t indicator_test_buzzer(void);

// Play an arbitrary RTTTL string once (the Tones page's per-tone Play
// button, for previewing an entry before saving it) -- like
// indicator_test_buzzer(), this ignores buzzer_on_event and the priority
// rules only apply the usual way (skipped/cut off by the override or an
// alarm). ESP_ERR_INVALID_ARG if `rtttl` has no playable notes.
// ESP_ERR_INVALID_STATE while the buzzer is already busy, or before
// indicator_start().
esp_err_t indicator_play_rtttl(const char *rtttl);

#ifdef __cplusplus
}
#endif

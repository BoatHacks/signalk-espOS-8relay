// The firmware's settings (SPEC.md §4, §9), read from espOS's config store
// (namespace "swbank", config/swbank.json) into one plain struct. Other
// components read that struct, never the store.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_CONFIG_NS "swbank"
#define DEVICE_CONFIG_NAME_MAX 32
// A tone name from the `tone_patterns` library ("" = no tone/off).
#define DEVICE_CONFIG_TONE_NAME_MAX 24
// `tone_patterns` itself: a JSON array of {"name","rtttl"} rows, stored as
// espOS's table-format string (plan 19, issue #14). Matches the config
// schema's maxLength, espOS's own NVS string ceiling.
#define DEVICE_CONFIG_TONE_TABLE_MAX 3999

typedef enum { RELAY_MODE_LATCHING, RELAY_MODE_MOMENTARY } relay_mode_t;
typedef enum { FAILSAFE_DEFAULT_SAFE, FAILSAFE_HOLD } failsafe_policy_t;
// How a relay reacts to its override input: copy it, or flip on each press.
typedef enum { INPUT_LINK_FOLLOW, INPUT_LINK_TOGGLE } input_link_t;

typedef struct {
    char name[DEVICE_CONFIG_NAME_MAX + 1];
    relay_mode_t mode;
    uint32_t pulse_ms;
    // As stored. Use device_config_effective_failsafe() to act on it.
    failsafe_policy_t failsafe;
    uint8_t override_di;  // 0 = none, else input channel 1-8
    input_link_t link;
    uint32_t max_on_s;    // 0 = no limit; latching relays only
    // Tone library entry to chirp on a direct-command on/off (plan 19).
    char on_tone[DEVICE_CONFIG_TONE_NAME_MAX + 1];
    char off_tone[DEVICE_CONFIG_TONE_NAME_MAX + 1];
} relay_cfg_t;

typedef struct {
    char name[DEVICE_CONFIG_NAME_MAX + 1];
    bool invert;
    // Tone library entry to chirp on a debounced level change (plan 19).
    char tone[DEVICE_CONFIG_TONE_NAME_MAX + 1];
} input_cfg_t;

typedef struct {
    uint8_t bank_id;
    uint8_t input_bank_id;
    uint16_t debounce_ms;
    uint16_t sk_loss_grace_s;
    uint16_t sk_republish_s;  // 0 = changes only
    bool eth_enabled;
    bool publish_switches_tree;
    bool publish_controls_tree;
    uint8_t led_brightness;  // percent, 0 = off
    bool buzzer_on_alarm;
    uint16_t buzzer_freq_hz;
    // Event chirps (plan 19, issue #14): a separate master switch from the
    // alarm, a named RTTTL tone library, and which entry (if any, "" = off)
    // plays for each boot-family event.
    bool buzzer_on_event;
    char tone_patterns[DEVICE_CONFIG_TONE_TABLE_MAX + 1];
    char boot_tone[DEVICE_CONFIG_TONE_NAME_MAX + 1];
    char portal_tone[DEVICE_CONFIG_TONE_NAME_MAX + 1];
    char factory_reset_tone[DEVICE_CONFIG_TONE_NAME_MAX + 1];
    relay_cfg_t relays[BOARD_CHANNELS];  // index 0 = relay 1
    input_cfg_t inputs[BOARD_CHANNELS];  // index 0 = input 1
} device_config_t;

// Read every setting. Missing or invalid stored values read as their
// defaults (espOS guarantees this), so this only fails before the store is
// initialised.
esp_err_t device_config_load(device_config_t *out);

// Momentary relays always behave as default-safe, whatever is stored
// (SPEC.md §2).
failsafe_policy_t device_config_effective_failsafe(const relay_cfg_t *relay);

// False when the relay and input banks share an id. espOS can't reject such
// a save, so the firmware stops publishing the input bank instead.
bool device_config_input_bank_usable(const device_config_t *cfg);

// Raise or clear the espOS health warning for a clashing input bank id.
void device_config_report_health(const device_config_t *cfg);

#ifdef __cplusplus
}
#endif

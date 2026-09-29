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

// Where a schedule's "sunrise"/"sunset" entries get the boat's position from
// (issue #9, plan 16). Either way, a stale or missing live reading falls
// back to fallback_lat/fallback_lon below -- "stale" is the schedule
// evaluator's call (it owns the clock), not device_config's.
typedef enum { POSITION_SRC_SIGNALK, POSITION_SRC_N2K } position_source_t;

// A schedule entry (issue #9, plan 16): either an on-time/off-time pair
// (SCHEDULE_MODE_CLOCK) or a duty cycle (SCHEDULE_MODE_REPEAT, "on for X
// min every Y min" -- a fan, say). Which of the two union-like halves below
// is meaningful follows `mode`; the other is simply unused, the same shape
// device_config already uses for e.g. `relay_cfg_t.max_on_s` being ignored
// in momentary mode.
typedef enum { SCHEDULE_MODE_CLOCK, SCHEDULE_MODE_REPEAT } schedule_mode_t;

// A CLOCK-mode on-time or off-time: a fixed time of day, or a sunrise/sunset
// offset (issue #9, plan 16). `offset_min` is signed, e.g. -30 for
// "sunrise-30m" (half an hour before sunrise).
typedef enum { SCHEDULE_TIME_CLOCK, SCHEDULE_TIME_SUNRISE, SCHEDULE_TIME_SUNSET } schedule_time_kind_t;
typedef struct {
    schedule_time_kind_t kind;
    int16_t minute_of_day; // SCHEDULE_TIME_CLOCK only, 0-1439
    int16_t offset_min;    // SCHEDULE_TIME_SUNRISE/SUNSET only
} schedule_time_t;

#define SCHEDULE_MAX_ENTRIES 8

typedef struct {
    // 0 = unused: an entry with nothing configured, one whose on/off string
    // didn't parse, or one that lost a same-relay conflict below -- all the
    // same "does nothing" outcome to the evaluator, whatever the reason.
    uint8_t relay;
    schedule_mode_t mode;
    schedule_time_t on;  // SCHEDULE_MODE_CLOCK
    schedule_time_t off; // SCHEDULE_MODE_CLOCK
    uint16_t on_min;     // SCHEDULE_MODE_REPEAT: on-duration, minutes
    uint16_t period_min; // SCHEDULE_MODE_REPEAT: cycle length, minutes
    // Bit 0 = Sunday .. bit 6 = Saturday, matching espos_time_parts_t.wday's
    // own convention -- both modes use this, a repeat-mode fan can still be
    // "weekdays only".
    uint8_t days;
} schedule_cfg_t;

typedef struct {
    char name[DEVICE_CONFIG_NAME_MAX + 1];
    relay_mode_t mode;
    uint32_t pulse_ms;
    // As stored. Use device_config_effective_failsafe() to act on it.
    failsafe_policy_t failsafe;
    uint8_t override_di;  // 0 = none, else input channel 1-8
    // The load sits on the NC terminal, not NO: relay_ctrl's public
    // get/set/PUT surface reports and commands the load's state (coil
    // inverted), while every fail-safe/boot/momentary/hold path keeps
    // driving the coil directly, unaffected by this.
    bool wired_nc;
    input_link_t link;
    uint32_t max_on_s;    // 0 = no limit; latching relays only
    // Effective, already-validated interlock partner (issue #8, plan 15):
    // 1-8 = that relay's *coil* must never be energized at the same time as
    // this one's, 0 = none. This is the derived pair, not the raw
    // `r<n>_interlock` setting -- device_config_load() zeroes it out again
    // (and flags device_config_t.interlock_invalid) unless the partner
    // names this relay back and neither side is self-referencing. Operates
    // on the coil, never the `wired_nc`-translated logical state (decided
    // 2026-09-28, predates plan 15's original writing): relay_ctrl must
    // read and enforce this the same way it reads wired_nc, at the coil
    // boundary.
    uint8_t interlock;
    // Tone library entry to chirp on a direct-command on/off (plan 19).
    char on_tone[DEVICE_CONFIG_TONE_NAME_MAX + 1];
    char off_tone[DEVICE_CONFIG_TONE_NAME_MAX + 1];
    // Momentary mode only: used instead of on_tone/off_tone for a pulse
    // starting (direct on-command) or ending (its timer, or a direct
    // command cutting it short) -- both chirp, unlike a latching relay's
    // off_tone, which only fires for a direct command.
    char pulse_start_tone[DEVICE_CONFIG_TONE_NAME_MAX + 1];
    char pulse_stop_tone[DEVICE_CONFIG_TONE_NAME_MAX + 1];
} relay_cfg_t;

// SignalK notification severity for an input alarm (plan 10, issue #3).
// Order matches the "off"/"warn"/"alarm"/"emergency" setting strings.
typedef enum { INPUT_ALARM_OFF, INPUT_ALARM_WARN, INPUT_ALARM_ALARM, INPUT_ALARM_EMERGENCY } input_alarm_t;

typedef struct {
    char name[DEVICE_CONFIG_NAME_MAX + 1];
    bool invert;
    // Tone library entry to chirp on a debounced level change (plan 19).
    char on_tone[DEVICE_CONFIG_TONE_NAME_MAX + 1];
    char off_tone[DEVICE_CONFIG_TONE_NAME_MAX + 1];
    // SignalK notification alarm (plan 10, issue #3): off by default. Raised
    // when the input reads on, cleared when it reads off.
    input_alarm_t alarm;
    // Empty = "<name> active".
    char alarm_msg[DEVICE_CONFIG_NAME_MAX + 1];
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
    // Input alarms also raised as NMEA 2000 alerts, PGN 126983/126985
    // (plan 21). Applies live.
    bool n2k_alerts;
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
    // Dead time between a partner's coil going off and an interlocked
    // relay's coil going on (issue #8, plan 15). Stored as "interlock_dead"
    // (espOS key names cap at 15 chars).
    uint32_t interlock_dead_ms;
    // Schedules (issue #9, plan 16): where sunrise/sunset entries get the
    // boat's live position from, and the position to use when that source
    // has none (not subscribed/received yet) or a stale one. Degrees,
    // positive north/east -- SignalK's navigation.position convention.
    position_source_t position_source;
    float fallback_lat;
    float fallback_lon;
    relay_cfg_t relays[BOARD_CHANNELS];  // index 0 = relay 1
    input_cfg_t inputs[BOARD_CHANNELS];  // index 0 = input 1
    schedule_cfg_t schedules[SCHEDULE_MAX_ENTRIES];  // index 0 = schedule 1
    // Bit n-1 = relay n's `r<n>_interlock` setting named a relay that
    // didn't name it back, or named itself: ignored (relays[n-1].interlock
    // reads 0), and device_config_report_health() warns about it.
    uint8_t interlock_invalid;
    // Bit n-1 = relay n was named by two or more *enabled* schedule entries
    // (issue #9, plan 16): rejected, not resolved by slot order -- every
    // entry naming that relay reads relay=0 (inert) until fixed, and
    // device_config_report_health() warns about it. A static check on the
    // settings themselves, the same shallow shape as interlock_invalid
    // above: it does not ask whether the entries' days/times could ever
    // actually collide, only whether they name the same relay at all.
    uint8_t schedule_relay_conflict;
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

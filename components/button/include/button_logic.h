// The BOOT button (plan 14, issue #7): held long enough, it reopens the
// setup access point or factory-resets, decided on release so a person can
// let go once the LED shows the action they want. Pure state machine, no
// hardware, so it's host-testable.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// How long the button must be held for each action.
#define BUTTON_PORTAL_MS 5000
#define BUTTON_RESET_MS 15000
// Consecutive same-level poll ticks needed before a level change is
// accepted, same shape as input_sense's debounce.
#define BUTTON_DEBOUNCE_TICKS 3

// What to show while the button is still held, so releasing now would do
// this. NONE once released, or before the shortest threshold is reached.
typedef enum {
    BUTTON_FEEDBACK_NONE,
    BUTTON_FEEDBACK_PORTAL,
    BUTTON_FEEDBACK_RESET,
} button_feedback_t;

// What to actually do, decided once, at the moment of release.
typedef enum {
    BUTTON_ACTION_NONE,
    BUTTON_ACTION_PORTAL,
    BUTTON_ACTION_RESET,
} button_action_t;

typedef struct {
    bool ignoring;         // true until first seen released (SPEC: a button
                            // held at start-up, e.g. into the bootloader
                            // check, does nothing until let go once)
    bool raw;               // last raw level sampled
    uint8_t same_count;     // consecutive ticks `raw` has held its value
    bool debounced;         // accepted (debounced) level, true = pressed
    bool was_pressed;       // debounced level on the previous update
    uint32_t press_start_ms;
} button_state_t;

// `pressed_now`: the raw level at the very first read, before any
// debouncing or polling has happened.
void button_logic_init(button_state_t *st, bool pressed_now);

// Call once per poll tick with the current raw level (true = pressed) and
// the current time. Returns the action to perform (only ever non-NONE on
// the tick the button is released). `*out_feedback` is set every call to
// what a release right now would do.
button_action_t button_logic_update(button_state_t *st, bool raw_pressed, uint32_t now_ms,
                                     button_feedback_t *out_feedback);

#ifdef __cplusplus
}
#endif

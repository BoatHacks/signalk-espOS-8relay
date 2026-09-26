#include "button_logic.h"

static bool debounce(button_state_t *st, bool raw_pressed)
{
    if (raw_pressed != st->raw) {
        st->raw = raw_pressed;
        st->same_count = 0;
    }
    if (st->same_count < BUTTON_DEBOUNCE_TICKS) {
        st->same_count++;
        if (st->same_count >= BUTTON_DEBOUNCE_TICKS) {
            st->debounced = st->raw;
        }
    }
    return st->debounced;
}

void button_logic_init(button_state_t *st, bool pressed_now)
{
    *st = (button_state_t){
        .ignoring = pressed_now,
        .raw = pressed_now,
        .same_count = BUTTON_DEBOUNCE_TICKS,
        .debounced = pressed_now,
        .was_pressed = pressed_now,
    };
}

button_action_t button_logic_update(button_state_t *st, bool raw_pressed, uint32_t now_ms,
                                     button_feedback_t *out_feedback)
{
    const bool pressed = debounce(st, raw_pressed);

    if (st->ignoring) {
        if (!pressed) {
            st->ignoring = false;
        }
        st->was_pressed = pressed;
        *out_feedback = BUTTON_FEEDBACK_NONE;
        return BUTTON_ACTION_NONE;
    }

    if (pressed && !st->was_pressed) {
        st->press_start_ms = now_ms;  // fresh press
    }

    button_action_t action = BUTTON_ACTION_NONE;
    button_feedback_t feedback = BUTTON_FEEDBACK_NONE;
    const uint32_t held_ms = now_ms - st->press_start_ms;  // wraps correctly (both uint32_t)

    if (pressed) {
        if (held_ms >= BUTTON_RESET_MS) {
            feedback = BUTTON_FEEDBACK_RESET;
        } else if (held_ms >= BUTTON_PORTAL_MS) {
            feedback = BUTTON_FEEDBACK_PORTAL;
        }
    } else if (st->was_pressed) {
        // Just released: the action is whichever threshold was reached.
        if (held_ms >= BUTTON_RESET_MS) {
            action = BUTTON_ACTION_RESET;
        } else if (held_ms >= BUTTON_PORTAL_MS) {
            action = BUTTON_ACTION_PORTAL;
        }
    }

    st->was_pressed = pressed;
    *out_feedback = feedback;
    return action;
}

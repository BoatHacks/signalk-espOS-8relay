#include "button_logic.h"
#include "unity.h"

// Feeds `pressed` for BUTTON_DEBOUNCE_TICKS ticks, 10ms apart starting at
// `*t`, so the debounced level actually settles to it -- one call isn't
// enough, by design. Returns the action from the settling tick (the only
// one where a level change, if any, is recognized) and advances `*t` past
// the ticks it consumed.
static button_action_t settle(button_state_t *st, bool pressed, uint32_t *t, button_feedback_t *fb)
{
    button_action_t action = BUTTON_ACTION_NONE;
    for (int i = 0; i < BUTTON_DEBOUNCE_TICKS; i++) {
        action = button_logic_update(st, pressed, *t, fb);
        *t += 10;
    }
    return action;
}

// A press shorter than the debounce window (one glitchy tick that bounces
// back) never counts as a press at all.
TEST_CASE("debounces a single glitchy tick", "[button]")
{
    button_state_t st;
    button_feedback_t fb;
    uint32_t t = 0;
    button_logic_init(&st, false);

    button_logic_update(&st, true, t, &fb);  // one glitch tick only
    t += 10;
    button_action_t action = settle(&st, false, &t, &fb);  // settles back to released

    TEST_ASSERT_EQUAL(BUTTON_ACTION_NONE, action);
    TEST_ASSERT_EQUAL(BUTTON_FEEDBACK_NONE, fb);
}

// A short press (released well before the 5s portal threshold) does
// nothing.
TEST_CASE("short press does nothing", "[button]")
{
    button_state_t st;
    button_feedback_t fb;
    uint32_t t = 0;
    button_logic_init(&st, false);

    settle(&st, true, &t, &fb);  // press accepted
    t += 1000;                   // held briefly
    button_logic_update(&st, true, t, &fb);
    TEST_ASSERT_EQUAL(BUTTON_FEEDBACK_NONE, fb);
    t += 10;

    button_action_t action = settle(&st, false, &t, &fb);  // released
    TEST_ASSERT_EQUAL(BUTTON_ACTION_NONE, action);
}

// Held past the 5s portal threshold, feedback shows PORTAL, and releasing
// there fires BUTTON_ACTION_PORTAL.
TEST_CASE("5s hold and release triggers the portal action", "[button]")
{
    button_state_t st;
    button_feedback_t fb;
    uint32_t t = 0;
    button_logic_init(&st, false);

    settle(&st, true, &t, &fb);  // press accepted
    t += BUTTON_PORTAL_MS;       // held well past the threshold
    button_logic_update(&st, true, t, &fb);
    TEST_ASSERT_EQUAL(BUTTON_FEEDBACK_PORTAL, fb);
    t += 10;

    button_action_t action = settle(&st, false, &t, &fb);  // released
    TEST_ASSERT_EQUAL(BUTTON_ACTION_PORTAL, action);
}

// Held past the 15s factory-reset threshold, feedback shows RESET (not
// PORTAL, even though 5s has also passed), and releasing fires RESET.
TEST_CASE("15s hold and release triggers the factory reset action", "[button]")
{
    button_state_t st;
    button_feedback_t fb;
    uint32_t t = 0;
    button_logic_init(&st, false);

    settle(&st, true, &t, &fb);
    t += BUTTON_RESET_MS;
    button_logic_update(&st, true, t, &fb);
    TEST_ASSERT_EQUAL(BUTTON_FEEDBACK_RESET, fb);
    t += 10;

    button_action_t action = settle(&st, false, &t, &fb);
    TEST_ASSERT_EQUAL(BUTTON_ACTION_RESET, action);
}

// A button already held at start-up (e.g. still being released from the
// bootloader check) must not trigger anything, even if it's held long
// enough to cross both thresholds, until it has been seen released once.
TEST_CASE("a button held at boot is ignored until first released", "[button]")
{
    button_state_t st;
    button_feedback_t fb;
    uint32_t t = 0;
    button_logic_init(&st, true);  // pressed at the very first read

    // Still held, well past both thresholds: no feedback, because it's
    // being ignored, not because it hasn't been held long enough.
    button_action_t action = button_logic_update(&st, true, BUTTON_RESET_MS + 1000, &fb);
    TEST_ASSERT_EQUAL(BUTTON_ACTION_NONE, action);
    TEST_ASSERT_EQUAL(BUTTON_FEEDBACK_NONE, fb);

    // Released (debounced): stops being ignored, but still does nothing on
    // this release, since it was never counted as a "real" press.
    t = BUTTON_RESET_MS + 1000;
    action = settle(&st, false, &t, &fb);
    TEST_ASSERT_EQUAL(BUTTON_ACTION_NONE, action);

    // A fresh press now behaves normally: a short one does nothing.
    settle(&st, true, &t, &fb);
    t += 100;
    action = settle(&st, false, &t, &fb);
    TEST_ASSERT_EQUAL(BUTTON_ACTION_NONE, action);
}

// Releasing a short press, then pressing again much later, must time the
// second press from its own start -- not carry over the first one's
// duration.
TEST_CASE("a fresh press after release starts its own timer", "[button]")
{
    button_state_t st;
    button_feedback_t fb;
    uint32_t t = 0;
    button_logic_init(&st, false);

    settle(&st, true, &t, &fb);          // first press accepted
    t += BUTTON_PORTAL_MS - 1000;        // held, but short of the threshold
    button_logic_update(&st, true, t, &fb);
    t += 10;
    settle(&st, false, &t, &fb);         // released early: a short press

    t += 100000;                         // long gap before the next press
    settle(&st, true, &t, &fb);          // a brand-new press
    button_logic_update(&st, true, t, &fb);
    TEST_ASSERT_EQUAL(BUTTON_FEEDBACK_NONE, fb);  // not yet 5s into *this* press
}

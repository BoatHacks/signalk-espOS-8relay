#include <string.h>

#include "input_sense.h"
#include "unity.h"

// ---------------------------------------------------------------- fakes

static uint8_t pins;  // raw levels; inputs are active-low, so 0xFF = all off
static uint32_t clock_ms;

static uint8_t read_pins(void *ctx)
{
    return pins;
}

static uint32_t fake_now(void)
{
    return clock_ms;
}

static const input_sense_hw_t hw = {.read_pins = read_pins, .now_ms = fake_now};

#define MAX_EVENTS 32
static struct {
    uint8_t ch;
    bool on;
} overrides[MAX_EVENTS], changes[MAX_EVENTS];
static int n_overrides, n_changes;

static esp_err_t record_override(uint8_t relay, bool on)
{
    overrides[n_overrides].ch = relay;
    overrides[n_overrides].on = on;
    n_overrides++;
    return ESP_OK;
}

static void record_change(uint8_t ch, bool on, uint8_t mask, void *arg)
{
    changes[n_changes].ch = ch;
    changes[n_changes].on = on;
    n_changes++;
}

static device_config_t cfg;

static void energise(int input, bool on)
{
    const uint8_t bit = 1u << (input - 1);
    pins = on ? (pins & ~bit) : (pins | bit);  // active-low
}

static void run_for(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5) {
        clock_ms += 5;
        input_sense_poll();
    }
}

static void fresh(void)
{
    input_sense_reset();
    memset(&cfg, 0, sizeof(cfg));
    cfg.debounce_ms = 50;
    pins = 0xFF;
    clock_ms = 1000;
    n_overrides = n_changes = 0;
}

static void start(void)
{
    TEST_ESP_OK(input_sense_init(&hw, &cfg, record_override));
    TEST_ESP_OK(input_sense_add_listener(record_change, NULL));
}

// ------------------------------------------------------------- start-up

TEST_CASE("inputs report nothing until the first readings settle", "[input_sense]")
{
    fresh();
    energise(3, true);
    start();
    run_for(45);
    TEST_ASSERT_FALSE(input_sense_ready());
    TEST_ASSERT_EQUAL_HEX8(0, input_sense_get_mask());
    run_for(10);
    TEST_ASSERT_TRUE(input_sense_ready());
    TEST_ASSERT_EQUAL_HEX8(0x04, input_sense_get_mask());
    TEST_ASSERT_EQUAL(8, n_changes);  // one initial report per input
}

TEST_CASE("start-up applies every override from the settled inputs", "[input_sense]")
{
    fresh();
    cfg.relays[0].override_di = 2;  // relay 1 follows input 2
    cfg.relays[5].override_di = 2;  // so does relay 6
    cfg.relays[3].override_di = 4;  // relay 4 follows input 4
    energise(2, true);
    start();
    run_for(60);
    TEST_ASSERT_EQUAL(3, n_overrides);
    TEST_ASSERT_EQUAL(1, overrides[0].ch);
    TEST_ASSERT_TRUE(overrides[0].on);
    TEST_ASSERT_EQUAL(6, overrides[1].ch);
    TEST_ASSERT_TRUE(overrides[1].on);
    TEST_ASSERT_EQUAL(4, overrides[2].ch);
    TEST_ASSERT_FALSE(overrides[2].on);
}

// ------------------------------------------------------------- debounce

TEST_CASE("bounce shorter than the debounce time is ignored", "[input_sense]")
{
    fresh();
    start();
    run_for(60);
    n_changes = 0;
    for (int k = 0; k < 5; k++) {  // 20 ms on, 20 ms off
        energise(1, true);
        run_for(20);
        energise(1, false);
        run_for(20);
    }
    TEST_ASSERT_EQUAL(0, n_changes);
    TEST_ASSERT_EQUAL_HEX8(0, input_sense_get_mask());
}

TEST_CASE("a steady change is reported once, after the debounce time", "[input_sense]")
{
    fresh();
    cfg.relays[7].override_di = 5;
    start();
    run_for(60);
    n_changes = n_overrides = 0;
    energise(5, true);
    run_for(45);
    TEST_ASSERT_EQUAL(0, n_changes);
    run_for(10);
    TEST_ASSERT_EQUAL(1, n_changes);
    TEST_ASSERT_EQUAL(5, changes[0].ch);
    TEST_ASSERT_TRUE(changes[0].on);
    TEST_ASSERT_EQUAL(1, n_overrides);
    TEST_ASSERT_EQUAL(8, overrides[0].ch);
    TEST_ASSERT_TRUE(overrides[0].on);
    run_for(500);
    TEST_ASSERT_EQUAL(1, n_changes);
    // A steady input doesn't re-assert its relay, so a remote command given
    // meanwhile stands until the input changes again (SPEC.md section 2).
    TEST_ASSERT_EQUAL(1, n_overrides);

    energise(5, false);
    run_for(60);
    TEST_ASSERT_EQUAL(2, n_changes);
    TEST_ASSERT_FALSE(overrides[1].on);
}

TEST_CASE("unmapped inputs switch no relays", "[input_sense]")
{
    fresh();
    cfg.relays[0].override_di = 1;
    start();
    run_for(60);
    n_overrides = 0;
    energise(2, true);
    run_for(60);
    TEST_ASSERT_EQUAL(0, n_overrides);
}

// --------------------------------------------------------------- invert

TEST_CASE("invert flips the reported state", "[input_sense]")
{
    fresh();
    cfg.inputs[0].invert = true;
    start();
    run_for(60);
    TEST_ASSERT_EQUAL_HEX8(0x01, input_sense_get_mask());  // idle NC switch reads on
    energise(1, true);
    run_for(60);
    TEST_ASSERT_EQUAL_HEX8(0x00, input_sense_get_mask());
}

TEST_CASE("changing invert reports the new state at once and switches no relay", "[input_sense]")
{
    fresh();
    cfg.relays[0].override_di = 3;  // relay 1 follows input 3
    start();
    run_for(60);
    n_changes = n_overrides = 0;
    cfg.inputs[2].invert = true;
    input_sense_update_config(&cfg);
    TEST_ASSERT_EQUAL(1, n_changes);
    TEST_ASSERT_EQUAL(3, changes[0].ch);
    TEST_ASSERT_TRUE(changes[0].on);
    TEST_ASSERT_EQUAL_HEX8(0x04, input_sense_get_mask());
    run_for(200);
    TEST_ASSERT_EQUAL(1, n_changes);   // no second report from the debounce
    TEST_ASSERT_EQUAL(0, n_overrides); // and the relay was never switched

    energise(3, true);  // a real change still drives the relay
    run_for(60);
    TEST_ASSERT_EQUAL(1, n_overrides);
    TEST_ASSERT_FALSE(overrides[0].on);
}

TEST_CASE("changing invert before inputs settle switches no relay either", "[input_sense]")
{
    fresh();
    cfg.relays[0].override_di = 3;
    start();
    run_for(20);
    cfg.inputs[2].invert = true;
    input_sense_update_config(&cfg);
    run_for(60);
    TEST_ASSERT_TRUE(input_sense_ready());
    TEST_ASSERT_EQUAL_HEX8(0x04, input_sense_get_mask());
    // The start-up override applies the settled state, as at any boot.
    TEST_ASSERT_EQUAL(1, n_overrides);
    TEST_ASSERT_TRUE(overrides[0].on);
}

TEST_CASE("linking a relay to an input that is on doesn't switch it", "[input_sense]")
{
    fresh();
    energise(6, true);
    start();
    run_for(60);
    n_overrides = 0;
    cfg.relays[1].override_di = 6;
    input_sense_update_config(&cfg);
    run_for(200);
    TEST_ASSERT_EQUAL(0, n_overrides);  // it follows the input from its next change
}

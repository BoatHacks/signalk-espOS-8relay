#include <string.h>

#include "counters.h"
#include "unity.h"

// ---------------------------------------------------------------- fakes

static struct {
    bool has_value;
    counters_data_t value;
    int saves;
} store;

static esp_err_t store_load(void *ctx, counters_data_t *out)
{
    if (!store.has_value) {
        return ESP_ERR_NOT_FOUND;
    }
    *out = store.value;
    return ESP_OK;
}

static esp_err_t store_save(void *ctx, const counters_data_t *data)
{
    store.has_value = true;
    store.value = *data;
    store.saves++;
    return ESP_OK;
}

static uint32_t clock_ms;
static uint32_t fake_now(void)
{
    return clock_ms;
}

static const counters_hw_t hw = {
    .store = {.load = store_load, .save = store_save},
    .now_ms = fake_now,
};

void tearDown(void) {}

void setUp(void)
{
    memset(&store, 0, sizeof(store));
    clock_ms = 0;
    counters_reset_all();
    TEST_ASSERT_EQUAL(ESP_OK, counters_init(&hw));
}

static void get(counters_kind_t kind, uint8_t ch, uint32_t *cycles, uint32_t *runtime_s)
{
    counters_get(kind, ch, clock_ms, cycles, runtime_s);
}

// ----------------------------------------------------------------- tests

TEST_CASE("a fresh channel starts at zero", "[counters]")
{
    uint32_t cycles, runtime_s;
    get(COUNTERS_RELAY, 1, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(0, cycles);
    TEST_ASSERT_EQUAL_UINT32(0, runtime_s);
}

TEST_CASE("the first report seeds state without counting a cycle", "[counters]")
{
    // Mirrors relay_ctrl never notifying listeners of a relay's boot state,
    // and input_sense's first settled reading arriving as an ordinary
    // listener call: either way, the very first report for a channel is
    // "this is where we are", not a transition.
    counters_on_change(COUNTERS_RELAY, 1, true, 1000);
    uint32_t cycles, runtime_s;
    clock_ms = 1000;
    get(COUNTERS_RELAY, 1, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(0, cycles);
    TEST_ASSERT_EQUAL_UINT32(0, runtime_s);

    // But it did start accruing runtime from the moment it was seen on.
    clock_ms = 5000;
    get(COUNTERS_RELAY, 1, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(0, cycles);
    TEST_ASSERT_EQUAL_UINT32(4, runtime_s);
}

TEST_CASE("an off->on edge counts one cycle; repeated on/off do not", "[counters]")
{
    counters_on_change(COUNTERS_RELAY, 3, false, 0);  // seed: starts off
    counters_on_change(COUNTERS_RELAY, 3, true, 100);   // edge: cycle 1
    counters_on_change(COUNTERS_RELAY, 3, true, 200);   // repeated on: nothing
    counters_on_change(COUNTERS_RELAY, 3, true, 300);   // repeated on: nothing
    counters_on_change(COUNTERS_RELAY, 3, false, 400);  // edge: off
    counters_on_change(COUNTERS_RELAY, 3, false, 500);  // repeated off: nothing
    counters_on_change(COUNTERS_RELAY, 3, true, 600);   // edge: cycle 2

    uint32_t cycles, runtime_s;
    clock_ms = 600;
    get(COUNTERS_RELAY, 3, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(2, cycles);
}

TEST_CASE("runtime accumulates across on-periods and while continuously on", "[counters]")
{
    counters_on_change(COUNTERS_INPUT, 2, false, 0);       // seed off
    counters_on_change(COUNTERS_INPUT, 2, true, 0);        // on at t=0
    counters_on_change(COUNTERS_INPUT, 2, false, 3000);    // off at t=3s: +3s
    counters_on_change(COUNTERS_INPUT, 2, true, 10000);    // on again at t=10s

    uint32_t cycles, runtime_s;
    clock_ms = 10000;
    get(COUNTERS_INPUT, 2, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(2, cycles);
    TEST_ASSERT_EQUAL_UINT32(3, runtime_s);  // only the first, closed, period so far

    // Still on: live runtime keeps growing without another event.
    clock_ms = 17500;
    get(COUNTERS_INPUT, 2, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(3 + 7, runtime_s);  // 3s closed + 7.5s (floored) running

    counters_on_change(COUNTERS_INPUT, 2, false, 20000);  // off: +10s more (10s to 20s)
    clock_ms = 20000;
    get(COUNTERS_INPUT, 2, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(3 + 10, runtime_s);
}

TEST_CASE("relay and input channels, and different channel numbers, are independent", "[counters]")
{
    counters_on_change(COUNTERS_RELAY, 1, false, 0);
    counters_on_change(COUNTERS_RELAY, 1, true, 0);
    counters_on_change(COUNTERS_INPUT, 1, false, 0);
    counters_on_change(COUNTERS_RELAY, 2, false, 0);

    clock_ms = 5000;
    uint32_t cycles, runtime_s;
    get(COUNTERS_RELAY, 1, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(1, cycles);
    TEST_ASSERT_EQUAL_UINT32(5, runtime_s);

    get(COUNTERS_INPUT, 1, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(0, cycles);
    TEST_ASSERT_EQUAL_UINT32(0, runtime_s);

    get(COUNTERS_RELAY, 2, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(0, cycles);
    TEST_ASSERT_EQUAL_UINT32(0, runtime_s);
}

TEST_CASE("tick saves at most once per interval, and only when something changed", "[counters]")
{
    // A closed on/off cycle: nothing keeps changing afterwards.
    counters_on_change(COUNTERS_RELAY, 1, false, 0);
    counters_on_change(COUNTERS_RELAY, 1, true, 0);
    counters_on_change(COUNTERS_RELAY, 1, false, 100);  // a cycle + closed runtime: dirty

    clock_ms = 1000;  // well under the interval
    counters_tick(clock_ms);
    TEST_ASSERT_EQUAL_INT(0, store.saves);

    clock_ms = COUNTERS_SAVE_INTERVAL_MS;  // exactly due
    counters_tick(clock_ms);
    TEST_ASSERT_EQUAL_INT(1, store.saves);

    // Nothing changed since: no further save, even well past another
    // interval's worth of ticks.
    clock_ms += COUNTERS_SAVE_INTERVAL_MS * 3;
    counters_tick(clock_ms);
    TEST_ASSERT_EQUAL_INT(1, store.saves);

    // A channel left continuously on still counts as "changed" (its
    // runtime keeps moving), so once due again it saves.
    counters_on_change(COUNTERS_RELAY, 2, false, clock_ms);
    counters_on_change(COUNTERS_RELAY, 2, true, clock_ms);
    clock_ms += COUNTERS_SAVE_INTERVAL_MS;
    counters_tick(clock_ms);
    TEST_ASSERT_EQUAL_INT(2, store.saves);
}

TEST_CASE("flush_now saves unconditionally, e.g. on a clean shutdown", "[counters]")
{
    counters_on_change(COUNTERS_RELAY, 1, false, 0);
    counters_on_change(COUNTERS_RELAY, 1, true, 0);
    clock_ms = 500;  // well under the interval, nothing due
    counters_flush_now(clock_ms);
    TEST_ASSERT_EQUAL_INT(1, store.saves);
    TEST_ASSERT_EQUAL_UINT32(1, store.value.relays[0].cycles);

    // A channel currently on gets its running period folded in too.
    clock_ms = 2500;
    counters_flush_now(clock_ms);
    TEST_ASSERT_EQUAL_INT(2, store.saves);
    TEST_ASSERT_EQUAL_UINT32(2, store.value.relays[0].runtime_s);
}

TEST_CASE("state survives init() after a flush, as a reboot would see it", "[counters]")
{
    counters_on_change(COUNTERS_RELAY, 4, false, 0);
    counters_on_change(COUNTERS_RELAY, 4, true, 0);
    counters_on_change(COUNTERS_RELAY, 4, false, 4000);
    counters_on_change(COUNTERS_RELAY, 4, true, 4000);
    clock_ms = 9000;
    counters_flush_now(clock_ms);  // 2 cycles, 4s + 5s(live) = 9s

    // "Reboot": a fresh in-RAM state loading the same store.
    counters_reset_all();
    TEST_ASSERT_EQUAL(ESP_OK, counters_init(&hw));

    uint32_t cycles, runtime_s;
    counters_get(COUNTERS_RELAY, 4, 9000, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(2, cycles);
    TEST_ASSERT_EQUAL_UINT32(9, runtime_s);

    // The reloaded channel is "seen" with its saved off/on state: the next
    // report of the same state is not a fresh edge.
    counters_get(COUNTERS_RELAY, 4, 9000, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(2, cycles);
}

TEST_CASE("a channel not seen at load starts fresh, at zero", "[counters]")
{
    // Nothing stored yet (store.has_value == false): counters_init() must
    // not fail, and every channel reads zero.
    uint32_t cycles, runtime_s;
    counters_get(COUNTERS_RELAY, 8, 0, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(0, cycles);
    TEST_ASSERT_EQUAL_UINT32(0, runtime_s);
}

TEST_CASE("reset clears one channel only, and flushes at once", "[counters]")
{
    counters_on_change(COUNTERS_RELAY, 1, false, 0);
    counters_on_change(COUNTERS_RELAY, 1, true, 0);
    counters_on_change(COUNTERS_RELAY, 2, false, 0);
    counters_on_change(COUNTERS_RELAY, 2, true, 0);
    clock_ms = 4000;

    counters_reset(COUNTERS_RELAY, 1, clock_ms);

    uint32_t cycles, runtime_s;
    get(COUNTERS_RELAY, 1, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(0, cycles);
    TEST_ASSERT_EQUAL_UINT32(0, runtime_s);

    // Channel 2 is untouched.
    get(COUNTERS_RELAY, 2, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(1, cycles);
    TEST_ASSERT_EQUAL_UINT32(4, runtime_s);

    // The reset itself was persisted immediately, not left for the throttle.
    TEST_ASSERT_TRUE(store.saves > 0);
    TEST_ASSERT_EQUAL_UINT32(0, store.value.relays[0].cycles);

    // A reset channel that was on keeps counting on-time from the reset
    // moment: it is still on, just with the odometer zeroed.
    clock_ms = 4000 + 6000;
    get(COUNTERS_RELAY, 1, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(6, runtime_s);
}

TEST_CASE("resetting an off channel leaves it off with zero counts", "[counters]")
{
    counters_on_change(COUNTERS_RELAY, 5, false, 0);
    counters_reset(COUNTERS_RELAY, 5, 1000);
    uint32_t cycles, runtime_s;
    counters_get(COUNTERS_RELAY, 5, 5000, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(0, cycles);
    TEST_ASSERT_EQUAL_UINT32(0, runtime_s);
}

TEST_CASE("an out-of-range channel is ignored, not a crash", "[counters]")
{
    counters_on_change(COUNTERS_RELAY, 0, true, 0);
    counters_on_change(COUNTERS_RELAY, 9, true, 0);
    counters_reset(COUNTERS_INPUT, 0, 0);
    uint32_t cycles, runtime_s;
    counters_get(COUNTERS_RELAY, 0, 0, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(0, cycles);
    counters_get(COUNTERS_RELAY, 9, 0, &cycles, &runtime_s);
    TEST_ASSERT_EQUAL_UINT32(0, cycles);
}

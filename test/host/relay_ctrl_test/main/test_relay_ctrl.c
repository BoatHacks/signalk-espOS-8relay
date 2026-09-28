#include <string.h>

#include "espos_health.h"
#include "relay_ctrl.h"
#include "unity.h"

// ---------------------------------------------------------------- fakes

#define MAX_WRITES 64

static struct {
    uint8_t regs[4];
    int fail_writes;  // this many writes fail from now on (-1 = all)
    struct {
        uint8_t reg, val;
    } writes[MAX_WRITES];
    int n_writes;
} chip;

static esp_err_t chip_read(void *ctx, uint8_t reg, uint8_t *val)
{
    *val = chip.regs[reg];
    return ESP_OK;
}

static esp_err_t chip_write(void *ctx, uint8_t reg, uint8_t val)
{
    if (chip.fail_writes != 0) {
        if (chip.fail_writes > 0) {
            chip.fail_writes--;
        }
        return ESP_ERR_TIMEOUT;
    }
    chip.regs[reg] = val;
    if (chip.n_writes < MAX_WRITES) {
        chip.writes[chip.n_writes].reg = reg;
        chip.writes[chip.n_writes].val = val;
        chip.n_writes++;
    }
    return ESP_OK;
}

static struct {
    bool has_value;
    uint8_t value;
    int saves;
} store;

static esp_err_t store_load(void *ctx, uint8_t *mask)
{
    if (!store.has_value) {
        return ESP_ERR_NOT_FOUND;
    }
    *mask = store.value;
    return ESP_OK;
}

static esp_err_t store_save(void *ctx, uint8_t mask)
{
    store.has_value = true;
    store.value = mask;
    store.saves++;
    return ESP_OK;
}

static uint32_t clock_ms;
static uint32_t fake_now(void)
{
    return clock_ms;
}

static const relay_ctrl_hw_t hw = {
    .expander = {.read_reg = chip_read, .write_reg = chip_write},
    .store = {.load = store_load, .save = store_save},
    .now_ms = fake_now,
};

#define MAX_EVENTS 32
static struct {
    uint8_t channel;
    bool on;
    relay_source_t src;
} events[MAX_EVENTS];
static int n_events;

static void on_change(uint8_t channel, bool on, relay_source_t src, uint8_t mask, void *arg)
{
    if (n_events < MAX_EVENTS) {
        events[n_events].channel = channel;
        events[n_events].on = on;
        events[n_events].src = src;
        n_events++;
    }
}

// ------------------------------------------------------------- fixtures

static device_config_t cfg;

static void power_on_chip(void)
{
    memset(&chip, 0, sizeof(chip));
    chip.regs[TCA9554_REG_OUTPUT] = 0xFF;  // TCA9554 power-on defaults
    chip.regs[TCA9554_REG_CONFIG] = 0xFF;
}

static void fresh(void)
{
    relay_ctrl_reset();
    espos_health_reset();
    power_on_chip();
    memset(&store, 0, sizeof(store));
    memset(&cfg, 0, sizeof(cfg));
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        cfg.relays[i].mode = RELAY_MODE_LATCHING;
        cfg.relays[i].pulse_ms = 1000;
        cfg.relays[i].failsafe = FAILSAFE_DEFAULT_SAFE;
    }
    clock_ms = 100000;
    n_events = 0;
}

static void start(void)
{
    TEST_ESP_OK(relay_ctrl_init(&hw, &cfg));
    TEST_ESP_OK(relay_ctrl_add_listener(on_change, NULL));
    chip.n_writes = 0;
}

static espos_health_state_t expander_health(void)
{
    espos_health_condition_t c[8];
    size_t n = espos_health_snapshot(c, 8);
    for (size_t i = 0; i < n && i < 8; i++) {
        if (strcmp(c[i].key, "relayExpander") == 0) {
            return c[i].state;
        }
    }
    return ESPOS_HEALTH_NORMAL;
}

// ---------------------------------------------------------------- boot

TEST_CASE("cold boot writes outputs before making pins outputs", "[relay_ctrl]")
{
    fresh();
    TEST_ESP_OK(relay_ctrl_init(&hw, &cfg));
    TEST_ASSERT_EQUAL(2, chip.n_writes);
    TEST_ASSERT_EQUAL(TCA9554_REG_OUTPUT, chip.writes[0].reg);
    TEST_ASSERT_EQUAL_HEX8(0x00, chip.writes[0].val);
    TEST_ASSERT_EQUAL(TCA9554_REG_CONFIG, chip.writes[1].reg);
    TEST_ASSERT_EQUAL_HEX8(0x00, chip.writes[1].val);
    TEST_ASSERT_EQUAL_HEX8(0x00, relay_ctrl_get_mask());
}

TEST_CASE("cold boot restores hold relays only", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].failsafe = FAILSAFE_HOLD;
    cfg.relays[1].failsafe = FAILSAFE_HOLD;
    cfg.relays[1].mode = RELAY_MODE_MOMENTARY;  // momentary: never held
    store.has_value = true;
    store.value = 0x07;  // relays 1-3 were on
    TEST_ESP_OK(relay_ctrl_init(&hw, &cfg));
    TEST_ASSERT_EQUAL_HEX8(0x01, relay_ctrl_get_mask());
    TEST_ASSERT_EQUAL_HEX8(0x01, chip.regs[TCA9554_REG_OUTPUT]);
}

TEST_CASE("warm boot keeps hold relays without switching them", "[relay_ctrl]")
{
    fresh();
    chip.regs[TCA9554_REG_CONFIG] = 0x00;  // expander kept its setup
    chip.regs[TCA9554_REG_OUTPUT] = 0x05;  // relays 1 and 3 on
    cfg.relays[0].failsafe = FAILSAFE_HOLD;
    store.has_value = true;
    store.value = 0x00;  // store is stale; the chip is right
    TEST_ESP_OK(relay_ctrl_init(&hw, &cfg));
    TEST_ASSERT_EQUAL_HEX8(0x01, relay_ctrl_get_mask());  // relay 3 default-safe: off
    TEST_ASSERT_EQUAL(1, chip.n_writes);                  // no direction write
    TEST_ASSERT_EQUAL(TCA9554_REG_OUTPUT, chip.writes[0].reg);
    TEST_ASSERT_EQUAL_HEX8(0x01, chip.writes[0].val);  // relay 1 bit never cleared
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL_HEX8(0x01, store.value);  // store corrected
}

// ------------------------------------------------------------- commands

TEST_CASE("set switches a relay and notifies once per change", "[relay_ctrl]")
{
    fresh();
    start();
    TEST_ESP_OK(relay_ctrl_set(4, true, RELAY_SRC_SK));
    TEST_ASSERT_TRUE(relay_ctrl_get(4));
    TEST_ASSERT_EQUAL_HEX8(0x08, chip.regs[TCA9554_REG_OUTPUT]);
    TEST_ASSERT_EQUAL(1, n_events);
    TEST_ASSERT_EQUAL(4, events[0].channel);
    TEST_ASSERT_TRUE(events[0].on);
    TEST_ASSERT_EQUAL(RELAY_SRC_SK, events[0].src);

    TEST_ESP_OK(relay_ctrl_set(4, true, RELAY_SRC_N2K));  // no change
    TEST_ASSERT_EQUAL(1, n_events);
    TEST_ESP_OK(relay_ctrl_set(4, false, RELAY_SRC_N2K));
    TEST_ASSERT_FALSE(relay_ctrl_get(4));
    TEST_ASSERT_EQUAL(2, n_events);
    TEST_ASSERT_EQUAL(RELAY_SRC_N2K, events[1].src);
}

TEST_CASE("bad channels are rejected", "[relay_ctrl]")
{
    fresh();
    start();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, relay_ctrl_set(0, true, RELAY_SRC_SK));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, relay_ctrl_set(9, true, RELAY_SRC_SK));
    TEST_ASSERT_FALSE(relay_ctrl_get(9));
    TEST_ASSERT_EQUAL(0, n_events);
}

// ------------------------------------------------------------ momentary

TEST_CASE("a momentary pulse ends on time", "[relay_ctrl]")
{
    fresh();
    cfg.relays[1].mode = RELAY_MODE_MOMENTARY;
    start();
    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));
    clock_ms += 999;
    relay_ctrl_tick();
    TEST_ASSERT_TRUE(relay_ctrl_get(2));
    clock_ms += 1;
    relay_ctrl_tick();
    TEST_ASSERT_FALSE(relay_ctrl_get(2));
    TEST_ASSERT_EQUAL(2, n_events);
    TEST_ASSERT_EQUAL(RELAY_SRC_PULSE_END, events[1].src);
}

TEST_CASE("a repeated on restarts the pulse; off cancels it", "[relay_ctrl]")
{
    fresh();
    cfg.relays[1].mode = RELAY_MODE_MOMENTARY;
    start();
    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));
    clock_ms += 800;
    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));
    clock_ms += 200;
    relay_ctrl_tick();
    TEST_ASSERT_TRUE(relay_ctrl_get(2));  // restarted, not yet over
    clock_ms += 800;
    relay_ctrl_tick();
    TEST_ASSERT_FALSE(relay_ctrl_get(2));

    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));
    TEST_ESP_OK(relay_ctrl_set(2, false, RELAY_SRC_SK));
    TEST_ESP_OK(relay_ctrl_set(3, true, RELAY_SRC_SK));  // latching, stays on
    clock_ms += 5000;
    n_events = 0;
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL(0, n_events);
    TEST_ASSERT_TRUE(relay_ctrl_get(3));
}

TEST_CASE("pulse timing survives the millisecond clock wrapping", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].mode = RELAY_MODE_MOMENTARY;
    start();
    clock_ms = 0xFFFFFF00u;
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));
    clock_ms += 500;  // wraps past zero
    relay_ctrl_tick();
    TEST_ASSERT_TRUE(relay_ctrl_get(1));
    clock_ms += 500;
    relay_ctrl_tick();
    TEST_ASSERT_FALSE(relay_ctrl_get(1));
}

TEST_CASE("switching a relay to latching stops its pulse", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].mode = RELAY_MODE_MOMENTARY;
    start();
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));
    cfg.relays[0].mode = RELAY_MODE_LATCHING;
    relay_ctrl_update_config(&cfg);
    clock_ms += 2000;
    relay_ctrl_tick();
    TEST_ASSERT_TRUE(relay_ctrl_get(1));
}

TEST_CASE("a relay that is on and becomes momentary switches off after one pulse", "[relay_ctrl]")
{
    fresh();
    start();
    TEST_ESP_OK(relay_ctrl_set(4, true, RELAY_SRC_SK));
    clock_ms += 60000;
    cfg.relays[3].mode = RELAY_MODE_MOMENTARY;
    relay_ctrl_update_config(&cfg);
    TEST_ASSERT_TRUE(relay_ctrl_get(4));  // saving didn't switch it
    clock_ms += 999;
    relay_ctrl_tick();
    TEST_ASSERT_TRUE(relay_ctrl_get(4));
    clock_ms += 1;
    relay_ctrl_tick();
    TEST_ASSERT_FALSE(relay_ctrl_get(4));
}

TEST_CASE("saving settings never switches a relay on", "[relay_ctrl]")
{
    fresh();
    start();
    n_events = 0;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        cfg.relays[i].mode = RELAY_MODE_MOMENTARY;
        cfg.relays[i].failsafe = FAILSAFE_HOLD;
    }
    relay_ctrl_update_config(&cfg);
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL_HEX8(0x00, relay_ctrl_get_mask());
    TEST_ASSERT_EQUAL(0, n_events);
}

// ------------------------------------------------------------ fail-safe

TEST_CASE("SignalK loss turns off only default-safe relays", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].failsafe = FAILSAFE_HOLD;
    cfg.relays[2].mode = RELAY_MODE_MOMENTARY;
    cfg.relays[2].failsafe = FAILSAFE_HOLD;  // ignored: momentary
    start();
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));
    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));
    TEST_ESP_OK(relay_ctrl_set(3, true, RELAY_SRC_SK));
    n_events = 0;
    relay_ctrl_sk_lost();
    TEST_ASSERT_EQUAL_HEX8(0x01, relay_ctrl_get_mask());
    TEST_ASSERT_EQUAL(2, n_events);
    TEST_ASSERT_EQUAL(RELAY_SRC_FAILSAFE, events[0].src);
}

// ---------------------------------------------------------- persistence

TEST_CASE("hold state is saved promptly, then at most every 5 s", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].failsafe = FAILSAFE_HOLD;
    start();
    store.saves = 0;

    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));  // default-safe: not saved
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL(0, store.saves);

    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL(1, store.saves);
    TEST_ASSERT_EQUAL_HEX8(0x01, store.value);

    clock_ms += 1000;
    TEST_ESP_OK(relay_ctrl_set(1, false, RELAY_SRC_SK));
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL(1, store.saves);  // too soon
    clock_ms += RELAY_CTRL_SAVE_DELAY_MS;
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL(2, store.saves);
    TEST_ASSERT_EQUAL_HEX8(0x00, store.value);
}

// ------------------------------------------------------------- failures

TEST_CASE("an I2C failure keeps the confirmed state and raises an alarm", "[relay_ctrl]")
{
    fresh();
    start();
    chip.fail_writes = -1;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, relay_ctrl_set(1, true, RELAY_SRC_SK));
    TEST_ASSERT_FALSE(relay_ctrl_get(1));
    TEST_ASSERT_EQUAL(0, n_events);
    TEST_ASSERT_EQUAL(ESPOS_HEALTH_ALARM, expander_health());

    chip.fail_writes = 0;
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));
    TEST_ASSERT_TRUE(relay_ctrl_get(1));
    TEST_ASSERT_EQUAL(ESPOS_HEALTH_NORMAL, expander_health());
}

TEST_CASE("a single failed write is retried", "[relay_ctrl]")
{
    fresh();
    start();
    chip.fail_writes = 1;
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));
    TEST_ASSERT_TRUE(relay_ctrl_get(1));
    TEST_ASSERT_EQUAL(ESPOS_HEALTH_NORMAL, expander_health());
}

// ------------------------------------------------------------- toggle

TEST_CASE("toggle flips a relay and reports the source", "[relay_ctrl]")
{
    fresh();
    start();
    TEST_ESP_OK(relay_ctrl_toggle(2, RELAY_SRC_INPUT));
    TEST_ASSERT_TRUE(relay_ctrl_get(2));
    TEST_ESP_OK(relay_ctrl_toggle(2, RELAY_SRC_INPUT));
    TEST_ASSERT_FALSE(relay_ctrl_get(2));
    TEST_ASSERT_EQUAL(2, n_events);
    TEST_ASSERT_EQUAL(RELAY_SRC_INPUT, events[1].src);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, relay_ctrl_toggle(9, RELAY_SRC_INPUT));
}

TEST_CASE("toggling a momentary relay starts a pulse; again ends it early", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].mode = RELAY_MODE_MOMENTARY;
    start();
    TEST_ESP_OK(relay_ctrl_toggle(1, RELAY_SRC_INPUT));
    TEST_ASSERT_TRUE(relay_ctrl_get(1));
    clock_ms += 400;
    TEST_ESP_OK(relay_ctrl_toggle(1, RELAY_SRC_INPUT));
    TEST_ASSERT_FALSE(relay_ctrl_get(1));
    clock_ms += 2000;
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL(2, n_events);  // no pulse end left running
}

// ------------------------------------------------------ maximum on-time

TEST_CASE("max on-time switches a relay off, whatever switched it on", "[relay_ctrl]")
{
    const relay_source_t sources[] = {RELAY_SRC_SK, RELAY_SRC_N2K, RELAY_SRC_WEB, RELAY_SRC_INPUT};
    for (size_t k = 0; k < sizeof(sources) / sizeof(sources[0]); k++) {
        fresh();
        cfg.relays[3].max_on_s = 60;
        start();
        TEST_ESP_OK(relay_ctrl_set(4, true, sources[k]));
        clock_ms += 59999;
        relay_ctrl_tick();
        TEST_ASSERT_TRUE(relay_ctrl_get(4));
        clock_ms += 1;
        relay_ctrl_tick();
        TEST_ASSERT_FALSE(relay_ctrl_get(4));
        TEST_ASSERT_EQUAL(2, n_events);
        TEST_ASSERT_EQUAL(RELAY_SRC_MAX_ON, events[1].src);
        TEST_ASSERT_FALSE(events[1].on);
    }
}

TEST_CASE("max on-time: a repeated on restarts it, off cancels it", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].max_on_s = 10;
    start();
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));
    clock_ms += 8000;
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));  // "still here"
    clock_ms += 8000;
    relay_ctrl_tick();
    TEST_ASSERT_TRUE(relay_ctrl_get(1));
    clock_ms += 2000;
    relay_ctrl_tick();
    TEST_ASSERT_FALSE(relay_ctrl_get(1));

    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));
    TEST_ESP_OK(relay_ctrl_set(1, false, RELAY_SRC_SK));
    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));  // no limit on relay 2
    n_events = 0;
    clock_ms += 3600 * 1000;
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL(0, n_events);
    TEST_ASSERT_TRUE(relay_ctrl_get(2));
}

TEST_CASE("max on-time is ignored for momentary relays", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].mode = RELAY_MODE_MOMENTARY;
    cfg.relays[0].pulse_ms = 5000;
    cfg.relays[0].max_on_s = 1;
    start();
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));
    clock_ms += 1000;
    relay_ctrl_tick();
    TEST_ASSERT_TRUE(relay_ctrl_get(1));  // the pulse decides, not the limit
    clock_ms += 4000;
    relay_ctrl_tick();
    TEST_ASSERT_FALSE(relay_ctrl_get(1));
    TEST_ASSERT_EQUAL(RELAY_SRC_PULSE_END, events[1].src);
}

TEST_CASE("max on-time: set or cleared while on applies from now", "[relay_ctrl]")
{
    fresh();
    start();
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));
    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));
    clock_ms += 3600 * 1000;
    cfg.relays[0].max_on_s = 30;  // set on a relay that has been on for an hour
    relay_ctrl_update_config(&cfg);
    relay_ctrl_tick();
    TEST_ASSERT_TRUE(relay_ctrl_get(1));
    clock_ms += 30000;
    relay_ctrl_tick();
    TEST_ASSERT_FALSE(relay_ctrl_get(1));

    cfg.relays[1].max_on_s = 30;
    relay_ctrl_update_config(&cfg);
    clock_ms += 20000;
    cfg.relays[1].max_on_s = 0;  // cleared before it ran out
    relay_ctrl_update_config(&cfg);
    clock_ms += 60000;
    relay_ctrl_tick();
    TEST_ASSERT_TRUE(relay_ctrl_get(2));
}

TEST_CASE("max on-time: a hold relay restored at boot gets a fresh timer", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].failsafe = FAILSAFE_HOLD;
    cfg.relays[0].max_on_s = 20;
    store.has_value = true;
    store.value = 0x01;
    start();
    TEST_ASSERT_TRUE(relay_ctrl_get(1));
    clock_ms += 19999;
    relay_ctrl_tick();
    TEST_ASSERT_TRUE(relay_ctrl_get(1));
    clock_ms += 1;
    relay_ctrl_tick();
    TEST_ASSERT_FALSE(relay_ctrl_get(1));
}

// --------------------------------------------------------- wiredNC (#13)

TEST_CASE("wiredNC translates every read path: reports the opposite of the coil", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].wired_nc = true;
    start();
    // Idle after a cold boot: coil off (de-energised), NC relay reports on.
    TEST_ASSERT_EQUAL_HEX8(0x00, chip.regs[TCA9554_REG_OUTPUT]);
    TEST_ASSERT_TRUE(relay_ctrl_get(1));
    TEST_ASSERT_EQUAL_HEX8(0x01, relay_ctrl_get_mask());

    // A plain (non-NC) relay on the same mask is unaffected.
    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));
    TEST_ASSERT_TRUE(relay_ctrl_get(2));
    TEST_ASSERT_EQUAL_HEX8(0x03, relay_ctrl_get_mask());  // relay 1 (NC, idle) + relay 2
}

TEST_CASE("wiredNC translates every write path: commanding on drives the coil off", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].wired_nc = true;
    start();
    TEST_ASSERT_TRUE(relay_ctrl_get(1));  // idle: reports on

    TEST_ESP_OK(relay_ctrl_set(1, false, RELAY_SRC_SK));  // "turn the load off"
    TEST_ASSERT_FALSE(relay_ctrl_get(1));
    TEST_ASSERT_EQUAL_HEX8(0x01, chip.regs[TCA9554_REG_OUTPUT]);  // coil energised

    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));  // "turn the load on"
    TEST_ASSERT_TRUE(relay_ctrl_get(1));
    TEST_ASSERT_EQUAL_HEX8(0x00, chip.regs[TCA9554_REG_OUTPUT]);  // coil de-energised
}

TEST_CASE("wiredNC: toggle flips the reported state and the coil together", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].wired_nc = true;
    start();
    TEST_ASSERT_TRUE(relay_ctrl_get(1));  // idle: on

    TEST_ESP_OK(relay_ctrl_toggle(1, RELAY_SRC_INPUT));
    TEST_ASSERT_FALSE(relay_ctrl_get(1));
    TEST_ASSERT_EQUAL_HEX8(0x01, chip.regs[TCA9554_REG_OUTPUT]);

    TEST_ESP_OK(relay_ctrl_toggle(1, RELAY_SRC_INPUT));
    TEST_ASSERT_TRUE(relay_ctrl_get(1));
    TEST_ASSERT_EQUAL_HEX8(0x00, chip.regs[TCA9554_REG_OUTPUT]);
}

TEST_CASE("wiredNC: listeners see the reported (logical) state, not the coil", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].wired_nc = true;
    start();
    n_events = 0;
    TEST_ESP_OK(relay_ctrl_set(1, false, RELAY_SRC_SK));  // coil goes on
    TEST_ASSERT_EQUAL(1, n_events);
    TEST_ASSERT_EQUAL(1, events[0].channel);
    TEST_ASSERT_FALSE(events[0].on);  // reported: load off, even though the coil energised
}

// ---------------------------------- the critical invariant: coil bypasses wiredNC

TEST_CASE("cold boot's coil write is unaffected by wiredNC", "[relay_ctrl]")
{
    for (int wnc = 0; wnc <= 1; wnc++) {
        fresh();
        cfg.relays[0].wired_nc = wnc;
        TEST_ESP_OK(relay_ctrl_init(&hw, &cfg));
        // Cold boot, default-safe: coil always ends up off, whatever wiredNC
        // says -- that's the fail-safe policy, matching an actual power loss.
        TEST_ASSERT_EQUAL_HEX8(0x00, chip.regs[TCA9554_REG_OUTPUT]);
        TEST_ASSERT_EQUAL_HEX8(0x00, chip.writes[0].val);
        // The report differs: a NC relay is powered by that de-energised coil.
        TEST_ASSERT_EQUAL(wnc ? true : false, relay_ctrl_get(1));
    }
}

TEST_CASE("SignalK-loss fail-safe writes the coil off directly, unaffected by wiredNC", "[relay_ctrl]")
{
    for (int wnc = 0; wnc <= 1; wnc++) {
        fresh();
        cfg.relays[0].wired_nc = wnc;
        start();
        // Drive the coil energised: whichever public request maps to that,
        // depending on wiredNC.
        TEST_ESP_OK(relay_ctrl_set(1, wnc ? false : true, RELAY_SRC_SK));
        TEST_ASSERT_EQUAL_HEX8(0x01, chip.regs[TCA9554_REG_OUTPUT]);
        n_events = 0;
        relay_ctrl_sk_lost();
        // The coil write itself: always de-energised, never translated.
        TEST_ASSERT_EQUAL_HEX8(0x00, chip.regs[TCA9554_REG_OUTPUT]);
        // What gets reported: on for a NC relay (its load is now powered).
        TEST_ASSERT_EQUAL(wnc ? true : false, relay_ctrl_get(1));
        TEST_ASSERT_EQUAL(1, n_events);
        TEST_ASSERT_EQUAL(wnc ? true : false, events[0].on);
    }
}

TEST_CASE("a momentary pulse's coil write is unaffected by wiredNC; only the report flips", "[relay_ctrl]")
{
    for (int wnc = 0; wnc <= 1; wnc++) {
        fresh();
        cfg.relays[0].mode = RELAY_MODE_MOMENTARY;
        cfg.relays[0].wired_nc = wnc;
        start();
        // The request that starts a pulse (coil energised) depends on
        // wiredNC; the coil-level pulse mechanics themselves do not.
        TEST_ESP_OK(relay_ctrl_set(1, wnc ? false : true, RELAY_SRC_SK));
        TEST_ASSERT_EQUAL_HEX8(0x01, chip.regs[TCA9554_REG_OUTPUT]);
        clock_ms += 999;
        relay_ctrl_tick();
        TEST_ASSERT_EQUAL_HEX8(0x01, chip.regs[TCA9554_REG_OUTPUT]);  // still mid-pulse
        clock_ms += 1;
        relay_ctrl_tick();
        // Pulse timer always switches the coil back off, unconditionally.
        TEST_ASSERT_EQUAL_HEX8(0x00, chip.regs[TCA9554_REG_OUTPUT]);
        // Report: a NC-wired momentary relay's pulse is a brief load-OFF
        // blip, ending back at load-on (idle, coil de-energised).
        TEST_ASSERT_EQUAL(wnc ? true : false, relay_ctrl_get(1));
    }
}

TEST_CASE("hold-restore reads/writes the coil bit unchanged; the report reflects wiredNC", "[relay_ctrl]")
{
    for (int wnc = 0; wnc <= 1; wnc++) {
        fresh();
        cfg.relays[0].failsafe = FAILSAFE_HOLD;
        cfg.relays[0].wired_nc = wnc;
        store.has_value = true;
        store.value = 0x01;  // relay 1 was held on (coil level, as stored)
        TEST_ESP_OK(relay_ctrl_init(&hw, &cfg));
        // The coil write restores exactly the stored bit, whatever wiredNC is.
        TEST_ASSERT_EQUAL_HEX8(0x01, chip.regs[TCA9554_REG_OUTPUT]);
        // The report differs: coil energised reads as off for a NC relay.
        TEST_ASSERT_EQUAL(wnc ? false : true, relay_ctrl_get(1));
    }
}

TEST_CASE("flipping wiredNC live changes the report without switching anything", "[relay_ctrl]")
{
    fresh();
    start();
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));  // coil on, reports on
    TEST_ASSERT_TRUE(relay_ctrl_get(1));
    TEST_ASSERT_EQUAL_HEX8(0x01, chip.regs[TCA9554_REG_OUTPUT]);
    chip.n_writes = 0;

    cfg.relays[0].wired_nc = true;
    relay_ctrl_update_config(&cfg);

    TEST_ASSERT_FALSE(relay_ctrl_get(1));                          // same coil, new meaning
    TEST_ASSERT_EQUAL_HEX8(0x01, chip.regs[TCA9554_REG_OUTPUT]);   // coil untouched
    TEST_ASSERT_EQUAL(0, chip.n_writes);                           // no I2C traffic at all
}

// ------------------------------------------------------- event chirps (#14)

TEST_CASE("event chirps: only direct commands, not automatic changes", "[relay_ctrl]")
{
    TEST_ASSERT_TRUE(relay_ctrl_source_chirps(RELAY_SRC_SK));
    TEST_ASSERT_TRUE(relay_ctrl_source_chirps(RELAY_SRC_N2K));
    TEST_ASSERT_TRUE(relay_ctrl_source_chirps(RELAY_SRC_WEB));
    TEST_ASSERT_TRUE(relay_ctrl_source_chirps(RELAY_SRC_INPUT));
    TEST_ASSERT_FALSE(relay_ctrl_source_chirps(RELAY_SRC_PULSE_END));
    TEST_ASSERT_FALSE(relay_ctrl_source_chirps(RELAY_SRC_FAILSAFE));
    TEST_ASSERT_FALSE(relay_ctrl_source_chirps(RELAY_SRC_MAX_ON));
}

TEST_CASE("is_momentary reports each relay's current mode", "[relay_ctrl]")
{
    fresh();
    cfg.relays[2].mode = RELAY_MODE_MOMENTARY;
    start();
    TEST_ASSERT_FALSE(relay_ctrl_is_momentary(1));
    TEST_ASSERT_TRUE(relay_ctrl_is_momentary(3));
    TEST_ASSERT_FALSE(relay_ctrl_is_momentary(0));
    TEST_ASSERT_FALSE(relay_ctrl_is_momentary(9));

    cfg.relays[2].mode = RELAY_MODE_LATCHING;
    relay_ctrl_update_config(&cfg);
    TEST_ASSERT_FALSE(relay_ctrl_is_momentary(3));
}

// --------------------------------------------------- interlock (issue #8)

static espos_health_state_t interlock_health(void)
{
    espos_health_condition_t c[8];
    size_t n = espos_health_snapshot(c, 8);
    for (size_t i = 0; i < n && i < 8; i++) {
        if (strcmp(c[i].key, "interlockBothOn") == 0) {
            return c[i].state;
        }
    }
    return ESPOS_HEALTH_NORMAL;
}

// No single expander write may ever set both of a pair's bits.
static void assert_never_both(uint8_t bit_a, uint8_t bit_b)
{
    const uint8_t both = bit_a | bit_b;
    for (int i = 0; i < chip.n_writes; i++) {
        if (chip.writes[i].reg == TCA9554_REG_OUTPUT) {
            TEST_ASSERT_NOT_EQUAL(both, chip.writes[i].val & both);
        }
    }
}

TEST_CASE("interlock: on to a relay kills its partner's coil now and pends its own", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].interlock = 2;
    cfg.relays[1].interlock = 1;
    cfg.interlock_dead_ms = 50;
    start();

    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));
    TEST_ASSERT_EQUAL_HEX8(0x02, relay_ctrl_get_mask());
    n_events = 0;

    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_WEB));
    TEST_ASSERT_EQUAL_HEX8(0x00, relay_ctrl_get_mask());  // relay 2 off now, relay 1 not yet on
    TEST_ASSERT_FALSE(relay_ctrl_get(1));
    TEST_ASSERT_FALSE(relay_ctrl_get(2));

    clock_ms += 49;
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL_HEX8(0x00, relay_ctrl_get_mask());  // dead time not up yet

    clock_ms += 1;  // 50ms elapsed
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL_HEX8(0x01, relay_ctrl_get_mask());  // relay 1 on now

    assert_never_both(0x01, 0x02);

    // Relay 2 off, then relay 1 on, both with the original command's source.
    TEST_ASSERT_EQUAL(2, n_events);
    TEST_ASSERT_EQUAL(2, events[0].channel);
    TEST_ASSERT_FALSE(events[0].on);
    TEST_ASSERT_EQUAL(RELAY_SRC_WEB, events[0].src);
    TEST_ASSERT_EQUAL(1, events[1].channel);
    TEST_ASSERT_TRUE(events[1].on);
    TEST_ASSERT_EQUAL(RELAY_SRC_WEB, events[1].src);
}

TEST_CASE("interlock: a repeated on while pending is a no-op", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].interlock = 2;
    cfg.relays[1].interlock = 1;
    cfg.interlock_dead_ms = 50;
    start();

    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));  // kills 2, pends 1
    TEST_ASSERT_EQUAL_HEX8(0x00, relay_ctrl_get_mask());

    clock_ms += 20;
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));  // repeat: still waiting, no restart
    TEST_ASSERT_EQUAL_HEX8(0x00, relay_ctrl_get_mask());

    clock_ms += 29;  // 49ms since the original "on"
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL_HEX8(0x00, relay_ctrl_get_mask());  // not yet -- the repeat didn't restart it

    clock_ms += 1;  // 50ms since the original "on"
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL_HEX8(0x01, relay_ctrl_get_mask());
}

TEST_CASE("interlock: an off to the pending relay cancels it", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].interlock = 2;
    cfg.relays[1].interlock = 1;
    cfg.interlock_dead_ms = 50;
    start();

    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));  // kills 2, pends 1
    TEST_ESP_OK(relay_ctrl_set(1, false, RELAY_SRC_SK));  // cancel 1's own pending

    clock_ms += 100;
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL_HEX8(0x00, relay_ctrl_get_mask());  // relay 1 never came on
}

TEST_CASE("interlock: an on to the partner cancels a relay's pending-on", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].interlock = 2;
    cfg.relays[1].interlock = 1;
    cfg.interlock_dead_ms = 50;
    start();

    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));  // kills 2, pends 1
    TEST_ASSERT_EQUAL_HEX8(0x00, relay_ctrl_get_mask());

    // Relay 2 isn't on yet (relay 1 is still only pending): its own "on"
    // proceeds immediately, and that cancels relay 1's pending.
    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));
    TEST_ASSERT_EQUAL_HEX8(0x02, relay_ctrl_get_mask());

    clock_ms += 100;
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL_HEX8(0x02, relay_ctrl_get_mask());  // relay 1 never came on
    assert_never_both(0x01, 0x02);
}

TEST_CASE("interlock: a fired pending-on starts a momentary relay's pulse from when its coil actually turns on",
          "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].mode = RELAY_MODE_MOMENTARY;
    cfg.relays[0].pulse_ms = 1000;
    cfg.relays[0].interlock = 2;
    cfg.relays[1].interlock = 1;
    cfg.interlock_dead_ms = 50;
    start();

    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));  // kills 2, pends 1 (a pulse relay)

    clock_ms += 50;
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL_HEX8(0x01, relay_ctrl_get_mask());  // relay 1's coil energizes now

    // If the pulse had wrongly started counting from the original command
    // (50ms before the coil actually turned on), it would already have
    // ended by now. It must still be on: the pulse starts from this tick.
    clock_ms += 999;
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL_HEX8(0x01, relay_ctrl_get_mask());

    clock_ms += 2;  // 1001ms since the coil turned on
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL_HEX8(0x00, relay_ctrl_get_mask());  // pulse over
}

TEST_CASE("interlock: SignalK loss cancels a pending-on for a default-safe relay, not a hold one", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].interlock = 2;  // default-safe (fresh()'s default)
    cfg.relays[1].interlock = 1;
    cfg.relays[2].interlock = 4;
    cfg.relays[2].failsafe = FAILSAFE_HOLD;
    cfg.relays[3].interlock = 3;
    cfg.relays[3].failsafe = FAILSAFE_HOLD;
    cfg.interlock_dead_ms = 50;
    start();

    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));  // kills 2, pends 1 (default-safe)
    TEST_ESP_OK(relay_ctrl_set(4, true, RELAY_SRC_SK));
    TEST_ESP_OK(relay_ctrl_set(3, true, RELAY_SRC_SK));  // kills 4, pends 3 (hold)

    relay_ctrl_sk_lost();

    clock_ms += 100;
    relay_ctrl_tick();
    TEST_ASSERT_FALSE(relay_ctrl_get(1));  // default-safe: pending cancelled, never came on
    TEST_ASSERT_TRUE(relay_ctrl_get(3));   // hold: fail-safe doesn't touch it, still fires
}

TEST_CASE("interlock: boot restores neither side of a both-on interlocked pair", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].failsafe = FAILSAFE_HOLD;
    cfg.relays[1].failsafe = FAILSAFE_HOLD;
    cfg.relays[0].interlock = 2;
    cfg.relays[1].interlock = 1;
    store.has_value = true;
    store.value = 0x03;  // relays 1 and 2 both stored on -- shouldn't happen, but did
    TEST_ESP_OK(relay_ctrl_init(&hw, &cfg));
    TEST_ASSERT_EQUAL_HEX8(0x00, relay_ctrl_get_mask());  // neither restored
    TEST_ASSERT_EQUAL(ESPOS_HEALTH_WARN, interlock_health());
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL_HEX8(0x00, store.value);  // the corrected (safe) state is persisted
}

TEST_CASE("interlock: a config change finding both interlocked coils on switches both off and warns",
          "[relay_ctrl]")
{
    fresh();
    start();
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_SK));
    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_SK));  // no interlock yet: both on
    TEST_ASSERT_EQUAL_HEX8(0x03, relay_ctrl_get_mask());

    n_events = 0;
    cfg.relays[0].interlock = 2;  // now pair them up, while both are on
    cfg.relays[1].interlock = 1;
    relay_ctrl_update_config(&cfg);
    TEST_ASSERT_EQUAL_HEX8(0x00, relay_ctrl_get_mask());
    TEST_ASSERT_EQUAL(ESPOS_HEALTH_WARN, interlock_health());
    TEST_ASSERT_EQUAL(2, n_events);
    TEST_ASSERT_EQUAL(RELAY_SRC_INTERLOCK, events[0].src);
    TEST_ASSERT_FALSE(events[0].on);
    TEST_ASSERT_FALSE(events[1].on);

    // A later, unrelated config save with nothing wrong clears the warning.
    relay_ctrl_update_config(&cfg);
    TEST_ASSERT_EQUAL(ESPOS_HEALTH_NORMAL, interlock_health());
}

// Decided 2026-09-28: two interlocked relays' override inputs both reading
// "on" at boot is left to the normal live enforcement (kill the partner,
// delay this one) rather than a dedicated "restore neither" path -- the
// safety invariant (never both coils energized together) holds either way,
// only one relay briefly flickers on and off before the other settles.
TEST_CASE("interlock: input overrides get the same enforcement as any other source", "[relay_ctrl]")
{
    fresh();
    cfg.relays[0].interlock = 2;
    cfg.relays[1].interlock = 1;
    cfg.interlock_dead_ms = 50;
    start();

    // Both override inputs read "on" at boot; input_sense applies them in
    // channel order.
    TEST_ESP_OK(relay_ctrl_set(1, true, RELAY_SRC_INPUT));
    TEST_ASSERT_EQUAL_HEX8(0x01, relay_ctrl_get_mask());  // no conflict yet: on immediately

    TEST_ESP_OK(relay_ctrl_set(2, true, RELAY_SRC_INPUT));
    TEST_ASSERT_EQUAL_HEX8(0x00, relay_ctrl_get_mask());  // relay 1 killed, relay 2 pending

    clock_ms += 50;
    relay_ctrl_tick();
    TEST_ASSERT_EQUAL_HEX8(0x02, relay_ctrl_get_mask());  // relay 2 settles on
    assert_never_both(0x01, 0x02);
    TEST_ASSERT_EQUAL(RELAY_SRC_INPUT, events[n_events - 1].src);
}

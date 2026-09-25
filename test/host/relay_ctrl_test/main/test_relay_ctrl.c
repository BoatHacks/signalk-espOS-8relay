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

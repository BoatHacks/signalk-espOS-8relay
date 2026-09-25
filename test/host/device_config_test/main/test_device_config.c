#include <string.h>

#include "device_config.h"
#include "espos_config.h"
#include "espos_config_backend.h"
#include "espos_health.h"
#include "unity.h"

static espos_config_mem_t *s_mem;

static void store_up(void)
{
    s_mem = espos_config_mem_create();
    TEST_ASSERT_NOT_NULL(s_mem);
    TEST_ESP_OK(espos_config_init(espos_config_backend_mem(), s_mem));
    espos_health_reset();
}

static void store_down(void)
{
    espos_config_deinit();
    espos_config_mem_destroy(s_mem);
}

static espos_health_state_t health_of(const char *key)
{
    espos_health_condition_t c[8];
    size_t n = espos_health_snapshot(c, 8);
    for (size_t i = 0; i < n && i < 8; i++) {
        if (strcmp(c[i].key, key) == 0) {
            return c[i].state;
        }
    }
    return ESPOS_HEALTH_NORMAL;
}

TEST_CASE("defaults match SPEC.md section 9", "[device_config]")
{
    store_up();
    device_config_t c;
    TEST_ESP_OK(device_config_load(&c));

    TEST_ASSERT_EQUAL(0, c.bank_id);
    TEST_ASSERT_EQUAL(1, c.input_bank_id);
    TEST_ASSERT_EQUAL(50, c.debounce_ms);
    TEST_ASSERT_EQUAL(30, c.sk_loss_grace_s);
    TEST_ASSERT_EQUAL(10, c.sk_republish_s);
    TEST_ASSERT_TRUE(c.eth_enabled);
    TEST_ASSERT_TRUE(c.publish_switches_tree);
    TEST_ASSERT_FALSE(c.publish_controls_tree);
    TEST_ASSERT_EQUAL(10, c.led_brightness);
    TEST_ASSERT_FALSE(c.buzzer_on_alarm);
    TEST_ASSERT_EQUAL_STRING("Relay 1", c.relays[0].name);
    TEST_ASSERT_EQUAL_STRING("Relay 8", c.relays[7].name);
    TEST_ASSERT_EQUAL_STRING("Input 8", c.inputs[7].name);
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        TEST_ASSERT_EQUAL(RELAY_MODE_LATCHING, c.relays[i].mode);
        TEST_ASSERT_EQUAL(1000, c.relays[i].pulse_ms);
        TEST_ASSERT_EQUAL(FAILSAFE_DEFAULT_SAFE, c.relays[i].failsafe);
        TEST_ASSERT_EQUAL(0, c.relays[i].override_di);
        TEST_ASSERT_FALSE(c.inputs[i].invert);
    }
    store_down();
}

TEST_CASE("stored values are read into the right channel", "[device_config]")
{
    store_up();
    TEST_ESP_OK(espos_config_set_i32("swbank", "bank_id", 12));
    TEST_ESP_OK(espos_config_set_str("swbank", "relay3_name", "Bilge pump"));
    TEST_ESP_OK(espos_config_set_str("swbank", "relay3_mode", "momentary"));
    TEST_ESP_OK(espos_config_set_i32("swbank", "relay3_pulse_ms", 250));
    TEST_ESP_OK(espos_config_set_str("swbank", "relay5_failsafe", "hold"));
    TEST_ESP_OK(espos_config_set_i32("swbank", "relay5_override", 2));
    TEST_ESP_OK(espos_config_set_bool("swbank", "input2_invert", true));

    device_config_t c;
    TEST_ESP_OK(device_config_load(&c));
    TEST_ASSERT_EQUAL(12, c.bank_id);
    TEST_ASSERT_EQUAL_STRING("Bilge pump", c.relays[2].name);
    TEST_ASSERT_EQUAL(RELAY_MODE_MOMENTARY, c.relays[2].mode);
    TEST_ASSERT_EQUAL(250, c.relays[2].pulse_ms);
    TEST_ASSERT_EQUAL(FAILSAFE_HOLD, c.relays[4].failsafe);
    TEST_ASSERT_EQUAL(2, c.relays[4].override_di);
    TEST_ASSERT_TRUE(c.inputs[1].invert);
    TEST_ASSERT_FALSE(c.inputs[0].invert);
    store_down();
}

TEST_CASE("the store rejects out-of-range values", "[device_config]")
{
    store_up();
    TEST_ASSERT_NOT_EQUAL(ESP_OK, espos_config_set_i32("swbank", "bank_id", 253));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, espos_config_set_i32("swbank", "relay1_override", 9));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, espos_config_set_str("swbank", "relay1_mode", "toggle"));
    store_down();
}

TEST_CASE("momentary relays are always default-safe", "[device_config]")
{
    relay_cfg_t r = {.mode = RELAY_MODE_MOMENTARY, .failsafe = FAILSAFE_HOLD};
    TEST_ASSERT_EQUAL(FAILSAFE_DEFAULT_SAFE, device_config_effective_failsafe(&r));
    r.mode = RELAY_MODE_LATCHING;
    TEST_ASSERT_EQUAL(FAILSAFE_HOLD, device_config_effective_failsafe(&r));
    r.failsafe = FAILSAFE_DEFAULT_SAFE;
    TEST_ASSERT_EQUAL(FAILSAFE_DEFAULT_SAFE, device_config_effective_failsafe(&r));
}

TEST_CASE("clashing bank ids raise a warning, fixing them clears it", "[device_config]")
{
    store_up();
    device_config_t c;
    TEST_ESP_OK(espos_config_set_i32("swbank", "input_bank_id", 0));
    TEST_ESP_OK(device_config_load(&c));
    TEST_ASSERT_FALSE(device_config_input_bank_usable(&c));
    device_config_report_health(&c);
    TEST_ASSERT_EQUAL(ESPOS_HEALTH_WARN, health_of("bankIdClash"));

    TEST_ESP_OK(espos_config_set_i32("swbank", "input_bank_id", 1));
    TEST_ESP_OK(device_config_load(&c));
    TEST_ASSERT_TRUE(device_config_input_bank_usable(&c));
    device_config_report_health(&c);
    TEST_ASSERT_EQUAL(ESPOS_HEALTH_NORMAL, health_of("bankIdClash"));
    store_down();
}

TEST_CASE("load fails before the store is up", "[device_config]")
{
    device_config_t c;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, device_config_load(&c));
}

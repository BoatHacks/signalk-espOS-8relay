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
    TEST_ASSERT_EQUAL(100, c.interlock_dead_ms);
    TEST_ASSERT_FALSE(c.buzzer_on_alarm);
    TEST_ASSERT_EQUAL(2700, c.buzzer_freq_hz);
    TEST_ASSERT_FALSE(c.buzzer_on_event);
    TEST_ASSERT_EQUAL_STRING("boot", c.boot_tone);
    TEST_ASSERT_EQUAL_STRING("portal", c.portal_tone);
    TEST_ASSERT_EQUAL_STRING("reset", c.factory_reset_tone);
    TEST_ASSERT_NOT_NULL(strstr(c.tone_patterns, "\"boot\""));
    TEST_ASSERT_EQUAL_STRING("Relay 1", c.relays[0].name);
    TEST_ASSERT_EQUAL_STRING("Relay 8", c.relays[7].name);
    TEST_ASSERT_EQUAL_STRING("Input 8", c.inputs[7].name);
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        TEST_ASSERT_EQUAL(RELAY_MODE_LATCHING, c.relays[i].mode);
        TEST_ASSERT_EQUAL(1000, c.relays[i].pulse_ms);
        TEST_ASSERT_EQUAL(FAILSAFE_DEFAULT_SAFE, c.relays[i].failsafe);
        TEST_ASSERT_EQUAL(0, c.relays[i].override_di);
        TEST_ASSERT_EQUAL(INPUT_LINK_FOLLOW, c.relays[i].link);
        TEST_ASSERT_EQUAL(0, c.relays[i].max_on_s);
        TEST_ASSERT_EQUAL(0, c.relays[i].interlock);
        TEST_ASSERT_FALSE(c.inputs[i].invert);
        TEST_ASSERT_EQUAL_STRING("relay-on", c.relays[i].on_tone);
        TEST_ASSERT_EQUAL_STRING("relay-off", c.relays[i].off_tone);
        TEST_ASSERT_EQUAL_STRING("pulse-start", c.relays[i].pulse_start_tone);
        TEST_ASSERT_EQUAL_STRING("pulse-stop", c.relays[i].pulse_stop_tone);
        TEST_ASSERT_EQUAL_STRING("input", c.inputs[i].on_tone);
        TEST_ASSERT_EQUAL_STRING("input", c.inputs[i].off_tone);
        TEST_ASSERT_EQUAL(INPUT_ALARM_OFF, c.inputs[i].alarm);
        TEST_ASSERT_EQUAL_STRING("", c.inputs[i].alarm_msg);
    }
    TEST_ASSERT_EQUAL(0, c.interlock_invalid);
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
    TEST_ESP_OK(espos_config_set_str("swbank", "relay5_link", "toggle"));
    TEST_ESP_OK(espos_config_set_i32("swbank", "relay6_max_on_s", 1800));
    TEST_ESP_OK(espos_config_set_i32("swbank", "interlock_dead", 250));
    TEST_ESP_OK(espos_config_set_i32("swbank", "buzzer_freq_hz", 4000));
    TEST_ESP_OK(espos_config_set_bool("swbank", "buzzer_event", true));
    TEST_ESP_OK(espos_config_set_str("swbank", "boot_tone", "custom-boot"));
    TEST_ESP_OK(espos_config_set_str("swbank", "relay3_on_tone", "custom-on"));
    TEST_ESP_OK(espos_config_set_str("swbank", "relay3_off_tone", ""));
    TEST_ESP_OK(espos_config_set_str("swbank", "relay3_ps_tone", "custom-pulse-start"));
    TEST_ESP_OK(espos_config_set_str("swbank", "relay3_pe_tone", "custom-pulse-stop"));
    TEST_ESP_OK(espos_config_set_str("swbank", "input2_on_tone", "custom-input-on"));
    TEST_ESP_OK(espos_config_set_str("swbank", "input2_off_tone", "custom-input-off"));
    TEST_ESP_OK(espos_config_set_str("swbank", "input4_alarm", "alarm"));
    TEST_ESP_OK(espos_config_set_str("swbank", "input4_alm_msg", "Bilge water high"));
    TEST_ESP_OK(espos_config_set_str("swbank", "input7_alarm", "emergency"));

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
    TEST_ASSERT_EQUAL(INPUT_LINK_TOGGLE, c.relays[4].link);
    TEST_ASSERT_EQUAL(INPUT_LINK_FOLLOW, c.relays[5].link);
    TEST_ASSERT_EQUAL(1800, c.relays[5].max_on_s);
    TEST_ASSERT_EQUAL(250, c.interlock_dead_ms);
    TEST_ASSERT_EQUAL(4000, c.buzzer_freq_hz);
    TEST_ASSERT_TRUE(c.buzzer_on_event);
    TEST_ASSERT_EQUAL_STRING("custom-boot", c.boot_tone);
    TEST_ASSERT_EQUAL_STRING("custom-on", c.relays[2].on_tone);
    TEST_ASSERT_EQUAL_STRING("", c.relays[2].off_tone);
    TEST_ASSERT_EQUAL_STRING("custom-pulse-start", c.relays[2].pulse_start_tone);
    TEST_ASSERT_EQUAL_STRING("custom-pulse-stop", c.relays[2].pulse_stop_tone);
    TEST_ASSERT_EQUAL_STRING("custom-input-on", c.inputs[1].on_tone);
    TEST_ASSERT_EQUAL_STRING("custom-input-off", c.inputs[1].off_tone);
    TEST_ASSERT_EQUAL(INPUT_ALARM_ALARM, c.inputs[3].alarm);
    TEST_ASSERT_EQUAL_STRING("Bilge water high", c.inputs[3].alarm_msg);
    TEST_ASSERT_EQUAL(INPUT_ALARM_EMERGENCY, c.inputs[6].alarm);
    TEST_ASSERT_EQUAL(INPUT_ALARM_OFF, c.inputs[0].alarm);
    store_down();
}

TEST_CASE("the store rejects an invalid alarm level", "[device_config]")
{
    store_up();
    TEST_ASSERT_NOT_EQUAL(ESP_OK, espos_config_set_str("swbank", "input1_alarm", "critical"));
    store_down();
}

TEST_CASE("the store rejects out-of-range values", "[device_config]")
{
    store_up();
    TEST_ASSERT_NOT_EQUAL(ESP_OK, espos_config_set_i32("swbank", "bank_id", 253));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, espos_config_set_i32("swbank", "relay1_override", 9));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, espos_config_set_str("swbank", "relay1_mode", "toggle"));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, espos_config_set_str("swbank", "relay1_link", "latch"));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, espos_config_set_i32("swbank", "relay1_max_on_s", 86401));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, espos_config_set_i32("swbank", "r1_interlock", 9));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, espos_config_set_i32("swbank", "interlock_dead", 2001));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, espos_config_set_i32("swbank", "buzzer_freq_hz", 41));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, espos_config_set_i32("swbank", "buzzer_freq_hz", 10001));
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

TEST_CASE("interlock: a pair that reciprocates is the effective value", "[device_config]")
{
    store_up();
    TEST_ESP_OK(espos_config_set_i32("swbank", "r1_interlock", 2));
    TEST_ESP_OK(espos_config_set_i32("swbank", "r2_interlock", 1));
    device_config_t c;
    TEST_ESP_OK(device_config_load(&c));
    TEST_ASSERT_EQUAL(2, c.relays[0].interlock);
    TEST_ASSERT_EQUAL(1, c.relays[1].interlock);
    for (int i = 2; i < BOARD_CHANNELS; i++) {
        TEST_ASSERT_EQUAL(0, c.relays[i].interlock);
    }
    TEST_ASSERT_EQUAL(0, c.interlock_invalid);
    store_down();
}

TEST_CASE("interlock: a one-sided or self-referencing setting is ignored", "[device_config]")
{
    store_up();
    // Relay 3 names relay 4, but relay 4 doesn't name relay 3 back: one-sided.
    TEST_ESP_OK(espos_config_set_i32("swbank", "r3_interlock", 4));
    // Relay 6 names itself.
    TEST_ESP_OK(espos_config_set_i32("swbank", "r6_interlock", 6));
    device_config_t c;
    TEST_ESP_OK(device_config_load(&c));
    TEST_ASSERT_EQUAL(0, c.relays[2].interlock);  // relay 3: ignored
    TEST_ASSERT_EQUAL(0, c.relays[3].interlock);  // relay 4: never set one, nothing to ignore
    TEST_ASSERT_EQUAL(0, c.relays[5].interlock);  // relay 6: ignored (self)
    // Only the side that actually named a (bad) partner is flagged.
    TEST_ASSERT_EQUAL((1u << 2) | (1u << 5), c.interlock_invalid);
    store_down();
}

TEST_CASE("interlock: a bad setting raises a warning, fixing it clears it", "[device_config]")
{
    store_up();
    device_config_t c;
    TEST_ESP_OK(espos_config_set_i32("swbank", "r1_interlock", 1));  // self
    TEST_ESP_OK(device_config_load(&c));
    device_config_report_health(&c);
    TEST_ASSERT_EQUAL(ESPOS_HEALTH_WARN, health_of("interlockInvalid"));

    TEST_ESP_OK(espos_config_set_i32("swbank", "r1_interlock", 0));  // fixed: no partner named
    TEST_ESP_OK(device_config_load(&c));
    device_config_report_health(&c);
    TEST_ASSERT_EQUAL(ESPOS_HEALTH_NORMAL, health_of("interlockInvalid"));
    store_down();
}

TEST_CASE("schedule: defaults are all unused", "[device_config]")
{
    store_up();
    device_config_t c;
    TEST_ESP_OK(device_config_load(&c));
    for (int i = 0; i < SCHEDULE_MAX_ENTRIES; i++) {
        TEST_ASSERT_EQUAL(0, c.schedules[i].relay);
    }
    TEST_ASSERT_EQUAL(0, c.schedule_relay_conflict);
    store_down();
}

TEST_CASE("schedule: clock mode parses HH:MM on/off and a days bitmask", "[device_config]")
{
    store_up();
    TEST_ESP_OK(espos_config_set_i32("swbank", "s1_relay", 3));
    TEST_ESP_OK(espos_config_set_str("swbank", "s1_on", "18:30"));
    TEST_ESP_OK(espos_config_set_str("swbank", "s1_off", "06:00"));
    TEST_ESP_OK(espos_config_set_i32("swbank", "s1_days", 62));  // Mon-Fri
    device_config_t c;
    TEST_ESP_OK(device_config_load(&c));
    const schedule_cfg_t *sc = &c.schedules[0];
    TEST_ASSERT_EQUAL(3, sc->relay);
    TEST_ASSERT_EQUAL(SCHEDULE_MODE_CLOCK, sc->mode);
    TEST_ASSERT_EQUAL(SCHEDULE_TIME_CLOCK, sc->on.kind);
    TEST_ASSERT_EQUAL(18 * 60 + 30, sc->on.minute_of_day);
    TEST_ASSERT_EQUAL(SCHEDULE_TIME_CLOCK, sc->off.kind);
    TEST_ASSERT_EQUAL(6 * 60, sc->off.minute_of_day);
    TEST_ASSERT_EQUAL(62, sc->days);
    store_down();
}

TEST_CASE("schedule: sunrise/sunset with a signed minute offset parses", "[device_config]")
{
    store_up();
    TEST_ESP_OK(espos_config_set_i32("swbank", "s2_relay", 1));
    TEST_ESP_OK(espos_config_set_str("swbank", "s2_on", "sunset-30"));
    TEST_ESP_OK(espos_config_set_str("swbank", "s2_off", "sunrise+15m"));
    device_config_t c;
    TEST_ESP_OK(device_config_load(&c));
    const schedule_cfg_t *sc = &c.schedules[1];
    TEST_ASSERT_EQUAL(1, sc->relay);
    TEST_ASSERT_EQUAL(SCHEDULE_TIME_SUNSET, sc->on.kind);
    TEST_ASSERT_EQUAL(-30, sc->on.offset_min);
    TEST_ASSERT_EQUAL(SCHEDULE_TIME_SUNRISE, sc->off.kind);
    TEST_ASSERT_EQUAL(15, sc->off.offset_min);
    store_down();
}

TEST_CASE("schedule: a bare \"sunrise\"/\"sunset\" is offset 0", "[device_config]")
{
    store_up();
    TEST_ESP_OK(espos_config_set_i32("swbank", "s1_relay", 1));
    TEST_ESP_OK(espos_config_set_str("swbank", "s1_on", "sunrise"));
    TEST_ESP_OK(espos_config_set_str("swbank", "s1_off", "sunset"));
    device_config_t c;
    TEST_ESP_OK(device_config_load(&c));
    TEST_ASSERT_EQUAL(0, c.schedules[0].on.offset_min);
    TEST_ASSERT_EQUAL(0, c.schedules[0].off.offset_min);
    store_down();
}

TEST_CASE("schedule: repeat mode parses on-minutes and period-minutes", "[device_config]")
{
    store_up();
    TEST_ESP_OK(espos_config_set_i32("swbank", "s3_relay", 5));
    TEST_ESP_OK(espos_config_set_str("swbank", "s3_mode", "repeat"));
    TEST_ESP_OK(espos_config_set_str("swbank", "s3_on", "10"));
    TEST_ESP_OK(espos_config_set_str("swbank", "s3_off", "60"));
    device_config_t c;
    TEST_ESP_OK(device_config_load(&c));
    const schedule_cfg_t *sc = &c.schedules[2];
    TEST_ASSERT_EQUAL(5, sc->relay);
    TEST_ASSERT_EQUAL(SCHEDULE_MODE_REPEAT, sc->mode);
    TEST_ASSERT_EQUAL(10, sc->on_min);
    TEST_ASSERT_EQUAL(60, sc->period_min);
    store_down();
}

TEST_CASE("schedule: repeat mode with on-minutes >= period is disabled", "[device_config]")
{
    store_up();
    TEST_ESP_OK(espos_config_set_i32("swbank", "s4_relay", 2));
    TEST_ESP_OK(espos_config_set_str("swbank", "s4_mode", "repeat"));
    TEST_ESP_OK(espos_config_set_str("swbank", "s4_on", "60"));
    TEST_ESP_OK(espos_config_set_str("swbank", "s4_off", "60"));  // not longer than on: invalid
    device_config_t c;
    TEST_ESP_OK(device_config_load(&c));
    TEST_ASSERT_EQUAL(0, c.schedules[3].relay);
    store_down();
}

TEST_CASE("schedule: a malformed clock time disables the entry", "[device_config]")
{
    store_up();
    TEST_ESP_OK(espos_config_set_i32("swbank", "s5_relay", 1));
    TEST_ESP_OK(espos_config_set_str("swbank", "s5_on", "25:00"));  // no such hour
    TEST_ESP_OK(espos_config_set_str("swbank", "s5_off", "06:00"));
    device_config_t c;
    TEST_ESP_OK(device_config_load(&c));
    TEST_ASSERT_EQUAL(0, c.schedules[4].relay);
    store_down();
}

TEST_CASE("schedule: two entries naming the same relay are both disabled, with a warning", "[device_config]")
{
    store_up();
    TEST_ESP_OK(espos_config_set_i32("swbank", "s6_relay", 4));
    TEST_ESP_OK(espos_config_set_str("swbank", "s6_on", "08:00"));
    TEST_ESP_OK(espos_config_set_str("swbank", "s6_off", "09:00"));
    TEST_ESP_OK(espos_config_set_i32("swbank", "s7_relay", 4));
    TEST_ESP_OK(espos_config_set_str("swbank", "s7_on", "20:00"));
    TEST_ESP_OK(espos_config_set_str("swbank", "s7_off", "21:00"));
    device_config_t c;
    TEST_ESP_OK(device_config_load(&c));
    TEST_ASSERT_EQUAL(0, c.schedules[5].relay);  // s6: disabled
    TEST_ASSERT_EQUAL(0, c.schedules[6].relay);  // s7: disabled
    TEST_ASSERT_EQUAL(1u << 3, c.schedule_relay_conflict);  // relay 4
    device_config_report_health(&c);
    TEST_ASSERT_EQUAL(ESPOS_HEALTH_WARN, health_of("scheduleOverlap"));

    // Fixed: move s7 to a different relay.
    TEST_ESP_OK(espos_config_set_i32("swbank", "s7_relay", 2));
    TEST_ESP_OK(device_config_load(&c));
    TEST_ASSERT_EQUAL(4, c.schedules[5].relay);
    TEST_ASSERT_EQUAL(2, c.schedules[6].relay);
    TEST_ASSERT_EQUAL(0, c.schedule_relay_conflict);
    device_config_report_health(&c);
    TEST_ASSERT_EQUAL(ESPOS_HEALTH_NORMAL, health_of("scheduleOverlap"));
    store_down();
}

TEST_CASE("schedule: an unused entry's on/off is never parsed", "[device_config]")
{
    store_up();
    // relay left at its default (0, unused); on/off are garbage that would
    // fail to parse if it were ever tried.
    TEST_ESP_OK(espos_config_set_str("swbank", "s8_on", "not a time"));
    TEST_ESP_OK(espos_config_set_str("swbank", "s8_off", "also not a time"));
    device_config_t c;
    TEST_ESP_OK(device_config_load(&c));
    TEST_ASSERT_EQUAL(0, c.schedules[7].relay);
    store_down();
}

TEST_CASE("load fails before the store is up", "[device_config]")
{
    device_config_t c;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, device_config_load(&c));
}

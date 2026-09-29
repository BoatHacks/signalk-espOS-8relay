#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "unity.h"
#include "web_ui_logic.h"

static device_config_t cfg;

static void fresh(void)
{
    memset(&cfg, 0, sizeof(cfg));
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        snprintf(cfg.relays[i].name, sizeof(cfg.relays[i].name), "Relay %d", i + 1);
        snprintf(cfg.inputs[i].name, sizeof(cfg.inputs[i].name), "Input %d", i + 1);
    }
}

static cJSON *state(uint8_t relays, uint8_t inputs, bool ready)
{
    const web_ui_view_t v = {.cfg = &cfg, .relay_mask = relays, .input_mask = inputs, .inputs_ready = ready};
    char *json = web_ui_state_json(&v);
    TEST_ASSERT_NOT_NULL(json);
    cJSON *root = cJSON_Parse(json);
    free(json);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, "not valid JSON");
    return root;
}

static cJSON *item(cJSON *root, const char *array, int index)
{
    cJSON *a = cJSON_GetObjectItem(root, array);
    TEST_ASSERT_EQUAL(BOARD_CHANNELS, cJSON_GetArraySize(a));
    return cJSON_GetArrayItem(a, index);
}

TEST_CASE("state: relay and input bits map to channels 1-8", "[web_ui]")
{
    fresh();
    cJSON *root = state(0x81, 0x02, true);  // relays 1 and 8, input 2
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(item(root, "relays", 0), "on")));
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(item(root, "relays", 1), "on")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(item(root, "relays", 7), "on")));
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(item(root, "inputs", 0), "on")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(item(root, "inputs", 1), "on")));
    TEST_ASSERT_EQUAL(8, cJSON_GetObjectItem(item(root, "relays", 7), "channel")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "inputsReady")));
    cJSON_Delete(root);
}

TEST_CASE("state: an input's alarm marker follows its setting and level", "[web_ui]")
{
    fresh();
    cfg.inputs[1].alarm = INPUT_ALARM_ALARM;  // input 2
    cJSON *root = state(0, 0x06, true);       // inputs 2 and 3 on, alarm only set on 2
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(item(root, "inputs", 1), "alarm")));
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(item(root, "inputs", 2), "alarm")));  // no alarm setting
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(item(root, "inputs", 0), "alarm")));  // off
    cJSON_Delete(root);

    root = state(0, 0x00, true);  // input 2 now off: no longer alarming
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(item(root, "inputs", 1), "alarm")));
    cJSON_Delete(root);

    root = state(0, 0x02, false);  // not settled: never shown as alarming
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(item(root, "inputs", 1), "alarm")));
    cJSON_Delete(root);
}

TEST_CASE("state: inputs are null until they have settled", "[web_ui]")
{
    fresh();
    cJSON *root = state(0, 0xFF, false);
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(item(root, "inputs", i), "on")));
    }
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(root, "inputsReady")));
    cJSON_Delete(root);
}

TEST_CASE("state: names are escaped; mode and input link reported", "[web_ui]")
{
    fresh();
    strcpy(cfg.relays[2].name, "Deck \"light\" \\ <b>");
    cfg.relays[2].mode = RELAY_MODE_MOMENTARY;
    cfg.relays[2].override_di = 5;
    cfg.relays[2].link = INPUT_LINK_TOGGLE;
    cfg.relays[2].max_on_s = 600;  // momentary: reported as no limit
    cfg.relays[1].max_on_s = 1800;
    cJSON *root = state(0, 0, true);
    cJSON *r = item(root, "relays", 2);
    TEST_ASSERT_EQUAL_STRING("Deck \"light\" \\ <b>", cJSON_GetObjectItem(r, "name")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(r, "momentary")));
    TEST_ASSERT_EQUAL(5, cJSON_GetObjectItem(r, "input")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(r, "inputToggle")));
    TEST_ASSERT_EQUAL(0, cJSON_GetObjectItem(r, "maxOnS")->valueint);
    TEST_ASSERT_EQUAL(1800, cJSON_GetObjectItem(item(root, "relays", 1), "maxOnS")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(item(root, "relays", 1), "inputToggle")));
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(item(root, "relays", 0), "momentary")));
    TEST_ASSERT_EQUAL(0, cJSON_GetObjectItem(item(root, "relays", 0), "input")->valueint);
    cJSON_Delete(root);
}

TEST_CASE("state: cycles and runTime per relay and input", "[web_ui]")
{
    fresh();
    web_ui_view_t v = {.cfg = &cfg, .inputs_ready = true};
    v.relay_cycles[2] = 7;
    v.relay_runtime_s[2] = 1234;
    v.input_cycles[5] = 3;
    v.input_runtime_s[5] = 42;
    char *json = web_ui_state_json(&v);
    cJSON *root = cJSON_Parse(json);
    free(json);
    TEST_ASSERT_EQUAL(7, cJSON_GetObjectItem(item(root, "relays", 2), "cycles")->valueint);
    TEST_ASSERT_EQUAL(1234, cJSON_GetObjectItem(item(root, "relays", 2), "runTime")->valueint);
    TEST_ASSERT_EQUAL(0, cJSON_GetObjectItem(item(root, "relays", 0), "cycles")->valueint);
    TEST_ASSERT_EQUAL(3, cJSON_GetObjectItem(item(root, "inputs", 5), "cycles")->valueint);
    TEST_ASSERT_EQUAL(42, cJSON_GetObjectItem(item(root, "inputs", 5), "runTime")->valueint);
    cJSON_Delete(root);
}

TEST_CASE("channel from path: 1-8 only", "[web_ui]")
{
    const char *p = "/api/v1/relays";
    TEST_ASSERT_EQUAL(1, web_ui_parse_channel("/api/v1/relays/1", p));
    TEST_ASSERT_EQUAL(8, web_ui_parse_channel("/api/v1/relays/8", p));
    TEST_ASSERT_EQUAL(3, web_ui_parse_channel("/api/v1/relays/3?x=1", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/api/v1/relays/0", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/api/v1/relays/9", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/api/v1/relays/10", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/api/v1/relays/1x", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/api/v1/relays/", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/api/v1/relays", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/api/v1/relaysX/1", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/other/1", p));
}

TEST_CASE("channel from a reset path: <prefix>/<n>/counters/reset", "[web_ui]")
{
    const char *p = "/api/v1/relays";
    TEST_ASSERT_EQUAL(1, web_ui_parse_reset_channel("/api/v1/relays/1/counters/reset", p));
    TEST_ASSERT_EQUAL(8, web_ui_parse_reset_channel("/api/v1/relays/8/counters/reset", p));
    TEST_ASSERT_EQUAL(3, web_ui_parse_reset_channel("/api/v1/relays/3/counters/reset?x=1", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_reset_channel("/api/v1/relays/0/counters/reset", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_reset_channel("/api/v1/relays/9/counters/reset", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_reset_channel("/api/v1/relays/1", p));  // no suffix: not a reset
    TEST_ASSERT_EQUAL(0, web_ui_parse_reset_channel("/api/v1/relays/1/counters", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_reset_channel("/api/v1/relays/1x/counters/reset", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_reset_channel("/other/1/counters/reset", p));

    const char *ip = "/api/v1/inputs";
    TEST_ASSERT_EQUAL(4, web_ui_parse_reset_channel("/api/v1/inputs/4/counters/reset", ip));
}

TEST_CASE("body: only a boolean \"on\" is accepted", "[web_ui]")
{
    bool on = false;
    TEST_ASSERT_TRUE(web_ui_parse_on("{\"on\":true}", &on));
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_TRUE(web_ui_parse_on("{ \"on\" : false, \"x\": 1 }", &on));
    TEST_ASSERT_FALSE(on);
    on = true;
    TEST_ASSERT_FALSE(web_ui_parse_on("{\"on\":1}", &on));
    TEST_ASSERT_FALSE(web_ui_parse_on("{\"on\":\"true\"}", &on));
    TEST_ASSERT_FALSE(web_ui_parse_on("{\"On\":true}", &on));
    TEST_ASSERT_FALSE(web_ui_parse_on("{}", &on));
    TEST_ASSERT_FALSE(web_ui_parse_on("not json", &on));
    TEST_ASSERT_FALSE(web_ui_parse_on("", &on));
    TEST_ASSERT_TRUE(on);  // untouched by the failures
}

TEST_CASE("body: rtttl preview accepts a non-empty string only", "[web_ui]")
{
    char out[32];
    TEST_ASSERT_TRUE(web_ui_parse_rtttl("{\"rtttl\":\"boot:d=4,o=5,b=63:c\"}", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("boot:d=4,o=5,b=63:c", out);
    TEST_ASSERT_FALSE(web_ui_parse_rtttl("{\"rtttl\":\"\"}", out, sizeof(out)));
    TEST_ASSERT_FALSE(web_ui_parse_rtttl("{\"rtttl\":1}", out, sizeof(out)));
    TEST_ASSERT_FALSE(web_ui_parse_rtttl("{}", out, sizeof(out)));
    TEST_ASSERT_FALSE(web_ui_parse_rtttl("not json", out, sizeof(out)));
    // Truncated, not overflowed, into a too-small buffer.
    char small[5];
    TEST_ASSERT_TRUE(web_ui_parse_rtttl("{\"rtttl\":\"abcdef\"}", small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("abcd", small);
}

TEST_CASE("state: last source and age per relay, null until known", "[web_ui]")
{
    fresh();
    web_ui_view_t v = {.cfg = &cfg, .inputs_ready = true};
    v.last_source[0] = "boot";
    v.last_change_ago_s[0] = 12;
    v.last_source[4] = "nmea2000";
    v.last_change_ago_s[4] = 180;
    char *json = web_ui_state_json(&v);
    cJSON *root = cJSON_Parse(json);
    free(json);
    cJSON *r = item(root, "relays", 0);
    TEST_ASSERT_EQUAL_STRING("boot", cJSON_GetObjectItem(r, "lastSource")->valuestring);
    TEST_ASSERT_EQUAL(12, cJSON_GetObjectItem(r, "lastChangeAgoS")->valueint);
    r = item(root, "relays", 4);
    TEST_ASSERT_EQUAL_STRING("nmea2000", cJSON_GetObjectItem(r, "lastSource")->valuestring);
    TEST_ASSERT_EQUAL(180, cJSON_GetObjectItem(r, "lastChangeAgoS")->valueint);
    r = item(root, "relays", 1);
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(r, "lastSource")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(r, "lastChangeAgoS")));
    cJSON_Delete(root);
}

TEST_CASE("status: network, SignalK and NMEA 2000 as reported", "[web_ui]")
{
    web_ui_status_t st = {.net_up = true, .sk_enabled = true, .sk_connected = true,
                          .n2k_started = true, .n2k_address = 35, .n2k_traffic = true};
    strcpy(st.version, "v0.0.6");
    strcpy(st.hostname, "espos-cf28");
    strcpy(st.net_iface, "eth");
    strcpy(st.ip, "10.42.23.50");
    strcpy(st.sk_server, "10.42.23.1:80");
    char *json = web_ui_status_json(&st);
    cJSON *root = cJSON_Parse(json);
    free(json);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL_STRING("v0.0.6", cJSON_GetObjectItem(root, "version")->valuestring);
    TEST_ASSERT_EQUAL_STRING("espos-cf28", cJSON_GetObjectItem(root, "hostname")->valuestring);
    cJSON *net = cJSON_GetObjectItem(root, "network");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(net, "up")));
    TEST_ASSERT_EQUAL_STRING("eth", cJSON_GetObjectItem(net, "interface")->valuestring);
    TEST_ASSERT_EQUAL_STRING("10.42.23.50", cJSON_GetObjectItem(net, "ip")->valuestring);
    cJSON *sk = cJSON_GetObjectItem(root, "signalk");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(sk, "connected")));
    TEST_ASSERT_EQUAL_STRING("10.42.23.1:80", cJSON_GetObjectItem(sk, "server")->valuestring);
    cJSON *n2k = cJSON_GetObjectItem(root, "nmea2000");
    TEST_ASSERT_EQUAL(35, cJSON_GetObjectItem(n2k, "address")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(n2k, "traffic")));
    cJSON_Delete(root);
}

TEST_CASE("status: down, unknown and not started become false or null", "[web_ui]")
{
    web_ui_status_t st = {.n2k_address = 35, .n2k_traffic = true};  // stale values
    strcpy(st.net_iface, "none");
    strcpy(st.ip, "0.0.0.0");
    char *json = web_ui_status_json(&st);
    cJSON *root = cJSON_Parse(json);
    free(json);
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(root, "version")));
    cJSON *net = cJSON_GetObjectItem(root, "network");
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(net, "up")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(net, "interface")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(net, "ip")));
    cJSON *sk = cJSON_GetObjectItem(root, "signalk");
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(sk, "connected")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(sk, "server")));
    cJSON *n2k = cJSON_GetObjectItem(root, "nmea2000");
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(n2k, "started")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(n2k, "address")));
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(n2k, "traffic")));
    cJSON_Delete(root);
}

TEST_CASE("status: a synced clock reports source, instant, local time and offset", "[web_ui]")
{
    web_ui_status_t st = {.clock_synced = true, .clock_unix_ms = 1790709605123LL,
                          .clock_utc_offset_s = 7200, .rtc_ok = true};
    strcpy(st.clock_source, "manual");
    strcpy(st.clock_local, "2026-09-29 21:20:05");
    strcpy(st.clock_tz, "CET-1CEST,M3.5.0,M10.5.0/3");
    char *json = web_ui_status_json(&st);
    cJSON *root = cJSON_Parse(json);
    free(json);
    cJSON *clk = cJSON_GetObjectItem(root, "clock");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(clk, "synced")));
    TEST_ASSERT_EQUAL_STRING("manual", cJSON_GetObjectItem(clk, "source")->valuestring);
    // Unix ms is past 2^31, so check the double, not cJSON's 32-bit valueint.
    TEST_ASSERT_TRUE(cJSON_GetObjectItem(clk, "now")->valuedouble == 1790709605123.0);
    TEST_ASSERT_EQUAL_STRING("2026-09-29 21:20:05", cJSON_GetObjectItem(clk, "local")->valuestring);
    TEST_ASSERT_EQUAL(7200, cJSON_GetObjectItem(clk, "utcOffsetS")->valueint);
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", cJSON_GetObjectItem(clk, "tz")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(clk, "rtc")));
    cJSON_Delete(root);
}

TEST_CASE("status: an unsynced clock nulls every time field but still reports tz and rtc", "[web_ui]")
{
    // Stale values behind synced = false must not leak out.
    web_ui_status_t st = {.clock_unix_ms = 1790709605123LL, .clock_utc_offset_s = 7200};
    strcpy(st.clock_source, "rtc");
    strcpy(st.clock_local, "2026-09-29 21:20:05");
    strcpy(st.clock_tz, "UTC0");
    char *json = web_ui_status_json(&st);
    cJSON *root = cJSON_Parse(json);
    free(json);
    cJSON *clk = cJSON_GetObjectItem(root, "clock");
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(clk, "synced")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(clk, "source")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(clk, "now")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(clk, "local")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(clk, "utcOffsetS")));
    TEST_ASSERT_EQUAL_STRING("UTC0", cJSON_GetObjectItem(clk, "tz")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(clk, "rtc")));
    cJSON_Delete(root);
}

// ---------------------------------------------------- interlock (issue #8)

TEST_CASE("all-on skip mask: interlocked relays only", "[web_ui]")
{
    fresh();
    cfg.relays[2].interlock = 4;  // relays 3 and 4 interlocked
    cfg.relays[3].interlock = 3;
    cfg.relays[5].interlock = 1;  // one-sided (device_config's job to catch, not this)

    const uint8_t skipped = web_ui_all_on_skipped(&cfg);
    TEST_ASSERT_EQUAL_HEX8(0x2C, skipped);  // bits 2,3 (relay 3,4) and 5 (relay 6)

    cfg.relays[2].interlock = cfg.relays[3].interlock = cfg.relays[5].interlock = 0;
    TEST_ASSERT_EQUAL_HEX8(0x00, web_ui_all_on_skipped(&cfg));
}

TEST_CASE("all-on skip mask is 0 in a plain state response, populated when passed in", "[web_ui]")
{
    fresh();
    cJSON *root = state(0, 0, true);
    cJSON *sk = cJSON_GetObjectItem(root, "skipped");
    TEST_ASSERT_TRUE(cJSON_IsArray(sk));
    TEST_ASSERT_EQUAL(0, cJSON_GetArraySize(sk));
    cJSON_Delete(root);

    cfg.relays[2].interlock = 4;
    cfg.relays[3].interlock = 3;
    const web_ui_view_t v = {.cfg = &cfg, .all_on_skipped = web_ui_all_on_skipped(&cfg)};
    char *json = web_ui_state_json(&v);
    root = cJSON_Parse(json);
    free(json);
    sk = cJSON_GetObjectItem(root, "skipped");
    TEST_ASSERT_EQUAL(2, cJSON_GetArraySize(sk));
    TEST_ASSERT_EQUAL(3, cJSON_GetArrayItem(sk, 0)->valueint);
    TEST_ASSERT_EQUAL(4, cJSON_GetArrayItem(sk, 1)->valueint);
    cJSON_Delete(root);
}

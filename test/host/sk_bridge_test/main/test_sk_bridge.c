#include <stdio.h>
#include <string.h>

#include "sk_bridge.h"
#include "unity.h"

// ------------------------------------------------------ fake espOS SignalK

#define MAX_CALLS 512

typedef enum { CALL_NUMBER, CALL_STRING, CALL_META, CALL_PUT_REG } call_kind_t;

static struct {
    call_kind_t kind;
    char path[128];
    char text[200];
    double number;
    sk_put_handler_t cb;
    void *arg;
} calls[MAX_CALLS];
static int n_calls;

static void record(call_kind_t kind, const char *path)
{
    TEST_ASSERT_LESS_THAN(MAX_CALLS, n_calls);
    memset(&calls[n_calls], 0, sizeof(calls[0]));
    calls[n_calls].kind = kind;
    snprintf(calls[n_calls].path, sizeof(calls[0].path), "%s", path);
}

static esp_err_t pub_number(const char *path, double v)
{
    record(CALL_NUMBER, path);
    calls[n_calls++].number = v;
    return ESP_OK;
}

static esp_err_t pub_string(const char *path, const char *v)
{
    record(CALL_STRING, path);
    snprintf(calls[n_calls++].text, sizeof(calls[0].text), "%s", v);
    return ESP_OK;
}

static esp_err_t meta(const char *path, const char *json, uint32_t period)
{
    record(CALL_META, path);
    calls[n_calls].number = period;
    snprintf(calls[n_calls++].text, sizeof(calls[0].text), "%s", json);
    return ESP_OK;
}

static esp_err_t put_reg(const char *path, sk_put_handler_t cb, void *arg)
{
    record(CALL_PUT_REG, path);
    calls[n_calls].cb = cb;
    calls[n_calls++].arg = arg;
    return ESP_OK;
}

static const sk_api_t api = {pub_number, pub_string, meta, put_reg};

static int count(call_kind_t kind)
{
    int n = 0;
    for (int i = 0; i < n_calls; i++) {
        n += calls[i].kind == kind;
    }
    return n;
}

// Index of the last call of `kind` on `path`, or -1.
static int find(call_kind_t kind, const char *path)
{
    int found = -1;
    for (int i = 0; i < n_calls; i++) {
        if (calls[i].kind == kind && strcmp(calls[i].path, path) == 0) {
            found = i;
        }
    }
    return found;
}

// --------------------------------------------------------- fake relays/io

static uint8_t relay_mask, input_mask;
static bool inputs_ready;
static int sk_lost_calls;
static uint32_t clock_ms;
static struct {
    uint8_t ch;
    bool on;
} sets[16];
static int n_sets;
static esp_err_t set_result;

static esp_err_t set_relay(uint8_t ch, bool on)
{
    sets[n_sets].ch = ch;
    sets[n_sets].on = on;
    n_sets++;
    return set_result;
}
static uint8_t get_relays(void) { return relay_mask; }
static bool get_ready(void) { return inputs_ready; }
static uint8_t get_inputs(void) { return input_mask; }
static void on_sk_lost(void) { sk_lost_calls++; }
static uint32_t now(void) { return clock_ms; }

static const sk_bridge_io_t io = {set_relay, get_relays, get_ready, get_inputs, on_sk_lost, now};

// --------------------------------------------------------------- fixtures

static device_config_t cfg;

static void fresh(void)
{
    sk_bridge_reset();
    n_calls = n_sets = sk_lost_calls = 0;
    relay_mask = input_mask = 0;
    inputs_ready = true;
    set_result = ESP_OK;
    clock_ms = 5000;
    memset(&cfg, 0, sizeof(cfg));
    cfg.bank_id = 0;
    cfg.input_bank_id = 1;
    cfg.sk_loss_grace_s = 30;
    cfg.publish_switches_tree = true;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        snprintf(cfg.relays[i].name, sizeof(cfg.relays[i].name), "Relay %d", i + 1);
        snprintf(cfg.inputs[i].name, sizeof(cfg.inputs[i].name), "Input %d", i + 1);
    }
}

static void start(void)
{
    TEST_ESP_OK(sk_bridge_start(&api, &io, &cfg));
}

static esp_err_t put(const char *path, const char *value)
{
    int i = find(CALL_PUT_REG, path);
    TEST_ASSERT_TRUE_MESSAGE(i >= 0, path);
    return calls[i].cb(path, value, calls[i].arg);
}

// ------------------------------------------------------------------ paths

TEST_CASE("default: switches tree only, a PUT handler per relay", "[sk_bridge]")
{
    fresh();
    relay_mask = 0x04;
    start();
    TEST_ASSERT_EQUAL(8, count(CALL_PUT_REG));
    TEST_ASSERT_TRUE(find(CALL_PUT_REG, "electrical.switches.bank.0.1.state") >= 0);
    TEST_ASSERT_TRUE(find(CALL_PUT_REG, "electrical.switches.bank.0.8.state") >= 0);
    TEST_ASSERT_EQUAL(16, count(CALL_META));  // 8 relays + 8 inputs
    int i = find(CALL_NUMBER, "electrical.switches.bank.0.3.state");
    TEST_ASSERT_TRUE(i >= 0);
    TEST_ASSERT_EQUAL(1, calls[i].number);
    i = find(CALL_NUMBER, "electrical.switches.bank.1.5.order");
    TEST_ASSERT_TRUE(i >= 0);
    TEST_ASSERT_EQUAL(5, calls[i].number);
    for (int k = 0; k < n_calls; k++) {
        TEST_ASSERT_NULL(strstr(calls[k].path, "electrical.controls"));
    }
}

// Boot order: the I/O task and relay/input listeners run before the network
// is up and sk_bridge_start() is called. v0.0.1 took a lock that didn't exist
// yet there and boot-looped.
static void call_everything_early(void)
{
    sk_bridge_tick();
    sk_bridge_relay_changed(1, true);
    sk_bridge_input_changed(2, true);
    sk_bridge_update_config(&cfg);
    sk_bridge_stream_changed(false);
}

TEST_CASE("calls before init or start are ignored, and start still works", "[sk_bridge]")
{
    fresh();
    call_everything_early();  // no lock yet
    TEST_ESP_OK(sk_bridge_init());
    call_everything_early();  // lock, not started
    TEST_ASSERT_EQUAL(0, n_calls);
    TEST_ASSERT_EQUAL(0, sk_lost_calls);
    start();
    TEST_ASSERT_EQUAL(8, count(CALL_PUT_REG));
}

TEST_CASE("every PUT path is published before its handler is registered", "[sk_bridge]")
{
    fresh();
    cfg.publish_controls_tree = true;
    start();
    for (int k = 0; k < n_calls; k++) {
        if (calls[k].kind == CALL_PUT_REG) {
            int p = -1;
            for (int j = 0; j < k; j++) {
                if (calls[j].kind == CALL_NUMBER && strcmp(calls[j].path, calls[k].path) == 0) {
                    p = j;
                }
            }
            TEST_ASSERT_TRUE_MESSAGE(p >= 0, calls[k].path);
        }
    }
}

TEST_CASE("controls tree uses espOS-instance identifiers from each bank", "[sk_bridge]")
{
    fresh();
    cfg.bank_id = 12;
    cfg.input_bank_id = 13;
    cfg.publish_controls_tree = true;
    start();
    TEST_ASSERT_EQUAL(16, count(CALL_PUT_REG));
    TEST_ASSERT_TRUE(find(CALL_PUT_REG, "electrical.controls.espOS-instance12-relay3.state") >= 0);
    TEST_ASSERT_TRUE(find(CALL_NUMBER, "electrical.controls.espOS-instance13-input8.state") >= 0);
    int i = find(CALL_STRING, "electrical.controls.espOS-instance12-relay3.type");
    TEST_ASSERT_EQUAL_STRING("switch", calls[i].text);
    i = find(CALL_STRING, "electrical.controls.espOS-instance12-relay3.manufacturer.model");
    TEST_ASSERT_EQUAL_STRING("ESP32-S3-ETH-8DI-8RO-C", calls[i].text);
    i = find(CALL_STRING, "electrical.controls.espOS-instance13-input2.name");
    TEST_ASSERT_EQUAL_STRING("Input 2", calls[i].text);
    TEST_ASSERT_EQUAL(32, count(CALL_META));
    TEST_ASSERT_EQUAL(-1, find(CALL_PUT_REG, "electrical.controls.espOS-instance13-input1.state"));
}

TEST_CASE("the longest path fits espOS's 96-character limit", "[sk_bridge]")
{
    fresh();
    cfg.bank_id = 251;
    cfg.input_bank_id = 252;
    cfg.publish_controls_tree = true;
    start();
    for (int k = 0; k < n_calls; k++) {
        TEST_ASSERT_LESS_THAN(96, strlen(calls[k].path));
    }
}

TEST_CASE("both trees off: nothing published, nothing registered", "[sk_bridge]")
{
    fresh();
    cfg.publish_switches_tree = false;
    start();
    TEST_ASSERT_EQUAL(0, n_calls);
}

// -------------------------------------------------------------------- PUT

TEST_CASE("PUT accepts true/false and 1/0", "[sk_bridge]")
{
    fresh();
    start();
    TEST_ESP_OK(put("electrical.switches.bank.0.2.state", "true"));
    TEST_ESP_OK(put("electrical.switches.bank.0.2.state", " 0 "));
    TEST_ESP_OK(put("electrical.switches.bank.0.7.state", "1"));
    TEST_ESP_OK(put("electrical.switches.bank.0.7.state", "false"));
    TEST_ASSERT_EQUAL(4, n_sets);
    TEST_ASSERT_EQUAL(2, sets[0].ch);
    TEST_ASSERT_TRUE(sets[0].on);
    TEST_ASSERT_FALSE(sets[1].on);
    TEST_ASSERT_EQUAL(7, sets[2].ch);
    TEST_ASSERT_TRUE(sets[2].on);
    TEST_ASSERT_FALSE(sets[3].on);
}

TEST_CASE("PUT rejects anything else without switching", "[sk_bridge]")
{
    fresh();
    start();
    const char *bad[] = {"2", "0.5", "\"on\"", "null", "", "truex", "1 1"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, put("electrical.switches.bank.0.1.state", bad[i]), bad[i]);
    }
    TEST_ASSERT_EQUAL(0, n_sets);
}

TEST_CASE("a relay that fails to switch answers PUT with an error", "[sk_bridge]")
{
    fresh();
    start();
    set_result = ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(ESP_FAIL, put("electrical.switches.bank.0.1.state", "1"));
}

TEST_CASE("PUTs on either tree switch the same relay", "[sk_bridge]")
{
    fresh();
    cfg.publish_controls_tree = true;
    start();
    TEST_ESP_OK(put("electrical.controls.espOS-instance0-relay3.state", "1"));
    TEST_ESP_OK(put("electrical.switches.bank.0.3.state", "0"));
    TEST_ASSERT_EQUAL(3, sets[0].ch);
    TEST_ASSERT_EQUAL(3, sets[1].ch);
}

// ------------------------------------------------------------ publishing

TEST_CASE("a relay change is published on every enabled tree", "[sk_bridge]")
{
    fresh();
    cfg.publish_controls_tree = true;
    start();
    n_calls = 0;
    sk_bridge_relay_changed(5, true);
    TEST_ASSERT_EQUAL(2, n_calls);
    TEST_ASSERT_EQUAL(1, calls[find(CALL_NUMBER, "electrical.switches.bank.0.5.state")].number);
    TEST_ASSERT_EQUAL(1, calls[find(CALL_NUMBER, "electrical.controls.espOS-instance0-relay5.state")].number);
}

TEST_CASE("inputs are published once they have settled", "[sk_bridge]")
{
    fresh();
    inputs_ready = false;
    start();
    TEST_ASSERT_EQUAL(-1, find(CALL_NUMBER, "electrical.switches.bank.1.1.state"));
    sk_bridge_input_changed(1, true);
    TEST_ASSERT_EQUAL(1, calls[find(CALL_NUMBER, "electrical.switches.bank.1.1.state")].number);
}

TEST_CASE("clashing bank ids: the input bank is never published", "[sk_bridge]")
{
    fresh();
    cfg.input_bank_id = cfg.bank_id;
    input_mask = 0xFF;
    start();
    TEST_ASSERT_EQUAL(8, count(CALL_META));  // relays only
    // With equal ids the input paths would be the relay paths; none of the
    // inputs' "on" states may reach them.
    for (int k = 0; k < n_calls; k++) {
        if (calls[k].kind == CALL_NUMBER && strstr(calls[k].path, ".state")) {
            TEST_ASSERT_EQUAL(0, calls[k].number);
        }
    }
    const int before = n_calls;
    sk_bridge_input_changed(1, true);
    TEST_ASSERT_EQUAL(before, n_calls);
}

TEST_CASE("reconnecting republishes all state", "[sk_bridge]")
{
    fresh();
    start();
    sk_bridge_stream_changed(true);
    n_calls = 0;
    relay_mask = 0x80;
    sk_bridge_stream_changed(false);
    sk_bridge_stream_changed(true);
    TEST_ASSERT_EQUAL(1, calls[find(CALL_NUMBER, "electrical.switches.bank.0.8.state")].number);
    TEST_ASSERT_TRUE(find(CALL_NUMBER, "electrical.switches.bank.1.8.state") >= 0);
}

TEST_CASE("a rename re-declares metadata, escaped", "[sk_bridge]")
{
    fresh();
    cfg.publish_controls_tree = true;
    start();
    n_calls = 0;
    device_config_t next = cfg;
    snprintf(next.relays[1].name, sizeof(next.relays[1].name), "Deck \"flood\" light");
    sk_bridge_update_config(&next);
    TEST_ASSERT_EQUAL(2, count(CALL_META));
    int i = find(CALL_META, "electrical.switches.bank.0.2.state");
    TEST_ASSERT_NOT_NULL(strstr(calls[i].text, "\"displayName\":\"Deck \\\"flood\\\" light\""));
    i = find(CALL_STRING, "electrical.controls.espOS-instance0-relay2.name");
    TEST_ASSERT_EQUAL_STRING("Deck \"flood\" light", calls[i].text);
}

// ---------------------------------------------------------- SignalK loss

TEST_CASE("no fail-safe at boot before any connection", "[sk_bridge]")
{
    fresh();
    start();
    clock_ms += 3600 * 1000;
    sk_bridge_tick();
    TEST_ASSERT_EQUAL(0, sk_lost_calls);
}

TEST_CASE("fail-safe fires once, after the grace period", "[sk_bridge]")
{
    fresh();
    start();
    sk_bridge_stream_changed(true);
    sk_bridge_stream_changed(false);
    clock_ms += 29999;
    sk_bridge_tick();
    TEST_ASSERT_EQUAL(0, sk_lost_calls);
    clock_ms += 1;
    sk_bridge_tick();
    TEST_ASSERT_EQUAL(1, sk_lost_calls);
    clock_ms += 60000;
    sk_bridge_tick();
    TEST_ASSERT_EQUAL(1, sk_lost_calls);

    sk_bridge_stream_changed(true);  // back, then lost again
    sk_bridge_stream_changed(false);
    clock_ms += 30000;
    sk_bridge_tick();
    TEST_ASSERT_EQUAL(2, sk_lost_calls);
}

TEST_CASE("reconnecting within the grace period cancels the fail-safe", "[sk_bridge]")
{
    fresh();
    start();
    sk_bridge_stream_changed(true);
    sk_bridge_stream_changed(false);
    clock_ms += 20000;
    sk_bridge_stream_changed(true);
    clock_ms += 20000;
    sk_bridge_tick();
    TEST_ASSERT_EQUAL(0, sk_lost_calls);
}

TEST_CASE("no fail-safe when SignalK isn't a control path", "[sk_bridge]")
{
    fresh();
    cfg.publish_switches_tree = false;
    start();
    sk_bridge_stream_changed(true);
    sk_bridge_stream_changed(false);
    clock_ms += 60000;
    sk_bridge_tick();
    TEST_ASSERT_EQUAL(0, sk_lost_calls);
}

// ------------------------------------------------------------- republish

static int count_state_numbers(void)
{
    int n = 0;
    for (int k = 0; k < n_calls; k++) {
        if (calls[k].kind == CALL_NUMBER && strstr(calls[k].path, ".state")) {
            n++;
        }
    }
    return n;
}

TEST_CASE("republish: every state, every interval, while connected", "[sk_bridge]")
{
    fresh();
    cfg.sk_republish_s = 10;
    relay_mask = 0x05;
    start();
    sk_bridge_stream_changed(true);  // full publish, restarts the interval
    n_calls = 0;
    clock_ms += 9999;
    sk_bridge_tick();
    TEST_ASSERT_EQUAL(0, n_calls);
    clock_ms += 1;
    sk_bridge_tick();
    TEST_ASSERT_EQUAL(16, count_state_numbers());  // 8 relays + 8 inputs
    TEST_ASSERT_EQUAL(16, n_calls);                // states only
    TEST_ASSERT_EQUAL(1, calls[find(CALL_NUMBER, "electrical.switches.bank.0.3.state")].number);
    n_calls = 0;
    sk_bridge_tick();
    TEST_ASSERT_EQUAL(0, n_calls);
    clock_ms += 10000;
    sk_bridge_tick();
    TEST_ASSERT_EQUAL(16, n_calls);
}

TEST_CASE("republish: nothing before a connection or while it is down", "[sk_bridge]")
{
    fresh();
    cfg.sk_republish_s = 10;
    start();
    n_calls = 0;
    clock_ms += 60000;
    sk_bridge_tick();
    TEST_ASSERT_EQUAL(0, n_calls);
    sk_bridge_stream_changed(true);
    sk_bridge_stream_changed(false);
    n_calls = 0;
    clock_ms += 20000;
    sk_bridge_tick();
    TEST_ASSERT_EQUAL(0, n_calls);
}

TEST_CASE("republish: 0 sends changes only", "[sk_bridge]")
{
    fresh();
    cfg.sk_republish_s = 0;
    start();
    sk_bridge_stream_changed(true);
    n_calls = 0;
    clock_ms += 3600 * 1000;
    sk_bridge_tick();
    TEST_ASSERT_EQUAL(0, n_calls);
}

TEST_CASE("republish: the interval is the metadata period, and follows changes", "[sk_bridge]")
{
    fresh();
    cfg.sk_republish_s = 10;
    start();
    TEST_ASSERT_EQUAL(10000, calls[find(CALL_META, "electrical.switches.bank.0.1.state")].number);
    TEST_ASSERT_EQUAL(10000, calls[find(CALL_META, "electrical.switches.bank.1.8.state")].number);
    n_calls = 0;
    cfg.sk_republish_s = 30;
    sk_bridge_update_config(&cfg);
    TEST_ASSERT_EQUAL(16, count(CALL_META));
    TEST_ASSERT_EQUAL(30000, calls[find(CALL_META, "electrical.switches.bank.0.1.state")].number);
    n_calls = 0;
    sk_bridge_update_config(&cfg);  // unchanged: no re-declare
    TEST_ASSERT_EQUAL(0, count(CALL_META));
}

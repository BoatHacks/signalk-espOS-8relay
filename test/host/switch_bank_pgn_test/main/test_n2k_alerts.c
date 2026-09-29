#include <stdio.h>
#include <string.h>

#include "n2k_alert_pgn.h"
#include "n2k_alerts.h"
#include "unity.h"

#define NAME UINT64_C(0x8123456789ABCDEF)
#define BANK 1

// ------------------------------------------------------------ payloads

TEST_CASE("126983: header, flags and state at canboat's offsets", "[alert]")
{
    n2k_alert_t a = {
        .id = {.type = N2K_ALERT_TYPE_ALARM,
               .category = N2K_ALERT_CATEGORY_TECHNICAL,
               .system = 0x11,
               .subsystem = 0x22,
               .id = 0x0304,
               .source_name = UINT64_C(0x0807060504030201),
               .instance = 9,
               .index = 3,
               .occurrence = 7},
        .acknowledged = true,
        .ack_supported = true,
        .ack_name = UINT64_C(0x1112131415161718),
        .trigger = N2K_ALERT_TRIGGER_AUTO,
        .threshold = N2K_ALERT_THRESHOLD_EXCEEDED,
        .priority = 0,
        .state = N2K_ALERT_STATE_ACKNOWLEDGED,
    };
    uint8_t p[N2K_ALERT_LEN];
    n2k_alert_encode(&a, p);
    const uint8_t want[N2K_ALERT_LEN] = {
        0x12,                                            // type 2 | category 1 << 4
        0x11, 0x22,                                      // system, sub-system
        0x04, 0x03,                                      // id, little endian
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,  // data source NAME
        9, 3, 7,                                         // instance, index, occurrence
        0xD2,                                            // ack status, ack support, reserved
        0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,  // ack source NAME
        0x11,                                            // trigger auto | threshold exceeded << 4
        0,                                               // priority
        N2K_ALERT_STATE_ACKNOWLEDGED,
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, p, N2K_ALERT_LEN);
}

TEST_CASE("126985: ASCII text is sent as-is with control byte 1", "[alert]")
{
    const n2k_alert_id_t id = {.type = N2K_ALERT_TYPE_WARNING, .id = 2, .source_name = NAME};
    uint8_t p[N2K_ALERT_TEXT_MAX_LEN];
    const size_t n = n2k_alert_encode_text(&id, "Bilge", "", p, sizeof(p));
    TEST_ASSERT_EQUAL(16 + 1 + 7 + 2, n);
    TEST_ASSERT_EQUAL_HEX8(0x05, p[0]);  // warning, category 0
    TEST_ASSERT_EQUAL(0, p[16]);         // English (US)
    const uint8_t text[] = {7, 1, 'B', 'i', 'l', 'g', 'e', 2, 1};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(text, p + 17, sizeof(text));
}

TEST_CASE("126985: non-ASCII text is sent as UCS-2 with control byte 0", "[alert]")
{
    const n2k_alert_id_t id = {.id = 1};
    uint8_t p[N2K_ALERT_TEXT_MAX_LEN];
    // "Bilge läuft": ä is U+00E4, two bytes of UTF-8.
    const size_t n = n2k_alert_encode_text(&id, "l\xC3\xA4uft", "", p, sizeof(p));
    const uint8_t text[] = {12, 0, 'l', 0, 0xE4, 0, 'u', 0, 'f', 0, 't', 0, 2, 1};
    TEST_ASSERT_EQUAL(17 + sizeof(text), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(text, p + 17, sizeof(text));
}

TEST_CASE("126985: malformed UTF-8 becomes '?', and text is cut to fit", "[alert]")
{
    const n2k_alert_id_t id = {.id = 1};
    uint8_t p[N2K_ALERT_TEXT_MAX_LEN];
    n2k_alert_encode_text(&id, "\xC3" "a", "", p, sizeof(p));  // lead byte, no continuation
    const uint8_t text[] = {6, 0, '?', 0, 'a', 0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(text, p + 17, sizeof(text));

    // 30 bytes of room: header 17, location 2, so 11 for the description.
    const size_t n = n2k_alert_encode_text(&id, "0123456789ABCDEF", "", p, 30);
    TEST_ASSERT_EQUAL(30, n);
    TEST_ASSERT_EQUAL(11, p[17]);
    TEST_ASSERT_EQUAL_MEMORY("012345678", p + 19, 9);
    TEST_ASSERT_EQUAL(2, p[28]);
    TEST_ASSERT_EQUAL(1, p[29]);
}

TEST_CASE("126984: decodes the header, acknowledger and command", "[alert]")
{
    uint8_t p[N2K_ALERT_RESPONSE_LEN] = {0x12, 0, 0, 3, 0, 0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x81,
                                         BANK, 3, 4, 1, 2, 3, 4, 5, 6, 7, 8, 0xFC | N2K_ALERT_RESPONSE_SILENCE};
    n2k_alert_response_t r;
    TEST_ASSERT_TRUE(n2k_alert_decode_response(p, sizeof(p), &r));
    TEST_ASSERT_EQUAL(N2K_ALERT_TYPE_ALARM, r.id.type);
    TEST_ASSERT_EQUAL(3, r.id.id);
    TEST_ASSERT_EQUAL_HEX64(NAME, r.id.source_name);
    TEST_ASSERT_EQUAL(BANK, r.id.instance);
    TEST_ASSERT_EQUAL(4, r.id.occurrence);
    TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x0807060504030201), r.ack_name);
    TEST_ASSERT_EQUAL(N2K_ALERT_RESPONSE_SILENCE, r.command);
    TEST_ASSERT_FALSE(n2k_alert_decode_response(p, sizeof(p) - 1, &r));
}

// ------------------------------------------------------------ alerts

typedef struct {
    int status;  // 126983s sent
    int text;    // 126985s sent
    uint8_t last_status[N2K_ALERT_LEN];
    uint8_t last_text[N2K_ALERT_TEXT_MAX_LEN];
    size_t last_text_len;
} sent_t;

static sent_t sent;

static void on_status(const uint8_t *d, size_t len, void *arg)
{
    TEST_ASSERT_EQUAL(N2K_ALERT_LEN, len);
    sent.status++;
    memcpy(sent.last_status, d, len);
}

static void on_text(const uint8_t *d, size_t len, void *arg)
{
    sent.text++;
    memcpy(sent.last_text, d, len);
    sent.last_text_len = len;
}

static const n2k_alerts_out_t out = {on_status, on_text, NULL};

static n2k_alerts_cfg_t cfg_with(uint8_t ch, input_alarm_t level)
{
    n2k_alerts_cfg_t c;
    memset(&c, 0, sizeof(c));
    c.enabled = true;
    c.inputs_on = true;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        snprintf(c.message[i], sizeof(c.message[i]), "Input %d active", i + 1);
    }
    if (ch) {
        c.level[ch - 1] = level;
    }
    return c;
}

static n2k_alerts_t a;

static void start(const n2k_alerts_cfg_t *c)
{
    memset(&sent, 0, sizeof(sent));
    n2k_alerts_init(&a, c, BANK, NAME, 0);
}

static uint8_t state_sent(void) { return sent.last_status[27]; }

TEST_CASE("alerts: input on raises text then Active, at the input's severity", "[alerts]")
{
    n2k_alerts_cfg_t c = cfg_with(3, INPUT_ALARM_ALARM);
    start(&c);
    n2k_alerts_tick(&a, true, 0x00, 10, &out);
    TEST_ASSERT_EQUAL(0, sent.status + sent.text);

    n2k_alerts_tick(&a, true, 0x04, 20, &out);
    TEST_ASSERT_EQUAL(1, sent.text);
    TEST_ASSERT_EQUAL(1, sent.status);
    TEST_ASSERT_EQUAL(N2K_ALERT_STATE_ACTIVE, state_sent());
    TEST_ASSERT_EQUAL_HEX8(0x12, sent.last_status[0]);  // alarm, technical
    TEST_ASSERT_EQUAL(3, sent.last_status[3]);           // id = channel
    TEST_ASSERT_EQUAL(BANK, sent.last_status[13]);       // instance = input bank
    TEST_ASSERT_EQUAL(3, sent.last_status[14]);          // index = channel
    TEST_ASSERT_EQUAL(1, sent.last_status[15]);          // first occurrence
    TEST_ASSERT_EQUAL_HEX8(0xD0, sent.last_status[16]);  // ack supported, not acked
    TEST_ASSERT_EQUAL_MEMORY("Input 3 active", sent.last_text + 19, 14);

    // Nothing more until the next period.
    n2k_alerts_tick(&a, true, 0x04, 30, &out);
    TEST_ASSERT_EQUAL(1, sent.status);
}

TEST_CASE("alerts: severities map to canboat alert types", "[alerts]")
{
    TEST_ASSERT_EQUAL(N2K_ALERT_TYPE_WARNING, n2k_alerts_type_of(INPUT_ALARM_WARN));
    TEST_ASSERT_EQUAL(N2K_ALERT_TYPE_ALARM, n2k_alerts_type_of(INPUT_ALARM_ALARM));
    TEST_ASSERT_EQUAL(N2K_ALERT_TYPE_EMERGENCY, n2k_alerts_type_of(INPUT_ALARM_EMERGENCY));
}

TEST_CASE("alerts: repeated every second while active, text every 10 s", "[alerts]")
{
    n2k_alerts_cfg_t c = cfg_with(1, INPUT_ALARM_WARN);
    start(&c);
    n2k_alerts_tick(&a, true, 0x01, 5, &out);
    for (uint32_t t = 10; t <= 10000; t += 10) {
        n2k_alerts_tick(&a, true, 0x01, t, &out);
    }
    TEST_ASSERT_EQUAL(1 + 10, sent.status);
    TEST_ASSERT_EQUAL(1 + 1, sent.text);
}

TEST_CASE("alerts: input off sends Normal three times, then nothing", "[alerts]")
{
    n2k_alerts_cfg_t c = cfg_with(2, INPUT_ALARM_ALARM);
    start(&c);
    n2k_alerts_tick(&a, true, 0x02, 10, &out);
    sent.status = 0;
    n2k_alerts_tick(&a, true, 0x00, 20, &out);
    TEST_ASSERT_EQUAL(1, sent.status);
    TEST_ASSERT_EQUAL(N2K_ALERT_STATE_NORMAL, state_sent());
    TEST_ASSERT_EQUAL_HEX8(0x01, sent.last_status[25]);  // threshold back to normal
    TEST_ASSERT_EQUAL(1, sent.last_status[15]);          // same occurrence as raised
    for (uint32_t t = 30; t <= 10000; t += 10) {
        n2k_alerts_tick(&a, true, 0x00, t, &out);
    }
    TEST_ASSERT_EQUAL(N2K_ALERTS_CLEAR_REPEATS, sent.status);
    TEST_ASSERT_EQUAL(N2K_ALERT_STATE_NORMAL, state_sent());
}

TEST_CASE("alerts: each trip is a new occurrence", "[alerts]")
{
    n2k_alerts_cfg_t c = cfg_with(1, INPUT_ALARM_ALARM);
    start(&c);
    n2k_alerts_tick(&a, true, 0x01, 10, &out);
    n2k_alerts_tick(&a, true, 0x00, 20, &out);
    n2k_alerts_tick(&a, true, 0x01, 30, &out);
    TEST_ASSERT_EQUAL(N2K_ALERT_STATE_ACTIVE, state_sent());
    TEST_ASSERT_EQUAL(2, sent.last_status[15]);
}

TEST_CASE("alerts: nothing for alarm off, n2k_alerts off, unsettled inputs or a clashing bank", "[alerts]")
{
    n2k_alerts_cfg_t c = cfg_with(0, INPUT_ALARM_OFF);
    start(&c);
    for (uint32_t t = 0; t < 5000; t += 10) {
        n2k_alerts_tick(&a, true, 0xFF, t, &out);
    }
    TEST_ASSERT_EQUAL(0, sent.status + sent.text);

    c = cfg_with(1, INPUT_ALARM_EMERGENCY);
    c.enabled = false;
    start(&c);
    n2k_alerts_tick(&a, true, 0x01, 10, &out);
    TEST_ASSERT_EQUAL(0, sent.status + sent.text);

    c = cfg_with(1, INPUT_ALARM_EMERGENCY);
    c.inputs_on = false;
    start(&c);
    n2k_alerts_tick(&a, true, 0x01, 10, &out);
    TEST_ASSERT_EQUAL(0, sent.status + sent.text);

    // Not before inputs have settled; raised as soon as they have, so an
    // already-tripped float switch alarms at start-up.
    c = cfg_with(1, INPUT_ALARM_EMERGENCY);
    start(&c);
    n2k_alerts_tick(&a, false, 0x01, 10, &out);
    TEST_ASSERT_EQUAL(0, sent.status + sent.text);
    n2k_alerts_tick(&a, true, 0x01, 20, &out);
    TEST_ASSERT_EQUAL(1, sent.status);
    TEST_ASSERT_EQUAL_HEX8(0x11, sent.last_status[0]);  // emergency
}

TEST_CASE("alerts: turning the alarm or n2k_alerts off while tripped clears it", "[alerts]")
{
    n2k_alerts_cfg_t c = cfg_with(4, INPUT_ALARM_WARN);
    start(&c);
    n2k_alerts_tick(&a, true, 0x08, 10, &out);
    n2k_alerts_cfg_t off = cfg_with(0, INPUT_ALARM_OFF);
    n2k_alerts_set_config(&a, &off);
    sent.status = 0;
    n2k_alerts_tick(&a, true, 0x08, 20, &out);
    TEST_ASSERT_EQUAL(1, sent.status);
    TEST_ASSERT_EQUAL(N2K_ALERT_STATE_NORMAL, state_sent());

    start(&c);
    n2k_alerts_tick(&a, true, 0x08, 10, &out);
    off = c;
    off.enabled = false;
    n2k_alerts_set_config(&a, &off);
    n2k_alerts_tick(&a, true, 0x08, 20, &out);
    TEST_ASSERT_EQUAL(N2K_ALERT_STATE_NORMAL, state_sent());
}

TEST_CASE("alerts: turning an alarm on for a tripped input raises it at once", "[alerts]")
{
    n2k_alerts_cfg_t c = cfg_with(0, INPUT_ALARM_OFF);
    start(&c);
    n2k_alerts_tick(&a, true, 0x01, 10, &out);
    c = cfg_with(1, INPUT_ALARM_ALARM);
    n2k_alerts_set_config(&a, &c);
    n2k_alerts_tick(&a, true, 0x01, 20, &out);
    TEST_ASSERT_EQUAL(1, sent.status);
    TEST_ASSERT_EQUAL(N2K_ALERT_STATE_ACTIVE, state_sent());
}

TEST_CASE("alerts: a severity change clears the old alert and raises the new one", "[alerts]")
{
    n2k_alerts_cfg_t c = cfg_with(1, INPUT_ALARM_WARN);
    start(&c);
    n2k_alerts_tick(&a, true, 0x01, 10, &out);
    c = cfg_with(1, INPUT_ALARM_EMERGENCY);
    n2k_alerts_set_config(&a, &c);
    sent.status = 0;
    sent.text = 0;
    n2k_alerts_tick(&a, true, 0x01, 20, &out);
    TEST_ASSERT_EQUAL(2, sent.status);  // warning Normal, then emergency Active
    TEST_ASSERT_EQUAL(1, sent.text);
    TEST_ASSERT_EQUAL_HEX8(0x11, sent.last_status[0]);
    TEST_ASSERT_EQUAL(N2K_ALERT_STATE_ACTIVE, state_sent());
}

TEST_CASE("alerts: a changed message resends the text at once", "[alerts]")
{
    n2k_alerts_cfg_t c = cfg_with(1, INPUT_ALARM_ALARM);
    start(&c);
    n2k_alerts_tick(&a, true, 0x01, 10, &out);
    snprintf(c.message[0], sizeof(c.message[0]), "Bilge water high");
    n2k_alerts_set_config(&a, &c);
    sent.status = 0;
    sent.text = 0;
    n2k_alerts_tick(&a, true, 0x01, 20, &out);
    TEST_ASSERT_EQUAL(1, sent.text);
    TEST_ASSERT_EQUAL(0, sent.status);
    TEST_ASSERT_EQUAL_MEMORY("Bilge water high", sent.last_text + 19, 16);
}

static void response(uint8_t ch, uint8_t type, uint8_t occurrence, uint64_t source, uint8_t command,
                     uint8_t p[N2K_ALERT_RESPONSE_LEN])
{
    memset(p, 0, N2K_ALERT_RESPONSE_LEN);
    p[0] = (uint8_t)(type | (N2K_ALERT_CATEGORY_TECHNICAL << 4));
    p[3] = ch;
    for (int i = 0; i < 8; i++) {
        p[5 + i] = (uint8_t)(source >> (8 * i));
        p[16 + i] = (uint8_t)(0xA0 + i);  // acknowledger's NAME
    }
    p[13] = BANK;
    p[14] = ch;
    p[15] = occurrence;
    p[24] = command;
}

TEST_CASE("alerts: an MFD's acknowledge turns the alert Acknowledged until it clears", "[alerts]")
{
    n2k_alerts_cfg_t c = cfg_with(2, INPUT_ALARM_ALARM);
    start(&c);
    n2k_alerts_tick(&a, true, 0x02, 10, &out);
    uint8_t p[N2K_ALERT_RESPONSE_LEN];
    response(2, N2K_ALERT_TYPE_ALARM, 1, NAME, N2K_ALERT_RESPONSE_ACKNOWLEDGE, p);
    TEST_ASSERT_TRUE(n2k_alerts_on_response(&a, p, sizeof(p)));
    sent.status = 0;
    n2k_alerts_tick(&a, true, 0x02, 20, &out);
    TEST_ASSERT_EQUAL(1, sent.status);
    TEST_ASSERT_EQUAL(N2K_ALERT_STATE_ACKNOWLEDGED, state_sent());
    TEST_ASSERT_EQUAL_HEX8(0xD2, sent.last_status[16]);  // ack status set
    TEST_ASSERT_EQUAL_HEX8(0xA0, sent.last_status[17]);  // acknowledger's NAME
    // A second ack changes nothing.
    TEST_ASSERT_FALSE(n2k_alerts_on_response(&a, p, sizeof(p)));

    // Still Acknowledged on the repeats; a new trip is Active again.
    n2k_alerts_tick(&a, true, 0x02, 1100, &out);
    TEST_ASSERT_EQUAL(N2K_ALERT_STATE_ACKNOWLEDGED, state_sent());
    n2k_alerts_tick(&a, true, 0x00, 1200, &out);
    n2k_alerts_tick(&a, true, 0x02, 1300, &out);
    TEST_ASSERT_EQUAL(N2K_ALERT_STATE_ACTIVE, state_sent());
}

TEST_CASE("alerts: responses for someone else's, a stale or an unsupported alert are ignored", "[alerts]")
{
    n2k_alerts_cfg_t c = cfg_with(2, INPUT_ALARM_ALARM);
    start(&c);
    n2k_alerts_tick(&a, true, 0x02, 10, &out);
    uint8_t p[N2K_ALERT_RESPONSE_LEN];
    response(2, N2K_ALERT_TYPE_ALARM, 1, NAME + 1, N2K_ALERT_RESPONSE_ACKNOWLEDGE, p);
    TEST_ASSERT_FALSE(n2k_alerts_on_response(&a, p, sizeof(p)));  // another device's
    response(3, N2K_ALERT_TYPE_ALARM, 1, NAME, N2K_ALERT_RESPONSE_ACKNOWLEDGE, p);
    TEST_ASSERT_FALSE(n2k_alerts_on_response(&a, p, sizeof(p)));  // not active
    response(2, N2K_ALERT_TYPE_ALARM, 9, NAME, N2K_ALERT_RESPONSE_ACKNOWLEDGE, p);
    TEST_ASSERT_FALSE(n2k_alerts_on_response(&a, p, sizeof(p)));  // stale occurrence
    response(2, N2K_ALERT_TYPE_WARNING, 1, NAME, N2K_ALERT_RESPONSE_ACKNOWLEDGE, p);
    TEST_ASSERT_FALSE(n2k_alerts_on_response(&a, p, sizeof(p)));  // other type
    response(2, N2K_ALERT_TYPE_ALARM, 1, NAME, N2K_ALERT_RESPONSE_SILENCE, p);
    TEST_ASSERT_FALSE(n2k_alerts_on_response(&a, p, sizeof(p)));  // silence not offered
    response(9, N2K_ALERT_TYPE_ALARM, 1, NAME, N2K_ALERT_RESPONSE_ACKNOWLEDGE, p);
    TEST_ASSERT_FALSE(n2k_alerts_on_response(&a, p, sizeof(p)));  // no such input
    TEST_ASSERT_FALSE(n2k_alerts_on_response(&a, p, 10));         // too short
    n2k_alerts_tick(&a, true, 0x02, 1010, &out);
    TEST_ASSERT_EQUAL(N2K_ALERT_STATE_ACTIVE, state_sent());
}

TEST_CASE("alerts: config copy uses the SignalK notification's message", "[alerts]")
{
    device_config_t d;
    memset(&d, 0, sizeof(d));
    d.n2k_alerts = true;
    d.bank_id = 0;
    d.input_bank_id = 1;
    snprintf(d.inputs[0].name, sizeof(d.inputs[0].name), "Bilge float");
    d.inputs[0].alarm = INPUT_ALARM_ALARM;
    snprintf(d.inputs[1].alarm_msg, sizeof(d.inputs[1].alarm_msg), "Fridge door open");
    n2k_alerts_cfg_t c;
    n2k_alerts_cfg_from(&d, &c);
    TEST_ASSERT_TRUE(c.enabled);
    TEST_ASSERT_TRUE(c.inputs_on);
    TEST_ASSERT_EQUAL(INPUT_ALARM_ALARM, c.level[0]);
    TEST_ASSERT_EQUAL_STRING("Bilge float active", c.message[0]);
    TEST_ASSERT_EQUAL_STRING("Fridge door open", c.message[1]);
    d.input_bank_id = 0;
    n2k_alerts_cfg_from(&d, &c);
    TEST_ASSERT_FALSE(c.inputs_on);
}

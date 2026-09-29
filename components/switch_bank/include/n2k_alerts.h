// Input alarms as NMEA 2000 alerts (plan 21): the same per-input alarm that
// raises a SignalK notification (plan 10, issue #3) also raises an alert on
// the bus, so an MFD sounds it without a SignalK server. Plain C, no bus
// access: n2k_bridge feeds it inputs and the clock, and sends what it asks
// for. Not thread-safe; n2k_bridge calls it from its own task only.
//
//   raise:  126985 (text), then 126983 state Active, at once.
//   active: 126983 every second, 126985 every 10 s (canboat's intervals).
//   clear:  126983 state Normal, N2K_ALERTS_CLEAR_REPEATS times a second
//           apart, then nothing.
//   ack:    a 126984 Acknowledge for one of ours turns it Acknowledged
//           until it clears. Temporary silence isn't offered.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device_config.h"
#include "n2k_alert_pgn.h"

#ifdef __cplusplus
extern "C" {
#endif

#define N2K_ALERTS_STATUS_PERIOD_MS 1000
#define N2K_ALERTS_TEXT_PERIOD_MS 10000
#define N2K_ALERTS_CLEAR_REPEATS 3

// The settings the alerts need, copied out of device_config_t so the NMEA
// 2000 task never touches the full struct.
typedef struct {
    bool enabled;    // n2k_alerts
    bool inputs_on;  // device_config_input_bank_usable()
    input_alarm_t level[BOARD_CHANNELS];
    // The SignalK notification's message: alarm_msg, or "<name> active".
    char message[BOARD_CHANNELS][DEVICE_CONFIG_NAME_MAX + 8];
} n2k_alerts_cfg_t;

void n2k_alerts_cfg_from(const device_config_t *cfg, n2k_alerts_cfg_t *out);

typedef struct {
    // 126983 payloads are fixed length, 126985 up to N2K_ALERT_TEXT_MAX_LEN.
    void (*send_status)(const uint8_t *data, size_t len, void *arg);
    void (*send_text)(const uint8_t *data, size_t len, void *arg);
    void *arg;
} n2k_alerts_out_t;

typedef struct {
    bool active;
    input_alarm_t level;  // while active
    uint8_t occurrence;
    bool acked;
    uint64_t ack_name;
    bool text_due;
    bool status_due;
    // A cleared alert still being reported Normal.
    uint8_t clear_left;
    input_alarm_t clear_level;
    uint8_t clear_occurrence;
} n2k_alerts_chan_t;

typedef struct {
    n2k_alerts_cfg_t cfg;
    uint8_t instance;      // the input bank id
    uint64_t source_name;  // our ISO NAME
    uint32_t status_at;
    uint32_t text_at;
    n2k_alerts_chan_t ch[BOARD_CHANNELS];
} n2k_alerts_t;

void n2k_alerts_init(n2k_alerts_t *a, const n2k_alerts_cfg_t *cfg, uint8_t instance, uint64_t source_name,
                     uint32_t now_ms);

// Settings change live. An active alert whose severity changed is cleared
// and raised again at the new one (the type is part of an alert's
// identity); one whose message changed resends its text now.
void n2k_alerts_set_config(n2k_alerts_t *a, const n2k_alerts_cfg_t *cfg);

// Call often (every loop of the NMEA 2000 task): raises, clears and
// repeats alerts from the current input readings.
void n2k_alerts_tick(n2k_alerts_t *a, bool inputs_ready, uint8_t input_mask, uint32_t now_ms,
                     const n2k_alerts_out_t *out);

// A received 126984. True when it acknowledged one of our active alerts
// (sent on the next tick).
bool n2k_alerts_on_response(n2k_alerts_t *a, const uint8_t *data, size_t len);

// The 126983 an active (or, `clearing`, a cleared) channel `ch` (1-8)
// reports. For tests and for n2k_alerts_tick() itself.
void n2k_alerts_status_of(const n2k_alerts_t *a, uint8_t ch, bool clearing, n2k_alert_t *out);

// canboat alert type for an input alarm level.
uint8_t n2k_alerts_type_of(input_alarm_t level);

#ifdef __cplusplus
}
#endif

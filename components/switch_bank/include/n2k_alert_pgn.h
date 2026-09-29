// NMEA 2000 alert payloads (layouts from canboat), for input alarms on the
// bus (plan 21):
//   126983 Alert:          an alert's identity, status flags and state.
//   126984 Alert Response: an MFD acknowledging (or silencing) one.
//   126985 Alert Text:     its description and location strings.
// All three start with the same 16-byte header naming the alert.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define N2K_ALERT_PGN 126983
#define N2K_ALERT_PGN_RESPONSE 126984
#define N2K_ALERT_PGN_TEXT 126985
#define N2K_ALERT_LEN 28
#define N2K_ALERT_RESPONSE_LEN 25
// Fast-packet maximum; 126985 is variable length.
#define N2K_ALERT_TEXT_MAX_LEN 223

// canboat ALERT_TYPE, ALERT_CATEGORY, ALERT_TRIGGER_CONDITION,
// ALERT_THRESHOLD_STATUS, ALERT_STATE, ALERT_RESPONSE_COMMAND.
#define N2K_ALERT_TYPE_EMERGENCY 1
#define N2K_ALERT_TYPE_ALARM 2
#define N2K_ALERT_TYPE_WARNING 5
#define N2K_ALERT_TYPE_CAUTION 8
#define N2K_ALERT_CATEGORY_TECHNICAL 1
#define N2K_ALERT_TRIGGER_AUTO 1
#define N2K_ALERT_THRESHOLD_NORMAL 0
#define N2K_ALERT_THRESHOLD_EXCEEDED 1
#define N2K_ALERT_STATE_NORMAL 1
#define N2K_ALERT_STATE_ACTIVE 2
#define N2K_ALERT_STATE_ACKNOWLEDGED 4
#define N2K_ALERT_RESPONSE_ACKNOWLEDGE 0
#define N2K_ALERT_RESPONSE_SILENCE 1

// ISO NAME "not available", for an acknowledge source nobody has filled.
#define N2K_ALERT_NAME_NONE UINT64_C(0xFFFFFFFFFFFFFFFF)

// The header every alert PGN starts with: which alert, from which device.
typedef struct {
    uint8_t type;      // N2K_ALERT_TYPE_*
    uint8_t category;  // N2K_ALERT_CATEGORY_*
    uint8_t system;
    uint8_t subsystem;
    uint16_t id;
    uint64_t source_name;  // data source's ISO NAME
    uint8_t instance;
    uint8_t index;
    uint8_t occurrence;
} n2k_alert_id_t;

typedef struct {
    n2k_alert_id_t id;
    bool silenced;
    bool acknowledged;
    bool escalated;
    bool silence_supported;
    bool ack_supported;
    bool escalation_supported;
    uint64_t ack_name;   // who acknowledged it, N2K_ALERT_NAME_NONE if nobody
    uint8_t trigger;     // N2K_ALERT_TRIGGER_*
    uint8_t threshold;   // N2K_ALERT_THRESHOLD_*
    uint8_t priority;
    uint8_t state;       // N2K_ALERT_STATE_*
} n2k_alert_t;

typedef struct {
    n2k_alert_id_t id;
    uint64_t ack_name;
    uint8_t command;  // N2K_ALERT_RESPONSE_*, or 2/3 for test on/off
} n2k_alert_response_t;

void n2k_alert_encode(const n2k_alert_t *a, uint8_t out[N2K_ALERT_LEN]);

// 126985 with language 0 (English, US). Strings are UTF-8: pure ASCII is
// sent as-is (control byte 1), anything else as UCS-2 (control byte 0), as
// the NMEA2000 library's own AddVarStr() does. Each string is cut to fit
// `size` (at most N2K_ALERT_TEXT_MAX_LEN); never splits a character.
// Returns the payload length.
size_t n2k_alert_encode_text(const n2k_alert_id_t *id, const char *description, const char *location, uint8_t *out,
                             size_t size);

// False (and `out` untouched) when the payload is too short.
bool n2k_alert_decode_response(const uint8_t *data, size_t len, n2k_alert_response_t *out);

#ifdef __cplusplus
}
#endif

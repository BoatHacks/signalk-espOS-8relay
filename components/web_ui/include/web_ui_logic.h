// The relay page's REST payloads, kept free of HTTP and hardware so they
// can be host-tested. web_ui.c wires them to espOS's HTTP server.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Everything GET /api/v1/relays reports.
typedef struct {
    const device_config_t *cfg;  // names, modes, input links
    uint8_t relay_mask;          // bit n-1 = relay n on
    uint8_t input_mask;          // bit n-1 = input n on (debounced, inverted)
    bool inputs_ready;           // false until inputs have settled after boot
    // What last switched each relay ("signalk", "boot", ...; NULL = not
    // known yet) and how many seconds ago.
    const char *last_source[BOARD_CHANNELS];
    uint32_t last_change_ago_s[BOARD_CHANNELS];
} web_ui_view_t;

// {"relays":[{"channel":1,"name":"…","on":false,"momentary":false,
//   "input":0,"inputToggle":false,"maxOnS":0,"lastSource":"boot",
//   "lastChangeAgoS":12},…],"inputs":[{"channel":1,"name":"…","on":false},…],
//  "inputsReady":true}; an input's "on" is null until inputs_ready.
// Returns a malloc'ed string, or NULL when out of memory.
char *web_ui_state_json(const web_ui_view_t *view);

// Everything GET /api/v1/relays/status reports.
typedef struct {
    char version[32];
    char hostname[33];
    bool net_up;
    char net_iface[16];  // espOS's name for the interface, e.g. "eth"
    char ip[16];
    bool sk_enabled;
    bool sk_connected;
    char sk_server[72];  // "host:port", "" when none is known
    bool n2k_started;
    uint8_t n2k_address;
    bool n2k_traffic;
} web_ui_status_t;

// {"version":"…","hostname":"…","network":{"up":true,"interface":"eth",
//  "ip":"…"},"signalk":{"enabled":true,"connected":true,"server":"…"},
//  "nmea2000":{"started":true,"address":35,"traffic":true}}; empty strings
// become null. Returns a malloc'ed string, or NULL when out of memory.
char *web_ui_status_json(const web_ui_status_t *st);

// Relay channel from a request path "<prefix>/<n>", n = 1..BOARD_CHANNELS
// written as one digit. 0 for anything else (a query string is ignored).
uint8_t web_ui_parse_channel(const char *uri, const char *prefix);

// {"on": true|false} → *on. False for anything else.
bool web_ui_parse_on(const char *body, bool *on);

#ifdef __cplusplus
}
#endif

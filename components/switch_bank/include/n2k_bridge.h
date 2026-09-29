// NMEA 2000 side of the switch bank (SPEC.md §6.2; plan 06): joins the bus
// as a load controller, broadcasts PGN 127501 for the relay and input banks,
// applies PGN 127502 to the relays, and raises input alarms as alerts
// (126983/126985, acknowledged by 126984; plan 21). Device builds only.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_err_t (*set_relay)(uint8_t channel, bool on);
    uint8_t (*relay_mask)(void);
    bool (*inputs_ready)(void);
    uint8_t (*input_mask)(void);
} n2k_bridge_io_t;

// Start CAN and the NMEA 2000 task. Bank ids are read once (they need a
// restart to change).
esp_err_t n2k_bridge_start(const n2k_bridge_io_t *io, const device_config_t *cfg);

// Settings saved: input alarm levels, messages and the n2k_alerts switch
// apply live to the NMEA 2000 alerts (plan 21). Callable from any task.
void n2k_bridge_update_config(const device_config_t *cfg);

// Relay or input state changed: send 127501 now rather than at the next
// periodic broadcast. Callable from any task.
void n2k_bridge_state_changed(void);

// Every received NMEA 2000 message's PGN and data bytes, for a use that
// isn't the switch-bank control PGN handled internally above -- e.g.
// components/schedule's position decoding (PGN 129025/129029, issue #9,
// plan 16). switch_bank owns the one CAN/N2K bus on the board and the
// library allows only one SetMsgHandler() for it, so this is the seam:
// switch_bank stays scoped to switch banks, forwarding raw messages rather
// than knowing what any of them mean. Same shape as
// relay_ctrl_add_listener()/input_sense_add_listener() elsewhere in this
// codebase. `data`/`len` are borrowed for the call only -- copy, don't
// hold the pointer. Runs on the NMEA 2000 task; keep it short.
typedef void (*n2k_bridge_msg_listener_t)(uint32_t pgn, const uint8_t *data, uint8_t len, void *arg);
#define N2K_BRIDGE_MAX_MSG_LISTENERS 4
esp_err_t n2k_bridge_add_msg_listener(n2k_bridge_msg_listener_t cb, void *arg);

typedef struct {
    bool started;    // CAN open and the NMEA 2000 task running
    uint8_t address; // our current source address (after address claim)
    bool traffic;    // a frame from the bus arrived in the last 10 s
} n2k_bridge_status_t;

// For the relay page. Callable from any task.
void n2k_bridge_get_status(n2k_bridge_status_t *out);

#ifdef __cplusplus
}
#endif

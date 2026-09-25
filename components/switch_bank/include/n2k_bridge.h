// NMEA 2000 side of the switch bank (SPEC.md §6.2; plan 06): joins the bus
// as a load controller, broadcasts PGN 127501 for the relay and input banks,
// and applies PGN 127502 to the relays. Device builds only.
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

// Relay or input state changed: send 127501 now rather than at the next
// periodic broadcast. Callable from any task.
void n2k_bridge_state_changed(void);

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

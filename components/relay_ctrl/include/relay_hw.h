// The real hardware behind relay_ctrl: the TCA9554 on the board's I2C bus,
// NVS for `hold` states, and the system clock. Device builds only.
#pragma once

#include "esp_err.h"
#include "relay_ctrl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Open the I2C bus and NVS namespace and fill `out`. Call once, after
// espos_start() (which initialises NVS).
esp_err_t relay_hw_create(relay_ctrl_hw_t *out);

// Erase the persisted `hold` state (factory reset, plan 14). Relays
// themselves are unaffected until the next boot, when they take their
// fail-safe policy's boot state (SPEC.md §3.2) with nothing to restore.
// relay_hw_create() must have run first.
esp_err_t relay_hw_clear_hold_state(void);

#ifdef __cplusplus
}
#endif

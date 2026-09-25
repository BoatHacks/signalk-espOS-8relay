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

#ifdef __cplusplus
}
#endif

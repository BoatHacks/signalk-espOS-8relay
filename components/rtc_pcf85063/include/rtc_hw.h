// The real hardware behind rtc_pcf85063: the PCF85063 on the board's shared
// I2C bus (board_i2c.h). Device builds only.
#pragma once

#include "esp_err.h"
#include "pcf85063.h"

#ifdef __cplusplus
extern "C" {
#endif

// Add the PCF85063 to the board's I2C bus (creating it if this is the first
// device on it) and fill `out`. Call once, from the same start-up path as
// relay_hw_create() -- either order is fine, both share the one bus.
esp_err_t rtc_hw_create(pcf85063_bus_t *out);

#ifdef __cplusplus
}
#endif

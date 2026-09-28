// The board's one I2C bus (SCL 41 / SDA 42), shared by the relay expander
// (TCA9554) and the real-time clock (PCF85063). Device builds only: host
// tests never call this, they hand relay_ctrl/rtc_pcf85063 a fake bus.
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Create the bus on first call; later calls just return the same handle.
// Not thread-safe -- call only from the single-threaded start-up path
// (before the I/O task and any other consumer could race the first call).
esp_err_t board_i2c_bus(i2c_master_bus_handle_t *out);

#ifdef __cplusplus
}
#endif

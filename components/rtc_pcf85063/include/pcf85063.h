// PCF85063 real-time clock, register level. Bus access goes through function
// pointers so host tests can use a fake chip (same shape as relay_ctrl's
// tca9554.h). I2C address 0x51, on the board's shared bus (board_i2c.h).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCF85063_I2C_ADDR 0x51

#define PCF85063_REG_CTRL1 0x00
#define PCF85063_REG_CTRL2 0x01
#define PCF85063_REG_SECONDS 0x04  // bit 7 = OS (oscillator stop / time invalid)
#define PCF85063_REG_MINUTES 0x05
#define PCF85063_REG_HOURS 0x06
#define PCF85063_REG_DAYS 0x07
#define PCF85063_REG_WEEKDAYS 0x08
#define PCF85063_REG_MONTHS 0x09
#define PCF85063_REG_YEARS 0x0A

// Seconds..Years is one contiguous auto-incrementing block (datasheet
// table 6), read or written in a single burst so the fields can never
// straddle a rollover.
#define PCF85063_TIME_BLOCK_LEN 7

typedef struct {
    esp_err_t (*read)(void *ctx, uint8_t reg, uint8_t *buf, size_t len);
    esp_err_t (*write)(void *ctx, uint8_t reg, const uint8_t *buf, size_t len);
    void *ctx;
} pcf85063_bus_t;

// Calendar fields, BCD already decoded. Deliberately not `struct tm`, same
// reasoning as espos_time_parts_t: this header stays free of platform types.
// `year` is the full year (2000-2099: all the chip's 2-digit register can
// hold); `wday` is 0 = Sunday, written but never read back (nothing here
// needs it -- the schedule evaluator derives weekday from unix time itself).
typedef struct {
    int32_t year;
    uint8_t month;  // 1-12
    uint8_t day;    // 1-31
    uint8_t hour;   // 0-23
    uint8_t minute; // 0-59
    uint8_t second; // 0-59
    uint8_t wday;   // 0-6, 0 = Sunday
} pcf85063_datetime_t;

// Force 24-hour mode and make sure the oscillator isn't stopped (Control_1 =
// 0x00), without touching the stored time or its OS/validity flag. Call once
// at boot; safe to call again.
esp_err_t pcf85063_init(const pcf85063_bus_t *bus);

// Read the stored calendar time. `*valid` is false when the OS flag is set
// (the oscillator stopped at some point -- no battery, first power-up, or a
// brownout -- so the stored fields cannot be trusted), in which case `*out`
// is left untouched. An I2C error leaves both untouched.
esp_err_t pcf85063_read_datetime(const pcf85063_bus_t *bus, pcf85063_datetime_t *out, bool *valid);

// Write the calendar time and clear the OS flag (a full write of the
// Seconds register with bit 7 = 0 does that per the datasheet). `year`
// outside 2000-2099 is clamped to the nearest end the chip can represent.
esp_err_t pcf85063_write_datetime(const pcf85063_bus_t *bus, const pcf85063_datetime_t *dt);

#ifdef __cplusplus
}
#endif

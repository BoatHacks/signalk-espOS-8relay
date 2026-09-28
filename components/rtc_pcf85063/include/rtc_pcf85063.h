// The board's real-time clock (PCF85063), in unix-millisecond terms: what
// main.c hands to espos_time_set() at boot (issue #9, plan 16) and writes
// back to on ESPOS_EVENT_TIME_SYNCED. Keeps time across power loss, so
// schedules work before (or without) the network -- unlike espos_time's own
// ESPOS_TIME_SRC_RTC, which is the ESP32's RTC memory and only survives a
// deep sleep, not a power cut.
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "pcf85063.h"

#ifdef __cplusplus
extern "C" {
#endif

// Set 24-hour mode and remember `bus` for the calls below. Call once at
// boot, before rtc_pcf85063_get_time()/set_time(). Safe to call again (e.g.
// a test resetting state) -- it replaces the stored bus each time.
esp_err_t rtc_pcf85063_init(const pcf85063_bus_t *bus);

// The chip's stored time as unix milliseconds (00:00:00.000 UTC assumed --
// the chip has no timezone concept, and neither does espos_time; SPEC.md
// §4's timezone setting is a display concern only). ESP_ERR_INVALID_STATE
// before rtc_pcf85063_init(), or when the chip's OS flag says its time
// cannot be trusted (no battery, first power-up, a brownout).
esp_err_t rtc_pcf85063_get_time(int64_t *out_unix_ms);

// Write `unix_ms` (UTC) to the chip and clear its OS flag.
// ESP_ERR_INVALID_STATE before rtc_pcf85063_init(). ESP_ERR_INVALID_ARG for
// a non-positive `unix_ms`.
esp_err_t rtc_pcf85063_set_time(int64_t unix_ms);

// Tests only: forget the stored bus.
void rtc_pcf85063_reset(void);

#ifdef __cplusplus
}
#endif

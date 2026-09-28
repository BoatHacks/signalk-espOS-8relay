// Sunrise/sunset, in absolute UTC instants, for a schedule's "sunrise±m" /
// "sunset±m" entries (issue #9, plan 16). NOAA's low-precision solar
// position algorithm (the one behind the NOAA Solar Calculator spreadsheet):
// accurate to about a minute, which is what a schedule's own minute
// resolution needs. Pure math, no device or clock dependency -- host-tested
// against externally cross-checked instants.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SUN_TRANSITION,  // an ordinary day: the sun both rises and sets
    SUN_ALWAYS_UP,   // polar day: the sun never goes below the horizon
    SUN_ALWAYS_DOWN, // polar night: the sun never comes above the horizon
} sun_day_kind_t;

typedef struct {
    sun_day_kind_t kind;
    // Valid only when kind == SUN_TRANSITION. Absolute UTC instants -- for a
    // longitude far from Greenwich (or a day near a DST-irrelevant equation-
    // of-time swing) these can fall just outside the UTC calendar day named
    // by `year`/`month`/`day` below, which is expected: the day parameter
    // only picks which of Earth's ~365 yearly positions to compute, not a
    // UTC-midnight-to-midnight window the answer is clamped into.
    int64_t sunrise_unix_ms;
    int64_t sunset_unix_ms;
} sun_times_t;

// `year`/`month`/`day` is the calendar day to compute for -- the caller
// decides which one (a UTC day, a local day, whichever the schedule
// evaluator needs) since this function has no notion of timezone. `lat` is
// degrees north (negative south), `lon_east` degrees east (negative west,
// SignalK's convention for navigation.position.longitude).
void sun_times(int32_t year, uint8_t month, uint8_t day, double lat, double lon_east, sun_times_t *out);

#ifdef __cplusplus
}
#endif

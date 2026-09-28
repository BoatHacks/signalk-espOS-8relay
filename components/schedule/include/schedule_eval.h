// Turns device_config's schedule_cfg_t entries (issue #9, plan 16) into
// relay_ctrl_set(..., RELAY_SRC_SCHEDULE) calls, edge-triggered: a manual
// command in between two scheduled transitions stands until the next one,
// the same rule input overrides already follow.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device_config.h"
#include "espos_time.h"  // espos_time_parts_t

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_err_t (*set_relay)(uint8_t channel, bool on);  // relay_ctrl_set(channel, on, RELAY_SRC_SCHEDULE)
} schedule_eval_io_t;

void schedule_eval_init(const schedule_eval_io_t *io);

// Evaluate every entry and switch relays at their transitions. Call this
// roughly once a second -- schedules have minute resolution, nothing here
// needs relay_ctrl's own 10 ms tick rate.
//
// `time_valid` false (no RTC time yet, and neither SNTP nor SignalK have
// set the clock) makes every entry do nothing, and raises the
// "scheduleNoTime" health warning instead; `local` is then not read, pass
// anything. `local` the rest of the time is the current local time
// (espos_time_parts()): schedules compare against local wall-clock time,
// not UTC, the same as a person reading a clock would expect from an
// "18:30" setting.
//
// `lat`/`lon` (position_get(), plan 16 step 2) are read only by an entry
// that actually references sunrise/sunset; a clock-only or repeat-mode
// config never touches them.
//
// The very first evaluation -- ever, or after time_valid returns to true,
// or after a previously-disabled entry (relay=0) becomes enabled again --
// brings that entry's relay in line with what it should already be,
// unconditionally, rather than waiting for the next edge: the point of an
// RTC-backed schedule is that a reboot during what should be an "on"
// period still finds the light on, not off until the next transition next
// day.
void schedule_eval_tick(const device_config_t *cfg, bool time_valid, const espos_time_parts_t *local, double lat,
                         double lon);

void schedule_eval_reset(void);  // tests only

#ifdef __cplusplus
}
#endif

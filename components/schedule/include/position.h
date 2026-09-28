// The position to use for a schedule's sunrise/sunset calculation (issue
// #9, plan 16): whichever live source device_config.position_source names,
// falling back to the configured fixed position when that source has
// nothing yet or nothing recent.
#pragma once

#include <stdint.h>

#include "device_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// A live reading older than this counts as stale. A fixed constant rather
// than another setting: a boat can drift for a few minutes without moving
// sunrise/sunset by anything a schedule's minute resolution would notice.
#define POSITION_STALE_MS (10u * 60u * 1000u)

// Always succeeds: `*lat`/`*lon` end up either the live reading from
// cfg->position_source (when it has one no older than POSITION_STALE_MS)
// or cfg->fallback_lat/fallback_lon.
void position_get(const device_config_t *cfg, uint32_t now_ms, double *lat, double *lon);

#ifdef __cplusplus
}
#endif

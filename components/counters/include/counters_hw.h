// The real store behind counters: one NVS blob, its own namespace (not
// espOS's settings store -- these aren't settings, plan 11), and the system
// clock. Device builds only.
#pragma once

#include "counters.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Open the NVS namespace and fill `out`. Call once, after espos_start()
// (which initialises NVS).
esp_err_t counters_hw_create(counters_hw_t *out);

// Erase the persisted blob (factory reset, if ever wired up -- see plan 11's
// open questions). counters_hw_create() must have run first.
esp_err_t counters_hw_clear(void);

#ifdef __cplusplus
}
#endif

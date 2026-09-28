// The most recently received navigation.position from the SignalK stream
// (issue #9, plan 16, position_source=signalk, the default), for the
// schedule evaluator's sunrise/sunset calculation.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "espos_sk_parse.h"  // espos_sk_update_t

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t (*now_ms)(void);
} position_sk_hw_t;

// Remembers `hw` for stamping a received position's arrival time. Call
// once at boot, before subscribing position_sk_on_update.
void position_sk_init(const position_sk_hw_t *hw);

// Register directly as an espos_sk_subscribe() callback:
// espos_sk_subscribe("navigation.position", 0, position_sk_on_update, NULL).
// Parses `u->value_json` as {"latitude":<number>,"longitude":<number>}
// (SignalK's own shape for navigation.position); anything else is ignored.
void position_sk_on_update(const espos_sk_update_t *u, void *arg);

// The last received position and when it arrived (hw.now_ms() at the time,
// for the caller to judge staleness against its own now_ms). False if
// nothing has arrived yet, or position_sk_init() hasn't run.
bool position_sk_get(double *lat, double *lon, uint32_t *received_ms);

void position_sk_reset(void);  // tests only

#ifdef __cplusplus
}
#endif

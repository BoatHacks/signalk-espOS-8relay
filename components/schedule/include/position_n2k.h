// The most recently decoded position from the board's own NMEA 2000 bus
// (issue #9, plan 16, position_source=n2k), for the schedule evaluator's
// sunrise/sunset calculation. Wires into switch_bank's raw-message
// listener (n2k_bridge_add_msg_listener) rather than owning any bus itself.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t (*now_ms)(void);
} position_n2k_hw_t;

// Remembers `hw` for stamping a decoded fix's arrival time. Call once at
// boot, before registering position_n2k_on_msg as a listener.
void position_n2k_init(const position_n2k_hw_t *hw);

// Matches n2k_bridge_msg_listener_t's signature exactly: register directly
// with n2k_bridge_add_msg_listener(position_n2k_on_msg, NULL). Decodes PGN
// 129025 and 129029, ignores everything else.
void position_n2k_on_msg(uint32_t pgn, const uint8_t *data, uint8_t len, void *arg);

// The last decoded fix and when it arrived (hw.now_ms() at the time, for
// the caller to judge staleness against its own now_ms). False if nothing
// has been decoded yet, or position_n2k_init() hasn't run.
bool position_n2k_get(double *lat, double *lon, uint32_t *received_ms);

void position_n2k_reset(void);  // tests only

#ifdef __cplusplus
}
#endif

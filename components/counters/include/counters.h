// Cycle counters and runtime hours for every relay and input (SPEC.md §3,
// §6; ARCHITECTURE.md §2; plan 11, issue #4). Pure core, fed from the relay
// and input listeners in main.c so every source is counted (SignalK, NMEA
// 2000, web, input, a pulse ending, fail-safe, max-on) -- not just direct
// commands.
//
// Kept in RAM and flushed to a store (NVS on the device, a fake in tests) at
// most every COUNTERS_SAVE_INTERVAL_MS and only when something changed, plus
// once on a clean restart via counters_flush_now(). A power cut can lose up
// to that interval's worth of counting; say so in the manual. One blob for
// all 16 channels, not 32 keys, and not in espOS's settings store (these
// aren't settings, SPEC.md/plan 11).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { COUNTERS_RELAY, COUNTERS_INPUT } counters_kind_t;

typedef struct {
    uint32_t cycles;      // off->on transitions
    uint32_t runtime_s;   // whole seconds spent on, persisted snapshot
} counters_channel_t;

// One NVS blob for every relay and input channel.
typedef struct {
    counters_channel_t relays[BOARD_CHANNELS];
    counters_channel_t inputs[BOARD_CHANNELS];
} counters_data_t;

// Persists the whole blob. ESP_ERR_NOT_FOUND (or any error) from load()
// means "nothing stored yet": every channel starts at zero.
typedef struct {
    esp_err_t (*load)(void *ctx, counters_data_t *out);
    esp_err_t (*save)(void *ctx, const counters_data_t *data);
    void *ctx;
} counters_store_t;

typedef struct {
    counters_store_t store;
    uint32_t (*now_ms)(void);
} counters_hw_t;

// How long changed counters may wait before being written to flash.
#define COUNTERS_SAVE_INTERVAL_MS (10u * 60u * 1000u)

// Loads the store (a load failure just means "start at zero"). Call once at
// boot, before any counters_on_change() calls.
esp_err_t counters_init(const counters_hw_t *hw);

// Report channel `channel` (1-BOARD_CHANNELS)'s on/off state, from a relay
// or input listener -- every source, direct or automatic. Counts an off->on
// edge as one cycle; a repeated "on" (or "off") does nothing.
//
// The first report ever for a channel seeds its current state without
// counting a cycle or losing runtime: relay_ctrl never notifies listeners
// of a relay's boot state (main.c must call this once per relay right after
// relay_ctrl_init(), the same way it seeds web_ui_relay_changed(ch, "boot")),
// while input_sense's first settled reading arrives through the normal
// listener and is seeded the same way. A channel already on when first seen
// starts accruing runtime from that moment.
void counters_on_change(counters_kind_t kind, uint8_t channel, bool on, uint32_t now_ms);

// Cycles and total runtime (seconds) for one channel, live: a channel
// currently on includes the seconds elapsed since it turned on, not just
// what was last folded into the persisted snapshot.
void counters_get(counters_kind_t kind, uint8_t channel, uint32_t now_ms, uint32_t *cycles, uint32_t *runtime_s);

// Zero one channel's cycles and runtime (its "on/off" state and any running
// on-period are unaffected) and flush at once: a deliberate, infrequent user
// action, not the automatic counting this component throttles.
void counters_reset(counters_kind_t kind, uint8_t channel, uint32_t now_ms);

// Call every ~10 ms (e.g. from the I/O tick, alongside relay_ctrl_tick()):
// folds any running on-period into the persisted snapshot and saves, at most
// once per COUNTERS_SAVE_INTERVAL_MS and only if something changed.
void counters_tick(uint32_t now_ms);

// Unconditional save of the current state (folding any running on-period
// first), regardless of the throttle or whether anything changed. Call from
// a clean-restart shutdown handler (OTA, a settings restart) so a deliberate
// restart never loses counting.
void counters_flush_now(uint32_t now_ms);

// Tests only: forget all state.
void counters_reset_all(void);

#ifdef __cplusplus
}
#endif

// The only code that switches relays (SPEC.md §2, §3; plan 03). Drives the
// TCA9554, runs momentary pulses, applies fail-safe policy at boot and on
// SignalK loss, stores the state of `hold` relays, and tells listeners about
// changes. Thread-safe: commands arrive from SignalK, NMEA 2000 and input
// tasks.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device_config.h"
#include "esp_err.h"
#include "tca9554.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RELAY_SRC_SK,
    RELAY_SRC_N2K,
    RELAY_SRC_INPUT,       // an input override
    RELAY_SRC_PULSE_END,   // a momentary pulse ran out
    RELAY_SRC_FAILSAFE,    // SignalK was lost
    RELAY_SRC_WEB,         // the device's own relay page
} relay_source_t;

// Persists the on/off state of `hold` relays (bit n-1 = relay n).
typedef struct {
    esp_err_t (*load)(void *ctx, uint8_t *mask);
    esp_err_t (*save)(void *ctx, uint8_t mask);
    void *ctx;
} relay_state_store_t;

typedef struct {
    tca9554_bus_t expander;
    relay_state_store_t store;
    uint32_t (*now_ms)(void);
} relay_ctrl_hw_t;

// Called after a relay changes, outside relay_ctrl's lock. `mask` is the
// full new state. Keep it short: it runs on the commanding task.
typedef void (*relay_listener_t)(uint8_t channel, bool on, relay_source_t src, uint8_t mask, void *arg);

// How long a changed `hold` state may wait before being written to flash.
#define RELAY_CTRL_SAVE_DELAY_MS 5000

// Set every relay per its fail-safe policy. Cold boot (expander just powered
// up): `hold` relays from the store, others off. Warm boot (ESP32 reset, the
// expander kept its outputs): `hold` relays keep their current state, so
// they don't switch; others go off.
esp_err_t relay_ctrl_init(const relay_ctrl_hw_t *hw, const device_config_t *cfg);

// Apply settings that change live (mode, pulse time, fail-safe policy).
// Never switches a relay on. A relay that is on and becomes momentary starts
// its pulse now, so it switches off after the pulse time.
void relay_ctrl_update_config(const device_config_t *cfg);

// Switch relay `channel` (1-8). The most recent call wins, whatever its
// source. "On" for a momentary relay starts (or restarts) its pulse.
// ESP_ERR_INVALID_ARG for a bad channel; an I2C error leaves the reported
// state at what was last confirmed on the chip.
esp_err_t relay_ctrl_set(uint8_t channel, bool on, relay_source_t src);

bool relay_ctrl_get(uint8_t channel);
uint8_t relay_ctrl_get_mask(void);

esp_err_t relay_ctrl_add_listener(relay_listener_t cb, void *arg);

// SignalK has been unreachable for the grace period: switch off every relay
// whose effective policy is default-safe.
void relay_ctrl_sk_lost(void);

// Call every ~10 ms: ends momentary pulses and saves `hold` state when due.
void relay_ctrl_tick(void);

// Tests only: forget all state and listeners.
void relay_ctrl_reset(void);

#ifdef __cplusplus
}
#endif

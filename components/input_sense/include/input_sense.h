// The 8 isolated digital inputs (SPEC.md §2, §3.1; plan 04): polled,
// debounced, optionally inverted, reported to listeners, and applied to
// relays that name an input as their override.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Raw pin levels, bit n-1 = input n (1 = pin high).
    uint8_t (*read_pins)(void *ctx);
    void *ctx;
    uint32_t (*now_ms)(void);
} input_sense_hw_t;

// Switches a relay for an override. relay_ctrl_set() with RELAY_SRC_INPUT on
// the device; a recorder in tests.
typedef esp_err_t (*input_override_fn_t)(uint8_t relay_channel, bool on);

// Called on a debounced change, and once per input when the first readings
// settle. `mask` is every input's state.
typedef void (*input_listener_t)(uint8_t channel, bool on, uint8_t mask, void *arg);

esp_err_t input_sense_init(const input_sense_hw_t *hw, const device_config_t *cfg, input_override_fn_t override);

// Apply settings that change live (debounce, invert, overrides).
void input_sense_update_config(const device_config_t *cfg);

// Call every few milliseconds, from one task only.
void input_sense_poll(void);

// False until every input has had one stable reading after start-up.
bool input_sense_ready(void);

// Debounced states, bit n-1 = input n (0 before input_sense_ready()).
uint8_t input_sense_get_mask(void);

esp_err_t input_sense_add_listener(input_listener_t cb, void *arg);

// Tests only.
void input_sense_reset(void);

#ifdef __cplusplus
}
#endif

// SignalK side of the switch bank (SPEC.md §6.1, §6.1a; plan 05): publishes
// relay and input state on the enabled path trees, accepts relay PUTs on
// each, declares names, and reports a lost SignalK server to relay control.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*sk_put_handler_t)(const char *path, const char *value_json, void *arg);

// The espOS SignalK calls the bridge uses (espos_sk_* on the device).
typedef struct {
    esp_err_t (*publish_number)(const char *path, double value);
    esp_err_t (*publish_string)(const char *path, const char *value);
    esp_err_t (*declare_meta)(const char *path, const char *meta_json, uint32_t period_ms);
    esp_err_t (*put_register)(const char *path, sk_put_handler_t cb, void *arg);
} sk_api_t;

// Relays, inputs and the clock (relay_ctrl / input_sense on the device).
typedef struct {
    esp_err_t (*set_relay)(uint8_t channel, bool on);
    uint8_t (*relay_mask)(void);
    bool (*inputs_ready)(void);
    uint8_t (*input_mask)(void);
    void (*sk_lost)(void);
    uint32_t (*now_ms)(void);
} sk_bridge_io_t;

// Declare metadata, publish current state and register PUT handlers for
// every relay on each enabled tree. Tree toggles and bank ids take effect
// here only (they need a restart).
esp_err_t sk_bridge_start(const sk_api_t *api, const sk_bridge_io_t *io, const device_config_t *cfg);

// Names change live: re-declare metadata and republish name paths.
void sk_bridge_update_config(const device_config_t *cfg);

// Feed from relay_ctrl / input_sense listeners.
void sk_bridge_relay_changed(uint8_t channel, bool on);
void sk_bridge_input_changed(uint8_t channel, bool on);

// Feed from espOS's stream connected/disconnected events. On connect all
// state is republished: the server routes a PUT only to paths it has seen
// this device publish on the current connection.
void sk_bridge_stream_changed(bool connected);

// Call periodically: fires io->sk_lost() once after the stream has been down
// for the grace period. Only after a connection was lost (not at boot), and
// only while a tree is published (otherwise SignalK isn't a control path).
void sk_bridge_tick(void);

// Tests only.
void sk_bridge_reset(void);

#ifdef __cplusplus
}
#endif

// The relay page on the device's own web server: GET /relays (the page),
// GET /api/v1/relays (live state, including each channel's cycles/runTime,
// plan 11), PUT /api/v1/relays/<n> and PUT /api/v1/relays (all) with
// {"on": true|false}, and GET /api/v1/relays/status for the page's status
// line, and POST /api/v1/buzzer/test. Also POST /api/v1/relays/<n>/counters/reset
// and POST /api/v1/inputs/<n>/counters/reset (plan 11, issue #4). Also
// GET /tones (plan 19, issue #14): CRUD for the named RTTTL tone library and
// the event/channel chirp settings, built entirely on espOS's existing
// GET/PUT /api/v1/config, plus POST /api/v1/buzzer/preview with
// {"rtttl": "..."} for the Tones page's per-tone Play button. Endpoints are
// protected by espOS's API key like its own; the pages are public and show a
// login hint when the API refuses it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device_config.h"
#include "esp_err.h"
#include "web_ui_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_err_t (*set_relay)(uint8_t channel, bool on);
    uint8_t (*relay_mask)(void);
    bool (*inputs_ready)(void);
    uint8_t (*input_mask)(void);
    // Fill in the status line: network, SignalK, NMEA 2000, version.
    void (*get_status)(web_ui_status_t *out);
    // Play the buzzer's alarm pattern once; ESP_ERR_INVALID_STATE while it
    // is already sounding.
    esp_err_t (*test_buzzer)(void);
    // Play an arbitrary RTTTL string once (Tones page preview).
    // ESP_ERR_INVALID_ARG if it has no playable notes, ESP_ERR_INVALID_STATE
    // while the buzzer is already sounding.
    esp_err_t (*preview_tone)(const char *rtttl);
    // Live cycle count and runtime seconds for one relay/input channel, and
    // resetting one back to zero (counters, plan 11).
    void (*relay_counters)(uint8_t channel, uint32_t *cycles, uint32_t *runtime_s);
    void (*input_counters)(uint8_t channel, uint32_t *cycles, uint32_t *runtime_s);
    void (*reset_relay_counters)(uint8_t channel);
    void (*reset_input_counters)(uint8_t channel);
} web_ui_io_t;

// Register the page and endpoints. Call after espos_start(), which starts
// the web server. `io` must stay valid.
esp_err_t web_ui_start(const web_ui_io_t *io, const device_config_t *cfg);

// Record what switched a relay: `source` is a short static name
// ("signalk", "nmea2000", "web", "input", "pulse", "failsafe", "maxOn",
// "boot") shown on the page. Safe from any task, before or after
// web_ui_start().
void web_ui_relay_changed(uint8_t channel, const char *source);

// Names, modes and input links change live. Safe from any task, before or
// after web_ui_start().
void web_ui_update_config(const device_config_t *cfg);

#ifdef __cplusplus
}
#endif

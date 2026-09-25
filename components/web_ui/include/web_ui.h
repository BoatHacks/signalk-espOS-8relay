// The relay page on the device's own web server: GET /relays (the page),
// GET /api/v1/relays (live state), PUT /api/v1/relays/<n> and
// PUT /api/v1/relays (all) with {"on": true|false}. Endpoints are
// protected by espOS's API key like its own; the page is public and shows
// a login hint when the API refuses it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_err_t (*set_relay)(uint8_t channel, bool on);
    uint8_t (*relay_mask)(void);
    bool (*inputs_ready)(void);
    uint8_t (*input_mask)(void);
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

// Status LED and alarm buzzer (device builds only). The LED shows espOS
// health and the SignalK connection; the buzzer, if enabled, sounds "ESP"
// and the last octet of the device's IP address in Morse while a health
// alarm is active, or "ESP AP" when it has no address.
#pragma once

#include "device_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t indicator_start(const device_config_t *cfg);

// Brightness, buzzer switch and frequency, tree toggles; callable from any
// task.
void indicator_update_config(const device_config_t *cfg);

// Play the alarm pattern once, whether or not the buzzer is enabled for
// alarms. ESP_ERR_INVALID_STATE while an alarm or another test is sounding,
// or before indicator_start().
esp_err_t indicator_test_buzzer(void);

#ifdef __cplusplus
}
#endif

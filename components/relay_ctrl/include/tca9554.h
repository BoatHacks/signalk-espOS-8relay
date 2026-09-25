// TCA9554 8-bit I2C I/O expander, register level. Bus access goes through
// function pointers so host tests can use a fake chip.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TCA9554_REG_INPUT 0x00
#define TCA9554_REG_OUTPUT 0x01
#define TCA9554_REG_POLARITY 0x02
#define TCA9554_REG_CONFIG 0x03  // bit set = pin is an input (power-on: 0xFF)

typedef struct {
    esp_err_t (*read_reg)(void *ctx, uint8_t reg, uint8_t *val);
    esp_err_t (*write_reg)(void *ctx, uint8_t reg, uint8_t val);
    void *ctx;
} tca9554_bus_t;

// True when every pin is already an output: the chip kept its registers
// through an ESP32 reset (it has no reset pin), so the relays are still in
// their pre-reset state.
esp_err_t tca9554_all_outputs(const tca9554_bus_t *bus, bool *out);

esp_err_t tca9554_read_outputs(const tca9554_bus_t *bus, uint8_t *out);

// Write the output register and read it back; ESP_ERR_INVALID_RESPONSE if
// the chip holds a different value.
esp_err_t tca9554_write_outputs(const tca9554_bus_t *bus, uint8_t value);

// Make every pin an output driving `value`. The output register is written
// before the direction register: after power-on it reads 0xFF, and switching
// pins to outputs first would drive that for a moment.
esp_err_t tca9554_init_outputs(const tca9554_bus_t *bus, uint8_t value);

#ifdef __cplusplus
}
#endif

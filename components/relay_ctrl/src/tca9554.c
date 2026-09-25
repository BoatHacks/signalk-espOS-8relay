#include "tca9554.h"

esp_err_t tca9554_all_outputs(const tca9554_bus_t *bus, bool *out)
{
    uint8_t cfg;
    esp_err_t err = bus->read_reg(bus->ctx, TCA9554_REG_CONFIG, &cfg);
    if (err == ESP_OK) {
        *out = cfg == 0x00;
    }
    return err;
}

esp_err_t tca9554_read_outputs(const tca9554_bus_t *bus, uint8_t *out)
{
    return bus->read_reg(bus->ctx, TCA9554_REG_OUTPUT, out);
}

esp_err_t tca9554_write_outputs(const tca9554_bus_t *bus, uint8_t value)
{
    esp_err_t err = bus->write_reg(bus->ctx, TCA9554_REG_OUTPUT, value);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t back;
    err = bus->read_reg(bus->ctx, TCA9554_REG_OUTPUT, &back);
    if (err != ESP_OK) {
        return err;
    }
    return back == value ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t tca9554_init_outputs(const tca9554_bus_t *bus, uint8_t value)
{
    esp_err_t err = tca9554_write_outputs(bus, value);
    if (err != ESP_OK) {
        return err;
    }
    return bus->write_reg(bus->ctx, TCA9554_REG_CONFIG, 0x00);
}

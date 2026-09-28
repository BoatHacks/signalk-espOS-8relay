#include "rtc_hw.h"

#include <string.h>

#include "board_i2c.h"
#include "driver/i2c_master.h"

#define I2C_TIMEOUT_MS 50

static i2c_master_dev_handle_t s_dev;

static esp_err_t bus_read(void *ctx, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, I2C_TIMEOUT_MS);
}

static esp_err_t bus_write(void *ctx, uint8_t reg, const uint8_t *buf, size_t len)
{
    uint8_t out[1 + PCF85063_TIME_BLOCK_LEN];
    if (len > sizeof(out) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    out[0] = reg;
    memcpy(&out[1], buf, len);
    return i2c_master_transmit(s_dev, out, len + 1, I2C_TIMEOUT_MS);
}

esp_err_t rtc_hw_create(pcf85063_bus_t *out)
{
    i2c_master_bus_handle_t bus;
    esp_err_t err = board_i2c_bus(&bus);
    if (err != ESP_OK) {
        return err;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }
    *out = (pcf85063_bus_t){.read = bus_read, .write = bus_write};
    return ESP_OK;
}

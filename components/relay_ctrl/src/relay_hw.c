#include "relay_hw.h"

#include "board.h"
#include "board_i2c.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "nvs.h"

#define I2C_TIMEOUT_MS 50
#define NVS_NS "relay_state"
#define NVS_KEY "hold"

static i2c_master_dev_handle_t s_dev;
static nvs_handle_t s_nvs;

static esp_err_t read_reg(void *ctx, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, I2C_TIMEOUT_MS);
}

static esp_err_t write_reg(void *ctx, uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t load(void *ctx, uint8_t *mask)
{
    return nvs_get_u8(s_nvs, NVS_KEY, mask);
}

static esp_err_t save(void *ctx, uint8_t mask)
{
    esp_err_t err = nvs_set_u8(s_nvs, NVS_KEY, mask);
    return err == ESP_OK ? nvs_commit(s_nvs) : err;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

esp_err_t relay_hw_clear_hold_state(void)
{
    esp_err_t err = nvs_erase_key(s_nvs, NVS_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    return nvs_commit(s_nvs);
}

esp_err_t relay_hw_create(relay_ctrl_hw_t *out)
{
    // Shared with the RTC (PCF85063, components/rtc_pcf85063): the bus
    // itself lives in board_i2c so nothing initialises the peripheral twice.
    i2c_master_bus_handle_t bus;
    esp_err_t err = board_i2c_bus(&bus);
    if (err != ESP_OK) {
        return err;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_TCA9554_ADDR,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_open(NVS_NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        return err;
    }
    *out = (relay_ctrl_hw_t){
        .expander = {.read_reg = read_reg, .write_reg = write_reg},
        .store = {.load = load, .save = save},
        .now_ms = now_ms,
    };
    return ESP_OK;
}

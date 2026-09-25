#include "relay_hw.h"

#include "board.h"
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

esp_err_t relay_hw_create(relay_ctrl_hw_t *out)
{
    // The RTC shares this bus; whoever adds RTC support should move the bus
    // into the board component and hand the handle to both.
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .scl_io_num = BOARD_I2C_SCL,
        .sda_io_num = BOARD_I2C_SDA,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
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

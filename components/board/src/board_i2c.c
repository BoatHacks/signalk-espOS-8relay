#include "board_i2c.h"

#include "board.h"

static i2c_master_bus_handle_t s_bus;

esp_err_t board_i2c_bus(i2c_master_bus_handle_t *out)
{
    if (s_bus) {
        *out = s_bus;
        return ESP_OK;
    }
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .scl_io_num = BOARD_I2C_SCL,
        .sda_io_num = BOARD_I2C_SDA,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        s_bus = NULL;
        return err;
    }
    *out = s_bus;
    return ESP_OK;
}

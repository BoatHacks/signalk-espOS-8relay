#include "input_hw.h"

#include "board.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static const int s_pins[BOARD_CHANNELS] = BOARD_DI_PINS;

static uint8_t read_pins(void *ctx)
{
    uint8_t raw = 0;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        raw |= (uint8_t)(gpio_get_level(s_pins[i]) << i);
    }
    return raw;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

esp_err_t input_hw_create(input_sense_hw_t *out)
{
    uint64_t mask = 0;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        mask |= 1ULL << s_pins[i];
    }
    const gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        // Harmless if the board pulls up already; defines the idle level if not.
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err == ESP_OK) {
        *out = (input_sense_hw_t){.read_pins = read_pins, .now_ms = now_ms};
    }
    return err;
}

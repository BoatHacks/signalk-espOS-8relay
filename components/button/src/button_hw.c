#include "button_hw.h"

#include "board.h"
#include "driver/gpio.h"

// GPIO0 is the ESP32 family's USB bootloader strapping pin: held low at
// reset, the chip enters the download bootloader instead of running this
// firmware. By the time this runs, that check has already happened, so a
// press here can only be a person holding the button after boot -- but the
// pin must still only ever be read, never driven, and configured input-only
// with a pull-up (a backstop if the board's own pull-up is ever absent) so
// nothing here could affect the next reset's strapping.
static bool read_pin(void *ctx)
{
    return gpio_get_level(BOARD_BOOT_BUTTON) == 0;  // pulled up, grounded when pressed
}

esp_err_t button_hw_create(button_hw_t *out)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_BOOT_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err == ESP_OK) {
        *out = (button_hw_t){.read = read_pin};
    }
    return err;
}

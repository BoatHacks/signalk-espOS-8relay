#include <stddef.h>

#include "board.h"
#include "unity.h"

// Every GPIO the firmware drives, built from board.h so a pin-map change is
// checked automatically. The I2C bus appears once: the relay expander and
// the RTC share it on purpose.
static size_t used_pins(int *out)
{
    static const int di[BOARD_CHANNELS] = BOARD_DI_PINS;
    static const int others[] = {
        BOARD_I2C_SCL, BOARD_I2C_SDA,
        BOARD_CAN_TX, BOARD_CAN_RX,
        BOARD_ETH_INT, BOARD_ETH_MOSI, BOARD_ETH_MISO, BOARD_ETH_SCLK, BOARD_ETH_CS,
        BOARD_RGB_LED, BOARD_BUZZER, BOARD_BOOT_BUTTON,
    };
    size_t n = 0;
    for (size_t i = 0; i < BOARD_CHANNELS; i++) {
        out[n++] = di[i];
    }
    for (size_t i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        out[n++] = others[i];
    }
    return n;
}

TEST_CASE("no GPIO is assigned to two functions", "[board]")
{
    int pins[32];
    const size_t n = used_pins(pins);
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i + 1; j < n; j++) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(pins[i], pins[j], "pin used twice");
        }
    }
}

TEST_CASE("no pin is one the module reserves for flash or PSRAM", "[board]")
{
    // GPIO26-32 connect to the module's flash; GPIO33-37 to octal PSRAM.
    int pins[32];
    const size_t n = used_pins(pins);
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_FALSE_MESSAGE(pins[i] >= 26 && pins[i] <= 37, "pin reserved for flash/PSRAM");
    }
}

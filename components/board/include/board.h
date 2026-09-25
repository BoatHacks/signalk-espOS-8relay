// Pin map of the Waveshare ESP32-S3-ETH-8DI-8RO-C, from the Waveshare wiki.
// Plain ints rather than gpio_num_t so host tests can include this file.
#pragma once

#define BOARD_NAME "Waveshare ESP32-S3-ETH-8DI-8RO-C"

#define BOARD_CHANNELS 8

// I2C bus shared by the relay expander (TCA9554) and the RTC (PCF85063).
#define BOARD_I2C_SCL 41
#define BOARD_I2C_SDA 42
#define BOARD_TCA9554_ADDR 0x20  // EXIO1-8 = relay 1-8
// Expander output level that energises a relay. Assumed high; confirm on
// the board (docs/plans/07-bringup-and-release.md).
#define BOARD_RELAY_ACTIVE_HIGH 1

// Digital inputs DI1-DI8, in channel order.
#define BOARD_DI_PINS {4, 5, 6, 7, 8, 9, 10, 11}

// CAN transceiver (TWAI).
#define BOARD_CAN_TX 17
#define BOARD_CAN_RX 18

// W5500 Ethernet on SPI. The board wires no W5500 reset line.
#define BOARD_ETH_INT 12
#define BOARD_ETH_MOSI 13
#define BOARD_ETH_MISO 14
#define BOARD_ETH_SCLK 15
#define BOARD_ETH_CS 16
#define BOARD_ETH_RST (-1)

#define BOARD_RGB_LED 38
#define BOARD_BUZZER 46
#define BOARD_BOOT_BUTTON 0

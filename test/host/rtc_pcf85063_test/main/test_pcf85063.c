#include <string.h>

#include "pcf85063.h"
#include "unity.h"

// ---------------------------------------------------------------- fake bus

#define MAX_REG 16

static struct {
    uint8_t regs[MAX_REG];
    bool fail;
} chip;

static esp_err_t chip_read(void *ctx, uint8_t reg, uint8_t *buf, size_t len)
{
    if (chip.fail) {
        return ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < len; i++) {
        buf[i] = chip.regs[reg + i];
    }
    return ESP_OK;
}

static esp_err_t chip_write(void *ctx, uint8_t reg, const uint8_t *buf, size_t len)
{
    if (chip.fail) {
        return ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < len; i++) {
        chip.regs[reg + i] = buf[i];
    }
    return ESP_OK;
}

static const pcf85063_bus_t bus = {.read = chip_read, .write = chip_write};

static void reset_chip(void)
{
    memset(&chip, 0, sizeof(chip));
}

// -------------------------------------------------------------------- init

TEST_CASE("init forces 24h mode and does not touch the stored time", "[pcf85063]")
{
    reset_chip();
    chip.regs[PCF85063_REG_CTRL1] = 0xFF;  // simulate power-on/garbage state
    chip.regs[PCF85063_REG_SECONDS] = 0x55;

    TEST_ASSERT_EQUAL(ESP_OK, pcf85063_init(&bus));

    TEST_ASSERT_EQUAL_HEX8(0x00, chip.regs[PCF85063_REG_CTRL1]);
    TEST_ASSERT_EQUAL_HEX8(0x55, chip.regs[PCF85063_REG_SECONDS]);  // untouched
}

// --------------------------------------------------------- read/write BCD

TEST_CASE("write then read round-trips the calendar fields", "[pcf85063]")
{
    reset_chip();
    const pcf85063_datetime_t in = {
        .year = 2026, .month = 9, .day = 28, .hour = 12, .minute = 34, .second = 56, .wday = 1,
    };
    TEST_ASSERT_EQUAL(ESP_OK, pcf85063_write_datetime(&bus, &in));

    pcf85063_datetime_t out;
    bool valid = false;
    TEST_ASSERT_EQUAL(ESP_OK, pcf85063_read_datetime(&bus, &out, &valid));
    TEST_ASSERT_TRUE(valid);
    TEST_ASSERT_EQUAL_INT32(2026, out.year);
    TEST_ASSERT_EQUAL_UINT8(9, out.month);
    TEST_ASSERT_EQUAL_UINT8(28, out.day);
    TEST_ASSERT_EQUAL_UINT8(12, out.hour);
    TEST_ASSERT_EQUAL_UINT8(34, out.minute);
    TEST_ASSERT_EQUAL_UINT8(56, out.second);
}

TEST_CASE("write clamps a year outside 2000-2099 to the chip's range", "[pcf85063]")
{
    reset_chip();
    pcf85063_datetime_t in = {.year = 1999, .month = 1, .day = 1};
    TEST_ASSERT_EQUAL(ESP_OK, pcf85063_write_datetime(&bus, &in));
    pcf85063_datetime_t out;
    bool valid;
    TEST_ASSERT_EQUAL(ESP_OK, pcf85063_read_datetime(&bus, &out, &valid));
    TEST_ASSERT_EQUAL_INT32(2000, out.year);  // clamped up

    in.year = 2150;
    TEST_ASSERT_EQUAL(ESP_OK, pcf85063_write_datetime(&bus, &in));
    TEST_ASSERT_EQUAL(ESP_OK, pcf85063_read_datetime(&bus, &out, &valid));
    TEST_ASSERT_EQUAL_INT32(2099, out.year);  // clamped down
}

// ------------------------------------------------------------- OS/validity

TEST_CASE("the OS flag set reads as invalid and leaves the output untouched", "[pcf85063]")
{
    reset_chip();
    chip.regs[PCF85063_REG_SECONDS] = 0x80;  // OS set, seconds otherwise 0

    pcf85063_datetime_t out = {.year = 4242};  // sentinel
    bool valid = true;
    TEST_ASSERT_EQUAL(ESP_OK, pcf85063_read_datetime(&bus, &out, &valid));
    TEST_ASSERT_FALSE(valid);
    TEST_ASSERT_EQUAL_INT32(4242, out.year);  // untouched
}

TEST_CASE("writing a time clears a prior OS flag", "[pcf85063]")
{
    reset_chip();
    chip.regs[PCF85063_REG_SECONDS] = 0x80;

    const pcf85063_datetime_t in = {.year = 2026, .month = 1, .day = 1};
    TEST_ASSERT_EQUAL(ESP_OK, pcf85063_write_datetime(&bus, &in));

    pcf85063_datetime_t out;
    bool valid = false;
    TEST_ASSERT_EQUAL(ESP_OK, pcf85063_read_datetime(&bus, &out, &valid));
    TEST_ASSERT_TRUE(valid);
}

// ------------------------------------------------------------------- I2C

TEST_CASE("an I2C failure propagates from every entry point", "[pcf85063]")
{
    reset_chip();
    chip.fail = true;
    pcf85063_datetime_t out;
    bool valid;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, pcf85063_read_datetime(&bus, &out, &valid));
    const pcf85063_datetime_t in = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, pcf85063_write_datetime(&bus, &in));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, pcf85063_init(&bus));
}

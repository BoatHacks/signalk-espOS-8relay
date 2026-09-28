#include <string.h>

#include "rtc_pcf85063.h"
#include "unity.h"

// ---------------------------------------------------------------- fake bus
// Same shape as test_pcf85063.c's, kept separate: this file exercises
// rtc_pcf85063's unix-millisecond layer, that one the raw BCD registers.

#define MAX_REG 16

static struct {
    uint8_t regs[MAX_REG];
} chip;

static esp_err_t chip_read(void *ctx, uint8_t reg, uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        buf[i] = chip.regs[reg + i];
    }
    return ESP_OK;
}

static esp_err_t chip_write(void *ctx, uint8_t reg, const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        chip.regs[reg + i] = buf[i];
    }
    return ESP_OK;
}

static const pcf85063_bus_t bus = {.read = chip_read, .write = chip_write};

static void reset_all(void)
{
    memset(&chip, 0, sizeof(chip));
    rtc_pcf85063_reset();
}

// --------------------------------------------------------------- get/set

// Cross-checked against `python3 -c "import calendar; print(calendar.timegm(...))"`.
TEST_CASE("set then get round-trips known instants", "[rtc_pcf85063]")
{
    reset_all();
    TEST_ASSERT_EQUAL(ESP_OK, rtc_pcf85063_init(&bus));

    // All within 2000-2099: the chip's year register is two BCD digits, so a
    // date outside that range doesn't round-trip (it clamps -- see
    // test_pcf85063.c's "write clamps a year..." case for that behaviour).
    const int64_t cases[] = {
        946684800000,    // 2000-01-01T00:00:00Z
        1790598896000,   // 2026-09-28T12:34:56Z
        1709251199000,   // 2024-02-29T23:59:59Z (leap day)
        4102444799000,   // 2099-12-31T23:59:59Z (top of the chip's 2-digit year)
        1583020800000,   // 2020-03-01T00:00:00Z (the day after a leap day)
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TEST_ASSERT_EQUAL(ESP_OK, rtc_pcf85063_set_time(cases[i]));
        int64_t got;
        TEST_ASSERT_EQUAL(ESP_OK, rtc_pcf85063_get_time(&got));
        TEST_ASSERT_EQUAL_INT64(cases[i], got);
    }
}

TEST_CASE("a pre-2000 instant clamps to 2000-01-01 (the chip has no century)", "[rtc_pcf85063]")
{
    reset_all();
    TEST_ASSERT_EQUAL(ESP_OK, rtc_pcf85063_init(&bus));
    TEST_ASSERT_EQUAL(ESP_OK, rtc_pcf85063_set_time(1000));  // 1970-01-01T00:00:01Z
    int64_t got;
    TEST_ASSERT_EQUAL(ESP_OK, rtc_pcf85063_get_time(&got));
    TEST_ASSERT_EQUAL_INT64(946684801000, got);  // 2000-01-01T00:00:01Z instead
}

TEST_CASE("set_time rejects a non-positive instant", "[rtc_pcf85063]")
{
    reset_all();
    TEST_ASSERT_EQUAL(ESP_OK, rtc_pcf85063_init(&bus));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, rtc_pcf85063_set_time(0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, rtc_pcf85063_set_time(-1000));
}

TEST_CASE("a sub-second remainder is dropped, not rounded", "[rtc_pcf85063]")
{
    reset_all();
    TEST_ASSERT_EQUAL(ESP_OK, rtc_pcf85063_init(&bus));
    TEST_ASSERT_EQUAL(ESP_OK, rtc_pcf85063_set_time(946684800999));  // 2000-01-01T00:00:00.999Z
    int64_t got;
    TEST_ASSERT_EQUAL(ESP_OK, rtc_pcf85063_get_time(&got));
    TEST_ASSERT_EQUAL_INT64(946684800000, got);  // the chip has no sub-second field
}

// --------------------------------------------------------------- validity

TEST_CASE("get before init fails", "[rtc_pcf85063]")
{
    reset_all();
    int64_t got;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, rtc_pcf85063_get_time(&got));
}

TEST_CASE("set before init fails", "[rtc_pcf85063]")
{
    reset_all();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, rtc_pcf85063_set_time(1000));
}

TEST_CASE("the OS flag means no valid time", "[rtc_pcf85063]")
{
    reset_all();
    TEST_ASSERT_EQUAL(ESP_OK, rtc_pcf85063_init(&bus));
    chip.regs[PCF85063_REG_SECONDS] = 0x80;  // OS set: first power-up, no battery
    int64_t got;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, rtc_pcf85063_get_time(&got));
}

TEST_CASE("setting the time clears a prior OS flag", "[rtc_pcf85063]")
{
    reset_all();
    TEST_ASSERT_EQUAL(ESP_OK, rtc_pcf85063_init(&bus));
    chip.regs[PCF85063_REG_SECONDS] = 0x80;

    TEST_ASSERT_EQUAL(ESP_OK, rtc_pcf85063_set_time(946684800000));
    int64_t got;
    TEST_ASSERT_EQUAL(ESP_OK, rtc_pcf85063_get_time(&got));
    TEST_ASSERT_EQUAL_INT64(946684800000, got);
}

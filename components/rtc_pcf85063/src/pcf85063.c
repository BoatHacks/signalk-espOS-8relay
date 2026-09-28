#include "pcf85063.h"

static uint8_t to_bcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static uint8_t from_bcd(uint8_t v)
{
    return (uint8_t)(((v >> 4) * 10) + (v & 0x0F));
}

esp_err_t pcf85063_init(const pcf85063_bus_t *bus)
{
    const uint8_t ctrl1 = 0x00;  // 24-hour mode, not stopped, no reset/EXT_TEST
    return bus->write(bus->ctx, PCF85063_REG_CTRL1, &ctrl1, 1);
}

esp_err_t pcf85063_read_datetime(const pcf85063_bus_t *bus, pcf85063_datetime_t *out, bool *valid)
{
    uint8_t reg[PCF85063_TIME_BLOCK_LEN];
    esp_err_t err = bus->read(bus->ctx, PCF85063_REG_SECONDS, reg, sizeof(reg));
    if (err != ESP_OK) {
        return err;
    }
    if (reg[0] & 0x80) {  // OS: oscillator stopped since it was last cleared
        *valid = false;
        return ESP_OK;
    }
    *valid = true;
    out->second = from_bcd(reg[0] & 0x7F);
    out->minute = from_bcd(reg[1] & 0x7F);
    out->hour = from_bcd(reg[2] & 0x3F);  // 24h mode: bits 5:0
    out->day = from_bcd(reg[3] & 0x3F);
    out->wday = reg[4] & 0x07;
    out->month = from_bcd(reg[5] & 0x1F);
    out->year = 2000 + from_bcd(reg[6]);
    return ESP_OK;
}

esp_err_t pcf85063_write_datetime(const pcf85063_bus_t *bus, const pcf85063_datetime_t *dt)
{
    int32_t year = dt->year;
    if (year < 2000) {
        year = 2000;
    } else if (year > 2099) {
        year = 2099;
    }
    uint8_t reg[PCF85063_TIME_BLOCK_LEN] = {
        to_bcd(dt->second),  // bit 7 = 0: clears OS
        to_bcd(dt->minute),
        to_bcd(dt->hour),
        to_bcd(dt->day),
        (uint8_t)(dt->wday & 0x07),
        to_bcd(dt->month),
        to_bcd((uint8_t)(year - 2000)),
    };
    return bus->write(bus->ctx, PCF85063_REG_SECONDS, reg, sizeof(reg));
}

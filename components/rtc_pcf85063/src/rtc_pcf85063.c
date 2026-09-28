#include "rtc_pcf85063.h"

#include <string.h>

static struct {
    pcf85063_bus_t bus;
    bool inited;
} s;

// Days from the civil epoch (1970-01-01) to y-m-d, and back. Howard
// Hinnant's days_from_civil / civil_from_days (exact for the whole int64
// range, no lookup tables, no libc) -- the same algorithm espos_time's
// time_policy.c uses for the same reason: it's a private static there, so
// this is its own small copy rather than a cross-component dependency for
// two functions.
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);                       // 0-399
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;   // 0-365
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // 0-146096
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int64_t *y, unsigned *m, unsigned *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yr = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = yr + (*m <= 2);
}

// 1970-01-01 was a Thursday (wday 4, 0 = Sunday).
static uint8_t wday_of(int64_t days)
{
    int64_t w = (days + 4) % 7;
    if (w < 0) {
        w += 7;
    }
    return (uint8_t)w;
}

esp_err_t rtc_pcf85063_init(const pcf85063_bus_t *bus)
{
    esp_err_t err = pcf85063_init(bus);
    if (err != ESP_OK) {
        return err;
    }
    s.bus = *bus;
    s.inited = true;
    return ESP_OK;
}

esp_err_t rtc_pcf85063_get_time(int64_t *out_unix_ms)
{
    if (!s.inited) {
        return ESP_ERR_INVALID_STATE;
    }
    pcf85063_datetime_t dt;
    bool valid;
    esp_err_t err = pcf85063_read_datetime(&s.bus, &dt, &valid);
    if (err != ESP_OK) {
        return err;
    }
    if (!valid) {
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t days = days_from_civil(dt.year, dt.month, dt.day);
    const int64_t secs = days * 86400 + dt.hour * 3600 + dt.minute * 60 + dt.second;
    *out_unix_ms = secs * 1000;
    return ESP_OK;
}

esp_err_t rtc_pcf85063_set_time(int64_t unix_ms)
{
    if (!s.inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (unix_ms <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    int64_t secs = unix_ms / 1000;  // the chip has no sub-second field
    int64_t days = secs / 86400;
    int32_t sod = (int32_t)(secs % 86400);
    if (sod < 0) {  // floor division/modulo, like espos_time's own formatter
        sod += 86400;
        days--;
    }
    int64_t y;
    unsigned mo, d;
    civil_from_days(days, &y, &mo, &d);
    pcf85063_datetime_t dt = {
        .year = (int32_t)y,
        .month = (uint8_t)mo,
        .day = (uint8_t)d,
        .hour = (uint8_t)(sod / 3600),
        .minute = (uint8_t)((sod / 60) % 60),
        .second = (uint8_t)(sod % 60),
        .wday = wday_of(days),
    };
    return pcf85063_write_datetime(&s.bus, &dt);
}

void rtc_pcf85063_reset(void)
{
    memset(&s, 0, sizeof(s));
}

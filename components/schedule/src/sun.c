#include "sun.h"

#include <math.h>
#include <stdbool.h>

// Days from the civil epoch (1970-01-01) to y-m-d. Howard Hinnant's
// days_from_civil, exact for the whole int64 range -- the same algorithm
// espos_time's time_policy.c and rtc_pcf85063 use, copied locally again for
// the same reason noted there: it's two functions, not worth a cross-
// component dependency for.
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

// ---------------------------------------------------- NOAA solar position
// Ported term-for-term from the public-domain NOAA Solar Calculator
// spreadsheet algorithm (the same one behind noaa.gov's sunrise/sunset
// tables). `t` is Julian centuries since J2000.0.

static double julian_day(int32_t year, unsigned month, unsigned day)
{
    int32_t y = year;
    unsigned m = month;
    if (m <= 2) {
        y -= 1;
        m += 12;
    }
    const double a = floor(y / 100.0);
    const double b = 2 - a + floor(a / 4.0);
    return floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + day + b - 1524.5;
}

static double geom_mean_long_sun(double t)
{
    double l0 = 280.46646 + t * (36000.76983 + t * 0.0003032);
    l0 = fmod(l0, 360.0);
    return l0 < 0 ? l0 + 360.0 : l0;
}

static double geom_mean_anomaly_sun(double t)
{
    return 357.52911 + t * (35999.05029 - 0.0001537 * t);
}

static double eccentricity_earth_orbit(double t)
{
    return 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
}

static double sun_eq_of_center(double t)
{
    const double m = geom_mean_anomaly_sun(t);
    const double mrad = m * M_PI / 180.0;
    const double sinm = sin(mrad), sin2m = sin(2 * mrad), sin3m = sin(3 * mrad);
    return sinm * (1.914602 - t * (0.004817 + 0.000014 * t)) + sin2m * (0.019993 - 0.000101 * t) +
           sin3m * 0.000289;
}

static double sun_true_long(double t)
{
    return geom_mean_long_sun(t) + sun_eq_of_center(t);
}

static double sun_apparent_long(double t)
{
    const double o = sun_true_long(t);
    const double omega = 125.04 - 1934.136 * t;
    return o - 0.00569 - 0.00478 * sin(omega * M_PI / 180.0);
}

static double mean_obliquity_ecliptic(double t)
{
    const double seconds = 21.448 - t * (46.815 + t * (0.00059 - t * 0.001813));
    return 23.0 + (26.0 + seconds / 60.0) / 60.0;
}

static double obliquity_correction(double t)
{
    const double e0 = mean_obliquity_ecliptic(t);
    const double omega = 125.04 - 1934.136 * t;
    return e0 + 0.00256 * cos(omega * M_PI / 180.0);
}

static double sun_declination(double t)
{
    const double e = obliquity_correction(t);
    const double lambda = sun_apparent_long(t);
    const double sint = sin(e * M_PI / 180.0) * sin(lambda * M_PI / 180.0);
    return asin(sint) * 180.0 / M_PI;
}

// Minutes, not degrees: the "equation of time" correction between apparent
// and mean solar time.
static double eq_of_time(double t)
{
    const double epsilon = obliquity_correction(t);
    const double l0 = geom_mean_long_sun(t);
    const double e = eccentricity_earth_orbit(t);
    const double m = geom_mean_anomaly_sun(t);
    double y = tan((epsilon / 2.0) * M_PI / 180.0);
    y *= y;
    const double sin2l0 = sin(2 * l0 * M_PI / 180.0);
    const double sinm = sin(m * M_PI / 180.0);
    const double cos2l0 = cos(2 * l0 * M_PI / 180.0);
    const double sin4l0 = sin(4 * l0 * M_PI / 180.0);
    const double sin2m = sin(2 * m * M_PI / 180.0);
    const double e_time = y * sin2l0 - 2 * e * sinm + 4 * e * y * sinm * cos2l0 - 0.5 * y * y * sin4l0 -
                           1.25 * e * e * sin2m;
    return (e_time * 180.0 / M_PI) * 4.0;
}

// Hour angle (degrees) between solar noon and the sunrise/sunset elevation
// of -0.833 deg (standard atmospheric refraction plus the sun's apparent
// radius -- the same constant every published sunrise table uses). Returns
// false when the sun never crosses that elevation this day: `*always_up`
// then says which side of the horizon it stayed on.
static bool hour_angle_deg(double lat, double solar_dec, double *out_deg, bool *always_up)
{
    const double lat_rad = lat * M_PI / 180.0;
    const double dec_rad = solar_dec * M_PI / 180.0;
    const double arg = cos(90.833 * M_PI / 180.0) / (cos(lat_rad) * cos(dec_rad)) - tan(lat_rad) * tan(dec_rad);
    if (arg > 1.0) {
        *always_up = false;  // the sun never reaches -0.833 deg: polar night
        return false;
    }
    if (arg < -1.0) {
        *always_up = true;  // the sun never goes below -0.833 deg: polar day
        return false;
    }
    *out_deg = acos(arg) * 180.0 / M_PI;
    return true;
}

void sun_times(int32_t year, uint8_t month, uint8_t day, double lat, double lon_east, sun_times_t *out)
{
    const double jd = julian_day(year, month, day);
    const double t = (jd - 2451545.0) / 36525.0;
    const double eq_time = eq_of_time(t);
    const double solar_dec = sun_declination(t);

    double ha_deg;
    bool always_up;
    if (!hour_angle_deg(lat, solar_dec, &ha_deg, &always_up)) {
        out->kind = always_up ? SUN_ALWAYS_UP : SUN_ALWAYS_DOWN;
        return;
    }
    out->kind = SUN_TRANSITION;

    // Minutes from that UTC calendar day's midnight. Longitude is EAST-
    // positive here; the classic NOAA worksheet uses WEST-positive, hence
    // the sign flip from the spreadsheet's own "720 - 4*longitude - eqTime".
    const double rise_min = 720.0 - 4.0 * (lon_east + ha_deg) - eq_time;
    const double set_min = 720.0 - 4.0 * (lon_east - ha_deg) - eq_time;

    const int64_t day_start_ms = days_from_civil(year, month, day) * 86400000LL;
    out->sunrise_unix_ms = day_start_ms + (int64_t)llround(rise_min * 60000.0);
    out->sunset_unix_ms = day_start_ms + (int64_t)llround(set_min * 60000.0);
}

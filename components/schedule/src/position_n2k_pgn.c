#include "position_n2k_pgn.h"

static int32_t read_i32_le(const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return (int32_t)v;
}

static int64_t read_i64_le(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) {
        v = (v << 8) | p[i];
    }
    return (int64_t)v;
}

bool position_n2k_parse_129025(const uint8_t *data, uint8_t len, double *lat, double *lon)
{
    if (len < 8) {
        return false;
    }
    const int32_t lat_raw = read_i32_le(data);
    const int32_t lon_raw = read_i32_le(data + 4);
    if (lat_raw == 0x7FFFFFFF || lon_raw == 0x7FFFFFFF) {
        return false;
    }
    *lat = lat_raw * 1e-7;
    *lon = lon_raw * 1e-7;
    return true;
}

bool position_n2k_parse_129029(const uint8_t *data, uint8_t len, double *lat, double *lon)
{
    if (len < 23) {
        return false;
    }
    const int64_t lat_raw = read_i64_le(data + 7);
    const int64_t lon_raw = read_i64_le(data + 15);
    if (lat_raw == (int64_t)0x7FFFFFFFFFFFFFFFLL || lon_raw == (int64_t)0x7FFFFFFFFFFFFFFFLL) {
        return false;
    }
    *lat = (double)lat_raw * 1e-16;
    *lon = (double)lon_raw * 1e-16;
    return true;
}

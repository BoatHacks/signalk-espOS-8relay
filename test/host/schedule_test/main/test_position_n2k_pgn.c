#include <string.h>

#include "position_n2k_pgn.h"
#include "unity.h"

static void write_i32_le(uint8_t *p, int32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void write_i64_le(uint8_t *p, int64_t v)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
    }
}

// London, roughly: 51.5074 N, -0.1278 E.
TEST_CASE("129025: a valid message decodes latitude/longitude", "[position_n2k_pgn]")
{
    uint8_t data[8];
    write_i32_le(data, (int32_t)(51.5074 / 1e-7));
    write_i32_le(data + 4, (int32_t)(-0.1278 / 1e-7));

    double lat, lon;
    TEST_ASSERT_TRUE(position_n2k_parse_129025(data, sizeof(data), &lat, &lon));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 51.5074, lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -0.1278, lon);
}

TEST_CASE("129025: the not-available sentinel is rejected", "[position_n2k_pgn]")
{
    uint8_t data[8];
    write_i32_le(data, 0x7FFFFFFF);  // latitude N/A
    write_i32_le(data + 4, (int32_t)(-0.1278 / 1e-7));
    double lat, lon;
    TEST_ASSERT_FALSE(position_n2k_parse_129025(data, sizeof(data), &lat, &lon));
}

TEST_CASE("129025: a short buffer is rejected", "[position_n2k_pgn]")
{
    uint8_t data[7] = {0};
    double lat, lon;
    TEST_ASSERT_FALSE(position_n2k_parse_129025(data, sizeof(data), &lat, &lon));
}

TEST_CASE("129029: a valid message decodes latitude/longitude at the right offsets", "[position_n2k_pgn]")
{
    uint8_t data[23] = {0};
    data[0] = 7;                              // SID, ignored
    data[1] = 0x34;                            // days since 1970, ignored
    data[2] = 0x12;
    memset(data + 3, 0, 4);                    // seconds since midnight, ignored
    write_i64_le(data + 7, (int64_t)(51.5074 / 1e-16));
    write_i64_le(data + 15, (int64_t)(-0.1278 / 1e-16));

    double lat, lon;
    TEST_ASSERT_TRUE(position_n2k_parse_129029(data, sizeof(data), &lat, &lon));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 51.5074, lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -0.1278, lon);
}

TEST_CASE("129029: the not-available sentinel is rejected", "[position_n2k_pgn]")
{
    uint8_t data[23] = {0};
    write_i64_le(data + 7, (int64_t)0x7FFFFFFFFFFFFFFFLL);  // latitude N/A
    write_i64_le(data + 15, (int64_t)(-0.1278 / 1e-16));
    double lat, lon;
    TEST_ASSERT_FALSE(position_n2k_parse_129029(data, sizeof(data), &lat, &lon));
}

TEST_CASE("129029: a short buffer is rejected", "[position_n2k_pgn]")
{
    uint8_t data[22] = {0};
    double lat, lon;
    TEST_ASSERT_FALSE(position_n2k_parse_129029(data, sizeof(data), &lat, &lon));
}

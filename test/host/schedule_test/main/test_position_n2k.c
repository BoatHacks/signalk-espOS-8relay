#include "position_n2k.h"
#include "unity.h"

static uint32_t clock_ms;
static uint32_t fake_now(void)
{
    return clock_ms;
}

static const position_n2k_hw_t hw = {.now_ms = fake_now};

static void fresh(void)
{
    clock_ms = 0;
    position_n2k_reset();
    position_n2k_init(&hw);
}

static void encode_129025(uint8_t *data, double lat, double lon)
{
    int32_t lat_raw = (int32_t)(lat / 1e-7);
    int32_t lon_raw = (int32_t)(lon / 1e-7);
    for (int i = 0; i < 4; i++) {
        data[i] = (uint8_t)((lat_raw >> (8 * i)) & 0xFF);
        data[4 + i] = (uint8_t)((lon_raw >> (8 * i)) & 0xFF);
    }
}

TEST_CASE("get before any message fails", "[position_n2k]")
{
    fresh();
    double lat, lon;
    uint32_t received_ms;
    TEST_ASSERT_FALSE(position_n2k_get(&lat, &lon, &received_ms));
}

TEST_CASE("a 129025 message is decoded and stamped with the current time", "[position_n2k]")
{
    fresh();
    uint8_t data[8];
    encode_129025(data, 51.5074, -0.1278);
    clock_ms = 12345;
    position_n2k_on_msg(129025, data, sizeof(data), NULL);

    double lat, lon;
    uint32_t received_ms;
    TEST_ASSERT_TRUE(position_n2k_get(&lat, &lon, &received_ms));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 51.5074, lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -0.1278, lon);
    TEST_ASSERT_EQUAL_UINT32(12345, received_ms);
}

TEST_CASE("an unrecognised PGN is ignored", "[position_n2k]")
{
    fresh();
    uint8_t data[8] = {0};
    position_n2k_on_msg(127502, data, sizeof(data), NULL);  // switch_bank's own PGN
    double lat, lon;
    uint32_t received_ms;
    TEST_ASSERT_FALSE(position_n2k_get(&lat, &lon, &received_ms));
}

TEST_CASE("an unavailable position in an otherwise-recognised PGN is ignored", "[position_n2k]")
{
    fresh();
    uint8_t data[8];
    encode_129025(data, 51.5074, -0.1278);
    data[0] = 0xFF;
    data[1] = 0xFF;
    data[2] = 0xFF;
    data[3] = 0x7F;  // latitude N/A
    position_n2k_on_msg(129025, data, sizeof(data), NULL);
    double lat, lon;
    uint32_t received_ms;
    TEST_ASSERT_FALSE(position_n2k_get(&lat, &lon, &received_ms));
}

TEST_CASE("a later message replaces the earlier one", "[position_n2k]")
{
    fresh();
    uint8_t data[8];
    encode_129025(data, 51.5074, -0.1278);
    clock_ms = 1000;
    position_n2k_on_msg(129025, data, sizeof(data), NULL);

    encode_129025(data, 40.7128, -74.0060);
    clock_ms = 2000;
    position_n2k_on_msg(129025, data, sizeof(data), NULL);

    double lat, lon;
    uint32_t received_ms;
    TEST_ASSERT_TRUE(position_n2k_get(&lat, &lon, &received_ms));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 40.7128, lat);
    TEST_ASSERT_EQUAL_UINT32(2000, received_ms);
}

#include <string.h>

#include "device_config.h"
#include "position.h"
#include "position_n2k.h"
#include "position_sk.h"
#include "unity.h"

static uint32_t clock_ms;
static uint32_t fake_now(void)
{
    return clock_ms;
}

static const position_sk_hw_t sk_hw = {.now_ms = fake_now};
static const position_n2k_hw_t n2k_hw = {.now_ms = fake_now};

static device_config_t cfg;

static void fresh(void)
{
    clock_ms = 0;
    position_sk_reset();
    position_n2k_reset();
    position_sk_init(&sk_hw);
    position_n2k_init(&n2k_hw);
    memset(&cfg, 0, sizeof(cfg));
    cfg.fallback_lat = 1.0;
    cfg.fallback_lon = 2.0;
}

static void sk_update(double lat, double lon, uint32_t at_ms)
{
    char json[96];
    snprintf(json, sizeof(json), "{\"latitude\":%f,\"longitude\":%f}", lat, lon);
    espos_sk_update_t u = {0};
    u.path = "navigation.position";
    u.value_json = json;
    clock_ms = at_ms;
    position_sk_on_update(&u, NULL);
}

TEST_CASE("no live reading of either kind: the fallback position", "[position]")
{
    fresh();
    cfg.position_source = POSITION_SRC_SIGNALK;
    double lat, lon;
    position_get(&cfg, 0, &lat, &lon);
    TEST_ASSERT_EQUAL_DOUBLE(1.0, lat);
    TEST_ASSERT_EQUAL_DOUBLE(2.0, lon);
}

TEST_CASE("signalk source, a fresh reading: the live position", "[position]")
{
    fresh();
    cfg.position_source = POSITION_SRC_SIGNALK;
    sk_update(51.5074, -0.1278, 1000);
    double lat, lon;
    position_get(&cfg, 1000 + 60000, &lat, &lon);  // a minute later, well under the 10-minute window
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 51.5074, lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -0.1278, lon);
}

TEST_CASE("signalk source, a stale reading: the fallback position", "[position]")
{
    fresh();
    cfg.position_source = POSITION_SRC_SIGNALK;
    sk_update(51.5074, -0.1278, 1000);
    double lat, lon;
    position_get(&cfg, 1000 + POSITION_STALE_MS + 1, &lat, &lon);
    TEST_ASSERT_EQUAL_DOUBLE(1.0, lat);
    TEST_ASSERT_EQUAL_DOUBLE(2.0, lon);
}

TEST_CASE("exactly at the staleness boundary still counts as fresh", "[position]")
{
    fresh();
    cfg.position_source = POSITION_SRC_SIGNALK;
    sk_update(51.5074, -0.1278, 1000);
    double lat, lon;
    position_get(&cfg, 1000 + POSITION_STALE_MS, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 51.5074, lat);
}

TEST_CASE("n2k source: a fresh signalk reading is not used", "[position]")
{
    fresh();
    cfg.position_source = POSITION_SRC_N2K;
    sk_update(51.5074, -0.1278, 1000);  // signalk has a fresh reading...
    double lat, lon;
    position_get(&cfg, 1000, &lat, &lon);  // ...but the source is n2k, which has none
    TEST_ASSERT_EQUAL_DOUBLE(1.0, lat);
    TEST_ASSERT_EQUAL_DOUBLE(2.0, lon);
}

TEST_CASE("n2k source, a fresh reading: the live position", "[position]")
{
    fresh();
    cfg.position_source = POSITION_SRC_N2K;
    uint8_t data[8];
    int32_t lat_raw = (int32_t)(51.5074 / 1e-7);
    int32_t lon_raw = (int32_t)(-0.1278 / 1e-7);
    for (int i = 0; i < 4; i++) {
        data[i] = (uint8_t)((lat_raw >> (8 * i)) & 0xFF);
        data[4 + i] = (uint8_t)((lon_raw >> (8 * i)) & 0xFF);
    }
    clock_ms = 1000;
    position_n2k_on_msg(129025, data, sizeof(data), NULL);

    double lat, lon;
    position_get(&cfg, 1000 + 60000, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 51.5074, lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -0.1278, lon);
}

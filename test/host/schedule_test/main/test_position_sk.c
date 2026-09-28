#include "position_sk.h"
#include "unity.h"

static uint32_t clock_ms;
static uint32_t fake_now(void)
{
    return clock_ms;
}

static const position_sk_hw_t hw = {.now_ms = fake_now};

static void fresh(void)
{
    clock_ms = 0;
    position_sk_reset();
    position_sk_init(&hw);
}

static espos_sk_update_t update_with(const char *value_json)
{
    espos_sk_update_t u = {0};
    u.path = "navigation.position";
    u.value_json = value_json;
    return u;
}

TEST_CASE("get before any update fails", "[position_sk]")
{
    fresh();
    double lat, lon;
    uint32_t received_ms;
    TEST_ASSERT_FALSE(position_sk_get(&lat, &lon, &received_ms));
}

TEST_CASE("a valid navigation.position update is decoded and stamped", "[position_sk]")
{
    fresh();
    clock_ms = 5000;
    espos_sk_update_t u = update_with("{\"latitude\":51.5074,\"longitude\":-0.1278}");
    position_sk_on_update(&u, NULL);

    double lat, lon;
    uint32_t received_ms;
    TEST_ASSERT_TRUE(position_sk_get(&lat, &lon, &received_ms));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 51.5074, lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -0.1278, lon);
    TEST_ASSERT_EQUAL_UINT32(5000, received_ms);
}

TEST_CASE("malformed JSON is ignored", "[position_sk]")
{
    fresh();
    espos_sk_update_t u = update_with("{not json");
    position_sk_on_update(&u, NULL);
    double lat, lon;
    uint32_t received_ms;
    TEST_ASSERT_FALSE(position_sk_get(&lat, &lon, &received_ms));
}

TEST_CASE("a value missing latitude or longitude is ignored", "[position_sk]")
{
    fresh();
    espos_sk_update_t u = update_with("{\"latitude\":51.5074}");
    position_sk_on_update(&u, NULL);
    double lat, lon;
    uint32_t received_ms;
    TEST_ASSERT_FALSE(position_sk_get(&lat, &lon, &received_ms));
}

TEST_CASE("a NULL value_json (a meta item, not a value) is ignored", "[position_sk]")
{
    fresh();
    espos_sk_update_t u = {0};
    u.path = "navigation.position";
    u.meta_json = "{\"units\":\"rad\"}";
    position_sk_on_update(&u, NULL);
    double lat, lon;
    uint32_t received_ms;
    TEST_ASSERT_FALSE(position_sk_get(&lat, &lon, &received_ms));
}

TEST_CASE("a later update replaces the earlier one", "[position_sk]")
{
    fresh();
    clock_ms = 1000;
    espos_sk_update_t u1 = update_with("{\"latitude\":51.5074,\"longitude\":-0.1278}");
    position_sk_on_update(&u1, NULL);

    clock_ms = 2000;
    espos_sk_update_t u2 = update_with("{\"latitude\":40.7128,\"longitude\":-74.0060}");
    position_sk_on_update(&u2, NULL);

    double lat, lon;
    uint32_t received_ms;
    TEST_ASSERT_TRUE(position_sk_get(&lat, &lon, &received_ms));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 40.7128, lat);
    TEST_ASSERT_EQUAL_UINT32(2000, received_ms);
}

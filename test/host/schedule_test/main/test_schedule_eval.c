#include <string.h>

#include "device_config.h"
#include "schedule_eval.h"
#include "unity.h"

#define MAX_CALLS 16

static struct {
    uint8_t channel;
    bool on;
} calls[MAX_CALLS];
static int n_calls;

static esp_err_t fake_set_relay(uint8_t channel, bool on)
{
    if (n_calls < MAX_CALLS) {
        calls[n_calls].channel = channel;
        calls[n_calls].on = on;
        n_calls++;
    }
    return ESP_OK;
}

static const schedule_eval_io_t io = {.set_relay = fake_set_relay};

static void fresh(void)
{
    n_calls = 0;
    schedule_eval_reset();
    schedule_eval_init(&io);
}

// Sunday 2026-09-27 = wday 0; Monday 28th = wday 1 (confirmed against the
// same reference used throughout this plan's tests). Building a local-time
// struct as espos_time_parts() would hand it: minute resolution, seconds
// left at 0 unless a test needs otherwise.
static espos_time_parts_t local_at(int32_t year, uint8_t month, uint8_t day, uint8_t wday, uint8_t hour,
                                    uint8_t minute, int32_t utc_offset_s)
{
    espos_time_parts_t p = {0};
    p.year = year;
    p.month = month;
    p.day = day;
    p.wday = wday;
    p.hour = hour;
    p.minute = minute;
    p.utc_offset_s = utc_offset_s;
    return p;
}

static device_config_t s_cfg;

static schedule_cfg_t *entry(int k)
{
    return &s_cfg.schedules[k - 1];
}

static void reset_cfg(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
}

// ----------------------------------------------------------------- no time

TEST_CASE("no valid time: nothing is switched", "[schedule_eval]")
{
    fresh();
    reset_cfg();
    entry(1)->relay = 3;
    entry(1)->days = 0x7F;
    entry(1)->on = (schedule_time_t){.kind = SCHEDULE_TIME_CLOCK, .minute_of_day = 0};
    entry(1)->off = (schedule_time_t){.kind = SCHEDULE_TIME_CLOCK, .minute_of_day = 1439};

    espos_time_parts_t local = local_at(2026, 9, 28, 1, 12, 0, 0);
    schedule_eval_tick(&s_cfg, false, &local, 0, 0);
    TEST_ASSERT_EQUAL(0, n_calls);
}

TEST_CASE("time becomes valid: the entry syncs to its current state immediately", "[schedule_eval]")
{
    fresh();
    reset_cfg();
    entry(1)->relay = 3;
    entry(1)->days = 0x7F;
    entry(1)->on = (schedule_time_t){.kind = SCHEDULE_TIME_CLOCK, .minute_of_day = 10 * 60};
    entry(1)->off = (schedule_time_t){.kind = SCHEDULE_TIME_CLOCK, .minute_of_day = 11 * 60};

    espos_time_parts_t local = local_at(2026, 9, 28, 1, 0, 0, 0);
    schedule_eval_tick(&s_cfg, false, &local, 0, 0);  // no time yet
    TEST_ASSERT_EQUAL(0, n_calls);

    local = local_at(2026, 9, 28, 1, 10, 30, 0);  // 10:30, within the window
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(1, n_calls);
    TEST_ASSERT_EQUAL(3, calls[0].channel);
    TEST_ASSERT_TRUE(calls[0].on);
}

// -------------------------------------------------------- clock mode edges

TEST_CASE("clock mode: only edges switch the relay", "[schedule_eval]")
{
    fresh();
    reset_cfg();
    entry(1)->relay = 5;
    entry(1)->days = 0x7F;
    entry(1)->on = (schedule_time_t){.kind = SCHEDULE_TIME_CLOCK, .minute_of_day = 10 * 60};
    entry(1)->off = (schedule_time_t){.kind = SCHEDULE_TIME_CLOCK, .minute_of_day = 11 * 60};

    espos_time_parts_t local = local_at(2026, 9, 28, 1, 9, 0, 0);  // 09:00: before, off
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(1, n_calls);  // first-ever tick: sync
    TEST_ASSERT_FALSE(calls[0].on);

    local.hour = 9;
    local.minute = 30;  // still before: no edge
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(1, n_calls);

    local.hour = 10;
    local.minute = 0;  // the on edge
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(2, n_calls);
    TEST_ASSERT_TRUE(calls[1].on);

    local.hour = 10;
    local.minute = 30;  // still on: no edge
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(2, n_calls);

    local.hour = 11;
    local.minute = 0;  // the off edge
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(3, n_calls);
    TEST_ASSERT_FALSE(calls[2].on);
}

// ----------------------------------------------- midnight-spanning + days

TEST_CASE("a window spanning midnight stays active past a day the mask excludes", "[schedule_eval]")
{
    fresh();
    reset_cfg();
    // Friday 22:00 to Saturday 06:00, Friday only in the mask.
    // Sunday=bit0 .. Saturday=bit6; Friday=bit5=0x20.
    entry(1)->relay = 2;
    entry(1)->days = 0x20;
    entry(1)->on = (schedule_time_t){.kind = SCHEDULE_TIME_CLOCK, .minute_of_day = 22 * 60};
    entry(1)->off = (schedule_time_t){.kind = SCHEDULE_TIME_CLOCK, .minute_of_day = 6 * 60};

    // Friday 23:00 (wday 5).
    espos_time_parts_t local = local_at(2026, 10, 2, 5, 23, 0, 0);
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(1, n_calls);
    TEST_ASSERT_TRUE(calls[0].on);

    // Saturday 00:30 (wday 6, excluded from the mask): still within
    // Friday's overnight window, must still be on.
    local = local_at(2026, 10, 3, 6, 0, 30, 0);
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(1, n_calls);  // no edge: still active

    // Saturday 07:00: past the off time now.
    local = local_at(2026, 10, 3, 6, 7, 0, 0);
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(2, n_calls);
    TEST_ASSERT_FALSE(calls[1].on);

    // The following Friday: the mask isn't a one-shot, it re-arms every
    // week.
    local = local_at(2026, 10, 9, 5, 22, 0, 0);
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(3, n_calls);
    TEST_ASSERT_TRUE(calls[2].on);
}

// ------------------------------------------------------------- repeat mode

TEST_CASE("repeat mode: a duty cycle anchored to local midnight", "[schedule_eval]")
{
    fresh();
    reset_cfg();
    entry(1)->relay = 4;
    entry(1)->days = 0x7F;
    entry(1)->mode = SCHEDULE_MODE_REPEAT;
    entry(1)->on_min = 10;
    entry(1)->period_min = 60;

    espos_time_parts_t local = local_at(2026, 9, 28, 1, 0, 5, 0);  // 00:05: within the first 10 minutes
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(1, n_calls);
    TEST_ASSERT_TRUE(calls[0].on);

    local.minute = 15;  // 00:15: past the on-minutes
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(2, n_calls);
    TEST_ASSERT_FALSE(calls[1].on);

    local.hour = 1;
    local.minute = 5;  // 01:05: 65 % 60 = 5, within the next cycle's on-minutes
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(3, n_calls);
    TEST_ASSERT_TRUE(calls[2].on);
}

TEST_CASE("repeat mode: a day excluded from the mask does nothing all day", "[schedule_eval]")
{
    fresh();
    reset_cfg();
    entry(1)->relay = 4;
    entry(1)->days = (uint8_t)~0x20;  // every day except Friday
    entry(1)->mode = SCHEDULE_MODE_REPEAT;
    entry(1)->on_min = 10;
    entry(1)->period_min = 60;

    espos_time_parts_t local = local_at(2026, 10, 2, 5, 0, 5, 0);  // Friday, 00:05
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(1, n_calls);
    TEST_ASSERT_FALSE(calls[0].on);  // sync: inactive, Friday excluded
}

// ------------------------------------------------------------ sun-relative

// London, 2026-09-28: sunrise 06:55 BST, sunset 18:46 BST (cross-checked in
// test_sun.c's own London case, converted to local time with the +1h
// summer-time offset here).
TEST_CASE("sunset-30/sunrise+15 spans midnight using real sun times", "[schedule_eval]")
{
    fresh();
    reset_cfg();
    entry(1)->relay = 1;
    entry(1)->days = 0x7F;
    entry(1)->on = (schedule_time_t){.kind = SCHEDULE_TIME_SUNSET, .offset_min = -30};   // 18:16
    entry(1)->off = (schedule_time_t){.kind = SCHEDULE_TIME_SUNRISE, .offset_min = 15};  // 07:10

    const double lat = 51.5074, lon = -0.1278;

    // 19:00: after sunset-30, should be on.
    espos_time_parts_t local = local_at(2026, 9, 28, 1, 19, 0, 3600);
    schedule_eval_tick(&s_cfg, true, &local, lat, lon);
    TEST_ASSERT_EQUAL(1, n_calls);
    TEST_ASSERT_TRUE(calls[0].on);

    // 07:00 the next morning: still before sunrise+15 (07:10), still on.
    local = local_at(2026, 9, 29, 2, 7, 0, 3600);
    schedule_eval_tick(&s_cfg, true, &local, lat, lon);
    TEST_ASSERT_EQUAL(1, n_calls);  // no edge

    // 08:00: past sunrise+15, off.
    local = local_at(2026, 9, 29, 2, 8, 0, 3600);
    schedule_eval_tick(&s_cfg, true, &local, lat, lon);
    TEST_ASSERT_EQUAL(2, n_calls);
    TEST_ASSERT_FALSE(calls[1].on);
}

// ---------------------------------------------------------------------- DST

TEST_CASE("a spring-forward jump across an on-time still fires the edge", "[schedule_eval]")
{
    fresh();
    reset_cfg();
    entry(1)->relay = 6;
    entry(1)->days = 0x7F;
    entry(1)->on = (schedule_time_t){.kind = SCHEDULE_TIME_CLOCK, .minute_of_day = 2 * 60 + 30};  // 02:30
    entry(1)->off = (schedule_time_t){.kind = SCHEDULE_TIME_CLOCK, .minute_of_day = 6 * 60};

    espos_time_parts_t local = local_at(2026, 3, 29, 0, 1, 59, 0);  // 01:59, before
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(1, n_calls);
    TEST_ASSERT_FALSE(calls[0].on);

    // The clock skips straight from 01:59 to 03:00 (local time itself did
    // this -- espos_time_parts() already reflects it, nothing here needs
    // to know why): 02:30 never occurs as a wall-clock instant, but the
    // window is still open at 03:00, so the very next tick still catches it.
    local.hour = 3;
    local.minute = 0;
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(2, n_calls);
    TEST_ASSERT_TRUE(calls[1].on);
}

// ------------------------------------------------------------- disable/re-enable

TEST_CASE("a disabled entry (relay=0) is skipped; re-enabling resyncs", "[schedule_eval]")
{
    fresh();
    reset_cfg();
    entry(1)->relay = 0;
    entry(1)->days = 0x7F;
    entry(1)->on = (schedule_time_t){.kind = SCHEDULE_TIME_CLOCK, .minute_of_day = 0};
    entry(1)->off = (schedule_time_t){.kind = SCHEDULE_TIME_CLOCK, .minute_of_day = 1439};

    espos_time_parts_t local = local_at(2026, 9, 28, 1, 12, 0, 0);
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(0, n_calls);

    entry(1)->relay = 7;  // re-enabled, still within the (huge) window
    schedule_eval_tick(&s_cfg, true, &local, 0, 0);
    TEST_ASSERT_EQUAL(1, n_calls);
    TEST_ASSERT_EQUAL(7, calls[0].channel);
    TEST_ASSERT_TRUE(calls[0].on);
}

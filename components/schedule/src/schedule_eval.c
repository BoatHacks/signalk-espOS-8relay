#include "schedule_eval.h"

#include <string.h>

#include "espos_health.h"
#include "sun.h"

#define HEALTH_KEY_NO_TIME "scheduleNoTime"

typedef struct {
    bool valid;  // has this entry's "last active" actually been computed
    bool active;
} entry_state_t;

static struct {
    schedule_eval_io_t io;
    entry_state_t entries[SCHEDULE_MAX_ENTRIES];
    bool was_time_valid;
} s;

void schedule_eval_init(const schedule_eval_io_t *io)
{
    memset(&s, 0, sizeof(s));
    s.io = *io;
}

void schedule_eval_reset(void)
{
    memset(&s, 0, sizeof(s));
}

static int wrap_minute(int m)
{
    m %= 1440;
    if (m < 0) {
        m += 1440;
    }
    return m;
}

// Local minute-of-day `t` names. 0-1439 for an ordinary day; for a
// sunrise/sunset reference on a polar day/night (no transition -- issue
// #9, plan 16, decided 2026-09-28), a constant standing in for "this
// boundary already happened" (0) or "this boundary never happens today"
// (1440, deliberately one past the last real minute): the whole day counts
// as continuously on one side of the sunrise/sunset line, and every actual
// `now` (always in 0-1439) falls on the correct side of that constant
// without clock_active() needing to know polar conditions exist at all.
// Permanent polar night (SUN_ALWAYS_DOWN): sunset already happened (0),
// sunrise never comes (1440) -- continuously night. Permanent polar day
// (SUN_ALWAYS_UP): the mirror image -- continuously day. An offset
// ("sunset-30") is not applied here: there is no actual event today to
// offset from.
static int resolve_minute(const schedule_time_t *t, const espos_time_parts_t *local, const sun_times_t *sun)
{
    if (t->kind == SCHEDULE_TIME_CLOCK) {
        return t->minute_of_day;
    }
    if (sun->kind != SUN_TRANSITION) {
        const bool boundary_already_passed = (sun->kind == SUN_ALWAYS_DOWN && t->kind == SCHEDULE_TIME_SUNSET) ||
                                              (sun->kind == SUN_ALWAYS_UP && t->kind == SCHEDULE_TIME_SUNRISE);
        return boundary_already_passed ? 0 : 1440;
    }
    const int64_t instant_ms = t->kind == SCHEDULE_TIME_SUNRISE ? sun->sunrise_unix_ms : sun->sunset_unix_ms;
    // Local minute-of-day of that instant, using *today's* utc_offset_s as
    // a stand-in for the offset at the instant itself: exact except within
    // a few hours of a DST transition, on a day an entry references
    // sunrise/sunset at all -- a documented, minute-resolution-scale
    // limitation, not a silent one.
    int64_t local_ms = instant_ms + (int64_t)local->utc_offset_s * 1000;
    int minute = (int)((local_ms / 60000) % 1440);
    if (minute < 0) {
        minute += 1440;
    }
    return wrap_minute(minute + t->offset_min);
}

static bool day_in_mask(uint8_t mask, int wday)
{
    return (mask & (1u << wday)) != 0;
}

static bool clock_active(const schedule_cfg_t *sc, const espos_time_parts_t *local, const sun_times_t *sun_today)
{
    const int now_min = local->hour * 60 + local->minute;
    const int today_wday = local->wday;
    const int yesterday_wday = (today_wday + 6) % 7;

    // Never negative: resolve_minute() always returns 0-1439 for a real
    // transition or a clock time, or the polar-day/night constant 0/1440.
    const int on_min = resolve_minute(&sc->on, local, sun_today);
    const int off_min = resolve_minute(&sc->off, local, sun_today);

    bool active = false;
    if (day_in_mask(sc->days, (uint8_t)today_wday)) {
        active = (on_min <= off_min) ? (now_min >= on_min && now_min < off_min)
                                      : (now_min >= on_min || now_min < off_min);
    }
    if (!active && on_min > off_min && day_in_mask(sc->days, (uint8_t)yesterday_wday)) {
        // Yesterday's window spans past midnight (on > off): still in its
        // tail end if we haven't reached today's off-time yet. today_wday
        // being excluded from the mask does not cut this short -- the
        // window already started on a day the mask permitted.
        active = now_min < off_min;
    }
    return active;
}

static bool repeat_active(const schedule_cfg_t *sc, const espos_time_parts_t *local)
{
    // Anchored to local midnight, and does not carry over from yesterday
    // (decided 2026-09-28): a repeat cycle resets cleanly at midnight, so
    // only today's weekday gates it, unlike clock mode's spanning windows
    // above.
    if (!day_in_mask(sc->days, local->wday)) {
        return false;
    }
    const int now_min = local->hour * 60 + local->minute;
    return (now_min % sc->period_min) < sc->on_min;
}

void schedule_eval_tick(const device_config_t *cfg, bool time_valid, const espos_time_parts_t *local, double lat,
                         double lon)
{
    if (!time_valid) {
        for (int i = 0; i < SCHEDULE_MAX_ENTRIES; i++) {
            s.entries[i].valid = false;  // force a resync once time is valid again
        }
        espos_health_report(HEALTH_KEY_NO_TIME, ESPOS_HEALTH_WARN,
                             "No valid time (RTC, SNTP or SignalK); schedules are not running");
        s.was_time_valid = false;
        return;
    }
    if (!s.was_time_valid) {
        espos_health_report(HEALTH_KEY_NO_TIME, ESPOS_HEALTH_NORMAL, NULL);
    }
    s.was_time_valid = true;

    // Sun times cost a handful of trig calls: skip them when nothing this
    // tick actually references sunrise/sunset.
    bool need_sun = false;
    for (int i = 0; i < SCHEDULE_MAX_ENTRIES; i++) {
        const schedule_cfg_t *sc = &cfg->schedules[i];
        if (sc->relay != 0 && sc->mode == SCHEDULE_MODE_CLOCK &&
            (sc->on.kind != SCHEDULE_TIME_CLOCK || sc->off.kind != SCHEDULE_TIME_CLOCK)) {
            need_sun = true;
            break;
        }
    }
    sun_times_t sun_today = {0};
    if (need_sun) {
        sun_times(local->year, local->month, local->day, lat, lon, &sun_today);
    }

    for (int i = 0; i < SCHEDULE_MAX_ENTRIES; i++) {
        const schedule_cfg_t *sc = &cfg->schedules[i];
        if (sc->relay == 0) {
            s.entries[i].valid = false;  // forgotten, so re-enabling resyncs rather than waiting for an edge
            continue;
        }
        const bool active =
            sc->mode == SCHEDULE_MODE_REPEAT ? repeat_active(sc, local) : clock_active(sc, local, &sun_today);
        if (!s.entries[i].valid || active != s.entries[i].active) {
            s.io.set_relay(sc->relay, active);
            s.entries[i].active = active;
            s.entries[i].valid = true;
        }
    }
}

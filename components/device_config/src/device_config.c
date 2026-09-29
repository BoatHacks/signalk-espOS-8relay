#include "device_config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "espos_config.h"
#include "espos_health.h"

#define HEALTH_KEY "bankIdClash"
#define HEALTH_KEY_INTERLOCK "interlockInvalid"
#define HEALTH_KEY_SCHEDULE "scheduleOverlap"

static const char *TAG = "device_config";

static int32_t get_int(const char *key)
{
    int32_t v = 0;
    espos_config_get_i32(DEVICE_CONFIG_NS, key, &v);
    return v;
}

static bool get_bool(const char *key)
{
    bool v = false;
    espos_config_get_bool(DEVICE_CONFIG_NS, key, &v);
    return v;
}

static void get_str(const char *key, char *buf, size_t size)
{
    buf[0] = '\0';
    espos_config_get_str(DEVICE_CONFIG_NS, key, buf, size, NULL);
}

static float get_float(const char *key)
{
    float v = 0.0f;
    espos_config_get_float(DEVICE_CONFIG_NS, key, &v);
    return v;
}

// "HH:MM", exactly 5 characters, both fields two digits (no single-digit
// short form): 0 <= h <= 23, 0 <= m <= 59.
static bool parse_hhmm(const char *s, int *minute_of_day)
{
    if (!isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1]) || s[2] != ':' ||
        !isdigit((unsigned char)s[3]) || !isdigit((unsigned char)s[4]) || s[5] != '\0') {
        return false;
    }
    const int h = (s[0] - '0') * 10 + (s[1] - '0');
    const int m = (s[3] - '0') * 10 + (s[4] - '0');
    if (h > 23 || m > 59) {
        return false;
    }
    *minute_of_day = h * 60 + m;
    return true;
}

// The "+30", "-45m" (a trailing "m" is optional and ignored) part of
// "sunrise+30"/"sunset-45m", or "" for a bare "sunrise"/"sunset" (offset 0).
// 0-1439 minutes either side of the sign.
static bool parse_offset_suffix(const char *s, int16_t *offset_min)
{
    if (*s == '\0') {
        *offset_min = 0;
        return true;
    }
    int sign = 1;
    if (*s == '+') {
        s++;
    } else if (*s == '-') {
        sign = -1;
        s++;
    } else {
        return false;
    }
    int v = 0;
    int digits = 0;
    while (isdigit((unsigned char)*s) && digits < 4) {
        v = v * 10 + (*s - '0');
        s++;
        digits++;
    }
    if (digits == 0) {
        return false;
    }
    if (*s == 'm') {
        s++;
    }
    if (*s != '\0' || v > 1439) {
        return false;
    }
    *offset_min = (int16_t)(sign * v);
    return true;
}

// SCHEDULE_MODE_CLOCK's on/off strings: "HH:MM", "sunrise[+-Nm]" or
// "sunset[+-Nm]" (issue #9, plan 16).
static bool parse_schedule_time(const char *s, schedule_time_t *out)
{
    if (strncmp(s, "sunrise", 7) == 0) {
        out->kind = SCHEDULE_TIME_SUNRISE;
        return parse_offset_suffix(s + 7, &out->offset_min);
    }
    if (strncmp(s, "sunset", 6) == 0) {
        out->kind = SCHEDULE_TIME_SUNSET;
        return parse_offset_suffix(s + 6, &out->offset_min);
    }
    int minute_of_day;
    if (!parse_hhmm(s, &minute_of_day)) {
        return false;
    }
    out->kind = SCHEDULE_TIME_CLOCK;
    out->minute_of_day = (int16_t)minute_of_day;
    return true;
}

// SCHEDULE_MODE_REPEAT's on/off strings: plain decimal minutes, 1-1439 (no
// sign, no unit suffix -- unlike the clock-mode offsets above, there's
// nothing here for a sign to mean).
static bool parse_minutes(const char *s, uint16_t *out)
{
    if (!isdigit((unsigned char)*s)) {
        return false;
    }
    int v = 0;
    int digits = 0;
    while (isdigit((unsigned char)*s) && digits < 4) {
        v = v * 10 + (*s - '0');
        s++;
        digits++;
    }
    if (*s != '\0' || v < 1 || v > 1439) {
        return false;
    }
    *out = (uint16_t)v;
    return true;
}

esp_err_t device_config_load(device_config_t *out)
{
    if (!espos_config_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(out, 0, sizeof(*out));
    out->bank_id = (uint8_t)get_int("bank_id");
    out->input_bank_id = (uint8_t)get_int("input_bank_id");
    out->debounce_ms = (uint16_t)get_int("debounce_ms");
    out->sk_loss_grace_s = (uint16_t)get_int("sk_grace_s");
    out->sk_republish_s = (uint16_t)get_int("sk_repub_s");
    out->eth_enabled = get_bool("eth_enabled");
    out->publish_switches_tree = get_bool("pub_switches");
    out->publish_controls_tree = get_bool("pub_controls");
    out->n2k_alerts = get_bool("n2k_alerts");
    out->led_brightness = (uint8_t)get_int("led_brightness");
    out->buzzer_on_alarm = get_bool("buzzer_alarm");
    out->buzzer_freq_hz = (uint16_t)get_int("buzzer_freq_hz");
    out->buzzer_on_event = get_bool("buzzer_event");
    get_str("tone_patterns", out->tone_patterns, sizeof(out->tone_patterns));
    get_str("boot_tone", out->boot_tone, sizeof(out->boot_tone));
    get_str("portal_tone", out->portal_tone, sizeof(out->portal_tone));
    get_str("reset_tone", out->factory_reset_tone, sizeof(out->factory_reset_tone));
    out->interlock_dead_ms = (uint32_t)get_int("interlock_dead");

    char top_val[24];
    get_str("position_src", top_val, sizeof(top_val));
    out->position_source = strcmp(top_val, "n2k") == 0 ? POSITION_SRC_N2K : POSITION_SRC_SIGNALK;
    out->fallback_lat = get_float("fallback_lat");
    out->fallback_lon = get_float("fallback_lon");

    char key[24];
    char val[24];
    for (int n = 1; n <= BOARD_CHANNELS; n++) {
        relay_cfg_t *r = &out->relays[n - 1];
        snprintf(key, sizeof(key), "relay%d_name", n);
        get_str(key, r->name, sizeof(r->name));
        snprintf(key, sizeof(key), "relay%d_mode", n);
        get_str(key, val, sizeof(val));
        r->mode = strcmp(val, "momentary") == 0 ? RELAY_MODE_MOMENTARY : RELAY_MODE_LATCHING;
        snprintf(key, sizeof(key), "relay%d_pulse_ms", n);
        r->pulse_ms = (uint32_t)get_int(key);
        snprintf(key, sizeof(key), "relay%d_failsafe", n);
        get_str(key, val, sizeof(val));
        r->failsafe = strcmp(val, "hold") == 0 ? FAILSAFE_HOLD : FAILSAFE_DEFAULT_SAFE;
        snprintf(key, sizeof(key), "relay%d_override", n);
        r->override_di = (uint8_t)get_int(key);
        snprintf(key, sizeof(key), "relay%d_wired_nc", n);
        r->wired_nc = get_bool(key);
        snprintf(key, sizeof(key), "relay%d_link", n);
        get_str(key, val, sizeof(val));
        r->link = strcmp(val, "toggle") == 0 ? INPUT_LINK_TOGGLE : INPUT_LINK_FOLLOW;
        snprintf(key, sizeof(key), "relay%d_max_on_s", n);
        r->max_on_s = (uint32_t)get_int(key);
        // Raw for now: r%d_interlock, 0-8 (schema-enforced). Reduced to the
        // validated, effective pair below, once every relay's raw value has
        // been read.
        snprintf(key, sizeof(key), "r%d_interlock", n);
        r->interlock = (uint8_t)get_int(key);
        snprintf(key, sizeof(key), "relay%d_on_tone", n);
        get_str(key, r->on_tone, sizeof(r->on_tone));
        snprintf(key, sizeof(key), "relay%d_off_tone", n);
        get_str(key, r->off_tone, sizeof(r->off_tone));
        // Stored as "ps"/"pe" (espOS key names cap at 15 chars): pulse-start,
        // pulse-end, matching RELAY_SRC_PULSE_END.
        snprintf(key, sizeof(key), "relay%d_ps_tone", n);
        get_str(key, r->pulse_start_tone, sizeof(r->pulse_start_tone));
        snprintf(key, sizeof(key), "relay%d_pe_tone", n);
        get_str(key, r->pulse_stop_tone, sizeof(r->pulse_stop_tone));

        input_cfg_t *in = &out->inputs[n - 1];
        snprintf(key, sizeof(key), "input%d_name", n);
        get_str(key, in->name, sizeof(in->name));
        snprintf(key, sizeof(key), "input%d_invert", n);
        in->invert = get_bool(key);
        snprintf(key, sizeof(key), "input%d_on_tone", n);
        get_str(key, in->on_tone, sizeof(in->on_tone));
        snprintf(key, sizeof(key), "input%d_off_tone", n);
        get_str(key, in->off_tone, sizeof(in->off_tone));
        snprintf(key, sizeof(key), "input%d_alarm", n);
        get_str(key, val, sizeof(val));
        in->alarm = strcmp(val, "warn") == 0     ? INPUT_ALARM_WARN
                    : strcmp(val, "alarm") == 0   ? INPUT_ALARM_ALARM
                    : strcmp(val, "emergency") == 0 ? INPUT_ALARM_EMERGENCY
                                                     : INPUT_ALARM_OFF;
        // "alm_msg": espOS key names cap at 15 chars.
        snprintf(key, sizeof(key), "input%d_alm_msg", n);
        get_str(key, in->alarm_msg, sizeof(in->alarm_msg));
    }

    // Schedules (issue #9, plan 16). relay=0 is both "unconfigured" and
    // this loop's own "the on/off string didn't parse" outcome -- a bad
    // string is logged (there's no dedicated health key for it, unlike the
    // cross-entry conflict below: nothing about a single entry's own string
    // is a cross-key validation problem espOS's schema couldn't already
    // have caught with a stricter type, it's just malformed free text) but
    // otherwise degrades the same as an empty entry, not a fatal load.
    char sched_val[24];
    for (int k = 1; k <= SCHEDULE_MAX_ENTRIES; k++) {
        schedule_cfg_t *sc = &out->schedules[k - 1];
        snprintf(key, sizeof(key), "s%d_relay", k);
        sc->relay = (uint8_t)get_int(key);
        if (sc->relay > BOARD_CHANNELS) {
            sc->relay = 0;
        }
        snprintf(key, sizeof(key), "s%d_mode", k);
        get_str(key, sched_val, sizeof(sched_val));
        sc->mode = strcmp(sched_val, "repeat") == 0 ? SCHEDULE_MODE_REPEAT : SCHEDULE_MODE_CLOCK;
        snprintf(key, sizeof(key), "s%d_days", k);
        sc->days = (uint8_t)(get_int(key) & 0x7F);

        if (sc->relay == 0) {
            continue;  // nothing configured; don't bother parsing on/off
        }
        char on_str[24], off_str[24];
        snprintf(key, sizeof(key), "s%d_on", k);
        get_str(key, on_str, sizeof(on_str));
        snprintf(key, sizeof(key), "s%d_off", k);
        get_str(key, off_str, sizeof(off_str));

        bool ok;
        if (sc->mode == SCHEDULE_MODE_REPEAT) {
            ok = parse_minutes(on_str, &sc->on_min) && parse_minutes(off_str, &sc->period_min) &&
                 sc->on_min < sc->period_min;
        } else {
            ok = parse_schedule_time(on_str, &sc->on) && parse_schedule_time(off_str, &sc->off);
        }
        if (!ok) {
            ESP_LOGW(TAG, "s%d: \"%s\"/\"%s\" not valid for %s mode; schedule %d disabled", k, on_str, off_str,
                     sc->mode == SCHEDULE_MODE_REPEAT ? "repeat" : "clock", k);
            sc->relay = 0;
        }
    }
    // Overlapping schedules on the same relay (decided 2026-09-28): two or
    // more *enabled* entries naming the same relay is rejected outright,
    // not resolved by slot order -- every one of them reads relay=0 until
    // fixed. A static check on the settings themselves, like interlock's
    // reciprocity check below: it does not ask whether the entries' days or
    // times could ever actually collide, only whether they name the same
    // relay at all. Snapshot first, same reason as raw_interlock below: the
    // count must see every entry's original target, not an already-cleared
    // one.
    uint8_t raw_sched_relay[SCHEDULE_MAX_ENTRIES];
    for (int i = 0; i < SCHEDULE_MAX_ENTRIES; i++) {
        raw_sched_relay[i] = out->schedules[i].relay;
    }
    out->schedule_relay_conflict = 0;
    for (int r = 1; r <= BOARD_CHANNELS; r++) {
        int count = 0;
        for (int i = 0; i < SCHEDULE_MAX_ENTRIES; i++) {
            if (raw_sched_relay[i] == r) {
                count++;
            }
        }
        if (count < 2) {
            continue;
        }
        out->schedule_relay_conflict |= (uint8_t)(1u << (r - 1));
        for (int i = 0; i < SCHEDULE_MAX_ENTRIES; i++) {
            if (raw_sched_relay[i] == r) {
                out->schedules[i].relay = 0;
            }
        }
    }

    // Derive the effective interlock pairs (issue #8, plan 15): a pair
    // counts only if both sides name each other. espOS can't validate
    // across keys, so a one-sided or self-referencing r<n>_interlock is
    // ignored here (relays[i].interlock -> 0) and flagged in
    // interlock_invalid for device_config_report_health(). Snapshot the raw
    // values first: every relay's reciprocity check must see what every
    // *other* relay originally had stored, not an already-zeroed neighbour.
    uint8_t raw_interlock[BOARD_CHANNELS];
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        raw_interlock[i] = out->relays[i].interlock;
    }
    out->interlock_invalid = 0;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        const int n = i + 1;
        const uint8_t partner = raw_interlock[i];
        if (partner == 0) {
            out->relays[i].interlock = 0;
            continue;
        }
        const bool self_ref = partner == n;
        const bool out_of_range = partner < 1 || partner > BOARD_CHANNELS;
        const bool reciprocated = !self_ref && !out_of_range && raw_interlock[partner - 1] == n;
        if (reciprocated) {
            out->relays[i].interlock = partner;
        } else {
            out->relays[i].interlock = 0;
            out->interlock_invalid |= (uint8_t)(1u << i);
        }
    }
    return ESP_OK;
}

failsafe_policy_t device_config_effective_failsafe(const relay_cfg_t *relay)
{
    return relay->mode == RELAY_MODE_MOMENTARY ? FAILSAFE_DEFAULT_SAFE : relay->failsafe;
}

bool device_config_input_bank_usable(const device_config_t *cfg)
{
    return cfg->input_bank_id != cfg->bank_id;
}

void device_config_report_health(const device_config_t *cfg)
{
    if (device_config_input_bank_usable(cfg)) {
        espos_health_report(HEALTH_KEY, ESPOS_HEALTH_NORMAL, NULL);
    } else {
        char msg[ESPOS_HEALTH_MSG_MAX];
        snprintf(msg, sizeof(msg), "Input bank id %u equals relay bank id; inputs not published until changed",
                 cfg->input_bank_id);
        espos_health_report(HEALTH_KEY, ESPOS_HEALTH_WARN, msg);
    }

    if (cfg->interlock_invalid == 0) {
        espos_health_report(HEALTH_KEY_INTERLOCK, ESPOS_HEALTH_NORMAL, NULL);
    } else {
        // "Relay 3, 6: ..." -- list every relay whose setting was ignored.
        char list[32] = "";
        size_t len = 0;
        for (int i = 0; i < BOARD_CHANNELS; i++) {
            if (!(cfg->interlock_invalid & (1u << i))) {
                continue;
            }
            int n = snprintf(list + len, sizeof(list) - len, "%s%d", len ? ", " : "", i + 1);
            if (n > 0 && (size_t)n < sizeof(list) - len) {
                len += (size_t)n;
            }
        }
        char msg[ESPOS_HEALTH_MSG_MAX];
        snprintf(msg, sizeof(msg), "Relay %s: interlock setting not reciprocated; ignored", list);
        espos_health_report(HEALTH_KEY_INTERLOCK, ESPOS_HEALTH_WARN, msg);
    }

    if (cfg->schedule_relay_conflict == 0) {
        espos_health_report(HEALTH_KEY_SCHEDULE, ESPOS_HEALTH_NORMAL, NULL);
        return;
    }
    // "Relay 2, 5: ..." -- list every relay with two or more conflicting
    // schedule entries.
    char sched_list[32] = "";
    size_t sched_len = 0;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        if (!(cfg->schedule_relay_conflict & (1u << i))) {
            continue;
        }
        int n = snprintf(sched_list + sched_len, sizeof(sched_list) - sched_len, "%s%d", sched_len ? ", " : "",
                          i + 1);
        if (n > 0 && (size_t)n < sizeof(sched_list) - sched_len) {
            sched_len += (size_t)n;
        }
    }
    char sched_msg[ESPOS_HEALTH_MSG_MAX];
    snprintf(sched_msg, sizeof(sched_msg), "Relay %s: two or more schedules target the same relay; all ignored",
             sched_list);
    espos_health_report(HEALTH_KEY_SCHEDULE, ESPOS_HEALTH_WARN, sched_msg);
}

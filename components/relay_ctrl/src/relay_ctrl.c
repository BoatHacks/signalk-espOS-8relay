#include "relay_ctrl.h"

#include <stdio.h>
#include <string.h>

#include "espos_health.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define HEALTH_KEY "relayExpander"
// Boot/hold-restore or a config change found an interlocked pair's coils
// both on (issue #8): distinct from device_config's own "interlockInvalid"
// (a bad r<n>_interlock *setting*) -- this is a bad *state* despite valid
// settings, e.g. older firmware's stored mask, or a relay just wired into
// a new pair while its partner happened to be on.
#define HEALTH_KEY_INTERLOCK "interlockBothOn"
#define MAX_LISTENERS 4

typedef struct {
    relay_listener_t cb;
    void *arg;
} listener_t;

static struct {
    SemaphoreHandle_t lock;
    relay_ctrl_hw_t hw;
    device_config_t cfg;
    uint8_t mask;               // last state confirmed on the chip
    uint8_t pulsing;            // momentary relays with a running pulse
    uint32_t pulse_end[BOARD_CHANNELS];
    uint8_t limited;            // latching relays with a running max on-time
    uint32_t max_on_end[BOARD_CHANNELS];
    // Interlocked relays (issue #8, plan 15) waiting out the dead time
    // before their coil goes on, having already switched their partner's
    // coil off. Coil-level, like everything else in this struct: a relay's
    // interlock partner is `cfg.relays[i].interlock` in coil terms too, per
    // the 2026-09-28 decision (wiredNC never enters into it).
    uint8_t pending_on;
    uint32_t pending_on_end[BOARD_CHANNELS];
    relay_source_t pending_on_src[BOARD_CHANNELS];
    bool save_pending;
    uint32_t last_save_ms;
    bool i2c_fault;
    listener_t listeners[MAX_LISTENERS];
} s;

static uint8_t to_reg(uint8_t mask)
{
    return BOARD_RELAY_ACTIVE_HIGH ? mask : (uint8_t)~mask;
}

static uint8_t from_reg(uint8_t reg)
{
    return BOARD_RELAY_ACTIVE_HIGH ? reg : (uint8_t)~reg;
}

static bool is_momentary(int i)
{
    return s.cfg.relays[i].mode == RELAY_MODE_MOMENTARY;
}

static uint8_t hold_mask(void)
{
    uint8_t m = 0;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        if (device_config_effective_failsafe(&s.cfg.relays[i]) == FAILSAFE_HOLD) {
            m |= 1u << i;
        }
    }
    return m;
}

// Channels wired to their NC terminal: a coil bit set in this mask reads/
// commands as the *opposite* logical (load) state at relay_ctrl's public
// get/set/PUT surface. Every internal coil-level path (fail-safe, boot,
// momentary pulses, max on-time, hold storage) must never use this -- it
// operates on s.mask directly and is deliberately left alone by this
// translation, mirroring input_sense's `invert` (SPEC.md §4).
static uint8_t wired_nc_mask(void)
{
    uint8_t m = 0;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        if (s.cfg.relays[i].wired_nc) {
            m |= 1u << i;
        }
    }
    return m;
}

// Maximum on-time in ms, 0 = none. Momentary relays end by themselves.
static uint32_t max_on_ms(int i)
{
    return is_momentary(i) ? 0 : s.cfg.relays[i].max_on_s * 1000u;
}

// This relay's validated interlock partner's coil bit, or 0 if it has none.
// `cfg.relays[i].interlock` is already the effective, reciprocated pair
// (device_config_load()), coil-numbered like everything else here.
static uint8_t interlock_partner_bit(int i)
{
    const uint8_t p = s.cfg.relays[i].interlock;
    return p ? (uint8_t)(1u << (p - 1)) : 0;
}

// Clears both coil bits of every interlocked pair that's set together in
// `mask` -- restoring or keeping neither is the safe choice when both
// somehow ended up wanting to be on (issue #8, plan 15: boot/hold-restore,
// or a config change that just paired up two relays that were already on).
// `*conflict` gets every cleared bit OR'd in, for the health warning.
static uint8_t clear_interlock_conflicts(uint8_t mask, uint8_t *conflict)
{
    uint8_t out = mask;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        const uint8_t bit = 1u << i;
        const uint8_t partner_bit = interlock_partner_bit(i);
        if (partner_bit && (mask & bit) && (mask & partner_bit)) {
            out &= ~bit;
            if (conflict) {
                *conflict |= bit;
            }
        }
    }
    return out;
}

// Raise or clear the "both interlocked coils were on" warning. `conflict`
// is the bitmask clear_interlock_conflicts() reported, 0 = nothing to warn
// about (clears a previous warning, if any).
static void report_interlock_health(uint8_t conflict)
{
    if (conflict == 0) {
        espos_health_report(HEALTH_KEY_INTERLOCK, ESPOS_HEALTH_NORMAL, NULL);
        return;
    }
    char list[32] = "";
    size_t len = 0;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        if (!(conflict & (1u << i))) {
            continue;
        }
        int n = snprintf(list + len, sizeof(list) - len, "%s%d", len ? ", " : "", i + 1);
        if (n > 0 && (size_t)n < sizeof(list) - len) {
            len += (size_t)n;
        }
    }
    char msg[ESPOS_HEALTH_MSG_MAX];
    snprintf(msg, sizeof(msg), "Relay %s: both interlocked coils were on; switched off", list);
    espos_health_report(HEALTH_KEY_INTERLOCK, ESPOS_HEALTH_WARN, msg);
}

static bool deadline_passed(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;  // correct across uint32 wrap
}

static void report_fault(esp_err_t err)
{
    char msg[ESPOS_HEALTH_MSG_MAX];
    snprintf(msg, sizeof(msg), "Relay expander not responding (%s); relay state may be wrong",
             esp_err_to_name(err));
    espos_health_report(HEALTH_KEY, ESPOS_HEALTH_ALARM, msg);
    s.i2c_fault = true;
}

// Write `target` to the chip (one retry). On success it becomes s.mask.
static esp_err_t commit_locked(uint8_t target)
{
    if (target == s.mask) {
        return ESP_OK;
    }
    esp_err_t err = tca9554_write_outputs(&s.hw.expander, to_reg(target));
    if (err != ESP_OK) {
        err = tca9554_write_outputs(&s.hw.expander, to_reg(target));
    }
    if (err != ESP_OK) {
        report_fault(err);
        return err;
    }
    if (s.i2c_fault) {
        espos_health_report(HEALTH_KEY, ESPOS_HEALTH_NORMAL, NULL);
        s.i2c_fault = false;
    }
    if ((target ^ s.mask) & hold_mask()) {
        s.save_pending = true;
    }
    s.mask = target;
    return ESP_OK;
}

// Keep max on-time timers in step with s.mask after a change: a relay that
// came on starts its timer, one that went off drops it, and `restart` (an
// explicit "on" command) restarts it for a relay that was already on.
static void track_max_on_locked(uint8_t before, uint8_t restart)
{
    const uint32_t now = s.hw.now_ms();
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        const uint8_t bit = 1u << i;
        if (!(s.mask & bit) || max_on_ms(i) == 0) {
            s.limited &= ~bit;
        } else if (!(before & bit) || (restart & bit)) {
            s.max_on_end[i] = now + max_on_ms(i);
            s.limited |= bit;
        }
    }
}

// Tell listeners about every relay named in `changed`, reporting `after`
// (already wiredNC-translated) as both the new mask and each one's new
// state. Called without the lock held.
static void notify_bits(uint8_t changed, uint8_t after, relay_source_t src)
{
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        if (!(changed & (1u << i))) {
            continue;
        }
        for (int l = 0; l < MAX_LISTENERS; l++) {
            if (s.listeners[l].cb) {
                s.listeners[l].cb(i + 1, after & (1u << i), src, after, s.listeners[l].arg);
            }
        }
    }
}

// Tell listeners about every relay that differs between `before` and
// `after`, all from the same source. Called without the lock held.
static void notify(uint8_t before, uint8_t after, relay_source_t src)
{
    notify_bits(before ^ after, after, src);
}

esp_err_t relay_ctrl_init(const relay_ctrl_hw_t *hw, const device_config_t *cfg)
{
    if (!s.lock) {
        s.lock = xSemaphoreCreateMutex();
        if (!s.lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    s.hw = *hw;
    s.cfg = *cfg;
    s.pulsing = 0;
    s.limited = 0;
    s.pending_on = 0;
    s.save_pending = false;
    s.last_save_ms = hw->now_ms() - RELAY_CTRL_SAVE_DELAY_MS;

    bool warm;
    esp_err_t err = tca9554_all_outputs(&s.hw.expander, &warm);
    if (err != ESP_OK) {
        report_fault(err);
        return err;
    }

    uint8_t held;
    if (warm) {
        uint8_t reg;
        err = tca9554_read_outputs(&s.hw.expander, &reg);
        if (err != ESP_OK) {
            report_fault(err);
            return err;
        }
        held = from_reg(reg);
    } else if (s.hw.store.load(s.hw.store.ctx, &held) != ESP_OK) {
        held = 0;  // nothing stored yet: everything starts off
    }
    // Restore (or keep) neither side of an interlocked pair that's somehow
    // both on -- older firmware's stored mask, most likely (issue #8).
    uint8_t boot_conflict = 0;
    const uint8_t target = clear_interlock_conflicts(held & hold_mask(), &boot_conflict);

    err = warm ? tca9554_write_outputs(&s.hw.expander, to_reg(target))
               : tca9554_init_outputs(&s.hw.expander, to_reg(target));
    if (err != ESP_OK) {
        report_fault(err);
        return err;
    }
    s.mask = target;
    // A relay restored on starts a fresh max on-time: the time it spent on
    // before the restart isn't known.
    track_max_on_locked(0, 0);
    // A warm boot may have held a newer state than the store had; so did a
    // boot conflict just corrected above -- the store's bad mask shouldn't
    // linger.
    s.save_pending = warm || boot_conflict != 0;
    report_interlock_health(boot_conflict);
    return ESP_OK;
}

void relay_ctrl_update_config(const device_config_t *cfg)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const uint8_t old_hold = hold_mask();
    const uint32_t now = s.hw.now_ms();
    uint32_t old_limit[BOARD_CHANNELS];
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        old_limit[i] = max_on_ms(i);
    }
    s.cfg = *cfg;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        const uint8_t bit = 1u << i;
        if (!is_momentary(i)) {
            s.pulsing &= ~bit;  // now latching: an "on" stays on
        } else if ((s.mask & bit) && !(s.pulsing & bit)) {
            // Became momentary while on: a momentary relay must never stay
            // on indefinitely, so its pulse starts now.
            s.pulse_end[i] = now + s.cfg.relays[i].pulse_ms;
            s.pulsing |= bit;
        }
    }
    if (hold_mask() != old_hold) {
        s.save_pending = true;
    }
    // A changed max on-time applies from now to a relay that is on; 0 or
    // momentary mode cancels it.
    uint8_t changed_limit = 0;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        if (max_on_ms(i) != old_limit[i]) {
            changed_limit |= 1u << i;
        }
    }
    track_max_on_locked(s.mask, changed_limit);

    // A config change that just paired up (or re-paired) two relays while
    // both happened to be on: switch both off and warn (issue #8) -- the
    // safe choice when the setup itself just changed underneath them.
    const uint8_t before = s.mask;
    uint8_t conflict = 0;
    clear_interlock_conflicts(s.mask, &conflict);
    if (conflict) {
        if (commit_locked(s.mask & ~conflict) == ESP_OK) {
            s.pulsing &= ~conflict;
            s.pending_on &= ~conflict;
            track_max_on_locked(before, 0);
        } else {
            conflict = 0;  // commit failed (I2C fault): nothing actually changed
        }
    }
    report_interlock_health(conflict);
    const uint8_t after = s.mask;
    const uint8_t wnc = wired_nc_mask();
    xSemaphoreGive(s.lock);
    notify(before ^ wnc, after ^ wnc, RELAY_SRC_INTERLOCK);
}

// relay_ctrl_set() and relay_ctrl_toggle(): `on_req` < 0 means "the opposite
// of now" (a toggle), decided under the lock so two sources can't both read
// the old state. A toggle flips the coil bit directly: flipping a bit and
// translating it for `wiredNC` commute, so that's also the opposite of the
// current *logical* state -- no separate translation needed. An explicit
// on_req (0 or 1) is the caller's requested *logical* (load) state and is
// translated to the coil level here, at the boundary, before anything below
// touches the coil.
static esp_err_t set_or_toggle(uint8_t channel, int on_req, relay_source_t src)
{
    if (channel < 1 || channel > BOARD_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    const int i = channel - 1;
    const uint8_t bit = 1u << i;

    xSemaphoreTake(s.lock, portMAX_DELAY);
    const uint8_t before = s.mask;
    const bool on = on_req < 0 ? !(s.mask & bit) : ((bool)on_req != s.cfg.relays[i].wired_nc);
    esp_err_t err = ESP_OK;

    if (!on) {
        // Off always cancels a pending-on for this same channel (plan 15):
        // if it hadn't committed to the coil yet, it never will now.
        s.pending_on &= ~bit;
        err = commit_locked(s.mask & ~bit);
        if (err == ESP_OK) {
            s.pulsing &= ~bit;
            track_max_on_locked(before, 0);
        }
    } else {
        const uint8_t partner_bit = interlock_partner_bit(i);
        if (partner_bit && (s.mask & partner_bit)) {
            // Interlocked with a relay whose coil is on right now (issue
            // #8): that coil goes off immediately, and this one only goes
            // on after the dead time -- checked in the tick, like a pulse.
            // Never both on in the same expander write.
            err = commit_locked(s.mask & ~partner_bit);
            if (err == ESP_OK) {
                s.pulsing &= ~partner_bit;
                s.pending_on &= ~partner_bit;  // can't itself be pending too
                track_max_on_locked(before, 0);
                s.pending_on |= bit;
                s.pending_on_end[i] = s.hw.now_ms() + s.cfg.interlock_dead_ms;
                s.pending_on_src[i] = src;
            }
        } else if (!(s.pending_on & bit)) {
            // No conflict, and not already waiting out a previous one (a
            // repeat "on" while pending is a no-op -- still waiting).
            err = commit_locked(s.mask | bit);
            if (err == ESP_OK) {
                if (is_momentary(i)) {
                    s.pulse_end[i] = s.hw.now_ms() + s.cfg.relays[i].pulse_ms;
                    s.pulsing |= bit;
                }
                // An "on" to a relay already on restarts its max on-time.
                track_max_on_locked(before, bit);
                // This channel's coil is now on: its partner's pending-on,
                // if it had one waiting on this channel going off, is
                // cancelled (plan 15) -- it must not fire into a conflict.
                if (partner_bit) {
                    s.pending_on &= ~partner_bit;
                }
            }
        }
    }

    const uint8_t after = s.mask;
    const uint8_t wnc = wired_nc_mask();
    xSemaphoreGive(s.lock);

    // Listeners (SignalK, NMEA 2000, the web page, chirps) see the reported
    // (logical) state, not the coil.
    notify(before ^ wnc, after ^ wnc, src);
    return err;
}

esp_err_t relay_ctrl_set(uint8_t channel, bool on, relay_source_t src)
{
    return set_or_toggle(channel, on ? 1 : 0, src);
}

esp_err_t relay_ctrl_toggle(uint8_t channel, relay_source_t src)
{
    return set_or_toggle(channel, -1, src);
}

bool relay_ctrl_get(uint8_t channel)
{
    return channel >= 1 && channel <= BOARD_CHANNELS && (relay_ctrl_get_mask() & (1u << (channel - 1)));
}

// Reported (logical/load) state, translated for `wiredNC`. Every internal
// user of coil state (fail-safe, boot, momentary, max on-time, hold
// storage) reads s.mask directly instead, never this.
uint8_t relay_ctrl_get_mask(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    uint8_t m = s.mask ^ wired_nc_mask();
    xSemaphoreGive(s.lock);
    return m;
}

bool relay_ctrl_is_momentary(uint8_t channel)
{
    if (channel < 1 || channel > BOARD_CHANNELS) {
        return false;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const bool momentary = s.cfg.relays[channel - 1].mode == RELAY_MODE_MOMENTARY;
    xSemaphoreGive(s.lock);
    return momentary;
}

esp_err_t relay_ctrl_add_listener(relay_listener_t cb, void *arg)
{
    for (int l = 0; l < MAX_LISTENERS; l++) {
        if (!s.listeners[l].cb) {
            s.listeners[l] = (listener_t){cb, arg};
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

void relay_ctrl_sk_lost(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const uint8_t before = s.mask;
    // Fail-safe always de-energizes the coil directly, unconditionally --
    // exactly what a real power loss does, whatever wiredNC says (SPEC.md
    // §2, §4). A wiredNC relay then correctly *reports* on afterwards (its
    // NC-wired load is powered by a de-energized coil): that's translated
    // below for listeners, not re-inverted a second time here.
    const uint8_t off = (uint8_t)~hold_mask();
    if (commit_locked(s.mask & ~off) == ESP_OK) {
        s.pulsing &= ~off;
        track_max_on_locked(before, 0);
    }
    // A default-safe relay waiting out its interlock dead time must not
    // switch on after all once SignalK is lost -- same as a running pulse
    // above, this is a command still in flight that fail-safe cuts short.
    s.pending_on &= ~off;
    const uint8_t after = s.mask;
    const uint8_t wnc = wired_nc_mask();
    xSemaphoreGive(s.lock);
    notify(before ^ wnc, after ^ wnc, RELAY_SRC_FAILSAFE);
}

void relay_ctrl_tick(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const uint32_t now = s.hw.now_ms();
    const uint8_t before = s.mask;

    uint8_t ended = 0;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        if ((s.pulsing & (1u << i)) && deadline_passed(now, s.pulse_end[i])) {
            ended |= 1u << i;
        }
    }
    if (ended && commit_locked(s.mask & ~ended) == ESP_OK) {
        s.pulsing &= ~ended;
    }
    const uint8_t after_pulses = s.mask;

    uint8_t expired = 0;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        if ((s.limited & (1u << i)) && deadline_passed(now, s.max_on_end[i])) {
            expired |= 1u << i;
        }
    }
    if (expired && commit_locked(s.mask & ~expired) == ESP_OK) {
        s.limited &= ~expired;
    }
    const uint8_t after_max_on = s.mask;

    // Interlocked relays (issue #8, plan 15) whose dead time has run out.
    uint8_t fired = 0;
    relay_source_t fired_src[BOARD_CHANNELS];
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        if (!(s.pending_on & (1u << i)) || !deadline_passed(now, s.pending_on_end[i])) {
            continue;
        }
        // Safety net: only commit if the partner's coil is actually off.
        // It always should be -- set_or_toggle() cancels a pending-on the
        // moment its partner's coil goes back on -- but a timer must never
        // be trusted to energize both coils together on its own; drop a
        // stale pending-on instead.
        const uint8_t partner_bit = interlock_partner_bit(i);
        if (!partner_bit || !(s.mask & partner_bit)) {
            fired |= 1u << i;
            fired_src[i] = s.pending_on_src[i];
        } else {
            s.pending_on &= ~(1u << i);
        }
    }
    if (fired && commit_locked(s.mask | fired) == ESP_OK) {
        for (int i = 0; i < BOARD_CHANNELS; i++) {
            if (!(fired & (1u << i))) {
                continue;
            }
            s.pending_on &= ~(1u << i);
            if (is_momentary(i)) {
                s.pulse_end[i] = now + s.cfg.relays[i].pulse_ms;
                s.pulsing |= (1u << i);
            }
        }
        track_max_on_locked(after_max_on, fired);
    } else {
        fired = 0;  // nothing committed (no channel due, or an I2C fault)
    }
    const uint8_t after_fired = s.mask;

    if (s.save_pending && now - s.last_save_ms >= RELAY_CTRL_SAVE_DELAY_MS) {
        if (s.hw.store.save(s.hw.store.ctx, s.mask & hold_mask()) == ESP_OK) {
            s.save_pending = false;
        }
        s.last_save_ms = now;  // also on failure, so a bad flash isn't hammered
    }
    const uint8_t wnc = wired_nc_mask();
    xSemaphoreGive(s.lock);

    notify(before ^ wnc, after_pulses ^ wnc, RELAY_SRC_PULSE_END);
    notify(after_pulses ^ wnc, after_max_on ^ wnc, RELAY_SRC_MAX_ON);
    // Each fired relay reports with the source of the "on" command that
    // scheduled it (SignalK, N2K, web, an input override) -- not a
    // synthetic one -- since it succeeds that original command, delayed.
    const uint8_t after_fired_wnc = after_fired ^ wnc;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        if (fired & (1u << i)) {
            notify_bits((uint8_t)(1u << i), after_fired_wnc, fired_src[i]);
        }
    }
}

void relay_ctrl_reset(void)
{
    SemaphoreHandle_t lock = s.lock;
    memset(&s, 0, sizeof(s));
    s.lock = lock;
}

bool relay_ctrl_source_chirps(relay_source_t src)
{
    switch (src) {
    case RELAY_SRC_SK:
    case RELAY_SRC_N2K:
    case RELAY_SRC_WEB:
    case RELAY_SRC_INPUT: return true;
    case RELAY_SRC_PULSE_END:
    case RELAY_SRC_FAILSAFE:
    case RELAY_SRC_MAX_ON:
    case RELAY_SRC_INTERLOCK: return false;
    }
    return false;
}

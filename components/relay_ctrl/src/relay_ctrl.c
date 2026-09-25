#include "relay_ctrl.h"

#include <stdio.h>
#include <string.h>

#include "espos_health.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define HEALTH_KEY "relayExpander"
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

// Tell listeners about every relay that differs between `before` and s.mask.
// Called without the lock held.
static void notify(uint8_t before, uint8_t after, relay_source_t src)
{
    uint8_t changed = before ^ after;
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
    const uint8_t target = held & hold_mask();

    err = warm ? tca9554_write_outputs(&s.hw.expander, to_reg(target))
               : tca9554_init_outputs(&s.hw.expander, to_reg(target));
    if (err != ESP_OK) {
        report_fault(err);
        return err;
    }
    s.mask = target;
    // A warm boot may have held a newer state than the store had.
    s.save_pending = warm;
    return ESP_OK;
}

void relay_ctrl_update_config(const device_config_t *cfg)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const uint8_t old_hold = hold_mask();
    const uint32_t now = s.hw.now_ms();
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
    xSemaphoreGive(s.lock);
}

esp_err_t relay_ctrl_set(uint8_t channel, bool on, relay_source_t src)
{
    if (channel < 1 || channel > BOARD_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    const int i = channel - 1;
    const uint8_t bit = 1u << i;

    xSemaphoreTake(s.lock, portMAX_DELAY);
    const uint8_t before = s.mask;
    esp_err_t err = commit_locked(on ? (s.mask | bit) : (s.mask & ~bit));
    if (err == ESP_OK) {
        if (on && is_momentary(i)) {
            s.pulse_end[i] = s.hw.now_ms() + s.cfg.relays[i].pulse_ms;
            s.pulsing |= bit;
        } else if (!on) {
            s.pulsing &= ~bit;
        }
    }
    const uint8_t after = s.mask;
    xSemaphoreGive(s.lock);

    notify(before, after, src);
    return err;
}

bool relay_ctrl_get(uint8_t channel)
{
    return channel >= 1 && channel <= BOARD_CHANNELS && (relay_ctrl_get_mask() & (1u << (channel - 1)));
}

uint8_t relay_ctrl_get_mask(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    uint8_t m = s.mask;
    xSemaphoreGive(s.lock);
    return m;
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
    const uint8_t off = (uint8_t)~hold_mask();
    if (commit_locked(s.mask & ~off) == ESP_OK) {
        s.pulsing &= ~off;
    }
    const uint8_t after = s.mask;
    xSemaphoreGive(s.lock);
    notify(before, after, RELAY_SRC_FAILSAFE);
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

    if (s.save_pending && now - s.last_save_ms >= RELAY_CTRL_SAVE_DELAY_MS) {
        if (s.hw.store.save(s.hw.store.ctx, s.mask & hold_mask()) == ESP_OK) {
            s.save_pending = false;
        }
        s.last_save_ms = now;  // also on failure, so a bad flash isn't hammered
    }
    const uint8_t after = s.mask;
    xSemaphoreGive(s.lock);

    notify(before, after, RELAY_SRC_PULSE_END);
}

void relay_ctrl_reset(void)
{
    SemaphoreHandle_t lock = s.lock;
    memset(&s, 0, sizeof(s));
    s.lock = lock;
}

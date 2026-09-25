#include "input_sense.h"

#include <stdatomic.h>
#include <string.h>

#define MAX_LISTENERS 4

typedef struct {
    input_listener_t cb;
    void *arg;
} listener_t;

// poll() and update_config() run on one task; other tasks only read the
// atomics.
static struct {
    input_sense_hw_t hw;
    device_config_t cfg;
    input_override_fn_t override;
    bool candidate[BOARD_CHANNELS];
    uint32_t candidate_since[BOARD_CHANNELS];
    uint8_t stable;
    atomic_uchar published;  // `stable`, once ready
    atomic_bool ready;
    listener_t listeners[MAX_LISTENERS];
} s;

static bool read_input(uint8_t raw, int i)
{
    bool level = raw & (1u << i);
    bool energised = BOARD_DI_ACTIVE_LOW ? !level : level;
    return energised != s.cfg.inputs[i].invert;
}

static void notify(int i, bool on, uint8_t mask)
{
    for (int l = 0; l < MAX_LISTENERS; l++) {
        if (s.listeners[l].cb) {
            s.listeners[l].cb(i + 1, on, mask, s.listeners[l].arg);
        }
    }
}

// Drive every relay that names input i+1 as its override. A follow link
// copies the input. A toggle link flips the relay on each press and ignores
// the release; at boot the input's level says nothing about the relay, so
// toggle links are left alone then (a button held at power-up is no press).
static void apply_override(int i, bool on, bool boot)
{
    for (int r = 0; r < BOARD_CHANNELS; r++) {
        if (s.cfg.relays[r].override_di != i + 1) {
            continue;
        }
        if (s.cfg.relays[r].link == INPUT_LINK_TOGGLE) {
            if (on && !boot) {
                s.override(r + 1, INPUT_ACTION_TOGGLE);
            }
        } else {
            s.override(r + 1, on ? INPUT_ACTION_ON : INPUT_ACTION_OFF);
        }
    }
}

esp_err_t input_sense_init(const input_sense_hw_t *hw, const device_config_t *cfg, input_override_fn_t override)
{
    s.hw = *hw;
    s.cfg = *cfg;
    s.override = override;
    s.stable = 0;
    atomic_store(&s.published, 0);
    atomic_store(&s.ready, false);
    const uint8_t raw = s.hw.read_pins(s.hw.ctx);
    const uint32_t now = s.hw.now_ms();
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        s.candidate[i] = read_input(raw, i);
        s.candidate_since[i] = now;
    }
    return ESP_OK;
}

void input_sense_update_config(const device_config_t *cfg)
{
    // Flipping `invert` changes what an unchanged pin means, not the pin
    // itself. Take the new meaning at once and report it, but don't fire
    // overrides: saving a setting must never switch a relay.
    uint8_t flipped = 0;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        if (cfg->inputs[i].invert != s.cfg.inputs[i].invert) {
            s.candidate[i] = !s.candidate[i];
            flipped |= 1u << i;
        }
    }
    s.cfg = *cfg;
    if (!flipped || !atomic_load(&s.ready)) {
        return;
    }
    s.stable ^= flipped;
    atomic_store(&s.published, s.stable);
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        if (flipped & (1u << i)) {
            notify(i, s.stable & (1u << i), s.stable);
        }
    }
}

void input_sense_poll(void)
{
    const uint8_t raw = s.hw.read_pins(s.hw.ctx);
    const uint32_t now = s.hw.now_ms();
    bool all_settled = true;
    uint8_t settled_value = 0;

    for (int i = 0; i < BOARD_CHANNELS; i++) {
        const bool v = read_input(raw, i);
        if (v != s.candidate[i]) {
            s.candidate[i] = v;
            s.candidate_since[i] = now;
        }
        if (now - s.candidate_since[i] >= s.cfg.debounce_ms) {
            settled_value |= (uint8_t)(v << i);
        } else {
            all_settled = false;
        }
    }

    if (!atomic_load(&s.ready)) {
        if (!all_settled) {
            return;
        }
        // First stable readings: report every input, then apply every
        // override (after relay_ctrl's own boot state, as SPEC.md §2 wants).
        s.stable = settled_value;
        atomic_store(&s.published, s.stable);
        atomic_store(&s.ready, true);
        for (int i = 0; i < BOARD_CHANNELS; i++) {
            notify(i, s.stable & (1u << i), s.stable);
        }
        for (int i = 0; i < BOARD_CHANNELS; i++) {
            apply_override(i, s.stable & (1u << i), true);
        }
        return;
    }

    uint8_t changed = 0;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        const uint8_t bit = 1u << i;
        const bool settled = now - s.candidate_since[i] >= s.cfg.debounce_ms;
        if (settled && s.candidate[i] != (bool)(s.stable & bit)) {
            s.stable ^= bit;
            changed |= bit;
        }
    }
    if (!changed) {
        return;
    }
    atomic_store(&s.published, s.stable);
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        if (changed & (1u << i)) {
            notify(i, s.stable & (1u << i), s.stable);
            apply_override(i, s.stable & (1u << i), false);
        }
    }
}

bool input_sense_ready(void)
{
    return atomic_load(&s.ready);
}

uint8_t input_sense_get_mask(void)
{
    return atomic_load(&s.published);
}

esp_err_t input_sense_add_listener(input_listener_t cb, void *arg)
{
    for (int l = 0; l < MAX_LISTENERS; l++) {
        if (!s.listeners[l].cb) {
            s.listeners[l] = (listener_t){cb, arg};
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

void input_sense_reset(void)
{
    memset(&s, 0, sizeof(s));
}

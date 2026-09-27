#include "counters.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    bool seen;             // has counters_on_change() ever reported this channel?
    bool on;                // current known state
    uint32_t on_since_ms;   // valid while `on`: when the running period started
                             // (or was last folded into runtime_s)
    counters_channel_t saved;  // persisted snapshot (cycles, whole-second runtime)
} slot_t;

static struct {
    SemaphoreHandle_t lock;
    counters_hw_t hw;
    slot_t relays[BOARD_CHANNELS];
    slot_t inputs[BOARD_CHANNELS];
    bool dirty;  // something not yet reflected in the store
    uint32_t last_save_ms;
} s;

static slot_t *slot_of(counters_kind_t kind, uint8_t channel)
{
    if (channel < 1 || channel > BOARD_CHANNELS) {
        return NULL;
    }
    return (kind == COUNTERS_RELAY ? s.relays : s.inputs) + (channel - 1);
}

// Whole seconds elapsed since on_since_ms, wrap-safe. Advances on_since_ms
// by that many whole seconds (keeping the sub-second remainder) and folds
// them into the persisted snapshot.
static void fold_running_locked(slot_t *slot, uint32_t now_ms)
{
    if (!slot->on) {
        return;
    }
    const uint32_t elapsed_ms = now_ms - slot->on_since_ms;  // correct across uint32 wrap
    const uint32_t whole_s = elapsed_ms / 1000;
    if (whole_s == 0) {
        return;
    }
    slot->saved.runtime_s += whole_s;
    slot->on_since_ms += whole_s * 1000u;
    s.dirty = true;
}

static void snapshot_locked(counters_data_t *out)
{
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        out->relays[i] = s.relays[i].saved;
        out->inputs[i] = s.inputs[i].saved;
    }
}

esp_err_t counters_init(const counters_hw_t *hw)
{
    if (!s.lock) {
        s.lock = xSemaphoreCreateMutex();
        if (!s.lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    s.hw = *hw;
    memset(s.relays, 0, sizeof(s.relays));
    memset(s.inputs, 0, sizeof(s.inputs));
    s.dirty = false;
    s.last_save_ms = hw->now_ms ? hw->now_ms() : 0;

    counters_data_t loaded;
    if (s.hw.store.load && s.hw.store.load(s.hw.store.ctx, &loaded) == ESP_OK) {
        for (int i = 0; i < BOARD_CHANNELS; i++) {
            s.relays[i].saved = loaded.relays[i];
            s.inputs[i].saved = loaded.inputs[i];
        }
    }
    return ESP_OK;
}

void counters_on_change(counters_kind_t kind, uint8_t channel, bool on, uint32_t now_ms)
{
    slot_t *slot = slot_of(kind, channel);
    if (!slot || !s.lock) {
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (!slot->seen) {
        // The first report is the channel's starting state (typically boot),
        // not a transition: nothing to count, but a channel already on
        // starts accruing runtime from here.
        slot->seen = true;
        slot->on = on;
        slot->on_since_ms = now_ms;
    } else if (on != slot->on) {
        if (on) {
            slot->saved.cycles++;
            slot->on = true;
            slot->on_since_ms = now_ms;
        } else {
            fold_running_locked(slot, now_ms);
            slot->on = false;
        }
        s.dirty = true;
    }
    xSemaphoreGive(s.lock);
}

void counters_get(counters_kind_t kind, uint8_t channel, uint32_t now_ms, uint32_t *cycles, uint32_t *runtime_s)
{
    slot_t *slot = slot_of(kind, channel);
    if (!slot) {
        if (cycles) {
            *cycles = 0;
        }
        if (runtime_s) {
            *runtime_s = 0;
        }
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const uint32_t live_extra = slot->on ? (now_ms - slot->on_since_ms) / 1000 : 0;
    if (cycles) {
        *cycles = slot->saved.cycles;
    }
    if (runtime_s) {
        *runtime_s = slot->saved.runtime_s + live_extra;
    }
    xSemaphoreGive(s.lock);
}

void counters_reset(counters_kind_t kind, uint8_t channel, uint32_t now_ms)
{
    slot_t *slot = slot_of(kind, channel);
    if (!slot || !s.lock) {
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    slot->saved.cycles = 0;
    slot->saved.runtime_s = 0;
    if (slot->on) {
        slot->on_since_ms = now_ms;  // keep counting on-time from now
    }
    s.dirty = true;
    counters_data_t data;
    snapshot_locked(&data);
    xSemaphoreGive(s.lock);

    // A deliberate, infrequent user action: flush at once rather than wait
    // for the periodic throttle, so a reset isn't undone by a power loss.
    if (s.hw.store.save && s.hw.store.save(s.hw.store.ctx, &data) == ESP_OK) {
        xSemaphoreTake(s.lock, portMAX_DELAY);
        s.dirty = false;
        s.last_save_ms = now_ms;
        xSemaphoreGive(s.lock);
    }
}

void counters_tick(uint32_t now_ms)
{
    if (!s.lock || now_ms - s.last_save_ms < COUNTERS_SAVE_INTERVAL_MS) {
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        fold_running_locked(&s.relays[i], now_ms);
        fold_running_locked(&s.inputs[i], now_ms);
    }
    bool save_now = s.dirty;
    counters_data_t data;
    if (save_now) {
        snapshot_locked(&data);
    }
    xSemaphoreGive(s.lock);

    if (save_now && s.hw.store.save && s.hw.store.save(s.hw.store.ctx, &data) == ESP_OK) {
        xSemaphoreTake(s.lock, portMAX_DELAY);
        s.dirty = false;
        xSemaphoreGive(s.lock);
    }
    s.last_save_ms = now_ms;  // also when nothing changed / on failure, so a
                                // bad flash (or a quiet board) isn't hammered
}

void counters_flush_now(uint32_t now_ms)
{
    if (!s.lock) {
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        fold_running_locked(&s.relays[i], now_ms);
        fold_running_locked(&s.inputs[i], now_ms);
    }
    counters_data_t data;
    snapshot_locked(&data);
    xSemaphoreGive(s.lock);

    if (s.hw.store.save && s.hw.store.save(s.hw.store.ctx, &data) == ESP_OK) {
        xSemaphoreTake(s.lock, portMAX_DELAY);
        s.dirty = false;
        s.last_save_ms = now_ms;
        xSemaphoreGive(s.lock);
    }
}

void counters_reset_all(void)
{
    SemaphoreHandle_t lock = s.lock;
    memset(&s, 0, sizeof(s));
    s.lock = lock;
}

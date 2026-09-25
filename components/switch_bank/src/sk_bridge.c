#include "sk_bridge.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define PATH_MAX_LEN 96  // espOS's ESPOS_SK_PATH_MAX
#define MANUFACTURER "Waveshare"
#define MODEL "ESP32-S3-ETH-8DI-8RO-C"

typedef enum { TREE_SWITCHES, TREE_CONTROLS } tree_t;

static struct {
    SemaphoreHandle_t lock;
    sk_api_t api;
    sk_bridge_io_t io;
    device_config_t cfg;
    bool started;
    bool connected_once;
    bool down;
    bool lost_fired;
    uint32_t down_since;
} s;

// ------------------------------------------------------------------ paths

static bool tree_on(tree_t t)
{
    return t == TREE_SWITCHES ? s.cfg.publish_switches_tree : s.cfg.publish_controls_tree;
}

static bool inputs_on(void)
{
    return device_config_input_bank_usable(&s.cfg);
}

// "<base>.<leaf>" for channel `ch` of the relay or input bank.
static void make_path(char *buf, tree_t t, bool input, uint8_t ch, const char *leaf)
{
    const unsigned bank = input ? s.cfg.input_bank_id : s.cfg.bank_id;
    if (t == TREE_SWITCHES) {
        snprintf(buf, PATH_MAX_LEN, "electrical.switches.bank.%u.%u.%s", bank, ch, leaf);
    } else {
        snprintf(buf, PATH_MAX_LEN, "electrical.controls.espOS-instance%u-%s%u.%s", bank, input ? "input" : "relay",
                 ch, leaf);
    }
}

static const char *name_of(bool input, uint8_t ch)
{
    return input ? s.cfg.inputs[ch - 1].name : s.cfg.relays[ch - 1].name;
}

// ------------------------------------------------------------- publishing

static void json_escape(char *out, size_t size, const char *in)
{
    size_t n = 0;
    for (; *in && n + 7 < size; in++) {
        const unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') {
            out[n++] = '\\';
            out[n++] = (char)c;
        } else if (c < 0x20) {
            n += snprintf(out + n, size - n, "\\u%04x", c);
        } else {
            out[n++] = (char)c;
        }
    }
    out[n] = '\0';
}

static void publish_state(tree_t t, bool input, uint8_t ch, bool on)
{
    char path[PATH_MAX_LEN];
    make_path(path, t, input, ch, "state");
    // n2k-signalk publishes PGN 127501 channels as 1/0; so do we, so apps
    // see one format whichever way the data arrives.
    s.api.publish_number(path, on ? 1 : 0);
}

static void declare_names(tree_t t, bool input, uint8_t ch)
{
    char path[PATH_MAX_LEN];
    char name[2 * DEVICE_CONFIG_NAME_MAX + 8];
    char meta[160];
    json_escape(name, sizeof(name), name_of(input, ch));
    snprintf(meta, sizeof(meta),
             "{\"displayName\":\"%s\",\"manufacturer\":{\"name\":\"" MANUFACTURER "\",\"model\":\"" MODEL "\"}}", name);
    make_path(path, t, input, ch, "state");
    s.api.declare_meta(path, meta, 0);
    if (t == TREE_CONTROLS) {
        make_path(path, t, input, ch, "name");
        s.api.publish_string(path, name_of(input, ch));
    }
}

// Everything but the state: fixed values that describe the channel.
static void publish_description(tree_t t, bool input, uint8_t ch)
{
    char path[PATH_MAX_LEN];
    if (t == TREE_SWITCHES) {
        make_path(path, t, input, ch, "order");  // as n2k-signalk does
        s.api.publish_number(path, ch);
        return;
    }
    make_path(path, t, input, ch, "type");
    s.api.publish_string(path, "switch");
    make_path(path, t, input, ch, "manufacturer.name");
    s.api.publish_string(path, MANUFACTURER);
    make_path(path, t, input, ch, "manufacturer.model");
    s.api.publish_string(path, MODEL);
}

static void publish_all_locked(void)
{
    const uint8_t relays = s.io.relay_mask();
    const bool inputs_ready = s.io.inputs_ready();
    const uint8_t inputs = s.io.input_mask();
    for (tree_t t = TREE_SWITCHES; t <= TREE_CONTROLS; t++) {
        if (!tree_on(t)) {
            continue;
        }
        for (uint8_t ch = 1; ch <= BOARD_CHANNELS; ch++) {
            publish_description(t, false, ch);
            publish_state(t, false, ch, relays & (1u << (ch - 1)));
            if (inputs_on() && inputs_ready) {
                publish_description(t, true, ch);
                publish_state(t, true, ch, inputs & (1u << (ch - 1)));
            }
        }
    }
}

// -------------------------------------------------------------------- PUT

// 1 = on, 0 = off, -1 = not a switch value. Accepts true/false and 1/0.
static int parse_on(const char *v)
{
    while (isspace((unsigned char)*v)) {
        v++;
    }
    if (strncmp(v, "true", 4) == 0 || strncmp(v, "false", 5) == 0) {
        const bool on = v[0] == 't';
        v += on ? 4 : 5;
        while (isspace((unsigned char)*v)) {
            v++;
        }
        return *v ? -1 : on;
    }
    char *end;
    const double d = strtod(v, &end);
    if (end == v) {
        return -1;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end) {
        return -1;
    }
    return d == 1 ? 1 : d == 0 ? 0 : -1;
}

// Runs on espOS's stream task. The lock isn't held: set_relay() reports
// back through sk_bridge_relay_changed(), which takes it.
static esp_err_t on_put(const char *path, const char *value_json, void *arg)
{
    const uint8_t ch = (uint8_t)(uintptr_t)arg;
    const int on = parse_on(value_json);
    if (on < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return s.io.set_relay(ch, on) == ESP_OK ? ESP_OK : ESP_FAIL;
}

// ---------------------------------------------------------------- public

esp_err_t sk_bridge_start(const sk_api_t *api, const sk_bridge_io_t *io, const device_config_t *cfg)
{
    if (!s.lock) {
        s.lock = xSemaphoreCreateMutex();
        if (!s.lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.api = *api;
    s.io = *io;
    s.cfg = *cfg;
    esp_err_t err = ESP_OK;
    for (tree_t t = TREE_SWITCHES; t <= TREE_CONTROLS; t++) {
        if (!tree_on(t)) {
            continue;
        }
        for (uint8_t ch = 1; ch <= BOARD_CHANNELS; ch++) {
            declare_names(t, false, ch);
            if (inputs_on()) {
                declare_names(t, true, ch);
            }
        }
    }
    // espOS accepts PUTs only on paths already published.
    publish_all_locked();
    for (tree_t t = TREE_SWITCHES; t <= TREE_CONTROLS && err == ESP_OK; t++) {
        for (uint8_t ch = 1; tree_on(t) && ch <= BOARD_CHANNELS && err == ESP_OK; ch++) {
            char path[PATH_MAX_LEN];
            make_path(path, t, false, ch, "state");
            err = s.api.put_register(path, on_put, (void *)(uintptr_t)ch);
        }
    }
    s.started = true;
    xSemaphoreGive(s.lock);
    return err;
}

void sk_bridge_update_config(const device_config_t *cfg)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    // Names and the grace period apply live. Bank ids and tree toggles need
    // a restart, so the running copy keeps its own.
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        const bool relay_renamed = strcmp(s.cfg.relays[i].name, cfg->relays[i].name) != 0;
        const bool input_renamed = strcmp(s.cfg.inputs[i].name, cfg->inputs[i].name) != 0;
        memcpy(s.cfg.relays[i].name, cfg->relays[i].name, sizeof(s.cfg.relays[i].name));
        memcpy(s.cfg.inputs[i].name, cfg->inputs[i].name, sizeof(s.cfg.inputs[i].name));
        for (tree_t t = TREE_SWITCHES; t <= TREE_CONTROLS; t++) {
            if (!tree_on(t)) {
                continue;
            }
            if (relay_renamed) {
                declare_names(t, false, i + 1);
            }
            if (input_renamed && inputs_on()) {
                declare_names(t, true, i + 1);
            }
        }
    }
    s.cfg.sk_loss_grace_s = cfg->sk_loss_grace_s;
    xSemaphoreGive(s.lock);
}

void sk_bridge_relay_changed(uint8_t channel, bool on)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    for (tree_t t = TREE_SWITCHES; s.started && t <= TREE_CONTROLS; t++) {
        if (tree_on(t)) {
            publish_state(t, false, channel, on);
        }
    }
    xSemaphoreGive(s.lock);
}

void sk_bridge_input_changed(uint8_t channel, bool on)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    for (tree_t t = TREE_SWITCHES; s.started && inputs_on() && t <= TREE_CONTROLS; t++) {
        if (tree_on(t)) {
            publish_description(t, true, channel);
            publish_state(t, true, channel, on);
        }
    }
    xSemaphoreGive(s.lock);
}

void sk_bridge_stream_changed(bool connected)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (connected) {
        s.connected_once = true;
        s.down = false;
        if (s.started) {
            publish_all_locked();
        }
    } else if (s.connected_once && !s.down) {
        s.down = true;
        s.lost_fired = false;
        s.down_since = s.io.now_ms();
    }
    xSemaphoreGive(s.lock);
}

void sk_bridge_tick(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool fire = s.down && !s.lost_fired && (s.cfg.publish_switches_tree || s.cfg.publish_controls_tree) &&
                s.io.now_ms() - s.down_since >= (uint32_t)s.cfg.sk_loss_grace_s * 1000;
    if (fire) {
        s.lost_fired = true;
    }
    xSemaphoreGive(s.lock);
    if (fire) {
        s.io.sk_lost();  // outside the lock: relay changes call back in
    }
}

void sk_bridge_reset(void)
{
    SemaphoreHandle_t lock = s.lock;
    memset(&s, 0, sizeof(s));
    s.lock = lock;
}

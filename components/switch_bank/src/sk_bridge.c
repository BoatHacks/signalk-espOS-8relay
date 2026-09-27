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
    uint32_t republished_at;
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

// "notifications.<base path>" for an input's alarm (plan 10, issue #3). A
// few bytes longer than PATH_MAX_LEN for the "notifications." prefix.
#define NOTIF_PATH_MAX_LEN (PATH_MAX_LEN + 16)

static void make_notif_path(char *buf, size_t size, tree_t t, uint8_t ch, const char *leaf)
{
    char base[PATH_MAX_LEN];
    make_path(base, t, true, ch, leaf);
    snprintf(buf, size, "notifications.%s", base);
}

// Whether input `ch` has an alarm configured at all (its own setting isn't
// "off", and the input bank is actually published).
static bool alarm_configured(uint8_t ch)
{
    return inputs_on() && s.cfg.inputs[ch - 1].alarm != INPUT_ALARM_OFF;
}

static const char *alarm_state_str(input_alarm_t a)
{
    switch (a) {
    case INPUT_ALARM_WARN:
        return "warn";
    case INPUT_ALARM_ALARM:
        return "alarm";
    case INPUT_ALARM_EMERGENCY:
        return "emergency";
    default:
        return "normal";
    }
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

// The alarm's SignalK Notification object (SPEC.md §6, plan 10, issue #3):
// active raises it at the input's own severity with a message; inactive
// clears it back to "normal".
static void publish_notification(tree_t t, uint8_t ch, bool active)
{
    char path[NOTIF_PATH_MAX_LEN];
    make_notif_path(path, sizeof(path), t, ch, "state");
    char json[2 * DEVICE_CONFIG_NAME_MAX + 96];
    if (active) {
        const input_cfg_t *in = &s.cfg.inputs[ch - 1];
        char raw[DEVICE_CONFIG_NAME_MAX + 16];
        if (in->alarm_msg[0]) {
            snprintf(raw, sizeof(raw), "%s", in->alarm_msg);
        } else {
            snprintf(raw, sizeof(raw), "%s active", in->name);
        }
        char msg[2 * DEVICE_CONFIG_NAME_MAX + 16];
        json_escape(msg, sizeof(msg), raw);
        snprintf(json, sizeof(json), "{\"state\":\"%s\",\"method\":[\"visual\",\"sound\"],\"message\":\"%s\"}",
                 alarm_state_str(in->alarm), msg);
    } else {
        snprintf(json, sizeof(json), "{\"state\":\"normal\",\"method\":[],\"message\":\"\"}");
    }
    s.api.publish_json(path, json);
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
    // With a republish interval the server can tell a quiet board from a
    // gone one: espOS turns the period into the path's "timeout".
    s.api.declare_meta(path, meta, (uint32_t)s.cfg.sk_republish_s * 1000);
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

// Every channel's state and nothing else: the periodic republish.
static void publish_states_locked(void)
{
    const uint8_t relays = s.io.relay_mask();
    const bool inputs_ready = s.io.inputs_ready();
    const uint8_t inputs = s.io.input_mask();
    for (tree_t t = TREE_SWITCHES; t <= TREE_CONTROLS; t++) {
        for (uint8_t ch = 1; tree_on(t) && ch <= BOARD_CHANNELS; ch++) {
            publish_state(t, false, ch, relays & (1u << (ch - 1)));
            if (inputs_on() && inputs_ready) {
                const bool on = inputs & (1u << (ch - 1));
                publish_state(t, true, ch, on);
                if (on && alarm_configured(ch)) {
                    publish_notification(t, ch, true);
                }
            }
        }
    }
    s.republished_at = s.io.now_ms();
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
                const bool on = inputs & (1u << (ch - 1));
                publish_state(t, true, ch, on);
                if (on && alarm_configured(ch)) {
                    publish_notification(t, ch, true);
                }
            }
        }
    }
    s.republished_at = s.io.now_ms();
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

esp_err_t sk_bridge_init(void)
{
    if (!s.lock) {
        s.lock = xSemaphoreCreateMutex();
        if (!s.lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t sk_bridge_start(const sk_api_t *api, const sk_bridge_io_t *io, const device_config_t *cfg)
{
    esp_err_t init = sk_bridge_init();
    if (init != ESP_OK) {
        return init;
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
    if (!s.lock) {
        return;  // before sk_bridge_init(): nothing to publish yet
    }
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
        // Alarm settings apply live too. If the input is currently on, its
        // notification (raised, cleared, or never raised) may need to
        // change under the new setting right away -- otherwise a float
        // switch that's already tripped would keep its old alarm (or lack
        // of one) until the next physical transition, which could be a long
        // time coming for a bilge that's slowly refilling.
        const bool old_configured = inputs_on() && s.cfg.inputs[i].alarm != INPUT_ALARM_OFF;
        const bool alarm_setting_changed = s.cfg.inputs[i].alarm != cfg->inputs[i].alarm ||
                                            strcmp(s.cfg.inputs[i].alarm_msg, cfg->inputs[i].alarm_msg) != 0;
        s.cfg.inputs[i].alarm = cfg->inputs[i].alarm;
        memcpy(s.cfg.inputs[i].alarm_msg, cfg->inputs[i].alarm_msg, sizeof(s.cfg.inputs[i].alarm_msg));
        const bool new_configured = alarm_configured(i + 1);
        if (alarm_setting_changed && (old_configured || new_configured) && s.started && inputs_on() &&
            s.io.inputs_ready() && ((s.io.input_mask() >> i) & 1)) {
            for (tree_t t = TREE_SWITCHES; t <= TREE_CONTROLS; t++) {
                if (tree_on(t)) {
                    publish_notification(t, i + 1, new_configured);
                }
            }
        }
    }
    s.cfg.sk_loss_grace_s = cfg->sk_loss_grace_s;
    if (s.cfg.sk_republish_s != cfg->sk_republish_s) {
        s.cfg.sk_republish_s = cfg->sk_republish_s;
        // The metadata timeout follows the interval.
        for (tree_t t = TREE_SWITCHES; s.started && t <= TREE_CONTROLS; t++) {
            for (uint8_t ch = 1; tree_on(t) && ch <= BOARD_CHANNELS; ch++) {
                declare_names(t, false, ch);
                if (inputs_on()) {
                    declare_names(t, true, ch);
                }
            }
        }
    }
    xSemaphoreGive(s.lock);
}

void sk_bridge_relay_changed(uint8_t channel, bool on)
{
    if (!s.lock) {
        return;  // before sk_bridge_init(): nothing to publish yet
    }
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
    if (!s.lock) {
        return;  // before sk_bridge_init(): nothing to publish yet
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    for (tree_t t = TREE_SWITCHES; s.started && inputs_on() && t <= TREE_CONTROLS; t++) {
        if (tree_on(t)) {
            publish_description(t, true, channel);
            publish_state(t, true, channel, on);
        }
    }
    // Raise on -> on (after debounce and invert, since this only runs once
    // the input has settled -- see input_sense's notify()); clear on -> off.
    // Nothing at all while the alarm setting is "off".
    if (s.started && alarm_configured(channel)) {
        for (tree_t t = TREE_SWITCHES; t <= TREE_CONTROLS; t++) {
            if (tree_on(t)) {
                publish_notification(t, channel, on);
            }
        }
    }
    xSemaphoreGive(s.lock);
}

void sk_bridge_stream_changed(bool connected)
{
    if (!s.lock) {
        return;  // before sk_bridge_init(): nothing to publish yet
    }
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
    if (!s.lock) {
        return;  // before sk_bridge_init(): nothing to publish yet
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool fire = s.down && !s.lost_fired && (s.cfg.publish_switches_tree || s.cfg.publish_controls_tree) &&
                s.io.now_ms() - s.down_since >= (uint32_t)s.cfg.sk_loss_grace_s * 1000;
    if (fire) {
        s.lost_fired = true;
    }
    // Only while the stream is up: espOS would otherwise buffer repeats of
    // unchanged values for a server that isn't there.
    const uint32_t every_ms = (uint32_t)s.cfg.sk_republish_s * 1000;
    if (s.started && s.connected_once && !s.down && every_ms &&
        s.io.now_ms() - s.republished_at >= every_ms) {
        publish_states_locked();
    }
    xSemaphoreGive(s.lock);
    if (fire) {
        s.io.sk_lost();  // outside the lock: relay changes call back in
    }
}

void sk_bridge_reset(void)
{
    // Back to before sk_bridge_init(), so each test also covers boot order.
    if (s.lock) {
        vSemaphoreDelete(s.lock);
    }
    memset(&s, 0, sizeof(s));
}

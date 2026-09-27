#include "device_config.h"

#include <stdio.h>
#include <string.h>

#include "espos_config.h"
#include "espos_health.h"

#define HEALTH_KEY "bankIdClash"

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
    out->led_brightness = (uint8_t)get_int("led_brightness");
    out->buzzer_on_alarm = get_bool("buzzer_alarm");
    out->buzzer_freq_hz = (uint16_t)get_int("buzzer_freq_hz");
    out->buzzer_on_event = get_bool("buzzer_event");
    get_str("tone_patterns", out->tone_patterns, sizeof(out->tone_patterns));
    get_str("boot_tone", out->boot_tone, sizeof(out->boot_tone));
    get_str("portal_tone", out->portal_tone, sizeof(out->portal_tone));
    get_str("reset_tone", out->factory_reset_tone, sizeof(out->factory_reset_tone));

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
        return;
    }
    char msg[ESPOS_HEALTH_MSG_MAX];
    snprintf(msg, sizeof(msg), "Input bank id %u equals relay bank id; inputs not published until changed",
             cfg->input_bank_id);
    espos_health_report(HEALTH_KEY, ESPOS_HEALTH_WARN, msg);
}

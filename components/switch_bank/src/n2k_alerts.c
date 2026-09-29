#include "n2k_alerts.h"

#include <stdio.h>
#include <string.h>

void n2k_alerts_cfg_from(const device_config_t *cfg, n2k_alerts_cfg_t *out)
{
    memset(out, 0, sizeof(*out));
    out->enabled = cfg->n2k_alerts;
    out->inputs_on = device_config_input_bank_usable(cfg);
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        const input_cfg_t *in = &cfg->inputs[i];
        out->level[i] = in->alarm;
        // Same message as the SignalK notification (sk_bridge.c).
        if (in->alarm_msg[0]) {
            snprintf(out->message[i], sizeof(out->message[i]), "%s", in->alarm_msg);
        } else {
            snprintf(out->message[i], sizeof(out->message[i]), "%s active", in->name);
        }
    }
}

uint8_t n2k_alerts_type_of(input_alarm_t level)
{
    switch (level) {
    case INPUT_ALARM_EMERGENCY:
        return N2K_ALERT_TYPE_EMERGENCY;
    case INPUT_ALARM_ALARM:
        return N2K_ALERT_TYPE_ALARM;
    default:
        return N2K_ALERT_TYPE_WARNING;
    }
}

static void id_of(const n2k_alerts_t *a, uint8_t ch, input_alarm_t level, uint8_t occurrence, n2k_alert_id_t *id)
{
    memset(id, 0, sizeof(*id));
    id->type = n2k_alerts_type_of(level);
    id->category = N2K_ALERT_CATEGORY_TECHNICAL;
    id->id = ch;  // unique per data source: one alert per input
    id->source_name = a->source_name;
    id->instance = a->instance;
    id->index = ch;
    id->occurrence = occurrence;
}

void n2k_alerts_status_of(const n2k_alerts_t *a, uint8_t ch, bool clearing, n2k_alert_t *out)
{
    const n2k_alerts_chan_t *c = &a->ch[ch - 1];
    memset(out, 0, sizeof(*out));
    id_of(a, ch, clearing ? c->clear_level : c->level, clearing ? c->clear_occurrence : c->occurrence, &out->id);
    out->ack_supported = true;
    out->trigger = N2K_ALERT_TRIGGER_AUTO;
    if (clearing) {
        out->ack_name = N2K_ALERT_NAME_NONE;
        out->threshold = N2K_ALERT_THRESHOLD_NORMAL;
        out->state = N2K_ALERT_STATE_NORMAL;
    } else {
        out->acknowledged = c->acked;
        out->ack_name = c->acked ? c->ack_name : N2K_ALERT_NAME_NONE;
        out->threshold = N2K_ALERT_THRESHOLD_EXCEEDED;
        out->state = c->acked ? N2K_ALERT_STATE_ACKNOWLEDGED : N2K_ALERT_STATE_ACTIVE;
    }
}

void n2k_alerts_init(n2k_alerts_t *a, const n2k_alerts_cfg_t *cfg, uint8_t instance, uint64_t source_name,
                     uint32_t now_ms)
{
    memset(a, 0, sizeof(*a));
    a->cfg = *cfg;
    a->instance = instance;
    a->source_name = source_name;
    a->status_at = now_ms;
    a->text_at = now_ms;
}

void n2k_alerts_set_config(n2k_alerts_t *a, const n2k_alerts_cfg_t *cfg)
{
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        n2k_alerts_chan_t *c = &a->ch[i];
        if (c->active && strcmp(a->cfg.message[i], cfg->message[i]) != 0) {
            c->text_due = true;
        }
        // A severity change (or the alarm turned off) is handled by the next
        // tick: it compares each active alert's level with the setting.
    }
    a->cfg = *cfg;
}

static void begin_clear(n2k_alerts_chan_t *c)
{
    c->clear_level = c->level;
    c->clear_occurrence = c->occurrence;
    c->clear_left = N2K_ALERTS_CLEAR_REPEATS;
    c->active = false;
    c->acked = false;
    c->text_due = false;
    c->status_due = true;
}

static void raise_alert(n2k_alerts_chan_t *c, input_alarm_t level)
{
    c->active = true;
    c->level = level;
    c->occurrence = c->occurrence == 255 ? 1 : c->occurrence + 1;
    c->acked = false;
    c->ack_name = N2K_ALERT_NAME_NONE;
    c->text_due = true;
    c->status_due = true;
}

void n2k_alerts_tick(n2k_alerts_t *a, bool inputs_ready, uint8_t input_mask, uint32_t now_ms,
                     const n2k_alerts_out_t *out)
{
    const bool on = a->cfg.enabled && a->cfg.inputs_on && inputs_ready;
    const bool status_period = now_ms - a->status_at >= N2K_ALERTS_STATUS_PERIOD_MS;
    const bool text_period = now_ms - a->text_at >= N2K_ALERTS_TEXT_PERIOD_MS;
    if (status_period) {
        a->status_at = now_ms;
    }
    if (text_period) {
        a->text_at = now_ms;
    }
    for (uint8_t ch = 1; ch <= BOARD_CHANNELS; ch++) {
        n2k_alerts_chan_t *c = &a->ch[ch - 1];
        const input_alarm_t level = a->cfg.level[ch - 1];
        const bool want = on && level != INPUT_ALARM_OFF && ((input_mask >> (ch - 1)) & 1);
        if (c->active && (!want || c->level != level)) {
            begin_clear(c);
        }
        if (!c->active && want) {
            raise_alert(c, level);
        }
        if (status_period && (c->active || c->clear_left)) {
            c->status_due = true;
        }
        if (text_period && c->active) {
            c->text_due = true;
        }

        if (c->text_due) {
            n2k_alert_id_t id;
            id_of(a, ch, c->level, c->occurrence, &id);
            uint8_t buf[N2K_ALERT_TEXT_MAX_LEN];
            const size_t n = n2k_alert_encode_text(&id, a->cfg.message[ch - 1], "", buf, sizeof(buf));
            out->send_text(buf, n, out->arg);
            c->text_due = false;
        }
        if (c->status_due) {
            n2k_alert_t st;
            uint8_t buf[N2K_ALERT_LEN];
            if (c->clear_left) {
                n2k_alerts_status_of(a, ch, true, &st);
                n2k_alert_encode(&st, buf);
                out->send_status(buf, sizeof(buf), out->arg);
                c->clear_left--;
            }
            if (c->active) {
                n2k_alerts_status_of(a, ch, false, &st);
                n2k_alert_encode(&st, buf);
                out->send_status(buf, sizeof(buf), out->arg);
            }
            c->status_due = false;
        }
    }
}

bool n2k_alerts_on_response(n2k_alerts_t *a, const uint8_t *data, size_t len)
{
    n2k_alert_response_t r;
    if (!n2k_alert_decode_response(data, len, &r)) {
        return false;
    }
    // Ours: our NAME and instance, an input's id. Type and occurrence must
    // match the alert currently raised, so a late ack of an earlier one
    // doesn't acknowledge a new trip.
    if (r.id.source_name != a->source_name || r.id.instance != a->instance || r.id.id < 1 ||
        r.id.id > BOARD_CHANNELS) {
        return false;
    }
    n2k_alerts_chan_t *c = &a->ch[r.id.id - 1];
    if (!c->active || r.id.type != n2k_alerts_type_of(c->level) || r.id.occurrence != c->occurrence) {
        return false;
    }
    if (r.command != N2K_ALERT_RESPONSE_ACKNOWLEDGE || c->acked) {
        return false;  // temporary silence isn't offered; test commands ignored
    }
    c->acked = true;
    c->ack_name = r.ack_name;
    c->status_due = true;
    return true;
}

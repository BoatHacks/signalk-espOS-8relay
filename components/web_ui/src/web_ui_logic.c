#include "web_ui_logic.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

char *web_ui_state_json(const web_ui_view_t *view)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *relays = cJSON_AddArrayToObject(root, "relays");
    cJSON *inputs = cJSON_AddArrayToObject(root, "inputs");
    if (!relays || !inputs) {
        cJSON_Delete(root);
        return NULL;
    }
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        const relay_cfg_t *r = &view->cfg->relays[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddItemToArray(relays, o);
        cJSON_AddNumberToObject(o, "channel", i + 1);
        cJSON_AddStringToObject(o, "name", r->name);
        cJSON_AddBoolToObject(o, "on", (view->relay_mask >> i) & 1);
        cJSON_AddBoolToObject(o, "momentary", r->mode == RELAY_MODE_MOMENTARY);
        cJSON_AddNumberToObject(o, "input", r->override_di);
        cJSON_AddBoolToObject(o, "inputToggle", r->link == INPUT_LINK_TOGGLE);
        cJSON_AddNumberToObject(o, "maxOnS", r->mode == RELAY_MODE_MOMENTARY ? 0 : r->max_on_s);
        if (view->last_source[i]) {
            cJSON_AddStringToObject(o, "lastSource", view->last_source[i]);
            cJSON_AddNumberToObject(o, "lastChangeAgoS", view->last_change_ago_s[i]);
        } else {
            cJSON_AddNullToObject(o, "lastSource");
            cJSON_AddNullToObject(o, "lastChangeAgoS");
        }

        cJSON *in = cJSON_CreateObject();
        cJSON_AddItemToArray(inputs, in);
        cJSON_AddNumberToObject(in, "channel", i + 1);
        cJSON_AddStringToObject(in, "name", view->cfg->inputs[i].name);
        if (view->inputs_ready) {
            cJSON_AddBoolToObject(in, "on", (view->input_mask >> i) & 1);
        } else {
            cJSON_AddNullToObject(in, "on");
        }
    }
    cJSON_AddBoolToObject(root, "inputsReady", view->inputs_ready);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

uint8_t web_ui_parse_channel(const char *uri, const char *prefix)
{
    const size_t n = strlen(prefix);
    if (strncmp(uri, prefix, n) != 0 || uri[n] != '/') {
        return 0;
    }
    const char *p = uri + n + 1;
    if (p[0] < '1' || p[0] > '0' + BOARD_CHANNELS || (p[1] != '\0' && p[1] != '?')) {
        return 0;
    }
    return (uint8_t)(p[0] - '0');
}

bool web_ui_parse_on(const char *body, bool *on)
{
    cJSON *root = cJSON_Parse(body);
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "on");
    const bool ok = cJSON_IsBool(v);
    if (ok) {
        *on = cJSON_IsTrue(v);
    }
    cJSON_Delete(root);
    return ok;
}

static void add_str_or_null(cJSON *o, const char *key, const char *v)
{
    if (v[0]) {
        cJSON_AddStringToObject(o, key, v);
    } else {
        cJSON_AddNullToObject(o, key);
    }
}

char *web_ui_status_json(const web_ui_status_t *st)
{
    cJSON *root = cJSON_CreateObject();
    add_str_or_null(root, "version", st->version);
    add_str_or_null(root, "hostname", st->hostname);
    cJSON *net = cJSON_AddObjectToObject(root, "network");
    cJSON *sk = cJSON_AddObjectToObject(root, "signalk");
    cJSON *n2k = cJSON_AddObjectToObject(root, "nmea2000");
    if (!net || !sk || !n2k) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddBoolToObject(net, "up", st->net_up);
    add_str_or_null(net, "interface", st->net_up ? st->net_iface : "");
    add_str_or_null(net, "ip", st->net_up ? st->ip : "");
    cJSON_AddBoolToObject(sk, "enabled", st->sk_enabled);
    cJSON_AddBoolToObject(sk, "connected", st->sk_connected);
    add_str_or_null(sk, "server", st->sk_server);
    cJSON_AddBoolToObject(n2k, "started", st->n2k_started);
    if (st->n2k_started) {
        cJSON_AddNumberToObject(n2k, "address", st->n2k_address);
    } else {
        cJSON_AddNullToObject(n2k, "address");
    }
    cJSON_AddBoolToObject(n2k, "traffic", st->n2k_started && st->n2k_traffic);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

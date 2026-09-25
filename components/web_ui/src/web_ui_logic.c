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

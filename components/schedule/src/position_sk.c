#include "position_sk.h"

#include <string.h>

#include "cJSON.h"

static struct {
    position_sk_hw_t hw;
    bool inited;
    bool has_fix;
    double lat, lon;
    uint32_t received_ms;
} s;

void position_sk_init(const position_sk_hw_t *hw)
{
    s.hw = *hw;
    s.inited = true;
    s.has_fix = false;
}

void position_sk_on_update(const espos_sk_update_t *u, void *arg)
{
    (void)arg;
    if (!s.inited || !u || !u->value_json) {
        return;
    }
    cJSON *root = cJSON_Parse(u->value_json);
    if (!root) {
        return;
    }
    const cJSON *lat_j = cJSON_GetObjectItemCaseSensitive(root, "latitude");
    const cJSON *lon_j = cJSON_GetObjectItemCaseSensitive(root, "longitude");
    if (cJSON_IsNumber(lat_j) && cJSON_IsNumber(lon_j)) {
        s.lat = lat_j->valuedouble;
        s.lon = lon_j->valuedouble;
        s.has_fix = true;
        s.received_ms = s.hw.now_ms();
    }
    cJSON_Delete(root);
}

bool position_sk_get(double *lat, double *lon, uint32_t *received_ms)
{
    if (!s.inited || !s.has_fix) {
        return false;
    }
    *lat = s.lat;
    *lon = s.lon;
    *received_ms = s.received_ms;
    return true;
}

void position_sk_reset(void)
{
    memset(&s, 0, sizeof(s));
}

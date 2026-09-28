#include "position_n2k.h"

#include <string.h>

#include "position_n2k_pgn.h"

static struct {
    position_n2k_hw_t hw;
    bool inited;
    bool has_fix;
    double lat, lon;
    uint32_t received_ms;
} s;

void position_n2k_init(const position_n2k_hw_t *hw)
{
    s.hw = *hw;
    s.inited = true;
    s.has_fix = false;
}

void position_n2k_on_msg(uint32_t pgn, const uint8_t *data, uint8_t len, void *arg)
{
    (void)arg;
    if (!s.inited) {
        return;
    }
    double lat, lon;
    bool ok;
    if (pgn == 129025) {
        ok = position_n2k_parse_129025(data, len, &lat, &lon);
    } else if (pgn == 129029) {
        ok = position_n2k_parse_129029(data, len, &lat, &lon);
    } else {
        return;
    }
    if (!ok) {
        return;
    }
    s.lat = lat;
    s.lon = lon;
    s.has_fix = true;
    s.received_ms = s.hw.now_ms();
}

bool position_n2k_get(double *lat, double *lon, uint32_t *received_ms)
{
    if (!s.inited || !s.has_fix) {
        return false;
    }
    *lat = s.lat;
    *lon = s.lon;
    *received_ms = s.received_ms;
    return true;
}

void position_n2k_reset(void)
{
    memset(&s, 0, sizeof(s));
}

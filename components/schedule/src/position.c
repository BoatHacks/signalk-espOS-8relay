#include "position.h"

#include "position_n2k.h"
#include "position_sk.h"

void position_get(const device_config_t *cfg, uint32_t now_ms, double *lat, double *lon)
{
    double live_lat, live_lon;
    uint32_t received_ms;
    bool have;

    if (cfg->position_source == POSITION_SRC_N2K) {
        have = position_n2k_get(&live_lat, &live_lon, &received_ms);
    } else {
        have = position_sk_get(&live_lat, &live_lon, &received_ms);
    }

    // Correct across a uint32 wrap of now_ms(), like relay_ctrl's own
    // deadline arithmetic (relay_ctrl.c's deadline_passed()).
    if (have && (uint32_t)(now_ms - received_ms) <= POSITION_STALE_MS) {
        *lat = live_lat;
        *lon = live_lon;
        return;
    }
    *lat = cfg->fallback_lat;
    *lon = cfg->fallback_lon;
}

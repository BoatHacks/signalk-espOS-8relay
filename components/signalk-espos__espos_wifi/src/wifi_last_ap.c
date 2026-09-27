// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
/*
 * Whether a remembered AP may be used to skip the channel scan.
 *
 * Split out of port_idf.c so it can be host-tested: that file needs 22 IDF
 * headers and cannot compile on the linux target, while this decision is pure
 * and is where every way of getting it wrong lives -- a garbage RTC record after
 * a cold power-on, a record belonging to a different SSID, an exhausted attempt
 * budget, an operator-pinned BSSID that must win. Each of those costs a failed
 * association if it is judged wrongly, which is exactly the delay the caller is
 * trying to remove.
 */

#include "wifi_last_ap.h"

#include <string.h>

uint32_t espos_wifi_last_ap_check(const char *ssid, const uint8_t *bssid, uint8_t channel)
{
    uint32_t h = ESPOS_WIFI_LAST_AP_MAGIC ^ ((uint32_t)channel * 2654435761u);
    for (size_t i = 0; i < ESPOS_WIFI_LAST_AP_SSID_MAX && ssid[i]; i++) {
        h = (h ^ (uint8_t)ssid[i]) * 16777619u;
    }
    for (size_t i = 0; i < 6; i++) {
        h = (h ^ bssid[i]) * 16777619u;
    }
    return h + 0x9e3779b9u;
}

bool espos_wifi_last_ap_usable(const espos_wifi_last_ap_t *rec, const char *ssid,
                               uint8_t attempts_left, bool operator_pinned)
{
    if (!rec || !ssid) {
        return false;
    }
    /* The operator's pin means "only ever this BSSID". A cache that widened or
     * narrowed it would silently override a deliberate setting. */
    if (operator_pinned) {
        return false;
    }
    if (attempts_left == 0) {
        return false;
    }
    if (rec->magic != ESPOS_WIFI_LAST_AP_MAGIC) {
        return false; /* never written, or cleared */
    }
    /* Uninitialised RTC RAM can hold anything, including a word that happens to
     * equal the magic, so the checksum is not redundant with it. */
    if (rec->check != espos_wifi_last_ap_check(rec->ssid, rec->bssid, rec->channel)) {
        return false;
    }
    if (rec->channel == 0 || rec->channel > 177) {
        return false; /* not a channel any band uses */
    }
    /* A different network in the list must not inherit this one's BSSID. */
    if (strncmp(rec->ssid, ssid, ESPOS_WIFI_LAST_AP_SSID_MAX) != 0) {
        return false;
    }
    return true;
}

void espos_wifi_last_ap_store(espos_wifi_last_ap_t *rec, const char *ssid,
                              const uint8_t *bssid, uint8_t channel)
{
    if (!rec || !ssid || !bssid) {
        return;
    }
    memset(rec->ssid, 0, sizeof(rec->ssid));
    strncpy(rec->ssid, ssid, sizeof(rec->ssid) - 1);
    memcpy(rec->bssid, bssid, 6);
    rec->channel = channel;
    rec->magic = ESPOS_WIFI_LAST_AP_MAGIC;
    rec->check = espos_wifi_last_ap_check(rec->ssid, rec->bssid, rec->channel);
}

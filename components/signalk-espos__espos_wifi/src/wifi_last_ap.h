// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESPOS_WIFI_LAST_AP_MAGIC    0x57494641u /* "WIFA" */
#define ESPOS_WIFI_LAST_AP_SSID_MAX 33

/* The AP a device last associated with. Lives in RTC memory in the IDF port, so
 * every field has to be treated as untrusted until the checksum says otherwise. */
typedef struct {
    uint32_t magic;
    char ssid[ESPOS_WIFI_LAST_AP_SSID_MAX];
    uint8_t bssid[6];
    uint8_t channel;
    uint32_t check;
} espos_wifi_last_ap_t;

uint32_t espos_wifi_last_ap_check(const char *ssid, const uint8_t *bssid, uint8_t channel);

/* May this record be used to skip the channel scan for `ssid`? */
bool espos_wifi_last_ap_usable(const espos_wifi_last_ap_t *rec, const char *ssid,
                               uint8_t attempts_left, bool operator_pinned);

void espos_wifi_last_ap_store(espos_wifi_last_ap_t *rec, const char *ssid,
                              const uint8_t *bssid, uint8_t channel);

#ifdef __cplusplus
}
#endif

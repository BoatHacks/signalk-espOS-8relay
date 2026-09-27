/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#include "espos_wifi_sm.h"

const char *espos_wifi_state_str(espos_wifi_state_t s)
{
    switch (s) {
    case ESPOS_WIFI_ST_DISABLED: return "disabled";
    case ESPOS_WIFI_ST_UNCONFIGURED: return "unconfigured";
    case ESPOS_WIFI_ST_CONNECTING: return "connecting";
    case ESPOS_WIFI_ST_OBTAINING_IP: return "obtaining_ip";
    case ESPOS_WIFI_ST_CONNECTED: return "connected";
    case ESPOS_WIFI_ST_BACKOFF: return "backoff";
    }
    return "unknown";
}

/* Codes are wifi_err_reason_t values (esp_wifi_types_generic.h); spelled as
 * numbers here so this file has no esp_wifi dependency on the host. */
const char *espos_wifi_reason_str(int reason)
{
    switch (reason) {
    case 0: return "";
    case 1: return "unspecified";
    case 2: return "wrong password (auth expired)";               /* AUTH_EXPIRE */
    case 3: return "left the network";                            /* AUTH_LEAVE */
    case 4: return "association expired";                         /* ASSOC_EXPIRE */
    case 5: return "access point full";                           /* ASSOC_TOOMANY */
    case 6: return "not authenticated";
    case 7: return "not associated";
    case 8: return "disconnected on request";                     /* ASSOC_LEAVE */
    case 9: return "association without authentication";
    case 13: return "invalid security element";                   /* IE_INVALID */
    case 14: return "message integrity failure";                  /* MIC_FAILURE */
    case 15: return "wrong password (4-way handshake timeout)";   /* 4WAY_HANDSHAKE_TIMEOUT */
    case 16: return "group key update timeout";
    case 17: return "handshake element mismatch";
    case 18: return "group cipher not supported";
    case 19: return "pairwise cipher not supported";
    case 20: return "AKM not supported";
    case 23: return "802.1X authentication failed";
    case 24: return "cipher suite rejected";
    case 200: return "lost beacon (out of range or AP off?)";     /* BEACON_TIMEOUT */
    case 201: return "network not in range";                      /* NO_AP_FOUND */
    case 202: return "wrong password";                            /* AUTH_FAIL */
    case 203: return "association failed";                        /* ASSOC_FAIL */
    case 204: return "auth timed out, weak signal?";              /* HANDSHAKE_TIMEOUT */
    case 205: return "connection failed";                         /* CONNECTION_FAIL */
    case 206: return "AP timing reset";                           /* AP_TSF_RESET */
    case 207: return "roaming";                                   /* ROAMING */
    case 208: return "association comeback time too long";
    case 209: return "SA query timeout";
    case 210: return "no AP found with compatible security";
    case 211: return "no AP found in auth-mode threshold";
    case 212: return "no AP found in RSSI threshold";
    case ESPOS_WIFI_REASON_DHCP_TIMEOUT: return "associated but no IP address (DHCP timeout)";
    case ESPOS_WIFI_REASON_CONNECT_TIMEOUT: return "no answer from the driver (connect timeout)";
    case ESPOS_WIFI_REASON_LOST_IP: return "IP address lost";
    case ESPOS_WIFI_REASON_CONFIG_CHANGE: return "reconnecting after configuration change";
    case ESPOS_WIFI_REASON_DISABLED: return "station disabled";
    default: return "unknown reason"; /* the numeric code travels alongside */
    }
}

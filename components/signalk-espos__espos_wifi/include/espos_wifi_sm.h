/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_wifi state machine — pure C, no esp_wifi calls. The driver side is
 * injected through espos_wifi_port_t so the machine runs unchanged on the
 * host under test. See docs/wifi.md for the state diagram.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_ESPOS_WIFI_MAX_NETWORKS
#define ESPOS_WIFI_MAX_NETWORKS CONFIG_ESPOS_WIFI_MAX_NETWORKS
#else
#define ESPOS_WIFI_MAX_NETWORKS 4
#endif

typedef enum {
    ESPOS_WIFI_ST_DISABLED = 0,   /* sta_enabled == false */
    ESPOS_WIFI_ST_UNCONFIGURED,   /* no network has an SSID */
    ESPOS_WIFI_ST_CONNECTING,     /* connect issued, waiting for association */
    ESPOS_WIFI_ST_OBTAINING_IP,   /* associated, waiting for DHCP */
    ESPOS_WIFI_ST_CONNECTED,      /* has an IP */
    ESPOS_WIFI_ST_BACKOFF,        /* waiting before the next attempt */
} espos_wifi_state_t;

/* Our own reason codes live above the 802.11 / esp_wifi range. */
#define ESPOS_WIFI_REASON_NONE            0
#define ESPOS_WIFI_REASON_DHCP_TIMEOUT    1001
#define ESPOS_WIFI_REASON_CONNECT_TIMEOUT 1002
#define ESPOS_WIFI_REASON_LOST_IP         1003
#define ESPOS_WIFI_REASON_CONFIG_CHANGE   1004
#define ESPOS_WIFI_REASON_DISABLED        1005

typedef struct {
    char ssid[33];
    char psk[65];
    bool has_bssid;
    uint8_t bssid[6];
} espos_wifi_net_t;

typedef struct {
    bool sta_enabled;
    espos_wifi_net_t nets[ESPOS_WIFI_MAX_NETWORKS];
    size_t net_count;                /* nets[0..net_count) have an SSID, priority order */
    uint32_t backoff_max_ms;
    uint32_t dhcp_timeout_ms;
    uint32_t connect_timeout_ms;
    bool portal_enabled;
    uint32_t portal_after_ms;
} espos_wifi_cfg_t;

typedef enum {
    ESPOS_WIFI_EV_START,          /* cfg loaded; begin */
    ESPOS_WIFI_EV_STOP,           /* shut down (deinit) */
    ESPOS_WIFI_EV_CONFIG,         /* cfg replaced (arg = const espos_wifi_cfg_t *) */
    ESPOS_WIFI_EV_STA_CONNECTED,  /* associated (arg = const espos_wifi_link_t *) */
    ESPOS_WIFI_EV_STA_DISCONNECTED, /* arg = int reason (802.11/esp_wifi code) */
    ESPOS_WIFI_EV_GOT_IP,         /* arg = const espos_wifi_ip_t * */
    ESPOS_WIFI_EV_LOST_IP,
    ESPOS_WIFI_EV_TIMER,          /* the single SM timer expired */
    ESPOS_WIFI_EV_PORTAL_CLIENT,  /* arg = int station count on the portal AP */
    ESPOS_WIFI_EV_PORTAL_FAILED,  /* the port could not bring the AP up */
    ESPOS_WIFI_EV_PORTAL_RECONFIG, /* portal SSID/password changed */
} espos_wifi_event_t;

typedef struct {
    char ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
    int8_t rssi;
} espos_wifi_link_t;

typedef struct {
    char ip[16];
    char netmask[16];
    char gateway[16];
} espos_wifi_ip_t;

/* Driver actions requested by the machine. All are called with the SM lock
 * held by the caller of espos_wifi_sm_event(); keep them short. */
typedef struct {
    esp_err_t (*connect)(void *ctx, const espos_wifi_net_t *net);
    esp_err_t (*disconnect)(void *ctx);
    esp_err_t (*portal_start)(void *ctx);
    esp_err_t (*portal_stop)(void *ctx);
    void (*arm_timer)(void *ctx, uint32_t ms);   /* one timer; re-arming replaces; must fire no earlier than now_ms()+ms */
    void (*cancel_timer)(void *ctx);
    uint32_t (*now_ms)(void *ctx);
    uint32_t (*random)(void *ctx);
    void (*status_changed)(void *ctx);           /* something observable changed */
} espos_wifi_port_t;

typedef struct {
    espos_wifi_state_t state;
    bool sta_enabled;
    int net_index;               /* network being tried / connected, -1 none */
    espos_wifi_link_t link;      /* valid in OBTAINING_IP / CONNECTED */
    espos_wifi_ip_t ip;          /* valid in CONNECTED */
    int reason;                  /* last failure reason (0 = none) */
    uint32_t backoff_until_ms;   /* valid in BACKOFF (port clock) */
    uint32_t attempt;            /* attempts since last success */
    uint32_t round;              /* full rounds over all networks since last success */
    uint32_t connected_since_ms; /* valid in CONNECTED */
    uint32_t disconnected_since_ms; /* when we last lost/lacked a connection */
    uint32_t connect_count;
    uint32_t disconnect_count;
    bool portal_active;
    int portal_clients;
} espos_wifi_sm_status_t;

typedef struct espos_wifi_sm {
    const espos_wifi_port_t *port;
    void *port_ctx;
    espos_wifi_cfg_t cfg;
    espos_wifi_sm_status_t st;
    bool started;
    bool timer_is_dhcp;          /* which timeout the armed timer represents */
    bool timer_is_portal;
    uint32_t portal_due_ms;      /* when the portal should come up (0 = not scheduled) */
    uint32_t state_due_ms;       /* deadline of the armed state timeout (0 = none) */
    uint32_t timer_due_ms;       /* deadline of the armed timer (0 = none); early fires are stale */
} espos_wifi_sm_t;

void espos_wifi_sm_init(espos_wifi_sm_t *sm, const espos_wifi_port_t *port, void *port_ctx,
                        const espos_wifi_cfg_t *cfg);
void espos_wifi_sm_event(espos_wifi_sm_t *sm, espos_wifi_event_t ev, const void *arg);
const espos_wifi_sm_status_t *espos_wifi_sm_status(const espos_wifi_sm_t *sm);
/* Remaining backoff in ms (0 if not in BACKOFF). */
uint32_t espos_wifi_sm_backoff_remaining_ms(const espos_wifi_sm_t *sm);

const char *espos_wifi_state_str(espos_wifi_state_t s);
/* Human explanation for a disconnect reason (esp_wifi codes and ours). */
const char *espos_wifi_reason_str(int reason);
/* Deprecated: espos_net_backoff_ms() (espos_net.h) is the same curve — 1 s ·
 * 2^round, capped, ±25 % jitter, floor 250 ms — for every retry loop in espOS.
 * Kept as a standalone copy so the state machine has no dependency and builds
 * on its own in test/host/espos_wifi_test; removed in 0.9. */
uint32_t espos_wifi_backoff_ms(uint32_t round, uint32_t cap_ms, uint32_t rnd);

#ifdef __cplusplus
}
#endif

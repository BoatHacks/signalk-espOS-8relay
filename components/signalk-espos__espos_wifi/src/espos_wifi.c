/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_wifi core: owns the state machine behind a mutex, loads the "wifi"
 * config namespace, follows config changes, exposes status/scan JSON and
 * publishes SSE events. Driver specifics live in the port.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "cJSON.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "espos_cfg_keys.h"
#include "espos_config.h"
#include "espos_event.h"
#include "espos_httpd.h"
#include "espos_httpd_sse.h"
#include "espos_net.h"
#include "espos_wifi.h"
#include "espos_wifi_priv.h"

static const char *TAG = "espos_wifi";

#define SCAN_MAX 20

static struct {
    SemaphoreHandle_t lock;
    espos_wifi_sm_t sm;
    const espos_wifi_driver_t *drv;
    bool started;
    char hostname[33];
    char portal_ssid[33];
    char portal_psk[65];
    /* scan cache */
    espos_wifi_scan_entry_t scan[SCAN_MAX];
    size_t scan_count;
    bool scanning;
    uint32_t scan_done_ms;
    TimerHandle_t cfg_debounce;
    /* Driver actions requested by the SM are queued under the lock and run
     * after it is released: driver calls may block on the WiFi task (or the
     * hosted RPC task), which itself needs to deliver events into the SM. */
    struct {
        enum { ACT_NONE,
               ACT_CONNECT,
               ACT_DISCONNECT,
               ACT_PORTAL_START,
               ACT_PORTAL_STOP } type;
        espos_wifi_net_t net;
    } actions[8];
    size_t action_head, action_count;
    char *pending_status_json;   /* latest snapshot to publish (coalesced) */
    /* The link as espos_net should hear it, decided under the lock on every
     * SM status change and delivered by the drainer outside it:
     * espos_net_report() takes its own lock and runs subscriber callbacks,
     * neither of which may happen under ours. */
    struct {
        bool pending;
        bool up;
        espos_wifi_ip_t ip;
        int8_t rssi;
    } net_report;
    SemaphoreHandle_t drain_lock;
    bool sm_ready;
    bool api_registered;
} s;

static void lock(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s.lock);
}

const char *espos_wifi_short_id(void)
{
    return espos_net_short_id(); /* deprecated wrapper, see the header */
}

/* ---------------------------------------------------------- config load */

static bool parse_bssid(const char *str, uint8_t out[6])
{
    unsigned b[6];
    if (!str || !*str) {
        return false;
    }
    if (sscanf(str, "%2x:%2x:%2x:%2x:%2x:%2x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)b[i];
    }
    return true;
}

static void load_cfg(espos_wifi_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    static const char *const ssid_keys[] = { ESPOS_CFG_WIFI_SSID0, ESPOS_CFG_WIFI_SSID1, ESPOS_CFG_WIFI_SSID2, ESPOS_CFG_WIFI_SSID3 };
    static const char *const psk_keys[] = { ESPOS_CFG_WIFI_PSK0, ESPOS_CFG_WIFI_PSK1, ESPOS_CFG_WIFI_PSK2, ESPOS_CFG_WIFI_PSK3 };
    static const char *const bssid_keys[] = { ESPOS_CFG_WIFI_BSSID0, ESPOS_CFG_WIFI_BSSID1, ESPOS_CFG_WIFI_BSSID2, ESPOS_CFG_WIFI_BSSID3 };
    espos_config_get_bool(ESPOS_CFG_NS_WIFI, ESPOS_CFG_WIFI_STA_ENABLED, &c->sta_enabled);
    for (int i = 0; i < 4 && i < ESPOS_WIFI_MAX_NETWORKS; i++) {
        char ssid[33] = { 0 };
        espos_config_get_str(ESPOS_CFG_NS_WIFI, ssid_keys[i], ssid, sizeof(ssid), NULL);
        if (ssid[0] == '\0') {
            continue; /* empty slots are skipped; order of the rest is kept */
        }
        char psk[65] = { 0 };
        espos_config_get_str(ESPOS_CFG_NS_WIFI, psk_keys[i], psk, sizeof(psk), NULL);
        size_t pl = strlen(psk);
        if (pl > 0 && pl < 8) {
            ESP_LOGW(TAG, "%s: password shorter than 8 characters is not valid WPA; slot skipped", ssid_keys[i]);
            continue;
        }
        espos_wifi_net_t *n = &c->nets[c->net_count++];
        strcpy(n->ssid, ssid);
        strcpy(n->psk, psk);
        char bssid[18] = { 0 };
        espos_config_get_str(ESPOS_CFG_NS_WIFI, bssid_keys[i], bssid, sizeof(bssid), NULL);
        n->has_bssid = parse_bssid(bssid, n->bssid);
    }
    int32_t v = 60;
    espos_config_get_i32(ESPOS_CFG_NS_WIFI, ESPOS_CFG_WIFI_BACKOFF_MAX_S, &v);
    c->backoff_max_ms = (uint32_t)v * 1000;
    v = 15;
    espos_config_get_i32(ESPOS_CFG_NS_WIFI, ESPOS_CFG_WIFI_DHCP_TMO_S, &v);
    c->dhcp_timeout_ms = (uint32_t)v * 1000;
    v = 20;
    espos_config_get_i32(ESPOS_CFG_NS_WIFI, ESPOS_CFG_WIFI_CONNECT_TMO_S, &v);
    c->connect_timeout_ms = (uint32_t)v * 1000;
    espos_config_get_bool(ESPOS_CFG_NS_WIFI, ESPOS_CFG_WIFI_PORTAL_ENABLED, &c->portal_enabled);
    v = 90;
    espos_config_get_i32(ESPOS_CFG_NS_WIFI, ESPOS_CFG_WIFI_PORTAL_AFTER_S, &v);
    c->portal_after_ms = (uint32_t)v * 1000;

    /* names: build locally, publish under the lock (readers copy under it).
     * The hostname is espos_net's (net.hostname; wifi.hostname until 0.7):
     * the status document keeps carrying it because the UI reads it there. */
    char h[33] = { 0 }, ap[33] = { 0 }, appsk[65] = { 0 };
    espos_net_status_t ns;
    if (espos_net_get_status(&ns) == ESP_OK) {
        strcpy(h, ns.hostname);
    }
    espos_config_get_str(ESPOS_CFG_NS_WIFI, ESPOS_CFG_WIFI_PORTAL_SSID, ap, sizeof(ap), NULL);
    espos_config_get_str(ESPOS_CFG_NS_WIFI, ESPOS_CFG_WIFI_PORTAL_PSK, appsk, sizeof(appsk), NULL);
    if (appsk[0] && strlen(appsk) < 8) {
        ESP_LOGW(TAG, "portal_psk shorter than 8 chars is invalid for WPA2; portal will be open");
        appsk[0] = '\0';
    }
    lock();
    if (h[0]) {
        strcpy(s.hostname, h);
    } else {
        snprintf(s.hostname, sizeof(s.hostname), "espos-%s", espos_net_short_id());
    }
    if (ap[0]) {
        strcpy(s.portal_ssid, ap);
    } else {
        snprintf(s.portal_ssid, sizeof(s.portal_ssid), "espOS-%s", espos_net_short_id());
    }
    strcpy(s.portal_psk, appsk);
    unlock();
}

void espos_wifi_portal_credentials(char ssid[33], char psk[65])
{
    lock();
    strcpy(ssid, s.portal_ssid);
    strcpy(psk, s.portal_psk);
    unlock();
}

/* --------------------------------------------------------- SM plumbing */

static char *status_json_locked(void);
static void drain_all_locked(void);

/* Lock held: append a driver action for later. */
static void queue_action(int type, const espos_wifi_net_t *net)
{
    if (s.action_count == sizeof(s.actions) / sizeof(s.actions[0])) {
        ESP_LOGE(TAG, "action queue full; dropping oldest");
        s.action_head = (s.action_head + 1) % (sizeof(s.actions) / sizeof(s.actions[0]));
        s.action_count--;
    }
    size_t i = (s.action_head + s.action_count) % (sizeof(s.actions) / sizeof(s.actions[0]));
    s.actions[i].type = type;
    if (net) {
        s.actions[i].net = *net;
    }
    s.action_count++;
}

/* Deferred wrappers handed to the SM (called with the lock held). */
static esp_err_t q_connect(void *ctx, const espos_wifi_net_t *net)
{
    (void)ctx;
    queue_action(ACT_CONNECT, net);
    return ESP_OK;
}
static esp_err_t q_disconnect(void *ctx)
{
    (void)ctx;
    queue_action(ACT_DISCONNECT, NULL);
    return ESP_OK;
}
static esp_err_t q_portal_start(void *ctx)
{
    (void)ctx;
    queue_action(ACT_PORTAL_START, NULL);
    return ESP_OK;
}
static esp_err_t q_portal_stop(void *ctx)
{
    (void)ctx;
    queue_action(ACT_PORTAL_STOP, NULL);
    return ESP_OK;
}

/* Lock held: what espos_net should hear. Identical repeats are cheap no-ops
 * there, so every status change may queue one; the drainer delivers it. */
static void queue_net_report_locked(void)
{
    s.net_report.pending = true;
    s.net_report.up = s.sm.st.state == ESPOS_WIFI_ST_CONNECTED;
    s.net_report.ip = s.sm.st.ip;
    s.net_report.rssi = s.sm.st.link.rssi;
}

/* status_changed from the SM (lock held): snapshot now, publish after unlock. */
static void on_status_changed(void *ctx)
{
    (void)ctx;
    char *json = status_json_locked();
    if (json) {
        free(s.pending_status_json);
        s.pending_status_json = json;
    }
    queue_net_report_locked();
}

/* Run queued driver actions in FIFO order, outside the SM lock. A single
 * drainer at a time keeps global ordering. */
static void drain(void)
{
    for (;;) {
        /* Never block here: the event task may be the caller, and the current
         * drainer may be inside a driver call whose completion needs that
         * very task. Whoever holds the drain lock drains everything queued. */
        if (xSemaphoreTake(s.drain_lock, 0) != pdTRUE) {
            return;
        }
        drain_all_locked();
        xSemaphoreGive(s.drain_lock);
        /* something may have been queued between our last look and the give */
        lock();
        bool more = s.action_count > 0 || s.pending_status_json != NULL || s.net_report.pending;
        unlock();
        if (!more) {
            return;
        }
    }
}

/* Called with drain_lock held. */
static void drain_all_locked(void)
{
    for (;;) {
        lock();
        if (s.net_report.pending) {
            /* espos_net first: the NETWORK_UP/DOWN it posts and the callbacks
             * it runs must not queue behind a driver call that may block on
             * the radio. */
            bool up = s.net_report.up;
            espos_wifi_ip_t ip = s.net_report.ip;
            int8_t rssi = s.net_report.rssi;
            s.net_report.pending = false;
            unlock();
            espos_net_report(ESPOS_NET_IF_WIFI_STA, up, ip.ip, ip.netmask, ip.gateway, rssi);
            continue;
        }
        if (s.action_count == 0) {
            char *json = s.pending_status_json;
            s.pending_status_json = NULL;
            unlock();
            if (json) {
                espos_httpd_sse_publish("wifi", json);
                free(json);
            }
            break;
        }
        int type = s.actions[s.action_head].type;
        espos_wifi_net_t net = s.actions[s.action_head].net;
        s.action_head = (s.action_head + 1) % (sizeof(s.actions) / sizeof(s.actions[0]));
        s.action_count--;
        unlock();
        const espos_wifi_port_t *p = s.drv->sm_port;
        esp_err_t err = ESP_OK;
        switch (type) {
        case ACT_CONNECT:
            err = p->connect(NULL, &net);
            if (err != ESP_OK) {
                /* the SM saw ESP_OK from the wrapper; report the failure as an event */
                int reason = ESPOS_WIFI_REASON_CONNECT_TIMEOUT;
                lock();
                espos_wifi_sm_event(&s.sm, ESPOS_WIFI_EV_STA_DISCONNECTED, &reason);
                unlock();
            }
            break;
        case ACT_DISCONNECT:
            p->disconnect(NULL);
            break;
        case ACT_PORTAL_START:
            if (p->portal_start(NULL) != ESP_OK) {
                lock();
                espos_wifi_sm_event(&s.sm, ESPOS_WIFI_EV_PORTAL_FAILED, NULL);
                unlock();
            } else {
                /* Announced BEFORE anyone tries to associate. On a part whose
                 * radio is shared with BLE -- the P4's C6 co-processor -- a
                 * scanner running at the default duty cycle owns half the
                 * airtime, and joining the portal then takes minutes instead
                 * of seconds. Listeners use this to stand down. */
                espos_event_post(ESPOS_EVENT_PORTAL_UP, NULL, 0);
            }
            break;
        case ACT_PORTAL_STOP:
            p->portal_stop(NULL);
            espos_event_post(ESPOS_EVENT_PORTAL_DOWN, NULL, 0);
            break;
        default:
            break;
        }
    }
}

void espos_wifi_dispatch(espos_wifi_event_t ev, const void *arg)
{
    if (!s.lock || !s.sm_ready) {
        return; /* driver events before the machine exists are meaningless */
    }
    lock();
    espos_wifi_sm_event(&s.sm, ev, arg);
    unlock();
    drain();
}

/* A PUT touching several wifi keys notifies once per key; coalesce them so
 * the machine sees one EV_CONFIG with the complete new configuration. */
static void cfg_debounce_cb(TimerHandle_t t)
{
    (void)t;
    if (!s.started) {
        return;
    }
    char old_ssid[33], old_psk[65];
    lock();
    strcpy(old_ssid, s.portal_ssid);
    strcpy(old_psk, s.portal_psk);
    unlock();
    espos_wifi_cfg_t c;
    load_cfg(&c);
    espos_wifi_dispatch(ESPOS_WIFI_EV_CONFIG, &c);
    lock();
    bool portal_changed = strcmp(old_ssid, s.portal_ssid) != 0 || strcmp(old_psk, s.portal_psk) != 0;
    unlock();
    if (portal_changed) {
        espos_wifi_dispatch(ESPOS_WIFI_EV_PORTAL_RECONFIG, NULL);
    }
}

static void on_config_change(const char *ns, const char *key, void *arg)
{
    (void)key;
    (void)arg;
    if (strcmp(ns, ESPOS_CFG_NS_WIFI) != 0 || !s.started || !s.cfg_debounce) {
        return;
    }
    xTimerReset(s.cfg_debounce, 0); /* (re)start the 150 ms window */
}

/* -------------------------------------------------------------- status */

/* Lock held. Never calls into the driver (a hosted RPC could block while
 * the event task waits for this lock); the live RSSI is added by callers
 * that run outside the lock. */
static void snapshot_locked(espos_wifi_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->sm = s.sm.st;
    out->backoff_remaining_ms = espos_wifi_sm_backoff_remaining_ms(&s.sm);
    if (out->sm.state == ESPOS_WIFI_ST_CONNECTED) {
        uint32_t nowms = s.drv->sm_port->now_ms(NULL);
        out->connected_s = (nowms - out->sm.connected_since_ms) / 1000;
        out->rssi = out->sm.link.rssi; /* from the association event */
    }
    strcpy(out->hostname, s.hostname);
    strcpy(out->portal_ssid, s.portal_ssid);
    snprintf(out->portal_ip, sizeof(out->portal_ip), "%s", s.drv->portal_ip ? s.drv->portal_ip : "");
}

esp_err_t espos_wifi_refresh_rssi(void)
{
    if (!s.lock) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s.drv || !s.drv->rssi) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    lock();
    bool connected = (s.sm.st.state == ESPOS_WIFI_ST_CONNECTED);
    unlock();
    if (!connected) {
        return ESP_ERR_INVALID_STATE;
    }
    /* MAY BLOCK. On co-processor boards this is an RPC to the radio and
     * can take the transport timeout to fail. Never call it from a UI,
     * render or event task — that is exactly the bug this API exists to
     * keep out of espos_wifi_get_status(). */
    int8_t r = s.drv->rssi();
    if (!r) {
        return ESP_FAIL;
    }
    lock();
    s.sm.st.link.rssi = r;
    queue_net_report_locked(); /* espos_net_status_t.rssi follows */
    unlock();
    drain();
    return ESP_OK;
}

esp_err_t espos_wifi_get_status(espos_wifi_status_t *out)
{
    if (!out || !s.lock) {
        return ESP_ERR_INVALID_STATE;
    }
    lock();
    snapshot_locked(out);
    unlock();
    /* Deliberately no driver call here.
     *
     * This used to ask the driver for a live RSSI. On co-processor
     * boards (esp_hosted: P4 host, C6 radio) that is a synchronous RPC
     * across the SDIO link, and callers poll status from wherever they
     * like — including a UI or render task once a second. When the link
     * wedges, every one of those calls blocks for the RPC timeout and
     * the caller's task stalls with it; a status label on a display
     * task is enough to freeze the whole UI. Worse, the timeout path in
     * esp_hosted destroys an already-NULL semaphore
     * (rpc_core.c, wait_for_sync_response) and asserts, so a transport
     * wedge turns into a panic on whichever task happened to ask.
     *
     * A status snapshot must never depend on the radio answering.
     * out->rssi is the value cached at association and refreshed by
     * espos_wifi_refresh_rssi(), both on the event task. */
    return ESP_OK;
}

static void bssid_str(const uint8_t b[6], char out[18])
{
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
}

static char *status_to_json(const espos_wifi_status_t *st)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "state", espos_wifi_state_str(st->sm.state));
    cJSON_AddBoolToObject(root, "sta_enabled", st->sm.sta_enabled);
    cJSON_AddStringToObject(root, "hostname", st->hostname);
    cJSON_AddNumberToObject(root, "network_index", st->sm.net_index);
    if (st->sm.state == ESPOS_WIFI_ST_CONNECTED || st->sm.state == ESPOS_WIFI_ST_OBTAINING_IP) {
        char b[18];
        bssid_str(st->sm.link.bssid, b);
        cJSON_AddStringToObject(root, "ssid", st->sm.link.ssid);
        cJSON_AddStringToObject(root, "bssid", b);
        cJSON_AddNumberToObject(root, "channel", st->sm.link.channel);
        cJSON_AddNumberToObject(root, "rssi", st->rssi ? st->rssi : st->sm.link.rssi);
    }
    if (st->sm.state == ESPOS_WIFI_ST_CONNECTED) {
        cJSON_AddStringToObject(root, "ip", st->sm.ip.ip);
        cJSON_AddStringToObject(root, "netmask", st->sm.ip.netmask);
        cJSON_AddStringToObject(root, "gateway", st->sm.ip.gateway);
        cJSON_AddNumberToObject(root, "connected_s", st->connected_s);
    }
    cJSON *reason = cJSON_AddObjectToObject(root, "reason");
    if (reason) {
        cJSON_AddNumberToObject(reason, "code", st->sm.reason);
        cJSON_AddStringToObject(reason, "text", espos_wifi_reason_str(st->sm.reason));
    }
    if (st->sm.state == ESPOS_WIFI_ST_BACKOFF) {
        cJSON_AddNumberToObject(root, "backoff_ms", st->backoff_remaining_ms);
    }
    cJSON_AddNumberToObject(root, "attempt", st->sm.attempt);
    cJSON_AddNumberToObject(root, "round", st->sm.round);
    cJSON_AddNumberToObject(root, "connect_count", st->sm.connect_count);
    cJSON_AddNumberToObject(root, "disconnect_count", st->sm.disconnect_count);
    cJSON *portal = cJSON_AddObjectToObject(root, "portal");
    if (portal) {
        cJSON_AddBoolToObject(portal, "active", st->sm.portal_active);
        cJSON_AddStringToObject(portal, "ssid", st->portal_ssid);
        if (st->sm.portal_active) {
            cJSON_AddStringToObject(portal, "ip", st->portal_ip);
            cJSON_AddNumberToObject(portal, "clients", st->sm.portal_clients);
        }
    }
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return txt;
}

static char *status_json_locked(void)
{
    espos_wifi_status_t st;
    snapshot_locked(&st);
    return status_to_json(&st);
}

esp_err_t espos_wifi_status_json(char **out_json)
{
    if (!out_json) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_json = NULL;
    espos_wifi_status_t st;
    esp_err_t err = espos_wifi_get_status(&st); /* includes the live RSSI */
    if (err != ESP_OK) {
        return err;
    }
    *out_json = status_to_json(&st);
    return *out_json ? ESP_OK : ESP_ERR_NO_MEM;
}

/* ---------------------------------------------------------------- scan */

esp_err_t espos_wifi_scan_start(void)
{
    if (!s.lock || !s.drv->scan_start) {
        return ESP_ERR_INVALID_STATE;
    }
    lock();
    if (s.scanning) {
        unlock();
        return ESP_OK;
    }
    s.scanning = true;
    unlock();
    esp_err_t err = s.drv->scan_start();
    if (err != ESP_OK) {
        lock();
        s.scanning = false;
        unlock();
    }
    return err;
}

void espos_wifi_scan_done(const espos_wifi_scan_entry_t *entries, size_t n)
{
    lock();
    if (n > SCAN_MAX) {
        n = SCAN_MAX;
    }
    memcpy(s.scan, entries, n * sizeof(*entries));
    s.scan_count = n;
    s.scanning = false;
    s.scan_done_ms = s.drv->sm_port->now_ms(NULL);
    unlock();
    char *json = NULL;
    if (espos_wifi_scan_json(&json) == ESP_OK) {
        espos_httpd_sse_publish("wifi_scan", json);
        free(json);
    }
}

static const char *auth_str(uint8_t a)
{
    /* wifi_auth_mode_t: 0 open, 1 WEP, 2 WPA, 3 WPA2, 4 WPA/WPA2, 5 ENT, 6 WPA3, 7 WPA2/WPA3, 8 WAPI, 9 OWE, 10 WPA3-ENT-192 */
    switch (a) {
    case 0: return "open";
    case 1: return "wep";
    case 2: return "wpa";
    case 3: return "wpa2";
    case 4: return "wpa/wpa2";
    case 5: return "wpa2-enterprise";
    case 6: return "wpa3";
    case 7: return "wpa2/wpa3";
    case 8: return "wapi";
    case 9: return "owe";
    default: return "other";
    }
}

esp_err_t espos_wifi_scan_json(char **out_json)
{
    if (!out_json) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_json = NULL;
    if (!s.lock) {
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    lock();
    cJSON_AddBoolToObject(root, "scanning", s.scanning);
    if (s.scan_done_ms) {
        cJSON_AddNumberToObject(root, "age_s", (s.drv->sm_port->now_ms(NULL) - s.scan_done_ms) / 1000);
    } else {
        cJSON_AddNullToObject(root, "age_s");
    }
    cJSON *arr = cJSON_AddArrayToObject(root, "results");
    for (size_t i = 0; arr && i < s.scan_count; i++) {
        cJSON *e = cJSON_CreateObject();
        if (!e) {
            break;
        }
        char b[18];
        bssid_str(s.scan[i].bssid, b);
        cJSON_AddStringToObject(e, "ssid", s.scan[i].ssid);
        cJSON_AddStringToObject(e, "bssid", b);
        cJSON_AddNumberToObject(e, "rssi", s.scan[i].rssi);
        cJSON_AddNumberToObject(e, "channel", s.scan[i].channel);
        cJSON_AddStringToObject(e, "auth", auth_str(s.scan[i].authmode));
        cJSON_AddItemToArray(arr, e);
    }
    unlock();
    *out_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return *out_json ? ESP_OK : ESP_ERR_NO_MEM;
}

/* ------------------------------------------------------------ SSE hello */

static void sse_hello(int client, void *arg)
{
    (void)arg;
    char *json = NULL;
    if (espos_wifi_status_json(&json) == ESP_OK) {
        espos_httpd_sse_send(client, "wifi", json);
        free(json);
    }
}

/* ------------------------------------------------------------ lifecycle */

esp_err_t espos_wifi_start(void)
{
    if (s.started) {
        return ESP_OK;
    }
    /* The /wifi endpoints and the portal's page live on the HTTP server; a
     * portal that comes up with nothing to serve is the worst first
     * impression a device can make. */
    if (!espos_httpd_handle()) {
        ESP_LOGE(TAG, "espos_wifi_start: call espos_httpd_start() first (or espos_start())");
        return ESP_ERR_INVALID_STATE;
    }
    /* The device id, the hostname and the route this station reports into
     * are espos_net's, as is the mDNS responder that used to be started here;
     * the netif gets its hostname from espos_net the moment the port registers it. */
    espos_net_status_t ns;
    if (espos_net_get_status(&ns) != ESP_OK) {
        ESP_LOGE(TAG, "espos_wifi_start: call espos_net_start() first (or espos_start())");
        return ESP_ERR_INVALID_STATE;
    }
    s.drv = espos_wifi_driver();
    if (!s.lock) {
        s.lock = xSemaphoreCreateMutex();
        if (!s.lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s.drain_lock) {
        s.drain_lock = xSemaphoreCreateMutex();
        if (!s.drain_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s.cfg_debounce) {
        s.cfg_debounce = xTimerCreate("wifi_cfg", pdMS_TO_TICKS(150), pdFALSE, NULL, cfg_debounce_cb);
        if (!s.cfg_debounce) {
            return ESP_ERR_NO_MEM;
        }
    }
    esp_err_t err = s.drv->init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "driver init failed: %s", esp_err_to_name(err));
        return err;
    }
    /* After init: the co-processor link has to exist before we can ask
     * it for a heartbeat. Failure here costs wedge detection, not WiFi,
     * so it is logged inside and never fails the start. */
    (void)espos_wifi_hosted_watchdog_start();
    espos_wifi_cfg_t cfg;
    load_cfg(&cfg);
    if (s.drv->set_ps) {
        char ps[8] = "none";
        espos_config_get_str(ESPOS_CFG_NS_WIFI, ESPOS_CFG_WIFI_PS_MODE, ps, sizeof(ps), NULL);
        s.drv->set_ps(ps);
    }

    /* The SM sees deferred wrappers: driver calls run after the lock is
     * released (see drain()); timer/clock/random stay direct. */
    static espos_wifi_port_t port;
    port = *s.drv->sm_port;
    port.connect = q_connect;
    port.disconnect = q_disconnect;
    port.portal_start = q_portal_start;
    port.portal_stop = q_portal_stop;
    port.status_changed = on_status_changed;
    lock();
    espos_wifi_sm_init(&s.sm, &port, NULL, &cfg);
    s.sm_ready = true;
    unlock();

    if (!s.api_registered) {
        /* URI handlers and the SSE hook survive stop(); register once. */
        ESP_ERROR_CHECK(espos_wifi_register_api());
        espos_httpd_sse_on_connect(sse_hello, NULL);
        s.api_registered = true;
    }
    espos_config_subscribe(on_config_change, NULL);
    s.started = true;
    ESP_LOGI(TAG, "hostname %s, %u network(s), portal %s", s.hostname, (unsigned)cfg.net_count,
             cfg.portal_enabled ? s.portal_ssid : "off");
    if (cfg.net_count == 0 && cfg.portal_enabled) {
        /* The machine brings the portal up at once when nothing is
         * configured; say what to do next before the driver lines start. */
        ESP_LOGI(TAG, "no network configured: join \"%s\" and open http://%s", s.portal_ssid,
                 s.drv->portal_ip ? s.drv->portal_ip : "192.168.4.1");
    }
    espos_wifi_dispatch(ESPOS_WIFI_EV_START, NULL);
    return ESP_OK;
}

esp_err_t espos_wifi_stop(void)
{
    if (!s.started) {
        return ESP_OK;
    }
    espos_config_unsubscribe(on_config_change, NULL);
    xTimerStop(s.cfg_debounce, 0);
    espos_wifi_dispatch(ESPOS_WIFI_EV_STOP, NULL);
    s.started = false;
    esp_err_t err = s.drv->deinit ? s.drv->deinit() : ESP_OK;
    lock();
    s.sm_ready = false;
    unlock();
    return err;
}

/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host (linux target) port: a scripted stand-in for the WiFi driver so the
 * state machine, the status JSON and the HTTP/SSE plumbing run in host
 * tests. Behaviour is chosen with ESPOS_SIM_WIFI:
 *
 *   connect            (default) associate after 200 ms, IP after 200 ms more
 *   fail:<reason>      disconnect with <reason> 200 ms after each connect
 *   dhcp               associate, never hand out an IP
 *   silent             never answer
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"

#include "espos_wifi_priv.h"

static const char *TAG = "espos_wifi_sim";

static TimerHandle_t s_sm_timer;
static TimerHandle_t s_script_timer;
static int s_script_step;
static char s_mode[32] = "connect";
static int s_fail_reason = 201;
static espos_wifi_net_t s_net;
static bool s_portal;

/* Exposed for tests. */
int espos_wifi_sim_connect_calls;
int espos_wifi_sim_disconnect_calls;
int espos_wifi_sim_portal_starts;
int espos_wifi_sim_portal_stops;

static void script_cb(TimerHandle_t t)
{
    (void)t;
    if (strncmp(s_mode, "fail", 4) == 0) {
        espos_wifi_dispatch(ESPOS_WIFI_EV_STA_DISCONNECTED, &s_fail_reason);
        return;
    }
    if (s_script_step == 0) {
        espos_wifi_link_t link = { .channel = 6, .rssi = -55 };
        strncpy(link.ssid, s_net.ssid, 32);
        memcpy(link.bssid, (uint8_t[]) { 0xde, 0xad, 0xbe, 0xef, 0x00, 0x01 }, 6);
        espos_wifi_dispatch(ESPOS_WIFI_EV_STA_CONNECTED, &link);
        if (strcmp(s_mode, "dhcp") != 0) {
            s_script_step = 1;
            xTimerChangePeriod(s_script_timer, pdMS_TO_TICKS(200), 0);
        }
    } else {
        espos_wifi_ip_t ip = { .ip = "10.0.0.2", .netmask = "255.255.255.0", .gateway = "10.0.0.1" };
        espos_wifi_dispatch(ESPOS_WIFI_EV_GOT_IP, &ip);
    }
}

static esp_err_t p_connect(void *ctx, const espos_wifi_net_t *net)
{
    (void)ctx;
    espos_wifi_sim_connect_calls++;
    s_net = *net;
    ESP_LOGI(TAG, "connect('%s') mode=%s", net->ssid, s_mode);
    if (strcmp(s_mode, "silent") == 0) {
        return ESP_OK;
    }
    s_script_step = 0;
    xTimerChangePeriod(s_script_timer, pdMS_TO_TICKS(200), 0);
    return ESP_OK;
}

static esp_err_t p_disconnect(void *ctx)
{
    (void)ctx;
    espos_wifi_sim_disconnect_calls++;
    xTimerStop(s_script_timer, 0);
    return ESP_OK;
}

static esp_err_t p_portal_start(void *ctx)
{
    (void)ctx;
    espos_wifi_sim_portal_starts++;
    s_portal = true;
    ESP_LOGI(TAG, "portal up");
    return ESP_OK;
}

static esp_err_t p_portal_stop(void *ctx)
{
    (void)ctx;
    espos_wifi_sim_portal_stops++;
    s_portal = false;
    ESP_LOGI(TAG, "portal down");
    return ESP_OK;
}

static void sm_timer_cb(TimerHandle_t t)
{
    (void)t;
    espos_wifi_dispatch(ESPOS_WIFI_EV_TIMER, NULL);
}

static void p_arm_timer(void *ctx, uint32_t ms)
{
    (void)ctx;
    TickType_t ticks = (ms + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS + 1; /* never early */
    xTimerChangePeriod(s_sm_timer, ticks, 0);
}

static void p_cancel_timer(void *ctx)
{
    (void)ctx;
    xTimerStop(s_sm_timer, 0);
}

static uint32_t p_now_ms(void *ctx)
{
    (void)ctx;
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static uint32_t p_random(void *ctx)
{
    (void)ctx;
    return (uint32_t)rand();
}

static const espos_wifi_port_t k_sm_port = {
    .connect = p_connect,
    .disconnect = p_disconnect,
    .portal_start = p_portal_start,
    .portal_stop = p_portal_stop,
    .arm_timer = p_arm_timer,
    .cancel_timer = p_cancel_timer,
    .now_ms = p_now_ms,
    .random = p_random,
};

static esp_err_t d_init(void)
{
    const char *m = getenv("ESPOS_SIM_WIFI");
    if (m) {
        snprintf(s_mode, sizeof(s_mode), "%s", m);
        if (strncmp(s_mode, "fail:", 5) == 0) {
            s_fail_reason = atoi(s_mode + 5);
        }
    }
    if (!s_sm_timer) {
        s_sm_timer = xTimerCreate("sim_sm", pdMS_TO_TICKS(1000), pdFALSE, NULL, sm_timer_cb);
        s_script_timer = xTimerCreate("sim_script", pdMS_TO_TICKS(200), pdFALSE, NULL, script_cb);
    }
    return (s_sm_timer && s_script_timer) ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t d_deinit(void)
{
    xTimerStop(s_sm_timer, 0);
    xTimerStop(s_script_timer, 0);
    return ESP_OK;
}

static int8_t d_rssi(void)
{
    return -55;
}

static esp_err_t d_scan_start(void)
{
    static const espos_wifi_scan_entry_t fake[] = {
        { .ssid = "Marina-Guest", .bssid = { 1, 2, 3, 4, 5, 6 }, .rssi = -70, .channel = 1, .authmode = 0 },
        { .ssid = "Boat", .bssid = { 0xde, 0xad, 0xbe, 0xef, 0, 1 }, .rssi = -48, .channel = 6, .authmode = 3 },
    };
    espos_wifi_scan_done(fake, 2);
    return ESP_OK;
}

static esp_err_t d_set_ps(const char *mode)
{
    (void)mode;
    return ESP_OK;
}

static const espos_wifi_driver_t k_driver = {
    .init = d_init,
    .deinit = d_deinit,
    .sm_port = &k_sm_port,
    .rssi = d_rssi,
    .scan_start = d_scan_start,
    .set_ps = d_set_ps,
    .portal_ip = "192.168.4.1",
};

const espos_wifi_driver_t *espos_wifi_driver(void)
{
    return &k_driver;
}

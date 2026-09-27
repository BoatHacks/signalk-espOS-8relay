/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * /api/v1/wifi/status, /api/v1/wifi/scan and the captive-portal probes.
 */
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"

#include "espos_httpd.h"
#include "espos_wifi.h"
#include "espos_wifi_priv.h"

static esp_err_t status_get(httpd_req_t *req)
{
    char *json = NULL;
    esp_err_t err = espos_wifi_status_json(&json);
    if (err != ESP_OK || !json) {
        return espos_httpd_send_error(req, "500 Internal Server Error", "status_failed", esp_err_to_name(err));
    }
    err = espos_httpd_send_json(req, NULL, json);
    free(json);
    return err;
}

static esp_err_t scan_get(httpd_req_t *req)
{
    char *json = NULL;
    esp_err_t err = espos_wifi_scan_json(&json);
    if (err != ESP_OK || !json) {
        return espos_httpd_send_error(req, "500 Internal Server Error", "scan_failed", esp_err_to_name(err));
    }
    err = espos_httpd_send_json(req, NULL, json);
    free(json);
    return err;
}

static esp_err_t scan_post(httpd_req_t *req)
{
    if (!espos_httpd_require_json(req)) {
        return ESP_OK;
    }
    esp_err_t err = espos_wifi_scan_start();
    if (err == ESP_OK) {
        return espos_httpd_send_json(req, "202 Accepted", "{\"status\":\"scanning\"}");
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return espos_httpd_send_error(req, "409 Conflict", "busy", "cannot scan right now (connecting?)");
    }
    return espos_httpd_send_error(req, "500 Internal Server Error", "scan_failed", esp_err_to_name(err));
}

/* Captive-portal detection probes from phones/laptops: answer with a
 * redirect to the setup page so the OS pops its "sign in" sheet. */
static esp_err_t captive_redirect(httpd_req_t *req)
{
    char location[64];
    const espos_wifi_driver_t *drv = espos_wifi_driver();
    snprintf(location, sizeof(location), "http://%s/", drv->portal_ip ? drv->portal_ip : "192.168.4.1");
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "redirecting to setup", HTTPD_RESP_USE_STRLEN);
}

esp_err_t espos_wifi_register_api(void)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/v1/wifi/status", .method = HTTP_GET, .handler = status_get },
        { .uri = "/api/v1/wifi/scan", .method = HTTP_GET, .handler = scan_get },
        { .uri = "/api/v1/wifi/scan", .method = HTTP_POST, .handler = scan_post },
        /* Android / Chrome OS */
        { .uri = "/generate_204", .method = HTTP_GET, .handler = captive_redirect },
        { .uri = "/gen_204", .method = HTTP_GET, .handler = captive_redirect },
        /* Apple */
        { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_redirect },
        { .uri = "/library/test/success.html", .method = HTTP_GET, .handler = captive_redirect },
        /* Windows */
        { .uri = "/connecttest.txt", .method = HTTP_GET, .handler = captive_redirect },
        { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_redirect },
        { .uri = "/redirect", .method = HTTP_GET, .handler = captive_redirect },
        /* Firefox / misc */
        { .uri = "/canonical.html", .method = HTTP_GET, .handler = captive_redirect },
        { .uri = "/success.txt", .method = HTTP_GET, .handler = captive_redirect },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        /* The captive-portal probes are what a phone fetches before it has any
         * credentials; they must answer without a key. Everything under
         * /api/ stays behind the device key like every other endpoint. */
        bool probe = strncmp(uris[i].uri, "/api/", 5) != 0;
        esp_err_t err = espos_httpd_register_ex(&uris[i], probe ? ESPOS_HTTPD_PUBLIC : ESPOS_HTTPD_PROTECTED);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

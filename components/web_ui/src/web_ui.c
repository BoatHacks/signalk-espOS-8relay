#include "web_ui.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "espos_httpd.h"
#include "freertos/FreeRTOS.h"
#include "web_ui_logic.h"

static const char *TAG = "web_ui";

#define API_PATH "/api/v1/relays"

extern const char relays_html_start[] asm("_binary_relays_html_start");
extern const char relays_html_end[] asm("_binary_relays_html_end");

static const web_ui_io_t *s_io;
// The config is a few hundred bytes copied in and out; a spinlock keeps
// that valid from any task, including before web_ui_start().
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static device_config_t s_cfg;
static const char *s_last_source[BOARD_CHANNELS];
static int64_t s_last_change_us[BOARD_CHANNELS];

void web_ui_relay_changed(uint8_t channel, const char *source)
{
    if (channel < 1 || channel > BOARD_CHANNELS) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_mux);
    s_last_source[channel - 1] = source;
    s_last_change_us[channel - 1] = now;
    taskEXIT_CRITICAL(&s_mux);
}

void web_ui_update_config(const device_config_t *cfg)
{
    taskENTER_CRITICAL(&s_mux);
    s_cfg = *cfg;
    taskEXIT_CRITICAL(&s_mux);
}

static esp_err_t send_state(httpd_req_t *req)
{
    device_config_t *cfg = malloc(sizeof(*cfg));
    if (!cfg) {
        return espos_httpd_send_error(req, "500 Internal Server Error", "no_memory", "out of memory");
    }
    web_ui_view_t view = {
        .cfg = cfg,
        .relay_mask = s_io->relay_mask(),
        .input_mask = s_io->input_mask(),
        .inputs_ready = s_io->inputs_ready(),
    };
    const int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_mux);
    *cfg = s_cfg;
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        view.last_source[i] = s_last_source[i];
        view.last_change_ago_s[i] = (uint32_t)((now - s_last_change_us[i]) / 1000000);
    }
    taskEXIT_CRITICAL(&s_mux);
    char *json = web_ui_state_json(&view);
    free(cfg);
    if (!json) {
        return espos_httpd_send_error(req, "500 Internal Server Error", "no_memory", "out of memory");
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = espos_httpd_send_json(req, NULL, json);
    free(json);
    return err;
}

// Reads {"on": bool}. On failure a response has been sent; returns false.
static bool read_on(httpd_req_t *req, bool *on, esp_err_t *ret)
{
    *ret = ESP_OK;
    if (!espos_httpd_require_json(req)) {
        return false;
    }
    char *body = NULL;
    size_t len = 0;
    esp_err_t err = espos_httpd_read_body(req, &body, &len);
    if (err != ESP_OK) {
        *ret = ESP_FAIL;  // 413 already sent, or the socket is gone
        return false;
    }
    const bool ok = web_ui_parse_on(body, on);
    free(body);
    if (!ok) {
        espos_httpd_send_error(req, "400 Bad Request", "bad_body", "expected {\"on\": true|false}");
    }
    return ok;
}

static esp_err_t set_failed(httpd_req_t *req, esp_err_t err)
{
    ESP_LOGW(TAG, "relay command failed: %s", esp_err_to_name(err));
    return espos_httpd_send_error(req, "503 Service Unavailable", "relay_failed",
                                  "the relay chip did not respond; see the health page");
}

static esp_err_t get_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    // EMBED_TXTFILES adds a NUL that isn't part of the page.
    return httpd_resp_send(req, relays_html_start, relays_html_end - relays_html_start - 1);
}

static esp_err_t get_state(httpd_req_t *req)
{
    return send_state(req);
}

static esp_err_t put_one(httpd_req_t *req)
{
    const uint8_t ch = web_ui_parse_channel(req->uri, API_PATH);
    if (ch == 0) {
        return espos_httpd_send_error(req, "404 Not Found", "no_such_relay", "relays are numbered 1-8");
    }
    bool on;
    esp_err_t ret;
    if (!read_on(req, &on, &ret)) {
        return ret;
    }
    esp_err_t err = s_io->set_relay(ch, on);
    if (err != ESP_OK) {
        return set_failed(req, err);
    }
    return send_state(req);
}

static esp_err_t put_all(httpd_req_t *req)
{
    bool on;
    esp_err_t ret;
    if (!read_on(req, &on, &ret)) {
        return ret;
    }
    // Every relay is attempted even if one fails.
    esp_err_t first = ESP_OK;
    for (uint8_t ch = 1; ch <= BOARD_CHANNELS; ch++) {
        esp_err_t err = s_io->set_relay(ch, on);
        if (err != ESP_OK && first == ESP_OK) {
            first = err;
        }
    }
    if (first != ESP_OK) {
        return set_failed(req, first);
    }
    return send_state(req);
}

esp_err_t web_ui_start(const web_ui_io_t *io, const device_config_t *cfg)
{
    s_io = io;
    web_ui_update_config(cfg);
    const struct {
        httpd_uri_t uri;
        uint32_t flags;
    } routes[] = {
        {{.uri = "/relays", .method = HTTP_GET, .handler = get_page}, ESPOS_HTTPD_PUBLIC},
        {{.uri = API_PATH, .method = HTTP_GET, .handler = get_state}, ESPOS_HTTPD_PROTECTED},
        {{.uri = API_PATH, .method = HTTP_PUT, .handler = put_all}, ESPOS_HTTPD_PROTECTED},
        {{.uri = API_PATH "/*", .method = HTTP_PUT, .handler = put_one}, ESPOS_HTTPD_PROTECTED},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        esp_err_t err = espos_httpd_register_ex(&routes[i].uri, routes[i].flags);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register %s: %s", routes[i].uri.uri, esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(TAG, "relay page at /relays");
    return ESP_OK;
}

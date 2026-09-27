/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * Captive-portal DNS: answers every A query with the portal's own address
 * so a phone joining the setup AP lands on the setup page whatever it asks
 * for. Minimal by design (RFC 1035 header + one question).
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "espos_dns";

static TaskHandle_t s_task;
static volatile bool s_enabled;
static uint32_t s_answer_ip;   /* network byte order */

/* One task for the life of the process: it stays bound to :53 and simply
 * ignores queries while the portal is down. Nothing here ever blocks the
 * caller (which may be the timer daemon or the event task). */
static void dns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket: %d", errno);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind 53: %d", errno);
        close(sock);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    uint8_t buf[512];
    for (;;) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf) - 16, 0, (struct sockaddr *)&from, &flen);
        if (n < 12 || !s_enabled) {
            continue;
        }
        /* only standard queries with at least one question */
        if ((buf[2] & 0x80) || (buf[4] == 0 && buf[5] == 0)) {
            continue;
        }
        /* walk the first QNAME */
        int i = 12;
        while (i < n && buf[i] != 0) {
            i += buf[i] + 1;
        }
        i += 1 + 4; /* NUL + QTYPE + QCLASS */
        if (i > n) {
            continue;
        }
        uint16_t qtype = (uint16_t)((buf[i - 4] << 8) | buf[i - 3]);
        /* response: copy header+question, set flags, one answer for A queries */
        buf[2] = 0x81; /* QR=1, RD copied */
        buf[3] = 0x80; /* RA=1, RCODE 0 */
        buf[6] = 0;
        buf[7] = (qtype == 1) ? 1 : 0; /* ANCOUNT */
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        int len = i;
        if (qtype == 1) {
            uint8_t ans[] = { 0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x04 };
            memcpy(buf + len, ans, sizeof(ans));
            len += sizeof(ans);
            memcpy(buf + len, &s_answer_ip, 4);
            len += 4;
        }
        sendto(sock, buf, len, 0, (struct sockaddr *)&from, flen);
    }
}

esp_err_t espos_wifi_portal_dns_start(const char *ip)
{
    s_answer_ip = inet_addr(ip);
    s_enabled = true;
    if (s_task) {
        return ESP_OK;
    }
    if (xTaskCreate(dns_task, "espos_dns", 3072, NULL, tskIDLE_PRIORITY + 3, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void espos_wifi_portal_dns_stop(void)
{
    s_enabled = false;
}

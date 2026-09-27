// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
/*
 * Liveness watchdog for the esp_hosted co-processor link (e.g. ESP32-P4
 * host + ESP32-C6 radio over SDIO).
 *
 * The failure this exists for: the transport dies while the host keeps
 * believing WiFi is fine. The symptom is a flood of
 *
 *     rpc_core: Timeout waiting for Resp for [0x126](Req_WifiStaGetApInfo)
 *
 * and nothing else — the whole RPC channel is gone, so the host cannot
 * even ask whether it is connected. esp_hosted declares
 * ESP_HOSTED_EVENT_TRANSPORT_FAILURE for faults it detects itself (a
 * dropped SDIO read, an all-ones PKT_LEN), but a silently wedged link
 * produces no event at all: there is nothing to detect, only an absence.
 *
 * The co-processor heartbeat supplies that missing signal. It arrives
 * over the same RPC channel that dies, so its absence IS the fault
 * detector. Miss enough of them and the link is gone regardless of what
 * the WiFi state machine believes.
 *
 * Recovery is a deliberate esp_restart(), not a transport re-init: the
 * esp_hosted_deinit()/init() pair asserts instead of failing when it cannot
 * re-allocate its SDIO pool (recover() below, and docs/wifi.md, "Why a
 * restart and not a transport re-init"). Detection is the valuable half: a
 * device that restarts 60 s after its link dies beats one that sits
 * unreachable until someone power-cycles it.
 */

#include "sdkconfig.h"

#if defined(CONFIG_ESP_HOSTED_ENABLED)

#include "espos_wifi.h"

#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_hosted_event.h"
#include "esp_hosted_misc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include <inttypes.h>

static const char *TAG = "espos_hostedwd";

/* The co-processor emits a heartbeat every HEARTBEAT_SEC. Allow several
 * to go missing before acting: a busy link, a scan, or a burst of traffic
 * can delay one, and re-initing the transport under a healthy link would
 * be its own outage. Three intervals is late enough to be certain and
 * still well inside the ~180 s an application-level watchdog would take. */
#define HEARTBEAT_SEC      20
#define MISSED_BEATS_LIMIT 3
#define TIMEOUT_US         ((int64_t)HEARTBEAT_SEC * MISSED_BEATS_LIMIT * 1000000)

static esp_timer_handle_t s_timer;
static uint32_t s_last_beat;
static bool s_seen_beat;

static void arm_timer(void)
{
    if (!s_timer) {
        return;
    }
    if (esp_timer_is_active(s_timer)) {
        esp_timer_restart(s_timer, TIMEOUT_US);
    } else {
        esp_timer_start_once(s_timer, TIMEOUT_US);
    }
}

/* Runs on the esp_timer task.
 *
 * Deliberately NOT esp_hosted_deinit()/init(): that pair asserts rather
 * than returning an error when it cannot re-allocate. A failed re-init
 * panics on whatever task called it —
 *
 *     assert failed: sdio_mempool_create sdio_drv.c:258 (buf_mp_g)
 *
 * observed on a panel doing exactly this, taking down the esp_timer
 * task. The transport is already broken at this point, so the recovery
 * must not have a failure mode of its own; a deliberate restart is the
 * one path that always works.
 *
 * Detection is the valuable half. A device that reboots 60 s after the
 * link dies is strictly better than one that sits unreachable until
 * someone power-cycles it, which is what happened before this existed. */
static void recover(void *arg)
{
    (void)arg;
    ESP_LOGE(TAG, "no co-processor heartbeat for %d s — the radio link is gone, restarting",
             HEARTBEAT_SEC * MISSED_BEATS_LIMIT);
    /* esp_restart() is safe from the timer task and always succeeds;
     * that is the whole point of choosing it over a re-init that can
     * assert. The log line above is the only breadcrumb explaining the
     * restart, so emit it before going down. */
    esp_restart();
}

static void on_hosted_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    switch (id) {
    case ESP_HOSTED_EVENT_CP_HEARTBEAT: {
        const esp_hosted_event_heartbeat_t *e = (const esp_hosted_event_heartbeat_t *)data;
        /* A gap in the sequence means beats were lost but the link
         * recovered on its own — worth seeing in the log ring when
         * diagnosing a flaky slot, not worth acting on. */
        if (s_seen_beat && e->heartbeat != s_last_beat + 1) {
            ESP_LOGW(TAG, "heartbeat gap: expected %" PRIu32 ", got %" PRIu32,
                     s_last_beat + 1, e->heartbeat);
        }
        s_last_beat = e->heartbeat;
        s_seen_beat = true;
        arm_timer();
        break;
    }
    case ESP_HOSTED_EVENT_TRANSPORT_FAILURE:
        /* esp_hosted found the fault itself. With
         * CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE=y it restarts
         * the system and we never get here; with it disabled, recover
         * now instead of waiting out the heartbeat timeout. */
        /* With CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE=y (the
         * default espOS keeps) esp_hosted restarts the system itself and
         * we never reach here. With it disabled, act now rather than
         * waiting out the heartbeat timeout. */
        ESP_LOGE(TAG, "transport failure reported — restarting");
        esp_restart();
        break;
    case ESP_HOSTED_EVENT_CP_INIT:
        /* The co-processor restarted underneath us, so its heartbeat
         * config went with it and no beat will ever arrive again.
         *
         * Re-enabling it here would mean an RPC from the event loop
         * task — blocking, on a link that has just proved unreliable,
         * which is exactly the mistake that made a wedged transport
         * panic the UI task. Restart instead: a co-processor that
         * reset under a running host is not a state worth nursing, and
         * the heartbeat is re-enabled cleanly on the next boot. The
         * timer is left running, so if this event ever fires spuriously
         * on a healthy link the beats keep it disarmed. */
        ESP_LOGE(TAG, "co-processor restarted underneath us — restarting");
        esp_restart();
        break;
    default:
        break;
    }
}

esp_err_t espos_wifi_hosted_watchdog_start(void)
{
    if (s_timer) {
        return ESP_OK;
    }
    const esp_timer_create_args_t args = {
        .callback = recover,
        .name = "hostedwd",
        .dispatch_method = ESP_TIMER_TASK,
    };
    esp_err_t err = esp_timer_create(&args, &s_timer);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_register(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID,
                                     on_hosted_event, NULL);
    if (err != ESP_OK) {
        /* No stop needed: the timer was only just created and no handler
         * is registered yet, so nothing can have armed it. */
        esp_timer_delete(s_timer);
        s_timer = NULL;
        return err;
    }
    /* Blocking RPC, but this runs once from espos_wifi_start() on the
     * app task at boot, where the link is known good and blocking is
     * expected. Never call it from the event loop or a UI task. */
    err = esp_hosted_configure_heartbeat(true, HEARTBEAT_SEC);
    if (err != ESP_OK) {
        /* Not fatal: WiFi works, we just cannot see it wedge. Unwind
         * fully so a later retry actually retries — leaving s_timer set
         * would make the next call return ESP_OK with detection off,
         * which is worse than failing. */
        ESP_LOGW(TAG, "co-processor heartbeat unavailable (%s) — "
                      "wedge detection is OFF",
                 esp_err_to_name(err));
        /* Unregister BEFORE touching the timer: the handler is already
         * live, and a heartbeat arriving mid-teardown would call
         * arm_timer() on a handle we are about to delete.
         *
         * Stop before delete: esp_timer_delete() returns
         * ESP_ERR_INVALID_STATE for an armed timer, so a delete that
         * silently failed would leak a timer that still fires recover()
         * — restarting a device whose detection we just reported OFF.
         * esp_timer_stop() on an idle timer is a harmless no-op. */
        esp_event_handler_unregister(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID, on_hosted_event);
        esp_timer_stop(s_timer);
        esp_err_t derr = esp_timer_delete(s_timer);
        if (derr != ESP_OK) {
            /* Should not happen now, but leaving s_timer set is the
             * safer failure: start() then returns early instead of
             * creating a second timer over a leaked one. */
            ESP_LOGE(TAG, "esp_timer_delete: %s", esp_err_to_name(derr));
            return err;
        }
        s_timer = NULL;
        return err;
    }
    arm_timer();
    ESP_LOGI(TAG, "watching co-processor heartbeat (%d s, act after %d missed)",
             HEARTBEAT_SEC, MISSED_BEATS_LIMIT);
    return ESP_OK;
}

uint32_t espos_wifi_hosted_recoveries(void)
{
    /* Always 0 on a hosted build: recovery is a restart, so a RAM
     * counter cannot survive to report it. Kept so the API is uniform;
     * the restart itself is visible as reset_reason plus the
     * "radio link is gone" line in the log ring. */
    return 0;
}

#endif /* CONFIG_ESP_HOSTED_ENABLED */

#if !defined(CONFIG_ESP_HOSTED_ENABLED)

/* Native-radio and simulator builds: the API exists so callers never
 * need an #ifdef, but there is no co-processor to watch. */
#include "espos_wifi.h"

esp_err_t espos_wifi_hosted_watchdog_start(void) { return ESP_ERR_NOT_SUPPORTED; }
uint32_t espos_wifi_hosted_recoveries(void) { return 0; }

#endif

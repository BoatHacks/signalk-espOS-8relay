/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * GET /api/v1/n2k — what the CAN bus is actually doing.
 *
 * This exists because of a question that could not be answered: the panel's
 * NMEA 2000 bus went quiet, its candump socket accepted a connection and
 * then delivered nothing for thirty seconds, and there was no way to tell
 * from the network whether the bus was unplugged, the bitrate was wrong, the
 * driver had never started, or the boat was simply silent. The counters that
 * distinguish those already existed inside the node; nothing exposed them,
 * and the health condition reports "normal" for a bus that has never spoken
 * as well as for a healthy one.
 *
 * Reading the answer:
 *
 *   running=false          the driver never came up. Pins, or a failed
 *                          twai_new_node_onchip. Nothing else matters.
 *   frames=0 errors=0      nothing on the wire at all: unplugged, unpowered,
 *                          or a bus where nobody is transmitting.
 *   frames=0 errors rising the bus is wired but not understood. ack_err on
 *                          its own means nothing else is listening;
 *                          stuff/form errors usually mean the bitrate is
 *                          wrong.
 *   frames rising          the bus works. If a client still sees nothing,
 *                          the problem is downstream of the receiver.
 *   dropped rising         frames arrive faster than they are consumed;
 *                          raise CONFIG_ESPOS_N2K_RX_QUEUE_DEPTH.
 */
#include <cstdio>

#include "espos_n2k/twai_receiver.h"

#if __has_include("espos_httpd.h")
#include "cJSON.h"
#include "espos_httpd.h"

namespace {

const espos_n2k::TwaiReceiver* s_rx = nullptr;

esp_err_t get_n2k(httpd_req_t* req) {
  cJSON* root = cJSON_CreateObject();
  if (!root) {
    return espos_httpd_send_error(req, "500 Internal Server Error", "no_memory",
                                  "out of memory");
  }

  if (!s_rx) {
    /* The component is in the build but the application never handed us a
     * receiver, so there is nothing to report -- said plainly rather than
     * with zeroes that would read as "a silent bus". */
    cJSON_AddBoolToObject(root, "present", false);
  } else {
    cJSON_AddBoolToObject(root, "present", true);
    cJSON_AddBoolToObject(root, "running", s_rx->bus_running());
    cJSON_AddBoolToObject(root, "ever_received", s_rx->ever_received());

    /* INT64_MAX is what the receiver returns when nothing has ever arrived;
     * null is the honest JSON for that, not a number a client would plot. */
    if (s_rx->ever_received()) {
      cJSON_AddNumberToObject(root, "idle_s",
                              (double)s_rx->seconds_since_last_rx());
    } else {
      cJSON_AddNullToObject(root, "idle_s");
    }

    cJSON_AddNumberToObject(root, "frames", (double)s_rx->frames_received());
    cJSON_AddNumberToObject(root, "dropped", (double)s_rx->frames_dropped());
    cJSON_AddNumberToObject(root, "errors", (double)s_rx->error_count());
    cJSON_AddNumberToObject(root, "bus_off", (double)s_rx->bus_off_count());

    /* Decoded as well as raw: "ack_err" is the one an operator can act on
     * (nothing else is listening), and a hex word is not. */
    const uint32_t f = s_rx->last_error_flags();
    cJSON* e = cJSON_AddObjectToObject(root, "last_error");
    if (e) {
      cJSON_AddNumberToObject(e, "flags", (double)f);
      cJSON_AddBoolToObject(e, "arb_lost", (f & 0x1) != 0);
      cJSON_AddBoolToObject(e, "bit_err", (f & 0x2) != 0);
      cJSON_AddBoolToObject(e, "form_err", (f & 0x4) != 0);
      cJSON_AddBoolToObject(e, "stuff_err", (f & 0x8) != 0);
      cJSON_AddBoolToObject(e, "ack_err", (f & 0x10) != 0);
    }
  }

  char* json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!json) {
    return espos_httpd_send_error(req, "500 Internal Server Error", "no_memory",
                                  "out of memory");
  }
  esp_err_t err = espos_httpd_send_json(req, nullptr, json);
  cJSON_free(json);
  return err;
}

}  // namespace

extern "C" esp_err_t espos_n2k_api_register(const void* receiver) {
  s_rx = static_cast<const espos_n2k::TwaiReceiver*>(receiver);
  static const httpd_uri_t uri = {
      .uri = "/api/v1/n2k",
      .method = HTTP_GET,
      .handler = get_n2k,
      .user_ctx = nullptr,
  };
  return espos_httpd_register(&uri);
}

#else /* no espos_httpd in this build */

extern "C" esp_err_t espos_n2k_api_register(const void* receiver) {
  (void)receiver;
  return ESP_ERR_NOT_SUPPORTED;
}

#endif

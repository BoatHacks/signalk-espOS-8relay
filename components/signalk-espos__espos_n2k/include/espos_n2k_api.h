/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_n2k's REST surface: GET /api/v1/n2k, the CAN bus's own diagnostics.
 *
 * C, and takes a void *, so a plain-C application can register the endpoint
 * without the C++ receiver type crossing the ABI (docs/development.md,
 * "Public API rules"). Pass the TwaiReceiver.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register GET /api/v1/n2k, reporting `receiver`'s counters.
 *
 * Call after espos_httpd_start() and after the receiver exists; the receiver
 * must outlive the server, which the usual `static` at file scope gives.
 * ESP_ERR_NOT_SUPPORTED when the firmware has no espos_httpd.
 *
 * The document answers the question a candump socket cannot: whether the
 * driver is up, whether anything has ever arrived, how long ago, and whether
 * the controller is seeing bus errors it cannot make frames out of.
 */
esp_err_t espos_n2k_api_register(const void* receiver);

#ifdef __cplusplus
}
#endif

/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#include "espos_n2k/twai_transmitter.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "twai_node.h"

namespace espos_n2k {

namespace {
constexpr const char* kTag = "twai_tx";
}

TwaiTransmitter::TwaiTransmitter(size_t tx_queue_depth)
    : tx_queue_depth_(tx_queue_depth) {}

TwaiTransmitter::~TwaiTransmitter() { stop(); }

void TwaiTransmitter::start() {
  if (running_.exchange(true)) return;

  // No pins: the bus belongs to whoever configured it, which in practice is
  // the receiver. That was implicit before too — twai_transmit() worked only
  // once somebody had called twai_driver_install() — but it failed silently.
  detail::TwaiNodeConfig cfg;
  cfg.tx_queue_depth = tx_queue_depth_;
  if (detail::TwaiNode::instance().acquire(cfg) != ESP_OK) {
    ESP_LOGE(kTag,
             "the CAN bus is not up — start the receiver (which owns the "
             "pins and bitrate) before the transmitter");
    running_.store(false);
    return;
  }
  ESP_LOGI(kTag, "TWAI transmitter started");
}

void TwaiTransmitter::stop() {
  if (!running_.exchange(false)) return;
  detail::TwaiNode::instance().release();
}

void TwaiTransmitter::set(const CanMessage& msg) {
  if (!running_.load()) {
    tx_fail_count_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  // Straight into the driver's own queue rather than through one of ours:
  // esp_twai queues internally (tx_queue_depth), so the task and queue this
  // class used to run were a second copy of the same thing. Timeout 0 keeps
  // set() non-blocking and drops when full, which is what it did before.
  esp_err_t err = detail::TwaiNode::instance().transmit(msg.frame, 0);
  if (err == ESP_OK) {
    last_tx_us_.store(esp_timer_get_time(), std::memory_order_relaxed);
  } else {
    tx_fail_count_.fetch_add(1, std::memory_order_relaxed);
    ESP_LOGD(kTag, "TX failed: %s", esp_err_to_name(err));
  }
}

}  // namespace espos_n2k

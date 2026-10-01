/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef COCKPIT_N2K_TWAI_TRANSMITTER_H_
#define COCKPIT_N2K_TWAI_TRANSMITTER_H_

#include <atomic>
#include <cstdint>

#include "esp_timer.h"

#include "espos_n2k/can_frame.h"

namespace espos_n2k {

/// Accepts CanMessage values and transmits them on the CAN bus.
///
/// The bus itself belongs to the receiver, which configures the pins and the
/// bitrate; start the receiver first. Queueing is the driver's — esp_twai has
/// a transmit queue of its own, so this class no longer runs a task.
class TwaiTransmitter {
 public:
  explicit TwaiTransmitter(size_t tx_queue_depth = 32);
  ~TwaiTransmitter();

  void start();
  void stop();

  /// ValueConsumer interface — queues a frame for transmission. Does not
  /// block: a full transmit queue drops the frame and counts a failure.
  void set(const CanMessage& msg);

  /// True if we have queued at least one frame since boot. (Queued, not
  /// acknowledged on the wire — the driver reports that asynchronously and
  /// this class does not subscribe to it.)
  bool ever_transmitted() const {
    return last_tx_us_.load(std::memory_order_relaxed) != 0;
  }

  /// Seconds since the last frame was queued. INT64_MAX if none yet.
  /// Replaces the old uint32_t tx_count_ which would overflow at ~20
  /// days of busy traffic.
  int64_t seconds_since_last_tx() const {
    int64_t last = last_tx_us_.load(std::memory_order_relaxed);
    if (last == 0) return INT64_MAX;
    return (esp_timer_get_time() - last) / 1000000;
  }

  uint32_t tx_fail_count() const { return tx_fail_count_; }

 private:
  size_t tx_queue_depth_;
  std::atomic<bool> running_{false};
  // Microseconds-since-boot of last successful TX; 0 = nothing yet.
  std::atomic<int64_t> last_tx_us_{0};
  std::atomic<uint32_t> tx_fail_count_{0};
};

}  // namespace espos_n2k

#endif  // COCKPIT_N2K_TWAI_TRANSMITTER_H_

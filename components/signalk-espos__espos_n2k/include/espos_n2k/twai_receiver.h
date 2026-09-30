/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef COCKPIT_N2K_TWAI_RECEIVER_H_
#define COCKPIT_N2K_TWAI_RECEIVER_H_

#include <atomic>
#include <cstdint>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <functional>
#include "espos_n2k/can_frame.h"

namespace espos_n2k {

struct TwaiReceiverConfig {
  /// No default: which pins carry CAN is a property of the board, and a
  /// number that exists on one target does not on another (esp32c3 has no
  /// GPIO 22 at all). The application must say. GPIO_NUM_NC leaves the
  /// receiver unstarted with an explicit log line rather than binding
  /// whatever pin the number happens to mean here.
  gpio_num_t tx_pin = GPIO_NUM_NC;
  gpio_num_t rx_pin = GPIO_NUM_NC;
  uint32_t bitrate = 250000;  // NMEA 2000 standard
  size_t rx_queue_depth = CONFIG_ESPOS_N2K_RX_QUEUE_DEPTH;
};

/// Reads CAN frames from the TWAI peripheral and emits them as CanMessage
/// values, on the shared node's RX task.
class TwaiReceiver {
 public:
  using FrameFn = std::function<void(const CanMessage&)>;
  explicit TwaiReceiver(const TwaiReceiverConfig& config = {});
  ~TwaiReceiver();

  void start();
  void stop();

  /// Called on the receiver task for every frame. Set before start().
  void set_on_frame(FrameFn fn) { on_frame_ = std::move(fn); }

  /// True if we have received at least one frame since boot.
  bool ever_received() const {
    return last_rx_us_.load(std::memory_order_relaxed) != 0;
  }

  /// Seconds since the last received frame. Returns INT64_MAX if no
  /// frame has ever been received. esp_timer_get_time() returns int64
  /// microseconds since boot — overflows in ~292,000 years, so no
  /// rollover concerns. (Was rx_count_ uint32 which overflowed at
  /// ~20 days of busy N2K traffic.)
  int64_t seconds_since_last_rx() const {
    int64_t last = last_rx_us_.load(std::memory_order_relaxed);
    if (last == 0) return INT64_MAX;
    return (esp_timer_get_time() - last) / 1000000;
  }

  /// Bus-off events counted by the shared node since it came up.
  uint32_t bus_off_count() const;

  /// Is the shared TWAI node up? False means the driver never started (bad
  /// pins, a failed twai_new_node_onchip) -- which from a candump socket
  /// looks exactly like a bus with nothing on it.
  bool bus_running() const;

  /// Frames the ISR accepted, and frames it had to drop because the queue
  /// was full. A rising drop count is a consumer that cannot keep up, not a
  /// bus problem.
  uint32_t frames_received() const;
  uint32_t frames_dropped() const;

  /// Bus errors seen by the controller, and the flags of the most recent
  /// one (arb_lost, bit_err, form_err, stuff_err, ack_err -- IDF's
  /// twai_error_flags_t). Errors rising while frames stay at zero is the
  /// signature of a bus that is wired but wrong: an ack error on every
  /// transmission means nothing else is listening, and a stuff or form
  /// error storm usually means the bitrate does not match.
  uint32_t error_count() const;
  uint32_t last_error_flags() const;

 private:
  static void sink(void* ctx, const CanMessage& msg);

  TwaiReceiverConfig config_;
  FrameFn on_frame_;
  std::atomic<bool> running_{false};
  // Microseconds-since-boot of last RX frame; 0 = nothing received yet.
  std::atomic<int64_t> last_rx_us_{0};
};

}  // namespace espos_n2k

#endif  // COCKPIT_N2K_TWAI_RECEIVER_H_

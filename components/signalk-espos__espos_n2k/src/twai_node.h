/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ESPOS_N2K_SRC_TWAI_NODE_H_
#define ESPOS_N2K_SRC_TWAI_NODE_H_

/// The one TWAI node, shared by the receiver and the transmitter.
///
/// IDF 6's esp_twai API allocates a *node* and hands back a handle, where the
/// old driver/twai.h API installed a process-wide singleton that any caller
/// could reach through twai_receive()/twai_transmit(). TwaiReceiver and
/// TwaiTransmitter were written against that singleton and are separate
/// objects with separate lifetimes, so somebody has to own the handle now.
/// This is that somebody: a reference-counted holder that behaves the way the
/// old global driver did — the first user to start configures the bus, the
/// last one to stop tears it down.
///
/// Internal to the component. Nothing here is part of the public API.

#include <atomic>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "espos_n2k/can_frame.h"

namespace espos_n2k {
namespace detail {

struct TwaiNodeConfig {
  gpio_num_t tx_pin = GPIO_NUM_NC;
  gpio_num_t rx_pin = GPIO_NUM_NC;
  uint32_t bitrate = 250000;
  size_t rx_queue_depth = CONFIG_ESPOS_N2K_RX_QUEUE_DEPTH;
  size_t tx_queue_depth = 32;
};

class TwaiNode {
 public:
  using FrameSink = void (*)(void* ctx, const CanMessage& msg);

  static TwaiNode& instance();

  /// Start (or join) the bus. The first caller's config wins; a later caller
  /// with a different one gets a warning, not a silently reconfigured bus.
  /// Reference-counted against release().
  esp_err_t acquire(const TwaiNodeConfig& config);
  void release();

  bool running() const { return refs_.load() > 0; }

  /// Frames are delivered on the node's own task, never from the ISR.
  void set_sink(FrameSink sink, void* ctx);

  esp_err_t transmit(const CanFrame& frame, int timeout_ms);

  uint32_t bus_off_count() const { return bus_off_count_.load(); }

  /* Counters for the status endpoint. A bus that is wired but silent, one
   * that is not wired at all, and one whose driver never started all look
   * identical from the network without these -- which is exactly the
   * position the panel was in when its N2K bus went quiet and there was no
   * USB cable to ask. */
  uint32_t frames_received() const { return frames_rx_.load(); }
  uint32_t frames_dropped() const { return frames_dropped_.load(); }
  uint32_t error_count() const { return error_count_.load(); }
  uint32_t last_error_flags() const { return last_error_flags_.load(); }

 private:
  TwaiNode() = default;

  static bool on_rx_done(twai_node_handle_t node,
                         const twai_rx_done_event_data_t* edata, void* ctx);
  static bool on_state_change(twai_node_handle_t node,
                              const twai_state_change_event_data_t* edata,
                              void* ctx);
  static bool on_error(twai_node_handle_t node,
                       const twai_error_event_data_t* edata, void* ctx);
  static bool on_tx_done(twai_node_handle_t node,
                         const twai_tx_done_event_data_t* edata, void* ctx);
  static void rx_task(void* arg);

  void teardown();

  twai_node_handle_t node_ = nullptr;
  TwaiNodeConfig config_;
  std::atomic<int> refs_{0};
  std::atomic<bool> task_running_{false};
  /// Set from the state-change ISR, acted on by the task: twai_node_recover()
  /// is not safe to call from an ISR.
  std::atomic<bool> recover_pending_{false};
  std::atomic<uint32_t> bus_off_count_{0};
  std::atomic<uint32_t> frames_rx_{0};
  std::atomic<uint32_t> frames_dropped_{0};
  std::atomic<uint32_t> error_count_{0};
  std::atomic<uint32_t> last_error_flags_{0};

  /// The driver keeps a pointer to each queued frame and to its payload
  /// until it has been sent, so every frame handed to twai_node_transmit()
  /// lives in one of these slots, not on transmit()'s stack. A slot's bit in
  /// tx_in_use_ is set by transmit() (under tx_lock_) and cleared by
  /// on_tx_done(). The frame must stay the first member: on_tx_done() finds
  /// the slot from the frame pointer the driver hands back.
  struct TxSlot {
    twai_frame_t frame;
    uint8_t data[kCanMaxData];
  };
  static constexpr size_t kMaxTxSlots = 32;
  TxSlot tx_slots_[kMaxTxSlots] = {};
  size_t tx_slot_count_ = 0;
  std::atomic<uint32_t> tx_in_use_{0};
  SemaphoreHandle_t tx_lock_ = nullptr;

  QueueHandle_t rx_queue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t lock_ = nullptr;

  /// Read by the task, written by set_sink(); a pointer pair small enough
  /// that a torn read is not possible on any target espOS builds for.
  std::atomic<FrameSink> sink_{nullptr};
  std::atomic<void*> sink_ctx_{nullptr};
};

}  // namespace detail
}  // namespace espos_n2k

#endif  // ESPOS_N2K_SRC_TWAI_NODE_H_

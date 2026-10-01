/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef COCKPIT_N2K_CANDUMP_TCP_SERVER_H_
#define COCKPIT_N2K_CANDUMP_TCP_SERVER_H_

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "espos_n2k/can_frame.h"
#include "espos_n2k/twai_receiver.h"
#include "espos_n2k/twai_transmitter.h"

namespace espos_n2k {

struct CandumpTcpServerConfig {
  uint16_t port = CONFIG_ESPOS_N2K_CANDUMP_PORT;
  uint8_t max_clients = 3;
  const char* interface_name = "can0";
};

/// TCP server that streams candump ASCII to connected clients and
/// accepts inbound candump lines for transmission on the CAN bus.
class CandumpTcpServer {
 public:
  CandumpTcpServer(TwaiReceiver* receiver, TwaiTransmitter* transmitter,
                   const CandumpTcpServerConfig& config = {});
  ~CandumpTcpServer();

  void start();
  void stop();

  uint32_t connected_clients() const { return connected_clients_; }

 private:
  static void server_task(void* arg);
  static void client_task(void* arg);

  // Called on the receiver task for every frame — fans out to per-client
  // queues.
  void on_frame(const CanMessage& msg);
  // mDNS registration, retried from the server task until mDNS is up.
  void advertise();
  bool advertised_ = false;

  TwaiReceiver* receiver_;
  TwaiTransmitter* transmitter_;
  CandumpTcpServerConfig config_;

  TaskHandle_t server_task_ = nullptr;
  // Set by the server task as its last act, waited on by stop(). A fixed
  // vTaskDelay was a guess at how long the loop takes; this is the answer.
  std::atomic<bool> server_task_done_{false};
  std::atomic<bool> running_{false};
  std::atomic<uint32_t> connected_clients_{0};

  // Per-client queues for fan-out.
  static constexpr int kMaxClients = 8;
  QueueHandle_t client_queues_[kMaxClients] = {};
  SemaphoreHandle_t clients_mutex_ = nullptr;
};

}  // namespace espos_n2k

#endif  // COCKPIT_N2K_CANDUMP_TCP_SERVER_H_

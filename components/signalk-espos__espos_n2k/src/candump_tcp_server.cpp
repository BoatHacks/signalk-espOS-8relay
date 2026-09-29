/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#include "espos_n2k/candump_tcp_server.h"

#include <cstring>

#include "mdns.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"

#include "espos_n2k/candump_format.h"
#include <cstdlib>  // malloc, free

namespace espos_n2k {

namespace {
constexpr const char* kTag = "candump_srv";

// The service type and the "model" tag stay `sensesp-n2k` by default even
// though this component no longer has anything to do with SensESP: they are
// on the wire, and SignalK servers already browse for them. Renaming would
// make every existing gateway invisible to every existing client for the
// sake of tidiness -- so the new names are opt-in (Kconfig, "espOS NMEA 2000").
#if CONFIG_ESPOS_N2K_MDNS_LEGACY_TYPE
constexpr const char* kMdnsServiceType = "_sensesp-n2k";
constexpr const char* kMdnsModel = "sensesp-n2k-gateway";
#else
constexpr const char* kMdnsServiceType = "_espos-n2k";
constexpr const char* kMdnsModel = "espos-n2k-gateway";
#endif

struct ClientContext {
  CandumpTcpServer* server;
  int sock;
  int slot;  // index into client_queues_
};
}  // namespace

CandumpTcpServer::CandumpTcpServer(TwaiReceiver* receiver,
                                   TwaiTransmitter* transmitter,
                                   const CandumpTcpServerConfig& config)
    : receiver_(receiver), transmitter_(transmitter), config_(config) {
  clients_mutex_ = xSemaphoreCreateMutex();
}

CandumpTcpServer::~CandumpTcpServer() {
  stop();
  if (clients_mutex_) vSemaphoreDelete(clients_mutex_);
}

void CandumpTcpServer::on_frame(const CanMessage& msg) {
  if (xSemaphoreTake(clients_mutex_, pdMS_TO_TICKS(5)) != pdTRUE) return;
  for (int i = 0; i < kMaxClients; i++) {
    if (client_queues_[i]) {
      // Non-blocking — drop if client can't keep up.
      xQueueSend(client_queues_[i], &msg, 0);
    }
  }
  xSemaphoreGive(clients_mutex_);
}

void CandumpTcpServer::start() {
  if (running_.exchange(true)) return;

  // Cleared before the task exists, so a restart does not see the previous
  // run's completion and return from stop() immediately.
  server_task_done_.store(false, std::memory_order_release);

  // Subscribe to TwaiReceiver's output.
  receiver_->set_on_frame([this](const CanMessage& m) { this->on_frame(m); });

  // Checked: an unstarted server task is a gateway that accepts nothing and
  // says nothing, and the caller has no other way to find out. Undo the
  // subscription too -- leaving it in place would have every received frame
  // fan out to a server that will never serve it.
  if (xTaskCreate(&CandumpTcpServer::server_task, "candump_srv", 4096, this, 3,
                  &server_task_) != pdPASS) {
    ESP_LOGE(kTag, "could not create the candump server task -- not started");
    receiver_->set_on_frame(nullptr);
    server_task_ = nullptr;
    running_.store(false);
    return;
  }
  ESP_LOGI(kTag, "Candump TCP server starting on port %u", config_.port);

  // Advertise via mDNS so canboatjs / SignalK Server can auto-discover the
  // gateway on the LAN. mDNS is started by espOS once WiFi is up, so the
  // server task retries the registration until it sticks (see serve()).
}

void CandumpTcpServer::advertise() {
  if (advertised_) return;
  mdns_txt_item_t txt[] = {
      {"txtvers", "1"},
      {"format", "candump3"},
      {"iface", config_.interface_name},
      {"model", kMdnsModel},
  };
  esp_err_t err =
      mdns_service_add(NULL, kMdnsServiceType, "_tcp", config_.port, txt, 4);
  if (err == ESP_OK) {
    advertised_ = true;
    ESP_LOGI(kTag, "Advertising mDNS service %s._tcp on port %u",
             kMdnsServiceType, config_.port);
  } else if (err !=
             ESP_ERR_INVALID_STATE) {  // INVALID_STATE = mdns not started yet
    advertised_ = true;                // do not spam on a hard failure
    ESP_LOGW(kTag, "mdns_service_add failed: %s", esp_err_to_name(err));
  }
}

void CandumpTcpServer::stop() {
  if (!running_.exchange(false)) return;

  // Stop feeding the fan-out before anything is torn down: on_frame() walks
  // client_queues_, and the receiver's task is not this one.
  if (receiver_) receiver_->set_on_frame(nullptr);

  // Wait for the server task to actually finish rather than assuming it has.
  // The old fixed 200 ms was shorter than one pass of the accept loop (a
  // 100 ms select plus whatever an accept does), so stop() could return
  // while the task was still walking members of an object the caller was
  // about to destroy -- and every destructor calls stop().
  //
  // Bounded: a task that does not finish is a bug worth a loud line, not a
  // reason to hang the caller forever.
  if (server_task_) {
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
    while (!server_task_done_.load(std::memory_order_acquire) &&
           xTaskGetTickCount() < deadline) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!server_task_done_.load(std::memory_order_acquire)) {
      ESP_LOGE(kTag, "server task did not finish within 2 s");
    }
    server_task_ = nullptr;
  }

  // Withdraw the mDNS advertisement. It was added on start and never
  // removed, so a stopped gateway kept answering browses and a client that
  // trusted discovery got a connection refused instead of no answer at all.
  if (advertised_) {
    esp_err_t err = mdns_service_remove(kMdnsServiceType, "_tcp");
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
      ESP_LOGW(kTag, "mdns_service_remove failed: %s", esp_err_to_name(err));
    }
    advertised_ = false;
  }
}

void CandumpTcpServer::server_task(void* arg) {
  auto* self = static_cast<CandumpTcpServer*>(arg);

  int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_sock < 0) {
    ESP_LOGE(kTag, "socket() failed: %d", errno);
    self->server_task_done_.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
    return;
  }

  int opt = 1;
  setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(self->config_.port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    ESP_LOGE(kTag, "bind() failed: %d", errno);
    close(listen_sock);
    self->server_task_done_.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
    return;
  }

  if (listen(listen_sock, self->config_.max_clients) != 0) {
    ESP_LOGE(kTag, "listen() failed: %d", errno);
    close(listen_sock);
    self->server_task_done_.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
    return;
  }

  ESP_LOGI(kTag, "Listening on port %u", self->config_.port);

  while (self->running_.load()) {
    // Accept with a timeout so we can check running_.
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(listen_sock, &fds);

    int sel = select(listen_sock + 1, &fds, nullptr, nullptr, &tv);
    self->advertise();  // no-op once registered / after mDNS is up
    if (sel <= 0) continue;

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_sock =
        accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
    if (client_sock < 0) continue;

    // Find a free slot, within the CONFIGURED limit.
    //
    // The scan used to run to kMaxClients (8) and max_clients (default 3) was
    // only the listen() backlog, so the server admitted eight clients while
    // its configuration said three. Each costs a 128-entry queue plus a 4 KB
    // stack -- more than double the footprint the config promised, silently.
    //
    // xQueueCreate is checked: a null queue would take the slot and then
    // drop every frame, which reads as a working client that receives
    // nothing.
    const int limit = self->config_.max_clients < kMaxClients
                          ? self->config_.max_clients
                          : kMaxClients;
    int slot = -1;
    if (xSemaphoreTake(self->clients_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
      for (int i = 0; i < limit; i++) {
        if (self->client_queues_[i] == nullptr) {
          self->client_queues_[i] = xQueueCreate(128, sizeof(CanMessage));
          if (self->client_queues_[i] != nullptr) {
            slot = i;
          }
          break;
        }
      }
      xSemaphoreGive(self->clients_mutex_);
    }

    if (slot < 0) {
      ESP_LOGW(kTag, "Max clients reached, rejecting connection");
      close(client_sock);
      continue;
    }

    char ip_str[16];
    inet_ntoa_r(client_addr.sin_addr, ip_str, sizeof(ip_str));
    ESP_LOGI(kTag, "Client connected from %s (slot %d)", ip_str, slot);
    self->connected_clients_.fetch_add(1, std::memory_order_relaxed);

    auto* ctx = new ClientContext{self, client_sock, slot};
    // Checked: on failure the socket, the context and the queue all leaked,
    // and the slot was never released -- so a device that hit this once
    // permanently lost one of its three client slots, and after three it
    // accepted nobody until it was rebooted.
    if (xTaskCreate(&CandumpTcpServer::client_task, "candump_cli", 4096, ctx, 3,
                    nullptr) != pdPASS) {
      ESP_LOGE(kTag, "could not create the client task -- dropping slot %d",
               slot);
      delete ctx;
      close(client_sock);
      self->connected_clients_.fetch_sub(1, std::memory_order_relaxed);
      if (xSemaphoreTake(self->clients_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (self->client_queues_[slot]) {
          vQueueDelete(self->client_queues_[slot]);
          self->client_queues_[slot] = nullptr;
        }
        xSemaphoreGive(self->clients_mutex_);
      }
    }
  }

  close(listen_sock);
  self->server_task_done_.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

void CandumpTcpServer::client_task(void* arg) {
  auto* ctx = static_cast<ClientContext*>(arg);
  auto* self = ctx->server;
  int sock = ctx->sock;
  int slot = ctx->slot;
  QueueHandle_t queue = self->client_queues_[slot];

  // Set socket to non-blocking for the TX side so we can interleave
  // reading inbound lines (RX from client → TX to CAN bus).
  struct timeval rcv_tv = {.tv_sec = 0, .tv_usec = 50000};  // 50ms read
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &rcv_tv, sizeof(rcv_tv));
  // Bounded write timeout — if TCP buffer is full (slow client / congested
  // SDIO link), drop the frame rather than blocking the candump task.
  // Blocking here causes SDIO RX queue overflow on the host side.
  struct timeval snd_tv = {.tv_sec = 0, .tv_usec = 20000};  // 20ms write
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &snd_tv, sizeof(snd_tv));

  char line_buf[128];
  int line_pos = 0;
  char encode_buf[128];
  uint32_t dropped_tx = 0;
  TickType_t last_drop_log = 0;

  // TX batching: pack many candump lines into a single send() to amortise
  // TCP/IP + WiFi 802.11 overhead. Flushes on full buffer or 20ms timeout,
  // whichever comes first. Drops the whole batch on EAGAIN (rare) to avoid
  // blocking the candump task and stalling the SDIO drain.
  //
  // NOTE: buffer is heap-allocated, NOT on stack — the candump_cli task
  // has a 4KB stack and a 2.5KB on-stack buffer overflows it (causes
  // "Guru Meditation Error: Stack protection fault" within ~30s).
  constexpr int kTxBufSize = 2560;      // ~50 candump lines per flush
  constexpr int kFlushIntervalMs = 20;  // max latency added per line
  static_assert(kTxBufSize >= 128, "must fit one max-length line");
  char* tx_buf = static_cast<char*>(malloc(kTxBufSize));
  if (!tx_buf) {
    ESP_LOGE("candump_srv", "slot %d: failed to alloc TX buffer", slot);
    close(sock);
    // Clean up the queue + client-slot accounting like the disconnect path,
    // then exit the task. Avoid `goto` past the lambda declaration below
    // (would skip its initialization — C++ compile error).
    if (xSemaphoreTake(self->clients_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (self->client_queues_[slot]) {
        vQueueDelete(self->client_queues_[slot]);
        self->client_queues_[slot] = nullptr;
      }
      xSemaphoreGive(self->clients_mutex_);
    }
    self->connected_clients_.fetch_sub(1, std::memory_order_relaxed);
    delete ctx;
    vTaskDelete(nullptr);
    return;  // unreachable; suppresses warning
  }
  int tx_len = 0;
  TickType_t last_flush = xTaskGetTickCount();

  auto flush_tx = [&]() -> bool {
    if (tx_len == 0) return true;
    int sent = send(sock, tx_buf, tx_len, MSG_NOSIGNAL);
    if (sent < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        dropped_tx += tx_len;  // approximate: byte count, not frame count
        if (xTaskGetTickCount() - last_drop_log > pdMS_TO_TICKS(5000)) {
          ESP_LOGW("candump_srv",
                   "slot %d: dropped %lu bytes (slow client / WiFi)", slot,
                   (unsigned long)dropped_tx);
          last_drop_log = xTaskGetTickCount();
        }
        tx_len = 0;
        last_flush = xTaskGetTickCount();
        return true;
      }
      return false;
    }
    // A PARTIAL send is the normal case on a socket whose buffer is nearly
    // full, not an error: send() returns how many bytes it took, which can be
    // fewer than asked for. Treating any non-negative return as "all of it"
    // dropped the tail mid-line, and the next flush appended onto that
    // fragment -- so a client like canboatjs did not lose a frame cleanly, it
    // parsed a corrupt CAN id built from the end of one line and the start of
    // another.
    //
    // The buffer holds whole newline-terminated candump lines, so recovery is
    // to discard forward to a line boundary: keep the bytes after the last
    // newline inside what was sent, drop the partial line, and carry the rest
    // to the next flush. A reader then sees a gap in the stream, which is what
    // a dropped frame should look like.
    if (sent < tx_len) {
      // candump_resync_offset() decides where to resume: the byte after the
      // next newline, so the half-sent line is discarded rather than having
      // the next frame appended to it. Pure and host-tested.
      const int keep_from = static_cast<int>(candump_resync_offset(
          tx_buf, static_cast<size_t>(tx_len), static_cast<size_t>(sent)));
      const int discarded = keep_from - sent;
      if (discarded > 0) {
        dropped_tx += static_cast<unsigned long>(discarded);
        if (xTaskGetTickCount() - last_drop_log > pdMS_TO_TICKS(5000)) {
          ESP_LOGW("candump_srv",
                   "slot %d: partial send, dropped %d bytes to the next line "
                   "(total %lu)",
                   slot, discarded, (unsigned long)dropped_tx);
          last_drop_log = xTaskGetTickCount();
        }
      }
      tx_len -= keep_from;
      if (tx_len > 0) memmove(tx_buf, tx_buf + keep_from, (size_t)tx_len);
      last_flush = xTaskGetTickCount();
      return true;
    }
    tx_len = 0;
    last_flush = xTaskGetTickCount();
    return true;
  };

  while (self->running_.load()) {
    // 1. Drain queued frames → encode into batch buffer, flush when full.
    CanMessage msg;
    while (xQueueReceive(queue, &msg, 0) == pdTRUE) {
      int n = candump_encode(msg, self->config_.interface_name, encode_buf,
                             sizeof(encode_buf));
      if (n > 0) {
        if (tx_len + n > kTxBufSize) {
          if (!flush_tx()) goto disconnect;
        }
        memcpy(tx_buf + tx_len, encode_buf, n);
        tx_len += n;
      }
    }

    // 2. Time-based flush so per-frame latency stays bounded even on a
    //    quiet bus or a slow trickle.
    if (tx_len > 0 &&
        xTaskGetTickCount() - last_flush >= pdMS_TO_TICKS(kFlushIntervalMs)) {
      if (!flush_tx()) goto disconnect;
    }

    // 2. Read inbound data from client (non-blocking, 50ms timeout).
    char recv_buf[128];
    int recv_len = recv(sock, recv_buf, sizeof(recv_buf), 0);
    if (recv_len > 0) {
      // Accumulate into line_buf, parse complete lines.
      for (int i = 0; i < recv_len; i++) {
        if (recv_buf[i] == '\n' || line_pos >= (int)sizeof(line_buf) - 1) {
          line_buf[line_pos] = '\0';
          // CanFrame default-initialises every field, but keep the
          // explicit {} — this used to be twai_message_t, whose flag bits
          // live in a union that candump_decode() only partly filled, and
          // the stack garbage in the rest reached the driver.
          CanMessage tx_msg = {};
          if (candump_decode(line_buf, &tx_msg) && self->transmitter_) {
            self->transmitter_->set(tx_msg);
          }
          line_pos = 0;
        } else {
          line_buf[line_pos++] = recv_buf[i];
        }
      }
    } else if (recv_len == 0) {
      // Client disconnected cleanly.
      goto disconnect;
    }
    // recv_len < 0 with EAGAIN/EWOULDBLOCK is normal (timeout).

    // Brief yield if no data in either direction.
    vTaskDelay(pdMS_TO_TICKS(1));
  }

disconnect:
  ESP_LOGI(kTag, "Client disconnected (slot %d)", slot);
  if (tx_buf) free(tx_buf);
  close(sock);

  // Free the per-client queue.
  if (xSemaphoreTake(self->clients_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (self->client_queues_[slot]) {
      vQueueDelete(self->client_queues_[slot]);
      self->client_queues_[slot] = nullptr;
    }
    xSemaphoreGive(self->clients_mutex_);
  }
  self->connected_clients_.fetch_sub(1, std::memory_order_relaxed);

  delete ctx;
  vTaskDelete(nullptr);
}

}  // namespace espos_n2k

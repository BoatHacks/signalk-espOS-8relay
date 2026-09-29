#include "n2k_bridge.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "NMEA2000.h"
#include "N2kMsg.h"
#include "board.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "espos_n2k/twai_receiver.h"
#include "espos_n2k/twai_transmitter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "espos_n2k_api.h"
#include "freertos/semphr.h"
#include "n2k_alerts.h"
#include "nvs.h"
#include "switch_bank_pgn.h"

namespace {

const char *TAG = "n2k";

// canboat: device class 30 "Electrical Distribution", function 140 "Load
// Controller". 2046 is the manufacturer code the NMEA2000 library's own
// examples use for non-certified devices; no manufacturer holds it.
constexpr unsigned char kDeviceClass = 30;
constexpr unsigned char kDeviceFunction = 140;
constexpr uint16_t kManufacturerCode = 2046;
constexpr unsigned char kIndustryMarine = 4;
constexpr unsigned char kDefaultAddress = 34;

// canboat gives no interval for 127501; switch banks commonly repeat it
// every 2 s besides sending on change.
constexpr uint32_t kStatusPeriodMs = 2000;
constexpr uint32_t kLoopMs = 10;

constexpr unsigned char kPriority = 3;
constexpr unsigned char kAlertPriority = 2;  // canboat's for the alert PGNs
constexpr uint32_t kOpenTimeoutMs = 5000;  // report a CAN bus that won't open
const unsigned long kTransmitPgns[] = {SWITCH_BANK_PGN_STATUS, N2K_ALERT_PGN, N2K_ALERT_PGN_TEXT, 0};
// Extended at n2k_bridge_start() with the position PGNs when
// position_source=n2k (issue #9, plan 16) -- ExtendReceiveMessages() keeps
// this pointer for the library's lifetime, so it must be static storage,
// not built on the stack. Sized for the worst case: control + alert
// response + both position PGNs + the 0 terminator.
unsigned long g_receive_pgns[5] = {SWITCH_BANK_PGN_CONTROL, N2K_ALERT_PGN_RESPONSE, 0, 0, 0};

struct MsgListener {
  n2k_bridge_msg_listener_t cb = nullptr;
  void *arg = nullptr;
};
MsgListener g_msg_listeners[N2K_BRIDGE_MAX_MSG_LISTENERS];

// Status for the relay page, written by the NMEA 2000 task, read anywhere.
std::atomic<TickType_t> g_last_rx_tick{0};
std::atomic<bool> g_rx_seen{false};
std::atomic<bool> g_started{false};
std::atomic<uint8_t> g_address{0};

// tNMEA2000 over espOS's TWAI receiver and transmitter. espOS owns the CAN
// peripheral; its candump server is not started because the receiver has a
// single frame callback, which this class takes.
class EsposN2k : public tNMEA2000 {
 public:
  // For espOS's GET /api/v1/n2k diagnostics (frames, drops, bus errors).
  const void *receiver() const { return &rx_; }

  EsposN2k()
      : rx_(espos_n2k::TwaiReceiverConfig{.tx_pin = static_cast<gpio_num_t>(BOARD_CAN_TX),
                                          .rx_pin = static_cast<gpio_num_t>(BOARD_CAN_RX)}) {}

 protected:
  bool CANOpen() override {
    queue_ = xQueueCreate(64, sizeof(espos_n2k::CanFrame));
    if (!queue_) return false;
    // Runs on the receiver's task: hand the frame over, never block.
    rx_.set_on_frame([this](const espos_n2k::CanMessage &m) { xQueueSend(queue_, &m.frame, 0); });
    rx_.start();
    tx_.start();
    return true;
  }

  bool CANSendFrame(unsigned long id, unsigned char len, const unsigned char *buf, bool) override {
    espos_n2k::CanMessage m;
    m.frame.id = id;
    m.frame.extended = true;
    m.frame.dlc = len > espos_n2k::kCanMaxData ? espos_n2k::kCanMaxData : len;
    memcpy(m.frame.data, buf, m.frame.dlc);
    tx_.set(m);  // queues without blocking; a full queue is counted by espOS
    return true;
  }

  bool CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf) override {
    espos_n2k::CanFrame f;
    if (!queue_ || xQueueReceive(queue_, &f, 0) != pdTRUE) return false;
    g_last_rx_tick.store(xTaskGetTickCount());
    g_rx_seen.store(true);
    id = f.id;
    len = f.dlc;
    memcpy(buf, f.data, f.dlc);
    return true;
  }

 private:
  espos_n2k::TwaiReceiver rx_;
  espos_n2k::TwaiTransmitter tx_;
  QueueHandle_t queue_ = nullptr;
};

struct State {
  EsposN2k *bus = nullptr;
  n2k_bridge_io_t io{};
  uint8_t relay_bank = 0;
  uint8_t input_bank = 0;
  bool inputs_on = false;
  std::atomic<bool> changed{true};
  nvs_handle_t nvs = 0;
  // Input alarms as alerts (plan 21). `alerts` belongs to the NMEA 2000
  // task; settings reach it through `pending_cfg`, under `cfg_lock`.
  n2k_alerts_t alerts{};
  SemaphoreHandle_t cfg_lock = nullptr;
  n2k_alerts_cfg_t pending_cfg{};
  std::atomic<bool> cfg_changed{false};
} s;

uint32_t now_ms() { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }

void send_alert_pgn(unsigned long pgn, const uint8_t *data, size_t len) {
  tN2kMsg msg;
  msg.SetPGN(pgn);
  msg.Priority = kAlertPriority;
  for (size_t i = 0; i < len; i++) msg.AddByte(data[i]);
  s.bus->SendMsg(msg);
}

void send_alert_status(const uint8_t *data, size_t len, void *) { send_alert_pgn(N2K_ALERT_PGN, data, len); }
void send_alert_text(const uint8_t *data, size_t len, void *) { send_alert_pgn(N2K_ALERT_PGN_TEXT, data, len); }

const n2k_alerts_out_t kAlertsOut = {send_alert_status, send_alert_text, nullptr};

void send_status(uint8_t instance, uint8_t mask) {
  uint8_t payload[SWITCH_BANK_PAYLOAD_LEN];
  switch_bank_encode_status(instance, mask, BOARD_CHANNELS, payload);
  tN2kMsg msg;
  msg.SetPGN(SWITCH_BANK_PGN_STATUS);
  msg.Priority = kPriority;
  for (uint8_t b : payload) msg.AddByte(b);
  s.bus->SendMsg(msg);
}

void send_all() {
  send_status(s.relay_bank, s.io.relay_mask());
  if (s.inputs_on && s.io.inputs_ready()) {
    send_status(s.input_bank, s.io.input_mask());
  }
}

void on_message(const tN2kMsg &msg) {
  // Raw-message listeners (position decoding and any future use) see every
  // PGN, whether or not switch_bank itself cares about it -- switch_bank
  // doesn't know what they mean, it just owns the one bus the library will
  // only hand a single message handler for.
  for (auto &l : g_msg_listeners) {
    if (l.cb) l.cb(static_cast<uint32_t>(msg.PGN), msg.Data, static_cast<uint8_t>(msg.DataLen), l.arg);
  }
  if (msg.PGN == N2K_ALERT_PGN_RESPONSE) {
    // Sent on the task's next alerts tick.
    n2k_alerts_on_response(&s.alerts, msg.Data, msg.DataLen);
    return;
  }
  if (msg.PGN != SWITCH_BANK_PGN_CONTROL) return;
  switch_bank_command_t cmd;
  if (!switch_bank_decode_control(msg.Data, msg.DataLen, s.relay_bank, BOARD_CHANNELS, &cmd)) return;
  for (uint8_t ch = 1; ch <= BOARD_CHANNELS; ch++) {
    const uint32_t bit = 1u << (ch - 1);
    if (cmd.on & bit) s.io.set_relay(ch, true);
    if (cmd.off & bit) s.io.set_relay(ch, false);
  }
}

void n2k_task(void *) {
  TickType_t last_status = 0;
  const TickType_t started = xTaskGetTickCount();
  bool reported = false;
  for (;;) {
    // Also opens the bus: the library retries Open() here until it is open.
    s.bus->ParseMessages();
    if (!reported && s.bus->IsOpen()) {
      reported = true;
      g_started.store(true);
      ESP_LOGI(TAG, "on the bus: relay bank %u, input bank %u%s", s.relay_bank, s.input_bank,
               s.inputs_on ? "" : " (not sent: same id as relays)");
    } else if (!reported && xTaskGetTickCount() - started > pdMS_TO_TICKS(kOpenTimeoutMs)) {
      reported = true;  // once; the library keeps retrying every second
      ESP_LOGE(TAG, "CAN bus did not open within %u ms; still retrying", (unsigned)kOpenTimeoutMs);
    }
    if (!s.bus->IsOpen()) {
      vTaskDelay(pdMS_TO_TICKS(kLoopMs));
      continue;
    }
    g_address.store(s.bus->GetN2kSource());
    if (s.bus->ReadResetAddressChanged()) {
      // Come back on the same address next boot, as the standard expects.
      nvs_set_u8(s.nvs, "addr", s.bus->GetN2kSource());
      nvs_commit(s.nvs);
    }
    const TickType_t now = xTaskGetTickCount();
    if (s.changed.exchange(false) || now - last_status >= pdMS_TO_TICKS(kStatusPeriodMs)) {
      send_all();
      last_status = now;
    }
    if (s.cfg_changed.exchange(false)) {
      xSemaphoreTake(s.cfg_lock, portMAX_DELAY);
      n2k_alerts_set_config(&s.alerts, &s.pending_cfg);
      xSemaphoreGive(s.cfg_lock);
    }
    n2k_alerts_tick(&s.alerts, s.io.inputs_ready(), s.io.input_mask(), now_ms(), &kAlertsOut);
    vTaskDelay(pdMS_TO_TICKS(kLoopMs));
  }
}

}  // namespace

extern "C" esp_err_t n2k_bridge_start(const n2k_bridge_io_t *io, const device_config_t *cfg) {
  s.io = *io;
  s.relay_bank = cfg->bank_id;
  s.input_bank = cfg->input_bank_id;
  s.inputs_on = device_config_input_bank_usable(cfg);

  // Position PGNs only join the receive list when position_source=n2k
  // (issue #9, plan 16): no point asking the library to hand us messages
  // nothing will decode. restart_required in the setting's schema, since
  // this list is fixed once ExtendReceiveMessages() below has run.
  int pgn_idx = 2;
  if (cfg->position_source == POSITION_SRC_N2K) {
    g_receive_pgns[pgn_idx++] = 129025;
    g_receive_pgns[pgn_idx++] = 129029;
  }
  g_receive_pgns[pgn_idx] = 0;

  esp_err_t err = nvs_open("n2k", NVS_READWRITE, &s.nvs);
  if (err != ESP_OK) return err;
  uint8_t address = kDefaultAddress;
  nvs_get_u8(s.nvs, "addr", &address);

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BASE);
  // NAME unique number: 21 bits, from the chip's MAC.
  const uint32_t unique = ((uint32_t)(mac[3] & 0x1F) << 16) | (mac[4] << 8) | mac[5];
  char serial[16];
  snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  s.bus = new EsposN2k();
  s.bus->SetProductInformation(serial, 100, "signalk-espOS-8relay", esp_app_get_description()->version,
                               "Waveshare ESP32-S3-ETH-8DI-8RO-C");
  s.bus->SetDeviceInformation(unique, kDeviceFunction, kDeviceClass, kManufacturerCode, kIndustryMarine);
  s.bus->SetMode(tNMEA2000::N2km_NodeOnly, address);
  s.bus->EnableForward(false);
  s.bus->ExtendTransmitMessages(kTransmitPgns);
  s.bus->ExtendReceiveMessages(g_receive_pgns);
  s.bus->SetMsgHandler(on_message);
  s.cfg_lock = xSemaphoreCreateMutex();
  if (!s.cfg_lock) return ESP_ERR_NO_MEM;
  n2k_alerts_cfg_t alerts_cfg;
  n2k_alerts_cfg_from(cfg, &alerts_cfg);
  // The NAME is fixed from here on (address claim changes only the address).
  n2k_alerts_init(&s.alerts, &alerts_cfg, s.input_bank, s.bus->GetDeviceInformation().GetName(), now_ms());
  // Not Open() here: the library only opens once a millisecond has passed
  // since the object was made (OpenScheduler.FromNow(0) is checked with
  // `>`), so a quick first Open() returns false without trying, and 0.0.9
  // gave up on NMEA 2000 for good that way. The task's ParseMessages()
  // opens it, retrying until it succeeds.
  g_address.store(address);
  if (xTaskCreate(n2k_task, "n2k", 4096, nullptr, 4, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
  // The CAN bus's own diagnostics: whether frames arrive at all and whether
  // the controller sees bus errors. Tells wiring faults from a silent bus.
  if (espos_n2k_api_register(s.bus->receiver()) != ESP_OK) {
    ESP_LOGW(TAG, "GET /api/v1/n2k not available");
  }
  return ESP_OK;
}

extern "C" void n2k_bridge_state_changed(void) { s.changed.store(true); }

extern "C" void n2k_bridge_update_config(const device_config_t *cfg) {
  if (!s.cfg_lock) return;  // not started
  n2k_alerts_cfg_t c;
  n2k_alerts_cfg_from(cfg, &c);
  // The input bank id needs a restart (as for 127501): keep the running one.
  c.inputs_on = s.inputs_on;
  xSemaphoreTake(s.cfg_lock, portMAX_DELAY);
  s.pending_cfg = c;
  xSemaphoreGive(s.cfg_lock);
  s.cfg_changed.store(true);
}

extern "C" void n2k_bridge_get_status(n2k_bridge_status_t *out) {
  out->started = g_started.load();
  out->address = g_address.load();
  out->traffic = g_rx_seen.load() && xTaskGetTickCount() - g_last_rx_tick.load() < pdMS_TO_TICKS(10000);
}

extern "C" esp_err_t n2k_bridge_add_msg_listener(n2k_bridge_msg_listener_t cb, void *arg) {
  for (auto &l : g_msg_listeners) {
    if (!l.cb) {
      l.cb = cb;
      l.arg = arg;
      return ESP_OK;
    }
  }
  return ESP_ERR_NO_MEM;
}

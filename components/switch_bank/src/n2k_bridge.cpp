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
const unsigned long kTransmitPgns[] = {SWITCH_BANK_PGN_STATUS, 0};
const unsigned long kReceivePgns[] = {SWITCH_BANK_PGN_CONTROL, 0};

// tNMEA2000 over espOS's TWAI receiver and transmitter. espOS owns the CAN
// peripheral; its candump server is not started because the receiver has a
// single frame callback, which this class takes.
class EsposN2k : public tNMEA2000 {
 public:
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
} s;

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
  for (;;) {
    s.bus->ParseMessages();
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
    vTaskDelay(pdMS_TO_TICKS(kLoopMs));
  }
}

}  // namespace

extern "C" esp_err_t n2k_bridge_start(const n2k_bridge_io_t *io, const device_config_t *cfg) {
  s.io = *io;
  s.relay_bank = cfg->bank_id;
  s.input_bank = cfg->input_bank_id;
  s.inputs_on = device_config_input_bank_usable(cfg);

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
  s.bus->ExtendReceiveMessages(kReceivePgns);
  s.bus->SetMsgHandler(on_message);
  if (!s.bus->Open()) {
    ESP_LOGE(TAG, "could not open the CAN bus");
    return ESP_FAIL;
  }
  if (xTaskCreate(n2k_task, "n2k", 4096, nullptr, 4, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
  ESP_LOGI(TAG, "on the bus: relay bank %u, input bank %u%s", s.relay_bank, s.input_bank,
           s.inputs_on ? "" : " (not sent: same id as relays)");
  return ESP_OK;
}

extern "C" void n2k_bridge_state_changed(void) { s.changed.store(true); }

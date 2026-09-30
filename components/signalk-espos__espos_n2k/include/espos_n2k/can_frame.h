/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ESPOS_N2K_CAN_FRAME_H_
#define ESPOS_N2K_CAN_FRAME_H_

#include <cstddef>
#include <cstdint>

/// A CAN frame as espOS sees it, deliberately not the driver's struct.
///
/// This used to be `twai_message_t` straight out of `driver/twai.h`, which
/// meant the candump encoder — pure text munging, no hardware in sight —
/// could not be compiled on the host and therefore had no tests. It also
/// meant that changing IDF's TWAI API changed the type every consumer of this
/// component names. One small struct fixes both: the driver types now appear
/// only in the two files that talk to the peripheral.
namespace espos_n2k {

/// Classic CAN, which is all NMEA 2000 uses: up to 8 data bytes.
inline constexpr size_t kCanMaxData = 8;

struct CanFrame {
  uint32_t id = 0;       ///< 11- or 29-bit arbitration ID, no flag bits
  bool extended = true;  ///< 29-bit ID; NMEA 2000 is always extended
  bool remote = false;   ///< remote-transmission request (no data)
  uint8_t dlc = 0;       ///< data bytes present, 0..kCanMaxData
  uint8_t data[kCanMaxData] = {};
};

struct CanMessage {
  CanFrame frame;
  int64_t timestamp_us = 0;  ///< esp_timer_get_time() when received
};

}  // namespace espos_n2k

#endif  // ESPOS_N2K_CAN_FRAME_H_

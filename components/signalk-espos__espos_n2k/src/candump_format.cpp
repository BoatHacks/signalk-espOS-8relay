/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#include "espos_n2k/candump_format.h"

#include <cstdio>
#include <cstdlib>  // strtoll, strtoul
#include <cstring>
#include <sys/time.h>

namespace espos_n2k {

namespace {

// One hex digit, or -1. Deliberately not isxdigit()+strtol: this is called
// per nibble on every received frame, and it must give a straight answer for
// the NUL byte so the caller can look at hash[1] without checking hash[0]
// separately.
int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

int candump_encode(const CanMessage& msg, const char* iface, char* buf,
                   size_t buf_len) {
  // Format: (seconds.microseconds) iface CANID#HEXDATA\n
  // Use wall-clock time (Unix epoch) so SignalK gets valid timestamps.
  // Falls back to uptime if NTP hasn't synced yet (time < 2020).
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  int64_t sec, usec;
  if (tv.tv_sec > 1577836800) {  // after 2020-01-01
    sec = tv.tv_sec;
    usec = tv.tv_usec;
  } else {
    sec = msg.timestamp_us / 1000000;
    usec = msg.timestamp_us % 1000000;
  }

  // Build the hex data string
  char data_hex[17];  // max 8 bytes = 16 hex chars + null
  int data_len = msg.frame.dlc;
  if (data_len > (int)kCanMaxData) data_len = (int)kCanMaxData;
  for (int i = 0; i < data_len; i++) {
    snprintf(data_hex + i * 2, 3, "%02X", msg.frame.data[i]);
  }
  data_hex[data_len * 2] = '\0';

  // CAN ID — always 8 hex digits for extended frames (NMEA 2000)
  int n = snprintf(buf, buf_len, "(%lld.%06lld) %s %08X#%s\n", (long long)sec,
                   (long long)usec, iface, (unsigned)msg.frame.id, data_hex);
  if (n < 0 || (size_t)n >= buf_len) return -1;
  return n;
}

bool candump_decode(const char* line, CanMessage* out) {
  // Parse: (seconds.microseconds) iface CANID#HEXDATA
  if (!line || !out) return false;

  // Skip leading whitespace
  while (*line == ' ' || *line == '\t') line++;

  // Parse timestamp: (sec.usec)
  //
  // Both halves are bounded before the multiply. A client is free to send
  // "(12345678903456.0)", and sec * 1000000 then overflows int64 -- signed
  // overflow is undefined behaviour, so this is not merely a wrong
  // timestamp. Found by the fuzz harness (UBSan), not by review.
  //
  // Out-of-range clamps rather than rejecting the frame: the timestamp is
  // advisory (the server restamps on arrival) and a CAN frame with a silly
  // time still carries the PGN somebody needs.
  static constexpr int64_t kMaxSec = 4000000000;  // ~2096, and sec*1e6 fits
  int64_t sec = 0, usec = 0;
  if (*line == '(') {
    line++;
    char* end;
    sec = strtoll(line, &end, 10);
    if (*end == '.') {
      end++;
      usec = strtoll(end, &end, 10);
    }
    if (*end == ')') end++;
    line = end;
  }
  if (sec < 0) sec = 0;
  if (sec > kMaxSec) sec = kMaxSec;
  if (usec < 0) usec = 0;
  if (usec > 999999) usec = 999999;
  out->timestamp_us = sec * 1000000 + usec;

  // Skip whitespace + interface name
  while (*line == ' ') line++;
  while (*line && *line != ' ') line++;  // skip iface
  while (*line == ' ') line++;

  // Parse CAN ID (hex, up to 8 digits)
  char* hash;
  unsigned long can_id = strtoul(line, &hash, 16);
  if (*hash != '#') return false;
  hash++;

  out->frame.id = can_id;
  out->frame.extended = true;  // NMEA 2000 always extended
  out->frame.remote = false;

  // Parse hex data bytes.
  //
  // Two digits per byte, and BOTH must be there. sscanf("%2x") is happy with
  // one -- it reads "A" at the end of a line as 0x0A and reports success --
  // so advancing a fixed two characters stepped over the NUL and kept
  // reading whatever followed the buffer. A candump client is a TCP peer
  // sending arbitrary bytes, so that was a heap overread reachable from the
  // network. Found by the fuzz harness, not by review.
  //
  // Checking the pair explicitly also drops the sscanf: a nibble table is
  // clearer about what is accepted and does not depend on how a libc reads a
  // width-limited conversion.
  int data_len = 0;
  while (data_len < (int)kCanMaxData) {
    const int hi = hex_nibble(hash[0]);
    if (hi < 0) {
      break;
    }
    const int lo = hex_nibble(hash[1]);  // safe: hash[0] was not the NUL
    if (lo < 0) {
      break;  // a lone digit is a truncated byte, not a byte
    }
    out->frame.data[data_len++] = (uint8_t)((hi << 4) | lo);
    hash += 2;
  }
  out->frame.dlc = (uint8_t)data_len;

  return data_len > 0;
}

size_t candump_resync_offset(const char* buf, size_t len, size_t sent) {
  if (sent >= len) return len;
  // Already at a line boundary -- the socket stopped between lines, or took
  // nothing at all. There is no partial line, so discard nothing: searching
  // forward from here would find the NEXT line's newline and throw away a
  // line that was never sent.
  if (sent == 0 || buf[sent - 1] == '\n') return sent;
  const void* nl = memchr(buf + sent, '\n', len - sent);
  // No newline left: what remains is the tail of one line and there is no
  // boundary to resume from, so all of it goes.
  if (nl == nullptr) return len;
  return static_cast<size_t>(static_cast<const char*>(nl) - buf) + 1;
}

}  // namespace espos_n2k

/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef COCKPIT_N2K_CANDUMP_FORMAT_H_
#define COCKPIT_N2K_CANDUMP_FORMAT_H_

#include <cstddef>

#include "espos_n2k/can_frame.h"

namespace espos_n2k {

/// Encode a CanMessage to candump ASCII format:
///   (1234567890.123456) vcan0 09F10203#FF00FF00FF00FF00\n
/// Returns number of bytes written (excluding null terminator),
/// or -1 if buf is too small.
int candump_encode(const CanMessage& msg, const char* iface, char* buf,
                   size_t buf_len);

/// Decode a candump ASCII line into a CanMessage.
/// Returns true on success, false on parse error.
bool candump_decode(const char* line, CanMessage* out);

/// How much of a partly-sent transmit buffer to discard.
///
/// send() on a nearly-full socket takes fewer bytes than it was offered, and
/// the tail it did not take usually ends mid-line. Continuing from exactly
/// `sent` would append the next frame onto that fragment, and a reader would
/// parse one corrupt line rather than cleanly missing a frame -- for a CAN id
/// that means a plausible wrong PGN, which is worse than a gap.
///
/// Given a buffer of newline-terminated lines and the number of bytes the
/// socket accepted, returns the offset to keep from: the byte after the first
/// newline at or beyond `sent`, or `len` when no newline follows (the whole
/// remainder is one partial line and goes). Bytes between `sent` and the
/// result are the dropped fragment.
///
/// Pure, so it is host-tested: see test/host/espos_n2k_test.
size_t candump_resync_offset(const char* buf, size_t len, size_t sent);

}  // namespace espos_n2k

#endif  // COCKPIT_N2K_CANDUMP_FORMAT_H_

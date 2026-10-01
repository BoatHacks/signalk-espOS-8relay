/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ESPOS_N2K_TWAI_MESSAGE_H_
#define ESPOS_N2K_TWAI_MESSAGE_H_

/// Compatibility header. `TwaiMessage` was a thin wrapper around the driver's
/// `twai_message_t`; it is now an alias for CanMessage, which carries the same
/// information without naming a driver type. The FIELDS changed with it —
/// `identifier`/`data_length_code`/`extd` are `id`/`dlc`/`extended` — so this
/// keeps code that names the type compiling, not code that reaches inside it.
///
/// Include espos_n2k/can_frame.h directly in new code.
#include "espos_n2k/can_frame.h"

namespace espos_n2k {

using TwaiMessage = CanMessage;

}  // namespace espos_n2k

#endif  // ESPOS_N2K_TWAI_MESSAGE_H_

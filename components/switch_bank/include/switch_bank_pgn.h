// NMEA 2000 switch-bank payloads (layouts from canboat): an instance byte
// followed by 28 two-bit fields, one per channel, least significant first.
//   127501 Binary Switch Bank Status: 0 off, 1 on, 3 unavailable.
//   127502 Switch Bank Control:       0 off, 1 on, 3 take no action.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWITCH_BANK_PGN_STATUS 127501
#define SWITCH_BANK_PGN_CONTROL 127502
#define SWITCH_BANK_PAYLOAD_LEN 8

// 127501 for a bank of `channels` (1-28) whose states are `mask` (bit n-1 =
// channel n). Channels above `channels` read as unavailable.
void switch_bank_encode_status(uint8_t instance, uint32_t mask, uint8_t channels,
                               uint8_t out[SWITCH_BANK_PAYLOAD_LEN]);

// Channels a 127502 asks to switch on and off, among the first `channels`.
typedef struct {
    uint32_t on;
    uint32_t off;
} switch_bank_command_t;

// False (and `out` untouched) when the payload is short or for another
// instance.
bool switch_bank_decode_control(const uint8_t *data, size_t len, uint8_t instance, uint8_t channels,
                                switch_bank_command_t *out);

#ifdef __cplusplus
}
#endif

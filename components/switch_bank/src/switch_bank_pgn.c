#include "switch_bank_pgn.h"

#include <string.h>

#define FIELDS 28

void switch_bank_encode_status(uint8_t instance, uint32_t mask, uint8_t channels,
                               uint8_t out[SWITCH_BANK_PAYLOAD_LEN])
{
    memset(out, 0, SWITCH_BANK_PAYLOAD_LEN);
    out[0] = instance;
    for (int i = 0; i < FIELDS; i++) {
        const uint8_t v = i >= channels ? 3 : (mask >> i) & 1;
        out[1 + i / 4] |= (uint8_t)(v << (2 * (i % 4)));
    }
}

bool switch_bank_decode_control(const uint8_t *data, size_t len, uint8_t instance, uint8_t channels,
                                switch_bank_command_t *out)
{
    if (len < SWITCH_BANK_PAYLOAD_LEN || data[0] != instance) {
        return false;
    }
    switch_bank_command_t cmd = {0, 0};
    for (int i = 0; i < channels && i < FIELDS; i++) {
        const uint8_t v = (data[1 + i / 4] >> (2 * (i % 4))) & 3;
        if (v == 1) {
            cmd.on |= 1u << i;
        } else if (v == 0) {
            cmd.off |= 1u << i;
        }
        // 2 reserved, 3 take no action
    }
    *out = cmd;
    return true;
}

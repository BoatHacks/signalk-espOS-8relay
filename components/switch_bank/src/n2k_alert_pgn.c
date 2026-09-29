#include "n2k_alert_pgn.h"

#include <string.h>

#define HEADER_LEN 16

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static uint64_t get_u64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) {
        v = (v << 8) | p[i];
    }
    return v;
}

static void encode_header(const n2k_alert_id_t *id, uint8_t *p)
{
    p[0] = (uint8_t)((id->type & 0x0F) | (id->category << 4));
    p[1] = id->system;
    p[2] = id->subsystem;
    put_u16(p + 3, id->id);
    put_u64(p + 5, id->source_name);
    p[13] = id->instance;
    p[14] = id->index;
    p[15] = id->occurrence;
}

static void decode_header(const uint8_t *p, n2k_alert_id_t *id)
{
    id->type = p[0] & 0x0F;
    id->category = p[0] >> 4;
    id->system = p[1];
    id->subsystem = p[2];
    id->id = (uint16_t)(p[3] | (p[4] << 8));
    id->source_name = get_u64(p + 5);
    id->instance = p[13];
    id->index = p[14];
    id->occurrence = p[15];
}

void n2k_alert_encode(const n2k_alert_t *a, uint8_t out[N2K_ALERT_LEN])
{
    encode_header(&a->id, out);
    out[16] = (uint8_t)((a->silenced ? 0x01 : 0) | (a->acknowledged ? 0x02 : 0) | (a->escalated ? 0x04 : 0) |
                        (a->silence_supported ? 0x08 : 0) | (a->ack_supported ? 0x10 : 0) |
                        (a->escalation_supported ? 0x20 : 0) | 0xC0);  // reserved bits set
    put_u64(out + 17, a->ack_name);
    out[25] = (uint8_t)((a->trigger & 0x0F) | (a->threshold << 4));
    out[26] = a->priority;
    out[27] = a->state;
}

// Next code point of UTF-8 `s` at *i, advancing *i; '?' for a malformed
// sequence or one outside UCS-2's range.
static uint16_t next_code_point(const char *s, size_t *i)
{
    const uint8_t c = (uint8_t)s[*i];
    int extra;
    uint32_t cp;
    if (c < 0x80) {
        (*i)++;
        return c;
    } else if ((c & 0xE0) == 0xC0) {
        extra = 1;
        cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2;
        cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3;
        cp = c & 0x07;
    } else {
        (*i)++;
        return '?';
    }
    (*i)++;
    for (int k = 0; k < extra; k++) {
        const uint8_t cc = (uint8_t)s[*i];
        if ((cc & 0xC0) != 0x80) {
            return '?';  // truncated sequence: resume at this byte
        }
        cp = (cp << 6) | (cc & 0x3F);
        (*i)++;
    }
    return cp > 0xFFFF ? '?' : (uint16_t)cp;
}

// A STRING_LAU (length byte counting itself and the control byte, control
// byte, characters) at `out`, using at most `room` bytes (>= 2). Returns the
// bytes written.
static size_t put_string(const char *s, uint8_t *out, size_t room)
{
    if (room > 255) {
        room = 255;  // the length byte's range
    }
    bool ascii = true;
    for (const char *p = s; *p; p++) {
        if ((uint8_t)*p >= 0x80) {
            ascii = false;
            break;
        }
    }
    size_t n = 2;
    if (ascii) {
        const size_t len = strlen(s);
        const size_t fit = len < room - 2 ? len : room - 2;
        memcpy(out + 2, s, fit);
        n += fit;
    } else {
        size_t i = 0;
        while (s[i] && n + 2 <= room) {
            const uint16_t cp = next_code_point(s, &i);
            out[n++] = (uint8_t)cp;
            out[n++] = (uint8_t)(cp >> 8);
        }
    }
    out[0] = (uint8_t)n;
    out[1] = ascii ? 1 : 0;
    return n;
}

size_t n2k_alert_encode_text(const n2k_alert_id_t *id, const char *description, const char *location, uint8_t *out,
                             size_t size)
{
    if (size > N2K_ALERT_TEXT_MAX_LEN) {
        size = N2K_ALERT_TEXT_MAX_LEN;
    }
    if (size < HEADER_LEN + 1 + 4) {
        return 0;
    }
    encode_header(id, out);
    out[HEADER_LEN] = 0;  // English (US)
    size_t n = HEADER_LEN + 1;
    // Leave the location at least its empty form.
    n += put_string(description ? description : "", out + n, size - n - 2);
    n += put_string(location ? location : "", out + n, size - n);
    return n;
}

bool n2k_alert_decode_response(const uint8_t *data, size_t len, n2k_alert_response_t *out)
{
    if (len < N2K_ALERT_RESPONSE_LEN) {
        return false;
    }
    decode_header(data, &out->id);
    out->ack_name = get_u64(data + 16);
    out->command = data[24] & 0x03;
    return true;
}

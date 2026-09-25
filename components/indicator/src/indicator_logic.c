#include "indicator_logic.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

indicator_state_t indicator_state(int health, bool sk_relevant, bool sk_connected)
{
    if (health >= 2) {
        return INDICATOR_ALARM;
    }
    if (health == 1) {
        return INDICATOR_WARN;
    }
    return sk_relevant && !sk_connected ? INDICATOR_NO_SIGNALK : INDICATOR_OK;
}

indicator_rgb_t indicator_color(indicator_state_t state, uint8_t brightness_pct)
{
    static const indicator_rgb_t full[] = {
        [INDICATOR_OK] = {0, 255, 0},
        [INDICATOR_NO_SIGNALK] = {0, 0, 255},
        [INDICATOR_WARN] = {255, 120, 0},
        [INDICATOR_ALARM] = {255, 0, 0},
    };
    const unsigned pct = brightness_pct > 100 ? 100 : brightness_pct;
    const indicator_rgb_t c = full[state];
    return (indicator_rgb_t){
        (uint8_t)((c.r * pct + 50) / 100),
        (uint8_t)((c.g * pct + 50) / 100),
        (uint8_t)((c.b * pct + 50) / 100),
    };
}

static const char *morse_of(char c)
{
    static const char *letters[26] = {
        ".-",   "-...", "-.-.", "-..",  ".",   "..-.", "--.",  "....", "..",   ".---", "-.-",  ".-..", "--",
        "-.",   "---",  ".--.", "--.-", ".-.", "...",  "-",    "..-",  "...-", ".--",  "-..-", "-.--", "--..",
    };
    static const char *digits[10] = {
        "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.",
    };
    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'Z') {
        return letters[c - 'A'];
    }
    if (c >= '0' && c <= '9') {
        return digits[c - '0'];
    }
    return NULL;
}

size_t morse_encode(const char *text, morse_seg_t *out, size_t max)
{
    size_t n = 0;
    uint8_t gap = 0;  // silence owed before the next element
    for (const char *p = text; *p; p++) {
        if (*p == ' ') {
            gap = n ? 7 : 0;
            continue;
        }
        const char *code = morse_of(*p);
        if (!code) {
            continue;
        }
        for (const char *e = code; *e; e++) {
            if (gap && n < max) {
                out[n++] = (morse_seg_t){false, gap};
            }
            if (n < max) {
                out[n++] = (morse_seg_t){true, *e == '-' ? 3 : 1};
            }
            gap = 1;
        }
        gap = 3;  // letter finished; a following space raises it to 7
    }
    return n;
}

void indicator_alarm_text(const char *ip, char *out, size_t size)
{
    // 0.0.0.0 is what an interface without an address reports.
    const char *dot = ip && strcmp(ip, "0.0.0.0") != 0 ? strrchr(ip, '.') : NULL;
    char *end;
    const long octet = dot ? strtol(dot + 1, &end, 10) : -1;
    if (dot && end != dot + 1 && *end == '\0' && octet >= 0 && octet <= 255) {
        snprintf(out, size, "ESP %ld", octet);
    } else {
        snprintf(out, size, "ESP");
    }
}

bool morse_tone_at(const morse_seg_t *segs, size_t n, uint32_t unit_ms, uint32_t pause_ms, uint32_t t_ms)
{
    uint32_t total = 0;
    for (size_t i = 0; i < n; i++) {
        total += segs[i].units * unit_ms;
    }
    if (total == 0) {
        return false;
    }
    t_ms %= total + pause_ms;
    for (size_t i = 0; i < n; i++) {
        const uint32_t len = segs[i].units * unit_ms;
        if (t_ms < len) {
            return segs[i].on;
        }
        t_ms -= len;
    }
    return false;  // in the pause
}

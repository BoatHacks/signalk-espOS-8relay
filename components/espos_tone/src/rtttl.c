#include "rtttl.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// Standard equal-temperament frequencies (Hz) for octave 4, c..b, A4 = 440.
static const double OCTAVE4_HZ[12] = {
    261.63, 277.18, 293.66, 311.13, 329.63, 349.23, 369.99, 392.00, 415.30, 440.00, 466.16, 493.88,
};

static int semitone_of(char letter)
{
    switch (tolower((unsigned char)letter)) {
    case 'c': return 0;
    case 'd': return 2;
    case 'e': return 4;
    case 'f': return 5;
    case 'g': return 7;
    case 'a': return 9;
    case 'b': return 11;
    default: return -1;
    }
}

static uint16_t note_freq_hz(int semitone, bool sharp, int octave)
{
    int idx = semitone + (sharp ? 1 : 0);
    int oct = octave;
    while (idx >= 12) {
        idx -= 12;
        oct++;
    }
    double hz = OCTAVE4_HZ[idx];
    int delta = oct - 4;
    while (delta > 0) {
        hz *= 2.0;
        delta--;
    }
    while (delta < 0) {
        hz /= 2.0;
        delta++;
    }
    return (uint16_t)(hz + 0.5);
}

// Reads decimal digits at *p into *out; returns whether any were read.
static bool read_digits(const char **p, int *out)
{
    const char *start = *p;
    int v = 0;
    while (isdigit((unsigned char)**p)) {
        v = v * 10 + (**p - '0');
        (*p)++;
    }
    if (*p == start) {
        return false;
    }
    *out = v;
    return true;
}

size_t rtttl_parse(const char *rtttl, rtttl_note_t *out, size_t max)
{
    if (!rtttl) {
        return 0;
    }
    // Skip the name, if any (text before the first ':').
    const char *p = rtttl;
    const char *first_colon = strchr(p, ':');
    int def_duration = 4, def_octave = 5, bpm = 63;
    if (first_colon) {
        p = first_colon + 1;
        const char *second_colon = strchr(p, ':');
        const char *defs_end = second_colon ? second_colon : p + strlen(p);
        // Defaults section: comma-separated d=/o=/b= tokens.
        const char *q = p;
        while (q < defs_end) {
            while (q < defs_end && (*q == ' ' || *q == ',')) {
                q++;
            }
            if (q >= defs_end) {
                break;
            }
            char key = (char)tolower((unsigned char)*q);
            const char *eq = q;
            while (eq < defs_end && *eq != '=') {
                eq++;
            }
            if (eq < defs_end && eq + 1 <= defs_end) {
                const char *v = eq + 1;
                int val = 0;
                bool got = false;
                while (v < defs_end && isdigit((unsigned char)*v)) {
                    val = val * 10 + (*v - '0');
                    v++;
                    got = true;
                }
                if (got) {
                    if (key == 'd') {
                        def_duration = val;
                    } else if (key == 'o') {
                        def_octave = val;
                    } else if (key == 'b') {
                        bpm = val;
                    }
                }
                q = v;
            } else {
                q = defs_end;
            }
            while (q < defs_end && *q != ',') {
                q++;
            }
        }
        p = second_colon ? second_colon + 1 : defs_end;
    }
    if (bpm <= 0 || def_duration <= 0) {
        return 0;
    }
    const double whole_note_ms = 240000.0 / bpm;

    size_t n = 0;
    while (*p && n < max) {
        while (*p == ' ' || *p == ',') {
            p++;
        }
        if (!*p) {
            break;
        }
        int duration = def_duration;
        read_digits(&p, &duration);
        char letter = *p;
        if (letter == '\0') {
            break;
        }
        const bool is_pause = tolower((unsigned char)letter) == 'p';
        const int semitone = is_pause ? 0 : semitone_of(letter);
        if (!is_pause && semitone < 0) {
            // Unrecognised token: skip to the next comma so a garbled note
            // doesn't desync the rest of the tune.
            while (*p && *p != ',') {
                p++;
            }
            continue;
        }
        p++;
        bool sharp = false;
        if (*p == '#') {
            sharp = true;
            p++;
        }
        bool dotted = false;
        if (*p == '.') {
            dotted = true;
            p++;
        }
        int octave = def_octave;
        read_digits(&p, &octave);
        if (*p == '.') {
            dotted = true;
            p++;
        }
        if (duration <= 0) {
            duration = def_duration > 0 ? def_duration : 4;
        }
        double ms = whole_note_ms / duration;
        if (dotted) {
            ms *= 1.5;
        }
        out[n].freq_hz = is_pause ? 0 : note_freq_hz(semitone, sharp, octave);
        out[n].duration_ms = (uint16_t)(ms + 0.5);
        n++;
    }
    return n;
}

uint32_t rtttl_duration_ms(const rtttl_note_t *notes, size_t n)
{
    uint32_t total = 0;
    for (size_t i = 0; i < n; i++) {
        total += notes[i].duration_ms;
    }
    return total;
}

bool rtttl_tone_at(const rtttl_note_t *notes, size_t n, uint32_t t_ms, uint16_t *freq_hz)
{
    for (size_t i = 0; i < n; i++) {
        if (t_ms < notes[i].duration_ms) {
            if (notes[i].freq_hz == 0) {
                return false;  // a rest
            }
            *freq_hz = notes[i].freq_hz;
            return true;
        }
        t_ms -= notes[i].duration_ms;
    }
    return false;  // past the end
}

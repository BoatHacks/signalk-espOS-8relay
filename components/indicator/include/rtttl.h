// A minimal RTTTL (Ring Tone Text Transfer Language) parser: turns
// "name:d=4,o=5,b=63:c,8e,g5,2p,..." into a sequence of notes the buzzer can
// play. Pure logic, no hardware.
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTTTL_MAX_NOTES 64

typedef struct {
    uint16_t freq_hz;      // 0 = rest ('p')
    uint16_t duration_ms;
} rtttl_note_t;

// Parses `rtttl`'s note section into `out` (at most `max` notes). The
// leading name and the trailing name-length limit are not returned; a
// missing/invalid default section (d=/o=/b=) falls back to RTTTL's own
// defaults (4, 5, 63). Unrecognised note tokens are skipped. Returns the
// number of notes written; 0 if the string has no playable notes at all
// (e.g. empty, or every token unrecognised).
size_t rtttl_parse(const char *rtttl, rtttl_note_t *out, size_t max);

// Sum of every note's duration.
uint32_t rtttl_duration_ms(const rtttl_note_t *notes, size_t n);

// Whether a tone should sound at `t_ms` into one (non-looping) pass, and at
// what frequency (*freq_hz unchanged if this returns false). False past the
// end of the sequence or during a rest note.
bool rtttl_tone_at(const rtttl_note_t *notes, size_t n, uint32_t t_ms, uint16_t *freq_hz);

#ifdef __cplusplus
}
#endif

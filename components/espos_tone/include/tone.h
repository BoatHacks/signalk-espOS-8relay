// A named library of short RTTTL tones (board-agnostic): parses a
// `[{"name","rtttl"}]` JSON table (an espOS table-format config column) into
// a lookup-by-name array, plus the priority rule for whether a chirp may
// start or keep playing. Pure logic, no hardware -- see rtttl.h for the
// underlying tune parser.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "rtttl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TONE_NAME_MAX 24
#define TONE_MAX_TONES 24

typedef struct {
    char name[TONE_NAME_MAX + 1];
    rtttl_note_t notes[RTTTL_MAX_NOTES];
    size_t n_notes;
} tone_t;

// Parses `json` (a table string: a JSON array of {"name","rtttl"} objects)
// into `out`. A row is skipped if its name is missing, empty or too long, or
// its RTTTL string has no playable notes. Malformed JSON parses as zero
// tones. Returns the number written (at most `max`).
size_t tone_library_parse(const char *json, tone_t *out, size_t max);

// Index of the tone named `name` in `tones[0..n)`, or -1 if `name` is empty
// or no tone in the list has that name.
int tone_library_find(const tone_t *tones, size_t n, const char *name);

// Priority order, highest first: caller-defined tiers above TONE_PRIORITY_NONE
// (e.g. a status override, a real alarm) beat an event chirp. A chirp may
// neither start nor continue while something at or above its own priority is
// active -- it is skipped/cut off, not queued or resumed once the
// higher-priority state ends. Devices with no higher-priority concept of
// their own just pass TONE_PRIORITY_NONE as `active`.
typedef enum {
    TONE_PRIORITY_NONE,
    TONE_PRIORITY_CHIRP,
    TONE_PRIORITY_HIGH,
} tone_priority_t;

// Whether a chirp/tone requested at `requested` priority may play given that
// `active` is the highest-priority thing currently sounding/shown (NONE if
// nothing is).
bool tone_priority_allowed(tone_priority_t requested, tone_priority_t active);

#ifdef __cplusplus
}
#endif

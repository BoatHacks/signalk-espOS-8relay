// One-shot playback of a parsed tone: when it started, where in it `now`
// is, and the pitch to sound. Pure logic, no hardware or clock of its own --
// the caller passes a free-running millisecond clock (wrap-safe) and drives
// its buzzer from what tone_player_step() returns (see tone_buzzer.h for an
// LEDC buzzer to drive).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tone.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    tone_t tone;
    bool playing;
    uint32_t start_ms;
    uint32_t len_ms;
} tone_player_t;

// Starts one pass of `tone` at `now_ms`, replacing anything playing. False
// (and nothing playing) if the tone has no notes.
bool tone_player_start(tone_player_t *p, const tone_t *tone, uint32_t now_ms);

// Stops at once; a no-op when idle.
void tone_player_stop(tone_player_t *p);

// Advances to `now_ms`. True while a note is sounding, with its pitch in
// *freq_hz (unchanged otherwise); false during a rest, when idle, and from
// the end of the pass on, which also stops the player (one pass, no loop).
bool tone_player_step(tone_player_t *p, uint32_t now_ms, uint16_t *freq_hz);

#ifdef __cplusplus
}
#endif

#include "tone_player.h"

bool tone_player_start(tone_player_t *p, const tone_t *tone, uint32_t now_ms)
{
    p->playing = false;
    if (tone->n_notes == 0) {
        return false;
    }
    p->tone = *tone;
    p->start_ms = now_ms;
    p->len_ms = rtttl_duration_ms(tone->notes, tone->n_notes);
    p->playing = true;
    return true;
}

void tone_player_stop(tone_player_t *p)
{
    p->playing = false;
}

bool tone_player_step(tone_player_t *p, uint32_t now_ms, uint16_t *freq_hz)
{
    if (!p->playing) {
        return false;
    }
    const uint32_t t_ms = now_ms - p->start_ms;  // unsigned: survives the clock wrapping
    if (t_ms >= p->len_ms) {
        p->playing = false;
        return false;
    }
    return rtttl_tone_at(p->tone.notes, p->tone.n_notes, t_ms, freq_hz);
}

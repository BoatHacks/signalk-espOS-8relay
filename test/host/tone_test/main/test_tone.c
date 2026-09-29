#include <string.h>

#include "tone.h"
#include "tone_player.h"
#include "unity.h"

// -------------------------------------------------------------- RTTTL (#14)

TEST_CASE("RTTTL: duration comes from the tempo and the default/note duration", "[tone]")
{
    rtttl_note_t notes[RTTTL_MAX_NOTES];
    // Whole note at 200 bpm = 240000/200 = 1200 ms; 16th = 75 ms.
    size_t n = rtttl_parse("boot:d=16,o=5,b=200:c,e,g", notes, RTTTL_MAX_NOTES);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL(75, notes[0].duration_ms);
    TEST_ASSERT_EQUAL(75, notes[1].duration_ms);
    TEST_ASSERT_EQUAL(75, notes[2].duration_ms);
    // c5, e5, g5 (equal temperament, A4 = 440).
    TEST_ASSERT_EQUAL(523, notes[0].freq_hz);
    TEST_ASSERT_EQUAL(659, notes[1].freq_hz);
    TEST_ASSERT_EQUAL(784, notes[2].freq_hz);
    TEST_ASSERT_EQUAL(225, rtttl_duration_ms(notes, n));
}

TEST_CASE("RTTTL: a per-note duration/octave overrides the defaults", "[tone]")
{
    rtttl_note_t notes[RTTTL_MAX_NOTES];
    size_t n = rtttl_parse("x:d=4,o=4,b=120:8c5", notes, RTTTL_MAX_NOTES);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL(523, notes[0].freq_hz);      // octave 5, not the default 4
    TEST_ASSERT_EQUAL(250, notes[0].duration_ms);  // an 8th, not the default quarter
}

TEST_CASE("RTTTL: a sharp raises the note a semitone, a dot adds half its length", "[tone]")
{
    rtttl_note_t notes[RTTTL_MAX_NOTES];
    // Whole note at 60 bpm = 4000 ms; quarter = 1000 ms; dotted = 1500 ms.
    size_t n = rtttl_parse("x:d=4,o=5,b=60:4c#6.", notes, RTTTL_MAX_NOTES);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL(1109, notes[0].freq_hz);  // c#6
    TEST_ASSERT_EQUAL(1500, notes[0].duration_ms);
}

TEST_CASE("RTTTL: 'p' is a rest -- no frequency, no tone", "[tone]")
{
    rtttl_note_t notes[RTTTL_MAX_NOTES];
    size_t n = rtttl_parse("x:d=4,o=5,b=120:c,p,e", notes, RTTTL_MAX_NOTES);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL(0, notes[1].freq_hz);
    uint16_t f;
    TEST_ASSERT_FALSE(rtttl_tone_at(notes, n, notes[0].duration_ms, &f));  // resting
}

TEST_CASE("RTTTL: unrecognised tokens are skipped, not fatal", "[tone]")
{
    rtttl_note_t notes[RTTTL_MAX_NOTES];
    size_t n = rtttl_parse("x:d=4,o=5,b=120:c,q,e", notes, RTTTL_MAX_NOTES);
    TEST_ASSERT_EQUAL(2, n);
}

TEST_CASE("RTTTL: empty or malformed input parses to zero notes", "[tone]")
{
    rtttl_note_t notes[RTTTL_MAX_NOTES];
    TEST_ASSERT_EQUAL(0, rtttl_parse("", notes, RTTTL_MAX_NOTES));
    TEST_ASSERT_EQUAL(0, rtttl_parse(NULL, notes, RTTTL_MAX_NOTES));
    TEST_ASSERT_EQUAL(0, rtttl_parse("x:d=4,o=5,b=120:", notes, RTTTL_MAX_NOTES));
}

TEST_CASE("RTTTL: the tone follows the notes, then stops (one pass, no loop)", "[tone]")
{
    rtttl_note_t notes[RTTTL_MAX_NOTES];
    size_t n = rtttl_parse("boot:d=16,o=5,b=200:c,e,g", notes, RTTTL_MAX_NOTES);
    uint16_t f = 0;
    TEST_ASSERT_TRUE(rtttl_tone_at(notes, n, 0, &f));
    TEST_ASSERT_EQUAL(notes[0].freq_hz, f);
    TEST_ASSERT_TRUE(rtttl_tone_at(notes, n, 74, &f));
    TEST_ASSERT_TRUE(rtttl_tone_at(notes, n, 75, &f));
    TEST_ASSERT_EQUAL(notes[1].freq_hz, f);
    const uint32_t total = rtttl_duration_ms(notes, n);
    TEST_ASSERT_FALSE(rtttl_tone_at(notes, n, total, &f));  // past the end
}

// ------------------------------------------------------ tone library (#14)

TEST_CASE("tone table: valid rows parse, invalid rows are skipped", "[tone]")
{
    const char *json = "[{\"name\":\"boot\",\"rtttl\":\"boot:d=16,o=5,b=200:c,e,g\"},"
                        "{\"name\":\"unplayable\",\"rtttl\":\"nonsense with no notes\"},"
                        "{\"name\":\"\",\"rtttl\":\"x:d=4,o=5,b=60:c\"},"
                        "{\"rtttl\":\"x:d=4,o=5,b=60:c\"},"
                        "{\"name\":\"relay-on\",\"rtttl\":\"relay-on:d=16,o=6,b=250:c\"}]";
    tone_t tones[TONE_MAX_TONES];
    size_t n = tone_library_parse(json, tones, TONE_MAX_TONES);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_STRING("boot", tones[0].name);
    TEST_ASSERT_EQUAL(3, tones[0].n_notes);
    TEST_ASSERT_EQUAL_STRING("relay-on", tones[1].name);
}

TEST_CASE("tone table: malformed JSON or an empty array parses to zero tones", "[tone]")
{
    tone_t tones[TONE_MAX_TONES];
    TEST_ASSERT_EQUAL(0, tone_library_parse("not json", tones, TONE_MAX_TONES));
    TEST_ASSERT_EQUAL(0, tone_library_parse(NULL, tones, TONE_MAX_TONES));
    TEST_ASSERT_EQUAL(0, tone_library_parse("[]", tones, TONE_MAX_TONES));
}

TEST_CASE("tone table: lookup by name, empty name and unknown name", "[tone]")
{
    tone_t tones[2] = {0};
    strcpy(tones[0].name, "boot");
    strcpy(tones[1].name, "relay-on");
    TEST_ASSERT_EQUAL(0, tone_library_find(tones, 2, "boot"));
    TEST_ASSERT_EQUAL(1, tone_library_find(tones, 2, "relay-on"));
    TEST_ASSERT_EQUAL(-1, tone_library_find(tones, 2, ""));
    TEST_ASSERT_EQUAL(-1, tone_library_find(tones, 2, "nope"));
    TEST_ASSERT_EQUAL(-1, tone_library_find(tones, 0, "boot"));
}

// --------------------------------------------------------- priority (#14)

TEST_CASE("chirp priority: nothing beats a chirp starting from NONE", "[tone]")
{
    TEST_ASSERT_TRUE(tone_priority_allowed(TONE_PRIORITY_CHIRP, TONE_PRIORITY_NONE));
}

TEST_CASE("chirp priority: an equal-or-higher active tier blocks the chirp", "[tone]")
{
    TEST_ASSERT_FALSE(tone_priority_allowed(TONE_PRIORITY_CHIRP, TONE_PRIORITY_CHIRP));
    TEST_ASSERT_FALSE(tone_priority_allowed(TONE_PRIORITY_CHIRP, TONE_PRIORITY_HIGH));
}

TEST_CASE("chirp priority: HIGH always outranks NONE and CHIRP", "[tone]")
{
    TEST_ASSERT_TRUE(tone_priority_allowed(TONE_PRIORITY_HIGH, TONE_PRIORITY_NONE));
    TEST_ASSERT_TRUE(tone_priority_allowed(TONE_PRIORITY_HIGH, TONE_PRIORITY_CHIRP));
    TEST_ASSERT_FALSE(tone_priority_allowed(TONE_PRIORITY_HIGH, TONE_PRIORITY_HIGH));
}

// ------------------------------------------------------ one-shot player

static tone_t two_notes(void)
{
    // 8th notes at 120 bpm = 250 ms each: a6 (1760 Hz), then a rest, then c7.
    tone_t t = {0};
    t.n_notes = rtttl_parse("x:d=8,o=6,b=120:a,p,c7", t.notes, RTTTL_MAX_NOTES);
    return t;
}

TEST_CASE("player: sounds each note at its pitch, silent in a rest", "[tone]")
{
    tone_t t = two_notes();
    tone_player_t p = {0};
    uint16_t hz = 0;
    TEST_ASSERT_TRUE(tone_player_start(&p, &t, 1000));
    TEST_ASSERT_TRUE(tone_player_step(&p, 1000, &hz));
    TEST_ASSERT_EQUAL(1760, hz);
    hz = 42;
    TEST_ASSERT_FALSE(tone_player_step(&p, 1300, &hz));  // the rest
    TEST_ASSERT_EQUAL(42, hz);                           // pitch left alone
    TEST_ASSERT_TRUE(p.playing);
    TEST_ASSERT_TRUE(tone_player_step(&p, 1600, &hz));
    TEST_ASSERT_EQUAL(2093, hz);
}

TEST_CASE("player: one pass, then it stops by itself", "[tone]")
{
    tone_t t = two_notes();
    tone_player_t p = {0};
    uint16_t hz = 0;
    tone_player_start(&p, &t, 0);
    TEST_ASSERT_EQUAL(750, p.len_ms);
    TEST_ASSERT_FALSE(tone_player_step(&p, 750, &hz));
    TEST_ASSERT_FALSE(p.playing);
    TEST_ASSERT_FALSE(tone_player_step(&p, 0, &hz));  // not restarted by an earlier time
}

TEST_CASE("player: an empty tone doesn't start; stop is immediate", "[tone]")
{
    tone_t empty = {0};
    tone_t t = two_notes();
    tone_player_t p = {0};
    uint16_t hz = 0;
    TEST_ASSERT_FALSE(tone_player_start(&p, &empty, 0));
    TEST_ASSERT_FALSE(p.playing);
    tone_player_start(&p, &t, 0);
    tone_player_stop(&p);
    TEST_ASSERT_FALSE(tone_player_step(&p, 10, &hz));
}

TEST_CASE("player: survives the millisecond clock wrapping", "[tone]")
{
    tone_t t = two_notes();
    tone_player_t p = {0};
    uint16_t hz = 0;
    tone_player_start(&p, &t, UINT32_MAX - 100);
    TEST_ASSERT_TRUE(tone_player_step(&p, 50, &hz));  // 151 ms in: first note
    TEST_ASSERT_EQUAL(1760, hz);
    TEST_ASSERT_TRUE(tone_player_step(&p, 500, &hz));  // 601 ms in: third note
    TEST_ASSERT_EQUAL(2093, hz);
}

#include <string.h>

#include "indicator_logic.h"
#include "unity.h"

// ------------------------------------------------------------ LED state

TEST_CASE("alarm beats warning beats missing SignalK", "[indicator]")
{
    TEST_ASSERT_EQUAL(INDICATOR_ALARM, indicator_state(2, true, false));
    TEST_ASSERT_EQUAL(INDICATOR_WARN, indicator_state(1, true, false));
    TEST_ASSERT_EQUAL(INDICATOR_NO_SIGNALK, indicator_state(0, true, false));
    TEST_ASSERT_EQUAL(INDICATOR_OK, indicator_state(0, true, true));
}

TEST_CASE("no SignalK connection is fine when no tree is published", "[indicator]")
{
    TEST_ASSERT_EQUAL(INDICATOR_OK, indicator_state(0, false, false));
}

TEST_CASE("colours scale with brightness; 0 is off", "[indicator]")
{
    indicator_rgb_t c = indicator_color(INDICATOR_ALARM, 100);
    TEST_ASSERT_EQUAL(255, c.r);
    TEST_ASSERT_EQUAL(0, c.g);
    c = indicator_color(INDICATOR_OK, 10);
    TEST_ASSERT_EQUAL(0, c.r);
    TEST_ASSERT_EQUAL(26, c.g);
    c = indicator_color(INDICATOR_NO_SIGNALK, 0);
    TEST_ASSERT_EQUAL(0, c.r + c.g + c.b);
    c = indicator_color(INDICATOR_WARN, 250);  // clamped to 100 %
    TEST_ASSERT_EQUAL(255, c.r);
}

// ---------------------------------------------------------------- Morse

TEST_CASE("Morse timing: elements, letter gaps and word gaps", "[indicator]")
{
    morse_seg_t s[32];
    // E = .   S = ...
    size_t n = morse_encode("ES", s, 32);
    TEST_ASSERT_EQUAL(7, n);
    TEST_ASSERT_TRUE(s[0].on);
    TEST_ASSERT_EQUAL(1, s[0].units);
    TEST_ASSERT_FALSE(s[1].on);
    TEST_ASSERT_EQUAL(3, s[1].units);  // between letters
    TEST_ASSERT_FALSE(s[3].on);
    TEST_ASSERT_EQUAL(1, s[3].units);  // inside a letter

    n = morse_encode("E E", s, 32);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL(7, s[1].units);  // between words
}

TEST_CASE("ESP 42 in Morse", "[indicator]")
{
    // E .  S ...  P .--.  4 ....-  2 ..---
    const char *want = ". ... .--. / ....- ..---";
    morse_seg_t s[64];
    size_t n = morse_encode("ESP 42", s, 64);
    char got[64];
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i].on) {
            got[k++] = s[i].units == 3 ? '-' : '.';
        } else if (s[i].units == 3) {
            got[k++] = ' ';
        } else if (s[i].units == 7) {
            got[k++] = ' ';
            got[k++] = '/';
            got[k++] = ' ';
        }
    }
    got[k] = '\0';
    TEST_ASSERT_EQUAL_STRING(want, got);
    TEST_ASSERT_TRUE(s[n - 1].on);  // no trailing silence
}

TEST_CASE("unknown characters are skipped and output is bounded", "[indicator]")
{
    morse_seg_t s[3];
    TEST_ASSERT_EQUAL(1, morse_encode("#e!", s, 3));
    TEST_ASSERT_EQUAL(3, morse_encode("SSSS", s, 3));
    TEST_ASSERT_EQUAL(0, morse_encode("", s, 3));
}

// ---------------------------------------------------------- alarm text

TEST_CASE("alarm text ends with the IP address's last octet", "[indicator]")
{
    char t[16];
    indicator_alarm_text("192.168.1.42", t, sizeof(t));
    TEST_ASSERT_EQUAL_STRING("ESP 42", t);
    indicator_alarm_text("10.0.0.7", t, sizeof(t));
    TEST_ASSERT_EQUAL_STRING("ESP 7", t);
}

TEST_CASE("without a usable address the alarm text is ESP AP", "[indicator]")
{
    char t[16];
    const char *bad[] = {NULL, "", "0.0.0.0", "10.0.0.", "10.0.0.x", "10.0.0.300", "fe80::1"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        indicator_alarm_text(bad[i], t, sizeof(t));
        TEST_ASSERT_EQUAL_STRING("ESP AP", t);
    }
}

// ----------------------------------------------------------------- tone

TEST_CASE("the tone follows the segments, then pauses, then repeats", "[indicator]")
{
    morse_seg_t s[8];
    size_t n = morse_encode("A", s, 8);  // .- : on1 off1 on3 = 5 units
    const uint32_t u = 100, pause = 1000;
    TEST_ASSERT_TRUE(morse_tone_at(s, n, u, pause, 0));
    TEST_ASSERT_FALSE(morse_tone_at(s, n, u, pause, 150));
    TEST_ASSERT_TRUE(morse_tone_at(s, n, u, pause, 250));
    TEST_ASSERT_TRUE(morse_tone_at(s, n, u, pause, 499));
    TEST_ASSERT_FALSE(morse_tone_at(s, n, u, pause, 500));   // pause
    TEST_ASSERT_FALSE(morse_tone_at(s, n, u, pause, 1499));
    TEST_ASSERT_TRUE(morse_tone_at(s, n, u, pause, 1500));   // again
    TEST_ASSERT_FALSE(morse_tone_at(s, 0, u, pause, 0));
}

TEST_CASE("one pass of the message lasts the sum of its segments", "[indicator]")
{
    morse_seg_t segs[64];
    // "E": a dot (1 unit).
    size_t n = morse_encode("E", segs, 64);
    TEST_ASSERT_EQUAL(80, morse_duration_ms(segs, n, 80));
    // "ESP": E . | gap 3 | S ... (5) | gap 3 | P .--. (11) = 23 units.
    n = morse_encode("ESP", segs, 64);
    TEST_ASSERT_EQUAL(23 * 80, morse_duration_ms(segs, n, 80));
    TEST_ASSERT_EQUAL(0, morse_duration_ms(segs, 0, 80));
    // The last segment is tone, so a single pass ends on the tone's end.
    const uint32_t len = morse_duration_ms(segs, n, 80);
    TEST_ASSERT_TRUE(morse_tone_at(segs, n, 80, 0, len - 1));
}

// -------------------------------------------------------------- RTTTL (#14)

TEST_CASE("RTTTL: duration comes from the tempo and the default/note duration", "[indicator]")
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

TEST_CASE("RTTTL: a per-note duration/octave overrides the defaults", "[indicator]")
{
    rtttl_note_t notes[RTTTL_MAX_NOTES];
    size_t n = rtttl_parse("x:d=4,o=4,b=120:8c5", notes, RTTTL_MAX_NOTES);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL(523, notes[0].freq_hz);      // octave 5, not the default 4
    TEST_ASSERT_EQUAL(250, notes[0].duration_ms);  // an 8th, not the default quarter
}

TEST_CASE("RTTTL: a sharp raises the note a semitone, a dot adds half its length", "[indicator]")
{
    rtttl_note_t notes[RTTTL_MAX_NOTES];
    // Whole note at 60 bpm = 4000 ms; quarter = 1000 ms; dotted = 1500 ms.
    size_t n = rtttl_parse("x:d=4,o=5,b=60:4c#6.", notes, RTTTL_MAX_NOTES);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL(1109, notes[0].freq_hz);  // c#6
    TEST_ASSERT_EQUAL(1500, notes[0].duration_ms);
}

TEST_CASE("RTTTL: 'p' is a rest -- no frequency, no tone", "[indicator]")
{
    rtttl_note_t notes[RTTTL_MAX_NOTES];
    size_t n = rtttl_parse("x:d=4,o=5,b=120:c,p,e", notes, RTTTL_MAX_NOTES);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL(0, notes[1].freq_hz);
    uint16_t f;
    TEST_ASSERT_FALSE(rtttl_tone_at(notes, n, notes[0].duration_ms, &f));  // resting
}

TEST_CASE("RTTTL: unrecognised tokens are skipped, not fatal", "[indicator]")
{
    rtttl_note_t notes[RTTTL_MAX_NOTES];
    size_t n = rtttl_parse("x:d=4,o=5,b=120:c,q,e", notes, RTTTL_MAX_NOTES);
    TEST_ASSERT_EQUAL(2, n);
}

TEST_CASE("RTTTL: empty or malformed input parses to zero notes", "[indicator]")
{
    rtttl_note_t notes[RTTTL_MAX_NOTES];
    TEST_ASSERT_EQUAL(0, rtttl_parse("", notes, RTTTL_MAX_NOTES));
    TEST_ASSERT_EQUAL(0, rtttl_parse(NULL, notes, RTTTL_MAX_NOTES));
    TEST_ASSERT_EQUAL(0, rtttl_parse("x:d=4,o=5,b=120:", notes, RTTTL_MAX_NOTES));
}

TEST_CASE("RTTTL: the tone follows the notes, then stops (one pass, no loop)", "[indicator]")
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

TEST_CASE("tone table: valid rows parse, invalid rows are skipped", "[indicator]")
{
    const char *json = "[{\"name\":\"boot\",\"rtttl\":\"boot:d=16,o=5,b=200:c,e,g\"},"
                        "{\"name\":\"unplayable\",\"rtttl\":\"nonsense with no notes\"},"
                        "{\"name\":\"\",\"rtttl\":\"x:d=4,o=5,b=60:c\"},"
                        "{\"rtttl\":\"x:d=4,o=5,b=60:c\"},"
                        "{\"name\":\"relay-on\",\"rtttl\":\"relay-on:d=16,o=6,b=250:c\"}]";
    indicator_tone_t tones[INDICATOR_MAX_TONES];
    size_t n = indicator_parse_tones(json, tones, INDICATOR_MAX_TONES);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_STRING("boot", tones[0].name);
    TEST_ASSERT_EQUAL(3, tones[0].n_notes);
    TEST_ASSERT_EQUAL_STRING("relay-on", tones[1].name);
}

TEST_CASE("tone table: malformed JSON or an empty array parses to zero tones", "[indicator]")
{
    indicator_tone_t tones[INDICATOR_MAX_TONES];
    TEST_ASSERT_EQUAL(0, indicator_parse_tones("not json", tones, INDICATOR_MAX_TONES));
    TEST_ASSERT_EQUAL(0, indicator_parse_tones(NULL, tones, INDICATOR_MAX_TONES));
    TEST_ASSERT_EQUAL(0, indicator_parse_tones("[]", tones, INDICATOR_MAX_TONES));
}

TEST_CASE("chirp priority: override and alarm both block a chirp", "[indicator]")
{
    TEST_ASSERT_TRUE(indicator_chirp_allowed(INDICATOR_OVERRIDE_NONE, false));
    TEST_ASSERT_FALSE(indicator_chirp_allowed(INDICATOR_OVERRIDE_NONE, true));
    TEST_ASSERT_FALSE(indicator_chirp_allowed(INDICATOR_OVERRIDE_PORTAL, false));
    TEST_ASSERT_FALSE(indicator_chirp_allowed(INDICATOR_OVERRIDE_RESET, false));
    TEST_ASSERT_FALSE(indicator_chirp_allowed(INDICATOR_OVERRIDE_RESET, true));
}

TEST_CASE("tone table: lookup by name, empty name and unknown name", "[indicator]")
{
    indicator_tone_t tones[2] = {0};
    strcpy(tones[0].name, "boot");
    strcpy(tones[1].name, "relay-on");
    TEST_ASSERT_EQUAL(0, indicator_find_tone(tones, 2, "boot"));
    TEST_ASSERT_EQUAL(1, indicator_find_tone(tones, 2, "relay-on"));
    TEST_ASSERT_EQUAL(-1, indicator_find_tone(tones, 2, ""));
    TEST_ASSERT_EQUAL(-1, indicator_find_tone(tones, 2, "nope"));
    TEST_ASSERT_EQUAL(-1, indicator_find_tone(tones, 0, "boot"));
}

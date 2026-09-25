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

TEST_CASE("without a usable address the alarm text is just ESP", "[indicator]")
{
    char t[16];
    const char *bad[] = {NULL, "", "0.0.0.0", "10.0.0.", "10.0.0.x", "10.0.0.300", "fe80::1"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        indicator_alarm_text(bad[i], t, sizeof(t));
        TEST_ASSERT_EQUAL_STRING("ESP", t);
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

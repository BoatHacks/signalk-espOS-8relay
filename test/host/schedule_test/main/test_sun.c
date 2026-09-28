#include <stdio.h>

#include "sun.h"
#include "unity.h"

// Reference instants computed independently in Python from the same NOAA
// low-precision algorithm (Julian-day / equation-of-time / solar-declination
// terms), not derived from this C port -- a transcription bug here would
// disagree with them. Two of these (Washington DC and NYC summer solstice)
// were additionally checked by hand against the sunrise/sunset times
// commonly published for those cities and instants (05:42 / 20:37 EDT and
// 05:25 / 20:31 EDT respectively), which the algorithm reproduces to the
// minute. Longitude is degrees EAST throughout (SignalK's convention), so
// New York/DC/London/Svalbard/Tromso are all negative or small positive,
// and Sydney/Auckland are large positive.
static void assert_close(int64_t expected_ms, int64_t actual_ms, const char *what)
{
    // The algorithm is documented to about a minute; cross-implementation
    // (Python vs. this C port) rounding differences are far smaller than
    // that, so a tight bound here still catches a real transcription bug.
    int64_t diff = expected_ms - actual_ms;
    if (diff < 0) {
        diff = -diff;
    }
    char msg[128];
    snprintf(msg, sizeof(msg), "%s: expected %lld, got a difference of %lld ms", what, (long long)expected_ms,
              (long long)diff);
    TEST_ASSERT_TRUE_MESSAGE(diff <= 5000, msg);
}

TEST_CASE("London, equinox season", "[sun]")
{
    sun_times_t out;
    sun_times(2026, 9, 28, 51.5074, -0.1278, &out);
    TEST_ASSERT_EQUAL(SUN_TRANSITION, out.kind);
    assert_close(1790574946193, out.sunrise_unix_ms, "London sunrise");
    assert_close(1790617615037, out.sunset_unix_ms, "London sunset");
}

TEST_CASE("New York, summer solstice", "[sun]")
{
    sun_times_t out;
    sun_times(2026, 6, 21, 40.7128, -74.0060, &out);
    TEST_ASSERT_EQUAL(SUN_TRANSITION, out.kind);
    assert_close(1782033896106, out.sunrise_unix_ms, "NYC sunrise");
    assert_close(1782088232632, out.sunset_unix_ms, "NYC sunset");
}

// Cross-checked by hand: 09:42:56 UTC / 00:36:47 UTC next day is 05:42 EDT /
// 20:36 EDT, matching the commonly published DC solstice sunrise/sunset.
TEST_CASE("Washington DC, summer solstice", "[sun]")
{
    sun_times_t out;
    sun_times(2026, 6, 21, 38.9072, -77.0369, &out);
    TEST_ASSERT_EQUAL(SUN_TRANSITION, out.kind);
    assert_close(1782034976197, out.sunrise_unix_ms, "DC sunrise");
    assert_close(1782088607373, out.sunset_unix_ms, "DC sunset");
}

TEST_CASE("the equator, equinox: close to a 12-hour day", "[sun]")
{
    sun_times_t out;
    sun_times(2026, 3, 20, 0.0, 0.0, &out);
    TEST_ASSERT_EQUAL(SUN_TRANSITION, out.kind);
    assert_close(1773986654924, out.sunrise_unix_ms, "equator sunrise");
    assert_close(1774030254767, out.sunset_unix_ms, "equator sunset");
}

// Southern hemisphere: June is winter (short day), December is summer (long
// day) -- the opposite of the northern-hemisphere cases above.
TEST_CASE("Sydney, June (southern winter, short day)", "[sun]")
{
    sun_times_t out;
    sun_times(2026, 6, 21, -33.8688, 151.2093, &out);
    TEST_ASSERT_EQUAL(SUN_TRANSITION, out.kind);
    assert_close(1781989199084, out.sunrise_unix_ms, "Sydney June sunrise");
    assert_close(1782024826311, out.sunset_unix_ms, "Sydney June sunset");
    TEST_ASSERT_TRUE(out.sunset_unix_ms - out.sunrise_unix_ms < 11 * 3600 * 1000LL);
}

TEST_CASE("Sydney, December (southern summer, long day)", "[sun]")
{
    sun_times_t out;
    sun_times(2026, 12, 21, -33.8688, 151.2093, &out);
    TEST_ASSERT_EQUAL(SUN_TRANSITION, out.kind);
    assert_close(1797792045100, out.sunrise_unix_ms, "Sydney Dec sunrise");
    assert_close(1797843913963, out.sunset_unix_ms, "Sydney Dec sunset");
    TEST_ASSERT_TRUE(out.sunset_unix_ms - out.sunrise_unix_ms > 13 * 3600 * 1000LL);
}

TEST_CASE("a longitude far from Greenwich still returns a sane instant", "[sun]")
{
    // Auckland, mid-January (southern summer): confirms the day/month
    // parameter and the actual UTC instant don't have to agree on which
    // calendar day it "is" -- see sun.h's note about that.
    sun_times_t out;
    sun_times(2026, 1, 15, -36.8485, 174.7633, &out);
    TEST_ASSERT_EQUAL(SUN_TRANSITION, out.kind);
    assert_close(1768411082777, out.sunrise_unix_ms, "Auckland sunrise");
    assert_close(1768462938391, out.sunset_unix_ms, "Auckland sunset");
}

// ------------------------------------------------------------- polar cases

TEST_CASE("Svalbard, winter solstice: polar night, no transition", "[sun]")
{
    sun_times_t out;
    sun_times(2026, 12, 21, 78.0, 15.6, &out);
    TEST_ASSERT_EQUAL(SUN_ALWAYS_DOWN, out.kind);
}

TEST_CASE("Svalbard, summer solstice: polar day, no transition", "[sun]")
{
    sun_times_t out;
    sun_times(2026, 6, 21, 78.0, 15.6, &out);
    TEST_ASSERT_EQUAL(SUN_ALWAYS_UP, out.kind);
}

// Tromso's real polar night ends in mid-January: a day with a genuine, very
// short transition -- the boundary case between the ordinary and the polar
// ones above, and the one most likely to break if the >1/<-1 branches in
// hour_angle_deg() were ever swapped.
TEST_CASE("Tromso, mid-January: a genuine but very short day", "[sun]")
{
    sun_times_t out;
    sun_times(2026, 1, 15, 69.6492, 18.9553, &out);
    TEST_ASSERT_EQUAL(SUN_TRANSITION, out.kind);
    assert_close(1768473706645, out.sunrise_unix_ms, "Tromso sunrise");
    assert_close(1768475102363, out.sunset_unix_ms, "Tromso sunset");
    TEST_ASSERT_TRUE(out.sunset_unix_ms > out.sunrise_unix_ms);
    TEST_ASSERT_TRUE(out.sunset_unix_ms - out.sunrise_unix_ms < 3600 * 1000LL);  // well under an hour
}

// Tromso in mid-March, still well short of the equinox: comfortably back to
// an ordinary transition day, on the other side of the boundary from the
// January case above.
TEST_CASE("Tromso, mid-March: an ordinary transition day again", "[sun]")
{
    sun_times_t out;
    sun_times(2026, 3, 15, 69.6492, 18.9553, &out);
    TEST_ASSERT_EQUAL(SUN_TRANSITION, out.kind);
    assert_close(1773551252996, out.sunrise_unix_ms, "Tromso March sunrise");
    assert_close(1773592731102, out.sunset_unix_ms, "Tromso March sunset");
}

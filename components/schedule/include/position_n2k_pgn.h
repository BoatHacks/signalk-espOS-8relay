// Decodes just the latitude/longitude fields of NMEA 2000 PGN 129025
// ("Position, Rapid Update") and 129029 ("GNSS Position Data") -- the two
// position_source=n2k schedules use (issue #9, plan 16).
//
// Field layout and resolutions were cross-checked against the vendored
// ttlappalainen/NMEA2000 library's own ParseN2kPGN129025()/
// ParseN2kPGN129029() (N2kMessages.cpp) rather than reused directly: that
// library (and espos_n2k) is excluded from the linux host-test target
// (its idf_component.yml has "rules: [{if: target != linux}]"), so
// calling it from here would put this decode path outside host-test
// coverage entirely. Hand-rolled instead, little-endian throughout
// (matching the library's own GetBuf<T>(), which only byte-swaps on a
// big-endian host -- neither ESP32 nor the linux host target is one).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// PGN 129025, exactly 8 bytes: int32 latitude at offset 0, int32 longitude
// at offset 4, both little-endian, resolution 1e-7 degrees. 0x7FFFFFFF in
// either field means "not available". False for a short buffer or an
// unavailable field; `*lat`/`*lon` are left untouched.
bool position_n2k_parse_129025(const uint8_t *data, uint8_t len, double *lat, double *lon);

// PGN 129029: only the fields needed here. SID (1 byte), days since 1970
// (2 bytes) and seconds since midnight (4 bytes) are skipped; latitude is
// an int64 at offset 7, longitude at offset 15, both little-endian,
// resolution 1e-16 degrees (altitude and everything after is ignored).
// 0x7FFFFFFFFFFFFFFF means "not available". False for a short buffer or an
// unavailable field.
bool position_n2k_parse_129029(const uint8_t *data, uint8_t len, double *lat, double *lon);

#ifdef __cplusplus
}
#endif

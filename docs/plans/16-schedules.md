# Implementation Plan: 16 — Schedules with the real-time clock

Issue: [#9](https://github.com/BoatHacks/signalk-espOS-8relay/issues/9)

## Overview
Time-based switching on the board: anchor light from sunset to sunrise,
a fan 10 minutes every hour. The board's PCF85063 real-time clock keeps
time across power loss, so schedules work before (or without) the
network.

## Relevant SPEC/ARCHITECTURE Sections
- SPEC.md §10.2 (schedules out of scope — this plan needs that decision
  reversed first)
- README "Board hardware" (PCF85063 on the relay I²C bus, unused)

## Decision (2026-09-25, reconfirmed 2026-09-27)
**On the board.** Schedules must keep working without a SignalK server.
SPEC.md §10.2 updated. The cost is a schedule UI that fits espOS's flat
key/value settings (or a page of our own, like the relay page).

## Approach
- **RTC driver** for the PCF85063 (I²C 0x51, on SCL 41 / SDA 42, shared
  with the TCA9554 — reuse the bus handle and its locking). At boot, if
  the RTC holds a valid time, hand it to espOS with `espos_time_set()`
  so the whole firmware sees a synced clock before the network is up.
  On `ESPOS_EVENT_TIME_SYNCED` from SNTP (or SignalK), write the time
  back to the RTC.
- **Time zone:** needed no new setting — espOS's own `time.tz` (POSIX TZ
  string, default `UTC0`, its own settings-page entry) already does this;
  the schedule evaluator just reads local time through
  `espos_time_parts()`.
- **Schedule entries**, a fixed number (8), each: relay, days of week, a
  mode (`s<k>_mode`: `clock` or `repeat`, decided 2026-09-28 — see below),
  and two mode-dependent fields (`s<k>_on`/`s<k>_off`): in `clock` mode
  each independently `HH:MM`, `sunrise±m` or `sunset±m`; in `repeat` mode
  on-duration/cycle-length minutes ("on for X min every Y min", anchored
  to local midnight). Stored as `s<k>_relay`, `s<k>_mode`, `s<k>_on`,
  `s<k>_off`, `s<k>_days` (days a bitmask, bit 0 = Sunday, matching
  `espos_time_parts_t.wday`).
- **Position source (decided 2026-09-28, added mid-implementation):** a
  setting, `signalk` (default) or `n2k`. `signalk`: SignalK
  `navigation.position` when subscribed and fresh. `n2k`: decoded from
  the board's own NMEA 2000 bus, PGN 129025 ("Position, Rapid Update")
  and 129029 ("GNSS Position Data") — hand-rolled decode of just the
  lat/lon fields rather than the vendored NMEA2000 library's own parsers,
  since that library is excluded from the linux host-test target and
  routing through it would put the decode path outside test coverage.
  Either way, a live reading older than 10 minutes (fixed, not another
  setting) falls back to a configured fixed position. `switch_bank`'s
  `n2k_bridge` gained one generic addition for this,
  `n2k_bridge_add_msg_listener()` (forwarding raw PGN/bytes, same shape as
  `relay_ctrl_add_listener()`), so it stays scoped to switch banks and
  the position decode logic lives entirely in `components/schedule/`.
- **Sun times**: a small sunrise/sunset function (NOAA's low-precision
  solar-position algorithm), host-tested against instants computed
  independently in Python from the same algorithm (not derived from the
  C port), plus two hand-checked against commonly published times for
  real cities/dates.
- **Switching** through `relay_ctrl_set(..., RELAY_SRC_SCHEDULE)` only
  at the transitions (edge-triggered), so manual commands in between
  stand until the next transition — same rule as input overrides.
- **No valid time** (no RTC time, no SNTP): schedules do nothing and a
  health warning says so.
- **Overlapping schedules on the same relay (decided 2026-09-28):**
  rejected, not silently resolved. `device_config` detects two or more
  *active* entries naming the same relay and raises a health warning
  (same pattern as `interlockInvalid`/`bankIdClash`) — the conflicting
  entries are ignored (that relay's schedule does nothing) until fixed,
  rather than picking a winner by slot order. More protective than
  letting it race, consistent with how this project treats other
  cross-key config conflicts (issue #8's interlock validation).
- **Momentary relays (decided 2026-09-28):** allowed, no special-casing.
  A schedule's "on" edge for a momentary relay fires
  `relay_ctrl_set(on=true, RELAY_SRC_SCHEDULE)` once, same as any other
  direct command — the relay's own `pulseMs` governs how long it stays
  on, exactly like an input-triggered pulse today. The schedule's
  off-time is simply a no-op for that relay (nothing to turn off).
- **Overlapping-schedule key shape (decided 2026-09-28):** an explicit
  `s<k>_mode` key (see above), not overloading `s<k>_on`'s string format
  to signal a repeating schedule — clearer for a settings page (a mode
  dropdown that swaps the field meaning, the same shape `relay1_mode`
  already uses) and for the evaluator (switches on an explicit mode
  rather than sniffing a string).
- **Polar day/night in the evaluator (decided 2026-09-28):** a
  sunrise/sunset reference on a day with no transition treats the whole
  day as continuously on one side of the boundary — permanent polar
  night is continuously past sunset/before sunrise ("always night"),
  permanent polar day continuously between sunrise/sunset ("always
  day") — rather than having no window that day. Implemented as a
  substitution in the minute-resolution function alone (the boundary a
  polar day's condition already includes resolves to "already happened",
  the one it doesn't to "never happens today"), not a new branch in the
  window-comparison logic itself.

## Test Strategy
- Host tests: sunrise/sunset against instants cross-checked independently
  (several latitudes and dates, both hemispheres, including polar
  day/night: no transition); schedule evaluation (edges only, across
  midnight, day-of-week — including a midnight-spanning window not cut
  short by an excluded following day, DST transitions, and the polar
  day/night substitution above); "no valid time" does nothing; the
  overlapping-schedule validation; a momentary relay's on-edge pulse and
  no-op off-edge.
- RTC driver against a fake I²C bus.
On the board: RTC keeps time across a power cut; a 2-minute schedule.

## Implementation Steps
- [x] Decision: on the board; SPEC.md updated
- [x] PCF85063 driver and clock sync
- [x] Time zone and position settings; sun calculation
- [x] Schedule settings and evaluator; `RELAY_SRC_SCHEDULE`
- [x] Host tests
- [x] USER_MANUAL new section; README hardware table; CHANGELOG

## Files to Create/Modify
- `components/rtc_pcf85063/` (new), `components/schedule/` (new)
- `components/device_config/`, `components/relay_ctrl/` (source)
- `components/switch_bank/` (n2k_bridge raw-message listener, for
  position_source=n2k)
- `main/main.c`
- `test/host/schedule_test/`, `test/host/device_config_test/`,
  `test/host/rtc_pcf85063_test/` (new/extended)
- `SPEC.md`, `USER_MANUAL.md`, `README.md`, `CHANGELOG.md`

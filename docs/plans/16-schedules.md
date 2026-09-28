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
- **Time zone:** a setting (POSIX TZ string, default UTC).
- **Schedule entries**, a fixed number (e.g. 8), each: relay, days of
  week, on-time, off-time, where a time is `HH:MM`, `sunrise±m` or
  `sunset±m`; or a repeating "on for X min every Y min". Stored as a few
  keys per entry (`s<k>_relay`, `s<k>_on`, `s<k>_off`, `s<k>_days`).
- **Sun times** from position. **Position source (decided 2026-09-28,
  user-requested): a setting, not hardcoded** — `position_source`:
  `"signalk"` (default) or `"n2k"`.
  - `"signalk"`: SignalK `navigation.position` when subscribed and
    fresh, as originally planned.
  - `"n2k"`: decoded from the board's own NMEA 2000 bus — PGN 129025
    (Position, Rapid Update) and/or 129029 (GNSS Position Data). No
    position decoding exists anywhere in this codebase yet (only
    switch-bank PGNs 127501/127502 are handled today); the vendored
    `ttlappalainen/NMEA2000` library (`managed_components/
    ttlappalainen__nmea2000`) is a generic N2K stack and should already
    support parsing these standard PGNs — confirm and wire up handling
    in `components/switch_bank/` alongside the existing PGN code, don't
    assume it needs writing from scratch.
  - Either way, if the selected source has no fresh position (not
    subscribed/decoded recently), fall back to the configured static
    position setting — this fallback already existed in the original
    plan and still applies regardless of which live source is chosen.
  - A small sunrise/sunset function (NOAA algorithm), host-tested.
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

## Test Strategy
- Host tests: sunrise/sunset against published tables for a few
  latitudes and dates (including polar day/night: no transition);
  schedule evaluation (edges only, across midnight, day-of-week, DST
  change); "no valid time" does nothing.
- RTC driver against a fake I²C bus.
On the board: RTC keeps time across a power cut; a 2-minute schedule.

## Implementation Steps
- [x] Decision: on the board; SPEC.md updated
- [ ] PCF85063 driver and clock sync
- [ ] Time zone and position settings; sun calculation
- [ ] Schedule settings and evaluator; `RELAY_SRC_SCHEDULE`
- [ ] Host tests
- [ ] USER_MANUAL new section; README hardware table; CHANGELOG

## Files to Create/Modify
- `components/rtc_pcf85063/` (new), `components/schedule/` (new)
- `components/device_config/`, `components/relay_ctrl/` (source)
- `main/main.c`
- `test/host/schedule_test/` (new)
- `SPEC.md`, `USER_MANUAL.md`, `README.md`, `CHANGELOG.md`

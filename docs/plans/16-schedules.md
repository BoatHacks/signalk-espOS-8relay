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

## Decision (2026-09-25)
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
- **Sun times** from position: SignalK `navigation.position` when
  subscribed and fresh, else a configured fallback position. A small
  sunrise/sunset function (NOAA algorithm), host-tested.
- **Switching** through `relay_ctrl_set(..., RELAY_SRC_SCHEDULE)` only
  at the transitions (edge-triggered), so manual commands in between
  stand until the next transition — same rule as input overrides.
- **No valid time** (no RTC time, no SNTP): schedules do nothing and a
  health warning says so.

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

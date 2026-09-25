# Implementation Plan: 12 — "Last switched by" on the relay page

Issue: [#5](https://github.com/BoatHacks/signalk-espOS-8relay/issues/5)

## Overview
Show for each relay what switched it last and how long ago, e.g. "on ·
by NMEA 2000 · 3 min ago", on the relay page and in its REST API.

## Relevant SPEC/ARCHITECTURE Sections
- ARCHITECTURE.md §2.1 (`relay_ctrl` listeners)
- CHANGELOG 0.0.4 (relay page)

## Approach
- Every relay change already reaches `on_relay_change()` in `main.c`
  with a `relay_source_t`. Record per relay the last source and the
  uptime (ms) of the change in a small table in `web_ui` (a new
  `web_ui_relay_changed(ch, src)`), guarded by the existing spinlock.
- Boot state counts as a change with source `boot`, recorded when
  `relay_ctrl_init()` has set the relays (add a notify for the initial
  state, or record it from `main.c` right after init).
- `GET /api/v1/relays` gains per relay `"lastSource"` (`"signalk"`,
  `"nmea2000"`, `"web"`, `"input"`, `"pulse"`, `"failsafe"`, `"maxOn"`,
  `"boot"`) and `"lastChangeAgoS"`. Uptime-based, so it works before SNTP
  has set the clock and needs no time zone.
- The page shows it under the relay name ("by NMEA 2000 · 3 min ago"),
  refreshed with the 1 s poll; the "ago" text is computed in the
  browser from the value and the poll time.
- Also log every relay change with its source at INFO level (one line),
  so the serial log answers the same question.

## Test Strategy
- `web_ui_test`: JSON carries source names and ages; unknown source
  maps to `"unknown"`; `boot` before any command.
- Browser check against the mock (as for 0.0.4): text renders, phone
  width, dark mode, no JS errors.
On the board: switch from each source and read the page.

## Implementation Steps
- [x] Source/time table in `web_ui`, fed from `on_relay_change()` and
      after `relay_ctrl_init()`
- [x] JSON fields in `web_ui_state_json()`; source names
- [x] Page rendering
- [x] INFO log line per change
- [x] Tests; USER_MANUAL §7.2; CHANGELOG

## Files to Create/Modify
- `components/web_ui/` (`web_ui.c/.h`, `web_ui_logic.c/.h`,
  `www/relays.html`)
- `main/main.c`
- `test/host/web_ui_test/`
- `USER_MANUAL.md`, `CHANGELOG.md`

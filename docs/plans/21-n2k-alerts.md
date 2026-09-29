# Implementation Plan: 21 — Input alarms as NMEA 2000 alerts

Follow-up to issue [#3](https://github.com/BoatHacks/signalk-espOS-8relay/issues/3)
and plan 10, which named "NMEA 2000 alerts (PGN 126983 family)" as a
separate step.

## Overview
The per-input alarm that raises a SignalK notification (plan 10) is also
raised on the NMEA 2000 bus as an alert, so a chartplotter sounds a bilge
float switch even with no SignalK server running.

## Relevant SPEC/ARCHITECTURE Sections
- SPEC.md §6.2 (PGNs), §9 (settings)
- Plan 06 (NMEA 2000 switch bank), plan 10 (input alarms)

## Approach
- **PGNs** (layouts from canboat; the pinned NMEA2000 library has no
  helpers for them, but already treats all three as fast-packet):
  - 126983 *Alert*: every 1 s while active (canboat's interval).
  - 126985 *Alert Text*: the SignalK notification's message, on raising and
    every 10 s (canboat's interval). ASCII as-is (control byte 1),
    anything else as UCS-2 (control byte 0), as the library's own
    `AddVarStr()` does.
  - 126984 *Alert Response*: received; an Acknowledge for one of ours
    turns it Acknowledged until it clears.
- **Identity.** Alert type from the input's severity (warn → Warning,
  alarm → Alarm, emergency → Emergency Alarm), category Technical, system
  and sub-system 0, alert id = input channel, data source NAME = ours,
  instance = input bank id, index = channel. A new occurrence number per
  trip, so a late acknowledge of an earlier trip doesn't acknowledge the
  new one.
- **Clearing.** 126983 state Normal, three times a second apart, then
  nothing — one frame could be lost, and a stale alert on an MFD is the
  failure that matters.
- **Severity changed while active:** the type is part of the alert's
  identity, so the old one is cleared and a new one raised.
- **Temporary silence** is not offered (support flag 0): the board has
  nothing of its own to silence, and the MFD can still acknowledge.
- **Same conditions as the SignalK notification:** only once inputs have
  settled after boot, not when the input bank is unusable (id clash).
- **Setting** `n2k_alerts` (bool, default on, applies live). Needed
  because signalk-to-nmea2000's *Notifications* conversion would otherwise
  put the same alarm on the bus a second time.
- **Code.** Plain C, host-testable: `n2k_alert_pgn.c` (payloads) and
  `n2k_alerts.c` (state, timing). `n2k_bridge.cpp` runs it on its task
  and gets settings through `n2k_bridge_update_config()`.

## Open questions
- Whether MFDs show UCS-2 text correctly. canboat has only seen control
  byte 0 on empty strings; pure-ASCII messages avoid the question.
- Acknowledging on the MFD doesn't change the SignalK notification.

## Test Strategy
Host tests in `switch_bank_pgn_test` (`test_n2k_alerts.c`):
- 126983 byte layout; 126985 ASCII, UCS-2, malformed UTF-8, truncation;
  126984 decode.
- Raise on input on, at the right type; repeat intervals; clear three
  times then silence; new occurrence per trip.
- Nothing when the alarm is off, `n2k_alerts` is off, inputs haven't
  settled, or the bank is unusable.
- Live changes: alarm or `n2k_alerts` turned off while tripped clears,
  turned on raises at once, severity change re-raises, message change
  resends the text.
- Acknowledge; ignored for other devices, stale occurrences, other types,
  silence, unknown ids, short payloads.
`device_config_test`: default on.
On the board: HARDWARE_TESTS.md E6.

## Implementation Steps
- [x] `n2k_alerts` setting
- [x] Payload encode/decode and alert state (`switch_bank`)
- [x] Wiring in `n2k_bridge` and `main.c`
- [x] Host tests
- [x] SPEC.md §6.2/§9, USER_MANUAL §6.4/§6.5/§7.3, CHANGELOG
- [ ] On a real bus: HARDWARE_TESTS.md E6

## Files to Create/Modify
- `components/switch_bank/` (`n2k_alert_pgn.*`, `n2k_alerts.*`,
  `n2k_bridge.*`, `CMakeLists.txt`)
- `components/device_config/`
- `main/main.c`
- `test/host/switch_bank_pgn_test/`, `test/host/device_config_test/`
- `SPEC.md`, `USER_MANUAL.md`, `CHANGELOG.md`, `docs/HARDWARE_TESTS.md`

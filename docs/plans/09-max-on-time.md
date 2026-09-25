# Implementation Plan: 09 — Maximum on-time per relay

Issue: [#2](https://github.com/BoatHacks/signalk-espOS-8relay/issues/2)

## Overview
A per-relay safety limit for latching relays: after being on for the
configured time, the relay switches itself off, whatever switched it on.
0 (the default) means no limit.

## Relevant SPEC/ARCHITECTURE Sections
- SPEC.md §2 (relay modes, fail-safe), §3.2
- ARCHITECTURE.md §2.1 (`relay_ctrl`)
- Plan 03 (relay control)

## Approach
- New setting per relay, `r<n>_max_on_s` (0–86400 s), in
  `relay_cfg_t.max_on_s`.
- `relay_ctrl` already runs a per-relay deadline for momentary pulses
  (`pulse_end[]`, `pulsing` mask, checked in `relay_ctrl_tick()`). Add a
  second deadline mask for latching relays with a limit, set whenever
  the relay goes off → on, and checked in the same tick.
- **An "on" command to a relay that is already on restarts the timer**
  ("I'm still here"), so an operator can keep a pump running by
  re-confirming. Document it.
- Expiry switches off with a new source, `RELAY_SRC_MAX_ON`, so
  listeners, logs and the "last switched by" display (plan 12) can say
  why.
- **Boot:** a `hold` relay restored on after a warm or cold boot starts
  a fresh timer; the time already spent before the restart is not
  known and is not stored.
- **Config changes:** setting a limit on a relay that is already on
  starts its timer now; clearing it cancels the timer. Changing the mode
  to momentary drops the limit (pulses already end).
- **Interaction with input overrides:** an input holding a relay on
  (follow mode) still gets switched off at the limit; the next input
  change applies again. Say so in the manual — this is the point of a
  safety limit.

## Test Strategy
Host tests in `relay_ctrl_test` (fake clock, fake expander):
- Relay switched on by each source turns off after exactly the limit,
  with `RELAY_SRC_MAX_ON` in the listener event.
- A repeated "on" restarts the timer; "off" cancels it.
- Limit 0 never expires; momentary relays ignore the limit.
- Setting/clearing the limit on a relay that is on.
- Hold relay restored at boot gets a fresh timer.
On the board: a 10 s limit on a lamp.

## Implementation Steps
- [x] `relay<n>_max_on_s` setting
- [x] Deadline mask and `RELAY_SRC_MAX_ON` in `relay_ctrl`
- [x] Restart-on-repeat, config-change and boot rules
- [x] Host tests
- [x] USER_MANUAL §6.3, §7.4.1; CHANGELOG

## Files to Create/Modify
- `components/device_config/`
- `components/relay_ctrl/` (`relay_ctrl.c/.h`)
- `test/host/relay_ctrl_test/`, `test/host/device_config_test/`
- `USER_MANUAL.md`, `CHANGELOG.md`

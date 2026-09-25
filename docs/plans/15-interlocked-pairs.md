# Implementation Plan: 15 — Interlocked relay pairs

Issue: [#8](https://github.com/BoatHacks/signalk-espOS-8relay/issues/8)

## Overview
Pairs of relays that must never be on together (windlass up/down, a
reversing motor's contactors). Switching one on first switches the other
off, optionally with a dead time before the second closes.

## Relevant SPEC/ARCHITECTURE Sections
- SPEC.md §2, §10.2 (pairwise interlocks brought into scope on
  2026-09-25; multi-condition rules stay deferred)
- ARCHITECTURE.md §2.1 (`relay_ctrl`)
- Plan 03

## Approach
- Setting per relay: `r<n>_interlock` (0 = none, else relay 1–8) and one
  global `interlock_dead_ms` (default 100 ms, 0–2000).
- **Validation.** espOS can't validate across keys, so `device_config`
  derives the effective pairs: a pair counts only if both sides name each
  other. A one-sided or self-referencing setting is ignored and raises a
  health warning (like `bankIdClash`), so a typo never silently leaves a
  motor unprotected without the operator seeing it.
- **Enforcement in `relay_ctrl`, under its lock,** so no source can
  bypass it: SignalK, NMEA 2000, web, inputs, "All on", schedules later.
  - "On" to relay A while its partner B is on: B off now; A on after the
    dead time (a pending-on deadline checked in the tick, like pulses).
    The command succeeds; listeners see B off, then A on.
  - "All on" (web page and `PUT /api/v1/relays`) — *decided
    2026-09-25:* skips every relay that is in an interlocked pair and
    switches the rest on. The response lists the skipped relays and the
    page says so ("All on: relays 3 and 4 skipped, interlocked").
  - A pending-on is cancelled by a later "off" to A or "on" to B.
- **Boot and hold:** if the stored state has both partners on (older
  firmware, or a config change), restore neither and raise a warning.
  Input overrides at boot obey the same rule.
- **Config change while both are on:** switch both off and warn — the
  safe choice when the setup just changed.
- Momentary relays can be interlocked too (a jog up/down pair).

## Test Strategy
Safety-relevant, so thorough host tests in `relay_ctrl_test`:
- Each source switching A on while B is on: B off first, A on only after
  the dead time, never both on in any expander write (assert on every
  write the fake expander receives).
- Pending-on cancelled by off / by the partner's on.
- One-sided or self pairs ignored + health warning.
- Boot/hold with both stored on; input overrides at boot.
- Config change with both on.
- All on.
On the board: two lamps as a pair, hammering both from the web page and
N2K at once; scope the relay outputs for overlap if possible.

## Implementation Steps
- [ ] Settings and pair derivation/validation in `device_config`
- [ ] Enforcement, dead time and pending-on in `relay_ctrl`
- [ ] Boot/hold/config-change rules
- [ ] Health warning wiring
- [ ] Host tests (every expander write checked)
- [ ] SPEC.md §2/§10.2, USER_MANUAL §6.3; CHANGELOG

## Files to Create/Modify
- `components/device_config/`
- `components/relay_ctrl/`
- `main/main.c` (health)
- `test/host/relay_ctrl_test/`, `test/host/device_config_test/`
- `SPEC.md`, `USER_MANUAL.md`, `CHANGELOG.md`

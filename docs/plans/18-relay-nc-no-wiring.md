# Implementation Plan: 18 — Per-relay NC/NO wiring setting

Issue: [#13](https://github.com/BoatHacks/signalk-espOS-8relay/issues/13)

## Overview
Each relay has three screw terminals (NO/COM/NC), but the firmware only
accounts for a load wired to NO: reported/commanded `on`/`off` is the
coil-driven state directly. A load wired to NC (e.g. a bilge pump or nav
light meant to keep running through total power loss) has no way to tell
the firmware that "coil off" means "load on". Add a per-relay wiring
setting that corrects reporting and PUT commands only, while every
fail-safe/boot-default code path keeps operating on the coil, unchanged.

## Relevant SPEC/ARCHITECTURE Sections
- SPEC.md §3.1 (relay `state`), §3.2 (`default-safe` fail-safe policy),
  §4 (`RelayChannel` settings, next to `overrideDI`)
- SPEC.md §4 `invert` (the equivalent, already-shipped input setting this
  mirrors)
- ARCHITECTURE.md §2.1 (`relay_ctrl`)
- Plan 03 (relay control), plan 04 (`invert` precedent for digital inputs)

## Approach
- Add `wiredNC: bool` (default `false`) to `RelayChannel`, next to
  `overrideDI`, in `device_config`.
- **The split that isn't a copy-paste of `invert`** (per the issue's own
  analysis): `wiredNC` affects only the SignalK/N2K/web-UI reporting and
  PUT-command boundary in `relay_ctrl` — translating logical (load) state
  to/from coil state there, the same shape as `input_sense`'s `invert`
  translation (`components/input_sense/src/input_sense.c:31`,
  `energised != invert`).
- **Every fail-safe/boot-default/momentary-auto-off/`hold`-restore path
  keeps commanding the coil directly, unconditionally**, regardless of
  `wiredNC`. This is the one behavior that must match what an actual
  power loss does (de-energized coil), which is the whole point of
  `default-safe` — get this backwards and a NC-wired load loses exactly
  the protection it was wired that way to get, silently, only surfacing
  during a real emergency.
- With the split in place, an NC-wired `default-safe` relay correctly
  *reports* `on` right after a fail-safe transition (de-energized coil =
  NC closed = load powered) — that's correct and must be surfaced as
  such, not hidden or re-inverted a second time.
- `lastSource`/`lastChangeAgoS` (plan 12) keep tracking the coil-level
  change event; only the *value* shown/reported flips for a NC relay.

## Decisions
- Setting name: `wiredNC` (bool, default `false`), matching input's
  `invert` in shape but scoped to relays and named for what's actually
  different (input `invert` is about sensor polarity; this is about
  which physical terminal a load sits on).
- Translation boundary is `relay_ctrl`'s public get/set/PUT surface only,
  not a new component — same reasoning as `invert` living in
  `input_sense` rather than a separate layer.
- **`wiredNC` + momentary (2026-09-27):** allowed, translated
  consistently, no special-casing. A momentary relay's "pulse" command
  still just means "flip the coil for `pulseMs`"; reporting and
  commanding translate through `wiredNC` the same as a latching relay.
  For a NC-wired momentary relay this means a pulse is a brief load-OFF
  blip rather than load-ON — an unusual but real use case (e.g.
  momentarily killing power to reset something downstream), and
  consistent/predictable beats special-casing it away. `device_config`
  does not reject or warn on the combination.

## Test Strategy
Host tests in `relay_ctrl_test`:
- A `wiredNC` relay reports the inverted coil state via every read path
  (`GET /api/v1/relays`, SignalK, N2K status).
- Setting/clearing `on` via SignalK/N2K/web PUT on a `wiredNC` relay
  drives the coil to the *opposite* level, and reports match afterwards.
- **The critical case:** `default-safe` fail-safe (SignalK loss), boot
  default, and momentary auto-off all still command the coil directly
  regardless of `wiredNC` — assert the actual TCA9554 write, not the
  reported value, for each of these paths with `wiredNC` both on and off.
- `hold`-restore reads/writes the coil-level bit unchanged; the reported
  value after restore reflects `wiredNC` correctly.
- Flipping `wiredNC` live (like `invert`) changes what an unchanged coil
  level is reported as, without switching anything.
On the board: one relay wired to NC (dry contact opens the circuit when
energized) and one to NO on the same expander line, wiredNC set only on
the NC one, confirm reported/commanded state via SignalK matches the
load's actual behavior for both, including through a factory reset back
to defaults (which must NOT retroactively re-wire; `wiredNC` and the
physical wiring are independent facts).

## Implementation Steps
- [ ] `wiredNC` setting in `device_config` / `RelayChannel`
- [ ] Translation at `relay_ctrl`'s report/PUT boundary
- [ ] Confirm every fail-safe/boot/momentary/`hold` path bypasses the
      translation (audit, don't just add a flag and hope)
- [ ] Host tests (translation + the coil-level fail-safe invariant)
- [ ] SPEC.md §4, USER_MANUAL settings reference; CHANGELOG
- [ ] On-board check with one NC-wired and one NO-wired relay

## Files to Create/Modify
- `components/device_config/`
- `components/relay_ctrl/`
- `test/host/relay_ctrl_test/`
- `SPEC.md`, `USER_MANUAL.md`, `CHANGELOG.md`

# Implementation Plan: 08 — Push-button toggle for inputs

Issue: [#1](https://github.com/BoatHacks/signalk-espOS-8relay/issues/1)

## Overview
A relay linked to an input (`override_di`) follows it today: input on →
relay on, input off → relay off. Add a per-relay link mode, *toggle*,
where each press of a momentary push button flips the relay and the
release does nothing.

## Relevant SPEC/ARCHITECTURE Sections
- SPEC.md §2 (override precedence), §3.1, §9 (`overrideDI`)
- ARCHITECTURE.md §2.2 (`input_sense`)
- Plan 04 (digital inputs)

## Approach
- New setting per relay, `r<n>_link` (`follow` default, `toggle`), next
  to `r<n>_input` in `config/swbank.json`; `relay_cfg_t.link_mode` in
  `device_config`.
- `input_sense` keeps calling its override callback on every debounced
  edge, but passes the edge and the relay's link mode. For *toggle* it
  acts only on the rising edge (after invert) and asks for "the opposite
  of the relay's current state". The callback therefore becomes
  `override(relay, action)` with `action` ∈ {on, off, toggle};
  `main.c` maps toggle to `relay_ctrl_set(relay, !relay_ctrl_get(relay),
  RELAY_SRC_INPUT)`.
- **Boot:** today a linked relay takes its input's state once inputs
  settle. In toggle mode the input's level says nothing about the relay,
  so the boot override is skipped; the relay keeps its fail-safe/hold
  state. A button held down at power-up must not toggle.
- **Other sources:** a SignalK/NMEA 2000/web command still works; the
  next press toggles from whatever state the relay is in.
- **Momentary relays** in toggle mode: a press starts a pulse (toggle of
  an off momentary relay = on); pressing again during the pulse turns it
  off early. Document it.
- **One input, several relays:** each relay toggles from its own state;
  document that mixed states stay mixed.

## Open questions
- Should a long press do something else (e.g. all linked relays off)?
  Not in this plan; note it in the issue if wanted.

## Test Strategy
Host tests in `input_sense_test` with the fake clock:
- A press toggles once; the release does nothing.
- Bounce within the debounce time doesn't double-toggle.
- Invert: with an NC button the "press" is the falling pin level.
- No action at boot in toggle mode, even with the input already on.
- Follow mode unchanged (existing tests stay green).
`device_config_test`: default `follow`, invalid value reads as default.
On the board: a real push button on DI1 toggling relay 1.

## Implementation Steps
- [ ] `r<n>_link` setting and `relay_cfg_t.link_mode`
- [ ] Override callback carries on/off/toggle; `main.c` maps toggle
- [ ] Rising-edge-only handling and no boot override for toggle links
- [ ] Host tests
- [ ] Relay page: show "toggled by input n" (links with plan 12)
- [ ] USER_MANUAL §6.3, §7.4; CHANGELOG

## Files to Create/Modify
- `components/device_config/` (`config/swbank.json`, header, loader)
- `components/input_sense/` (`input_sense.c/.h`)
- `main/main.c`
- `test/host/input_sense_test/`, `test/host/device_config_test/`
- `USER_MANUAL.md`, `CHANGELOG.md`

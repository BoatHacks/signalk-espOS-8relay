# Implementation Plan: 04 — Digital inputs

## Overview
Build `input_sense`: read the 8 isolated inputs, debounce and optionally
invert them, notify listeners on change, and apply configured
input-to-relay overrides.

## Relevant SPEC/ARCHITECTURE Sections
- SPEC.md §2 (override precedence), §3.1, §4 (DigitalInputChannel), §9
  (`debounceMs`, `invert`)
- ARCHITECTURE.md §2.2 (`input_sense`)

## Approach
Inputs are GPIO4–GPIO11 behind optocouplers. Poll all 8 every 5 ms from
one `esp_timer`, rather than using interrupts: contact bounce would cause
interrupt storms, and polling makes the debounce logic a pure function
that host tests can drive with a fake clock. A reading counts as a change
once it has been stable for `debounceMs`. `invert` is applied after
debouncing. Whether the raw level is active-low (typical for opto inputs)
is confirmed on hardware.

**Overrides.** Each relay may name one input (`overrideDI`); one input
may drive several relays. On a debounced change, each relay mapped to
that input is set to the input's state via `relay_ctrl_set(…,
SOURCE_DI)`. Remote commands still win until the input changes again
(SPEC.md §2).

**Boot.** Once all inputs have their first stable reading (one
`debounceMs` after start), apply every mapped override. This runs after
`relay_ctrl`'s fail-safe restore, so an override input beats the stored
state on boot, as SPEC.md §2 requires.

## Test Strategy
Host tests with fake GPIO and clock:
- Bounce shorter than `debounceMs` is ignored; a stable change is
  reported once.
- `invert` flips the reported state.
- A mapped input sets its relays on each change; unmapped inputs don't.
- Boot applies overrides after the first stable reading, not before.
- A remote command after an override stands until the next input change.
On hardware (plan 07): check 50 ms against a real float switch.

## Implementation Steps
- [ ] Input pins in `board`, behind a read interface with a host fake
- [ ] Debounce and invert logic
- [ ] Listener API for `switch_bank`
- [ ] Override application, including boot
- [ ] Host tests

## Files to Create/Modify
- `components/board/` (input pins)
- `components/input_sense/` (`input_sense.c/.h`)
- `test/host/test_input_sense.c`

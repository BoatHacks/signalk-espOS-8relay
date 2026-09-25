# Implementation Plan: 14 — BOOT button: setup access point and factory reset

Issue: [#7](https://github.com/BoatHacks/signalk-espOS-8relay/issues/7)

## Overview
Use the BOOT button (GPIO 0), which the firmware ignores today: a long
press (~5 s) reopens the setup access point, a very long press (~15 s)
resets all settings to their defaults. The status LED shows which action
a release will trigger.

## Relevant SPEC/ARCHITECTURE Sections
- README "Board hardware" (BOOT button: *Partly*)
- USER_MANUAL §4 (first-time setup)

## Approach
- **Reading the button.** GPIO 0 is a strapping pin: held low at reset
  it enters the USB bootloader. Configure it as an input with pull-up
  only (never drive it), poll it from the I/O task every 10 ms, debounce
  it, and ignore a press that is already held when the firmware starts.
  Act on **release**, so the user can let go when the LED shows the
  wanted action.
- **Feedback:** while held, the LED blinks white after 5 s ("release
  for access point") and red after 15 s ("release for factory reset").
  Needs a small "override pattern" input to `indicator`.
- **Reopen the access point.** espOS has no public "start portal now"
  call (checked in 0.10.3: `espos_wifi` opens its portal on its own,
  `portal_after_s` after the station fails, or when none is configured).
  Options, in order of preference:
  1. Ask upstream for an `espos_wifi_portal_open(duration)` API and use
     it once available.
  2. Meanwhile: turn the station off for this boot only
     (`sta_enabled=false` in RAM, not saved) and restart WiFi, so the
     portal opens; a restart returns to normal. Needs checking whether
     espOS allows a non-persistent override; if not, use option 3.
  3. Save `sta_enabled=false`, restart, and let the portal's own save
     turn the station back on. Riskier: document clearly.
- **Factory reset:** `espos_config_factory_reset()` (exists in 0.10.3),
  also clear the saved `hold` relay state and counters (plan 11), then
  `esp_restart()` so boot and fail-safe rules apply to the relays.
- Short presses do nothing, so an accidental knock is harmless.

## Open questions
- Which portal option espOS supports (see above) — decide before coding.
- Should a factory reset keep the SignalK token? No: "factory" means
  everything; say so in the manual.

## Test Strategy
- Host tests for a pure `button` state machine: debounce, held-at-boot
  ignored, 5 s / 15 s thresholds, action on release only, short press
  ignored.
On the board: both actions; power up with the button held (must enter
the bootloader and not trigger anything later); relays after a factory
reset follow boot rules.

## Implementation Steps
- [ ] Resolve the portal question with espOS
- [ ] Button state machine (host-tested) and GPIO 0 input in `board`
- [ ] Indicator override pattern
- [ ] Actions: portal, factory reset (+ hold state, counters), restart
- [ ] USER_MANUAL §4, §8; README hardware table; CHANGELOG

## Files to Create/Modify
- `components/board/` (pin), new `components/button/` or in `main`
- `components/indicator/`
- `main/main.c`
- `test/host/button_test/` (new)
- `USER_MANUAL.md`, `README.md`, `CHANGELOG.md`

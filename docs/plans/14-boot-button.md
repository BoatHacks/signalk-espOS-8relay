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
- **Reopen the access point** — *decided 2026-09-25:* save
  `wifi.sta_enabled=false` and restart. espOS 0.10.3 has no public "start
  portal now" call; with the station off it opens its portal. Saving a
  network in the portal turns the station back on.
  - The risk: if nobody completes the portal, the board stays off WiFi
    (Ethernet, NMEA 2000, inputs and relays keep working). Mitigate: the
    LED shows a distinct "portal open" colour, the buzzer (if enabled)
    beeps "ESP AP", and the manual says how to finish or undo it.
  - Check first that the portal's save really sets `sta_enabled=true`
    again; if it doesn't, set it from our side on the portal's
    "network saved" event.
  - Still worth an upstream request for `espos_wifi_portal_open()`;
    switch to it when it exists.
- **Factory reset:** `espos_config_factory_reset()` (exists in 0.10.3),
  also clear the saved `hold` relay state and counters (plan 11), and
  **forget the SignalK token** (`espos_sk_forget_token()`, *decided
  2026-09-25*: factory means everything, the board must be approved on
  the server again), then `esp_restart()` so boot and fail-safe rules
  apply to the relays.
- Short presses do nothing, so an accidental knock is harmless.

## Decisions (2026-09-25)
- Access point: save the station off and restart (see Approach).
- Factory reset forgets the SignalK token.

## Test Strategy
- Host tests for a pure `button` state machine: debounce, held-at-boot
  ignored, 5 s / 15 s thresholds, action on release only, short press
  ignored.
On the board: both actions; power up with the button held (must enter
the bootloader and not trigger anything later); relays after a factory
reset follow boot rules.

## Implementation Steps
- [x] Portal approach decided (save station off)
- [ ] Check that the portal's save turns the station back on
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

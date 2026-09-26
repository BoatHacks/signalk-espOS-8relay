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
- **Feedback:** while held, the LED blinks **white** after 5 s ("release
  for access point") and blinks **red** after 15 s ("release for
  factory reset") — *decided 2026-09-26.* White is a new indicator
  state (the existing 4-color palette in `indicator_logic.c` is green/
  blue/amber/solid-red; none of those fit "about to open the portal").
  Red is reused for its "destructive action" association, but blinking
  rather than solid, so it reads as distinct from `INDICATOR_ALARM`'s
  solid red at a glance. Needs a small "override pattern" input to
  `indicator`.
- **Reopen the access point** — *decided 2026-09-25:* save
  `wifi.sta_enabled=false` and restart. espOS 0.10.3 has no public "start
  portal now" call; with the station off it opens its portal.
  - **Confirmed on hardware (2026-09-26): saving a network in the portal
    does NOT turn the station back on by itself.** The portal's page has
    a separate "station enabled" checkbox the person must also tick; a
    network saved without ticking it leaves the board stuck in AP mode.
    For a board that got here by losing network access, expecting a
    person to notice and check an unrelated box is exactly the kind of
    friction this feature exists to remove.
    **Decision needed:** implement the plan's own fallback — hook the
    portal's "network saved" event and force `sta_enabled=true`
    ourselves, so completing setup always turns the station on. This
    can live entirely in this project (subscribe to whatever event/
    config-change espOS exposes for a portal save) or, since the
    behavior is arguably an espOS portal UX gap and not specific to
    this board, also be raised upstream.
  - The risk: if nobody completes the portal, the board stays off WiFi
    (Ethernet, NMEA 2000, inputs and relays keep working). Mitigate: the
    LED shows the new "portal open" colour, the buzzer (if enabled)
    beeps "ESP AP", and the manual says how to finish or undo it.
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

## Decisions (2026-09-26)
- 5 s (portal) feedback: new indicator state, blinking white.
- 15 s (factory reset) feedback: blinking red, not solid — solid red
  already means `INDICATOR_ALARM`.
- Confirmed on hardware: the portal does not re-enable the station on
  its own (separate checkbox). This project will force
  `sta_enabled=true` on the portal's "network saved" event rather than
  rely on the person noticing the checkbox. Whether to also report this
  upstream (espOS's portal arguably shouldn't require it) is still
  open — not blocking, since the fix works either way.

## Test Strategy
- Host tests for a pure `button` state machine: debounce, held-at-boot
  ignored, 5 s / 15 s thresholds, action on release only, short press
  ignored.
On the board: both actions; power up with the button held (must enter
the bootloader and not trigger anything later); relays after a factory
reset follow boot rules.

## Implementation Steps
- [x] Portal approach decided (save station off)
- [x] Checked whether the portal's save turns the station back on —
      confirmed on hardware it does not; this project must force it
      (see Decisions, 2026-09-26)
- [ ] Force `sta_enabled=true` on the portal's "network saved" event
- [ ] Button state machine (host-tested) and GPIO 0 input in `board`
- [ ] Indicator override pattern: new blinking-white state (portal) and
      blinking-red pattern reusing the alarm color (factory reset)
- [ ] Actions: portal, factory reset (+ hold state, counters), restart
- [ ] USER_MANUAL §4, §8; README hardware table; CHANGELOG

## Files to Create/Modify
- `components/board/` (pin), new `components/button/` or in `main`
- `components/indicator/`
- `main/main.c`
- `test/host/button_test/` (new)
- `USER_MANUAL.md`, `README.md`, `CHANGELOG.md`

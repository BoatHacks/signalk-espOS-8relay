# Implementation Plan: 17 — Buzzer test button and configurable frequency

Issue: [#10](https://github.com/BoatHacks/signalk-espOS-8relay/issues/10)

## Overview
A *Test buzzer* button on the relay page that plays the alarm pattern
once, and a *Buzzer frequency* setting instead of the fixed 2700 Hz.

## Relevant SPEC/ARCHITECTURE Sections
- USER_MANUAL §6.5, §7.6 (LED and buzzer)
- `components/indicator/` (`indicator.c`, `indicator_logic.c`)

## Approach
- **Play once.** The indicator task already builds the Morse segments
  (`indicator_alarm_text()`, `morse_encode()`) and plays them in a loop
  with a pause (`morse_tone_at()`). Add `indicator_test_buzzer()` which
  sets an atomic request; the task then plays one pass of the same
  pattern (duration = the segments' total, no repeat) and stops.
  - Works even when *Buzzer on alarm* is off.
  - Refused (`ESP_ERR_INVALID_STATE`) while a real alarm is sounding or
    a test is already playing.
  - Add a pure helper `morse_duration_ms(segs, n, unit)` to
    `indicator_logic` for the one-pass length.
- **Endpoint:** `POST /api/v1/buzzer/test` in `web_ui` (protected,
  `espos_httpd_require_json()`), 202 when started, 409 when refused. The
  page gets a *Test buzzer* button in the header area.
- **Frequency setting:** `buzzer_freq_hz`, default 2700, range 1000–5000
  Hz. Applied live with `ledc_set_freq()` from `indicator_update_config()`
  (under the indicator task's control, not from the config task
  directly, to avoid racing a tone). 50 % duty at every frequency; with
  `LEDC_TIMER_10_BIT` on the 80 MHz APB clock the range fits.
- Tuning flow in the manual: change the frequency, save, press Test.

## Test Strategy
- `indicator_test`: `morse_duration_ms` for "ESP 42" and "ESP AP"; a
  play-once state machine (if factored out) plays exactly one pass and
  refuses while an alarm is active.
- `device_config_test`: default 2700, out-of-range reads as default.
- `web_ui_test`: none needed beyond the handler (thin); browser check
  of the button against the mock.
On the board: test with the setting off, during an alarm (refused),
frequencies at both ends of the range audible.

## Implementation Steps
- [x] `buzzer_freq_hz` setting; live `ledc_set_freq()`
- [x] Play-once request and `morse_duration_ms`
- [x] `POST /api/v1/buzzer/test` and page button
- [ ] Host tests; browser check
- [ ] USER_MANUAL §6.5, §7.6; CHANGELOG

## Files to Create/Modify
- `components/indicator/` (`indicator.c/.h`, `indicator_logic.c/.h`)
- `components/device_config/`
- `components/web_ui/` (endpoint, page)
- `test/host/indicator_test/`, `test/host/device_config_test/`
- `USER_MANUAL.md`, `CHANGELOG.md`

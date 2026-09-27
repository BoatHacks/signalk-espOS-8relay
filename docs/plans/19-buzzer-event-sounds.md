# Implementation Plan: 19 — Buzzer sound effects on events

Issue: [#14](https://github.com/BoatHacks/signalk-espOS-8relay/issues/14)

## Overview
Play a short tone over the buzzer on notable events — boot, each relay
on/off, each digital input level change — separate from the existing
alarm pattern (`INDICATOR_ALARM`, "ESP"/"ESP AP" Morse) and the manual
test tone (`POST /api/v1/buzzer/test`). Distinct from plan 17 (buzzer
test button and frequency, issue #10), which only added a manual trigger
and a frequency setting, not automatic per-event sounds.

## Relevant SPEC/ARCHITECTURE Sections
- ARCHITECTURE.md §2.x (`indicator`: status LED + alarm buzzer)
- `components/indicator/src/indicator.c` (alarm pattern, buzzer
  enable/frequency, `indicator_set_override` added in plan 14/#7)
- `components/indicator/include/indicator_logic.h` (`indicator_override_t`
  from #7: `INDICATOR_OVERRIDE_NONE/PORTAL/RESET`)
- Plan 14 (BOOT button indicator override — the precedent for adding a
  new indicator/buzzer state without breaking the alarm)
- Plan 17 (buzzer test + frequency setting)

## Approach (open questions first — decide before implementing)
- **Coexistence with the alarm and the #7 override.** Priority order,
  highest first: `INDICATOR_OVERRIDE_*` (BOOT button held) > alarm > an
  event chirp. A chirp must never play over, or be masked confusingly
  by, either — likely: skip a chirp entirely if the override or alarm is
  currently active, rather than queueing it.
- **Settings shape.** Needs its own on/off, separate from
  `buzzer_on_alarm` — a boat is a quiet-hours environment. Open question
  for the interactive session: one `buzzer_on_event` toggle covering all
  three event categories (boot/relay/input), or one per category? Start
  with the simpler single toggle unless there's a concrete reason to
  split.
- **Tone design.** Short, distinguishable chirps per category (not the
  alarm's Morse pattern): e.g. a single rising beep for boot, a short
  double-beep for relay-on vs. a short falling beep for relay-off, a
  distinct third tone for input changes. Needs an actual sound-design
  pass against the existing `buzzer_freq_hz` setting (reuse it as a base
  frequency, offsetting per tone, rather than adding N new frequency
  settings).
- **Which relay sources chirp.** Relay changes carry a `lastSource`
  (SignalK, N2K, web, input, pulse end, fail-safe, max on-time, start-up;
  plan 12). Open question: chirp on every source, or suppress it for
  high-frequency/automatic ones (fail-safe, max-on-time) where a chirp
  might be more alarming than useful? Lean towards chirping only on
  direct commands (SignalK, N2K, web, input) and not on the automatic
  ones, but confirm with the user before implementing.
- **Input debounce.** A chirp fires only on the debounced, reported
  input state change (the same event `on_input_change`/`sk_bridge_input_
  changed` already receives), never on raw bounce.
- **Implementation shape.** A small event-to-tone mapping in `indicator`,
  fed from the same places that already call `sk_bridge_input_changed`
  and the relay-change listener in `main.c`, playing a short one-shot
  tone (not the looping alarm pattern) on the buzzer GPIO, gated by the
  new setting and the priority order above.

## Test Strategy
- Host tests for the priority/gating logic: no chirp while an
  `indicator_override_*` is active or the alarm is sounding; chirp
  plays once per qualifying event; setting off means no chirp at all.
  (Buzzer PWM/GPIO output itself is hardware, so tests cover the
  decision logic and the requested tone/duration, like plan 17's
  frequency-setting tests, not the physical sound.)
- Host tests for input debounce gating (only reported changes chirp) and
  for which relay sources chirp per the decision above.
On the board: boot chirp audible on power-up once connected; each
relay-on/off and input-change chirp audible and distinguishable by ear;
confirm no chirp plays while the BOOT-button override LED is blinking or
while a real alarm is sounding.

## Implementation Steps
- [ ] Resolve open questions (settings shape, which sources chirp, tone
      design) — interactive session, before coding
- [ ] Event-to-tone mapping + priority/gating in `indicator`
- [ ] Wire boot, relay-change and input-change events into it
- [ ] New setting(s) alongside `buzzer_on_alarm`/`buzzer_freq_hz`
- [ ] Host tests (gating/priority, debounce, per-source chirping)
- [ ] USER_MANUAL, SPEC.md settings reference; CHANGELOG
- [ ] On-board check: all three event types, and non-interference with
      the alarm and the BOOT-button override

## Files to Create/Modify
- `components/indicator/`
- `main/main.c` (event wiring)
- `components/device_config/` (new setting(s))
- `test/host/indicator_test/` (or equivalent, new if it doesn't exist)
- `USER_MANUAL.md`, `SPEC.md`, `CHANGELOG.md`

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

## Decisions (resolved in the interactive session — scope grew from the
original sketch below; this supersedes it)

Tones are **RTTTL** (Ring Tone Text Transfer Language) strings, stored as a
**named library** the user maintains through a new **"Tones" web page**
(`/tones`), and every event (boot, AP-portal, factory-reset, each relay's
on/off, each input's change) picks its tone from that library via a
dropdown. This is materially bigger than the original plan (which assumed a
couple of new frequency-offset settings): it adds an RTTTL parser, a
variable-length named-pattern store, and a new CRUD web page.

- **Coexistence with the alarm and the #7 override** (unchanged from the
  original sketch, not re-litigated): priority highest to lowest is
  `INDICATOR_OVERRIDE_*` (BOOT button held) > alarm > event chirp. A chirp
  is skipped entirely (not queued) if the override or alarm is active when
  it would start; an already-playing chirp is cut off if the alarm/override
  takes over mid-chirp.
- **Settings shape.**
  - One global `buzzer_on_event` toggle: master on/off for all chirps
    (separate from `buzzer_on_alarm` — a boat is a quiet-hours
    environment).
  - A named tone-pattern library: `tone_patterns`, a table-format string
    setting (espOS's `x-espos-format: "table"`, columns `name`/`rtttl`),
    edited through the new Tones page (CRUD: add/rename/edit/delete rows).
  - Three boot-family event settings, each a dropdown of library tone names
    plus "(none)" for off: `boot_tone` (power-on), `portal_tone` (BOOT
    button → setup access point, issue #7), `factory_reset_tone` (BOOT
    button → factory reset, issue #7). Adding portal/reset tones expands
    the original issue #14 scope (which only asked for power-on boot) —
    confirmed with the user as in-scope for this issue.
  - Per-channel dropdowns, each "(none)" or a library tone name:
    `relay<N>_on_tone` / `relay<N>_off_tone` for N=1..8, `input<N>_tone`
    for N=1..8 (one tone per input channel, fired on any debounced level
    change — not separate high/low tones, matching the original plan's
    "a distinct third tone for input changes").
  - "(none)"/off is represented by an empty string, not a separate enable
    flag — consistent with existing conventions in this config
    (`relay<N>_override_di` 0 = none, `relay<N>_max_on_s` 0 = no limit).
- **Which relay sources chirp.** Direct commands only — SignalK, N2K, web,
  input-override — not the automatic ones (pulse end, fail-safe,
  max-on-time). (Relay boot-state application never reaches the
  relay-change listener at all — see `main.c`'s `start_io()` — so start-up
  needed no special-casing.)
- **Input debounce.** A chirp fires only on the debounced, reported input
  state change (the same event `on_input_change`/`sk_bridge_input_changed`
  already receives), never on raw bounce.
- **Tone design / defaults.** Short, functional beeps (not melodies), all
  under ~1 s, built as RTTTL so the user can also write their own. Shipped
  defaults (also the `tone_patterns` table's out-of-the-box rows):
  - `boot`: ascending 3-note blip (cheerful, "I'm up")
  - `portal`: a short repeating trill (distinct "waiting for you" feel,
    different in character from `boot` and `reset` so the three
    boot-family events are tellable apart by ear)
  - `reset`: a short descending phrase (deliberately "heavier"/more final,
    since a factory reset is destructive)
  - `relay-on`: a short high blip
  - `relay-off`: a short low blip
  - `input`: a short neutral double-blip
  Every relay's `_on_tone`/`_off_tone` and every input's `_tone` default to
  `relay-on`/`relay-off`/`input` respectively; `boot_tone`/`portal_tone`/
  `factory_reset_tone` default to `boot`/`portal`/`reset`.
- **Implementation shape.**
  - A pure RTTTL parser (`rtttl.h`/`.c`, new files in `components/
    indicator/`): parses `name:d=..,o=..,b=..:notes` into a
    `{freq_hz, duration_ms}` note array, mirroring the existing Morse
    encoder's shape (`morse_seg_t` → `rtttl_note_t`) so the same
    host-testable, hardware-free pattern applies.
  - `indicator_logic`/`indicator` gain a small named-tone table (parsed
    from the `tone_patterns` config string with cJSON, `components/
    indicator`'s new `PRIV_REQUIRES espressif__cjson`, same as `web_ui`)
    and one-shot chirp playback reusing the existing "test buzzer"
    play-once machinery and priority/gating logic already in
    `indicator_task()`. Unlike the alarm/test tone (fixed `buzzer_freq_hz`
    for the whole message), a chirp's *frequency* varies per RTTTL note, so
    the buzzer's applied PWM frequency now also updates while a chirp
    plays, not just from the `buzzer_freq_hz` setting.
  - New `indicator_play_event(event)` (boot/portal/factory-reset) and
    `indicator_play_relay_tone(channel, on)` / `indicator_play_input_tone
    (channel)`, called from `main.c`'s `app_main()` (after `indicator_
    start()`), `do_reopen_portal()`/`do_factory_reset()` (before
    `esp_restart()`, with a short fixed `vTaskDelay` so the chirp is
    actually heard before the reboot — the indicator task's own polling
    loop would otherwise never get scheduled in time), `on_relay_change()`
    (gated by a new pure `relay_ctrl_source_chirps(relay_source_t)` in
    `relay_ctrl` alongside its already-host-tested logic), and `on_input_
    change()`.
  - New `/tones` web page (`components/web_ui/www/tones.html`, registered
    in `web_ui.c` like `/relays`): CRUD for the `tone_patterns` table plus
    dropdowns for every event/channel setting, populated from the current
    library. No new REST endpoints — it reads/writes through the existing
    generic `GET`/`PUT /api/v1/config?ns=swbank` (`espos_httpd`'s
    `api_config.c`), the same mechanism the schema-driven settings page
    already uses.

## Test Strategy
- Host tests for `rtttl_parse`/`rtttl_duration_ms`/`rtttl_tone_at`: known
  RTTTL strings decode to the expected notes/frequencies/durations;
  malformed input parses to zero notes.
- Host tests for `indicator_parse_tones`/`indicator_find_tone`: a table
  JSON string decodes to named tones, skipping rows with an invalid name
  or unparseable RTTTL; lookup by name, empty name, and unknown name.
- Host tests for the priority/gating logic: no chirp starts while an
  `indicator_override_*` is active or the alarm is sounding; a chirp
  already playing is cut off if the alarm/override takes over; `buzzer_
  on_event` off means no chirp at all; a chirp plays once, not looping.
  (Buzzer PWM/GPIO output itself is hardware, so tests cover the decision
  logic and the requested tone/duration, like plan 17's frequency-setting
  tests, not the physical sound.)
- Host test for `relay_ctrl_source_chirps`: true for SignalK/N2K/web/input,
  false for pulse-end/fail-safe/max-on-time.
- Input debounce needs no new test: `on_input_change`/`sk_bridge_input_
  changed` already only fire on the debounced, reported state change
  (existing `input_sense` behaviour), and the chirp call sits right next
  to that existing call.
On the board: boot chirp audible on power-up once connected; BOOT-button
portal and factory-reset chirps audible (and different from each other and
from boot) before the board restarts; each relay-on/off and input-change
chirp audible and distinguishable by ear; confirm no chirp plays while the
BOOT-button override LED is blinking or while a real alarm is sounding; the
Tones page: add/edit/delete a tone pattern, and confirm the event/channel
dropdowns pick it up.

## Implementation Steps
- [x] Resolve open questions (settings shape, which sources chirp, tone
      design) — interactive session, before coding
- [x] `rtttl.h`/`.c`: pure RTTTL parser (`components/indicator/`)
- [x] `indicator_logic`: named-tone table parsing (cJSON) + lookup
- [x] `indicator`/`indicator.h`: chirp playback state machine (priority/
      gating, variable-frequency one-shot playback), `indicator_play_event`/
      `indicator_play_relay_tone`/`indicator_play_input_tone`
- [x] `device_config`: `buzzer_on_event`, `tone_patterns` (table), boot/
      portal/factory_reset tone settings, per-relay on/off + per-input tone
      settings, all in `swbank.json` + `device_config_t`/`device_config_load`
- [x] `relay_ctrl`: `relay_ctrl_source_chirps(relay_source_t)`
- [x] Wire boot (`app_main`), portal/factory-reset (`do_reopen_portal`/
      `do_factory_reset`, with the pre-restart delay), relay-change and
      input-change events into `indicator_play_*` in `main.c`
- [x] `components/web_ui/www/tones.html` + a `/tones` route in `web_ui.c`
- [x] Host tests (RTTTL parsing, tone-table parsing, gating/priority,
      per-source chirping) — written; not yet run (no ESP-IDF toolchain in
      this environment, see below)
- [x] USER_MANUAL, CHANGELOG. SPEC.md is intentionally untouched: it's
      scoped to the MVP spec, and neither #7 (BOOT button) nor #10 (buzzer
      test/frequency) touched it either — CHANGELOG is where this kind of
      addition is recorded.
- [x] Host tests actually run (`test/host/run_all.sh`): all 9 host-test
      projects pass (58 test cases, 0 failures), including the new ones. A
      full `idf.py build` for the real board target also succeeds.
- [x] Flashed to real hardware: found and fixed a crash loop
      (`TG1WDT_SYS_RST` every ~1.5 s) from `device_config_t` growing by
      ~4.6 KB for `tone_patterns` — two call sites held it (or a same-size
      scratch array) as a stack local on a task with a stack too small for
      that, corrupting memory. Neither host test caught it: host threads
      have generously sized stacks, so this class of bug only shows up on
      the real target. Fixed (`io_task`'s per-reload copy and the
      tone-table parse buffer are now `static`); board now boots cleanly
      and repeatedly with no resets.
- [ ] On-board check (needs a person at the board — network reachability
      and audible chirps aren't checkable from here): all boot-family +
      relay + input chirps, non-interference with the alarm and the
      BOOT-button override, and the Tones page's CRUD + dropdowns

## Files to Create/Modify
- `components/indicator/` (`rtttl.h`/`.c` new; `indicator_logic.*`,
  `indicator.c`/`.h` extended)
- `components/relay_ctrl/` (`relay_ctrl_source_chirps`)
- `main/main.c` (event wiring)
- `components/device_config/` (new settings)
- `components/web_ui/` (`www/tones.html` new, `web_ui.c` route)
- `test/host/indicator_test/`, `test/host/relay_ctrl_test/`
- `USER_MANUAL.md`, `SPEC.md`, `CHANGELOG.md`

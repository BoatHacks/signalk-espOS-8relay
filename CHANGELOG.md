# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Each version starts with a one-line summary (at most 127 bytes): boards show
it as the update's notes, and *Cut release* refuses a version without one.

## [0.4.0] - 2026-10-01

Input alarms on NMEA 2000 and the buzzer, setting the clock by hand, espOS 0.12.1 and review fixes.

### Added

- Input alarms on NMEA 2000 (plan 21, follow-up to issue #3): an input
  alarm is also raised on the bus as an NMEA 2000 alert, so a chartplotter
  sounds a bilge float switch without a SignalK server. PGN 126983 every
  second and 126985 (the notification's message) every 10 s while active,
  at the input's severity (Warning/Alarm/Emergency Alarm); back to Normal
  when it clears. Acknowledging it on the chartplotter (126984) marks it
  acknowledged. New setting *Input alarms on NMEA 2000* (on by default,
  applies live). Host-tested; not yet checked on a real bus
  (HARDWARE_TESTS.md E6).
- Input alarms on the buzzer (plan 10 follow-up, issue #3): new per-input
  setting *Alarm buzzer* (`input<n>_alm_buzz`, off by default). While that
  input's alarm is active the buzzer sounds "IN" and the input's number in
  Morse ("IN 3"), independent of *Buzzer on alarm*, and takes over from a
  health alarm's pattern. Applies live. Host-tested; not yet checked on
  the board (HARDWARE_TESTS.md C7).
- Set the clock by hand (for a board with no NTP server or SignalK to
  learn the time from): the relay page has a new *Clock* section showing
  the board's local time, where it came from and its time zone, with a
  *Set to this device's time* button and a date/time field. It uses
  espOS's existing `PUT /api/v1/time`, and the firmware writes the result
  to the RTC chip like any other sync, so it survives a power cut. A
  manual time outranks SignalK and the RTC but never NTP (the buttons are
  greyed out while NTP keeps the clock). `GET /api/v1/relays/status` gains
  a `clock` object. Host-tested; not yet checked on a board
  (HARDWARE_TESTS.md G6).

### Changed

- More of the buzzer moved into `components/espos_tone/` (issue #17, step
  two): a one-shot tone player (`tone_player.h`: start, step, stop, pitch
  per note) and an LEDC buzzer driver (`tone_buzzer.h`: pitch and on/off
  on one channel). `indicator` now uses both, and event chirps and Tones
  page previews share one player instead of two copies of the same
  playback code. No change in behaviour.
- espOS 0.10.3 -> 0.12.1. The locally vendored `espos_wifi` (upstream PR
  #139, fast reconnect to the last-known AP) is gone: espOS 0.11.0 ships it
  from the registry, together with PR #146, which stops the setup portal
  from opening in the middle of a connection attempt (the multi-minute
  WiFi stall, issue #15 / espOS#136).

### Fixed

- One input bouncing at start-up (a loose wire, a pulsing signal) no
  longer keeps all eight inputs, their alarms and their relay links dark.
  Start-up waits at most the debounce time plus 1 s; an input still
  bouncing then reads off until it settles (plan 22, finding 3.1).
- An input alarm that clears while the SignalK connection is down, or
  while the board restarts, no longer stays raised on the server: every
  configured alarm is sent at start-up and on each reconnect, cleared ones
  as `normal` (plan 22, finding 3.2).
- Several NMEA 2000 alerts falling due at once no longer overflow the CAN
  transmit queue and lose whole alerts: they go out one input per 10 ms,
  and a frame the queue can't take is kept and retried by the NMEA 2000
  library instead of dropped (plan 22, finding 1.2).
- SignalK no longer keeps showing a relay's older state when two sources
  switch it at nearly the same moment: each change publishes the relay's
  current state (plan 22, finding 2.1).
- NMEA 2000 messages longer than one frame (alerts, product information,
  PGN lists) and the input bank's 127501 no longer go out corrupted. espOS
  0.12.1 gave the CAN driver frames on the stack, but the driver keeps a
  pointer to every frame it has to queue until it is sent. This repo
  carries a patched copy of espOS's `espos_n2k` under
  `components/signalk-espos__espos_n2k` until an espOS release fixes it
  (plan 22, finding 1.1; HARDWARE_TESTS.md E7).
- The BOOT button's portal and factory-reset chirps are no longer cut off:
  the restart used to follow 700 ms after the chirp started, shorter than
  the new ~2 s default melodies. It now waits for the chirp's own length
  (plus a small margin, capped at 10 s) on a timer, so the I/O loop's stall
  alarm can't silence it either.

## [0.3.0] - 2026-09-28

Time-based schedules with the board's own real-time clock, and better buzzer diagnostics.

### Added

- Time-based schedules (issue #9): up to 8 schedule entries, each
  switching one relay either by an on-time/off-time pair (a fixed
  `HH:MM`, or a sunrise/sunset offset like `sunset-30`/`sunrise+15m`) or a
  repeating duty cycle ("on for X minutes every Y minutes", anchored to
  local midnight — a fan, say), restricted to any combination of days of
  the week. Kept by a new PCF85063 real-time clock driver
  (`rtc_pcf85063`), which keeps time across a power cut, so schedules work
  before (or entirely without) a SignalK server: the clock is handed to
  espOS as soon as the API allows after boot, and written back whenever a
  better source (SNTP or SignalK) syncs. A new *Position source* setting
  picks where sunrise/sunset math gets the boat's position from — SignalK
  `navigation.position`, or the board's own NMEA 2000 bus (PGN 129025/
  129029) — falling back to a configured fixed position when the live
  source has nothing recent. Time zone uses espOS's existing POSIX-TZ
  *Timezone* setting; nothing new was needed there. A schedule's
  transitions switch relays through the existing `relay_ctrl_set()`
  entry point (a new `RELAY_SRC_SCHEDULE`, edge-triggered only, so a
  manual command in between two scheduled transitions stands until the
  next one — the same rule input overrides already follow), which means
  `wiredNC` translation and interlock enforcement apply automatically with
  no special-casing, and a momentary relay's scheduled "on" just starts
  its usual pulse. Two or more enabled schedule entries naming the same
  relay are rejected outright (all of them disabled until fixed) with a
  new health warning, the same shallow shape as a clashing bank id or an
  unreciprocated interlock — it doesn't ask whether the entries' days/
  times could ever actually collide, only whether they name the same
  relay at all. No valid time yet (no RTC, no SNTP, no SignalK) makes
  every schedule do nothing, with its own health warning. On a permanent
  polar day or night (no sunrise/sunset that day), a schedule referencing
  the missing boundary treats the whole day as continuously on the
  correct side of it, rather than doing nothing. Host-tested throughout,
  including the RTC driver against a fake I²C bus, sunrise/sunset math
  cross-checked against independently computed instants (several
  latitudes, both hemispheres, and the polar cases), and the evaluator's
  edge-triggering, midnight-spanning windows, day-of-week interactions,
  DST transitions and polar day/night handling
  (docs/plans/16-schedules.md).
- Every buzzer call is now logged on serial the instant it happens
  (`indicator_play_event`/`_relay_tone`/`_relay_pulse_tone`/
  `_input_tone`, `indicator_test_buzzer`, `indicator_play_rtttl`),
  separate from the existing `chirp:`/`preview:`/`buzzer test:`/`alarm:`
  lines that only fire once a request actually starts sounding — so a
  call silently dropped downstream (event chirps off, no tone assigned,
  the buzzer already busy) is now distinguishable on serial from nothing
  having called it at all.

## [0.2.0] - 2026-09-28

Input alarms, cycle counters, interlocked pairs and NC wiring, all confirmed on real hardware.

### Added

- Per-input alarms (issue #3): a new *Alarm* setting per input
  (`off`/`warn`/`alarm`/`emergency`, off by default) raises a SignalK
  notification on the input's own path (e.g.
  `notifications.electrical.switches.bank.1.3.state` for a bilge float
  switch on input 3) while it reads on, with a custom or default message,
  and clears it back to `normal` when it reads off. Raised only once the
  input has settled after boot, so an already-tripped switch still alarms
  at start-up; resent on the SignalK republish interval while active and
  after every reconnect. The relay page marks an input currently in alarm.
  Host-tested (docs/plans/10-input-alarms.md).
- Cycle counters and runtime hours for every relay and input (issue #4):
  `electrical.switches.bank.B.n.cycles` and `.runTime` (seconds), also
  under `electrical.controls.*` when that tree is on, published with the
  SignalK republish interval rather than on every change, and counting
  every source (SignalK, NMEA 2000, the relay page, an input override, a
  pulse ending, the SignalK-loss fail-safe, the maximum on-time) — not
  just direct commands. `GET /api/v1/relays` reports each channel's
  `cycles`/`runTime`, and `POST /api/v1/relays/<n>/counters/reset` /
  `POST /api/v1/inputs/<n>/counters/reset` zero one channel. The relay
  page shows both and has a *Reset* button per channel. Kept in RAM and
  flushed to flash at most every 10 minutes (plus once on a clean
  restart), like the existing `hold` relay-state pattern, so a power cut
  can lose up to that much counting; a reset itself is always flushed at
  once. New `counters` component, host-tested (counting from every
  source, save throttling, restore after a restart, per-channel reset)
  (docs/plans/11-counters-and-runtime.md).
- The BOOT button now does something while the firmware is running (issue
  #7): held ~5 s, it reopens the setup access point; held ~15 s, it
  factory-resets the board. The status LED blinks white or red to show
  which action a release will trigger. Host-tested and confirmed on
  real hardware (docs/plans/14-boot-button.md).
- Buzzer chirps on notable events, not just alarms (issue #14): boot, a
  BOOT-button action (a distinct chirp for reopening the setup portal vs.
  a factory reset, played just before the restart), a latching relay
  switching on/off by a direct command (SignalK, NMEA 2000, the relay
  page, or an input override — not for an automatic change), a momentary
  relay's pulse starting or ending (ending includes its own timer running
  out — the normal way a pulse finishes, unlike a latching relay's
  automatic changes), and an input's reported state changing to on or off
  (separate tones for each direction). New *Buzzer on events* setting
  (off by default, separate from *Buzzer on alarm*), and a new `/tones`
  page for a named RTTTL tone library (eight built-in tones, each with a
  *Play* button to preview it) and per-event/per-channel dropdowns to pick
  which tone plays where, or none. A chirp is silently skipped while the
  alarm is sounding or the BOOT button's LED override is active, never
  queued. Host-tested (RTTTL parsing, the tone library, gating/priority,
  which relay sources chirp, momentary-mode detection)
  (docs/plans/19-buzzer-event-sounds.md).
- New per-relay *Wired to NC* setting (issue #13, off by default): for a
  load wired to a relay's NC terminal instead of NO (e.g. a bilge pump or
  nav light meant to keep running through total power loss), reported and
  commanded on/off (SignalK, NMEA 2000, the relay page) now mean the
  load's state, not the coil's. **This never changes what an automatic
  switch-off does**: SignalK-loss fail-safe, boot/restart defaults, a
  momentary relay's pulse ending, and `hold` persistence all still
  energize/de-energize the coil directly, exactly like a real power
  loss — a `wiredNC` relay then correctly *reports* on right after one of
  those, since that's the state its load is powered in. A `wiredNC`
  momentary relay's pulse is therefore a brief load-*off* blip rather
  than load-on. Host-tested, including the coil-level fail-safe/boot/
  momentary/hold paths proven unaffected by the setting
  (docs/plans/18-relay-nc-no-wiring.md).
- *Buzzer frequency*'s range widened from 1000–5000 Hz to 42–10000 Hz.
- The debug console (issue #16) gained `relay <1-8> <on|off|toggle>`,
  `inputs` and `cfg <ns> <key> <value>`, for exercising relays/inputs and
  setting arbitrary config over USB serial without network reachability.
- New per-relay *Interlocked with* setting (issue #8): pairs of relays
  that must never be on together, e.g. windlass up/down or a reversing
  motor's two contactors. Switching one relay on while its interlocked
  partner is on now switches the partner off immediately and defers the
  first relay's own on for a new global *Interlock dead time* (default
  100 ms, 0–2000 ms); the command still succeeds, listeners just see the
  partner go off, then the relay go on once the dead time passes. A pair
  only takes effect when both relays name each other -- a one-sided or
  self-referencing setting is ignored and raises a new health warning, the
  same pattern as a clashing bank id. **Like `wiredNC` (issue #13), this
  rule acts on the relay's coil, never the reported/commanded state**: if
  boot/hold-restore or a settings change finds both coils of a pair
  already on, neither is restored/kept -- both switch off and warn, rather
  than guess which side to trust. *All on* (the relay page and
  `PUT /api/v1/relays`) now skips every relay in an interlocked pair, and
  the response/page say which ones and why. Host-tested (dead time,
  pending-on cancelled by an off or by the partner's on, never both coils
  in the same expander write, one-sided/self settings, boot/hold and
  config-change both-on, *All on*)
  (docs/plans/15-interlocked-pairs.md).

### Changed

- RTTTL parsing, the named tone library and the chirp/preview priority
  gate moved out of `components/indicator/` into a new
  `components/espos_tone/` (issue #17), first step towards making #14's
  event-chirp system reusable by other espOS boards. Purely internal:
  `indicator.h`'s public API and all observable behavior are unchanged.
  Not yet vendored into espOS itself — that's tracked separately as
  [espOS#143](https://github.com/signalk-espOS/espOS/issues/143).

### Fixed

- The `indicator` task's stack (3072 bytes) was too tight once event
  chirps (issue #14) added two `indicator_tone_t` locals on top of its
  existing Morse buffer: playing a relay chirp could overflow it and
  crash the board. Found on real hardware; raised to 4096 bytes.
- WiFi station reconnects paid a ~2.3 s full-channel rescan on every
  single attempt, including a reconnect to the AP the board just lost
  ([espOS#136](https://github.com/signalk-espOS/espOS/issues/136)).
  Vendored espOS's upstream fix
  ([PR #139](https://github.com/signalk-espOS/espOS/pull/139)) locally
  under `components/signalk-espos__espos_wifi`, since the ESP Component
  Registry hasn't published a release containing it yet: the first 3
  attempts after boot/link-loss now try the last-known AP/channel
  directly before falling back to a full scan. Does not fix the
  underlying AUTH_EXPIRE timing miss itself, only the scan overhead on
  each retry.

## [0.1.0] - 2026-09-26

First release tested end to end on real hardware: relays, inputs, SignalK, NMEA 2000 (bench-tested, not on a boat yet).

### Changed

- Every relay's and input's settings have their descriptions again (taken
  out in 0.0.8 to save RAM; with PSRAM since 0.0.9 the schema fits).

### Tested

- Hardware bring-up on the first physical unit: relay and input polarity,
  debounce, momentary pulse, max on-time, restart/hold, cold power-up, OTA
  reboot, toggle/follow input overrides, SignalK connection/republish/PUT
  switching/fail-safe, NMEA 2000 on-bus operation/switching/address-claim
  collision handling, relay page, and the buzzer. Full results in
  [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md). Not yet tested: LED
  colour, the alarm-triggered buzzer pattern, Ethernet with a cable
  plugged in, MFD device listing, and captive-portal provisioning.

## [0.0.10] - 2026-09-25

Fixes NMEA 2000 not starting and a crash when SignalK connects during a page load; releases include the ELF.

### Fixed

- NMEA 2000 could stay off after boot (`n2k: could not open the CAN
  bus`): the NMEA 2000 library only opens the bus once a millisecond has
  passed since it was set up, and a first open attempt within the same
  millisecond returned "not yet", which the firmware took as final. The
  bus is now opened by the NMEA 2000 task, which retries until it
  succeeds; the log says `on the bus` once it is open, or reports an
  error after 5 s if it still isn't.
- A crash (stack overflow in espOS's SignalK task) when SignalK
  connected while a browser was loading the board's page: the board ran
  out of network sockets (IDF's default of 10). Raised to 16.

### Added

- Releases carry the firmware's ELF file, so a crash backtrace or core
  dump can be decoded.

## [0.0.9] - 2026-09-25

Turns on the board's PSRAM so the settings page loads again, and fixes the 404 on /api/v1/n2k.

### Fixed

- The settings page still failed with `ESP_ERR_NO_MEM` in 0.0.8. The
  board's 8 MB PSRAM (octal, in the ESP32-S3R8) is now enabled, and all
  JSON work — espOS's settings schema included — uses it, keeping
  internal RAM for WiFi and the rest. If PSRAM ever fails to start, the
  firmware runs without it. The boot log's chip report shows how much
  PSRAM is in use.
- `GET /api/v1/n2k` answered 404 in 0.0.8: the same espOS registry-name
  problem as in 0.0.3 made espOS build a stub instead of the endpoint. The
  build now gives espos_n2k the web server it looks for.

## [0.0.8] - 2026-09-25

Smaller settings schema, a chip and PSRAM report in the boot log, and CAN bus diagnostics.

### Fixed

- The settings page could fail with `ESP_ERR_NO_MEM`: espOS builds the
  settings schema in RAM on every page load, and the 0.0.6 settings made
  it too big. Relays and inputs 2–8 no longer repeat relay 1's and input
  1's descriptions (the schema shrinks from 52.6 to 47.2 KB).

### Added

- Boot log reports the chip: model and revision, the PSRAM inside the
  chip package (size, vendor, and whether it is quad or octal), and the
  flash size, as esptool would. Needed before PSRAM can be enabled.
- `GET /api/v1/n2k`: espOS's CAN diagnostics (frames received and
  dropped, bus errors), to tell a wiring fault from a silent bus.
  Troubleshooting section in the manual.

## [0.0.7] - 2026-09-25

The board checks for updates out of the box; every release now publishes an update manifest.

### Added

- Update checks out of the box: on its first start the board sets
  espOS's *Manifest URL* to this project's update list, which every
  release now updates (`manifest.json` on the `ota` branch). Full
  releases are offered on the *stable* channel (the default), full
  releases and pre-releases on *beta*. Emptying the URL turns checks off
  for good.
- *Cut release* has a *Publish as a pre-release* checkbox, so the first
  full release (0.1.0) can be made from the workflow.

## [0.0.6] - 2026-09-25

Toggle inputs, maximum on-time, and a relay page with status, sources and a buzzer test.

### Added

- *Input link* setting per relay (#1): *follow* (as before) or *toggle*,
  where each press of a push button on the input switches the relay over
  and the release does nothing. Nothing happens at start-up, even with the
  button held.
- *Maximum on-time* setting per relay (#2): a latching relay switches
  itself off after that long, whatever switched it on; another "on"
  restarts the time. 0 (the default) means no limit.
- The relay page shows what last switched each relay and how long ago
  (#5): SignalK, NMEA 2000, the page, an input, pulse end, fail-safe,
  maximum on-time or start-up. `GET /api/v1/relays` reports it as
  `lastSource` and `lastChangeAgoS`, and the log prints one line per
  relay change with its source.
- Relay page extras (#6): the board's name and firmware version in the
  header; a status line for the network, the SignalK connection and the
  NMEA 2000 address, warning when the bus has been silent for 10 s
  (`GET /api/v1/relays/status`); *Pulse* and *Stop* buttons for momentary
  relays.
- Buzzer test and frequency (#10): a *Test buzzer* button on the relay
  page (`POST /api/v1/buzzer/test`) plays the alarm pattern once, even
  with *Buzzer on alarm* off, and not while an alarm is sounding. New
  *Buzzer frequency* setting (default 2700 Hz, 1000–5000 Hz), applied
  live.

## [0.0.5] - 2026-09-25

Relay and input states are resent to SignalK on an interval, so apps don't show them as stale.

### Added

- *SignalK republish interval* setting (default 10 s, 0 = off): every
  relay and input state is resent to SignalK on that interval while the
  stream is up, not only when it changes, so SignalK apps no longer show
  quiet switches as stale. The interval is also declared as the state
  paths' metadata period (a `timeout` of 2.5× the interval), where the
  server doesn't already have metadata of its own. Changes apply live.

## [0.0.4] - 2026-09-25

A relay page on the board: switch relays and watch the inputs from any browser.

### Added

- Relay page on the device, at `/relays`: On and Off buttons for each
  relay, *All on* (with a confirmation) and *All off*, and the state of
  each digital input next to its relay, updated every second. Behind it,
  `GET /api/v1/relays` and `PUT /api/v1/relays[/<n>]` with
  `{"on": true|false}`, protected by espOS's API key like its own
  endpoints. Switching goes through the same path as SignalK and NMEA
  2000, so momentary pulses and input overrides apply. The page is built
  into the firmware, so OTA updates keep it current.

## [0.0.3] - 2026-09-25

Fixes WiFi, SignalK and OTA updates never starting, and network time.

### Fixed

- No WiFi, no setup access point, no SignalK connection and no OTA updates:
  espOS 0.10.3, installed from the component registry, looks for its
  optional parts under their bare names and misses the registry's
  `signalk-espos__` prefix, so `espos_start()` started none of them. The
  build now links and enables WiFi, the SignalK client and OTA itself, and
  lets OTA see whether a WiFi network is configured.
- Network time (SNTP) failed to start: the DHCP-supplied NTP server option
  is now enabled.

## [0.0.2] - 2026-09-25

Fixes the boot loop and adds signed OTA updates with automatic rollback.

### Fixed

- Boot loop on every start: the I/O task, which starts before the network,
  took the SignalK bridge's lock before the bridge had created it, and the
  firmware aborted in `xQueueSemaphoreTake`. The lock is now created before
  the I/O task and the relay/input listeners start, and every SignalK
  bridge call made before that is ignored. A host test now covers the boot
  order.

### Added

- Over-the-air updates through espOS's `espos_ota`: signed images only,
  with automatic rollback if a new image doesn't reach the network within
  the rollback timeout (10 minutes by default). An update replaces the
  firmware and keeps all settings; it doesn't update the web page. Built
  in but never started until 0.0.3, so update 0.0.1 and 0.0.2 over USB.

## [0.0.1] - 2026-09-25

First build for hardware bring-up. Not usable: it boot-loops (fixed in 0.0.2).

### Added

- Firmware for the Waveshare ESP32-S3-ETH-8DI-8RO-C on espOS 0.10.3: 8
  relays and 8 digital inputs as switch banks over SignalK and NMEA 2000.
- Relays: latching or momentary, per-relay fail-safe (keep last state or
  switch off) on SignalK loss and restart, safe boot sequence, input-to-relay
  overrides.
- Inputs: debounce (50 ms default) and invert.
- SignalK: `electrical.switches.bank.*` (on by default) and the RFC 0009
  style `electrical.controls.*` tree (off by default), with PUT control,
  names and manufacturer metadata.
- NMEA 2000: load-controller device with PGN 127501 status and 127502
  control for both banks.
- Ethernet (W5500, own driver) preferred, WiFi as fallback.
- Status LED and optional Morse alarm buzzer ("ESP" + last IP octet, or
  "ESP AP" without an address).
- All settings in espOS's web UI.
- Signed release builds with a merged image for USB and an image for OTA.

[0.4.0]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.0.10...v0.1.0
[0.0.10]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.0.9...v0.0.10
[0.0.9]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.0.8...v0.0.9
[0.0.8]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.0.7...v0.0.8
[0.0.7]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.0.6...v0.0.7
[0.0.6]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.0.5...v0.0.6
[0.0.5]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.0.4...v0.0.5
[0.0.4]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.0.3...v0.0.4
[0.0.3]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.0.2...v0.0.3
[0.0.2]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.0.1...v0.0.2
[0.0.1]: https://github.com/BoatHacks/signalk-espOS-8relay/releases/tag/v0.0.1

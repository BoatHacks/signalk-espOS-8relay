# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Each version starts with a one-line summary (at most 127 bytes): boards show
it as the update's notes, and *Cut release* refuses a version without one.

## [Unreleased]

### Changed

- Every relay's and input's settings have their descriptions again (taken
  out in 0.0.8 to save RAM; with PSRAM since 0.0.9 the schema fits).

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

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.5] - 2026-09-25

### Added

- *SignalK republish interval* setting (default 10 s, 0 = off): every
  relay and input state is resent to SignalK on that interval while the
  stream is up, not only when it changes, so SignalK apps no longer show
  quiet switches as stale. The interval is also declared as the state
  paths' metadata period (a `timeout` of 2.5× the interval), where the
  server doesn't already have metadata of its own. Changes apply live.

## [0.0.4] - 2026-09-25

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

First build for hardware bring-up. Not usable: it boot-loops (fixed in
0.0.2).

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

[0.0.5]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.0.4...v0.0.5
[0.0.4]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.0.3...v0.0.4
[0.0.3]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.0.2...v0.0.3
[0.0.2]: https://github.com/BoatHacks/signalk-espOS-8relay/compare/v0.0.1...v0.0.2
[0.0.1]: https://github.com/BoatHacks/signalk-espOS-8relay/releases/tag/v0.0.1

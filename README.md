# signalk-espOS-8relay

Firmware for the Waveshare
[ESP32-S3-ETH-8DI-8RO-C](https://www.waveshare.com/wiki/ESP32-S3-ETH-8DI-8RO-C)
industrial relay board that makes it a boat switch bank: 8 relays you can
switch, and 8 isolated inputs you can read, over **SignalK** (WiFi or
Ethernet) and **NMEA 2000** (the board's CAN port) at the same time.

It runs on [espOS](https://github.com/signalk-espOS/espOS), which provides the
networking, SignalK connection, settings web page and signed over-the-air
updates. This project adds everything specific to the board.

> **Status: hardware bring-up.** Test releases exist, but nothing has been
> checked on a boat yet. See [Current status](#current-status).

## Scope

**In scope**
- Switch each of the 8 relays on/off from SignalK (PUT) and from NMEA 2000
  (PGN 127502), with state reported on both (SignalK deltas, PGN 127501).
- Read the 8 digital inputs (e.g. bilge float switches) and report them on
  both, as a separate switch bank.
- Per relay: a name, latching or momentary (auto-off after a set time), and
  what happens when SignalK is lost or the board restarts (keep the last
  state, or switch off).
- Optional local override: an input can drive one or more relays directly,
  with no network involved.
- SignalK paths under the standard `electrical.switches.bank.*` tree, plus
  an optional `electrical.controls.*` tree following the proposed
  [SignalK RFC 0009](https://github.com/SignalK/specification/issues/441).
- Ethernet preferred, WiFi as fallback; setup through espOS's access point
  and web page.
- Time-based schedules (issue [#9](https://github.com/BoatHacks/signalk-espOS-8relay/issues/9)):
  up to 8 entries, each switching a relay by a fixed time of day, a
  sunrise/sunset offset, or a repeating duty cycle (e.g. a fan 10 minutes
  every hour), kept by the board's own real-time clock so schedules work
  without a SignalK server.

**Out of scope for the first release:** dimmers, scenes/groups,
multi-condition interlocks, and the board's RS485/expansion header.

The full requirements are in [SPEC.md](SPEC.md).

## Current status

| Stage | State |
|---|---|
| Specification ([SPEC.md](SPEC.md)) | Done |
| Architecture ([ARCHITECTURE.md](ARCHITECTURE.md)) | Done, being corrected against espOS as findings come in |
| 00 — espOS fit check | Done: espOS 0.10.3 builds for the ESP32-S3 and boots on the real board |
| 01 — project scaffold | Done: builds for the ESP32-S3, W5500 Ethernet driver initializes on the real board (link not yet tested — no cable), host tests, CI |
| 02 — settings | Done: all settings confirmed working through the API/web UI on the real board |
| 03 — relay control | Done: boot sequence, momentary pulses, max on-time, fail-safe, hold-state, cold power-up and OTA reboot all confirmed on the real board. Interlocked relay pairs and per-relay cycle counters/runtime both confirmed on the real board (issues [#8](https://github.com/BoatHacks/signalk-espOS-8relay/issues/8) and [#4](https://github.com/BoatHacks/signalk-espOS-8relay/issues/4), both closed) |
| 04 — digital inputs | Done: polarity, 50 ms debounce, and toggle/follow input-to-relay overrides confirmed on the real board |
| 05 — SignalK | Done: connection, republish interval, PUT switching, and SignalK-loss fail-safe confirmed on the real board; `controls.*` tree and renaming not yet exercised. Per-input alarm notifications confirmed on the real board (issue [#3](https://github.com/BoatHacks/signalk-espOS-8relay/issues/3), closed) |
| 06 — NMEA 2000 | Done: on-bus operation, 127501 status, 127502 control, and address-claim collision handling confirmed against a real second device; MFD device listing not yet tested (no MFD on the bench) |
| Status LED and alarm buzzer | Buzzer tone and live frequency change confirmed on the real board; LED colour not yet tested. The alarm-triggered buzzer pattern is deliberately not tested — the only way to trigger it is opening the case, which is declined; blocked until [espOS#137](https://github.com/signalk-espOS/espOS/issues/137)'s proposed remote test-injection endpoint lands upstream. Buzzer chirps on notable events (boot, BOOT-button actions, relay/input changes), the `/tones` page (Play button, CRUD, momentary/latching layout), and non-interference with the alarm and BOOT-button override are all confirmed (issue [#14](https://github.com/BoatHacks/signalk-espOS-8relay/issues/14), closed) |
| BOOT-button setup/reset and debug console | Confirmed on real hardware: BOOT button reopens the setup portal (~5 s) or factory-resets (~15 s) (issue [#7](https://github.com/BoatHacks/signalk-espOS-8relay/issues/7), closed). A USB serial debug console (`relay`, `inputs`, `cfg`, `wifi_sta` commands) is also on `main`, useful for bench-testing without network reachability (issue [#16](https://github.com/BoatHacks/signalk-espOS-8relay/issues/16), closed) |
| 07 — hardware bring-up and first release | In progress: most of the bring-up checklist passed on the first physical unit (2026-09-26); see [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md) for the full results. Ethernet-plugged-in, captive portal, MFD-listing, and a signed release are still open. |
| Schedules (issue [#9](https://github.com/BoatHacks/signalk-espOS-8relay/issues/9), [plan 16](docs/plans/16-schedules.md)) | Implemented and host-tested: PCF85063 RTC driver and clock sync, timezone/position settings (SignalK or NMEA 2000 PGN 129025/129029), NOAA sunrise/sunset calculation, 8 schedule entries (fixed time, sunrise/sunset offset, or repeating duty cycle), overlapping-schedule and no-valid-time health warnings. Not yet confirmed on real hardware: RTC keeping time across a power cut, and a live sunrise/sunset transition. |

Known issues found so far:
- espOS does not support this board's W5500 Ethernet chip, so this project
  has its own driver (initializes cleanly on the real board; not yet tested
  with a cable plugged in).
- espOS's NMEA 2000 debug server (candump) can't run alongside our NMEA 2000
  code, so it's left out.
- espOS's NMEA 2000 component only passes raw CAN frames, so the NMEA 2000
  protocol layer comes from a separate library.
- espOS's WiFi station regularly retries a few times with `AUTH_EXPIRE`
  before connecting on boot or reconnect; diagnosed and filed upstream as
  [espOS#136](https://github.com/signalk-espOS/espOS/issues/136). Usually
  resolves within about a minute, but a board that got here by losing
  network access and re-enabling the station while the setup portal was
  still open was once observed stuck for several minutes before recovering
  (investigated as [#15](https://github.com/BoatHacks/signalk-espOS-8relay/issues/15),
  closed as tracking the same upstream root cause). espOS 0.11.0 carries
  two upstream fixes for it: [PR #139](https://github.com/signalk-espOS/espOS/pull/139)
  reconnects to the last-known AP/channel directly instead of spending
  ~2.3 s on a full-channel rescan before each retry, and
  [PR #146](https://github.com/signalk-espOS/espOS/pull/146) no longer
  raises the setup portal in the middle of a connection attempt (the
  multi-minute stall). Neither fixes the underlying `AUTH_EXPIRE` timing
  miss itself, which is why espOS#136 stays open. This repo now takes both
  from the registry (espOS 0.12.1) instead of vendoring `espos_wifi`.
- The relay outputs' three-terminal (NO/COM/NC) wiring has a per-relay
  `wiredNC` setting so reporting/commanding reflect the load, not the
  coil, while fail-safe/boot/momentary-auto-off/hold-restore still always
  operate on the coil directly, including through a factory reset (issue
  [#13](https://github.com/BoatHacks/signalk-espOS-8relay/issues/13),
  closed, confirmed on real hardware).
- If the `signalk-n2k-switching` plugin is installed on the SignalK server,
  it registers itself as a generic PUT handler for every
  `electrical.switches.bank.*` path and can win over the board's own
  SignalK PUT handling, sending an NMEA 2000 PGN 127502 command instead of
  talking to the board directly. If the server has no working N2K
  connection at the time (or the board isn't on the bus), that command
  goes nowhere and the PUT fails after ~20 s with "Did not receive change
  confirmation". Disable that plugin (or fix its N2K connection) if relay
  switching from SignalK hangs or times out.
- Most of issue #14's buzzer-chirp system (RTTTL parsing, the named tone
  library, priority/gating) isn't specific to relays or digital inputs and
  would work on any espOS device with a buzzer; it currently lives entirely
  in `components/indicator/` here. Extracting the reusable parts into an
  espOS core component is tracked in
  [#17](https://github.com/BoatHacks/signalk-espOS-8relay/issues/17), not
  urgent while this is the only board using it.

## Plans

Work is split into stages, each with its own plan in
[docs/plans/](docs/plans/README.md):

| # | Stage |
|---|---|
| 00 | [espOS fit check](docs/plans/00-espos-fit-check.md) |
| 01 | [Project scaffold](docs/plans/01-project-scaffold.md) |
| 02 | [Device config](docs/plans/02-device-config.md) |
| 03 | [Relay control](docs/plans/03-relay-control.md) |
| 04 | [Digital inputs](docs/plans/04-digital-inputs.md) |
| 05 | [SignalK bridge](docs/plans/05-signalk-bridge.md) |
| 06 | [NMEA 2000 switch bank](docs/plans/06-n2k-switch-bank.md) |
| 07 | [Bring-up and first release](docs/plans/07-bringup-and-release.md) |

## Documentation

- [USER_MANUAL.md](USER_MANUAL.md) — installing, setting up and using the
  firmware
- [CHANGELOG.md](CHANGELOG.md) — what changed in each release
- [SPEC.md](SPEC.md) — what the firmware does and why
- [ARCHITECTURE.md](ARCHITECTURE.md) — how the code is organised
- [RFC-441-DIGITAL-SWITCHING.md](RFC-441-DIGITAL-SWITCHING.md) — how this
  project relates to SignalK RFC 0009
- [IMPLEMENTATION_CHECKLIST.md](IMPLEMENTATION_CHECKLIST.md) — how each stage
  is worked through
- [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md) — how to test a release
  on the real board
- [CONTRIBUTING.md](CONTRIBUTING.md) — how to build, test and propose a
  change
- [LICENSE](LICENSE) — Apache-2.0

## Safety

The relays switch real loads (up to 10 A at 250 V AC / 30 V DC). Until the
first release has been tested on hardware, don't connect this board to
anything on a boat that matters.

## Board hardware

What's on the Waveshare ESP32-S3-ETH-8DI-8RO-C and what this firmware does
with it. Pins are from the Waveshare wiki and the community ESPHome config
for this board. Confirmed correct on real hardware: the I²C relay
expander, the 8 digital inputs (GPIO4–11), the CAN transceiver, the
status LED and the buzzer. The W5500 Ethernet SPI pins initialize
without error but haven't been tested with a cable plugged in.

| Component | Pins / interface | Used | By |
|---|---|---|---|
| ESP32-S3 dual-core CPU | — | Yes | Everything |
| 16 MB flash | internal | Yes | Firmware (two OTA slots), settings, web UI |
| 8 MB PSRAM (octal, in the chip) | internal | Yes | Large allocations, e.g. espOS's settings schema (0.0.9 and later) |
| WiFi 2.4 GHz | internal | Yes | espOS: setup access point, network, SignalK |
| Bluetooth LE | internal | **No** | espOS could provision over BLE instead of the access point |
| 8 relays via TCA9554 I²C expander (0x20) | SCL 41, SDA 42 | Yes | `relay_ctrl` |
| 8 isolated digital inputs | GPIO 4–11 | Yes | `input_sense` |
| Isolated CAN transceiver | TX 17, RX 18 | Yes | NMEA 2000 (`switch_bank`) |
| W5500 Ethernet, RJ45 | SPI MOSI 13, MISO 14, SCLK 15, CS 16, INT 12 | Yes | `eth_w5500` |
| WS2812 RGB LED | GPIO 38 | Yes | `indicator`: status colour |
| Passive piezo buzzer | GPIO 46 | Yes | `indicator`: Morse alarm (off by default) |
| PCF85063 real-time clock | same I²C bus as the relays | Yes | `rtc_pcf85063`: keeps time across power loss, feeding `schedule`'s sunrise/sunset and time-of-day switching (issue #9) |
| BOOT button | GPIO 0 | Yes | USB bootloader, and (confirmed on real hardware) a held press reopens the setup access point (~5 s) or factory-resets (~15 s), release to trigger |
| microSD (TF) card slot | SPI MISO 45, MOSI 47, SCLK 48 | **No** | Could hold an event log; chip-select pin not yet known |
| GPIO expansion header | various | **No** | Out of scope (SPEC.md §10.2) |
| USB-C | USB-Serial-JTAG | Yes | Power, flashing, serial log |
| Power input 7–36 V DC | — | Yes | Supply only; the voltage isn't measured |

The isolation, optocoupler and TVS protection circuits are passive.

GPIO 45 (SD card) and GPIO 46 (buzzer) are ESP32-S3 strapping pins: they
set boot options at reset, so anything using them must leave them at their
safe levels during reset.

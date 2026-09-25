# signalk-espOS-8relay

Firmware for the Waveshare
[ESP32-S3-ETH-8DI-8RO-C](https://www.waveshare.com/wiki/ESP32-S3-ETH-8DI-8RO-C)
industrial relay board that makes it a boat switch bank: 8 relays you can
switch, and 8 isolated inputs you can read, over **SignalK** (WiFi or
Ethernet) and **NMEA 2000** (the board's CAN port) at the same time.

It runs on [espOS](https://github.com/signalk-espOS/espOS), which provides the
networking, SignalK connection, settings web page and signed over-the-air
updates. This project adds everything specific to the board.

> **Status: design stage. There is no working firmware yet.** See
> [Current status](#current-status).

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

**Out of scope for the first release:** dimmers, scenes/groups, schedules,
multi-condition interlocks, and the board's RS485/expansion header.

The full requirements are in [SPEC.md](SPEC.md).

## Current status

| Stage | State |
|---|---|
| Specification ([SPEC.md](SPEC.md)) | Done |
| Architecture ([ARCHITECTURE.md](ARCHITECTURE.md)) | Done, being corrected against espOS as findings come in |
| 00 — espOS fit check | Software checks done; espOS 0.10.3 builds for the ESP32-S3. Boot test waiting for the board |
| 01 — project scaffold | Done except the on-board check: builds for the ESP32-S3, W5500 Ethernet driver, host tests, CI |
| 02 — settings | Done except the on-board check: all settings in espOS's web UI, host-tested |
| 03 — relay control | Done except the on-board check: safe boot sequence, momentary pulses, fail-safe, hold-state storage; host-tested |
| 04 — digital inputs | Done except the on-board check: debounce, invert, input-to-relay overrides; host-tested |
| 05 — SignalK | Done except the on-board check: both path trees, relay PUTs, names, SignalK-loss fail-safe; host-tested |
| 06 — NMEA 2000 | Done except the bus test: joins the bus as a load controller, 127501 status, 127502 control; payloads host-tested |
| Status LED and alarm buzzer | Done except the on-board check; logic host-tested |
| 07 — hardware bring-up and first release | Not started; waiting for the board |

Known issues found so far:
- espOS does not support this board's W5500 Ethernet chip, so this project
  has its own driver (untested until the board arrives).
- espOS's NMEA 2000 debug server (candump) can't run alongside our NMEA 2000
  code, so it's left out.
- espOS's NMEA 2000 component only passes raw CAN frames, so the NMEA 2000
  protocol layer comes from a separate library.

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
- [SPEC.md](SPEC.md) — what the firmware does and why
- [ARCHITECTURE.md](ARCHITECTURE.md) — how the code is organised
- [RFC-441-DIGITAL-SWITCHING.md](RFC-441-DIGITAL-SWITCHING.md) — how this
  project relates to SignalK RFC 0009
- [IMPLEMENTATION_CHECKLIST.md](IMPLEMENTATION_CHECKLIST.md) — how each stage
  is worked through

## Safety

The relays switch real loads (up to 10 A at 250 V AC / 30 V DC). Until the
first release has been tested on hardware, don't connect this board to
anything on a boat that matters.

## Board hardware

What's on the Waveshare ESP32-S3-ETH-8DI-8RO-C and what this firmware does
with it. Pins are from the Waveshare wiki and the community ESPHome config
for this board, and are not yet confirmed on real hardware.

| Component | Pins / interface | Used | By |
|---|---|---|---|
| ESP32-S3 dual-core CPU | — | Yes | Everything |
| 16 MB flash | internal | Yes | Firmware (two OTA slots), settings, web UI |
| 8 MB PSRAM | internal | **No** | Off until its type (quad or octal) is confirmed; the wrong mode stops boot |
| WiFi 2.4 GHz | internal | Yes | espOS: setup access point, network, SignalK |
| Bluetooth LE | internal | **No** | espOS could provision over BLE instead of the access point |
| 8 relays via TCA9554 I²C expander (0x20) | SCL 41, SDA 42 | Yes | `relay_ctrl` |
| 8 isolated digital inputs | GPIO 4–11 | Yes | `input_sense` |
| Isolated CAN transceiver | TX 17, RX 18 | Yes | NMEA 2000 (`switch_bank`) |
| W5500 Ethernet, RJ45 | SPI MOSI 13, MISO 14, SCLK 15, CS 16, INT 12 | Yes | `eth_w5500` |
| WS2812 RGB LED | GPIO 38 | Yes | `indicator`: status colour |
| Passive piezo buzzer | GPIO 46 | Yes | `indicator`: Morse alarm (off by default) |
| PCF85063 real-time clock | same I²C bus as the relays | **No** | Would keep time across power loss, e.g. for scheduled switching |
| BOOT button | GPIO 0 | Partly | USB bootloader only; the firmware doesn't read it (could do a factory reset or reopen the setup access point) |
| microSD (TF) card slot | SPI MISO 45, MOSI 47, SCLK 48 | **No** | Could hold an event log; chip-select pin not yet known |
| GPIO expansion header | various | **No** | Out of scope (SPEC.md §10.2) |
| USB-C | USB-Serial-JTAG | Yes | Power, flashing, serial log |
| Power input 7–36 V DC | — | Yes | Supply only; the voltage isn't measured |

The isolation, optocoupler and TVS protection circuits are passive.

GPIO 45 (SD card) and GPIO 46 (buzzer) are ESP32-S3 strapping pins: they
set boot options at reset, so anything using them must leave them at their
safe levels during reset.

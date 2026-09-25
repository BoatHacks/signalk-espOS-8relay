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

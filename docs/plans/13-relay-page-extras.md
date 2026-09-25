# Implementation Plan: 13 — Relay page: Pulse button, status, version

Issue: [#6](https://github.com/BoatHacks/signalk-espOS-8relay/issues/6)

## Overview
Three additions to the relay page: a *Pulse* button for momentary
relays, a status line for SignalK / NMEA 2000 / network, and the
firmware version and device name in the header.

## Relevant SPEC/ARCHITECTURE Sections
- CHANGELOG 0.0.4 (relay page)
- Plans 05, 06 (bridges)

## Approach
- **Pulse:** for relays with `"momentary": true`, show one *Pulse*
  button instead of On/Off. It sends the existing
  `PUT /api/v1/relays/<n>` `{"on": true}`; `relay_ctrl` ends the pulse.
  Keep a small *Off* link for ending a long pulse early.
- **Status:** new `GET /api/v1/relays/status` (protected), polled every
  5 s, not with the 1 s state poll:
  - SignalK: from `espos_sk_ws_get_status()` (connected, server host).
  - NMEA 2000: add `n2k_bridge_get_status()` returning "on the bus" and
    the claimed source address (the NMEA2000 library knows it).
  - Network: `espos_net_get_status()` (interface Ethernet/WiFi, IP).
- **Version and name:** `esp_app_get_description()->version` and the
  espOS hostname, in the same status response; shown in the header and
  the page title.
- Everything stays in the one embedded page, no external resources.

## Test Strategy
- `web_ui_test`: status JSON building (pure function from a status
  struct), including "not connected" and missing server host.
- Browser check against the mock: Pulse button only on momentary relays,
  status line states, phone width, dark mode, no JS errors.
On the board: status matches reality when unplugging Ethernet, stopping
the SignalK server, disconnecting the CAN bus.

## Implementation Steps
- [x] `n2k_bridge_get_status()`
- [x] Status struct + JSON builder in `web_ui_logic`, handler in
      `web_ui.c`
- [x] Page: Pulse button, status line, header
- [x] Tests; USER_MANUAL §7.2; CHANGELOG

## Files to Create/Modify
- `components/web_ui/`
- `components/switch_bank/src/n2k_bridge.cpp`, `include/n2k_bridge.h`
- `test/host/web_ui_test/`
- `USER_MANUAL.md`, `CHANGELOG.md`

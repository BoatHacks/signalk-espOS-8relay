# signalk-espOS-8relay Architecture

## 1. Overview

signalk-espOS-8relay is an ESP-IDF 6 application built on top of the
[espOS](https://github.com/signalk-espOS/espOS) runtime. espOS owns
networking, SignalK transport, config storage, web UI, and OTA; this
project owns board I/O (relays/DI) and the switch-bank domain logic that
bridges relay and input state to SignalK and NMEA2000.

Facts about espOS and the board below were checked against the espOS
repository and the Waveshare wiki; items still to be confirmed are listed
in [docs/plans/00-espos-fit-check.md](docs/plans/00-espos-fit-check.md).

```
                        +-----------------------------+
                        |   signalk-espOS-8relay app   |
                        |  (this repo, main/ + comps)  |
                        |                               |
   TCA9554 (I2C) RO1-8  |  relay_ctrl / input_sense      |
   GPIO4-11 DI1-8       |  switch_bank (SK + N2K bridge)|
   <------------------->|         |         |            |
                        |         |    NMEA2000 library   |
                        |         |    (PGNs, addr claim) |
                        +---------|---------|------------+
                                  |         |
                    espos_sk------+         +------espos_n2k
                    (SK deltas/PUT/meta)    (raw CAN frames over
                                              TWAI, candump server)
                                  |
                    espos_config, espos_net/wifi/eth,
                    espos_httpd (web UI + config REST),
                    espos_ota
                        (all provided by espOS, unmodified)
```

SignalK connectivity and NMEA2000 connectivity are independent paths into
the same in-memory switch-bank state (§2.3); neither depends on the other
being present.

## 2. System Components

### 2.1 `relay_ctrl` (this repo)

Owns the 8 relays. They are not on ESP32 GPIOs: they are pins EXIO1–8 of
a TCA9554 I2C expander (address 0x20, I2C SCL GPIO41 / SDA GPIO42, shared
with the board's PCF85063 RTC), driven with ESP-IDF's `i2c_master`
driver. Applies commands (on/off), enforces momentary pulse timing (an
`esp_timer` per momentary relay), and applies fail-safe policy on boot and
on SignalK loss. The expander keeps its outputs through an ESP32 reset,
which lets `hold` relays survive an OTA reboot without switching; see
[plan 03](docs/plans/03-relay-control.md) for the boot sequence this
requires; relays start in espOS's `before_network` hook, before any
networking. Momentary pulses and flash saves are driven by
`relay_ctrl_tick()`, called every 10 ms by a relay task. Nothing else in
this repo talks to the expander — all relay
state changes go through this component so `switch_bank` (§2.3) always
has one place to ask for/command state.

### 2.2 `input_sense` (this repo)

Owns the 8 digital inputs (GPIO4–GPIO11, opto-isolated), read with
ESP-IDF's GPIO driver and polled every 10 ms by the same I/O task that
ticks `relay_ctrl`. Debounces raw input state and
exposes a simple state-change callback. Applies the configured DI→relay
override mapping on a DI edge, through a callback that `main` points at
`relay_ctrl_set()` (this
is the only cross-component write path outside of `switch_bank`, since the
override is a hardware-local behavior independent of SK/N2K availability).

### 2.3 `switch_bank` (this repo)

The bridge component. Translates `relay_ctrl`/`input_sense` state into:
- SignalK deltas, plus meta deltas for
  `displayName`/`manufacturer.name`/`manufacturer.model`, via `espos_sk`,
  on each enabled tree: `electrical.switches.bank.*` when
  `publishSwitchesTree` is on, `electrical.controls.*` when
  `publishControlsTree` is on (SPEC.md §6.1/§6.1a; the latter tracks
  SignalK/specification#441 — see RFC-441-DIGITAL-SWITCHING.md). Both
  trees are rendered from the same relay/input state, so no separate
  component is needed.
- NMEA2000 PGN 127501 transmissions (always, regardless of the SignalK
  tree toggles).

And translates incoming commands back into `relay_ctrl` calls:
- SignalK PUT handler registration (`espos_sk` PUT callback API) for
  relay paths on each enabled tree. Handlers on both trees resolve to the
  same `relay_ctrl` call; `controls.*` identifiers are parsed back to bank
  id + channel.
- PGN 127502 reception.

`espos_n2k` only sends and receives raw CAN frames, so the NMEA2000 side
uses Timo Lappalainen's NMEA2000 library for address claim, ISO requests,
product info, fast packets and heartbeat, with a small driver class that
sends and receives through `espos_n2k` (see
[plan 06](docs/plans/06-n2k-switch-bank.md)). `switch_bank` itself only
encodes 127501 and decodes 127502.

This is the only component that needs to know both "SignalK shape" and
"N2K shape" of a relay/input — `relay_ctrl` and `input_sense` stay
transport-agnostic.

### 2.4 `device_config` schema (this repo)

An espOS config descriptor (a JSON file added from CMake with
`espos_config_add_descriptor`) declaring this firmware's settings
(SPEC.md §9), plus typed accessors and the cross-field checks the
descriptor can't express. Descriptors are flat key lists, so per-channel
settings are numbered keys (`relay1_name` … `input8_invert`). espOS's
existing config REST/web UI renders and persists them.

### 2.5 espOS components consumed (not modified)

- `espos_sk` — SignalK discovery, token, delta stream, meta
  (`espos_sk_declare_meta`), PUT handlers (`espos_sk_put_handler_register`;
  a path must be published before it accepts PUTs).
  At most 16 PUT handlers (`ESPOS_SK_MAX_PUT_HANDLERS`), which 8 relays ×
  2 trees fills exactly. Stream connect/disconnect events
  (`ESPOS_EVENT_SK_STREAM_CONNECTED` / `_DISCONNECTED`) drive the
  SignalK-loss fail-safe.
- `espos_n2k` — raw CAN frames over TWAI (C++ `TwaiReceiver` /
  `TwaiTransmitter`) and a candump TCP server. No PGN support. The
  receiver has a single frame callback, so our NMEA2000 code and the
  candump server can't both receive (plan 06).
- `espos_net` / `espos_wifi` — network management and captive-portal
  provisioning. espOS always prefers Ethernet over WiFi when both are up;
  `wifi.sta_enabled` turns the WiFi station off. espOS's `espos_eth`
  doesn't support the W5500 (internal Ethernet MAC only), so Ethernet
  needs a transport of our own: the `eth_w5500` component, using
  ESP-IDF's `espressif/w5500` driver and reporting into espOS via
  `espos_net_register_if()` / `espos_net_report()`, as `espos_eth` does.
- `espos_config` — NVS-backed config store with JSON descriptors. Validates
  each key on its own; no hook for cross-setting rules.
- `espos_health` — warnings/alarms (used for I2C failures and config
  errors).
- `espos_httpd` — web UI + config REST server.
- `espos_ota` — signed OTA + rollback.

espOS has no GPIO component; pins are driven with ESP-IDF drivers
directly.

## 3. Data Models

Settings (SPEC.md §4, §9) live in `components/device_config/include/device_config.h`:
`device_config_t` holds the device-wide settings plus a `relay_cfg_t` and
an `input_cfg_t` per channel. It is a snapshot read from espOS's config
store by `device_config_load()`; components take a copy rather than
sharing one global, and reload on a config-change callback where a setting
applies live.

Runtime state (relay on/off, input readings) belongs to `relay_ctrl` and
`input_sense` respectively and is not part of the settings struct.

## 4. Technology Stack

| Layer | Choice | Why |
|---|---|---|
| Language | C (ESP-IDF 6), C++ for the NMEA2000 side | Relay/input logic in C; `espos_n2k` and the NMEA2000 library are C++. |
| Framework | ESP-IDF 6 + espOS component | espOS is the agreed base runtime; avoids reimplementing WiFi/SK/OTA plumbing. |
| SignalK transport | `espos_sk` (espOS) | Handles discovery/token/delta already; no separate SK client library needed. |
| NMEA2000/CAN | `espos_n2k` (espOS) + NMEA2000 library (Timo Lappalainen, MIT) | CAN transceiver is on TWAI TX GPIO17 / RX GPIO18. `espos_n2k` moves frames; the library provides the N2K protocol layer espOS lacks. |
| Relay driver | TCA9554 over ESP-IDF `i2c_master` | Relays sit behind the I2C expander, not on GPIOs. |
| Config storage | `espos_config` (NVS + JSON descriptors) | Reuses espOS's existing config store/UI instead of a bespoke one, per SPEC.md §7/§9. |
| Build | ESP-IDF CMake, component registry | This firmware is a top-level ESP-IDF project depending on the `signalk-espos/espos_*` component-registry packages (released in lockstep, pinned to one exact version), per espOS's documented integration path. |
| Testing | ESP-IDF host tests (Linux target) for `relay_ctrl`/`switch_bank` logic; on-target smoke tests for GPIO/CAN | espOS's own `test/host` pattern makes bank/state-machine logic (fail-safe transitions, momentary timing, DI override precedence) unit-testable without hardware. |

## 5. Integration Points

- **espOS component registry** — this firmware pins a specific espOS
  version (pre-1.0, so pinning is required per espOS's own guidance) and
  vendors its partition table / sdkconfig.defaults / signing key setup as
  documented by espOS's consuming-firmware instructions.
- **SignalK server** — via `espos_sk`'s mDNS discovery + token flow; no
  direct HTTP/WS code in this repo.
- **NMEA2000 bus** — via the board's onboard isolated CAN transceiver into
  the ESP32-S3 TWAI peripheral, with frames moved by `espos_n2k` and the
  protocol handled by the NMEA2000 library. Other N2K devices
  (MFDs, physical switch keypads) interact through standard PGNs only.
- **espOS web UI** — this firmware's only UI surface is the config schema
  it registers; no separate HTTP routes are added.

## 6. Security Considerations

- **Physical/electrical trust boundary**: relay outputs can switch
  real 250VAC/30VDC loads. `relay_ctrl` is the sole gatekeeper for
  writes to the relay expander; no other component (including the config
  web UI) writes to it directly, keeping the fail-safe/momentary invariants (SPEC.md §2) in one
  place.
- **SignalK auth**: relies entirely on espOS's existing `espos_sk` token
  acquisition flow — this firmware does not implement its own
  authentication and does not accept unauthenticated relay commands
  outside that flow.
- **NMEA2000 bus trust**: N2K/CAN has no authentication by design (per the
  protocol); anything on the physical bus can send PGN 127502. This is
  accepted as inherent to N2K (same trust model as any other N2K switch
  device) and is out of scope to change — physical bus access is the
  security boundary, consistent with standard marine electronics practice.
- **OTA**: uses espOS's signed OTA with rollback unmodified; this firmware
  does not add a separate update mechanism.
- **Config web UI**: inherits espOS's existing access model (local
  network / captive portal); this firmware adds no new unauthenticated
  network-facing surface beyond registering additional config fields.

## 7. File Structure

```
signalk-espOS-8relay/
├── CMakeLists.txt              # top-level ESP-IDF project, declares espos dependency
├── partitions.csv              # copied/adapted from espOS's reference partition table
├── sdkconfig.defaults
├── main/
│   └── main.c                  # espos_start() + component init/wiring
├── components/
│   ├── board/                  # pin map
│   ├── eth_w5500/              # W5500 Ethernet transport into espos_net
│   ├── relay_ctrl/
│   │   ├── relay_ctrl.c/.h
│   │   ├── tca9554.c/.h        # I2C expander driver
│   │   └── CMakeLists.txt
│   ├── input_sense/
│   │   ├── input_sense.c/.h
│   │   └── CMakeLists.txt
│   ├── switch_bank/
│   │   ├── sk_bridge.c/.h, paths.c/.h        # SignalK side
│   │   ├── n2k_bridge.cpp/.h, n2k_espos_driver.cpp/.h, switch_bank_pgn.c/.h  # NMEA2000 side
│   │   └── CMakeLists.txt
│   └── device_config/
│       ├── config/swbank.json  # espOS config descriptor
│       ├── device_config.c/.h  # typed accessors
│       └── CMakeLists.txt
├── test/
│   └── host/                   # one ESP-IDF linux-target test project per suite; run_all.sh runs them all
├── docs/
│   └── plans/                  # per-stage implementation plans
├── SPEC.md
├── ARCHITECTURE.md
├── RFC-441-DIGITAL-SWITCHING.md
└── IMPLEMENTATION_CHECKLIST.md
```

## 8. Deployment

- **Build/flash**: standard ESP-IDF (`idf.py build flash monitor`) against
  the ESP32-S3; USB-C for initial flashing/debug, matching the board's
  documented interface.
- **Runtime requirements**: 7-36VDC or 5V USB-C power (per board spec);
  Ethernet (RJ45) and/or WiFi network reachable to a SignalK server;
  NMEA2000 bus connection to the onboard isolated CAN transceiver's
  terminal if N2K integration is used.
- **First-time setup**: espOS's existing WiFi captive-portal provisioning
  flow, then this firmware's config schema fields (bank id, relay/input
  names, etc.) are filled in through espOS's config web UI.
- **Updates**: espOS's signed OTA mechanism; no separate update path.

## 9. Future Considerations

- Multi-relay scenes/groups and local scheduling (SPEC.md §10.2) would
  live in `switch_bank` or a new component sitting alongside it — the
  current one-component-per-concern split (I/O vs. bridging vs. config)
  should accommodate that without restructuring.
- If richer interlock logic is needed later than single-DI→single-relay,
  `input_sense`'s override callback is the natural extension point rather
  than something `switch_bank` should absorb.
- RS232/expansion header support, if ever taken on, should be a new
  sibling component rather than folded into `input_sense`/`relay_ctrl`,
  since it's electrically and functionally unrelated to the relay/DI bank.

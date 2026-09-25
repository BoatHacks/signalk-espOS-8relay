# signalk-espos-8relay Architecture

## 1. Overview

signalk-espos-8relay is an ESP-IDF 6 application built on top of the
[espOS](https://github.com/signalk-espOS/espOS) runtime. espOS owns
networking, SignalK transport, config storage, web UI, and OTA; this
project owns board I/O (relays/DI) and the switch-bank domain logic that
bridges GPIO state to SignalK and NMEA2000.

```
                        +-----------------------------+
                        |   signalk-espos-8relay app   |
                        |  (this repo, main/ + comps)  |
                        |                               |
   GPIO (RO1-8, DI1-8)  |  relay_ctrl / input_sense      |
   <------------------->|  switch_bank (SK + N2K bridge)|
                        |         |         |            |
                        +---------|---------|------------+
                                  |         |
                    espos_sk------+         +------espos_n2k
                    (SK deltas/PUT/meta)    (PGN 127501/127502,
                                              TWAI/CAN driver)
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

Owns the 8 relay GPIOs. Applies commands (on/off), enforces momentary
pulse timing (via an ESP-IDF timer per momentary-mode relay), and applies
fail-safe policy on boot and on SignalK-disconnect events. Depends on
espOS's `espos_gpio` for the actual pin driving and on `espos_config` for
persisted per-relay settings and `hold` state. Nothing else in this repo
talks to relay GPIOs directly — all relay state changes go through this
component so `switch_bank` (§2.3) always has one place to ask for/command
state.

### 2.2 `input_sense` (this repo)

Owns the 8 digital input GPIOs. Polls/debounces raw input state and
exposes a simple state-change callback. Applies the configured DI→relay
override mapping by calling into `relay_ctrl` directly on a DI edge (this
is the only cross-component write path outside of `switch_bank`, since the
override is a hardware-local behavior independent of SK/N2K availability).

### 2.3 `switch_bank` (this repo)

The bridge component. Translates `relay_ctrl`/`input_sense` state into:
- SignalK deltas on `electrical.switches.bank.*` paths, plus meta deltas
  for `displayName`/`manufacturer.name`/`manufacturer.model`, via `espos_sk`.
- NMEA2000 PGN 127501 transmissions, via `espos_n2k`.
- When `publishControlsTree` is enabled (SPEC.md §6.1a/§9, tracking
  SignalK/specification#441 — see RFC-441-DIGITAL-SWITCHING.md), a
  read-only mirror of the same state under `electrical.controls.*`. No
  separate component: it's the same relay/input state, the same bridge
  responsibility, just an additional delta path emitted alongside the
  canonical one.

And translates incoming commands back into `relay_ctrl` calls:
- SignalK PUT handler registration (`espos_sk` PUT callback API).
- PGN 127502 reception (`espos_n2k` callback API).

This is the only component that needs to know both "SignalK shape" and
"N2K shape" of a relay/input — `relay_ctrl` and `input_sense` stay
transport-agnostic.

### 2.4 `device_config` schema (this repo)

Not a runtime component but a JSON-Schema document registered with
espOS's `espos_config`, describing this firmware's config fields (bank id,
network preference, per-channel settings — SPEC.md §9). espOS's existing
config REST/web UI renders and persists it; this repo owns only the
schema and the typed accessors generated/hand-written around it.

### 2.5 espOS components consumed (not modified)

- `espos_gpio` — relay/DI pin access.
- `espos_sk` — SignalK discovery, token, delta stream, PUT registration.
- `espos_n2k` — NMEA2000 over TWAI, PGN encode/decode, candump TCP server.
- `espos_net` / `espos_wifi` / (Ethernet transport) — network interface
  management, captive portal provisioning.
- `espos_config` — NVS-backed config store, JSON-Schema-described fields.
- `espos_httpd` — web UI + config REST server.
- `espos_ota` — signed OTA + rollback.

## 3. Data Models

Runtime C structs mirror SPEC.md §4 directly:

```c
typedef enum { RELAY_MODE_LATCHING, RELAY_MODE_MOMENTARY } relay_mode_t;
typedef enum { FAILSAFE_HOLD, FAILSAFE_DEFAULT_SAFE } failsafe_policy_t;

typedef struct {
    uint8_t channel;          // 1-8
    char name[32];
    relay_mode_t mode;
    uint32_t pulse_ms;        // valid when mode == MOMENTARY
    failsafe_policy_t failsafe;
    int8_t override_di;       // -1 = none, else 1-8
    bool state;
} relay_channel_t;

typedef struct {
    uint8_t channel;          // 1-8
    char name[32];
    bool invert;
    bool state;
} input_channel_t;

typedef struct {
    uint32_t bank_id;
    net_pref_t network_pref;  // ETH_PREFERRED_WIFI_FALLBACK | WIFI_ONLY | ETH_ONLY
    relay_channel_t relays[8];
    input_channel_t inputs[8];
} device_config_t;
```

`device_config_t` is the in-RAM mirror of what `espos_config` persists;
`relay_ctrl`/`input_sense`/`switch_bank` all read/write through it rather
than hitting NVS directly.

## 4. Technology Stack

| Layer | Choice | Why |
|---|---|---|
| Language | C (ESP-IDF 6) | Required by espOS and ESP-IDF; no C++ needed for this scope. |
| Framework | ESP-IDF 6 + espOS component | espOS is the agreed base runtime; avoids reimplementing WiFi/SK/OTA plumbing. |
| SignalK transport | `espos_sk` (espOS) | Handles discovery/token/delta already; no separate SK client library needed. |
| NMEA2000/CAN | `espos_n2k` (espOS), TWAI driver | Board's onboard CAN transceiver is wired to the ESP32-S3's TWAI peripheral; espOS already exposes PGN encode/decode over it. |
| Config storage | `espos_config` (NVS + JSON Schema) | Reuses espOS's existing config store/UI instead of a bespoke one, per SPEC.md §7/§9. |
| Build | ESP-IDF CMake, component registry | This firmware is a top-level ESP-IDF project depending on `signalk-espos/espos` via the component registry (managed_components), per espOS's documented integration path. |
| Testing | ESP-IDF host tests (Linux target) for `relay_ctrl`/`switch_bank` logic; on-target smoke tests for GPIO/CAN | espOS's own `test/host` pattern makes bank/state-machine logic (fail-safe transitions, momentary timing, DI override precedence) unit-testable without hardware. |

## 5. Integration Points

- **espOS component registry** — this firmware pins a specific espOS
  version (pre-1.0, so pinning is required per espOS's own guidance) and
  vendors its partition table / sdkconfig.defaults / signing key setup as
  documented by espOS's consuming-firmware instructions.
- **SignalK server** — via `espos_sk`'s mDNS discovery + token flow; no
  direct HTTP/WS code in this repo.
- **NMEA2000 bus** — via the board's onboard isolated CAN transceiver into
  the ESP32-S3 TWAI peripheral, driven by `espos_n2k`. Other N2K devices
  (MFDs, physical switch keypads) interact through standard PGNs only.
- **espOS web UI** — this firmware's only UI surface is the config schema
  it registers; no separate HTTP routes are added.

## 6. Security Considerations

- **Physical/electrical trust boundary**: relay outputs can switch
  real 250VAC/30VDC loads. `relay_ctrl` is the sole gatekeeper for GPIO
  writes; no other component (including the config web UI) writes GPIO
  directly, keeping the fail-safe/momentary invariants (SPEC.md §2) in one
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
signalk-espos-8relay/
├── CMakeLists.txt              # top-level ESP-IDF project, declares espos dependency
├── partitions.csv              # copied/adapted from espOS's reference partition table
├── sdkconfig.defaults
├── main/
│   └── app_main.c              # espos_start() + component init/wiring
├── components/
│   ├── relay_ctrl/
│   │   ├── relay_ctrl.c/.h
│   │   └── CMakeLists.txt
│   ├── input_sense/
│   │   ├── input_sense.c/.h
│   │   └── CMakeLists.txt
│   ├── switch_bank/
│   │   ├── switch_bank.c/.h    # SK + N2K bridging
│   │   └── CMakeLists.txt
│   └── device_config/
│       ├── schema.json         # JSON Schema registered with espos_config
│       ├── device_config.c/.h  # typed accessors
│       └── CMakeLists.txt
├── test/
│   └── host/                   # ESP-IDF Linux-target unit tests (fail-safe, momentary, override precedence)
├── docs/
│   └── (SPEC.md / ARCHITECTURE.md live at repo root, per this skill's convention)
├── SPEC.md
└── ARCHITECTURE.md
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

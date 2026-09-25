# signalk-espos-8relay Specification

## 1. Introduction

### 1.1 Purpose

signalk-espos-8relay is device firmware for the Waveshare ESP32-S3-ETH-8DI-8RO-C
industrial relay board, turning it into a remotely controllable 8-channel
relay/8-input switch bank for a boat's electrical system. It lets a boat
owner switch loads (pumps, lights, pumps, solenoids, horn, etc.) from a
SignalK instrument panel, a mobile app, or a physical NMEA2000 keypad/MFD,
and lets other SignalK/N2K consumers see both relay and digital-input state
(e.g. a bilge float switch) in real time.

It is built as an application on top of [espOS](https://github.com/signalk-espOS/espOS),
which supplies the generic device runtime (networking, SignalK client,
config store, web UI, OTA). This project owns everything specific to the
8DI-8RO board: GPIO mapping, relay behavior, digital-input handling, the
SignalK switch-bank paths, and the NMEA2000 switch-bank PGNs.

### 1.2 Background

- **Board**: Waveshare ESP32-S3-ETH-8DI-8RO-C — ESP32-S3 (dual-core LX7),
  8 isolated relay outputs (≤10A 250VAC / ≤10A 30VDC), 8 isolated digital
  inputs, onboard isolated CAN transceiver, W5500 Ethernet (RJ45), WiFi/BLE,
  USB-C (power + programming).
- **Runtime**: [signalk-espOS/espOS](https://github.com/signalk-espOS/espOS)
  (ESP-IDF 6) — provides config store, WiFi provisioning + captive portal,
  Ethernet, mDNS, SignalK discovery/token/delta stream, web UI over
  LittleFS, signed OTA, and an `espos_n2k` component for NMEA2000 over TWAI.
  This firmware consumes espOS as a component-registry dependency; it does
  not fork or vendor espOS's runtime code.
- **Protocol conventions**: SignalK `electrical.switches.bank.*` path
  convention, and NMEA 2000 PGN 127501 (Binary Switch Bank Status) / 127502
  (Switch Bank Control), which this firmware maps directly to the same
  relay/DI state so the device behaves consistently whether commanded from
  SignalK or from the N2K bus.

### 1.3 Terminology

- **Relay / RO (relay output)** — one of the 8 switched outputs (RO1–RO8).
- **DI (digital input)** — one of the 8 isolated digital inputs (DI1–DI8).
- **Bank** — the set of 16 addressable switch-bank channels (8 relay + 8
  input) this device instance exposes, identified by a single bank
  instance id, per the SignalK/N2K switch-bank convention.
- **Momentary/pulse relay** — a relay configured to auto-return to off N
  milliseconds after being switched on, rather than latching.
- **Fail-safe mode** — a relay's configured behavior (`hold` or
  `default-safe`) when SignalK/network connectivity is lost or the device
  reboots.
- **Override mapping** — an optional configured link where a DI's state
  directly forces a relay's output, independent of SignalK/N2K commands.

## 2. Domain Rules

- Each relay and each digital input occupies one channel number (1–8)
  within its own bank; relay channels and DI channels are reported as two
  separate switch banks (a relay bank and an input bank) sharing one bank
  instance id space, per NMEA2000/SignalK convention (a bank is either
  outputs or inputs, not mixed).
- A relay's live state must always be observable from SignalK (delta) and
  from NMEA2000 (PGN 127501), and must be consistent between the two within
  normal transmission latency — this device is the single source of truth
  for relay state, whichever transport a command arrived on.
- A relay command may originate from: a SignalK PUT, an NMEA2000 PGN 127502
  control message, or (if configured) its mapped DI override. The most
  recent command from any source wins; the firmware does not attempt to
  reconcile simultaneous conflicting commands beyond last-write-wins.
- An override-mapped DI always takes precedence over a stale/absent SignalK
  or N2K command on boot, but a subsequent explicit command from either bus
  overrides the DI-forced state until the DI changes again (see §12 for the
  precedence rule in full).
- Momentary relays always auto-return to off after their configured pulse
  duration regardless of command source, and cannot be configured with
  `fail-safe: hold` (holding a momentary relay on indefinitely across a
  network outage is a non-goal — see §12).

## 3. State / Lifecycle Model

### 3.1 State Definitions

Each relay has:
- **Output state**: `on` / `off` (boolean, mirrors `electrical.switches.bank.<id>.<n>.state`).
- **Mode**: `latching` / `momentary` (config).
- **Fail-safe policy**: `hold` / `default-safe` (config; momentary relays
  are implicitly `default-safe`/off).

Each digital input has:
- **Input state**: `on` / `off`, debounced.
- **Override target**: none, or a relay channel it forces.

The device as a whole has a **connectivity state**: `sk-connected`,
`sk-disconnected`, used to evaluate fail-safe policy (N2K bus presence does
not affect fail-safe policy — a live N2K bus is treated as available
control regardless of SignalK connectivity).

### 3.2 Transitions

- `off → on`: via SignalK PUT, PGN 127502, or DI override edge, or boot
  restore (see below).
- `on → off`: same triggers, plus automatic transition for momentary relays
  after the pulse duration elapses.
- On boot: each relay initializes per its fail-safe policy — `hold`
  restores the last persisted state from NVS; `default-safe` forces `off`
  regardless of prior state.
- On SignalK disconnect exceeding a configured grace period: relays with
  `default-safe` policy transition to `off`; `hold` relays are unaffected.
  Reconnection does not itself change relay state (the server must
  re-PUT/re-sync if it wants a particular state).

## 4. Data Model

**RelayChannel** (1 per RO, 8 total)
- `channel`: 1–8
- `name`: user label (e.g. "Bilge Pump"), used for SignalK meta/displayName
  and NMEA2000 product-info-adjacent naming where applicable
- `mode`: `latching` | `momentary`
- `pulseMs`: momentary pulse duration (only when `mode: momentary`)
- `failSafe`: `hold` | `default-safe`
- `overrideDI`: optional DI channel number that force-drives this relay
- `state`: `on` | `off` (runtime, persisted per `failSafe` policy)

**DigitalInputChannel** (1 per DI, 8 total)
- `channel`: 1–8
- `name`: user label
- `invert`: bool, for normally-closed sensors
- `state`: `on` | `off` (runtime, not persisted — always read live)

**DeviceConfig**
- `bankId`: NMEA2000/SignalK switch bank instance id (default derived from
  device serial, overridable)
- `network`: interface preference (see §9)
- `relays[8]`, `inputs[8]`: the arrays above

## 5. Sources / Inputs

- **GPIO** — 8 relay driver outputs, 8 opto-isolated digital inputs, read
  via espOS's `espos_gpio` component; this is the ground truth for actual
  hardware state.
- **SignalK server** — delta stream (subscribe not required for control,
  but consumed for reflecting external state changes) and PUT requests via
  espOS's `espos_sk` component.
- **NMEA2000 bus** — PGN 127502 control messages via `espos_n2k`.
- **Local config store** — relay/DI configuration and persisted `hold`
  state, via espOS's `espos_config`.

If SignalK is unreachable, the N2K bus (if present) remains fully
functional for control and status; if N2K is absent/unused, SignalK/WiFi
control is unaffected. The two sources do not depend on each other.

## 6. API Specification

### 6.1 SignalK Paths

- `electrical.switches.bank.<bankId>.<n>.state` — relay output state
  (n = 1–8), writable via standard SignalK v1 PUT (no v2 switches API
  exists as of SignalK server v2 — confirmed against server docs, see
  §11). Meta delta includes `displayName` from the relay's configured
  `name`.
- `electrical.switches.bank.<bankId>.<n>.state` (separate bank id for
  inputs, or a documented offset/suffix convention — see Open Questions
  §13) — digital input state, read-only (PUT rejected).
- Firmware registers a SignalK PUT handler per relay path via espOS's
  `espos_sk_subscribe`/PUT-registration API; it does not implement a
  custom v2-style REST resource, since no such SignalK server API exists
  for switches.

### 6.2 NMEA2000 PGNs

- **127501 Binary Switch Bank Status** — transmitted periodically (and
  on-change) for both the relay bank and the input bank, via `espos_n2k`.
- **127502 Switch Bank Control** — received for the relay bank; individual
  channel commands are applied identically to a SignalK PUT.
- **60928 ISO Address Claim** / standard product info — device declares
  itself as a switch bank device class so it enumerates correctly on
  MFDs/keypads.

### 6.3 Local Config REST (via espOS web UI)

- Extends espOS's existing JSON-Schema-described config store with this
  firmware's schema: `bankId`, `network` preference, and per-channel relay
  (`name`, `mode`, `pulseMs`, `failSafe`, `overrideDI`) / input (`name`,
  `invert`) settings. No new REST surface or UI framework — same
  config store, same generated web UI form.

## 7. User Interface

No custom UI. Configuration happens through espOS's existing generic
config web UI (schema-driven form rendered from this firmware's JSON
Schema, per §6.3). Day-to-day relay control happens through whatever
SignalK instrument/app or N2K MFD/keypad the boat already uses — this
firmware does not ship its own control UI.

## 8. Persistence

- **Persisted (NVS, via espOS config store)**: device config (bank id,
  network preference, per-channel name/mode/pulseMs/failSafe/overrideDI/
  invert), and the last commanded state of each `hold`-policy relay.
- **Ephemeral**: digital input readings, `default-safe` relay state across
  reboot (always resets to off), connectivity state.

## 9. Configuration

User-tunable (via config store, §6.3):
- Bank instance id
- Network interface preference: Ethernet-preferred-with-WiFi-fallback, or
  fixed WiFi-only / Ethernet-only (WiFi captive-portal provisioning is
  always available regardless of this setting, per espOS)
- Per-relay: name, mode (latching/momentary), pulse duration, fail-safe
  policy, optional DI override source
- Per-input: name, invert (NC vs NO sensor)

Fixed (not user-tunable, board/firmware constants):
- Channel count (8 relays, 8 inputs)
- GPIO pin mapping to physical RO/DI terminals
- NMEA2000 PGN set supported (127501/127502)

## 10. MVP Scope

### 10.1 MVP Features

- Relay on/off control via SignalK PUT and via NMEA2000 PGN 127502.
- Relay state published via SignalK delta and PGN 127501.
- Digital input state published via SignalK delta and PGN 127501 (separate
  input bank).
- Per-relay momentary/pulse mode.
- Per-relay fail-safe policy (hold vs. default-safe).
- Per-DI-to-relay override mapping.
- SignalK meta (displayName) for relay/input names.
- NMEA2000 product info / device class declaration for switch bank.
- Configuration via espOS's existing config web UI/store.
- Ethernet-preferred-with-WiFi-fallback networking (built on espOS).

### 10.2 Post-MVP / Deferred

- Multi-relay group/scene control (e.g. "all off") — not needed until
  real usage shows a pattern; keep MVP to per-channel control.
- Local scheduling/timers (e.g. time-of-day relay control) — deferred;
  SignalK-side automation (Node-RED, etc.) can already do this without
  firmware changes.
- Interlock logic beyond simple single-DI-forces-single-relay (e.g.
  multi-condition rules) — deferred; keep the override mapping minimal for
  MVP and revisit only if a real use case needs it.
- RS232/expansion header support — the board exposes an expansion GPIO
  header not used by this spec at all; out of scope entirely, not just
  deferred.

## 11. References

- [signalk-espOS/espOS](https://github.com/signalk-espOS/espOS) — runtime this firmware builds on.
- [Waveshare ESP32-S3-ETH-8DI-8RO-C wiki](https://www.waveshare.com/wiki/ESP32-S3-ETH-8DI-8RO-C) — board hardware reference.
- [SignalK PUT Requests spec](https://signalk.org/specification/1.7.0/doc/put.html)
- [SignalK REST/v2 API docs](https://demo.signalk.org/documentation/Developing/REST_APIs.html) — confirms no dedicated v2 switches endpoint exists (checked during this brainstorm); v1 PUT-on-path is the correct mechanism.
- NMEA 2000 PGN 127501 (Binary Switch Bank Status) and PGN 127502 (Switch
  Bank Control) — standard switch bank PGNs.

## 12. Design Decisions

- **SignalK v1 PUT over a custom or v2 API**: researched during this
  brainstorm — SignalK server v2's REST APIs (Autopilot, Course, History,
  Radar, Resources, Notifications, BLE) do not include an electrical
  switches resource. Standard v1 PUT-on-path is therefore both the only
  option and the conventional one; it also interoperates with existing
  SignalK switch-bank instruments without this firmware needing to
  document a bespoke API.
- **`electrical.switches.bank.*` over a flat custom path**: chosen because
  it matches the NMEA2000 switch-bank PGN semantics 1:1, so the SignalK
  and N2K representations of a relay are the same concept at two
  transports, not two separate models that need reconciling.
- **espOS as a dependency, not a fork**: espOS explicitly positions itself
  as "not a sensor framework" and expects device-specific logic to sit on
  top via its component registry. Vendoring/forking it would create
  merge/update pain for no benefit, since none of its runtime code needs
  board-specific changes.
- **Last-write-wins across command sources**: simplest correct behavior
  for a device with three possible command origins (SK, N2K, DI override)
  and no natural priority order between SK and N2K. DI override is
  special-cased to apply only on boot / DI edge, not continuously, so an
  explicit SK/N2K command can still control the relay afterward — this
  avoids a DI override silently fighting every subsequent remote command.
- **Momentary relays can't be `hold`**: holding a pulsed/momentary output
  on indefinitely across a fail-safe "hold last state" reboot would mean a
  horn or pump-test relay could stick on for an unbounded time with no
  bus activity — treated as unsafe by construction rather than a
  configuration a user could accidentally select.

## 13. Open Questions

- Exact bank-id convention for the separate input bank: whether digital
  inputs get their own bank instance id (e.g. `bankId + 1` or a fixed
  offset) or a documented sub-path — needs a decision before implementing
  §6.1, but doesn't affect anything else in this spec.
- Debounce timing for digital inputs is unspecified — needs a sensible
  default (likely tens of ms) validated against real float-switch/sensor
  behavior once hardware is in hand.
- Whether `bankId` should default from the ESP32's factory MAC/serial (to
  avoid collisions with zero config) or default to a fixed value the user
  must change when running multiple boards — leaning toward MAC-derived
  default per the earlier answer, but needs to be finalized in
  ARCHITECTURE.md against what espOS's device-id APIs actually expose.

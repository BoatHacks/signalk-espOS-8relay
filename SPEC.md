# signalk-espOS-8relay Specification

## 1. Introduction

### 1.1 Purpose

signalk-espOS-8relay is device firmware for the Waveshare ESP32-S3-ETH-8DI-8RO-C
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
- **Bank** — a set of up to 8 addressable switch-bank channels, identified
  by a bank instance id, per the SignalK/N2K switch-bank convention. This
  device exposes two banks: the **relay bank** (`bankId`, 8 outputs) and
  the **input bank** (`inputBankId`, 8 inputs).
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
  separate switch banks (relay bank `bankId`, input bank `inputBankId`),
  per NMEA2000/SignalK convention (a bank is either outputs or inputs, not
  mixed). The two ids must differ, and must not collide with any other
  switch bank on the same N2K bus or SignalK server.
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
- **Output state**: `on` / `off` (boolean; published as `electrical.switches.bank.<bankId>.<n>.state` and/or its `electrical.controls.*` equivalent).
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
- On SignalK disconnect exceeding the grace period `skLossGraceS`
  (default 30 s, §9): relays with
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
- `bankId`: relay bank instance id, 0–252 (default `0`)
- `inputBankId`: input bank instance id, 0–252 (default `1`); must differ
  from `bankId`
- `debounceMs`: digital input debounce time (default `50`)
- `skLossGraceS`: seconds without SignalK before `default-safe` relays
  turn off (default `30`)
- `ethEnabled`: bool, default `true` — use the W5500 Ethernet port (see §9)
- `publishSwitchesTree`: bool, default `true` — publish and accept PUTs on
  `electrical.switches.bank.*` (§6.1)
- `publishControlsTree`: bool, default `false` — publish and accept PUTs on
  `electrical.controls.*` (§6.1a, RFC 0009)
- `relays[8]`, `inputs[8]`: the arrays above

## 5. Sources / Inputs

- **GPIO** — 8 relay driver outputs, 8 opto-isolated digital inputs, read
  (relays through the board's I2C expander, inputs directly); this is the
  ground truth for actual
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

The firmware can publish relay/input state under two path trees, each
with its own on/off setting: `electrical.switches.bank.*` (this section,
`publishSwitchesTree`, default on) and `electrical.controls.*` (§6.1a,
`publishControlsTree`, default off). Rules that apply to both:

- Every enabled tree accepts SignalK PUTs for relay channels. With both
  on, a relay can be commanded on either path; both land in the same
  relay, so the usual last-write-wins rule (§2) applies and both trees
  always report the same state.
- Input channels are read-only on every tree (PUT rejected).
- Both trees may be off. The device then publishes nothing to SignalK
  and is controlled only over NMEA2000 (§6.2) and by input overrides;
  espOS's SignalK connection, web UI and OTA keep working.

**`electrical.switches.bank.*`**:

- `electrical.switches.bank.<bankId>.<n>.state` — relay output state
  (n = 1–8), writable via standard SignalK v1 PUT (no v2 switches API
  exists as of SignalK server v2 — confirmed against server docs, see
  §11). Meta delta includes `displayName` from the relay's configured
  `name`.
- `electrical.switches.bank.<inputBankId>.<n>.state` — digital input
  state (n = 1–8), read-only (PUT rejected).
- Firmware registers a SignalK PUT handler per relay path, on each
  enabled tree, via espOS's
  `espos_sk_subscribe`/PUT-registration API; it does not implement a
  custom v2-style REST resource, since no such SignalK server API exists
  for switches.
- All paths carry static `manufacturer.name` (`Waveshare`) and
  `manufacturer.model` (`ESP32-S3-ETH-8DI-8RO-C`) meta, per RFC 0009 —
  see RFC-441-DIGITAL-SWITCHING.md.

### 6.1a `electrical.controls.*` tree (RFC 0009, opt-in)

When `publishControlsTree` is enabled (default off — see §9, §12, and
RFC-441-DIGITAL-SWITCHING.md), relay/input state is published under
`electrical.controls.<identifier>`, matching the path
shape proposed in [SignalK/specification#441](https://github.com/SignalK/specification/issues/441).

Internally, and on `electrical.switches.bank.*`, identity stays the
numeric bank id + channel pair (§4). Only the `controls.*` tree uses RFC
0009's string-identifier convention, built as:

- Relay channel `n`: `espOS-instance<bankId>-relay<n>`
- Digital input channel `n`: `espOS-instance<inputBankId>-input<n>`

Each identifier uses the id of the bank the channel actually lives in, so
`electrical.switches.bank.<X>.<n>` and `espOS-instance<X>-…<n>` always
name the same channel. The firmware derives this 1:1 mapping (bank
id + channel + kind ↔ string identifier); it is never separately
configured, so the two trees can never drift apart or collide.

- `electrical.controls.<identifier>.state` (on/off; writable via PUT for
  relay identifiers, read-only for input identifiers)
- `electrical.controls.<identifier>.type` = `"switch"` (this board has no dimmers)
- `electrical.controls.<identifier>.name`
- `electrical.controls.<identifier>.meta.displayName`
- `electrical.controls.<identifier>.manufacturer.name` / `.manufacturer.model`

Example: relay channel 3 on bank 12 is `electrical.switches.bank.12.3`
and `electrical.controls.espOS-instance12-relay3`. A PUT to either path
switches the same relay, and both paths then report the new state.

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
  firmware's settings: `bankId`, `inputBankId`, `debounceMs`,
  `skLossGraceS`, `ethEnabled`, `publishSwitchesTree`,
  `publishControlsTree`, and per-channel relay (`name`, `mode`, `pulseMs`,
  `failSafe`, `overrideDI`) / input (`name`, `invert`) settings. No new
  REST surface or UI framework — same config store, same generated web UI
  form.
- espOS validates each setting on its own and has no way to reject a save
  that breaks a rule spanning two settings. So if `inputBankId == bankId`,
  the save is accepted, the device raises an `espos_health` warning naming
  the problem, and the input bank is not published (SignalK or NMEA2000)
  until the ids differ.

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
- Relay bank id `bankId` (default `0`) and input bank id `inputBankId`
  (default `1`); must differ from each other
- Digital input debounce `debounceMs` (default `50`), one value for all
  8 inputs
- SignalK-loss grace period `skLossGraceS` (default `30` s), after which
  `default-safe` relays turn off (§3.2)
- Network: Ethernet is preferred whenever it has an address, with WiFi as
  fallback (espOS's fixed rule). Two switches select the other modes:
  espOS's own "Station enabled" WiFi setting (off = Ethernet only), and
  this firmware's `ethEnabled` (off = WiFi only). WiFi's setup access
  point stays available either way. `ethEnabled` depends on this firmware
  providing its own W5500 driver, since espOS doesn't support the W5500.
- Per-relay: name, mode (latching/momentary), pulse duration, fail-safe
  policy, optional DI override source
- Per-input: name, invert (NC vs NO sensor)
- `publishSwitchesTree`: bool, default `true` — the
  `electrical.switches.bank.*` tree (§6.1)
- `publishControlsTree`: bool, default `false` — the
  `electrical.controls.*` tree (§6.1a, RFC 0009 compatibility; see
  RFC-441-DIGITAL-SWITCHING.md). Any combination is valid, including both
  off (NMEA2000-only operation).

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
- **Two independently switchable path trees, both writable**:
  SignalK/specification#441 ("RFC 0009: Digital Switching") proposes a
  separate `electrical.controls.*` tree for digital switching devices,
  which the accepted `electrical.switches.bank.*` convention doesn't
  share. The RFC is open, unmerged and undiscussed, so `switches.bank.*`
  is on by default and `controls.*` is off by default, but each has its
  own toggle. Every enabled tree accepts relay PUTs. This does not create
  two sources of truth: both trees are views of the one relay state held
  in `relay_ctrl`, so a PUT on either path updates both. Both trees off is
  allowed, for installs that want NMEA2000-only control but still use
  espOS for OTA and the web UI. Internal identity stays numeric bank id + channel; the
  `controls.*` tree alone is addressed with RFC 0009's string-identifier
  shape (`espOS-instance<bankId>-relay<n>` /
  `espOS-instance<inputBankId>-input<n>`), derived deterministically from
  bank id + channel + kind so the mapping between the two trees can never
  drift or collide. Full comparison and rationale in
  RFC-441-DIGITAL-SWITCHING.md.
- **Separately configurable input bank id**: inputs get their own
  `inputBankId` (default `1`) rather than a derived `bankId + 1`. A
  derived id would force boards on the same boat to keep bank ids at least
  2 apart; a separate setting lets each bank be placed freely.
- **Fixed default bank ids (`0` / `1`), not MAC-derived**: predictable
  and easy to document. The cost is that a second board on the same boat
  collides with the first until someone reconfigures it; reconfiguring
  bank ids is a required setup step when installing more than one board.
- **Single global debounce (50 ms default)**: one configurable value for
  all 8 inputs is enough for MVP; per-input debounce can be added later
  if a mix of fast and slow/noisy sensors needs it. The default should be
  checked against a real float switch once hardware is available.
- **Momentary relays can't be `hold`**: holding a pulsed/momentary output
  on indefinitely across a fail-safe "hold last state" reboot would mean a
  horn or pump-test relay could stick on for an unbounded time with no
  bus activity — treated as unsafe by construction rather than a
  configuration a user could accidentally select.

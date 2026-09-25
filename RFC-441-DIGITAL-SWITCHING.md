# RFC 0009 "Digital Switching" (SignalK/specification#441) — Notes & Conflicts

This is a specification-adjacent reference document, not part of SPEC.md.
It consolidates [SignalK/specification#441](https://github.com/SignalK/specification/issues/441)
("RFC 0009: Digital Switching") and records where its proposal overlaps or
conflicts with the design already committed in SPEC.md/ARCHITECTURE.md for
this firmware. **The RFC is an open, unmerged proposal — not part of the
accepted SignalK specification.** It is not authoritative, but it's worth
tracking since it targets the same domain (digital switching devices fed
by NMEA 2000, explicitly citing Raymarine EmpirBus as a motivating
example) that this firmware also operates in.

## 1. What the RFC proposes

**Source**: [SignalK/specification#441](https://github.com/SignalK/specification/issues/441), open, no recorded discussion/comments as of this writing.

**Summary** (quoting the issue): "Digital Switching is not yet part of the
schema. Some Digital Switching bus systems like e.g. Raymarine EmpirBus are
sending their data via NMEA 2000 and therefore can be monitored and
controlled via Signal K."

**Scope**: covers two device types — switches (`on`/`off`) and dimmers
(0–100%, expressed as 0..1).

**Proposed path tree**, rooted at `electrical/controls/<identifier>`:

```
electrical/controls/<identifier>
electrical/controls/<identifier>/state              (on|off)
electrical/controls/<identifier>/brightness          (0..1, 1 = 100%)
electrical/controls/<identifier>/type                (switch | dimmer | etc.)
electrical/controls/<identifier>/name                (system name, e.g. "Switch 0.8")
electrical/controls/<identifier>/meta/displayName     (display name)
electrical/controls/<identifier>/associatedDevice/instance
electrical/controls/<identifier>/associatedDevice/device
electrical/controls/<identifier>/source
electrical/controls/<identifier>/dataModel
electrical/controls/<identifier>/manufacturer/name
electrical/controls/<identifier>/manufacturer/model
```

**Identifier convention**: `<identifier>` = `systemname-deviceaddress`,
e.g. `empirBusNxt-instance<instance>-dimmer|switch<#>` — the path encodes
the source system and address rather than a bare bank/channel number.

**Design rationale given for separating `state` from `brightness`**: "A
dimmer can be switched off, but it can have a stored brightness level that
is to be set when the dimmer is switched on again" — modeled after
HomeKit's separate `On` / `Brightness` characteristics.

## 2. Comparison against this project's current design

| Aspect | This firmware (SPEC.md) | RFC 0009 (#441) |
|---|---|---|
| Base path | `electrical.switches.bank.<bankId>.<n>` (existing, accepted SignalK switch-bank convention) | `electrical.controls.<identifier>` (new, proposed) |
| Identifier | Numeric bank id + numeric channel (1–8) | Composite string `systemname-deviceaddress` |
| State field | `state` (on/off) — same name | `state` (on/off) — same name |
| Dimming | Not modeled (relay board is on/off only) | `brightness` (0..1), explicit design intent to keep `state` and `brightness` independent |
| Naming/meta | `meta.displayName`, sourced from this firmware's config (SPEC.md §4, §6.1) | `name` (system name) + `meta/displayName` (display name) — two distinct fields where this project currently only has one |
| Device/source metadata | Not currently modeled beyond bank id | `associatedDevice/instance`, `associatedDevice/device`, `source`, `dataModel`, `manufacturer/name`, `manufacturer/model` |
| NMEA2000 mapping | Explicit: PGN 127501/127502 switch-bank PGNs (SPEC.md §6.2) | Not addressed in the RFC text as fetched — the RFC's own motivating example (EmpirBus) is NMEA2000-sourced, but the proposal doesn't specify a PGN mapping |
| Maturity | This is our own committed design | Open GitHub issue, unmerged, no visible community discussion |

## 3. Identified conflicts

### 3.1 Path convention conflict (blocking)

SPEC.md commits to `electrical.switches.bank.<bankId>.<n>.state` — chosen
specifically (SPEC.md §12) because it maps 1:1 to the NMEA2000 switch-bank
PGNs (127501/127502) this firmware also implements. RFC 0009 proposes a
different, sibling path tree (`electrical.controls.*`) for the same
general domain (NMEA2000-sourced digital switching). These are not
reconcilable by using both simultaneously without either: (a) publishing
the same relay state twice under two different path trees, or (b)
picking one.

### 3.2 Identifier shape conflict

RFC 0009's identifier embeds the source system name and device address in
the path segment itself (`empirBusNxt-instance2-switch3`). This project's
convention uses a numeric bank id + numeric channel, per the existing
switch-bank spec. Adopting RFC 0009's identifier shape *only* on the
`controls.*` mirror (not internally, not on `switches.bank.*`) resolves
this without a structural change to SPEC.md §4 — see resolution below.

### 3.3 Metadata richness gap (non-blocking, additive)

RFC 0009's `manufacturer/*`, `associatedDevice/*`, `dataModel`, and
`source` fields aren't in this project's current data model at all. These
don't conflict with the bank-path convention — they could be added as
meta/attribute deltas under the existing `electrical.switches.bank.*`
paths without changing the path scheme. This is an additive gap, not a
logical conflict.

### 3.4 Dimming — not applicable

The ESP32-S3-ETH-8DI-8RO-C's relays are on/off only; `brightness` doesn't
apply to this hardware. No conflict, just out of scope.

## 4. Resolution (decided 2026-09-25)

**§3.1 Path convention** — Publish under **both** trees, each independently
toggleable in the config UI:
- `electrical.switches.bank.*` — **on by default**. Remains the primary,
  N2K-aligned representation.
- `electrical.controls.*` — **off by default**. Opt-in mirror for RFC 0009
  compatibility, for installations that want it ahead of (or regardless
  of) the RFC being merged. SPEC.md/ARCHITECTURE.md updated accordingly
  (see §5 below).

**§3.2 Identifier scheme** — Amended 2026-09-25 (superseding the "no
change" note above): internally, and on `electrical.switches.bank.*`,
identity stays numeric `bankId` + channel, unchanged. On
`electrical.controls.*` specifically, use RFC 0009's string-identifier
shape, built deterministically from that same numeric identity:
- Relay channel `n`: `espOS-instance<bankId>-relay<n>`
- Digital input channel `n`: `espOS-instance<bankId>-input<n>`

The two identifiers (numeric on `switches.bank.*`, string on
`controls.*`) always describe the same channel — the mapping is derived,
never separately configured, so the trees cannot drift apart or collide.
`-input<n>` for digital inputs is this project's own extension of the
RFC's pattern (the RFC's examples only cover `-switch<#>`/`-dimmer<#>`
outputs, not inputs) — flagged here in case the RFC or its eventual
implementation converges on different wording for input channels.

**§3.3 Metadata richness gap** — `manufacturer.name` / `manufacturer.model`
static meta fields are added now (`Waveshare` / `ESP32-S3-ETH-8DI-8RO-C`),
published on both trees. `associatedDevice.*`, `dataModel`, and `source`
remain deferred (Post-MVP) — they describe multi-device gateway topology
this single-board firmware doesn't have.

## 5. Consequences for SPEC.md / ARCHITECTURE.md

Applied in the same change as this resolution:
- SPEC.md §4 (Data Model): `RelayChannel`/`DigitalInputChannel` gain a
  `controlsEnabled` flag is unnecessary — control is global per §9, not
  per-channel; SPEC.md §9 (Configuration) gains `publishControlsTree: bool`
  (default `false`) alongside the always-on switches-bank publishing.
- SPEC.md §6.1a (API): documents the `electrical.controls.<identifier>`
  mirror path (identifier = `espOS-instance<bankId>-relay<n>` /
  `-input<n>`, derived from `bankId`+channel, never separately
  configured) and its fields (`state`, `name`, `meta.displayName`,
  `manufacturer.name`, `manufacturer.model`, `type: "switch"`), gated by
  `publishControlsTree`.
- SPEC.md §12 (Design Decisions): records this resolution and its
  rationale.
- ARCHITECTURE.md §2.3 (`switch_bank`): now also owns emitting the
  `electrical.controls.*` mirror when enabled, from the same relay/input
  state — no new component, since it's the same bridge responsibility.

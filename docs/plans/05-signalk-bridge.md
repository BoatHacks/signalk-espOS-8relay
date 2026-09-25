# Implementation Plan: 05 — SignalK bridge

## Overview
The SignalK half of `switch_bank`: publish relay and input state on the
enabled path trees, accept relay PUTs on each of them, declare metadata,
and tell `relay_ctrl` when the SignalK server is lost.

## Relevant SPEC/ARCHITECTURE Sections
- SPEC.md §6.1 (both trees, PUT rules), §6.1a (`controls.*` identifiers),
  §3.2 (SignalK-loss fail-safe), §12
- ARCHITECTURE.md §2.3 (`switch_bank`)
- [RFC-441-DIGITAL-SWITCHING.md](../../RFC-441-DIGITAL-SWITCHING.md)

## Approach

**Paths.** One table maps each (tree, bank id, channel, kind) to its path
string, built at boot from config:
- `electrical.switches.bank.<bankId>.<n>.state` /
  `electrical.switches.bank.<inputBankId>.<n>.state`
- `electrical.controls.espOS-instance<bankId>-relay<n>.*` /
  `electrical.controls.espOS-instance<inputBankId>-input<n>.*`

The same table parses an incoming PUT path back to a channel, so the two
trees can't disagree about what a path means. Host tests cover both
directions.

**Publishing.** espOS only accepts PUTs on paths the device has already
published, so each state is published before its handler is registered.
State is published on every change and on connect. Meta
(`displayName`, and `manufacturer.name`/`model` on the switches tree) goes
through `espos_sk_declare_meta` and is re-declared when a name changes.
On the `controls.*` tree, `type`, `name` and `manufacturer.*` are
published as values, per SPEC.md §6.1a.

**PUT.** One handler per relay path on each enabled tree, all calling
`relay_ctrl_set(…, SOURCE_SK)`. Input paths get no handler, so espOS
answers 405. Accept `true`/`false` and `1`/`0` as values; anything else
returns `ESP_ERR_INVALID_ARG` (400).

**Value format — verify first.** Check how SignalK's NMEA2000 converter
(`n2k-signalk`) publishes PGN 127501 on
`electrical.switches.bank.*.state` (boolean or 0/1 number) and publish
the same type, so SignalK clients see one format regardless of source.

**SignalK loss.** Subscribe to `ESPOS_EVENT_SK_STREAM_DISCONNECTED` /
`_CONNECTED`; when disconnected for longer than `skLossGraceS` (default
30 s), call `relay_ctrl_sk_lost()`.

**Risk to raise before building.** If the SignalK server also receives
the NMEA2000 bus through a gateway, it will convert this device's own
127501 broadcasts into the same `electrical.switches.bank.*` paths, from
a second source. Values will agree, but clients may show two sources, and
server plugins that turn switch-bank PUTs into 127502 could also claim
these paths. Decide whether to accept this, document it, or change
something (for example, recommending the gateway filter this device's
source address).

## Test Strategy
Host tests with a fake `espos_sk`: path table in both directions for
several bank ids; PUTs on either tree switch the same relay; input PUTs
rejected; toggles control which paths exist; publish happens before
handler registration; meta re-declared on rename. Manual: SignalK
instrument panel toggles relays via both trees.

## Implementation Steps
- [ ] Settle the value format and the duplicate-source risk (above)
- [ ] Path table and parser
- [ ] Publishing and meta, per tree toggle
- [ ] PUT handlers
- [ ] SignalK-loss detection
- [ ] Host tests
- [ ] Update SPEC.md §6.1 with the value format

## Files to Create/Modify
- `components/switch_bank/` (`sk_bridge.c/.h`, `paths.c/.h`)
- `test/host/test_sk_paths.c`, `test/host/test_sk_bridge.c`
- `SPEC.md` §6.1

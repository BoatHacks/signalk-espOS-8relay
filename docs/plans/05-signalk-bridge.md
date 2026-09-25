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
- [x] Value format: numbers 1/0 plus `.order`, as n2k-signalk publishes
      PGN 127501. Duplicate sources: documented, with the switches-tree
      toggle as the remedy (user decision)
- [x] Paths built in one function for both trees; no parser needed, since
      each PUT handler carries its channel as its argument
- [x] Publishing and metadata per tree toggle; everything republished on
      each stream connect (the server routes PUTs only to paths seen on the
      current connection)
- [x] PUT handlers on every enabled tree (16 with both on, espOS's limit)
- [x] SignalK-loss detection from espOS's stream events; only after a
      connection was lost, and only while a tree is published
- [x] `CONFIG_ESPOS_SK_MAX_META=48`: espOS uses up to 9 metadata slots,
      both trees need 32
- [x] Host tests (`test/host/sk_bridge_test`, 18 tests)
- [x] SPEC.md §6.1 and USER_MANUAL.md §7.1 updated (value format, names,
      duplicate sources)
- [ ] On the board: an instrument panel switches relays via both trees;
      pulling the server's cable triggers the fail-safe after 30 s

Found while building: espOS only writes metadata the server doesn't
already have, so renames on the device don't reach SignalK apps after the
first connection. Documented; changing it would need espOS support.

## Files to Create/Modify
- `components/switch_bank/` (`sk_bridge`, `sk_espos`)
- `main/main.c`, `sdkconfig.defaults`
- `test/host/sk_bridge_test/`
- `SPEC.md` §6.1, `USER_MANUAL.md` §7.1

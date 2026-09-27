# Implementation Plan: 11 — Cycle counters and runtime hours

Issue: [#4](https://github.com/BoatHacks/signalk-espOS-8relay/issues/4)

## Overview
For every relay and input: how many times it turned on, and its total
on-time. Kept across restarts, published to SignalK, shown on the relay
page, and resettable.

## Relevant SPEC/ARCHITECTURE Sections
- SPEC.md §3, §6
- ARCHITECTURE.md §2 (new `counters` component)

## Approach
- New component `counters` with a pure core: `counters_on_change(ch,
  on, now_ms)`, `counters_tick(now_ms)`, `counters_get(ch, &cycles,
  &runtime_s)`, `counters_reset(ch)`. Fed from the relay and input
  listeners in `main.c`, so every source is counted (SignalK, NMEA 2000,
  web, input, pulse end, fail-safe, max-on).
- **Persistence without wearing out the flash.** Keep counters in RAM;
  save to NVS through a store interface (like `relay_ctrl`'s
  `relay_state_store_t`) at most every 10 minutes and only if something
  changed, plus on a clean restart (OTA, settings restart) via an
  `esp_register_shutdown_handler`. Accept losing up to 10 minutes on a
  power cut; say so in the manual. One NVS blob for all 16 channels, not
  32 keys, and not in espOS's settings namespace (these aren't settings).
- Runtime counts whole seconds; the running on-period is added when the
  channel turns off and at each save, so a channel that is always on
  still accumulates.
- **SignalK:** `electrical.switches.bank.<b>.<n>.cycles` and
  `.runTime` (seconds, `units: "s"` in metadata), for both banks, and
  the controls-tree equivalents when enabled. Published with the
  republish interval, not every second.
- **Relay page / REST:** `GET /api/v1/relays` gains `cycles` and
  `runTime` per relay and input; `POST /api/v1/relays/<n>/counters/reset`
  and the input equivalent, protected and JSON-only like the others.
- Counters are 32-bit; runtime wraps after 136 years.

## Open questions
- Reset everything when the bank id changes? No — counters belong to
  the physical channel. Document it.

## Test Strategy
Host tests for `counters` (new `counters_test`):
- Cycles count off→on edges only; repeated "on" doesn't count.
- Runtime accumulates across on-periods and while continuously on.
- Save throttling: at most one save per interval, none without changes,
  one on shutdown; restore after "reboot" gives the saved values.
- Reset clears one channel only.
`sk_bridge_test`: counters published with the states on the interval.
`web_ui_test`: JSON fields.

## Implementation Steps
- [x] `counters` component and store interface (NVS on device, fake in
      tests)
- [x] Wire listeners and tick in `main.c`; shutdown handler
- [x] SignalK publishing (bridge gets a counters getter)
- [x] REST fields and reset endpoints; relay page display and reset
- [x] Host tests
- [x] USER_MANUAL §7; CHANGELOG

## Implementation notes (2026-09-27)

- **The first report for a channel seeds it, uncounted.** `relay_ctrl`
  never notifies listeners of a relay's boot state (only `main.c`'s own
  `web_ui_relay_changed(ch, "boot")` loop knows it), while `input_sense`'s
  first settled reading after boot *does* arrive as an ordinary listener
  call. To treat both the same way without special-casing either,
  `counters_on_change()`'s very first call for a channel is always a seed
  (record the state, start runtime if on, count no cycle) rather than an
  edge. `main.c` calls it once per relay right after `relay_ctrl_init()`,
  mirroring the existing `web_ui_relay_changed(ch, "boot")` seeding; inputs
  get seeded automatically by their first real listener call. This isn't
  spelled out in the plan's own test strategy, so it's worth a second look.
- **Reset flushes immediately**, ahead of the usual throttle: it's a rare,
  deliberate user action (not the automatic counting the throttle exists
  to protect the flash from), and the UX of "I reset it and a moment later
  it reappeared" seemed worse than one extra flash write. Not explicit in
  the plan.
- **Factory reset (plan 14) does not clear counters.** Plan 14 was written
  before this component existed and only notes "no cycle counters exist
  yet, issue #4" for its own future reference; it isn't wired up here.
  Counters are treated as belonging to the physical channel (the plan's
  own open question about bank-id changes points the same way), so a
  factory reset — which does forget the SignalK token and `hold` state —
  leaves counters alone; only the per-channel Reset button clears them.
  This is a judgment call, not a decision recorded anywhere.

## Files to Create/Modify
- `components/counters/` (new)
- `main/main.c`
- `components/switch_bank/src/sk_bridge.c`
- `components/web_ui/`
- `test/host/counters_test/` (new), `sk_bridge_test`, `web_ui_test`
- `USER_MANUAL.md`, `CHANGELOG.md`

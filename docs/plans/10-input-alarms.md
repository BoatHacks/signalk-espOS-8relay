# Implementation Plan: 10 — Input alarms as SignalK notifications

Issue: [#3](https://github.com/BoatHacks/signalk-espOS-8relay/issues/3)

## Overview
Per input, an optional alarm: when the input turns on (after debounce
and invert), raise a SignalK notification; clear it when the input
turns off. The typical use is a bilge float switch.

## Relevant SPEC/ARCHITECTURE Sections
- SPEC.md §3.1 (inputs), §6 (SignalK)
- ARCHITECTURE.md §2.3 (`sk_bridge`)
- Plan 05 (SignalK bridge)

## Approach
- Settings per input: `i<n>_alarm` (`off` default, `warn`, `alarm`,
  `emergency`) and `i<n>_alarm_msg` (empty = "<input name> active").
- **Path.** espOS's `espos_sk_notify(key, state, message)` publishes
  under `notifications.espos.<device label>.<key>` and only has
  normal/warn/alarm. That is fine for device health but not for a
  bilge alarm people key rules on. So the bridge publishes the
  notification itself with `publish_json` on the input's own path:
  `notifications.electrical.switches.bank.<inputBank>.<n>.state` (and
  `notifications.electrical.controls.espOS-instance<b>-input<n>` when the
  controls tree is on), value
  `{"state": "alarm", "method": ["visual", "sound"], "message": "..."}`;
  `{"state": "normal", ...}` to clear.
- Republish active alarms with the SignalK republish interval and after
  every reconnect, in the same code path as the states (added in 0.0.5).
- Only once inputs have settled after boot, so a float switch that is
  already up raises its alarm at start-up — which is correct.
- Relay page: mark inputs in alarm.
- **Buzzer:** optionally also sound the board's buzzer for input alarms
  (setting `i<n>_alarm_buzz`); depends on plan 17's "play pattern"
  refactor. Keep it out of the first version if that plan isn't done.
- NMEA 2000 alerts (PGN 126983 family) are a separate follow-up: plan 21.

## Open questions
- Should `emergency` be allowed for a plain input? SignalK allows it;
  keep it, default off.
- Not covered by the plan, resolved during implementation: editing an
  input's alarm setting live (portal/settings page, no restart) while the
  input is already on. Implemented so `sk_bridge_update_config()` compares
  old vs. new alarm level/message per input and, if the input currently
  reads on, republishes (or clears) its notification immediately --
  otherwise turning an alarm off while its float switch is still up would
  leave a stale "alarm" notification on the server until the next physical
  transition, and turning one on for an already-tripped switch wouldn't
  alarm until the next transition either. Not explicitly asked for; flagged
  for review. **Decided 2026-09-27: keep it.** Reviewed and confirmed —
  without it a disabled alarm could sit stale on the server for an
  arbitrarily long time on a slowly-refilling bilge.

## Test Strategy
Host tests in `sk_bridge_test`:
- Input on raises the notification on each enabled tree with the right
  severity and message; off clears it.
- `off` alarm setting publishes nothing.
- Not before inputs have settled; republished on reconnect and on the
  interval while active; nothing republished once cleared.
- Message falls back to the input's name; JSON is escaped.
`device_config_test`: defaults.
On the board: float switch on DI → alarm on a SignalK display.

## Implementation Steps
- [x] Settings and `input_cfg_t` fields
- [x] Notification publish/clear in `sk_bridge` (input change, settle,
      reconnect, republish)
- [x] Host tests
- [x] Relay page marker (small)
- [x] USER_MANUAL §6.4, §7.1; CHANGELOG
- [x] On-board check: float switch on a DI raises the alarm on a SignalK
      display (HARDWARE_TESTS.md C6, passed 2026-09-28)

## Files to Create/Modify
- `components/device_config/`
- `components/switch_bank/src/sk_bridge.c`, `include/sk_bridge.h`
- `components/web_ui/` (marker)
- `test/host/sk_bridge_test/`, `test/host/device_config_test/`
- `USER_MANUAL.md`, `CHANGELOG.md`

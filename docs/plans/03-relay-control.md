# Implementation Plan: 03 — Relay control

## Overview
Build `relay_ctrl`: the only code that switches relays. It drives the
TCA9554 I2C expander, runs momentary pulses, applies fail-safe policy at
boot and on SignalK loss, stores `hold` state, and tells listeners when a
relay changes.

## Relevant SPEC/ARCHITECTURE Sections
- SPEC.md §2 (domain rules), §3 (state/lifecycle), §4 (RelayChannel), §8
- ARCHITECTURE.md §2.1 (`relay_ctrl`), §6 (single gatekeeper for outputs)

## Approach

**Expander.** Relays 1–8 are TCA9554 (0x20) pins EXIO1–8 on I2C
GPIO41/42, a bus shared with the RTC. Use ESP-IDF's `i2c_master` driver
with one bus handle owned by the `board` component, so the RTC can use
the same bus later. The register-level driver sits behind a small
interface so host tests can swap in a fake.

Two TCA9554 details drive the boot sequence:
- After power-on, all pins are inputs and the output register reads
  0xFF. Switching pins to outputs before writing the output register would
  briefly turn every relay on (assuming active-high, to be confirmed on
  hardware). Always write the output register first, then the direction
  register.
- The expander has no reset pin, so it keeps its registers through an
  ESP32 reset (OTA, crash, watchdog). If the direction register already
  reads 0x00 (all outputs), this is a warm boot and the relays are still
  in their pre-reset state. Writing the target state in one write then
  leaves `hold` relays untouched, with no click.

**Boot.** Compute each relay's target state from its fail-safe policy
(`hold` → state stored in NVS, `default-safe` and all momentary relays →
off) and write it in one register write. Digital-input overrides are
applied after this, by `input_sense` (plan 04).

**Commands.** `relay_ctrl_set(channel, on, source)` is the single entry
point for SignalK, NMEA2000 and input overrides; the most recent call
wins (SPEC.md §2). It is thread-safe (commands come from several tasks)
and calls listeners outside its lock. Each write is read back from the
expander; on I2C failure it retries, then raises an `espos_health` alarm
and keeps reporting the last confirmed state.

**Momentary.** One `esp_timer` per momentary relay. "On" starts or
restarts the pulse; "off" cancels it.

**SignalK loss.** `relay_ctrl_sk_lost()` turns off `default-safe` relays;
something else decides when SignalK is lost (plan 05, based on plan 00
item 3), after the `skLossGraceS` grace period (default 30 s, SPEC.md
§9).

## Test Strategy
Host tests with a fake expander:
- Cold boot writes the output register before the direction register.
- Warm boot keeps `hold` relays on and turns `default-safe` relays off.
- Momentary pulse turns off on time, restarts on a repeated "on", and is
  cancelled by "off".
- SignalK loss affects only `default-safe` relays.
- I2C failure raises health and doesn't report a state that wasn't
  written.
On hardware (plan 07): confirm relay polarity and that an OTA reboot
doesn't click `hold` relays.

## Implementation Steps
- [ ] `board`: pin map and shared I2C bus
- [ ] TCA9554 driver behind an interface, with a host fake
- [ ] `relay_ctrl` state, boot sequence, set/get, listeners
- [ ] Momentary timers
- [ ] `hold` state persistence via plan 02's writer
- [ ] SignalK-loss hook
- [ ] Host tests

## Files to Create/Modify
- `components/board/`
- `components/relay_ctrl/` (`relay_ctrl.c/.h`, `tca9554.c/.h`)
- `test/host/test_relay_ctrl.c`, `test/host/fake_tca9554.c`

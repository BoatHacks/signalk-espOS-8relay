# Implementation Plan: 07 — Bring-up and first release

## Overview
Run the whole firmware on the real board, on a real NMEA2000 bus and
SignalK server, check every behaviour in SPEC.md's MVP list, and publish
a first signed release.

## Relevant SPEC/ARCHITECTURE Sections
- SPEC.md §10.1 (MVP features), §2, §3
- ARCHITECTURE.md §8 (deployment)

## Approach
Work through the checklist below with the board wired to test loads
(lamps are fine; don't test on boat systems first). Anything that fails
goes back to the plan that owns it. Record hardware facts learned here
(relay polarity, input polarity, debounce result) in ARCHITECTURE.md.

## Test Strategy

The step-by-step procedure, with commands and pass criteria, is
[docs/HARDWARE_TESTS.md](../HARDWARE_TESTS.md). The list below is the
summary; tick items here as the procedure passes them.

**Hardware basics**
- [ ] Relay polarity: which expander level turns a relay on
- [ ] Cold power-up: no relay clicks on before the firmware sets it
- [ ] OTA reboot: `hold` relays stay on without clicking; `default-safe`
      relays turn off
- [ ] Input polarity; 50 ms debounce against a real float switch
- [ ] Changing an input's invert, or linking a relay to an input, in the
      web page doesn't switch the relay

- [ ] Status LED: colour order right (green shows green), brightness
      setting works, blue until SignalK connects
- [ ] Buzzer: tone audible; with "Buzzer on alarm" on, pulling the relay
      chip's I²C (or another way to raise an alarm) beeps "ESP" + octet;
      with no network address it beeps "ESP AP"

**Relays and inputs**
- [ ] Momentary relay turns off after its pulse time
- [ ] Input override: turns its relay on/off; a later SignalK or N2K
      command stands until the input changes; override applies at boot
- [ ] Losing the SignalK server turns off `default-safe` relays after the
      grace period; `hold` relays stay

**SignalK**
- [ ] Toggle relays from an instrument panel on
      `electrical.switches.bank.*`
- [ ] With `controls.*` enabled: toggle via either tree, both show the
      same state; input paths reject PUT
- [ ] Both trees off: nothing published, N2K control still works
- [ ] Names show as display names; renaming updates them

**NMEA2000**
- [ ] An MFD lists the device with the right product info
- [ ] Both banks show on the MFD/keypad; keypad switches relays
- [ ] Two devices with conflicting addresses: address claim resolves it

**Network**
- [ ] Ethernet used when plugged in; unplugging falls back to WiFi;
      replugging returns to Ethernet
- [ ] Captive-portal provisioning on a fresh board

**Release**
- [x] Production signing key created (fingerprint `4b1b12689b58f2f3`),
      kept in the private BoatHacks/laserbrain repo
- [x] Key added as the repository secret `SIGNING_KEY_PEM`
- [ ] Publish a GitHub release: Actions → "Cut release" (`cut-release.yml`)
      tags main and calls `release-firmware.yml`, which builds, signs,
      merges and attaches `-merged.bin` (USB) and `-ota.bin` (OTA)
- [ ] Published where `signalk-espos-manager` can find and install it

## Implementation Steps
- [ ] Wire the board to test loads, a CAN bus and a SignalK server
- [ ] Run the checklist; fix failures in their owning plan
- [ ] Update ARCHITECTURE.md with hardware facts and SPEC.md with any
      behaviour that changed
- [ ] Cut the first release

## Files to Create/Modify
- `ARCHITECTURE.md`, `SPEC.md` (findings)
- `CHANGELOG.md`
- Release workflow under `.github/workflows/`

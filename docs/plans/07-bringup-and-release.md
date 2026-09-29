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
- [x] Relay polarity: which expander level turns a relay on — active
      high, on = coil energized, no inversion needed (2026-09-26)
- [x] Cold power-up: no relay clicks on before the firmware sets it —
      confirmed via N2K-bus power-cycle: single clean click (the hold
      relay), no glitch-then-settle (2026-09-26)
- [x] OTA reboot: `hold` relays stay on without clicking; `default-safe`
      relays turn off (2026-09-26)
- [x] Input polarity; 50 ms debounce against a real float switch — both
      confirmed; 9 flip cycles (fast and slow) each produced exactly one
      clean transition, down to ~70 ms between edges (2026-09-26)
- [x] Changing an input's invert, or linking a relay to an input, in the
      web page doesn't switch the relay — exercised repeatedly via
      `setcfg` across the C-series tests with no unexpected relay
      switching on a bare config change (2026-09-26)

- [x] Status LED: colour order right (green shows green), brightness
      setting works, blue until SignalK connects — confirmed by the
      operator from prior observation, green when connected and blue
      within ~30 s of the server stopping (2026-09-28)
- [x] Buzzer: tone audible (2700 Hz default, 1000/5000 Hz both audible
      with a pitch change, applied live) (2026-09-26). "Buzzer on
      alarm" still not exercised, but no longer blocked: espOS 0.12's
      health drill endpoint (espOS#137, added in espOS#149) raises a
      synthetic alarm condition remotely, and HARDWARE_TESTS G4 now runs
      through it

**Relays and inputs**
- [x] Momentary relay turns off after its pulse time — 1510 ms measured
      vs. 1500 ms configured (2026-09-26)
- [x] Input override: turns its relay on/off; a later SignalK or N2K
      command stands until the input changes; override applies at boot —
      all three confirmed (toggle, follow, and boot-safety with the
      "button" held through a restart) (2026-09-26)
- [x] Losing the SignalK server turns off `default-safe` relays after the
      grace period; `hold` relays stay — 30.81 s vs. ~30 s configured
      (2026-09-26)

**SignalK**
- [x] Toggle relays from an instrument panel on
      `electrical.switches.bank.*` — PUT via the SignalK server switched
      the relay both ways; attributed to `by nmea2000` rather than
      `by signalk` in the log because this test network has a real
      N2K↔SK bridge device the multi-source PUT routed through, but the
      control path worked end-to-end (2026-09-26)
- [ ] With `controls.*` enabled: toggle via either tree, both show the
      same state; input paths reject PUT
- [ ] Both trees off: nothing published, N2K control still works
- [ ] Names show as display names; renaming updates them

**NMEA2000**
- [ ] An MFD lists the device with the right product info
- [ ] Both banks show on the MFD/keypad; keypad switches relays
- [x] Two devices with conflicting addresses: address claim resolves it —
      a competing ISO Address Claim (NAME=0) for our address made the
      board move to the next free one immediately, stayed stable and
      fully functional (2026-09-26)

**Network**
- [ ] Ethernet used when plugged in; unplugging falls back to WiFi;
      replugging returns to Ethernet — not tested, Ethernet port not
      wired yet
- [ ] Captive-portal provisioning on a fresh board

**Release**
- [x] Production signing key created (fingerprint `4b1b12689b58f2f3`),
      kept in the private BoatHacks/laserbrain repo
- [x] Key added as the repository secret `SIGNING_KEY_PEM`
- [x] Publish a GitHub release: Actions → "Cut release" (`cut-release.yml`)
      tags main and calls `release-firmware.yml`, which builds, signs,
      merges and attaches `-merged.bin` (USB) and `-ota.bin` (OTA) —
      v0.1.0 (2026-09-26) through v0.3.0 (2026-09-28) all cut this way
- [x] Published where `signalk-espos-manager` can find and install it —
      `release-firmware.yml`'s `manifest` job adds each build to
      `manifest.json` on the `ota` branch; a board offered and took an
      update from it (HARDWARE_TESTS H1)
- [x] OTA update from the release manifest (HARDWARE_TESTS H1): a board
      on v0.0.6, pointed at `manifest.json` on the `beta` channel, was
      offered v0.0.7 and updated without problems (2026-09-25)
- [x] PSRAM: the chip is an ESP32-S3R8 (8 MB octal PSRAM, 3.3 V); with it
      enabled (0.0.9) the boot log shows `Found 8MB PSRAM device`, `SPI
      SRAM memory test OK`, and the settings page loads again on 0.0.10
      (2026-09-25)
- [x] NMEA 2000 opens at boot on 0.0.10 (`n2k: on the bus: relay bank 0,
      input bank 1` at 1.7 s); before, the library's same-millisecond open
      could make the firmware give up (2026-09-25)

**Findings from the 2026-09-26 bring-up session** (full detail in
[docs/HARDWARE_TESTS.md](../HARDWARE_TESTS.md)'s results table)
- A stale core dump from the previously-installed v0.0.10 release was
  decoded: a race between the `n2k` task's `TwaiTransmitter::set()` and
  the TWAI transmit-from-ISR path over a shared frame buffer. Not present
  in the dirty build tested this session; worth a fix in espos_n2k
  upstream if it recurs.
- espOS's WiFi station regularly retries a few times with `AUTH_EXPIRE`
  before connecting on boot or reconnect, doing a full channel rescan
  each attempt. Diagnosed (ruled out password, AP config, host CPU load)
  and filed upstream as
  [espOS#136](https://github.com/signalk-espOS/espOS/issues/136), with a
  proposed mitigation (pin BSSID/channel for the first few attempts).
  This is also why B6 and D4's reconnect timing ran longer than the test
  doc's nominal figures.
- The board's three-terminal (NO/COM/NC) relays have no way to tell the
  firmware which contact a load is wired to, which matters for fail-safe
  correctness on a load deliberately wired to NC. Filed as
  [signalk-espOS-8relay#13](https://github.com/BoatHacks/signalk-espOS-8relay/issues/13).
- `espos_health` has no way to inject a test alarm condition remotely
  (needed for HARDWARE_TESTS G4); filed as
  [espOS#137](https://github.com/signalk-espOS/espOS/issues/137).
- Fixed a doc bug in HARDWARE_TESTS.md B4 (`setcfg` targeted
  `relay2_mode` instead of `relay3_mode`, which would have made the test
  meaningless).

## Implementation Steps
- [x] Wire the board to test loads, a CAN bus and a SignalK server
- [x] Run the checklist; fix failures in their owning plan — everything
      except the items still listed as open above
- [x] Update ARCHITECTURE.md with hardware facts and SPEC.md with any
      behaviour that changed (relay/input polarity, debounce)
- [x] Cut the first release (v0.1.0, 2026-09-26)

**Still open before 1.0.0**
- Ethernet with a cable plugged in (F1), and WiFi fallback / return to
  Ethernet — the port isn't wired on the bench yet
- Captive-portal provisioning on a fresh board (F2)
- An MFD listing the device and switching from a keypad (E3) — no MFD on
  the bench
- `controls.*` tree: switching through either tree, input paths rejecting
  PUT, and display names after renaming
- Alarm buzzer on a real alarm condition (G4), now runnable through the
  health drill endpoint
- Schedules on hardware (B11): the RTC keeping time across a power cut,
  and a live sunrise/sunset transition (issue #9)

## Files to Create/Modify
- `ARCHITECTURE.md`, `SPEC.md` (findings)
- `CHANGELOG.md`
- Release workflow under `.github/workflows/`

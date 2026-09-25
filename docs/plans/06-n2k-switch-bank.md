# Implementation Plan: 06 — NMEA2000 switch bank

## Overview
The NMEA2000 half of `switch_bank`: join the bus as a proper device,
broadcast relay and input state with PGN 127501, and accept relay
commands with PGN 127502.

## Relevant SPEC/ARCHITECTURE Sections
- SPEC.md §6.2 (PGNs), §2 (single source of truth, last write wins)
- ARCHITECTURE.md §2.3 (`switch_bank`), §5 (NMEA2000 integration)

## Approach

**What's missing.** `espos_n2k` only sends and receives raw CAN frames.
A well-behaved N2K device also needs address claim (with conflict
handling), answers to ISO requests, product information (a multi-frame
"fast packet" message), a heartbeat and a PGN list. SPEC.md §6.2 only
names 127501/127502 and address claim; the rest is what MFDs expect.

**Choice: use the NMEA2000 library, not a hand-written stack
(recommended).** Timo Lappalainen's MIT-licensed NMEA2000 C++ library
already implements address claim, ISO requests, product info, fast
packets, heartbeat and PGN lists, and is widely used on ESP32. It talks to
hardware through a small driver class; write one that sends through
`espos_n2k::TwaiTransmitter` and receives from
`TwaiReceiver::set_on_frame` via a queue. espOS keeps owning the CAN
peripheral, so its candump server keeps working for debugging. The
alternative, implementing these protocols ourselves, is more code to get
right for no benefit. If plan 00 item 6 finds `espos_n2k` can't be shared
this way, the library's own ESP32 TWAI driver is the fallback.

**Device identity.** Manufacturer code, device class and device function
go in the N2K NAME. Look up the right values in canboat's PGN database
rather than guessing (an electrical-distribution / load-controller class
is the likely fit), and use a manufacturer code meant for
non-certified devices. Derive the unique number from the MAC address.
Store the last claimed source address in NVS so the device returns to it.

**Switch-bank PGNs** (single-frame, 2 bits per channel: 0 off, 1 on, 3
unavailable/no change):
- 127501 for the relay bank (`bankId`) and the input bank
  (`inputBankId`), sent on every change and periodically. Take the
  periodic interval from the PGN definition, not a guess.
- 127502: act only on the relay bank instance; for each channel whose
  field is 0 or 1, call `relay_ctrl_set(…, SOURCE_N2K)`; ignore 3.
  Ignore 127502 addressed to the input bank.
- Both N2K messages are sent regardless of the SignalK tree toggles.

## Test Strategy
- Host tests: 127501 encode and 127502 decode, including "no change"
  fields, channels above 8, and the wrong instance.
- On the bench: a USB-CAN adapter (or the device's own candump server)
  piped into canboat's `analyzer` to check address claim, product info,
  heartbeat and 127501 decode correctly; send 127502 frames and watch the
  relays.
- In plan 07: an MFD lists the device and shows both banks.

## Implementation Steps
- [ ] Add the NMEA2000 library as an ESP-IDF component (pinned version)
- [ ] Driver class over `espos_n2k` receive/transmit
- [ ] NAME, product info and address persistence
- [ ] 127501 encode + send on change and periodically
- [ ] 127502 decode + dispatch to `relay_ctrl`
- [ ] Host tests for the bitfield encoding
- [ ] Bench test with canboat `analyzer`
- [ ] Update SPEC.md §6.2 with the full PGN list and the chosen NAME
      values

## Files to Create/Modify
- `components/switch_bank/` (`n2k_bridge.cpp/.h`,
  `n2k_espos_driver.cpp/.h`, `switch_bank_pgn.c/.h`)
- `main/idf_component.yml` or `components/` (NMEA2000 library)
- `test/host/test_switch_bank_pgn.c`
- `SPEC.md` §6.2

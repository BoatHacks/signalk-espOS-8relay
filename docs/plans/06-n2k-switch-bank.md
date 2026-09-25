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
`TwaiReceiver::set_on_frame` via a queue. The alternative, implementing
these protocols ourselves, is more code to get right for no benefit.

**No candump server.** Plan 00 found the receiver has a single frame
callback, which espOS's candump server takes privately, so it can't run
alongside our code. For MVP, don't start the candump server; bench
testing uses a USB-CAN adapter instead. Worth proposing upstream: letting
`TwaiReceiver` feed several subscribers, which would bring the candump
server back as a debugging aid.

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
- On the bench: a USB-CAN adapter
  piped into canboat's `analyzer` to check address claim, product info,
  heartbeat and 127501 decode correctly; send 127502 frames and watch the
  relays.
- In plan 07: an MFD lists the device and shows both banks.

## Implementation Steps
- [x] NMEA2000 library added through the component manager straight from
      git, pinned to commit `5b7b9fc` (no releases are tagged); it
      registers itself as an ESP-IDF component and uses `esp_timer`
- [x] Driver class over `espos_n2k` receive/transmit (candump server not
      started, as plan 00 found)
- [x] NAME from canboat (class 30, function 140), manufacturer code 2046,
      unique number and serial from the MAC; product info; address kept
      in NVS
- [x] 127501 encode (plain C, `switch_bank_pgn.c`), sent on change and
      every 2 s (canboat has no interval; common practice)
- [x] 127502 decode and dispatch to `relay_ctrl` with `RELAY_SRC_N2K`
- [x] Host tests for the payloads (`test/host/switch_bank_pgn_test`,
      5 tests, including a round-trip over all 256 relay states)
- [ ] Bench test: USB-CAN adapter and canboat `analyzer` (address claim,
      product info, heartbeat, 127501; 127502 switching relays)
- [x] SPEC.md §6.2 updated with the full PGN list and NAME values

## Files to Create/Modify
- `components/switch_bank/` (`n2k_bridge.cpp/.h` with the driver class
  inside, `switch_bank_pgn.c/.h`, `idf_component.yml` for the library)
- `main/main.c`
- `test/host/switch_bank_pgn_test/`
- `SPEC.md` §6.2

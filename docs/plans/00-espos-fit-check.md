# Implementation Plan: 00 — espOS fit check

## Overview
Answer the open questions about espOS before building on it. Several
assumptions in ARCHITECTURE.md turned out to be wrong when checked (see
[README](README.md#facts-established-while-writing-these-plans)); the
remaining ones below would each change later plans. This is a throwaway
spike: the output is answers written into ARCHITECTURE.md, not code
kept in the repo.

## Relevant SPEC/ARCHITECTURE Sections
- ARCHITECTURE.md §2.5 (espOS components consumed), §4 (stack), §5
  (integration points)
- SPEC.md §3.2 (SignalK-disconnect fail-safe), §6.3 (config validation)

## Approach
Build espOS's `minimal` example for `esp32s3`, flash it to the board, and
read espOS headers for each question. Answer each item as yes / no /
workaround, with the file or header that proves it.

| # | Question | If the answer is no |
|---|---|---|
| 1 | Does espOS build and boot on `esp32s3` (examples only target c6/p4)? | Blocker. Stop and discuss. |
| 2 | Does `espos_eth` drive a W5500 over SPI on the S3? Which Kconfig/API sets the SPI pins? | Write our own W5500 transport with ESP-IDF's `esp_eth` W5500 driver, registered with `espos_net` if it allows external transports; otherwise WiFi-only for MVP and ask whether that's acceptable. |
| 3 | Is there a getter or event for SignalK connection state (e.g. token/stream status in `espos_sk.h`)? | Poll `GET /api/v1/sk/status` logic internally, or treat "no delta sent/acked for N s" as disconnected. |
| 4 | Can an app validate a config save before it's stored (to reject `inputBankId == bankId`)? | Accept the save, then raise an `espos_health` warning and stop publishing the input bank until fixed. SPEC.md §6.3 must be updated to match. |
| 5 | What types does a config descriptor support (enum? string length?) and do per-key `restart_required` flags work? | Model enums as ints with documented values. |
| 6 | Can `espos_n2k`'s `TwaiReceiver`/`TwaiTransmitter` be used from app code while also running the candump server, and what are the TX queue semantics? | Use the TWAI driver directly and skip `espos_n2k`. |
| 7 | Does anything in espOS already use GPIO41/42 (I2C), GPIO38 (LED) or GPIO46 (buzzer) by default? | Reconfigure espOS's pins in `sdkconfig.defaults`. |
| 8 | What flash size / PSRAM does this board's module have, and does espOS's reference partition table fit? | Adapt the partition table in plan 01. |
| 9 | Latest espOS version on the component registry; pin it exactly. | — |
| 10 | espOS always prefers Ethernet over WiFi. Can an app force WiFi-only or Ethernet-only, as SPEC.md §9's `network` setting offers? | Drop those two options from SPEC.md §9 and keep only "Ethernet preferred, WiFi fallback". |

## Test Strategy
Manual: build, flash, check the serial log, open the web UI, confirm a
SignalK server discovers the device. For item 2, plug in Ethernet and
check it gets an address.

## Implementation Steps
- [ ] Install the ESP-IDF version named in espOS's `.idf-version`
- [ ] Build and flash espOS `minimal` for `esp32s3` (item 1)
- [ ] Work through items 2–9, noting the evidence for each
- [ ] Update ARCHITECTURE.md with the answers and remove the "unclear"
      notes from `docs/plans/README.md`
- [ ] If item 1 or 2 fails, stop and discuss before plan 01

## Files to Create/Modify
- `ARCHITECTURE.md`
- `docs/plans/README.md`
- Later plans, where an answer changes their approach

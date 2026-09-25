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

## Results (2026-09-25, espOS 0.10.3, ESP-IDF v6.0.3)

| # | Answer | Evidence |
|---|---|---|
| 1 | **Builds: yes.** Boot not yet checked (needs the board). espOS's `from_registry` example, with registry packages 0.10.3 and the 16 MB partition table, builds and signs for `esp32s3`: 580 KB app, 91% of the 6.5 MB slot free. | Local build |
| 2 | **No W5500 support.** `espos_eth` only drives the internal EMAC and returns `ESP_ERR_NOT_SUPPORTED` on the S3; the README's "W5500 over SPI" is wrong. **Workaround:** our own transport using ESP-IDF 6's `espressif/w5500` registry driver (2.0.0; moved out of ESP-IDF in 6.0), reporting into espOS with the public `espos_net_register_if()` / `espos_net_report()` API, as `espos_eth` itself does. **Decided:** build this (done in plan 01). | `espos_eth.h`, `espos_eth.c`, `espos_net.h` |
| 3 | **Yes.** Events `ESPOS_EVENT_SK_STREAM_CONNECTED` / `_DISCONNECTED` on the espOS event bus, and `espos_sk_ws_get_status()` (`.connected`). | `espos_event.h`, `espos_sk.h` |
| 4 | **No.** Writes are validated per key only (type, range, length, enum); there is no hook to veto a save. Fallback applied: a health warning, and the input bank isn't published until fixed. | `espos_config.h` |
| 5 | Types: bool, int, float, string (with optional enum values), blob. Keys can be grouped into UI tabs; `restart_required` and read-only flags exist. Key names are at most 15 characters, so e.g. `relay1_failsafe`. | `espos_config_desc.h` |
| 6 | **Not both.** The receiver has one frame callback, and the candump server claims it (privately). Our NMEA 2000 code and the candump server can't both receive. Options in plan 06. `TwaiTransmitter::set()` never blocks and drops frames when its queue (default 32) is full. | `twai_receiver.h`, `candump_tcp_server.cpp` |
| 7 | **No conflicts.** The espOS runtime claims no GPIO or I2C bus; only the optional `espos_sensors` does, when used. It has a shared-I2C-bus helper (`espos_i2c_bus`) worth reusing for the relay expander and RTC. | Source grep |
| 8 | 16 MB flash, 8 MB PSRAM. espOS's `16mb.csv` fits. PSRAM type (quad/octal) unconfirmed, so leave PSRAM off until checked on hardware. | Waveshare product page |
| 9 | **0.10.3** (latest on the registry). Pin all `signalk-espos/*` packages to exactly this. | Registry API |
| 10 | **Yes, without our own setting for WiFi.** espOS has `wifi.sta_enabled` (off keeps the setup access point available). With our own Ethernet transport, an "Ethernet enabled" setting covers the rest. | `espos_wifi/config/wifi.json` |

Also found:
- espOS allows at most **16 PUT handlers** (`ESPOS_SK_MAX_PUT_HANDLERS`, a
  fixed define), matched by exact path. 8 relays × 2 trees uses all 16.
- SignalK paths are limited to **96 characters** (`ESPOS_SK_PATH_MAX`);
  our longest is about 64.

## Implementation Steps
- [x] Install the ESP-IDF version named in espOS's `.idf-version`
- [x] Build espOS `minimal` for `esp32s3` (item 1, build half)
- [ ] Flash and boot it on the board (item 1, hardware half)
- [x] Work through items 2–10, noting the evidence for each
- [x] Update ARCHITECTURE.md with the answers and remove the "unclear"
      notes from `docs/plans/README.md`
- [x] Decide on the W5500 workaround (item 2): our own driver, built in plan 01

## Files to Create/Modify
- `ARCHITECTURE.md`
- `docs/plans/README.md`
- Later plans, where an answer changes their approach

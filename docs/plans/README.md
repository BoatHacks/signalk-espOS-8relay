# Implementation plans

One plan per stage, in the order they should be built. Each plan links to
the SPEC.md / ARCHITECTURE.md sections it implements. Work through
[IMPLEMENTATION_CHECKLIST.md](../../IMPLEMENTATION_CHECKLIST.md) for each
stage.

| # | Plan | Depends on | Hardware needed |
|---|---|---|---|
| 00 | [espOS fit check](00-espos-fit-check.md) | — | Board (for the Ethernet and flash checks) |
| 01 | [Project scaffold](01-project-scaffold.md) | 00 | No |
| 02 | [Device config](02-device-config.md) | 01 | No |
| 03 | [Relay control](03-relay-control.md) | 02 | Yes, for the final check |
| 04 | [Digital inputs](04-digital-inputs.md) | 02, 03 | Yes, for the final check |
| 05 | [SignalK bridge](05-signalk-bridge.md) | 03, 04 | SignalK server |
| 06 | [NMEA2000 switch bank](06-n2k-switch-bank.md) | 03, 04 | CAN adapter or N2K bus |
| 07 | [Bring-up and first release](07-bringup-and-release.md) | 05, 06 | Board, N2K bus, SignalK server |

05 and 06 are independent of each other and can be built in either order.

## Facts established while writing these plans

Checked against the espOS repository and the Waveshare wiki on
2026-09-25. ARCHITECTURE.md has been corrected to match.

- **Relays are not on GPIOs.** They are driven by a TCA9554 I2C expander
  at address 0x20 (EXIO1–8 = relay 1–8), on I2C SCL GPIO41 / SDA GPIO42.
  The board's PCF85063 RTC shares that bus.
- **Digital inputs** are GPIO4–GPIO11 (DI1–DI8).
- **CAN** is TX GPIO17, RX GPIO18. **W5500 Ethernet** is on SPI: INT
  GPIO12, MOSI GPIO13, MISO GPIO14, SCLK GPIO15, CS GPIO16. RGB LED
  GPIO38, buzzer GPIO46.
- **espOS has no GPIO component.** Pins are driven with ESP-IDF drivers
  directly.
- **`espos_n2k` only moves raw CAN frames** (a TWAI receiver/transmitter
  plus a candump TCP server, in C++). It has no PGN encoding, address
  claim, product info or switch-bank support; this project has to supply
  all of that (plan 06).
- **SignalK PUT** is supported: `espos_sk_put_handler_register(path, cb,
  arg)`, and a path must be published before it can accept PUTs.
- **SignalK connection events** exist: `ESPOS_EVENT_SK_STREAM_CONNECTED`
  / `_DISCONNECTED`.
- **No W5500 support in espOS**, despite its README. This project has its
  own transport (`components/eth_w5500`), plugged into espOS's network
  layer.
- **espOS 0.10.3 builds for the ESP32-S3** from the component registry.
- **Registry packages** are per component (`signalk-espos/espos_*`),
  released in lockstep; pin all to 0.10.3.
- **At most 16 SignalK PUT handlers**, exactly what 8 relays × 2 trees
  need.

Full results: [plan 00](00-espos-fit-check.md#results-2026-09-25-espos-0103-esp-idf-v603).

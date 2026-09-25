# Implementation Plan: 01 — Project scaffold

## Overview
Create an ESP-IDF project that pulls espOS from the component registry,
boots on the board with espOS's WiFi, web UI and OTA working, and has
empty components and a host test setup ready for plans 02–06.

## Relevant SPEC/ARCHITECTURE Sections
- ARCHITECTURE.md §4 (technology stack), §7 (file structure), §8
  (deployment)
- SPEC.md §1.2 (espOS as a dependency, not a fork)

## Approach
Follow espOS's `from_registry` example rather than cloning espOS.
Dependencies are pinned to the exact version found in plan 00, since
espOS is pre-1.0. The app components are C; `switch_bank` needs a C++
file for `espos_n2k` (a C++ API), so the project enables C++.

Host tests follow espOS's `test/host` pattern (ESP-IDF `linux` target)
so relay, input and bridge logic can be tested without hardware. To make
that possible, hardware access sits behind small interfaces (I2C
expander, GPIO reads, clock) that tests replace with fakes.

## Test Strategy
- `idf.py set-target esp32s3 && idf.py build` passes.
- Host test runner builds and runs one placeholder test.
- On the board: boots, WiFi captive portal appears, web UI loads, a
  SignalK server discovers the device.

## Implementation Steps
- [x] Root `CMakeLists.txt`, `main/idf_component.yml` with espOS pinned to
      exactly 0.10.3, started from espOS's `from_registry` example
- [x] `partitions.csv` copied from espOS's `16mb.csv`
- [x] `sdkconfig.defaults`: espOS's required lines, 16 MB flash; PSRAM off
      until its type is confirmed on hardware
- [x] Development signing key, git-ignored, stored in the private
      BoatHacks/laserbrain repo; build instructions in USER_MANUAL.md §3
- [x] `main/main.c` calling `espos_start()` and starting Ethernet
- [x] `board` component with the pin map
- [x] `eth_w5500` component: W5500 over SPI (`espressif/w5500` 2.0.0),
      reported into espOS's network layer (plan 00 decision)
- [x] `test/host/run_all.sh` and a `board_test` checking the pin map for
      double use and flash/PSRAM pins
- [x] `.gitignore`, `dependencies.lock` committed
- [x] CI: firmware build for esp32s3 and host tests. espOS's reusable
      firmware workflow wasn't used: it expects espOS as a git submodule.
- [ ] On the board: boots, setup access point appears, web UI loads, a
      SignalK server discovers it, Ethernet gets an address

The other components (`relay_ctrl`, `input_sense`, `switch_bank`,
`device_config`) are created by the plans that implement them, rather than
as empty placeholders here.

## Files to Create/Modify
- `CMakeLists.txt`, `partitions.csv`, `sdkconfig.defaults`, `.gitignore`
- `main/main.c`, `main/CMakeLists.txt`, `main/idf_component.yml`
- `components/board/`, `components/eth_w5500/`
- `test/host/`
- `.github/workflows/build.yml`, `dependencies.lock`
- `README.md`, `USER_MANUAL.md`

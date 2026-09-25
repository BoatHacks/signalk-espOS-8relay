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
- [ ] Root `CMakeLists.txt`, `main/idf_component.yml` with pinned espOS
      packages (`espos_sk`, `espos_n2k`, `espos_config`, `espos_httpd`,
      `espos_ota`, `espos_health`, `espos_eth` if plan 00 confirmed W5500)
- [ ] `partitions.csv` from espOS's reference table, sized for this
      board's flash (plan 00 item 8)
- [ ] `sdkconfig.defaults`: target esp32s3, flash size, C++ enabled, pins
      from plan 00 item 7
- [ ] Development signing key, kept out of git (`.gitignore`), with
      README instructions to generate one
- [ ] `main/app_main.c` calling `espos_start(NULL)`
- [ ] Empty components: `relay_ctrl`, `input_sense`, `switch_bank`,
      `device_config`, plus a `board` component holding the pin map
- [ ] `test/host/` runner with one placeholder test
- [ ] `.gitignore` for `build/`, `managed_components/`, `sdkconfig`, keys
- [ ] CI workflow that builds the firmware and runs host tests (espOS
      provides a reusable firmware workflow; use it if it fits)
- [ ] USER_MANUAL.md §3: how to build, flash and update; update README.md status

## Files to Create/Modify
- `CMakeLists.txt`, `partitions.csv`, `sdkconfig.defaults`, `.gitignore`
- `main/app_main.c`, `main/CMakeLists.txt`, `main/idf_component.yml`
- `components/{board,relay_ctrl,input_sense,switch_bank,device_config}/`
- `test/host/`
- `.github/workflows/build.yml`
- `README.md`, `USER_MANUAL.md`

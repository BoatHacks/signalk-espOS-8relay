# Implementation Plan: 02 — Device config

## Overview
Declare every user setting from SPEC.md §9 as an espOS config namespace,
so it appears in espOS's web UI and is stored in NVS, and give the other
components typed, validated access to it.

## Relevant SPEC/ARCHITECTURE Sections
- SPEC.md §4 (DeviceConfig, RelayChannel, DigitalInputChannel), §6.3,
  §8 (persistence), §9 (configuration)
- ARCHITECTURE.md §2.4 (`device_config`), §3 (data models)

## Approach
espOS settings are declared in a JSON descriptor added from CMake with
`espos_config_add_descriptor(config/relay.json)`, and read with typed
getters that fall back to the compiled-in default for bad values.
Descriptors are flat key lists, so per-channel settings become numbered
keys (`relay1_name`, `relay1_mode`, …, `input8_invert`).

`device_config` loads everything into the `device_config_t` struct at
boot and subscribes to changes with `espos_config_subscribe`. Other
components read the struct, never NVS directly.

Cross-field rules the descriptor can't express are checked in code:
- `inputBankId` must differ from `bankId`. A bad save can't be rejected
  (plan 00 item 4), so the device raises an
  `espos_health` warning and the input bank isn't published until fixed
  (SPEC.md §6.3).
- A `momentary` relay is always treated as `default-safe`, whatever its
  stored fail-safe value (SPEC.md §2).
- `overrideDI` must be 0 (none) or 1–8.

Changing a bank id or a tree toggle changes which SignalK paths and PUT
handlers exist; mark those keys `restart_required` rather than rebuilding
handlers live. Names, pulse times, fail-safe policy and debounce apply
immediately.

The last commanded state of `hold` relays is not a user setting, so it
isn't in this namespace; `relay_ctrl` stores it (plan 03).

**Built:** one namespace, `swbank` (63 keys, UI tabs General / Relays /
Inputs), in `components/device_config/config/swbank.json`.
`device_config_load()` fills a `device_config_t`; consumers who need
live changes subscribe with `espos_config_subscribe()` and reload. Two
defaults SPEC.md didn't give were chosen as the safe option: fail-safe
`default-safe`, pulse time 1000 ms.

## Test Strategy
Host tests: defaults match SPEC.md §9; each cross-field rule; a
momentary relay reports `default-safe`; out-of-range values rejected. Manual: all settings appear and save in the web UI.

## Implementation Steps
- [x] `components/device_config/config/swbank.json` descriptor
- [x] `device_config.h/.c`: load into a struct, effective fail-safe
- [x] Cross-field check: health warning (`bankIdClash`); suppressing the
      input bank happens in plans 05/06 via `device_config_input_bank_usable()`
- [x] Host tests (`test/host/device_config_test`, 6 tests)
- [ ] On the board: all settings appear and save in the web UI

## Files to Create/Modify
- `components/device_config/` (descriptor, source, header, CMakeLists)
- `test/host/device_config_test/`

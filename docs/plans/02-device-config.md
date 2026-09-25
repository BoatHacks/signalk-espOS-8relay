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

The last commanded state of `hold` relays is also stored in NVS, but not
in the settings namespace (it isn't a user setting). Writes are
coalesced (write only on change, at most once every few seconds) to
limit flash wear from frequently toggled relays.

## Test Strategy
Host tests: defaults match SPEC.md §9; each cross-field rule; a
momentary relay reports `default-safe`; change callbacks fire for the
right keys. Manual: all settings appear and save in the web UI.

## Implementation Steps
- [ ] `components/device_config/config/relay.json` descriptor
- [ ] `device_config.h/.c`: load, typed accessors, change subscription
- [ ] Cross-field checks: health warning, input bank suppressed
- [ ] Separate NVS namespace and coalesced writer for `hold` relay state
- [ ] Host tests

## Files to Create/Modify
- `components/device_config/` (descriptor, source, header, CMakeLists)
- `test/host/test_device_config.c`

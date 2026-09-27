# Contributing

Thanks for looking at this project. It's a small, boat-specific firmware
fork; this doc is short on purpose and points at the existing
documentation rather than repeating it.

## Reporting a problem

For a firmware bug, please include:

- The release version (or `git describe` if built from source).
- The boot serial log.
- `GET /api/v1/relays/status` and `GET /api/v1/n2k`.
- For a crash: the core dump (`GET /api/v1/system/coredump/raw`) plus the
  `.elf` from the matching release — releases carry it so a backtrace can
  be decoded.

## Building and testing

- ESP-IDF v6.0.3, `idf.py build`.
- Host tests: `test/host/run_all.sh`.
- Script tests: `python3 -m unittest discover -s test/scripts`.

See [USER_MANUAL.md](USER_MANUAL.md) §3.1 for the full build setup.

## Signing

Releases are signed with a private project key that isn't in this
repository. Building from source uses your own development key
(USER_MANUAL.md §3.1); a board flashed with such a build won't accept
signed release OTA updates until it's flashed back to an official one.

## How changes are made

- Each feature has a plan in [docs/plans/](docs/plans/README.md) before
  code is written; see
  [IMPLEMENTATION_CHECKLIST.md](IMPLEMENTATION_CHECKLIST.md) for how a
  stage is worked through.
- One commit (or a small series) per issue, referencing it (`Closes #n`
  when it closes one).
- A [CHANGELOG.md](CHANGELOG.md) entry in [Keep a
  Changelog](https://keepachangelog.com/en/1.1.0/) format.
- A hardware-dependent change is checked against
  [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md) before it's considered
  done, not just host-tested.

## Safety

This firmware switches real loads (SPEC.md's Safety note). Any change
that can switch a relay on its own — fail-safe, boot state, input
overrides, maximum on-time, interlocks — needs host tests covering every
source that can trigger it, and a hardware check before release. When in
doubt, a plan doc's Test Strategy section should say so explicitly.

## Scope

See [SPEC.md](SPEC.md) §10 for what's explicitly in and out of scope.

## License

By contributing, you agree your contribution is licensed under this
project's [LICENSE](LICENSE) (Apache-2.0).

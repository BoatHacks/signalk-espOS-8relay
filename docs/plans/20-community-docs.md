# Implementation Plan: 20 — CODE_OF_CONDUCT.md and CONTRIBUTING.md

Issue: [#12](https://github.com/BoatHacks/signalk-espOS-8relay/issues/12)

## Overview
Add the two standard community documents at the repository root so GitHub
shows them on the community profile and links them from new issues/PRs.
Unlike the other open issues, this has no firmware component — it's
blocked on two decisions, not on design or code.

## Relevant SPEC/ARCHITECTURE Sections
- None (documentation/repo-governance only)
- README.md "Documentation" list (needs a link added once these exist)

## Approach
- **LICENSE (blocking, decide first).** The repository currently has none
  — all-rights-reserved by default — so CONTRIBUTING.md can't state terms
  for contributions until this is chosen. espOS (this project's
  dependency) is Apache-2.0; matching it is the default recommendation
  unless there's a reason to diverge. **Decision needed from the repo
  owner**, not something to pick unilaterally.
- **Code of Conduct enforcement contact (blocking, decide first).** The
  Contributor Covenant 2.1 needs a private contact (email or similar) for
  reports — must not be a public GitHub issue. **Decision needed.**
- **CODE_OF_CONDUCT.md:** Contributor Covenant 2.1 text, unmodified
  except for the contact line above.
- **CONTRIBUTING.md**, specific to this project and short, pointing at
  existing docs rather than repeating them:
  - Reporting a firmware bug: release version, boot serial log,
    `GET /api/v1/relays/status`, `GET /api/v1/n2k`, and for a crash the
    core dump (`GET /api/v1/system/coredump/raw`) plus the matching
    release's `.elf`.
  - Building/testing: ESP-IDF v6.0.3, `idf.py build`,
    `test/host/run_all.sh`, `python3 -m unittest discover -s
    test/scripts`; pointer to USER_MANUAL §3.1.
  - Signing: releases are signed with a private project key;
    contributors build with their own development key (USER_MANUAL
    §3.1), and such a build won't take signed release OTA updates.
  - Process: plans in `docs/plans/`, `IMPLEMENTATION_CHECKLIST.md`, one
    commit per issue with `Closes #n`, CHANGELOG.md in Keep a Changelog
    format, hardware-dependent changes checked against
    `docs/HARDWARE_TESTS.md`.
  - Safety: changes that can switch relays on their own (fail-safe, boot
    state, overrides, max on-time, interlocks) need host tests covering
    every source, plus a hardware check before release.
  - Scope: SPEC.md §10 (in/out of scope).
  - License of contributions: whatever is decided above.
- Add LICENSE, CODE_OF_CONDUCT.md and CONTRIBUTING.md links to README's
  Documentation list.

## Decisions (2026-09-27)
- License: Apache-2.0, to match espOS.
- Code of Conduct enforcement contact: not decided yet. Skipping
  CODE_OF_CONDUCT.md for this pass rather than blocking LICENSE and
  CONTRIBUTING.md on it; add it once a contact is chosen.

## Test Strategy
None (no code). Verify after merging: GitHub's *Insights → Community
standards* page shows License, Code of Conduct and Contributing as
present.

## Implementation Steps
- [x] Get the license decision (Apache-2.0)
- [ ] Get the enforcement-contact decision — not yet made
- [x] Add LICENSE
- [ ] Add CODE_OF_CONDUCT.md (Contributor Covenant 2.1 + contact) —
      blocked on the contact decision above
- [x] Add CONTRIBUTING.md (project-specific, per Approach above)
- [x] README.md Documentation list links LICENSE and CONTRIBUTING.md
- [ ] Confirm GitHub's community-standards checklist is green — will
      still show Code of Conduct missing until that's added

## Files to Create/Modify
- `LICENSE` (new)
- `CODE_OF_CONDUCT.md` (new)
- `CONTRIBUTING.md` (new)
- `README.md`

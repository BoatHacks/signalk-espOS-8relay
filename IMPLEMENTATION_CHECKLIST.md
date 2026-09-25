# Implementation Checklist

Work through this for each stage in [docs/plans/](docs/plans/README.md).

## 1. Explore
- [ ] Read the stage's plan, and the SPEC.md / ARCHITECTURE.md sections
      it links to
- [ ] Check anything the plan marks "verify" or "decide" before relying
      on it — in the espOS headers, the board wiki, or by asking
- [ ] Read the existing code the stage touches

## 2. Plan
- [ ] Update the plan if exploring changed the approach
- [ ] List the test cases before writing code

## 3. Build and test
- [ ] Keep hardware access behind interfaces so logic runs in host tests
- [ ] Write host tests alongside the code; run them often
- [ ] If a test seems wrong, fix it deliberately — don't loosen it to
      get to green

## 4. Verify
- [ ] Firmware builds for esp32s3 and host tests pass
- [ ] Edge cases covered, not just the happy path
- [ ] Behaviour matches SPEC.md; structure follows ARCHITECTURE.md
- [ ] Anything only checkable on hardware is added to plan 07's checklist

## 5. Document and commit
- [ ] Update SPEC.md / ARCHITECTURE.md if the stage changed what they
      describe
- [ ] Tick off the plan's steps
- [ ] Commit with a message that explains why

## Things that are easy to get wrong here
- Only `relay_ctrl` switches relays. Nothing else writes to the expander.
- A SignalK path must be published before its PUT handler is registered.
- `controls.*` identifiers are derived from bank id + channel, never
  stored.
- Relays switch real loads. Test on lamps, not boat systems.

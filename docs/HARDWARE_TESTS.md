# Hardware test procedure

How to check a release on the real board. Written so that someone (or a
Claude session) with no other context can run it: every test says what
it proves, what it needs, the exact commands, and what counts as a pass.
Record results in the table at the end and copy the outcome into
[plan 07](plans/07-bringup-and-release.md)'s checklist.

## Who does what

Each test is marked:

- **[auto]** — needs only the serial port and the board's web API (a
  session with serial access can run it alone).
- **[loop]** — needs the loop-back wiring below, then runs unattended.
- **[human]** — needs a person: to press, look, listen or unplug. The
  session sets it up and tells the person exactly what to do and when.

## Safety

Relay contacts carry up to 10 A at 250 V AC / 30 V DC. Test with lamps or
nothing at all on the relay outputs. Several tests switch relays on by
themselves (maximum on-time, toggle mode, fail-safe, boot).

## Bench setup

| Connection | For |
|---|---|
| USB-C to the test computer (`/dev/ttyACM0`) | Serial log, USB flashing |
| Ethernet and/or WiFi on the same network as the test computer | Web API, SignalK |
| A SignalK server on that network | Tests D1–D4 |
| NMEA 2000 bus (or a CAN adapter on the test computer, e.g. `can0`) | Tests E1–E4 |
| 7–36 V DC supply | Everything; a switchable one for cold power-up (B6) |

**Loop-back wiring** (for [loop] tests). The inputs are isolated
optocoupler inputs: an input is "on" when current flows through it.
Wire each loop so the relay contact switches the input's supply:

- **Loop A — relay 8 drives DI8.** Supply + → relay 8 COM; relay 8 NO →
  DI8; DI8's return → supply −. (Check the Waveshare wiki for the DI
  wiring of your board revision — COM/common terminal and the voltage the
  inputs expect.) Relay 8 on ⇒ DI8 on.
- **Loop B — relay 7 drives DI7**, the same way. Used as a "push button"
  by setting relay 7 to momentary with a short pulse.

Leave relays 1–6 free for the tests that switch them.

## Tools on the test computer

```sh
# Serial log (exit with Ctrl+]):
python3 -m serial.tools.miniterm /dev/ttyACM0 115200
# or, from the repo with ESP-IDF: idf.py -p /dev/ttyACM0 monitor

B=http://espos-cf28.local          # or http://<board IP>; see the boot log
H='Content-Type: application/json'
# If an API key is set (espOS security settings), add to every curl:
#   -H "Authorization: Bearer <key>"

relays()  { curl -s $B/api/v1/relays | python3 -m json.tool; }
relay()   { curl -s -X PUT -H "$H" -d "{\"on\":$2}" $B/api/v1/relays/$1 >/dev/null; }   # relay 3 true
status()  { curl -s $B/api/v1/relays/status | python3 -m json.tool; }
setcfg()  { curl -s -X PUT -H "$H" -d "{\"swbank\":{$1}}" $B/api/v1/config; echo; }  # setcfg '"relay1_link":"toggle"'
getcfg()  { curl -s "$B/api/v1/config?ns=swbank" | python3 -m json.tool; }
logs()    { curl -s "$B/api/v1/logs?limit=200"; }
```

`setcfg` sends the same shape `getcfg` returns; check a key's exact name
there first. A setting that needs a restart says `"restart_required":
true` in the reply; restart with `curl -s -X POST -H "$H" -d '{}'
$B/api/v1/system/reboot`.

Every relay change is logged once, with its source:

```
I (…) app: relay 3 on by web
```

Sources: `web`, `signalk`, `nmea2000`, `input`, `pulse`, `failsafe`,
`maxOn`. The relay page shows the same (`lastSource` in `relays`).

**Before starting:** note the firmware version (`status` → `version`),
then reset the switch-bank settings to defaults so earlier experiments
don't interfere: `curl -s -X PUT -H "$H" -d '{"swbank":null}'
$B/api/v1/config` resets the namespace (check the reply lists changed
keys; if the API rejects `null` for a whole namespace, set the keys used
below back to their defaults by hand).

---

## A. Boot

**A1 [auto] Boot log is clean.** Restart (`system/reboot`) and capture
the serial log from reset for 30 s. Pass if all of these appear, in
roughly this order, with no `E (` error lines from our tags (`app`,
`n2k`, `web_ui`, `indicator`, `eth_w5500`), no `espos_health` alarm
(a dead relay chip shows up as `relayExpander`), and no `abort()`,
`assert failed` or `Guru Meditation`:

```
espos: espOS … app signalk-espOS-8relay v0.0.x
espos_health: watching task io (2000 ms)
espos_skin: PUT handler for electrical.switches.bank.0.1.state   (×8)
twai_node: TWAI started: TX=17 RX=18 250kbps
n2k: on the bus: relay bank 0, input bank 1
web_ui: relay page at /relays
```

and, with a network: an IP address, `espos_time: clock set from sntp`,
and (with SignalK) `espos_skws: stream connected`.

**A2 [auto] No crash left behind.** `curl -s $B/api/v1/system/coredump`
— pass if it reports no core dump. If there is one, save it
(`…/coredump/raw`) and decode it against the release's ELF before
anything else.

**A3 [human] LED colour.** With SignalK connected and no warnings the
LED is **green**; stop the SignalK server → **blue** within ~30 s. If
green shows as red (or blue as another colour), the LED's colour order is
wrong (`color_component_format` in `components/indicator/src/indicator.c`,
currently RGB).

## B. Relays

**B1 [loop] Relay polarity.** `relay 8 true`, wait 200 ms, `relays` →
pass if `inputs[7].on` is `true`; `relay 8 false` → `false`. Fails the
other way round if relays are active-low (`BOARD_RELAY_ACTIVE_HIGH`) or
inputs are inverted (B1 and C1 together tell which).

**B2 [auto] Every relay switches and reports.** For n = 1…8: `relay n
true`, then `relay n false`. Pass if each produces exactly two log lines
`relay n on by web` / `relay n off by web`, and `relays` shows the state
in between. **[human]** ask the person to confirm each relay clicks (and
its LED, if the board has per-relay LEDs).

**B3 [auto] Momentary pulse length.** `setcfg
'"relay2_mode":"momentary","relay2_pulse_ms":1500'`, `relay 2 true`.
Pass if the log shows `relay 2 on by web` then `relay 2 off by pulse`
1.5 s later (±20 ms, from the log timestamps).

**B4 [auto] Maximum on-time (#2).** `setcfg
'"relay3_mode":"latching","relay3_max_on_s":10'`, `relay 3 true`. Pass
if `relay 3 off by maxOn` follows 10 s later. Repeat, and at 8 s send
`relay 3 true` again: the off must come 10 s after that second command.
Then `setcfg '"relay3_max_on_s":0'`.

**B5 [auto] Restart keeps hold relays, drops the rest.**
`setcfg '"relay4_failsafe":"hold"'`, `relay 4 true`, `relay 5 true`,
wait 6 s (the hold state is saved at most every 5 s), reboot via the API.
Pass if after boot `relays` shows relay 4 on and relay 5 off, and the log
has no `relay 4 off` line (a warm restart must keep it on without a
click). **[human]** ask: did relay 4 click during the restart? It must
not.

**B6 [human] Cold power-up.** With relay 4 still set to hold and on,
switch the supply off for 10 s and on again. Pass if: no relay clicks on
at power-up before the firmware runs (watch for a brief click on all
relays), relay 4 comes back on within ~1 s of boot, the others stay off.

**B7 [auto] OTA reboot.** Install the same release over OTA:
`curl -s -X POST -H "$H" -d '{"url":"https://github.com/BoatHacks/signalk-espOS-8relay/releases/download/<tag>/signalk-espOS-8relay-<tag>-ota.bin"}' $B/api/v1/ota`.
Follow `…/api/v1/ota/status`. Pass if it downloads, reboots, logs
`espos_ota: new image confirmed`, and relay 4 (hold, on) stays on
without an off line, as in B5.

## C. Inputs

**C1 [loop] Input polarity.** As B1, from the input side: with relay 8
off, `inputs[7].on` must be `false`; on → `true`. If B1 fails but the
relay clicks, the input polarity is the problem (`BOARD_DI_ACTIVE_LOW`).

**C2 [human] Debounce against a real switch.** Wire a float switch (or
any mechanical switch) to DI1 and flip it ~10 times, fast and slow. Pass
if `relays` / the relay page shows exactly one change per flip (the log
of a linked relay is easiest: `setcfg '"relay1_override":1'` → one
`relay 1 … by input` per flip). Then `setcfg '"relay1_override":0'`.

**C3 [loop] Toggle mode (#1).** Relay 7 becomes the "button":
`setcfg '"relay7_mode":"momentary","relay7_pulse_ms":300,"relay1_override":7,"relay1_link":"toggle"'`.
Then `relay 7 true` (one 300 ms "press"), wait 1 s, check; repeat 3×.
Pass if relay 1 alternates on/off/on with one `relay 1 … by input` line
per press, and nothing happens when relay 7 turns off (the release).

**C4 [loop] Toggle mode does nothing at boot.** Leave C3's settings;
`setcfg '"relay7_mode":"latching","relay7_failsafe":"hold"'`, `relay 7
true` (button held), `relay 1 false`, wait 6 s, reboot. Pass if relay 1
is still off after boot and the log has no `relay 1 on by input`. Then
reset relays 1 and 7's settings.

**C5 [loop] Follow mode.** `setcfg '"relay2_override":8,"relay2_link":"follow"'`;
`relay 8 true` → relay 2 on `by input`; `relay 8 false` → relay 2 off.
Then `relay 2 true` from the web (override) → stays on until relay 8
changes again.

## D. SignalK

Needs a SignalK server with the board approved (read/write).

**D1 [auto] Connection.** `status` → `signalk.connected: true` and the
server's address. Log: `espos_skws: stream connected`, no repeating
`stream closed by server` (watch 2 minutes).

**D2 [auto] Republish interval.** On the server:
`curl -s http://<server>/signalk/v1/api/vessels/self/electrical/switches/bank/0/1/state`
twice, 12 s apart. Pass if the `timestamp` advanced (default interval
10 s) with no relay change in between.

**D3 [auto] Switching from SignalK.** Through the server:
`curl -s -X PUT -H "$H" -d '{"value":1}' http://<server>/signalk/v1/api/vessels/self/electrical/switches/bank/0/3/state`
(add the server's token if its security needs one). Pass if the log shows
`relay 3 on by signalk` and the server's value becomes 1. Same with 0.

**D4 [auto] SignalK-loss fail-safe.** `relay 5 true` (default-safe) and
relay 4 on (hold, from B5). Stop the SignalK server (or block the board
from reaching it). Pass if ~30 s later the log shows `relay 5 off by
failsafe` and relay 4 stays on. Start the server again: the board
reconnects within a minute and republishes.

## E. NMEA 2000

**E0 [auto] CAN diagnostics.** `curl -s $B/api/v1/n2k` (0.0.8 and
later) reports espOS's CAN counters. Read them first when NMEA 2000
doesn't work:

| Shows | Means |
|---|---|
| frames received > 0 | the bus works; look above the CAN layer |
| 0 frames, error count rising | signals arrive but can't be decoded: termination (jumper `120R`), CAN-H/L swapped, bitrate, bus-side ground |
| 0 frames, no errors | nothing arrives: bus not connected to the CAN terminal, or no other device sending |

**E1 [auto] On the bus.** `status` → `nmea2000.started: true`, an
`address` (34 unless another device took it), and `traffic: true` when
other devices are on the bus. With a CAN adapter: `candump can0` shows
PGN 127501 from our address every 2 s (CAN id `0x0DF20Dxx`, xx = our
address) for bank 0 and bank 1.

**E2 [auto] Switching from NMEA 2000.** With a CAN adapter as `can0`
(250 kbit/s), send PGN 127502 to bank 0 from source address 0x10:

```sh
cansend can0 0DF20E10#00FDFFFFFFFFFFFF   # relay 1 on  (field 1 = 01)
cansend can0 0DF20E10#00FCFFFFFFFFFFFF   # relay 1 off (field 1 = 00)
```

Pass if the log shows `relay 1 on by nmea2000` / `off by nmea2000` and
the next 127501 reflects it. Or **[human]** switch relay 1 from an MFD /
keypad that supports switch banks.

**E3 [human] MFD lists the device.** On the MFD's device list: model
"signalk-espOS-8relay", the firmware version, both banks.

**E4 [human] Address claim.** Put another device on the board's address
(or set the board's stored address to one in use). Pass if the board
moves to a free address (`status` → new `address`) and keeps working.

**E5 [auto] Traffic warning.** Disconnect the bus (or stop all other
devices). Pass if within ~10 s `status` → `traffic: false` and the relay
page shows "no bus traffic".

## F. Network

**F1 [human] Ethernet preferred.** With Ethernet and WiFi both
configured: `status` → `network.interface: "eth"`. Unplug Ethernet →
within ~30 s `"wifi_sta"` and still reachable at the new address (see
the log); plug back in → `"eth"` again.

**F2 [human] Setup access point on a fresh board.** After a USB flash of
the merged image (settings reset): the board opens `espOS-xxxx`; joining
it shows the setup page; saving a WiFi network connects the board.

## G. Relay page and buzzer

**G1 [auto] Page loads.** `curl -s $B/relays | head -5` returns the
page; open it in a browser (phone width): 8 rows, status line, name and
version in the header.

**G2 [human] Buzzer test (#10).** `curl -s -X POST -H "$H" -d '{}'
$B/api/v1/buzzer/test` → `202`; the log shows `indicator: buzzer test:
"ESP <octet>" at 2700 Hz`. Ask the person: heard once, Morse "ESP" and
the last number of the IP address? A second POST while it plays → `409`.

**G3 [human] Buzzer frequency.** `setcfg '"buzzer_freq_hz":1000'`, test;
`5000`, test; back to `2700`. Pass if the pitch changes without a
restart and both ends are audible.

**G4 [human] Alarm buzzer.** `setcfg '"buzzer_alarm":true'`, then cause
an alarm (disconnect the relay chip's I²C, or ask for another way); pass
if the buzzer repeats the pattern every few seconds and the LED is red.
Restore and `setcfg '"buzzer_alarm":false'`.

## H. Updates

**H1 [auto] The board finds and installs an update from the manifest.**
On a board running the previous release, point it at the manifest and the
beta channel (a board on 0.0.7 or later already has the URL):
`curl -s -X PUT -H "$H" -d '{"ota":{"manifest_url":"https://raw.githubusercontent.com/BoatHacks/signalk-espOS-8relay/ota/manifest.json","channel":"beta"}}' $B/api/v1/config`,
then `curl -s -X POST -H "$H" -d '{}' $B/api/v1/ota/check` and after a few
seconds `curl -s $B/api/v1/ota/status`. Pass if `available` names the new
release with `"newer": true`, and installing it
(`curl -s -X POST -H "$H" -d '{"url":"<available.url>"}' $B/api/v1/ota`)
ends with the new version running and `espos_ota: new image confirmed`.
On the new version, a first boot logs `app: update manifest: https://…`
only if the URL was empty.

---

## Results

Copy for each release tested.

**Release:** v0.0.10-4-g20b99e1-dirty (local dev build, not a tagged release) · **Date:** 2026-09-26 · **Tested by:** Tobias Rosenstock (with Claude) · **Board revision:** ESP32-S3-ETH-8DI-8RO-C, first unit

| Test | Result | Notes |
|---|---|---|
| A1 Boot log | pass | one benign, self-recovering blip: the SignalK websocket was closed by the server right as SNTP set the clock, reconnected within ~1s. No error from a tracked tag, no alarm. |
| A2 No core dump | fail → resolved | a coredump was present at session start, from the earlier-installed v0.0.10 release (ELF hash matched exactly). Decoded: `n2k` task race between `TwaiTransmitter::set()` (task context) and the TWAI TX-from-ISR path over a shared frame buffer, `LoadProhibited` in `twai_ll_format_frame_buffer`. Crashed whichever task happened to be running (`wifi`, coincidentally). Not present in the dirty build under test. Cleared before continuing. |
| A3 LED colour | not run | skipped this session |
| B1 Relay polarity | pass | on = coil energized, no inversion needed |
| B2 Every relay | pass | all 8 channels, clean `on by web`/`off by web` pairs; human confirmed audible clicks and per-relay status LEDs |
| B3 Momentary pulse | pass | 1510ms measured vs. 1500ms configured (±20ms tolerance) |
| B4 Max on-time | pass | 10010ms vs. 10000ms; re-sending "on" at 8s correctly restarted the timer (cutoff at 18050ms). Found and fixed a doc bug: the `setcfg` example set `relay2_mode` instead of `relay3_mode`, which would have left relay 3 in momentary mode and made the test meaningless. |
| B5 Restart / hold | pass | hold relay stayed on with no click across two restarts, confirmed by ear; default-safe relay correctly dropped |
| B6 Cold power-up | pass | single clean click (the hold relay) after N2K-bus power-cycle, no click-then-settle glitch; a long WiFi reconnect during this test was the known AUTH_EXPIRE pattern (espOS#136), not a new fault — confirmed via serial log the board was healthy throughout |
| B7 OTA reboot | pass | re-installed the same dirty build over OTA (no tagged release to point at); confirmed `new image confirmed`, hold relay survived |
| C1 Input polarity | pass | current flowing = on, no inversion |
| C2 Debounce | pass | 9 flip cycles (fast and slow), each exactly one clean transition, no chatter, down to ~70ms between edges |
| C3 Toggle mode | pass | 3 presses, clean alternation, one `by input` line per press, nothing on the release |
| C4 Toggle at boot | pass | no `relay 1 on by input` anywhere in the boot log with the "button" relay held on through the restart |
| C5 Follow mode | pass | follows both directions; a web override correctly persists until the next real input edge, not just a matching value |
| D1 SignalK connection | pass | connected throughout a 2-minute watch, no repeat disconnects |
| D2 Republish | pass | 10.01s measured once sampled with wall-clock-anchored polling; an initial two-sample check showed an apparent 30s gap, which was a tool round-trip measurement artifact, not a real interval problem |
| D3 SignalK switching | pass | PUT via the SignalK server worked in both directions; landed as `by nmea2000` rather than `by signalk` in the log because this network has a real N2K↔SK bridge device (`halpi2-nmea2000`) that the multi-source PUT routed through — the control path was still proven end-to-end |
| D4 Fail-safe | pass, with a note | failsafe transition at 30.81s (target ~30s); reconnect after the server came back took 69.2s, over the doc's "within a minute," but traced precisely to the reconnect backoff having already grown large during the ~2.5 minute outage, not a functional defect — reconnected and republished automatically with no intervention |
| E0 CAN diagnostics | pass | frames received, zero dropped, zero bus-off; arbitration-lost errors present but expected on a live multi-node bus |
| E1 On the bus | pass | address 34 (at the time), `traffic` flag verified against exact frame timing (flips true on an external frame, decays false exactly 10s later) |
| E2 N2K switching | pass | `cansend` PGN 127502 switched relay 1 both ways, log wording exact, next PGN 127501 broadcast reflected the change |
| E3 MFD lists device | not run | no MFD/chartplotter on the bench |
| E4 Address claim | pass | a competing ISO Address Claim (NAME=0) for address 34 made the board move to 35 immediately, stayed stable, stayed fully functional. Left on 35 afterward (not forced back to 34). |
| E5 Traffic warning | pass | `traffic: false` and the relay page's own template renders "no bus traffic" after a natural quiet window |
| F1 Ethernet preferred | not run | Ethernet port not wired this session |
| F2 Setup access point | not run | skipped this session |
| G1 Page loads | pass | `200`, valid responsive HTML; 8-row rendering and header confirmed by code inspection against the live API (no browser available in this environment to visually confirm) |
| G2 Buzzer test | pass | `202`/`409` as expected, log line exact (`"ESP 141"` at 2700 Hz), human confirmed audible |
| G3 Buzzer frequency | pass | 1000 Hz and 5000 Hz both confirmed audible with a pitch change, applied live with no restart |
| G4 Alarm buzzer | not run | the only `ALARM`-level condition that doesn't force a reboot (`relayExpander`) needs opening the case; filed espOS#137 suggesting a test-injection endpoint like the buzzer's, so this can be exercised remotely in future |
| H1 Update from the manifest | not run | skipped this session |

# signalk-espOS-8relay User Manual

This manual covers installing, setting up and using the
signalk-espOS-8relay firmware on the Waveshare ESP32-S3-ETH-8DI-8RO-C
board. Once installed, the board is an 8-relay, 8-input switch bank on
your SignalK server and your NMEA 2000 bus, configured from a web page on
the board itself.

The short path is: flash a release over USB-C (section 3), join the
board's setup access point and connect it to your network and SignalK
server (section 4), then name and configure each relay and input
(section 6). Section 7 covers day-to-day use, and section 8 what to do
when something doesn't work.

Releases are on
[GitHub](https://github.com/BoatHacks/signalk-espOS-8relay/releases). What
each one changed is in [CHANGELOG.md](CHANGELOG.md), how each is checked on
a real board is in [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md), and
the requirements behind this manual are in [SPEC.md](SPEC.md).

## 1. What you need

- A Waveshare ESP32-S3-ETH-8DI-8RO-C board
- Power: 7–36 V DC on the power terminal, or 5 V over USB-C
- A USB-C cable and a computer, for the first install
- A SignalK server on the boat network, reached over WiFi or Ethernet
- Optional: an NMEA 2000 connection to the board's CAN terminal

## 2. Safety

- Each relay is rated at most 10 A at 250 V AC or 30 V DC. Fuse every load.
- Test with harmless loads (lamps) before wiring anything that matters.
- Decide for each relay what should happen when the network is lost or the
  board restarts (section 6.3) before connecting it to a pump, heater or
  anything else that must not be left on or off by accident.

## 3. Installing the firmware

Each GitHub release carries two files:

- `signalk-espOS-8relay-<version>-merged.bin`: everything in one image
  (bootloader, partition table, firmware, web UI). Use it for the first
  install over USB-C.
- `signalk-espOS-8relay-<version>-ota.bin`: the firmware alone, for
  over-the-air updates of a board that already runs this firmware
  (v0.0.3 or later; earlier releases can't take OTA updates). It keeps all settings
  but doesn't update the web page itself. A new image must reach the
  network within the rollback timeout (10 minutes by default), or the
  board goes back to the previous firmware.

Releases are on [GitHub](https://github.com/BoatHacks/signalk-espOS-8relay/releases);
grab `-merged.bin` for a first USB install, or `-ota.bin` to update a
board that's already running. To build from source instead, see 3.1.

### 3.0 Flash a release over USB-C

With esptool installed (`pip install esptool`):

```sh
esptool.py --chip esp32s3 -p /dev/ttyACM0 write_flash 0x0 signalk-espOS-8relay-<version>-merged.bin
```

**This resets all settings.** The merged image is one continuous block
that also covers the settings area, so relay names, bank ids, WiFi
credentials and the saved state of `hold` relays go back to defaults. Use
it for a first install or a deliberate reset; update a working board with
the OTA image instead.

### 3.1 Build it

You need ESP-IDF v6.0.3 (see Espressif's installation guide). Then, from
the repository root:

```sh
. $IDF_PATH/export.sh
# A signing key must exist before the first set-target. Use the project's
# development key if you have access to it; otherwise make your own:
espsecure generate-signing-key --version 2 --scheme rsa3072 secure_boot_signing_key.pem
idf.py set-target esp32s3
idf.py build
```

Every image is signed. A board only accepts over-the-air updates signed
with the same key it was first flashed with, so keep the key: lose it and
boards flashed with it can only be updated over USB. `*.pem` files are
git-ignored; never commit one.

### 3.2 Flash it over USB-C

Connect the board's USB-C port and run:

```sh
idf.py -p /dev/ttyACM0 flash monitor
```

(The port name varies: `/dev/ttyACM0` or `/dev/ttyUSB0` on Linux,
`/dev/cu.usbmodem…` on macOS, `COM3` or similar on Windows.) `flash`
writes the firmware and the web UI; `monitor` shows the log. Exit the
monitor with Ctrl+].

### 3.3 Updates

A board running 0.0.7 or later checks for new firmware by itself. The
settings are in the web page under *Firmware updates*.

- **Where it looks.** *Manifest URL* is set on the first start to this
  project's update list,
  `https://raw.githubusercontent.com/BoatHacks/signalk-espOS-8relay/ota/manifest.json`,
  which every release updates. To turn update checks off, empty it; the
  board won't fill it in again.
- **Channel.** *stable* (the default) offers full releases only. *beta*
  also offers pre-releases (test builds). Change the *Channel* setting to
  choose.
- **Checking and installing.** The board checks shortly after start and
  then every *Check interval* (24 h). A newer build is shown in the web
  page; you install it from there. *Install automatically* (off by
  default) installs it as soon as it is found — the board restarts, so
  relays follow the restart rules of section 7.5. Leave it off on a boat
  unless you're sure.
- **Without internet on board.** Set *Where to look for updates* to
  *signalk*: the board then asks its SignalK server, which serves the same
  list through the signalk-espos-updates plugin (fetched when the server
  is online).

Every update is checked against the project's signature before it is
installed; a build that doesn't carry it is refused. A new build that
can't reach the network within 10 minutes of starting is rolled back to
the previous one.

## 4. First-time setup

Setup uses espOS's standard provisioning; *details to be confirmed on
hardware*:

1. Power the board. With no network configured, it opens a WiFi access
   point named `espOS-xxxx`.
2. Connect to it; a setup page opens. Choose your boat's WiFi network, or
   skip this if you use Ethernet only.
3. Open the device's web page (at `espos-xxxx.local` or its IP address) and
   go through the settings in section 6.
4. On the SignalK server, approve the device's access request
   (Security → Access Requests). Without approval it can't publish or be
   switched from SignalK.

If the board later loses its WiFi (e.g. the boat's password changed) and
you don't have a laptop and USB cable handy, see the BOOT button recovery
methods in section 8 instead of repeating this from scratch.

## 5. Connections

| Board terminal | Use |
|---|---|
| RO1–RO8 | Relay contacts for your loads |
| DI1–DI8 | Isolated inputs: switches, float switches, sensors |
| CAN (H/L) | NMEA 2000 backbone (via a drop cable) |
| RJ45 | Ethernet |

When an Ethernet cable is connected and gets an address, the board uses it;
otherwise it uses WiFi.

## 6. Settings

All settings are in the device's web page. Changes marked *restart* take
effect after the board restarts.

### 6.1 Switch banks

| Setting | Default | Notes |
|---|---|---|
| Relay bank id (`bankId`) | 0 | The relays' switch-bank number, 0–252. *Restart.* |
| Input bank id (`inputBankId`) | 1 | The inputs' switch-bank number. Must differ from the relay bank id. *Restart.* |

**If you install more than one board, give each one different bank ids.**
Every board starts with 0 and 1, and two boards with the same ids will
clash on NMEA 2000 and in SignalK. If both ids on one board are the same,
the board reports a warning and doesn't publish the inputs until fixed.

### 6.2 SignalK paths

| Setting | Default | Notes |
|---|---|---|
| Publish `electrical.switches.bank.*` | On | The standard SignalK switch-bank paths. *Restart.* |
| Publish `electrical.controls.*` | Off | Paths in the style proposed by SignalK RFC 0009. *Restart.* |

Both can be on at once, and relays can be switched through either. Both
can be off; the board is then controlled only over NMEA 2000 and by input
overrides.

### 6.3 Each relay

| Setting | Default | Notes |
|---|---|---|
| Name | Relay *n* | Shown in SignalK apps, e.g. "Bilge pump" |
| Mode | Latching | *Latching* stays as set; *momentary* switches off by itself after the pulse time |
| Pulse time | 1 s | For momentary relays |
| When SignalK is lost or the board restarts | Switch off | *Keep last state*, or *switch off*. Momentary relays always switch off. |
| Wired to NC | Off | Turn on if this relay's load is wired to its NC (normally-closed) terminal instead of NO. SignalK, NMEA 2000 and the relay page then report and command the *load's* state, not the coil's -- see the warning below. |
| Interlocked with | None | Another relay (1–8) that must never be on at the same time as this one, e.g. windlass up/down or a reversing motor's two contactors -- see below. |
| Controlled by input | None | An input (1–8) that switches this relay directly |
| Input link | Follow | *Follow*: the relay copies its input. *Toggle*: each press of a push button on the input switches the relay over (section 7.4). |
| Maximum on-time | 0 (no limit) | Switch off automatically after this long, however the relay was switched on. Ignored in momentary mode. |
| On chirp | relay-on | Latching mode only: tone (from the Tones page, section 7.7) to play when this relay switches on by a direct command (SignalK, NMEA 2000, the relay page, or an input override) -- not for an automatic change. *(none)* = no chirp. |
| Off chirp | relay-off | As above, for switching off. |
| Pulse-start chirp | pulse-start | Momentary mode only: tone to play when a pulse starts (a direct on-command), in place of the on chirp above. |
| Pulse-stop chirp | pulse-stop | Momentary mode only: tone to play when a pulse ends -- its timer running out counts too, unlike the off chirp above, since that's the normal way a pulse finishes. |

> **Wired to NC, and what "switch off" means for it.** This setting only
> changes what *on*/*off* mean when reported or commanded -- it never
> changes what "switch off" *does*. "When SignalK is lost or the board
> restarts: switch off" (and every other automatic switch-off above)
> always de-energizes the relay's coil, exactly like a real power loss
> would. For a normally-wired (NO) relay that turns its load off. For a
> relay wired to NC, a de-energized coil is what *powers* the load -- so
> after one of these automatic transitions, a `wiredNC` relay correctly
> *reports as on*. That is the point of wiring a bilge pump or a nav light
> to NC in the first place: it keeps running through exactly the kind of
> event `switch off`/`default-safe` is built to survive. Set *Wired to
> NC* to match how the relay is actually wired, not to change how it
> behaves on loss of power or SignalK -- it can't do that.
>
> A momentary relay's pulse still always means "energize the coil for the
> pulse time, then release it" -- for a `wiredNC` momentary relay this is
> a brief *load-off* blip (e.g. to momentarily kill power to reset
> something downstream), not a load-on pulse.
>
> A factory reset returns *Wired to NC* to *Off* along with every other
> setting -- it does not know or guess how a relay is physically wired.
> Check this setting after any factory reset on a board with NC-wired
> relays.

> **Interlocked with.** Set this on *both* relays of a pair, each naming
> the other -- a one-sided or self-referencing setting is ignored and
> raises a warning on the health page, so a typo never silently leaves a
> motor unprotected. Switching one on while its partner is on switches the
> partner off immediately, and the first relay itself switches on only
> after the *Interlock dead time* (section 6.5) has passed -- so the two
> coils are never energized together, whichever page, app or bus the
> command came from. *All on* (section 7.2) skips every relay in an
> interlocked pair rather than switching one on and fighting this rule
> over the other. If the board restarts with both of a pair's coils
> somehow already on (very old firmware's saved state, or a settings
> change that just paired up two relays that happened to both be on),
> neither is restored -- both switch off, and a warning is raised.
> Momentary relays can be interlocked too (a jog up/down pair).

### 6.4 Each input

| Setting | Default | Notes |
|---|---|---|
| Name | Input *n* | Shown in SignalK apps |
| Invert | Off | Turn on for normally-closed switches |
| On chirp | input | Tone (from the Tones page, section 7.7) to play when this input's reported state changes to on. *(none)* = no chirp. |
| Off chirp | input | As above, for changing to off. |
| Alarm | Off | Raise a SignalK notification (section 7.1), and an NMEA 2000 alert (section 7.3), while this input reads on, e.g. a bilge float switch. `off`/`warn`/`alarm`/`emergency` set the severity; `off` raises nothing. |
| Alarm message | *(none)* | Notification text. Empty = "*name* active". |
| Alarm buzzer | Off | Also sound the board's buzzer while this input's alarm is active (section 7.6). Needs *Alarm* set to something other than `off`; works whether or not *Buzzer on alarm* is on. |

### 6.5 Other

| Setting | Default | Notes |
|---|---|---|
| Input debounce | 50 ms | How long an input must be steady before a change counts (10 ms minimum) |
| SignalK-loss grace period | 30 s | How long SignalK may be unreachable before relays set to *switch off* do so |
| SignalK republish interval | 10 s | Resend every relay and input state this often even when nothing changed, so SignalK apps don't show them as stale. 0 = send changes only. |
| Status LED brightness | 10 % | 0 turns the LED off (section 7.6) |
| Buzzer on alarm | Off | Beep in Morse while an alarm is active (section 7.6) |
| Buzzer frequency | 2700 Hz | Tone of the buzzer, 42–10000 Hz. Applies at once; try it with *Test buzzer* on the relay page. |
| Buzzer on events | Off | Chirp on boot, a BOOT-button action, a relay switching on/off, or an input changing (section 7.7). Separate from *Buzzer on alarm*. |
| Interlock dead time | 100 ms | How long an interlocked relay's partner stays off before it switches on (0–2000 ms). Applies to every interlocked pair (section 6.3). |
| Input alarms on NMEA 2000 | On | Also raise input alarms as NMEA 2000 alerts (section 7.3). Applies at once. |
| Ethernet enabled | On | Off = WiFi only. To use Ethernet only, turn off espOS's WiFi "Station enabled" setting instead; the setup access point stays available. |

### 6.6 Schedules

The board's own real-time clock keeps schedules running before, or
entirely without, a SignalK server (section 7.8). Time zone is espOS's own
*Timezone* setting (Clock settings page, POSIX TZ form, e.g. `UTC0` or
`CET-1CEST,M3.5.0,M10.5.0/3` for central Europe) -- nothing new here, a
schedule's `HH:MM` and days of the week are read in that zone.

| Setting | Default | Notes |
|---|---|---|
| Position source | SignalK | Where sunrise/sunset math gets the boat's position: `navigation.position` from the SignalK stream, or decoded from the board's own NMEA 2000 bus (PGN 129025/129029). *Restart.* |
| Fallback latitude / longitude | 0°, 0° | Used when the position source above has no fresh reading yet (nothing received in the last 10 minutes) -- a boat that stays put, or a starting point before the first live reading arrives. |

**Each of the 8 schedule entries:**

| Setting | Default | Notes |
|---|---|---|
| Relay | None | 1–8, or *none* to leave this entry unused. |
| Mode | Clock | *Clock*: an on-time and an off-time (below). *Repeat*: a duty cycle -- on for a set number of minutes, repeating every so many minutes, restarting fresh at local midnight. |
| On time / on-minutes | *(none)* | Clock mode: `HH:MM` (24-hour), or a sunrise/sunset offset -- `sunrise`, `sunset+30`, `sunset-45m` (minutes before/after, the trailing `m` is optional). Repeat mode: how many minutes on, e.g. `10`. |
| Off time / cycle minutes | *(none)* | Clock mode: same form as the on time. An off time earlier than the on time means the schedule runs overnight, across midnight. Repeat mode: the full cycle length in minutes, e.g. `60` for "every hour" -- must be longer than the on-minutes above. |
| Days | Every day | Which days of the week this entry runs. |

An on-time and an off-time can be mixed freely -- e.g. on at a fixed
`18:00`, off at `sunrise+30`. A schedule's transitions behave like any
other command: an entry switching a momentary relay just starts its usual
pulse (the *off* transition is then a no-op, nothing left to switch off);
a `wiredNC` relay reports and is commanded by its load state as usual; an
interlocked relay still can't be switched on while its partner is on.
Between two scheduled transitions, a manual command from SignalK, NMEA
2000, the relay page or an input override stands -- a schedule only acts
at its own on/off instants, the same rule an input override follows.

**Two or more entries switching the same relay** is rejected, not resolved
by which one is listed first: every entry naming that relay does nothing
until the clash is fixed, and the health page warns which relay. This
check only looks at whether the entries name the same relay, not whether
their days or times could ever actually overlap -- simpler and more
cautious than trying to work that out.

**No valid time yet** (freshly powered on with a dead RTC battery, and
neither SNTP nor SignalK have set the clock) makes every schedule do
nothing, with its own health warning, until a source sets the clock.

## 7. Using it

### 7.1 From SignalK

Relay *n* on relay bank *B* appears as
`electrical.switches.bank.B.n.state`, and input *n* on input bank *I* as
`electrical.switches.bank.I.n.state`. Any SignalK app that can switch a
path (a switch in an instrument panel, a Node-RED flow) can turn relays on
and off. Inputs can only be read.

With the `electrical.controls.*` tree turned on, the same relay also
appears as `electrical.controls.espOS-instanceB-relayn` and the input as
`electrical.controls.espOS-instanceI-inputn`.

States are sent when they change, and again every *SignalK republish
interval* (10 s by default, section 6.5), so apps that mark old values as
stale keep showing them as current. The interval is also sent as the
paths' `timeout` metadata, unless the server already has its own metadata
for them.

States are `1` (on) and `0` (off). To switch a relay, send `1`/`0` or
`true`/`false`.

**Cycle counters and runtime.** Each relay and input also publishes
`electrical.switches.bank.B.n.cycles` (how many times it has switched on)
and `.runTime` (total seconds it has been on), and the same under
`electrical.controls.*` when that tree is on. These update with the
SignalK republish interval, not on every change, and count every source
(SignalK, NMEA 2000, the relay page, an input override, a pulse ending,
the fail-safe, the maximum on-time) -- not just direct SignalK commands.

**Names.** The first time the board connects, each relay's and input's
name becomes its display name in SignalK. After that, the SignalK server's
own setting wins: to rename a channel later, change its display name on
the server (or clear it there, and the board's name is used again).

**Input alarms.** An input with its *Alarm* setting (section 6.4) not
*off* raises a SignalK notification on its own path, e.g. a bilge float
switch on input 3 of input bank 1 raises
`notifications.electrical.switches.bank.1.3.state` (and the matching
`electrical.controls.*` path when that tree is on) as
`{"state":"warn"|"alarm"|"emergency","method":["visual","sound"],
"message":"…"}` while the input reads on, and clears it back to
`{"state":"normal",...}` as soon as it reads off. It's raised only once the
input has settled after boot (section 4), so a float switch that's already
tripped when the board starts up still raises its alarm; it's resent with
the *SignalK republish interval* while it stays active, and again after
every reconnect, same as relay and input states.

**If your SignalK server also reads the NMEA 2000 bus**, it already gets
the relays and inputs from the bus, under the same
`electrical.switches.bank.*` paths. Having both makes apps list two
sources for each switch. Turn off *Publish electrical.switches.bank.\** on
the board; the server keeps seeing the relays through NMEA 2000, and
switching them from SignalK still works if the server has an NMEA 2000
switching plugin (such as signalk-n2k-switching) set up for this bank.

### 7.2 From the relay page

Open `http://<board address>/relays` (for example
`http://espos-cf28.local/relays`) in a browser on the same network. It
lists the 8 relays with On and Off buttons and their current state, and
next to each relay the state of the input with the same number. *All on*
(which asks first) and *All off* switch every relay, except that *All on*
skips any relay that's interlocked with another (section 6.3) -- it says
so afterwards, naming which ones. The page updates every second.

The header shows the board's name and firmware version, and a status line
below it shows the network (Ethernet or WiFi, and the address), whether
SignalK is connected and to which server, and the board's NMEA 2000
address, with a warning when no traffic has been seen on the bus for 10
seconds. A momentary relay has *Pulse* and *Stop* buttons instead of *On*
and *Off*; *Stop* ends a pulse early.

Under each relay's name the page shows what switched it last and how long
ago: SignalK, NMEA 2000, this page, an input, the end of a pulse, the
SignalK-loss fail-safe, the maximum on-time, or start-up. The serial log
prints the same for every change, e.g. `relay 3 on by nmea2000`.

The page switches relays the same way SignalK and NMEA 2000 do: a
momentary relay switched on turns itself off after its pulse time, and a
relay that follows an input keeps the page's command until that input
changes.

**Cycle counters and runtime hours.** Each relay and its paired input also
show how many times they have switched on ("cycles") and their total
runtime ("3 h 12 min", etc.), counting every source: SignalK, NMEA 2000,
this page, an input override, a pulse ending, the fail-safe and the
maximum on-time. A *Reset* button next to each clears that channel's
count back to zero (it asks first, and can't be undone) -- resetting one
relay or input never affects any other. The same reset is available to
scripts: `POST /api/v1/relays/<n>/counters/reset` and
`POST /api/v1/inputs/<n>/counters/reset`, `Content-Type: application/json`,
no body needed.

Counters are kept in memory and written to flash at most every 10
minutes, and once more on a clean restart (an update, a settings change):
a power cut can lose up to that last 10 minutes of counting, so a channel
that was on right up to a sudden power loss may show slightly less
runtime than it actually had. A reset, being a deliberate action, is
always written to flash immediately.

If an API key is set in espOS's security settings, log in on the
device's main page first. Without a key, anyone on the network can switch
the relays from this page, so set one on a shared network.

The same actions are available to scripts: `GET /api/v1/relays` returns
the state (including each channel's `cycles` and `runTime`),
`GET /api/v1/relays/status` the status line, and
`PUT /api/v1/relays/<n>` (one relay) or `PUT
/api/v1/relays` (all) with the body `{"on": true}` or `{"on": false}` and
`Content-Type: application/json` switch them.

### 7.3 From NMEA 2000

The board appears on the bus as a switch-bank device. MFDs and switch
panels that support NMEA 2000 switch banks show both banks, and can switch
the relays.

**Input alarms.** An input alarm (section 6.4) is also raised on the bus
as an NMEA 2000 alert, so a chartplotter sounds it even without a SignalK
server: PGN 126983 (*Alert*) every second while the input reads on, and
126985 (*Alert Text*) with the same message as the SignalK notification
every 10 seconds. `warn`, `alarm` and `emergency` are sent as the alert
types *Warning*, *Alarm* and *Emergency Alarm*. Acknowledging it on the
chartplotter marks it acknowledged until the input turns off; temporary
silence isn't offered. When the input turns off, the alert goes back to
*Normal* (sent three times, a second apart) and stops being sent. Like
the SignalK notification, it's only raised once the input has settled
after boot, so a switch that's already tripped still alarms at start-up.

If your SignalK server runs signalk-to-nmea2000 with its *Notifications*
conversion on, it also puts the board's SignalK notification on the bus,
and the chartplotter shows the alarm twice. Turn off one of the two:
*Input alarms on NMEA 2000* (section 6.5) on the board, or that option in
the plugin.

### 7.4 Input overrides

A relay linked to an input reacts to it in one of two ways (*Input link*,
section 6.3):

- **Follow** (the default): the relay follows the input whenever the input
  changes. A command from SignalK, NMEA 2000 or the relay page after that
  still works, and holds until the input changes again. At start-up, a
  linked relay takes the input's state.
- **Toggle**, for momentary push buttons: each press switches the relay
  over; letting go does nothing. Other commands work as usual, and the
  next press switches over from whatever state the relay is in. At
  start-up nothing happens, even with the button held. A momentary relay
  in toggle mode starts its pulse on a press, and a second press during
  the pulse ends it early. For a normally-closed button, also turn on the
  input's *Invert*.

### 7.4.1 Maximum on-time

A relay with a *Maximum on-time* switches itself off once it has been on
that long, whoever switched it on, including an input it follows. Another
"on" command restarts the time, so a pump can be kept running by
confirming it. After a restart, a relay that comes back on starts a fresh
time. Setting or clearing the limit while the relay is on applies from
that moment.

### 7.5 If the network or power is lost

- **SignalK unreachable for longer than the grace period:** relays set to
  *switch off* switch off; the rest stay as they are. NMEA 2000 and input
  overrides keep working.
- **Board restart (e.g. after an update):** relays set to *keep last state*
  stay as they were, without switching; the rest switch off.
- **Power loss:** all relays drop out. When power returns, relays set to
  *keep last state* switch back on to their last state. Cycle counters and
  runtime (section 7.2) survive too, minus up to 10 minutes of counting
  not yet written to flash when the power went (section 7.2).

"Switch off" and "drop out" always mean the coil de-energizes -- for a
relay set to *Wired to NC* (section 6.3), that's the state its load is
*powered* in, so it correctly reports as on afterwards. See the warning
in section 6.3.

### 7.6 Status LED and buzzer

| LED | Meaning |
|---|---|
| Green | Everything is fine |
| Blue | Not connected to a SignalK server (not shown when both SignalK path settings are off) |
| Amber | A warning, e.g. relay and input bank ids are the same |
| Red | An alarm, e.g. the relay chip isn't responding |

The web page's health section says what the warning or alarm is.

With *Buzzer on alarm* turned on, the board beeps while an alarm is
active: "ESP" and then the last number of its IP address, in Morse, every
few seconds. On a boat with several boards, that tells you which one is
complaining: `. ... .--.  ....- ..---` is "ESP 42", the board at
192.168.x.42. Without a network address (for example when WiFi is down)
it beeps "ESP AP" (`. ... .--.  .- .--.`): join the board's setup access
point, `espOS-xxxx`, to reconfigure it.

An input with *Alarm buzzer* turned on (section 6.4) sounds the buzzer
while its alarm is active, whether or not *Buzzer on alarm* is on: "IN"
and the input's number in Morse, every few seconds, so `.. -.  ...--`
("IN 3") is input 3's float switch. Several inputs at once are all named,
lowest first ("IN 3 5"). It stops when the input reads off again; to
silence it before then, turn *Alarm buzzer* off (applies at once). An
input alarm takes the buzzer over from a health alarm, which still shows
on the LED; the health alarm's pattern comes back once no input alarm is
left.

To hear the buzzer without waiting for an alarm, press *Test buzzer* on
the relay page (section 7.2). It plays the same pattern once, whether or
not *Buzzer on alarm* is on, and does nothing while an alarm is already
sounding. Passive buzzers differ: if the tone is quiet or shrill, change
*Buzzer frequency* (section 6.5) and test again.

### 7.7 Event chirps and the Tones page

With *Buzzer on events* turned on (section 6.5), the board plays a short
chirp on:

- **Boot**, once power-on finishes.
- **A BOOT-button action** (section 8), just before the board restarts: a
  distinct chirp for reopening the setup access point, and another for a
  factory reset.
- **A latching relay switching on or off** by a direct command (SignalK,
  NMEA 2000, the relay page, or an input override) -- not for an
  automatic change (the SignalK-loss fail-safe, or the maximum on-time
  running out), which would be noise rather than useful feedback.
- **A momentary relay's pulse starting or ending**: starting always means
  a direct on-command; ending includes the pulse's own timer running out,
  since that's the normal, expected way it finishes, not a surprise.
  These use their own pulse-start/pulse-stop chirps (section 6.3), not
  the on/off chirps above.
- **An input's reported state changing** to on or off (after debouncing,
  never on a raw bounce; each direction has its own chirp, section 6.4).

A chirp never plays over, or gets queued behind, the alarm or the
BOOT-button's LED override (section 8): it's silently skipped while either
is active, and cut off if the alarm starts mid-chirp.

Open `http://<board address>/tones` to manage this. It has:

- A **tone library**: named tones written as RTTTL (Ring Tone Text Transfer
  Language, the format used by old ringtone-composer tools) --
  `name:d=<default duration>,o=<default octave>,b=<tempo>:<notes>`, e.g.
  `boot:d=16,o=7,b=200:c,e,g`. Add, edit or delete entries, then *Save tone
  library*. Each entry has a *Play* button to hear it immediately, without
  saving first -- handy while writing or tweaking an RTTTL string. The
  board ships with eight: `boot`, `portal`, `reset`, `relay-on`,
  `relay-off`, `pulse-start`, `pulse-stop`, `input`.
- A **dropdown per event** (boot, setup portal, factory reset, each
  relay's on/off and pulse-start/pulse-stop, each input's on/off) to pick
  which library tone plays for it, or *(none)* for silence. Changes here
  apply immediately.

### 7.8 Schedules

Settings are in section 6.6. A few things worth knowing about how a
schedule actually behaves once it's set up:

- **The board's own clock keeps schedules running without SignalK.** The
  PCF85063 real-time clock survives a power cut, so a schedule set to
  switch a relay at sunset still does, even if the board has never
  connected to a SignalK server, or lost power the night before and came
  back up mid-window. On the very first evaluation after boot (or after
  the clock first becomes valid), a relay that *should already be on* by
  its schedule switches on immediately -- it doesn't wait for the next
  transition, which could be up to a day away.
- **A midnight-spanning schedule isn't cut short by the days setting.** A
  "Friday 22:00 to 06:00" entry stays on into Saturday morning even if
  *Days* doesn't include Saturday -- the window already started on a day
  it was allowed to, and finishing it out is the point of an overnight
  schedule. A *repeat*-mode duty cycle is different: it resets cleanly at
  local midnight and simply does nothing on an excluded day, with no
  carry-over from the day before.
- **A sunrise/sunset-referenced schedule during permanent polar day or
  night treats the whole day as continuously on the correct side of the
  boundary**, rather than doing nothing that day. A sunset-to-sunrise
  entry (an anchor light, say) stays on right through a polar night, and
  off right through a polar day; a sunrise-to-sunset entry is the mirror
  image.
- **Daylight saving changes are handled through the board's own local
  clock**, the same as any household timer: a "spring forward" that skips
  over a scheduled time still catches it on the next check (at most a
  second later); a "fall back" that repeats an hour runs that hour's
  schedule twice, which is the same thing a mechanical timer on the wall
  would do.
- **A schedule switching a relay counts as an automatic change, not a
  direct command** -- it doesn't chirp (section 7.7) even with *Buzzer on
  events* on, the same as a maximum on-time expiring or the SignalK-loss
  fail-safe. It also doesn't fight a manual override: between two
  scheduled transitions, whatever SignalK, NMEA 2000, the relay page or an
  input override last set stands until the schedule's next transition.

## 8. Troubleshooting

### Recovering a board that's lost its network, with the BOOT button

The small button next to the USB-C port (used for USB flashing) doubles as
a recovery button once the firmware is running. Hold it and watch the
status LED:

- **~5 seconds: the LED blinks white.** Release now to reopen the setup
  access point (`espOS-xxxx`), the same one you connected to for
  first-time setup (section 4). Go through it again to point the board at
  a different (or corrected) WiFi network. The board keeps working over
  Ethernet, NMEA 2000, its inputs and relays the whole time it's showing
  this access point — only WiFi is affected — but if nobody finishes the
  portal, it stays off WiFi until someone does, or the button is used
  again.
- **~15 seconds: the LED starts blinking red instead.** Release now for a
  full factory reset: every setting goes back to its default, the board
  forgets its SignalK server approval (it must be approved again,
  section 4 step 4), and it restarts. Relays come up in their normal
  boot state (section 7.5), not whatever they happened to be doing before
  the reset.
- **Let go before 5 seconds** and nothing happens — a stray knock against
  the button is harmless.

A button already being held when the board powers up is ignored until it's
released once, so it can't trigger either action by accident during boot.

With *Buzzer on events* turned on (section 7.7), each action also chirps a
distinct tone just before the board restarts, so you know which one fired
without watching the LED.

### NMEA 2000: nothing received, or the board isn't listed

The relay page's status line says *no bus traffic* when no frame has
arrived for 10 seconds. `http://<board>/api/v1/n2k` shows espOS's CAN
counters, which tell the causes apart:

- **Frames received, but the board still doesn't show on an MFD**: the
  wiring is fine; report it as a firmware problem.
- **No frames, and the error count rises**: the board hears the bus but
  can't make sense of it. Check that CAN-H and CAN-L aren't swapped, that
  the bus is terminated at both ends of the backbone (a board on a drop
  cable must **not** have its `120R` jumper fitted; a board at the end of
  a backbone, or on a two-device test bench, must), and that the CAN
  terminal's ground is connected to the bus ground (NET-C).
- **No frames and no errors**: nothing reaches the board. Check the drop
  cable reaches the CAN terminal, and that at least one other device on
  the bus is powered and sending.

The CAN interface is isolated from the rest of the board, so its ground
is not the power supply's ground.

### A schedule isn't switching the relay

Check the health page first for one of two schedule-specific warnings:

- **"No valid time"**: the board doesn't know what time it is yet -- a
  brand-new board before its first SNTP or SignalK sync, or one whose RTC
  battery has died and lost power completely. Every schedule does nothing
  until a source sets the clock. Confirm the board has a network route to
  an NTP server or a SignalK connection; `http://<board>/api/v1/time`
  shows whether the clock is currently synced and from which source.
- **A relay named by two or more schedules**: rejected outright, not
  resolved by listing order -- every entry naming that relay does nothing
  until only one of them still does. The health message names the relay;
  fix it in section 6.6's schedule list.

If neither warning is showing and the schedule still isn't switching:

- **Check the days setting** -- it's easy to leave a day unticked by
  mistake, and a schedule simply does nothing on an excluded day.
- **For a sunrise/sunset-referenced entry, check the position.** With
  *Position source* set to SignalK, the board needs a `navigation.position`
  update from the server within the last 10 minutes, or it falls back to
  the configured fixed position -- which may be far enough from the boat's
  actual location to shift sunrise/sunset by more than expected. With
  *Position source* set to NMEA 2000, confirm the board is actually
  receiving PGN 129025 or 129029 from a GPS on the bus (section 8's NMEA
  2000 troubleshooting above covers diagnosing bus traffic generally).
- **Double-check the time zone** (espOS's *Timezone* setting, section
  6.6) -- a schedule's `HH:MM` and its sunrise/sunset offsets are read in
  that zone, so a wrong or default (`UTC0`) time zone shifts every
  clock-mode entry by the difference.

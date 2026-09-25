# signalk-espOS-8relay User Manual

> **This manual describes the planned behaviour.** No firmware has been
> released yet, so nothing here can be tried out. Sections are filled in
> and checked as each stage is built; setting names and some details may
> still change. The requirements behind this manual are in
> [SPEC.md](SPEC.md).

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

There are no release images yet, so for now the firmware is built from
source. Over-the-air updates (including through signalk-espos-manager) will
be described with the first release.

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
| Controlled by input | None | An input (1–8) that switches this relay directly |

### 6.4 Each input

| Setting | Default | Notes |
|---|---|---|
| Name | Input *n* | Shown in SignalK apps |
| Invert | Off | Turn on for normally-closed switches |

### 6.5 Other

| Setting | Default | Notes |
|---|---|---|
| Input debounce | 50 ms | How long an input must be steady before a change counts (10 ms minimum) |
| SignalK-loss grace period | 30 s | How long SignalK may be unreachable before relays set to *switch off* do so |
| Status LED brightness | 10 % | 0 turns the LED off (section 7.5) |
| Buzzer on alarm | Off | Beep in Morse while an alarm is active (section 7.5) |
| Ethernet enabled | On | Off = WiFi only. To use Ethernet only, turn off espOS's WiFi "Station enabled" setting instead; the setup access point stays available. |

## 7. Everyday use

### 7.1 From SignalK

Relay *n* on relay bank *B* appears as
`electrical.switches.bank.B.n.state`, and input *n* on input bank *I* as
`electrical.switches.bank.I.n.state`. Any SignalK app that can switch a
path (a switch in an instrument panel, a Node-RED flow) can turn relays on
and off. Inputs can only be read.

With the `electrical.controls.*` tree turned on, the same relay also
appears as `electrical.controls.espOS-instanceB-relayn` and the input as
`electrical.controls.espOS-instanceI-inputn`.

States are `1` (on) and `0` (off). To switch a relay, send `1`/`0` or
`true`/`false`.

**Names.** The first time the board connects, each relay's and input's
name becomes its display name in SignalK. After that, the SignalK server's
own setting wins: to rename a channel later, change its display name on
the server (or clear it there, and the board's name is used again).

**If your SignalK server also reads the NMEA 2000 bus**, it already gets
the relays and inputs from the bus, under the same
`electrical.switches.bank.*` paths. Having both makes apps list two
sources for each switch. Turn off *Publish electrical.switches.bank.\** on
the board; the server keeps seeing the relays through NMEA 2000, and
switching them from SignalK still works if the server has an NMEA 2000
switching plugin (such as signalk-n2k-switching) set up for this bank.

### 7.2 From NMEA 2000

The board appears on the bus as a switch-bank device. MFDs and switch
panels that support NMEA 2000 switch banks show both banks, and can switch
the relays.

### 7.3 Input overrides

A relay linked to an input follows that input whenever the input changes.
A command from SignalK or NMEA 2000 after that still works, and holds until
the input changes again. At start-up, a linked relay takes the input's
state.

### 7.4 If the network or power is lost

- **SignalK unreachable for longer than the grace period:** relays set to
  *switch off* switch off; the rest stay as they are. NMEA 2000 and input
  overrides keep working.
- **Board restart (e.g. after an update):** relays set to *keep last state*
  stay as they were, without switching; the rest switch off.
- **Power loss:** all relays drop out. When power returns, relays set to
  *keep last state* switch back on to their last state.

### 7.5 Status LED and buzzer

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

## 8. Troubleshooting

*To be written during hardware bring-up (stage 07).*

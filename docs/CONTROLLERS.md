# MIDI controller profiles

A controller profile tells LYNX SHARE what a MIDI controller's buttons, knobs, faders and pads do, and what LYNX
sends back to it (lights, motor faders, display text). A profile is one small text file. There is no code per
device, so you can write a profile for your own controller.

- **Choose a profile:** Settings › MIDI › Input devices › *Controller type* of the device.
  - *Detect automatically* picks the profile whose port-name patterns match the device.
  - *Generic controller* uses no profile: link each control by hand with a control's right-click menu ›
    *Link to MIDI controller*.
  - Any profile can be forced by picking it from the list.
  - For the detailed way use *Link to controller...* in the same menu: the "Remote control settings" window sets
    the port, channel and controller number (or detects them), a mapping formula (presets: inverted, log scale,
    switch, dead zone...), smoothing, Omni and whether other controls on the same controller are unlinked. Give a
    device a *Port* number in Settings › MIDI › Input devices when links should follow only that device.
- **Links made by hand come first.** A controller (CC) you linked by hand is never taken by a profile.
- **Add your own profile:** click *Controller profiles folder* in Settings › MIDI, put a `<name>.lynxctl` file
  there, then click *Refresh device list*. Your profiles are listed with "(user)". A file of yours with the same
  name as a built-in profile replaces it.
- **A file with a mistake is left out.** Every mistake is reported with its line number, for example
  `line 12: unknown action 'mixer.volum'`.
- **What a profile does not name plays as before:** keys, pads, wheels and pedals go to the selected channels.

The built-in profiles are the files in `resources/controllers/`. They are compiled into the program by
`tools/make_controllers.py` (run it after changing a file; a test compares the two).

## Built-in profiles

Every map comes from the maker's own public document, named at the top of each file. None was tried on a
device yet: each file lists what the document does not say and has to be checked on hardware.

| Controller type | Detected on a port named | What it covers | Source |
|---|---|---|---|
| Mackie Control Universal | `Mackie Control*`, `MCU Pro*`, `*Mackie Control Universal*` (not `*XT*`, `*Extender*`) | 8 motor faders with touch + Master fader; V-Pots with rings in three layers (Pan / Plug-in / Track = Channel Rack volumes); REC, SOLO, MUTE, SELECT with lights; bank and channel left / right; cursor keys (pages, channels); transport, cycle, click, undo, jog; LCD (names and values) | Logic Control / Mackie Control protocol appendix of the Logic user manual (Emagic / Mackie) |
| Mackie Control Extender (XT) | the same with `XT` or `Extender` | The same strips; takes the next 8 strips after the main unit and moves with its bank | the same |
| Novation Launchkey MK3 (DAW mode) | `*Launchkey MK3*DAW*`, `MIDIIN2 (*Launchkey MK3*` | 8 pots (Pan / Volume / Device, following the keyboard's own pot mode), 9 faders and fader buttons (49 / 61 / 88), transport, track and scene buttons, 16 pads as steps with lights, the 2 x 16 display | Launchkey MK3 Programmer's Reference Guide (Novation) |
| Akai APC Key 25 mk2 | the device's second port (`MIDIIN2 (APC Key 25 mk2*`, `APC Key 25 mk2 Control*`) | 8 relative knobs (Volume / Pan / Device by the Scene Launch buttons), top two pad rows as 16 steps with lights, lower rows as notes, track buttons select, bank left / right, play, stop, record | APC Key 25 mk2 Communication Protocol v1.1 (Akai Professional) |
| Arturia KeyLab Essential mk3 (Mackie Control port) | `*KL Essential*MCU*`, `*KeyLab Essential*MCU*` | The Mackie Control map on the keyboard's MCU/HUI port (set the keyboard's DAW mode to Mackie Control) | KeyLab Essential mk3 user manual (Arturia) |
| Korg nanoKONTROL2 (Mackie Control mode) | never: choose it by hand | The Mackie Control map. Start the device in one of its Mackie Control DAW modes (hold a button while plugging in, see its manual) | nanoKONTROL2 Owner's Manual and Parameter Guide (Korg) |

Not covered, because the maker publishes no MIDI map for them: Akai MPK mini mk3 and the first APC Key 25 (use
*Generic controller* and link by hand), and the nanoKONTROL2's own CC mode (its numbers are set in Korg's editor).
HUI is not built.

## What a profile can drive

| Area | Actions |
|---|---|
| Transport | play, stop, record, loop (pattern / song mode), metronome, tap tempo, one bar back / forward, jog, undo, previous / next pattern |
| Mixer | a bank of strips (8 per device): volume, pan, mute, solo, record arm, select; bank left / right; one strip left / right; Master volume |
| Channel Rack | a bank of channels: volume, pan, mute, select; previous / next channel; the selected channel's volume and pan |
| Plugin | the parameters of the selected channel's instrument, in pages of one knob per strip; page left / right |
| Pads | notes into the selected channels; the selected channel's steps in the current pattern |
| Feedback | lights for every switch above, motor faders, encoder rings, text displays (SysEx) |

Knobs and sliders without motors use **soft takeover**: a control moves its parameter only after it reaches the
parameter's value, so nothing jumps. After a bank, page or layer change the controls have to reach their new
targets again. Relative encoders and motor faders do not need it.

## File format

Plain text, UTF-8. Lines that start with `#` or `;` are comments. Outside `[device]`, anything after ` #` on a
line is a comment too. Numbers are decimal (`0x1F` is accepted). The bytes of `send` and `write` lines are
hexadecimal. MIDI channels are 1 to 16. Strips are counted from 1.

```ini
[device]
name = My Controller 49            # as the Controller type list shows it (required)
maker = My Company
source = My Controller 49 User Guide v1.2, "MIDI implementation", https://example.com/manual.pdf
match = My Controller 49*, *MYCTRL DAW*   # port-name patterns; * = anything; upper / lower case is ignored
exclude = *XT*                     # a port that matches one of these is not this device
strips = 8                         # strips per device (default 8)
group = mygroup                    # devices of one group share one bank; each takes the next strips
layers = pan, plugin               # optional: the layers of the device; the first one is active at start
extends = mackie-control           # optional: start from another profile's maps

[init]                             # messages sent when the device opens
send = 9F 0C 7F

[exit]                             # messages sent when it closes (lights and display are cleared before)
send = 9F 0C 00

[input]                            # <message> = <action> [strip] [options]
cc 0-7 ch 1 = mixer.volume         # a range: the first number is strip 1, the next strip 2 ...
cc 16-23 ch 1 = mixer.pan mode=sign step=0.02 layer=pan
cc 16-23 ch 1 = plugin.param mode=sign layer=plugin
bend ch 1-8 = mixer.volume         # a range of channels: pitch-bend faders
note 94 ch 1 = transport.play
cc 70 = channel.volume selected    # no "ch": any MIDI channel
note 36-51 ch 10 = pad.step        # 16 pads as steps 1-16
cc 9 ch 16 value 2 = layer plugin  # only when the data value is 2

[feedback]                         # <state> [strip] = <message> [options]
mixer.mute = note 16-23 ch 1       # lights: on = 127, off = 0 unless set
pad.step = note 36-51 ch 10 on=5 off=0
mixer.volume = bend ch 1-8         # motor faders
mixer.pan = cc 48-55 ch 1 ring=pan layer=pan

[display]
write = F0 00 00 66 14 12 {pos} {text} F7
width = 7                          # characters per cell
cells = 8                          # cells per row
row 1 = mixer.name layer=pan
row 2 = mixer.pan layer=pan
```

### `[device]`

| Key | Meaning |
|---|---|
| `name` | The name in the Controller type list. Required. |
| `maker` | The maker's name. |
| `source` | Where the map comes from: the maker's document and its address. One line per source. |
| `match` | Port-name patterns, separated by commas. The profile with the longest matching pattern wins; your own profile wins a tie. A profile without `match` is never detected, only chosen by hand. |
| `exclude` | Port-name patterns that are not this device. |
| `strips` | Strips per device, 1 to 64. Default 8. |
| `group` | Devices of the same group share the mixer bank, the page and the layer. Each device takes the next `strips` strips, in the order of the Input devices list. Used for a control surface and its extenders. |
| `layers` | Layer names, separated by commas. Must come before the lines that use them. |
| `extends` | The id (file name without `.lynxctl`) of a profile to start from. Lines after it add to that profile. `name`, `maker`, `match` and `exclude` are not inherited. |

### Messages

| Form | Meaning |
|---|---|
| `note N` | Note on. A note off, or a note on with velocity 0, counts as value 0 (a release). |
| `cc N` | Control change. |
| `bend` | Pitch bend, 14 bits. |
| `N-M` | A range of numbers. Element 1 is the first strip. |
| `ch C` | MIDI channel 1 to 16. In `[input]` it may be left out (any channel); in `[feedback]` it is required. |
| `ch C-D` | A range of channels instead of a range of numbers. |
| `value V` | `[input]` only: the line applies to this data value only. |

### `[input]` actions

A strip is a number from 1, or `selected` (the selected mixer track or channel). With a range the strip is the
first element's.

| Action | Does |
|---|---|
| `transport.play` | Starts playback. While playing: pauses. |
| `transport.stop` | Stops and returns to where playback started. While stopped: goes to the start. |
| `transport.record`, `transport.loop`, `transport.metronome`, `transport.tap` | Record, pattern / song mode, metronome, tap tempo. |
| `transport.rewind`, `transport.forward` | One bar back / forward. |
| `transport.jog` | A relative control moves the song position by beats. |
| `edit.undo`, `pattern.previous`, `pattern.next` | Undo; previous / next pattern. |
| `mixer.volume`, `mixer.pan` | A strip's fader and pan. |
| `mixer.mute`, `mixer.solo`, `mixer.arm`, `mixer.select` | A strip's switches (a press toggles) and selection. |
| `mixer.touch`, `master.touch` | A touch-sensitive fader is held: LYNX does not move its motor until release. |
| `master.volume` | The Master fader. |
| `mixer.bank.left`, `mixer.bank.right` | Move the bank by the group's strip count. |
| `mixer.strip.left`, `mixer.strip.right` | Move the bank by one. |
| `channel.volume`, `channel.pan`, `channel.mute`, `channel.select` | A Channel Rack channel. |
| `channel.bank.left`, `channel.bank.right` | Move the channel bank. |
| `channel.previous`, `channel.next` | Select the previous / next channel. |
| `plugin.param` | A parameter of the selected channel's instrument (strip = the knob on the page). |
| `plugin.page.left`, `plugin.page.right` | The previous / next page of parameters. |
| `pad.note` | Plays a note on the selected channels: `key=` is the first element's note. |
| `pad.step` | Toggles a step of the selected channel in the current pattern. |
| `layer <name>` | Switches to a layer. |
| `ignore` | Takes the message and does nothing. For buttons LYNX has no use for: without a line they would play as notes or controllers. Lines are read top to bottom and the first match wins, so a catch-all such as `cc 0-127 ch 16 = ignore` goes last. |

Options:

| Option | Meaning |
|---|---|
| `mode=abs` | The value is the position (default). |
| `mode=sign` | Relative: bit 6 set = down, bits 0-5 = ticks (Mackie Control V-Pots, jog wheel). |
| `mode=twos` | Relative: 1-63 = up, 127-65 = down. |
| `mode=offset` | Relative: 65 and above = up, 63 and below = down. |
| `step=0.01` | Relative controls: the change per tick, as a part of the full range. |
| `pickup=on` / `off` | Soft takeover for an absolute control. On by default; off by default for `bend` (motor faders). |
| `layer=<name>` | The line applies only while this layer is active. |
| `key=36` | `pad.note`: the first element's note number. |

### `[feedback]` states

`transport.play`, `transport.stop` (lit while stopped), `transport.record`, `transport.loop` (lit in pattern
mode), `transport.metronome`, `mixer.volume`, `mixer.pan`, `mixer.mute`, `mixer.solo`, `mixer.arm`,
`mixer.select`, `master.volume`, `channel.volume`, `channel.pan`, `channel.mute`, `channel.select`,
`plugin.param`, `pad.step`, `layer <name>`.

Options:

| Option | Meaning |
|---|---|
| `on=127`, `off=0` | The values sent for a lit and a dark light (a colour index on devices with colour pads). |
| `ring=dot`, `pan`, `wrap`, `spread` | A Mackie-style encoder ring: one LED, from the centre, filled from the left, spread. |
| `layer=<name>` | Sent only while this layer is active. |

A position is sent as 0 to 127, or for `bend` as 14 bits with 10 bits of resolution. LYNX sends only what
changed. A strip with nothing behind it (past the last insert) is dark and shows no text.

### `[display]`

| Key | Meaning |
|---|---|
| `write` | The message that writes text: hex bytes and the place holders `{pos}` (row × cells × width + cell × width), `{row}` and `{cell}` (both from 0) and `{text}`. |
| `width` | Characters per cell. |
| `cells` | Cells per row. |
| `row N = <text>` | What row N (from 1) shows; `layer=<name>` at the end limits it to a layer. |

Texts with one value per cell: `mixer.name`, `mixer.volume`, `mixer.pan`, `channel.name`, `channel.volume`,
`channel.pan`, `plugin.param.name`, `plugin.param.value`. Texts for the whole row: `mixer.selected.name`,
`channel.selected.name`, `plugin.name`, `layer.name`, or a fixed text in quotes.

Text is sent as plain ASCII: accented Latin letters lose their accent, other characters become `?`.

## How it fits together (for developers)

- `src/core/ControllerProfile.h`: the file's model, the parser, the library and detection by port name.
- `src/core/ControllerSession.h`: `Surface` (LYNX as a controller sees it), `Session` (one device running one
  profile), `Hub` (all devices; groups), `Claims` (the quick test on the MIDI thread).
- `src/engine/ControllerIo.h`: the device side. It takes a profile's messages out of the normal MIDI path, queues
  them to the message thread and owns the output ports.
- `src/ui/ControllerBridge.h`: the app's `Surface`. Values go through the same path as MIDI links; buttons run the
  main window's actions.
- Tests: `tests/Controller190awTests.inc` feeds device bytes into the layer and checks the calls on a fake
  `Surface` and the bytes sent back.
  `tests/ControllerProfiles190awTests.inc` does the same with each built-in profile and checks detection by
  port name.

The next phases of the plan (the local control API of issue #75, FL-style device scripts) use the same `Surface`.

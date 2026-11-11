---
title: MIDI controllers
summary: Play notes from a keyboard and link hardware knobs to controls.
status: full
---
![The MIDI page of the settings: input devices, output devices with master sync, and external sync](/assets/img/wiki/settings-midi.webp)

LYNX SHARE takes notes and controller moves from any MIDI input device. The devices are set up on the MIDI page of the settings: `Options › MIDI settings...`, or `F10`. Without hardware, the typing keyboard and the touch keyboard play notes too.

## Connect a keyboard

1. Plug the keyboard in and open `Options › MIDI settings...` (`F10`).
2. Find it under `Input devices` and tick it. If it is not listed, click `Refresh device list`.
3. Leave the two boxes on `Any MIDI channel` and `Plays the selected channels`.
4. Close the settings and select a channel in the [Channel rack](/wiki/channel-rack/).
5. Play. The selected channel sounds.

## Input devices

| Setting | What it does |
|---|---|
| Tick box | Turns the device on or off |
| `Any MIDI channel` | Listens to every MIDI channel, or to one channel only |
| `Plays the selected channels` | Notes go to the channels selected in the Channel rack, or always to one fixed channel |
| `Velocity` | `As played`, `Soft (light playing sounds louder)`, `Hard (needs harder playing)` or `Fixed (every note at full velocity)` |
| `Performance mode channel` | Notes on this MIDI channel launch Playlist clips while Performance mode is on |
| `Song marker jump` | Notes on this MIDI channel move the song to a marker instead of playing |

## Link a knob to a control

1. Right-click the knob or fader in LYNX SHARE.
2. Choose `Link to MIDI controller (move a knob on the controller)`.
3. Move the knob or fader on your hardware. The link is made.

The link is saved with the project. To remove it, right-click the control again and choose `Unlink controller`. The menu item shows the CC number in use.

| Command | Key | What it does |
|---|---|---|
| `Override global link...` | none | Makes a link that works in every project. It is kept in the settings, not in the project |
| `Remove global link` | none | Removes it again |
| `Multilink to controllers` | `Ctrl+J` | While it is on, the controls you move are linked, in order, to the hardware controls you move next. It is in the `Options` menu |
| `Pickup (takeover mode)` | none | On the MIDI page. A controller moves a parameter only after reaching its current value, so nothing jumps |

The MIDI page lists the links under `Links (this project, then global)`. `Controller type` on the same page reads `Generic controller`.

A third-party plugin's parameters are reached through `Automate plugin parameter` in the channel's menu or the effect slot's menu. Each parameter there has the same link items. See [Automation](/wiki/automation/).

## The typing keyboard

`Ctrl+T`, or `Options › Typing keyboard to piano`, turns the computer keyboard into a piano. The rows from `Z` to `M` and from `Q` to `P` play the selected channels.

- While it is on, letter and number keys are notes. The single-letter tool keys of the Piano roll and the Playlist work only while it is off.
- It always plays at the Piano roll's velocity. The `Velocity` setting on the MIDI page is for MIDI devices.
- The same switch is in `Options › General settings...`.

## The touch keyboard

![The touch keyboard: octave buttons, key size, velocity mode and a row of piano keys from C4](/assets/img/wiki/touch-keyboard.webp)

`View › Touch keyboard` (`Alt+F7`) opens a piano you play with the mouse or a touch screen. It plays the channel selected in the Channel rack, and shows its name at the right.

| Setting | What it does |
|---|---|
| `-` and `+` | One octave down or up |
| Key size | `Small keys`, `Medium keys` or `Large keys` |
| `Velocity: where you touch` | Near the top of a key plays soft, near the bottom loud |
| `Velocity: fixed` | Every note at the velocity set with the slider |
| `Note names` | Shows the names on the keys |

## MIDI sync

`Output devices` on the MIDI page sends the transport to hardware and other software. Tick `Send master sync` for a device and choose `MIDI clock` (with Start, Continue, Stop and Song Position Pointer) or MIDI Time Code. `Offset (ms)` sends the sync later with a positive value and earlier with a negative one. `Send all notes off` sends that message to the device on init and after an export.

`External clock sync` works the other way: LYNX SHARE follows the MIDI clock of the input you choose, with its tempo, Start, Continue, Stop and Song Position Pointer. `External sync offset (milliseconds)` plays LYNX SHARE later with a positive value and earlier with a negative one. `Status` shows whether a clock is running.

## Good to know

- Choosing an input for `External clock sync` turns that device on under `Input devices`.
- A tempo moved by a linked MIDI controller does not resize audio clips. Type or set the final tempo instead.
- To record what you play, see [Recording](/wiki/recording/). Incoming notes can be snapped to a scale in the [Piano roll](/wiki/piano-roll/).
- MIDI CC, channel aftertouch and pitch bend sent to a plugin can be automated with `Automate MIDI controller (CC, aftertouch, pitch bend)`.

## Related

- [Recording](/wiki/recording/)
- [Automation](/wiki/automation/)
- [Channel rack](/wiki/channel-rack/)
- [Settings](/wiki/settings/)
- [Shortcuts](/wiki/shortcuts/)

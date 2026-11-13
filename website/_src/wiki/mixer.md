---
title: Mixer
summary: Inserts with faders, effect slots, sends and sidechain routing.
status: full
---
![The Mixer](/assets/img/mixer.webp)

Every channel plays through a Mixer insert. Each insert has a fader, pan, mute and solo, a peak meter, ten effect slots with a dry/wet amount each, and a built-in EQ. All inserts end up in the Master unless you route them elsewhere. Press `F9` to show or hide the Mixer.

A project can have up to 127 inserts. The Mixer has an add strip for a new one. The box in the Mixer's title bar picks one of eight strip layouts, from `Compact` to `Extra large`.

## Put an effect on a channel

1. In the [Channel rack](/wiki/channel-rack/), select the channel and press `Ctrl+L`. The channel goes to a free insert, which takes its name and colour.
2. Press `F9` to show the Mixer and click that insert's strip.
3. In the effects panel, click an empty slot and pick an effect.
4. Click the slot's name to show or hide the effect's window.
5. Click the slot's light to bypass the effect and compare.

## Send a channel to an insert

In the Channel rack, drag the channel's route box (or use the wheel on it) to pick the insert number. Double-clicking the box shows that insert in the Mixer.

`Ctrl+L` in the Channel rack works on several selected channels at once. Each goes to its own free insert.

With the Mixer focused, `Ctrl+L` links the selected channels to the selected insert, and `Ctrl+Shift+L` links them to consecutive inserts starting from it.

## Select and move inserts

- Click a strip to select it.
- `Ctrl`+click, or drag across strips, selects a range. `Ctrl+Shift`+click adds or removes one.
- Drag a strip sideways, or press `Alt+Left` and `Alt+Right`, to move the selected inserts.
- Right-click a strip for the insert menu. `F2` renames and colours the selected insert.
- `Del`, or `Delete` in the insert menu, deletes the selected inserts. LYNX SHARE asks first. Tick `Remember my choice` to skip the question next time.

Moving, inserting or deleting inserts keeps sends, sidechains and automation on their insert.

The insert menu's `Mixer` section and `Options › Mixer` have two more commands. `Arrange inserts like the Playlist` moves the inserts linked to Playlist tracks to the front, in the Playlist's order. With `Keep links to deleted inserts, greyed (LYNX)` on, the sends that went into a deleted insert stay, greyed, so you can connect them again.

## Mute and solo

Click the mute light to mute an insert. Right-click or `Ctrl`+click it to solo. `S` solos the selected inserts, and `Alt+S` solos them together with the inserts that feed them.

A muted insert keeps its meter moving. In new projects and in imported FL Studio projects it also keeps running its effects, and only its output to the Master and its sends is cut. To stop a muted insert from processing, turn on `Muted inserts don't process (LYNX, saves CPU; this project)` in the insert menu's `Mixer` section or in `Options › Mixer`. Its meter then stops and its effects start fresh when you unmute it.

## Effects

The effects panel shows the ten slots of the selected insert.

- Drag a slot, or use the wheel over it, to move it up or down.
- Right-click a slot for its options (replace, move, delete).
- While a slot's menu is open, `Del` deletes the plugin in that slot.
- `Group into Audio Effect Rack` in the slot's menu puts the effect into a new rack, and `Group all into Audio Effect Rack` does it for the whole chain. `Ungroup` puts a one-chain rack's effects back into the slots.
- The switch in the panel's header turns all effects of the insert on or off.
- You can drag an effect from the [Browser](/wiki/browser/)'s Plugin database onto an insert or an effect slot.
- `Clean voice` in the insert menu puts a ready vocal clean-up chain on the insert.

## Edison in a slot

Edison, the audio editor and recorder, can sit in any effect slot, several per insert. Its window is the audio editor with a row of recording controls.

- `Open audio editor` (`Ctrl+E`) in the insert menu loads Edison into the insert's first empty slot and opens it. `Open audio editor and record` (`Shift+E`) loads it ready to record when the song plays.
- The record modes are `Now`, `On input` (the default: it waits for the input to pass a threshold), `Input` and `On play`. `Append` puts a new recording after the audio already there. The maximum recording time is `Forever` or 1, 3, 5, 10, 30 or 60 minutes: a new slot starts at 30, `Open audio editor and record` sets 5.
- `Slave` plays Edison's audio at the song's time while the song plays.
- Edison keeps recording after you close its window.
- `Tools › Macros › Remove all Edison instances` empties every Edison slot in the project, after a question.
- Edison slots in FL Studio projects (.flp) open with their audio.

## Insert EQ

Each insert has its own EQ. Drag a band to set its frequency and level, use the wheel for the width, and double-click a band to put it back to 0 dB. Right-click the EQ's title to bypass it. If you want a Mixer with no built-in EQ, new projects can be set to start with a clean mixer.

## Routing, sends and sidechain

![The Mixer with the Kick insert selected, its sends drawn as cables under the strips and the effects panel on the right](/assets/img/wiki/mixer-sends.webp)

Select the insert you want to route. Every other strip then has a route switch:

- Click the switch on another strip to send the selected insert there. Dragging after enabling sets the send level.
- `Ctrl`+click routes to that insert only.
- `Shift`+click makes a sidechain send. `Ctrl+Shift`+click makes it sidechain only.
- Right-click opens the same choices as a menu.
- For a pre-fader send, right-click the route switch or the send's level knob and tick `Pre-fader send (not moved by this track's fader)`. The send is then marked `PRE`.

Effects with a side input, such as a compressor, have a `Sidechain from` picker that lists the inserts to listen to. Picking one creates the route for you.

## Groups and VCA faders

Mixer groups fold with their chevron (or `Alt`+click for all groups). A VCA fader moves the level of its member inserts while they keep their own faders. Create one from the insert menu: under Group, choose `VCA › New VCA from selected`.

## Recording

Each insert has a record-arm button. With an insert armed, Rec and Play record its audio input into a Playlist clip. `Shift+R` arms the selected insert and starts recording in one step. See [Recording](/wiki/recording/).

When you pick an external input for an insert that also records other audio, LYNX SHARE asks whether to switch it to `External input only`, so the take holds the input alone. `Mixer › Disk recording › Latency compensation` in the insert menu is on by default. Turned off, recorded takes are placed with no latency compensation at all.

## Keys

`Ctrl+L` and `Ctrl+Shift+L` have this meaning only while the Mixer has focus.

| Command | Key | What it does |
|---|---|---|
| Link channels | `Ctrl+L` | Links the selected channels to the selected insert |
| Link to consecutive inserts | `Ctrl+Shift+L` | Links the selected channels to inserts starting from the selected one |
| Move inserts | `Alt+Left` / `Alt+Right` | Moves the selected inserts |
| Rename and colour | `F2` | Renames and colours the selected insert |
| Solo | `S` | Solos the selected inserts |
| Solo with sources | `Alt+S` | Solos the selected inserts and the inserts that feed them |
| Delete inserts | `Del` | Deletes the selected inserts, after a question |
| Arm and record | `Shift+R` | Arms the selected insert and starts recording |
| Open audio editor | `Ctrl+E` | Loads Edison into the insert's first empty slot and opens it |
| Open audio editor and record | `Shift+E` | The same, ready to record when the song plays |

## Good to know

- `Loudness meter beside the Master (LYNX)`, in `Options › Mixer`, shows the short-term and integrated loudness (LUFS) and the true peak of the Master's output, with a `Reset` button.
- An instrument plugin with several outputs can play each output on its own insert. See [Third-party plugins](/wiki/plugins/).
- `Ctrl+L` does different things in the Channel rack and in the Mixer. In the rack it picks free inserts. In the Mixer it uses the selected insert.
- The route switches appear on the other strips, not on the selected one.
- The limit is 127 inserts per project and ten effect slots per insert.
- Edison's recording is kept as a WAV file in the project's folder, not inside the project file.
- The Session mixer under the clip grid belongs to the Clip-launch workflow only. It shows the same inserts as this Mixer. See [Layouts](/wiki/layouts/).

## Related

- [Channel rack](/wiki/channel-rack/)
- [Recording](/wiki/recording/)
- [Automation](/wiki/automation/)
- [Third-party plugins](/wiki/plugins/)
- [Instruments and effects](/instruments-and-effects/)

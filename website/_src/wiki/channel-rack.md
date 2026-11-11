---
title: Channel rack
summary: Step sequencer with one row per instrument, editing the current pattern.
status: full
---
![The Channel rack with step rows](/assets/img/rack.webp)

The Channel rack lists every instrument (channel) in the project, one per row, and shows the steps of the current pattern. Press `F6` to show or hide it. Right-clicking the Pattern button in the transport also shows it.

## Make a drum pattern

1. Press `F6` to show the Channel rack.
2. Click the `+ add channel` box below the last row and pick an instrument from the menu. You can also double-click a sample in the [Browser](/wiki/browser/) to get a new sampler channel.
3. Click steps in the channel's row to switch them on. Drag to draw a run of steps.
4. Right-click a step to erase it.
5. Press `Space` to play. In Pattern mode the pattern loops.
6. Drag the row's volume knob to set the level.

## Program steps

- Click a step to switch it on. Dragging draws a run of steps. If the drag starts on a lit step, it erases instead.
- Right-click or right-drag erases.
- Clicking after the pattern's end adds a step and makes the pattern longer.
- A row whose notes are not plain steps (other keys, longer notes, slides) shows a miniature of the notes. Clicking it opens them in the [Piano roll](/wiki/piano-roll/).
- You can drop audio files on the rack to make channels.

The channel menu (right-click the channel's name) has `Fill each 2 steps`, `Fill each 4 steps`, `Fill each 8 steps`, `Advanced fill`, and `Rotate left` and `Rotate right`.

## The graph lane

![The graph lane open under the Kick channel, with one velocity bar per step](/assets/img/wiki/rack-graph.webp)

`Ctrl+K` shows the graph lane under the selected channel. The buttons at its left pick what it edits: Velocity, Pan, Release, Fine pitch, Mod X or Mod Y. Dragging sets a value per step, right-drag draws a ramp, and `Alt`+click resets a step.

## The parts of a row

| Part | What it does |
|---|---|
| Mute light | Click mutes. `Ctrl`+click or right-click solos. |
| Pan and volume knobs | Drag or use the wheel. |
| Route box | Drag or wheel picks the Mixer insert. Double-click shows that insert in the Mixer. Right-click takes the next free insert. |
| Name button | Click opens or closes the channel's editor. `Alt`+click keeps the other editors open. `Ctrl`+click auditions. Drag up or down reorders. Right-click opens the channel menu. |
| Selector | Click selects one channel. `Ctrl`+click or right-click adds or removes. `Shift`+click selects a range. Double-click selects all. |

## Patterns

The rack always edits the current pattern. The rack's pattern selector shows its name: click the name for the list, or use the arrows. `Shift`+wheel over the rack's title also changes the pattern.

The rack's pattern length box sets the length in bars (drag it or use the wheel). Double-click it for an automatic length taken from the notes.

The swing knob in the main toolbar applies to all channels. `Swing mix` in the channel menu sets how much one channel follows it.

| Command | Key | What it does |
|---|---|---|
| Next pattern | `Numpad +` | Makes the next pattern the current one |
| Previous pattern | `Numpad -` | Makes the previous pattern the current one |
| Next empty pattern | `F4` | Goes to the next empty pattern and asks for a name |
| Rename and colour | `F2` | Renames and colours the current pattern |

## Playing notes from the PC keyboard

`Ctrl+T` turns the typing keyboard into a piano: `Z` to `M` and `Q` to `P` play the selected channels. While it is on, the single-letter shortcuts of the editors are paused.

## Keys

These work while the Channel rack has focus.

| Command | Key | What it does |
|---|---|---|
| Select channel | `Up` / `Down` | Selects the channel above or below |
| Extend selection | `Shift+Up` / `Shift+Down` | Adds the channel above or below to the selection |
| Move channels | `Alt+Up` / `Alt+Down` | Moves the selected channels up or down |
| Select all | `Ctrl+A` | Selects all channels |
| Cut, copy, paste | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Cuts, copies or pastes the selected channels' steps |
| Rotate steps | `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Rotates the selected channels' steps |
| Clone | `Alt+C` | Clones the selected channels |
| Delete | `Alt+Del` | Deletes the selected channels |
| Group | `Alt+G` | Puts the selected channels in a new filter group |
| Route to Mixer | `Ctrl+L` | Sends the selected channels to free Mixer inserts |
| Mute | `1` to `0` | Mutes or unmutes channels 1 to 10 |
| Solo | `Ctrl+1` to `Ctrl+0` | Solos channels 1 to 10 |
| Graph lane | `Ctrl+K` | Shows or hides the graph lane |

## Freeze a channel

Freezing renders a channel to audio and plays that audio in its place. The instrument is unloaded, a third-party plugin too, so it uses no CPU and no memory.

1. Right-click the channel and choose `Freeze`. With several channels selected the item reads `Freeze selected channels`.
2. The row gets a snowflake tag. The channel's volume, pan and the effects of its insert still work as before.
3. To change the notes or the instrument again, right-click the channel and choose `Unfreeze`. The instrument comes back with the settings it had.

A frozen channel cannot be edited: its steps are locked and its instrument window does not open. If the notes, the tempo or the arrangement change after the freeze, the tag changes colour and the audio made before keeps playing until you choose `Re-freeze`. If the rendered audio files are missing, the instrument plays instead.

Freeze works on channels only. A Mixer insert cannot be frozen.

## Other things in the channel menu

`Piano Roll`, `Rename and color`, `Replace` (swap the instrument), `Clone`, `Delete`, `Arpeggiator`, `Loop point`, `Burn MIDI to`, `Sort channels`, `Select unused channels` and `Channel settings`.

## Good to know

- Only the `+ add channel` box opens the add menu.
- The rack has no pattern of its own. Changing the current pattern anywhere changes what the rack shows.
- A row that shows a miniature of notes cannot be edited as steps. Use the Piano roll for it.
- The number keys reach the first ten channels only.
- The keys in the table need the rack to have focus. Click the rack first.

## Related

- [Piano roll](/wiki/piano-roll/)
- [Playlist](/wiki/playlist/)
- [Mixer](/wiki/mixer/)
- [Browser](/wiki/browser/)
- [Shortcuts](/wiki/shortcuts/)

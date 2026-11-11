---
title: Channel rack
summary: Step sequencer with one row per instrument, editing the current pattern.
status: full
---
![The Channel rack with step rows](/assets/img/rack.webp)

The Channel rack lists every instrument (channel) in the project, one per row, and shows the steps of the current pattern. Press `F6` to show or hide it. Right-clicking the Pattern button in the transport also shows it.

## Add a channel

Click the `+ add channel` box below the last row and pick an instrument from the menu. Only that box opens the add menu. You can also drop audio files on the rack, or double-click a sample in the [Browser](/wiki/browser/) to get a new sampler channel.

## Program steps

- Click a step to switch it on. Dragging draws a run of steps. If the drag starts on a lit step, it erases instead.
- Right-click or right-drag erases.
- Clicking after the pattern's end adds a step and makes the pattern longer.
- A row whose notes are not plain steps (other keys, longer notes, slides) shows a miniature of the notes. Clicking it opens them in the [Piano roll](/wiki/piano-roll/).

The channel menu (right-click the channel's name) has `Fill each 2 steps`, `Fill each 4 steps`, `Fill each 8 steps`, `Advanced fill`, and `Rotate left` and `Rotate right`.

`Ctrl+K` shows the graph lane under the selected channel. Dragging in it sets a value per step, right-drag draws a ramp, and `Alt`+click resets a step.

## The parts of a row

| Part | What it does |
|---|---|
| Mute light | Click mutes. `Ctrl`+click or right-click solos. |
| Pan and volume knobs | Drag or use the wheel. |
| Route box | Drag or wheel picks the Mixer insert. Double-click shows that insert in the Mixer. Right-click takes the next free insert. |
| Name button | Click opens or closes the channel's editor. `Alt`+click keeps the other editors open. `Ctrl`+click auditions. Drag up or down reorders. Right-click opens the channel menu. |
| Selector | Click selects one channel. `Ctrl`+click or right-click adds or removes. `Shift`+click selects a range. Double-click selects all. |

## Patterns

The rack always edits the current pattern. The rack's pattern selector shows its name: click the name for the list, or use the arrows. `Shift`+wheel over the rack's title also changes the pattern. `Numpad +` and `Numpad -` go to the next and previous pattern, `F4` goes to the next empty one and asks for a name, and `F2` renames and colours it.

The rack's pattern length box sets the length in bars (drag it or use the wheel). Double-click it for an automatic length taken from the notes.

The swing knob in the main toolbar applies to all channels. `Swing mix` in the channel menu sets how much one channel follows it.

## Playing notes from the PC keyboard

`Ctrl+T` turns the typing keyboard into a piano: `Z` to `M` and `Q` to `P` play the selected channels. While it is on, the single-letter shortcuts of the editors are paused.

## Keys

These work while the Channel rack has focus.

| Key | Action |
|---|---|
| `Up` / `Down` | Select the channel above or below |
| `Shift+Up` / `Shift+Down` | Extend the selection |
| `Alt+Up` / `Alt+Down` | Move the selected channels |
| `Ctrl+A` | Select all channels |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Cut, copy, paste the selected channels' steps |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Rotate the selected channels' steps |
| `Alt+C` | Clone the selected channels |
| `Alt+Del` | Delete the selected channels |
| `Alt+G` | Put the selected channels in a new filter group |
| `Ctrl+L` | Send the selected channels to free Mixer inserts |
| `1` to `0` | Mute or unmute channels 1 to 10 |
| `Ctrl+1` to `Ctrl+0` | Solo channels 1 to 10 |

## Other things in the channel menu

`Piano Roll`, `Rename and color`, `Replace` (swap the instrument), `Clone`, `Delete`, `Arpeggiator`, `Loop point`, `Burn MIDI to`, `Sort channels`, `Select unused channels` and `Channel settings`.

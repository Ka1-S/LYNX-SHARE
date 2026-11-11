---
title: Piano roll
summary: Edit the notes of one channel in the current pattern.
status: full
---
![The Piano roll](/assets/img/pianoroll.webp)

The Piano roll shows the notes of one channel in the current pattern, with a keyboard on the left and a lane for per-note values at the bottom. Press `F7` to show or hide it, or choose `Piano Roll` in a channel's menu in the [Channel rack](/wiki/channel-rack/).

The channel box in the toolbar picks which channel you edit. Channels that have notes in this pattern are marked. The wheel over the box steps through channels with notes, `Ctrl`+wheel through all of them.

## Tools

Each tool has a letter key. The letters work while the typing keyboard piano (`Ctrl+T`) is off.

| Tool | Key | What it does |
|---|---|---|
| Pencil | `P` | Click adds a note, drag positions it |
| Brush | `B` | Drag paints a run of notes. With `Shift`, one at a time |
| Drum brush | `N` | One hit per grid cell on the key under the mouse |
| Eraser | `D` | Click or drag deletes |
| Mute | `T` | Click or drag mutes or unmutes notes |
| Knife | `C` | Drag a line to cut across notes, click cuts at that point |
| Lasso | `E` | Drag draws a selection box. `Shift` adds |
| Zoom | `Z` | Click zooms in, drag a range to zoom to it, right-click zooms out |
| Listen | `Y` | Click or drag plays the notes under the cursor |

## Working with notes

- Drag a note to move it. `Shift`+drag makes a copy.
- Drag a note's end to resize it. The start can be dragged too, with the end staying in place.
- Right-click deletes a note, in any tool.
- Double-click a note to open its properties.
- `Alt`+wheel over a note changes its velocity.
- `Ctrl`+drag selects. `Ctrl`+click adds a note to the selection or removes it.
- Hold `Alt` while dragging to ignore the snap. `Backspace` turns snap off and on.
- With several notes selected, the stretch handle stretches or squeezes them in time.

## Keyboard and ruler

Click a key on the keyboard to hear it. Further right on the key is louder. `Ctrl`+click a key selects its notes. Middle-drag on the keyboard makes the keys taller or shorter.

Click the ruler to set the position. `Ctrl`+drag, right-drag or double-click-drag on the ruler selects the notes in a time range. `Ctrl`+wheel zooms and `Shift`+wheel scrolls.

## Toolbar

- **Snap**: "Main" follows the snap in the main toolbar, or pick one for this window.
- **Chord**: clicking creates the chosen chord, with the clicked note as the root. `Shift+N` goes back to single notes.
- **Scale**: highlights the notes of a scale. A separate button snaps new and moved notes, and incoming MIDI, to the chosen scale.
- **Slide** (`S`): new notes are slide notes. The note that is sounding glides to them (808 glide).
- **Ghost notes** (`Alt+V`): shows the other channels' notes behind yours.
- **Note colour**: new notes get this colour. There are 16.
- **Follow**: the view scrolls with the playhead while the pattern plays.
- **Lane box**: chooses what the bottom lane edits (velocity, pan and so on).

## Keys

| Key | Action |
|---|---|
| `Ctrl+A` | Select all |
| `Ctrl+D` or `Esc` | Deselect |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy, cut, paste |
| `Ctrl+B` | Duplicate the selection |
| `Del` | Delete the selection |
| `Up` / `Down` | Move the selection a semitone |
| `Ctrl+Up` / `Ctrl+Down` | Move the selection an octave |
| `Left` / `Right` | Move the selection one grid step |
| `Ctrl+Q` | Quick quantize (starts and lengths) |
| `Shift+Q` | Quantize starts |
| `Ctrl+U` | Chop the notes to the grid |
| `Ctrl+G` | Join touching notes |
| `Ctrl+L` | Legato |
| `Alt+M` / `Shift+Alt+M` | Mute, unmute the selection |
| `Ctrl+M` | Import a MIDI file into this channel |
| `Ctrl+Shift+M` | Export the pattern as a MIDI file |
| `PageUp` / `PageDown` | Zoom in, out |

The note tools (Arpeggiate, Strum, Flip, Randomise, Quantize, Chop and others) open dialogs. Their keys are on the [Shortcuts](/wiki/shortcuts/) page.

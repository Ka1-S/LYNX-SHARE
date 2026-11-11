---
title: Your first song
summary: From an empty project to an exported audio file, step by step.
status: full
---
This page walks once through the whole path: a drum pattern, a melody, an arrangement, a mix and an exported file. Each part links to the page that covers its window in full. If you hear nothing at any point, see [Audio setup](/wiki/audio-setup/).

## Make a drum pattern

1. Choose `File › New from template › Empty`. The project starts with five channels, Kick, Clap, Hat, Snare and Synth, and no steps.
2. Press `F6` if the [Channel rack](/wiki/channel-rack/) is not showing.
3. Click some steps on the Kick row. Dragging draws several, and right-click erases.
4. Press `Space`. The transport is in Pattern mode, so the pattern plays in a loop. Press `Space` again to stop.
5. Give the Clap, Hat and Snare rows their own steps.
6. Press `F2` to name and colour the pattern.

To add a sound that is not there, click the `+ add channel` box under the last row and pick an instrument from the menu. To use a sample of your own, open the [Browser](/wiki/browser/) with `Alt+F8`, click an audio file to hear it and double-click it to add it as a new channel.

## Write a melody

1. Press `F4`. LYNX SHARE goes to the next empty pattern and asks for its name.
2. Right-click the Synth channel's name and choose `Piano Roll`.
3. With the Pencil (`P`), click in the grid to add a note.
4. Drag a note to move it and drag its end to change its length. Right-click deletes a note.
5. Press `Space` to hear the pattern while you edit.

`Ctrl+T` turns the typing keyboard into a piano, so you can try notes on `Z` to `M` and `Q` to `P` first. Turn it off again before you use letter shortcuts such as `P`.

## Arrange the patterns

1. Press `F5` to show the [Playlist](/wiki/playlist/).
2. In the Picker panel on the left of the Playlist, click the drum pattern. It is now the clip source.
3. With the Pencil (`P`), click on a track to place the pattern. Click again further right to place it again, or use the Brush (`B`) and drag to paint clips end to end.
4. Click the melody pattern in the Picker and place it on another track.
5. Press `L` to switch to Song mode, then `Space`. The Playlist plays from the song position. Click the ruler to set it.

## Mix it and add an effect

1. Press `F9` to show the [Mixer](/wiki/mixer/). The five starting channels already play through inserts 1 to 5, and a channel added from the `+ add channel` menu takes a free insert. The number in the channel's route box in the Channel rack says which one.
2. To move channels to free inserts of their own, select them in the Channel rack and press `Ctrl+L`. Each insert takes the channel's name and colour.
3. Drag an insert's fader to set its level.
4. Click an insert to select it. The effects panel on the right shows its ten slots.
5. Click an empty slot and pick an effect. Click the slot's name to show or hide the effect's window. The slot's light bypasses the effect.

## Export the song

![The export window: range and tail on the left, format, bit depth and sample rate on the right, mix options below](/assets/img/wiki/export.webp)

1. Press `Ctrl+S` and save the project first.
2. Choose `File › Export › Wave file...`. For other formats the same menu has `MP3 file...`, `OGG file...` and `FLAC file...`.
3. Set `Range` to `Song (Playlist)`. The other choices are `Current pattern` and `Playlist selection`.
4. Leave the other options as they are.
5. Click `Export...`, choose the file name and wait for the render.

## What each key did

| Command | Key | What it does |
|---|---|---|
| Channel rack | `F6` | Shows or hides the Channel rack |
| Next empty pattern | `F4` | Goes to the next empty pattern and names it |
| Piano roll | `F7` | Shows or hides the Piano roll |
| Playlist | `F5` | Shows or hides the Playlist |
| Pattern or Song mode | `L` | Switches what `Space` plays |
| Mixer | `F9` | Shows or hides the Mixer |
| Send to free inserts | `Ctrl+L` | Gives the selected channels their own Mixer inserts |
| Save | `Ctrl+S` | Saves the project as `.lynx` |
| Export | `Ctrl+R` | Opens the export window with the last used format |

## Good to know

- In Pattern mode you hear only the current pattern. If the song is silent after arranging, check that the transport is in Song mode.
- The export window starts on `Song (Playlist)` when the transport is in Song mode and the Playlist has clips. Otherwise it starts on `Current pattern`.
- The metronome click is never part of an export.
- The foot of the export window shows the song's length in bars, its total time and the disk space the file needs.
- By default a new project has a Limiter on the Master insert. It is part of what you hear and what you export.
- Undo is `Ctrl+Z`. Pressed again it redoes that step. `Ctrl+Alt+Z` goes further back.

## Related

- [A tour of the window](/wiki/tour/)
- [Exporting](/wiki/exporting/)
- [Automation](/wiki/automation/)
- [Recording](/wiki/recording/)
- [Instruments and effects](/instruments-and-effects/)

---
title: Playlist
summary: Arrange pattern, audio and automation clips on tracks to build the song.
status: full
---
![The Playlist with clips on tracks](/assets/img/playlist.webp)

The Playlist is where patterns become a song. It holds three kinds of clips on its tracks: pattern clips, audio clips and automation clips. Press `F5` to show or hide it, and `Enter` to maximize or restore it.

The transport has two modes. Pattern mode plays the current pattern in a loop. Song mode plays the Playlist. Press `L` to switch.

## Place clips

The Picker panel lists the patterns, audio and automation of the project. Click one to make it the clip source.

1. Pick the Pencil (`P`).
2. Click on a track to place the Picker's clip, and drag to position it.
3. With the Brush (`B`), drag to paint clips end to end.

Audio files can be dragged from the [Browser](/wiki/browser/) onto the Playlist.

## Tools

The letters work while the typing keyboard piano (`Ctrl+T`) is off.

| Tool | Key | What it does |
|---|---|---|
| Pencil | `P` | Click places the Picker's clip |
| Brush | `B` | Drag paints clips end to end |
| Eraser | `D` | Click or drag deletes |
| Mute | `T` | Click or drag mutes or unmutes clips |
| Knife | `C` | Drag a line to cut across clips, click cuts at that point |
| Lasso | `E` | Drag draws a selection box. `Shift` adds |
| Zoom | `Z` | Click zooms in, drag a range to zoom to it, right-click zooms out |
| Listen | `Y` | Click or drag plays from that point |
| Slip | `S` | Drag slides the clip's content inside the clip |

## Working with clips

- Drag a clip to move it. `Shift`+drag makes a copy.
- Drag a clip's end to resize it. Dragging the start trims it while the content stays in place.
- Right-click deletes a clip. `Ctrl`+drag selects.
- Double-click a clip to open it.
- The button at a clip's top left opens the clip menu (make unique, rename and colour, chop, and for audio: pitch, fit to tempo, reverse).
- Hold `Alt` while dragging to ignore the snap.
- Audio clips have fade handles and a clip gain you drag up or down.
- `Shift+M` switches Stretch: resizing an audio clip then stretches the audio instead of cutting it.

## Tracks

In a track's header, click selects, `Ctrl`+click adds or removes, `Shift`+click selects a range and right-click opens the track menu. `Shift`+wheel moves the track up or down. The track's light mutes it (click) or solos it (`Ctrl`+click or right-click). Drag the divider between two tracks to resize one, or `Shift`+drag to resize all.

## Ruler, loop and markers

- Click the ruler to set the song position.
- Right-drag, `Ctrl`+drag or double-click-drag on the ruler makes a time selection, which is also the loop. `Shift`+drag moves it.
- `Ctrl+Ins` inserts the time selection's length across all tracks, `Ctrl+Del` removes it.
- `Alt+T` adds a marker. Click a marker to jump to it.
- `Ctrl`+wheel zooms, `Alt`+wheel changes the track height.

## Automation

Right-click a knob or fader and choose `Create automation clip`. The clip appears on the Playlist. With the Pencil, drag its points, and click on the curve to add one. Automation lanes under tracks are optional and off by default.

## Arrangements

The arrangement box in the toolbar switches between arrangements. A project can have several.

## Clip-launch layout

`View › Layout style › Clip-launch workflow` adds a grid of clips and scenes next to the Playlist. It is optional. The Pattern workflow is the default.

## Keys

| Key | Action |
|---|---|
| `Ctrl+A` | Select all |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy, cut, paste |
| `Ctrl+B` | Duplicate |
| `Del` | Delete the selected clips |
| `Shift+Del` | Delete the selected clips and close the gaps |
| `Shift+S` | Ripple editing on or off |
| `Alt+M` / `Shift+Alt+M` | Mute, unmute the selected clips |
| `Ctrl+Shift+V` | Make the selected clips unique |
| `Ctrl+G` | Merge pattern clips |
| `PageUp` / `PageDown` | Zoom in, out |
| `Shift+4` | Zoom to show everything |

## Known limits

From the 0.1.25 release notes: with several overlapping automation clips on one control, the choice of which one plays can differ from FL Studio's. A tempo moved by a linked MIDI controller does not resize audio clips. On Linux, video needs FFmpeg.

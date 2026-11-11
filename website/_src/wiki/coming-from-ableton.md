---
title: Coming from Ableton Live
summary: The optional Clip-launch layout, opening and exporting .als sets, and warp modes.
status: full
---
LYNX SHARE starts in the Pattern workflow (Channel rack, patterns and a Playlist). The Clip-launch workflow is optional and adds a clip grid with scenes beside the Playlist. LYNX SHARE is not affiliated with Ableton.

![The Clip-launch layout: a clip grid with scenes beside the Playlist](/assets/img/cliplaunch.webp)

## Turning on the Clip-launch layout

1. Choose `View › Layout style › Clip-launch workflow`.
2. Use the `Grid / Both / Song` switch in the Playlist's title bar to show the grid, the Playlist, or both.

`View › Layout style › Pattern workflow (default)` goes back. The same choice is in `Options › General settings`. Patterns and the Channel rack stay available in both layouts.

## The clip grid

Playlist tracks are the columns and scenes are the rows. A slot holds a pattern clip or an audio clip.

- A track plays one clip at a time. A launched clip replaces the one playing.
- A launch waits for the next step of the launch quantisation: `Global`, `None`, 8 bars down to 1 bar, or a note value from 1/2 to 1/32. Each clip can have its own.
- Launch modes: `Trigger`, `Gate`, `Toggle` and `Repeat`.
- `Legato` starts the new clip at the position the replaced clip had reached. `Loop` is per clip.
- An empty slot can have a stop button, so launching a scene stops that track there.
- Scenes can be inserted, deleted and renamed, and a scene can set the tempo when it is launched.

A track that launched a clip stays silent in the Playlist until you press `Back to Playlist`. An option gives tracks back when the transport stops.

## Follow actions

Each clip has an Action A and an Action B with a chance for each, run after a number of bars or passes of the clip. The actions are `No action`, `Stop`, `Play again`, `Previous`, `Next`, `First`, `Last`, `Any` and `Other`. A switch turns follow actions on or off for the whole grid.

## Opening a .als

`File › Open...` reads sets saved by Live 11 and Live 12.

- Imported: MIDI, audio, group and return tracks, tempo, time signature, locators, the loop, mixer settings (volume, pan, mute, solo, sends) and their automation.
- VST2 and VST3 plugins come over with their state, if the same plugin is installed. See [Plugins](/wiki/plugins/).
- Session clips and scenes go into the clip grid with their launch mode, quantisation, legato, loop, follow actions and stop buttons. To place them on the Playlist instead, one section per scene, change `Session clips of Live sets` in `Options › General settings`.
- Warp markers are kept.

Live's own devices are not recreated. Simpler, Sampler and drum racks become LYNX SHARE samplers. Racks keep only their first chain. Live's own effects are dropped. The import report lists what was left out.

`File › Save` then asks where to write a `.lynx` file.

## Exporting a .als

1. Choose `File › Export › Ableton Live set (.als)...`.
2. Under `Save for`, pick `Live 12` or `Live 11 (also opens in Live 12)`.
3. Under `Samples`, copy them into the set's project folder or refer to them where they are.
4. Choose a folder. The set is saved as `<name> Project/<name>.als`.

What is written:

- A MIDI track per channel, with its notes as MIDI clips.
- Audio tracks with stretch, transpose, gain, fades and warp mode.
- Groups, returns and sends.
- VST2 and VST3 plugins with their state.
- Markers, and volume, pan and tempo automation.
- The clip grid, as Session clips and scenes.

What is not written:

- LYNX SHARE's own instruments and effects are not converted. Their tracks and notes are kept.
- Automation of effect parameters and insert-to-insert routes are dropped.
- Live trims audio clips that overlap.

A report after the export lists what was left out.

## Warp modes

`Channel settings › Time stretch` has four warp modes modelled on Live's: `Beats`, `Tones`, `Texture` and `Complex`. Warp markers from a Live set are kept on import and written back on export.

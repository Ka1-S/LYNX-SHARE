---
title: Layouts
summary: Switch between the Pattern workflow and the optional Clip-launch workflow.
status: full
---
![The Clip-launch workflow: a grid of clips and scenes on the left, the Picker and the Playlist on the right](/assets/img/cliplaunch.webp)

`View › Layout style` has three entries: `Pattern workflow (default)`, `Clip-launch workflow` and `Simple workflow`. The same choice is in `Options › General settings`, under Workflow. The choice is kept when you close the program.

The Pattern workflow is the Channel rack, patterns and the Playlist. The Clip-launch workflow adds a clip grid with scenes beside the Playlist. Patterns and the Channel rack stay available.

## Launch clips in the grid

1. Choose `View › Layout style › Clip-launch workflow`. The clip grid appears next to the Playlist.
2. Right-click an empty slot, choose `Put a pattern here` and pick a pattern. Double-clicking an empty slot puts the current pattern there.
3. Click the triangle at the left of the clip to launch it. If the transport is stopped, it starts.
4. Click the triangle of a scene, in the `Scenes` column, to launch every clip in that row.
5. Click `Stop all clips` to stop them.
6. Click `Back to Playlist` to let the tracks play their Playlist clips again.

## What changes on screen

- The clip grid is shown beside the Playlist. Playlist tracks are the columns and scenes are the rows.
- The Playlist's title bar gets a `Both`, `Grid`, `Song` switch. `Grid` shows only the grid, `Song` only the Playlist.
- The first time, the Channel rack is hidden. `F6` or its toolbar button shows it.
- The Playlist's automation lanes are switched on.
- The window layout is stored per style, so each style comes back the way you left it.
- Under the grid, the `Mixer` button shows or hides the Session mixer, as in Ableton Live: `In/Out` (`Audio From`, `Monitor`, `Audio To`), `Sends`, `Track Delay`, and each track's volume with its activator, solo, arm button and meter, plus the `Main` strip.
- `Grooves`, under the grid, shows the Groove Pool. `Commit groove` writes a groove into the notes of a MIDI clip.
- The `MIDI` and `Key` buttons start MIDI Map Mode and Key Map Mode. See [MIDI controllers](/wiki/midi-controllers/).

## The grid

A launched clip does not start at once. It waits for the next launch quantisation point, and its triangle blinks until then. The box at the bottom of the grid sets the global launch quantisation, which every clip set to `Global` uses. A stop waits in the same way.

Right-click a clip for its settings.

| Setting | What it does |
|---|---|
| `Launch quantisation` | When this clip starts: `Global`, `None`, `8 bars` down to `1/32` |
| `Launch mode` | `Trigger`, `Gate`, `Toggle` or `Repeat` |
| `Legato (keep the position)` | The clip takes over the play position of the clip that played before on that track |
| `Loop` | On: the clip repeats. Off: it plays once, then the track stops |
| `Follow action` | What happens after the clip: two actions with a chance each, and the time after which they act |
| `Delete` | Empties the slot |

Right-click an empty slot for `Stop button (a scene stops this track here)`. Clicking the square at the left of an empty slot stops that track. Right-click a scene for `Insert scene`, `Add scene below`, `Delete scene`, `Rename scene...` and the scene's tempo.

Clips can be cut, copied, pasted and duplicated (`Ctrl+D`) from their menu, and dragged between slots and to or from the Playlist. A pattern or a sample dragged from the Browser, or audio files dropped from outside, fill the slot they land on. A scene's menu also has `Duplicate` and `Capture and insert scene`, which puts the clips playing now into a new scene. `View › Scene tempo and time signature` shows a tempo and time signature for each scene.

The slot menu also has two options for the whole grid: `Give tracks back to the Playlist when the transport stops` and `Follow actions on the whole grid`.

| Command | Key | What it does |
|---|---|---|
| Move | Arrow keys | Selects another slot |
| Launch clip | `Enter` | Launches the selected clip |
| Launch scene | `Ctrl+Enter` | Launches the selected scene |
| Delete clip | `Del` | Empties the selected slot |
| Draw Mode | `B` | A drag in an automation lane draws steps as wide as the grid, or freehand with snap off or `Alt` held, as in Ableton Live. Also in `Options` |

## Ableton Live sets

When you open a Live set, its Session clips and scenes go into the clip grid, with their launch settings. `File › Export › Ableton Live set (.als)` writes the grid back as Session clips. `Options › General settings` has `Session clips of Live sets`, where `Playlist, after the arrangement` places them on the Playlist instead.

## The Simple workflow

`View › Layout style › Simple workflow` is one window with the tracks and the timeline, for a first song. `View › Track dock` shows a dock under the timeline for the selected track, with the `Effects`, `Instrument`, `Keyboard` and `Editor` tabs. In this workflow, the `Help` menu has `First-song tour` and `Welcome screen...`, which offers a seven-step first song, a demo song or an empty project. See [Simple layout](/wiki/simple-layout/).

`Lyrics and notes` and `Reference match...` in the `View` menu, and `Clean voice` in a Mixer insert's menu, are there in every layout. In the Simple workflow, `Clean voice` is in a track's menu too.

## Switch back

Choose `View › Layout style › Pattern workflow (default)`. The window layout you had in the Pattern workflow comes back, and the automation lanes return to the setting they had before. The clips in the grid stay in the project.

## Good to know

- A track that launched a clip stays silent in the Playlist until you press `Back to Playlist`. The option `Give tracks back to the Playlist when the transport stops` does it for you at every stop.
- The new keys of the grid (`B`, `Q`, `Ctrl+M`, `Ctrl+K` and others) act this way in the Clip-launch workflow only. In the Pattern workflow they keep FL Studio's meaning.
- A scene in 6/8 plays a bar of three beats, because the meter counts quarter notes.
- Follow actions exist for clips, not for scenes.
- Automation lanes switched by hand while in the Clip-launch workflow are not kept when you go back.
- With the Pattern workflow selected, nothing of the grid is loaded.

## Related

- [Playlist](/wiki/playlist/)
- [Coming from Ableton Live](/wiki/coming-from-ableton/)
- [Importing projects](/wiki/importing-projects/)
- [What only LYNX does: workflows](/why/#workflows)

The third entry is one window for a first song. See [Simple layout](/wiki/simple-layout/).

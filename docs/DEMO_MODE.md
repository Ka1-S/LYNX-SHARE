# LYNX `--demo`: the automation mode for tutorial videos

`LYNX --demo` starts LYNX SHARE for scripted recordings, such as the tutorial videos. It is a developer feature. It
is off by default, it appears in no menu, and normal starts never use it.

With `--demo`:

- **Remote Control is on** for this run, whatever the setting says. See [REMOTE_CONTROL.md](REMOTE_CONTROL.md) for
  discovery, authentication and the project operations.
- **The "Remote control active" strip is hidden**, so it never appears in a recorded frame.
- **No splash card, no start-up sound, no update check and no support note**, so a take starts on the window itself.
- **The Simple layout's starter beat is the same in every take.** Its Loop Starter plan uses a fixed seed, so the
  English and Portuguese recordings show the same beat.
- **The API gains view operations** (below). They let a script click a control by its component ID and read where the
  control is on screen. The pipeline then places the cursor, the click ripple, the zoom and the callouts from those
  rectangles. Scripts hold no screen coordinates, so one script works in every language and at every window size.

A fresh profile (no `ui-options.txt`, no `setup-done.txt`) also shows the first-run welcome card, so a video can start
from it. Add `--lang pt-BR` (or any other language id) to record the interface in that language.

## View operations

The operations are sent like any other Remote Control operation (`{"op": …, "args": {…}}`, after the handshake). They
are view operations, so they need no `expectedProjectId` / `expectedEpoch` / `expectedRevision` guards. A click can
still change the project, exactly as the user's own click would. `api.describe` lists them while `--demo` is on.

A **target** is a control's component ID, such as `welcome.choice.mobile`, `simple.addTrack`, `simple.loops` or
`next3.do.addLoop`. If no control has that ID, the target is matched against the control's name, then against its
button text (which is translated, so prefer IDs). Only controls that are showing are found. `ui.list` prints the IDs
that are on screen now.

Since round 190pg a target can also be:

- **`menu:<English text>`**: an item of an open menu, or a title of the menu bar (`menu:View`, `menu:Layout style`,
  `menu:Pattern workflow (default)`). The English text is the one in the code (the `tr ("…")` source), so the same script
  works in every language: the item's text is compared with the text and with its translation, without a trailing `...`
  and the shortcut after the tab; an item that ends in a count, such as `Create scenes from sections (3)`, also matches
  `Create scenes from sections`. `ui.click` on a menu bar title opens its menu, on an item with a submenu opens the
  submenu, on any other item runs it (the menu closes). `ui.hover` lights the item as the mouse would.
- **a control in another LYNX window** (a plugin editor, a tool window, a dialog): when the main window has no control
  with that ID or name, the other windows are searched too. `ui.editor` gives the editor window it opens the ID `editor`.

| Operation | Arguments | Result |
|---|---|---|
| `ui.window` | — | `screen` (the main window's rectangle), `scale` (the display's scale), `layout`, `language` |
| `ui.find` | `target`, `cell` (optional `{ "row", "column" }`: one cell of a control made of cells, such as the Simple Drum Machine, `simple.dock.drumMachine`; row 0 is the bottom row) | `found`, and `control`: `id`, `name`, `kind` (button, toggle, combo, slider, text, label, component), `text`, `on`, `enabled`, `showing`, `screen` (rectangle on screen, logical pixels), `window` (rectangle in the window) |
| `ui.list` | `under` (optional target), `limit` (1–2000, default 500) | `controls`: the showing controls that have a component ID, described as by `ui.find` |
| `ui.click` | `target`, `cell` (optional, as for `ui.find`: the cell is pressed and released through the control's own mouse handling, so a step toggles), `double` (optional), `right` (optional: a right-click, e.g. a clip grid slot's menu; round 190pg), `at` (optional `{ "x": 0..1, "y": 0..1 }`, a point in the control, or in the cell, as fractions of its size) | `clicked`, and `control` as it was before the click. A button runs its own click, as when its shortcut is pressed. A combo box opens its menu. Any other control receives a press and a release, delivered to the component itself. No input reaches the operating system |
| `ui.hover` | `target` | `hovered`, `control`. An open menu's item (`menu:…`) lights as under the mouse; for any other control only its rectangle is returned (round 190pg) |
| `ui.open` | `file` (an absolute path) | `opened`. Opens a project or a file LYNX imports (`.lynx`, `.flp`, `.als`, `.flm`, `.dawproject`, `.blx`, `.band`, modules…) as File › Open does after its file dialog. The open project is not asked about (round 190pg) |
| `ui.action` | `id` (a key-map action, `src/core/KeyMap.cpp`, e.g. `view.commandPalette`, `view.tracker`, `view.detailDevices`) | `ran`. Runs the action as its shortcut does; an id that runs nothing is an error (round 190pg) |
| `ui.editor` | `channel` (index) | `channel`, `screen` (the new window's rectangle). Opens the channel's editor (its instrument's window), as a click on its name in the Channel rack does; the newest such window gets the component ID `editor` (round 190pg) |
| `ui.place` | `target` (a control in a window of its own, or the window's ID: `editor`, `modulators`), `x`, `y` (screen pixels) | `screen`. Moves that window, kept on the screen (round 190pg) |
| `ui.polynotes` | — | `notes`: the open polyphonic editor's notes, one line each: `k midi start-end [edit]` (`k` is the note's cell column of `polyEditor`) (round 190pg) |
| `ui.layout` | `style`: `pattern`, `clipLaunch` or `simple` | `layout`. The same runtime switch as View › Layout style |
| `ui.select_track` | `track` (Playlist track index) | `track`. In the Simple layout the dock shows that track |
| `ui.show` | `channel` (optional, with `pianoRoll`: that channel is selected first and the Piano roll opens on its notes; round 190pg), `fit` (optional, with `playlist`: the whole song in view; round 190pg), `panel`: `channelRack`, `pianoRoll`, `playlist`, `mixer`, `browser`, `loops` (the Simple layout's Loops dock), `welcome` (the welcome card), `gettingStarted` (the Next-3 card), `dock` (the Simple dock of the selected track) | `shown` |
| `ui.welcome` | `choice`: `mobile`, `fl`, `live`, `timeline`, `new` or `skip` | Applies that choice of the welcome card, as a click on it does |
| `ui.clipmenu` | `clip` (the clip's `index` in the project snapshot's `playlist`) or `clipId` (its stable id, the snapshot's `playlist[].clipId`: 16 hex digits) | Selects the clip and opens its context menu anchored at the clip, as a right-click on it does, with no mouse needed. `clip`, `screen` (the clip's rectangle on screen, so a script can point at it), `menu`. Needs the Playlist showing (`ui.show` `playlist`) and the clip in view. The menu is not a window of the main window, so a script clicks its items with the keyboard path of its video, not with `ui.click` |
| `ui.polyeditor` | `channel` (index), `channelId` (the snapshot's `channels[].channelId`), or `clip` / `clipId` (the channel that clip plays) | Opens the polyphonic note editor ("Edit notes (polyphonic)...") of an audio clip channel, as the clip menu item does. `channel`, `screen` (the editor window's rectangle on screen). Refused for a channel that is not an audio clip with a loaded sample, or that an ARA plugin edits |
| `session.start` | `name` (optional) | Starts an Online Session as host (guests are let in without the question). **Local test relay only**: `LYNX_RELAY_URL` must be a loopback URL such as `http://127.0.0.1:8765`, otherwise the request is refused |
| `session.status` | — | `state` (idle, starting, connecting, knocking, loading, live, reconnecting, waitingHost, ended), `inviteCode`, `status`, `host` |

Errors look like any other Remote Control error: `{"ok": false, "error": "No control showing with the ID or name …"}`.

### Cells of the editors (round 190pg)

`ui.find` and `ui.click` take `"cell": { "row", "column" }` on these controls too. A cell scrolled out of view is an error.

| Target | Row | Column |
|---|---|---|
| `channelRack` | the rack's row (0 = the top channel) | the step (0 = the first) |
| `pianoRoll` | the MIDI key (60 = C5) | the 16th step from the pattern's start |
| `playlist` | the Playlist track (0 = the first) | the 16th step from the song's start (16 = bar 2) |
| `mixer.strips` | 0 the fader, 1 the mute switch, 2 the meter | the Mixer track (0 = Master) |
| `clipGrid` | the scene (-1: the column's stop button, -2: the column's title) | the grid's column (-1: the scene's launch button) |
| `playlist.sectionsLane` | (any) | a section's block (0 = the first; -1: the "+" button that adds one) |
| `chordHelper` | 0 the related keys, 1 the chords | the row of that list (0 = the first) |
| `polyEditor` | (any) | a note's blob in the polyphonic editor (its number `k`, as `ui.polynotes` lists them) |

A click on a cell is the control's own click there: a Channel rack step toggles, the Piano roll's Draw tool draws a note,
the Playlist's Draw tool places the selected pattern, a clip grid slot launches or selects its clip.

`LYNX --demo190pgcheck <prefix> <project.lynx>` runs these operations against a real window (on a private X server) and
writes `<prefix>-checks.txt`; run it with `--lang pt-BR` too.

### Operations of tutorial batch 2 (round 190pl)

These answer like the others (no guards). Some of them change the project or write files, exactly as the menu items they
stand for do; `mixer.route` is one undo step.

| Operation | Arguments | Result |
|---|---|---|
| `ui.export` | `format` (`wav`, `mp3`, `ogg`, `flac`, `aiff`, `m4a`), `path` (optional, absolute), `run` (optional, default true) | Opens the Export window of File › Export › *format* file... (ID `export.window`). With `path`, its `Export...` button (`export.start`) writes that file without the system's file dialog (once). `run: false` only arms the path, for a script that opens the window through the real menu. `opened`, `screen` |
| `file.export` | `format`, `path` | The same render without a window, here and now (the song in Song mode, else the pattern; the window's remembered choices): `file`, `bytes`, `seconds`, `peakDb` |
| `file.save` | `path` (optional, absolute), `newVersion` (optional), `run` (optional) | Save (the project's own file), Save as `path` without the file dialog, or Save new version (`Song_2`...). `path` with `run: false`: the next File › Save as... writes it. `file`, `name`, `modified` |
| `midi.input` | `events`: `[{ "type": "note", "note", "velocity" (1–127), "at" (s from now), "length" (s), "channel" (1–16) }, { "type": "cc", "cc", "value", "at" }]` | Plays the virtual input **LYNX demo keyboard**, which `--demo` lists in Options › MIDI like a plugged-in device. Its notes go through that device's MIDI settings (on / off, MIDI channel, what it plays), recording and Map Mode as a keyboard's do. `device`, `messages`, `seconds` |
| `plugin.scan` | `folders` (optional: absolute, existing), `add` (optional, default true), `status` (optional) | Scans the folders in the background as the Plugin manager's Find new plugins does, and adds them to the plugin folders unless `add` is false (no folders: the plugin folders). `status: true` answers `running`, `progress`, `found`, `finished`, `known` instead |
| `ui.drag` | `from`, `to` (each `{ "target", "cell"?, "at"?, "dx"?, "dy"? }`, as ui.click's, `dx` / `dy` pixels from there), `mods` (optional: `["ctrl", "shift", "alt"]`), `seconds` (optional, default 0.6) | A press, moves and a release delivered **through the window** (JUCE's `ComponentPeer::handleMouseEvent`, as the system's mouse events arrive; nothing reaches the operating system): a drag-and-drop (a loop onto the timeline, a plugin picture onto a slot) drops where it ends, a clip moves, a knob or an EQ band turns. Answers at once with `from` / `to` (screen points); the drag takes its time |
| `ui.controlmenu` | `target` (`param:...`), or `channel` / `insert` (+ `slot`) with `parameter` (an index, `volume` or `pan`) | A right-click on the control that sets that parameter in any open LYNX window: the knob's own menu opens (Create automation clip, Link to controller, Modulate with...) where the pointer is, as the knob's does. `screen` (the knob) |
| `ui.type` | `target` (a text field), `text`, `interval` (optional, ms per character, default 70), `replace` (optional) | Types the text into the field one character at a time, as the keyboard would (the field takes the focus; its listeners see each change; no key event reaches the system): `typing`, `seconds` |
| `ui.repaint` | — | Paints every LYNX window again (`repainted`): a private X server without a compositor can keep a closed menu's pixels on screen |
| `mixer.route` | `from` (an insert), `to` (a track, 0 = Master), `kind` (`route`, `routeOnly`, `sidechain`, `sidechainOnly`; default `route`), `level` (optional, linear 0–1.25) | The route arrow's menu items in one undo step: `sends`, `toMaster` |

Also new in round 190pl:

- **`"cell": { "row", "name" }`**: on controls that can look their cells up by what they show (the Browser's lists, the
  Plugin picker's tiles, the Plugin manager's rows), the cell that shows that name.
- **`param:` targets** for `ui.find`, `ui.click` and `ui.drag`: a control by the parameter it sets —
  `param:channel.<c>.<p>` (an instrument parameter), `param:channel.<c>.volume` / `.pan`, `param:insert.<t>.<slot>.<p>`
  (an insert effect's parameter), `param:insert.<t>.volume` / `.pan`. Found in every open window: a knob tagged with its
  parameter, or the part of an effect editor that answers with it.
- **`ui.click` `peer: true`** (with `mods` optional): the press and the release go through the window, so whatever is on top
  at that point gets them (a list's row, a table's cell).
- **`ui.editor` `insert` + `slot`**: an insert effect's editor window, ID `editor`.
- **`ui.show`** `pluginManager` and `versions`.
- **Cells**: `mixer.strips` row 3 = a track's route arrow, 4 = its fader's cap where it is now, 5 = its pan knob, 6 = its solo
  switch, 7 = its header (a click selects the track); `mixer.slots` (the Mixer's insert slots, column = the slot); `browser.tree` / `browser.results` (the Browser's and
  the Loops dock's lists, column = the row; `"name"` finds a row by its label); `automationEditor` (an automation clip's editor: row 0 = point *column*, row 1 = the grid at 16th *column*, top to bottom =
  its value range); `pluginChooser` (the Mixer slot's More plugins... window: column = the row shown, `"name"` by the plugin's
  name); `pluginPicker` (row 0 = a tile, column = its
  place or `"name"`; row 1 = a category tab); `versions` (row = the list's row);
  `pluginManager` (row = the table's row shown); `facetEq` (row 0 = band *column*'s node; row 1 = the listen switch,
  column 0 the EQ, 1 the source, 2 the target, 3 the Compare menu; row 2 = the chosen band's shape menu; rows 76–124 = the graph at *column* Hz and *row − 100* dB).
- **IDs**: the Export window `export.window`, `export.format.<wav|mp3|ogg|flac|aiff|m4a>`, `export.start`, `export.cancel`,
  `export.range`, `export.mp3Bitrate`; the Versions window `versions`, `versions.list`, `versions.open`, `versions.show`,
  `versions.label`, `versions.newVersion`; the Plugin manager `pluginManager`, `pluginManager.search`, `.list`, `.scanNew`,
  `.rescanAll`, `.stop`, `.folders`, `.addFile`, `.status`, `.filter.<all|instruments|effects|favourites|failed>`;
  Options › MIDI's inputs `midi.input.<n>` with `.enabled`, `.channel`, `.plays`; Facet EQ's editor `facetEq`; the Browser's
  search box `browser.search`; the Mixer's insert slots `mixer.slots`; the plugin chooser `pluginChooser` with `.search`,
  `.instruments`, `.effects`, `.manage`, `.scan`, `.add`; the F8 Plugin picker `pluginPicker`; an automation clip's editor
  `automationEditor`.

`LYNX --demo190plcheck <prefix>` runs these against a real window (on a private X server) and writes
`<prefix>-checks.txt` (files in `<prefix>-files/`); run it with `--lang pt-BR` too.

### Useful component IDs

| Where | IDs |
|---|---|
| Welcome card | `welcome.card`, `welcome.choice.mobile`, `welcome.choice.fl`, `welcome.choice.live`, `welcome.choice.timeline`, `welcome.choice.new`, `welcome.skip`, `welcome.close`, `welcome.more`, `welcome.language`, `welcome.test` |
| "Next: 3 things to try" | `next3.card`, `next3.<item>` (a row), `next3.do.<item>` (its button), `next3.close`, `next3.hide`, `next3.tutorials`. Items: `play`, `addLoop`, `recordVoice`, `steps`, `openFlp`, `plugins`, `slotClip`, `launchScene`, `openAls`, `instrumentTrack`, `recordAudio`, `openMixer` |
| Empty-state hints | `empty.playlist`, `empty.clipGrid`, `empty.simple` (the cards); buttons `empty.channelRack`, `empty.demo`, `empty.open`, `empty.browse`, `empty.addTrack`, `empty.loops`, `empty.tour` |
| Tips | `tip.songModeEmpty`, `tip.recordNoInput` (the cards); `tip.action`, `tip.gotIt`, `tip.noMore` |
| Transport | `toolbar.play`, `toolbar.stop`, `toolbar.record`, `toolbar.patternMode`, `toolbar.songMode`, `toolbar.tempo`, `toolbar.key` |
| Instruments (round 190pg) | `model.models` (LYNX Model's six models, marimba first, equal widths: use `at`), `model.bar` (its strike point), `model.hardness`, `model.position`, `model.velocity`, `model.noise`, `model.damping`, `model.resonator`, `model.motor`, `model.release`, `model.tone`, `model.volume`; `helix.<parameter index>` (every control of Helix's editors) |
| Editors (round 190pg) | `toolbar.cpu` (the CPU panel; a double click opens the Plugin performance monitor), `palette` (the command palette), `modulators` (its window), `modulators.add`, `modulators.assign.<n>` (a modulator's Assign switch), `clipLaunch.both`, `clipLaunch.grid`, `clipLaunch.song` (the Clip-launch view switch), `channelRack`, `pianoRoll`, `playlist`, `mixer.strips`, `clipGrid` (with `cell`), `editor` (the window `ui.editor` opened), `tracker`, `tracker.edit` (its Edit switch), `playlist.sectionsLane`, `chordHelper`, `polyEditor`, `polyEditor.play`, `polyEditor.original` |
| Simple layout (round 190dv) | `simple.dock.drumMachine` (with `cell`), `simple.addTrack`, `simple.loops`, `simple.lyrics`, `simple.automation`, `simple.tile.drums`, `simple.tile.instrument`, `simple.tile.voice`, `simple.starters`, `simple.dropLane`, `simple.finish`, `simple.dock.*` |

Run `ui.list` against a running build for the full, current list.

## The video pipeline

The tools are in `/mnt/e/lynx-work/onboard190np/tools` (round 190np; the 190nj prototype is in
`/mnt/e/lynx-work/onboard190nj/tools`). Everything runs on a private X server and a private PipeWire with only a null
sink, so nothing touches the desktop and no sound reaches the speakers.

```sh
T=/mnt/e/lynx-work/onboard190np/tools
$T/xrun.sh start && $T/pw.sh start        # Xvfb :219 1920x1200 + openbox; PipeWire with a null sink only
$T/take.sh np-en1 script.json rec-en/      # a fresh first-run profile, LYNX --demo, the window fitted to 1920x1080 at 0,0
                                           # (the X pointer rests below it, so nothing shows a hover), director.py, LYNX closed
$T/take.sh np-pt1 script.json rec-pt/ --lang pt-BR
python3 $T/post.py script.json rec-en/ out/name-en --voice ~/opt/piper-voices190nj/en_US-joe-medium.onnx
python3 $T/post.py script.json rec-pt/ out/name-pt-BR --lang pt-BR --texts script.pt-BR.json --voice ~/opt/piper-voices190nj/pt_BR-cadu-medium.onnx
$T/pw.sh stop; $T/xrun.sh stop
```

In a script, an action names its target instead of coordinates:

```json
{ "t": 6.0, "do": "ui.click", "target": "welcome.choice.mobile" }
{ "t": 12.0, "do": "point", "target": "simple.loops" }
{ "t": 15.0, "do": "api", "op": "tempo.set", "args": { "bpm": 96 }, "guard": true, "target": "toolbar.tempo" }
{ "t": 18.0, "do": "ui.click", "target": "simple.dock.drumMachine", "cell": [3, 22] }
```

The sample script `simple-beat.json` (with `simple-beat.pt-BR.json`) in `/mnt/e/lynx-work/onboard190np/video` makes
"Make a beat in the Simple layout" from a first run: the welcome card, the first choice, the starter beat, Play, a few
steps, the tempo, and the Next-3 card's Open Loops.

`director.py` asks `ui.find` for the target's rectangle just before each action and writes it into `events.json`.
`post.py` draws the cursor, the ripple and the callouts there, and its zoom keyframes can name a target too
(`["t", "target", zoom]`). Captions, titles, end cards and chapters come from the script, with one text file per
language.

### Vertical cut (TikTok, YouTube Shorts, Instagram Reels)

`vertical.py` makes a 1080×1920 (9:16) video from the same script and the same take as `post.py`:

```sh
python3 $T/vertical.py script.json rec-en/ out/name-en-vertical --voice-dir out/name-en-voice
python3 $T/vertical.py script.json rec-pt/ out/name-pt-BR-vertical --texts script.pt-BR.json --voice-dir out/name-pt-BR-voice
```

- **The cut:** the script's `vertical` block gives `from` / `to` in the take (at most 60 s), a `hook` title card for
  the first `hook_seconds` (title and one line), the `strip` title over the picture, and `endcard_from`. Captions,
  narration and chapters move with the cut; the per-language texts file may override `vertical.hook` and
  `vertical.strip`.
- **The picture:** a 1080×1080 (1:1) window on the 1920×1080 take that follows the action. When the script zooms, it
  follows the zoom's target (the `--demo` rectangle), otherwise the drawn cursor. The motion is smoothed, so the
  window never jumps. The cursor, the click ripples and the callouts are drawn in the window.
- **Safe areas:** the apps put their own controls along the top (~120 px), the right edge and the bottom (~380 px).
  The title strip sits under the top bar, and the captions sit in a box from x 60 to 940 under the picture that ends 390 px above the
  bottom.
- **Narration:** the WAVs `post.py` wrote for the horizontal video (`--voice-dir`), or Piper again (`--voice`).

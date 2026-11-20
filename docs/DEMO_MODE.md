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

| Operation | Arguments | Result |
|---|---|---|
| `ui.window` | — | `screen` (the main window's rectangle), `scale` (the display's scale), `layout`, `language` |
| `ui.find` | `target`, `cell` (optional `{ "row", "column" }`: one cell of a control made of cells, such as the Simple Drum Machine, `simple.dock.drumMachine`; row 0 is the bottom row) | `found`, and `control`: `id`, `name`, `kind` (button, toggle, combo, slider, text, label, component), `text`, `on`, `enabled`, `showing`, `screen` (rectangle on screen, logical pixels), `window` (rectangle in the window) |
| `ui.list` | `under` (optional target), `limit` (1–2000, default 500) | `controls`: the showing controls that have a component ID, described as by `ui.find` |
| `ui.click` | `target`, `cell` (optional, as for `ui.find`: the cell is pressed and released through the control's own mouse handling, so a step toggles), `double` (optional), `at` (optional `{ "x": 0..1, "y": 0..1 }`, a point in the control as fractions of its size) | `clicked`, and `control` as it was before the click. A button runs its own click, as when its shortcut is pressed. A combo box opens its menu. Any other control receives a press and a release, delivered to the component itself. No input reaches the operating system |
| `ui.layout` | `style`: `pattern`, `clipLaunch` or `simple` | `layout`. The same runtime switch as View › Layout style |
| `ui.select_track` | `track` (Playlist track index) | `track`. In the Simple layout the dock shows that track |
| `ui.show` | `panel`: `channelRack`, `pianoRoll`, `playlist`, `mixer`, `browser`, `loops` (the Simple layout's Loops dock), `welcome` (the welcome card), `gettingStarted` (the Next-3 card), `dock` (the Simple dock of the selected track) | `shown` |
| `ui.welcome` | `choice`: `mobile`, `fl`, `live`, `timeline`, `new` or `skip` | Applies that choice of the welcome card, as a click on it does |
| `session.start` | `name` (optional) | Starts an Online Session as host (guests are let in without the question). **Local test relay only**: `LYNX_RELAY_URL` must be a loopback URL such as `http://127.0.0.1:8765`, otherwise the request is refused |
| `session.status` | — | `state` (idle, starting, connecting, knocking, loading, live, reconnecting, waitingHost, ended), `inviteCode`, `status`, `host` |

Errors look like any other Remote Control error: `{"ok": false, "error": "No control showing with the ID or name …"}`.

### Useful component IDs

| Where | IDs |
|---|---|
| Welcome card | `welcome.card`, `welcome.choice.mobile`, `welcome.choice.fl`, `welcome.choice.live`, `welcome.choice.timeline`, `welcome.choice.new`, `welcome.skip`, `welcome.close`, `welcome.more`, `welcome.language`, `welcome.test` |
| "Next: 3 things to try" | `next3.card`, `next3.<item>` (a row), `next3.do.<item>` (its button), `next3.close`, `next3.hide`, `next3.tutorials`. Items: `play`, `addLoop`, `recordVoice`, `steps`, `openFlp`, `plugins`, `slotClip`, `launchScene`, `openAls`, `instrumentTrack`, `recordAudio`, `openMixer` |
| Empty-state hints | `empty.playlist`, `empty.clipGrid`, `empty.simple` (the cards); buttons `empty.channelRack`, `empty.demo`, `empty.open`, `empty.browse`, `empty.addTrack`, `empty.loops`, `empty.tour` |
| Tips | `tip.songModeEmpty`, `tip.recordNoInput` (the cards); `tip.action`, `tip.gotIt`, `tip.noMore` |
| Transport | `toolbar.play`, `toolbar.stop`, `toolbar.record`, `toolbar.patternMode`, `toolbar.songMode`, `toolbar.tempo`, `toolbar.key` |
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

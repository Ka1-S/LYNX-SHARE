# REA round 171c (v0.1.24) — discussion #10 (masoudnp, Windows, 0.1.23): Plugin picker + smooth fader

Date: 2026-11-04. The reporter's files: `/mnt/e/lynx-work/issues171/d10/` (shot.png = FL's Plugin picker, clip.mp4 =
9 s of LYNX 0.1.23 with a fader dragged while the song plays, sheet.jpg = frames). Working data:
`/mnt/e/lynx-work/picker171c/` (clip frames and spectrum in `clip/`, build logs in `logs/`, FL evidence in
`fl-evidence.md`, screenshots in the folder itself).

Markers: **[CLIP]** measured on the reporter's video, **[MEAS]** measured here, **[CODE]** LYNX source, **[REA]** FL's
binaries (clean-room), **[SHOT]** a screenshot looked at.

## 1. B — "Make smoother movement to volume knob when play"

### 1.1 What is not smooth in the clip [CLIP]

- Frames 50–169 of clip.mp4 cropped on insert 3's fader (`clip/tile2.png`): while the song plays and the fader is
  dragged, the cap, its tooltip and the dB read-out change only every **4–7 frames at 25 fps (~160–280 ms)**:
  −10.4, −10.4, −10.1, −10.1, −7.8 ×3, −5.6 ×4, −17.1 ×2, −22.4 ×5, −1.5 ×6, −9.7 ×7, −0.8 ×5, −7.4 ×6, −14.7 ×4…
  The mouse pointer itself keeps moving between those frames. So the whole message thread stalls between fader
  updates, i.e. the UI side, and the sound follows the same ~4–6 Hz staircase.
- The audio track (`clip/spec.png`): broadband vertical lines at the moments of the big steps (4.3, 4.5, 4.8, 4.9,
  5.1, 5.3 s) on top of the 16-step kick/clap loop: each staircase step is a fast level jump.

### 1.2 Cause [CODE]

- Every mouse move of a fader went `MixerStrips::setValue` → `AudioEngine::editSong` → `publishSong()`: stand-in
  projection, `RackState::build` of the whole snapshot (every channel, every insert, automation tables, PDC plan,
  mixer graph), the prefetcher update and `sendChangeMessage()`, after which the whole Mixer (and ~45 other
  listeners) repainted. On a real project that is tens of ms per move while playing, so Windows coalesces the mouse
  moves and the fader advances a few times a second.
- The engine's gain glide is already FL's: each coefficient of the level matrix moves at most 0.001 per sample
  (REA_rodada85 §3, `applyMatrix`). A step of 0.3 in gain takes ~7 ms: a staircase of 4–6 Hz with 7-ms ramps is the
  audible "steps". The glide itself is not the problem; the rate of new values is.

### 1.3 Fix (branch feature/fader171c)

- `LiveLevels` (core/RackRenderer.h): each snapshot carries its mixer volume / pan / separation and channel volume /
  pan as atomics; the renderer reads them at each block (`liveLevel`), the glide is unchanged.
- `AudioEngine::editLevels`: an edit that changes only levels saves the same undo step as editSong (one per press),
  writes the new values into the playing snapshot's live levels, bumps the song stamp and notifies — no rebuild. The
  full publish follows once the mouse is released (engine timer, `flushLevels`). Structural edits fall back to
  editSong. Used by the Mixer's fader / pan / separation drags (with the selected-strips group), the toolbar's master
  volume and the Channel Rack's volume / pan knobs.
- `MixerStrips::changeListenerCallback`: when no full publish happened since the last call (`getPublishCount`), only
  the strips whose volume / pan / separation changed are repainted.
- `MixerStrips` timer 30 → 60 Hz for the faders that follow automation (the meters keep their 30-Hz ballistics).

### 1.4 Measurements

(filled in below as they are run)

## 2. A — the Plugin picker (branch feature/picker171c)

FL evidence (static decompile of TPlugListForm in FLEngine_x64.dll + one FL session, shots s00–s27): the full list is in
`/mnt/e/lynx-work/picker171c/fl-evidence.md`. What LYNX copies, case by case:

| Case | FL (proof) | LYNX |
|---|---|---|
| Single left click on a tile | nothing (FormMouseDown 0xec7ed0 only stores the drag point; s03→s04) | nothing |
| Double-click / right-click / Enter | loads: generator = new channel, effect = first free slot of the selected insert, window opened (0xec7840; s10, s12, s07→s08) | same (`targets.addInstrument` / `addEffect`, the effect's window opens) |
| Drag | picker closes, Browser drag of the plugin with the tile picture; an effect brings up the Mixer (0xec85f0 → msg 0x556 → 0x11cae80; s14, s20) | same (`BrowserComponent::startPluginDrag`, `targets.showMixer`) |
| Drop on the Channel Rack | new last channel, window open (s15→s16) | same (the Browser's Plugin database drop; onto a channel it replaces the instrument, FL audit 165z-a) |
| Drop on a mixer strip | slot 1 of that insert, insert selected, window open (s20→s21) | same |
| Drop on the Playlist | nothing (s17→s18) | nothing |
| Hover | tile slightly bigger with a light frame + glow, dark label "name / category" (paint 0xec9870; s03, s07, s26) | same, in LYNX's red accent |
| Type to search | case-insensitive substring of the name, others dimmed not removed, pointer jumps to the first name starting with the text else the last containing it, 70 chars, Backspace / Ctrl+Backspace / Ctrl+V (0xec7370; s05, s06) | same (`pickerview::nameMatches`, `searchJump`, `dropLastWord`); the pointer is moved (`Desktop::setMousePosition`) |
| Keys | Esc closes, Left/PageUp, Right/PageDown = tab, Up/Down = next/previous tile wrapping, skipping dimmed (0xec7060; s07) | same (`stepWrapping`), F8 also closes |
| Wheel | one tab per notch; Ctrl+wheel = Up/Down (0xec8950; s26) | same; Shift+wheel scrolls a long tab |
| Right / middle click on empty space, focus lost | closes (0xec7ed0, FormDeactivate 0xec6400; s27) | same |
| Click a tab | closes and opens the Browser on that Plugin database category (s22→s23) | same; LYNX's Browser has no per-category folders yet, so it opens Generators / Effects when the folder is not there |
| Pointing at a tab | shows that tab (s24→s25) | same |
| Tile pictures | screenshots of each plugin's editor at one common scale for both halves (s02) | LYNX's own editors drawn by LYNX (`--pluginthumbs`), one scale for both halves; never FL's images |

LYNX's own parts: the pictures are drawn by a background copy of LYNX (`LYNX --pluginthumbs <cache>/plugin-thumbs/<version>`)
the first time the picker opens, so the picker opens at once with plain tiles that turn into pictures as they arrive.
VST / VST3 / CLAP plugins get a tile with their maker and name until their editor has been open once; then
`PluginWindow` captures it (Windows, PrintWindow PW_RENDERFULLCONTENT) into `<cache>/plugin-thumbs/plugins`.

## 3. Measurements

### 3.1 Picker [MEAS] (Windows 11, 1920×1080, LYNX test mode, fresh profile)

- `--pluginthumbs`: 163 pictures (50 generators, 113 effects) in **12.6 s** in the background process, 16–60 ms each.
- uiperf on oxygen.flp (playing), first build: F8 shown in **222 ms** (building the view + first paint, the pictures
  not there yet). The frames while it stayed open were slow (mean 78 ms, pointing 108 ms): the hovered tile's picture was
  refitted at high quality on every hover change (the cache held one size and the hovered tile is 8 % bigger) and the
  dimmed window behind was a full-window scaled draw per frame. Fixed: the picture is fitted once per tile size, off the
  paint (8 per tick), the hovered one is that picture drawn bigger, and the backdrop is composed once.
  Second build: see 3.3.

### 3.2 Fader drag while playing [MEAS] (uiperf, Ryzen 5 3600, Windows 11, Master muted by LYNX_UIPERF_MUTE)

60 frames with 4 fader moves each on insert 1 (the old path = editSong per move, the new = editLevels):

| Project | old path: wall time / paint mean | live levels (first build) |
|---|---|---|
| oxygen.flp | 3.1 s / 24.2 ms | 1.7 s / 23.4 ms |
| thief.flp | 2.0 s / 19.3 ms | 1.3 s / 16.3 ms |

The rebuild is gone from the move (wall time −35…45 %), but each frame still painted ~20 ms: every engine listener
(~45 views) repainted at each move. Second build: editLevels notifies only a levels broadcaster that the Mixer strips
and the Channel Rack listen to; the rest catch up with the full publish when the mouse is released. See 3.3.

### 3.3 Fader, final build [MEAS]

Same run, editLevels notifying only the level views:

| Project | old path: wall time / paint mean / frames >16 ms | live levels |
|---|---|---|
| oxygen.flp | 2.6 s / 17.9 ms / 59 of 60 | **1.2 s / 13.2 ms / 31 of 59** |
| thief.flp | 1.8 s / 17.2 ms / 43 of 60 | **1.1 s / 10.9 ms / 11 of 53** |

60 frames at the 16-ms frame timer take ~1.0 s, so a drag now runs at about the frame rate with 4 moves per frame,
where the old path ran at ~23–33 fps here (on the reporter's slower machine and bigger song: the 4–6 Hz of the clip).
The sound: the engine glides each gain at FL's 0.001 per sample (engine test
`fader171c_a_live_fader_move_glides_without_a_rebuild`: the worst step per sample stays ≤ 0.001 × the input level),
so with new values arriving every frame instead of every 160–280 ms the staircase is gone.

FL [SHOT f0–f5]: FL's Master fader follows the mouse during playback with the hint updating live; LYNX now does the same.

## Where I stopped

- 2026-11-04 19:10. feature/fader171c: done (tests 16 checks / 0 failures, full suite passed before), measured (3.3),
  going through the merge train.
- feature/picker171c: picker works on Windows (shots p01–p19, q01–q06 in /mnt/e/lynx-work/picker171c/gui/): F8,
  background pictures, hover, typed search, Enter / double-click, drag to the Channel Rack and to a mixer insert, tab
  click → Browser, right-click on empty space closes, Ctrl+wheel. Perf fixes (fit off the paint, bilinear halving,
  list-order walk) in the next Windows build; then the final screenshots, and the picker waits for the user's OK.

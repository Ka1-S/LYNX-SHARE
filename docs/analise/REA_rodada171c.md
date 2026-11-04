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

(see the picker section; FL evidence in /mnt/e/lynx-work/picker171c/fl-evidence.md)

## Where I stopped

- feature/fader171c: code + engine tests written (tests/Fader171cTests.inc), uiperf steps "mixer fader drag" (old
  path vs live levels) added; build-local and Windows build running (logs /mnt/e/lynx-work/picker171c/logs/).
- feature/picker171c (origin/main + feature/mixfx170b + feature/art170k merged in, not yet on main): PluginThumbs
  (background `--pluginthumbs` drawing, disk cache, third-party capture), picker view rewrite, core helpers + tests
  (tests/Picker171cTests.inc). Windows build running. Translations not done yet.
- Next: results of the builds; Windows uiperf before/after on a real project; Windows GUI check + picker screenshots
  into /mnt/e/lynx-work/picker171c/; translations; notes + reply draft.

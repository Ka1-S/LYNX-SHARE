# Round 173f (v0.1.24) — UI smoothness: discussion #10 "make smoother movement to volume knob when play lines"

Date: 2026-11-05. Reporter: masoudnp (Windows 10, LYNX 0.1.23, 1024×768). His clip: `/mnt/e/lynx-work/disc10media/v.mp4`
(9 s, 25 fps, AAC 44.1 kHz). Working data: `/mnt/e/lynx-work/smooth173f/` (frame crops in `frames/`, --uiperf reports in
`runs/`, build logs in `logs/`). Round 171c already removed the snapshot rebuild per fader move (REA_rodada171c.md);
this round measures what is left on Windows, with the reporter's layout, through the real mouse path.

Markers: **[CLIP]** measured on the reporter's video, **[MEAS]** measured here, **[CODE]** LYNX or JUCE source.

## 1. What the clip shows [CLIP]

Channel Rack and Mixer open (Mixer layout Wide), a 4-channel pattern playing at 130 BPM, insert 3's fader dragged up
and down for ~4 s (frames 100–175), released at ~7.0 s. Per-frame change detection on fixed screen regions
(`frames/cropsA.png`, `cropsB.png`; a region "changes" when ≥ 20 pixels move by > 40 grey levels):

| Region | playing, before the drag | while dragging | after the release |
|---|---|---|---|
| meters of inserts 4–9 | every frame (25/s) | every 4–7 frames (~5/s), one gap of 280 ms | every frame again |
| master meter | ~every frame | ~4/s, one gap of **1040 ms** | ~every frame |
| time display (bar:beat:step) | follows the steps | 4.3/s, gaps up to 360 ms | — |
| insert 3's dB read-out | — | 3.9/s, gaps up to 280 ms | — |

- The mouse pointer moves every frame; the fader cap, its tip and the read-out jump every 120–280 ms and sit up to ~40 px
  away from the pointer (−10.4, −7.8, −5.6, −17.1, −22.4, −1.5, −9.7, −0.8, −7.4, −14.7…).
- So the UI was **not** slow while the pattern only played: it ran at the recorder's 25 fps. The whole message thread
  starved **while the fader was dragged** (every window, not just the fader): the per-move cost of 0.1.23 (a full
  snapshot rebuild and ~45 listeners repainting per mouse move, REA_rodada171c §1.2).
- Audio: broadband clicks on the spectrogram (`frames/spec.png`) at the big steps (4.3–5.3 s of the track): the gain
  followed the same ~4–6 Hz staircase. Each step glides at 0.001/sample (FL's), i.e. 2–15 ms ramps between values that
  arrive 120–280 ms apart: audible as steps, not as a smooth fade.
- In the last frames the song stops (bar:beat:step 1:4:1 → 1:1:1, Play off) while the fader is still held, and the
  fader shows −10.8 dB again, its value before the drag, with the pointer far below the cap. Not explained from the
  0.1.23 code (no stop or release path restores a drag's start value; an undo would). The current build was checked for
  exactly this sequence (§3: drag, stop the song with the button down, release): the value stays.

## 2. Method [MEAS]

- **Frame probe** (`ui/FrameProbe.h`, new): a transparent, mouse-transparent component on top of the main window. JUCE
  skips a component's `paint()` where opaque children cover the dirty region, so the window's own paint (the old
  "painted frames" of --uiperf) missed most partial frames (a meter or a fader repaint); the probe's paint runs once
  per painted frame with that frame's dirty area as the clip. --uiperf reports per step: frames per second, frame gaps
  p50 / p95 / p99 / max and gaps > 50 ms, process CPU, **message-thread CPU** (`threadCpuSeconds`, GetThreadTimes) and
  the audio device's CPU. `LYNX_TRACE_UI` now logs frames per second and the longest gap in ui.log too, so a user can
  send us the same numbers.
- **Fader drag by mouse** (`UiPerfDrag` in Main.cpp): the reporter's layout (`showPanelsForSnapshot`: Channel Rack +
  Mixer), Pattern mode playing, then insert 3's fader dragged up and down (±40 % of the rail, 1.3 Hz) by mouse events
  through `ComponentPeer::handleMouseEvent` — the window's own path — at 125 moves/s from a high-resolution timer, at
  most one waiting at a time (Windows coalesces WM_MOUSEMOVE the same way; a move that finds one waiting is "folded").
  Measured: how long a move waited for the message thread, **input → paint** (from the move to the first frame whose
  dirty area covers the fader), then the song is stopped with the button down and released; the value is checked.
- Windows 11, Ryzen 5 3600, RTX 3060, 143 Hz display, Direct2D (JUCE 8 default), window 1689×908, fresh
  `LYNX_PROFILE_DIR`, `LYNX_TEST_MODE=1`, Master muted (`LYNX_UIPERF_MUTE=1`, the inserts still meter). Projects:
  oxygen.flp (FL project, ~40 % audio load here) and Demo.lynx. **A** = origin/main ff186dde (0.1.24 train, 171c
  included) + the probe only; **B** = A + this round's fixes. A and B were run back to back.
- `LYNX_TRACE_UI=1 LYNX_TRACE_UI_MIN=0.5`: every paint of the big views over 0.5 ms with its clip, per second.

## 3. Measurements [MEAS]

oxygen.flp, Channel Rack + Mixer, pattern playing, insert 3's fader dragged (`runs/uiperf-{a,b}-oxygen2.txt`):

| | A (main) | B (this round) |
|---|---|---|
| frames while dragging | 39 fps, gap p50 26 / p95 30 ms | **132 fps** (the display's 143 Hz), gap p50 7.0 / p95 13.9 ms |
| moves handled / folded (125/s offered) | 178 / 197 | **373 / 2** |
| a move waits for the message thread | p50 15.2 / p95 22.1 ms | **p50 0.1 / p95 2.9 ms** |
| input → paint | p50 38.7 / p95 48.0 / max 54 ms | **p50 4.6 / p95 8.3 / max 31 ms** |
| message-thread CPU while dragging | 79 % | **17 %** |
| process CPU while dragging | 199 % | 126 % |
| playing only (no drag): message thread, fps | 15 %, 56 fps | 12 %, 55 fps |
| value after stop + release | kept | kept |

Trace (`prof-*-oxygen-trace*/LYNX/ui.log`), during the drag, per frame:

| | A | B |
|---|---|---|
| MixerStrips paint | 12–13 ms (clip = all strips, full height) | **0.6–1.8 ms** (the fader column + meters) |
| ChannelRackComponent paint | ~6 ms (its whole visible area, every frame) | **none** |

B2 (B + the vertical names through `textcache`), same run: drag 132 fps, input → paint p50 4.4 / p95 7.9 ms, 2
moves folded; Mixer maximised and scrolled 39.4 fps (A) → 52.2 (B) → **63.0 fps** (B2); "whole window repainted"
56.8 → 59.9 fps. Windows `--selftest` on B2: result=OK.

Demo.lynx (light project): the drag was already fine in A (103 fps, input → paint p50 9.3 ms, message thread 60 %).

The causes, in order:
1. **The Channel Rack repainted and resized itself at every level edit**, also for Mixer faders that move nothing in it
   (it listens to the levels broadcaster of 171c; `changeListenerCallback` did `updateSize(); repaint();`): ~6 ms
   per frame on oxygen.
2. **A fader move repainted the whole strip, and a meter frame repainted every strip between the meters**: JUCE hands
   `paint()` one clip whose *bounds* cover every dirty rectangle, and MixerStrips culled strips by those bounds only,
   so names (the vertical ones shaped by HarfBuzz on every paint), knobs, buttons and the routing lane of ~10 strips
   were drawn 30–60 times a second for a few meter rectangles: ~13 ms per frame while dragging.
3. Together they cost ~10 ms of message thread per mouse move: the moves queued (15 ms wait), half were folded, the
   frame rate fell to 39 fps and the fader showed values 40–50 ms old. On the reporter's slower PC, the same work in
   0.1.23 plus the snapshot rebuild per move (fixed in 171c) gave the 4–6 fps of his clip.

Audio [CODE + tests]: the mixer's volume / pan / separation are smoothed per sample in the renderer (`applyMatrix`:
each matrix coefficient moves at most 0.001 per sample toward its target — FL's glide, measured on FL's renders in
REA_rodada85 §3; a full 0 → 1 change takes 1000 samples ≈ 21 ms). Values arrive every frame now (171c's live levels),
so a drag gives a continuous piecewise-linear gain curve. New tests: a fader jumping between 0.05 and 0.95 every 256
samples (faster than any screen) and a pan flipping hard left / right every block never step the output by more than
the glide (≤ 0.001 × input per sample, ≤ 2 × for a pan side, which sums two coefficients), and settle on the last
value. Channel volume / pan (Channel Rack knobs) reach the instruments per block as gains; they were not changed here
(FL's native sampler voices smooth them inside the voice, REA_rodada76).

## 4. Fixes (branch wip/smooth173f)

- `ChannelRackComponent::changeListenerCallback`: from the levels broadcaster, only the channel knobs whose volume /
  pan changed repaint; nothing for a Mixer fader. The full refresh stays for the engine's own change message.
- `MixerStrips::changeListenerCallback`: a level change repaints just the moved control (the fader column with its cap
  and dB read-out, or the pan / separation knob).
- `MixerStrips::paint`: each strip part (arm, header, name + its level bars, PDC, knobs, mute / solo, routing lane) is
  drawn only where the frame's clip *region* reaches it (`Graphics::clipRegionIntersects`); the vertical names go
  through `textcache` (laid out once).
- The Wide layouts' thin level bars beside the names repaint with the meters (before, they only moved when something
  else repainted the strip).
- Probe + --uiperf steps + trace (§2), kept for the next rounds.

Not changed / open:
- The meters stay at 30 Hz (their ballistics); frames while only playing are ~55 fps here, all gaps < 40 ms.
- A *full* Mixer repaint (Mixer maximised and scrolled, or the publish after a release) still costs ~15 ms on oxygen's
  ~25 strips (Direct2D draws the knob paths directly; sprites are Linux-only). Candidate for a later round:
  per-strip cached images for the static parts.

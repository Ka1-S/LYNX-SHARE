# Round 174f (v0.1.25) — whole-app fluidity: zoom, scroll, mixer, sweep, idle

Date: 2026-11-06. Continues rounds 174e / 174e2 (`REA_rodada174e.md`: method, tools, FL numbers). Working data:
`/mnt/e/lynx-work/fluid174f/` (logs, uiperf reports, profiles); Windows builds: `C:\Users\kauan\lynx-test\fluid174f\{base, n1, ...}`
(`base` = 174e2's final s2d build, the starting point of this round).

Markers: **[MEAS]** measured here, **[CODE]** LYNX or JUCE source.

## 1. Gaps at the start (174e2, measured at load 33–47)

| case | LYNX s2d | FL Studio 2026 |
|---|---|---|
| Playlist Ctrl+wheel zoom while playing | 52–53 fps (quieter runs 41–144) | 128 |
| Playlist wheel scroll while playing | 74–79 fps | 118–125 |
| Mixer at the display rate, main thread | 20–28 % at ~140 fps | ~55 % at ~88 fps |

## 2. Tools added [CODE]

- `LYNX --cachetest project report` — the Playlist off screen (1466 × 930, scale 1), its cached picture after each case
  (first picture, blit only, zoom steps, zoom settled, zoom past 2x, scrolls by whole pixels, partial and whole repaints)
  compared pixel by pixel with an exact `paintEntireComponent` of the same view; also the picture's memory and that it
  is freed when the Playlist is hidden. Exit code 0 = every exact case matches.
- `--uiperf`: a "playlist zoom (Ctrl+wheel)" step (a notch every third frame, three in / three out, like dupmon's
  zoom) and "playlist after the zoom (settles)".
- `LYNX_PROFILE_MAIN=<file>`: a sampling profiler of the message thread (1 kHz, only the samples where the thread
  ran, unwound like the hang watch, LYNX's frames named with LYNX.pdb next to the exe), 5-second windows of self /
  total functions and modules.
- `LYNX_ZOOM_SCALE=0`: the zoom gesture painted exactly every step (A/B in one build).

## 3. Slice 174f-1: zoom by scaling the picture, exact strips

### 3.1 Profile [MEAS]

`prof-n1a.txt` (first version: the lanes scaled on every frame from the picture of the last exact view, the columns
a zoom out uncovers painted straight to the window), Thief playing, Ctrl+wheel phase: of the message thread's busy
samples, `PlayheadCache::paint` 35 %, of which `paintExactly` → `PlaylistComponent::paint` 24 % (the uncovered
columns, up to 42 % of the lanes after three notches out, painted again on every playhead frame too), Direct2D image
flushes 21 %, `HintBar::paint` 5 %, the toolbar scope 2.6 %. Mixer phase: 30 % in `Direct2DGraphicsContext::endFrame`
(the per-frame present, in the NVIDIA driver), LYNX's own paints ~15 %.

### 3.2 What changed [CODE]

1. **A zoom step is a scaled shift of the cached picture** (`PlaylistComponent::PlayheadCache`, `zoomFrom`). While a
   zoom gesture is on (Ctrl+wheel, the zoom keys' `zoomAroundCentre`, dragging the overview), a change of zoom (and
   scroll) is drawn like 174e2's scroll shift: the picture is copied into the spare image with the lanes scaled around
   the same ticks (`x' = a·x + b`, `a` = new / old pixels per tick, bilinear), and only what is new is painted exactly
   into it: the columns a zoom out uncovers (only the ones this notch uncovered) and the ruler; the headers, toolbar
   and everything else are kept. Playhead frames between notches just blit. After 8 scaled steps in one gesture, or a
   step of more than 2×, the lanes are painted exactly. 120 ms after the last notch (`zoomSettleMs`) the lanes are
   painted exactly once, through the picture. A scaled frame may look slightly soft for those 120 ms; it is never
   stretched by more than one notch (×1.2) from the previous frame's picture.
2. **Clip-grid sampling**: a clean waveform's columns (`ui::paintWaveform`) and an automation clip's curve samples were
   placed from the left edge of the repainted area, so a strip repaint (174e2's scroll shift, a hover) drew a slightly
   different envelope and line than a whole paint: visible seams after a scroll until the next whole repaint (found by
   `--cachetest`: 187–406 px off by up to 152/255). Both now use the clip's own pixel grid (and two extra columns each
   side for the 1-2-1 smoothing).
3. Strips are painted with one rectangular clip each (`paintExactly`), and the shift copies the picture whole and then
   the moving area with a rectangular clip: no clip region with holes (a Direct2D layer mask).
4. **Hidden views do not poll**: the 30 Hz timers of the Channel rack (automated knob repaint), the Piano roll (channel
   box, chord readout, lit keys, play button) and the Mixer's effects panel (input level, slot fitting) are stopped
   while the view is not on screen and restarted when it shows (`ui::TimerWhileShowing`, a ComponentMovementWatcher on
   the view and its parents); each reads its state again on the first tick. None of them has state that changes unseen
   and must be caught, so none keeps a slow rate. (A minimised window reports no visibility change: those timers keep
   running then and skip their work while `! isShowing()`.)
5. `--cachetest`, the uiperf zoom step and the profiler (section 2). The profiler copies the suspended thread's stack and
   unwinds after resuming it: unwinding the suspended thread hung LYNX once at start-up (it may hold the loader lock).

### 3.3 Correctness [MEAS] (`--cachetest`, Thief, 1466 × 930)

Every exact case matches: first picture, blit, zoom steps outside the lanes, **zoom settled: 0 px differ**, zoom out
past 2× settled: 0 px, whole repaint: 0 px. Scrolls by whole pixels: ≤ 12 px off by 1/255 (a shape's float x after a
scroll differs from the shifted one by ~1e-12 px; one coverage step of an anti-aliased edge), accepted by the test as
"1-step rounding". Scaled zoom frames differ from exact (expected; ~90–117 k px, the lanes only). Picture memory
10.4 MB (two full-size RGB images), 0 when the Playlist is hidden.

### 3.4 Results [MEAS] (`session3.sh`, 60 s phases, Thief playing, dupmon fps, two runs each)

| build | load | pl-watch | pl-wheel | pl-zoom | fd-watch | fd-drag |
|---|---|---|---|---|---|---|
| base (s2d) | 18–30 | 124 / 122 | 66 / 70 | 46 / 45 | 117 / 116 | — |
| n1 (scaled, strips straight) | 31–43 | 119 / 123 | 80 / 86 | 74 / 73 | 110 / 114 | 95 / 96 |
| n1, `LYNX_ZOOM_SCALE=0` | 23–43 | 128 / 126 | 81 / 88 | 48 / 49 | 119 / 123 | 116 / 106 |
| n3, `LYNX_ZOOM_SCALE=0` | 13–17 | 141 / 141 | 88 / 103 | 56 / 55 | 136 / 136 | 121 / 126 |
| **n3 (scale-shift)** | 11–15 | 139 / 143 | **99 / 108** | **115 / 116** | 140 / 140 | 125 / 128 |
| FL Studio 2026 (174e2) | — | 135 / 126 | 125 / 118 | 128 / 128 | 90 | 70 / 77 |

Zoom: 46 → 115 fps (2.5×), FL 128. dupmon's zoom sends a notch every 40 ms (25 a second), so the count is mostly
playhead frames: a zoom step that costs more than a frame drops them. Wheel (scroll) also rose (66–70 → 99–108) from
the plain-clip strips and the waveform fix; the loads differ between rows, so the scroll gain is partly noise.

### 3.5 Is this the best way?

- **FL**: FL's zoom is redrawn exactly at ~128 fps (174e2 measurement); it paints with its own GDI-style renderer and
  far fewer primitives per clip. LYNX's exact paint of a large project is 15–30 ms on Direct2D (one geometry per
  rounded shape, path fills for waveforms), so an exact repaint per notch cannot reach 120 fps; scaling the last
  picture during the gesture and painting it exactly when the wheel stops gives FL-like smoothness with the same
  final picture. Per-clip mip-mapped waveform images (the brief's other idea) would still leave the clip frames,
  names and grid to paint per notch; the whole-picture scale covers all of it with one GPU blit.
- **Ableton Live 12**: not measured in this slice (its zoom is to be compared in a later slice of the sweep).
- **Users**: the request behind this round is the user's ("smoother and lighter everywhere, beat FL where it still
  wins"); scaling first and refining when still is the usual technique of map and image viewers.
- Verdict: keep; exact at rest, ~2.5× smoother while zooming.

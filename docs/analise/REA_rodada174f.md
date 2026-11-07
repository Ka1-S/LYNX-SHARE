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
a zoom out uncovers painted straight to the window), Thief playing, load 31–43, window 60–65 s (Ctrl+wheel). Shares of
all samples taken after the thread had run, **including 53 % waiting** (`NtUserGetMessage`, `NtWaitForSingleObject`): this
first profiler counted samples, not cycles, so they rank the work and are not CPU shares. `PlayheadCache::paint` 35 %, of
which `paintExactly` → `PlaylistComponent::paint` 24 %; Direct2D image flushes 21 %; `HintBar::paint` 5 %; the toolbar scope
2.6 %. Window 100–105 s (phase B, the Mixer, same caveat): `Direct2DGraphicsContext::endFrame` 30 %, `paintComponentAndChildren`
15 %. The cycle-weighted profiler has not produced a run yet (see 3.2.5).

### 3.2 What changed [CODE]

1. **A zoom step is a scaled shift of the cached picture** (`PlaylistComponent::PlayheadCache`, `zoomFrom`). While a
   zoom gesture is on (Ctrl+wheel, the zoom keys' `zoomAroundCentre`, dragging the overview), a change of zoom (and
   scroll) is drawn like 174e2's scroll shift: the picture is copied into the spare image with the lanes scaled around
   the same ticks (`x' = a·x + b`, `a` = new / old pixels per tick, bilinear), and only what is new is painted exactly
   into it: the columns a zoom out uncovers (only the ones this notch uncovered) and the ruler; the headers, toolbar
   and everything else are kept. Playhead frames between notches just blit. After 2 scaled steps (`maxScaledSteps`), or a
   step of more than 2×, the lanes are painted exactly. 120 ms after the last notch (`zoomSettleMs`) the lanes are
   painted exactly once, through the picture. A scaled frame shows the clip names and waveforms stretched by the
   zoom since the last exact picture (at most 1.2² = 1.44×) and slightly soft, until the next exact frame.
   Decision (coordinator, by eye on the gesture-frame shots `shots/zoomframe-*.png`): one notch (×1.2) looks fine;
   the first build allowed 8 steps, up to ~4× stretch on a long zoom-in, too visible → cap 2. Follow-up idea
   (coordinator): adaptive — when the last exact paint was cheap (< ~6 ms), paint exactly on the next frame anyway.
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
   and must be caught, so none keeps a slow rate. Minimising does not count as hidden (JUCE reports no event on restore, so a
   stopped timer would never restart): those timers keep running, as before. `--timertest` checks the transitions.
   Idle CPU has not been measured yet.
5. `--cachetest`, `--timertest`, the uiperf zoom step and the profiler (section 2). A profiler build that unwound the suspended
   thread and weighted by cycles left LYNX stuck at start-up in its one run (n3a: 31 ms of main-thread CPU; the same build
   without the profiler ran normally; the older profiler had run once without hanging). The cause was not proven; the likely one
   is unwinding a thread suspended while it holds the loader lock. It now copies the stack while the thread is suspended and
   unwinds after resuming it. **This version has not been run yet.**

### 3.3 Correctness [MEAS] (`--cachetest`, Thief, 1466 × 930)

Every exact case matches: first picture, blit, zoom steps outside the lanes, **zoom settled: 0 px differ**, zoom out
past 2× settled: 0 px, whole repaint: 0 px. Scrolls by whole pixels: ≤ 12 px off by 1/255 (a shape's float x after a
scroll differs from the shifted one by ~1e-12 px; one coverage step of an anti-aliased edge), accepted by the test as
"1-step rounding". Scaled zoom frames differ from exact (expected; ~90–117 k px, the lanes only). Picture memory
10.4 MB (two full-size RGB images), 0 when the Playlist is hidden.

The build being queued (cap of 2 scaled steps, main `ab256f09` merged: `99f95ff4`, install n6) gave the same result: `--cachetest`
failures 0, zoom settled and zoom out settled 0 px, scrolls 4 / 5 / 12 px at max 1; `--selftest` OK; build-local 1938931 checks,
0 failures. These are off-screen tests: they do not measure fps.

### 3.4 Results [MEAS] (`session3.sh`, 60 s phases, Thief playing, dupmon fps, two runs each)

| build | load | pl-watch | pl-wheel | pl-zoom | fd-watch | fd-drag |
|---|---|---|---|---|---|---|
| base (s2d) | 18–30 | 124 / 122 | 66 / 70 | 46 / 45 | 117 / 116 | — |
| n1 (scaled, strips straight) | 31–43 | 119 / 123 | 80 / 86 | 74 / 73 | 110 / 114 | 95 / 96 |
| n1, `LYNX_ZOOM_SCALE=0` | 23–43 | 128 / 126 | 81 / 88 | 48 / 49 | 119 / 123 | 116 / 106 |
| n3, `LYNX_ZOOM_SCALE=0` | 13–17 | 141 / 141 | 88 / 103 | 56 / 55 | 136 / 136 | 121 / 126 |
| **n3 (scale-shift, 8 steps)** | 11–15 | 139 / 143 | **99 / 108** | **115 / 116** | 140 / 140 | 125 / 128 |
| FL Studio 2026 (174e2) | — | 135 / 126 | 125 / 118 | 128 / 128 | 90 | 70 / 77 |
| **n6 (2 steps, main merged), g1** | 14–19 | 130 / 132 | 87 / 96 | **77 / 79** | 126 / 126 | 107 / 115 |
| n6, g2 (stopped by the guard before zoom) | 13 | 133 / 133 | 93 / 108 | — | — | — |
| n6, g3 (6 zoom runs in a row) | 14–15 | 142 / 139 | — | **89 / 96 / 97 / 102 / 103 / 103** | — | — |
| n7 (n6 + profiler fix), with the profiler on, p1 | 14–15 | 144 / 143 | 95 / 97 | 77 / 80 | — | — |
| **FL Studio 2026, same evening, fl1** | 14–17 | 136 / 128 | 121 / 126 | **123 / 122** | — | — |

The n3 row is option (a), 8 scaled steps. The n6 rows are (b), the 2 scaled steps queued as 174f-1, measured with `session4.sh`
(`dupmon3`), at the 13–19 load of a running merge train, so they are noisy. (b) reaches 77–103 fps against 46 for base and 115 for (a); **FL
still wins at 122–123**. Frame pacing shows the same: LYNX p95 30–37 ms and 16–69 gaps over 33 ms per 10 s, FL p95 14 ms and no such gap; (a) had
p95 18–22 ms and 4 gaps. The likely reason, not separately measured: with the cap, every third notch is an exact repaint of 21–34 ms. FL's
shots (`shots/fl1-zoom-*.png`) show its ruler range changing, so FL really zoomed. Wheel scroll: LYNX 87–108, FL 121–126.
**The earlier n6 and n6p sessions (20:33–20:40) are invalid and are not quoted**: see 3.6.
dupmon's zoom sends a notch every 40 ms (25 a second) while the song plays, so the count is mostly playhead frames: a zoom
step that costs more than a frame drops them. It does not show whether a frame is exact, nor whether FL animates its zoom.
Wheel scroll is higher than base too, but the rows ran at different loads and no run separates the causes.

### 3.5b 2 or 8 scaled steps against FL, and the wheel after a zoom [MEAS]

Same sequence in each session (`session4.sh`, Thief, playing); the loads are close at best, not equal, and FL ran at the highest one. n4 is the 8-step build, older than
the main merge. Shots mid-gesture (stopped, ~25 ms after the last notch) and settled: `/mnt/e/lynx-work/fluid174f/shots/compare-z2-mid.png`, `compare-z8-mid.png`.

| session | build | load | zoom fps | zoom p95 ms | zoom gaps > 33 ms per 10 s |
|---|---|---|---|---|---|
| q2 | n6, 2 steps | 6–7 | 104 / 115 / 114 | 29 / 26 / 26 | 9 / 3 / 6 |
| w1 | n6, 2 steps | 14 | 77 / 75 | 33 / 34 | 36 / 39 |
| q2b | n6, 2 steps | 16–25 | 85 / 94 / 96 | 38 / 36 / 35 | 76 / 61 / 55 |
| q8 | n4, 8 steps | 10 | 111 / 122 / 123 | 17 / 14 / 14 | 30 / 19 / 15 |
| w0 | n6, scaled zoom off | 15 | 48 / 50 | 37 / 36 | 63 / 65 |
| qf | FL Studio 2026 | 17–27 | 117 / 119 / 116 | 14 / 14 / 14 | 0 / 0 / 0 |

Picture: FL is sharp mid-gesture at 2 and at 8 notches. LYNX at 2 notches is slightly soft in both builds. After 8 notches one way, 2 steps looks close to exact and 8 steps
is badly blurred, names unreadable. **Neither cap matches FL**, which is sharp and has no gap over 33 ms; how FL does it was not studied.

**The wheel slows down after a zoom.** Same session, same build: w1 wheel 92 / 97 fps before the zoom runs, 35 / 31 after (median 29–34 ms, about an exact repaint). With the scaled zoom off (w0): 84 / 87
before, 32 / 33 after, so the scaled zoom is not the cause. Slow too: q8 33–39, q2b 30–34. Not slow: q2 113–126 at load 6, whose view after zooming still started at bar 1 (bars 1–161; the slow ones
showed about 17–145). Cause not found. Unverified candidate: `cacheShift` only shifts by whole pixels (174e2), which a view at a zoomed scale may not satisfy. Whether builds before 174f do this was not measured.

### 3.6 Invalid sessions, the profiler crash, the input guard [MEAS]

- **Profiler crash.** The first copy-based `LYNX_PROFILE_MAIN` crashed at start-up, in 3 launches of 3: access violation at ntdll+0x20191 in
  RtlVirtualUnwind, on the sampling thread (`profileLoop`). Fixed in 9c2323c8: each unwind step runs under SEH and all non-volatile
  registers are moved onto the copy, not only rbp. With the fix: 0 crashes in 3 launches (`launchtest.txt`, 4 profile windows each) and a whole
  session (p1, 25 windows). Which of the two changes was the one needed was not isolated, and caught faults are not counted. Without the env var the
  profiler does not start.
- **Sessions that ran without a working LYNX.** n6p (20:38–20:40): LYNX died at 20:36:13 from the crash above, and session3.sh went on sending
  input at fixed points. n3a (17:32–17:35): LYNX was alive but hung with no painting (main thread 31 ms, an earlier version of the profiler), and input went on too.
- **n6 (20:33–20:36), not explained.** LYNX stopped painting during `pl-watch`, before any wheel or zoom (0–2 frames; main thread 563 ms in 26 s),
  and the process was gone by 20:34:52. No crash file survives, because the script deleted the profile. Not reproduced: the same build, without the
  profiler, painted through 8 zoom runs (g1, g3) and 4 wheel runs (g1, g2), and g1 and g3 ended without a crash file.
- **Guard.** `dupmon3` checks before every injected move or notch that the PID is alive and owns the foreground and the window under the point
  (and the cursor has not moved), and exits 7 otherwise. Killing LYNX in the middle of a zoom run (k1) stopped it at the next check, with Ctrl and the left
  button released afterwards. In that run the foreground passed to **WindowsTerminal** when LYNX died: an app dying under a scripted session
  hands the foreground to the terminal.

### 3.7 Disabled scripts: do not re-enable (2026-11-06)

They send injected input with no window check and at fixed coordinates, so they were renamed to `*.disabled` (rename only, nothing deleted). Use `E:\lynx-work\fluid174f\session4.sh` and `dupmon3.exe` instead.

- `E:\lynx-work\fluid174f`: `session3.sh`
- `E:\lynx-work\fluid174e`: `session.sh`, `play.sh`, `idle.sh`
- `E:\lynx-work\fluid174e2`: `session2.sh`, `chain-s2d.sh`, `tool\dupmon2.exe`
- `E:\lynx-work\perf174c`: `run.sh`, `fluid.sh`, `fluid-lynx.cmd`, `fluid-fl.cmd`, `bench.ps1`, `sess.ps1`
- `C:\Users\kauan\lynx-test\perf174c`: `bench.ps1`, `sess.ps1`, `dupmon.exe`
- `C:\Users\kauan\lynx-test\fluid174e`: `dupmon.exe`, `dupmon2.exe`
- `C:\Users\kauan\lynx-test\fluid174f`: `dupmon.exe`, `dupmon2.exe`

### 3.5 Is this the best way?

- **FL**: 128 fps on the same test (174e2). **How FL draws its zoom has not been studied** (no REA in this slice): whether it
  redraws exactly, animates or scales is unknown. LYNX's exact paint of Thief is 21–34 ms in `--cachetest` (off screen,
  with read-back, so an upper bound), so an exact repaint per notch costs several frames. Per-clip mip-mapped waveform images
  (the brief's other idea) were not built or measured: the frames, names and grid would still be painted per notch.
- **Ableton Live 12**: not measured in this slice (its zoom is to be compared in a later slice of the sweep).
- **Users**: the request behind this round is the user's ("smoother and lighter everywhere, beat FL where it still
  wins"); scaling first and refining when still is the usual technique of map and image viewers.
- Verdict: keep, exact at rest, but (b) does not beat FL on this test (77–103 against 122–123 fps). How FL zooms (REA) and the Ableton comparison are still owed.

# Round 174e (v0.1.25) — display-rate playhead and meters, mixer CPU and late callbacks

Date: 2026-11-06. Working data: `/mnt/e/lynx-work/fluid174e/` (session logs `sess-<label>.log`, glitch logs
`glitches-<label>.log`, build logs in `logs/`, screenshots in `shots/`). Windows builds measured:
`C:\Users\kauan\lynx-test\fluid174e\{base (0.1.24), new1..new4}`.

Markers: **[MEAS]** measured here, **[CODE]** LYNX or JUCE source.

## 1. Goal

Be at least as fluid as FL Studio on screen during playback (144 Hz display), without costing audio stability or CPU:
playhead, meters and animated views driven by the display refresh, dirty-region repaints only, nothing repainted when
idle; and find why an open mixer raised CPU and late audio callbacks.

## 2. Method [MEAS]

Same as round 174c: `dupmon.exe` (DXGI desktop duplication) counts the frames that changed in a region while it drives
the mouse. One session (`session.sh <label> <dir> <phaseS>`) opens Thief maximized with a seeded profile
(`LYNX_TEST_MODE=1`, fresh `LYNX_PROFILE_DIR`, wingui lock), plays, and runs two phases of `phaseS` seconds each
(240 s = an 8-minute playback):

- **A** Playlist (F5): 2× `pl-watch` (playhead only), 2× `pl-wheel` (Ctrl+wheel scroll while playing);
- **B** Mixer (F9): 2× `fd-watch` (meters only), 2× `fd-drag` (a fader dragged).

CPU: `Get-Process` thread times; the main (message) thread is the earliest thread. Audio: the cumulative over-deadline
and late-callback counters of `audio-glitches.log` at each phase boundary (`phases.py`). FL Studio 2026 measured the
same way on the same project (`fl1`, 60 s phases).

## 3. Results [MEAS]

fps from dupmon (two runs each); CPU = main-thread share over the whole phase; "watch CPU" = main thread over the watch
runs only. Over / late = over-deadline / late callbacks in phase A / B.

| build | pl-watch | pl-wheel | fd-watch | fd-drag | main CPU A / B | watch CPU A / B | over A / B | late A / B | load |
|---|---|---|---|---|---|---|---|---|---|
| base (0.1.24), base8 | 30 / 30 | 30.5 / 35.9 | 30.8 | 81 | 16.4 % / 20.1 % | 8.6 % / 17.3 % | 6 / 16 | 6 / 12 | 27→16 |
| base (0.1.24), base8b | 30.1 / 30.2 | 30.2 / 36.3 | 30.9 / 31.0 | 84.8 / 84.1 | 16.9 % / 19.0 % | 13.5 % / 14.5 % | 164 / 0 | 0 / 0 | 19→10 |
| new2 (144 Hz, toolbar unaligned) | 72 | 35–36 | 100 / 99 | 86 / 88 | 29.3 % / 40.0 % | 25.1 % / 32.7 % | 22 / 49 | 13 / 31 | 16→12 |
| new2, `LYNX_FRAME_HZ=30` | 30 | — | 30 | — | — | 10.1 % / 15.9 % | 282 / 57 | 0 / 2 | build running |
| new3 (aligned frames, toolbar 72 Hz, culling, device boost) | 70.2 / 72.0 | 29.6 / 36.2 | 112.0 / 105.7 | 90.4 / 90.5 | 26.6 % / 42.4 % | 17.5 % / 38.0 % | 100 / 29 | 0 / 1 | 15→19 |
| new4 (sub-pixel playhead) | 127.9 / 132.6 | 30.5 / 31.1 | 104.6 / 103.2 | 86.6 / 90.0 | 40.6 % / 52.1 % | 41.4 % / 43.3 % | 18 / 22 | 0 / 0 | 19→19 |
| **new5 (Playlist image cache)** | **143.5 / 143.6** | 23.4 / 27.9 | **140.4 / 141.4** | **131.1 / 136.7** | 17.7 % / 34.3 % | **7.3 %** / 37.6 % | 1 / 0 | **1 / 0** | 13→7 |
| **new6 (+ scrolls painted direct)**, local build running | **143.4 / 143.3** | **33.2 / 41.8** | 135.8 / 135.6 | 117.1 / 119.4 | 18.4 % / 37.1 % | 9.5 % / 38.7 % | 1 / 0 | 17 / 37 ¹ | 18→11 |
| new6b, tests running in phase A | 130.8 / 124.5 | 34.0 / 37.6 | 140.7 / 141.8 | 128.4 / 130.4 | 19.7 % / 36.1 % | 19.1 % ² / 23.5 % | 45 / 0 | 15 / 1 | 6→10 |
| FL Studio 2026 (fl1) | 135 / 129 | 125 / 118 | 88 / 85 | 66 / 78 | 54–61 % (watch) | — | — | — | — |

¹ new6's late calls came in clusters every 50–70 s, also while stopped right after start and during plain playback,
while a full build-local compiled on the same machine; new5 (same code without the scroll bypass, quiet machine) had
1 / 0 and new6b's phase B (after the tests) 1. ² `lynx_tests` ran during new6b's phase A.

FL's process uses 106–110 % CPU while playing Thief; its main thread alone uses 54–61 % watching: about twice LYNX's
GUI cost at a higher frame rate on the Playlist.

## 4. Findings

1. **Fixed 30 Hz timers capped every live view at 30 fps** [CODE]: Playlist, Piano roll, Channel rack, toolbar
   oscilloscope / time display and plugin level wells all ran on `startTimerHz (30)`; the Mixer strips on a 60 Hz
   timer that repainted every strip's whole meter. Replaced by `ui::FrameTicker` (`src/ui/FrameTicker.h`): a
   `juce::VBlankAttachment` per view while it animates (JUCE 8.0.4 on Windows: one `VBlankThread` per monitor waits on
   `WaitForVBlank` and posts to the message thread), detached after 40 quiet frames, with a 20 Hz idle poll that
   re-attaches as soon as the step reports movement. Idle = no repaints.
2. **Frames must be aligned** [MEAS]: new2 ran the toolbar at 144 Hz on its own phase; main-thread CPU rose from
   ~17 % to ~33 % watching the mixer. Capped tickers now share one frame-period estimate and a frame index, so a
   72 Hz ticker runs on the same frames as the 144 Hz ones (one present per frame), and the toolbar scope is capped at 72 Hz.
3. **The playhead only moved per audio block**: between two blocks the position did not change, so at 144 Hz most
   frames had nothing new. `AudioEngine::playheadAdvanceTicks()` extrapolates from the last block (at most one block
   duration), and the Playlist / Piano roll repaint the playhead strip whenever it moved by more than 0.002 px (an
   anti-aliased line shows fractions of a pixel). new1's 0.34 px threshold gave 14 fps zoomed out; 0.05 px gave 72.
4. **Ballistics were per tick**: meter fall, hold and clip times are now in milliseconds (`PeakHold::advance`,
   `ui::fallFor`), the same dB/s as before at any frame rate (test `fluid174e_meter_advance_matches_30hz_ticks`). The
   mixer repaints only the meter rows a level crossed (`meters::dirtyRows`, test `fluid174e_meter_dirty_rows`) and only
   the controls whose automated value changed.
5. **The mixer did not cause the late callbacks** [MEAS]: over-deadline callbacks cluster by song bars (9–25, 66–82)
   in both phases, and with the same build at 30 Hz they went from 22 / 49 to 282 / 57 while another build ran on the
   machine. The counts follow the machine's load (WSL / Hyper-V builds), not the GUI rate. The audio thread was
   checked: no lock or allocation on the meter path (the meters read atomics). With the same machine state, base8b
   and new4/new5 all had 0–1 late callbacks over 8 minutes with the Mixer open. LYNX also raises the driver's own
   callback thread to MMCSS "Pro Audio" when it finds it below time-critical (`LYNX_DEVICE_BOOST=0` turns it off;
   `describeRealtime` reports "device thread priority X -> Y"). On this machine's ASIO driver the thread already runs at
   priority 18, so the log says "(kept)": a safety net for drivers that call back at normal priority.
6. **Paint culling**: the Playlist grid, lanes, ruler and headers paint only inside the clip region, so a 16 px
   playhead strip no longer walks every bar line and lane.

7. **Where the main thread's time goes at 144 Hz** [MEAS]: `LYNX_TRACE_UI=1 LYNX_TRACE_UI_MIN=0` (`ui-tr4.log`) and
   20-second play-only runs (`play.sh`, Playlist view, main-thread CPU):

   | new4 run | main thread |
   |---|---|
   | display rate (vblank) | 37.6 % / 39.5 % |
   | `LYNX_FRAME_HZ=144` / `72` / `30` | 28.8 % / 25.2 % / 6.7 % |
   | without the toolbar oscilloscope (`toolbarGroups` without `output`) | 27.6 % |
   | toolbar monitor in Long view | 35.5 % |
   | base (0.1.24) | 11.1 % |
   | **new5 (image cache)** / new5 with `LYNX_PLAYLIST_CACHE=0` | **20.0 %** / 36.3 % |

   A playhead strip paint (16×930 px) cost 0.57 ms: the lanes, clips and waveforms under the column painted again every
   frame. The oscilloscope costs ~1.5 ms per 72 Hz frame: JUCE's Direct2D `strokePath` builds a new path geometry and
   stroke style every call. The rest is the per-present cost of the window.
8. **The Playlist image cache** [CODE]: the Playlist installs its own `juce::CachedComponentImage`
   (`PlaylistComponent::PlayheadCache`): the playhead's repaints leave the image valid (`playheadRepaintOnly`), and the
   cache draws the playhead line and ruler triangle over the blitted image. Any other repaint (edit, hover, child)
   refreshes its area of the image. Large repaints (scroll, zoom, resize: more than half the view) paint straight to the
   window, and the image is rebuilt once the view has been still for 300 ms (through the image a wheel scroll fell
   from ~33 to ~26 fps). Snapshots (`paintEntireComponent`) still get the playhead from `paint()`.
9. **Idle** [MEAS]: Thief open and stopped, 20 s: main thread 1.4 % (new4) against 5.9 % (base). The toolbar monitor
   now treats a signal below −80 dB as silence (a plugin's noise floor kept it repainting at 72 Hz).

## 5. Open

- **Wheel scrolling while playing (~30–36 fps, FL ~120)**: a whole Playlist repaint costs ~15–28 ms on Thief
  (clip frames, names and audio clips). Needs scroll-by-blit of the cached image (only the newly exposed columns
  painted) — next slice.
- **Mixer CPU at the display rate**: 140 fps (FL 88) costs 37.6 % of the main thread watching (base 14.5 % at 30 fps,
  FL ~55 %). Capping the mixer meters at 72 Hz would roughly halve it but falls below FL's 88 fps; a user setting
  ("Animation rate: display / 60 / 30 Hz") would let low-power machines choose — needs a decision.
- **Oscilloscope**: ~11 % of a core at 72 Hz from Direct2D path stroking; a cached stroke style or a JUCE patch that
  realises the geometry once would cut it without changing its look.
- Over-deadline counts need a quiet machine to compare builds; the 8-minute sessions here ran with load 12–27.

## 6. Slice 2 (174e2): animation rate, scroll by shifting, cheaper mixer and scope

Working data: `/mnt/e/lynx-work/fluid174e2/` (`sess-*.log`, `glitches-*.log`, `load-*.log` = load average every
10 s, `ui-*.log` = `LYNX_TRACE_UI` traces, `fluid/` = dupmon frame times). Builds:
`C:\Users\kauan\lynx-test\fluid174e\{base, s2a (new7), s2b, exp8, s2c}`. Method as above, with `session2.sh`
(profiles on E:, `dupmon2.exe`) adding 2× `pl-zoom` (Ctrl+wheel) to phase A. No quiet window was available (other
rounds building): load was 7–16 during these runs, so CPU shares are ±5 points between identical runs; the A/B
below alternates builds three times.

### 6.1 What changed [CODE]

1. **Settings › General › Animation rate**: Display (default) / 60 Hz / 30 Hz caps every `ui::FrameTicker`
   (playhead, meters, scope, step column) while keeping them on the display's vertical blanks. The frames are chosen
   by `framepace::Pacer` (`src/core/FramePacing.h`): one per cap, time based (the next frame is due one interval of
   the cap after the last; half a display frame early counts; no catch-up burst after a stall), shared by every
   ticker with that cap, so they present on the same frames. A first version numbered the display frames
   (absolute vblank time / estimated period, then a shared counter); both paced 60 Hz unevenly (gaps of 1–11
   display frames, p95 38 ms) because the period estimate moved the number and other vblank sources (a second
   monitor, a second window) advanced the counter. Test `fluid174e2_frame_pacing`. FL has no equivalent setting;
   it is a LYNX addition asked for by the coordinator.
2. **JUCE patch 16** (`cmake/LynxPatchJuce.cmake`, `juce_Direct2DImage_windows.cpp`): JUCE 8.0.4 copies the whole
   image back from the GPU each time a `Graphics` drawing into a Direct2D image ends (a full-screen Playlist picture
   = 8 MB and a GPU stall per frame). `juce::lynxSkipImageReadback`, set by `ui::ScopedNoImageReadback`, skips it for
   LYNX's cached pictures, which are only drawn to the window. `juce_graphics` now builds from the patched copy.
3. **Playlist: scroll by shifting the picture.** A scroll by whole pixels shifts the cached picture (two images,
   swapped) and paints only the rows / columns it uncovered; the glide steps whole pixels. Two bugs kept it from
   working: JUCE sends a whole-component `repaint()` to `CachedComponentImage::invalidateAll()`, not to
   `invalidate()`, so the scroll flag was ignored and the picture dropped every frame; and the "large repaint →
   paint straight to the window" path then kept the picture empty for the whole scroll. Both fixed (a scroll always
   goes through the picture). Traced: a wheel frame paints 1.8–7 ms instead of 21–28 ms.
4. **Mixer**: the cached picture of the visible strips now leaves out everything that moves with the level: the
   meter fills and the two thin bars beside a vertical name (Wide layouts) are drawn over it, and their repaints keep
   it valid (the thin bars had invalidated it every frame). An automated fader repaints its rail only when the cap
   moved by 1/8 px and its dB readout only when the text changed. The meter gradient is anchored at x = 0: JUCE's
   Direct2D gradient-brush cache (128 entries, keyed by the gradient's points) then shares one brush across all the
   meters instead of one per meter.
5. **Toolbar oscilloscope**: stroked into a software image and copied (`BitmapData`, row `memcpy`) into one
   persistent native image, drawn with one `drawImage`: Direct2D no longer builds a path geometry and stroke style
   per frame, and no software image is converted per draw. Same look (`LYNX_SCOPE_IMAGE=0` = the old path).

### 6.2 Results [MEAS]

8-minute sessions (240 s phases), fps from dupmon (two runs each):

| build | pl-watch | pl-wheel | pl-zoom | fd-watch | fd-drag | main A / B | watch CPU A / B | over A / B | late A / B | load |
|---|---|---|---|---|---|---|---|---|---|---|
| base (0.1.24), base2 | 30.2 / 30.2 | 40.7 / 40.7 | 49.6 / 49.6 | 30.9 / 30.8 | 89 / 90 | 19.2 % / 17.9 % | 13.1 % / 15.0 % | 0 / 0 | 0 / 0 | 7.5–11 |
| s2a (slice 2 before the fixes) | 143.7 / 143.7 | 38.6 / 42.7 | 105 / 107 | 141.3 / 141.2 | 134 / 136 | 21.6 % / 29.7 % | 6.7 % / 31.9 % | 0 / 1 | 0 / 22 ¹ | 5–8 |
| s2b (+ scroll / mixer fixes) | 127.2 / 128.4 | 70.3 / 72.3 | 41.4 / 41.4 | 140.3 / 142.1 | 133 / 137 | 22.6 % / 27.4 % | 11.9 % / 31.7 % | 59 ² / 1 | 1 / 0 | 9–16 |
| s2d (final: time pacer, one union meter repaint), 13:04 | 132.4 / 130.8 | 74.0 / 79.3 | 51.8 / 53.3 | 127.7 / 125.1 | 106 / 112 | 23.1 % / 25.9 % | 13.0 % / 28.1 % | 198 ³ | 0 / 0 | 33–47 |
| FL Studio 2026 (fl2, 60 s phases) | 135 / 126 | 125 / 118 | 128 / 128 | 90 | 70 / 77 | 72 % / 56 % | — | — | — | — |

¹ one burst of late *device* calls in 6 s during plain playback (09:01:45–51, load 7.4), LYNX's own processing
over deadline 1. ² bars 68–78 in plain playback while the load average was ~15 (above 12: machine noise, as in
slice 1); base2 at load 8 had 0. ³ both phases together (87,376 callbacks, 0.23 %; 4–22 a minute evenly spread
over both phases, not tied to the Mixer), with the load average at 33–47 during the whole session (the Windows and
Linux builds of four other rounds running): not comparable with the quiet-machine rows; late device calls 0.

A/B, 30 s phases, three alternating rounds (main thread while watching; fps):

| run | Mixer watch CPU | fd-watch | Playlist watch CPU | pl-wheel | pl-zoom | load |
|---|---|---|---|---|---|---|
| exp8 (per-meter rects) ×3 | 25.5 / 20.1 / 19.4 % | 141 / 122 / 141 | 26.2 / 15.1 / 12.3 % | 92–111 | 59–144 | 8.8 / 14.7 / 11.5 |
| exp8, one union rect per frame ×3 | 17.4 / 28.9 / 25.6 % | 141 / 142 / 141 | 11.4 / 11.7 / 24.3 % | 84–112 | 82–91 | 7.0 / 8.3 / 11.3 |
| base ×3 | 17.7 / 12.2 / 12.9 % | 30.9 | 12.0 / 11.4 / 9.2 % | 34–38 | 49–50 | 6.8 / 8.6 / 9.4 |

pl-zoom varies most between runs (41–144 fps on the same code): the runs start from the zoom the previous run
left, and at the zoom limit nothing changes on screen (counted as fast frames); read it as a range.

Per-meter rectangles and one union rectangle per frame are within the noise of each other (over all six runs of
each, x/y/z series: union 21.6 % mean, best 15.1 %; per-meter 22.4 %, best 15.0 %); the union rectangle is kept
(one repaint call per frame, `MixerStrips::advanceMeters`). Earlier single runs with experiment switches (since removed): flat meter colour instead of the gradient
26.7 %, no meter overlay at all 22.5 %: the remaining Mixer cost is not in LYNX's paint code (the traced paints are
0.1–0.4 ms a frame) but in the per-frame window present, which only fewer frames reduce.

Animation rate, 30 s phases on s2b (before the pacer): 60 Hz = 60 fps, Mixer watch 15.3 %; 30 Hz = 29 fps,
Mixer watch 5.7 % (gaps uneven: fixed by the pacer).
After the time-based pacer (s2d, 30 s phases, load 35–45, logs `sess-d30.log`, `sess-d60.log`, `load-d*.log`):

| rate | pl-watch fps | p95 / max frame | Mixer fd-watch fps | p95 / max frame | Playlist watch CPU | Mixer watch CPU | late calls |
|---|---|---|---|---|---|---|---|
| 30 Hz | 30.0 / 30.0 | 41.8 / 55.9 ms | 30.0 / 30.0 | 42.5 / 49.8 ms | 9.6 % | 9.1 % | 10 ⁴ |
| 60 Hz | 60.1 / 60.1 | 23.4 / 37.5 ms | 60.0 / 60.0 | 22.9 / 41.5 ms | 10.0 % | 13.3 % | 2 ⁴ |
| (60 Hz, frame-number pacer, car60) | 59.8 / 60.0 | 27.7 / 35.2 ms | — | — | — | — | — |

At 60 Hz on the 144 Hz display the frames alternate 2 and 3 display frames (13.9 / 20.8 ms; the p95 of 23 ms is
one display frame of present jitter), with at most one gap over 33 ms in 10 s; at 30 Hz 5 / 4 display frames.
⁴ 30 Hz: 8 while the project loads (stopped), 2 during playback in the Playlist phase; 60 Hz: both while loading.

### 6.3 Open

- **Mixer main thread at 140 fps is ~20 % (base 14 % at 31 fps; 28 % in the final s2d session at load 33–47)**: per frame it is a third of base's cost; the
  rest is the window's present at 140 Hz. Not reached: ≤ 14.5 % at 140 fps. The 60 Hz setting brings it to ~15 %.
  Next step if wanted: present the Mixer from its own child window / swap chain, or measure on a quiet machine first
  (the coordinator schedules a clean 174c re-run before the 0.1.25 tag).
- **Zoom (Ctrl+wheel) 50–90 fps (s2d: 52–53 at load 33–47), FL 128**: every zoom step repaints the whole Playlist (~22 ms traced: clip
  frames, names, waveforms). Needs a per-clip picture cache (waveform and frame images reused across zoom levels).

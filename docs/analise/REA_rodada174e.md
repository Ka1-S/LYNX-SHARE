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

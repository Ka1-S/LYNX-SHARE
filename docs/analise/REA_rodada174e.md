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
| new2 (144 Hz, toolbar unaligned) | 72 | 35–36 | 100 / 99 | 86 / 88 | 29.3 % / 40.0 % | 25.1 % / 32.7 % | 22 / 49 | 13 / 31 | 16→12 |
| new2, `LYNX_FRAME_HZ=30` | 30 | — | 30 | — | — | 10.1 % / 15.9 % | 282 / 57 | 0 / 2 | build running |
| new3 (aligned frames, toolbar 72 Hz, culling, device boost) | 70.2 / 72.0 | 29.6 / 36.2 | 112.0 / 105.7 | 90.4 / 90.5 | NEW3_CPU | NEW3_WATCH | NEW3_OVER | NEW3_LATE | 17→15 |
| new4 (sub-pixel playhead) | NEW4_PL | NEW4_WHEEL | NEW4_FD | NEW4_DRAG | NEW4_CPU | NEW4_WATCH | NEW4_OVER | NEW4_LATE | NEW4_LOAD |
| FL Studio 2026 (fl1) | 135 / 129 | 125 / 118 | 88 / 85 | 66 / 78 | 54–61 % (watch) | — | — | — | — |

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
   checked: no lock or allocation on the meter path (the meters read atomics). What the device thread lacked was
   priority: the driver's own callback thread ran at its default priority. LYNX now raises it once to MMCSS
   "Pro Audio" when it is below time-critical (`LYNX_DEVICE_BOOST=0` turns it off), and `describeRealtime` reports
   "device thread priority X -> Y".
6. **Paint culling**: the Playlist grid, lanes, ruler and headers paint only inside the clip region, so a 16 px
   playhead strip no longer walks every bar line and lane.

## 5. Open

- **Wheel scrolling while playing (~30–36 fps, FL ~120)**: a whole Playlist repaint costs ~15 ms on Thief (`--uiperf`
  trace: clip frames and audio clips up to 9.8 ms). Needs a cached clip layer (backing image per track row, or
  scroll by blit) — next slice.
- Over-deadline counts need a quiet machine to compare builds; the 8-minute sessions here ran with load 12–27.

# REA round 167b — v0.1.22: Newtime, Newtone, Transmitter parity

Worktree `../lynx-wt-ntt167`, branch `feature/ntt167` (wip: `origin/wip/ntt167`). Clean-room as every REA round:
behaviour in our own words; FL's own front ends (Delphi DLLs) may be decompiled; zplane's engine
(`Newtone_Engine_x64.dll`, licensed) is black-box only (EXEC renders of FL's unmodified DLL, as round 165g).
Data: `/mnt/e/lynx-work/ntt167`.

Evidence markers: [DECOMP addr], [EXEC], [SHOT], [TEST], [HYP].

## 1. Transmitter — transient / sustain sends to other mixer tracks

Reporter's words (brief): "route transient/sustain sends to other mixer tracks … as FL".

FL evidence ([DECOMP Transmitter_x64.dll], Ghidra project `flplugins165/ghidra/Transmitter`):
- Eff_Render `FUN_00717600` (vtable 0x714360 slot 38): the sidechain input (`+0x230`, host call `+0x120`) is copied to the
  detection buffer (`+0x144`), else the main input is; the engine (`+0x154`, vcall `+0x30`) gets n, in, out, the
  transient part buffer (`+0x134`), the sustain part buffer (`+0x13c`) and the detection buffer.
- Transient send track `+0x234`, sustain send track `+0x238`: for each one > 0 the host (`+0x128`, flag 0) hands the
  track's send buffer, the part is **copied** into it with the routine behind `PTR 0x7748f8` (resolved in
  `FUN_005ad7d0` to `ippsCopy_64f`; Fruity Send adds instead), then the buffer is released (flag 1).
- The routes are registered with the host (`FUN_00717d20` / `FUN_00711870`; object `+0x104` index 0 = sustain,
  index 1 = transient; `+0xfc` = sidechain). The editor's track lists are menus `0x7e0/0x7e8/0x7f0`, sized from host
  dispatcher 0x32 (insert count). State order: 42 raw parameters, transient send, sustain send, sidechain, editor
  speed (`+0x248`), 4 bytes.
- [EXEC] goldens (`/mnt/e/lynx-work/fltrans165/x G_*.tr/.su/.out`): out = levelT·tr + levelS·su exactly (residual
  −150 dB); the sent parts are taken after the tilt and section effects and **before** each part's level and on/off
  switch (sustain switched off is still sent).

LYNX before: Transmitter imported with "sends not routed" (partial); no send targets.
LYNX now: `EffectSlot::sendTracks` (two outputs), `SlotSendTap{slot,target,output,afterSlot}`; the mixer graph
(order, routing check, solo, insert-in-use, delete/remap, history, PDC — the sends leave after the plugin's own
latency) handles both outputs; FL import routes them; project file `sendTransient` / `sendSustain`; the editor has two
send boxes (menu of inserts). [TEST `ntt167_transmitter_*`, `fltrans165` import test]. [HYP] FL copies while LYNX adds:
the same result because a LYNX send tap is a buffer of its own per slot; it would only differ if another plugin sent
into the same FL send buffer in the same block.

## 2. Transmitter — synced delay time in steps:ticks

FL evidence [DECOMP `FUN_00716b00`, parameters 0x1b / 0x1f]: with x = raw / 10000, i = floor(x·767 + 0.5) + 1 and the
text is `"%d:%d"` of i / 48 : i % 48 (steps : ticks, 48 ticks a step).
LYNX before: the synced value showed as decimal steps ("4.00 steps"). LYNX now: `transmitter::syncedText()` — FL's text. [TEST]

## 3. Transmitter — detection envelope view

FL evidence [DECOMP `FUN_00717900`, `FUN_00717560`, `FUN_00714af0`]: a ring of columns (count `+0x218` = panel width·k);
each column holds 6 maxima: output peak L/R, detector output e L/R, 1 − e L/R (R copies L when the mono flag `+0x718`
is set). A column covers round(fs·((601^(1−speed/100) − 1)·0.0004125 + 0.0025)·uiScale) samples (minimum 16); speed 90
in Default and most presets (127 samples at 44.1 kHz). `+0x15c` = the larger channel peak (meter).
LYNX before: no envelope view. LYNX now: `Engine::envelope()`, `EnvelopeView`, the editor's detection section draws
the ring (`tssenvelope`), mouse wheel changes the speed. [TEST `ntt167_transmitter_envelope_view`]
Left: the editor speed is not read from FL's state into the running view (default 90; wheel only).

## 4. Newtime — FL's warp markers and stretch

FL evidence [EXEC harness `ntt167/nt/fx.exe` + `ntw.py` on FL's unmodified `Newtime_x64.dll`; states saved by FL from
`drums.wav`, `drst.wav` (stereo), `v1.wav` (sung vowel)]:
- Newtime's state XML keeps a track `Trk_N` of type `class CzplJTimelineWarpSource` with `Pitch_N` segments (found at
  transients by FL's analysis). A warp marker is (`iOrigStartSamplePos` → `iCurStartSamplePos`);
  `fCurInvStretchFactor` = original / current length. The unmoved analysis leaves ±2-sample offsets.
- Renders follow the piecewise-linear map between markers (a moved marker's hit lands where predicted).
- FL always runs élastique: the identity case nulls only to −3.3 dB, levels ±1 dB, the first 40 ms −1.8…−3.6 dB,
  renders about 290 samples longer.
- LYNX vs FL (`cmpw.py`, LYNX renders by `tools/ntw`): mono drums id/m2/m2n/r15/r075 onsets within ±54 samples,
  10 ms envelopes median 0.00 dB, 90% within 0.64–0.87 dB, per-hit levels within ±1.8 dB; stereo drums ×1.5 both
  channels median 0.00/−0.03 dB, 90% within 0.96/0.92 dB, totals +0.04/+0.24 dB, no lag; vowel ×0.75 / ×1.3
  median 0.00/+0.01 dB, 90% within 0.75/1.14 dB, f0 kept (221.6 / 262.5 / 294.0 Hz at 25/50/75%, one frame 260.9).
LYNX before: Newtime's FL state not read (pass-through + "time warping not reproduced", beta).
LYNX now: `newtonefx::warpFromTrack()` / `warpOf()` read LYNX's own `LXNW` or FL's Newtime track; markers moved less than
2.5 samples count as unmoved, ratios within 1e-3 are resampled instead of stretched; the editor shows FL's markers;
import reports Newtime converted. Newtime is out of beta (menu name "Newtime"). [TEST `ntt167_newtime_imports_fl_warp_markers`]
Left: FL's élastique colouring of unmoved audio (≈1 dB, −3.3 dB null) is not reproduced on purpose (LYNX plays
unmoved audio clean); Newtime's marker detection modes (None / Tempo / Transient, density wheel, shift markers,
snap to downbeat — strings `DetectMarkerNoneMenuClick 0x797ee0`, `MarkerDensityWheel 0x799bd0`, `ShiftMarkerAmountWheel
0x79c010`) are not built.

## 5. Newtone / Newtime — Sync, Snap to grid, Send to Playlist

FL evidence [DECOMP `Newtone_x64.dll`, Ghidra `~/gh-r73/ntfe`; DFM strings]:
- Send to playlist (`SendToPlaylistBtnClick 0x79a5b0`, menu `0x79a7b0`): if a sample is loaded (`+0x218`), host
  dispatcher 0x1d gives a folder; the name is the sample's name (`FUN_007973b0`, "Dragged sample" when none) + ".wav";
  `FUN_007934f0` saves the edited audio as WAV ("Saving: %s"); when the file exists the host gets it with dispatchers
  0x5f and 0x2a (flag 2 — into the Playlist as an audio clip). Newtime has the same (`0x79bd80/0x79bf90`).
- Snap menu (DFM): "Snap to grid" with Beat, 1/2, 1/3, 1/4, 1/6, 1/8 beat, Off (radio group, one handler
  `SixthBeatMenuClick`); also "Snap to scale" (`0x79aaf0` sets engine parameter 0x16) — LYNX already had snap to scale.
- Sync (`SyncBtnClick 0x79b340` → `FUN_007941f0`): the button state goes to `+0x1eb` and the engine's play-with-host
  flag (vcall `+0x150`) = sync ? (mode `+0x1e8` or `+0x1ea`==2) : `+0x1ea`==2 — the slave mode LYNX already has as
  "Play with the song" (round 73).
LYNX before: Snap to scale and Play with the song only; Save as WAV only.
LYNX now: a "Snap" button with FL's menu (Beat … 1/8 beat, Off) — Ctrl+time-drag of notes (the dragged note's start)
and Newtime marker drags land on the song's beat grid (Alt = free); "Send to Playlist" writes the edited sample to
`Music/LYNX/Edits/<sample name>.wav` and adds an audio clip channel + clip at the beat of the song position on the first
free track. [TEST `ntt167_newtone_snap_to_beat_grid`] [HYP] FL's grid origin = the sample start (the Newtone view's
time 0); its default snap item is not visible in the DFM (Off in LYNX).

## 6. Newtone — legato glides and sample realign (left)

Not done in this round: both need EXEC studies of zplane's engine (black-box) as round 165g §1.6 describes
(long smooth times up to 40 c off; FL's whole-sample re-alignment after edits). The harness is ready
(`/mnt/e/lynx-work/flnewt165/tools`: `flrun.sh`, `run2b.py`, `cmp.py`, `ntr`).

## Per-item table

| Item | Reporter's words | FL evidence | LYNX before | LYNX now |
|---|---|---|---|---|
| Transmitter sends | route transient/sustain to other tracks | DECOMP 0x717600, EXEC goldens | not routed | two send outputs, mixer graph, import |
| Synced time | steps:ticks | DECOMP 0x716b00 | "x.xx steps" | FL's "s:t" |
| Envelope view | detection envelope view | DECOMP 0x717900/0x714af0 | none | FL's 6-value column ring |
| Newtime warp | import FL's warp markers, stretch as FL | EXEC fx.exe ntw.py | pass-through, beta | markers read, measured, out of beta |
| Snap / Send to Playlist / Sync | as FL | DECOMP 0x79a5b0, DFM, 0x79b340 | Sync only | Snap to grid menu, Send to Playlist |
| Legato glides / realign | as FL | — | 165g residuals | left |

## Where I stopped
- 2026-10-31 (2): Transmitter, Newtime and Newtone Snap / Send to Playlist written and committed; builds and Windows
  shots next; legato / realign left.
- 2026-10-31: worktree created, briefs and earlier rounds (73, 95, 165g flnewt165 / fltrans165) read. Starting with
  Transmitter (sends, steps:ticks, envelope view), then Newtone, then Newtime.

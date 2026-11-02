# REA round 168j — .flp leftovers of the 168c plan, the icon model, PDC flags, DC hold (v0.1.23)

Date: 2026-11-01. Scope: the plan's "168d" section of `REA_rodada168c-gaps.md` §3 (the round letter changed: 168d became
the video round) — trace the [HYP] events E1–E18, icons for channels / inserts / Playlist tracks in the model (+ import
and .lynx), PDC Automatic 0x1D / Compensate automations 0x27, mixer track count 0x67, Time settings 0x23, scale 0xF3,
and FL's DC hold at an audio clip's end (M23).

Clean-room: headless Ghidra, read-only, on our own copy of the analysed `FLEngine_x64.dll` project of round 167g
(`/mnt/e/lynx-work/r167g`), DFM form resources of FLEngine (`tmp/rea165/dfm`), the 165 projects of FL's `Data` folder
and FL's own render files of rounds 167k / 167l. FL Studio was **not** launched this round. Only behaviour and formats
are written here; no Image-Line code, art or data is copied (the 37 reference-scale degree lists are music theory; the
names are the file names FL shows).

Markers: **[DECOMP addr]** Ghidra pseudocode — loader `FUN_00f46490` (decompiled in `tmp/r167g/d2.c`), project saver
`FUN_00f40840` (`tmp/r167g/saver.c`), mixer track saver `FUN_00f3d620`; handler names from the Delphi method tables
(`tmp/r168c/allmeth.txt`); **[DFM]** a form resource; **[DATA]** the corpus (`tmp/r168j/py/*.py`); **[FL render]** FL's
own render of a test project; **[CODE]** LYNX source; **[TEST]** a LYNX test.

Material: `~/.claude/jobs/f8789b49/tmp/r168j/` — `d1.txt` (ExtInputMonitorOffMenuClick, DockToLMenuClick, the
pattern-length handlers, the PDC planner FUN_010ff3b0, TimeDivision_Update, ProjDataPathBtnClick, the icon handlers,
the scale functions, AutoPDCMenuClick, FUN_012b5e60 / FUN_012b6020), `d2.txt` (Playlist track menu handlers),
`py/` (corpus scripts), `g.sh` (Ghidra runner).

## 1. The events, one by one

| # | Event | FL (evidence) | LYNX before | LYNX now |
|---|---|---|---|---|
| E1 | **0x9B** channel u32 | the loader stores it in the channel's +0x7c [DECOMP FUN_00f46490]; written after the name 0xCB; 1 312 of 7 007 channels ≠ 0 [DATA]: the channel **icon** index (the channel menu's "Change icon", `SetPlugIconMenuClick` 0x1232780 [DECOMP]) | ignored | `ChannelDef::icon` (FL's index, 0 = none), imported, saved in .lynx (`"icon"`). No drawing yet (168h's icon UI) [TEST rea168j_flp_events_parsed / _flp_import] |
| E2 | **0x5F** insert u16 | → mixer track +0x14 [DECOMP loader]; the saver writes it only when > 0, before the name 0xCC [DECOMP FUN_00f3d620]; mixer "Change icon..." `SetIconMenuClick` 0x1295570 → FUN_012be9b0 (the rename/colour/icon dialog) [DFM TFXForm SetIconMenu] | ignored | `MixerTrack::icon`, imported (it precedes the 0xEC that opens its insert, like the colour), saved |
| E3 | 0xEE playlist track u32 at +8 | the Playlist track's icon (`EEPLTrackIconMenuClick` 0x1228bd0 → FUN_012e2510 "icon" mode 0x102 [DECOMP]); 711 tracks ≠ 0 [DATA] | not read | `PlaylistTrack::icon`, imported, saved (`"playlistTrackIcons"`) |
| E4 | **0x67** u16 | "ABORT FLP_FXTrackCount ItemData out of range" outside 3…502 [DECOMP loader]; the saver writes the mixer's track count `*PTR_DAT_015e2198` [DECOMP saver]; equals the number of 0xEC records in all 34 files that carry it (18 → 18, 127 → 127) [DATA `cnt.py`] = Master + inserts + FL's "current" track | LYNX made max(16, the highest used) inserts | the import makes `count − 2` inserts (FL's 18-track templates: 16; FL 2026's full mixer: 125), still more when a route / channel needs it; files without 0x67 unchanged [TEST rea168j_flp_import] |
| E5 | **0xCA** text | the project **data folder**: written by `ProjDataPathBtnClick` 0x1364de0 ("Select the directory where your project data is to be stored...", "The project data folder has been changed…") into `*PTR_DAT_015e2ba8`, which the saver writes as 0xCA and the loader reads back (FL ≥ 8) [DECOMP] — [HYP] of 168c proven | ignored | `Song::dataFolder`, imported and saved; the feature itself (Options › Project › Data folder, "Copy used samples") is 168i's (O10) |
| E6 | 0x9F u32 | the saver writes the version record's +0x26 u16 after 0xC7 [DECOMP saver]; equal to the last field of the version text in 81 of 84 files [DATA `ver.py`]; **the loader has no branch for it** [DECOMP FUN_00f46490] | ignored | nothing to import (FL ignores it on load; the version text already has the build) |
| E7 | **0xA4** pattern u32 | pattern +0x44 [DECOMP loader]; written by the Channel Rack's pattern length selector `PatternLengthSelectChange` 0x10394c0 (undo "pattern length change", value × the ticks unit) [DECOMP]; 1536 = 16 beats at PPQ 96 in one demo [DATA] — [HYP] proven: the pattern's fixed length in ticks, 0 = automatic | ignored | `Pattern::lengthTicks` (LYNX's existing fixed length, 0 = automatic), rescaled to LYNX's PPQ [TEST rea168j_flp_pattern_length] |
| E8 | 0x9D / 0x9E pattern u32 | pattern +0x10 / +0x18 [DECOMP loader]; no handler of the pattern menus writes them (`EEPat*MenuClick`, `PatSel*` looked at) | ignored | not traced further; left unread (−1 in 765 / 790 of ~830 patterns) |
| E9 | 0x19, 0x34, 0xAB, 0xFC / 0xFD pattern | +0x1c, +0xa4 (bool), +0xa8, +0xac, +0xb4 [DECOMP loader]; 2–8 projects, always 0 / constant [DATA] | ignored | not traced; nothing visible in the corpus |
| E10 | **0x2A** insert u8 | the track's +0x00 byte: the colour loader 0x95 sets it to "colour ≠ the default 0x636c71" and 0x2A (written right after 0x95) overrides it [DECOMP loader + FUN_00f3d620]; [DATA `mix.py`] 0x2A = 1 on exactly the 896 inserts that carry a colour → the insert's **"own colour" flag** (FL draws the default colour when it is off) | ignored | an insert with 0x2A = 0 keeps the default colour whatever 0x95 says [TEST] |
| E11 | **0xA6** insert u32 | track +0x30, written by `ExtInputMonitorOffMenuClick` 0x129dd90 (undo "set mixer track monitoring option") with the menu item's tag [DECOMP] = the input button menu's **Monitor external input**: 0 Off, 1 When armed, 2 Auto (hidden), 3 On (FL's default, checked) [DFM TFXForm]; the Playlist's audio-track menu shows the same byte (FUN_00eaadf0) [DECOMP]; loader: 4+ → 3 | ignored; an insert with an input was set to On | `MixerTrack::monitor` from 0xA6 (LYNX's `InputMonitor` has FL's order); files without it keep the old rule [TEST] |
| E12 | **0x2B** playlist track u8 | written after each 0xEE; 0 → the loader puts the default colour back in the track [DECOMP loader]; [DATA `pl.py`] 1 on exactly the 823 tracks whose colour is not the default → the track's "own colour" flag, as E10 | ignored | a track with 0x2B = 0 gets the default colour |
| E13 | 0x24 arrangement u8 | arrangement +0x10 [DECOMP]; 0 or 91 (6 projects) | ignored | not traced (no arrangement handler writes it among `Arrangement*`) |
| E14a | **0x1D** u8 | `AutoPDCMenuClick` 0x129f0c0 toggles `*PTR_DAT_015e3448` [DECOMP]; the planner `FUN_010ff3b0` reads it twice: off → it skips the generators' latencies of the track (local max stays 0) **and** the effects' latencies of its slots (sum 0); the input latency (+0x1c8) and the manual track latency (+0x1cc) are added outside both branches [DECOMP] | ignored (always automatic) | `Song::autoPdc`; `computePdc` leaves out channel and slot latencies when off (manual latencies and routing stay); mixer menu › Plugin delay compensation › **Automatic**; saved only when off [TEST rea168j_pdc_automatic_off] |
| E14b | **0x27** u8 | `CompensateAutomationsMenuClick` 0x129efb0 toggles `*PTR_DAT_015e0568` [DECOMP]; read by FUN_012b5e60 / FUN_012b6020 (the per-slot tempo / position messages) and six times in the mixer slot processing FUN_01046a70: with it on and a slot delay (+0x120, the slot's input lag set by the planner = the track's alignment + the slots before it) the messages are queued at now + that delay instead of sent at once [DECOMP] | ignored | `Song::compensateAutomations`; when on (and Automatic on) a mixer effect's automation-clip value is taken at the song position minus its input lag (`PdcPlan::slotInput`, `RackRenderer::compensatedAutomation`) — LYNX's own effects and hosted plugins; mixer menu › **Compensate automations**; saved only when off [TEST rea168j_compensate_automations] |
| E14c | **0x23** u8 | `TimeSettingComboChange` 0x13659d0 → `*PTR_DAT_015e37c8`; the Options › Project combo "Set as time division" ("Original FL Studio system") / "Set as time signature" ("Traditional time signatures") [DFM TMIDIForm]; `TimeDivision_Update` 0xeccbd0: in signature mode the numerator sets the beats per bar and the denominator the steps per beat (16 / denominator) [DECOMP]; FUN_01365810 swaps the controls [DECOMP] | ignored | `Song::timeSignatureMode`; Settings › Project › **Time settings** combo; in signature mode "Beats per bar" reads "Numerator" and the grid line shows the denominator (LYNX's step grid is fixed at 4 per beat → 4) |
| E14d | 0xF3 8 B | the saver writes `*PTR_DAT_015e3a90` (scale) and `*PTR_DAT_015e3da8` (root) [DECOMP saver]; `EEMenuScaleClick` 0x11f87e0: the menu tag × 256 + root → FUN_00e90670, which splits them, loads "Reference scales\\<name>.FSC" for a scale ≥ 0 and sets the Default / Black notes / Automatic flags for −1 / −2 / −3 [DECOMP]; tags −1 Default, −2 Black notes, −3 Automatic, roots C…B = 0…11 [DFM TShortcutsModule EEAutoKey*]; FUN_00e370c0 falls back to −2 [DECOMP]; the list is FL's `Data/Patches/Scores/Reference scales` folder in name order (37 .fsc; corpus values 10 = "Minor Natural (Aeolian)", 0 = "Major (Ionian)") [FILES] | ignored | `Song::scaleHighlight {scale, root}` (FL's numbers, default −2), imported, saved; `notetools::flReferenceScales()` (FL's 37 scales, checked against FL's .fsc files when FL is installed) and `scaleForFlReference()` (onto LYNX's scale list) for the Piano Roll's scale UI (168g) [TEST rea168j_reference_scales] |
| E14d | 0x2F, 0x85 | 0x2F: byte +0x1b of the project-options object (next to 0x28 fast declick +0x18, 0xA7 +0x1a), always 0 [DATA]; 0x85: a u32 of a window's bounds record (FUN_00609830), **no loader branch** [DECOMP] | ignored | nothing to import |
| E15 | 0xAA channel u32 | channel +0x714 [DECOMP loader/saver]; 0 / −1 | ignored | unchanged (no reader found) |
| E16 | 0x30, 0x33, marker fields | always 0 [DATA] | — | — |
| E17 / E18 | 0xE2 / 0xE6, 0x26 | as 168c | — | not in this round |

## 2. FL's DC hold at an audio clip's end (M23)

- **FL:** when an audio clip is longer than its (processed) sample, FL's output keeps the sample's last value as a
  constant until the clip ends — [FL render] round 167l take 2 (`/mnt/e/lynx-work/r167l/fl_take2.wav`: −0.0435 on the
  left after `l_stereo.wav` ends at 0.5 s, up to the clip's end at 4.186 s) and round 167k `c_base.wav`
  (−0.0564 from 2.0 s to 4.2 s). FL's own sampler voice does the same for channel notes (hold the last frame, then its
  end table, REA_rodada76 §6, already in LYNX's `renderFl`).
- **LYNX before:** an audio clip's voice stopped at the sample's last frame (silence to the clip's end).
- **LYNX now:** in clip mode the Sampler voice holds its last output frame (`SamplerVoice::holdL/holdR`, set at the
  note-on from the region's last frame so a clip resumed past its sample holds too) until the clip's note-off, which
  fades it out as before. [TEST rea168j_audio_clip_holds_its_last_value]; FL comparison in §4.

## 3. UI (LYNX style)

- Mixer menu › Plugin delay compensation: **Automatic (compensate the plugins' latencies)** and **Compensate
  automations (delay a plugin's automation with its audio)**, ticked like FL's (both on by default).
- Settings › Project › Time settings: the **Time settings** combo (Set as time division / Set as time signature).
- New strings in all 15 languages.

## 4. Checks

- **Tests:** build-local with FL's Data folder: 2 105 045 checks, 0 failures (`rea168j_*` all pass; the reference
  scales are checked against FL's 37 `.fsc` files).
- **Older clip tests updated to FL:** `rea167k_clip_pitch_keeps_the_length`, `rea167l_clip_stretch_plays_like_fl` and the
  163c length analysis expected silence after a clip's file ends. FL's own renders of round 167k
  (`C:\Users\kauan\lynx-test\r167k\fl\clip\*.wav`) hold a constant there in every case: c_base −0.0564, c_p3 −0.4396,
  c_m5 −0.0061, p_normpol +0.0627, reversed / faded-out clips 0 (their last frame is 0). The tests now check "constant"
  (FL's DC hold) instead of "silent".
- **[FL render] vs LYNX `--export`** (Windows build, fresh profile, test mode; `tmp/r168j/lyrender.sh`, output
  `/mnt/e/lynx-work/r168j/out`):

  | project | FL hold | LYNX before | LYNX now | RMS FL − LYNX, 0–4.2 s |
  |---|---|---|---|---|
  | c_base | −0.0564 from frame 88199 | 0 | −0.0564 from frame 88199 | 0.0012 |
  | p_normpol | +0.0627 | 0 | +0.0626 | 0.0014 |
  | c_rev | 0 | 0 | 0 | 0.00001 |
  | c_p3 / c_m5 | −0.4396 / −0.0061 | 0 | +0.1189 / +0.4294 (also from frame 88199) | 0.55 / 0.44 |

  Unpitched clips match FL. On pitched clips LYNX holds its own last output frame, as FL does, but LYNX's clip pitch
  shifter already gave a different waveform from FL's before this change (round 167k), so the held value differs too.
  This comes from the shifter, not from the hold.
- **GUI** (Windows build, real mouse, fresh profile, test mode, `C:\Users\kauan\lynx-test\r167k\fl\clip\c_base.flp`;
  shots `/mnt/e/lynx-work/r168j/g6c.png`, `g7c.png`, `g9.png`, `g10c.png`, `g11c.png`): an insert's right-click menu ›
  Plugin delay compensation shows **Automatic** and **Compensate automations** ticked; clicking Automatic unticks it
  (Compensate automations stays as it was); Options › Project settings shows **Time settings = Set as time signature**
  (FL saved this project with 0x23 = 1) with "Numerator" and "Denominator: 4 (the Channel Rack's step grid is 4 steps
  per beat)." Picking "Set as time division" in the combo turns these back into "Beats per bar" and "Steps per beat". The
  page fits the dialog. LYNX was closed by its PID.

## Where I stopped

- Done: everything above; tests green; FL render comparison and GUI check done. Next: land via coord/land.sh.
- Not done: icon drawing (168h), Piano Roll scale UI on `Song::scaleHighlight` (168g), project data folder feature
  (168i), E8 / E9 / E13 / E15 meanings (no writer / constant in the corpus).

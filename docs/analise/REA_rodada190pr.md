# REA round 190pr — FL demo song sweep: Harmor's automation and link velocity, FL's song-start values, the export's markers

FL Studio 2026 (`C:\Program Files\Image-Line\FL Studio 2026`). Clean-room: behaviour only, no code or data copied (the
range table of §1 is FL's observable parameter ranges, measured). Evidence kinds as in the earlier rounds: **[EXEC]**
FL's own output — `FL64.exe /R /Ewav` renders (minimized, no injected input, under the GUI and FL locks) of the demo songs
and of copies changed in one place (a channel switched off, a marker's type or tick, a mixer slot bypassed), or the
installation's DLLs driven by our harness `hm.exe` (round 34's `hm.c` + `pf=` ProcessParam calls with any flags);
**[DECOMP]** Ghidra decompilation of `Harmor_x64.dll` (round 75's `plug_all.c` / `pp.c`, the same DLL: SHA-256
`23673ad8…5aba`); **[DATA]** the .flp files. Work folder `/mnt/e/lynx-work/flparity2_190pr/` (`fl/` FL renders of the
demos, `solo/` FL and LYNX solo renders, `rea/hm/` the harness and its logs, `start/`, `markers/`, `sub/`, `stroll/`
the variant projects, `gold/` LYNX renders, `scripts/` our tools: `cmp.py`, `bands.py`, `drift.py`, `solofl.py`,
`slotoff.py`, `setmarker.py`, `chmap.py`, `flpstate.py`, `peread.py`, `flsession.sh` + `flrun.ps1`, `gold.sh`, `lxsolo.sh`).

Item 50 of the 0.1.32 queue: Lightning's non-vocal channels ~1 dB under FL, Stroll Up −4.4 dB, Umziky's 50 ms error
3.1 dB, With You, Raubana LIFE +1.4 dB, Overcome's first 2 s −3.9 dB.

## 1. Harmor under FL's automation

| # | Fact | Evidence |
|---|------|----------|
| H1 | FL numbers Harmor's parameters 0…824: k < 772 is part k / 386's parameter k mod 386 (A, then B), 772…824 the global parameter k − 772. Inside a part, 95…342 are the articulators' settings (stored elsewhere in the state); the others are the plain values LYNX's `harmorParamOffset` reads: all 577 of them read back from the DLL (REC_GetValue) equal LYNX's reading of the same state. | [DECOMP] FUN_00879380; [EXEC] `rea/hm/r*.txt`, `rea/chk/lynx.txt` |
| H2 | Every parameter's raw range, from the DLL itself: ProcessParam with REC_FromMIDI (0x20) maps a normalised 0…2^30 value to raw = min + Round (v · (max − min)), rounded half to even (−2…7 at 0.5 → 2; −7…8 → 1; 0…999 → 500). Filter 1 / 2 frequency (52 / 64): 0…8192; Mod X / Y / Z (772…774): 0…512; part volume (11): −2000…2000; unison order (34): 1…9. Parts A and B have the same ranges. | [DECOMP] FUN_00879540 (flag 0x20, constant 2^−30 at 0x0087c7e0); [EXEC] `rea/hm/ranges.txt` (825 rows) |
| H3 | ProcessParam stores Mod X / Y / Z as raw / 512 (float, plugin +0x1108…; the voices glide to them through their own X / Y / Z smoothing, FUN_008826b0) and a filter's frequency knob as lo + raw / 8192 · span in double (part +0x488 / +0x548). | [DECOMP] `pp.c`; constants 1/512, 1/8192 at 0x0087c948 / 0x0087c860 |
| H4 | The DLL and LYNX's reference renderer, the same held note with the same ProcessParam changes: "pling riser" (Lightning ch 50), filter 1 at 1000 / 8192 / 4000 while it sounds: residual −89.8 dB; filter 2 and part B's filters: −89.8 dB; "doodad" (ch 22) Mod X / Y changes: −70 dB (its static residual). A change on a sounding note of "doodad" leaves −9 dB of residual for ~20 ms and −35 dB decaying over 0.5 s before both settle at −80 dB (open: the voice-level smoothing right after a step). | [EXEC] `rea/hm/a_*.f32`, `b_*.f32` (hrun.sh) |
| H5 | FL resets the song-start values (0xD8) of Harmor's parameters before the song plays: Lightning's "saw chords" is saved with filter 1 at 0 (shut) and starts at 8192; FL's solo render of the channel plays it open from bar 1 (LYNX before: −31.2 dB RMS against FL). | [DATA] 0xD8 records; [EXEC] `solo/fl/li_c32.wav` |
| H6 | Harmor never asks the host to unlink the velocity (only voice events 2 and 3, never 6, at TriggerVoice, on every state tried), so FL's Final.Vol is Init.Vol × the channel volume, (11^(1.28 v) − 1)/10, as for 3x Osc. Lightning's "sub bass" (a patch that ignores the velocity itself: the DLL plays velocity 0.5 / 0.78 / 1.0 at the same level) with all its effects bypassed: FL −11.6 dB at velocity 128 and −19.4 dB at 88; LYNX before −17.8 / −16.6; the law predicts −11.5 / −19.4. | [EXEC] `rea/hm/*.txt` host log; `sub/fl/sub_all.wav` |

**LYNX now** (ChannelDef::engineRev 3; .lynx projects saved before keep 0–2 and play bit for bit as before):
`harmor::flParamSlot / flParamRange / flRawOf / flParamInState / automatable`; FlpImport writes FL's song-start values
of plain parameters into the channel's state (H5) and reads Harmor's pattern events with H2's ranges; HarmorInstrument
follows automation of the filter frequencies (52, 64, 438, 450) and Mod X / Y / Z (772…774) live (H3, H4) and plays
Final.Vol = Init.Vol × channel volume, slide notes moving Init.Vol (H6). Other Harmor parameters under automation are
still not followed (song-start values of plain ones are).

## 2. Sytrus' main page and FL's own effects: song-start values

| # | Fact | Evidence |
|---|------|----------|
| S1 | Sytrus' main-page parameters that round 190pi automates (0 master level 0…160, 1 LFO −128…128, 2 pitch −24…24, 18 / 19 Mod X / Y 0…512) have 0xD8 song-start values in 5 demos (Lightning's "fm bass": Mod X 123, Mod Y 256); the import now writes them into the state (`sytrus::setSetting`) and reads pattern events of them. | [DATA] |
| S2 | Raw ranges of FL's own effects whose 0xD8 values and pattern events the demos use (0xD8 records: Fruity Balance 34, Fruity Filter 32, Fast LP 15, PEQ 2 …), from each DLL's ProcessParam (REC_FromMIDI 0 / 2^30): Balance pan −128…128, volume 0…320; Fast LP 0…1024 ×2; Filter 0…1024 ×5, 0…1; Free Filter 100…700, 0…1024 ×3; WaveShaper 64…192, 0…128 ×2; PanOMatic 0…256, 0…320, 0…2 ×2, 0…128, 0…55000; Delay 2 −128…128, 0…160 ×2, 0…127, 1…768, −512…512, 0…2, 0…128; Delay 3 26 parameters; Limiter 18; PEQ 2 levels −1800…1800, frequencies and widths 0…65536, types 0…7, orders −3…3, main −1800…1800. They now reach the effects as the other song-start values do. (Love Philter's DLL stops the harness: not measured; LYNX keeps its own map.) | [EXEC] `rea/hm/fx/*.txt` |

## 3. renderwav's clock (measurement only)

`tools/renderwav` advanced its song position at the project's start tempo, while the app's transport moves at each
block's tempo (RackRenderer::currentBpm). Songs with tempo automation were compared against FL out of step: Secret Agent
Rendezvous (24 bars at 120 BPM in FL, a tempo clip from bar 1; the tool played them at the 110 BPM of 0xD8) drifted 3.9 s.
The tool now follows the tempo map (LYNX_FIXEDTEMPO=1: the old clock). No change to the app.

## 4. The export's markers

| # | Fact | Evidence |
|---|------|----------|
| M1 | FL's render starts at the arrangement's Start marker: Umziky (Start at 2976 ticks, first clip at 3000) starts 0.115 s before its first sound; the marker moved to 1536: 5.47 s of lead-in (the 1464 ticks before the clip); moved to 3456, after the first clip: the render is 1.8 s shorter and starts mid-sound; made a plain marker: 11.19 s of lead-in (bar 1). | [EXEC] `start/fl/um_*.wav`, `fl/…Umziky…` |
| M2 | FL's render jumps over Marker skip sections, and a skip that lands on another skip marker follows it too: Raubana – LIFE (skips at 15360, 24576, 30720, 43008, 49152 ticks, each to the next marker; 24576 → 30720 → 43008 → 49152 → 52224 chain) renders 89.2 s of music (100.5 s file) = 0…15360, 21504…24576 and 52224 to the end at 172 BPM; the same project with its skips made plain markers renders 212 s, as LYNX did. | [EXEC] `markers/fl/rb_plain.wav`, FL render of the demo |
| M3 | FL's render does not stop at a Marker pause: Raubana with its pause moved to 53760 ticks (mid-outro) or made plain renders the same 89.2 s. Marker loops: not tested (a loop would make the render endless). | [EXEC] `markers/fl/rb_pause53760.wav`, `rb_nopause.wav` |

REA_rodada165f-markers §1.3 found every marker action ignored by `/R` on synthetic projects; M1–M2 are FL 2026's renders
of its own demos and of copies changed only in the marker records. LYNX now (Song::exportMarkers, on for .flp imports and
new projects; .lynx saved before: off, the old export bit for bit): `exportopts::exportSpans` — from the first Start
marker, over every Marker skip's section (chained skips followed), to the song's end — and `makeRenderSong` renders
those spans back to back (Raubana: 101.5 s against FL's 100.5 s file; RMS vs FL +1.46 → −0.76 dB, 50 ms error
0.92 → 0.62 dB; Umziky 1.42 → 0.24 dB).

## 5. Sytrus: the notes' Mod X / Y

| # | Fact | Evidence |
|---|------|----------|
| Y1 | Overcome's "Vinyl Click" (FL ch 82) is 1102 four-tick notes with their own velocity, pan and Mod Y; FL hands a note's Mod X / Y to the voice as Final.FCut / FRes, and Sytrus' Mod X / Y maps read main Mod X / Y + 256 · FCut / FRes (round 31's model; LYNX used 0). LYNX's solo of the channel against FL's: RMS −4.13 → 0.00 dB, 50 ms error 7.56 → 0.00 dB, 22 → 100 % within 3 dB; the song's first 2 s −3.70 → +0.41 dB. | [EXEC] `solo/fl/ov_c82.wav`; [DATA] the note records |

## 6. Other findings (not changed)

- **Stroll Up** (−4.4 dB): 5 FLEX channels (protected packs, played with LYNX synth presets: 808 Clean −8.8, Just Strollin
  −8.3, Driving Through −4.8 dB solo), and its master chain: with every master effect bypassed the choir solo matches FL
  within 0.02 dB at every band; FL's Patcher 1 on the master (Control Surface + X-Y-Z controllers driving Flanger /
  Flangus: "not mapped" in LYNX) accounts for +7 dB of LYNX's low band. Also LYNX's import does not bypass a Patcher slot
  that FL's slot switch turns off (0x1F00 on a Patcher slot): not used by the demos as saved.
- **With You** (18 FLEX channels), **Never Out** (5 FLEX), **Stroll Up**: FLEX stays a synth-preset stand-in.
- **Harmor's rack compressor** (System\Compression presets): the reference renderer against the DLL differs by 2–4 dB on
  "saw chords" with its compressor on at filter values the song does not keep; the song solo matches FL within 0.5 dB.
- Pattern events / song-start values still without a known scale: Love Philter (11 songs), Harmless (6), 3x Osc (4), GMS,
  DirectWave, Pitch Shifter, Delay Bank, Patcher (round 190pr's report lists them per song).

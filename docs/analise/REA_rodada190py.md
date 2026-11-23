# REA round 190py — FL-native parity in the demo songs: channel PITCH automation, Gross Beat automation, Love Philter / Harmless / 3x Osc scales

FL Studio 2026 (`C:\Program Files\Image-Line\FL Studio 2026`). Clean-room: behaviour only, no code, tables or presets
copied; ranges and state places are facts read from the installation's own DLLs through our harness.
Evidence kinds: **[EXEC]** the installation's DLLs driven by round 190pr's `hm.exe` (our own code; round 190py adds an
answer to host call 29 and `save2=`, the state saved after the `pf=` ProcessParam calls) and FL 2026's own `/R /Ewav`
renders (round 190pr's FL solo `solo/fl/li_c50.wav`, the 45 demo renders `fl/`); **[DATA]** the .flp files; **[STATIC]**
earlier REA (REA_rodada16 §2.4: Love Philter's parameter numbering and ranges, the exact engine's own). Work folder
`/mnt/e/lynx-work/parity_190py/` (`SCORES.md` the ranked table, `rea/hm/` harness + range files, `gold/` LYNX renders,
`lx/solo/` solo renders, `scripts/` our tools: `usage.py`, `ranges.sh`, `cmp.py`, `chg.py`, `solocmp.py`, `track.py`;
`tools/pt2.cpp` the Patcher probe).

Round 190pr's sweep left, for FL's own plug-ins: pattern events / song-start values "value scale not known" on Love
Philter (19 demos), Harmless (18), 3x Osc, GMS; Gross Beat's automation "target not reproduced" (16 demos); the channel
PITCH knob's automation not reproduced (Lightning's "pling riser - pitch"); Harmor's other parameters not live.

## 1. The channel PITCH knob (FL channel parameter 4)

| # | Fact | Evidence |
|---|------|----------|
| C1 | FL's automation clips, song-start values (0xD8) and pattern events address a channel's PITCH knob as channel parameter 4 (any generator: Sampler, 3x Osc, Harmor, Sytrus, FLEX…). 10 of the 45 demos automate it, 50 clips (Bombs Away 31, on 3x Osc channels). LYNX gave it no target: the clips played nothing. | [DATA] LYNX_REPORT of every demo (`gold/base/*.log`) |
| C2 | The parameter's raw value is cents (0xD8 values 3100, −2400, −400, 0). Its normalised value spans −4800 … +4800 cents (±48 semitones), not the knob's ±range: Lightning's "pling riser" (Harmor, channel range 12) rises from 619 to 1244 Hz (+12 semitones) in FL's solo render while its clip goes 0.5 → 0.625; the ±range reading played +3 semitones (743 Hz). | [EXEC] `solo/fl/li_c50.wav` vs `lx/solo/li_c50_dev1.raw` / `_dev3.raw` (`scripts/track.py`) |
| C3 | The knob's range (ChannelMisc::pitchRange) does not hold the value: Bombs Away's "3x Osc #2" is saved at +1200 cents with range 2, and its "BASS Pitch" clip (0.375 … 0.625 = ±12 st) is heard at ±12 in FL's solo render; held within ±2 st the solo was 2.74 dB (50 ms error), unheld 1.46 dB (FL vs LYNX, `fl/py_bombs_osc.wav`). | [DATA] `tools/autodump`; [EXEC] |

**LYNX now:** FL channel parameter 4 → `AutomationTargetKind::channelParam` with `channelPitchParam` (0x10A0, past the
MIDI parameters) on any channel; value 0..1 = −48 … +48 semitones (`channelPitchSemitones`); the
rack hands the automated knob to the instrument as its channel pitch every chunk (`RackRenderer::channelKnobCents`, the
same `setChannelPitchNow` every native engine and the plug-in wrapper already follow), the song-start value sets the
knob (`Song::setTargetValue`), the .flp export writes it back as channel parameter 4 with cents. Old projects: their
clips had no target (kind none) — unchanged. Lightning's pling riser solo vs FL: RMS −2.71 → +0.03 dB, 50 ms level
error 4.62 → 1.47 dB, within 3 dB 32.6 → 78.8 %, spectral-peak track error median 535 → 0 cents (> 50 cents: 71 → 9.5 %).

## 2. Gross Beat's automation

| # | Fact | Evidence |
|---|------|----------|
| G1 | Gross Beat's 9 ProcessParam parameters: 0 / 1 time / volume slot 0…35, 2 / 3 time / volume mix 0…256, 4 / 5 0…10000, 6 −256…256, 7 0…1, 8 the scratch offset 0…65536 (default 65536). They equal the exact engine's `grossbeat::rawMin / rawMax` (round 53 / 190oa). | [EXEC] `rea/hm/GrossBeat.rng` (REC_FromMIDI 0 / 2^30, read back) |
| G2 | 16 demos automate Gross Beat (clips and 0xD8 values on 0 … 3); the importer had no target for them ("target not reproduced"), although the exact engine keeps the 9 parameters as (raw − min) / range (the scratch dial turned round). | [DATA] |

**LYNX now:** Gross Beat parameters map to the exact engine's parameter k (scratch: 1 − v); raw ranges above for 0xD8 /
pattern events. Import-time only (old projects' clips had kind none).

## 3. Love Philter and Harmless scales

| # | Fact | Evidence |
|---|------|----------|
| L1 | Love Philter's DLL stops the parameter harness after host call 29 (also with a path string answered: not that call's meaning; not pursued). Its ranges are the exact engine's (`philter::rawRange`, REA_rodada16 §2.4), the ones its automation clips already play through. | [EXEC] `rea/hm/LovePhilter.txt`; [STATIC] |
| H1 | Harmless' 100 ProcessParam ranges equal LYNX's knob table (`harmlessEngineParams`, raw FL integers) for 99; 72 "Phaser scale" is 0…2 in the DLL (LYNX's switch 0…1). | [EXEC] `rea/hm/Harmless.rng` |

**LYNX now:** `flRawRange` gives both (Harmless 72: 0…2); 19 + 18 demos' song-start values / pattern events now reach
the engines. Import-time only.

## 4. A desktop 3x Osc under FL's automation (engine revision 4)

| # | Fact | Evidence |
|---|------|----------|
| O1 | ProcessParam k of 3x Osc stores the raw value unscaled at state offset 4 + 4k for k = 0…20 and at 0x60 + 4 (k − 23) — the 364-byte channel-settings snapshot's int k − 23 — for k = 23…113 (110 Mod X / cut at snapshot 0x15C, 111 Mod Y / res 0x160, 113 filter type 0x168, 99 the pitch block's amount 0x130, 60 / 30 envelope times). | [EXEC] `pf=k,77,1` then `save2=` vs the default state (`rea/hm/s*.bin`) |
| O2 | Ranges: per oscillator pan ±64, shape 0…6, coarse ±24, fine ±100, stereo phase ±64, detune ±50, mix 0…128, phase randomness 0…64; five 17-int blocks (pan, volume, cut, res, pitch) with times 100…65536, 0…128, amounts ±64 / ±128 / ±2400, 200…65536, 0…2, ±128 ×3; the blocks' first int is passed unscaled (0…2^30: not a target); 110 / 111 0…256, 113 0…7. | [EXEC] `rea/hm/3xOsc.rng` |
| O3 | FL's channel Mod X / Y (channel parameters 2 / 3, 0…256) on a 3x Osc channel are the same values (110 / 111). Right Night's 3x Osc channels carry hundreds of pattern events on them ("target not reproduced ×10"). | [DATA] |

**LYNX now:** `threeosc::isFlTarget / flRange / writeFlParam`; a desktop 3x Osc's `setParameter` writes FL's raw value
into its working state and reloads the engine (the path FL Studio Mobile lanes use since round 190fe: the sounding
voices follow); the rack binds those targets from `ChannelDef::engineRev` 4 (FL imports and new channels); projects saved
before (revision ≤ 3) ignore them, bit for bit. Channel parameters 2 / 3 on a 3x Osc map to 110 / 111.

## 5. Re-checked, not changed

- **GMS:** its DLL also stops after host call 29; its engine keeps 0…1 floats (no FL raw table on record) — 1–2 events
  in 2 demos, left as "value scale not known".
- **Harmor's other parameters live:** the demos automate 782 (global 10), part A 75 / 78 / 79 / 80, 104 / 106
  (articulators, not in the state) and 34 (unison order), ≤ 2 clips each; their song-start values reach the state
  (190pr), live changes would need a patch rebuild per change. Not done.
- **Stroll Up's Patcher 2 (+0.84 dB flat):** the graph as imported (`tools/pt2`): host → IN Stereo Shaper → four PEQ 2
  bands → Soft Clippers → PEQ 2 → PanOMatic → OUT Stereo Shaper → "wetin" PanOMatic → host, and a "dryin" PanOMatic at
  volume 0 in parallel (no dry path heard). Cables off, the level is the same (190pt); the remaining error sits in one of
  the nodes' static laws inside the Patcher (two Stereo Shapers at 18432 / 16000-ish, four PanOMatic volumes 255…286,
  clipper posts) — needs FL renders of node-edited copies; not done.

## 6. Results

See the round's HANDOFF (before → after per demo, the plug-in quality gate, gold).

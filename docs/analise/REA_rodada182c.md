# REA round 182c: FL Studio Mobile (.flm format, instruments/effects, workflow) → LYNX plan

2026-11-08. Target: `Plugins/Fruity/Generators/FL Studio Mobile/FL Studio Mobile_x64.dll` (FL Studio Mobile 5.0.x
built as an FL plugin, sha256 c63ba7ae…), static REA only (Ghidra 12.1.4 through the REA MCP, one 17-minute turn under
`~/.cache/lynx-ghidra.lock`). No GUI session was needed or run. Clean-room: behaviour and layout only, in our words;
nothing was copied into LYNX. No artwork was used.

Inputs:
- FLM's own content in `…/FL Studio Mobile/Installed/` (targeted paths only): 11 demo `.flm`, 9 `.songtmpl`,
  3 drum templates, 13 rack presets, 26 effect-preset folders, 5 synth-preset folders;
- the user's `MONTAGEM XONADA REMAKE.flm`, as zipped by FLM 5.0.4 (Android) with its samples, plus a WAV render
  (`/mnt/e/lynx-work/flm182c/user-zip/`).

Deliverables:
- `docs/analise/FLM_FORMAT.md`: the format, every claim marked PROVEN-REA / PROVEN-FILE / INFERRED;
- this file: the inventory, the workflow and the plan;
- the prototype reader `/mnt/e/lynx-work/flm182c/flmdump.py` + `verify.py`. They are clean-room Python, kept outside
  the repo, and serve only as the reference model for the C++ importer.

## 1. Format: summary of findings

Full detail is in FLM_FORMAT.md.

- **Container: PROVEN-REA.** The magic is `10LF`; internally the format is "FLM3". The rest is a flat list of
  little-endian chunks (tag + u32 size):
  - `HEAD` holds the title, **tempo f64**, key-locker scale/root and app version;
  - `KEYB` holds the chord-set JSON;
  - `META` holds the dates, author and notes;
  - `TDIV` holds the time signature;
  - then N × `RACK` and N × `CHNL`, paired by index.
- **RACK: PROVEN-REA.** It holds:
  - `RPRM` (volume, pan, …);
  - the built-in drum kit `RSMP` (pads + samples);
  - the module chain: `RMOd` = type id + instance id + body (`ADD1` key range/bypass, `PRST` preset/sample path,
    `PRMS` parameter values, `CSTM` engine state).
- **CHNL: PROVEN-REA.** `CHHD` holds the name, order and ids. Then come one `TRKH` lane per lane, where
  `DESc.kind` is 0 notes, 1 automation, 2 audio or 3 drum steps. Each `CLIP` holds:
  - a start tick;
  - `CLHd`: timeline length, loop length, start offset, content id, source length, all in beats;
  - `EVN2`: 20-byte records (tick, f64 length, i16 key, velocity-ish byte, …, u16 automation value);
  - `CLSm` for audio.
- **Time base.** INFERRED, but every note of 12 649 fits it: 128 ticks per beat. The user's vocal clip matches the
  WAV's length at 130 BPM to 1e-9 beats.
- **Paths: PROVEN-FILE.** `@L@` = the user library, which equals the folder of the `.flm` in an FLM share-zip; all 93
  audio clips of the user's song resolve. `@R@` = the app resources. FL 2026's copy has only part of them (77 of 168
  demo audio refs, 135 of 271 kit samples), so missing files must be reported.
- **Decoding check.** The prototype reads all 21 projects with 0 errors. On the user's song it finds:
  - 14 channels, 130 BPM, 4/4, created 2025-07-06;
  - 1 vocal audio track;
  - Grand Piano (DirectWave "Keyboard/Grand Piano") with 4 pattern clips;
  - 6 single-sample DW Sampler channels;
  - a sliced kick track of 86 audio clips;
  - 2 automation lanes (master Level, "Mix : FX Gate");
  - effects Reverb 2 (35), Graphic EQ (7), Leveller (20), MultiFX (4), Tuner (24), Gate (27).

  The last note clip ends at beat 160 (Trompete, 147 + 13). The user's WAV lasts 160.55 beats at 130 BPM. The export
  stops after the last note clip; the vocal sample's silent tail runs to 163.78. INFERRED.

## 2. Inventory: FLM instruments and effects vs LYNX

The ids, names and categories are PROVEN-REA: the complete registry of 47 modules. The parameter counts are
PROVEN-FILE. The LYNX equivalents are INFERRED by name and purpose: no sound comparison was made in this round.

### Generators

| Id | FLM module | What it is | Params | LYNX today | Import mapping |
|---|---|---|---|---|---|
| 0 | MiniSynth | FLM's 2-osc subtractive synth with built-in FX | 44 | **MiniSynth** (`src/core/MiniSynth.*`): port of FL's MiniSynth_x64.dll, which is the desktop build of this synth | direct engine; needs a PRMS → LYNX state map (REA, slice F4) |
| 1 | DW Sampler | DirectWave player: one `.dwp` multisample or a single WAV with zones in `CSTM` | 34 | **DirectWave** (`DirectWave.*`) + the channel sampler | `.dwp` → DirectWave; a single WAV → a LYNX sampler channel. **Used by 8 of the 14 user channels** |
| 2 | GMSynth | Groove Machine synth: 3 osc, choir, 2 FX slots, `.spectra` wave shapes | 75 | **GMS** (`Gms.*`), FL's desktop GMS. INFERRED to be the same engine family | param map needed; the wave shapes ship in `Installed/GMSynthWaveshapes` |
| 19 | Transistor Bass | 303-style bass (class TB303Synth) | 14 | **TransistorBass** (`TransistorBass.*`) | direct; param map |
| 23 | SuperSaw | 3–12-voice supersaw with filter and arp | 60 | **none** (only a supersaw *mode* in other synths) | gap: stand-in (ThreeOsc unison) + note, or a new engine |
| 33 | Slicer | beat slicer for loops | 7 | **Fruity Slicer** (`Slicer.*`), FlAutoSlicer | map slices → Slicer |
| 39 | 3x Osc | FL's 3x Osc (its presets are FL `.fst` files) | 149 | **ThreeOsc** (`ThreeOsc.*`) | direct; the 149-value layout needs REA |
| 52 | SoundFont Player | SF2 player | ? | **SoundFont** (`SoundFont*.cpp`) | direct (path + patch) |
| — | built-in **drum sampler** (RSMP, not a module) | kit of up to 22 pads, each a sample with pitch/stretch/env/cut-group | 11 per sample | sampler channels; LYNX has no FPC-style pad instrument | each kit sample → its own sampler channel; or one drum-kit channel when one lands |

### Effects

| Id | FLM effect | LYNX equivalent | Note |
|---|---|---|---|
| 3 | Equalizer (Param EQ) | parametricEq / Fruity Parametric EQ 2 | 17/21 params |
| 7 | Graphic EQ | equo / eq | 9 bands. **In 8 of the 14 user racks** |
| 30 | Tuned EQ | none | gap |
| 9 | Filter | fruityFilter | param 1 = Frequency |
| 40 | Comb Filter | none (Harmless comb only) | gap |
| 15 | Stereoizer | stereoEnhancer | |
| 34 | Spacer | stereoShaper (Haas/separation) | param 3 = Separation |
| 44 | Spreader | spreader | |
| 5 / 28 | Limiter (legacy) / Limiter | fruityLimiter / limiter | |
| 8 | Compressor | fruityCompressor | |
| 36 | Multiband Compressor | fruityMultiband | 27/31 params |
| 20 | Leveller | glowLeveler / utility | 3 params |
| 27 | Gate | volumeGate | 9 params |
| 10 / 29 | Autoduck (legacy, ex-"Trancer") / Autoduck | rhythmGain / grossBeat volume | tempo-synced duck shape |
| 6 | Distortion | distortion / fruityBloodOverdrive | |
| 41 | Waveshaper | fruityWaveShaper | 21 params |
| 11 | Chorus | fruityChorus | |
| 13 | Flanger | fruityFlanger | |
| 14 | Phaser | vintagePhaser | |
| 46 | Tremolo | autoPan (tremolo) | partial |
| 43 | Wow & Flutter | lofi (partial) | gap for the exact effect |
| 42 | Tape Stop | grossBeat (tape-stop preset) | partial |
| 4 | MultiFX | none | XY-pad multi effect; gap |
| 12 / 32 / 35 | Reverb (legacy) / Reverb / Reverb 2 | reeverb1 / reeverb2 / luxeVerb | Reverb 2 in 3 of the 14 user racks |
| 16 | Trance Delay | fruityDelay3 (tempo sync) | |
| 18 | Tape Delay | fruityDelay3 / EchoSuite | |
| 25 / 45 | Pitcher / Pitcher 2 | Pitcher | |
| 24 | Tuner | analysis only, no audio change | skip on import |
| 26 | Analyzer | Spectroman | skip on import (visual only) |
| 47–51 | Note Echo, Transpose, Randomizer, Scale, Arpeggiator (MIDI FX) | channel Arpeggiator; Piano-roll tools (echo, strum, scale snap) work offline, not live | Arpeggiator → LYNX channel arp; the other four are gaps as *live* note effects |

Coverage: 38 of 47 module types (some only partly) have a LYNX engine to land on, by name or purpose. The gaps are SuperSaw, MultiFX,
Tuned EQ, Comb Filter, Wow & Flutter and the live note FX (Note Echo, Transpose, Randomizer, Scale).

## 3. Workflow strengths users praise, vs LYNX

Sources:
- MusicRadar reviews ([1](https://www.musicradar.com/reviews/tech/fl-studio-mobile-for-android-578086),
  [2](https://musicradar.com/reviews/tech/image-line-fl-studio-mobile-486982));
- Sound On Sound ([3](https://www.soundonsound.com/reviews/app-works-24));
- KVR user reviews ([4](https://www.kvraudio.com/product/fl-studio-mobile-by-image-line/reviews/3561));
- the IL manual ([5](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/FL%20Studio%20Mobile_Editors.htm));
- IL forum threads on FLM → desktop ([6](https://forum.image-line.com/viewtopic.php?p=1568332),
  [7](https://forum.image-line.com/viewtopic.php?p=1845476)).

Binary evidence is the DLL's strings and RTTI (PROVEN-REA as presence only).

| Strength | What FLM does | Evidence | LYNX |
|---|---|---|---|
| Step sequencer is the highlight | Tap steps; swing; per-step **graph editor** for velocity, pan, pitch and **note repeat up to 8×**; triplet/1/64 note tracks | SoS, KVR, IL manual; `CDrumsEditor`, "Repeat", "Swing rate" | has the step sequencer, swing and graph editor (FL-exact); **lacks per-step note repeat** |
| Drum pads for live recording | `CDrumpadsControl`, "Repeat Pad", repeat-rate X/Y (pad note repeat by finger position), count-in | IL manual, strings | **lacks** performance pads with note repeat (it has the touch keyboard) |
| Keyboard modes + chord sets | Keyboard kinds **Classic / Melodic / Strum / Chords**; chord sets (`KEYB` JSON, 8 "Minor n" sets ship) with project key | strings, `.chords` files, `CChordBuilder`, `CChordStrip` | has piano-roll strum/arp tools, chord detection (181b) and touch keyboard; **lacks** chord-set pads and the strum performance keyboard |
| Scale lock ("key locker") + **Guess Scale…** | Song-wide scale/root saved in HEAD; keyboard and editors lock to it; can guess the scale from your notes | HEAD fields, "Guess Scale...", "scale locking" strings, `CScaleGuessResultDialog` | has piano-roll scale snap; **lacks** project-wide key + guess scale |
| Simple record flow | Arm, count-in toggle, metronome level, 32-bit option, "My Recordings" | strings | has these (FL-exact recording) |
| Wave editor with envelopes on clips | Per-clip volume/pan/pitch envelopes, trim, fade, normalize | IL manual, `CAudioEditor` | has Edison-style editing; **per-clip envelopes are a gap** (FL desktop also lacks them; Ableton has clip envelopes) |
| Gamepad control | Full gamepad map (`gamepad.json`, hints overlay) | strings | **lacks** |
| Share as zip with samples | The user's file came this way | user zip | LYNX has project packaging; can **read** such a zip directly (slice F1) |
| Complaints | No editable FLM → desktop path; "instruments change"; "nothing works" | IL forum [6][7], MusicRadar | issue #88. The import plan below targets exactly this |

## 4. Best way?

- **FL:** FL desktop runs FLM projects *inside* the FL Studio Mobile plugin, or exports FLP where each instrument is
  an opaque "FL Studio Mobile Rack" (round 178a). The patterns are not native FL patterns that you can edit with FL's
  own instruments. [STATIC 178a; web]
- **Ableton:** no FLM support. Users export MIDI/stems from FLM. [web]
- **Users:** users want an editable, faithful transfer and complain that instruments change [6][7]. Issue #88 asks for
  the same.
- **Verdict:** a **native conversion** into LYNX channels, patterns, playlist clips, audio clips and automation, with
  each FLM module mapped onto LYNX's engine, beats FL's own path.
  - This is not FL-exact parity: FL has no native .flm import. So it is a **LYNX-own feature**, offered as an import
    command and changing no default. → to the user as option A (recommended).
  - Option B, hosting the FLM engine itself, is not possible: it is Image-Line's DLL.

## 5. Proposed plan: `.flm` import slices (not started; waiting for the OK)

Each slice gets a unit test built from **synthetic fixtures written by the test itself**. The user's song and FL's demo
files are not ours to commit. There is also an optional local test that runs on the FL `Installed/` demos and the
user's zip when they are present (skipped otherwise), like the FL Data path tests.

| Slice | Scope | Done when |
|---|---|---|
| **F1 Reader + arrangement** | `src/core/FlmImport.{h,cpp}`:<br>• chunk walker and version-tolerant readers for HEAD/META/TDIV/RACK/CHNL/TRKH/CLIP/EVN2/sample bodies<br>• tempo, time signature, title<br>• note lanes → one LYNX channel + patterns: content id = pattern, loop length = pattern length, clip → playlist clip with length/offset/loop<br>• drum lanes → one sampler channel per kit sample, notes on step tracks<br>• audio lanes → audio clips (offset, length, source length)<br>• path resolution `@L@` = .flm folder, `@R@` = FL's FLM `Installed/`<br>• MissingFiles report<br>• open a share-zip directly via `Inflate` | the user's song opens with 14 channels, every clip at the right beat, 93 of 93 audio clips found; synthetic round-trip tests pass |
| **F2 Mixer basics** | rack volume/pan (RPRM 0/1), master level, channel order, AUX (kind 0x80) tracks + SEND levels, mute/solo after REA confirms CHHD flags | levels match FLM within the mapped curve; one REA pass for RPRM/SEND/CHHD fields |
| **F3 Samplers** | DW Sampler single-WAV → sampler channel (key range from ADD1, sample PRMS pitch/volume, STR1 stretch/pitch); `.dwp` → DirectWave; drum-kit sample params | the user's 6 sampler channels and Grand Piano play the right samples |
| **F4 Engines** | PRMS → state maps, one engine per sub-slice: MiniSynth, TransistorBass, 3x Osc, GMSynth, SoundFont, Slicer. Each needs an REA pass on that module's parameter table | render A/B of the demo projects' synth channels, FLM plugin in FL vs LYNX, under the GUI/FL locks |
| **F5 Effects** | map the covered effects (section 2) with parameter maps; uncovered ones → bypassed placeholder + note in the import report | every user effect lands; report lists the rest |
| **F6 Automation** | automation lanes → LYNX automation clips on the mapped target: value u16/65535, point shapes after REA of the shape codes | the master Level and FX lanes of the user song follow the curve |
| **F7 #88 FLP path** | reuse the rack decoder for the "FL Studio Mobile Rack" 0xD5 state inside `.flp`, plus 178a's mixer fix (inserts without 0xEC/0x62) | an FLM-exported FLP opens like the .flm |
| **F8 Check** | render the user's song in LYNX and compare onsets and section timing with the user's WAV (not the sound: different engines) | onsets within one block, sections aligned |

Order: F1 → F3 → F2 → F6 → F5 → F4 → F7. F1 + F3 already give the user's song a playable, editable arrangement: its
channels are almost all samplers and audio.

Open REA items, each closed inside its slice:
- EVN2 bytes 14–19 (velocity scale, pan, fine pitch, per-step repeat);
- RPRM [2..5];
- SEND/CHHD flags;
- the automation shape codes;
- the per-engine parameter tables.

## 6. Workflow ideas for the user (LYNX-own options; add, never remove)

None of these is FL-desktop parity, so none changes a default. For each one the user decides yes, no or later.

1. **Performance pads with note repeat.** A pad view for any drum/sampler channel, with repeat rate set by finger
   position or by a rate menu, and a count-in record. It draws on FLM's Drumpads and Ableton's Note Repeat.
2. **Project key + scale lock + "Guess scale".** A song-wide key/scale, stored in the project, that drives piano-roll
   snap, the touch keyboard and chord tools. Guess scale works from the notes in the project. The FLM-imported key
   lands here.
3. **Chord-set / strum keyboard.** Touch-keyboard modes Classic / Melodic / Strum / Chords, with the chord sets
   imported from `KEYB`.
4. **Per-step note repeat** in the step sequencer's graph editor (FLM: up to 8×).
5. **Per-clip envelopes** on audio clips: volume, pan and pitch (FLM wave editor, Ableton clip envelopes).
6. **Gamepad control** (FLM ships a full map).

## 7. Cleanup

- REA: one session, FL Studio Mobile_x64.dll, opened under `~/.cache/lynx-ghidra.lock`. It was closed with
  close_binary at 04:17, and the lock was released the same minute.
- No GUI, FL, LYNX or build process was started.
- The work files are in `/mnt/e/lynx-work/flm182c/`:
  - the extracted user zip;
  - the walker and prototype reader;
  - the REA pseudocode notes in `rea/`, kept for the study only and never copied into LYNX.

## 8. Build log (after the plan was approved, 2026-11-08)

The coordinator approved option A: native .flm import as an added command (File › Open, and the open dialog's
`*.flm`), in the slice order F1 → F3 → F2 → F6 → F5 → F4 → F7, with F8 as the check.

### F1 + F3 (branch `feature/flm182c-f1`)

- `src/core/FlmFormat.{h,cpp}` is the clean-room C++ reader of the structure in FLM_FORMAT.md. It is
  version-tolerant: each field is read only when its chunk is long enough. A nested container that runs short keeps
  what came before it. Only a top-level chunk past the end of the file fails.
- `src/core/FlmImport.{h,cpp}` turns the project into a Song (header comment). The import report and the Missing files
  window are the .flp/.als ones. FL's FLM resources (`<FL>/Plugins/Fruity/Generators/FL Studio Mobile/Installed`) are
  found from FL's install folder.
- F3:
  - a DW Sampler with one sample becomes a LYNX sampler, keeping its key range;
  - a DW Sampler with a `.dwp` program becomes LYNX's DirectWave: the file is "DwPr" + DirectWave's own state, and
    its zone files are looked for in the program's folder;
  - a drum kit becomes one sampler channel per sample.
- Paths saved on the device (Android `…/com.imageline.FLM/files/`, Windows `…/LocalState/`, the app's `Resources/`)
  are mapped onto the project folder and FL's resources.
- Wiring:
  - the open dialog lists `*.flm`, and `.flm`/`.songtmpl` take the .flp import path (progress panel, samples,
    report);
  - an FLM share-zip opens like a project package: `ProjectPackage::extractZip` finds the `.flm`, and its
    `My Samples/…` tree sits beside it;
  - `--render` / `--export-*` accept it too;
  - the title shows "(imported from FL Studio Mobile)", translated in all 14 tables.
- Tests (`tests/FlmImport182cTests.inc`) use synthetic projects written field by field:
  - structure;
  - damage: a truncated file, an oversized nested chunk, 300 random-noise rounds;
  - the import: inserts, sampler and key range, kit channels, missing file under FL's resources and its relocation,
    patterns and loop passes, offsets, the audio clip's offset and pitch, report entries.

  Two opt-in tests run on files that stay on this PC: `LYNX_FLM_DIR` + `LYNX_FLM_RESOURCES`, and DirectWave with
  FL's `Electric.dwp`.
- Local check (mini harness with ASan/UBSan on the reader and importer; the full build-local below):

| Project | Result |
|---|---|
| User's MONTAGEM XONADA REMAKE.flm | 130 BPM, 14 inserts, 13 channels (6 samplers with their sample), 21 patterns (751 notes), 168 clips (93 audio, all found), end 163.784 beats. 1 missing: `Grand Piano.dwp`, a resource FL's copy lacks |
| 11 FL demos + 9 templates | all import. Missing samples are FLM resources absent from FL's copy, or paid EXP packs |

Not yet (later slices):
- channel volume/pan and sends (F2);
- automation lanes (F6), listed in the report;
- effects (F5), listed as dropped;
- MiniSynth / GMSynth / SuperSaw / TransistorBass / 3x Osc / SoundFont / Slicer engines (F4): they play the built-in
  synth for now, listed;
- the Mobile Rack inside .flp (F7).

The six workflow ideas in §6 go to the user as options, with the "Simple" layout proposal (the coordinator, 2026-11-08).

### F2 mixer basics (branch `feature/flm182c-f2`, on top of F1 + F3)

A second REA turn (04:56–05:19) proved the level and route encoding (FLM_FORMAT §3–4):
- **Volume:** RPRM [0] → the channel's insert. The FLM fader law gives gain = 10^(3.3·v^0.6 − 3), i.e.
  66·v^0.6 − 60 dB. FLM's top (+6 dB) is capped at LYNX's +5.6 dB and noted in the report.
- **Pan:** RPRM [1] → the insert's pan (2v − 1).
- **Routes:** SEND (target channel id, …, level) → the insert's route to the Master and its level (masterLevel), or
  a send to the target's insert. A channel with routes but none to the master doesn't reach it directly.
- **Instrument channels** sit at 0 dB (channel volume 1.0), because FLM's level lives on the rack, now the insert.

Mute/solo flags (CHHD) stay INFERRED and are not imported.

### F8 early sanity check (cloud build of 773f81ce9, run 42463458560)

On the cloud build (LYNX_TEST_MODE=1, a fresh profile): `--selftest` result=OK; `--render` of MONTAGEM XONADA
(76 s) result=OK with 11/11 samples loaded. That render was compared with the user's WAV, an FLM export at 48 kHz
lasting 74.1 s, by `/mnt/e/lynx-work/flm182c/compare.py` (10 ms RMS envelopes):

| Measure | Result |
|---|---|
| Timing | aligned: best envelope lag 0 ms; matched onsets median error 0.0 ms, p90 10 ms (one envelope hop) |
| Onsets | 56 of the reference's 159 have a LYNX onset within 30 ms (35 %). Envelope correlation 0.34 |
| Level per 8 bars (LYNX − reference) | +0.2, +0.8, +1.8, +1.1, +4.9 dB (bars 1–40, where the export ends) |

The differences are expected while the later slices are missing:
- the Grand Piano program (`Keyboard/Grand Piano.dwp`) is not in FL's copy of FLM's resources, so it plays LYNX's
  built-in synth (F3/F4);
- no FLM effects yet (F5): MultiFX on the master, Gate, Reverb 2, Leveller, Graphic EQ;
- the "Mix : FX Gate" automation is a module-parameter lane (F6, after F5). The +4.9 dB in the last section is where
  FLM's gate pumps the piano;
- FLM's channel levels reach +6 dB, and LYNX's fader tops out at +5.6 dB.

Shots of the import (`/mnt/e/lynx-work/flm182c/shots/`):
- `xonada-playlist.png`: the whole arrangement;
- `xonada-mixer.png`: the 13 inserts with FLM's levels, Master −7.3 dB;
- `xonada-desktop-mixer.png`.
### F6 automation, first part (branch `feature/flm182c-f6`, after 0.1.25)

- The rack's own lanes (module 0: [0] Level, [1] Pan) → LYNX automation clips on the channel's insert, the
  Master's for the master channel. Each FLM clip is one clip, with its points repeated over its loop passes. Values
  are u16 / 65535: Level goes through FLM's fader law onto LYNX's fader position, Pan is direct.
- The point shapes (the negative key codes) are not decoded yet: straight lines, listed in the report.
- Lanes on module parameters wait for F4/F5 and are listed.

### F2 + F6, round 187d (branch `feature/flm187d`, 0.1.26)

REA: one 13-minute turn on FL Studio Mobile_x64.dll under `~/.cache/lynx-ghidra.lock` (07:50–08:03), plus a survey of
every EVN2 point in the 20 sample projects and the user's song. Results in FLM_FORMAT §3, §3.2 and §4.

- **Rack parameters (PROVEN-REA 0x18059eb20).** Ids 0 Level, 1 Pan, 2 Mute, 3 Solo, 4 Pitch Bend, 5 Aftertouch,
  7…133 MIDI CCs, 0x1000 + id "Enable Send (to …)", 0x2000 + id "Send Level (to …)".
- **Mixer (F2).** On import:
  - RPRM Mute and Solo set the insert's mute and solo (the master's Mute the Master's);
  - the held Pitch Bend (2^(4v − 2), round 182c's setter) moves a sampler channel's pitch knob by 48·v − 24 semitones.
    On another instrument it is reported;
  - Aftertouch is reported;
  - the routes from round 182c are unchanged. Channels without SEND chunks (the 4.x demos) keep the route to the
    master. INFERRED: FLM 5 writes that route out as a SEND to id 0, so its absence means the default route.
- **Automation (F6).** The point shapes need no maths: FLM saves each clip's curve already sampled at every 1/128 of
  the value range, with control points marked by a positive key, so straight lines through the saved points are FLM's
  curve. The "straight lines" approximation note is gone from the report.
  - Imported: Level → insert volume, Pan → insert pan, Send Level (to X) → that route's level knob (`mixerSend`,
    FLM's fader law), for the master channel too.
  - Reported, one line each: Mute/Solo lanes (LYNX automates neither), Pitch Bend/Aftertouch lanes, MIDI CC lanes,
    send on/off lanes, a send level without that route, and module-parameter lanes. The last group waits for F5/F4:
    across the 21 projects there are 73 module-parameter lanes against 15 rack lanes. The module lanes are led by
    FX Filter "Frequency" (32), MiniSynth "FLT Freq" (9) and DirectWave "Pitch" (3).
- **Tests.** `tests/FlmImport187dTests.inc` writes its own project. The opt-in `flm187d_real_project_reports`
  (LYNX_FLM_DIR + LYNX_FLM_REPORT) prints each real project's import report. The user's song: Level automation 1,
  module lanes 1 (Gate Mix, waiting for F5), and no change to its 14 inserts.

### F5 effects, round 188f (branch `feature/flm188f`, 0.1.26)

REA: Ghidra 12.1.4 headless on FL Studio Mobile_x64.dll (sha256 c63ba7ae…), in the round's own project
(`~/ghproj/flm188f`), every query under `~/.cache/lynx-ghidra.lock`. The effect classes were found by their RTTI
vtables (FXEqualizer 0x1809fac30, FXGraphEQ 0x1809fa3b8, FXFilter 0x1809faf98, FXTrancer 0x1809ff130 = Autoduck
legacy, FXReverb 0x1809fd998, FXReverb3 0x1809fe2f0 = Reverb 2, FXPad 0x1809fca98 = MultiFX). Per module: slot 65 is
the parameter setter, 67 the defaults, 68 the names, 69 the display text, 78 the audio process. Pseudocode notes stay
in `/mnt/e/lynx-work/flm188f/rea/` (study only). Usage in the 21 sample projects (20 FL demos/templates + the user's
song): Equalizer 59, Filter 44, Autoduck legacy 43, Reverb legacy 36, Reverb 2 26, MultiFX 24, Graphic EQ 20 (8 in the
user's song).

**Common to every module (PROVEN-REA 0x180799350, 0x1807988a0).**
- Parameter 0 is Enable: below 0.5 the module is bypassed.
- The SMPR values are the reserved ids 0x7FC0 Post Gain, 0x7FC1 Mix and 0x7FC2 Wet Pan. Automation lanes reach the
  first two as −3 and −2 (a sample's "Mix : FX Distortion" lane has −2).
- How Mix and Post Gain are applied is not traced. INFERRED: Mix is a dry/wet blend like a LYNX slot's mix.

**Equalizer (PROVEN-REA setter/coefficients 0x18061d9a0, design 0x18060de70, process 0x18061de80).**
- Each of the 4 bands: gain (v − 0.5)·24 dB as the knob shows it; frequency 2^(9.25·v + 5) Hz (32 Hz…19.5 kHz;
  the presets "Low Cut 50Hz" / "Low Cut 200Hz" sit at 0.07 / 0.2863); bandwidth 7·v + 0.25 octaves; kind ⌊2v⌋ =
  0 low shelf, 1 peak, 2 high shelf; channel ⌊4v⌋ = 0 both, 1 left, 2 right, 3/4 left and right with opposite gains.
- The design is the cookbook's bandwidth form (α = sin ω · sinh(ln2/2 · BW · ω/sin ω)), but the amplitude is
  A = 10^(dB/20) where the cookbook has 10^(dB/40): **every band moves twice the dB its knob shows** (±24 dB).
- The bands are designed at twice the rate (ω capped at 3) and run 2× oversampled: each input sample is fed twice,
  every second output is kept, and the four bands run as a pipeline with one sample of delay per band.

**Graphic EQ (PROVEN-REA 0x180619f90, 0x1806198a0, 0x180619bd0).** 8 bands at 80, 160, 375, 750, 1500, 3000, 6000
and 12000 Hz: a low shelf, six peaks and a high shelf, 1 octave wide, gain 24·v − 12 dB on the same doubled amplitude
law, designed at the running rate, run as plain biquads.

**Filter (PROVEN-REA setter 0x18061fa80, coefficients 0x18061f340, designs 0x18060dae0 / 0x18060dc80 /
0x18060d940, process 0x18061f6b0).**
- Frequency 2^(10.3·v + 4) Hz (16 Hz…20.2 kHz). Q = 2v² + 0.8. Type ⌊2v⌋: low-pass, band-pass, high-pass.
- Slope ⌊2v⌋: 24, 48 or 96 dB per octave, i.e. 1, 2 or 4 pairs of identical biquads.
- The low/high-pass biquad has its feedback at α = sin ω/Q and its feed-forward at α = sin ω/2Q. That is the
  cookbook's filter at Q/2, times (1 + s/Q)/(1 + s/2Q), with s = sin ω.
- The low-pass output is scaled by 0.8 per biquad pair (0.8^pairs).
- The band-pass is the cookbook's constant-skirt form, with a peak level of Q.
- Two details are INFERRED: the low/high-pass zero coefficients (+2 / −2, set in the constructor), and that the
  design call returns sin before cos.

**Autoduck legacy (FXTrancer, PROVEN-REA process 0x18063fd00).**
- The phase is the song position in beats, modulo 1. The attack point is a = 0.5·Attack + 0.001.
- The envelope rises p/a up to a, then falls 1 − (p − a)/(1 − a). The gain is 1 − Ducking² · env^((Curve + 1)/2).
- A one-pole smooths the gain, at 0.01 per sample.

**Reverbs (PROVEN-REA FXReverb setter 0x180636f30, FXReverb3 setter 0x18063ab20 and process 0x18063a5a0, engine
`fxReverb` 0x180735840 / setter 0x180736700 / coefficients 0x180737690).**
- Both reverbs drive one engine. It is FLM's copy of the plate behind FL's Fruity Reeverb 2 (REA_rodada49): the same
  room scaling ⌊fs·(room + 15)/115⌋, the same twelve line proportions (142, 107, 379, 277, 672, 4453, 1800, 3720, 908,
  4217, 2656, 3163 per 20000), the same early-reflection taps (×1.5/44100, weights 0.5 / 0.36) and the same decay law
  (ln 0.001, T = decay·0.1 s).
- Its controls are integers ⌊lo + v·(hi − lo) + 0.5⌋ from a (min, max, default) table. Inner controls:
  0 input (tenths of dB), 1 low cut 19…3000 Hz, 2 high cut 5…221 (×100 Hz), 3 predelay in ms (0…250 legacy,
  0…1000 Reverb 2), 4 room 1…100, 5 diffusion 0…100, 6 bass 2…30 (×0.1), 7 crossover 25…2000 Hz, 8 decay 1…200
  (×0.1 s), 9 damping 5…221, 10 separation 0…200, 11 dry, 12 early reflections, 13 wet (tenths of dB), 14…17 mute
  switches for input, dry, early reflections and wet.
- Engine gains: 10^(t/200). The early reflections are scaled by 0.5, the wet by 0.3. The predelay is fs·ms/1000
  samples.
- Low cut, high cut, room, diffusion, decay, damping and crossover use FL Reeverb 2's own ranges and defaults
  (75 Hz, 4 kHz, 50, 100, 1.5 s, 4 kHz, 500 Hz).
- The legacy Reverb's knobs map to the inner controls as follows: DECAY (cubed) → 8, HIGH DAMP → 9, MIX → 13,
  PREDELAY → 3, ROOM → 4, DIFFUSION → 5, WIDTH → 10, LO-CUT → 1, HI-CUT → 2, INPUT → 0, EARLY → 12, DRY → 11,
  13…16 → the switches, BASS → 6, CRSV → 7, then the modulation speed and amount.
- Reverb 2 maps HIGH CUT → 2, LOW CUT → 1, SIZE → 4, DECAY → 8, DIFFUSION → 5, DAMPING → 9, DELAY → 3 (or tempo
  sync), BASS → 6, CROSS → 7, plus the modulation. It mixes DRY / ER / WET itself, each at 1.25·v, on the engine's
  default ER (−3.5 dB) and wet (−12 dB) levels. SEPARATION is a mid/side width (0.5 = unchanged), PAN a wet
  balance, and SIDE INPUT feeds the plate L − R.

**MultiFX (FXPad): no LYNX equivalent.** It is an XY-pad effect with 7 "FX Kind"s, Axis X/Y and an LFO. The decision
was report-only: it stays listed in the report, and no new LYNX effect is made for it.

**What the import does (`src/core/FlmEffects.{h,cpp}`, called from FlmImport).**
- **Placement.** Each mapped module becomes LYNX slots on the channel's insert, in rack order. Enable becomes the
  slot's on switch.
- **Equalizer and Graphic EQ** become LYNX's 7-band parametric EQ (cookbook biquads, LYNX law):
  - band dB = 2 × FLM's knob, capped at ±18 (reported);
  - peak Q = 1/(2 sinh(ln2/2 · BW · ω/sin ω));
  - shelves use the cookbook slope S that gives FLM's α: 1/S = 1 + (4 sinh² − 2)/(A + 1/A);
  - a Graphic EQ with more than seven bands that aren't flat takes two slots.
  - Measured: the Graphic EQ slot is FLM's filter to < 0.001 dB, 20 Hz–20 kHz. The Equalizer's design differs, since
    FLM's is at twice the rate:
    - low shelf: < 0.001 dB everywhere;
    - peaks: < 0.002 dB at 1 kHz, up to 0.2 dB near 15 kHz;
    - a 5.4 kHz high shelf: 0.1 dB at 1 kHz, 0.7 dB at 19 kHz.
  - Not reproduced: one-channel bands (reported) and the 1.5-sample pipeline delay.
- **Filter** becomes the parametric EQ:
  - one single-biquad cut or band-pass band per FLM biquad, at Q/2 for the cuts and Q for the band-pass;
  - FLM's level offsets go on the EQ's output: ((1 + s/Q)/(1 + s/2Q))^N · 0.8^pairs, or Q^N for the band-pass;
  - 96 dB/oct takes two slots.
  - Measured: < 0.001 dB against the rebuilt FLM chain (low-pass 24, band-pass 48, high-pass 24, low-pass 48).
- **Autoduck legacy** becomes LYNX's rhythm gain:
  - a one-beat period, with 12 points of the full-depth curve: 4 on the rise, 7 on the fall;
  - Ducking² on the slot's mix, which is exact, since 1 − m·env = a dry/wet blend at m;
  - the 2 ms smoothing and the exact power curve between the points are not reproduced (reported).
- **Reverb legacy and Reverb 2** become LYNX's exact Reeverb 2. Per control:
  - the same-unit controls are copied as they are;
  - bass ×10;
  - predelay round(ms·0.384);
  - SIDE INPUT → FL's Mid/Side input.
  - Levels: FLM's engine ends with a mid/side stage on its wet and early-reflection outputs (0x1807383d0):
    L = (l + r) + (r − l)·σ, R = (l + r) − (r − l)·σ, σ = separation/100. At the default σ = 1 this is the two
    sides swapped at twice the level. FL's matrix gives the same blend with its gain at 2× FLM's and f = 1 − σ:
    FL separation 64·log₅(4f + 1) for f > 0, 64·f below. The legacy WIDTH is converted this way. The side swap is not
    reproduced (the plate's two sides are statistically alike).
  - FLM's linear gains go through the inverse of FL's level law, (e^(2.3979·p/128 or p/256) − 1)·0.1, to FL's
    integer controls. When a level is beyond FL's range (Reverb 2's default early reflections are), all three are
    lowered by one factor and a LYNX utility after the reverb gives the gain back (the user's song: +5.7 dB). FL's
    integer steps leave up to about ±0.2 dB.
  - Reported, not reproduced: Reverb 2's own SEPARATION other than 0.5, its wet PAN, modulation and tempo-synced
    predelay.
  - INFERRED, to confirm by a render: that FLM's plate outputs (before its gains) equal FL's. The structure and
    constants are the same; the level scaling between the two builds is not proven. (Settled in round 188h: the
    levels, the mid/side law and the Reverb 2 mix above are corrected there.)
- **Automation (F6 for modules).** A lane on a mapped module's parameter is imported through the same mapping:
  - per saved point, the module is mapped again with that value, and every LYNX parameter that moves becomes its own
    automation clip (`effectParam`, or `effectMix` for Autoduck's Ducking and the module Mix);
  - the first clip takes the lane's row, the others get rows of their own right after it. For example, a 24 dB
    Filter's frequency moves two bands and the output level;
  - lanes whose value changes the slot layout (a Filter's slope) and Enable lanes are reported. So are lanes on
    unmapped modules (MultiFX's Axis X) and on instruments (F4).
  - In the 21 sample projects, 41 of the 73 module-parameter lanes are now imported (the Filter's Frequency alone
    is 32). The other 32 are instrument lanes (F4), unmapped effects (MultiFX, Trance Delay, Spacer, Leveller, …)
    and one Autoduck Curve lane (a whole-curve knob).
- **Real projects** (`/mnt/e/lynx-work/flm188f/demo-reports.txt`): all 21 import. Mapped: Equalizer 59, Filter 44,
  Autoduck legacy 43, Reverb legacy 36, Reverb 2 26, Graphic EQ 20. The user's song gets its 8 Graphic EQs and
  3 Reverb 2s; its 2 MultiFX, 2 Leveller and 1 Gate are listed.
- **Sound check:** there are no per-effect FL Studio Mobile renders. The only FLM render is the user's full mix, so
  the EQ and filter slots were checked against FLM's own designs, rebuilt from the REA formulas (the response tests
  above). A render A/B in FL's FLM plugin is still open.
- **Tests:** `tests/FlmImport188fTests.inc`, with the response comparisons above and the import of a written project.

Open (next rounds): render A/B of the reverbs and the Equalizer against FL Studio Mobile in FL (GUI/FL locks) to
settle the reverb level scaling; Mix/Post Gain application; the rest of the effects (Limiter, Distortion, Leveller,
Trance Delay, Tape Delay, Compressor, Spacer, …); F4 engines.

### F5 effects, round 188h (branch `feature/flm188h`, 0.1.26)

REA: the same Ghidra project and lock as 188f. Classes by RTTI: FXPad (MultiFX) 0x1809fca98, FXTranceDelay 0x1809fee28,
FXDistort 0x1809fa060, FXLimiter (legacy) 0x1809fbfb0, FXLimiter2 (Limiter) 0x1809fc2f8, FXLeveller 0x1809fbcc0,
FXGate 0x1809fb798, FXTapeDelay 0x1809feae0 (engine CTapeDelay 0x180a16d88). Formula specs (study notes, no code)
are in `/mnt/e/lynx-work/flm188h/rea/spec-*.md`. None of these effects is reproduced exactly by an existing LYNX
effect, so each became a **Pocket** engine (`src/core/PocketFx.{h,cpp}`, `PocketMultiFx.cpp`, `PocketTape.h`): LYNX
names, FL Studio Mobile's role, parameters = the module's own 0…1 values in its order (LYNX k = FLM k + 1; Enable is
the slot's switch), so the import copies them.

**Module wrapper: Mix, Post Gain, Wet Pan (PROVEN, 0x180799750, its only caller 0x18059d3b0).** The two 188f
assumptions are settled:
- Mix is a linear dry/wet crossfade, out = (1 − a)·dry + a·wet (a LYNX slot's mix). Mix exactly 0, or the module off,
  skips the module.
- Post Gain is 10^(2g − 1) = 40g − 20 dB (±20 dB, 0.5 = 0 dB) on the module's output before the blend. The import puts
  it on a LYNX utility after the module (exact at Mix 100 %, which every Post Gain in the 21 projects has; with a lower
  Mix it is reported).
- Wet Pan is a linear balance of the wet amount (never off-centre in the 21 projects; reported).
- The three are smoothed per sample (Mix and Pan 0.001, Post Gain 0.01); the dry is not delay-compensated.

**Reverb levels (PROVEN, corrects 188f).** FLM's plate is fed 0.5·(L + R) where FL's is fed L + R; its mid/side output
stage is L = (l + r) + (l − r)·σ, R = (l + r) − (l − r)·σ (no side swap): at σ = 1 that is 2l / 2r, which cancels the
½. So FL's gains equal FLM's, and 188f's slots were 6 dB too hot. The legacy Reverb runs the engine in place and the
engine adds onto its buffer, so its dry is 1 + input·DRY (188f gave none). Reverb 2 sets the engine's levels to 0 dB and
mixes two of its outputs × 1.25: in FL plate units wet 0.375·WET, early reflections 0.625·(ER + WET), dry 1.25·DRY.
FLM's input diffusers 2 and 4 use a second feed-forward coefficient and its early reflections are taken after the
second diffuser: a tilt of at most ~0.4 dB that a gain can't match (not reproduced). FL's Mid/Side input also inverts
the right output, FLM's SIDE INPUT doesn't (reported).

**Pocket Distortion (module 6).** Drive d, BitCrush, Gain (24v − 24 dB), Stereo s, Kind ⌊3v⌋, Bias b. Per side
x = in + (2(b − 0.5))⁵, the drive × min(1, 2s) left / × min(1, 2(1 − s)) right:
- Soft: g = 10^(5d·f), k = g − 1, y = gx/(k|x| + 1);
- Tanh: k = 10^(4d·f) − 0.99, y = tanh(kx)/tanh(k);
- Exponential: k = 20d²f, y = 1 − 10^(−kx)(1 − x) (x ≥ 0), 10^(kx)(x + 1) − 1 (x < 0);
- Hard clip: clamp(x·10^(3d²f), ±1).
Then a sample-and-hold every crush⁴·256 + 1 samples, × Gain, and with a bias a one-pole high-pass at 220.5/fs.

**Pocket Limiter Classic (module 5, "Limiter (legacy)").** Peak max(|L|, |R|); above the threshold (−24…0 dB) the
gain drops at once to threshold/peak and recovers along 1 − cos²(πt/2)(1 − g₀), t advancing by
(100(0.2 − 0.2v)² + 1)/fs; output × out/threshold (out −24…+12 dB): automatic make-up.

**Pocket Limiter (module 28).** In Gain and Out Gain ±24 dB, Release v⁴·1.9 + 0.1 s, Threshold −24…0 dB. A 2 ms
delay line (reported as latency); the floor threshold/peak is taken at once and recovers along 1 − cos(πt/2)(1 − g₀)
in exactly Release seconds (sin in double precision); no make-up.

**Pocket Leveller (module 20).** Not a compressor: gain 72·v^0.6 − 60 dB (−60…+12), linear balance pan, both
gliding by 1 % per sample.

**Pocket Gate (module 27).** Two 256-sample rings; when they wrap, the RMS (dB) of each side decides: open above
66v − 60 dB, close (after Hold v s) when both are under threshold − 66v² dB (not below −60). The envelope ramps
linearly (Attack / Release 1000v ms), shaped by its direction's curve (2v − 1: −1 = 1 + log₁₀₀₀ x, +1 = 0.001^(1 − x),
1024-point tables through a Catmull-Rom-form cubic whose "previous" point is the point itself, PROVEN from the
instruction bytes at 0x180476a5f); Mix sets the floor 10^(−3|2v − 1|), below 0.5 inverted; the gain glides by 10 %
per sample. The output is the ring's oldest sample: 255 samples late (FLM reports 256; LYNX reports the 255 it plays).

**Pocket Trance Delay (module 16).** Time ⌊4v⌋ → 1/4, 3/8, 1/2, 3/4, 1/1 beat (D = beats·fs·60/BPM samples, whole,
up to 2 s); the loop stores 0.99·Fdbk·(input + echo); the echo is heard × Mix²; the loop filter is a one-pole low-pass
(k = 2v) below Filter 0.5 and a high-pass (k = (2v − 1)⁴) above. Modes ⌊2v + ½⌋: Stereo; Ping-Pong (the lines cross,
an input balance smoothed by 1 %); Mirror (lines of 2D, the dry left delayed by D). A Time or Mode change clears the
lines. A one-byte saved flag (CSTM; a module without it loads as on) makes Ping-Pong pan the dry too: LYNX parameter 6.

**Pocket Tape Delay (module 18).** Time v² s, Async shortens one side by up to half, the loop holds input + echo ×
Fdbk, the echo is heard × Mix × Fdbk. FLM's "tape": a loop whose phase advances by a speed 1/(t(N − 2) + 2)
(N = fs), each sample recorded with its time stamp, the previous loop read back by linear interpolation between stamps,
the speed gliding by 0.1 % per sample (a Time change bends the pitch). A new tape runs 10 s of silence first: LYNX
replays only the stamps (same result, no reads).

**Pocket MultiFX (module 4).** FX Kind ⌊7v⌋, Axis X / Y, LFO Rate ⌊9v⌋ → 1/8, 1/4, 3/8, 1/2, 3/4 beat, 1, 2 beats,
1, 2, 4 bars, LFO Amount v², LFO Shape ⌊2v⌋ (Sine, Saw, Square). Ten effect objects; the kind selects one (Delay,
Reverb, LP Filter, HP Filter, Flanger, Phaser, Panner, Vox), each fades to its target by 0.5 % per sample and all that
sound run in series (FLM's order: Flanger, Phaser, LP, HP, Delay, Reverb, Panner, Vox). The LFO phase is the song
position over the rate, set every block; most kinds read one LFO value per block.
- Delay: a line of (1.5 fs − 1)·(the doubled rate's seconds/3, halved until ≤ 1) samples ≈ the LFO Rate; X² is the
  feedback and the echo level; Y < 0.5 a one-pole low-pass in the loop, > 0.5 a high-pass; the LFO pans the echoes.
- Reverb: FLM's plate (LYNX's Reeverb 2 at FLM's levels), wet only: decay 0.75·X³, damping Y², wet 0.5 + 0.2·decay,
  room and diffusion full, low cut 317 Hz.
- LP / HP: four equal biquads (the Filter module's design), each fed by the previous one's output of the sample
  before; LP 2^(8.28v + 6) Hz, HP 2^(7.424v + 5) Hz, v = X + LFO, Q = 2Y² + 1.
- Flanger: −(L + R)/2 through the tape engine above (up to 5 ms, feedback −0.96·Y), added to both sides.
- Phaser: six first-order allpasses, 50 Hz…20 kHz by X²(LFO + 1), feedback 0.98·(0.6Y)^⅓.
- Panner: a Haas delay up to 2 ms and √Y attenuation on the far side.
- Vox: eight band-passes (Q 12) on formants blended across three vowels, × 2^(1.7(Y − 0.5)).
The silence logic (an effect that has been quiet for its limit is skipped; a deselected one that faded out is reset)
follows FLM's; "input active" = the block has a non-zero sample.

**Real projects** (`/mnt/e/lynx-work/flm188h/demo-reports.txt`): all 21 import; the user's MONTAGEM XONADA now has no
dropped effect (its 2 MultiFX, 2 Leveller and the Gate are mapped). Still listed: Chorus,
Spacer, Wow & Flutter, Phaser, Waveshaper, Compressor, Spreader, Stereoizer, Flanger, Autoduck (new), Tremolo, and the
silent Analyzer and Tuner.

**Tests** (`tests/FlmImport188hTests.inc`): each engine against values worked by hand from the laws above (curves,
crush hold, bias, the limiters' attack and recovery, the gate's floor and delay, the delays' echo times and levels,
the tape's interpolated echo, MultiFX's delay / filter / panner / reverb), the Post Gain utility and the imports.
No per-effect FLM render exists yet; an A/B in FL's FL Studio Mobile plugin (GUI + FL locks) is the next check.

### F5 effects, round 188i: A/B against FL Studio Mobile inside FL (branch `feature/flm188i`, 0.1.26)

**Method.** FL Studio 2026 ships "FL Studio Mobile Rack FX" (an FL effect that hosts an FLM effect rack) and IL's own
presets for it. Its state is the `SSLF/10LF → RACK → RMOd{HEAD DESC PRST ADD1 PRMS SMPR CSTM}` tree of §8 of
FLM_FORMAT.md; a module's type, PRMS and SMPR can be rewritten in place (the preset re-encodes byte for byte). Each
test project is FL's own "Sampler" render test with one note playing a 28 s stimulus (1 kHz stair −36…0 dB, drums,
noise bursts, sweep, music) into insert 1, where the Rack FX holds one module; FL renders it with `/R` (no input,
minimized). The LYNX side runs the same module values through `tools/fxrender` on FL's own dry render, so both sides
get identical input. Tools: `/mnt/e/lynx-work/flm188i/ab/tools` (mkflp.py, jobs1.py, lx.py).

Host facts measured on the way (not engine behaviour, the LYNX side reproduces them for the comparison):
- the rack wrapper with a disabled module is bit-transparent (−215 dB against the dry render);
- FL runs the module for one 512-sample block before song position 0 (the BitCrush hold phase proves it:
  a unique pre-roll of 512 samples fits every hold boundary of a 62.47-sample hold);
- FL's 16-bit render clips at full scale; latency (Limiter 88, Gate 256 samples) is compensated by FL.

**Scores** (round-170i scale, ESR in dB; 16-bit renders put the floor near −75 dB):

| Effect | Settings (score) |
|---|---|
| Distortion | preset 10; Drive 0.7 8 (−54.5); BitCrush 0.3 / 0.5 / 0.7 9 / 9 / 9; Tube + Bias 0.7 9; Clip + Stereo 0.9 10; wrapper Mix 50 % + Post Gain 0.65 9 (−59.4) |
| Limiter (legacy) | preset 10 (bit-identical); threshold 0.5 / 0.3 / 0.7 10 / 10 / 10 |
| Limiter | preset 10 (bit-identical); In +12 dB 10; threshold −12 dB 10; release 0.8 10 |
| Leveller | preset 10; gain 0.4 + pan 0.2 9; gain 1.0 + pan 0.9 10 |
| Gate | preset 10; thresholds −20 / −10.5 dB 10 / (6: the output is at the 16-bit floor, residual ≈ −100 dB); Mix 0.25 10; curves 10 |
| Trance Delay | preset (Ping-Pong) 10; Stereo + filter 10; Mirror 10; Ping-Pong + input pan + dry pan 10; 1/4 beat + feedback 0.9 10 |
| Tape Delay | preset 10; three more settings 10 / 10 / 10 |
| MultiFX | preset 10; the seven other kinds 8 (Reverb, −35), 10, 10, 10, 10, 10, 9 (Vox); **with the LFO 6 / 7 / 4** |

**Fixed:** BitCrush's hold length. FLM computes v⁴ as (v²)²; the last-bit difference from ((v·v)·v)·v moved
fractional holds (BitCrush 0.3 and 0.7 went from −19.5 / −23.0 to −68.9 / −68.8 dB). Regression test in
tests/FlmImport188hTests.inc.

**Open:** MultiFX with the LFO on. The LFO is song-locked and in phase (`fxrender --songpos`), its square edges land
in the same 10 ms windows, but inside a window FLM switches earlier and its swept filters differ; no block size or
position offset reproduces it. Needs one REA pass on how FLM applies the LFO within a block (per-sample update or a
coefficient ramp).

### F5 effects, round 189h: MultiFX's LFO inside FL (branch `feature/flm189h`, 0.1.26)

The LFO settings were not an engine difference but how FL drives the module, found by fitting the square-LFO edges of
`mfx_L3` (HP Filter, 1 kHz stair) and confirmed on all three settings:
- **FL runs a hosted FLM rack in chunks of one song tick (1/96 beat)** cut on the song's grid (chunk k starts at sample
  round(k · tick)), and each chunk gets its tick's exact position. FLM reads its per-block LFO once per chunk. The
  falling edges of a square LFO land on the beat, the rising ones one tick (229.7 samples at 120 BPM) after the half
  beat: the chunk at the half beat starts on ph = 0.5 exactly, and FLM's square switches on ph > 0.5. A 512-sample host
  block (fxrender's default) put both edges up to 511 samples late, which was the 188i mismatch. The 48 and 192 grids
  and floor/ceil rounding all fit worse (fxrender `--ticks <ppq> <mode>`).
- **The "input is active" flag is always true inside FL.** With LYNX computing it from the block (FLM's own tracks
  derive it from playing voices, spec-multifx §2), the Flanger skipped the silent gaps of the burst segment and its
  tape speed did not glide on: bursts −3.5 dB. Always active: −52.7 dB. The flag only gates skipping and tails; no
  static setting changes.

LYNX's Pocket MultiFX now cuts every host block on the same grid, carries a chunk that a block boundary cuts with the
LFO value it started with, gives grid chunks their exact tick position and never skips silent input. Its output is
the same to the bit for host blocks of 64, 100, 256, 512, 1000, 2048 and 4096 samples (before, it changed with the
block). The Reverb kind has no LFO and runs once per block (identical output). Test: `flm189h_pocket_multifx_tick_chunks`.

| Setting | 188i | 189h |
|---|---|---|
| mfx_L1 LP Filter, sine 3/8 beat, amount 0.36 | 6 (−9.5) | 9 (−59.5) |
| mfx_L2 Flanger, saw 1 beat, amount 0.64 | 7 (−11.8) | 10 (−72.4) |
| mfx_L3 HP Filter, square 1 beat, amount 0.25 | 4 (−6.3) | 9 (−68.2) |
| mfx_D, K1–K7 (no LFO) | 10, 8, 10, 10, 10, 10, 10, 9 | 10, 8, 10, 10, 10, 10 (−70.2 → −80.9), 10, 10 (−65.3 → −72.0) |

Not reproduced (INFERRED from the fit, not REA): whether FL or the rack wrapper makes the tick chunks. FLM's own app
calls with its device buffer and voice-derived activity; LYNX follows the measurable host (FL).

### F4 MiniSynth (rounds 182c / 190n, branch `feature/flm190n-minisynth`, 0.1.26)

FLM's MiniSynth parameters 1…43 are FL's desktop MiniSynth parameters 0…42, in the same order (the two name tables,
FLM_FORMAT §7.1 and REA_rodada41 §1.2; PROVEN-REA: CMiniSynth's setter 0x1806af050 forwards parameter n to the engine
setter with n − 1). FLM's parameter 0 is the module's Enable. The importer writes the state the way the desktop plugin
saves it ("SSLF" + .flms: HEAD with a name, DATA "0rev" + 43 floats). The engine's wave tables come from `oscdata.dat`,
a file byte-identical in FLM's resources and in FL's Minisynth folder (checked with cmp).

FLM runs **its own build of the same engine** (PROVEN-REA, CMiniSynth vft 0x180a057c8, engine call 0x1806a89a0; every
formula of REA_rodada41 was re-read, the filter, envelopes, LFO, oscillators and effects are the same constant for
constant). The differences, which LYNX's MiniSynth now has as its "FLM build" (`ChannelDef::modelParams = { 1 }`,
`miniSynthIsFlm`; the desktop path is unchanged and renders bit-identically to before):

| | Desktop MiniSynth | FLM's module | LYNX |
|---|---|---|---|
| velocity | MIDI byte from Init.Vol, / 127 | the note's velocity 0…1 as it is (slot 71, 0x1806ae9d0) | engine switch |
| level / pan | host law A: centre 0.7071 per side | linear balance, centre 1 / 1 (+3.01 dB) (0x1806ab0a0) | engine switch |
| voices | 99 (host max-polyphony event) | 5 | engine switch |
| LFO → OSC Mod | m + 12·L | m + L, clamped to ±1 (0x1806aab90) | engine switch |
| first note's glide | from key 0 | from its own key | engine switch |
| effects' silence gate | "a voice was active" per engine call | per 32-sample block: the effects stop 0.5 s after the last voice | engine switch |
| MW Dest | int(p): filter, LFO amount | int(2p): filter, LFO amount, OSC Mod | converted at import (OSC Mod → wheel amount 0) |

Not modelled: FLM's per-voice pitch / pan / velocity offsets (slot 73, from FLM's event queue), the instant kill on a
negative velocity, last-bit summation orders. OPEN: the scale of the velocity byte stored in a .flm note (the importer
uses byte / 255, unchanged).

**A/B inside FL (round 190n).** Projects made from FL's own render test `MiniSynth.flp` (only the state floats, the
notes and, for the FLM side, the channel's plugin events from FL's `FL Studio Mobile Rack.fst` preset were changed),
rendered with FL /R: FL Studio Mobile Rack holding one MiniSynth module against FL's MiniSynth with the same 43
values, 10 settings, 7 notes (keys 36…72, a chord, an overlap, one note at velocity 50). MiniSynth draws random phases,
drift and dither, so renders never null; the measure is the level per 125 ms window and the third-octave spectrum
(mean differences in dB; `lvl / spec`). "LYNX" is LYNX's engine against FL's MiniSynth (the yardstick of what two
renders of one patch give), "LYNX FLM build" is the engine with the switches above, driven with the velocity FLM's FL
wrapper hands its module (int(41.362274·ln(10·Init.Vol + 1)) / 127, one MIDI step below the desktop's: PROVEN-REA
0x180834b80), against the FLM render.

| Setting | FLM vs FL MiniSynth | LYNX vs FL MiniSynth | LYNX FLM build vs FLM |
|---|---|---|---|
| D default | 0.35 / 0.13 | 0.24 / 0.13 | 0.23 / 0.27 |
| S1 osc kind, filter freq / peak / kbd / vel | 0.28 / 0.65 | 0.19 / 0.17 | 0.34 / 0.35 |
| S2 filter envelope (osc kind 5: random phase sets each note's level) | 2.86 / 2.34 | 3.76 / 1.66 | 2.08 / 2.01 |
| S3 transpose, amp envelope | 0.15 / 0.15 | 0.17 / 0.10 | 0.18 / 0.20 |
| S4 LFO | 0.16 / 0.10 | 0.13 / 0.23 | 0.21 / 0.44 |
| S5 LEAD voices, slide, mod wheel | 1.72 / 0.49 | 1.29 / 0.36 | 1.45 / 0.30 |
| S6 distortion | 5.74 / 4.66 | (harness: no channel level) | 0.10 / 0.41 |
| S7 delay + chorus | 0.20 / 0.25 | 0.24 / 0.25 | 0.17 / 0.41 |
| S8 phaser | 0.72 / 0.52 | 0.48 / 0.50 | 0.85 / 0.52 |
| S9 noise, LFO → OSC Mod | 5.25 / 3.01 | 0.39 / 0.16 | 0.24 / 0.27 |

Reading: the parameter order holds on every setting. FLM is 3.01 dB above FL's MiniSynth at the same channel level
(the pan law); S1's 0.65 is the wrapper's velocity step acting through FLT Vel; S6 (the 3 dB reach the distortion)
and S9 (LFO → OSC Mod depth) are the engine differences above, and LYNX's FLM build follows FLM on both. S2 and S5 are
dominated by the random phases / the harness' note order (LYNX against FL's own MiniSynth scatters as much). The rack
at its default volume (RPRM 0 = 0.853) is 0 dB, as F2's law says. Tools: /mnt/e/lynx-work/flm190n/ab (mkgen.py,
render.sh, score2.py), tools/msr.cpp; REA spec /mnt/e/lynx-work/flm190n/rea/spec-minisynth-diff.md.

Quality gate: the desktop engine renders the same bytes as before on 7 of the settings (old against new object); the
FLM build costs the same per voice (branches on a flag). MiniSynth parameter automation lanes of a .flm are still
listed, not imported (LYNX's MiniSynth takes no automation clips on its knobs from the renderer yet).

**Best way?** FL: FLM-in-FL is the reference, matched. Ableton: no FLM import. Users: FLM users ask for desktop
continuity of their MiniSynth patches (FL's own answer is the FLM plugin). Verdict: FLM-exact, no alternative proposed.

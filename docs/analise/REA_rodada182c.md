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

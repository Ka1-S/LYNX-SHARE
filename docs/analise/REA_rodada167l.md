# Round 167l (v0.1.22) — Sampler legacy effects, export extras, Alt solo, channel lock, CC 123 on reset

Clean-room: Ghidra on FL Studio 2026's own `FLEngine_x64.dll`, its DFM form resources and published field / method
tables; command-line renders by FL itself (`FL64.exe /R`, both FL locks held). Behaviour and formats only; nothing of
FL's code is copied.

Markers: **[DECOMP addr]** Ghidra pseudocode, **[DFM]** a form resource, **[DATA]** FL's demo projects, **[FL render]**
FL's command-line render, **[GUI]** LYNX driven on Windows (fresh profile, `LYNX_TEST_MODE=1`), **[TEST]** LYNX tests
(`tests/Rea167lTests.inc`).

(sections are filled as the items land)

## 1. Sampler › Precomputed effects: the legacy effects

- **Brief:** "Sampler legacy panel (boost, ring mod, low-pass, stereo delay, reverb, FX, FX3) — used by many FL demo
  channels: trace FL's engine for each, implement, render-compare against FL."
- **FL evidence:**
  - [DFM] the channel form's precomputed panel: BOOST, EQ ("Shifts the frequency balance"), RM MIX, RM FREQ, STEREO
    DELAY, CUT, RES, REVERB + `RevTypeSelect`, CLIP ("Add clipping at 0dB"), POGO ("Pogo effect").
  - [DECOMP the channel form's handlers] the .flp events: 0x4A boost (0…256), 0x45 EQ (default 128), 0x83 ring mod
    (mix | frequency << 16, default 0 | 128 << 16), 0x47 cutoff (16…1024, default 1024), 0x53 resonance (0…640),
    0x55 stereo delay (0…4096, default 2048), 0x8B reverb (amount | type << 16, default type 1), 0x56 pogo (0…512,
    default 256), 0x46 bit 2 clip. Channel fields +0x23c, +0x228, +0x22c / +0x230, +0x234, +0x238, +0x248, +0x250 /
    +0x254, +0x24c, +0x25f.
  - [DECOMP FUN_00ff0db0, the precompute] the order: crop → resample → ping-pong → **stereo delay** (more than 3
    frames: offset = round(|v − 2048| / d0) frames, d0 = 44100 / the file's rate; below 2048 the right channel is
    delayed, above it the left) → reverse → per channel: **pogo** (a time warp read with Catmull-Rom from a copy),
    **ring mod** (16384-entry sine table, phase from ch · 4096, increment ((freq + 8) · 16384 · d0) / 5760, mixed by
    mix / 256), **low-pass** (f = ((601^(cut/1024) − 1) / 600) · d0, q = 1 − (601^((1024 − res)/1024) − 1) / 600;
    v += (x − y) · f; y += v; v *= q), fades, **boost** (× (1 + v / 16)), **clip** (to ±1) → **EQ** (feedback
    y = x + prev · k, k = (((v · 192) >> 8) − 96) / 128) → fade stereo, DC, polarity, normalize, crossfade → swap →
    **reverb** (stereo float, 22050 silent frames added; type 0 FUN_00a99270: 3 allpasses 389 / 127 / 43, g 0.7, 4
    combs 1777 / 1847 / 1993 / 2137, a 3-tap FIR, wet 0.3 / dry 0.7; type 1 FUN_00a99730: 6 combs 2467…4127, allpasses
    599 / 197 / 67, a one-pole low-pass 0.7 / 0.3, allpasses 101 / 97 / 79, wet × 0.3 × 0.3 + dry 0.7; comb gain
    10^(−3 · D / (T · 44100)), T = reverb / 256 s; trailing frames below 1/512 trimmed down to the loop end) → trim.
    Constants: 1/128, 1/256, 1/1024, 1/600, ln 601 (0x15e3890, 0x15dfde8, 0x15e4788, 0x15e2000, 0x15e2250).
  - [FL render] `FL64.exe /R` of `r167l/fl/l_all.flp` (take 2, built on the FL-saved misc164p `base.flp`: 24 audio
    clip channels, one per case, each with its own 6-beat clip 8 beats apart at 86 BPM; `tmp/r167l/py/mk167l3.py`).
    Take 1 (a project built from the Empty template's Sampler) rendered silence: the generated pattern / sampler
    channels were not valid for FL; its marker chunks are still the §3 evidence.
- **LYNX before:** the legacy effects were ignored (import and engine).
- **LYNX now:** `SamplePrecompute` gets boost, eq, ringMix, ringFreq, stereoDelay, cutoff, resonance, reverb,
  reverbType, pogo, clip (`legacy()`), imported from the events above, saved in .lynx when not the default, and
  `core/SamplePrecompute.h` runs FL's chain in FL's order (stereo delay before the reverse; pogo, ring, low-pass,
  fades, boost, clip, then EQ per channel; the reverb after the swap with its tail trim). Channel settings ›
  Precomputed effects has a "Legacy effects" section: 9 knobs, the reverb type, Clip.
- **[FL render] compared with LYNX** (`--export` of the same file, `tmp/r167l/py/cmp.py`; RMS of FL − LYNX over the
  sound, per channel; FL's file is 16-bit):

  | case | settings | L error | R error |
  |---|---|---|---|
  | base / dly_p100 / dly_m300 / dly_mono | stereo delay 2048 / +100 / −300 / mono +300 | 0.0 % | 0.0 % |
  | fx3_128 / fx3_0 / fx3_384 / fx3_512 | pogo | 0.0 % | 0.0 % |
  | ring_64 / ring_full | ring mod 128 @ 64, 256 @ 200 (mono) | 0.0 % | 0.0 % |
  | lp_600 / lp_res / res_only | cut 600; cut 800 res 700; res 300 | 0.0 % | 0.0 % |
  | boost64 / boost_clip / clip_only | boost 64; + clip on a loud file; clip | 0.0 % | 0.0 % |
  | fx_200 / fx_40 | EQ | 0.0 % | 0.0 % |
  | rev1_100 / rev0_150 / rev1_mono | reverb type 1 / 0, mono | 0.0 % (tails end together) | 0.0 % |
  | combo | everything at once | 0.0 % | 0.0 % |
  | r48_dly / r48_lp | a 48 kHz file | 0.0 % | 0.0 % |

  (every error ≤ 3e-5 RMS, about the 16-bit step; compared up to where the sound ends, see below.) [TEST] `rea167l_flp_legacy_precompute_events`,
  `rea167l_legacy_precompute_formulas`.
- **Seen in FL, not done:** when an audio clip is longer than its (processed) sample, FL holds the sample's **last
  value** as a constant until the clip ends (every case above: e.g. −0.0435 on the left after `l_stereo.wav` ends at
  0.5 s, to the clip's end at 4.186 s; also in round 167k's `c_base.wav`: −0.0564 from 2.0 s to 4.2 s). LYNX ends at
  silence. A DC step at the clip's end is a click; left as LYNX's behaviour and reported.

## 2. Per-clip audio stretch (+0x40)

- **Brief:** "Per-clip audio stretch (+0x40) on Playlist audio clips."
- **FL evidence:**
  - [DECOMP FUN_01064f40] a clip needs its own variant of the sample when: its channel's time-stretch mode
    (+0x2f0, the .flp 0xD7 +0x6C) is ≥ 1 (the offline modes) and the clip's pitch (+0x3C) ≠ 0 or its stretch
    (+0x40, double) ≠ 1; mode 0 (Resample) and the pitch ≠ 0; or a clip flag (+0x48). [DECOMP FUN_010650f0] the
    variant is looked up by (pitch, stretch, flags) and made by FUN_00e25380.
  - [FL render] take 2 of `l_all.flp`, the 2 s 440 Hz ramp in 6-beat clips (86 BPM):

    | case | channel mode | clip stretch | FL plays |
    |---|---|---|---|
    | st_r_1 | Resample | 1 | 2.0 s, 440 Hz |
    | st_r_05 | Resample | 0.5 | 1.0 s, 880 Hz |
    | st_r_2 | Resample | 2 | 4.0 s, 220 Hz |
    | st_e3_05 | E3 generic (1) | 0.5 | 1.0 s, 440 Hz |
    | st_e3_2 | E3 generic | 2 | 4.0 s, 440 Hz |
    | st_rt_2 | Stretch (−1, real time) | 2 | 4.0 s, 440 Hz (its first ~0.5 s fades in) |
    | st_e3_15p3 | E3 generic | 1.5, pitch +3 | 3.0 s, 522 Hz (soft start) |

    and the song (its `smpl` loop end, its acid beats) ends at beat 249 = the last clip's start (240) + 6 × 1.5: the
    item's length field is the unstretched length; the clip lasts length × stretch.
- **LYNX before:** +0x40 ignored.
- **LYNX now:** `Clip::stretch` (1 = none; .lynx clip array index 15, compared by undo), imported from +0x40 with the
  clip's length and start offset × stretch. The scheduler hands it to the sampler voice (`setNextClipStretch`): the
  clip reads its file `stretch` times slower; on a Resample channel the pitch follows, on a keep-pitch channel the
  voice's pitch shifter shifts it back (+12 · log2 stretch). The Playlist draws the file at that speed
  (`clipops::clipSpeed (channel, clip)`). [TEST] `rea167l_clip_stretch_import`, `rea167l_clip_stretch_plays_like_fl`.
- **Not done:** FL's real-time Stretch mode's slow start; a way to set the stretch from LYNX's Playlist (FL: resize
  with the stretch tool); FL's "Create variants when stretching audio clips" switch.

## 3. Export: "Tempo information", markers in the file, the gap above "Mix"

- **Brief:** "Export extras: 'Save tempo information (acid)', markers in the exported file, the empty gap above 'Mix'
  in the export dialog."
- **FL evidence:**
  - [DFM] RenderForm › ExtraDataPanel: `ExtraTempoBox` "Tempo information" (off), `ExtraNoteMarkersBox` (off),
    `ExtraLoopMarkersBox` (on), `ExtraPlaylistMarkersBox` (on). [DECOMP FUN_00ee7d90] packs them into the form's
    +0xe2e (bit 0 playlist, 1 loop, 2 note markers) and +0xe2f bit 0 (tempo); [DECOMP FUN_00eec630] saves them as
    "Save playlist markers", "Save loop markers", "Save note markers", "Save tempo". The registry of the user's FL
    (`HKCU\Software\Image-Line\FL Studio 26\General\WAVRenderForm`, read only): AcidBox 0, NoteMarkersBox 0,
    LoopMarkersBox 1, PlaylistMarkersBox 1 (FL's defaults).
  - [DECOMP FUN_00d1af20] the WAV render's end writes the marker list (24-byte entries: frame, length, name, type,
    id): ids 0x10000 + index for plain markers, "Marker %d" (the index) for an unnamed one, `labl` per marker,
    `ltxt` for a marker with a length, a trigger list (`tlst`, up to 125 entries), then INFO; [DECOMP FUN_00c4b8a0]
    the acid chunk (tempo, beats = round(frames · tempo / (rate · 60)), numerator = steps per bar / steps per beat,
    denominator = 16 / steps per beat).
  - [FL render] `FL64.exe /R` of `r167l/fl/l_all.flp` (take 1: 120 BPM, plain markers at beats 8 "Verse", 16
    unnamed, 30 "Drop A"), AcidBox switched to 1 for that one render and restored to 0 right after
    (`tmp/r167l/flsession.sh`). The file: `JUNK`, `fmt ` (16-bit stereo 44.1 kHz), `data`, then
    - `smpl` 60 B: period 22676 ns (1e9 / 44100 rounded), unity note 60, one loop: id 0x20000, type 0x400, start 0,
      end 3925129 = the music's last frame (the data is 229 frames longer: the tail);
    - `acid` 24 B: flags 4, root 60, 0x80, 0.0, beats 178, 4, 4, tempo 120.0;
    - `cue ` 76 B: 3 points, ids 0x10000/0x10001/0x10002 at frames 176400 / 352800 / 661500 (4 s, 8 s, 15 s), chunk
      "data", offsets 0, sample offset = the frame;
    - `LIST` `adtl`: `labl` "Verse", "Marker 1", "Drop A";
    - `tlst` 76 B: 3 × ('cue ', id − 1, 1, 0xff, note 60 + index, 0x90, zeros);
    - `LIST` `INFO` `ISFT` "FL Studio 2026".
- **LYNX before:** the WAV had `fmt ` + `data` only; no "Extra data" choices. The export window had a fixed height
  (486 px): the columns needed ~420 of it, so an empty band sat above "Mix".
- **LYNX now:**
  - `exportopts::Options::saveTempo` (off), `loopMarkers` (on), `playlistMarkers` (on), remembered with the other
    export settings; three switches "Tempo information", "Loop markers", "Playlist markers" as a fourth row of the
    switches (enabled when WAV is written).
  - `exportopts::wavExtras` (plain Playlist markers of the range, the selection's moved to its start; tempo, meter)
    and `wavExtraChunks` write FL's chunks in FL's order with FL's bytes; `appendWavExtras` appends them after the
    data and fixes the RIFF size. On for the export window, stems and `--export` (`ExportJob::setExtraData`), off for
    LYNX's own renders (bounces, consolidation, the auto-sampler: their files become samples again, a loop would make
    them loop). `--export … --save-tempo` switches the acid chunk on.
  - The window's height is computed from its content (the taller column, FLAC's rows, "Also save" on its own line in
    some languages) plus the switches: no empty band.
  - [TEST] `rea167l_export_wav_extras_match_fl` (every byte after `data` equals FL's file up to its INFO list),
    `rea167l_export_wav_extras_song_markers`.
- **Not done:** "Note markers" (FL's off-by-default box; what it marks was not traced); FL's `ISFT` software name.

## 4. Mixer: "Alt solo selected track(s)" (Alt+S)

- **Brief:** "Alt+S (prove in FL first)".
- **FL evidence:**
  - [DFM] `/FXForm/EffectsMenu`: `SoloMenu` "Solo selected track(s)" ShortCut 83 (S) and `AltSoloMenu` Tag 1 "Alt solo
    selected track(s)" ShortCut 32851 = 0x8000 (Alt) | 'S'. Both items share the handler 0x12abe00.
  - [DECOMP 0x12abe00 → FUN_0127c2a0 (−1 = the selected tracks, 2, Tag == 1 ? 3 : 2)] the solo of the selection with
    flags 2 (plain) or 3 (alt). [DECOMP FUN_0127bb10 / FUN_0127bab0] flag 2: the target and the tracks it reaches
    (its path to the Master stays heard); flag 1 (the alt bit): also the tracks that reach the target (its sources).
    [DECOMP FUN_0127c640] the reach test is transitive over the route bytes (+0x398). Locked tracks (+0x1b) are
    skipped by the solo.
  - Toggle [DECOMP FUN_0127c2a0]: alt on tracks already soloed: an alt solo goes away, a plain one becomes alt;
    plain: an alt solo with sources becomes plain, a plain solo goes away, anything else is soloed plain.
- **LYNX before:** one solo kind: `Song::soloAudible` always kept both the upstream and the downstream tracks (= FL's
  alt solo); the mixer menu's "Solo selected track(s)" (S) soloed with that; no Alt+S.
- **LYNX now:** `MixerTrack::soloSources` (true = LYNX's strip solo and FL's alt solo; saved in .lynx only when
  false, compared by undo). `mixerops::soloSelected (song, targets, alt)` is FL's toggle; "Solo selected track(s)\tS"
  is the plain one (only the path to the Master), "Alt solo selected track(s)\tAlt+S" the alt one, in the mixer's
  menu and as keys (`mixer.soloSelected`, `mixer.altSoloSelected`). `soloAudible` keeps the upstream tracks only of
  tracks soloed with their sources. The strip's solo button stays LYNX's (with sources). [TEST]
  `rea167l_mixer_alt_solo`.

## 5. .flp channel events 0x20 / 0x30 / 0x32 / 0xAA / 0x2B / 0xE2 / 0xE6 / 0x26

- **0x20 "Lock active/mute state"** (u8, channel +0x892): [DECOMP] `SoloLockMenuClick` 0x1213320 toggles it, and
  FUN_00ffac40 (solo a channel) skips the locked channels: their mute switch keeps its state when another channel is
  soloed or unsoloed. **LYNX now:** `ChannelDef::soloLock` (read from the .flp, saved in .lynx, undoable),
  `noteedit::soloChannel` leaves locked channels alone, channel menu "Lock active/mute state" (checkable).
- **0x32 ghost notes** (u8, channel +0x1fc, written by FUN_00e568b0): 1 = the channel's notes show as ghosts in the
  Piano Roll of the other channels. **LYNX now:** imported as `ghostHidden = !value` (LYNX had the field since round
  166d, only the .flp import was missing). [TEST] `rea167l_channel_solo_lock_and_ghosts` (both events + the solo).
- **0x30** (u8, channel +0x224): written by the saver, but 0 in every channel of the demo corpus and in the FL test
  projects of this round; no reader found that changes playback. Left: ignored (nothing to import).
- **0xAA** (channel +0x714, a count followed by strings, sampler only): a per-sampler string list; its form handler
  was not traced this round. Left: ignored.
- **0x2B** (playlist track byte, track +0x70; when 0 the loader puts a default in +0x2c; 1 on 823 of 27 000 tracks of
  the corpus): its meaning is still not traced. Left: ignored (REA_rodada167g-gaps row).
- **0xE2 / 0xE6** (the second link list, 20 B records + names, next to 0xE3 which LYNX reads): the saver writes list 0
  as 0xE2; no project of the corpus links anything there that LYNX could use. Left: ignored.
- **0x26** (project global: bit 0 = Playlist "Monitor external input and mixer input", bit 1 = "Monitor external
  input only", the audio-track monitoring choice): LYNX has no audio-track monitoring choice to set. Left: ignored
  until audio tracks monitor input.

## 6. CC 123 to the MIDI outputs on a transport reset

- **Brief:** "CC123 to MIDI Out on transport reset".
- **FL evidence:** [DECOMP FUN_011e7a10] setting the song position (rewind, a click on the ruler, a jump) with "Reset
  plugins on transport" on (Settings form +0xb50) calls [DECOMP FUN_011c89c0]: for every open MIDI output device of
  the list 0x15e4560 whose "Send all notes off" flag is set (+0x49, written by `SendAllNotesOffBoxClick` 0x135cd70),
  `midiOutShortMsg (0x7bb0 + channel)` for the 16 channels: CC 123 value 0 only (the start-up / after-render reset
  also sends CC 64 = 0). The MIDI Out *plugin* gets the generic plugin reset (round 167k).
- **LYNX before:** "Send all notes off" outputs got CC 64 + CC 123 at start-up and after a render only.
- **LYNX now:** the audio thread calls `MidiSyncIo::requestTransportNotesOff()` on a rewind or seek when "Reset
  plugins on transport" is on (a counter; it never touches a device); the MIDI sync sender thread (now also running
  when an output only has "Send all notes off") sends `midisync::transportNotesOff` (16 × B0+ch 7B 00) to those
  outputs. [TEST] `rea167l_transport_notes_off_bytes`; `--midisync-test` reports the burst through the sender thread
  in dry run ("transport notes off: bursts=1").


## Where I stopped

- Done and landed: items 1–6 (§1–§6). Tests: `rea167l_*` (9 tests, 83 checks with the rest of the 167l set).
- FL compare (take 2, `C:\Users\kauan\lynx-test\r167l\fl\l_all.flp`, FL render `/mnt/e/lynx-work/r167l/fl_take2.wav`):
  the 24 legacy cases and the three Resample stretch cases match FL to ≤ 3e-5 RMS; the E3 / real-time stretch cases
  have FL's length and pitch but not its waveform (élastique is not reproduced: 13–39 % error, same as LYNX's other
  pitch-keeping stretches). The `smpl` loop end equals FL's (7661092).
- Open:
  - FL holds an audio clip's last sample value as DC until the clip's end (seen in every case of take 2 and in 167k
    c_base); LYNX ends at the sample's end. Not implemented.
  - The acid chunk's beats follow the file's length *with* its tail (FL and LYNX use the same formula), and FL's
    tail after the music is shorter (642 frames on take 2, LYNX 22 492 frames with "Leave remainder"): FL writes 249
    beats, LYNX 250. The difference is the tail detection, not the chunk.
  - Export "Note markers" and the INFO/ISFT `LIST` FL writes last: not done.
  - §5: 0x30, 0xAA, 0x2B, 0xE2/0xE6, 0x26 documented only.
- REA material: tmp/r167l/d*.c (decompiles), m*.txt / o*.txt / r*.txt (constants, offsets, references).
  Rerun: `tmp/r167l/g.sh <out.c> 0 <hex addr…>`, `SCRIPT=Mem.java|Dump.java|Off.java tmp/r167l/g.sh …`.

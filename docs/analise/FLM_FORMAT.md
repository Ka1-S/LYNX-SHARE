# FL Studio Mobile project format (.flm): layout for a LYNX importer

Round 182c, 2026-11-08. Sources:
- **Binary:** `Plugins/Fruity/Generators/FL Studio Mobile/FL Studio Mobile_x64.dll`, shipped with FL Studio 2026. It is the
  FL Studio Mobile 5.0.x app built as a plugin. Its sha256 begins with c63ba7ae; Ghidra VAs use image base 0x180000000.
- **Sample files:**
  - the 11 demo `.flm` and 9 `.songtmpl` files in that plugin's `Installed/` folder;
  - the user's `MONTAGEM XONADA REMAKE.flm`, saved by FLM 5.0.4 on Android and zipped with its samples (`/mnt/e/lynx-work/flm182c/user-x2/`).

Clean-room: this text describes the layout and its behaviour in our own words. No decompiled code was copied. The LYNX
reader will be written from this page.

Evidence markers:
- **PROVEN-REA:** read from the plugin's own reader or writer code; the addresses are given.
- **PROVEN-FILE:** holds in every sample file (20 projects, 59 511 events), checked by
  `/mnt/e/lynx-work/flm182c/flmdump.py` + `verify.py`.
- **INFERRED:** a reading that fits the data, but no code shows it.

## 1. Container

| Item | Layout | Evidence |
|---|---|---|
| File magic | 4 bytes `10LF`, which is the little-endian u32 0x464C3031. The loader calls the format "FLM3" and fails with "Invalid FLM3 header" when the magic is missing | PROVEN-REA song loader 0x18076b9e0 |
| Chunk | 4-byte tag + u32 body size + body. The writer writes the size as a placeholder and patches it once the body is done | PROVEN-REA writers 0x18059bdb0 (RMOd), 0x180757d80 (CHNL), 0x18075c150 (CLIP) |
| Byte order | Little-endian. Every reader has a "swap" flag that reverses each field (a big-endian path), but no sample uses it | PROVEN-REA (byte-reversal loops in every reader) |
| Nested container | Some chunk bodies start with a 4-byte *versioned magic*, then hold child chunks: `10KR` rack, `10HC`/`20HC` channel, `20LC` clip, `20LS` sample, `10MS` drum-sample set. The two digits are the version (10 = 1.0, 20 = 2.0) | PROVEN-REA (rack expects 0x524B3031 at 0x18059b100; channel checks 0x43483031 version byte 2; clip/sample writers emit 0x434C3032 / 0x534C3032) |
| Unknown chunks | The song loader logs "Error when loading chunk %s" and goes on. The rack loader skips any tag it does not know | PROVEN-REA 0x18076b9e0, 0x18059b100 |
| Fixed strings | Fixed-size, NUL-padded byte arrays (256, 512, 1024 or 2048 bytes) | PROVEN-REA |
| Counted strings | Some are u32 length + bytes (`lp32`), some u16 length + bytes (`lp16`). No NUL; UTF-8 in the samples | PROVEN-REA / PROVEN-FILE |

Top-level order in every file: `HEAD`, `KEYB` (since 4.x), `META`, `TDIV`, then one `RACK` and one `CHNL` per channel
(all RACKs first, all CHNLs after). Rack *n* belongs to channel *n*. PROVEN-FILE: the counts are equal in every
sample, and the pairs agree (drum kits sit on drum lanes, sampler paths match the channel names). The loader looks up
a rack for each channel and logs "Missing rack for Channel #n … skipped" when there is none (PROVEN-REA 0x18076b9e0).
Whether that lookup goes by index or by channel id is INFERRED to be by index.

`.songtmpl` (new-song templates) use the same format. `.flmrack` (rack presets) holds one rack body, `RMOd` chunks
included, and `.flmpst` (module presets) one module body (`HEAD`…). Both PROVEN-FILE.

## 2. Song header chunks

### HEAD (song)

PROVEN-REA (0x18076b9e0); the meanings are INFERRED where marked.

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | header word A (3 or 4 in the demos, 0 in the user file). INFERRED: version/flags |
| 4 | u32 | header word B |
| 8 | char[256] | song title |
| 264 | f64 | **tempo, BPM.** PROVEN-FILE: 130.0 in the user file, 128/174/110.02… in the demos |
| 272 | f64, f64 | handed to one setter together. INFERRED: loop/marker start and end in beats (0,0 or 128,128) |
| 288 | 8 bytes | skipped by the reader (a legacy field) |
| 296 | 4 × f64 | view state. INFERRED: playlist zoom/scroll; not needed for import |
| 328 | 16 bytes | project GUID |
| 344 | u32, u32 | **scale and root note of the "key locker"** (the reader's log calls them "scale / root note") |
| 352 | u8 | sharp/flat display flag ("#/b key locker") |
| 353 | u8 | flag (unknown) |
| 354 | u32 | channel-id counter (must be ≤ 0xFFF, else the reader warns) |
| 358 | lp32 | app version that saved the file ("5.0.4", "4.8.3"). The UI warns when a file comes from a newer app |

Older files end earlier, and the reader stops at the first missing field.

### KEYB

u32 length + JSON text: the keyboard's chord set, an array of `{baseNote, extensions[], octave, quality}`. The keyboard
settings loader reads it. PROVEN-REA (0x18076b9e0 calls the keyboard-settings loader); the JSON shape is PROVEN-FILE.

### META (3081 bytes)

PROVEN-REA 0x18076b9e0. Offsets:
- 0: char[256] creation date "YYYY-MM-DD HH-MM-SS";
- 256: f64 (INFERRED: total edit time);
- 264: char[256] author, with a `lastAuthor` JSON fallback;
- 520: char[256] original song name ("New Song (156)");
- 776: u8;
- 777: char[2048] notes/description;
- 2825: char[256] last-saved date. When it is empty the UI shows "Never / demo project".

### TDIV

2 bytes: time-signature numerator and denominator (4, 4). PROVEN-REA (two single bytes read after META), PROVEN-FILE.

## 3. RACK: a channel's instrument and effect chain

Body: u32 **rack kind** + magic `10KR` + child chunks. Rack kind: 0x000 master, 0x080 bus/AUX send track, 0x100
normal channel. PROVEN-FILE; the values match the CHNL kind in the same slot.

| Child | Layout | Evidence |
|---|---|---|
| `RHED` | u32 (skipped), u32 next module id, i32, i32 (−1 = none). INFERRED: selected or focused slots | PROVEN-REA 0x18059b100 |
| `FOUT` | i32, i32 → output routing (the channel's output target) | PROVEN-REA (tag at 0x1809f4f38) |
| `RPRM` | 6 × 32-bit rack parameters, stored as normalized floats: [0] **volume** 0…1, [1] **pan** 0…1 (0.5 = centre), [2..5] other rack controls. INFERRED: [2..5] are mute/solo/send-type controls | PROVEN-REA (6 values → 0x18059e0c0); the meaning of [0] and [1] is INFERRED from the values (0.69…1.0 and 0.5) |
| `CCLK` | list of (i32, i32, i32) triples, deduplicated. INFERRED: controller links | PROVEN-REA |
| `RSMP` | **built-in drum sampler**: magic `10MS` + u32 + `PADS` (22 bytes, pad → sample index; identity by default) + one `SMPl`/`SMPL` per kit sample (section 6). Drum notes use key = sample index | PROVEN-REA (0x18074fcd0), PROVEN-FILE (2404 drum notes, all keys < kit size) |
| `RMOd` | **one module (instrument or effect)**: u32 module **type id** + u32 module **instance id** + module body (section 3.1). Order = slot order in the rack | PROVEN-REA 0x18059b100 → factory 0x180597870 |
| `RMOD` | older form: type, id, module body, then u8 enabled, u8, u8 key-range high | PROVEN-REA |

The factory looks the type id up in a hash map filled by one static registrar per module. An unknown id logs
"Unknown module type: %d" and the module is skipped (PROVEN-REA 0x180597750).

### 3.1 Module body (RMOd)

PROVEN-REA (module loader 0x180796e40):

| Child | Layout |
|---|---|
| `HEAD` | u32 type id (must match the instance), u32, u8 |
| `ADD1` | u8 **bypass/enabled flag**, u8 **key-range low**, u8 **key-range high** (0…127; swapped if low > high). INFERRED: the flag is bypass, since every sample has 0 |
| `DESC` | char[256] name, char[256] category/folder, lp16 extra. The UI's preset name |
| `PRST` | lp32 preset/sample **file path** (absolute, as saved on the device) + lp32 **display/relative name** ("Keyboard/Grand Piano", "My Samples/808 (8).wav") |
| `PRMS` | N × 32-bit **parameter values** in the module's own order. N = the module's parameter count (table in section 7). An older file with fewer values gets defaults for the rest, and the reader can remap old indices |
| `SMPR` | 3 values for the reserved parameter ids 0x7FC0…0x7FC2: (0.5, 1.0, 0.5) in every sample. INFERRED: the module's own pan, volume and mix |
| `CSTM` | module-specific state (DirectWave zones `ZONE`, `MIDI`, `PATH`/`PATh`, `SENS`). It is parsed by the module itself |

Parameter values are floats. INFERRED: they are normalized 0…1, because almost every value is in 0…1 and the
defaults are 0.5/1.0. The value → unit mapping per parameter needs one REA pass per engine (plan slice F4).

## 4. CHNL: a channel's tracks and clips

Body: u32 kind (0 master, 0x80 bus, 0x100 normal) + magic `20HC` (older: `10HC`) + child chunks. PROVEN-REA writer
0x180757d80, reader 0x180758ac0.

### CHHD

PROVEN-REA:

| Offset | Type | Field |
|---|---|---|
| 0 | char[1024] | channel display name |
| 1024 | u32 | (unknown; 1 on an instrument channel) |
| 1028 | f64 | **display order** (0 = master, 1, 2, …). PROVEN-FILE: a permutation |
| 1036 | f32 | handed to a setter. INFERRED: colour/hue |
| 1040 | f64, f64 | INFERRED: track height / view (1.137…, NaN) |
| 1056 | u32, u32 | two booleans. INFERRED: mute and solo (both 0 in the samples) |
| 1064 | 16 bytes | channel GUID |
| 1080 | u32 | channel id |
| 1084 | 3 × lp32 | default type name ("DW Sampler 3", "Instrument 2", "Audio 4"), user name ("Grand Piano"), alternative name |

`10HC` files stop after the GUID or earlier.

### SEND

One chunk per send, 6 × 32-bit: target, level, …. The writer converts a level through a dB-style curve and also
stores the linear value. INFERRED: a send to the AUX track (0x80 rack) at that level. PROVEN-REA (writer); the meaning
is INFERRED.

### TRKH: one track lane of the channel (a channel can have several)

`DESc` comes first. PROVEN-REA writer 0x180757d80, with the offsets PROVEN-FILE:
- 0: u32 **lane kind**: 0 = note track (piano roll), 1 = **automation**, 2 = **audio**, 3 = **drum/step track**.
  The 259 = 0x103 seen once is kind 3 with a flag in the high byte.
- 4: i32 automation target **module instance id**. 0 = the rack itself ("Level", "Pitch Bend"); −1 = none.
- 8: i32 automation target **parameter index**.
- 12: f64, u32, u32 (view).
- 28: char[1024] lane name. Automation names are "<param> : <module>", e.g. "FLT Freq : MiniSynth", "Frequency : FX Filter".
- 1052: lp32 alternative name.

Then one `CLIP` per clip.

### CLIP

Body: u32 **clip start in ticks** + magic `20LC` + children.

The tick unit is **128 per beat**. Evidence: INFERRED by the data, though the strength is near-proof:
- clip starts / 128 fall on whole beats;
- in all 12 649 notes of the 20 sample projects, note start / 128 + note length ≤ the clip's loop length, with the
  maxima exactly equal;
- the user's vocal clip is 163.9097 beats long, and the WAV lasts 75.6506 s at 130 BPM: a match to 1e-9.

Clip children (PROVEN-REA writer 0x18075c150):
- **`CLHd`** (37 bytes), f64 values in **beats**:
  - +0 f64 **clip length on the timeline** (0 = one loop);
  - +8 f64 **loop/pattern length**: content repeats every this many beats;
  - +16 f64 **start offset into the content/sample** (beats);
  - +24 u32 **content id**. Clips with the same id on one lane share one pattern ("unique/linked" clips);
  - +28 f64 **source length** (for audio: the whole sample in beats at the project tempo);
  - +36 u8 flag.
- `ZOOM`: 4 × f64 editor view. UI only.
- `CLSm`: present on audio clips; a sample body (section 6).
- **`EVN2`**: u16 record size (always 20) + N records. Record layout:

| Offset | Type | Note / drum event | Automation event |
|---|---|---|---|
| 0 | u32 | start tick inside the clip (128/beat) | point tick |
| 4 | f64 | **length in beats** (0.25 = 1/16; drum steps 0.1249) | 0 |
| 12 | i16 | **key**: MIDI note (0…127), or kit sample index on drum lanes | negative: point shape/tension code (−2, −4, −5, −10, …). INFERRED |
| 14 | u8 | INFERRED: **velocity** 0…255 (178/200/204/255 common) | 0xB2 constant |
| 15 | u8 | INFERRED: pan, 127 = centre (always 127 in the samples) | 0x7F |
| 16 | u16 | INFERRED: fine pitch, 0x7FFF = centre (always 0x7FFF in the samples) | **value 0…65535** (normalized) |
| 18 | u8, u8 | INFERRED: per-step note repeat / flags (0 in the samples) | 0 |

The record layout is PROVEN-REA. The meanings of bytes 14–19 are INFERRED; the graph editor's velocity, pan, pitch
and repeat fields fit them (section 9).

On audio lanes `EVN2` holds the clip's envelope points (volume/pan/pitch of the wave editor). INFERRED.

## 5. Paths and resources

`PTH1` holds lp16: a *root token*, a NUL, then a path relative to that root (PROVEN-FILE):
- `@L@` = the user's FLM library (Android: `/storage/emulated/0/Android/data/com.imageline.FLM/files/`). The user's
  zip keeps exactly this tree (`My Samples/…`, `My MIDI/…`, `<song> Remake/Sample1/…`), so `@L@` = the folder of the
  `.flm`. PROVEN-FILE: all 93 audio clips of the user file resolve.
- `@R@` = the app's resources. FL 2026's copy is `Plugins/Fruity/Generators/FL Studio Mobile/Installed/`. It is a
  **subset**: 77 of 168 demo audio refs and 135 of 271 kit samples exist there. The rest (and the paid "EXP" packs)
  must be reported as missing.

`PRST` keeps the absolute device path plus the relative name, so either can be used as a fallback.

## 6. Sample body (SMPl / SMPL / CLSm, magic `20LS`)

PROVEN-REA writer 0x18074a1c0:
- `MAIN`:
  - u32 −1;
  - f64 (−1.0);
  - char[512] **sample name**;
  - char[512] folder/category;
  - u8, f64, u32, u8, u8;
  - optional lp16.
- `LINk`: u32, u32, u16, u32 (INFERRED: link/choke group).
- `STRC`: u8 mode, f64 **time-stretch ratio**, f64 pitch ratio 2^(st/12), f32 normalized pitch (st/48 + 0.5), f32.
- `STR1`: u8 **stretch mode**, f64 stretch ratio, f64 **pitch in semitones**.
- `PTH1`: path (section 5).
- `PRST`: lp32 absolute path + lp32 relative name.
- `PRMS`: 11 × f32 sample params. In the user file: 0.946, 0.5, 0.5, 0, 0, 0, 0, 1, 1, 1, 0.5. INFERRED:
  volume, pan, …, envelope.

## 7. Module type registry (complete)

All 47 registrations are PROVEN-REA (registrar callers of 0x180449900; name strings read at each). Category = the high
word of the registrar's flag value: 1 generator, 2 delay/reverb, 3 dynamics/drive, 4 EQ/filter/stereo, 5 modulation/misc,
6 note (MIDI) effect.

| Id | Name | Cat. | Params (PROVEN-FILE) |
|---|---|---|---|
| 0 | MiniSynth | gen | 44 |
| 1 | DW Sampler (DirectWave player) | gen | 34 + CSTM zones |
| 2 | GMSynth | gen | 75 |
| 19 | Transistor Bass | gen | 14 |
| 23 | SuperSaw | gen | 60 |
| 33 | Slicer | gen | 7 |
| 39 | 3x Osc | gen | 149 |
| 52 | SoundFont Player | gen | ? |
| 3 | Equalizer ("Param EQ") | EQ | 17 / 21 |
| 4 | MultiFX | mod | 7 |
| 5 | Limiter (legacy) | dyn | 4 |
| 6 | Distortion | dyn | 4 / 7 |
| 7 | Graphic EQ | EQ | 9 |
| 8 | Compressor | dyn | 7 / 9 |
| 9 | Filter | EQ | 5 |
| 10 | Autoduck (legacy; was "Trancer") | dyn | 4 |
| 11 | Chorus | mod | 5 |
| 12 | Reverb (legacy) | rev | 19 / 22 |
| 13 | Flanger | mod | 12 |
| 14 | Phaser | mod | 10 |
| 15 | Stereoizer | EQ | 7 |
| 16 | Trance Delay | rev | 5–7 |
| 18 | Tape Delay | rev | 5 |
| 20 | Leveller | dyn | 3 |
| 24 | Tuner | mod | 1 |
| 25 | Pitcher | mod | ? |
| 26 | Analyzer | mod | 1 |
| 27 | Gate | dyn | 9 |
| 28 | Limiter | dyn | 5 |
| 29 | Autoduck | dyn | 5 / 8 |
| 30 | Tuned EQ | EQ | ? |
| 32 | Reverb | rev | ? |
| 34 | Spacer | EQ | 7 / 8 |
| 35 | Reverb 2 | rev | 19 |
| 36 | Multiband Compressor | dyn | 27 / 31 |
| 40 | Comb Filter | EQ | 12 |
| 41 | Waveshaper | dyn | 21 |
| 42 | Tape Stop | mod | ? |
| 43 | Wow & Flutter | mod | 11 |
| 44 | Spreader | EQ | 5 |
| 45 | Pitcher 2 | mod | ? |
| 46 | Tremolo | mod | 12 |
| 47 | Note Echo | note | ? |
| 48 | Transpose | note | ? |
| 49 | Randomizer | note | ? |
| 50 | Scale | note | ? |
| 51 | Arpeggiator | note | 31 |

Ids 17, 21, 22, 31, 37 and 38 are not registered: retired modules. A file that holds one makes FLM itself log
"Unknown module type". "?" = the parameter count is not seen in any sample. Known parameter indices, from automation
lane names (PROVEN-FILE):
- MiniSynth: 9 = FLT Freq, 25 = AMP Rel;
- DW Sampler: 3 = Pitch;
- GMSynth: 28 = Transpose;
- SuperSaw: 0 = Enable, 19 = Filter Cutoff, 36 = Transpose;
- Filter: 1 = Frequency;
- Reverb (12): 3 = MIX;
- Flanger: 10 = Mix;
- Spacer: 3 = Separation;
- Trance Delay: 2 = Fdbk, 3 = Mix;
- Autoduck (10): 1 = Ducking;
- Equalizer: 9 = Gain 3;
- Reverb 2: 1 = HIGH CUT;
- MultiFX: 2 = Axis X;
- rack (id 0): 0 = Level, 4 = Pitch Bend.

## 8. Relation to FLM-exported .flp (issue #88)

FL Studio desktop hosts FLM instruments as the "FL Studio Mobile Rack" generator (`FL Studio Mobile Rack_x64.dll`).
Round 178a found that an FLM→FLP export stores each instrument as that generator, with its state in FLP event 0xD5.
The state uses the same `RACK 10KR` tree (`SSLF/10LF → RACK → RHED RPRM RSMP RMOD{DESC PRMS CSTM}`). So one LYNX rack
decoder serves both paths: opening a `.flm` directly, and replacing the inert Mobile Rack in an exported `.flp`.

## 9. Open points (each is INFERRED today; the plan says which slice closes it)

1. EVN2 bytes 14–19 meanings (velocity scale, pan, fine pitch, repeat). Close it with one REA pass on the note loader,
   or by round-tripping a note edited in the plugin UI (GUI turn).
2. RPRM [2..5], CHHD colour/mute/solo, SEND fields.
3. The automation point shape codes (negative keys) and their curve maths.
4. The per-module parameter value → unit maps. Each LYNX engine needs its own mapping (MiniSynth, DirectWave and
   TransistorBass first).
5. HEAD 272/280 (loop range).

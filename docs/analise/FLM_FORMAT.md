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
| 344 | u32, u32 | **root note (0..11) and scale of the "key locker"** (the reader's log calls them "scale / root note"; round 190ds: every file on disk has the root first, the scale = index into FLM's list of 33 scales, see `scalekeys::flmKeyLocker`) |
| 352 | u8 | sharp/flat display flag ("#/b key locker") |
| 353 | u8 | flag (unknown) |
| 354 | u32 | channel-id counter (must be ≤ 0xFFF, else the reader warns) |
| 358 | lp32 | app version that saved the file ("5.0.4", "4.8.3"). The UI warns when a file comes from a newer app |

Older files end earlier, and the reader stops at the first missing field.

### KEYB

u32 length + JSON text: the keyboard's chord set, an array of `{baseNote, extensions[], octave, quality}`. The keyboard
settings loader reads it. PROVEN-REA (0x18076b9e0 calls the keyboard-settings loader); the JSON shape is PROVEN-FILE.
Round 190ds (REA on the FL Studio Mobile plugin DLL): `quality` 0 Maj, 1 min, 2 aug, 3 dim, 4 Sus2, 5 Sus4, 6 power;
`extensions` are semitones above the root (9 "6", 10 "7", 11 "Maj7", 13 "b9", 14 "9", 17 "11", 18 "#11"). LYNX imports
the set as the project's Chord pad set (`Song::keyboardChords`).

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

2 bytes (4, 4): **beats per bar** and **steps per beat**. PROVEN-REA (two single bytes read after META into the
song's +0xa8 / +0xa9; the note grid's snap values are 1/X, 1/2X, 1/3X, 1/4X, 1/6X of a beat with X = the second byte
and "bar" = the first; round 190ab), PROVEN-FILE. The second byte is also the modules' tick rate: 3x Osc steps its
envelopes and LFOs 384 / X times per beat (96), each tick round(fs·60 / (tempo·384 / X)) samples.

## 3. RACK: a channel's instrument and effect chain

Body: u32 **rack kind** + magic `10KR` + child chunks. Rack kind: 0x000 master, 0x080 bus/AUX send track, 0x100
normal channel. PROVEN-FILE; the values match the CHNL kind in the same slot.

| Child | Layout | Evidence |
|---|---|---|
| `RHED` | u32 (skipped), u32 next module id, i32, i32 (−1 = none). INFERRED: selected or focused slots | PROVEN-REA 0x18059b100 |
| `FOUT` | i32, i32 → output routing (the channel's output target) | PROVEN-REA (tag at 0x1809f4f38) |
| `RPRM` | 6 × 32-bit rack parameters, stored as normalized floats (the rack's parameters 0…5; the same ids automation lanes use with module 0):<br>• [0] **volume**, gain = 10^(3.3·v^0.6 − 3), i.e. 66·v^0.6 − 60 dB: 0 dB at v ≈ 0.853, +6 dB at 1, silent at 0<br>• [1] **pan** p = 2v − 1 as a balance: p > 0 scales the left side by 1 − p, p < 0 the right side by 1 + p<br>• [4] pitch bend: a ratio of 2^(4v − 2) on the playing voices<br>• [5] **Aftertouch**, passed on to the modules<br>• [2] **Mute**, [3] **Solo**: not handled by the setter; 0 in every sample (INFERRED: switches, on above 0.5) | PROVEN-REA: setter 0x18059e0c0, fader law 0x1805943b0 (round 182c F2); the names from the rack's parameter-name function 0x18059eb20 (round 187d, §3.2) |
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

### 3.2 The rack's parameter ids (round 187d)

The rack is module instance 0. Its parameter-name function (PROVEN-REA 0x18059eb20) names the ids that automation
lanes on module 0 use (TRKH `DESc` +4 = 0, +8 = the id):

| Id | Name |
|---|---|
| 0 | Level |
| 1 | Pan |
| 2 | Mute |
| 3 | Solo |
| 4 | Pitch Bend |
| 5 | Aftertouch |
| 7…133 | "MIDI CC #n" |
| 0x1000 + channel id | "Enable Send (to <channel>)" |
| 0x2000 + channel id | "Send Level (to <channel>)" |

The number after 0x1000/0x2000 is looked up as a channel id among the song's channels, the master's included, and the
name shows that channel's name.

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

One chunk per route of the channel, 6 × 32-bit (PROVEN-REA: writer 0x180757d80, reader 0x180758ac0):
- [0] the **target channel id** (CHHD +1080). The master's id is its route to the main output, another id is a send
  to that channel (an AUX/bus track);
- [1] a float (1.0 in every sample);
- [2] the **linear gain**, from [5] by the fader law, capped at 1;
- [3] a float (1.0);
- [4] a float (0.853 = 0 dB);
- [5] the **level 0…1 on the fader law**. When [5] is missing the reader rebuilds it from [2].

Every channel of the user's song has one SEND, to id 0 (the master) at 0.853 = 0 dB. FLM 4.x demos have none: their
channels then use the default route.

### TRKH: one track lane of the channel (a channel can have several)

`DESc` comes first. PROVEN-REA writer 0x180757d80, with the offsets PROVEN-FILE:
- 0: u32 **lane kind**: 0 = note track (piano roll), 1 = **automation**, 2 = **audio**, 3 = **drum/step track**.
  The 259 = 0x103 seen once is kind 3 with bit 0 of the second byte set: **the lane holds its notes** (round 190bs,
  PROVEN-REA: the song scheduler hands a drum note's end to the pad only when the lane's flag is set; that this flag is
  the second byte of `DESc`'s kind is INFERRED from the one file). Without it every hit is a one-shot.
- 4: i32 automation target **module instance id**. 0 = the rack itself ("Level", "Pitch Bend"); −1 = none.
- 8: i32 automation target **parameter index**.
- 12: f64 **swing** 0…1 of a drum/step track (PROVEN-REA, round 190ab: the lane field the clip compiler reads for a
  note's repeats; in FLM's shipped songs it is 0 on every note lane and 0.17…0.70 on drum lanes). The saved start
  ticks of the steps already hold the swing: odd 1/16 steps sit swing / 8 of a beat late. Then u32, u32 (view).
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
| 14 | u8 | **velocity**: the module's note-on value w = byte / 255 (PROVEN-REA, round 190u; the piano roll draws 204 = 0.8, the drum grid 255, the bare template 178; 200 = MIDI 100) | 0xB2 constant |
| 15 | u8 | pan = byte / 255 at playback (PROVEN-REA; 127 = 0.498 = the editors' centre; 40 drum notes of the shipped songs are panned, 57…199) | 0x7F |
| 16 | u16 | pitch p = u16 / 65534, 0x7FFF = centre: a drum pad plays at 2^(2p − 1) of its speed, ±1 octave (PROVEN-REA; 165 drum notes of the shipped songs); not handed to the module's note-on on note lanes | **value 0…65535** (normalized) |
| 18 | i8, u8 | 18 = note repeat count n, 19 = slide flag (see below) (PROVEN-REA; 2 repeats and no slide in the shipped songs) | 0 |

The record layout is PROVEN-REA, and since round 190u so are bytes 14–19 (the song scheduler 0x180770330 multiplies
the velocity byte by 1/255 and hands it on unchanged, through the event queue and the channel's note entry, to the
module's note-on: no curve, no lane or clip multiplier; the editors write trunc(clamp(255·v, 1, 255)). Over the 12 709
notes of FLM's 20 shipped songs and templates byte 14 peaks at 178, 204, 200 and 255. Drum pads are linear in w with a
balance pan; evidence /mnt/e/lynx-work/flm190u/rea/spec-velocity.md).

Bytes 15–19 at playback (PROVEN-REA, round 190ab: clip compiler 0x18075d6b0, song scheduler 0x180770330; imported):
- **Pan**: a module gets byte / 255 with the note-on; a drum pad plays w·min(1, 2(1 − p)) left and w·min(1, 2p) right
  (a balance: the louder side keeps its level).
- **Repeat** n > 0: n more note-ons of the same key, velocity and pan. Hit i (1…n) starts i / (n + 1) of W beats after
  the note, W = 1/4 + swing / 8 when the note's 1/16 step ((int) (start beat × 4)) is even, 1/4 − swing / 8 when odd;
  the tick is (int) ((float) (i / (n + 1) · W) · 128 + start · 128). One note-off, at the note's own end.
- **Slide** (note lanes only; a drum lane plays the note as a hit): no note-on. The highest plain note sounding at
  the slide's start is the held note; over the slide's length FLM glides key, velocity and pan linearly from the held
  note's to the slide's, tick by tick, and sends the offsets to every voice of the module. No held note: nothing.
  Imported as LYNX's slide note (the key glide; the velocity and pan glides are not converted).

On audio lanes `EVN2` holds the clip's own automation of its sample slot (round 190bs, PROVEN-REA: the scheduler
0x180770330). |key| − 1 is the slot parameter of §6.1 (key ±1 = Level, ±2 = Pan, ±3 = Speed …), the sign as on
automation lanes (positive = a point the user placed, negative = a sample of the curve), the value u16 / 65534. In
FL's 20 sample projects 26 of 261 audio clips have such points: 25 Level, 1 Speed. LYNX imports a constant Level /
Pan / Speed as the clip's own; a Level or Pan that moves becomes automation of a channel the clip has to itself.

**Audio clips loop** (round 190bs, PROVEN-REA): a clip longer than what is left of its loop starts its sample again
at clip start − offset + k · loop length. (That each new pass starts at the sample's first frame is INFERRED.) One
clip of the 20 sample projects does (length 96, loop 16). LYNX imports one clip per pass.

**On automation lanes too, |key| − 1 is a parameter index** in the scheduler's eyes (the lane's `DESc` names the
target; the observation below, that |key| stays the same within a segment, is PROVEN-FILE).

**Automation points (round 187d; PROVEN-FILE over the 20 sample projects, about 50 000 points).** An automation
clip's `EVN2` is its curve *already sampled*, not a list of control points to interpolate:
- a positive key marks a control point the user placed, a negative key a sample FLM computed between two control
  points. |key| stays the same within a segment (1, 2, 4, 5, 10, 20, 29, 37 seen); INFERRED: it names the segment's
  curve shape;
- between control points the samples sit one value step of 512 (1/128 of 0…65535) apart, at whatever tick the curve
  reaches that value, so they follow the curve whatever its shape;
- a jump is two control points on one tick.

So straight lines through every saved point give FLM's own curve to within 1/128 of the range. A reader needs no
shape maths; it must keep every point, the two-on-one-tick jumps included.

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

PROVEN-REA writer 0x18074a1c0 (the tail of `MAIN` and `LINk`: round 190bs, the drum sampler's trigger 0x1807503a0
and the clip compiler 0x18075d6b0):
- `MAIN`:
  - u32 −1;
  - f64 (−1.0);
  - char[512] **sample name**;
  - char[512] folder/category;
  - u8 **reverse** (the slot plays its sample backwards);
  - f64 **time shift** in beats, added to the start of every drum note of the slot (never before the clip's start);
  - f32 (0.5 in every file; not used by the playback code that was read);
  - u8 **cut group**: 0 = none, 1…3 = "A", "B", "C". Any trigger of a slot fades out (about 2 ms) the voices of the
    *other* slots of its group, not its own;
  - u8 **polyphonic** (1 in almost every file): 0 = a new hit fades out the slot's own voices;
  - optional lp16.
- `LINk`: i32 **slot id** inside the drum set; i32; u16 pad key (−1 = by order); i32 **output link** (−1 = the slot
  mixes into its own rack; another value sends it elsewhere — not imported, listed in the report).
- `STRC` (legacy, still read): u8 mode, f64 **time-stretch ratio**, f64 pitch ratio 2^(st/12), f32 normalized pitch
  (st/48 + 0.5), f32.
- `STR1`: u8 **stretch mode**, f64 stretch ratio, f64 **pitch in semitones**. PROVEN-REA (round 190bs, the loader
  0x180747a50 / 0x180740330): FLM renders a stretched copy of the file when it loads it. The copy has
  frames × ratio frames — the ratio is a **length** ratio (2 = twice as long) — and is used only when the mode is
  not 0; the pitch is rounded to whole semitones and shifts without changing the length.
- `PATH` (legacy, still read): char[2048] absolute path.
- `PTH1`: path (section 5).
- `PRST`: lp32 absolute path + lp32 relative name.
- `PRMS`: 11 × f32, the slot's parameters (§6.1). A file may hold fewer: the rest are at their defaults.

### 6.1 The sample slot's parameters (round 190bs)

A drum pad and an audio clip's sample are the same object, with eleven parameters. Names PROVEN-REA 0x180748680,
defaults 0x180748120, laws from the setter 0x180748390, the voice start 0x18074bf40 and the voice render 0x18074c070
(all in the FL-hosted "FL Studio Mobile_x64.dll"). `v` = the stored 0…1 value. LYNX's code: `core/FlmPad.h`.

| # | Name | Default | Law |
|---|---|---|---|
| 0 | Level | 0.853125 | gain = 10^(3.3·v^0.6 − 3): the fader law of §3 without its silent bottom (v = 0 is −60 dB, shown "-inf."); default = 0 dB, 1 = +6 dB |
| 1 | Pan | 0.5 | p = 2v − 1, a balance: the far side × (1 − \|p\|), the near side unchanged |
| 2 | Speed | 0.5 | whole semitones, (int)(48v − 24) truncated towards 0, ±24: a resampling (pitch and length together) |
| 3 | Mute | 0 | on above 0.5 |
| 4 | Solo | 0 | on above 0.5: every slot of the rack whose Solo is off is silent |
| 5 | Length | 0 | not 0: the release starts (int)(frames × v) output samples after the voice's start. Unused in the samples |
| 6 | EG A | 0 | attack: below 0.001 immediate; else rate = 0.0002 / ((0.95v + 0.05)^4 · 500) |
| 7 | EG D | 1 | decay rate = 0.0002 / ((0.95v + 0.05)^4 · 50) |
| 8 | EG S | 1 | sustain level, linear |
| 9 | EG R | 1 | release rate, as EG D |
| 10 | Sample Start | 0.5 | ±0.5·(2v − 1)² seconds: positive = that far into the file, negative = silence first |

The envelope is one level stepped once per output sample, `level += (target − level) · rate`: the attack goes
towards 5 and is clipped at 1; from there the decay goes towards EG S; the release goes towards 0 and the voice ends
below 0.0001. A release happens only (a) at a note's end on a lane that holds its notes (§4, `DESc`), (b) at
Length. The step comes before the sample is output. The rates are per output sample whatever the device's rate;
LYNX takes them at 44.1 kHz (an assumption: FL's default and the rate of the factory content).

Level and Pan apply to the slot's summed voices. A voice's own gain is its velocity; its note pan is the balance of
§4 (left = w·min(1, 2(1 − p)), right = w·min(1, 2p)); its speed is the note's pitch × Speed × file rate / output rate,
read with linear interpolation.

Use in FL's 20 sample projects (272 kit samples, 261 audio clips): Level 262, Speed 45, EG D 58, EG S 57, EG R 38,
EG A 10, Pan 8, cut group 5, Sample Start 3, Mute 2; Length, reverse and time shift 0.

A rack renders its drum slots first, then its modules in order on the same buffer: an effect processes only the
generators before it (PROVEN-REA 0x18059d3b0; LYNX's insert processes them all, which the import report lists).

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
| 52 | SoundFont Player | gen | 6 * |
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
| 25 | Pitcher | mod | 23 * |
| 26 | Analyzer | mod | 1 |
| 27 | Gate | dyn | 9 |
| 28 | Limiter | dyn | 5 |
| 29 | Autoduck | dyn | 5 / 8 |
| 30 | Tuned EQ | EQ | 11 * |
| 32 | Reverb | rev | 16 * |
| 34 | Spacer | EQ | 7 / 8 |
| 35 | Reverb 2 | rev | 19 |
| 36 | Multiband Compressor | dyn | 27 / 31 |
| 40 | Comb Filter | EQ | 12 |
| 41 | Waveshaper | dyn | 21 |
| 42 | Tape Stop | mod | 8 * |
| 43 | Wow & Flutter | mod | 11 |
| 44 | Spreader | EQ | 5 |
| 45 | Pitcher 2 | mod | 27 * |
| 46 | Tremolo | mod | 12 |
| 47 | Note Echo | note | 16 * |
| 48 | Transpose | note | 3 * |
| 49 | Randomizer | note | 5 * |
| 50 | Scale | note | 16 * |
| 51 | Arpeggiator | note | 31 (33 allocated *) |

Ids 17, 21, 22, 31, 37 and 38 are not registered: retired modules. A file that holds one makes FLM itself log
"Unknown module type". "*" = the count is not seen in any sample: it is the number the module's constructor allocates
(round 190bn, read in the Android 5.0.3 build; the Arpeggiator saves 31 of its 33). Known parameter indices, from automation
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


### 7.1 Parameter names per module (F4/F5 groundwork)

Each module class has a parameter-name function: a switch from the PRMS index to the name. The list below is
PROVEN-REA: the 43 overrides of the base 0x1807993f0, read in round 182c's third REA turn, 06:47–07:02.
The base names index 0 "Enable" in every module, and the SMPR ids: 0x7FC0 "Post Gain", 0x7FC1 "Mix",
0x7FC2 "Wet Pan". Function → module is INFERRED from the names, with "?" where unsure. The automation lane names
of section 7 confirm MiniSynth, DW Sampler, GMSynth, SuperSaw, Equalizer, Reverb (legacy), Flanger, Spacer,
Autoduck (legacy), Reverb 2 and MultiFX. Indices the switch does not name are left out: some cases use code
shapes the extraction didn't read. The complete list is in `/mnt/e/lynx-work/flm182c/rea/paramnames.txt`.

| Module | Function | Parameters (index = name) |
|---|---|---|
| 0 MiniSynth | 0x1806ae070 | 1=Voices Mode | 2=Transpose | 3=Slide Time | 4=MW Dest | 5=MW Amount | 6=OSC Kind | 7=OSC Mod | 8=OSC Noise | 9=FLT Freq | 10=FLT Peak | 11=FLT Kbd | 12=FLT Vel | 13=EGF Atk | 14=EGF Dec | 15=EGF Sus | 16=EGF Rel | 17=EGF Amnt | 18=LFO Shape | 19=LFO Dest | 20=LFO Rate | 21=LFO Amnt | 22=AMP Atk | 23=AMP Dec | 24=AMP Sus | 25=AMP Rel | 26=DIST | 27=Overdrive | 28=Decimator | 29=DELAY | 30=DELAY Time | 31=DELAY Fdbk | 32=DELAY Filter | 33=DELAY Mix | 34=CHORUS | 35=CHORUS ModRate | 36=CHORUS ModDepth | 37=CHORUS Mix | 38=PHASER | 39=PHASER ModSpeed | 40=PHASER ModWidth | 41=PHASER Fdbk | 42=PHA … |
| 1 DW Sampler | 0x180781d00 | 1=Attack | 2=Release | 3=Pitch | 4=Fine Tune | 5=Decay | 6=Sustain | 7=Slide | 8=Reverse | 9=Loop | 10=Legato | 11=Mono | 12=Cutoff | 13=Resonance | 14=Kbd. Track | 15=Vel. Track | 16=Filter type | 17=Filter Attack | 18=Filter Decay | 19=Filter Sustain | 20=Filter Release | 21=Filter Env. Amount | 22=Pitch Attack | 23=Pitch Decay | 24=Pitch Sustain | 25=Pitch Release | 26=Pitch Env. Amount | 27=LFO Shape | 28=LFO Rate | 29=LFO Mode | 30=LFO Sync Rate | 31=LFO Amount | 32=LFO Destination | 33=LFO Attack |
| 2 GMSynth | 0x180647390 | 1=Level | 2=Pan | 3=Level Attack | 4=Level Decay | 5=Level Sustain | 6=Level Release | 11=Osc B Pitch | 12=Osc B Detune | 13=Osc B Mix | 14=Osc B Phase | 15=OSc Mod | 16=Osc Mod Kind | 17=Osc Mod Attack | 18=Osc Mod Decay | 19=Osc Mod Contour | 20=Osc Noise | 21=Cutoff | 22=Resonance | 23=Filter Kind | 24=Filter Kbd. Scl. | 25=Filter Attack | 26=Filter Decay | 27=Filter Contour | 28=Transpose | 29=Slide Time | 30=Single Voice | 31=Auto Portamento | 32=LFO Rate | 33=LFO Shape | 34=LFO Ampl. | 35=LFO Dest | 36=EG Attack | 37=EG Decay | 38=EG Sustain | 39=EG Release | 40=EG Contour | 41=EG Dest | … |
| 3 Equalizer | 0x18061b5a0 | 1=Gain 1 | 2=Frequency 1 | 3=Bandwidth 1 | 4=Kind 1 | 5=Gain 2 | 6=Frequency 2 | 7=Bandwidth 2 | 8=Kind 2 | 9=Gain 3 | 10=Frequency 3 | 11=Bandwidth 3 | 12=Kind 3 | 13=Gain 4 | 14=Frequency 4 | 15=Bandwidth 4 | 16=Kind 4 | 17=Channel 1 | 18=Channel 2 | 19=Channel 3 | 20=Channel 4 |
| 4 MultiFX | 0x18062cd50 | 1=FX Kind | 2=Axis X | 3=Axis Y | 4=LFO Rate | 5=LFO Amount | 6=LFO Shape |
| 5 Limiter (legacy) | 0x180625ce0 | 1=Threshold | 2=Release Time | 3=Output Gain |
| 6 Distortion | 0x180618700 | 1=Drive | 2=BitCrush | 3=Gain | 4=Stereo | 5=Kind | 6=Bias |
| 7 Graphic EQ | 0x1806192b0 | 1=80 Hz | 2=160 Hz | 3=375 Hz | 4=750 Hz | 5=1.5 kHz | 6=3 kHz | 7=6 kHz | 8=12 kHz |
| 8 Compressor | 0x180615bc0 | 1=Thresh | 2=Ratio | 3=Attack | 4=Release | 5=Level | 6=Hold | 7=Auto gain | 8=Input gain | 9=RMS |
| 10 Autoduck (legacy) | 0x18063fc30 | 1=Ducking | 2=Attack |
| 11 Chorus | 0x1806146c0 | 1=Rate | 2=Depth | 3=Stereo |
| 12 Reverb (legacy) | 0x1806367c0 | 1=DECAY | 2=HIGH DAMP | 3=MIX | 4=PREDELAY | 5=ROOM SIZE | 6=DIFFUSION | 7=WIDTH | 8=LO-CUT | 9=HI-CUT | 10=INPUT | 11=EARLY REFL. | 12=DRY LEVEL | 17=BASS MULT. | 18=CRSV | 19=MOD SPEED | 20=MOD AMOUNT |
| 13 Flanger | 0x18061fc60 | 1=LFO Rate | 2=Sync | 3=LFO Speed | 4=LFO Shape | 5=LFO Amount | 6=Delay | 7=Feedback | 8=Inv. FDBK | 9=Stereo | 10=Mix | 11=Inv. Mix |
| 14 Phaser | 0x18062ffc0 | 1=LFO Rate | 2=LFO Amount | 3=Feedback | 4=Frequency | 5=Mix | 7=LFO Speed | 8=LFO Shape | 9=Stereo |
| 15 Stereoizer | 0x18062e500 | 1=Strength | 2=Timbre | 3=Focus | 4=L/R Delay |
| 16 Trance Delay? | 0x18063ef40 | 3=Mix |
| 18 Tape Delay? | 0x18063df50 | 3=Async. |
| 19 Transistor Bass | 0x180585070 | 1=Waveform | 2=Cutoff | 3=Reso | 4=Env Mod | 5=Decay | 6=Accent |
| 20 Leveller | (round 190bn) | 1=Gain | 2=Pan |
| 28 Limiter | 0x180626690 | 1=In Gain | 2=Out Gain | 3=Release Time | 4=Threshold |
| 23 SuperSaw | 0x1807f6b90 | 2=Osc. Pulse Mode | 3=Osc. Sub Level | 4=Osc. Unisono | 5=Osc. Unisono Detune | 6=Osc. Unisono Pan | 7=Osc. Unisono Detune Curve | 8=Osc. Re-trig Phases | 9=Osc. Phases Offset | 10=SuperSync Mode | 11=Osc. Sync | 12=Osc. Pulse Width | 13=Osc. Sync Attack | 14=Osc. Sync Decay | 15=Osc. Sync Sustain | 16=Osc. Sync Release | 17=Osc. Sync EG Amount | 18=Osc. Sync Kbd Dir. | 19=Filter Cutoff | 20=Filter Resonance | 21=Filter KBD | 22=Filter EG Attack | 23=Filter EG Decay | 24=Filter EG Sustain | 25=Filter EG Release | 26=Filter EG Amount | 27=Filter VEL | 28=Level EG Attack | 29=Level EG Decay | 30 … |
| 25/45 Pitcher (2)? | 0x180465280 | 1=Note C | 2=Note Db | 3=Note D | 4=Note Eb | 5=Note E | 6=Note F | 7=Note Gb | 8=Note G | 9=Note Ab | 10=Note A | 11=Note Bb | 12=Note B | 13=Speed | 16=Formant Mode | 17=Formant Ratio | 18=Ref. Channel | 19=Stability | 20=Humanization | 21=Snapping | 22=Mix | 23=Sensitivity | 24=Vibrato | 25=Latency | 26=Process Sibilants |
| 27 Gate | 0x180624300 | 1=Threshold | 2=Asymmetry | 3=Attack | 4=A. Curve | 5=Release | 6=R. Curve | 7=Mix | 8=Hold |
| 29 Autoduck | 0x180640bc0 | 1=Ducking | 2=Attack | 3=Curve | 4=Time | 5=Shift | 6=Hold | 7=Shape |
| 30 Tuned EQ? | 0x18061a0c0 | 9=Tuning | 10=Bandwidth |
| 32 Reverb | 0x180637b50 | 1=DECAY | 2=HIGH DAMP | 3=MIX | 4=PAN | 5=EARLY REFL. | 6=PREDELAY | 7=ROOM SIZE | 8=DIFFUSION | 9=WIDTH | 10=LO-CUT | 11=HI-CUT | 12=MOD SPEED | 13=MOD AMOUNT |
| 34 Spacer | 0x18063dc60 | 1=L/R Delay | 2=Pan | 3=Separation | 4=L/R Inversion | 5=M/S Delay | 6=M/S Inversion | 7=Stereo Mode |
| 35 Reverb 2 | 0x1806394c0 | 1=HIGH CUT | 2=LOW CUT | 3=SIZE | 4=DECAY | 5=DIFFUSION | 6=DAMPING | 7=DRY | 8=ER | 9=WET | 10=SIDE INPUT | 11=DELAY | 12=TEMPO | 13=MOD | 14=BASS | 15=SEPARATION | 16=SPEED | 17=CROSS | 18=PAN |
| 39 3x Osc | 0x1807e24c0 | 24=Osc 3 = amplitude modulation | 25=HQ Mode | 26=Stereo Phase Randomness | 27=Pan LFO Global Phase | 28=Volume LFO Global Phase | 29=ModX LFO Global Phase | 30=ModY LFO Global Phase | 31=Pitch LFO Global Phase | 32=Pan LFO Tempo Sync | 33=Volume LFO Tempo Sync | 34=ModX LFO Tempo Sync | 35=ModY LFO Tempo Sync | 36=Pitch LFO Tempo Sync | 37=Pan LFO Shape | 38=Volume LFO Shape | 39=ModX LFO Shape | 40=ModY LFO Shape | 41=Pitch LFO Shape | 42=Pan LFO Predelay | 43=Volume LFO Predelay | 44=ModX LFO Predelay | 45=ModY LFO Predelay | 46=Pitch LFO Predelay | 47=Pan LFO Attack | 48=Volume LFO Attack  … |
| 40 Comb Filter | 0x18045a700 | 1=Mode | 2=Pitch | 3=Feedback | 4=Mix | 5=Offset | 6=Smoothing | 7=Snap | 8=Polarity | 9=Asymmetry | 10=Pitch Oct | 11=Pitch Note |
| 41 Waveshaper | 0x180481070 | 1=Shape | 2=Auto-Gain | 3=X | 4=Y | 5=Drive | 6=Post Gain | 7=Stereo Spread | 8=Mix | 9=High Cut | 10=Low Cut | 11=Bass | 12=Mid Freq | 13=Mid Gain | 14=Treble | 15=LFO Shape | 16=LFO Sync | 17=LFO Rate | 18=LFO Rate Free | 19=LFO to X | 20=LFO to Y |
| 42 Tape Stop | 0x1804788d0 | 3=Stop Time | 4=Start Time | 5=Toggle Play | 6=Warp | 7=Sync |
| 43 Wow & Flutter | 0x180488b90 | 2=Wow Rate | 3=Wow Amount | 5=Flutter Rate | 6=Flutter Amount | 7=Sync | 9=Random Rate | 10=Random Amount |
| 44 Spreader | 0x18046f780 | 1=Width | 2=Low Bypass | 3=Mono Bass |
| 46 Tremolo | 0x18047d750 | 1=Rate Free | 2=Rate Sync | 3=Amount Max | 4=Amount Min | 5=Phase Offset | 6=Stereo Offset | 7=Sync | 8=Shape | 9=Smooth | 10=Skew | 11=Width |
| 47 Note Echo | 0x18045f2c0 | 1=Repetitions | 2=Delay | 3=Delay (sync) | 4=Sync | 5=Pitch Offset | 6=Vel. Offset | 7=Velocity | 8=Pitch | 9=Time Drift | 10=Length Drift | 11=Delay Rand. | 12=Pan Offset | 13=Pan Drift | 14=Ping-Pong | 15=Stop |
| 48 Transpose | 0x18047a830 | 1=Octave | 2=Semitone |
| 49 Randomizer | 0x180468390 | 1=Pitch | 2=Velocity | 3=Pan | 4=Gate |
| 50 Scale | (round 190bn) | 1=Key | 2=Scale | 3=Snap | 4…15=Note C … Note B |
| 51 Arpeggiator | 0x180453a40 | 1=Rate | 2=Rate (ms) | 3=Sync | 4=Swing | 5=Swing rate | 6=Latch | 7=Velocity 1 | 8=Velocity 2 |
| ? (ATTACK, SUST…) | 0x1807d4d30 | 3=ATTACK | 5=SUST |
| 52 SoundFont Player | 0x1807eff80 | 1=Attack | 2=Decay | 3=Sustain | 4=Release | 5=Envelope |

(Round 190bs corrected this table: until then it listed Limiter's names under Leveller, and Limiter (legacy) without
its Output Gain. LYNX's engines always used the right lists, `core/PocketFx.h`.)

### 7.2 Note effects (round 190bs)

PROVEN-REA in the FL-hosted build: the rack's note pass 0x18059c400, Transpose 0x18047abe0 / 0x18047a970, Scale
0x180469f80 / 0x18046a470, Randomizer 0x180468b60 / 0x180468550. LYNX's code: `core/FlmNoteFx.h`.

Before a rack hands its notes to its generators it runs them through every note-effect module, in the rack's module
order, wherever those modules sit among the instruments. Only note events pass through them: drum lanes are not
touched. A module whose Enable (parameter 0) is below 0.5 passes everything on. Then every generator plays every note
inside its key range (`ADD1`), so a rack with two generators is a layer or a keyboard split.

- **Transpose**: Octave = round(8v − 4), Semitone = round(24v − 12); the key moves by semitone + 12·octave and is
  kept inside 0…127. A note's end uses the key its start was given.
- **Scale**: Key = round(11v) (the root), Scale = round(33v) (33 named scales, the last value "Custom"), Snap above
  0.5, parameters 4…15 one switch per pitch class C…B. The switches are the scale that is used. A note outside it
  moves to the nearest key inside when Snap is on, and is *not played* when Snap is off. "Nearest" looks one semitone
  further each turn: upwards first for the major-like scales (Major, Major Bebop, Major Bulgarian, Major Pentatonic,
  Major Persian, Major Polymode, Enigmatic, Lydian, Whole Tone), downwards first for the others, upwards first for a
  custom set. The list, in order: Chromatic, Major, Major Bebop, Major Bulgarian, Major Pentatonic, Major Persian,
  Major Polymode, Minor Harmonic, Minor Hungarian, Minor Melodic, Minor Natural, Minor Neapolitan, Minor Pentatonic,
  Minor Polymode, Minor Romanian, Arabic, Blues, Diminished, Dominant Bebop, Dorian, Egyptian, Enigmatic, Hirajoshi,
  Iwato, Japanese Insen, Locrian, Locrian Super, Lydian, Mixolydian, Neapolitan, Phrygian, Phrygian Dominant, Whole
  Tone.
- **Randomizer**: Pitch = round(24v) semitones either way (a whole number, drawn evenly), Velocity ± v, Pan ± v of the
  whole width, Gate = the chance a note plays (0 = none). Defaults 0, 0, 0, 1: nothing. FLM draws at every playback;
  LYNX draws once at import (listed in the report).
- **Note Echo** and **Arpeggiator** act in time: §7.8 (round 190de).

### 7.3 SoundFont Player (round 190bs)

PROVEN-REA (class CSoundFontPlayer: names 0x1807eff80, defaults 0x1807eff30, envelope rates 0x1807f23a0, render
0x1807f0820). `CSTM` holds one chunk `SF2D`: lp32 the font's path, i32 the chosen preset's index in the font (that the
index follows the file's preset order is INFERRED; no sample project uses the module).
- Envelope (parameter 5) above 0.5 = "Custom": the four knobs replace the font's volume envelope; else "Original".
- Custom times: T = max(0.005, (0.95v + 0.05)³ · 10) seconds. The attack reaches the top after T (a one-pole towards
  5, clipped at 1). Decay and release are exponential and fall 40 dB in T. Sustain: gain 10^(−5 + 5·(n/127)^0.2), n =
  round(127v) kept in 1…127 — the desktop Fruity Soundfont Player's law.
- Original: T = max(0.005, 2^(timecents / 1200)) with a rate 8.82 / (T · sample rate); sustain 10^(−0.005 · cB).
- The level is the zone's attenuation (10^(−0.005 · cB)) × the velocity, linear. 32 voices, linear interpolation.

LYNX imports it as LYNX Soundfont: the font and preset, the linear velocity curve, and for Custom the four overrides
(decay and release as 2.5·T on LYNX's 100 dB time knobs). The level and the envelope's exact shape are approximations.

### 7.4 The module wrapper on instruments; instrument and Pitch Bend automation (round 190de)

PROVEN-REA 0x180799750 (the wrapper the rack's render 0x18059d3b0 calls for every module, generators included):
- Enable (parameter 0) below 0.5, or Mix 0: the module's render is **not called** — a switched-off instrument makes
  no sound and its voices stand still until it is switched on again.
- Otherwise the module renders into its own buffer; the result × the Post Gain 10^(2g − 1) (40g − 20 dB) is mixed into
  the rack's buffer at Mix, the wet side through the Wet Pan's balance (min(1, 2(1 − p)) left, min(1, 2p) right),
  both smoothed per sample (one-pole 0.001). With Mix 1 and Wet Pan ½ the module renders straight into the rack's
  buffer and the Post Gain then scales that whole buffer (what earlier modules and the drum slots put there too).
- LYNX: a generator's channel is muted when its Enable is off; Post Gain × Mix is the channel's level (above 0 dB: the
  sampler's gain, capped at 0 dB on other engines and listed); Wet Pan is added to the channel's pan. Exact for the
  first generator of a rack without drum slots (its "dry" buffer is silent); with other sounds before it in the rack
  a Mix below 1 would also lower them in FLM, which is listed. In FL's 20 sample projects: 3 generators switched off,
  one Post Gain (−6 dB) and one Mix (0.045).

Automation lanes on a generator's parameters (their names in §7.1, values the module's own 0…1): every FLM engine
LYNX has keeps FLM's values, so a lane becomes LYNX automation of the same value — Pocket SuperSaw (FLM k → k − 1),
MiniSynth's FLM build (FLM k → desktop k − 1; MW Dest folded as on import), GMSynth (FLM) (P[k]), Transistor Bass
(the 13 knobs through the import's map). Enable lanes drive the channel's volume (on / silent): approximate, as
FLM freezes the module's voices while LYNX's run on silently. The sample projects hold 19 such lanes: MiniSynth FLT
Freq (9) and AMP Rel, SuperSaw Filter Cutoff, Transpose and Enable (2), GMSynth Transpose (2), DW Sampler Pitch (3).

The rack's Pitch Bend (module 0, parameter 4; its held value and its lanes, PROVEN-REA 0x18059e0c0) is not one law:
the rack hands b = 4v − 2 to every module's bend input and plays its drum slots at 2^b (±24 semitones). The modules
bend by their own range: DW Sampler b/2 × its PB Range (CSTM `MIDI`, default 4; PROVEN-REA 0x18077f560), SuperSaw b/2 ×
int(11·P37 + 1), GMSynth b/2 × its CSTM range {1, 2, 7, 12}, MiniSynth b semitones (PROVEN-REA 0x1806aea20: stored as
is and added to the note). LYNX: the channel's bend range is set to that range (24 on drum slots); a held value moves
the channel's PITCH knob by (2v − 1) × range, a lane becomes LYNX pitch-bend automation of every such channel (LYNX
bends by (2v − 1) × the range: the same semitones). Modules whose bend was not traced (Transistor Bass, 3x Osc,
SoundFont Player) are listed. Three sample projects bend this way (one lane holds 114 clips). Aftertouch and MIDI CC
lanes stay listed.

### 7.5 DW Sampler (round 190de)

PROVEN-REA (class CSmpSynth, vtable 0x180a0ecd8: CSTM reader 0x180785550, preset loader 0x180787410, note slot
0x180783980, render 0x180783e90, voice 0x18078bf00 / 0x18078b3f0, filter update 0x18078c440, setter 0x180784a50);
PROVEN-FILE on the 42 CSTM bodies and 56 parameter sets of FL's sample projects and the 6 shipped `.dwp`. LYNX's
code: `core/FlmDwSampler.h` (FLM's own engine on a Sampler channel's zones).

**CSTM** = `10WD` + chunks (unknown tags skipped): one `ZONE` per zone **or** one `PATh` / `PATH`, then `MIDI`.
- `ZONE` (1065 bytes): 1024-byte path (root marker `@R@` / `@L@`, NUL, relative path; bytes after the next NUL are
  stale and ignored — 13 sample bodies carry such text), then root key, key low, key high (128 in a default zone),
  velocity low, velocity high (u8 each), f32 gain (used only above 0.0001 **and with Loop on**; old files hold the
  integer 63 here, i.e. gain 1), f32 pan (not used), i32 loop mode (loops only from 2), i32 loop start, i32 loop end
  (exclusive; start = end → the file's own loop), f32 attack, decay (only its sign: ≤ 0 = instant decay), sustain (not
  used), release.
- `PATh`: lp32 path + f32 Attack + f32 Release (they replace parameters 1 and 2 after the file loads); `PATH`: path only.
  The file is a `.dwp` (FLM reads it itself: `DwPr`, version > 35, entries u32 id + u64 size; program id 1, zone id 3,
  end id 2; zone entries 500 = root / ranges / gain / pan, 502 = the sample's path, 503 = frames, channels, bits, rate,
  loop mode / start / end, 509 = A / D / S / R; after loading, Attack / Release = the last zone's) or a single sample
  (one zone: root 48, every key and velocity, looping on the file's own loop).
- `MIDI`: [PB Range in semitones (default 4), smooth attack (default 1: an Attack of 0 still takes 1 ms)].

**Parameters** (v = stored 0…1; float32 where it matters): 1 Attack, 2 Release, 5 Decay: T = 10·v⁴ + 0.001 s, fixed
at the note's start; 3 Pitch = int(48v − 24) semitones (it also moves the key a zone is chosen with); 4 Fine Tune =
2v − 1 semitones; 6 Sustain = the envelope's level; 7 Slide: glide time (v/2)³ s (none below 1e-5), re-planned every
render call; 8 Reverse (no loop); 9 Loop (zone loops and zone gains); 10 Legato; 11 Mono (last-note priority, glides
without retrigger); 12 Cutoff c, 13 Resonance (ladder feedback v, biquad Q = 16v³ + 0.5), 14 Kbd. Track, 15 Vel. Track
(adds v × velocity to c); 16 Filter type = int(3v): off, 4-pole ladder low-pass, band-pass, high-pass (the corpus value
0x3EAAAAAB is the ladder only in float32); 17…21 / 22…26 filter / pitch envelope attack, decay, sustain, release,
amount; 27 LFO shape int(3v + 0.5): sine, falling saw, square, sample-and-hold; 28 rate 1 / ((1 − v)³·30 + 1/30) Hz;
29 mode int(2v): free, one shot, tempo sync ({1/8 … 32} beats by 30, aligned to the song position); 31 amount ±(2v −
1)²; 32 destination int(3v): cutoff, pitch, volume, pan; 33 LFO fade-in.

**Voice**: zone = the one whose key and velocity ranges hold key + Pitch and int(127·velocity + 0.5) (several: a
seeded draw, never the same twice in a row on a key; none: no sound); rate = 2^((key − root)/12) × file rate / output
rate × 2^((Fine Tune + Pitch + bend)/12) × glide × pitch envelope × LFO, linear interpolation at the file's own rate;
the level is the velocity (× the zone's gain with Loop on), **linear**; the amp envelope is linear and heard **squared**
(attack 0→1 in TA, decay 1→S in (1 − S)·TD, release from the level in level·TR); filter and pitch envelopes are
one-pole segments (rate 0.0002 / ((0.95v + 0.05)⁴ × 500 for attacks, 50 otherwise), per sample whatever the rate, the
attack aiming at 4 and clipped at 1); the pitch envelope bends 2^(4·(2v − 1)³·e³); cutoff = min(22 kHz,
2^((key + glide − 48)·kbd/12 + 10.4c + 4 + (2·amount − 1)·8·e)), filters and LFO updated every 16 samples; note pan is
a balance. 128 voices, no stealing. A voice ends past its sample's end, or silent after its attack.

LYNX: the channel is a Sampler channel whose zones carry the files, ranges, gain and loops, and whose `modelParams`
start with the engine's marker, the PB Range, the smooth flag, the 34 values, then one decay flag per zone; the rack
plays FLM's voice on it (`core/FlmDwSampler.h`). The module's values are automatable (`automationBase` + k; FL's sample
projects automate Pitch). Editing the Sampler's envelope by hand drops the module's values (LYNX's own voice).
Approximate: the shared random generator is one per channel; the render call is LYNX's block cut to 512 samples (FLM
plans its glides per host call); reverse starts on the last frame. Not read: the old `INST` preset format and audio
embedded in a `.dwp` (no sample file uses either).

### 7.6 Pitcher and Pitcher 2 (round 190de)

PROVEN-REA (Pitcher: vtable 0x1809fd4c8, CSTM 0x1806359f0 / 0x180635aa0, target choice 0x1806347c0; Pitcher 2:
vtable 0x1809e6ed0, engine setter 0x18072f7f0, note choice 0x18078ee90). Both correct the sung pitch towards the
nearest of twelve allowed pitch classes. **Pitcher** (25): 1 Speed (a one-pole of τ = (3(1 − v)³ + 0.001) / 8.82 s,
instant from 0.99), 2 Pitch (int(24(v − ½)) semitones), 3 Fine Tune (int(100(v − ½)) cents), 4 formant mode, 5 formant
shift 2^(2v − 1), 6 detector input (left, sum, right), 7 Base Freq (A4 = 440·2^((2v − 1)/12)), 8 Stability, 9 Mix,
10…21 per-note bypass (on at 1), 22 bypass snapping; CSTM = 12 mask bytes (C…B) + Key and Scale menu floats (the mask
is what plays; its "Pentatonic" holds four notes). **Pitcher 2** (45): 1…12 the notes (default C minor), 13 Speed (four
one-poles at (200^v − 0.9)·2.29896 Hz), 16 Formant Mode (inverted), 17 formant ratio, 18 detector input (labels rotated
against the engine), 19 Stability, 20 Humanization, 22 Mix, 23 Sensitivity, 24 Vibrato, 25 low latency; 14, 15 unused;
CSTM = Key and Scale menu floats only.

LYNX: LYNX Tune with Scale = Custom and the same twelve note switches (exact), the retune time from the Speed law
(Pitcher: the same one-pole; Pitcher 2: matched at its half-way time), transpose, mix, formant preserve / shift
(wide range), per-note bypass, bypass snapping (correction 0), the reference pitch from Base Freq and the fine tune
(430…450 Hz), Pitcher 2's Stability as LYNX's tolerance. Approximate and listed: the detectors and shifters (LYNX's
own), Pitcher's Stability, Pitcher 2's Humanization and Vibrato. No shipped file uses either module.

### 7.7 Slicer (round 190de)

PROVEN-REA (class CSynthSlicer: setter 0x1807d54f0, CSTM 0x1807d6540 / 0x1807d5f40, detector 0x180591160 /
0x1805915d0, voice 0x1807d6e60 / 0x1807d4ec0). Parameters: 1 PITCH int(48v − 24), 3 ATTACK and 4 DECAY (the drum
pad's laws, §6.1; DECAY above 0.99 holds), 5 SUST int(2v): one-shot slice / to the sample's end while held / to the
end, 6 MONO (a note fades the others at 0.01). CSTM: `SENS` (f32 sensitivity, 0 = the file's markers; u8 editor
flag) and `PATH` (u32 n + path). Slicing: the left channel normalised, three float passes of a gentle high-pass,
128-point frames every 16 samples with a periodic Hamming window, power in bins 1…5 and 6…63, per band a hold (55 / 41
frames), release (0.9761 / 0.9644) and a rise above the held level (× 0.9 / 1.0, floor 1e-10 / 1.8 or 20), the
strength × sensitivity; an onset where it reaches 1, started at the earliest frame of the last 28 above 0.75 and then
at the first sample louder than 5 % of the peak, at least 60 ms apart, at most 128. Slice i plays on the saved low key
+ i (FLM saves the key range it gave the slices). Voice: linear interpolation, linear velocity and balance, every note
its own voice, a 512-sample dip before the slice's end, a fade at 0.01 per sample at the end point.

LYNX: a Sampler channel with the sample and `modelParams` = { marker, sensitivity, first key, the 7 values }; the rack
slices the decoded sample and plays FLM's voice (`core/FlmSlicer.h`). The shipped break (`Parrik - Initiated`) slices
at 0.25 into the 11 starts FLM saved (also the starts at 0.05, 0.5 and 1.0 of round 190de's simulation, to the frame).

### 7.8 Arpeggiator and Note Echo (round 190de)

PROVEN-REA (Arpeggiator: vtable 0x1809e5c50, engine 0x18044a2c0, sequence builder 0x18044b610, grid 0x18044d510;
Note Echo: vtable 0x1809e69b0, scheduler 0x18045ffc0, emit 0x180460380). Both run on the rack's notes before its
instruments, in the rack's order with the other note effects (§7.2), and both act **in time**:
- **Arpeggiator** (51): two independent arps fed the same notes, which they replace. Global: 1 Rate (1/16 … 1/2, or
  one bar), 2 Rate in ms (50 + 950v, Sync off), 3 Sync, 4 Swing, 5 swing rate (1/8, 1/16), 6 Latch, 7 / 8 the arps'
  velocity (× 2v). Per arp (arp 1 at 9…15, 31, 17…19; arp 2 at 20…26, 32, 28…30): Gate 0.1 + 0.9v, Arp = one of 24
  modes (As Played, Chord, Up, Down, Up-Down, Down-Up, Outward, Inward, Anchor, Bass Up, Bass Up-Down, Treble Down,
  Treble Down-Up, Jump Up, Jump Down, Jump Up-Down, SuperSaw, Cascade Up / Down / Up-Down, Random, Random No Repeat,
  Random Once, Random Walk), Range 1…4 octaves, Octave shift ±3, Start note ±8, Rhythm (21 patterns) and Rotate, a
  rate multiplier ×1/8…×8 (31-value files keep it at 16 / 27 in 11 steps), Repeats, Retrig (notes, 1/8 … 8 bars),
  Enable. Steps sit on multiples of the step from song position 0; a chord plays from the next grid point; releasing
  every key ends the arp's notes.
- **Note Echo** (47): the dry notes pass; up to 32 echoes, the first after D (beats: 1/32 + 7.96875v², or the nearest of
  1/8 … 8 with Sync), the next gaps D·d², D·d³… (Time Drift d = 0.7…1.3), each echo + Pitch semitones and × Velocity,
  the first with the offsets, pan drift and ping-pong, lengths × Length Drift (capped by the gaps); the chain stops at
  velocity ≤ 1/127 or a length under 1/128 beat; a dry note still held at its first echo ends there.

LYNX bakes them: a rack with an active Arpeggiator or Note Echo takes its notes from every note lane as the song plays
them, runs the rack's note effects in order and writes one pattern over the song, on its first note lane's row.
Exact for every mode but the random ones (Random… and Delay Rand.: one seeded draw, listed) and up to FLM's audio-block
jitter. Left out: slide notes and note repeats in such a rack, the same-key rules of Note Echo (an echo under a held
dry note of its key), the editor's Stop.

### 7.9 MIDI to the modules, controller links, pad output links, GMSynth's last four effects (round 190dw)

PROVEN-REA (FL-hosted build; addresses in the round's notes `/mnt/e/lynx-work/flm190dw/rea/spec-item4.md`).

- **How the rack hands MIDI on** (setter 0x18059e0c0): every CC lane value goes to each module's CC handler (vtable
  slot 77); CC 1 then to slot 74 (mod wheel), the Pitch Bend 4v − 2 to slot 75, the Aftertouch value to slot 76. CC 7
  is the rack's Level (same storage and fader law), CC 8 and CC 10 its Pan, CC 64 falling below ½ releases held notes.
- **Who listens** (the 47 vtables): mod wheel — MiniSynth (engine wheel × MW Amount on MW Dest: filter ×4, LFO amount,
  OSC Mod), GMSynth and SuperSaw, as the raw 0…1 value; nobody else. **Aftertouch — no module** (slot 76 is the base's
  empty handler everywhere; the rack only stores RPRM [5]). Bend — DW Sampler, MiniSynth, GMSynth, SuperSaw, 3x Osc
  (stored; its use was not found) and SoundFont Player (half the value × 2: ±2 semitones); **Transistor Bass does not
  bend** (base handler).
- **CCLK** (rack child): 12-byte entries (i32 CC, i32 module instance id, i32 parameter). The base CC handler sets that
  parameter of that module to the CC's value: a CC lane is then a lane on each linked parameter.
- **PADS** only maps the 22 on-screen pads to kit slots (the pad grid reads it); notes carry the slot index, so it
  does not change playback.
- **A pad's output link** (LINk's fourth word, a channel id): the slot is not mixed into its own rack; the target
  channel's mix renders it into the target's buffer, with the sends that channel receives.
- **GMSynth's FLNG / F HP / RING / VOX** (kinds 1, 4, 8, 9; until now passed through):
  FLNG = the MultiFX flanger's tape ((int)(fs·0.005) samples), −(L + R)/2·fade in, feedback −0.96·Y, delay
  (1 − (X + lfo/2))⁴ of the tape, added to both sides; F HP = four high-pass biquads (2^(7.424·(X + lfo) + 5) Hz,
  q = 2Y² + 1, feed-forward (1 + cos)/(sin/q + 2)) in F LP's pipeline; RING = a sine of 2^(8.7·(X + lfo/2) + 5) Hz times
  the signal, Y unused; VOX = eight constant-skirt band-passes (Q 12) per side on three formant sets (A 250/700/2500/3500,
  B 700/1250/2500/3500, C 250/2500/2700/3500, then 4200/8500/10000/12000 Hz for all) cycled A → B → C → A by 3·(X + lfo),
  shifted by 2^(1.7·(Y − ½)), the sum × (1 − pair/8). RING and VOX fade their wet in through a second gain that a reset
  sets to 0 and that rises by 0.005 a sample.
- **3x Osc's CSTM**: one "PATH" chunk (u32 length + "@R@…"/"@L@…" text), the custom shape's file.

LYNX: GMSynth (FLM) renders all ten kinds; MiniSynth's FLM build, GMSynth (FLM) and Pocket SuperSaw take a CC #1 lane
as their wheel; CC 7 / 8 / 10 lanes play as Level / Pan lanes; CC lanes with CCLK links play as lanes on the linked
parameters; Aftertouch lanes and held aftertouch are dropped (nothing in FLM reads them); a linked pad plays into the
target channel's insert; SoundFont Player's held bend and lanes use ±2 semitones, Transistor Bass ignores the bend;
SoundFont Player's Attack / Decay / Sustain / Release lanes (Envelope = Custom) and 3x Osc's Pan lane import; 3x Osc's
custom shape plays the file its PATH names.

### 7.10 Equalizer, Graphic EQ, Filter, Autoduck (legacy) and the three reverbs on FLM's engines (round 190dw)

Until round 190dw these seven modules were approximated (rounds 188f / 188h / 190by: the Parametric EQ, the rhythm gain
and FL's Reeverb 2). They now play on FLM's own engines, laws in `docs/analise/REA_rodada190dw.md` (PROVEN-REA, every
set rendered by FLM inside FL and nulled at the 16-bit floor):

| Module | LYNX | What the old mapping missed |
|---|---|---|
| Equalizer (3) | Pocket EQ | the 2× hold / keep path (high bands up to 7 dB off), ±24 dB (capped at ±18), one-channel / opposite-gain channel modes, the 1-sample pipeline |
| Graphic EQ (7) | Pocket Graphic EQ | the 8-sample pipeline, ±24 dB bands |
| Filter (9) | Pocket Filter | 16 Hz…20.2 kHz (clamped to 20 Hz…20 kHz), the level offsets above ±18 dB, one sample per biquad pair |
| Autoduck (legacy, 10) | Pocket Autoduck (module 29's engine) | the curve between 12 points, the 0.01 one-pole; depth Ducking² = √(Ducking⁴) |
| Reverb (legacy, 12), Reverb 2 (35), Reverb (32) | Pocket Reverb Classic / Reverb 2 / Reverb | FLM's own plate: half-sum input, the second diffuser coefficient on stages 2 and 4, early reflections after stage 2 (FLM's long low end), truncated ER offsets, exact ms predelay, its gain glides; Reverb 2's separation, pan and side input; Reverb (32)'s raw ER bus |

Every module copies its values (parameter k + 1 → LYNX k), so lanes on any parameter (a Filter's Slope included) are
one LYNX lane each. A project saved before round 190dw keeps its Parametric EQ / rhythm gain / Reeverb 2 slots, whose
engines are unchanged; re-importing the .flm gives FLM's engines.

## 8. Relation to FLM-exported .flp (issue #88)

FL Studio desktop hosts FLM instruments as the "FL Studio Mobile Rack" generator (`FL Studio Mobile Rack_x64.dll`).
Round 178a found that an FLM→FLP export stores each instrument as that generator, with its state in FLP event 0xD5.
The state uses the same `RACK 10KR` tree (`SSLF/10LF → RACK → RHED RPRM RSMP RMOD{DESC PRMS CSTM}`). So one LYNX rack
decoder serves both paths: opening a `.flm` directly, and replacing the inert Mobile Rack in an exported `.flp`.

## 9. Open points (each is INFERRED today; the plan says which slice closes it)

1. (closed, round 190u) EVN2 bytes 14–19: velocity = byte / 255, pan = byte / 255, fine pitch, repeat count, slide flag.
   Pan, drum pitch, repeat and slide notes are imported since round 190ab. Open: the DW sampler's velocity → level law.
2. CHHD colour and its two flags at 1056; SEND words [1], [3], [4]. (RPRM [2], [3] are Mute and Solo, §3.2; how FLM
   applies them on load is not traced.)
3. ~~The automation point shape codes~~: closed in round 187d. The saved points are the sampled curve (§4).
4. The per-module parameter value → unit maps. Each LYNX engine needs its own mapping (MiniSynth, DirectWave and
   TransistorBass first). Done for Equalizer, Graphic EQ, Filter, Autoduck (legacy), Reverb (legacy) and Reverb 2
   in round 188f (REA_rodada182c.md "F5 effects"). That round also found that parameter 0 is every module's Enable,
   and that SMPR holds the reserved ids 0x7FC0 Post Gain, 0x7FC1 Mix and 0x7FC2 Wet Pan (automation lanes: −3, −2).
   Round 188h added MultiFX, Trance Delay, Distortion, both Limiters, Leveller, Gate and Tape Delay (the Pocket
   engines) and proved how the module wrapper applies Mix (linear dry/wet), Post Gain (40g − 20 dB on the output) and
   Wet Pan (a balance of the wet). Left: Chorus, Spacer, Wow & Flutter, Phaser, Waveshaper, Compressor, Spreader,
   Stereoizer, Flanger, Autoduck (29), Tremolo (Transpose, Scale and Randomizer: round 190bs, §7.2; Note Echo and the
   Arpeggiator: round 190de, §7.8).
   Round 190by: SuperSaw (Pocket SuperSaw), Comb Filter, Multiband Compressor and Tape Stop are LYNX engines; Reverb
   (32) maps to Reeverb 2 and Tuned EQ (30) to the Parametric EQ (REA_rodada190by.md). Round 190dw: Equalizer, Graphic
   EQ, Filter, Autoduck (legacy) and the three reverbs on FLM's own engines (§7.10). Parameter counts read in the
   code: 30 = 11, 32 = 16 (14 and 15 are unnamed wet and dry levels), 42 = 8.
5. HEAD 272/280 (loop range).
6. (round 190bs) The sample slot: the third `MAIN` tail field (f32 0.5) and `LINk`'s second word; where a slot with
   an output link plays; the stretch modes' algorithms (LYNX uses its own stretcher) (round 190dw: where a linked slot plays is settled, §7.9); how a kit sample's legacy `STRC`
   stretch sounds (two samples of one demo song; since round 190de LYNX stretches them with its own stretcher); the module's octave shift
   and the second dispatch switch next to the key range (0x420 / 0x42c in the module object: not found in the file).

# FL Studio 2026 behavioural analysis: round 9 (REA) — state of the sample-based generators

> **Nature of this document.** Continuation of rounds 2–7 (round 8, done in parallel, covers
> Maximus/Soundgoodizer/Fast Dist/Filter/Flangus and is not touched here). *Clean-room* reverse engineering,
> behavioural only. No Image-Line code or pseudocode has been copied: what is here are
> descriptions, data layouts and formulas written in our own words. Addresses (`FUN_xxxxxxxx`), offsets and
> globals serve only to trace the evidence.

- **Targets** (all in `Plugins/Fruity/Generators/<name>/`):

  | Plugin | DLL (SHA-256) | Language | `SaveRestoreState` (VMT/vtable + 3) |
  |---|---|---|---|
  | FPC | `5990885c…6f04` | Delphi (`TTestPlug`, VMT `0x868d48`) | `FUN_0086d4c0` |
  | DirectWave | `3432775d…6570` | C++ MSVC (`CDirectWaveFL`, vtable `0x1800d0418`) | `FUN_180016140` |
  | Fruity Slicer | `7616d1f8…2ed6` | Delphi (`TTestPlug`, VMT `0x766188`) | `FUN_00766c40` |
  | Slicex | `401b5ece…857b` | Delphi (`TTestPlug`, VMT `0x879d40`) | `FUN_0087a8a0` |
  | FL Keys / Drumpad | `56ca1209…822e` / `28a0a642…a361` | — | data only (§5) |

  The VMT follows the SDK order already used in rounds 4 and 7 (3 = `SaveRestoreState`, 4 = `GetName`,
  6 = `ProcessParam`, 9 = `TriggerVoice`). In the Delphi DLLs the VMT was found by the class name
  (`TTestPlug`, the name pointer sits at VMT−0x88); in DirectWave, via the MSVC RTTI
  (`.?AVCDirectWaveFL@@` → *Complete Object Locator* → vtable). The DFM forms were read with the round 7
  `TPF0` parser. **Method:** the REA server was busy with round 8 (single target per session), so
  the decompilations were done with the same Ghidra 12.1.4 in *headless* mode in a separate project
  (decompilation/xref/grep scripts in `tmp/r9/gh/`); addresses in each DLL's default image space.
- **Format validation:** 165 `.flp` files in `FL Studio 2026/Data`, **channel section** (before the 1st `0xEC`):
  `0xC9` (plugin name) → `0xD5` (blob). Actual counts: **FPC 25** (20 files), **DirectWave 57**
  (18 files), **Slicex 43** (21 files), **Fruity Slicer 10** (6 with the name `"Fruity Slicer"` +
  4 with the old name `"Fruity slicer"` — the importer must accept both), FL Keys 11, Drumpad 10
  (+1 `"DrumPad"`).
- **Date:** 2026-10-13

| Mark | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation of the cited function(s) |
| **[STR]** | Inferred from strings, RTTI or DFM forms |
| **[DATA]** | Statistically confirmed in the demo `.flp` files |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

Conventions: everything little-endian; `i32` signed; `P+0xNN` = offset into the plugin object; paths with
`%FLStudioFactoryData%` = installation root and `%FLStudioData%` = `<installation>\Data` (both resolved
against `/mnt/c/Program Files/Image-Line/FL Studio 2026` during validation).

### "Chunk" format shared by FPC and DirectWave **[DECOMP+DATA]**

FPC and DirectWave use the same container (writer `FUN_0083c4a0` in FPC, `FUN_180003f70` in DirectWave;
reader `FUN_0083c750` / `FUN_180003ea0`):

```
chunk = id:u32  size:u64  body[size]       (no alignment; body may contain sub-chunks)
```

The end-of-list id is a chunk of size 0 (`0x6A` for an FPC pad, `0x12E` for a layer, `4` for a DirectWave
zone, `2` for a DirectWave program). A generic reader that just skips unknown chunks reads all 82
blobs with no leftover bytes **[DATA]**.

---

## 1. FPC (25 channels) **[DECOMP+STR+DATA]**

### 1.1 Layout of `0xD5`

| Off | Type | Content |
|---|---|---|
| 0x00 | u32 | **version** `0x1E84E403` (all 25 blobs). The reader rejects > `0x1E84E403`; if ≥ `0x200000` it shifts by 8 bits (→ 2,000,100); chunk sizes are u64 from 2,000,014 onwards |
| 0x04 | chunks | sequence of top-level chunks (table below) |

Top-level chunks (`FUN_0086d4c0`):

| id | Size | Content |
|---|---|---|
| `2` | var. | **one pad** (container, up to 32; always 32 in the demos: pads 0–15 = bank A, 16–31 = bank B) |
| `1` | var. | name of the selected MIDI drum loop (UTF-16, e.g. `World Loops\Polka\fpc_polka_10.mid`); ignore |
| `3` | 1 | UI option (`P+0xC9C`) |
| `4` | 32 | UI state (2 × u32 at `+0xE74/+0xE78`); ignore |
| `5` | 1 | UI option (`+0xB50`), only 3 demos |

Old versions (< 0xF424B) stored a pad counter and one block per pad without chunks
(`FUN_0086c5e0`); they do not appear in the demos.

**Versions in FL 2026's own files (round 190m) [DATA]:** 65 states in `Data` — `0x1E84E403` (2,000,100; 61 states),
`0x1E848E` (2,000,014; 4 `.fst` kits) and `0xF424B` (1,000,011; the five kits in `System/FL Mobile/Instruments`).
All three use the `id:u32 size:u64` container. What changes at 2,000,014 is the **text**: pad names, layer paths and
the loop name are UTF-16LE from there on and one byte per character in 1,000,011 (this corrects the note above about
chunk sizes). Every state has the same chunk order: 32 × `2`, then `1`, `3`, `5`, `4` (each optional); a pad is
`0x64 0x65 0x66`, its layers, `0x67 0x68` twice, `0x6A`; a pad without a sample still has one layer with an empty
path. `src/core/Fpc.{h,cpp}` reads and writes all of them byte for byte (test
`fpc190m_fl_kits_and_projects_round_trip_byte_for_byte`), and `Empty.fst` is `fpc::emptyKit()`. Envelope blob:
`u32 1, u32 3, u32 n`, `n × 24` bytes, 36-byte tail, in all 4,160 envelopes; the pan envelope has **one** point
(0, 0.5, tension 0.2) by default, the volume envelope four.

### 1.2 Pad (chunk `2`, reader/writer `FUN_0086cc50`)

| id | Size | Content |
|---|---|---|
| `0x64` | 64 | **16 × i32 pad parameters** (table 1.3) |
| `0x65` | var. | **pad name**, UTF-16LE without terminator (empty in pads without a sample) |
| `0x66` | 32 | 8 × i32; only the 1st is used: **random trigger mode** (`pad+0x45`, table 1.5) |
| `0x69` | var. | **layer** (container, 0..n; table 1.4) |
| `0x67` | 8 | index of the envelope that follows (0 = volume, 1 = pan) + 0 |
| `0x68` | var. | serialised envelope (§1.6) |
| `0x6A` | 0 | end of pad |

### 1.3 Pad parameters (`0x64`) **[DECOMP+STR+DATA]**

The pad structure lives at `P+0x14C + 0x56·i`. Mapping i32 → field (read in `FUN_0086cc50`; names
from the DFM *handlers* `WheelChange FUN_00857990`, `CutGroupChange FUN_0085f410`,
`OutputSelectorChange FUN_0085e490` and from `TriggerVoice FUN_008698a0`):

| i | Field | Range / unit | Demos (352 pads with a sample) |
|---|---|---|---|
| 0 | **pad volume** (`pad+2`) | 0…128 (`PadVolWheel` Max 127, default 127; 128 appears in empty pads and in 51 used pads). Gain = `L11(v/127) = (11^(v/127) − 1)/10` **[DECOMP]** | 99 ×72, 128 ×51, 92, 64, 127… |
| 1 | **pad pan** (`pad+6`) | −128…+128, 0 = centre | 0 ×257, ±4…±24 |
| 2 | **flags**: bit0 = **mute** (`pad+0`), bit1 = **solo** (`pad+1`), bit2 = `pad+0x26` **[HYP: "scale volume with velocity"]**, bit3 = **random trigger/cycling of overlapping layers** (`pad+0x44`) | — | 0 ×183, 8 ×112, 4 ×57 |
| 3 | **MIDI note that triggers the pad** (`pad+0x22`) | 0…127 (GM map: 36 kick, 38 snare, 42 closed hi-hat, 46 open…) | 36–51 typical |
| 4 | **mixer output offset** (`pad+0x1E`) | −99…103 (`PadOutputWheel`); 0 = same track as the channel | 0 ×130, 1…6 |
| 5 | **"Cut" (group this pad cuts)** (`pad+0x27`) | −1 (none)…99 | −1 ×290, 0 ×58, 1 ×4 |
| 6 | **"Cut by" (group that cuts this pad)** (`pad+0x2B`) | −1…99 | −1 ×291, 0 ×58, 1 ×3 |
| 7 | **pad tuning** (`pad+0xA`) | −128…+128; cents = `v · 9.375` (= 1200/128, ±1 octave) **[DECOMP]** | 0 ×345 |
| 8 | `pad+0x4E` | pad colour in RGB **[HYP]** | 0 or 0x47A145… |
| 9 | `pad+0x52` | **[HYP: icon/style index]** | 0, 4…7, 239 |
| 10–15 | — | reserved (0) | — |

Triggering (`FUN_008698a0` → `FUN_0086b480`) **[DECOMP]**:
- The incoming note triggers **all** pads whose note (`i=3`) is equal; muted pads do not play; if any pad
  is soloed, only the soloed ones play. Validation: the **648 pattern notes** of the FPC channels in the demos all
  land on a pad with a sample and the same note, with no ambiguous note (two pads on the same note) **[DATA]**.
- **Pitch does not follow the note**: FPC adds `(60 − note)·100` cents to the voice pitch, cancelling the
  host's transposition; the pad sounds at the sample's original pitch + tunings (pad + layer).
- **Choke**: when a pad P triggers, every active voice of a pad Q with `cutBy(Q) = cut(P)` and `cutBy(Q) ≠ −1`
  is cut (`P+0x173`/`P+0x177` in `FUN_0086b480`). Typical example: closed hi-hat with cut=0 and open hi-hat
  with cut-by=0 (the 58 "0/0" pairs in the demos).
- Voice gain = `L11(volPad/127)` (in the pad group) × `L11(volLayer/127)` (in the layer voice) **[DECOMP: both
  laws; HYP: simple multiplication]**. Pan = pad pan + layer pan, each in −128…128
  **[HYP: sum and clamp]**.

### 1.4 Layer (chunk `0x69`, `FUN_0086c8f0`)

| id | Size | Content |
|---|---|---|
| `0x12C` | var. | **sample path**, UTF-16LE without terminator (empty = layer without sample) |
| `0x12D` | 28 | 7 × i32 (below) |
| `0x12E` | 0 | end of layer |

| i | Field (offset in the layer) | Range / unit | Demos (1,269 layers) |
|---|---|---|---|
| 0 | **layer volume** (`+0x1C`) | 0…127 (DFM default 100; new layer = 127). Gain `L11(v/127)` | 127 ×1,112, 99 ×121 |
| 1 | **layer pan** (`+0x20`) | −128…128 | 0 ×1,265 |
| 2 | **minimum velocity** (`+0x2C`) | 0…127, inclusive | 0, 16, 32, 48, 64… |
| 3 | **maximum velocity** (`+0x30`) | 0…127, **exclusive** (except 127, which includes 127) | 127 ×464 |
| 4 | flags: bit0 → `+0x34` (always 1; copied to the voice) **[HYP: "layer enabled"]**; bit1 → `+0x3D` **[HYP: reverse, `SampleReverseBtn`]** | — | 1 ×1,269 |
| 5 | **layer tuning** (`+0x38`) | −128…128 → `v·9.375` cents (added to the pad's) | 0 ×1,240; −54…+75 |
| 6 | **sample region** (`+0x44`) | −1 = whole sample; ≥0 = index of the WAV region/marker (start/end taken from the file's region list) | −1 ×1,269 |

Layer selection on a note **[DECOMP]**: without pad bit3, **all** layers with `vmin ≤ vel < vmax`
play together. With bit3, only one of them plays, chosen by the mode of table 1.5 among those covering the
velocity (`FUN_00869db0`, the last one chosen is kept in `pad+0x49`). The velocity comes from the host (0…1 × 128,
clamped to 0…127).

### 1.5 Trigger mode for overlapping layers (`0x66`[0]) **[DECOMP+STR]**

| Value | Menu (`RandomBtnMenu`) | Behaviour |
|---|---|---|
| 0 | Random | uniform draw |
| 1 | Random (avoid previous) | draw among the others (default in new pads; 240 pads) |
| 2 | Cycle backward | the previous one in the list, wrapping |
| 3 | Cycle | the next one in the list, wrapping (the 112 "HQ Kit" pads with 8 layers) |

The "HQ" kits in the demos have 8 layers with **overlapping** ranges (0–32, 16–47, 32–64, 48–80, …, 111–127):
with bit3 on and mode 3 this becomes *round-robin* within the velocity range.

### 1.6 Pad envelopes (`0x67`/`0x68`) **[partial DECOMP + DATA; semantics HYP]**

Two per pad (0 = volume, 1 = pan), objects `TFPCVolEnvelope`/`TFPCPanEnvelope`. Blob:
`u32 1, u32 3, u32 nPoints`, then `nPoints × { f64 Δtime (s) **[HYP]**, f64 value 0…1, f32 tension,
u32 type }` and a trailer of 9 × i32 (sustain/loop points, −1 = none, and 128,128,0,128). Demos: the pan
one is always the default (2 points); the volume one has 2 points (1.0 → 0 in 1.25 s, tension 0.2) in 283 pads and
4 points (0 → 1 in 0.75, → 0.5 in +1.5, → 0 in +0.25; sustain at point 2) in 377. There is only an effect when the
envelope object is "active" (`+0x48 ≠ 0`, tested in `FUN_0086b480`); which blob field turns this on
was not isolated **[HYP: the default is off — the pads sound as a *one-shot* of the whole sample]**.

### 1.7 Statistics and sample existence **[DATA]**

- 25 blobs, all version 0x1E84E403, all with 32 pads; **352 pads with a sample**; layers per pad:
  1 ×185, 8 ×112, 4 ×24, 2 ×15, 3 ×9, 5 ×7.
- **1,269 paths, 1,269 exist** in the installation (all `%FLStudioData%\Patches\Packs\Drums\…`).

---

### 1.8 The editor form (round 190q) **[DATA: form resource of `FPC_x64.dll`]**

The plugin's Delphi form `TSYNTHEDITORFORM` (client 790 × 367) gives every control's position, range, hint and menu
(read with a DFM outline script; nothing of the resource is in the repo). LYNX's editor (`src/ui/FpcEditor.cpp`) uses
these positions, drawn 1.25 × larger.

| Area | Controls (left, top, width × height) |
|---|---|
| Title strip (y 0…33) | `TitleBevel` "FPC" (−1, 1, 128 × 32); `KbBtn` (125, 7, 22 × 22) "Toggle view: single bank, two banks, pad settings"; MIDI loop bar: open (546, 10), previous (578, 10), next (750, 10), options (765, 10) |
| Pad strip (y 33…66) | previous pad (8, 41, 15 × 17), `CurrentPadBtn` "Pad 1/16" (23, 41, 78 × 17), next pad (101, 41), options (116, 41); pad name (202, 43, 90 × 15); volume wheel (301, 37, 24 × 24; 0…127), pan (335, 37; −128…128), tune (375, 37; −128…128); mute (415, 40, 19 × 18), solo (447, 40), "Scale volume with velocity" (479, 40); note label (594, 44, 42 × 13); "Cut which group?" (648, 42, 19 × 14; −1…99), "Cut by which group?" (702, 42), "Pad output offset" (754, 42, 26 × 14; −99…103) |
| Pads | 16 buttons 60 × 56: columns at x 14 / 81 / 148 / 215, rows at y 274 (pads 1–4) / 209 / 144 / 79 (pads 13–16); bank selector (14, 334, 261 × 19) |
| Layer bar (y 73) | CREATE (299, 73, 54 × 19), DELETE (357, 73), layer up / down (417, 73 / 82, 13 × 10), SPREAD EVEN (440, 74, 79 × 18), "Lock layers" (522, 74, 19 × 18), "Test pad range" (547, 74, 213 × 18), "Trigger overlapping layers randomly" (762, 74, 19 × 18) |
| Layer list | scroll box (300, 99, 479 × 118); one row per layer, 465 × 29: select button (3, 3, 21 × 22), volume (37, 2, 24 × 24; 0…127, default 100), pan (72, 2), tune (107, 2), waveform (140, 4, 101 × 20), "Active range" slider (247, 2, 215 × 24); splitter (300, 217, 479 × 9) |
| Sample | waveform (301, 220, 190 × 117) "Left click to preview / Middle click to edit"; load (303, 343), name (322, 341, 147 × 17), "Reverse sample" (473, 342, 17 × 14) |
| Envelope | editor (495, 220, 269 × 137); VOLUME (766, 220, 13 × 67) and PAN (766, 288, 13 × 68) tabs |

Menus: the random button's — Cycle (mode 3), Random (0), Random (avoid previous) (1), Cycle backward (2, hidden);
the note label's — Last hit, Learn, Map notes for entire bank, Show GM note names; the sample's — Load..., Edit, Edit
in new window, Show in browser, Select region. The third pad switch is "Scale volume with velocity" = pad flag bit 2
(§1.3 had it as not decoded; name from the form, its effect on the sound not measured yet).

## 2. DirectWave (57 channels) **[DECOMP+STR+DATA]**

### 2.1 Layout of `0xD5`

`u32 version` (38 ×49, 37 ×8; the reader `FUN_18003dcd0` rejects > 38 and uses another path for < 36) followed
by top-level chunks (writer `FUN_180040610`):

| id | Size | Content |
|---|---|---|
| `6` | 16 | global flags (u64): bits 0–2 options, bit3 = "unregistered", bit4/bit5 = mode (`+0x90`) |
| `0x206` | 7 (5 in 6 blobs) | global UI/quality options; ignore |
| `5` | 628 | ×16: one per **MIDI channel/multitimbral slot** (index + no. of the assigned program, e.g. `0, 0x45` / `1, 0x3C`…) **[HYP]** |
| `1` | var. | **program** (container) ×128 — the whole bank of 128 programs is always stored |

**Only program 0 has zones** in the 57 blobs (the other 127 are `"---"`, 2,819 B each — which is why a
"1 piano" blob is ~380 KB) **[DATA]**. The importer should use the selected program of slot 0 (chunk `5`,
[HYP]) and, in practice, the first program with zones.

### 2.2 Program (chunk `1`, `FUN_180040c50` / reader `FUN_18003f130`)

| id | Size | Content |
|---|---|---|
| `0x64` | 30 | program index (i32) + no. of zones, polyphony, voice mode etc. |
| `0x66` | var. | **program name** (ASCII without terminator; e.g. `Close Grand`) |
| `0x67` | var. | source file name (`Steinway Grand.dwp`), ASCII |
| `0x68`…`0x6B` | 10/18/17/17 | program glide, LFOs and effects (floats 0…1) |
| `0x6C` ×2, `0x6D` ×4 | 20 / 4 | modulators/modulation values |
| `0x6E` ×100 | 13 | modulation matrix (index + source + destination + amount) |
| `3` | var. | **zone** (container), one per zone |
| `2` | 0 | end of program |

Program voice mode: `Poly / Mono / Legato` and overlapping-zone selection `Normal / Cycle / Random /
Avoid prev.` are name tables at `0x180107238…` **[STR]** (the exact field in `0x64` was not isolated).

### 2.3 Zone (chunk `3`, writer `FUN_1800410d0`, reader `FUN_18003f840`)

| id | Size | Content |
|---|---|---|
| `0x1F4` | 25 | **zone header** (below) |
| `0x1F5` | var. | zone name (ASCII, e.g. `Close Grand 27`) |
| `0x1F6` | var. | **sample path**, ASCII (`%FLStudioFactoryData%\Data\Patches\Packs\Instruments\Keyboard\Close Grand\Close Grand 27.wav`) |
| `0x1F7` | 40 | **sample format and points** (below) |
| `0x1F8` | 8 | f32 **Pitch** (`Z+0x2EE`), u8 `Z+0x2BC`, u8 `Z+0x2BD`, u16 `Z+0x2BE` (=100) |
| `0x1F9` | 14 | u8 `Z+0x2C2` (*time-stretch* mode), u8 `Z+0x2C3` (sync by ticks), 3 × f32 of the *stretch* |
| `0x1FA` | 48 | 4 × f32: zone Delay/Chorus/Reverb/Dry sends (0.5/0.5/0.35/1.0 typical) + 32 zeroed B |
| `0x1FB` ×2 | 20 | **filter A and B**: u8 type (0 Off, 1 Lowpass, 2 Highpass, 3 Bandpass, 4 Notch, 5 Allpass **[STR]**), 3 bytes, f32 cutoff, f32 res, f32 shape, f32 |
| `0x1FC` | 2 | u8 `Z+0x2C0` (sync to tempo), u8 `Z+0x2C1` (slice across the keyboard) |
| `0x1FD` | 16 | **amplitude envelope**: 4 × f32 A, D, S, R (`Z+0x2F2…0x2FE`) |
| `0x1FE`…`0x201` | 9 each | 2 modulation envelopes and 2 zone LFOs |
| `0x202` ×2, `0x203` ×2 | 16 / 20 | parameters of the modulation envelopes/LFOs |
| `0x204` ×16 | 8 | zone modulation routes |
| `0x205` | var. | **embedded audio, raw PCM** (optional, §2.5) |
| `0x206` | var. | **embedded audio compressed as FLAC** (optional; does not appear in the demos) |
| `4` | 0 | end of zone |

**Header `0x1F4` (25 B)** **[DECOMP+DATA]**:

| Off | Type | Field | Demos (1,287 zones) |
|---|---|---|---|
| 0 | u8 | **root note** (`Z+0x2B4`) | 36…96 |
| 1 | u8 | **minimum key** (inclusive) | 0…127 |
| 2 | u8 | **maximum key** (inclusive) | |
| 3 | u8 | **minimum velocity** | 0 in all |
| 4 | u8 | **maximum velocity** | 127 in all |
| 5, 6 | u8 | `Z+0x2B9` (zone group/"key group" **[HYP]**), `Z+0x2BA` (output group **[HYP]**) | 0 |
| 7 | u8 | unused (read and discarded; contains garbage 0/64/255) | |
| 8 | u8 | `Z+0x2C4` | 0 |
| 9 | f32 | **Amp Gain** 0…1 (`Z+0x302`) | 1.0 ×1,238; 0.675 ×49 |
| 13 | f32 | **Amp Pan** 0…1 (`Z+0x306`), 0.5 = centre | 0.5 |
| 17 | u32 | `Z+0x396` (0/1) — alternation/rotation index **[HYP]** | 0 ×1,265, 1 ×22 |
| 21 | u32 | 512 (constant: buffer slack frames, §2.5) | 512 |

**Format `0x1F7` (40 B)**: `i64 frames` · `i32 channels` · `i32 Z[2]` (no. of beats/slices used by
"sync"/"slice across the keyboard"; 4…128) · `f32 sample rate` · `i32 loop mode` · `i32 Loop A` ·
`i32 Loop B` · `i32 Sample Offset` · `i32 bits per sample` (16/32). Points in sample frames.

**Loop mode** (`Z[7]`, decision at voice start/end in `FUN_180030090`) **[DECOMP+STR+DATA]**:
0 = *Disabled* (plays once, stops at the end), 1 = *One-Shot* (plays to the end even after note-off),
2 = *Forward* (continuous A→B loop), 3 = *Sustained* (loops while the note is held, then continues to
the end), 4 = *Bounce* (back and forth). Demos: 0 ×907, **2 ×373**, 3 ×7. Zones with a loop: `LoopA > 0`
in 356, `LoopB > LoopA` in 380.

### 2.4 Conversions **[DECOMP]** (parameter text, `FUN_180027740`)

The zone parameters are normalised floats 0…1 (the host sees `value·100`, `FUN_180016070`; host
parameter no. = `128·program + index`, names in `FUN_180029f30`).

| Parameter | Law |
|---|---|
| Pitch | `cents = (v − 0.5)·2400` (±1 octave); 0.5 = no transposition |
| Amp Gain | `dB = 20·log10(v²)` → **linear amplitude = v²** (1.0 = 0 dB; 0.675 → −6.8 dB) |
| Amp Pan | `(v·100 − 50)` → "L/R"; pan −1…+1 = `2v − 1` |
| Amp Env Att / Dec / Rel | `t = 10·v⁴` s (0.5 → 0.625 s; 0.505 → 0.65 s; 0.25 → 0.039 s) |
| Amp Env Sus | `v·100` % |
| Smpl Offset / Loop A / Loop B | in frames (`Z[10]`, `Z[8]`, `Z[9]`; the host sees `frame/frames`) |

Per-note tuning: playback rate = `2^((note − root)/12 + cents/1200) · sample_rate / output_rate`
**[DECOMP: root and rate; HYP: exact form]**. Demos: typical envelope A=0, D=0.5, S=1.0, R=0.25…0.505
(piano: release 0.04…0.65 s); 90 zones with D=1.0/S=0 (decays without sustain).

### 2.5 Embedded audio **[DECOMP+DATA]**

When the option to save the sample in the project is on, each zone gets `0x205` with the channels **in
separate blocks (planar)**, each block with `(frames + 512) × bits/8` bytes (float32 when bits = 32; the
`Z[2]=4` of these zones suggests "float format" **[HYP]**). E.g.: 61,322 frames × 2 channels × 4 B + 2×512×4 =
494,672 B. Alternative `0x206` = the same audio as an embedded temporary FLAC file. In the demos: **12 embedded
zones** (all from one project, "Mehdi Ebrahiminejad – Never Out"), precisely the **12 whose path
points to `D:\00 Projects\…` and does not exist**.

### 2.6 Statistics **[DATA]**

- 57 blobs, 57 programs with zones (always program 0); zones per program: 6–12 (majority), 17–18, 32,
  35, 45 ×3, 88 ×7. Total **1,287 zones**, all with `velocity 0–127` (no velocity layers in the demos).
- **1,287 paths, 1,275 exist**; the remaining 12 have embedded audio.
- The **2,224 pattern notes** of the DirectWave channels all land in exactly one zone (no overlapping
  key ranges) **[DATA]**.

---

## 3. Fruity Slicer (10 channels) **[DECOMP+STR+DATA]**

### 3.1 Layout of `0xD5` (`FUN_00766c40`, sequential "stream" format, no chunks)

| Off | Type | Field |
|---|---|---|
| 0x00 | u32 | **version** (15 ×4, 16 ×6; writes 16) |
| 0x04 | f32 | **original length in beats** (`LengthSelect`; 4, 8, 16) |
| 0x08 | f32 | **original BPM** (`TempoSelect`; 60, 100, 174, 175) |
| 0x0C | i32 | `P+0x128` **[HYP: pitch in cents, `PitchWheel` −1200…1200]** (0) |
| 0x10 | i32 | `P+0x12C` **[HYP: "fit length"/stretching, `FGLengthWheel` −20000…20000]** (0, 83, 167, 766) |
| 0x14 | i32 | **time-stretch method** `P+0x130` (0 Fill gaps, 1 Alt fill gaps, 2 Pro default, 3 Pro transient, 4 Transient, 5 Tonal, 6 Monophonic, 7 Speech **[STR]**; v<15 added 2 if >1) |
| 0x18 | i32 | `P+0x134` **[HYP: fade in 0…100]** |
| 0x1C | i32 | `P+0x138` **[HYP: fade out 0…100]** |
| 0x20 | str8 | **sample path**: u8 length + ANSI bytes (e.g. `%FLStudioFactoryData%\Data\Patches\Packs\Loops\DL Breaker.wav`) |
| … | u32 | **no. of slices** N |
| … | N × slice | `str8 name` (may be empty; `Kick`, `Snare`, `HiHat`…) · `i32 position in samples` · `i32 −1` · `f32 −1.0` · `u8 flags` (bit0 **[HYP: reverse, `InvertBtn`]**) |
| … | u8 | `P+0x141` (boolean UI option) |
| … | i32 | **note of the 1st slice** `P+0x144` (default 60) |
| … | u8 | **"play to end"** `P+0x13E` (1 = the slice plays to the end of the sample; 0 = up to the next slice) **[DECOMP]** |
| … | i32 | sample rate of the sample at save time (44100) **[HYP]** |
| … | u8 ×3 | `P+0x13F`, `P+0x13C`, `P+0x140` (auto-dump / declick / auto-fit **[HYP]**) |
| … | i32 | display mode |
| … | 364 B | v16 only: voice envelope/filter block (`FUN_00757d10`) — ignore |

Versions < 13 stored the slice with only the position (4 B) and, from 6 to 12, +1 flag byte; < 9 stored the 3
header floats as 2.

### 3.2 Triggering (`TriggerVoice` `FUN_00768230`) **[DECOMP+DATA]**

- Note → slice via a table of 125 entries (`P+0x198`); slice *i* sits at note `note1 + i`
  (`note1` = `P+0x144` = 60 in all demos) **[partial DECOMP; DATA: the notes used in the patterns of the
  Slicer channels start at 60 and go up to 60+N−1]**.
- The slice plays **at the original pitch** (the voice receives `(60 − note)·100` cents, cancelling the host).
- End of slice: start of the next one (or the end of the sample if "play to end").

### 3.3 Statistics **[DATA]**

10 blobs; slices per channel: 9, 9, 18, 20, 22, 29, 32, 52 ×3; 9 of the 10 paths exist (the missing one is an
`.ogg` from a user project). Positions in samples at 44.1 kHz (e.g. DL Breaker: 0, 22050, 44100…).

---

## 4. Slicex (43 channels) **[DECOMP+STR+DATA]**

### 4.1 Layout of `0xD5` (`FUN_0087a8a0`, sequential stream)

| Off | Type | Field |
|---|---|---|
| 0x00 | u32 | **version**: `0x00060003` (35 blobs) or `6` (8 old blobs, same layout) |
| 0x04 | 8 × i32 | **master parameters** (index = DFM `Tag`): [0] filter env., [1] cutoff, [2] **master volume** (0…16000, default 12800), [3] randomness (256), [4] LFO, [5] **master pitch in cents** (−1200…1200; demos 0, 455, 687, 760), [6] Mod X (256), [7] Mod Y (256) |
| 0x24 | 47 B | options (`P+0x12F0`): u32 [0] = **layering mode** (0 All, 1 All gain-comp., 2 Velocity, 3 Mod X, 4 Mod Y, 5 Random, 6 Cycle, 7 Song-time **[DECOMP+STR]**), other UI flags/selectors |
| 0x53 | 3 × u32 | UI state (the 3rd = octave shown on the keyboard, `BaseNoteSel`; visual only) |
| 0x5F | **deck A**, then **deck B** | (below) |
| end | 8 × (20 B + 6 objects) | articulator groups (envelopes/LFOs); ignore |

Each **deck** (Slicex has two, A and B; in the demos only A has slices):

| Field | Content |
|---|---|
| u8 | **0 = embedded audio follows; 1 = file reference only** |
| str8 | file path (u8 length + ANSI; may be empty) |
| (if 0) u32 + bytes | **complete WAV** (RIFF/WAVE) of `size` bytes |
| 76 B | deck parameters: 2 × f64 (−1 = automatic), f64 start and f64 end of the region in **beats**, 5 × i32, 6 × u8 |
| k objects | deck envelopes/LFOs (`FUN_00790720`) |
| u32 + N × 68 B | **per-region/slice parameters** (17 × i32; DFM defaults: volume 8000 of 16000, pan 0 of ±6400, pitch 0 of ±1200 cents, cutoff 128, res 128, articulators 12800, start 0, delay 0) **[STR+DATA; exact field order HYP]** |

The embedded audio is the WAV that Slicex/Edison itself generates: `fmt` (16-bit PCM or 32-bit float), `data`,
`smpl` (root note 60), `inst`, `acid` (beats and BPM), `strc`, **`cue ` (one point per slice, position in
samples)**, `LIST/adtl` with **`labl` (slice name)** and `ltxt` (region length). When the deck only
references the file, **the installation's loops (`DL *.wav`) carry exactly the same chunks** — that is,
**Slicex slices come from the WAV, not from the blob**; the blob only stores the count and the parameters of each slice
**[DATA]**.

### 4.2 Key mapping and pitch **[DECOMP+DATA]**

`TriggerVoice` (`FUN_00881d70`): the note indexes a list of (deck, region) per key; if there is more than one
region on the key, the layering mode chooses. The voice sounds at the original pitch + region pitch + master pitch
(`(60 − note)·100` cents cancel the host). In the pattern notes of the 43 channels, the keys used start at
**60** and go up (60…91) — **slice *i* ↔ note 60+i** **[DATA; the table is derived, not saved]**.

### 4.3 Statistics **[DATA]**

- 43 blobs; **14 with an embedded WAV** (the largest at 3.0 MB), **29 with a reference only**; 8 in old version 6.
- Slices per deck A: 6…24 (DL loops: 19–24; legacy: 8–11; "!westand out": 10–12); root always 60.
- References: all 29 files exist. Embedded: 11 of the 14 have a path that **does not exist**
  (`!westand out.wav` ×8, `DL_The Funk.wav`, `DL_Ghost Break.wav`, a path on a user's
  desktop) — the embedded WAV is the only source in those cases; 2 point to files that exist.
- 1 blob (`Video chopping.flp`) has an empty embedded WAV (`data` of 0 bytes) and no path.

---

## 5. FL Keys and Drumpad (cheap) **[DATA+STR]**

**FL Keys** (11): `u32 1` · `str8 set name` (`Grand Piano` ×8, `mda Piano` ×3) · 15 × i32
(−1000…1000; the plugin's 15 parameters: decay, release, hardness, vel→hardness, muffle, vel→muffle,
velocity sensitivity, stereo, tune, detune, stretch, treble, pan/tremolo, LFO rate, overdrive
**[STR, order; HYP: scale `p = (v + 1000)/2000`]**). The name indexes
`Plugins/Fruity/Generators/FL Keys/Wavtables/<name>.wti`, an INI with `[info]` (rate, type `riff` or `raw`,
folder), `[velocitylayers]` (velocity thresholds) and one `[sampleN]` per sample with `root` (base note and
**start** of the range), `high` (end of the range), `pos/end/loop` (for raw `.dat`). `Grand Piano` = 30 WAVs
(`CHANGPIANOHARD_A1.wav`…, roots every 3 semitones, one layer); `mda Piano` = 15 samples in a raw `.dat`
at 22,050 Hz **[HYP: 16-bit mono PCM]**; `Rhodes` = raw `.dat` at 32 kHz, 3 layers. Both sets
used in the demos exist.

**Drumpad** (10+1): blob `"SSLF"` + u32 size + `"3PMD"` + preset name (`War Drum Lo 01`,
`Hi Bongo 01`, `Dance Kick 01`…) + ~60 floats. It is **physical-modelling synthesis, without samples** — outside the
sampler's scope; treat as an unsupported generator (or a generic kick/tom).

---

## Recommendations for LYNX

LYNX has: a 1-sample-per-channel sampler (pitch, ADSR, start/end region, reverse, *cut*, slices with
fractional start and "note 60+i plays slice i", filter), channels routed to mixer tracks and patterns
with notes per channel. Given that:

1. **Path resolution (common).** `%FLStudioFactoryData%` → FL installation root (or a folder
   configured by the user); `%FLStudioData%` → `<root>\Data`; other absolute paths as they are; if the
   file does not exist, search by file name in the same folder as the `.flp`. Strings: FPC in UTF-16,
   DirectWave in ASCII, Slicer/Slicex in ANSI `str8`. Embedded audio takes priority when the file does not
   exist (DirectWave `0x205`, Slicex deck flag 0).

2. **FPC → one LYNX channel per pad with a sample** (352 pads in 25 channels).
   - For each pad with ≥1 layer with a path: create a channel "*<FPC channel name> – <pad name>*" with the
     sample of the **chosen layer** (rule below), same mixer track as the FPC channel + `output offset`
     (i=4; clamp to the track range).
   - Channel level = `L11(volPad/127) · L11(volLayer/127)`; pan = `(panPad + panLayer)/128` clamped to ±1;
     pitch = `(tunePad + tuneLayer)·9.375` cents. **No key tracking**: the sampler must play the sample at the
     original pitch whatever the note (always generate the new channel's notes at 60, or enable a
     "fixed pitch" option on the channel).
   - **Note splitting:** each note in the FPC channel's pattern goes to the channel(s) whose pad has
     `note == i3` (muted pads discarded; if there is a solo, only the soloed ones). In the demos this covers 100 % of the
     648 notes and there are never two pads on the same note.
   - **Velocity layers:** if the pad has 1 layer, use it. With several and bit3 off, create one channel per
     layer and send each note to the channels whose range `[vmin, vmax)` contains the velocity (127 inclusive). With
     bit3 on (8-layer HQ kits), the cheapest and most faithful option is **one channel with the layer covering the average
     velocity** (or, if LYNX gains *round-robin*, alternate between the layers covering the velocity — mode 3 =
     cycle).
   - **Choke:** map `cut`/`cutBy` to the LYNX *cut*: pads with `cutBy = g ≥ 0` join the cut group
     of whoever has `cut = g` (typical case: open hi-hat cut by the closed one). If the LYNX *cut* is a
     "symmetric group", use the same group number for both sides.
   - Envelope: "one-shot" ADSR (A=0, D/S irrelevant, short R) and region = whole sample (`i6 = −1` in
     all demos); reverse if layer bit1.

3. **DirectWave → one channel per zone** (or, if the sampler gains *multi-zone*, a single channel).
   - Program: the first one with zones (always 0 in the demos). For each zone: sample (file or `0x205`
     planar float32/int16 → temporary WAV), **root note** (byte 0), **key range** [b1, b2],
     **velocity** [b3, b4], pitch `(v−0.5)·2400` cents, gain `v²`, pan `2v−1`, ADSR `t = 10·v⁴` s and
     sustain `v`, loop = mode `Z[7]` with A/B in frames (Forward → continuous loop; Sustained → loop until
     note-off; Disabled/One-Shot → no loop), start at `Sample Offset`.
   - **Note splitting:** each note of the DirectWave channel goes to the channel of the zone whose key range
     contains it (at most one in the demos), **keeping the note** — these channels *follow* the keyboard with pitch
     relative to the root: `semitones = note − root`. Since the LYNX sampler treats 60 as the original pitch, add
     `(60 − root)·100` cents to the channel pitch and keep the original note in the pattern.
   - Cost: the pianos in the demos have 6–12 zones per channel (88 in 7 channels); acceptable as grouped hidden
     channels, but **a multi-zone sampler in LYNX** (list of {sample, root, key lo/hi, vel lo/hi,
     gain, pan, pitch, loop}) is the right path and also serves FL Keys.

4. **Fruity Slicer → 1 channel with slices.** Sample = path; slices = `position / no. of frames in the file`
   (start fractions, in stored order); note 60+i = slice i (if `note1 ≠ 60`, transpose the pattern notes
   by `60 − note1`). No key tracking (the slice plays at the original pitch). End = next slice (or the end of the
   sample if "play to end"). Global pitch `P+0x128` in cents **[HYP]**; ignore time-stretch (original BPM/beats
   are only needed if LYNX gains stretching).

5. **Slicex → 1 channel with slices.** Source = embedded WAV (deck A, flag 0) or referenced file; read from the
   WAV the `cue ` (positions → start fractions, sorted), `labl` (names), `smpl` (root 60) and `acid`
   (beats/BPM, optional). Note 60+i = slice i (confirmed by the patterns). Channel volume ← master
   parameter [2] (`12800` = 100 %, **[HYP] linear `v/12800`**), pitch ← master [5] in cents; per-slice
   parameters (volume/pan/pitch) only if the sampler gains per-slice values. Deck B: ignore (empty in the demos).

6. **FL Keys → multi-zone instrument from the `.wti`** (once multi-zone exists; until then, one channel with
   the `.wti` sample closest to C5 and normal key tracking). **Drumpad**: unsupported (synthesis).

7. **Suggested implementation order (return/cost):** (a) FPC per pad (25 channels, 100 % of the samples
   exist, exact notes); (b) Slicex via WAV chunks (43 channels; embedded WAV extraction + reader of
   `cue`/`labl`); (c) DirectWave as per-zone channels and, later, a multi-zone sampler; (d) Fruity Slicer
   (trivial after (b)); (e) FL Keys via `.wti`.

---

## Evidence index

| Function / data | What it does | Mark |
|---|---|---|
| FPC `FUN_0086d4c0` (VMT+3) | `SaveRestoreState`: version, top-level chunks `1…5`, 32 pads | DECOMP |
| FPC `FUN_0086cc50` | Pad: chunks `0x64…0x6A`, mapping of the 16 i32 → fields | DECOMP |
| FPC `FUN_0086c8f0`, `FUN_00870430` | Layer: `0x12C/0x12D/0x12E`, 7 i32, defaults (vol 127, vel 0–127, region −1) | DECOMP |
| FPC `FUN_0083c4a0`, `FUN_0083c750` | Container `id:u32 + size:u64` | DECOMP |
| FPC `FUN_008698a0` (VMT+9), `FUN_0086b480` | Note → pads (mute/solo), velocity → layer, L11 laws, tune ×9.375, cut/cut-by, pitch cancellation | DECOMP |
| FPC `FUN_00869db0` | Modes random / avoid previous / cycle back / cycle | DECOMP |
| FPC `FUN_00857990`, `FUN_0085c3b0/450/4d0`, `FUN_0085f410`, `FUN_0085e490`, `FUN_0086eb80` | DFM handlers → fields; "cents" text (`v/128·1200`) | DECOMP |
| FPC DFM `TSynthEditorForm` @`0x5beb84` | Pad and layer vol/pan/tune ranges, cut/cut-by, output, mode menu | STR |
| DirectWave RTTI `CDirectWaveFL` → vtable `0x1800d0418` | `SaveRestoreState` `FUN_180016140` | DECOMP |
| DirectWave `FUN_180040610`, `FUN_180040750`, `FUN_18003dcd0` | Top level: version 38, chunks `6`, `0x206`, `5`×16, `1`×128 | DECOMP |
| DirectWave `FUN_180040c50`, `FUN_18003f130` | Program: chunks `0x64…0x6E`, zones | DECOMP |
| DirectWave `FUN_1800410d0`, `FUN_18003f840` | Zone: `0x1F4…0x206`, header and format fields, planar audio + 512 | DECOMP |
| DirectWave `FUN_180026e10`, `FUN_180029f30`, `FUN_180027740`, `FUN_180028080/283d0/28ca0` | Host parameter ↔ field; pitch/gain/pan/time laws | DECOMP |
| DirectWave `FUN_180030090` | Voice start: loop mode `Z[7]`, sync, slice across the keyboard | DECOMP |
| DirectWave strings `0x105968…0x105a7c` | Filter types, loop modes, voice modes | STR |
| Fruity Slicer `FUN_00766c40` (VMT+3) | Layout v1…16 | DECOMP |
| Fruity Slicer `FUN_00768230` (VMT+9) | Note→slice table, play to end, fixed pitch | DECOMP |
| Fruity Slicer DFM `TSynthEditorForm` | Tempo, length, pitch, fades, stretch methods | STR |
| Slicex `FUN_0087a8a0` (VMT+3) | Layout v6/0x60003, 2 decks, embedded WAV, 68 B records | DECOMP |
| Slicex `FUN_00881d70` (VMT+9), `FUN_00896c10` | Note → (deck, region), layering modes; base-note selector visual only | DECOMP |
| Slicex DFM | Master parameters by `Tag`, per-region defaults | STR |
| Installation / embedded `DL *.wav` WAVs | `cue`, `labl`, `ltxt`, `smpl`, `acid` = slices | DATA |
| FL Keys `Wavtables/*.wti` | Sample map (root/high/layers) | DATA+STR |
| 165 `.flp` (channel section + `0xE0` notes) | Counts, versions, file existence, notes used | DATA |

### Pending items

- FPC: which bit/field of the envelope blob (`0x68`) turns on the volume/pan envelope; confirm pad bit2
  ("scale volume with velocity") and layer bit1 (reverse); pan law.
- DirectWave: program field with the Poly/Mono/Legato mode and the overlapping-zone choice; meaning of
  `Z+0x2B9/0x2BA` and `Z+0x396`; filter cutoff curve.
- Fruity Slicer: exact names of fields `P+0x128/0x12C/0x134/0x138` and of the 3 final booleans (order of the
  form's published fields).
- Slicex: order of the 17 i32 per region and unit of the master volume; role of deck B.
- FL Keys: scale of the 15 parameters and format of the raw `.dat`.

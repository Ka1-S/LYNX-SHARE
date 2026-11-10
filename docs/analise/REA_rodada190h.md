# REA round 190h (with 188c): Slicex — state, regions, key map, articulators, voice render

Clean-room study of FL Studio 2026's Slicex generator (`Slicex_x64.dll`): behaviour, layouts, formulas and constants in
our own words; no Image-Line code or data. Round 188c made the three sub-studies below (it ended in a PC reboot before
any code); round 190h checked them against each other, built `src/core/Slicex.{h,cpp}` from Part 1 and verified it.

**What LYNX implements so far (190h slice 1, `core/Slicex.h`):** the state codec (Part 1 §1–§6; read and written byte
for byte), the regions of a deck's WAV (§3.1), the key map (§7) and the stored-integer laws (masters, region record).
Checks: 40 states saved by FL 2026 (presets, plugin database, demo projects) and the 22 Slicex channels of the
installation's projects rewrite identically; `fresh()` equals FL's Default preset in its masters, options, region
record and all 8 articulator groups (`tests/Slicex190hTests.inc`).

**Where the sub-studies disagree, and which one holds:**
- Articulator record (Part 1 §6 vs Part 2 §2.1): Part 2 is right. The 16 bytes Part 1 calls a header are
  `i32 version (3), f32 base, f32 envelope depth, f32 LFO depth`, and the four maps are stored in the order Mod X,
  Mod Y, velocity, random (the editor's part selector lists them as velocity, Mod X, Mod Y, random).
- Runtime master fields (Part 3 §9): +0x1088 is the randomness level and +0x108c the master LFO.
- The region volume law's K is ln 3 (Part 3 §1 read the constant; Part 1 had it as a hypothesis).

**Open [HYP] items that need an FL render before the voice engine relies on them:** the sub-block length during an
export (Part 3 "Uncertain" 1), note-off with every articulator envelope off (Part 3 §8), the pan kernel variant
(Part 3 "Uncertain" 2), how FL fills a deck's beat grid for a newly loaded sample (Part 1 §3.2).

---

# Part 1 — state codec, decks, regions and key map


Binary: Slicex_x64.dll (rea/slicex.dll). Everything below is my own description, written from Ghidra output (rea/x1.c,
rea/deck-d1.c, rea/deck-d2.c) and from objdump disassembly. Ghidra was locked by a long flm.dll import for most of
this round, so I read the handlers with objdump directly from the DLL (no lock involved). I also read the Delphi
published field and method tables straight from the file (p/deck_fields.py, p/deck_pubmeth.py, p/deck_ftab.py).

Parser/writer: **p/sxstate.py**. It round-trips 72 of 72 Slicex states byte-for-byte (40 desktop + 32 FL Mobile).
The results table is in §8.

## 0. Addresses read

| addr | role |
|---|---|
| 0087a8a0 | state load (param_3=0) / save (else) |
| 00789250 / 00789390 | string read / write |
| 00790340 | base envelope serializer (articulator parts); 007997a0 = type-6 class slot 2 (base only) |
| 00798310 | type-1 subclass serializer (base + 16 B); ctor 00798230 sets type=1 |
| 00798790 | type-2 subclass (LFO) serializer (base + 16 B + 4 B) |
| 00799a50 / 00790720 | wrap the stream and call the object's virtual slot 2 (+0x10) |
| 008515e0, 00851790, 0086f290, 008441c0, 00844160, 0084d6b0 | deck-param setters (load) |
| 008855b0 | rebuild region records from the sample + **build per-key tables** |
| 0062ab40 | auto base key; 0062a6d0 region end; 00629e20 region display name |
| 00887420 | region-record defaults; 008874a0 record → floats |
| 00898760 | region editor: all 17 controls → record (the authoritative field map) |
| 00894030 | articulator-group selectors → editor target |
| 00881260 | "sample was edited / must embed" test used by the no-embed option |
| 00887b10/00887b40 | ".fldownload.wav" path fix-up |
| 00613570 (0x613f00–0x614440) | Edison WAV writer: cue / adtl / ltxt / tlst |
| 0087d25e–0087d304 | plugin constructor option defaults |
| 008793b0 | OptionsMenuPopup (menu check ↔ byte map) |
| 00897c60, 00898940, 008941d0, 008655b0, 008656c0, 00875800 | UI handlers used to name bytes |
| 0087e4f0 (x1.c) | ProcessParam (group header fields and articulator values) |

Published-field tables (offset → control) live in rea/slicex.dll. TSynthEditorForm: 0x838 RegionSel, 0x840–0x858
group selects, 0x860 OutputSelect, 0x868 FResWheel, 0x870 FCutWheel, 0x878 FOrderSelect, 0x880–0x898 group wheels,
0x8a0 RVol, 0x8a8 RFRes, 0x8b0 ROfs, 0x8b8 RPan, 0x8c0 RFCut, 0x8c8 RPitch, 0x8d0 EnvSelect, 0x8d8 EnvTargetSelect,
0x8e0 ArtGroupSelect, 0x920 ModSmoothBtn, 0x930 CutGroupSelect, 0x938 DispModeSelect, 0x948 DualDeckBtn,
0x960 FEnvWheel, 0x968 AutoDumpBtn, 0x978 LModeSelect, 0x980 LSameDeckBtn, 0x988 LCrossfadeSelect,
0x990 FTypeSelect, 0x9a0 RDelayWheel, 0x9c0 KbBtn, 0x9c8 BaseNoteSel, 0x7f0 NoAutoScrollBtn, 0x810 LoopBtn.
TSynthResModule: 0x100 HQEnvMenu, 0x120 StartWholeLengthMenu, 0x130 VelToVolMenu, 0x170 LockAudioMenu,
0x180 LoopHalfMenu, 0x1a0 NoEmbedAudioMenu. Deck editor (TMEnvEditor-like, `deck+0x18` object): 0x720
ViewScrollerAboveMenu, 0x728 ViewRegionsMenu, 0x730 ViewLoopMenu, 0xa10 ViewSpecMenu, 0xc60 ViewDualMenu, 0xf48
VisModeBtn, 0xf50 ViewPointsBtn, 0xf58 FreezeBtn, 0xf60 ViewNoiseBtn.

## 1. Top-level layout (little-endian)

```
u32   version            saver always writes 0x00060003
i32   masters[8]         plugin+0x140 (32 B)
u8    options[47]        plugin+0x12f0..0x131e (46 B when ver<2)
i32   ui[3]              current deck, current region (deck<<16|idx), BaseNoteSel value
deck  decks[2]           §3
group groups[8]          §6
(no trailer)
```

**Version normalisation.** Let v = raw. If 0xFFFF < v < 0x60004, then v = v >> 16, so 0x60003 becomes 6. The plugin
database entry (states/db_Slicex.bin) stores a plain 6, which gives the same value. Branches:
- v < 2: the options block is 46 bytes (no 0x131e); there is no per-deck flag byte (flag = 0, embedded); there is no path.
- v == 2: there is a flag byte; the path is present only if flag = 1.
- v > 2 (all real files): there is a flag byte and the path is always present.
- v ≥ 0x60004 is kept raw and behaves like v > 2.

The loader then rebuilds runtime state in this order: masters → FUN_0087efe0; groups → FUN_008877f0 and FUN_00887610
(tables for parts 3 and 4); FUN_008855b0 (regions and key tables); every parameter is re-sent to the host; then the
UI is refreshed. Runtime flag plugin+0x132d = **Lock audio data** (menu, not saved). When it is set, the loader skips
the WAV bytes, the deck params, the deck envelopes and the region records. It still consumes them: envelopes are
read into a temporary object and records are skipped as count×68.

### 1.1 Masters (i32 ×8, plugin+0x140)
| # | ctrl | range | default |
|---|---|---|---|
|0| (param 0, host msg 0x54f) | – | 0 |
|1| (param 1 is stored at +0x144, a separate path) | – | 0 |
|2| MainVolSlider | 0..16000 | 12800 |
|3| MainRandSlider | 0..256 | 256 |
|4| MainLFOSlider | −128..128 | 128 in states (DFM default 0) |
|5| MainPitchSlider | −1200..1200 cents | 0 (demo sx0: 760, sx12: 455, sx16: 687) |
|6,7| ModX / ModY wheels | 0..512 | 256 |

The laws are in FUN_0087efe0 (x1.c): vol is v/12800 through exp((·)·K)−1)·0.1, and the pitch, LFO and mod laws are
another agent's work.

## 2. Options block (47 bytes, plugin+0x12f0). Defaults come from the constructor at 0087d25e; the rest are zero.

| blk ofs | plugin | type | meaning / control | default | evidence |
|---|---|---|---|---|---|
|0x00|12f0|i32|**Drum layering mode**, LModeSelect: 0 All, 1 All (gain-compensated), 2 Velocity-mapped, 3 ModX-mapped, 4 ModY-mapped, 5 Random, 6 Cycle, 7 Song-time-synced|0|load → UI 0x978; TriggerVoice reads it|
|0x04|12f4|i32|**Crossfade**, LCrossfadeSelect: 0 none, 1 linear, 2 balanced curve, 3 unbalanced curve, 4 adding|3|UI 0x988; TriggerVoice|
|0x08|12f8|i32|unused (written by the ctor only)|5|no other plugin reference [HYP legacy]|
|0x0c|12fc|i32|>0 enables forwarding of MIDI note on/off to the host (dispatcher msg 0x554), together with 0x1313|3|0087a6e7 [HYP name]|
|0x10|1300|i32|**Display mode**, DispModeSelect: 0 full, 1 no audio editor, 2 no region settings, 3 compact|0|UI 0x938. When ≠0 the per-deck "freeze" byte is kept at deck+0x62c instead of being applied|
|0x14|1304|i32|current **articulator group** 0..7 (ArtGroupSelect)|0|FUN_00887310|
|0x18|1308|i32|current **articulator part** 0..5 (EnvSelect: envelope, LFO, velocity, modX, modY, random)|0|UI 0x8d0|
|0x1c|130c|i32|current **articulator target** 0..5 (EnvTargetSelect: pan, volume, cutoff, resonance, speed, sample start)|1 (DFM Value=1)|UI 0x8d8; ProcessParam|
|0x20|1310|u8|unused|0||
|0x21|1311|u8|deck **LoopBtn** (loop playback in the deck)|0/1|UI 0x810|
|0x22|1312|u8|**NoAutoScrollBtn**|1|UI 0x7f0; ctor|
|0x23|1313|u8|host-set flag (dispatcher sets it to value≠0); on load it is echoed to the host (callback code 0x43); gates note forwarding|0|0087a771 [HYP name]|
|0x24|1314|u8|unused|0||
|0x25|1315|u8|**ModSmoothBtn** ("Smooth up modulation")|1|UI 0x920|
|0x26|1316|u8|**AutoDumpBtn** ("Dump deck A to piano roll after editing")|1|UI 0x968|
|0x27|1317|u8|**DualDeckBtn** (show dual deck)|1|UI 0x948|
|0x28|1318|u8|menu **Sample start covers whole length** (otherwise 100 ms)|0|OptionsMenuPopup; 0x8947a2 hint uses range 12800|
|0x29|1319|u8|menu **HQ envelopes** (picks the table at 0x909c50 in FUN_008854c0)|0|popup|
|0x2a|131a|u8|**LSameDeckBtn**: "Layer regions of same deck only"|0|FUN_008855b0|
|0x2b|131b|u8|menu **Link velocity to volume**|1|popup; TriggerVoice skips the velocity query when 0|
|0x2c|131c|u8|**KbBtn** (keyboard panel visible)|1 in states|UI 0x9c0; KbBtnClick 008941d0|
|0x2d|131d|u8|menu **Loop half of regions**|0|popup|
|0x2e|131e|u8|menu **Leave unedited audio data on disk** (no-embed). Absent when v<2|0|popup; save branch|

Not saved: 0x132d **Lock audio data** (menu), 0x1324 (an HQ-related runtime value), 0x132b (loading guard),
0x1090 (FUN_008854c0 argument, from elsewhere).

## 3. Deck record (×2; deck d lives at plugin+0x160+d·0x630)

```
u8   ref_only        (v>=2)  0 = audio embedded, 1 = load from path
str  path            (if ref_only or v>2): u8 n, if n==255 then u32 n; n bytes (no terminator)
if ref_only==0:
   i32  wav_len;  u8 wav[wav_len]     (wav_len may be 0)
u8   params[76]      §3.2
env  deck_envs[N]    N is NOT stored: it is the runtime length of the deck editor list (deck_engine+0x1520).
                     It is 4 in every FL build/state seen; each is a type-6 envelope (§5), 2 points
i32  nregions
u8   regions[nregions][68]  §4
```

**Save rule for ref_only.** ref_only = 1 only if option 0x131e is on and FUN_00881260(d) is false. In practice that
means the deck's sample is unchanged since loading and is backed by a file. When FUN_00881260 is true the audio is
embedded anyway. The menu item is disabled when FUN_00881260(−1) is true. The path is the sample's filename (sample
+0xa0) after the host shortens it to a %FLStudioData%-style path (host callback 0x1c).

**Load.** If the path ends with ".fldownload.wav", that suffix is replaced by ".wav" and the deck is forced to
ref_only. When ref_only = 1 the host expands the path (callback 0xe) and the file is loaded; if it cannot be loaded
the deck is cleared (FUN_00872da0). When ref_only = 0, the embedded WAV is parsed from memory (FUN_00619a30) and the
sample's filename is set to the path. Deck B in all demos is empty: ref_only = 1 with an empty path, or an embedded
216/188-byte WAV with 0 frames.

The embedded WAV is FL's own writer output. A u32 size placeholder is written first and back-patched, so wav_len
equals the RIFF size + 8. Chunks: [JUNK] fmt [fact] data smpl inst acid strc cue LIST/adtl [LIST/tlst] LIST/INFO
(ISFT "Slicex"). It is a normal RIFF/WAVE and is parsed by the same routine as files on disk.

### 3.1 Edison region list (sample+0x100, entries of 0x38 bytes)
| ofs | type | meaning |
|---|---|---|
|0x00|i64|start (frames)|
|0x08|i64|end (frames, inclusive). 0x7FFF…FFFF = marker, whose effective end is computed by FUN_0062a6d0: the start of the next region with a larger start, minus 1, or the sample length minus 1 for the last one|
|0x10|str|name (labl)|
|0x18|str|note text (note chunk); also listed in the region combo's second column|
|0x28|i32|**MIDI key** (−1 = none / auto)|
|0x2c|u8|region type → ltxt purpose ID from table 0x8e1574: 'loop','rgn ','beat','dwnb','trak'; type 2 ('beat') markers get no ltxt|
|0x30|i32|cue ID (≥0 when saved)|
|0x34|i32|index of the Slicex region record linked to this region (set on load; FUN_008855b0 uses it to keep records when regions are re-ordered)|

**WAV ↔ region mapping (writer 0x613f00–0x614440; the reader is symmetric at 0x616c00+):**
- `cue ` has one point per region whose cue ID ≥ 0: {id, position = start − ofs, 'data', 0, 0, sampleOffset = start}.
- LIST `adtl`: `labl` (id, name) when the name is non-empty; `note` (id, text) when +0x18 is non-empty; `ltxt`
  (id, length, purpose) for every region except type-2 markers. length = min(end, last) − start + 1 for real regions
  and 0 for markers.
- LIST **`tlst`** (written only if some region has key ≥ 0): i32 count, then 24-byte entries
  {'cue ', cueID−1, 1, u8 0xFF, u8 **key**, u8 0x90, u8 0, i32 0, i32 0}. This is where the **per-region key** lives:
  it is an Acid-style trigger list holding a note-on of `key`.
- None of the demo WAVs has `ltxt` or `tlst`. Every demo region is a marker with key −1, so every demo map is automatic.
- `strc` and `acid` are present (FL writes them too). They describe the beat/tempo, not the regions.
- Auto-slicing (SliceUpBtn, BeatDetect, ClawMachine and so on) adds markers to the same list (key −1). The algorithms
  are outside this sub-study.
- Default display name (FUN_00629e20): the name if set. Otherwise, if editor flag +0x188 is set, just "n". If not,
  "Marker #n" for markers or "Region #n" for regions (n = index+1). In the combo, leading '-' characters are removed.

### 3.2 Deck params (76 bytes). Source: the deck editor object E = deck+0x18
| ofs | type | E field | meaning | demo value |
|---|---|---|---|---|
|0x00|f64|+0x1500|selection start (frames). FUN_008515e0 sorts start/end; equal values or an empty sample → −1/−1 (no selection)|−1|
|0x08|f64|+0x1508|selection end|−1|
|0x10|f64|sample tempo obj +0x550|beat-grid start (beats)|0|
|0x18|f64|+0x550 + +0x560|beat-grid end = start + length in beats; FUN_00851790 → FUN_0066cc10(start, end). −1 means take +0x520/+0x548 from the sample. Tempo shown = beats·60 / duration|4 or 8 (sx16: 14.68)|
|0x20|f32|+0x1204|noise threshold drawn when ViewNoiseBtn (+0x1219) is on|0.02|
|0x24|i32|+0x1544|View ▸ Spectrum (1 = spectrogram; VisBtn / ViewSpecMenu)|0|
|0x28|i32|+0x1530|spectrum band count (N…bands menu)|512|
|0x2c|i32|+0x1538, clamped 0..3 on load|scope channel mode (ScopeChan…Menu)|1|
|0x30|i32|+0x121c|view parameter; FUN_0084d6b0 stores it and rebuilds the view (redraw 0x200, mode 4) [HYP: scope/zoom]|−1|
|0x34|u8|+0x1215|ViewPointsBtn state (FUN_008441c0 → button 0xf50)|1|
|0x35|u8|+0x1216|View ▸ Regions|1|
|0x36|u8|+0x1217|View ▸ Loop|0|
|0x37|u8|+0x1218|View ▸ Scroller above|1|
|0x38|u8|+0x1106 (or deck+0x62c when display mode ≠0)|FreezeBtn (FUN_00844160 → button 0xf58)|0|
|0x39|u8|+0x1554|View ▸ Dual (stereo dual display)|0|
|0x3a|18 B|–|zero padding (memset before save)|0|

These are editor/view settings and are not audio-relevant, except the beat grid (tempo/stretch) and the selection.

## 4. Region record (68 bytes = 17 i32). The in-memory record is 0x70 bytes: these 68 bytes plus 11 floats at +0x44..+0x6c

Authoritative map: FUN_00898760, which copies every region control's value into the current record. Defaults come
from FUN_00887420. Floats come from FUN_008874a0. Constants were read from the DLL (p/deck_mem.py): 0x90c9d8 =
1/6400, 0x90c858 = 1/12800, 0x90b200 = 1/256, 0x90c590 = 0.5.

|#|ofs|control (tag)|range|default|float (offset) and law|
|---|---|---|---|---|---|
|0|0x00|CutGroupSelect|0..99|0|– (0 = no cut group)|
|1|0x04|OutputSelect "Output track offset"|−99..99|0|–|
|2|0x08|RPanWheel (−2)|−6400..6400|0|+0x44 pan = v/6400|
|3|0x0c|RVolWheel (−3)|0..16000|8000|+0x48 vol: f = v·0.000125; if f == 1 then 1, else (exp(f·K) − 1)·0.5 with K = ln 3 (K is a runtime-initialised global at 0x91b53c; ln 3 is the only value continuous with the f == 1 shortcut) [HYP K]. v = 16000 gives 4.0 (+12 dB)|
|4|0x10|RAmpGroupSelect − 1 (−10)|−1..7 (UI 0..8, 0 = none)|0 (= group 1)|–|
|5|0x14|RAmpGroupWheel|0..12800|12800|+0x4c = v/12800|
|6|0x18|RFCutWheel (−4)|0..256|128|+0x50 = v/256|
|7|0x1c|RFResWheel (−5)|0..256|128|+0x54 = v/256|
|8|0x20|RFGroupSelect − 1 (−11)|−1..7|0|–|
|9|0x24|RFGroupWheel|0..12800|12800|+0x58 = v/12800|
|10|0x28|RPitchWheel (−6)|−1200..1200 cents|0|+0x5c = (float)v cents|
|11|0x2c|RScratchGroupSelect − 1 (−12) "Speed articulator group"|−1..7|0|–|
|12|0x30|RScratchGroupWheel|0..12800|12800|+0x60 = v/12800·8|
|13|0x34|RDelayWheel (−7)|0..12800|0 (not reset by FUN_00887420)|+0x64 = v/12800|
|14|0x38|ROfsWheel (−8) "Sample start"|0..12800|0|+0x68 = v/12800. It covers 100 ms, or the whole region length when option 0x1318 is on|
|15|0x3c|ROfsGroupSelect − 1 (−13)|−1..7|0|–|
|16|0x40|ROfsGroupWheel|0..12800|12800|+0x6c = v/12800·2|

Hint texts (OutputSelectChange 00894570) agree with this map: pan ±(v/64)%, volume law as above, a 256-step display
for cut/res, and ms or "whole length" for start (range 12800 when 0x1318 is on). One other branch prints v·0.015 [HYP: delay]. Changing a group selector also re-targets the
articulator editor (FUN_00894030): amp group → target volume (1) or pan; filter group → cutoff/res (2/3); speed group → 4;
start group → 5. EnvSelect is then switched to that group.

Records follow the sample's region order. On load, record i is linked to sample region i (+0x34 = i). FUN_008855b0
later rebuilds the deck's record array so it is parallel to the sample regions. A region whose link is out of range
gets a default record. If the sample has no regions, a single default record is created, which is why empty decks
store nregions = 1.

## 5. Envelope object stream (shared by deck envs and articulators; FUN_00790340)

Write: `i32 type, i32 version(=3), i32 npoints, npoints × {f64 time, f64 value, 8 bytes (point+0xc..0x13: f32 tension + flag bytes)}, 20 bytes (obj+0x44..0x57)`.
Subclass extras follow: **type 1** (envelope) adds 16 bytes (obj+0x98, 4 i32, which ProcessParam edits as articulator
values 0..3); **type 2** (LFO) adds 16 bytes (obj+0xd0) and 4 bytes (obj+0xe0, only when version ≥ 3). Types 3/6
add nothing. Size = 32 + 24·n (+16 for type 1, +20 for type 2).

Read compatibility: version > 3 aborts. Version < 2 stores points as {time f64 if type == 6 else f32, value f32}.
Version < 1 has 4 fewer flag bytes. A type-tag mismatch on a typed object stops the read [HYP edge case].

## 6. Articulator groups (×8; plugin+0xdc0+g·0x50)

```
i32 hdr[5]           FEnvWheel (env amount 0..256, def 128), FCutWheel (128), FResWheel (128),
                     FTypeSelect (0 LP,1 BP,2 BS,3 HP), FOrderSelect (0 off,1 single,2 dual,3 triple)
articulator art[6]   one per target: pan, volume, cutoff, resonance, speed, sample start
   u8  hdr[16]       always 03 00 00 00 | 00 00 00 00 | 1.0f | 1.0f in every state [HYP: version 3 + 3 params]
   env parts[6]      type 1 (envelope), type 2 (LFO), then 4 × type 3 (velocity, modX, modY, random maps)
```
After loading, FUN_00887610 pre-computes a 513-point table for parts 3 and 4 (modX/modY maps) of each articulator.
FUN_008877f0 refreshes the curve caches. The content of these objects is left to the articulator agent; the count and
skip rules above parse every state exactly. Group headers seen: (128,128,128,0,0) ×310, (128,128,100,0,1) ×9,
(128,34,0,3,1) ×1.

## 7. Key map (FUN_008855b0, plus TriggerVoice FUN_00881d70)

Each deck d holds 128 lists at deck+0x28+key·8. Every entry is the i32 `srcDeck<<16 | regionIndex`.

1. Clear all 128 lists of deck d.
2. For each source deck s in {0,1}, skipped when option **0x131a (same deck only)** is on and s ≠ d: for each region i of s
   (in sample order) with 0 ≤ key ≤ 127, append (s<<16|i) to list[key]. Explicit keys from both decks therefore
   **layer** on the same note unless "same deck only" is set.
3. Auto keys: base = FUN_0062ab40(n) with n = number of regions of deck d. base = 60 if n ≤ 65; otherwise
   base = max(0, 60 − 12·floor((n − 54)/12)). For n = 66..77 the base is 48, for 78..89 it is 36, and so on. For each
   region i of deck d with key == −1, k = base + i: if 0 ≤ k ≤ 127 and list[k] is **empty**, then list[k] = [(d<<16|i)].
   An explicit key therefore wins over an auto key, and auto-keyed regions of deck B never appear in deck A's table.
4. At note-on (TriggerVoice), the table used is decks[(voice param 2 from the host) & 1], which is the note's
   colour/channel parity [HYP: param 2 = note colour]. The key is the note number (0..127, otherwise no voice). With
   one entry, that region plays. With more than one entry, layering mode (0x12f0) and crossfade (0x12f4) choose or mix
   the regions; that is studied by the voice agent.
5. The Options-menu macros rewrite the sample regions' key field and then call FUN_008855b0. The handlers (not
   decompiled in detail) are MapBothDecksMenuClick 00878b90 (serial across the keyboard), Layerbothdecks1Click 008782f0
   (A on white keys, B on black), MapEachDeckMenuClick 00878d10 (parallel), LinkSelMenuClick 00878600,
   LinkHomoMenuClick 00878350 (same name → same note) and MapToKeyMenuClick 00878d70 (current region → clicked key).

Also: UI int 0 = current deck (UI+0xb88, FUN_00897c60). UI int 1 = current region encoded as deck<<16|index
(UI+0xb8c, FUN_00898940). UI int 2 = BaseNoteSel position (3 in every state; the keyboard-view octave) [HYP meaning].

## 8. Validation: p/sxstate.py round-trip (all byte-identical)

| file | result | deck A regions (src) | auto keys |
|---|---|---|---|
| pre_Default/Minimal/Sustaining, sx21, SlicexTrack, db_Slicex (v=6) | OK | empty (1 default record) | – |
| sx0 (Ghost Break) | OK | 16 (embedded, labl Kick/HiHat/Snare) | 60..75 |
| sx1 | OK | 10 (embedded) | 60..69 |
| sx10 (The Funk) | OK | 8 (embedded, labelled) | 60..67 |
| sx11 (Stadium) | OK | 19 (ref → FL Data file) | 60..78 |
| sx12, sx15, sx17–sx20 (!westand out) | OK | 12 (embedded) | 60..71 |
| sx13, sx14 | OK | 10 (embedded) | 60..69 |
| sx16 (VOC Push It) | OK | 6 (embedded) | 60..65 |
| sx2, sx3, sx4, sx7, sx8 | OK | 20 (ref) | 60..79 |
| sx5 (Buried) | OK | 24 (ref) | 60..83 |
| sx6 | OK | 21 (ref) | 60..80 |
| sx9, rt0 | OK | 22 | 60..81 |
| sx22/26/30 | OK | 8 (ref) | 60..67 |
| sx23/27/31, sx24/28/32 | OK | 10 (ref) | 60..69 |
| sx25/29/33 | OK | 11 (ref) | 60..70 |
| mob/*.bin (32 FL Mobile presets) | OK (mobile codec) | 11..22 | see below |
| db_DirectWave.bin | skipped (DirectWave state, version 0x25) | | |

In every desktop state the record count equals the WAV cue count. Every region record has default values and no WAV
carries `tlst`, so all demo key maps are the automatic 60 + i. Region-parameter fields are therefore validated by
code only, not by data. A state with edited region knobs is still needed.

**FL Mobile Slicex states (version 15, not the 0xD5 desktop format):** u32 15, f32 beats (4/8), f32 bpm (99.0001/60/128),
5 × i32 (0,0,4,0,4), str path, i32 n, n × {str name, i32 start frame, i32 key (60+i or −1), f32 −1.0, u8 flag},
then a 17-byte tail {u8 1, i32 60 (root), u8 0, i32 44100, u8 1,1,1, i32 0} [HYP field names]. The desktop loader
has no branch for version 15. A version of 15 would be misparsed as v > 2, so these presets are FL Mobile only.

## Uncertain [HYP]
- K = ln 3 in the region volume law (global 0x91b53c is initialised at runtime). Check it by dumping the global or by an FL render.
- 0x12f8 (=5) is unused. The meanings of 0x12fc and 0x1313 (note forwarding / host flag) are not certain.
- Deck env count N = 4 is a runtime list length; the type-6 deck envelopes are probably the editor's volume/edit map.
- Articulator 16-byte header = version 3 + (0, 1, 1).
- Deck param +0x30 (E+0x121c, −1) is a view setting; +0x2c is the scope channel mode.
- The deck used at note-on is selected by host voice param 2 & 1 (assumed to be the note colour).
- Edison's WAV reader is assumed symmetric to the writer (tlst key byte = entry offset 13; ltxt length gives a region end).

---

# Part 2 — articulators (envelopes, LFOs, maps) and articulator groups


Sub-study `art` of round slicex188c. Clean-room description (behaviour, layouts, constants); no decompiled code copied.
Binary `slicex.dll` (sha256 401b5ece…), Ghidra project ~/ghproj/r69 `slicex`.

## 0. Addresses read

| Address | What it is |
|---|---|
| `FUN_0087a8a0` | state reader/writer (groups part at the end) |
| `FUN_0087cfc0` (plugin constructor, `art13.c` lines ~255–330) | creates the 8×6 articulators, sets their modes |
| `FUN_008867d0` | constructor of the Slicex articulator classes (4 maps + base) |
| VMT `0x879960` = `TGroupArticulator`, VMT `0x879ae0` = `TScratchGroupArticulator`, base VMT `0x7900e0` = `TArticulator` | class names from the RTTI strings next to the VMTs |
| `FUN_007999a0` / `FUN_008869a0` | articulator Load (base / Slicex subclass) |
| `FUN_00799c70` | articulator start (voice trigger) |
| `FUN_00886a20` (group) / `FUN_00886b40` (scratch) / `FUN_00799d70` (base, unused by Slicex) | articulator per-tick step (VMT slot 0x38) |
| `FUN_00799e80` (VMT slot 0x20) | set time scales |
| `FUN_00799ef0`, `FUN_00886bf0` (slot 0) | SetMode |
| `FUN_00790340` | generic curve record load/save (`TCustomMEnvelope`) |
| TMEnvelope VMT `0x78fc00`: create `798230`, load `798300`, prepare `798430`+`7983d0`, start `798490`, step `796540`, scale `798200` | envelope |
| TMLFO VMT `0x78fda0`: create `7985a0`, load `798790`, prepare `798840`, start `798c90`, step `798d00`, position `798c30` | LFO |
| TMMap VMT `0x78ff30`: create `798e20`, load `7997a0`, prepare+table `7996b0`/`7993d0`, isDefault `7992e0`, neutral `799170` | maps |
| `FUN_00795930` point value, `FUN_007971e0` map stepping, `FUN_00796380` release, `FUN_00796130` env start state, `FUN_007935e0` prepare | curve library (`DSP_MEnvelope`) |
| `FUN_00619c70` / `FUN_00619e30` / `FUN_0061a090` | LFO waveform build / value / start phase |
| `FUN_00881d70` TriggerVoice, `FUN_008843d0` voice render, `FUN_00883970` voice release, `FUN_00883b20` voice kill | runtime |
| `FUN_008853e0`, `FUN_00885300`, `FUN_008854c0` | tick length and time scales |
| `FUN_0087efe0`, `FUN_0087e4f0` (group params), `FUN_00887610` (Mod X/Y tables), `FUN_00887310` (UI) | parameter laws |
| Constants: `90c3a8`=1/128, `90b200`=1/256, `90c858`=1/12800, `90c9d8`=1/6400, `90c950`=0.01, `90b4b8`=0.125, `90c590`=0.5, `90c728`=0.1, `90cac8`=2⁻²⁴; `0x909c50` = {48, 4} | |

Decompiles: `art1.c`, `art2.c` (range 0x78f000–0x79b000, the curve library), `art5.c` (0x87f000–0x889000), `art10.c`,
`art12.c`, `art13.c` (constructor), `art14.c` (LFO wave). VMT dumps `art7-vmt.txt`, `art9-dump.txt`; constants
`art8-mem.txt`. Note: the Ghidra listing of this project has no disassembly (Refs/Ins/Off scripts return nothing), so all
findings come from the decompiler.

---

## 1. Articulator groups (plugin `+0xdc0 + g·0x50`, g = 0…7)

### 1.1 In memory

| Offset | Type | Meaning |
|---|---|---|
| +0x00 | i32 | **[0] FEnvWheel**: filter cutoff envelope / LFO amount, 0…256, default 128 |
| +0x04 | i32 | **[1] FCutWheel**: filter cutoff offset, 0…256, default 128 |
| +0x08 | i32 | **[2] FResWheel**: filter resonance offset, 0…256, default 128 |
| +0x0C | i32 | **[3] filter type** (low byte copied to voice+0x7f8; value 2 changes the stage-gain law, §1.3), default 0 |
| +0x10 | i32 | **[4] filter mode/order**: 0 = filter off; n ≥ 1 = n stages (voice+0x7fc = n − 1), default 0 |
| +0x14 + k·8 | ptr ×6 | the 6 articulators (k = 0…5, §1.4). The pointers are not 8-aligned. |
| +0x44 | i32 | parameter-id base set by the constructor = `g·43 + 8` (not saved) |
| +0x48 | f32 | derived cutoff offset = ([1] − 128)·(1/256) (−0.5…+0.5) |
| +0x4C | f32 | derived resonance offset = ([2] − 128)·(1/256) |

[HYP naming] Which of [3]/[4] is the "FTypeSelect" and which the "FOrderSelect" widget: the UI writes [3] into the control
at form+0x990 and [4] into form+0x878; the semantics above (0 = off for [4]) come from TriggerVoice. Demo values:
sx11 = [128, 34, 0, 3, 1]; sx12–sx20 = [128, 128, 100, 0, 1].

### 1.2 Conversions (`FUN_0087efe0`, run after every load via the parameter refresh, and `ProcessParam` for group params with index < 3)

1. For **every** articulator of every group: `lfoDepth (art+0x28) = masterLFO` (plugin+0x108c). The LFO depth stored in
   the state is therefore overwritten.
2. For the **cutoff** articulator (k = 2) of each group: `envDepth (art+0x24) = [0]·(1/128)` and
   `lfoDepth (art+0x28) = envDepth·masterLFO`.
3. `+0x48 = ([1] − 128)/256`, `+0x4C = ([2] − 128)/256` (float).

Master laws (masters are the 8 i32 at plugin+0x140; [3] random, [4] LFO):
`law(x) = 1 if x == 1 else (exp(x·ln 11) − 1)·0.1` in float (round 69), applied to |x| with the sign of x restored:
- `masterRand (+0x1088) = sign·law(|[3]/256|)` (default 256 → 1.0)
- `masterLFO  (+0x108c) = sign·law(|[4]/128|)` (default 128 → 1.0)

### 1.3 Filter stage set-up at trigger (TriggerVoice)

With region filter group `fg ≥ 0` and `n = group[fg].[4]`, if `n − 1 ≥ 0`: voice+0x7f8 = type byte; n stage records
(0x78 bytes each, first 0x40 cleared) at voice+0x81c; `voice+0x804 = (type == 2 ? 1/n : n) · C` with C = the runtime
double at `0x91b510` [HYP: not readable statically]; `voice+0x800 = 1/n`; `voice+0x808 = host+0x20` (as float).
Coefficients: `FUN_00883cd0(cutoff, resonance)` per tick (§4.6) — not decoded here.

### 1.4 Which articulator is which (constructor + voice render, DECOMP)

| k | Target | Class | Mode (art+8) | Envelope offset | LFO offset | Maps' offset/neutral | Region selector / amount |
|---|---|---|---|---|---|---|---|
| 0 | **Pan** | TGroupArticulator | 1 (additive) | 0.5 | 0.5 | 0.5 / 0.5 | amp group (region i32[4]), amp amount ([5]) |
| 1 | **Volume** | TGroupArticulator | 0 (multiplicative) | 0 | 0.5 | 0 / 1.0 | amp group, amp amount |
| 2 | **Filter cutoff** | TGroupArticulator | 1 | 0.5 | 0.5 | 0.5 / 0.5 | filter group ([8]), filter amount ([9]) |
| 3 | **Filter resonance** | TGroupArticulator | 1 | 0.5 | 0.5 | 0.5 / 0.5 | filter group, filter amount |
| 4 | **Speed ("scratch")** | TScratchGroupArticulator | 0 | **0.5** (forced) | 0.5 | 0 / 1.0 | speed group ([11]), speed amount ([12]) |
| 5 | **Sample start** | TGroupArticulator | 1 | 0.5 | 0.5 | 0.5 / 0.5 | start group ([15]), start amount ([16]) |

Mode = 0 for k ∈ {1, 4} (constructor bit mask 0x12), 1 otherwise. SetMode sets art+8 and the offset `mode·0.5` of the
envelope and the 4 maps; the LFO object's offset is 0.5 from its own constructor for all k; for k = 4 the constructor
then forces envelope and LFO offsets to 0.5. "Offset" = value subtracted from every point level when the curve is read.
For k < 5 the constructor loads default envelope/LFO shapes from preset files (`FUN_00790770`), overwritten by the
state.

Region fields used (region record = 17 i32, voice copy at voice+0x18): [4] amp group, [8] filter group, [11] speed
group, [15] start group (< 0 = no articulator; the group number is used directly as index 0…7 [HYP: UI shows it +1]);
amounts: amp `[5]/12800`, filter `[9]/12800`, speed `[12]·8/12800`, start `[16]·2/12800`; plus pan `[2]/6400`, vol
`law3([3]/8000)`, cut `[6]/256`, res `[7]/256`, pitch `[10]` cents, delay `[13]/12800` beats, start `[14]/12800`.

---

## 2. State layout (validated, §5)

After the two decks: `repeat 8 × { 5 × i32 group ints ; repeat 6 × articulator }`.

### 2.1 Articulator record (`FUN_008869a0` → `FUN_007999a0`)

```
i32  version            (3; read and ignored)
f32  base               → art+0x20 (all demos 0.0)
f32  envelope depth     → art+0x24 (all demos 1.0; overwritten for k=2, §1.2)
f32  LFO depth          → art+0x28 (all demos 1.0; always overwritten by masterLFO, §1.2)
curve  envelope (kind 1)   + 4 × i32 knobs A, D, S, R   (TMEnvelope)
curve  LFO      (kind 2)   + 4 × i32 speed, tension, skew, pulse width + 1 × i32 phase offset (version ≥ 3)  (TMLFO)
curve  map Mod X    (kind 3)   → art+0x30
curve  map Mod Y    (kind 3)   → art+0x38
curve  map velocity (kind 3)   → art+0x40
curve  map random   (kind 3)   → art+0x48
```
Mode (art+8) is **not saved** (fixed per k, §1.4). Writer: version written as 3.

### 2.2 Curve record (`FUN_00790340`, generic `TCustomMEnvelope`)

```
i32 kind   (1 env, 2 LFO, 3 map, 6 deck wave envelope; the reader keeps the object's own kind)
i32 version (3)
i32 n
n × point (version ≥ 2: 24 bytes): f64 dt, f64 y (stored to f32; NaN → previous point's y),
                                    f32 tension, u8 mode, u8 b1, u8 b2, u8 b3
i32 flags  (bit 0 TEMPO, bit 1 GLOBAL (LFO phase), bit 2 LFO unipolar shape, bit 3 LFO frozen)
i32 enabled (0 = off, any other = on; FL writes −1)
i32 decay point, i32 loop start, i32 sustain point / loop end (−1 = none)
```
Older versions: v < 2 points are f32 dt (f64 when kind 6) + f32 y (+ f32 tension, + 4 bytes for v ≥ 1). In memory a point
is 0x24 bytes: f64 dt, f32 y, f32 tension, u8 mode, u8 b1 (cleared unless class flag 0x10 — cleared for Slicex
envelopes/LFOs), u8 b2, u8 sign (recomputed at prepare: 0, ±1, 2 = straight), f32×3 prepared coefficients, f32 weight.
Loader post-processing by class capability byte (obj+0x20): bit 3 clear → decay point = −1; bit 2 clear → decay, loop,
sustain = −1. Slicex: envelope byte = 0x2D (keeps decay/loop/sustain and reads the A/D/S/R knobs), LFO = 0x27 (decay
dropped; loop/sustain kept), map = 0x20 (decay/loop/sustain dropped, x axis normalised to length 1).

**Correction to REA_rodada69 §5**: the deck objects (`FUN_00790720`, 4 per deck) are the same 24-byte-point curve record
(kind 6, v3) + the 20-byte header — not "16-byte points + 36-byte trailer". Default deck curve: 2 points
(0, 0.5) → (2/44100, 0.5), header (0, −1, −1, −1, −1).

### 2.3 Envelope knobs (`FUN_007983d0`)

`A, D, S, R` i32 (default 128, 128, 0, 128) → floats `A/128, D/128, S/128, R/128` (obj+0xa8…0xb4); `obj+0xb8 = S/128` if
a sustain point exists, else 0. Demos: all default.

### 2.4 LFO knobs (`FUN_00798840`)

speed (i32, obj+0xd0), tension (+0xd4), skew (+0xd8), pulse width (+0xdc), phase offset (+0xe0, raw u32 added to the start
phase). Constructor defaults speed 40898, others 0; demo states hold 31102 (k = 0…4) and 40000 (k = 5).

---

## 3. Time base

1. Tick resolution `v = 48` normally, `4` when option byte plugin+0x1324 or +0x1319 is set (table `0x909c50`)
   [HYP: those are the HQ / render flags].
2. `x = v·SR/44100`; **samples per tick** `spt = max(4, 4·round(x/4))` (round = round-half-even) → plugin+0x1050.
3. `tick seconds d = spt/SR`. Time scales passed to every envelope/LFO (`FUN_00885300`):
   `free = 2·d`, `tempo = d·BPM/60` (BPM = plugin+0x10a0). An object uses `tempo` when its flags bit 0 is set, else `free`.
4. A segment of duration `dt` (× knob factor) advances its phase by `scale/dt` per tick, so it lasts `dt/scale` ticks:
   **free mode: dt units = 0.5 s (dt = 1 → 0.5 s); tempo mode: dt in beats (quarter notes).** (Same ×2 as Love Philter,
   whose LYNX model is bit-exact.)
5. Example 44.1 kHz: spt = 48 (1.088 ms); 48 kHz: x = 52.24 → spt = 52.
6. Mod X/Y smoothing coefficient `plugin+0x1040 = exp(ln(0.01)·w/96)` with `w = plugin+0x1054 = spt·44100/(SR·96)`.

---

## 4. Runtime

### 4.1 Curve evaluation (the shared `DSP_MEnvelope` library)

The step (`FUN_00796540`), point value (`FUN_00795930`), shapes (modes 0–12), prepare (`FUN_007935e0`), map stepping
(`FUN_007971e0`), LFO waveform (`FUN_00619c70/9e30/a090`) and LFO speed law (`FUN_007989c0`) are **the same algorithms as
LYNX's `src/core/LovePhilter.cpp` (`Curve`, `EnvSt`, `LfoWave`)**: same constants (ln 1000001, 0.6 step count, 21 and
1e18 for LFO tension, 2³²/625 = 6871947.6736, cap 805306368, ln 10001), same mode formulas. Re-use them. Differences /
additions in Slicex:

1. **Point level** = `y + w_i·s − offset`, where `s` = the state's sustain shift (state+0x30) and `w_i` the point's
   prepared weight; when `s ≠ 0` and the object's clip byte (+0x21, default 1) is set, `y + w_i·s` is clamped to
   [obj+0x70, obj+0x78] = [0, 1] before subtracting the offset. `s` = 0 in the attack (stage < 2: start value) and
   becomes `S/128` (state+0x48) when the decay point is passed (stage → 3) — i.e. the S knob raises the sustain part.
2. **Knob factors on durations**: segment duration = `dt_i · F`, F = A/128 while stage < 3 (before the decay point),
   D/128 for stages 3…5, R/128 in release (stage ≥ 6) when a sustain point exists (else D/128). `dt·F ≤ 0` → instant
   (rate 1, phase += 1). (LYNX's Love Philter model takes F = 1.)
3. **Start** (`FUN_00796130`): disabled → stage 9, output 0. Enabled → idx 0, stage 1 if a decay point exists else 3,
   `rate = scale/dt_0` and phase 0, or (dt_0 = 0) rate 1 / phase 1 (the first step jumps to point 1), start = output =
   level(0). Knob floats copied into the state; `state+0x48 = S/128` (if sustain point) ; `state+0x50` (loop-hold flag)
   = stored flag OR `D/128 ≤ 0`.
4. **Stepping** (per call with `adv` ticks): `phase += adv·rate`; while phase ≥ 1: carry the remainder, start =
   level(idx); if `idx < endpt` (endpt = sustain point, or last point when only a loop exists) or no endpt or released:
   pass the decay point (stage ≥ 3, s = S/128), `idx += 1` or (last point) stage 9 and stop; else at the sustain point:
   no loop → hold (stop), loop → `idx = loop + 1`, stage 4, stop only if the loop-hold flag is set. delta =
   level(idx) − start; new rate from `dt_idx·F`. Output `out = start + shape(idx, phase)·delta` (out = start when
   delta = 0). Envelope output at state+0x38, LFO amplitude at LFO state+0x38.
5. **Release** (`FUN_00796380`, called for every env and LFO of the voice at note-off): if the state stage < 6 and a
   sustain point exists: `idx = sustain point`, the sustain shift becomes `s += out − level(sus, s)` (computed with the
   clip disabled, so the release starts from the current output), phase = 1 (next step starts the following segment
   immediately), stage 6. Without a sustain point: only stage = 6 (the envelope continues; durations then use D/128).
   (An "arp break" path with point byte b1 exists but b1 is always 0 for Slicex curves.)
6. **LFO** (`FUN_00798d00`): the LFO's own curve is its **amplitude envelope** (stepped as above, offset 0.5 → a flat
   1.0 point gives amplitude 0.5). Before the step, if `idx == 0`, stage < 9 and phase < 1 (still in the first,
   delay segment): `adv' = max(0, adv + (phase − 1)/rate)`. After the step, if the first segment is over (idx ≠ 0,
   stage ≥ 9, or phase ≥ 1): `phase32 += round(adv'·inc)` (u32 wrap) and **LFO out = amplitude · wave(phase32)**
   (state+0x40, read by the articulator as state+0xc0).
   - `inc = round((float)(exp((float)(speed·1.52587890625e-05)·ln 10001) − 1) · 6871947.6736 · scale)`, capped at
     805306368; 0 when flags bit 3 (frozen). Period = `625/(10001^(speed/65536) − 1)` units (0.5 s or beats).
     Examples: 31102 → 7.9996 units; 40000 → 2.2700; 40898 → 2.0000.
   - LFO start (`FUN_00798c90`): knob floats (1, 1, 0, 1); flags bit 1 (GLOBAL) clear → `phase32 = startPhase +
     phaseOffset` (startPhase = `p5 + (p7 − p5)/2 + 1`); set → the object's shared running phase (obj+0xa8)
     [HYP: advanced by song-position sync `FUN_008878e0`/`FUN_00798c30`; no demo enables an LFO].
   - Waveform build `FUN_00619c70(k_tension, skew/256, pw/128, unipolar = flags bit 2)` and value `FUN_00619e30`: as
     LYNX `LfoWave` **except two conditions**: `p7 = round((1 − pw)·2³² − 1)` only when `pw > 0`, else p7 = 0xFFFFFFFF;
     `p6 = round(dd + pw·2³²)` (0 → 1) only when `pw < 0`, else `p6 = p5` (LYNX tests `pw ≤ 1/128` / `pw < 1/128`).
     Tension: `a = (exp((|t|/128)·ln 21) − 1)·0.05`, `k = sign(t)·(exp(a·ln 1e18) − 1)` (float steps as in LYNX).
7. **Maps** (TMMap): x axis normalised to [0, 1]; value(x) = level − offset of the curve iterated from 0 by x
   (`FUN_00797dc0` = start iterator + one step of length x). Default test (`FUN_007992e0`): all points share one y and
   that y equals the neutral `1 − offset` (1.0 for mode 0, 0.5 for mode 1) → the map is ignored.
   Mod X/Y tables (`FUN_00887610`, built at load for maps 0/1 when not default): 513 floats, entry j = value(j/512).

### 4.2 Voice trigger (`FUN_00881d70`, per articulator k = 0…5)

Inputs: `vel` = host note velocity (0…1), `masterRand`.
1. Articulator pointers: k = 0, 1 from amp group; 2, 3 from filter group; 4 from speed group; 5 from start group (§1.4).
   Missing group → state stages 9, output = factor of step 2.
2. **Factor** `F_k` (voice+0x7a4 + 8k) = 1.0 for k = 1, else 0.0. For k = 4, if its envelope or LFO is enabled:
   `F_4 = 1.0` and the scratch flag (voice+0xf8) is set.
3. Velocity map not default → `F_k = map_vel(vel)` (replaces).
4. Random map not default → `r = map_rand(Random())`; map offset 0.5 → `F_k += r·masterRand`; else `F_k *= (r − 1)·masterRand + 1`.
5. k = 5 only: Mod X / Mod Y tables applied to `F_5` with index `iX`, `iY` (§4.3) by the mode rule
   (mode 0: `×t`; mode 1: `+t`; other: `×2t`). **The sample-start offset uses `F_5` only** (start = region start +
   start amount·F_5; the start envelope/LFO play no role in the offset).
6. Start (`FUN_00799c70`): env start, LFO start; `out = base + env·envDepth` (if env on; no mode test); LFO on: mode 1 →
   `out += lfo·lfoDepth`, else `out *= lfo·lfoDepth·2 + 1`. Then, if env or LFO is on, combine with `F_k` by the mode
   rule (mode 0: `out *= F_k`; mode 1: `out += F_k`); if both are off, `out = F_k` (base ignored).
7. Scratch flag kept only if `F_4 · speedAmount ≠ 0`.
8. Copies `(float)out` to voice+0x78c + 4k (display / trigger values).

### 4.3 Per-tick evaluation (`FUN_008843d0`, voice render)

The render splits the block at tick boundaries (counter voice+0x90). The trigger sets the counter to `−spt`: the
**first** render call evaluates with `adv = 0` (the values at t = 0) and reloads the counter with `spt`; each later tick
boundary evaluates with `adv = 1` (one tick). During the voice's start delay no ticks are counted.

At each tick:
1. If the voice is released (stage > 4) its stage is first set to 9.
2. Mod X/Y source: `X = master[6]`, `Y = master[7]` (raw 0…512, default 256) or, with option plugin+0x1315 (default on),
   the smoothed value `X ← master + (X − master)·c` (c = plugin+0x1040, §3). Indices
   `iX = clamp(round(X + note.modX·256), 0, 512)`, `iY` likewise (note+0x20 / +0x24) [HYP: note X/Y in −1…1].
3. For each k with an articulator:
   - `v = F_k`; Mod X table → `v ∘= tX[iX]`, Mod Y table → `v ∘= tY[iY]` (∘ by the mode rule of §4.2.5);
   - **Group class** step: env on → step env (adv), mode 1: `v += env·envDepth`, mode 0: `v *= env` (depth unused);
     LFO on → step LFO, mode 1: `v += lfo·lfoDepth`, mode 0: `v *= lfo·lfoDepth·2 + 1`.
   - **Scratch class** (k = 4): `d = (env on ? env : 0) + (LFO on ? lfo·lfoDepth : 0)`; `v *= d` (env read with
     offset 0.5, so env ∈ −0.5…0.5).
   - The **base value (art+0x20) is never used per tick** (only at trigger).
   - If released: voice stage = min(voice stage, env stage, LFO stage) → the voice ends (stage 9) once every
     articulator's envelope and LFO have finished (disabled ones count as finished).
4. Targets (A_k = v of articulator k this tick):
   - **Pan** = clamp(regionPan + notePan + 2·A_0·ampAmount, −1, 1).
   - **Volume factor** = `(A_1 − 1)·ampAmount + 1`, times regionVol · note gain (note+0x18) · master volume; pan/vol go
     through `FUN_006226e0` and are ramped linearly over the tick (`FUN_00637c60`, `spt` samples; the first tick jumps).
   - **Speed**: if the scratch flag is set: `speed = (A_4·speedAmount + 1)·baseSpeed` (speedAmount = [12]·8/12800);
     a reversal fade (`voice+0xac`) is applied when the play position jumps by less than a window [HYP detail].
   - **Filter** (if n ≥ 1): `cutoff = regionCut + (group.cutOfs + A_2)·filterAmount`,
     `resonance = regionRes + (group.resOfs + A_3)·filterAmount` → `FUN_00883cd0` (per tick, coefficient law not decoded
     here), then the n stages run per sample.
   - A_5 (start articulator) is stepped but not used after the trigger.

Effective defaults: envDepth = 1 (cutoff: FEnv/128), lfoDepth = masterLFO (cutoff: FEnv/128·masterLFO), base = 0.

---

## 5. Validation on the demo states (`/mnt/e/lynx-work/slicex188c/p/art_parse.py`)

`python3 -I art_parse.py pre_Default.bin <states…>` parses every state completely (masters, options, 2 decks with 4
kind-6 curves each, regions, 8 groups × 6 articulators with the record of §2.1) and asserts kinds/versions; **all 41
Slicex states are consumed to the last byte** (`rest 0`): sx0…sx33, rt0, pre_Default/Minimal/Sustaining, SlicexTrack,
db_Slicex (version 6). The 32 `states/mob/*.bin` files are FL Mobile instrument presets (path string + slice table),
not 0xD5 Slicex states — not parsed. Output: `art-demos.txt`.

Default articulators (every group, all demos unless listed): base 0, depths 1/1, envelopes disabled, LFOs disabled
(flags 3 = tempo + global, speed 31102; k = 5: 40000), all maps default, knobs default. Envelope defaults per k:
pan (0, 0.5); vol (0, 1) → (1.0, 0, t 0.175) sus 1; cut same as vol; res (0, 0.5); speed (0, 0.5) → (1.2, 0, t 0.2)
sus 1; start = the library default (0,0)→(0.75,1)→(1.5,0.5)→(0.25,0), decay 1, sus 2.

**Demos with non-default articulators** (only group 0 is ever changed; no LFO, map or knob change anywhere):

| State | Change |
|---|---|
| pre_Sustaining | volume env **on**: (0, 1) → (6.0, 1), sustain 0 |
| sx0 | volume env **on**: (0, 1) → (0.624, 0, t 0.175), sustain 1 (≈ 0.31 s decay to silence while held) |
| sx11 | group 0 ints [128, 34, 0, 3, 1] (filter on, 1 stage, type 3, cutoff offset −0.367, res offset −0.5); volume env **on**: (0, 1) → (1.52, 0, t 0.21), no sustain (one-shot 0.76 s) |
| sx12–sx20 (9 states) | group 0 ints [128, 128, 100, 0, 1] (filter on, 1 stage, type 0, res offset −0.109); cutoff env **on**, TEMPO: (0, 1) → (0.5 beat, 0, t −0.131), sustain 1; resonance env **on**, TEMPO: (0, 0.5) → (1 beat, 0.625, t −0.55, mode 6 "wave") → (0, 0.5, mode 2), loop 0, sustain 2 (1-beat loop); speed env present but **off** |
| sx2–sx9 | volume env edited ((0,1) → (1.6, 0)) but **off** — no effect |
| all others | defaults |

Masters in those demos: master[4] (LFO) = 760 (sx0), 455 (sx12–14, 19, 20), 687 (sx16) — irrelevant while no LFO is on.

---

## 6. Uncertain / not done

- [HYP] Names of group ints [3]/[4] (type vs. order widget); the stage-gain constant at 0x91b510; the filter coefficient
  law `FUN_00883cd0` (not decoded).
- [HYP] Region group selector stored 0-based (UI shows +1); "off" representation (< 0) not seen in demos.
- [HYP] Global-LFO phase advance outside the voice (only song-position sync seen); note Mod X/Y input scale.
- [HYP] Option bytes +0x1324/+0x1319 = HQ/render (tick 4 instead of 48).
- The per-step float/double rounding order is the library's (as in LYNX LovePhilter); the A/D/S/R knob factors and the
  sustain shift are additions not present in the LYNX model (all demos use default knobs, so they are inactive there).
- Speed reversal fade detail and the sample-start offset law (`×min(SR·0.1, slice length)` or `clamp ≤ 1 × slice
  length` by option +0x1318) belong to the sample-playback sub-study.

---

# Part 3 — voice render path and master / global DSP


Binary: Slicex_x64.dll (rea/slicex.dll, sha256 401b5ece…), Ghidra project ~/ghproj/r69 `slicex`. Everything below is my
own description of the behaviour (prose / pseudo-code), written from Ghidra decompiles and objdump disassembly.
Raw outputs: rea/voice-*.c, rea/voice-*.txt. Validation script: p/voice_check.py (uses the deck agent's p/sxstate.py).

Note on the Ghidra project: functions in r69 have 1-byte bodies (no code analysis), so Refs/Off/Ins give nothing for code.
I used `objdump -d slicex.dll` (image base 0x400000, VAs match Ghidra) for instruction-level checks and a raw
RIP-relative scanner (`XRip.java`, added to the scripts dir) to find writers of `.bss` constants.

## 0. Addresses

| VA | role |
|---|---|
| VMT 0x879d40 | plugin VMT: slot 3 SaveRestoreState 0x87a8a0, 5 ProcessEvent 0x87def0, 6 ProcessParam 0x87e4f0, **8 Gen_Render 0x880740**, **9 TriggerVoice 0x881d70**, **10 Voice_Release 0x883970**, **11 Voice_Kill 0x883c20**, 12 Voice_ProcessEvent 0x883cc0 (returns 0), 13 Voice_Render = empty stub 0x40c5a0 |
| 0x8843d0 | **per-voice render** (called by Gen_Render for every voice; not a host slot) |
| 0x883cd0 | per-voice filter coefficient update |
| 0x883b20 | voice "cut" (force end + declick), recursive over layer chain |
| 0x885150 | cut-group kill |
| 0x883a40 | UI animation event "voice ended" |
| 0x8853e0 / 0x8854c0 | sub-block length + modulation smoothing coefficient / set-sample-rate |
| 0x885300 | articulator tick time (per sub-block) |
| 0x879f30 | Dispatcher (FPD_ShowEditor 0 → 0x1325, FPD_ProcessMode 1 → 0x1324, FPD_SetSampleRate 4 → 0x8854c0) |
| 0x87efe0 | master parameter laws (0x1084/0x1088/0x108c/0x1080, group cutoff/res offsets) |
| 0x876710 / 0x876030 / 0x876830 / 0x876880 | voice declick object: setup, table builder, per-voice init, per-chunk apply |
| 0x637db0 | host sample-rate helper object (P+0xcc) setter |
| 0x61f6c0, 0x620360, 0x6203e0, 0x61fa90, 0x61ff80, 0x6200d0, 0x621cb0 | resampler: ctor, set range (one-shot), set range (looped), set format, set speed, set pitch, render |
| 0x6226e0 / 0x637d30 | 4-gain pan matrix / equal-power pan law |
| 0x637c60 | gain ramp (slope-limited) |
| 0x635d10 | stereo mix-add with 4 ramped gains |
| 0x631600 / 0x631e20 | biquad coefficient designer / biquad stereo process |
| 0x6357e0, 0x635f10 | mono-table × constant add into stereo; stereo × mono-table |
| 0x63a840, 0x6345f0 | DSP library constants + kernel pointer tables (set at start-up) |
| TGroupArticulator VMT 0x879960 (slot 0x38 = 0x886a20), TScratchGroupArticulator VMT 0x879ae0 (slot 0x38 = 0x886b40) | per-sub-block articulator output |

## 1. Constants (read with Mem.java / from the DLL; `.bss` ones from their initialiser 0x63a840)

| pointer | value |
|---|---|
| 0x90c950 | 0.01f (key = Round(InitPitch · 0.01f)) |
| 0x90b890 | int global that receives the last key (UI) |
| 0x90c590 | 0.5f |
| 0x90b4b8 | 0.125f |
| 0x90cac8 | 5.9604645e-8f (2^-24) — minimum layer gain for a voice to be created |
| 0x90c9d8 | 1/6400 (f) |
| 0x90c858 | 1/12800 (f) |
| 0x90b200 | 1/256 (f) |
| 0x90c3a8 | 1/128 (f) |
| 0x90c728 | 0.1f |
| 0x90c8a8 → 0x91b53c | (float) ln 3 |
| 0x90b3d0 → 0x91b534 | (float) ln 11 |
| 0x90b588 → 0x91b510 | (double) ln2 / 2 |
| 0x90b228 → 0x91b518 | (double) ln2 / 1200 |
| 0x90ba68 → 0x91b500 | (double) ln 2 (so the declick "X" of Fruity Slicer's doc is ln 2, confirmed) |
| 0x90c370 → 0x91b4f4 | (float) 1/sin(π/4) = 1.4142135f |
| 0x8dbd30 | (double) π/4 |
| 0x91b4fc | 2^-24 as float bits (ramp snap-to-zero threshold) |
| 0x909c40 | int[4] = {1,2,3,4}: FTypeSelect → biquad mode |
| 0x909c50 | int[2] = {48, 4}: sub-block base (normal, HQ) |
| 0x8aff98 | 2^-32 (Delphi Random scale) |
| 0x637e70.. | 1.0, 44100.0, 2π, 0.4999, 0.001, 15.0 (d); 10.0f, 32768.0f |

Host sample-rate object (P+0xcc), set by 0x637db0(sr): `+0x08 int sr`, `+0x18 = 1/sr`, `+0x20 = 44100/sr`,
`+0x28 = 2π/sr`, `+0x30 = (float)(0.4999·sr)`, `+0x34 = (float)(0.001·(44100/sr))`, `+0x38 = +0x34 · 10f` (float),
`+0x3c = +0x34 · 32768f`. All doubles except where marked float.

Rounding: `FUN_0040c3e0` = `cvtsd2si` (round half to even under the default MXCSR); `FUN_0040c4d0` = truncation.
`FUN_0040a030` = Delphi Random (32-bit LCG output × 2^-32, double in [0,1)).

## 2. Voice record (0x97c bytes) — fields used by the render path

The region record (0x70 bytes, see deck-notes §4) is copied to voice+0x18, so **voice offset = region offset + 0x18**.

| voice | type | meaning |
|---|---|---|
| +0x00 | ptr | host voice handle (param 3 of TriggerVoice) |
| +0x08 | ptr | host TVoiceParams (InitLevels at +0: Pan, Vol, Pitch, FCut, FRes; FinalLevels at +0x14: Pan, Vol, Pitch, FCut, FRes) |
| +0x10 | ptr | deck (P+0x160+d·0x630); `*(int*)deck` = deck number |
| +0x18 | i32 | region f0 cut group (0 = none) |
| +0x1c | i32 | region f1 output track offset |
| +0x28/+0x38/+0x44/+0x54 | i32 | amp / filter / speed / start group (−1 none) |
| +0x5c | f | pan v/6400 |
| +0x60 | f | **gain = regionVolLaw(f3) × layer gain** (float multiply) |
| +0x64 | f | amp articulator amount v/12800 |
| +0x68 / +0x6c | f | cutoff / resonance v/256 |
| +0x70 | f | filter articulator amount v/12800 |
| +0x74 | f | **pitch cents** = region pitch + (float)((60 − key)·100) (float add at trigger) |
| +0x78 | f | speed articulator amount v/12800·8 |
| +0x7c | f | delay v/12800 (beats, see §6.4) |
| +0x80 / +0x84 | f | start v/12800 / start articulator amount v/12800·2 |
| +0x88 | i32 | state: 1 playing, 5 released, 9 ending (declick out running), 10 dead |
| +0x8c | i32 | delay countdown (frames) |
| +0x90 | i32 | sub-block countdown (starts at −L) |
| +0x94 | i32 | 1 until the first render (then 0) |
| +0x98 | ptr | resampler object |
| +0xa0 | d | position at the previous sub-block (init −2147483647.0) |
| +0xa8 / +0xac | f | scratch-fade threshold `(float)(sampleSR·1e-6·L)` and its reciprocal |
| +0xb0 | f | current speed ratio (frames of sample per output frame) |
| +0xb4 | f | base speed `(float)(sampleSR · (1/outSR))`, sign-flipped for reverse notes |
| +0xb8..+0xc4 | f[4] | target gains g0..g3 (§5) |
| +0xc8..+0xd4 | f[4] | ramp target store |
| +0xd8..+0xe4 | f[4] | ramp start (passed to the mixer) |
| +0xe8..+0xf4 | f[4] | ramp increments |
| +0xf8 | u8 | speed (scratch) articulator active |
| +0xfc.. | | declick state (§7): +0xfc attack frame counter, +0x100 release frame counter (−1 = not releasing), +0x104 release start skip, +0x108/+0x10c attack offsets, +0x110/+0x114 release low-pass state, +0x128 table cache index |
| +0x12c + k·8 | ptr | articulator object for target k (0 pan, 1 volume, 2 cutoff, 3 resonance, 4 speed, 5 start) |
| +0x15c + k·0x108 | | articulator voice state (env state at +0, LFO state at +0x80, output double at +0x100) |
| +0x7a4 + k·8 | d | articulator base value (k = 1 → 1.0, others 0.0; then velocity / random / X / Y maps) |
| +0x78c + k·4 | f | articulator output at trigger (UI only; the render recomputes) |
| +0x7d4 / +0x7dc / +0x7e4 | | next layer voice / master voice / master's live-child count |
| +0x7e8 / +0x7ec | i32 | Mod X / Mod Y table index 0..512 |
| +0x7f0 / +0x7f4 | f | per-voice smoothed Mod X / Mod Y (0..512) |
| +0x7f8 | u8 | filter type (group FTypeSelect) |
| +0x7fc | i32 | filter stage count − 1 (group FOrderSelect − 1; −1 = no filter) |
| +0x800 | f | 1/(n+1) |
| +0x804 | f | (float)((n+1, or 1/(n+1) for type 2) · ln2/2) |
| +0x808 | f | (float)(44100/sr) |
| +0x80c + i·0x78 | | biquad stage i: mode int at +0, coefficients b0 +0x50, b1 +0x58, b2 +0x60, a1 +0x68, a2 +0x70 (double); state (8 doubles) at +0x10 |
| +0x974 / +0x978 | i32 | UI animation id / highlighted key |

## 3. TriggerVoice additions to the coordinator's reading (FUN_00881d70)

Host queries (host vt+0x30 = Voice_ProcessEvent on the host side, FL SDK ids): **2 = FPV_GetColor** (note colour
0..15), **3 = FPV_GetVelocity** (float 0..1), **6 = FPV_SetLinkVelocity** sent with 0 when option 0x131b "Link velocity
to volume" is off (the return is ignored). So:

- deck = colour & 1 (even colours deck A, odd deck B); **colour > 13 (colours 14, 15) plays the slice reversed**
  (sign of +0xb4 flipped).
- key = Round(InitLevels.Pitch · 0.01f) (float multiply); keys outside 0..127 or without a region list → no voice.

### 3.1 Layering (n = number of regions on the key, only when n ≥ 2)

Selection value `s` (double): mode 2 velocity; 3 `P+0x158 · (1/512)` (master Mod X int, **not** the note's ModX);
4 the same with Mod Y (+0x15c); 5 Random; 6 `(cycle[key] mod n)/(n−1)` then cycle[key]++ (per deck, at deck+0x428);
7 host query 0x24 (song position, clamped ≥ 0), s = 0. Index `i0 = clamp(Round(s·n − 0.5), 0, n−1)`.

Gain per layer i (initial value 1.0f; float):
- mode 0 "All": 1.0; mode 1 "All gain-compensated": `1/(float)n`.
- modes 2..6, crossfade 0 (none): `(i == i0) ? 1 : 0`.
- modes 2..6, crossfade ≥ 1: `t = (float)(1 − |s·(n−1) − i|)` (double then float); if t > 0:
  1 linear: t; 2 balanced: `(float)((cos((1−t)·π) + 1)·0.5)`; 3 unbalanced: `1 − (float)(exp((1−t)·ln 9) − 1)·0.125f`;
  4 adding: `t ≥ 0.5 → 1, else (float)((cos((1 − 2t)·π) + 1)·0.5)`. (ln 9 computed at run time in double.)
- mode 7 (song time), only when the deck sample has a tempo (sample+0x164 > 0): loop length in beats
  `Lb = frames·bpm/(sr_smp·60)`, `tl = song − Trunc(song/Lb)·Lb`, sample position `p = tl·sr_smp·60/bpm`; with the
  region start `a_i`, its end `e_i` and the previous region start `a_{i−1}` (i−1 clamped at 0):
  `t = 1 − (p − a_i)/(e_i − a_i + 1)` if p ≥ a_i else `t = 1 − (a_i − p)/(a_i − a_{i−1} + 1)` (float); if t > 0 the
  same shapes, but **crossfade 0 gives Round(t)**. If the sample has no tempo the gain variable keeps its previous
  value (1.0 for the first layer).
- A voice is created only if gain > 2^-24. The cut group of the region is applied (§8) **before** the gain test.
- Only regions whose deck has `deck+4 < 1` are layered.

The first created voice is the master (returned to the host); later ones are chained (+0x7d4), point to the master
(+0x7dc) and increment its child count (+0x7e4).

### 3.2 Start position (frames inside the region, double)

`x = (double)start + (double)startAmt · base5` (base5 = voice+0x7cc, the start articulator base after velocity /
random / Mod X / Mod Y maps, before envelope/LFO). If x ≤ 0 → 0. Else, option 0x1318 off: `x · min(outSR·0.1, len+1)`
(100 ms at the **output** rate, capped to the region); on: `min(x,1)·(len+1)`. No rounding. Reverse (+0xb0 < 0):
`x = len − x`. Clamp to [0, len+1]. len = regionEnd − regionStart (inclusive end; markers end at next start − 1).

### 3.3 Resampler set-up

New resampler object (FL's sampler resampler library, structurally identical to what src/core/FlResampler.h
reproduces: position double at +8, speed double at +0x10, sinc widened to round(|speed|) rounded up to a multiple of
4 (≤ 512) with cutoff π/|speed| above speed 1, 0x61ff80). Interpolation settings linked to P+0x120; quality is the
global set by FPD_ProcessMode (0x634ca0: index < 3 → mode 1, == 3 → mode 2, else sinc with that many points ≤ 512).
"fill with zeros past the end" flag (+0x77) = 1. Data = sample frames from regionStart, range [0, len+1).
- option 0x131d off (0x620360): one-shot, no loop.
- option 0x131d **"Loop half of regions"** on (0x6203e0): forward loop with loop start `clamp((len+1)/2, 0, len)`
  (integer division) and loop end len+1.

## 4. Per-voice render (0x8843d0), called by Gen_Render for each voice

Gen_Render (0x880740) grows the work buffer (P+0x105c, stereo float, frames rounded up to 4), then renders the voices
**from the last in the list to the first** (newest first); each voice mixes (+=) into its output. The rest of
Gen_Render is the editor preview player and meters (not part of the instrument sound).

Per call (N = host block frames, out = host stereo buffer):

1. If state ≥ 10 skip to step 9.
2. **Delay**: if +0x8c > 0: +0x8c −= N; if still ≥ 0 return (silent); else the voice starts `N + (+0x8c)` frames
   into the block (out pointer advanced, N = −(+0x8c)). [Observed: the offset is applied to the main buffer only; a
   voice on another output track gets its track buffer from the start of the block.]
3. Per call constants (float): `cents = (voice+0x74 + FinalLevels.Pitch) + (float)masterPitch` (P+0x1080, int);
   `panBase = voice+0x5c + FinalLevels.Pan`; `volBase = (voice+0x60 · FinalLevels.Vol) · P+0x1084`.
4. Loop while frames remain:
   a. **Sub-block start** (when +0x90 ≤ 0): if +0x90 < 0 (first): +0x90 = −(+0x90), advance = 0.0; else +0x90 = L,
      advance = 1.0. If state ≥ 5: state = 9 (provisional).
      - Mod X/Y (j = 0,1, target `T = (float)master[6+j]`): option 0x1315 "Smooth up modulation" on:
        `m = T + (m − T)·P+0x1040` (float); off: `m = T`.
      - `idxX = clamp(Round((double)(m_X + FinalLevels.FCut·256f)), 0, 512)`, `idxY` likewise with m_Y and
        FinalLevels.FRes (float adds; the note's Mod X/Y are FL's FCut/FRes levels).
      - For each articulator k with an object: `out_k = base_k (+0x7a4)`; if the object has an X table (+0x50):
        combine `tableX[idxX]` by the object's mode (+8): 0 → `out·v`, 1 → `out + v`, else `out·v·2` (double);
        same with the Y table (+0x58)[idxY]; then the class method (§4.1) with `advance`; if state > 4:
        `state = min(state, envState_k, lfoState_k)`.
      - If state > 8 and the declick-out has not started (+0x100 < 0): start it now (+0x100 = 0, skip +0x104 = 0).
      - **Pan**: `p = (float)((double)panBase + out_0 · 2.0 · (double)ampAmt)`, clamped to [−1, 1].
      - **Volume**: `v = (float)((double)volBase · ((out_1 − 1.0)·(double)ampAmt + 1.0))`.
      - Gains g0..g3 = PanMatrix(p, v) (§5).
      - If the speed articulator is active (+0xf8): `+0xb0 = (float)((out_4 · (double)speedAmt + 1.0) · (double)+0xb4)`;
        and if not releasing (+0x100 < 0): `d = (float)(+0xa0 − pos)`; if `|d| < +0xa8`: g0..g3 ·= `d·(+0xac)` (signed,
        as in the code — forward motion gives a negative factor); +0xa0 = pos.
      - First render (+0x94 > 0): ramp target store = g (no ramp). Then for each gain: Ramp(inc, store, start, g,
        maxSlope = host+0x38, L) (§5.3).
   b. `n = min(+0x90, remaining)`.
   c. **Resampler pitch** (every chunk): speed = `cents == 0 && +0xb0 == 1 ? 1.0 : exp(cents·ln2/1200)·(double)+0xb0`.
   d. **Render** n frames into the work buffer (stereo; mono samples are duplicated to L=R). Returns r.
      - r == n: if not scratch and pos ≤ 0.0 and state < 9 → state = 9, declick-out starts now (skip 0). (This is how
        reversed slices end.)
      - r < n (forward end of a one-shot region; the remainder is zeros): if not scratch and state < 9 → state 9,
        declick-out starts after r frames (skip = r) — i.e. **the slice ends hard at its last frame**; the filter tail of
        the zeros then runs through the 20 ms declick and the voice dies.
      - A scratching voice (+0xf8) never ends at the region edges (it is clamped there).
   e. **Filter** (if stage count ≥ 1): update coefficients (§6.1) — every chunk, from values that change once per
      sub-block — then run stages 0..n in place.
   f. **Declick** (§7) on the work buffer.
   g. **Mix** n frames into the output (track offset +0x1c: 0 = the host's buffer, else the host-provided track
      buffer via host vt+0x128 lock/unlock): MixAdd(work, out, n, start g0..g3, inc0..3) (§5.2).
   h. start_j += inc_j · (float)n (float); +0x94 = 0; +0x90 −= n.
5. After the block: if state ≥ 9 and < 10 and the declick-out counter (+0x100) ≥ release length → state 10 and the
   master's child count is decremented (child voices).
6. A master voice with state > 9 and no live children asks the host to kill it (host vt+0x28(handle, −1)).

L (sub-block) and P+0x1040 come from 0x8853e0(base) with base = 48, or 4 when option 0x1319 "HQ envelopes" or the
runtime flag P+0x1324 is set: `D = base·sr/44100`, `L = max(4, Round(D/4)·4)`, `P+0x1054 = L·base/(D·96)`
(= L·44100/(96·sr)), `P+0x1040 = (float)exp(ln 0.01 · P+0x1054/96)`. Examples: 44.1 kHz → 48 (HQ 4), 48 kHz → 52,
96 kHz → 104. The articulators tick every sub-block with `dt = P+0x1054·96/44100 = L/sr` seconds (0x885300: they
receive 2·dt and dt·tempo/60).

**P+0x1324 = FPD_ProcessMode flag 0x10 (PM_IsRendering)**, read only when the sample rate is set (FPD_SetSampleRate →
0x8854c0). If FL sends ProcessMode before SetSampleRate in an export, **FL renders use L = 4·sr/44100** [HYP: order
of the two host messages in a render not checked].

### 4.1 Articulator output per sub-block (state at +0x15c + k·0x108, output double at +0x100)

Base value as in step 4a. TGroupArticulator (targets 0,1,2,3,5) 0x886a20: envelope enabled → step it; mode 1:
`out += env·envAmt(+0x24)`, else `out *= env`; LFO enabled → step it; mode 1: `out += lfo·lfoAmt(+0x28)`, else
`out *= lfo·lfoAmt·2 + 1`. TScratchGroupArticulator (target 4, speed) 0x886b40: `out *= (env? env : 0) + (lfo?
lfo·lfoAmt : 0)`. (Envelope/LFO internals, the meaning of env/LFO "states" and of mode +8: articulator sub-study.)
At trigger 0x799c70 seeds a different formula (`+0x20 + env·amt…`) that only feeds +0x78c (display); the first render
sub-block (advance 0) recomputes everything from the base.

LFO depths (0x87efe0): every articulator's lfoAmt (+0x28) = P+0x108c (master LFO); the cutoff articulator (k = 2) gets
envAmt = FEnv/128 and lfoAmt = FEnv/128 · P+0x108c.

## 5. Gains, pan law, mixer

### 5.1 PanMatrix(p, v) (0x6226e0, called with law 0)
- p == 0: g0 = g1 = v, g2 = g3 = 0. v == 0: all 0.
- else `L, R = cos θ · w, sin θ · w` with `θ = (1 + p)·π/4`, `w = v · 1.4142135f` (float multiply) — plain kernel
  0x637d30 (x87 fcos/fsin in extended precision, results stored as float). [HYP: the plain kernel is the active one
  as in Sytrus; the alternative 0x639ff0 uses a float SinCos.]
- p < 0: `a = 1 − R/v`; `g2 = L·a·0.5f`; `g0 = L·(1 − a·0.5f)`; g1 = R; g3 = 0.
  p > 0: `b = 1 − L/v`; `g3 = R·b·0.5f`; `g1 = R·(1 − b·0.5f)`; g0 = L; g2 = 0. (All float.)

### 5.2 MixAdd (0x635d10), gains[0..3] = start, [4..7] = increments
`outL += inL·g0 + inR·g2; outR += inR·g1 + inL·g3` (float, products summed left to right then added to out). Frame k
uses `g + k·inc` built by repeated float additions (frame 0 uses g). When all increments are 0 and g2 = g3 = 0 the
plain form `outL += inL·g0; outR += inR·g1` is used (same values).

### 5.3 Ramp (0x637c60), once per sub-block
`start = store; inc = target − start; if inc ≠ 0: store = target; inc /= (float)L; if |inc| > maxSlope:
inc = copysign(maxSlope, inc); store = start + inc·L; if |store| < 2^-24: store = 0, inc = −start/L`.
maxSlope = host+0x38 = `10f · (float)(0.001·44100/sr)` (0.01 per frame at 44.1 kHz).

## 6. Per-voice filter

Active when the region's filter group ≥ 0 and that group's FOrderSelect ≥ 1 (stages = FOrderSelect). Group header
(P+0xdc0+g·0x50, 5 stored i32): +0 FEnv, +4 FCut, +8 FRes, +0xc FTypeSelect (byte), +0x10 FOrderSelect; runtime
floats +0x48 = (FCut − 128)/256, +0x4c = (FRes − 128)/256 (verified movss in 0x87f25a).

Inputs (double): `c = cut(+0x68) + ((double)grp+0x48 + out_2)·(double)fltAmt`, `q = res(+0x6c) + ((double)grp+0x4c +
out_3)·(double)fltAmt`. K = +0x808 = (float)(44100/sr). E(x, N) = `(double)(float)(exp((double)(float)x·ln N) − 1)`.

| FTypeSelect | biquad mode | frequency f | w | Q / bandwidth | α |
|---|---|---|---|---|---|
| 0 low-pass | 1 | c > 0 ? E(c,1501)·0.0006646666442975402 : 0 | (f + 0.003)·K·2π | Q = q ≤ 0: 0.1; q < 1: E(q,2001)·0.024950001388788223 + 0.1; else 50; if stages > 1: Q = (float)Power((float)Q, +0x800) | sin w · (0.5/Q) |
| 1 band-pass | 2 | min(E(c,1501)·0.00039993334212340415, 1.0001) (0 if c ≤ 0) | (f + 0.0001)·K·2π | BW = q ≤ 0: 0.01; q < 1: E(q,101)·0.11989999562501907 + 0.01; else 12 | sin w · sinh(BW·+0x804·w/sin w) |
| 2 notch | 3 | as type 1 | as type 1 | as type 1 (+0x804 uses 1/(n+1)) | as type 1 |
| 3 high-pass | 4 | min(E(c,1501)·0.00039993334212340415, 0.6001) | (f + 0.0001)·K·2π | as type 0 | sin w · (0.5/Q) |

w is clamped to [0, 2.849517146113191]; sin/cos from the RTL SinCos (x87). ln N computed in double at run time.
Power = exp(y·ln x) in double rounded to float (with the RTL's integer-exponent shortcut). The same coefficients go to
every stage. Coefficients (0x631600, a0inv = 1/(1+α), all double):
- mode 1 LP: a1 = −2cos·a0inv, a2 = (1−α)·a0inv, b1 = (1−cos)·a0inv, b0 = b2 = b1·0.5.
- mode 2 BP: a1, a2 as above, b0 = α·a0inv, b1 = 0, b2 = −α·a0inv.
- mode 3 notch: a1, a2, b1 = a1, b0 = b2 = a0inv.
- mode 4 HP: a1, a2, b1 = −(1+cos)·a0inv, b0 = b2 = −b1·0.5.
Process (0x631e20, per channel, double state): `y = x·b0 + (((x1·b1 − y2·a2) + x2·b2) − y1·a1)`; output stored as
float, y kept in double; then a denormal flush of the 8 state doubles (0x634c50). (0x631d40, the alternative kernel
pointer, raises an error: the SSE2 variant is the one used.)

Cutoff examples, type 0 at 44.1 kHz: c = 0 → 132 Hz, 0.25 → 285 Hz, 0.5 (knob 128) → 1239 Hz, 0.75 → 7172 Hz,
1 → 20.0 kHz (w clamp).

## 6.4 Other per-voice laws
- Region volume (0x8874a0): `x = v·0.000125f`; x == 1 → 1 else `(float)((exp(x·ln3f) − 1)·0.5)` (0 → 0, 8000 → 1,
  16000 → 4).
- Delay frames (trigger): `Round((float)(((delay · (float)sr) · 60f) / tempo))` — all float; tempo = P+0x10a0
  (FPE tempo event in 0x87def0); delay v/12800 → up to one beat. Not re-evaluated on tempo change.
- Release during the delay → state 10 at once (no sound).

## 7. Declick object (P+0x106c, built at set-sample-rate: 0x876710(obj, attack type 2, 4 ms, …, release type 1, 20 ms))

- **Attack** (only when the resampler start position > 0, i.e. a start offset or a reversed slice; set at trigger by
  0x876830): `na = max(1, Round(0.004·sr))`, table `A[k] = (float)((cos((k+1)·π/(na+1)) + 1)·0.5)`, k = 0..na−1
  (falls from ~1 to ~0). At the first processed frame, `oL = −xL[0]`, `oR = −xR[0]` (post-filter); then
  `x[i] += A[counter+i]·o` per channel until the counter reaches na. (A DC-step cancel, not a fade.)
- **Release** (starts when the voice reaches state 9, after `skip` frames): `nr = max(1, Round(0.02·sr))`,
  `m = min(nr, Round(0.001·sr))`; coefficient `C[k] = (float)min(1, (2/sr)·exp((k+1)·(ln 22050/(ln2·nr))·ln2))`
  (= 2·22050^((k+1)/nr)/sr); gain `G[k] = 1` for k+1 ≤ nr − m, else `(float)((cos((k+1 − (nr−m))·π/(m+1)) + 1)·0.5)`.
  Per frame (j = min(nr−1, counter), counter++), per channel, float:
  `y += C[j]·(x − y); x = (x − y)·G[j]` (y starts at 0). The voice dies at the end of the host block in which the
  counter reached nr.
- Natural forward end: release skip = frames rendered, so the release only shapes the zero tail (hard slice end, as
  REA_rodada170i §9 saw).

## 8. Note-off, cut groups, kill, polyphony

- **Voice_Release** (0x883970): state = max(state, 5); every articulator's envelope and LFO go to release
  (0x796380); if the delay is still pending → state 10; releases the next layer voice too. The voice keeps playing
  until all its articulators report finished (state 9) or the region ends. [HYP: what an articulator without an
  enabled envelope reports — if it reports 9 at once, a note-off starts the 20 ms declick immediately; if it stays
  < 9, slices play to their end regardless of the note length. Check with the articulator study or an FL render with
  short notes.]
- **Cut groups**: before each layered voice of a region with cut group > 0 is created, every existing voice with the
  same cut group (voice+0x18) that has already rendered (+0x94 == 0) is cut (0x883b20): state 9, all its articulator
  states 9, declick-out starts at its next render with skip 0. The new note's own cut group includes itself (a
  re-triggered slice cuts its previous instance). Layer children of the cut voice are not followed (flag 0).
- **Voice_Kill** (0x883c20): UI end event, kills the next layer voice, frees.
- **Polyphony**: no limit or stealing inside Slicex (the voice list grows); FL's channel polyphony applies to the
  master handles.

## 9. Master / global parameters (0x87efe0; masters at P+0x140)

| master | stored | runtime | law |
|---|---|---|---|
| [2] volume | 0..16000 (12800) | P+0x1084 | `x = v/12800 (float)`; x == 1 → 1 else `(float)((exp(x·ln11f) − 1)·0.1f)` |
| [3] randomness | 0..256 (256) | **P+0x1088** | `x = v/256`; `g(|x|)` with the same ln 11 law, times sign(x) |
| [4] LFO | −128..128 | **P+0x108c** | `x = v/128`; `g(|x|)·sign(x)`; written into every articulator's LFO amount (§4.1) |
| [5] pitch | −1200..1200 cents | P+0x1080 (int) | added to the cents (float) every block |
| [6]/[7] Mod X/Y | 0..512 (256) | per voice +0x7f0/+0x7f4 | smoothing §4a; layer modes 3/4 use the raw ints / 512 |

(The brief's labels are swapped: +0x1088 is the randomness level, +0x108c the master LFO.)
Randomness at trigger, per articulator with a random map object (+0x48): `r = map(Random)`; if the map is bipolar
(its +0x18 == 0.5): `base += r·P+0x1088`, else `base *= (r − 1)·P+0x1088 + 1`. Velocity map (+0x40) replaces the
base: `base = map(velocity)`.

## 10. The other option/runtime bytes

- 0x1318 "Sample start covers whole length" (§3.2), 0x131d "Loop half of regions" (§3.3), 0x1319 "HQ envelopes" (§4).
- 0x1325 = editor hidden (FPD_ShowEditor with 0); 0x131c = keyboard panel visible. **P+0x1074 is a locked UI event
  queue** (records {kind, a, b, c, d}): kind 0 "voice started" (deck number, animation id +0x974, start position
  regionStart + offset, region end), kind 1 "voice ended", kind 2 key highlight (key, 1 on / 0 off, only with 0x131c).
  It drives the editor's play cursors and keyboard; it has no effect on audio. Skipped while 0x1325 is set.
- 0x1328 peak meter enabled (editor visible, not rendering). 0x1330 last velocity, 0x104c master voice count (UI).

## 11. Demo-state validation (p/voice_check.py, 40 states)

- Masters: volume 12800 (→1.0) or 13549 (→1.165699), randomness 256 (→1.0), LFO 128 (→+1.0), pitch 0/455/687/760,
  Mod X/Y 256. Options in every state: layering 0, crossfade 3, smooth 1, HQ 0, whole-length 0, loop-half 0,
  link-velocity 1.
- All 503 regions hold the defaults (cut 0, output 0, pan 0, vol 8000, groups 0 = group 1, amounts 12800, cut/res 128,
  pitch/delay/start 0).
- Group headers: FType 0..3 and FOrder 0..1 everywhere (layout holds). **Ten demos use group 1's filter**:
  sx11 high-pass, FCut 34 FRes 0, 1 stage (→ c = 0.5 − 0.367, ≈ 33 Hz, Q 0.1 before articulators); sx12–sx20 low-pass,
  FCut 128 FRes 100, 1 stage, with the group-1 cutoff and resonance envelopes on (articulator agent's art-demos.txt).
  Since every region uses group 1, these filters apply to every slice of those demos — a good first render test.

## Uncertain [HYP]

1. Render sub-block during an FL export: L = 4·sr/44100 only if FPD_ProcessMode(IsRendering) reaches the plugin
   before FPD_SetSampleRate.
2. The active pan kernel (x87 0x637d30 vs float 0x639ff0) and mixer/filter variants depend on CPU flags at
   lRam90c998+0x12/+0x16; assumed the same as in Sytrus (plain kernels).
3. Note-off behaviour with disabled articulator envelopes (§8).
4. FinalLevels.Pitch taken as relative to C5 (so `(60 − key)·100` keeps the slice at its own pitch, matching LYNX's
   170i render tests); InitLevels.Pitch absolute.
5. Host vt+0x128 (output-track buffer lock/unlock) and query 0x24 (song position units for layering mode 7).
6. The signed scratch fade factor (§4a) looks like an FL quirk; reproduce as is.

# Round 180c: issue #74, Merge similar pattern clips and Merge automation clips (slice 1)

Read from FL Studio 2026's own `FLEngine_x64.dll` (Ghidra headless on the `r167g` project, outputs in
`/mnt/e/lynx-work/issues180c/d1.c` to `d6.c`). Clean-room: behaviour documented, no code or art copied.

## Where they are in FL

`ShortcutsModule` [DFM]: `EEMergeAction` Ctrl+G, `EEMergeSimAction` Ctrl+Shift+G, `EEMergeAutoClipsAction` Ctrl+Alt+G.
Menu order in the DFM: Merge pattern clips, Merge similar pattern clips, Merge automation clips, then Purge unused audio
clips. LYNX uses the same order and keys.

## Merge similar pattern clips (`0xf35090` → `FUN_00eb40b0` → `FUN_012dc060 (1, 1)`)

The same routine as Ctrl+G, with a flag. It does not pass through `FUN_01302d30`, so other clip kinds in the selection are
skipped rather than refused.

1. Candidates: `(flags & 0xa0) == mask`, with mask `0x80` when something is selected and `0` otherwise. `0x80` is selected
   and `0x20` is muted, so muted clips never take part. Fewer than two: "Could not find enough valid pattern clips or
   instances to merge."
2. They merge into a new pattern after the lowest one involved, and one clip goes at the first position. The row is the
   largest stored row (`local_84`). Stored rows are `500 − track` (`FUN_01306040`), so this is the topmost track.
3. Similar groups (`FUN_012dbb20`, restarted from the first clip after every hit):
   - The anchor is any clip that equals the group's first clip. `FUN_012dbac0` compares the id (pattern), the window
     (+0x18, +0x1c) and the mute bit, and for legacy blocks the row. **The track is not compared.**
   - Every other group clip must be found at the same time distance. All must be found, or the anchor fails.
   - The hits go, and one clip of the new pattern goes at the anchor's time on the topmost track of the hits.

## Merge automation clips (`0xf350b0` → `FUN_00eb40d0` → `FUN_013009f0`)

`FUN_01302d30` (Ctrl+G) also comes here when the whole selection is automation clips (type 5).

Checks, in order:

| Check | FL's message | LYNX |
|---|---|---|
| Fewer than 2 selected | "Please select at least two automation clips." | same |
| Another kind selected | "The selection may only contain automation clips." | same |
| LFO mode (+0x18 → +0x48) | lossy question | no such clip mode in LYNX |
| Window with no point at its start and end (`FUN_013007a0`) | lossy question | same |
| Min / max differ | "The clips don't have all the same min and max values." | LYNX points are always 0..1 |
| Min / max automated (`FUN_01300950`) | "...has its min or max value automated." | not possible in LYNX |
| Overlap | lossy question | same |
| Links differ (`FUN_012e1a80`) | "Target links conflict": Keep links of the first / Merge all / Delete all | same question; see Differences |

Lossless: a new automation channel named `<first clip> - merged`. Points are copied per clip, rebased to the earliest
clip. A gap gets a point with the last value at the next clip's start. The clips go; the old channels stay. The new clip
goes at the earliest time on the first clip's track.

Lossy (`FUN_012ff0d0 (1)`): overlapping clips are cut at the next clip's start, then each clip is sampled per tick.

## Differences from FL, for the coordinator

1. **Merge all links.** An FL clip holds several links; a LYNX `Automation` holds one target. Each other target gets its
   own clip with the same curve, on the same track. The sound is the same, but there is more than one clip.
2. **"Remember my choice"** on FL's lossy question is not in LYNX.
3. **Lossy tool.** FL opens its events-to-automation tool (`TMEEventsToAutoTool`, `FUN_00dfb410`); Cancel there deletes
   the new channel and changes nothing. LYNX opens its own "Convert to automation" panel at the same step, starting at
   99.8 % rather than at the top as FL does (LYNX's 100 % makes a step per sample, thousands of points). FL also shows
   the new channel's editor as a preview; LYNX shows the point count. The panel isn't modal here, so a later edit of
   those clips cancels the merge.
4. **Legacy pattern blocks** (id `0x60000000`) take part in FL's merges. They do not in LYNX's, as with Ctrl+G since 168f.
5. **A clip past its last point.** LYNX lets a clip run on past its automation's last point (painted clips are 4 bars).
   It only holds a value, so it counts as whole, not as a window missing its end point.

## Checked in FL Studio 2026 (2026-11-07, test projects in `C:\Users\kauan\lynx-test\r180c`, shots `/mnt/e/lynx-work/issues180c/shots/f00–f18`)

Each project was made by LYNX's `--export-flp`. Mute and selection bits went into the `.flp` item flags; FL kept the
mute but not the selection, so the selections were made with Ctrl+drag. Results were read back from FL's saved files
with `flpdump`.

| Case | FL | LYNX |
|---|---|---|
| Ctrl+G, nothing selected: P1 on track 6, P2 on track 4 a bar later, P3 muted on track 2 | one clip "P1 #2 (merged)" at bar 1 on **track 4** (the topmost), 2 bars; P3 stays (f02) | same (the name fixed after this run: LYNX said "P1 #2") |
| Ctrl+Shift+G on P1+P2 at bar 1 (tracks 3, 4) | bar 1 → track 3; the copy at bar 9 (tracks 7, 6) → track 6; the copies one bar apart, with another window, or with a muted clip stay (f06, saved file) | same (test `merge180c_similar_replaces_every_group` is this project) |
| Ctrl+G on two automation clips | automation merge: "Cut - merged" on the first clip's track, the gap held, 4 bars (f08) | same |
| Ctrl+Alt+G on overlapping clips | "Can't perform lossless merge — The selection contains overlapping clips. Perform lossy merge ?" Yes / No and a "Remember my choice" tick (f10); Yes opens "Convert to automation" (Sensitivity, Reset, Accept) beside the new channel's editor (f12); closing it leaves everything as it was (f13) | the same question without the tick; Yes opens LYNX's "Convert to automation" panel; Cancel leaves everything as it was |
| Ctrl+Alt+G on clips of different targets | "Target links conflict" with three buttons and a close box (f16); Merge all links gives one clip "Cut - merged" (f17) | same question plus Cancel; Merge all links gives one clip per target |

## Found and fixed in Ctrl+G (168f)

The merged clip went on the first clip's track; FL uses the topmost track. Muted clips were merged; FL leaves them out.
A selection of only automation clips was refused; FL sends it to the automation merge.

## Not done

`FUN_01302d30` sends an all-audio selection to `FUN_012fc180` (an audio merge). LYNX's Ctrl+G still answers "different
clip types" there, and the clip menu's "Merge clips" is a different operation. Not in this round's scope; not read in
detail.

## Best way?

- **FL:** as above, REA proof in this file.
- **Ableton:** Live has no equivalent of "find every other instance". Consolidate (Ctrl+J) joins a selection into a new
  clip, per track (`/mnt/e/lynx-work/ableton/STUDY.md` item 7, `research-web.md` row 12); repeats stay
  separate clips.
- **Users:** the command dates from FL 9.5 ([Image-Line, "Merge Similar Clips"](https://www.image-line.com/fl-studio-news/merge-similar-clips)).
  Users report merged clips coming out longer than the selection in Ctrl+G
  ([IL forum, June 2026](https://forum.image-line.com/viewtopic.php?p=2062414)); seen in search results only, not
  reproduced in FL or LYNX. LYNX sets the merged clip's length to the span of the selection.
- **Verdict:** FL-exact default. No alternative proposed. Round 177d's "Merge identical patterns" is a different, complementary
  tool: it merges whole duplicate patterns, whereas this merges groups of clips.

# Slice 2: Fruity Slicer as a LYNX generator

Binary: `Plugins/Fruity/Generators/Fruity Slicer/Fruity Slicer_x64.dll` (sha256 `7616d1f8…`, the one REA_rodada69 used),
Ghidra project `~/ghproj/r69` program `slicer.dll`; decompiles in `/mnt/e/lynx-work/issues180c/rea/sl*.c` (unit
`r:765000–76c000` in `slall.c`). Menus from the `TSynthResModule` DFM (`dfm-slicer-menus.txt`), the editor from
`TSynthEditorForm` (`dfm-slicer.txt`, 377 × 330). Handlers found through the published-method table (Delphi RTTI).

## State (`FUN_00766c40`, refines REA_rodada9 §3.1)

The two slice fields after the position are the slice's **own key** (`+0xC`, −1 = first note + index; the SliceKeyMenu
sets it) and its **place in beats** (`+0x10`, −1 = from the position; a `.zgr` groove brings it). After the slices:
ANIMATE (`P+0x141`), first note, PLAY TO END, the sample rate, AUTO-DUMP (`P+0x13F`), DECLICK (`P+0x13C`),
AUTO-FIT (`P+0x140`), the editor's display mode (v ≥ 14), then the 364-byte voice block (read only when complete).
Versions < 15 add 2 to a method > 1. FL's six demo states (two projects) read and write back byte for byte
(test `slicer180c_fl_demo_states_round_trip`, three of them version 16).

## What FL does to the sample (`FUN_007686d0`, in this order)

1. **REV** (`FUN_00769cd0`): the slice's frames reversed in place.
2. **DECLICK** (`FUN_0076a790` → `FUN_00608530 (…, 1, 0.7)` → `FUN_0060d020` → `FUN_0060cdd0`): per slice, over its
   last `round(sr · 0.04 · 0.7)` frames (after skipping trailing zeros), a one-pole low-pass whose coefficient climbs as
   `min(1, 2/sr · exp(i · ln 22050 / n · X))` is subtracted from the sound, and the last `round(sr/1000)` frames fade on
   a raised cosine. `X` is a global at `0x7db440` filled at start-up; LYNX takes it as ln 2 so the rate reaches
   22050·2/sr at the end **[HYP, to check against a render]**. FL turns the sample to float here.
3. **TS / PS** (`FUN_0076aa20`): factor `Exp(Ln 2 · v/10000)` in single precision (`FUN_00766300`). Methods 0/1
   (Fill gaps, Alternate) with a stretch: the sample goes to 16 bits, every slice moves to `round(pos · factor)`, the
   total to `round(frames · factor)`; a slice that grows runs `FUN_0075b5d0` (short ones, < 150 frames, with method 0),
   one that shrinks is cut. `FUN_0075b5d0`: a slice whose last 10 ms are below one step is copied and padded with
   silence; otherwise, per channel, the part after the first local peak of the slice's second half (from 20 ms before
   its middle) ping-pongs between that peak and the last one (`|((k − first + span) mod 2·span) − span| + first`),
   faded from the old end to twice the old length by `(1 − t)³` pulled toward 0.275 after t = 0.5 (Alternate: toward
   0.1375, times `1 − w³`), silent beyond; method 0 ends with a 2 ms linear fade. Methods ≥ 2 and PS go through
   `FUN_007520c0` (Elastique, licensed): **not reproduced** — LYNX keeps the length there, and plays PS by resampling.
4. **ATT / DEC** (`FUN_0076a830`): per slice, `round(ms · (float)(sr/1000))` frames, capped at the slice. Through
   `FUN_00607ed0`: `t = (k − first + 1)/(n + 1)`; fade in shape 2 (linear), fade out shape 3 with 10:
   gain `(e^{(1−t)·ln 11} − 1)/10` (single precision steps); both turn into a raised cosine when the faded part is
   50 ms or less (flag 0x100).

## Notes (`FUN_00768230`, TriggerVoice)

Key = `round(pitch · 0.01)` (0…124) through the 125-entry key table; the voice gets `(60 − key) · 100` cents, so the
slice keeps its pitch. Length: up to the next slice, or to the sample's end with PLAY TO END (`P+0x13E`), minus one
frame. LYNX plays it on the sampler's slice mode (first key and PLAY TO END added to it).

## Note-off and the voice settings (round 190f, issue #129)

Voice_Release (`FUN_007684d0`) hands the voice to the plugin voice processor's release, `FUN_00756d80` →
`FUN_007579d0`: the volume, Mod X, Mod Y and pitch articulators enter their release stage, and **when the volume
record's envelope is off** (`+4` of the second 0xA0-byte record) it calls `FUN_00757a50`, which starts the 10 ms end
fade at once — the same code as 3x Osc's (REA_rodada47 §3.1). So a slice never rings past its note in FL: either the
volume envelope's release plays, or the 441-entry end fade does. LYNX's sampler voice followed FL's *channel sampler*
here (no envelope = the sample plays on, REA_rodada76 §6), so a Slicer made in LYNX, or one from a state older than
version 16, kept sounding after the note-off. Now a Slicer voice without volume envelope takes the end fade
(`Instruments.cpp`, note-off; test `slicer190f_note_off_ends_the_slice`).

The plugin's defaults (`FUN_00757bd0`, used when the state has no block): five neutral records, the volume one with
flag 4 and release tension −101, Mod X 256, envelopes off. FL's three version-16 demo states all hold a volume envelope
that is on with delay/attack/hold/decay 100, sustain 128, release 6500; no such constant is in the DLL, so whether a new
Fruity Slicer starts like that in FL is **[OPEN: needs a fresh Slicer saved from FL]**.

The block's fields are read and written through `core/FlVoiceSettings.h` (`fslicer::voiceSettings` /
`setVoiceSettings`), in FL's units, so a .flp export carries an edit made in LYNX and FL reads it as its own
(test `slicer190f_voice_settings_survive_flp_export_and_import`).

## Dump beat to piano roll (`FUN_0076b640`, tags of the DumpToPRMenu)

Times: `pos · ((ppq · P188 · beats / frames) / 4)` (P188 taken as 4), or the slice's beat place · ppq; Original length
(9) uses real time at the project tempo. Quantize (2) and Swing (3) round to 16ths (swing: odd ones + a third of a
16th). A note lasts to the next later time (the loop end for the last). Slice per note: Reverse `n−1−i`, Random,
Flatten (all slice 1), Shift up `i−1`, Shift down `i+1` (mod n). Velocity 100/128; Accentuate 110/128 on the beat
(from −ppq/8), 95/128 off it; Pitch up beat +300 cents on alternate beats; Widen stereo ∓16 of ±64; Crazy random pan
±8, velocity (90…109)/128, pitch −200…600 cents; Stutter splits each note in 2 or 4. LYNX runs it at PPQ 96 and
scales to its 960. Random uses LYNX's generator (Delphi's seed is not shared).

## Differences (LYNX)

1. PS and the Elastique methods (2–7) are not reproduced (above).
2. Auto-slicing (Dull / Medium / Sharp with LOW / HIGH) uses LYNX's transient detector; FL's detector was not read.
3. Open (`.zgr` beat grooves) opens samples only; "Open in Edison" and "View › Spectrum" are not there.
4. ~~A slice's own key is saved and shown; the voices still map the slices from the first note up (the key table is not
   rebuilt from custom keys).~~ Closed by issue #160 (round 190eg): the voices use FL's key table, `FUN_0076c200`
   **[REA, static, Fruity Slicer_x64.dll sha256 7616d1f8…]**: all 125 entries −1, then for each slice in order
   `table[FUN_0076b4f0(i)] = i` (its own key, else first note + i, clamped 0…124), so a later slice wins a shared key.
   TriggerVoice (`FUN_00768230`) starts no voice for a key outside 0…124 or whose entry is −1: below the first slice's
   key, past the last one, or a key a custom key left empty, nothing plays (`fslicer::keyTable`, tests
   `issue160_*`). Issue #160 itself was LYNX's rack: a kept Slicer was configured with the bare channel at the next edit,
   which emptied its slice table, so every key played the sample from its start; the instrument now takes its slices,
   key table and voice block from its state at every configure (also inside a Patcher).
5. The editor is LYNX's own drawing in FL's layout, at twice its size.

## A new Fruity Slicer, and FL's Envelope / instrument settings page (round 190m) **[EXEC: FL 2026 GUI + saved .flp]**

**A new Slicer's state.** A Fruity Slicer added from FL's ADD menu and saved at once (no sample) writes 418 bytes:
version 16, BEATS 0, BPM 0, PS 0, TS 0, **method 4**, ATT 0, **DEC 4**, empty path, 0 slices, **ANIMATE 1**, first
note 60, PLAY TO END 0, 44100, AUTO-DUMP 1, **DECLICK 1**, AUTO-FIT 1, display 0, then the 364-byte voice block. The
block is the plugin's defaults (`flvp::defaultSnapshot()`) except the **volume record**: envelope **on** (−1), delay /
attack / hold / decay **100** (none), sustain **128**, release **6500**; its flag 4 and release tension −101 are the
defaults already. So a new Slicer's notes end with that short release, not with the 10 ms end fade (which is what a
state with the envelope off plays). `fslicer::fresh()` carries all of it but the method: LYNX keeps Fill gaps there,
because method 4 is the licensed engine's and LYNX's TS wheel cannot run it.

**The page** (a Fruity Slicer's channel window, second tab; the same page every generator on the voice processor
shows). Tabs Panning, Volume, Mod X, Mod Y, Pitch, then three boxes side by side:

| Box | Contents, left to right / top to bottom |
|---|---|
| Envelope | switch + title; graph (a node at each corner; on Mod X, Mod Y and Pitch the volume envelope is drawn in grey behind); knobs DELAY, ATT, HOLD, DEC, SUS, REL and, at the right edge, AMT; three small tension knobs under ATT, DEC and REL with "TENSION" between them; Tempo switch bottom right |
| LFO | three shape buttons (sine, triangle, pulse) + title; graph (a flat line while the amount is 0); knobs DELAY, ATT, AMT, SPEED; Tempo and Global switches |
| Filter | MOD X knob, MOD Y knob (larger than the others), the type selector ("Fast LP") |

- **Panning has no Envelope box** (LFO and Filter stay where they are); the **Volume envelope has no AMT** knob.
- The knobs show no value text (FL's hint bar does); each target has its own colour.
- The graph's time axis is not decompiled. From the shots: a release of 6500 is 8 px of 272, and 20000 or 30000 are
  a tenth of the width each — LYNX draws `min (1/10, 0.33 · (v − 100)/65436)` of the width per stage **[HYP]**.
- Not taken over yet: the menu behind the arrow at the Envelope box's right edge (not opened in this session), dragging
  the graph's nodes, the per-target colours.

**Best way?** FL: one page per generator, a tab per target, envelope and LFO side by side with a graph each.
Ableton: Simpler / Sampler show one envelope at a time too (Amp, Filter, Pitch tabs) with a graph whose nodes are
dragged, and the LFO on its own tab. Users: not researched this round. Verdict: FL's layout as the default (it is
also what Live does in its own way); dragging the nodes in the graph would be the next improvement and is what both
do.

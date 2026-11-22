# REA round 190ov: FL automation clips linked to several controls

Static analysis of `FLEngine_x64.dll` (FL Studio 2026, md5 fce4ea557685c0aeeeeb126013066c25), Ghidra 12.1.4 headless on a
copy of `~/ghproj/r190hb`, plus the 46 demo `.flp`. Behaviour is described here. No FL code was copied.
Tags: [DECOMP fn] decompiled, [DATA] the demo files, [INFER] a reading that is not proven.

Notation: a *link object* is FL's 0x1C-byte heap record `{ptr formula; u32 rec[5]}`. `rec` is exactly the 20-byte payload
of the event. Link offsets below are **file offsets** (+0..+16). The in-memory offset is the file offset + 8.

## 1. Writing and reading

### 1.1 Two global link lists, two event ids

FL keeps two global lists (pointer table at `PTR_DAT_015e0a00`): list 0 holds remote-control (MIDI) links and list 1
holds internal links (automation clips and internal-controller outputs).

- **Save** [DECOMP FUN_00f40840, block at ~0xf4352d]: for list k = 0, 1, and for each link in list order, it writes
  event **0xE2 + k** with exactly **20 bytes** (the 5 dwords). If the link has a compiled formula, it then writes
  **event 0xE6 = the formula's source text** (string event, writer FUN_00a9eb60) right after that record. So 0xE2 is for
  MIDI links and 0xE3 for internal links. This block comes before "Saving channels...". It is skipped when the global
  flag `*PTR_DAT_015e4d18` is set ([INFER] a partial or template save).
- **Load** [DECOMP FUN_00f46490 (the project loader), `iVar3 - 0xE2 < 2` branch]:
  1. A new link is made with FUN_00f82530 [DECOMP]: formula = null, rec all 0, **+16 = 0x1D5 (469)**.
  2. The loader copies the event payload over rec. The copy uses the event's size, so FL expects **one 20-byte record
     per event**. [DATA] All 1 362 0xE3 and 107 0xE2 events in the demos are exactly 20 bytes. Reading 20-byte chunks
     in a loop, as LYNX does, is harmless.
  3. If the file's version code is below `0x3D2071`, **+16 is multiplied by −4** (an old smoothing scale).
  4. It **ORs the flags (+12) with 4 for 0xE2 and with 8 for 0xE3**, so the class bit comes from the event id, not from
     the file.
  5. It appends the link to list `eid − 0xE2`.
  - **0xE6** [same function]: it compiles the text with FUN_00f81f70 and stores the result in the formula slot of
    **the link most recently loaded**. A text that fails to compile gives null, so the link maps 1:1.
- After loading, the list is sorted by source (+0) [DECOMP FUN_00f825b0, bubble sort]. The runtime finds a source's
  links by binary search [DECOMP FUN_00f82660], so all links of one source are contiguous.

### 1.2 The 20-byte record

| Off | Meaning | Proof |
|---|---|---|
| +0 | **source**. For an internal link: the source's id. An automation clip is `channel << 16` (the channel's +0x9C id). A controller output is `id + 0x8000 + k` [DECOMP FUN_0132b9d0: `chan+0x9C + k + 0x8000`]. For a MIDI link: a packed address. The low word is the controller number; the word at +2 holds bits 0..3 = MIDI channel − 1 and bits 6.. = port + 1 | [DECOMP FUN_00f8beb0 / FUN_00f8bc70, FUN_00f697d0 "add new automation clip target link"] |
| +4 | **unused by FL 2026.** The constructor zeroes it. Neither the remote-control form nor "add new automation clip target link" writes it. No runtime user of link+0x0C turned up | [DECOMP FUN_00f82530, FUN_00f8bc70, FUN_00f697d0]. [INFER] Not exhaustive: the offset is too generic to scan. [DATA] 0 in most records; `channel<<16` (a copy of the source) in 1 older project; 0xFF in 22 MIDI records; ASCII "Play" garbage in 9. Ignore it on read and write 0 |
| +8 | **target control id**. 0x0FFF in bits 16..31 = Omni channel (the selected channel). Top 10 bits = 0xFF = Omni mixer slot (the selected track) | [DECOMP FUN_00f8c9d0, FUN_00f8bc70] |
| +12 | **flags**, see below | |
| +16 | **smoothing time**, the Remote-control "Time" slider (−1024…1024, default 469). It is **used only when flag bit 1 is set** | [DECOMP FUN_00f8bc70 store, FUN_00d0b7b0 / FUN_00fdaec0 use] |

Flags (+12) [DECOMP FUN_00f8bc70 store, FUN_00f8beb0 load-into-form, FUN_00f83350 Accept]:

| Bit | Meaning |
|---|---|
| 0 (0x1) | internal links only: the form's **"Record automation"** check box (form +0x960). When set, the link sets the target with FL's recording bits, so the value can be recorded: set-value mode 0xA61 instead of 0x821, from table DAT_0150FE7C [DECOMP FUN_00d0b7b0; 0x200/0x40 are the record paths in FUN_01046a70] |
| 1 (0x2) | **smoothing enabled** (the check box in the "Smoothing" group title) |
| 2 (0x4) | MIDI / remote-control link (forced by event 0xE2) |
| 3 (0x8) | internal link: automation clip or controller (forced by event 0xE3) |
| 4 (0x10) | temporary marker used while the form edits (cleared on Accept) |
| 8..15 | MIDI note-link mode (Default / Hold / Latch / Inc / Stay), see REA_rodada190bt |

[DATA] 0xE3 flags in the demos: 8 (1 356 records), 9 (4 records, record on), 10 (2 records, smoothing on; one of them
is an automation-clip link with +16 = 349).

### 1.3 Where the mapping formula lives: event 0xE6, one per link

- The formula belongs to **each link record**, in the event that **follows its 0xE2 / 0xE3**. Its payload is the formula
  text, NUL-terminated: UTF-16LE in current files, ANSI in old ones (Synth_dfr). [DECOMP save/load above]
  [DATA] There are 36 0xE6 events in the demos. Every one follows a 0xE3, and most belong to **automation-clip links**,
  for example:
  - Bombs Away: clip channels 0x7F, 0x80, 0x81, 0x82 → target 0x2F878000 with "Input*2".
  - Synth_dfr: clip channel 0x10 drives 5 targets, each with its own formula, such as "(Input*.6+.4)*0.8000030517…".
- **No 0xE6 for the default.** FUN_00f81ec0 rejects an empty text or one equal to "INPUT" (compared after upper-casing),
  so the link keeps a null formula and nothing is written [DECOMP FUN_00f81ec0, FUN_00f81f70]. Compiled formulas are
  shared by their text (global list `DAT_016bacc8`, reference count at +0x48).
- Writing back: put event 0xE6 (the formula text) directly after the link's 0xE3, only when the formula is not "Input".

### 1.4 Channel event 0xAA and the strings 0xF5…0xFF: Loop Starter, not links

- Save [DECOMP FUN_00f3e190]: it writes `0xAA = chan+0x714` (int32), then event 0x33 (byte: bit0 = +0x738,
  bit1 = +0x79D, bit2 = +0x79C). **When +0x714 > 0**, it then writes the strings 0xF5 = +0x718, 0xF6 = +0x728,
  0xF8 = +0x720, 0xF9 = +0x75C, 0xFE = +0x73C, 0xFF = +0x744.
- Load [DECOMP FUN_00f46490]: it reads the same fields back.
- Meaning: the undo step **"unset channel Loop Starter status"** [DECOMP FUN_00dbe3a0, STR in it] sets +0x714 = −1 and
  clears +0x718, +0x728, +0x738, +0x79C and +0x79D. Its undo record (type 0x31) [DECOMP FUN_0101e0a0 / FUN_01025780]
  saves exactly these fields.
- So **0xAA is the channel's Loop Starter index (−1 = none)** and the strings are its Loop Starter metadata. They are
  **unrelated to automation links**; round 167h's "count of target links" is wrong. [DATA] 0xAA is −1 or 0 in every
  demo, never > 0.

## 2. Playback of a multi-link clip

Chain per tick [DECOMP FUN_00d0c8e0 (clip voice tick), FUN_00fe9b90, FUN_00ba1620, FUN_00d0b7b0, FUN_00f824b0,
FUN_01046a30]:

1. **Clip value, once per clip and not per link.** The clip's articulator gives
   `out = base + env·amount (+ lfo·lfoAmt)`, where:
   - `base = pan/12800`, from the clip channel's pan level (chan+0x16C), scale 7.8125e-5 [DATA: the global behind
     PTR_DAT_015e4780].
   - `amount = volume/12800 − base`, from the volume level (chan+0x170).
   - `lfoAmt = lfoLevel/128 · amount`.

   FL re-derives these every time the clip's pan or volume changes [DECOMP FUN_00fe9b90, called from FUN_00fe9bf0 /
   FUN_00fe9d20 when FUN_01046a70 sets the channel's pan or volume, both limited to 0…12800]. So **min = pan, max =
   volume**, as LYNX reads them. The value is converted to an integer `v = round(out·2^30)`, clamped to 0…2^30.
2. **Send only on change.** The voice keeps the last value it sent (voice+0x370; set to 0x7FFFFFFF when the voice
   starts, FUN_00d275f0, so the first tick always sends). It calls FUN_00d0b7b0(clip id, v) only when v changed.
3. **Every link of that source gets the same v** (FUN_00d0b7b0 walks the contiguous links with source = clip id). Per link:
   - If the target is in the "user-override" list (`PTR_DAT_015e3ba0`), the link is skipped (§3).
   - **Formula** (FUN_00f824b0): no formula → v unchanged. Otherwise `x = v/2^30`, `y = formula(x)` (the "Input"
     variable = x), **y clamped to 0…1**, `v' = round(y·2^30)`.
   - **Smoothing off** (flag bit 1 clear, the default for clip links): FUN_01046a70(target, v', 0x821, or 0xA61 when
     bit 0 is set). The 0x20 bit means "normalised input", and FUN_01046a30 maps it to the target's own integer
     range: `min + round(v'·2^−30·(max−min))`. The 0x800 bit marks the change as coming from automation.
   - **Smoothing on** (bit 1): FUN_00f8b8e0(target, v', +16, mode) puts the target into the per-control smoother list
     (`DAT_016bacd0`). Its time comes from |+16| and the slider's sign selects a variant (see 190bt for the
     poles/time read-out).
4. There is **no per-link min/max, range or offset**: the 20-byte record has no such field. A per-link range exists
   only as a formula (e.g. "Input*0.5+0.5"). The clip's min/max is applied once, before the formulas. [DECOMP + DATA]
5. Stored but **unused for clip links**: +16 (469) while bit 1 is clear, and the mode bits 8..15 (used only on the
   MIDI path FUN_00fdaec0).

Chase (seek / play start) [DECOMP FUN_011e7470 → FUN_011e7090 / FUN_011e6e60 → FUN_00d0b8e0]: the same chain, with the
clip evaluated at `min(position − start + offset, length)`. The formula is applied, but there is **no smoothing, no
record bits** (mode 0x821 always) and **no override check**.

## 3. Two clips on one control

[DATA] This is common: 18 demos have a target driven by 2…10 different clip channels (e.g. Bombs Away 0x31458000 ←
channels 49…52).

- **No arbitration.** FUN_00d0b7b0 / FUN_01046a70 simply set the target. There is no owner, no priority and no
  "playing clip wins" check. [DECOMP] During playback **the last write wins**:
  - A clip that starts sends at once (sentinel), so it takes over.
  - After that, each clip writes only when its own value changes. A flat clip that overlaps a moving one is silent
    after its first tick, so the moving one wins.
  - If both change in the same tick, the voice processed later wins. [INFER] The voice order is FL's voice list order.
  - Whether a clip has one link or several makes no difference: links are independent.
- **After a clip ends**: the last tick evaluates the remaining fraction up to the clip's end. The voice then releases
  and **sends nothing more**, so the target **holds the clip's final value** until something else writes it.
  [DECOMP FUN_00d0c8e0 / FUN_00d241e0] No reset to a default was found.
- **On seek / play start** [DECOMP FUN_011e7090, FUN_011e6e60]:
  1. For each clip **channel**, FL keeps the one item that started at or before the position and ends latest.
     Muted items, disabled tracks and disabled channels are skipped.
  2. It sorts these by end time (start + length), ascending.
  3. It applies them in that order, each evaluated at `min(pos − start, length)`. Past its end, a clip gives its last
     value.

  So for a shared control **the clip channel whose latest item ends last wins**. An item still under the playhead
  usually ends last, so it wins. Equal ends keep the playlist order. Items under the playhead also get their voices
  started mid-way (FUN_00ffa0a0), and then the playback rules apply.
- **User override**: FUN_01046a70 with bit 0x100 and without 0x800 (a user tweak) adds the control to
  `PTR_DAT_015e3ba0`. Clip links then skip that control [DECOMP FUN_00d0b7b0] until either:
  - a transport state change clears the list [DECOMP FUN_011ca490 via FUN_011ca520], or
  - an item of that clip starts again while a global option is on (`*PTR_DAT_015e2c10`, FUN_00ffa0a0 → FUN_00ff9420 →
    FUN_010524e0). [INFER] Which UI option this is was not identified.

## 4. For LYNX

- **Read:**
  - Take one record per 0xE3 / 0xE2.
  - Ignore +4.
  - Attach the next 0xE6 (when present) to the record just read, as that link's formula.
  - Flags: bit 1 = smoothing (time +16), bit 0 = record. The class bit comes from the event id.
  - Do not treat 0xAA / 0xF5…0xFF as links: they are Loop Starter data.
- **Play:**
  - Compute the clip value once: pan…volume range, plus the LFO.
  - Then, for each link: formula on 0…1, clamp to 0…1, then the target's own range.
  - No per-link range.
  - Smoothing only when bit 1 is set.
  - Overlapping clips on one control: the last writer wins, values are sent only on change, and the value holds after
    a clip ends. On seek, the latest-ending clip channel wins.
- **Write:** 0xE3 = {source, 0, target, 8 (|2 smoothing, |1 record), smoothing or 469}, then 0xE6 "formula" only when it
  is not "Input".

Not resolved: what the global option `PTR_DAT_015e2c10` is, and the exact voice processing order inside one tick.

## 5. Built in LYNX (round 190ov)

- `.flp` reader (`flp/Flp.cpp`): an 0xE6 after an automation-clip 0xE3 record is that link's formula
  (`flp::AutomationLink::formula`; "Input" in any case = none). 0xAA is not read as links (it never was).
- Import (`core/FlpImport.cpp`): a clip with one link and no formula imports as before (its points on the target's own
  scale), so such projects render bit for bit as before. A clip with several reproducible links, or one link with a
  formula, becomes an automation with target links (`Automation::links`, round 190gw): the points hold FL's clip value
  (the channel's pan…volume range applied, as FL does once per clip), each link its formula and, where LYNX counts the
  control differently from FL (a mapped effect parameter, the tempo of a clip without a range), its own scale
  (`Automation::Link::scale`, the map sampled with the fewest points that reproduce it within 1e-6: a straight map is
  two points). A link whose control LYNX lacks is left out; the first reproducible one names the clip's target.
- Playback (`core/RackRenderer.cpp`): each link takes the clip's value through its formula (clamped to 0…1), then its
  scale. A control that several clips drive (links and plain clips alike) plays them as one slot with LYNX's issue #97
  rule (the playing clip that started last, else the one that ended last), each through its own link. Before, a link
  took its control over the whole song. FL's own rule is "the last write wins, writes only on change" (§3); the
  difference shows only where a flat clip overlaps a moving one on the same control.
- `.lynx`: a link with a scale is saved with it as a sixth element. The copy for 0.1.30 drops the scales (0.1.30 plays
  links 1:1) and reports the clips whose links have a scale or share a control with another clip.
- `.flp` export (`core/FlpExport.cpp`): one 0xE3 record per link, an 0xE6 with its formula when it has one, the points
  as the clip's value.
- Not done: per-link smoothing (flag bit 1; one demo clip link has it), "Record automation" on clip links (bit 0), the
  user-override list (§3).

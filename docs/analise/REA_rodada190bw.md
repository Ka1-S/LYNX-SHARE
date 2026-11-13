# REA round 190bw — automation clip curve modes and four Piano roll tools

Target: `FL Studio 2026/FLEngine_x64.dll` (Ghidra project r167g `flea`, read-only headless decompiles kept in the
round's work folder: `ptmenu.c`, `envclass.c`, `disp1628.c`, `prflip.c`, `flip800.c`, `prlimit.c`, `str/lvl-*.c`,
`lvlfn.c`; one static disassembly of 0xdf0900 with objdump; constants read from the file). Form resources from the
round 190bc inventory (`TFruityLoopsMainForm.EEACPointMenu`, `TPRFlipForm`, `TPRKeyLimitForm`, `TPRLevelScaleForm`,
`TPRLFOForm`, `TPRRandomForm`). Static only: FL was not started. Clean-room: behaviour and formulas in own words.

## 1. The point menu of an automation clip [RES]

`EEACPointMenu` (main form), top to bottom: a header whose caption is set at run time, **Delete**, header **Mode**,
thirteen radio items — Smooth, Hold, Single curve, Single curve 2, Single curve 3, Double curve, Double curve 2,
Double curve 3, Half sine, Stairs, Smooth stairs, Pulse, Wave — a separator, **Copy value**, **Paste value**,
**Type in value...**. The items carry no handler in the form; they are wired at run time to the envelope editor
(the same editor class the plugins' envelope pages use, where the menu was seen in round 173d).

## 2. The envelope editor's point and its rules [DECOMP]

- A point is a 32-byte record: value (f32, +4), tension (f32, +8), mode (u8, +0xC), a selected bit (+0xE), a sign
  byte (+0xF, 2 = straight) and three prepared coefficients (+0x10…+0x18). The mode and tension of a point describe
  the segment that **ends** at it; the first point's are never drawn.
- "Change point curve" (0xcd79f0) writes the picked mode into every selected point, leaves the tension alone, and
  remembers the mode in the editor (+0x1628).
- A new point (0xcce3b0) takes the editor's remembered tension (+0x1624) and mode (+0x1628) and becomes the selected
  point. The remembered mode starts at 0 (Single curve); no other writer was found.
- Dragging a tension handle (0xcc9790) writes the point's tension and updates both remembered values. A right-click on
  the handle (0xccfd0c) sets tension 0 and clears the remembered tension.
- Delete (0xcd3180) never removes the first point and refuses below the minimum point count.
- "Turn all points smooth" (0xce7f90) sets mode 10 and tension 0 on every point.

## 3. The evaluator [DECOMP 0xb9def0 stepper, 0xb9f7e0 value at a time]

The engine's stepper keeps a segment index, a phase and a rate; the value is `start + shape(phase) · (end − start)`
in single precision. `shape` is the shared envelope library's law (documented for the WaveShaper map in
REA_rodada51 §2.4 and for the X-Y-Z controller in round 165, `core/FlCurve`), mode by mode:

| # | Mode | Shape for x in 0…1, tension t in −1…1 |
|---|---|---|
| 0 | Single curve | exponential bend, k = 1000001^|t| − 1: t > 0: 1 − ((k+1)^(1−x) − 1)/k; t < 0: ((k+1)^x − 1)/k |
| 1 | Double curve | mode 0 on each half, mirrored around the middle |
| 2 | Hold | 0 until the end |
| 3 | Stairs | n steps, n from the tension (below) |
| 4 | Smooth stairs | n steps with cubic risers |
| 5 | Pulse | square wave of n periods |
| 6 | Wave | t < 0: sine of n + ½ periods; t ≥ 0: triangle |
| 7 | Single curve 2 | x − (v⁶ − v)·t with v = x or 1 − x by the tension's sign |
| 8 | Double curve 2 | mode 7 on each half |
| 9 | Half sine | x blended with a quarter sine (rising or falling by sign) by |t| |
| 10 | Smooth | cubic Hermite; end slopes from the neighbouring Smooth segments, flattened by 1 − |t| |
| 11 | Single curve 3 | hyperbola through both ends, p = 1 − (1 − max(0.9|t|, 2⁻¹⁶))³ |
| 12 | Double curve 3 | mode 11 on each half |

Count for modes 3…6: n = round(0.001 · (1000001^(1 − 0.6|t|) − 1)).

Smooth: with dx the segment's length and y0 → y1 its values, the slope at each end is taken from the neighbour on
that side when the neighbour is a Smooth segment too (weighted by the ratio of the two lengths: weights 2r+1 and r+2
at the end, 2+r and 1+2r at the start); a flat segment is drawn straight.

## 4. .flp storage [DATA + writer]

Event 0xEA, per point 24 bytes: f64 Δposition, f64 value, f32 tension, u8 mode, 3 bytes. The mode and tension sit on
the point where the segment ends (§2).

## 5. Piano roll tools

### 5.1 Score flipper (`TPRFlipForm`) [RES + DECOMP 0xdee5c0, 0xdee8d0]
Boxes: Flip horizontally, Flip vertically, **Preserve start times**. A first pass (0xdee5c0) takes over the notes in
play: first start, last end, last start, lowest and highest key. Horizontal: `new start = last end − (start − first
start) − length`; with Preserve start times: `new start = last start − (start − first start)`; never below 0.
Vertical: `key = highest − (key − lowest)`.

### 5.2 Limiter / transposer (`TPRKeyLimitForm`) [RES + DECOMP 0xded3b0]
"Key / scale restriction": base key, a chord / scale list ("(none)" first), **Snap**: Above, Under, Alternate.
"Keyboard range / transpose": a keyboard with a range and one marked key, **Wrap to top / bottom** (hint: wrap notes
to the highest or lowest octave in the selected range). Per note, in this order:
1. Restriction (only with a scale chosen, and only when the Piano roll's own scale snapping is off — then the boxes
   are disabled with the text "Scale options are unavailable when Scale Snapping is enabled in Piano Roll"): a key
   that is not in the scale goes to the nearest scale key; exactly between two, Above takes the upper, Under the
   lower, Alternate the upper for the first such note, the lower for the next, and so on.
2. Range: a key above the top comes down by `((key − top + 11) div 12)` octaves, with Wrap by `((key − bottom) div
   12)` octaves (into the lowest octave of the range); below the bottom the mirror image (`(bottom − key + 11) div 12`
   up, with Wrap `(top − key) div 12`). Then a clamp to the range.
3. Transposition: `key + 60 − marked key` (the hint line shows it as "transpose +n"), folded by octaves onto the
   keyboard.

### 5.3 Level scaling (`TPRLevelScaleForm`) [RES + DECOMP 0xdf0a40 + DISASM 0xdf0900]
Sliders Center (0…8192, default 0), Multiply (−16384…16384, default 8192), Offset (−8192…8192), wheel Tension
(−8192…8192). With c, m, o, t the four divided by 8192 and x the level normalised to 0…1:

```
reach = max(c, 1 − c)        d = |x − c| / reach
p = 1001^|t| − 1             t > 0: f = 1 − ((p+1)^(1−d) − 1)/p      t < 0: f = ((p+1)^d − 1)/p      t = 0: f = d
y = sign(x − c) · f · m · reach + o + c, limited to 0…1
```
The result is rounded to the level's integer range. (p, 1/p and ln(p+1) are kept in single precision.)

### 5.4 LFO tool (`TPRLFOForm`) [RES + DECOMP 0xde93e0]
Both speed wheels (start and end; 0…40960, default 20480) share a popup menu: 1 step, 2 steps, 4, 8, … 512 steps
(tags 9…0). An item sets the wheel to `tag · 4096`, in line with period = one beat · 2^(7 − wheel/4096) steps.

### 5.5 Randomizer (`TPRRandomForm`) [RES]
Strength wheels for panning, velocity, release, modulation X, modulation Y and fine pitch (law not read this round).

## 6. What LYNX does now (round 190bw)

- `AutomationPoint::mode` (FL's 0…12; 255 = a point saved by an older LYNX, which keeps LYNX's old bend
  `x^(2^(3·tension))`). A segment's mode and tension live on the point where it **starts**, the tension with the
  opposite sign; `.flp` import and export move them one point over. `.lynx` writes a fourth element only for points
  that carry a mode, so files without modes are byte-identical to before.
- `flcurve::prepareSmooth` (the Smooth law with its neighbours) joins the other twelve in `core/FlCurve`.
- Editing follows §2: a new point takes the editor's remembered mode and tension; the split segment keeps its curve
  on the far side; deleting a point hands its curve to the one before; the mode menu is FL's, in FL's order.
- Tools: Flip gets Preserve start times; Limit gets base key, scale, Snap, Wrap and Transpose with FL's order and
  octave rules (the narrow-range case now moves by octaves before the clamp, as FL does); Scale levels gets Center
  and Tension with FL's law (`notetools::flLevelScale`) and a multiplier down to −200 %; the LFO tool's two Period
  sliders get the step menu; Randomise gets Mod X, Mod Y and Release.

Differences kept on purpose, or left open:
- The first point's mode items are disabled in LYNX (it ends no segment; FL stores an unused mode there).
- The menu header: LYNX shows the point's value; FL's run-time caption for an automation clip was not seen (needs a
  live check).
- Limit's scale list is LYNX's scale list, not FL's chord / scale table (its contents were not read); the range and
  transposition are sliders, not FL's keyboard widget; keys stop at 127 where FL's keyboard goes to 131.
- Scale levels keeps LYNX's start / end ramp of the multiplier; FL has one Multiply. LYNX's Center starts on the
  centre line for pan and fine pitch (FL: 0 for every level). LYNX's levels are not rounded to FL's integer steps.
- Randomise's new amounts use LYNX's own draw; FL's randomizer law was not read.

## 7. Best way?

**Curve modes per point**
- FL: 13 modes on the point where the segment ends, one tension per point, a remembered mode for new points.
- Ableton: no modes; a segment is straight or bent with Alt-drag (one curvature per segment), plus shape presets
  that insert points.
- Users: no user sources were gathered this round. (Reasoned only: with FL's storage the mode picked on a point
  changes the curve that arrives at it.)
- Verdict: FL's modes and FL's meaning exactly (files must round-trip, and the menu of a point sets the curve that
  arrives at it, as in FL). Only the storage inside LYNX differs (on the segment's first point, where LYNX always
  kept the tension). LYNX's "Straight segment" and "Whole curve" items stay as extras.

**Limit: scale restriction and wrap**
- FL: as §5.2. Ableton: the Pitch MIDI effect and Scale device do the same live, not as an edit tool; the range folds
  ("Lowest" + "Range").
- Users: no user sources were gathered this round.
- Verdict: FL's rules as they are; nothing better found. Sliders instead of the keyboard widget until the tool
  windows get one.

**Scale levels: Center and Tension**
- FL: as §5.3, Center 0 by default. Ableton: velocity range / "Ramp" in the MIDI tools, no centre.
- Users: no user sources were gathered this round. (Reasoned only: with Center 0 a multiplier below 100 % moves
  every pan value towards the left end.)
- Verdict: FL's law; LYNX starts Center on the level's centre line for pan and pitch (an option the user can move).

**Flip: Preserve start times** — FL's rule; no alternative worth offering.

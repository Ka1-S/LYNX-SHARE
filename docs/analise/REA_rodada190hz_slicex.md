# REA round 190hz (Slicex half): articulator zoom, grid and Snap; deck B's title; the keyboard's marks (#172)

Target: FL Studio 2026's `Slicex_x64.dll` (the copy analysed in round 188c, byte-identical to the installed one),
read statically: headless Ghidra on round 188c's analysed project (read-only, `p/gs.sh` + `p/gscripts/Dec2.java`,
under `~/.cache/lynx-ghidra.lock`), objdump for operand searches, the DLL's Delphi form / field / method tables. FL
shots from rounds 190hg / 190r (no new live session). Work folder: /mnt/e/lynx-work/slicex190hz (rea/e*.c decompiles,
rea/slicex.asm). Facts below are in our own words; addresses are FL's.

Tags: [DEC] decompiled code, [FORM] form resource, [SHOT] measured on an FL screenshot.

## 1. The articulator editor's time axis: zoom, range and scroller

The articulator editor is FL's shared envelope editor (`MEnvEditor: TMEnvEditor`, field +0x8e8 of Slicex's form;
graph `EnvVisual` 365 × 116; `TimeScroller` at 153, 120 in the editor, 211 wide, 17 high, with arrows, a knob that
slides and handles at its ends [FORM]).

- [DEC] Zoom: units per pixel = the scroller's page / the graph's width (TimeScrollerChange 0x84ef30; pixels per unit
  is its inverse, +0x1080). The scroller's line size is an eighth of the shown range (+0x570 = page / 8).
- [DEC] Range (0x851120, for a curve without a sample): the scroller's smallest page is the axis' base step; its
  maximum 7200 units; the curve's range is `max(last + 1, last × 1.25)`, at least 2 and at most 7200, where `last` is
  the time of the last point (+1 for an LFO curve, 0x852b80 sets the LFO kind, 0x10f4 = 1). A curve with its own
  fixed length (record field 0x68 not the "none" value 2147483647) uses that instead — Slicex's curves are not seen
  doing so (both FL shots below fit the computed range).
- [SHOT] Slicex's default volume envelope (last point at 1.0 unit) shows 2.0 units over the 365 px graph: the sustain
  point is at 183 px (shot `slicex190hg/fl/shots/m0-c.png`, graph x 353…718, S line at 536). The default LFO (one
  point) shows 2 beats, its beat line at 536 too (`lfo-c.png`).
- [DEC] Wheel (FrameMouseWheel 0x865070, over the graph or the scroller): the shown range is multiplied by
  `exp(−notches × lines × 0.075 × k)` about the mouse, Ctrl: 0.01875 instead of 0.075 (finer). `lines` (FL's wheel
  setting) and `k` are run-time values; with Windows' 3 lines and k = ln 2 a notch is 2^−0.225, exactly the menu zoom
  step measured in round 190aj (`slicex::zoomStep`). Shift / Alt + wheel also zoom the level axis (a second scroller,
  +0xe78), not built.
- [DEC] Wheel zoom about a point (0x6707d0): the anchor keeps its place; when the end passes the curve's range the
  range shifts back so it ends there.

LYNX before: the graph always fitted the whole curve. Now (exact where stated, see §5): the time parts (ENV, LFO) show
FL's range from 0 when a curve is shown (group, target, part or TEMPO change), the wheel zooms about the mouse
(2^∓0.225 per notch, Ctrl a quarter of that exponent), and a time scroller at FL's place under the graph scrolls
(drag the knob, the arrows step by an eighth) and zooms (drag an end). The maps (VEL, MOD X, MOD Y, RAND) still show
their whole curve (FL's map range not read).

## 2. The grid FL draws and snaps to

- [DEC] Axis setup by Slicex when a part is chosen (0x886f60 → 0x85be60 per axis): time axis of ENV / LFO: mode 1
  ("time"); level axis: fixed step 1/16, a thick line every 4 (1/4), its step never recomputed (axis flag off); the
  maps' x axis: the same fixed 1/16 / 4.
- [DEC] Time-axis base steps (0x8442e0): not tempo-based: step1 2.0, step2 none, base 2.0 / 10, divided by the owner's
  answer to the editor's grid question 6 — Slicex's handler for MEnvEditor (0x881cb0, set at 0x87d97b) answers 10:
  base 0.02 units (0.5 s units). Tempo-based (the curve's flag bit 0, 0x8529a0): step1 1 beat, step2 = steps per bar /
  steps per beat (the host's, FL's default 16 / 4 = 4 beats), base 1/4 beat / 10 = 0.025 beat.
- [DEC] The ruler (0x844700 with its test 0x844650): the step is n × base for the smallest n (searching n = 1, 2, …
  up to the count of base steps in the larger of step1 / step2; if even that count fails, doubling it) whose lines lie
  at least 12 px apart (8 px on the level axis) and where n divides or is divided by the base-step counts of step1 and
  of step2. The line drawn at index i (0x844ec0) is thickest when i is a multiple of step2's count, thicker on step1's
  count, thin otherwise.
- [SHOT] Check: at 182.5 px a unit this gives 0.08 units = 14.6 px for the envelope (FL: lines at 367, 381.6, 396.3 …,
  14.63 px apart, all thin) and 0.1 beat = 18.25 px for the tempo LFO (FL: 18.4 px, a thicker line at the beat,
  x 536). Level lines: 1/16 = 7.3 px with thicker ones every quarter (FL: 215, 245, 274, 303, 333 thick, three thin
  between).
- [DEC] Snap (0x84daf0, grid mode): a time is rounded to the nearest multiple of the step from the axis offset (0 for
  these curves; Delphi Round = halves to even). It applies when Snap is on, Alt inverts it (0x864fe0: Snap XOR Alt).
  Point drag (EnvVisualMouseMove 0x853c40, moving one point): the point's absolute time is snapped to the time step and
  its level to the level step (0x84d9d0, 1/16), unless Shift and Ctrl are both held. FL's manual (Slicex Articulator):
  "Snap — the control points snap to the nearest step in the timeline while dragging".
- [FORM] Buttons (GlobalPanel, right-aligned, y 136 in the editor): Freeze 73, Step 97, Snap 121, Slide 145
  (24 px apart), SnapBtn "Snap to grid", off.

LYNX now: FL's grid lines at the current zoom (thin / step1 / step2 classes), the Snap button between Freeze and Slide
(off by default; editor state like Freeze / Slide), Alt inverting it, Shift + Ctrl disabling it, point drags snapped in
time and level. `slicex::rulerStep`, `articulatorTimeAxis`, `articulatorView`, `snapToGrid`, `snapActive`,
`wheelZoomFactor` (core, tested).

## 3. The deck's Snap menu (not built)

Read (0x84daf0, the same editor class drives the decks): snap kinds +0x1110 = 0 grid, 1 regions (nearest region edge,
0 or the sample's end within 12 px; with Slide on), 2 samples (one frame), 3 zero-crossing (0x62b9e0 near the frame),
4 pitch period (0x62be20); the deck's grid comes from the same ruler with the deck's own axis (tempo-based when the
sample has a tempo between 10 and 999 BPM, 0x8529a0; Slicex's deck handler 0x8815a0 gives no subdivision); a
selection drag (0x853c40, select / move selection with kinds grid, samples, pitch only — mask 0x15) and region drags
call it. Not built because these stay unread or unseen: the menu's place and entries in Slicex's deck toolbar, the
deck's default kind, the zero-crossing and pitch-period searches, how FL's deck axis is set up for Slicex (0x87c8a0's
axis calls) and, from rounds 190ac / 190aj, region tags were not seen snapping in FL. Listed under "needs a live check".

## 4. Deck B's title and the keyboard's marks

- [DEC] Title (0x892520): the info row's NameLabel shows the current deck's sample name, or "(empty)" when that deck's
  sample has no frames. LYNX showed a generated name for an empty deck B ("Breaker (deck B)-…"); now "(empty)".
- [DEC] Keyboard (Slicex's keyboard paint override 0x898cf0, VMT 0x888648 slot 0x88): a key is lit when the deck's key
  table (+0x188 + key × 8 in the deck's 0x630-byte record) has an entry; the deck is the current one (form +0xb88) in
  display mode 0 (full) and deck A in the other display modes (form +0x1300 = display mode, CompactBtnClick 0x891960).
  One lit colour; a key that is playing gets an extra mark. LYNX lit both decks' keys in two colours; now only the
  marked deck's keys (in that deck's LYNX colour).
- [SHOT] FL's deck-B shot (empty deck B current) lights one key, the first key of deck A's set; LYNX's key map gives
  an empty deck B one entry on that same key (61), so LYNX lights the same single key (editor test `keys`, shot pair
  slicex190hz-deckB).

## 5. Exact / approximate / left out

| item | status |
|---|---|
| range shown for ENV / LFO (max(L+1, 1.25 L), 2…7200; LFO +1) | exact (law [DEC], two FL shots) |
| range reset when another curve is shown | approximate: FL sets the range's bounds on every curve change; that it also resets the page is inferred from the two shots |
| grid step law, base steps, line classes, level grid | exact ([DEC] + both shots match to the pixel) |
| tempo-based bar length | 4 beats (FL takes the host's steps per bar / beat; LYNX uses FL's default 4/4) |
| Snap button, Snap XOR Alt, Shift + Ctrl off, point time + level snapped | exact ([DEC]); new points added by right click are not snapped (FL's add branch not read) |
| wheel zoom | approximate in constants (FL's wheel lines and the factor are run-time values; the result equals FL's measured menu step) |
| time scroller | look and edge-drag zoom LYNX's; arrows step page / 8 (FL's line size) |
| maps' x range | left as LYNX had it (fit) |
| Step button, level-axis zoom (Shift / Alt + wheel), deck Snap menu | left out |
| deck B "(empty)", keyboard marks of the current deck | exact ([DEC]; the empty deck B's single lit key matches FL's shot) |
| bar numbers FL writes on a tempo-based time axis ("1" top left, 0x844ec0) | left out |

## 6. Best way?

- **FL:** as above: a fixed range per curve (last point + margin), the wheel and the scroller zoom it, the grid adapts
  to the zoom (lines ≥ 12 px apart, multiples of the base step), Snap is a toggle under the graph (off), Alt inverts
  it while dragging.
- **Ableton:** Live's envelopes snap breakpoints to the arrangement / clip grid when "Snap Automation to Grid" is on
  (Options menu, since Live 10); holding Alt (Win) / Cmd (Mac) while dragging horizontally bypasses the snap; the grid
  follows the zoom (adaptive grid) or a fixed value ([Ableton: Automation vs modulation](https://help.ableton.com/hc/en-us/articles/209070629-Automation-Vs-Modulation),
  [Live 12 manual, Automation and editing envelopes](https://www.ableton.com/en/live-manual/12/automation-and-editing-envelopes/)).
  Both: zoom-dependent grid and a modifier that flips snapping for one drag.
- **Users:** FL's own manual documents the articulator Snap as "snap to the nearest step in the timeline while
  dragging" ([Slicex Articulator](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Slicex%20Articulator.htm));
  #172's reporter asked for grid snapping; Gross Beat users note that placing points without snap is fiddly
  ([Image-Line forum](https://forum.image-line.com/viewtopic.php?p=1969428)).
- **Verdict:** FL-exact default (Snap off, Alt inverts). No better alternative found worth proposing: the two DAWs
  agree on the model. A possible later option (not built): remembering the Snap state per project, which FL does not
  appear to do (the button is editor state).

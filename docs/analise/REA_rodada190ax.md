# Round 190ax: the step of FL's "Line" and "Cell" snaps

Question (round 190ag's open point): the Playlist's "Line (follows zoom)" step did not match FL's at some zooms. FL at
87 px per bar moved a clip half a beat per Shift+Right with Line and with Cell; LYNX stepped a whole beat there.

## Sources

- FL Studio 2026 `FLEngine_x64.dll`, image base 0x400000, round 187a's Ghidra project (read-only, decompile only).
  The decompiles are in `E:\lynx-work\plkeys190ag\rea\t1` (FUN_00e5a530, FUN_00e59e90) and
  `E:\lynx-work\bugs190ax\rea\t1` (FUN_01062830, FUN_01062860, FUN_01062870, FUN_01062970).
- Live check of round 190ag (FL 2026, 96 PPQ, 4/4, 87 px per bar = 4.4 ticks per pixel): Shift+Right with snap Line
  and with snap Cell moved the clip 48 ticks per press; with Main (= Step) 24 ticks; with (none) 1 tick.
- FL 2026 installs no offline manual (`Help\` holds only the What's new page and an index that points online), so there
  is no manual text to quote from the install.

## What FL does [DECOMP]

`FUN_00e5a530 (editor, tick)` is "the snap step at this position" (the Shift+Left / Right action calls it, round
190ag). It reads the time signature in force at `tick` (FUN_01062970: a list of ranges with numerator and denominator
bytes) and calls `FUN_00e59e90 (tick, snap mode, editor+0xa68, bar ticks, beat ticks, editor)`.

- `S` = `*PTR_DAT_015e25f8` = the ticks of one step (24 at 96 PPQ).
- beat ticks = `(16 / denominator) * S` (FUN_010628b0, FUN_01062830); bar ticks = `numerator * beat` (FUN_01062880).
- Snap mode (low byte of editor+0xb44): 3 = (none): 1 tick; 4…8 = 1/6, 1/4, 1/3, 1/2 step and Step; 9…13 = 1/6, 1/4,
  1/3, 1/2 beat and Beat; 14 = Bar. Modes 0 and 1 are Line and Cell: both run the same code below, so they have the
  same step. (Which of the two is 0 was not needed. They differ where a position is rounded, outside this function.)

Modes 0 and 1, with `zoom` = the double at editor+0xb0c (pixels per tick: 87 px per bar / 384 ticks):

```
if zoom == 8.0:                       step = 1 tick            (FL's closest zoom)
min     = trunc (f * 10)              f = a float reached through editor+0x304 (see "Not proven")
stepPx  = S * zoom
beatPx  = beat * zoom
twoSteps = min <= 2 * stepPx   and not (denominator == 16 and numerator is odd)
twoBeats = min <= 2 * beatPx   and numerator is even
if   stepPx >= min:   step = S
elif twoSteps:        step = 2 * S
elif beatPx >= min:   step = beat
elif twoBeats:        step = 2 * beat
else:                 step = bar; while step * zoom < min and step <= 16 * bar: step *= 2
```

Constants (read from the file at the addresses the decompile names): `_DAT_00e5a328` = 8.0 (double), `_DAT_00e5a330` =
10.0 (float), `_DAT_00e5a338` = -0.49999997 (double; with FL's round it makes a truncation), `_DAT_00e5a340` = 2.0.

The live point agrees: at 87 px per bar a step is 5.4 px, two steps 10.9 px, so the step is two steps = 48 ticks.

## Zoom table, 4/4 (px per bar)

| px per bar | FL Line / Cell | LYNX before (`noteedit::screenStep`, 16 px) |
|---|---|---|
| 512 and more | step | 1/2 step (1/4 step from 1024) |
| 256 … 511 | step | step |
| 160 … 255 | step | beat |
| 80 … 159 | two steps (1/2 beat) | beat (from 64) |
| 40 … 79 | beat | beat from 64, else bar |
| 20 … 39 | two beats | bar |
| 10 … 19 | bar | bar from 16, else two bars |
| 5 … 9 | two bars | two bars from 8, else four |
| under 5 | four bars, eight … up to 32 | four bars … |

Other signatures: an odd numerator has no two-beat step (3/4 goes from the beat to the bar); in x/8 a beat is two steps;
x/16 with an odd numerator has neither the two-step nor the two-beat step.

## LYNX

- `noteedit::flLineStep (pixelsPerTick, numerator, denominator, minPixels = 10)` (core/NoteEdit) is the rule above.
  `PlaylistComponent::gridTicks` uses it for "Line (follows zoom)" and for the grid under "Events (edges)".
- Tests: `tests/Snap190axTests.inc` (the zoom table, other signatures, the old values where they differed).
- The `zoom == 8.0` case is left out: at 96 PPQ that is 3072 px per bar, and LYNX's Playlist stops at 800.

## Not proven / not built

- `f`, the float that scales the 10 px, was not identified (`*(float*)(*(*(editor+0x304)+0xb4)+0xc)`). The live point
  only bounds `trunc (f * 10)` to 6…10 px. LYNX uses 10 px at its own UI scale. A zoom sweep in FL around 160, 80, 40
  and 20 px per bar would pin it.
- LYNX has no "Cell" entry in its snap menus. In FL it has Line's step; the difference is how a position is rounded
  (not read here). Adding it is a menu entry in every language, so it is the coordinator's call.
- The Piano roll still uses `screenStep`. FL's function is the editor's, not the Playlist's alone, so the same rule
  may hold there; not checked live, not changed.
- The Playlist draws beat and bar lines only (beats from 8 px), so "Line" snaps to positions that have no drawn line
  at some zooms, as before. Drawing FL's lines is a UI change that needs FL shots.
- FL reads the time signature at the position; `gridTicks` uses the song's beats per bar with a quarter-note beat, as it
  did before.

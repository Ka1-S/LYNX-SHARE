# Behavioural analysis of FL Studio 2026: round 190oz (REA) — FL <= 11 step grids, and why Overcome's kicks were silent

Markers: **[DECOMP addr]** Ghidra pseudocode / listing of `FLEngine_x64.dll` (installation copy, image base 0x400000,
the analysed project of round flgap187a opened read-only); **[DATA]** the demo files; **[EXEC]** FL Studio 2026
command-line renders (`FL64 /R /Ewav`, no input) of minimal projects cut from the demo, compared with LYNX's renderwav.
Clean-room: the facts below are re-stated laws and layouts; no code or tables were copied.

## 1. The symptom

`Synth_dfr - Overcome - Sytrus.flp` (saved by FL 11.1.0): the two kick channels (Sytrus, insert 7) were silent in LYNX,
and so were most snares, claps and hats. Their patterns ("Kick Snare" used 61 times, "Kick", "Claps", "Hats"...) have
**no 0xE0 note record at all** [DATA]: the Sytrus engine was never asked to play. Not a Sytrus problem.

## 2. The file layout [DATA + DECOMP `FUN_00f46490`]

Inside a channel's events (after its 0x40, usually after its colour 0x80), FL 11 and older write, per pattern:

| Event | Payload (u16) | FL's grid row (`FUN_012d45b0` 4th argument) | Row default / max |
|---|---|---|---|
| 0x41 | pattern number (capped at 999) | selects the pattern for the following events | |
| 0x5B | `(1 << 8) \| step` = step on; bit 0x40 of the value = porta | 9 (on) | 0 / 1 |
| 0x48 | `(velocity << 8) \| step` | 1 | 100 / 128 |
| 0x49 | pan | 0 | 64 / 128 |
| 0x4D | key | 8 | 60 / 131 |
| 0x4E | fine pitch | 5 | 120 / 240 |
| 0x58 | Mod X (before 2.2.0 a law that depends on the channel type) | 3 | 128 / 255 |
| 0x57 | Mod Y (before 3.2.2: `clamp((x − 80)·128/80 + 128, 0, 255)`, before 2.2.0 `x/2 + 80` first, before 2.0.1 a log law `FUN_00f44d30`) | 4 | 128 / 255 |
| 0x5C | shift (before 2.1.0 `x·48/255`) | 7 | 0 / 48 |
| 0x44 | FL 1–3: 16-bit mask, bit i = step i on | 9 | |

Rows 2 (release, default 64) and 6 (default 0, max 14) are never written by the loader. The pattern's name (0xC1) and
colour come after the first channel's steps of that pattern. The step is the low byte (0…255); a value equal to the
row's default does not create the grid (`FUN_012d45b0`; grid = 10 rows × 2 048 bytes, `FUN_012d2f90`, defaults table at
0x015d51ac, 6 dwords per row: max, default, …).

The loader's 0x41 case writes the pattern into a local (`local_c4`) and the step events act on the channel of the last
0x40 (`local_150`) whatever the section.

## 3. The conversion at load [DECOMP `FUN_00f46490` → `FUN_010012e0`]

After the whole file is read, **if the file's version is below 11.5.0** (`DAT_016bac50 < 0xA7EC48`), FL runs for every
pattern 1…999 and every channel `FUN_010012e0(channel, pattern, pattern, 3)` (bit 0 of 3: the channel's existing notes
are kept), then frees the grid (`FUN_012d3000`). For each step that is on (row 9 bit 0), in step order, it adds a note:

- position = step × step ticks + row 7 × step ticks / 48 (integer, `FUN_01312200`; step ticks = `DAT_015d89c4`, 24 at
  96 PPQ) + on odd steps the swing offset `step ticks × swing >> 8` (`FUN_01311070`, swing = project event 0x0B);
- length 0 (a step note), flags 0x4000, the channel;
- key = row 8, fine pitch = row 5, release = 64 (constant 0x40), porta (0x10) when the step's value has bit 0x40;
- pan = row 0, velocity = row 1, Mod X = row 3, Mod Y = row 4.

The step loop runs to max(pattern +0x50, the pattern's steps from 0x11/0x12) and wraps the step index on a channel
with its own loop; the dots in the demo all lie inside (pattern 19's 29 dots up to step 56 all play in FL [EXEC]).
After the conversion FL asks "Enable Swing?" when the project's swing is above 0 (FL 11 swung only steps).

Projects from FL 11.5 on store steps as 0xE0 notes; FL 20.99 templates still carry stray 0x48 values without 0x5B —
not converted (version ≥ 11.5), no sound either way.

## 4. Verification [EXEC]

Minimal projects cut from the demo (only one playlist item kept, `scripts/onlypat.py`), FL 2026 render vs LYNX:

| Project | What | Result |
|---|---|---|
| kick2 (pattern 2) | both kicks, steps 0/4/8/12 | attack −88 dB residual; same peak (−1.27 dBFS) |
| hats19 (pattern 19) | 29 dots up to step 56, velocities 6…127 | −60 dB residual over the whole pattern |
| claps (patterns 15, 1, 3) | shifts 41–45 / 48 of a step | onsets and levels within 1 dB (noise operators: not sample-exact) |

## 5. Step notes on generators are held until the channel's next note [EXEC]

FL sends no note-off at the end of a step note (length 0) on a generator channel (channel type 2): the voice sounds
until **the channel's next note starts** (any key), and FL ends it then; the last one sounds on (to the end of the
render and beyond). Cuts of the demo with steps added (`scripts/adddots.py`): one dot on an organ (Org Classic) or a
pad (Saw Pad Dry) sounds to the end of the 269 s render (`fl/out/org42.wav`, `pad42.wav`; the pattern clip ends at
1.45 s); an organ step at C5 then one at G5 eight steps later (`org2k.wav`): C4 / C5 fall 60 dB within 0.2 s of the second
step while G4 / G5 sound on for the rest of the render. Not the clip's end, not one step. A Sytrus kick therefore
decays through its envelope's held part: FL's kick is −22 dB 150 ms after the hit where a one-step note gives −45 dB.
(Round 171e saw 3x Osc voices pile up from step notes: notes on several channels / the last notes of a loop.)
Holding them without that release made LYNX's Overcome render 3× slower (Sytrus voices end only once released).

## 6. For LYNX

- `flp::parseEvents` keeps the grids (`OldStepGrids`) and converts them as §3 for projects below 11.5.0 (the notes join
  the pattern's own, then the usual sort). Tests: `flp_old_step_grids_*` (tests/OldSteps190ozTests.inc).
- §5: an imported step note (length 0) on an FL generator channel (types 1 / 2) is `Note::held`; while it keeps its one
  step of length (`heldStep()`) the schedulers send its "on" only (no "off", no repeats; a held note before a clip's
  offset has nothing left, as FL's length 0) and `ScheduledNotes` sends its "off" (its id) on the sample of the
  channel's next "on", before it. .lynx: the pattern's `heldNotes` (places in `notes`);
  .flp export: back as length 0; Save a copy for 0.1.30 drops the marks with one line. Samplers keep one-step notes.
  Tests: `flp_import_step_notes_on_generators_are_held`, `scheduler_held_step_note_has_no_off`, --selftest
  `heldsteps190oz`.
- Checked [EXEC]: the kick cut (kick2) matches FL at −84 dB over the whole render (one-step notes: −12.6 dB); solo
  renders of other demos' held channels: Harmless (Galactic) closer to FL (median 50 ms level error 3.4 → 1.8 dB),
  BassDrum (Oxygen) and Ogun (Ogun Dream) unchanged; Ogun in Astes keeps its tails 8–10 dB above FL's below −55 dB
  (LYNX's Ogun voice handling, open).
- Not modelled: the pre-2.0.1 Mod Y law and the pre-2.2.0 Mod X laws (no such file in the corpus); the step loop's
  wrap on channels with their own loop (no old file uses one).

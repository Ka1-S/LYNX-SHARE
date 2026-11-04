# REA round 169g2: Transistor Bass knobs (discussion #56 follow-up)

Reporter (Jan12139, after reinstalling twice and rescanning): Sytrus works, but Transistor Bass "has no functionality
apart from loading the sound preset from three options (empty, factory and one more) and piano roll that does work.
You can't change any parameter such as cutoff or the other knobs… all knobs are always 0 by default and don't do
anything. It is practically as if it's just loading samples."

## 1. What the reporter sees, per version

| Item | Evidence | LYNX before | LYNX now |
|---|---|---|---|
| "All knobs are always 0 and don't do anything" | 0.1.22 run with FL 2026, fresh profile: `C:\Users\kauan\lynx-test\r169g\run-v022-fl26\shots\transistorbass-fl-100.png` | **0.1.22** (the reporter's version) shows generic, unrelated knobs (Mallet amp, Mallet decay, Material…) all at 0.000 and wired to nothing: TB's parameter table and designed page only shipped in 0.1.23 (def899e0, 89bc19f3, 50c34543) | 0.1.23+ shows FL's 42 patch controls + the 16-step sequence with the state's values |
| "Three options (empty, factory and one more)" | FL's `Data\Patches\Plugin presets\Generators\Transistor Bass` holds exactly `Empty.fst`, `Factory programs.fst`, `Sequencer off.fst` (same SHA-256 in FL 2025.1.6 and 2026) | — | unchanged: that is FL's real list; the many sounds are TB's **128 internal programs** inside each preset |
| "Just loading samples" | 0.1.23 with FL 2025's `engine_x64.dll`: "Transistor Bass: no fl channel"; no sample-only mode exists | 0.1.23 could not create TB at all with a non-2026 engine DLL (whole-file hash gate) | fixed in round 169g (de52b09f): cycles found by shape, FL 2025 renders byte-identical to FL 2026 |
| Default of a new TB | FL adds the Plugin database entry `Generators\Synth classic\Transistor Bass.fst` = `Factory programs.fst` (byte-identical) | 0.1.23 picked `Empty.fst` (folder order) | `Factory programs` (round 169l's Plugin-database default, on main); test `tb169g2_new_channel_has_fl_values` |
| Knobs with the sequencer **on** (Empty / Factory programs: every preset but "Sequencer off") | §2 | a note selects program = key; LYNX's page kept editing the saved program (e.g. 61) while the note played program 36/60…: **no knob changed what played** (probe: 42 of 42 params "differs 0") | the page follows the note's program as FL's does; every knob moves the sound (tests below) |

## 2. FL: the edit program and the follow flag **[DECOMP engine_x64.dll 4cc72470…, FL 2026]**

- State save `FUN_18000cb70` / load `FUN_18000ce40`: after the version, i32 `+0x3bf2c` (program playing), i32
  `+0x3bf30` (**edit program**), byte `+0x3bf28` (**follow**: the editor follows the notes). LYNX's parser skipped the
  last two. Presets: Empty 60/60/1, Factory programs 65/65/1, Sequencer off 60/60/0 (read from the files).
- Editor parameter set (`FUN_18000c230`, opcode 6): `FUN_18000b1f0 (this, editProgram, index, value)` — the knobs
  edit the **edit program's** patch; parameter get (opcode 5) reads it too.
- Note-on `FUN_18000d5a0`, sequencer on: program = key, patch re-applied (reset when the patch differs), then
  `if (follow) editProgram = key` — so after a note the page shows and edits the program that plays.
- Program change `FUN_18000fd30` (from `FUN_18000b1f0`): sets +0x3bf2c, re-applies the 42 patch values.

## 3. LYNX now

- `TransistorBass.cpp`: the state's edit program and follow flag are parsed; the page's values (`tbValue` /
  `tbSetValue`, sequence fields, `transistorBassProgram`) use the **edit program**; the engine's note-on sets
  editProgram = key when follow is set; live edits (`hostParam`, `seqParam`) go to the edit program and reach the
  sound when its patch is the one playing.
- `Instrument::selectedProgram()` (new virtual, −1 by default): TB returns the engine's edit program.
- `ChannelSettings` ModelPanel (TB, rack channels): a 10 Hz timer — when the engine's edit program differs from the
  state's, the state takes it (`transistorBassSelectProgram`: program and edit program, not an undo step) and every
  knob reads it again. `liveChange` accepts that program change without rebuilding the plugin (the sequence keeps
  running).
- Tests `tests/Tb169g2Tests.inc`: new channel = FL's Plugin database values (cutoff 0.159, resonance 0.934, env mod
  0.775, not zeros); every one of the 42 patch knobs changes the render with the sequencer off (effects on) and on
  (Factory programs, Empty; after the note's program is followed); live: a knob turned after the follow changes the
  sound at once without a rebuild.

## Where I stopped

- Done: diagnosis (0.1.22 page, sequencer-on edit program), FL REA (§2), fix + tests.
- Next / rerun: `LYNX_TEST=tb169g2 coord/buildslot.sh scripts/build-local.sh "/mnt/c/Program Files/Image-Line/FL Studio 2026/Data"`.

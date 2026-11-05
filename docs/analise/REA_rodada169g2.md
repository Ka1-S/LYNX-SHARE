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

## 4. Level against FL (coordinator, from REA_rodada169l §5) **[DECOMP + FL render]**

- `FUN_18000d780` (MyPlugin render): MIDI CC 7 sets `+0x3c330 = value / 127` (float of a double division); every
  sample is (engine × pan gains, delay) × that factor; the reverb runs on the block afterwards. Final.Vol is not read
  (REA_rodada57 §2.10), so FL's channel volume reaches the plugin only as CC 7 — the same host protocol as Kepler's
  (REA_rodada165g-flbeta165k §3: raw volume·128/12800 truncated, at most 127; FL's default 10000 → 100).
- LYNX before: its channel gain curve (11^v − 1)/10 on the output = 0.552 at 78 %, i.e. 3.0 dB under 100/127.
- LYNX now: the instrument recovers v from its gains and sends CC 7 = trunc(v·128) to the plugin
  (`Plugin::setMidiVolume`), no other output gain.
- Check: FL's own render `C:\Users\kauan\lynx-test\gen169l\fl\g169l.wav` (round 169l, Plugin database state, keys
  60 / 48 / 72) vs the Windows `LYNX.exe --export` of the same .flp (fresh profile, test mode;
  `C:\Users\kauan\lynx-test\r169g\g2level\lynx.wav`):

| key | FL peak / RMS dB | LYNX before | LYNX now |
|---|---|---|---|
| 60 | −7.4 / −29.3 | −9.9 / −32.1 | −6.8 / −29.0 |
| 48 | −19.0 / −33.5 | −21.7 / −36.6 | −18.6 / −33.5 |
| 72 | −15.1 / −32.6 | −18.3 / −35.8 | −15.2 / −32.7 |

  The 10 ms envelopes line up step for step (same sequencer timing). The other channels of the project render
  unchanged (≤ 3·10⁻⁵, the 16-bit LSB). Test `tb169g2_channel_volume_as_midi_volume`.

## 5. Windows check (fresh LYNX_PROFILE_DIR, LYNX_TEST_MODE=1)

| Run | Result |
|---|---|
| FL 2026, `LYNX_NO_FL=0` | new TB = Factory programs ("Program 66, sequencer on"), knobs at FL's values: `C:\Users\kauan\lynx-test\r169g\run-g2-fl26\shots\transistorbass-fl-100.png`; render OK |
| FL 2025.1.6 files (`fl25sim`), `LYNX_NO_FL=0` | same shot and WAV byte-identical to FL 2026's |
| `LYNX_NO_FL=1` | no channel ("requires FL Studio"), as designed |
| bug found | round 169g's `LYNX_NO_FL` hook treated `0` as "no FL" (any non-empty value): now only a non-empty value other than `0` |

## Where I stopped

- Done: diagnosis (0.1.22 page, sequencer-on edit program), FL REA (§2), fix + tests, level (§4), Windows check (§5).
- Next / rerun: `LYNX_TEST=tb169g2 coord/buildslot.sh scripts/build-local.sh "/mnt/c/Program Files/Image-Line/FL Studio 2026/Data"`.

# Round 168b (part B) — Loop Starter, local

FL Studio 25's Loop Starter (Channel Rack dice: pick a genre, get a loop of audio loops + drum kit channels with
generated steps, re-roll samples / steps, send to the Playlist) was parked as ⛔ in REA_rodada165f §1.5 because FL
draws its content from FL Cloud. The user decided (2026-11-02): do what can be done locally. Code:
`src/core/LoopStarter.{h,cpp}` (JUCE-free), `src/ui/LoopStarterWindow.{h,cpp}`, tests `tests/LoopStarter168bTests.inc`.

Sources (FL's own files only; no FL launch, no plugin scan):
- FL's Loop Starter logic is FL's own Python package `System/Tools/Chord progression` (Python 3.12 bytecode:
  `control_rack`, `rack`, `util_rack`, `constants_rack`, `Notes/drums`, `Notes/drum_setup`, `Notes/drum_patterns`,
  `States/sequencer`, `States/markov`; entry `stack.pyscript` → `from control_rack import *; initialise_system()`).
  Read with Python 3.12's `dis` / `marshal` (pyc magic `cb0d`) and pycdc for partial sources.
- Its configuration: `Notes/genres.lstj` (JSON, v2.0.2) and `Notes/content/<genre> presets.lstj`.
- FLEngine_x64.dll (Ghidra, project r165i, read-only): the menus, undo names, hints and the send-to-playlist code.

| Item | What FL does | FL evidence | LYNX before | LYNX now |
|---|---|---|---|---|
| Genre list | genres whose `content` has `loops` or `oneshots`, label from `tag_map[genre].label`, default `default_genre` ("edm") | genres.lstj; FLEngine `0x00dbf640` | — | FL's file read at run time (15 genres listed of 17); without FL a LYNX-own list of 8 |
| Kit channels | the template's (host `FLS`) Sampler "Drum" channels Kit 1..6, each with `oneshot_channels[Kit n].options` categories; DRUM_SELECTION_MODE `first` | genres.lstj templates; constants_rack | — | same slots and options; the category actually used is one the local library has samples of |
| Audio loop channels | 3 Audio channels Drums / Melodic / Elements with loops from FL Cloud | genres.lstj templates | — | **not done** (online content). Honest note in the window |
| Tempo | `update_pattern_tempo`: `int(rand_tri(mode=(default-min)/width, 2) * width) + min` | util_rack bytecode | — | FL-exact (`pickTempo`) |
| Random generator | toolbox `rand_int` (xoroshiro-like, state `[seed, (75*seed+74)%65537]`), `rand_uniform`, `rand_tri`; seed `time_ns()//100` | toolbox bytecode | — | `FlRand`, **bit-exact** vs FL's Python (seed 12345 → 556608163 323275282 1178251266…; tests) |
| Blank slots | `DrumKit.set_blanks`: unlocked slots by kit weight, odds `(1-w)*0.3`, ≤ 2 blank, ≥ 2 non-blank, none below `size[0]` | drums.pyc; constants_rack (DRUMS_MAX_BLANK 2, DRUMS_MIN_NON_BLANK 2, KIT_BLANK_CHANCE_MULT 0.3) | — | FL-exact (`setBlanks`) |
| Preset pick | `get_random_preset_name`: `u * sum(weights)`, first cumulative > target | util.pyc | — | FL-exact over the genre's presets with a lane for the category |
| Step tokens | rhythm `>` hit / `` ` `` rest; velocity `range(72,125,4)/128` default 7; shift `a..x` = n/24 step; octave `q r s`; note offset `0..b`; repeat STEP_PROPORTION `[1,.75,2/3,.5,.375,1/3,.25,1/6]` (note repeat) | drum_setup.pyc | — | FL-exact decoders; repeats written as notes (LYNX has no per-note repeat property) |
| Steps generation | Markov sequencer (`Drum.random_init`) morphing a prior = the picked preset (MORPH_PRIOR_WEIGHT 0.7…) | drums / States/markov | — | **LYNX-own**: the preset's own per-bar variants (`refs`) played over 4 bars, no Markov morph |
| Presets without file | 134 internal presets in drum_patterns.pyc (edm, house, techno, pop, hip hop, r&b) | drum_patterns.pyc | — | LYNX-own fallback grooves for those genres (the pyc is not read at run time) |
| Samples | FL Cloud one-shots by tag; re-roll avoids the last `same_sample_block_count.oneshot` (3) | genres.lstj; FLEngine "Replace sample" | — | local library: FL's installed `Data/Patches/Packs` classified by name (LYNX-own rules), else LYNX built-in drums; last 3 avoided |
| Channel dice menu | `-Sound`, Replace sample ("Replace the channel's sample with a similar other sample"), Find similar / Show more from this pack (Cloud), Generate steps (steps channels), Filter tags, Locked ("Don't replace the channel's sample when all samples are replaced"), `-Loop category`, `-One-shot category` | FLEngine `0x01034d90` | — | Replace sample, Generate steps, Locked, One-shot category: per row in the window and in the channel's right-click menu "Loop Starter ›". Cloud items and tags not done |
| Replace all | all LS channels, or the selected ones; locked keep their sample ("Replacing samples in all/the selected Loop Starter channels") | FLEngine `0x01044550` | — | same |
| Genre change | undo "change Loop Starter genre", re-rolls | FLEngine `0x00dbea60` | — | re-rolls samples (unlocked) + steps, picks the tempo |
| Pattern name | "Loop Starter #N", first unused N | FLEngine `0x00dc4400` | — | FL-exact |
| Steps insert | mixer track "LS Steps" (name + colour) | FLEngine `0x00dc1bd0` / `0x00dc2130` | — | shared insert named "LS Steps" (colour not set) |
| Send to Playlist | first free track over the span ("There aren't enough empty playlist tracks available."), loop channels as audio clips, pattern clip at the position, then each channel's LS status unset | FLEngine `0x00dc33a0`, `0x00dbe3a0` | — | pattern clip on the first free track at the playhead; channels leave Loop Starter; same message |
| Project | LS status per channel and genre per pattern saved | — | — | `.lynx` keys `loopStarter` (channel) and `loopStarterGenre` (pattern) |

UI: Channel Rack title bar dice button → "Loop Starter" tool window (genre, New loop, replace-all dice, Generate
steps, Send to Playlist, one row per LS channel). 34 strings in all 15 languages.

Checked on Windows (fresh profile, LYNX_TEST_MODE=1, real mouse, 2026-11-01): the rack's dice opens the window, New
loop (EDM) made "Loop Starter #1" at 128 BPM with Kit 1..6 (kick, snare, closed hat, shaker, percussion, ride) and steps
on 4 bars, the replace-all dice and Send to Playlist (clip on track 1, channels leave Loop Starter) work.
Tests: `tests/LoopStarter168bTests.inc` (8 tests; with FL Data also FL's genres.lstj and Trap presets).

## Where I stopped
- Landed as described above. Not done: FL Cloud audio loops (Drums / Melodic / Elements channels), tags filter,
  "Find similar samples" / "Show more from this pack", the Markov morph of the presets, the "LS Steps" colour, the
  remote-script commands (rerollLoopStarterLoop / Steps / Sample, setLoopStarterRootNote).

# REA round 190jv: FL residue — FL Studio Mobile racks, Piano roll value-3 tails, stepped slide notes, the audio clip menu's columns, Mixer › Trim unused tracks, one undo step per drag

Round 190jv (LYNX SHARE 0.1.30; queue items BUILD-LIST 19, BUILD-LIST 20, 15, 19 and 73). Binaries read: FL Studio 2026's
`FLEngine_x64.dll` (headless Ghidra, the 187a project, read-only, under the Ghidra lock) and FL Studio Mobile 5.0.3's
`libFLM.so` (round 190fn's extract). FL's own headless renders (`FL64.exe /R`) were made under the GUI lock, the FL lock
and the idle gate, with no screen input. The FL online manual is quoted where it says the same. Clean-room: behaviour in
our own words, addresses only, no Image-Line code, data or images shipped. Marks: **[DEC]** decompiled / static read,
**[FORM]** form resource, **[EXEC]** FL's own output, **[MANUAL]** Image-Line's manual, **[SHOT]** FL screenshot, **[HYP]**
not confirmed. Work folder: `/mnt/e/lynx-work/flresidue190jv/` (slide/ projects, FL renders and measures; rea/ raw
decompiles; notes/ helper notes and harnesses; shots/ FL | LYNX pairs).

Contents:
1. FL Studio Mobile residue (BUILD-LIST 19)
2. FL Piano roll value-3 tails (BUILD-LIST 20)
3. The audio clip menu in FL's two-column layout (queue 15)
4. Stepped slide notes on every FL native generator (queue 15)
5. One undo step per drag gesture (queue 73)
6. Mixer › Trim unused tracks (queue 19)

## 1. FL Studio Mobile residue (BUILD-LIST 19)

Clean-room: every fact below is in our words; Image-Line's code is cited by address only (earlier rounds' static
decompiles, never copied). FL renders are FL's own `/R` command-line render of projects this round wrote (no screen
input, GUI lock + FL lock + 60 s user-idle gate per render, render180d's `flrun.ps1`, which stops only the FL PID it
started). Tags: [DEC] decompile / static read, [EXEC] measured on FL's own output, [MANUAL] Image-Line's manual,
[HYP] hypothesis not proven.

Files: projects `notes/flm/flr/flp/*.flp` (generator `notes/flm/tools/mkgen_jv.py`, built from FL's render test
"MiniSynth.flp" and FL's own "FL Studio Mobile Rack.fst" / "FL Studio Mobile.fst" as rounds 190u / 190fe did), FL renders
`notes/flm/flr/fl/<name>/*.wav`, LYNX renders `notes/flm/ly/<name>.raw` (`notes/flm/bin/flpr`: LYNX's .flp import +
RackRenderer, 44.1 kHz), analysis `notes/flm/tools/cmp_fe.py`, `notes/flm/tools/pitch.py`, `scripts/flref_compare.py`.

### 1.1 A rack with several instruments (coverage `flm:instruments-0022`)

#### 1.1.1 How FLM plays it

- [DEC, rounds 190bs / 190fe, FL-hosted `FL Studio Mobile_x64.dll`] The rack's note dispatch (0x18059bfa0) hands every
  note to every generator module whose ADD1 key range holds the key; there is no "first instrument only" rule.
- [DEC, 190fe] The rack's render (0x18059d3b0) runs the modules in slot order on **one** stereo buffer: an effect
  processes what the buffer holds when its turn comes, i.e. only the instruments (and drum slots) before it. An
  instrument's wrapper (0x180799750) scales the buffer it renders into by (1 − Mix) + Post Gain · Mix.
- The .flm importer has rebuilt this since rounds 190bs (Layer: every generator a child channel with its key range) and
  190fe (rack parts: an instrument after an effect starts a new insert, each part sends into the next at the wrapper's
  factor). Round 190jv checked `importFlmProject` on the 12 demo songs and 9 templates: 16 layered racks
  (`notes/flm/gates.txt`), all already Layers.

#### 1.1.2 The same signal flow inside FL's "FL Studio Mobile Rack" generator — [EXEC]

FL hosts the same engine (the Rack generator's state is the rack chunk, FLM_FORMAT §8). FL's renders of Rack generator
projects, compared as level relations (FL's renders of different projects are not phase-coherent, 190fe §4):

| Project (one 3x Osc patch, phrase B) | What it holds | FL vs one instrument | LYNX 190jv vs one instrument |
|---|---|---|---|
| `jv_osc2` (this round) | two identical 3x Osc, full key range | **+5.73 dB** (both play every note; +6.02 if in phase) | +6.02 dB |
| `fe_osc_pg` (190fe) | a second 3x Osc on keys 0–10 (plays nothing) at Post Gain −6 dB | **−6.36 dB** (it scales the first one's sound) | −6.00 dB |
| `fe_osc_pgmix` (190fe) | the same at +6 dB, Mix 0.5 | **+3.35 dB** (law +3.51) | +3.51 dB |
| `fe_lp_then_osc` (190fe) | Filter, then the 3x Osc | **−0.11 dB** (190fe: bit-identical, the Filter hears nothing) | 0.00 dB (the Filter is left out) |
| `fe_osc_then_lp` (190fe) | 3x Osc, then the Filter | −16.15 dB | −17.25 dB (the Filter's own response, 190fn: max 1.1 dB per band) |
| `fe_osc_lp_mix` (190fe) | the Filter at Mix 0.5, +6 dB | −6.32 dB | −6.40 dB |
| `jv_split_pan` (this round) | 3x Osc keys 0–59, 3x Osc keys 60–127 hard right (Wet Pan 1) | see §1.4 | see §1.4 |

Before 190jv every one of these but `fe_osc`, `fe_osc_then_lp` and `fe_osc_lp_mix` played LYNX's stand-in synth.
(The absolute level of the 3x Osc patch is ~2.8 dB above FL's in every row, `fe_osc` included: a 3x Osc engine matter
shared with .flm files, unchanged by this round.)

#### 1.1.3 What LYNX builds (src/core/FlmImport.cpp `importFlmRackState`, src/core/FlpImport.cpp)

- `importFlmRackState` no longer refuses several instruments or an effect before an instrument. It runs the .flm import
  on the one-rack song as before and hands back: one instrument → exactly the 190fn result (tested: children and parts
  empty; the single-instrument branch of FlpImport is the same code, moved into a lambda); several → a Layer plus the
  instruments, each with the part of the rack it plays into, and the parts (inserts that send into a later part or into
  the rack's insert at the wrapper factor). Parts nothing plays into are dropped (an effect before the first instrument
  hears nothing: FL's `fe_lp_then_osc` is bit-identical to the instrument alone).
- In the .flp the channel becomes a Layer at 0 dB, centred, unpitched; each child is the rack's instrument on a copy of
  FL's channel, merged exactly as a one-instrument rack is (FL's volume, pan, pitch, porta, polyphony, key region with
  the module's on top). The rack's insert and parts are placed after FL's mixer exists (insert → FL's insert; parts →
  next part / rack insert). Layer children are linked after FL's own Layer remap (so they are not taken for FL channel
  indices), and the generator's FL origin is not kept for such a channel (Export to FL would otherwise write the Rack
  and its children twice).
- Time › Gate: a note's gate is its channel's (core/Scheduler.cpp `playedTiming`), i.e. the Layer's. A module's own
  gate (3x Osc Poly › Gate) therefore goes on the Layer when the rack's instruments share one (else listed, notes as
  written). Before this, `fe_osc_pg` rendered 7.4 dB too loud in LYNX (ungated notes); the .flm path gets the same
  rule. No shipped .flm / template has a gated instrument in a layered rack (`gates.txt`: every Layer gate 0), so no
  shipped song's import changes.

#### 1.1.4 Key ranges inside FL — [EXEC]

`jv_split_pan`: 3x Osc A on keys 0–59 (centred), 3x Osc B on keys 60–127 with Wet Pan 1 (right only). Levels per note
(0.4 s windows, dB):

| key | 60 | 48 | 55 | 36 | 43 | 67 | 72 |
|---|---|---|---|---|---|---|---|
| FL L / R | −107 / −29.9 | −25.1 / −25.1 | −29.0 / −28.9 | −26.0 / −26.0 | −29.3 / −29.3 | −44.2 / −26.2 | −44.2 / −29.4 |
| LYNX L / R | −240 / −23.5 | −22.7 / −22.7 | −26.4 / −26.4 | −22.7 / −22.7 | −26.3 / −26.3 | −240 / −20.2 | −240 / −23.9 |

Each instrument plays exactly the notes of its ADD1 range in FL as in LYNX (the −44 dB on FL's left at 67 / 72 is the
previous note's tail). Seen on the way (not changed, a 190fn law): a hard Wet Pan is ~3 dB louder on its side in LYNX
than FL's balance (LYNX's channel pan law on an .flp channel vs FLM's balance min(1, 2p)).

#### 1.1.5 Still not rebuilt in the .flp path (listed, the stand-in synth plays)

- A drum kit inside the Rack generator: FLM's own FLP export never writes one there (each kit sample becomes an FL
  channel, 190fn [DEC] 0x652974); how FL's MIDI notes reach the kit's slots inside the generator was not traced.
- An Arpeggiator or a Note Echo that is on (they act in time on FL's notes; Transpose, Scale and Randomizer are built:
  §2).

### 1.2 Note effects inside FL's Rack generator — [EXEC], built

`jv_transpose`: Transpose (Octave 0, Semitone +12) before the 3x Osc. FL's render plays every note an octave up (the
fundamental of each phrase note, FFT peak of its first 0.12 s: 60 → 523.2 Hz, 48 → 261.6, 55 → 392.0, 67 → 783.9,
72 → 1046.2 Hz, i.e. ratio 2.000 against the same project without Transpose; the two lowest notes' peaks are noisy). So
FL's notes go through the rack's note effects before its instruments, as a .flm lane's notes do [DEC 190bs, FLM_FORMAT
§7.2].

LYNX (190jv): `importFlmRackState` hands the rack's Transpose / Scale / Randomizer (switched on) to the caller as
`FlmRackChannel::noteEffects`; FlpImport runs the same `flmnotefx::Chain` over the channel's notes in every pattern
(a Scale without Snap drops what it does not play; the Randomizer draws once, listed). LYNX's import of `jv_transpose`
plays the same octave (ratio 2.000 on every note). An Arpeggiator or a Note Echo that is on still keeps the stand-in
(they act in time on FL's notes; not rendered in FL).

### 1.3 O25 — the "FL Studio Mobile" generator (not the Rack)

- [DEC: file] Its state (FL's "FL Studio Mobile.fst" in `Data/Patches/Plugin database/Generators/Misc`, the same bytes
  as `Data/System/FLStudioMobileTrack.fst`, 87 742 bytes) is `SSLF` + a **whole FLM song**: `10LF`, an older 328-byte
  HEAD (song name "newsong"), then 8 `RACK`s (the master with a Limiter (legacy), seven channels: DW Sampler
  "Varazdin" + Equalizer + Filter + Autoduck, a drum kit, DW "Guitar", GMSynth "Synth Bass", …) and 8 `CHNL`s
  (`notes/flm/tools/mkgen_jv.py` walks it). The Rack generator's state is one `RACK` only. So the layout does **not**
  match the rack importer's: it is a song (its own channels, tracks and clips) inside one FL channel.
- Corpus: no .flp under FL's `Data` (165 demo projects and the rest) or under `/mnt/e/lynx-work` uses the "FL Studio
  Mobile" generator; only plugin-database copies of the .fst exist (`notes/flm/fm-*.txt` lists).
- [EXEC] FL's /R render of FL's render test with this generator and its own preset state (`jv_mobile0`, phrase B on
  FL's piano roll) writes a 6.3 s wav with sound, but the sound does not follow FL's notes: the strongest partials of
  every note window sit at 45–56 Hz whatever the key (C4 … C5 expected 131–523 Hz), and the level alternates −13.2 /
  −16.2 dB in a 1.5 s pattern — the plugin plays its own embedded song in step with FL. FL then did not exit (its window
  came to the foreground in 34 of 300 polls; no input was sent) and flrun stopped that FL PID at 600 s; no more renders
  of this generator were made. A state cut down to the master and one 3x Osc channel (`jv_mobile1`, `notes/flm/tools/
  mkgen_jv.py`) made FL exit without a wav (twice).
- Verdict (O25 "yes when the state layout matches"): the layout does not match — the generator is an embedded FLM song
  player, not a rack fed by FL's notes. Left as LYNX's stand-in (the report line stays "FL Studio Mobile (its
  instruments are not reproduced; …)"). What it would take: owner question 1 in the NOTES.

### 1.4 Pitcher / Pitcher 2 on LYNX Tune (coverage `flm:effects-0018/0033`) — [EXEC]

Material: a held sine (3x Osc, osc 1 sine, filter open, gate / echo / arpeggiator off) in the Rack generator, phrase P =
C4 D4 E4 F4 G4 A4 B4, 0.75 s each, one per second; then the module. Allowed notes: D only (Pitch +5 / all notes for
`jv_pit_tr5`, Mix 0.5 for `jv_pit_mix`). Measured with `tools/pitch.py` (YIN, 5 ms hop) and `tools/pitsum.py`: mean
|FL − LYNX| pitch over each note's first 0.7 s (cents), the mean difference of the 90 % times, the settled pitch. LYNX
renders through LYNX Tune start after its 50 ms latency (PDC hides it in the app): aligned on the first onset.
`jv_sine` itself: FL and LYNX within 0.2 c on every note.

#### 1.4.1 Pitcher (type 25) — measured, mapping unchanged

| render (Speed) | mean \|d\| | t90 LYNX − FL | settled FL / LYNX |
|---|---|---|---|
| `jv_pit_fast` (1.0) | 11.7 c | +18.5 ms | 62.04 62.0 62.03 62.0 62.1 74.02 74.0 / 62 62 62 62 62 74 74 |
| `jv_pit_mid` (0.5) | 18.5 c | −0.7 ms | as above |
| `jv_pit_slow` (0.2) | 59.5 c | −33.5 ms | 61.95 … 73.57 74.09 / 61.88 … 73.74 73.84 |
| `jv_pit_v0` (0) | 97.2 c | −47.7 ms | note 0: t90 424 / 429 ms (τ = 340 ms, the law) |
| `jv_pit_tr5` (1.0, +5, all notes) | 22.1 c | +22.1 ms | +5.00 on every note, both |
| `jv_pit_mix` (1.0, Mix 0.5) | 29.9 c | +16.4 ms | the same two-tone readings |

- The target choice (nearest allowed note over the octaves: A4 → D5), the transpose, the mix and the Speed law
  (round 190de's one-pole, exact) hold: t90 within ±35 ms, settled within 0.1 semitone. Levels: rms LYNX − FL −0.07 …
  +0.03 dB (`jv_pit_mix` +1.04 dB).
- What remains is FLM's, not the mapping's: (a) LYNX's detector needs ~18 ms more than FLM's 800-sample AMDF at the
  "instant" Speed; (b) FLM keeps its correction through the silence between notes (each note starts from the previous
  one's shift: `jv_pit_slow` note 1 starts at 63.70 for a played 62), LYNX Tune resets on silence — a LYNX Tune DSP
  behaviour, not changed (old projects).
- Tried and rejected (plugin quality gate): LYNX Tune's range "Low male" (55–700 Hz, the lowest pitch FLM's detector
  takes) instead of "Instrument" (40–1200): 11.7 → 10.3 c (fast), 18.5 → 17.8 c (mid), but it stops correcting
  above 700 Hz where FLM corrects up to 1500 Hz; "Soprano" 7.8 / 15.0 c but nothing below 150 Hz. Low latency on:
  worse (18.9 / 23.4 c).

#### 1.4.2 Pitcher 2 (type 45) — two mapping changes (FlmEffects.cpp `pitcher2`)

1. **The target note is looked for in the sung note's own octave (C … B).** FL: with only D allowed, A4 and B4 land on
   **D4** (61.98, 61.97), not on the nearer D5 that Pitcher and LYNX Tune take. LYNX: per pitch class, LYNX Tune's note
   direction — a class with allowed notes only below it in its octave goes down, only above it goes up, both: the
   nearest (which is then the octave's own). Matches every settled note.
2. **Speed**: FL lands a target change at the 4-stage cascade's 90 % time (5.32 stage τ) + ~25 ms detection, and
   never faster than ~45 ms (note 0 of each render: Speed 0.5 → t90 55 ms, 0.25 → 140 ms, 0.1 → 444 ms, 0 → not
   reached in 0.7 s; the cascade's law of 190de's spec holds). LYNX's retune is a one-pole of τ = 400·v² ms (its t90 =
   2.30 τ, measured 2.3–2.5 τ): matched at the 90 % time, τ = 2.31 stage τ, at least 9 ms (190de matched the half-way
   time, τ = 5.30 stage τ).

Before → after (same LYNX build, the old mapping forced through the slot's parameters; `notes/flm/pit2-gate.txt`):

| render (Speed) | mean \|d\| before | after | t90 LYNX − FL before | after |
|---|---|---|---|---|
| `jv_pit2_fast` (1) | 346.0 c | **20.2 c** | −9.3 ms | +0.0 ms |
| `jv_pit2_mid` (0.5) | 344.3 c | **22.7 c** | +37.1 ms | +6.4 ms |
| `jv_pit2_slow` (0.25) | 343.9 c | **33.6 c** | +204.5 ms | +78.4 ms |
| `jv_pit2_v10` (0.1) | 354.3 c | **109.5 c** | +282.2 ms | +227.3 ms |
| `jv_pit2_v0` (0) | 431.2 c | **256.0 c** | +342.1 ms | +341.4 ms |

Nothing got worse in pitch or time; CPU unchanged (the same LYNX Tune, other parameter values). Not reproducible by a
mapping: Pitcher 2's smoothed target persists across notes (at Speed 0 FL's output drifts from 60 to 61.8 over the
whole phrase whatever is sung; at 0.1–0.5 a note whose target did not change lands at once), and the level (§4.3).

#### 1.4.3 Pitcher 2's level

FL's Pitcher 2 output on the held sine, steady part of each note, against the same render without it: **−3.02 dB with
all twelve notes allowed** (`jv_pit2_all`: nothing shifted, the pitch within 0.6 c of the input), −4.44 dB while it
shifts (formant mode on or off alike: `jv_pit2_fast`, `jv_pit2_nofmt`), −4.96 dB at Speed 0. So the module passes 1/√2
of its input, its shifter ~1.4 dB less. LYNX Tune passes at 0 dB (−0.3 dB shifting). Mapping (3): LYNX Tune's output
trim = −3.01 dB (the structural part; the shifter's own loss is left).

Plugin quality gate, all Pitcher 2 renders (`notes/flm/pit2-gate.txt`; before = round 190de's mapping forced through the
slot, after = 190jv; pitch |d| and rms LYNX − FL):

| render | |d| before → after | rms before → after |
|---|---|---|
| `jv_pit2_fast` (Speed 1) | 346.0 → 20.2 c | +3.47 → +0.72 dB |
| `jv_pit2_mid` (0.5) | 344.3 → 22.7 c | +3.50 → +0.72 dB |
| `jv_pit2_slow` (0.25) | 343.9 → 33.6 c | +3.61 → +0.75 dB |
| `jv_pit2_v10` (0.1) | 354.3 → 109.5 c | +3.88 → +0.82 dB |
| `jv_pit2_v0` (0) | 431.2 → 256.0 c | +4.31 → +1.31 dB |
| `jv_pit2_all` (all notes) | 0.6 → 0.6 c | +2.78 → −0.23 dB |
| `jv_pit2_nofmt` (formant off) | 346.2 → 20.1 c | +4.09 → +1.07 dB |

Every number closer, none worse. CPU: the same LYNX Tune with other parameter values (no change). The trim is measured at
Mix 1 (LYNX Tune trims dry and wet alike; FLM's dry path at Mix < 1 was not rendered).

#### 1.4.4 Commands (LYNX side, reproducible)

```
cd /mnt/e/lynx-work/flresidue190jv/notes/flm
tools/pool.sh 2 && tools/link.sh $PWD/tools/flpr.cpp $PWD/bin/flpr      # core objects of the dev tree + the tool
bin/flpr flr/flp/<name>.flp 9 ly/<name>.raw [index=value …]             # LYNX's .flp import + render (overrides LYNX Tune)
python3 tools/pitsum.py flr/fl/<name>/*.wav ly/<name>.raw               # pitch |d|, t90, settled
python3 tools/cmp_fe.py <names>                                         # level relations of the rack projects
python3 ~/projetos/lynx-wt/flresidue190jv/scripts/flref_compare.py pair flr/fl/<name>/*.wav ly/<name>.raw
```

FL /R renders: `tools/flrender.sh <name>…` (GUI lock, FL lock, idle gate, render180d's flrun.ps1). Note: FL's renders of
3x Osc projects are not sample-repeatable (the same project 3 days apart: +3.2 dB residual — oscillator phases), so the
rack comparisons use level relations and the Pitcher ones pitch trajectories; esr is not meaningful there.

### 1.5 The audio-clip stretch (`flm:audiotracks-0003`)

Unchanged: FLM's clip stretch is a licensed third-party library inside the DLL (190fe §5), studied black-box only; not
worked on in this round.


## 2. FL Piano roll value-3 tails (BUILD-LIST 20)

Binary: FLEngine_x64.dll of FL Studio 2026 (the 187a Ghidra project `~/ghidra-flgap187a/proj`, read-only, under
`~/.cache/lynx-ghidra.lock`). Decompiles and dumps: `/mnt/e/lynx-work/flresidue190jv/notes/pr-ghidra/{a,b,c,d,e,f,g,h}`
(scripts in `.../pr-ghidra/scripts`, wrapper `.../pr-ghidra/gh.sh`). Chord-progression tool: static disassembly of the
shipped .pyc (Python 3.12 `dis`) in `/mnt/e/lynx-work/flresidue190jv/notes/pr-dis/`.

### 2 · pianoroll-0183 View › Scale highlighting › Automatic (BUILT)

| # | Fact | Evidence |
|---|------|----------|
| A1 | The manual describes it like this: "Automatic: Detect the score scale. The Piano roll is shaded to reflect the detected scale based on the Notes you are using." Default = "No shading", Black notes = "The black Notes are shaded darker". | [MANUAL] fl-studio-online-manual/html/pianoroll_menu.htm |
| A2 | The scale menu handler `FUN_011f87e0` → `FUN_00e90670 (form, tag*256+root)`: tag −3 sets the Automatic flag `DAT_016ba822 = 1` (−2 sets Black notes `DAT_016ba823`, −1 clears the 12-entry mask `DAT_016ba814`, ≥ 0 loads `Reference scales\<name>.FSC` into the mask), then calls `FUN_00e500c0 (pr, 0, 1)` and `FUN_00ea8c00 (pr, 1)`. | [DEC] a/00e90670.c, a/011f87e0.c |
| A3 | `FUN_00ea8c00 (pr, selOnly)`: only with Automatic on, it calls `FUN_00ea8a80 (notes, selOnly)`, then `FUN_00e500c0 (pr, key, 0)`, and repaints when the key changed. It is called by the scale menu, the Note grid highlights option change (`FUN_00eba400`), and the editor's note-change paths `FUN_00e5a850` / `FUN_00e5a9d0` (selOnly 1) and `FUN_00e391b0` (selOnly = (flags & 4) == 0). So FL guesses again after the notes or the selection change. | [DEC] b/auto-00ea8c00.c, c/ea8c00-caller-*.c |
| A4 | `FUN_00ea8a80`: a 12-bin pitch-class histogram. Every note adds 0.25 (`_DAT_00ea8bf8`) at `key % 12` (`*(short*)(note+0xc)`). Length and velocity do not count. A note counts only when its +4 field is in `[DAT_0196eb20, DAT_0196eb24]`. The editor sets both to its own value `+0x3f4` via `FUN_012dea10`, so only the edited channel's notes count. If any note has the 0x80 bit of byte +0x13 (counted by `FUN_01060d60` into +0x48: the selection), only those notes count. With fewer than 3 notes counted it returns −1. | [DEC] c/00ea8a80.c, c/01060d60.c, f/tA-012dea10.c; the meaning of +4 and +0x13 bit 7 is [HYP], matching the behaviour |
| A5 | `FUN_00dd3ed0` is Krumhansl-Schmuckler. It computes the Pearson correlation of the histogram with the major profile `0x1526ad8` = 6.35 2.23 3.48 2.33 4.38 4.09 2.52 5.19 2.39 3.66 2.29 2.88 (mean 3.4825 at `0xdd41d8`) and the minor profile `0x1526b38` = 6.33 2.68 3.52 5.38 2.60 3.53 2.54 4.75 3.98 2.69 3.34 3.17 (mean 3.709167 at `0xdd41e0`), at each of the 12 rotations. Deviations are squared in single precision (`FUN_00664980 ((float)x, 2)`). It returns the argmax of the 24 values, major C…B (0…11) then minor C…B (12…23). A tie keeps the earlier value (strict `>`). | [DEC] c/00dd3ed0.c; [DATA] d/dump.txt |
| A6 | `FUN_00e500c0 (pr, key, force)`: with Automatic on and key ≥ 0, the caption is the root name + " " + `PTR_u_Major_0152d450[key / 12]` ("Major" / "Minor"). The row mask (`+0x66c`, one byte per key, 1 = dark row) is bit `(row + 24 − key) % 12` of a Delphi set: key < 12 → `0xe504ac` = {1,3,6,8,10}, else `0xe504cc` = {1,4,6,9,10}. So the light (in-scale) degrees are major 0 2 4 5 7 9 11 and minor 0 2 3 5 7 8 11 (**harmonic** minor). With key −1, or the master highlight switch `DAT_0152c358` off, the mask stays cleared: no dark row. The `FUN_00eae4c0` loader confirms the polarity: it fills the mask with 1 and clears the .fsc's own (in-scale) notes. | [DEC] c/00e500c0.c, e/00eae4c0.c; [DATA] d/dump.txt, c/ptrtable.txt |
| A7 | The caption (`+0xcac`) is appended to the keyboard / row hover hints (`FUN_00e6d580`, `FUN_00e6dd30`). | [DEC] g/cap-00e6d580.c, g/cap-00e6dd30.c (the exact hint layout was not read: not built) |
| A8 | Snap to scale (`FUN_00e67fa0`) reads the reference mask `DAT_016ba814`, not the Automatic rows. Choosing Automatic does not clear that mask, so in FL the snap keeps the last chosen reference scale. | [DEC] b/mask-00e67fa0.c, a/00e90670.c |
| A9 | The menu's static items, in DFM order: "-Root note", C…B, "-Scale", Snap to scale, Snap incoming MIDI, None/Default, Black notes, Automatic. `FUN_00eadc60` deletes everything after the item at main form +0x20d0 and appends the Reference-scale list. It ticks Default when scale = −1 and Automatic is off, Black notes when −2, and Automatic from its flag. | [DFM] rea190bc/areas/pianoroll.tsv rows 0164–0183; [DEC] b/auto-00eadc60.c |

LYNX: `src/core/ScaleDetect.h` implements A4–A6 as written: histogram, ≥ 3 notes, profiles, single-precision squares,
the first-max rule, and FL's two highlight sets. The Piano roll has it as the scale box item "Automatic" and in
View › Scale highlighting. Rows in Automatic follow FL's rule A6: out-of-key rows dark; no guess means no dark row;
Note grid highlights off means nothing. The guess is made again on the paint after a song edit, pattern, channel or
selection change (A3), with no timer [TEST pr190jv_auto_scale_*].

### 2 · pianoroll-0375 grid / event-lane splitter: already built (round 190fz)

`src/core/EventPaneSplit.h`, `PianoRollComponent::eventPaneBarArea / moveEventPaneBar / toggleEventPaneAlternate /
resetEventPane`, tests `tests/EvPane190fzTests.inc` (FL's TQuickSplitter: 10 px bar, 46 px minimum, 23 px snap zone,
aspect 0.8 default, Shift+Enter alternate size). The coverage row should be re-judged as covered. Nothing was changed.

### 2 · prtools-0003 FL's Piano roll arpeggiator tool with its pattern picker (BUILT, second slice)

Decompiles: `notes/pr-ghidra/i` (apply, loader), `j` (handlers), `k` (tables), `l`/`m` (score class), `h` (constructor).

| # | Fact | Evidence |
|---|------|----------|
| R1 | The form TPRArpForm has these controls: Pattern (FileName + OpenBtn "Select arpeggiator pattern..." + PresetBtn "Show preset list"), Pattern mode and Range mode {Normal, Flip, Alternate}, Sync {Time, Block, Chord}, Range digi-wheel 1 (max 6), Time mul wheel 1…7 (default 4), Gate wheel 0…48 (default 48), Group notes, and six level wheels PAN VEL REL MOD X MOD Y PITCH, each 0…128 (default 128). | [DFM] rea190bc/dfm/TPRARPFORM.json |
| R2 | The constructor `FUN_00df21f0` sets the folder to `Arpeggiator\` and opens `Default.fsc`. OpenBtnClick `FUN_00df3770` → file chooser → loader `FUN_00df2680`. | [DEC] h/arpdir-00df21f0.c, j/00df3770.c |
| R3 | `TimeMulWheelChange FUN_00df3930`: time mul = table `0x15280f8`[wheel] = 1/8 1/4 1/2 1 2 3 4; gate = wheel / 48.0. Both reload the pattern. `AbsBoxClick FUN_00df38b0`: sync = SyncModeSelect, pattern mode, range mode, range. Range mode is disabled while range < 2. `PanWheelChange` → strengths `+0x8e8…+0x8fc` = PAN VEL REL MODX MODY PITCH (`FUN_00df37c0`). | [DEC] j/00df3930.c, j/00df38b0.c, i/00df37c0.c; [DATA] k/dump.txt |
| R4 | Loader `FUN_00df2680`: loop = TScore length (VMT slot 0 `FUN_010620e0`: max of start + max(len,1)). Each note of colour (byte 0x13 & 0xF) 12 sets loop = its length and is dropped. Every other note: start = Round(start × tm); length = Round(length × tm × (gate if length < loop)), at least the minimum length (`PTR_DAT_015e0c60` → 4). Finally loop = Round(loop × tm), at least 1. | [DEC] i/00df2680.c, m/010620e0.c; [DATA] k/dump.txt |
| R5 | Apply `FUN_00df2bd0` (undo "arpeggiator"): the eligible notes are the editor's channel and the selection (or all), with length > 0. The score is rebuilt from them, and the other notes are added back at the end. Notes are read in time order. A note starting after the current segment start ends the segment there, flushes, and is read again. Otherwise it joins the held list, sorted by key (ascending, before equal keys). The segment end is the minimum of the held ends. `FUN_00df2b80` drops held notes that ended at or before the segment start. | [DEC] i/00df2bd0.c, i/00df2b80.c |
| R6 | Flush: cycle = (start − origin) div loop and offset = −((start − origin) mod loop), both unsigned (`FUN_00661480`). For each pattern step: held index = (key mod 12) mod count, reversed for Flip or for Alternate on odd cycles. Octave = key div 12 − 5. Range > 1 adds (cycle mod range) octaves, reversed for Flip or for Alternate when (cycle div range) is odd; not for pattern notes of colour 8. t = start + offset + step.start; the flush stops at the segment end; the length is cut to the segment end and to its start. | [DEC] i/00df2bd0.c |
| R7 | Sync: origin = 0 (Time); origin = the segment start when a note is added with nothing held (Block); origin = the segment start whenever a note is added, or when the segment end moves past a held note's end (Chord). | [DEC] i/00df2bd0.c |
| R8 | New note = the held note, plus the pattern's flags low nibble (slide), porta (byte 0x13 bit 0x10) toggled, and key clamped to 0…131. If FL's scale snap is on, the key is also snapped (`FUN_00e67fa0`). The note is "open" (0x40) when cut by the segment end or when the step length ≥ loop. Join: the first note with the same flags and channel and the same key (`FUN_01062230` = key) overlapping [t−1, t−1+len) (`FUN_01060620`). If it is open, has the same pan/vel/modX/modY bytes, and (start < t−1 or origin ≠ start), it is extended to t+len instead of a new note being added. | [DEC] i/00df2bd0.c, i/01060620.c, l/01062230.c |
| R9 | Levels `FUN_00df2ad0`: velocity target = p·h div 100; the others target = p + h − centre. Result = h + ((target − h)·s div 128), clamped to [0, max]. Max/centre table `0x15d51a8`: pan 128/64, vel 128/100, rel 128/64, modX 255/128, modY 255/128, pitch 240/120. | [DEC] i/00df2ad0.c; [DATA] k/dump.txt |
| R10 | Group notes: a source note without a group gets a new group number (counter `+0x8cc`, ≤ 0xFFFF), which its copies share. | [DEC] i/00df2bd0.c; the counter's start is [HYP]: LYNX uses the pattern's highest group + 1 |
| R11 | A last pass (with the 0x40 markers cleared) runs only when an option of the current channel (`+500` bit 0) is set: it lengthens by 1 tick every note whose end is the next note's start. | [DEC] i/00df2bd0.c; which channel option this is is [HYP], so it was **not built** |
| R12 | FL's 61 patterns (`Data/Patches/Scores/Arpeggiator`, folders 1–5 notes and Riff machine) all parse with `prtools::parseScore`. 40 of their notes use colour 8 and 8 use colour 12. | [FILES] local harness `notes/pr-harness/real.cpp` (reads the user's install only; nothing copied) |

LYNX: `src/core/FlArpeggiator.h` implements R4–R10 as written. The Arpeggiate dialog gains an "FL arpeggiator pattern"
section listing FL's patterns from the user's install (read only), with FL's controls and defaults [TEST
prarp190jv_*]. Not built: R11, FL's scale snap of the result (R8), and the PresetBtn tool-preset menu. No live FL check
was run: the expected results in the tests are hand-computed from the decompiled law (R4–R9).

### 2 · prtools-0055 / 0057 Generate mode + Analyze, prtools-0117 Learn from MIDI input (NOT BUILT)

- These are controls of FL's chord-progression tool form TPRChordToolForm: ModePageSelect "Generate mode"
  (TPaintBoxSelector), AnalyzeBtn "Generate mode: Analyze the notes found in the Piano roll" (hidden by default),
  MidiLearnCheckBox "Learn from MIDI input" ("Play a chord on your MIDI controller"), and the chord menu "Learn from
  MIDI input" [DFM prtools.tsv 0032–0127, 0153].
- The Python side (static `dis`, black box for its content) contains: `control.analyse_chord_sequence`,
  `analyse_scale`, `learn_chord` (control.py line 479), and `get_gen_mode` / `PR_NOTE_MODE`. It also contains
  `constants.GEN_MODES` = {'Replace all', 'Replace clashing', 'Keep', 'Snap', 'Replace region'} (each a dict of
  overwrite_mode / snap / remove_overlaps …; `DEFAULT_GEN_MODE = 2`, 'Keep'), and a Markov model
  (`MARKOV_ORDER = 3`, `ChordAnalyser`) [STATIC notes/pr-dis/constants.pyc.dis, control.pyc.dis].
- Why they were not built: all three act on FL's chord-tool UI (a strip of chord blocks with a selected chord, the
  Generate-mode page selector). LYNX's Chord progression dialog is LYNX's own parameter dialog with a typed progression
  and no chord blocks or selected chord. "Learn" (it replaces the selected chord) and "Analyze" (it fills the strip from
  the Piano roll's chords) have no FL-exact place to land. Which page / mode the ModePageSelect icons stand for also
  needs a live FL look. A FL-style chord-tool rebuild (chord strip + per-chord menu, see BUILD-LIST "Chord progression
  per-chord menu → 20") must come first.


## 3. The audio clip menu in FL's two-column layout (queue item 15)

- [SHOT] FL 2026's audio clip menu, `/mnt/e/lynx-work/live190ew/shots/O19-1-4-audio-clip-menu-fl2026.png` (and
  `flgap189o/shots/FL-clipmenu-*.png`): column 1 = Audio clip + Region, column 2 = Sample + Automation + Fades, nothing
  below Fade out curve. [FORM]/[DEC] facts as in `core/FlClipMenu.h` (round 190dz, EEAUMenu / EEAUMenuPopup 0x1237290).
- LYNX before: FL's two columns (since round 190dz) **plus 18 LYNX items under FL's Fades** in column 2 (round 190gi's
  "LYNX extras" section), so column 2 ran about twice as tall as FL's; and FL's "Deverb" was left out (round 190gw:
  FL 2026 only offers a module download there; details190if DE-113 lists it as the one missing item).
- LYNX now: FL's two columns exactly as FL draws them, Deverb included (it opens the clip's sample in LYNX's audio editor
  with LYNX's own Deverb tool, round 167c's DSP); the LYNX extras keep every item in a **third column** of their own
  (add, never remove).
- The menu-column code of round 190je (`g1menus190je`: FL's section band, full-width hover, column line, JUCE fix 39
  `lynxTallMenuBreakHook`) is not in wip/rollup030 yet. This round uses JUCE's own `PopupMenu::addColumnBreak` (the
  existing builder); fix 39 only splits menus that have **no** column break, so it leaves this menu alone, and 190je's
  band / hover / column-line look applies to it unchanged when that branch lands (the hook: the section headers here
  are plain `addSectionHeader` calls, which 190je's LookAndFeel draws as bands).

## 4. Stepped slide notes on every FL native generator (queue item 15)

Round 190hg measured FL's slide law on Slicex's renders: the host moves a sounding voice's Final pitch **once per tick
of the project (PPQ 96)**, in **n + 1 equal steps** for a slide note n ticks long, the first step with the slide's start
and the last at its end (REA_rodada190hg §1.1). It left the other generators on LYNX's straight glide
(`core/HostSlide.h`) until FL's own renders proved them. This round renders them.

Note: the queue item said "the Sampler and a few others already do it". Before this round only Slicex stepped. The
Sampler (and LYNX Soundfont) glided per sample on their own.

### 4.1 FL's renders [EXEC]

- Projects: FL 2026's own single-generator render tests (`Data/System/Render tests`, the 29 generator projects: 3x Osc,
  Autogun, BassDrum, BeepMap, BooBass, DirectWave, Drumaxx, Drumpad, DrumSynth Live, FL Keys, FPC, Fruit Kick, DX10,
  Granulizer, Fruity Slicer, GMS, Harmless, Harmor, MiniSynth, Ogun, Patcher (generator), PoiZone, Sakura, Sampler,
  Sawer, SimSynth, Slicex, Sytrus, Wave Traveller). Each has five copies of its first channel's notes, all at 120 BPM
  and PPQ 96 (`slide/p/make.sh`, `flpnotes.py`):
  - b: a C5 two beats long;
  - u: b + a slide note to G5 at tick 48, 24 ticks long;
  - w: b + a slide to C6 over 48 ticks, then one back to C5 over 24;
  - s: a C6 + a slide note an octave up, **2 ticks** long;
  - t: a C5 + a slide an octave up, **4 ticks** long.
- 145 headless renders `FL64.exe /R /Ewav` under the GUI + FL locks and the idle gate (`slide/p/flr.sh`); none failed.
  FL ran without a window; no input was injected.
- **Pitch per tick** (`slide/p/steptrack.py`: normalised autocorrelation, one period wide, medians in the middle of
  each tick). A stepped slide of 2 ticks sits at +400 / +800 cents in ticks 48 / 49. A straight glide reads about
  +390 / +970 through the same tracker (modelled on clean sines, the same window).
  - FL's renders of 3x Osc, DirectWave, DX10, Harmless, Harmor, MiniSynth, Ogun, Patcher, PoiZone, Sampler, SimSynth
    and Sytrus all read **+396…418 / +797…806**: stepped. The 4-tick versions read +305…333 / 522…546 / 752…784 /
    961…1023 (stepped model: 304 / 538 / 764 / 1008). No generator reads as a straight glide.
  - The drums (BassDrum, Fruit Kick, DrumSynth Live, Drumaxx, FPC) do not follow slide notes in FL: their renders with
    and without slides match LYNX's equally. Granulizer, GMS, Sawer, FL Keys, Fruity Slicer, Wave Traveller, Autogun,
    BeepMap, BooBass and Sakura can't be tracked this way (noise, grains, no steady period). For these, the waveform
    comparison in §4.3 decides.
  - Files: `slide/out-fl-steptrack.txt`, `slide/track-fl-dev-base.txt` (FL | LYNX after | LYNX before, tick by tick).

### 4.2 LYNX

- `core/HostSlide.h`: `Steps` (n ticks, a tick's share of the slide), set by the rack around `Instrument::slideTo` for a
  slide note. `start()` then makes n + 1 equal steps: the first at once, one per tick after, the target at the end.
  `advanceFirst()` is for engines that move the pitch before rendering a block. A straight glide stays as before:
  `advanceFirst == advance`, so the porta, LYNX's own engines and any project without slide notes render bit for bit
  as before.
- `RackRenderer`: `stepsSlides(kind)` lists FL's native generators. A slide note there sets the steps and splits the
  channel's render calls on the song's FL ticks: one sample after the slide's start, then every tick up to its end. So
  each engine moves its pitch exactly on the tick, whatever the device block is. Without a stepped slide nothing is
  split.
- Engines:
  - every HostSlide engine gets the steps from `start()`;
  - the 12 that move the pitch before rendering use `advanceFirst` (Morphine, Ogun, Harmless, Sawer, Slayer, Sakura /
    Drumpad / BooBass, Drumaxx, BassDrum, Sytrus, Harmor, GMS, DirectWave);
  - the Sampler (speed or shifted transposition) and LYNX Soundfont (FL's SoundFont Player) step on their own at the
    render call / control block that starts on the tick;
  - Slicex keeps its own stepped law from round 190hg.
- Not stepped, by choice (reversible): LYNX's own generators (drum kinds, LYNX Synth, Tetra, Helix), FL Studio Mobile's
  SuperSaw, hosted plugins (unchanged) and a Mono channel's porta glide (not
  measured this round: it stays LYNX's glide; [HYP] FL's host may step it the same way, an FL render of a porta channel
  decides).

### 4.3 LYNX vs FL [EXEC]

`slide/p/cmp.sh` renders every project with `renderwav`, base (rollup 4c45364e4) and this round. `slide/p/slidewin.py`
gives the esr inside each slide window, at the alignment of the plain note (before the slide the renders match: 3x Osc
−82 dB, DX10 −80, Sampler −83, Sytrus −90). Lower is closer to FL. Full table: `slide/slidewin.txt`.

| generator | u (24 t) | w (48 + 24 t) | s (2 t) | t (4 t) |
|---|---|---|---|---|
| 3x Osc | +2.0 → **−45.6** | +2.4 → **−37.8** | +2.7 → **−26.4** | +2.7 → **−36.5** |
| DX10 | +0.3 → **−46.5** | +2.9 → **−39.3** | +1.2 → **−25.2** | +4.6 → **−37.6** |
| Granulizer | −21.4 → **−85.0** | −18.4 → **−82.7** | −84.5 → −84.5 | −58.8 → **−86.0** |
| BooBass | −3.8 → **−16.3** | +0.2 → **−9.5** | +2.7 → **−9.1** | +1.6 → **−17.5** |
| Drumpad | −15.7 → **−29.2** | −20.7 → **−29.8** | −2.9 → **−23.3** | −6.7 → **−28.5** |
| Fruity Slicer | −24.9 → **−29.5** | −20.5 → **−29.4** | +1.3 → +1.8 | −19.4 → **−29.4** |
| MiniSynth / Sawer / Sakura / PoiZone / DirectWave / GMS / Sampler / Sytrus / Patcher | about +2 → −3 … −17 | | | |
| Harmless / Harmor / SimSynth / Ogun | about +2 → −2 … +3 (pitch matches tick by tick, §4.1; the waveforms are already uncorrelated in base) | | | |
| BassDrum, Fruit Kick, Slicex, Wave Traveller, DrumSynth | unchanged (no slides in FL either; Slicex already stepped) | | | |

- Pitch tick by tick (§4.1 tracker on LYNX's renders): after the change every trackable generator reads FL's steps
  within a few cents. Examples:
  - Sampler s: FL 400 / 801, LYNX 400 / 800 (before: 382 / 971);
  - Sytrus t: FL 538 / 768 / 1000, LYNX 506 / 765 / 999 (before: 675 / 979 / 476);
  - DX10 t: FL 333 / 546 / 767 / 999, LYNX 333 / 544 / 767 / 999.
- Cells that read slightly higher are all in the uncorrelated range above 0 dB. They are Autogun u / w, BeepMap w,
  Harmless s, Harmor s and Fruity Slicer s, where the waveforms already differ in base (random phases / noise). They
  are not a regression of the law.
- Slicex keeps 190hg's −62 … −64 dB (its own implementation, untouched).

## 5. One undo step per drag gesture (queue item 73, BUG)

No FL REA needed (a LYNX bug; FL keeps one history step per mouse gesture, as every editor of LYNX already intends:
`AudioEngine::editSong`'s own comment "all edits made while the same mouse click is held down ... become a single
step").

**Cause.** `AudioEngine::saveUndoStep` merged the edits of one press by asking JUCE for the *main mouse* source's last
press time while `ModifierKeys::currentModifiers` said a button was down. That works only for events that the OS
delivers through the main mouse. Every other input source missed it:
- events driven into a component (the test and shot routes, e.g. round 190jq's `--playlistshot190jq`, where the bug was
  seen: `currentModifiers` stays empty, so every move of the drag became its own step);
- a touch screen or a pen (Windows gives them their own `MouseInputSource`: the main mouse's press time never changes,
  so successive touch gestures were *merged* into one step, or a drag split per move when no button flag was set).
- Real mouse drags in the Playlist, Piano roll and Channel rack were already one step each (the heuristic matched).
- The Event editor never depended on it: it previews its drag without history and commits one step at mouseUp
  (`EventEditor::beginGesture / commitGesture`).

**Fix (`src/engine/AudioEngine.{h,cpp}`).** An explicit gesture: `beginUndoGesture()` / `endUndoGesture()` and the
RAII helper `AudioEngine::UndoGesture` (begin at the first line of mouseDown; an `end` guard at the first line of
mouseUp that closes the gesture when mouseUp *returns*, so the drop / commit edits made inside mouseUp share the step).
While a gesture is open every undoable edit after the first is merged, whatever source drives it. Safety: a gesture
that began with a real button down and whose mouseUp never arrived (the component went away, a popup took the mouse)
closes itself at the next edit made with no button down; `undo`, `redo`, history jumps and remote edits make the next
edit of an open gesture a new step (as `lastUndoPress` already did). Without a gesture the old rule applies unchanged.
Applied to `PlaylistComponent` (and its Pattern picker), `PianoRollComponent` and `ChannelRackComponent`.

**Proof.** `LYNX --undogesture190jv <report>` (`src/UndoGesture190jv.inc`): a 12-move drag in each editor (Playlist
clip move, Piano roll note move, Channel rack step paint, Event editor draw), once through the component's handlers and
once through the window peer as touch input; base (rollup 4c45364e4 + the same route) vs this round — table in
HANDOFF.md.

## 6. Mixer › "Trim unused tracks" (queue item 19)

FL Studio 2026 `FLEngine_x64.dll` (`C:\Program Files\Image-Line\FL Studio 2026`), headless Ghidra on
`~/ghidra-flgap187a/proj` (read-only, `StrRefs.java` / `DecompAt.java` / `DecompTree.java`; raw output in
`/mnt/e/lynx-work/flresidue190jv/rea/trim-g1 … trim-g5`), flag bytes read from the PE file, the FL online manual
(`mixer_mixermenu.htm`, "Layout" heading) and the FL menu shot `/mnt/e/lynx-work/live190gu/reafl/fl-mixer-options-menu.png`.

### 6 · Place and caption

- [EXEC shot] The mixer's top-left menu: **Options** section = Disk recording ›, Plugin delay compensation ›, Link all
  parameters..., View ›, Detached, *separator*, **Trim unused tracks** (no arrow: it opens a question), Switch smart
  disable for all plugins, Remove all Edison instances; then the "Mixer track (…)" sections. Not in the right-click
  track menu (`fl-mixer-track-menu.png` has no Options section).
- [MANUAL] lists it under "Layout": "Delete any unused Mixer Tracks. Options include: Unlinked tracks only – Deletes tracks
  that are completely unused: no incoming audio and no effects slots in use. These are tracks in their default,
  untouched state. Unlinked and non-empty tracks – Deletes all unused tracks, including those with effects slots in use
  but no incoming audio." (The manual's "default, untouched state" is looser than the code below.)
- [DEC] RTTI names `TrimUnusedMenuItem` / `TrimUnusedMenuItemClick` / `TrimUnusedTracksSeparator`; handler
  **`FUN_012a6920`** (references every string below).

### 6 · What the handler does (`FUN_012a6920`) [DEC]

1. **Side-routing guard** `FUN_012a6640`: walks every channel's plugin (`[015e2218]` list, `FUN_00fe8bf0`) and the 10
   slot plugins of every mixer track (record `+0x134c`); a plugin whose list at `+0x128 → +8` is non-empty (records of
   0x28 bytes, `+0x24` = a mixer track) is collected as `Channel: <name>` or `Effect plugin on mixer track "<track>" …`.
   Any hit → `Cannot trim unused tracks|Cannot trim unused mixer tracks because side routing is active on one or more
   plugins.` + the list (flags 0x1000010, OK only) and **nothing happens**. (Why: FL's renumbering leaves plugin-side
   track numbers alone — REA_rodada177a §1 — so it refuses rather than mis-route them.) [HYP] which plugins fill that
   list (Fruity Send / Patcher / multi-out connections) was not traced further.
2. **Question 1** (`FUN_009638e0 → FUN_009636c0`, flags `0x6000031`): title `Remove unused tracks`, text `Select the
   mixer track types to be removed.`, icon MB_ICONEXCLAMATION (0x30), buttons MB_OKCANCEL (0x1). Flag 0x2000000 adds
   one checkbox per option (`FUN_009654f0`, theme class `forms.msgform.checkbox:checkbox`), flag 0x4000000 makes them
   exclusive (`FUN_00964780`: the clicked one is ticked, every other unticked). Options: `Unlinked tracks only|1`
   (the `|1` suffix at `012a70b0` = ticked at start) and `Unlinked and non-empty tracks`. Result 2 (Cancel / close) →
   nothing; result `0x302` (the second option) → "non-empty too" (`local_9`).
3. **Listing** (`FUN_012c6910/50/70/30`): inserts **1 … count-2** in order (the Master 0 is never visited; the last
   record, FL's "Current" track, maps to 0x1F5 and is skipped). An insert is listed when `FUN_012a65c0` says so:
   - `FUN_0127bcc0 (t, 0, flags = 0)` (**unlinked**, flag byte at `012a6634` = 0) is true when ALL hold:
     - 0 < t < 0x1F5;
     - no channel has its output (`+600`) on t;
     - no Playlist track of any arrangement (`FUN_012f9f10/00`, 500 tracks × 0x114 bytes) is an **audio track**
       (type 1) linked to t;
     - no route x → t and no route t → x for x = 1 … 500 (`FUN_0127c6f0`: the route matrix, level-0 sidechain routes
       included; the route to the Master is not looked at);
     - (flag 2, not set here: no plugin side routing points at t; flag 1, not set here: no effect / name).
   - unlinked AND (an effect in one of the 10 slots `FUN_0127c030` OR a custom name, record `+0x0C` — the string
     `FUN_012be330` shows, "Insert %d" when empty) → listed only with "Unlinked and non-empty tracks"; unlinked and
     neither → listed in both modes.
   - Not looked at: fader, pan, stereo separation, mute/solo, colour, icon, audio input source, docking, armed state,
     automation clips / links on the track's parameters.
4. **None listed → nothing at all** (no message).
5. **Question 2**: `FUN_00c26810 (0x33)` selects message record 0x33 of the table at `0x01500260`
   (id `TrimUnusedMixerTracks`, label `Trim unused mixer tracks`, category `File/Data`, read from the PE), then
   `Remove unused tracks|This will |delete| %d unused mixer track(s). Are you sure?` with flags `0x20024`
   (MB_YESNO | MB_ICONQUESTION + FL's 0x20000 = **Remember my choice**, as Delete's record 0x32, REA_rodada186c).
6. **Yes** (6): `FUN_011ff450 (1)` (an autosave backup when FL's autosave level asks for one before risky edits),
   then **`FUN_012a5280 (list, n-1, 0x11)`** (flag byte at `012a71ac`):
   - bit 0x10: one undo step, caption `"trim unused" + " mixer tracks"` → **"trim unused mixer tracks"**;
   - bit 2 (Delete's "send the feeders on to the deleted track's destinations") is **not** set — moot anyway, an
     unlinked track has no routes;
   - the listed records are moved to the end (`FUN_012a4030`, map), the track count drops by n (`FUN_0127cee0`):
     **removed, not reset; every higher track moves down**, the mixer gets shorter;
   - references are remapped through the map (`FUN_012cb6c0 (FUN_012a3f00, map)`): channels, routes, Playlist tracks,
     automation / links of the moved tracks follow; those to a removed track become -1 (no target);
   - the selected track stays on the same number, the last one when past the end (`FUN_0128fa20`).

### 6 · LYNX (this round)

| FL | LYNX |
|---|---|
| Options section, after View / Detached + separator | Mixer menu's "Mixer" section (LYNX's place for FL's Options), after Disk recording, Plugin delay compensation, Mixer layout + separator; a separator after it |
| Question 1, two exclusive choices, first ticked, OK / Cancel, warning icon | same (`FlMessage::options`, `showFlOptions`, Dialogs.cpp) |
| Unlinked test (channels, Playlist audio links, routes in/out except to Master) | `mixerops::insertLinked` — the same, plus an instrument-track link (conservative) |
| Non-empty = effect or custom name | `mixerops::insertNonEmpty` — same ("Insert n" = default) |
| Refuses everything while a plugin has side routing | **does not refuse**: LYNX renumbers plugin-side numbers (side-chains, Fruity Send / Patcher / Transmitter / EQUO / Transporter sends, hosted routed outputs, Soundfont reverb/chorus, Voltage outputs — `remapTrackReferences`), so an insert such a reference points at just counts as linked (FL's own flag-2 test) |
| none listed → nothing | a hint "No unused mixer tracks" (LYNX) |
| Question 2 with Remember my choice (record 0x33) | same; Settings › Manage messages lists "Trim unused mixer tracks" (File/Data) |
| `FUN_012a5280` flags 0x11: removed, higher tracks renumbered, mixer shorter, refs remapped, one undo step | `mixerops::trimTracks` / `trimUnusedTracks` inside one `editSongStructural` |
| selected number kept / clamped | same |
| autosave backup before | not done (LYNX has no "backup before risky edit" level) |

Tests: `tests/Trim190jvTests.inc` (4 tests, 54 checks, run in a standalone harness linked against a lynx_core of the
same commit: 0 failures). Shots: `LYNX --trimshot190jv <folder>` (src/TrimShot190jv.inc).

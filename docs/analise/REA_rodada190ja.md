# Round 190ja — Helix, LYNX's own wavetable synth, and Live's Wavetable imported onto it (build list 17b, O13)

Evidence tags: **[DATA]** read in Live 12.4 Trial's own files on this machine (the 271 Core Library Wavetable presets,
`Defaults/Instruments/Wavetable.adv`, the names of its table files), **[SHOT]** screenshots of Live's device
(`/mnt/e/lynx-work/wavetable190ja/shots/live-*.png`), **[MEASURED]** black-box renders of Live sets built for this
round (`/mnt/e/lynx-work/wavetable190ja/sets/w1…w4-render.als`, 48 kHz float, Live's "All Individual Tracks" export),
**[MODEL]** LYNX's own choice where nothing measured decides. Clean-room: no Live code, no Live table data (Live's
`Vector_Sprite_*.wav` files were never opened: only their names were listed), no preset content in LYNX.

## 1. What the references have

### Ableton Live 12.4 — Wavetable (`InstrumentVector`)
- [DATA] Parameters (the default preset's tree; ranges from `MidiControllerRange`): two oscillators (`Voice_OscillatorN_…`:
  On, Pitch Transpose ±24, Detune ±0.5 st, Wavetables WavePosition 0…1, Effects EffectMode 0 None / 1 FM / 2 Classic /
  3 Modern, Effect1 −1…1, Effect2 0…1, Pan, Gain 0…1), the sub (On, Tone, Gain, Transpose 0 / 1 / 2 = 0 / −1 / −2 oct),
  two filters (On, Type 0 LP / 1 HP / 2 BP / 3 Notch / 4 Morph, CircuitLpHp Clean / OSR / MS2 / SMP / PRD, CircuitBpNoMo,
  Slope 12 / 24, Frequency 20 … 20480 Hz, Resonance 0 … 1.25, Drive 0 … 24 dB, Morph), routing (Serial / Parallel / Split),
  the amp envelope (times, slopes −1 … 1, sustain, loop mode None / Trigger / Loop), envelopes 2 / 3 (also Initial / Peak /
  Sustain / Final values), two LFOs (Retrigger, Shape Sine / Triangle / Saw / Rectangle / Random, Amount, Shaping, Phase
  Offset, Sync, Rate 0.01 … 30 Hz, Synced Rate 0 … 21, Attack), Time, Global Mod Amount 0 … 2, Unison Mode (None, Classic,
  Shimmer, Noise, Phase Sync, Position Spread, Random Note), Voice Count 2 … 8, Amount, Global Transpose ±48, Glide,
  Volume (linear), MonoPoly, PolyVoices 0 … 6 (= 2 … 8), HiQ.
- [DATA] The modulation matrix: `ModulationConnections` holds always 52 rows in a fixed order, each with 13 amounts
  (−1 … 1). The rows' `TargetName` changes with the effect modes ("Osc 1 FX 1" / "FM Pitch" + "FM Amt" / "PW" + "Sync" /
  "Warp" + "Fold"); the columns, settled from the 271 presets' usage and Live's tabs [SHOT live-wt-u1…u3]: Amp, Env 2,
  Env 3, LFO 1, LFO 2 (Matrix tab), Velocity, Note ("Key"), Pitch Bend, Pressure, Mod Wheel, Random (MIDI tab), Note PB,
  Slide (MPE tab, which shows Velocity and Pressure too). Live shows Pitch ← PB 1/24 as "2" and Pitch ← Note PB 1 as "48".
- [DATA] Tables: `SpriteName1/2` hold the table's name; 194 tables in 11 categories (Basics, Collection, Complex,
  Distortion, Filter, Formant, Harmonics, Instrument, Noise, Retro, Vintage), named by the files in
  `Resources/Builtin/Samples/Vector/Sprites`. A user table is in `UserSprite1/2`.
- [SHOT] The device (1000 × 190 px at 100 %): sub column, oscillator panel (category and table menus, the stacked frames
  with the playing one in orange, gain fader with pan, position slider, effect mode and its two values, Semi, Det),
  filter panel (two headers, the response, routing, Res / Frequency knobs), modulation panel (Amp / Env 2 / Env 3 /
  LFO 1 / LFO 2 with the curve and A D S R in Time / Slope / Value views; Matrix / MIDI / MPE grids), global column
  (Volume, Poly + voices, Glide, Unison mode, Voices, Amount).

### FL Studio 2026
- No wavetable synth of FL's own takes Live's tables or parameter set (Harmor / Sytrus / FLEX are other engines). Helix
  is a LYNX-own engine; it sits in the "Synth classic" folder next to Tetra.

### Users
- Wavetable is Live's most praised synth for its directness (two oscillators, one matrix, visible tables); the common
  wishes are more tables, user tables and per-voice modulation (MPE), which Helix has in its matrix.

### Verdict (best way)
- Helix keeps Wavetable's parameter set and units (so Live sets import 1 : 1), its device layout in the Clip-launch
  layout's Device View (Live users find every control where Live has it), and LYNX's look in the Pattern layout's
  channel window. Tables are LYNX's own (additive recipes); Live's table names map onto the closest one.

## 2. Laws (Live 12.4 renders, W1 128 tracks / W4 32 tracks)

| Law | Helix | Evidence |
|---|---|---|
| Level | a full-scale table at Gain 1 peaks at 0.707 per side (−3 dB) at the centre; Volume and Gain linear | [MEASURED] a0 / a1 / a2: sine −9.03 dBFS at Volume 0.5 |
| Pan | near side 0.707 (1 + √\|p\|), far side 0.707 √(1 − \|p\|) | [MEASURED] a3, a4, m11 (LFO-swept pan) |
| Detune / transpose | semitones (Detune 0.25 = 25 ct) | [MEASURED] a6, a7 |
| FM | the read phase moves by 10 × Amt radians of a sine at 2^(2 × Tune) times the note | [MEASURED] x1: exact (0.00–0.07 dB) at Amt 0.25 / 0.5 / 1, ratios 0.25 … 4 |
| Classic PW | the whole cycle squeezed into 1 − \|PW\| of it, the rest holds the cycle's end (PW < 0: the gap first); PW 1 = silence | [MEASURED] x2 pw0.5 / pw1 / pw−0.5 |
| Classic Sync | the cycle restarts 1 + 3 × Sync times per period (polyBLEP at the restart) | [MEASURED] x2: 1.75 / 2.5 / 4, exact |
| Modern Warp | phase x / (x + c (1 − x)), c = 1 / (1 + 32 w²); w < 0 mirrored | [MEASURED] x3 warp 0.5 / 1: exact |
| Modern Fold | phase folded at (1 + 7 × Fold) times its speed, mirrored about the cycle's middle (from Fold 1/7, where it equals the plain fold Helix uses below) | [MEASURED] x3 fold 0.15 / 0.25 / 0.5 / 1: exact; W4 fold 0.02 … 0.1: 6 … 9 dB apart (Live's small folds are none of the models tried) |
| Sub | a sine 0 / 1 / 2 octaves below, same level as an oscillator; Tone crossfades it into a 40 % pulse, all of it at 0.4 | [MEASURED] s1, s2, W4 s2 (harmonics 2 / 3 / 4 within 1 dB from Tone 0.25 up; 0.1 approximate) |
| Envelopes | segments x / exponential, k = 7.6 × \|slope\| (slope > 0 fast first); Trigger: A D R; Loop: A D R again while held | [MEASURED] e0 … e4 |
| Time | every envelope time × 10^Time | [MEASURED] e9: ×3.16 / ×0.32 at ±0.5 |
| LFO | sine / triangle start at 0 rising; saw falls from 0 and jumps at half the cycle; synced rates 8 bars … 1/64 (index 6 = 1 bar, 11 = 5/16, 15 = 1/8); attack = a straight fade in; phase offset in degrees | [MEASURED] l0 … l7 |
| Matrix: pitch | Pitch row: 48 st per unit; Osc Pitch: 24 st | [MEASURED] m1 |
| Matrix: sources | Velocity bipolar (2 v − 1) for every row but the level rows; Note = (key − 60) / 48; Global Mod Amount scales the envelopes and LFOs only | [MEASURED] m1, m2, m3, m8 |
| Matrix: level rows | Amp, Osc Gain, Sub Gain multiply: 1 − a (1 − u) (a < 0: 1 + a u), u = the source 0 … 1 (bipolar sources mapped to (s + 1) / 2) | [MEASURED] m4, m5, m6, m7, m10, d1 |
| Matrix: Pan rows | the knob's whole range (± 2) per unit | [MEASURED] m11, W4 p1 |
| Matrix: FX 1 rows | ± 1 per unit (Classic PW ← Velocity 0.5 at 127 = PW 0.5) | [MEASURED] W4 p2 |
| Matrix: time rows | Attack / Decay rows move the knob whose travel is ∛(t / 20 s); Release and Glide rows add amount × 20 s straight | [MEASURED] W4 t1 … t5, t7 (10.1 s at Glide ← Velocity 0.5, velocity 127) |
| Matrix: Filter Freq rows | the knob's 10 octaves per unit for every source but Note (4 octaves per unit = 1 oct / oct) | [MEASURED] W4 k1 … k4 (resonant peak of a square at C1: +0.25 → ×5.63 = 2.49 oct) |
| Filter key tracking | Filter Freq ← Note 1 = one octave per octave | [MEASURED] m2 f1 note1 |
| Unison | voices alternate hard left / right (L / R uncorrelated), 1 / √n each; Classic and Phase Sync spread ± 50 ct × Amount evenly; Shimmer wanders ± 180 ct × Amount slowly; Noise ± 70 ct fast; Random Note fixed per note | [MEASURED] u1 … u6 |
| Glide | Mono only, straight in semitones | [MEASURED] g1, g2 |
| Slide (MPE) | a note without MPE data has Slide 0 (the default preset's Pos ← Slide 0.33 does not move it) | [MEASURED] d0 |
| Filters | Tetra's measured laws (Live's filter family, REA_rodada190hs); driven circuits saturate at ±2 | [MEASURED 190hs]; W4 k7 / k8: MS2 at Drive 24 peaks at 1.93; W3 fit |

## 3. Band limiting, cost
- Every table: 64 frames, mipmaps half an octave apart (partials ≤ 1023 / 2^(m/2)), each frame ≥ 8 samples per cycle of
  its top partial; an oscillator reads the richest mip with nothing above Nyquist at its pitch × its effect's stretch.
  Aliasing (energy outside the harmonics): see HANDOFF (≤ −62 dB at C3–C4, ≤ −80 dB at C7–C8 on the saw frame).
- Tables are built on the UI / loading thread the first time they are used (~0.1 s each), never on the audio thread.

## 4. Tables and Live's names
- 73 LYNX tables in Live's 11 categories (shapes, ladders, sync / FM / feedback sweeps, vowel formants, filter sweeps,
  folds, clips, bit crush, ring, instruments' spectra, noises, retro, vintage, collection), all from recipes in
  `core/WavetableTables.cpp`.
- Matching (`/mnt/e/lynx-work/wavetable190ja/ana/match.py`): Live's output of every table at positions 0 / 0.5 / 1
  (set W2: Pos ← Velocity) against every LYNX table's partials, mean difference over 13 harmonic bands, a table of the
  same category winning ties by 1 dB. Mean distance 7.7 dB (median 7.4, 90 % ≤ 11.8): the import report names each pair.
- Export writes the Live table that maps onto the LYNX table (else "Basic Shapes"), and the LYNX names in the device's
  info text ("LYNX Helix tables: …") so a set LYNX wrote comes back with its exact tables.

## 5. Quality gate
See HANDOFF.md (scores per law group, presets, CPU per voice).

## 6. Needs a live check / open
1. Fold below 0.15: none of the phase models tried reproduces Live (6 … 9 dB apart); Helix uses the plain fold there,
   continuous with the measured law at 1/7.
2. The Time row (Time ← Velocity) and the time rows' curves beyond the values measured; the Release row's straight law
   against the Attack / Decay rows' cube-root knob is what W4 shows, not a known design.
3. Live's own tables stay Live's: imported sets sound like LYNX's nearest table (mean 7.7 dB apart).
4. .als round trip with Live in the loop: Live's W1 set → LYNX import → LYNX export → Live renders it: mean 9.94 / 10
   against Live's original renders (124 of 128 tracks ≥ 9.9; the rest are Live's own random unison modes).

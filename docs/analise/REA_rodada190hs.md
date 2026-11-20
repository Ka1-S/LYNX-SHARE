# Round 190hs: Tetra, LYNX's own 4-operator FM synth, and Live's Operator imported onto it

Owner decision O15 (build list slice #17a): a LYNX-own FM engine, with Live's Operator devices in `.als` sets mapped onto
it. Clean-room: Operator was measured **black-box** only, on Live 12.4 renders of sets this round generated (no
disassembly, no Ableton code, preset or sample in the repository; the presets of Live's Core Library were read only to
learn the parameter names and the value ranges in use). Marks: **MEASURED** = fitted on a Live render (the case names
below are the tracks of `/mnt/e/lynx-work/fm190hs/sets/f1-render.als` (164 tracks) and `f2-render.als` (78 tracks));
**DATA** = read in a saved set; **ASSUMED** = not confirmed in Live.

Implementation: `src/core/FmSynth.{h,cpp}` (`InstrumentKind::tetra`, project name `tetra`, "Tetra" in the plugin list,
Synth classic), the editor `src/ui/TetraEditor.{h,cpp}`, the import `src/core/AlsDevicesFm.cpp` (`operatorToTetra`, called
by `AlsImport.cpp` for every `Operator` device; the built-in-synth stand-in `operatorToSynth` of round 190ed is gone).
Tests: `tests/Tetra190hsTests.inc`; the old Operator checks of `AlsDevices190edTests.inc` / `190ekTests.inc` now expect
Tetra. Tool: `tools/alsrender.cpp` (an `.als` → one WAV per MIDI track, like Live's "All Individual Tracks").

## 1. Parameters

The channel keeps Operator's own values in `ChannelDef::modelParams` (`fm::stateSize` floats): per operator 15 values +
an envelope of 14 (`fm::op`, `fm::env`), then the global, LFO, pitch envelope, filter and shaper values (`fm::g`), then
each operator's 64 user partials. So the import copies what Live stores (ms, linear levels, coarse 0…48 / fine ‰, Hz,
semitones) and an automation clip moves a value linearly over its range (`tetraEngineParams`).

## 2. Laws

| Area | Law | Evidence |
|---|---|---|
| Level | a 0 dB sine carrier = Volume; carriers add | MEASURED w00 (−12.00 dBFS at Volume −12 dB), 190ei alg 10 |
| Velocity | level × ((1 − s) + s · velocity / 64)², s = Vel % | MEASURED v1001…v1127 (−72 dB at 1, +12 dB at 127), v5 (50 %, −4.95 dB at 32), 190ei (30 %, +4.56 dB at 127) |
| Key scaling | ±6 dB per octave from C3 at ±100 % | MEASURED v2, v3, j36, j84 |
| FM | phase modulation, index 4π per unit of modulator output; a modulator always has a higher number than what it feeds | MEASURED i0* (I = 12.3 × level at Tone 0.635), x2 (I = 12.56 at Tone 1); routing table of all 11 algorithms from the 190ei shots |
| Tone | the modulators' index × 1 / (1 + (f / F)^1.25), log10 F = 1.74 + 1.90 T + 2.21 T²; the heard sound is not filtered | MEASURED x0 = x1 (saw unchanged by Tone), x2, x3, i0*, i1, i2, o36 … o84 |
| Start phase | every wave starts half a cycle in; Phase = % of a cycle (Phase above 100 = 100) | MEASURED i1 (ratio-2 sidebands fit only with it), t4, t5 |
| Feedback | only on an operator nothing modulates in the algorithm (switched-off ones still count); depth 2.4 tanh (2 · Feedback) rad on its own output after level and envelope | MEASURED b010…b100 / b2 (A and B of algorithm 1: no effect), b1010…b1050, b3 / b4 |
| Waves | 0 sine, 1 / 2 sine at 16 / 256 steps, Saw n = n partials (2/π)/h of a rising ramp, Square n = n odd partials (4/π)/h, Triangle odd partials (8/π²)/h², "D" = every partial below Nyquist, 20 looped noise (pitched), 21 white noise, 22 user (64 partials, peak-normalised) | MEASURED w00…w20, o3, i4 (the saw's direction), 190ei (user wave) |
| Band limit | every additive wave is read from the table whose partials stay below Nyquist | LYNX's choice (Live's Saw D at C7 aliases less than a naive saw: w10h) |
| Ratio / fixed | coarse 0 = ½, fine ‰; fixed = Freq × 10^(Range − 4) | MEASURED t00…t1, t22…t25 |
| Envelope segments | slope 0 straight, slope 1 an exponential that is −90 dB from its end at the end, every other value holds and jumps at the segment's end | MEASURED e00…e2*, a*, d*, r* (−0.75 … 0.9 all hold) |
| End level | an operator envelope's end is the voice's end | MEASURED e32 |
| Loop modes | Loop: from the sustain straight to silence over Loop Time, then again; Beat: again every Repeat; Sync: again on the song's Repeat grid; Trigger: the note off is ignored | MEASURED e40, e41, e500 … e515, e6, e7 |
| Repeat / sync values | 1/12, 1/8, 1/6, 1/4, 1/3, 1/2, 2/3, 1, 4/3, 2, 4, 8 … beats | MEASURED indices 0, 3, 6, 7, 9; the rest ASSUMED |
| Time | envelope times × 2^(3 · Time / 100); Time < Vel: × 2^(0.7 (1 − velocity)) | MEASURED e90, e91, e8 |
| LFO | linear frequency modulation (the frequency goes through zero): f × (1 + 2 · Amount² · AmountA) | MEASURED l00, l4, l5 |
| LFO rate | low 0.0196 × 10^(0.02515 · Rate) Hz (0.02 … 30.8 Hz), high × 412, sync as the repeat values | MEASURED l1000 … l1127, l2, l20, l30 … l37 |
| LFO waves | sine, square, triangle (from its bottom), saw up, saw down, S&H, noise | MEASURED l00 … l06 (random shapes: other draws) |
| Pitch envelope | levels in semitones × Amount × Amount A | MEASURED p0 … p3 |
| Filter | TPT state-variable, 12 or 24 dB/oct; Res 0 … 1 (above 1 = 1) → damping 2 … 0.067 (12 dB) / 0.267 (24 dB); band pass −6 dB at Res 0; morph low → band → high → notch → low | MEASURED f00 … f20.75, q24, q12, q3, q4 |
| Filter key / velocity | an octave per octave around key 70 at 100 %; 0.074 octave per velocity step around 70 at 100 % | MEASURED k36 … k84, u32 … u127, f7 |
| Circuits / drive | OSR / MS2 / SMP / PRD: resonance × 0.85 / 1.08 / 0.97 / 0.88 at 0.88 × the frequency, a soft saturation; Drive acts only on them | MEASURED (approximate) f31 … f35, f4, c0 … c2 |
| Shaper | only with the filter on; Soft tanh (1.19 g x), Hard clip (4.4 g x), Sine sin (π/2 g x), output − Drive / 2 dB; 4 Bit approximate | MEASURED s1 … s5 (no effect with the filter off), h100 … h6 |
| Spread | two voices ± 10.2 cents × Spread, hard left / right, each at full level | MEASURED x4, x5 |
| Glide | only into a note played while another is held | MEASURED x8, x9 |

## 3. Quality gate (black-box A/B against Live 12 Operator)

Score per track 0…10 = 10 − 0.5 E_env − 0.25 E_spec (E_env: mean |dB| of 20 ms frame levels; E_spec: mean |dB| of
1/3-octave band levels; `/mnt/e/lynx-work/fm190hs/ana/score.py`), every track of both render sets, the LYNX side through
the real import and song renderer (`tools/alsrender`). **Before** = today's stand-in (origin/main's import: the built-in
synth when no FM is used, otherwise "unsupported"); **after** = Tetra.

| Set | Tracks | Before (stand-in) | After (Tetra) | Tracks worse than before |
|---|---:|---:|---:|---:|
| f1 (waves, FM index, algorithms, feedback, tuning, levels, envelopes, loop modes, LFO, pitch envelope, filter, shaper, globals, voices) | 164 | 3.60 | **9.42** | 0 |
| f2 (slopes, feedback, resonance, filter key / velocity / envelope / LFO, shaper with the filter on, velocity, Tone by pitch, mono, circuits, key scaling) | 78 | 2.11 | **9.18** | 0 |
| 190ei m1 (Operator tracks: the Sine preset with Vel 30 %) | 9 | 2.58 | **9.67** | 0 |

By group (f1 before → after): waves 5.4 → 9.3, FM index 6.6 → 9.6, algorithms 7.3 → 9.0, feedback 4.5 → 9.8, tuning
3.9 → 9.7, levels 2.2 → 9.8, envelopes 2.5 → 9.6, LFO 1.3 → 9.4, pitch envelope 2.8 → 9.7, filter 1.3 → 9.1, shaper
4.9 → 9.8, globals 5.4 → 9.3, voices 4.1 → 9.2. The lowest after: the looped-noise wave (3.7: LYNX's own random partials,
on purpose), Tone 0 with a 0 dB modulator (7.2), the noise LFO (7.2: other draws), resonance 0.8 at 300 Hz on a saw
(7.3), the filter envelope at +72 st (7.8). Per-track tables: `/mnt/e/lynx-work/fm190hs/lynx/score-*.txt`.

CPU (Tetra's engine, one core, −O2, 8 held 4-operator FM voices, 256-sample blocks): 0.21 % of a core per voice (0.33 % with the 24 dB filter on; the scalar path 0.24 %). The operators of a
voice run per 16-sample step, one operator after the other (a modulator's step is ready before the operator it feeds);
operators without feedback or noise take an SSE path (phases, modulation, table interpolation and level ramp four
samples at a time, sse2neon on ARM); the filter's coefficients are computed once per step. Idle: no voice, no work.

## 4. Import

`operatorToTetra` copies every value above. Listed in the import report ("Operator: … (not applied)"): pan by key /
random pan, envelope time by key, LFO rate by key, LFO amount by velocity, LFO and pitch envelope destination B, the
MIDI modulation routings, Osc < Vel; approximated: the legacy filter mode, a user wave whose Normalize is off. The wheel
range becomes the channel's pitch range.

## 5. Open (needs a live check)

- Envelope slopes other than 0 and 1 rendered as a hold-and-jump in every case tried (−1 … 0.9), although Live's Core
  Library presets hold such values (0.53 × 8 …): whether Live's own GUI draws the same needs a look at Operator's
  envelope display with such a preset.
- Repeat / LFO sync indices other than 0, 3, 6, 7, 9; LFO ranges' exact ends; the filter circuits' and 4 Bit shaper's
  shapes; LFO → filter depth; feedback above 50 %.
- The random waves (looped noise, S&H, noise LFO) are LYNX's own draws.

## 6. Round 190ix: FL's host pitch features and the control audit

- **Slide notes, channel Porta and Mono** (FL host features, not Operator's): FL's host owns each voice's pitch and
  every native generator follows it (REA_rodada166a §4, [INFERENCE]; round 168l's channel porta laws, core/ChannelPoly.h).
  Tetra now does what LYNX's other generators do: each voice keeps a `HostSlide` (core/HostSlide.h) added to its pitch
  at the modulation step; a note started by the rack's porta takes `HostSlide::pendingGlide()` and glides from the last
  note; `slideTo` moves the held voices (a slide note, a Mono channel's next note) linearly in cents over the slide
  time; released voices stay where they are. Operator's own Glide (above) is unchanged and independent of it. A voice
  started without a porta glide renders exactly as before (the 190hs scores are unchanged).
- **Control audit** (ctlwake173d, issue #77's rule): every control reached the engine; the 172 "changes nothing" were
  controls of sections a new Tetra has off (operators B…D at the floor, the LFO, the pitch envelope, the filter, the
  loop modes, Fixed) or Operator's own context rules (feedback only on an unmodulated operator, a phase's 0 % and 100 %
  being one angle, the default user wave being a sine). Tetra's contexts are hints in tools/CtlWake.h (every operator
  on, the LFO / pitch envelope / filter on, slow shaped envelopes in each loop mode, a saw user wave); the audit's
  half-cycle try of a phase now also matches "Phase".

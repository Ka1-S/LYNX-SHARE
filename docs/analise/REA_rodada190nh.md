# REA round 190nh — Kilohearts part 2 (0.1.31)

Lab, captures and logs: `/mnt/e/lynx-work/khs190nh/` (HANDOFF.md). Branch `khs2_190nh` off `khs190nb` (7b8ad5d2d).
Evidence classes as in round 190nb: [STATIC] read in Kilohearts' HeartCore 2.4.5/2.4.6 (Ghidra headless + objdump, no
execution), [MEASURED] black-box against round 190nb's 1 650 lossless references, [DATA] the plug-in's own saved state.
Clean-room: LYNX's code restates the laws as formulas (textbook designs where the plug-in uses one: the RBJ cookbook,
Chebyshev type II low / high passes, the TPT state-variable filter, Catmull-Rom and a Hann-windowed sinc); no Kilohearts
code, table or preset is copied. Where the plug-in reads a fixed table (the Frequency Shifter's 90° pair, the Ensemble's
run-time motion curves, the Nonlinear Filter's polyphase half-bands) LYNX uses its own design and says so.

## 1. Hosting

The eight laws join round 190nb's five as hidden laws of FL's Effector (core/KiloheartsDsp.h): [12] law = code / 64 —
round 190nb's five keep their k / 8 values (8, 16, 24, 32, 40: the same floats every saved slot holds), the new ones
are 41 Comb Filter, 42 Chorus, 43 Trance Gate, 44 Distortion, 45 Bitcrush, 46 Frequency Shifter, 47 Phaser, 48 Ensemble
(49 reserved: Nonlinear Filter, not built). [13 …] the law's values, Wet = the snapin's mix (the Trance Gate's depth).
The Comb Filter law reports its 7 samples of latency (Effector::latencySamples, fixed when the slot is created; every
other law and FL's Effector report 0, as before). A slot saved with an earlier stand-in keeps that stand-in.

## 2. HeartCore's sleep [MEASURED]

While a snapin's input has been silent for longer than its tail (from a reset too), HeartCore does not call its block
process; snapins with an LFO get a separate "advance by n samples" call (vtable slot 13) instead. The chorus fits it with
the tail = delay + depth (+3 samples): the LFO advanced in one step per sleeping block (its float rounding differs from
the per-sample sum) — D/stim −27 → −59 dB, Delay 100 % stereo −13 → −44 dB. Bitcrush has no slot 13: its sample-and-hold
grid stands still while asleep (the renders' first sections null at −80 … −115 dB; after a 24 000-sample gap the grid
lands ~5 samples off the model's — not resolved, see §5).

## 3. The laws [STATIC + MEASURED]

- **Comb Filter** (CombFilterDsp, slot 14 0x1802899f0): y = x(t − 7) + g · x(t − 7 − P), P = fs / cutoff (rounded to whole
  samples above 1000), read through a 16-tap sinc in a Hann window ±8 samples wide (the 7: the read's look-ahead);
  g = mix, negative with Minus; Stereo inverts the right channel's g. [MEASURED] a least-squares FIR of D/stim: dry at
  7, the tap's 16 weights as read, residual −147 dB. **10.00** on all 19 files.
- **Chorus** (ChorusDsp 0x18027fa70, reset 0x18027f6d0, advance 0x18027f9f0): a float sine LFO from 0 at a reset,
  += 2π rate / fs before each sample and wrapped once a block (floor); two taps: d = (½ sin φ + ½) depth + delay + 1/fs,
  the right at φ + spread · π, Catmull-Rom reads; out = x + ½ mix (wet − x). Three taps (multiTap): A = (½ sin φ + ½) depth
  + 1/fs, B = delay / 2 + 1/fs + (½ sin (φ + 2π/3) + ½) depth, C = delay + 1/fs + (½ sin (φ − 2π/3) + ½) depth;
  wet_L = 0.6 (B_L + A_R + (1 − spread) C_L), wet_R = 0.6 (C_R + A_L + (1 − spread) B_R); out = x + mix (wet − x).
  [MEASURED] the settle (0.5 s of silence after the reset) runs the LFO; float32 accumulation is needed for sample
  truth over minutes (double drifts by 20 dB of ESR over 17 s).
- **Trance Gate** (TranceGateDsp 0x18082fe30, envelope 0x1808302b0, reset 0x18082fd90): steps a beat 2, 3, 4, 6, 8, 12, 16
  (1/8 … 1/64); u = song beats, swing warps each beat ([0, swing) → [0, ½)), step = ⌊(⌊u⌋ + warp) · steps⌋ mod length; an
  "on" step opens the gate and restarts the attack from the envelope's value unless the step before is tied into it with
  the gate open; "off" closes it. Envelope: linear attack, decay e^(−3 t / decay) towards the sustain, release
  e^(−3 t / release) (never above the decay's curve), off below 2^-20; gain = 1 + depth (env − 1). The plug-in reads the
  host's play head every block (runs on its own while the transport stands); fxrender's `--settlesong` lets the play head
  run through the settle as fxmeasure's does (harness only).
- **Phaser** (PhaserDsp 0x1807270d0, reset 0x180726fa0, advance 0x180727060): a triangle LFO updated once a block
  (φ += rate · n / fs at the block's start, float), tri (u) = 4 (u ≤ ½ ? u : 1 − u) − 1, the right at φ + spread / 2;
  f = 40 Hz · 250^(c + depth / 2 · tri) (c: the Cutoff's own log position), ≤ 0.49 fs; "order" cookbook all-passes
  (Q 1/√2) run as TPT state-variable filters whose g = tan (π f / fs) moves linearly across the block; out =
  x + ½ mix (ap − x).
- **Distortion** (DistortionDsp 0x1803295b0, constructor 0x1803291b0, prepare 0x180329440): the snapins' IIR oversampler
  (object 0x180b823a0 / rate 0x180b825d0 / design 0x180bacef0): N = ⌈150 kHz / fs⌉ (1 … 16; 4 at 48 kHz), up = each sample
  held N times + a 10th-order Chebyshev II low pass (55 dB, stop band from 25 kHz, ≤ 0.55 fs, prewarped bilinear,
  sections kept in float), down = the same low pass + the first of every N. At the oversampled rate: an envelope
  follower (floor 1e-4, attack 4 ms, release 100 ms, coefficients of the base rate), g = 1 + dynamics (e − 1),
  v = u · drive / g + b (b = bias ± spread), y = (f (v) − f (b)) · g, out = u + mix (y − u); then the DC filter (cookbook
  30 Hz high pass). f: tanh, the quintic smoothstep, 2v / (1 + v²), and with first-order antiderivative anti-aliasing
  sin (πv / 2), clip and ⌊z⌋ + ½ (quantize: z = u · 8 / drive / g + b, scaled back by drive / 8).
- **Bitcrush** (BitcrushDsp 0x1802747a0, constructor 0x180274610, reset 0x180274700): the same oversampler at 45 dB,
  edges from the qualities (1: the new Nyquist, 0: 22 kHz, log between; ≤ min (0.499 fs, 22 kHz)); a sample-and-hold
  whose phase falls by rate / (N fs) a sample: at the crossing (f past it) the new value is the input there (linear
  between the last two samples), quantized as ⌊v L + noise⌋ / L (L = 2^(bits − 1), blended by Quantize, enabled below
  16 bits; noise uniform in ½ ± dither / 2 from the 48-bit linear congruential generator x · 0x5DEECE66D + 11 seeded
  with 1 at a reset, one draw per base sample) and the step is split at the crossing; from 44.1 kHz only the quantizer.
- **Frequency Shifter** (FrequencyShifterDsp 0x1804a6a20): a 12th-order Chebyshev II high pass (55 dB, edge max (20 Hz,
  −0.95 · shift)) run as six state-variable filters, the analytic pair, y = cos 2πφ · re − sin 2πφ · im (φ += shift / fs).
  The plug-in's 90° pair is a fixed table: LYNX uses round 190nb's own minimax pair (spectra match, ESR does not).
- **Ensemble** (UnisonDsp 0x18084dd90, prepare 0x18084dd10): up to 16 voices read one stereo line at (B − D) + 2D · m_v
  (B = ⌈0.016 fs⌉, D = 0.015 fs), each through its own all-pass (cutoff 50 … 1000 Hz), gains mix / √n · √2 · (cos θ, sin θ),
  θ = π/4 ± spread · π/4 (odd voices left, the first centred when n is odd), voices 2, 3, 6, 7 … inverted; out =
  (1 − mix) x (t − B) + Σ. The motion curves m_v are tables HeartCore builds at run time; LYNX uses its own smooth
  per-voice motions with the same speed law (8 detune² + 0.4 steps a second) and size (the voices drift ±0.5 % at 25 %).

## 4. Scores (fxparity time window, the same harness and song positions as round 190nb)

Before = round 190nb's branch (its stand-ins and laws), after = this round; bypass = the plug-in's input against its
render (round 190nb's column); all 1 648 references of the 35 licensed snapins, every column through the same scorer.
The Trance Gate's after is with fxrender `--settlesong` (the play head running through the 0.5 s settle as in the
captures; without it the law scores 8.46: only its envelope's state at the first sample differs).

| plug-in | files | bypass | before | after | up / same / down |
|---|---|---|---|---|---|
| kHs 3 Band EQ | 30 | 6.80 | 8.57 | 8.57 | 0 / 30 / 0 |
| kHs Bitcrush | 40 | 3.35 | 3.27 | 7.92 | 39 / 1 / 0 |
| kHs Channel Mixer | 25 | 5.72 | 10.00 | 10.00 | 0 / 25 / 0 |
| kHs Chorus | 60 | 2.82 | 3.90 | 8.28 | 58 / 2 / 0 |
| kHs Clipper | 25 | 5.48 | 5.96 | 5.96 | 0 / 25 / 0 |
| kHs Comb Filter | 19 | 2.95 | 3.47 | 10.00 | 18 / 1 / 0 |
| kHs Compactor | 82 | 7.01 | 7.12 | 7.12 | 0 / 82 / 0 |
| kHs Compressor | 62 | 6.65 | 7.48 | 7.48 | 0 / 62 / 0 |
| kHs Delay | 98 | 2.57 | 6.23 | 6.23 | 0 / 98 / 0 |
| kHs Distortion | 72 | 2.75 | 3.10 | 8.25 | 72 / 0 / 0 |
| kHs Dual Delay | 114 | 2.93 | 3.30 | 3.30 | 0 / 114 / 0 |
| kHs Dynamics | 106 | 7.60 | 8.59 | 8.59 | 0 / 106 / 0 |
| kHs Ensemble | 52 | 4.08 | 3.58 | 4.21 | 33 / 18 / 1 |
| kHs Filter | 33 | 2.18 | 7.39 | 7.39 | 0 / 33 / 0 |
| kHs Flanger | 90 | 3.92 | 4.32 | 4.32 | 0 / 90 / 0 |
| kHs Formant Filter | 24 | 2.75 | 4.96 | 4.96 | 0 / 24 / 0 |
| kHs Frequency Shifter | 10 | 3.20 | 3.30 | 6.40 | 9 / 1 / 0 |
| kHs Gain | 12 | 7.00 | 8.33 | 8.33 | 0 / 12 / 0 |
| kHs Gate | 73 | 7.60 | 8.01 | 8.01 | 0 / 73 / 0 |
| kHs Haas | 12 | 4.00 | 9.00 | 9.00 | 0 / 12 / 0 |
| kHs Ladder Filter | 54 | 1.37 | 8.52 | 8.52 | 0 / 54 / 0 |
| kHs Limiter | 46 | 5.74 | 6.78 | 6.78 | 0 / 46 / 0 |
| kHs Nonlinear Filter | 56 | 2.18 | 4.62 | 4.62 | 0 / 56 / 0 |
| kHs Phase Distortion | 35 | 3.60 | 4.11 | 4.11 | 0 / 35 / 0 |
| kHs Phaser | 70 | 3.11 | 3.61 | 8.34 | 68 / 2 / 0 |
| kHs Pitch Shifter | 30 | 8.17 | 8.13 | 8.13 | 0 / 30 / 0 |
| kHs Resonator | 27 | 3.33 | 9.93 | 9.93 | 0 / 27 / 0 |
| kHs Reverb | 66 | 4.21 | 4.26 | 4.26 | 0 / 66 / 0 |
| kHs Reverser | 34 | 1.85 | 7.44 | 7.44 | 0 / 34 / 0 |
| kHs Ring Mod | 30 | 2.20 | 4.43 | 4.43 | 0 / 30 / 0 |
| kHs Shaper | 20 | 4.85 | 6.85 | 6.85 | 0 / 20 / 0 |
| kHs Stereo | 20 | 5.60 | 7.75 | 7.75 | 0 / 20 / 0 |
| kHs Tape Stop | 21 | 9.52 | 9.71 | 9.71 | 0 / 21 / 0 |
| kHs Trance Gate | 50 | 1.48 | 3.94 | 9.18 | 49 / 1 / 0 |
| kHs Transient Shaper | 50 | 6.98 | 7.32 | 7.32 | 0 / 50 / 0 |
| **all** | 1648 | | 5.88 | 6.85 | 346 / 1301 / 1 |

The one file down: Ensemble D / stim 4 → 3 (+1.9 dB level on the stimulus' low tones, where the voices add up more
coherently than the plug-in's; owner decision in HANDOFF.md). Untouched plug-ins score exactly as before.

CPU (35 s stereo 48 kHz, one pinned core, best of 5, user + sys incl. file I/O; `cpu/bench.log`): Effector law 0
unchanged (0.14 … 0.19 s, ±0.02 s noise); the laws: Comb Filter 0.17, Trance Gate 0.21, Chorus 0.24, Phaser 0.24,
Frequency Shifter 0.38, Ensemble 0.51, Bitcrush 0.64, Distortion 1.22 s (the stand-ins they replace 0.16 … 0.24 s) —
the Distortion and Bitcrush laws run the plug-in's own 4× oversampling (two 10th-order filters a sample): 3.5 % and
1.8 % of a core per instance. Gold: 168 renders (round 190nb's 153 + its five laws through their translator, 3 rates),
base vs round binary: 0 differ (`gold/gold.log`).

## 5. Not done / next

- **Nonlinear Filter** (DirtyFilterDsp 0x1802fd750, designer 0x1802feba0): located, not built — a TPT state-variable
  filter whose integrators saturate by mode (nine cases), drive mapped per mode by exp2 (drive² · c), at 2× through a
  polyphase IIR half-band whose coefficients are a fixed table (LYNX would need its own half-band). Stays on Filter (4.62).
- **Ring Mod** phase carry-over: the per-render best-fit start phases (rep/ringfit.log) sit at ≈ 0 on the steady
  renders and the best phase does not lift them (D / steady −20.9 dB either way): the carry-over is not what limits it;
  the remaining error is elsewhere (its anti-aliased rectifier / oversampler). Not changed.
- **Reverser** restart after a long silence and **Shaper** curve tension: no reference exercises them (every Shaper
  reference uses the default identity curve; the one Reverser file down since round 190nb is the ms mode after 9 s of
  silence) — they need new probes in the hidden host; not changed.
- **Bitcrush** after a long silence: the sample-and-hold grid resumes ~5 samples off the frozen-grid model (its first
  sections null at −80 … −115 dB); the plug-in's wake rule is not resolved.
- Phase Distortion and Ladder use the same IIR oversampler (0x180b823a0): Phase Distortion (4.11) could take it now.

# REA round 190py-khs — the Kilohearts Flanger and Dual Delay (0.1.31)

Lab, captures and logs: `/mnt/e/lynx-work/parity_190py/khs/` (HANDOFF.md). Branch `parity_190py-khs` off
origin/wip/rollup031. Evidence classes as in rounds 190nb / 190nh / 190oc: [STATIC] read in Kilohearts' HeartCore 2.4.6
(`HeartCore.core_64`, Kilohearts' own build of 2026-11-19; Ghidra headless and objdump, no execution), [MEASURED]
black-box against round 190nb's lossless kHs references (90 Flanger, 114 Dual Delay files), [DATA] the snapins' own saved
states. Clean-room: LYNX's code restates the laws as formulas; the Flanger's up-sampler is a fixed coefficient table in
the plug-in that LYNX does not use — LYNX designs its own (§2). No Kilohearts code, table or preset is copied.

## 1. Hosting

Two more hidden laws of FL's Effector (core/KiloheartsDsp.h), the next codes after round 190oc's 49: [12] = 50 / 64
Flanger, 51 / 64 Dual Delay (`lawCode = 41 + k − 6` unchanged; round 190nb's five keep k / 8). The mix is Effector's Wet.
The translator (core/KiloheartsStates.inc) opens kHs Flanger on Effector (Type: Flanger) and kHs Dual Delay on Effector
(Type: Delay); a slot saved with the earlier stand-in (Fruity Flanger, Echoform) keeps it — no saved project changes
(gold, §5). The Flanger law reports 16 samples of latency (its up-sampler's centre, dry and wet alike, as the Comb
Filter's 7); the Dual Delay none. Encodings: Flanger [13] delay × 64 s, [14] depth × 64 s, [15] rate / 32 Hz (powers of
two: the snapin's floats come back exactly), [16] offset (v + 1) / 2, [17] motion (v + 10) / 20, [18] feedback
(v + 1) / 2, [19] spread, [20] barber-pole; Dual Delay [13] time / 16 s, [14] ratio / 4, [15] steps / 64, [16] the
denominator's index / 6 (4, 4t, 8, 8t, 16, 16t, 32), [17] sync, [18] feedback, [19] cross-talk, [20] spread, [21] tone
(v + 1) / 2, [22] duck. The Dual Delay's second pair of lines (5.2 s) is allocated only for a slot created with it
(`effector::make` now takes the law's code; every other law allocates as before).

[DATA] Flanger state: `delay`, `mod_depth`, `mod_rate`, `phase` (the Offset, −1 … 1), `scroll` (the Motion, −10 … 10),
`feedback` (−0.98 … 0.98), `spread`, `barberpole`, `mix`. Dual Delay: `delay_time`, `ratio`, `delay_numerator`,
`delay_denominator`, `sync_mode` (time / sync), `feedback`, `crosstalk`, `spread`, `tone` (−1 … 1), `duck`, `mix`.

## 2. FlangerDsp [STATIC + MEASURED]

Vtable 0x181786958: prepare (slot 7, 0x18049b1f0: four lines at 2 fs), the sleep advance (slot 13, 0x18049b680), process
(slot 14, 0x18049bb50), reset (slot 15, 0x18049b040), tail (slot 19: ⌈0.01 fs + 0.01⌉).

- **Up 2×** (0x18049fde0): a 32-sample history per channel; the even 2× sample is the input 16 samples back, the odd one a
  32-tap FIR centred between 15 and 16 — the plug-in's 32 coefficients are a fixed table: not used. LYNX's own: a
  windowed sinc at the odd half-sample positions, a Kaiser window over the 32 taps, normalised to unity at DC; β chosen
  black-box (lab/sweepfl.sh, lab/lsfit.py): β 4 → −54 dB, 5 → −63.7, **5.5 → −69.5**, 6 → −64.1, 7 → −56.6, 10 → −48.8
  on D/steady; a wider / narrower window or cutoff (16.5, 17, 15.5; 0.9 … 1.01) and least-squares designs (pass band
  0.8 … 0.95 of Nyquist, stop weight 1e-2 … 1e-6) all came out worse (−35 … −67.8). Every frame of the 2× stream runs
  the flanger; only the even one is output (no down-sampling filter).
- **Per 2× sample:** d = (delay − 1.5 / fs + depth · (½ sin 2π (φ + s_c) + ½)) · 2 fs with s = (−spread / 4, +spread / 4)
  (left, right; a ramp across the block), φ (double, turns) += rate / 2 fs, wrapped once a block; the read is Catmull-Rom
  from ⌊d⌋ + 2 samples back (clamped to what has been written; t = 1 − frac d), the line written after the read with
  x + feedback · wet. Out (even samples) = cos (mix π/4) · x + sin (mix π/4) · wet. Delay, depth, feedback and offset ramp
  linearly over the 2n samples of the block (HeartCore's ramp: a reset or a sleeping block jumps; a change under 1e-15
  holds), the mix gains over n.
- **Barber-pole:** an analytic pair (I, Q) of the 2× input (four double biquads a branch, Q one sample late — a fixed
  table again: LYNX uses its own minimax pair from round 190nb) feeds two lines a channel, A ← I + fb · w and
  B ← Q + fb · (cos ψ · B + sin ψ · A), w = cos ψ · A − sin ψ · B, ψ = 2π (θ − (offset ∓ spread / 4) / 2), θ +=
  −motion / 4 fs a 2× sample; out = dry · (sin ψ · Q + cos ψ · I) + wet · w. A barber-pole toggle clears the lines.
- **Sleep:** slot 13 advances φ (and θ) by n · rate / fs at once and sets the ramps; the tail is 10 ms (round 190nh's
  offset: asleep once the input has been silent for ⌈0.01 fs⌉ − 61 samples).

[MEASURED] D/steady −69.5 dB ESR, lag 0 (the 16 samples as the reference has them); depth 0 −115.8 dB (the even samples
are exact: the residual of every other file is the up-sampler's odd taps, 0 … 2 kHz and 20 … 22 kHz); mix 0 bit-exact.
The error grows with feedback (±0.98: −17 … −32 dB, the resonance amplifies the up-sampler's error); the barber-pole
renders (2) −5 dB with a 5-sample lag (the 90° pairs differ), score 7.

## 3. DualdelayDsp [STATIC + MEASURED]

Vtable 0x18160f4e8: prepare (slot 7, 0x18033ae90), process (slot 14, 0x18033b000), reset (slot 15, 0x18033ac70), tail
(slot 19, 0x18033adc0: max (T1, T2) + 64).

- **Times:** T1 = round (time · fs), T2 = round (ratio · time · fs) (float products; synced: time = steps · beats / (bpm /
  60), beats 1, 2/3, 1/2, 1/3, 1/4, 1/6, 1/8 for 4 … 32); read before the write at min (T, written) samples back, so each
  echo comes T + 1 samples late (round 190nb's 9 601 at 200 ms). A change of either time crossfades the old reads into
  the new ones over round (0.05 fs) samples (weight (3 − 2f) f², f the share left); a reset jumps.
- **Feedback with cross-talk:** u1 = x + fb c · a1 − fb s · a2, u2 = x + fb s · a1 + fb c · a2 (c, s = cos, sin of
  cross-talk · π/4; a rotation, ramped across the block), each through a tone shelf into its line.
- **Tone:** a TPT state-variable filter (k = 2) at min (0.4999 fs, 800 Hz), in double: tone ≥ 0 scales the low pass,
  tone < 0 the high pass, by g = 2^(∓0.1661 · 48 · T̄ · tone) = 10^(−48 dB · T̄ · |tone| / 20) (T̄ the mean of the two times
  in seconds, crossfaded like them: 48 dB per second of echo), the band pass by 0.5858 g + 1.4142 (g = 1: the identity).
- **Out:** wet_L = √2 (sin β · a1_L + cos β · a2_L), wet_R = √2 (cos β · a1_R + sin β · a2_R), β = π/4 (1 + spread); the
  block's spread takes effect a block later. Ducking: e += k (p / e − e) (p = max (½ (x_L² + x_R²), 2^-20), from e =
  0.005: a Newton step towards the RMS), k = ½ (1 − 2^(−1 / (fs τ))); gain = clamp (1 − 10 duck² · e, 0, 1) (ramped);
  out = (1 − mix) · x + mix · gain · wet (linear, round 190nb's measurement).
- **Sleep:** HeartCore's tail check plus the snapin's own flag — a block whose wet reaches 2^-20 marks itself as sounding,
  so the repeats ring on through the silence between them; no advance call.

[MEASURED] τ = 50 ms (Duck 1.0 ir: τ 10 ms −40.8, 30 ms −45.5, **50 ms −84.3**, 200 ms −38.2, 500 ms −35.9 dB). The
first draft took the sleep from the input alone and fell asleep after the second echo (D/ir −10.5 dB); with the flag:
−169 dB. Every one of the 114 files ≤ −84 dB (most −130 … −175).

## 4. UnisonDsp again — the Ensemble's own voice curves [STATIC + MEASURED]

Round 190nh built law 48 from UnisonDsp's structure with LYNX's own voice motions (the plug-in's curves are made at
start-up). This round read the maker (0x18084d110, called from the static initializer at 0x18084e940; the curves live in
a global array of 3 motions × 16 voices × 2 curves × 1028 floats) and restates it as law **52** (`Law::ensemble2`); the
translator now opens kHs Ensemble on 52, and a slot saved with 48 keeps 48 (its code is unchanged).

- **The generator** (0x1800aefe0 seed, 0x1800af200 next): the 48-bit linear congruential generator a · 0x5DEECE66D + 11
  mod 2^48 (the POSIX drand48 recurrence), seeded with 123124439 (0x756bad7) without scrambling; a draw is bits 24 … 47 as
  an int × 2^-24 (uniform [0, 1)). Per voice, per curve (delay, then cutoff), per motion: random — 1024 draws, three
  passes of a circular two-point average (to a scratch array and back), then scaled to exactly 0 … 1 by the minimum and
  maximum; symmetric — the same, except that the odd voices' delay curves are 1 − the voice before's (no draws); sine —
  ½ sin 2π (k · 100 / 1024 + 0.618034 · (16 · curve + voice)) + ½. Four guard points repeat the first four. LYNX
  generates the curves the same way at first use (no table of the plug-in is stored; the seed is a single number).
- **The line:** the input goes up 2× through the same 32-tap structure as the Flanger's (LYNX's own Kaiser taps, §2) into
  one stereo line; the dry is read at 2 B + 1 2× samples as the plug-in indexes its line, the voices by Catmull-Rom at
  2 d_v (t = frac, the weights of §2), B = ⌈0.016 fs⌉ (769 at 48 kHz: the product is 768.00006), D = 0.015 fs.
  [MEASURED] the mix-0 renders are the input 784 samples late (16 + B − 1): LYNX's line indexes the same reads two 2×
  samples nearer, the voices likewise (a 2× sample either way: −22.8 dB instead of −29.6 on the first 4 s).
- **Per block:** the voice gains (mix / √n · √2 · (cos θ, sin θ), θ = π/4 ± spread · π/4 from a two-entry table by the
  voice's parity, the first voice centred when n is odd; voices 2, 3, 6, 7 … inverted) and each voice's all-pass
  cutoff from its cutoff curve at w (Catmull-Rom, e^(2.9957 c + 3.9120), ≤ 0.4999 fs, g = tan (π f / fs)) ramp across the
  block; the motion speed (8 detune² + 0.4) / 1024 fs ramps too; w += n / 128 fs.
- **Per sample:** u += speed; each voice's delay curve at u (Catmull-Rom between points ⌊u⌋ + 1 and + 2), the read, a TPT
  all-pass (k = √2, in double), the gains; out = dry gain · dry + Σ.

[MEASURED] Until a render's first long silence the voices match at −27 … −40 dB a second (the waveform: env 0.98 … 1.0,
spectrum within 0.1 dB); after it they drift apart: HeartCore's sleep onset (the curves stand still while asleep; no
advance call) decides where the voices resume, and no single tail fits every reference — B + D + 19 (1 508 samples at
48 kHz) … 2 (B + D) + 3 (2 984) were tried; 37.5 ms (1 800) is the best compromise (W_0_Voices_0.000 / steady −10.2 →
−16.1 dB, D / stereo −0.3 → −2.7 dB, D / stim −7.4 → −7.5 dB). Not resolved: what the plug-in does on waking.

## 5. Results and gates

Scores: fxparity's time-window scorer (round 190lg's, as rounds 190nb / 190nh / 190oc), round 190nb's references and song
positions; before = origin/wip/rollup031's fxrender (Fruity Flanger, Echoform's dual, law 48), after = this round's
(runs/base, runs/r2 in the lab folder; rep/table_final.md).

| plug-in | files | bypass | before | after | up / same / down |
|---|---|---|---|---|---|
| kHs Dual Delay | 114 | 2.93 | 3.30 | **10.00** | 112 / 2 / 0 |
| kHs Ensemble | 52 | 4.08 | 4.21 | **5.79** | 27 / 25 / 0 |
| kHs Flanger | 90 | 3.92 | 4.32 | **8.96** | 88 / 2 / 0 |
| **all three** | 256 | | 3.84 | 8.78 | 227 / 29 / 0 |

No file goes down. Flanger: 70 × 9, 14 × 10, 3 × 7 (feedback +0.98, barber-pole), 3 × 5 (feedback −0.98, +0.98 stereo).
Dual Delay: every file 10 (ESR −84 … −175 dB). Ensemble: 2 × 10, 2 × 8, 23 × 7, 2 × 6, 23 × 4 (the renders that sleep
in a long silence and come back; §4) — D / stim, round 190nh's one file down, 3 → 6.

- **Gold** (gold/khsgold.sh, base vs round fxrender, 44.1 / 48 / 96 kHz): **210 renders, 0 differ** — Facet via Pro-Q, the
  22 stand-ins' defaults (Fruity Flanger and Echoform included), Effector's 12 types × 2 settings, round 190nb's five,
  round 190nh's seven other laws and round 190oc's law 49 through their translator, and law 48 set directly (3 motions
  × 2 settings: a slot saved with it renders as before).
- **CPU** (cpu/khsbench_r2.log; 35 s stereo 48 kHz, best of 5, one pinned core, user + sys incl. file I/O; exact
  instructions per stereo sample from callgrind on the lab harness): Flanger 0.48 s (stand-in 0.20; 1 082 instr.),
  barber-pole 0.66 s (2 105); Dual Delay 0.26 … 0.29 s (Echoform 0.51 … 0.53; 455); Ensemble 6 voices 0.73 s (law 48
  0.48; 2 251 vs 1 028), 16 voices 1.41 s (0.97; 4 946 vs 2 652) — about 4 % of a core at 16 voices, as round 190oc's
  Nonlinear Filter (1.41 s) and round 190nh's Distortion (1.26 s here).
- **Save a copy for 0.1.30** (engine/ProjectDowngrade.cpp): codes 50 and 51 are cut like 41 … 49 (Effector plays its
  Flanger / Delay type) with their own loss line (one new string, 14 tables, lint clean); 52 joins round 190nh's line
  (its text already names the Ensemble stand-in).
- **Tests:** tests/Parity190pyKhsTests.inc (5: codes and translation, the Flanger's dry / wet through the 2× line, the Dual
  Delay's echoes / mix / spread, its ringing through silence and ducking, the Ensemble's curves and its bit-exact dry);
  rounds 190nb / 190nh / 190oc's Kilohearts tests follow the new mapping. Mini runner (190nh's list of 88 test files +
  190oc's and this round's, run from the worktree): **546 117 checks, 0 failures**.

## 6. Not done

- The Flanger's barber-pole 90° pair and its 32-tap up-sampler are fixed tables in the plug-in; LYNX's own designs
  leave the barber-pole at 7 and the ±0.98 feedback renders at 5 … 7.
- The Ensemble's sleep / wake after long silences (§4): 23 of 52 files at 4.
- The sine motion's lane order in the maker was read from the decompile only (2 references; both 4 → 4 / 7).

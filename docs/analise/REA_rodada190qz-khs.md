# REA round 190qz-khs — the Kilohearts Ring Mod, Formant Filter, Delay and Clipper (0.1.33 track)

Lab, captures and logs: `/mnt/e/lynx-work/parity2_190qz/khs/` (HANDOFF.md). Branch `parity2_190qz-khs` off
origin/wip/rollup032. Evidence classes as in rounds 190nb / 190nh / 190oc / 190py-khs: [STATIC] read in Kilohearts'
HeartCore 2.4.6 (`HeartCore.core_64`, Kilohearts' own build of 2026-11-19; Ghidra headless and objdump, no execution),
[MEASURED] black-box against round 190nb's lossless kHs references, [DATA] the snapins' own saved states. Clean-room:
LYNX's code restates the laws as formulas. The Ring Mod's half-band is a fixed coefficient table in the plug-in that LYNX
does not use — LYNX designs its own (§2). No Kilohearts code, table or preset is copied.

## 1. Hosting

Four more hidden laws of FL's Effector (core/KiloheartsDsp.h), the next codes after round 190py's 52 (`lawCode = 41 + k −
6` unchanged): **53** Ring Mod with the plug-in's own 2× path, **54** Formant Filter, **55** Delay, **56** Clipper. The
translator (core/KiloheartsStates.inc) opens kHs Ring Mod on 53 (law 8, round 190nb's, stays for slots saved with it),
and kHs Formant Filter, kHs Delay and kHs Clipper on Effector (Type Vowel, Delay, Distort) instead of Facet EQ, Echoform
and Fruity Soft Clipper; a slot saved with the earlier stand-in keeps it (gold, §6). Every value is encoded so that the
snapin's float comes back exactly (powers of two): Ring Mod [13] frequency / 32 768 Hz; Formant [13] / [14] cutoffs /
32 768 Hz, [15] Q / 64, [16] Lows, [17] Highs; Delay [13] time / 16 s, [14] steps / 64, [15] the denominator's index / 6,
[16] sync, [17] feedback, [18] pan (v + 1) / 2, [19] bounce, [20] duck, [21] tone (v + 1) / 2; Clipper [13] threshold / 2,
[14] knee, [15] pre gain / 16, [16] post gain / 16 (linear). The mix is Effector's Wet. The Ring Mod law reports 7
samples of latency.

## 2. RingModDsp again — its 2× path [STATIC + MEASURED]

Process (vtable slot 14, 0x1807680b0), the oscillator (0x1807671f0), the modulator shaping (0x180768730), prepare
(0x180767170), reset (0x180767070). Round 190nb's law has the right formula but runs at the base rate; the plug-in runs
the whole effect at 2 fs:

- **Up 2×:** a 16-entry input history; the even 2× sample is the input 4 samples back, the odd one
  2 Σ t_k (x[n − 4 − k] + x[n − 3 + k]) (four coefficient pairs, a half-band's odd taps).
- **The oscillator:** the phase in turns (double) += f / 2 fs a 2× sample (the increment ramped across the block on a
  change), wrapped once a block (`floor`); the left modulator sin 2π ((float) φ + s), the right sin 2π ((float) φ − s),
  s = spread / 4, in float. A reset sets φ = 0.
- **The rectifier** with first-order antiderivative anti-aliasing per 2× sample: F (m) = ½ (g₊ max (m, 0)² + g₋ min (m,
  0)²), y = (F (m) − F (m₋₁)) / (m − m₋₁) (the plug-in divides by a refined reciprocal), or g · ½ (m + m₋₁) when
  |m − m₋₁| < 5 · 10⁻⁶; g₊ = min (1, 1 + 2r), g₋ = min (1, 1 − 2r). m' = 1 + mix (bias − 1 + y) multiplies the 2×
  input.
- **Down 2×:** the same half-band (centre ½ at 7 2× samples back, the four pairs around it), then a cookbook high pass
  at **15 Hz**, Q 1/√2, direct form II in double (prepare: the designer's case 2 at the base rate).
- **The half-band:** the plug-in's eight coefficients are a fixed table — not used. LYNX's own: a Kaiser-windowed sinc at
  the odd 2× positions (15 taps), normalised to unity at DC; β chosen black-box on the references (lab/hb.py, the lab
  law through khslab): β 3 → −46 … −55 dB on the first 6 s of D / mix, W_1_Frequency_0.750, W_3_Mix_0.000; 4 → −55 …
  −67; 4.5 → −60 … −88; **4.55 → −60 … −100**; 4.6 → −60 … −94; 5 → −61 … −74; 7 → −52 … −66; 9 → −47 … −67.

[MEASURED] Until a render's first long silence the law matches at −51 … −100 dB (D / steady −63 dB: the state stores
"440" while the plug-in ran 440.00003 Hz, the next float; W_1_Frequency_1.000 −51.6 dB likewise: "10239.996"). Not
resolved: after a silence (the 'steady' stimulus has 0.5 s at 8 s; drums, vocal and stim have several) the plug-in's
oscillator resumes at a phase that differs between renders of the same frequency (the jump varies with Mix, Bias,
Spread at 440 Hz: 0.80, 0.99, 0.17, 0.57, 0.63, 0.22, 0.87, 0.91, 0.74, 0.94 turns). No model fitted every file: phase
stopped while asleep over any whole number of samples (no K fits five frequencies within 0.02 turn), a reset to 0 on
waking at any sample, the output's tail crossing a threshold (2^-16 … 10^-8) as the sleep point, host / internal block
sizes 16 … 1 024. Round 190nb's sleep rule (asleep from the second all-zero block) is kept: 5 files keep the plug-in's
phase after the gap (D, Spread 0, Mix 1, Bias 0, Rectify 0.5).

## 3. VocalFilterDsp — the Formant Filter [STATIC]

Process 0x18085a730, reset 0x18085a6f0, the shared designer 0x180bac490 (the textbook cookbook: w0 = 2π f / fs with f
held at 0.49 fs, α = sin w0 / 2Q, A = 2^(dB · 0.0830482) = 10^(dB / 40), the coefficients divided by a0). Two sections in
a row, both at the plug-in's Q: at min (cutoff 1, cutoff 2) a peak (Lows on; the designer's case 5) or a high pass (case
2), at the max a peak (Highs on) or a low pass (case 0); the peak gain 20 · log10f (Q) dB, taken in float. Direct form
I in double; the first section's output is rounded to float before the second; the output in float. Built as read:
every reference −131 … −165 dB.

## 4. DelayDsp — the Delay [STATIC + MEASURED]

Process 0x1802ed7f0, prepare 0x1802ed550, reset 0x1802ed3b0, the sleep advance (slot 13) 0x1802ed790, tail (slot 19)
0x1802ee4d0; constructor fields: duck τ and glide time both 0.05 s.

- **Time:** T seconds or, synced, steps × the note's beats / the tempo's beats a second (note table 1, 2/3, 1/2, 1/3,
  1/4, 1/6, 1/8 for 4 … 32, as the Dual Delay), held to 0 … 5 s. A change of T glides linearly from where the time was
  over 0.05 s, then ramps per sample across a block; while it moves the line is read between samples (an interpolating
  read not modelled sample for sample: LYNX reads Catmull-Rom there). Otherwise the read is round (T fs − 7) + 7
  samples back from the newest before the write (each echo one sample after that: 9 601 at 0.2 s), clamped to what has
  been written (from 15). [MEASURED] round, not ceiling: W_1_DelayTimeseconds_0.500 (T fs − 7 = 10 726.126) is one
  sample earlier than a ceiling would put it.
- **Into the line:** pan gains √2 cos, √2 sin of (pan + 1) π/4 (crossfaded linearly across a block on a change);
  u_L = fb · a_L + g_L x_L, u_R = fb · a_R + g_R x_R; with Bounce u_L = fb · a_L + g_R x_L, u_R = fb · a_R + g_L x_R
  and the two results cross (the left line takes u_R's). Each through the Dual Delay's tone shelf (a TPT state-variable
  filter at min (0.4999 fs, 800 Hz), k = 2, in double; tone ≥ 0 scales the low pass, < 0 the high pass, by
  2^(∓0.1661 · 48 T tone), the band pass by 0.5858 g + 1.4142), written in float.
- **Ducking and mix:** an RMS follower e += k (p / e − e), p = max (½ (x_L² + x_R²), 2^-20), from 0.005,
  k = ½ − ½ · 2^(−1 / (0.05 fs)); out = x + clamp (1 − e · 10 duck², 0, 1) · mix · (wet − x) — the duck scales the mix
  (the Dual Delay's scales the wet only).
- **Sleep:** asleep after T fs + 64 samples of silence (round 190nh's offset), a block whose echoes reach 2^-20 counting
  as sound (the plug-in clears the block's silence flag); while asleep the follower relaxes, e = (1 − k)^n (e − 2^-20) +
  2^-20 (slot 13). [MEASURED] the references show no follower steps before an impulse that wakes the plug-in, whatever
  the host's 512-sample grid (the ir's impulse at 4 800 is 192 samples into a host block; with those 192 per-sample steps
  the Duck renders were −10 … −19 dB): LYNX wakes on the first sound of a block (its leading silence relaxes as asleep),
  which also makes the result independent of the block size. Then every Duck render −72 … −113 dB.

## 5. ClipperDsp — the Clipper [STATIC]

Process 0x180884b30, reset 0x180884b10. Memoryless at the base rate: u = x · pre, a = |u|; a ≤ (1 − K) T passes,
a > (1 + K) T gives T, between them T (1 − (a / T − 1 − K)² / 4K); the sign restored (a two-entry table ±1), × post.
The four values ramp across a block on a change. Built as read: every reference −331 … −371 dB (bit-exact).

## 6. Results and gates

Scores: fxparity's time-window scorer (round 190lg's, as rounds 190nb … 190py-khs), round 190nb's references and song
positions; before = origin/wip/rollup032's fxrender (law 8, Facet EQ, Echoform, Fruity Soft Clipper), after = this
round's (runs/base, runs/new in the lab folder; rep/table_final.md). Table and gates: the HANDOFF.

## 7. Not done

- **kHs Phase Distortion** (4.11): PhaseDistortionDsp runs at the snapins' Chebyshev II oversampler (round 190nh's) with
  a 30 Hz output high pass and the dry undelayed, but its analytic pair is two biquad cascades with a fixed coefficient
  table, rate-independent (constructor, 0x18058f13d …): without that table the pair's phase differs, so the ESR stays
  near 0 dB whatever else is matched; LYNX may not copy it. Left at round 190nb's law.
- **kHs Reverb** (4.26): ReverbDsp is a 16-line feedback delay network whose delay lengths (a factor table rounded up to
  primes), feedback matrix (256 floats) and input / output gains are fixed tables (0x181f2e190 …, 0x181f2e280,
  0x181f2e2c0); waveform parity needs them (or measuring them, which is the same). Left on Reeverb 2.
- **kHs Ensemble's sleep / wake** (5.79): the same render-to-render difference after a silence as the Ring Mod's (§2) —
  not a deterministic function of the input on these captures. Left.

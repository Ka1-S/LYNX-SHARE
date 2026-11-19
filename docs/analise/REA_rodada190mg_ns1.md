# REA round 190mg, sub-round ns1 — Waves NS1 at 20 … 100 % → Quell Denoiser's NS1 law

Owner's permission of 2026-11-17 (REA on third-party binaries for parity, clean-room only). Nothing of the vendor's code,
tables, presets or content was copied: the DSP was decompiled for its STRUCTURE (round 190lf's decompile of
`GenericMultiCoreProcessor.dll` `NS1Proc`, reused here — no new Ghidra session), the parameter names come from the plug-in's
own parameter description (the interface every saved state refers to), and every number LYNX uses was measured or fitted
on our own renders of the official trial. Work folder /mnt/e/lynx-work/waves3_190mg/ns1/ (HANDOFF.md, lab/, runs/, ev/).

## 1. Structure (REA, round 190lf's decompile read again)

Per sample, zero latency:

- **Front end** — two stages in cascade, each "a first-order and a second-order recursive filter combined" (the
  parallel form of a third-order filter): a complementary all-pass split. With A1 the first-order and A2 the
  second-order all-pass at the same corner, low part = (A2 + A1) / 2, high part = (A2 − A1) / 2. The first stage keeps its
  high part and multiplies its low part by a gain, the second keeps its low part and multiplies its high part. At gain 1
  each stage is exactly its second-order all-pass — the "all-pass pair" round 190lf measured at 0 % (60 Hz / 16 kHz).
- **Six bands in cascade**, each three Regalia–Mitra sections `y = (x + A x)/2 + g (x − A x)/2` on second-order
  all-passes, the three sharing one gain g per band (g = 1: exactly transparent; the cut at the centre is g³).
- **Per band detector** on the mid signal of the front end's output (half scale): a band-pass made of filters in cascade,
  rectified, plus a tiny offset.
- **Per band gain computer**: a noise floor that is multiplied by an "up" factor while the detector is above it and a
  "down" factor while below (frozen under a gate level); a level follower that slews the same way; a threshold between
  the floor and the level at the "position", kept between fixed distances above the floor, scaled by a global
  voice-activity flag (a count of bands over their floors, with hold counters); a soft weight `w = T⁴ / (e⁴ + T⁴)`; the
  gain `1 − (1 − g_min) w²`; one-pole smoothing with separate opening / closing coefficients.
- The DSP also forms a global level-over-floor ratio of all bands (used to scale the thresholds and a cap) and a
  floor-gain shaping between bands; LYNX's fit did not need them (§4).

The saved state (RealWorld list; names from the ParamXML, file ev/state_values.txt) shows what the one knob
(PARAM_WET_DRY_CNTRL, 229) writes: PARAM_MIN_FC / PARAM_MAX_FC (85 / 86) = the outer band centres and the split corners,
PARAM_HP_GAIN / PARAM_LP_GAIN (206 / 203) = the cuts of the parts below / above them, PARAM_ADAPT_THR_POSITION (226), and
PARAM_MIN_ADAPT_REDUCTION (228, 0 / −6 dB); the band centres 0 / 13 / … / 65 are geometric between MIN_FC and MAX_FC.

| knob | MIN_FC / MAX_FC | cut (206 = 203) | position (226) |
|---|---|---|---|
| 0 % (D) | 60 / 16000 | 0 dB | 0 |
| 20 % (X1) | 60 / 14000 | −10 dB | 0.02 |
| 35 % (X2) | 65 / 13500 | −25 dB | 0.0325 |
| 50 % (S1) | 70 / 13000 | −40 dB | 0.045 |
| 100 % (S2) | 110 / 8000 | −70 dB | 0.8 |

## 2. Measurements (our renders)

- **The front end is confirmed**: with the band gains at 1, LYNX's model at the D layout matches refcap182's D mix to
  −76 dB ESR (the bare pair).
- **The high split's second-order Q moves with the knob** (fits of the complex response on the −20 dBFS pink-noise step of
  probe n1 — where the bands are open — and of the programme relative to D): corner = MAX_FC (14006 / 13007 Hz fitted),
  cut = LP_GAIN (−9.97 / −37.6 dB fitted), the real pole at the corner (×1.004 / ×1.009), Q 0.370 (20 %, −53.6 dB residual),
  0.392 (35 %, programme), 0.432 (50 %, −44.6 dB); at 100 % the bands are never open on our signals (fitted with the rest,
  §3). The low split is Butterworth (Q ≈ 1, corner = MIN_FC, cut = HP_GAIN).
- **The band gains**: at 20 … 50 % a loud programme is hardly touched (< 1 dB in its loud bands) while bands with little
  content go down by up to the full depth (bass: the 4.7 / 14 kHz bands −13 … −19 dB; guitar: the 65 Hz band −13 dB); at
  100 % a dense programme is taken down by 10 … 22 dB over its first seconds (mix, guitar), a sparse one hardly (vocal).
  The floor moves slowly both ways (the noise steps: a few dB per second).

## 3. LYNX: Quell's NS1 law (hidden [6], `voicetools::quell::ns1Law`)

- **On** (the NS1 translator sets it, with the zero law; returns 4 values): at Amount > 0 a new engine,
  `voicetools::(anon)::ns1::Engine`, our own code with the structure of §1:
  - layout `ns1Layout (amount)`: corners, cut and position through the five saved states (0 / 20 / 35 / 50 / 100 %, linear
    in between, the corners in log), the high split's Q 0.8992 / 0.370 / 0.392 / 0.432 / 0.699 (the last fitted on the
    100 % renders); band centres geometric between the corners;
  - front end = the two complementary all-pass splits (§1), the low split's Q 1.0;
  - six bands × three Regalia–Mitra sections on RBJ all-passes (Q 0.7 at 20 … 50 %, 0.5 at 100 %), one gain per band;
  - detector: two RBJ band-passes in cascade at the band centre (Q 2.7 / 1.2) on the mid of the front end's output, rectified;
  - floor: × (up) while the detector is above it, × (down) below (0.75 / 13 dB per second at 20 … 50 %, 1.5 / 47 at
    100 %), frozen under −80 dBFS, starting at −70 / −60 dBFS; level follower 118 600 dB/s up (instant) / 32 … 38 dB/s down;
  - threshold `T = max (floor · 10^(9/20), min (kT · (floor + position · (level − floor)), floor · 10^(cap/20))) · flag`,
    kT −10 dB / cap 21 dB at 20 … 50 %, kT +1 dB / cap 25 dB at 100 %; flag = +3 dB while no band of 100 … 998 Hz is
    23 … 36 dB over its floor for 60 ms (the pause flag), else 1;
  - `w = T⁴ / (e⁴ + T⁴)`, gain `1 − (1 − g_min) w²` with g_min = the depth (8.5 dB at 20 … 50 %, 9.9 dB at 100 %, the
    three sections together at the centre) faded in from 0 to 20 %; smoothing 3.4 / 38 ms (open / close) at 20 … 50 %,
    2.4 / 30 ms at 100 %; at 100 % the rectified detector is smoothed (0.1 / 0.5 ms) first.
  - The two fitted sets (20 … 50 % and 100 %) are blended linearly between 50 and 100 %. All coefficients are designed from
    times / frequencies at the session rate (44.1 / 48 / 96 kHz alike).
- **Amount 0** keeps the zero law's path bit for bit (round 124's pair); once the NS1 engine has run, automation down to 0
  stays in it (its own open pair, −55 dB from the old one: no audible step).
- **Off (0)** for new instances and every project saved before: unchanged code (gold renders below).
- **Fit** (stated, per the brief): the constants were fitted by a coordinate search on the scorer's own score of the
  refcap182 programme renders at 20 / 35 / 50 % (one shared set) and at 100 % (its own set), with the probes (190lf's
  noise steps, sine stairs, noise + tone, bursts, impulses) in the first fits' objective and as the check below; the last
  20 … 50 % fit was constrained so that no file goes below today's score. The model (lab/ns1sim.cpp) and the C++ engine
  agree to −33 … −84 dB on the programme (lab/portchk.py: the C++ uses the layout law's geometric centres and float knob
  values).

### Table (refcap182 programme, 25 files; before = fxr_base, after = this branch's fxr_w4; real scorer)

| file | score | ESR dB | spec dB | rms dB |
|---|---|---|---|---|
| D_bass | 8 → 8 | -52.6 → -52.6 | 0.00 → 0.00 | +0.00 → +0.00 |
| D_drums | 8 → 8 | -40.0 → -40.0 | 0.02 → 0.02 | +0.00 → +0.00 |
| D_guitar | 10 → 10 | -70.6 → -70.6 | 0.00 → 0.00 | +0.00 → +0.00 |
| D_mix | 8 → 8 | -54.4 → -54.4 | 0.00 → 0.00 | +0.00 → +0.00 |
| D_vocal | 9 → 9 | -66.7 → -66.7 | 0.00 → 0.00 | +0.00 → +0.00 |
| X1_bass | 5 → 8 | -17.8 → -33.4 | 0.65 → 0.12 | -0.08 → -0.06 |
| X1_drums | 4 → 8 | -8.1 → -26.9 | 0.65 → 0.13 | +0.67 → -0.08 |
| X1_guitar | 6 → 6 | -20.2 → -21.7 | 0.47 → 0.45 | +0.14 → -0.02 |
| X1_mix | 6 → 8 | -13.0 → -31.5 | 0.56 → 0.10 | +0.19 → -0.08 |
| X1_vocal | 5 → 7 | -17.2 → -25.0 | 0.64 → 0.18 | +0.06 → -0.08 |
| X2_bass | 5 → 7 | -18.8 → -30.9 | 0.82 → 0.19 | -0.19 → -0.03 |
| X2_drums | 4 → 8 | -8.5 → -27.1 | 0.84 → 0.11 | +0.16 → -0.11 |
| X2_guitar | 6 → 6 | -18.0 → -20.6 | 0.54 → 0.59 | +0.22 → +0.06 |
| X2_mix | 5 → 8 | -13.9 → -31.8 | 0.72 → 0.07 | +0.00 → -0.04 |
| X2_vocal | 5 → 6 | -17.8 → -24.5 | 0.82 → 0.18 | +0.07 → -0.04 |
| S1_bass | 5 → 7 | -21.1 → -28.6 | 0.75 → 0.21 | -0.22 → +0.01 |
| S1_drums | 4 → 8 | -11.2 → -27.1 | 0.75 → 0.09 | -0.25 → -0.13 |
| S1_guitar | 5 → 5 | -16.3 → -19.8 | 0.71 → 0.70 | +0.28 → +0.12 |
| S1_mix | 5 → 8 | -16.0 → -30.4 | 0.61 → 0.03 | -0.11 → +0.02 |
| S1_vocal | 5 → 8 | -16.9 → -23.9 | 0.79 → 0.10 | +0.05 → -0.01 |
| S2_bass | 3 → 6 | -4.9 → -16.2 | 2.51 → 0.52 | -0.00 → -0.06 |
| S2_drums | 4 → 6 | -4.0 → -23.4 | 2.12 → 0.26 | +0.44 → -0.04 |
| S2_guitar | 4 → 4 | -4.1 → -6.2 | 2.14 → 1.36 | +0.49 → +0.25 |
| S2_mix | 2 → 4 | -0.7 → -9.1 | 3.34 → 0.69 | +3.17 → +0.40 |
| S2_vocal | 4 → 5 | -8.9 → -13.2 | 1.52 → 0.74 | -0.36 → -0.48 |
Mean 5.40 → 7.04; up 16, same 9, down 0. ESR means: 20 % −15.3 → −27.7, 35 % −15.4 → −27.0, 50 % −16.3 → −26.0,
100 % −4.5 → −13.6 dB.

- Probes (lab/probechk.py; ESR vs the trial, lead and demo mutes masked; base → this branch; runs/probechk.txt): mean
  −0.5 → −9.9 dB; noise steps X1 −5.0 → −29.0, S1 −4.9 → −14.4, S2 −1.2 → −1.4; sine stairs X1 −12.3 → −18.8, S2 −5.9 →
  −9.4; bursts over noise S2 −11.4 → −16.9; impulses (+20 … +61 → ±0.8); noise + tone X1 −14.2 → −11.9, X2 −7.6 → −6.9
  and S1 −6.2 → −5.8 (worse; not analysed further).
- **Gold renders** (ns1/lab/gold.sh, base fxr_base = origin/waves2_190lf 2c893d3ca vs this branch): Quell default, the
  classic engine at 4 amounts, the matched engine at 4 amounts × tracker law on / off, the zero law at 4 amounts, Listen,
  Output, and NS1's 0 % state through the translator; × 44.1 / 48 / 96 kHz × vocal / mix / drums: 180 renders,
  0 mismatches (runs/gold.log).
- **CPU** (35 s stereo mix, 48 kHz, one core, wall clock, best of 5, two interleaved passes; runs/cpu.log): NS1 X1
  2.29 → 1.13 s, S1 2.27 → 1.10 s, S2 2.13 → 1.11 s (a file copy through fxrender: 0.69 … 0.78 s); D (unchanged path)
  2.29 / 2.17 s. The engine costs about a quarter of round 124's (≈ 1.2 % of a core against ≈ 4.6 %): no coefficients
  are redesigned per sample.
- **Tests**: tests/Parity190mgNs1Tests.inc (hidden / saved while non-zero; NS1 translation; the layout law at the five
  states and monotone; Amount 0 bit for bit with the zero law and continuous above it; steady noise down by > 10 dB and a
  tone through at 44.1 / 48 / 96 kHz; Listen; different from round 124's engine); mini runner (190ky's set + Vqe190dp +
  Parity190mgNs1): 544 150 checks, 0 failures.

## 4. Tried and not kept

- **The global level-over-floor ratio** of §1 in the threshold (T ∝ ratio · floor, and as an extra lower bound
  kG · ratio · floor): every variant cut loud programme bands too (X1 mix −5 … −13 dB where the trial leaves them alone;
  drums 8 → 3 … 6). Not used.
- **One detector band-pass** (Q 1.2, the first fits) instead of two in cascade: too little band selectivity — the
  guitar's 65 Hz band at 35 % −0.4 dB where the trial takes −13 dB (two in cascade: −2 … −5 dB).
- **The floor-gain shaping between bands** (gain toward a target residual spectrum): not needed by the fit (left out).
- **Fitting for ESR alone** (fit.py, the first fits): the ESR optimum is not the score's (X2 guitar 6 → 5 on the spectral
  criterion with a better ESR); the later fits (fit2.py) use the scorer's own score, spectral distance and level.
- **A slow level follower at 100 %** (so that dense programme is cut over seconds and phrases are not): dense programme
  then not cut at all (S2 guitar / mix +5 dB ESR). The 100 % set keeps an instant follower; its S2 files go up, S2 guitar
  stays at 4 (the trial takes dense guitar down by 10 … 16 dB over seconds — not reproduced in time, only in level).
- The 0 % path could also move to the NS1 engine's own pair (its open path matches the D mix to −76 dB, round 124's to
  −54 dB): not done — the brief keeps the D files bit for bit (an owner option for later).

## 5. Files

- src/core/VoiceTools.h / .cpp (`ns1Law`, `Ns1Layout ns1Layout (double)`, namespace `ns1` engine), src/core/PluginSubstitutes.cpp
  (translateWavesNs1 sets it, returns 4), tests/Parity190mgNs1Tests.inc (+ its include line), the two NS1 translation
  count checks (VoiceToolsTests119.inc, Parity190lfNs1Tests.inc: 3 → 4).
- Lab: ns1/lab (ns1sim.cpp = the engine's model, sim.py, fit.py / fit2.py coordinate searches, pscore.py = the scorer's
  metrics in-process, hifit / hifit2 / lofit front-end fits, bandatt / bandcmp / probecmp / errspec views, tocpp.py,
  portchk.py, gold.sh, cpu.sh), fits ns1/runs (fit_*.json / .log).

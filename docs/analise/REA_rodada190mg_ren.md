# REA round 190mg (sub-round ren): Waves Renaissance Compressor → Compressor's "Renaissance law"

Owner's permission of 2026-11-17 (REA on third-party binaries for parity, clean-room). Evidence markers: [REA] read from
the V17 binaries (Ghidra decompiles in /mnt/e/lynx-work/waves3_190mg/ren/ev: `gmcp` = GenericMultiCoreProcessor.dll, the
DSP, export ProcessFuncS / ProcessFuncM; `rcdll` = RComp.dll, the control code that turns the knobs into the DSP's 32
coefficients; ParamXML / ProcessXML in ev/xml), [EXEC] our own black-box probes of the demo-mode plug-in on refcap030's
hidden host (ren/cap: p1.txt, 32 crafted states), [model] our reading. No vendor code, table or preset was copied: the
structure below is described in our words, LYNX's engine is written from it, and every constant in it was fitted on our
probes (ren/fit). No licence or protection was touched (the DSP DLL is unprotected; demo-mode mutes masked).

## 1. What the binaries show [REA]

- Parameters (ParamXML, 28 values): release, threshold, gain, attack, ratio (50 … 0.5, its own scale), the hidden
  program-dependent-release values ("PDR REL" / "Crest" times, "PDR ratio"), a hidden frequency and Q, Warm / Smooth
  (named "number of filters"), ARC / Manual (named "Fast / Slow"), Electro / Opto (named "inverted release"), the
  limiter's release, mix, trim. The stereo DSP has 36 states and 32 coefficients; a 1025-word cyclic buffer (audio and
  gain delay) and two 256-word tables (the release law).
- The DSP, per sample:
  1. a **linked peak detector**: per channel the magnitude of the input and — Smooth only — of three first-order
     all-pass copies of it (their break frequencies spread around the hidden frequency), the largest of all;
  2. its **maximum over a look-ahead window** (64 samples under 64 kHz, 128 under 128 kHz, 256 above);
  3. the **output limiter** on the same peak: target min (1, ceiling / peak) with the ceiling = 0 dBFS after the gain,
     instant down, a one-pole recovery (slower in Manual);
  4. the **gain computer in linear gain**: g = 1 / (1 + peak / T), bent by g + c g² (1 − g) (c larger when compressing
     than when expanding), never above 1; T = the threshold, raised at ratios near and below 1 : 1;
  5. a **one-pole smoother** on that gain: the attack knob's time when the gain falls; when it rises a coefficient read
     from the two tables — the release time shaped by where a slow follower of the gain sits (Electro: faster towards
     no reduction, Opto: the reverse), times a program-dependent factor that speeds the release while the gain is under
     its slow follower (much stronger in ARC than in Manual). The slow follower has its own fall / rise times;
  6. the **ratio as a floor**: gain = (1 − a) · smoothed + a, a < 1 compressing (a → 0 at 20 : 1 and above), a > 1
     expanding (the upward expansion below 1 : 1); then the smaller of this and the limiter's gain;
  7. **two cascaded boxcars** over that gain (lengths summing to the window + 1), in fixed point;
  8. the audio and the dry path **delayed by the window**; output = makeup · gain · mix · trim · wet + (1 − mix) · trim
     · dry, mix and trim glide over ~10 ms.
- So the ratio is not a dB slope: the curve is the same soft knee for every ratio, and the ratio only sets how far the
  gain can fall (the floor). This is why the round-123 / 152 engine (a dB-domain soft knee with a ratio scale, a slow
  "program" path) could not follow programme material away from its fit point.

## 2. Probes and fits [EXEC]

Probe set p1 (ren/cap/mkp1.py; 48 kHz stereo, 1.5 s of silence after each reset; demo-mode mutes masked):
1 kHz stairs −60 … 0 dBFS in 3 dB steps at 11 ratios (0.5 … 50) and thresholds −10 / −30 / −45, Smooth, +12 dB gain,
mix 40 %; 1 kHz bursts −40 → −10 dBFS (5 ms … 2 s) at ARC / Manual × Electro / Opto, releases 50 / 160 / 500 / 2000 ms,
attacks 0.5 / 16 / 100 ms, ratios 1.5 / 4 / 8; 50 / 100 Hz and 60 Hz + 1 kHz tones at Warm and Smooth.

- Static (fit/fitstatic.py, fitstatic2.py): per stair a free threshold shift, floor and bend; the structure fits every
  compressing stair to 0.000 dB rms and the expanding ones to ≤ 0.001 dB. Fitted laws (LYNX's constants):
  ln (a + 0.0062) = 0.0062 − 3.000 u + 0.434 u², u = ln ratio (ratio > 1; a ≥ 0); below 1 : 1 a = e^(5.545 (1 − ratio));
  threshold shift max (0, 25 − 20 · ratio) dB; bend 1.5 compressing, 1.25 expanding.
- Dynamics (fit/fitdyn.py, Nelder–Mead on the C model fit/rcsim2.c against the 16 burst / tone probes, mean of the
  sample ESRs): attack and release knob scales 1.0000 / 0.9998, program-dependent exponent 0.003911 (ARC) / 0.0003532
  (Manual) with offset 0.003941 (on a gain scale of 0 … 1), slow follower 149.58 ms falling / 56.24 ms rising, all-pass
  break frequencies 49.50 / 130.74 / 280.18 Hz (Smooth); the release shape (1 − v⁴)⁴ with a 0.002 ms floor kept as read.
  The release law read as 256-step tables, truncated, is 15 … 20 dB closer on the probes than a continuous law (it is
  what the reference does), so LYNX builds its own tables from the fitted law the same way.
- Limiter (fit/fitlim.py on p2: +18 dB into 1.2 : 1, bursts and drums, ARC / Manual, and the expanding stairs): recovery
  1.500 ms, × 5.000 in Manual; its target capped at +24 dB (so the expansion's boost passes under the ceiling).
- Rates (p3 at 96 kHz, p4 at 44.1 kHz: the reference reports 128 / 64 samples of latency): the model with the window
  64 / 128 / 256 samples (under 64 kHz / 128 kHz / above), boxcars 0.42 / 0.58 of it and the same times, −71 … −95 dB.

| probe set | settings | model vs reference, sample ESR |
|---|---|---|
| p1 stairs (static) | 16 | every compressing stair 0.000 dB rms on the static curve; −77 … −107 dB |
| p1 bursts / tones | 16 | −78 … −94 dB |
| p2 limiter | 5 | −76 … −97 dB |
| p3 (96 kHz) / p4 (44.1 kHz) | 4 + 4 | −71 … −88 / −81 … −95 dB |

## 3. LYNX: Compressor's Renaissance law

- `vmatch::progcomp::Renaissance` (core/VoiceMatch.h / .cpp): the structure of §1 with the constants of §2, in double,
  the boxcars in fixed point (exact running sums), the release tables rebuilt when the release / mode / curve change.
  Latency = the window (64 samples at 44.1 / 48 kHz, 128 at 96 kHz).
- Compressor's hidden law parameter [20] (`compressorRenLaw`, core/Effects.h; saved only while non-zero, reset for any
  project saved with 20 values or fewer): 1 runs the vintage voicing on the Renaissance law; 0 = the round-123 / 152
  engine bit for bit (and the classic voicing untouched). Only `translateWavesRComp` sets it. No UI change, no strings.

## 4. Results

refcap182 programme (RCompressor Stereo, 30 files; scorer scripts/fxparity.py of the branch):

| setting | before | after |
|---|---|---|
| D (transparent) | 10 10 10 10 10 | 10 10 10 10 10 |
| S1 (ARC, −24 dB, 4 : 1) | 5 5 5 5 6 | 9 10 9 10 10 |
| S2 (Manual, −30 dB, 8 : 1, 2 / 80 ms) | 5 3 5 5 5 | 10 10 10 10 10 |
| S3 (Opto + Smooth, −18 dB, 2 : 1) | 5 5 7 5 6 | 9 10 9 9 9 |
| X1 (−36 dB, 10 : 1, mix 40 %, +6 dB) | 8 8 8 8 8 | 10 10 10 10 10 |
| X2 (−20 dB, 2 : 1, 30 ms, +2 dB) | 5 6 7 6 6 | 10 10 10 10 10 |
| mean | 6.57 | 9.80 (up 25, same 5, down 0) |

(order bass, drums, guitar, mix, vocal). Sample ESR after: −63 … −112 dB on every compressing file (before −10 … −46).

- Old projects: the law is 0 for new instances and for every project saved before (20 saved values or fewer); 108 gold
  renders (Compressor classic default / settings / FET / auto gain / RComp-range expansion, vintage default / RComp
  ranges ARC / Manual · Opto · Smooth / expansion / round-150 curve + mix / trim, two SSLComp states; 44.1 / 48 / 96 kHz
  × vocal / mix / drums), base binary (origin/waves2_190lf 2c893d3ca) vs this one: 0 differ (ren/lab/gold.sh).
- Only translator using the vintage voicing: `translateWavesRComp` (grep VoiceMatch / params[8]); SSLComp (Crest) and
  the preset-only Waves compressors are untouched (in the gold set).
- Tests: tests/Parity190mgRenTests.inc (hidden law + reset on load, translation + latency per rate, static curve vs our
  stairs (worst 0.047 dB), release vs our bursts (worst 0.007 dB), expansion under the limiter, mix / gain).

## 5. Not done / next

- The release shape exponents (1 − v⁴)⁴ and the 0.002 ms floor were kept as read, not refitted (Opto probes −78 dB, the
  weakest; the remaining 9s on refcap182 are S1 bass / guitar and S3).
- Rates above 128 kHz (window 256) were not probed (the reference's own rate laws for 192 kHz are taken as read).
- The side-chain input (the reference has one) is not wired for this law, as for the round-123 engine.

CPU (ren/lab/cpu2.sh: user seconds, best of 7, base / new interleaved, one core; 35 s stereo programme at 48 kHz, file
I/O included): RComp S1 0.437 → 0.188, S2 0.463 → 0.181, S3 (Smooth) 0.405 → 0.251, X1 0.456 → 0.217; controls
(unchanged code) vintage 0.451 / 0.438, classic 0.196 / 0.163. The law has no per-sample transcendental (tables, one
division) and is about half the old engine's cost.

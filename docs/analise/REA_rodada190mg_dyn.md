# REA round 190mg, sub-round dyn: the 190lf leftovers (PuigTec stage / hum, L2 IDR, CLA-76 amplifier)

Owner's permission of 2026-11-17 (clean-room REA for parity). Structure from the decompiles of rounds 190lf / 190ky (no new
Ghidra session); every LYNX number fitted on our Waves demo-mode probes (/mnt/e/lynx-work/waves3_190mg/dyn/cap: pt1 18,
l2a 33, c76a 29 renders; 1.5 s lead; crafted states through loadcomponent). The sub-round stopped responding at 01:30
(after its first builds and scores); the coordinator finished it from its worktree (tests, gate, this document).

## Structure read
- PuigTec output stage: the EQ's output, then a hum oscillator (a triangle with an even term through low- and high-pass
  all-pass sums) and a coloured LCG noise, then a rational waveshaper (an odd soft clip with an even factor).
- L2 IDR: random TPDF / a second type / none, an error-feedback shaping filter, rounding.
- CLA-76 amplifier: a rational soft clip on a filtered path, a FET distortion term, then a per-half-wave clip.

## Measured [EXEC]
- PuigTec findings [EXEC]: the harmonics come from a static waveshaper AFTER the EQ and after the Output gain (Output
  ±12 shifts the H2/H3 curve by 12 dB; 100 Hz / 1 kHz / 4.8 kHz identical at the same output level; In off = flat
  path, same harmonics). H2 −82.6 / H3 −86.4 / H5 −103 dBc at −39 dBFS out, 1 dB per dB; gain compression 1.11 →
  0.91 dB at +7 dBFS. Fit (lab/tubefit.py, rational form): every H2…H5 within 0.1 dB, phases exact, gain curve within
  0.01 dB (fit/tube1.json). Mains: rw16 1 = 60 Hz (measured 59.8505 Hz, H1 −90.1 dBFS), 2 = 50 Hz (49.794 Hz, H1
  −103.3); hum identical L/R, added after the Output gain and outside the EQ; its phase is NOT deterministic after reset
  (83° / −13° / 7° in three renders) → not sample-matchable; noise floor white −102.1 dB per sample + broad LF bell
  (+18 dB, 19 Hz, Q 0.05) + high shelf (12.2 kHz, −3.2 dB), independent L/R (fit 0.34 dB rms, lab/noisefit.py).
  Hum generator fit (lab/humfit2.py: triangle + even term → 2nd-order LP / HP): H1/H2 within 0.3 dB, H3 0.4 / 2.3 dB.
  Resampler: the reference is 148.774 samples late vs LYNX's minimum-phase curve, flat over 100 Hz–20 kHz → the
  aligned comparison has a 0.226-sample error (−30 dB at 1 kHz!): LYNX adds a 3rd-order Thiran all-pass (latency 3).
- L2 findings [EXEC]: quantize 0…4 = 24/22/20/18/16 bits (grid); dither setting 1 = TPDF (white error 0.250 LSB² in
  every render = TPDF + rounding); settings 0 / 2 add no random dither (signal-correlated, tonal error); shaping 0 none
  (digital silence stays silent), 1/2/3 = moderate/normal/ultra (noise 1.02 / 3.95 / 15.25 LSB²). Shaping fitted as
  order-48 min-phase NTFs (lab/dfit.py, gen_l2d.py): 0.26 / 0.24 / 0.56 dB rms over 1/6 octaves.
- CLA-76 findings [EXEC]: the amplifier's H2 / H3 follow the level at the Output knob's −30 mark only (Comp Off and On
  alike; the Output knob is after it): H2 = level − 22.3 dB (1 dB per dB), H3 = H2 − 9.2, H5 = H3 − 17, with DC
  (dc/A² = 0.077) and a phase lead of the harmonics (+14° / +20° / +34° at 2 / 3 / 5 kHz); clip above ≈ −11 dBFS at
  that level (comp off only). Fit (lab/ampfit.py: s + q + lead·Δq, q = a s² + b s|s|): H2 0.2 dB, H3 ≤ 1 dB, phases
  ≤ 5°. Auto make-up (rw105): NO steady-state change in any probe (Input −36…0, 4:1 / 20:1 / all, Output −30/−18,
  Bluey) — only a start-up ramp after reset (−12 dB at Input −12 recovering over ~5 s, a host artefact): not translated.

## LYNX laws (hidden; 0 = every project saved before, bit for bit)
1. Facet EQ [125] `puigLaw` (1 = stage, 2 = + 60 Hz mains, 3 = + 50 Hz mains; set by the PuigTec translator): the fitted
   waveshaper after the output gain, a 3rd-order Thiran all-pass for the resampler's 0.226-sample fractional delay (3 samples
   of latency) and, with Mains, LYNX's hum generator + noise floor (deterministic: the reference's hum phase is not).
   refcap182 programme 7.60 → 7.84 (up 6 / same 19 / down 0; the Mains switch is off in every scored state).
2. Limiter [6 … 8] IDR (set by the L2 translator): word length 24 … 16 bits, TPDF at Type 1, the fitted order-48
   shaping filters (moderate / normal / ultra), seeded per instance (renders stay deterministic). refcap182 9.52 → 9.52
   (only X4 quantizes, at 16 bits: the noise is far below the scores' thresholds).
3. Crest's fetAmp (CLA-76's output amplifier, fitted: H2 0.2 dB, H3 ≤ 1 dB, phases ≤ 5°): modelled, NOT set by the
   translator — it cost X1_bass 8 → 7 and X4_vocal 6 → 5 (ESR −32.9 → −32.1 / −17.1 → −16.9 dB) with nothing up.
   Auto make-up (rw 105): no steady-state effect in any probe (only a start-up ramp after reset), not translated.

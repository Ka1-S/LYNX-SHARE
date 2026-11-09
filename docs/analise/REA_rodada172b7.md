# Round 172b7: Prism's crossover near Nyquist (Pro-MB, black box)

Pro-MB was measured as a black box only (public parameters, renders of our own test signals). No binary analysis of
the third-party plugin.

## What differs

Audition (round 172b6) plays the band's detector signal, so it shows the band filter's shape directly. Band 500 Hz to
8 kHz, 24 dB/oct, 48 kHz (`b6/a3_base.wav`, impulse through Pro-MB, divided by the input spectrum). Correction from
round 172b7: that probe asked for "Minimum Phase" by text, the text did not match and Pro-MB stayed in **Linear phase**
(`b6/p13.log`: `set Processing Mode = 0.000000 (Linear Phase)`), so this table is Pro-MB's Linear phase:

| Frequency | Pro-MB | LYNX (b6) | Analog LR4 low-pass, 1 / (1 + x⁴) | LYNX's LrSplit, computed |
|---|---|---|---|---|
| 8 kHz | −6.0 dB | −6.0 dB | −6.0 dB | −6.0 dB |
| 12 kHz | −15.7 dB | −20.0 dB | −15.7 dB | −20.0 dB |
| 16 kHz | −24.6 dB | −38.3 dB | −24.6 dB | −38.3 dB |
| 20 kHz | −32.1 dB | −64.8 dB | −32.1 dB | −64.8 dB |

Pro-MB follows the analog Linkwitz-Riley response; LYNX follows the bilinear-transform biquads of `LrSplit`
(RBJ cookbook, a zero at Nyquist). Below the band's high edge the two agree within about 1 dB (low side: 50 Hz to
1 kHz within 1.2 dB). With 48 dB/oct the gap is larger (16 kHz: Pro-MB −48.2 dB, LYNX −76.3 dB). The "Dynamic" and
"Linear" cases of that probe were Linear phase too.

## Compression by phase mode (probe p18b, mode set by value)

Pink noise, band 1 from 500 Hz to the high edge, threshold −60 dB, ratio ∞, range −12 dB, knee 0, 48 kHz. Output over
input power in 1/6-octave bands; LYNX before any change (`b7/ly_before_*.wav`) against Pro-MB (`b7/o_*.wav`, log
`b6/p18b.log`, modes 0.9 Minimum / 0.45 Dynamic / 0.1 Linear verified in the log).

| Case | Largest difference, 400 Hz to 22 kHz | Above the high edge |
|---|---|---|
| Minimum, 8 kHz, 24 dB/oct | 0.1 dB | 0.0 dB |
| Minimum, 12 kHz, 24 dB/oct | 0.1 dB | 0.0 dB |
| Minimum, 8 kHz, 48 dB/oct | 0.1 dB | 0.0 dB |
| Minimum, 4 kHz, 24 dB/oct | 0.1 dB | 0.0 dB |
| Dynamic, 8 kHz, 24 dB/oct | 0.1 dB | 0.0 dB |
| Minimum, range 0 | 0.1 dB | 0.0 dB |
| **Linear, 8 kHz, 24 dB/oct** | **0.6 dB** (5 kHz) | 12 kHz −0.7 vs −1.2, 16 kHz −0.1 vs −0.4 |

(200 Hz differs by up to 0.2 dB in every case, including range 0.)

In Minimum and Dynamic phase Pro-MB's bands follow the same curve as LrSplit. Only Linear phase differs, and it
matches the analog shape 1 / (1 + x⁴) on the f / fc axis: for a −12 dB band gain that gives −1.14 dB at 12 kHz and
−0.39 dB at 16 kHz (Pro-MB −1.2 / −0.4); LYNX's prewarped axis gives −0.67 / −0.08 (LYNX −0.7 / −0.1).

## Scope: every user of LrSplit (src/core/LrCrossover.h)

- `src/core/DynamicsSuite.cpp`: Prism Multiband (`using Split = LrSplit` inside `namespace prism`: the crossover
  tree, the external side-chain split, the analytic band responses). Crest does not use it.
- `src/core/VoiceTools.cpp`: Quell (`splitters`, `comp` all-passes).
- `src/core/MixSuite.cpp`: Ember Saturator's multiband mode (`split`, `ap`); `tests/Parity152bTests.inc` pins its
  LrSplit slopes against the FL trial.
- `src/core/MinPhaseBand.h`: no LrSplit object, but `warp()` uses the same prewarped axis for Prism's measured
  Dynamic-phase model, so Prism's analytic band magnitudes follow the bilinear curve too.

LrSplit itself stays as it is (Quell's and Ember's parity was measured with it). Since Minimum and Dynamic phase
already match, no IIR change is needed: the fix is in Prism's Linear-phase FIR design only (analog axis), see below.

## Change (approved by the coordinator 2026-11-07)

Prism only; LrSplit, Quell and Ember untouched; no legacy switch (a parity fix confined to one mode).

- `designFreeKernels` (free layout) and `designKernels` (split layout): the Linear-phase FIRs take each crossover on the
  analog axis. Free layout: Pro-MB's `1 / (1 + x^m)` with x = f / fc. Split layout: the same LrSplit magnitudes read at
  `analogBin (w, fc)`, the frequency where the bilinear crossover shows its analog prototype's value at f / fc, so every
  slope keeps its own shape and the top band (the rest) still makes the bands add up to the delayed input.
- `responseDb` (the drawn curve) follows the same axis in Linear phase.
- Minimum and Dynamic phase: no change (they match within 0.1 dB).
- No CPU cost: only the kernel design changes.

## Audition by phase mode (probe p19)

Impulse, band 1 from 500 Hz to the high edge, Audition on band 1, mode set by value (log `b6/p19.log`). dB:

| Case | 12 kHz | 16 kHz | 20 kHz |
|---|---|---|---|
| Pro-MB Minimum, 8 kHz, 24 dB/oct | −20.0 | −38.2 | −64.6 |
| Pro-MB Dynamic, 8 kHz, 24 dB/oct | −20.0 | −38.2 | −64.6 |
| LrSplit (prewarped), computed | −20.0 | −38.3 | −64.8 |
| Pro-MB Linear, 8 kHz, 24 dB/oct | −15.6 | −24.6 | −32.0 |
| Analog 1 / (1 + x⁴), computed | −15.7 | −24.6 | −32.1 |

Minimum 8 kHz / 48 dB/oct and 12 kHz / 24 dB/oct also follow the prewarped curve within 0.2 dB up to 18 kHz. So Audition
differs only in Linear phase, where the change above covers it (the detector hears the FIR band).

## ∞-ratio steps in Minimum phase (probe p20)

1 kHz stair, threshold −30, range −30, knee 0, attack and release 0 (log `b6/p20.log`), LYNX `b6/ly/r3*.wav`:

| Ratio | Rising steps, max difference | Falling steps |
|---|---|---|
| 0.99 (91.9 : 1) | 0.02 dB | 0.22 dB |
| top (∞) | 0.03 dB (Pro-MB −33.01 on every step, LYNX −33.03) | 0.22 dB |
| Expander 0.975 / top | 0.08 dB | 0.58 dB |

Round 172b6's numbers (0.45 / 2.2 dB on the falling side) were against Pro-MB's Linear phase; Pro-MB's own Minimum and
Linear phase differ by 0.23 / 1.65 dB there. The top as a true limit is confirmed in Minimum phase.

## After the change (LYNX `b7/ly_after_*.wav`, `b7/ly_au_*.wav`, same probes)

Compression (p18b cases), largest difference to Pro-MB from 4 kHz to 22 kHz:

| Case | Before | After |
|---|---|---|
| Linear, 8 kHz, 24 dB/oct | 0.6 dB (12 kHz −0.7 vs −1.2) | 0.1 dB (12 kHz −1.2 vs −1.2, 16 kHz −0.4 vs −0.4) |
| Minimum 8k/24, 12k/24, 8k/48, 4k/24, range 0; Dynamic 8k/24 | 0.1 dB | 0.1 dB (unchanged) |

(400 Hz: 0.2 dB in Linear phase before and after; 200 Hz up to 0.2 dB in every case, range 0 included.)

Audition (p19 cases), 250 Hz to 20 kHz: every case within 0.1 dB of Pro-MB, Linear phase included (16 kHz: −24.6
both; before −38.3). The only larger gap is at 22 kHz with 48 dB/oct, below −150 dB on both.

The unit tests' measured Linear-phase gap case (`prism_linear_and_minimum_free_round150`, two bands, 6 kHz) moved from
−0.33 to −0.39 dB, Pro-MB's −0.39.

## Tool fix

`tools/fxmeasure`: a `settext` whose text no value shows, an unknown parameter, or a plugin that then shows another
text than asked, now stops the script with exit code 3 (the old fallback set the value 0 without a word).

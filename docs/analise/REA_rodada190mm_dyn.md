# REA round 190mm (sub-round dyn): Waves Renaissance Vox, RDeEsser, C6, API-2500, MaxxBass, Renaissance Bass, Vitamin

Owner's permission of 2026-11-17 (REA on third-party binaries for parity, clean-room) and decision 2 of round 190mm (build
or fit stand-ins for the common Waves plug-ins that had no reference). Evidence markers: [REA] read from the V17 bundles
(Ghidra 12.1.4 headless decompiles of each bundle's GenericMultiCoreProcessor.dll — the DSP — and its control DLL, in
/mnt/e/lynx-work/waves4_190mm/dyn/ev/dc; ParamXML / ProcessXML in ev/xml), [EXEC] our own demo-mode captures of round
190mm's bank (cap/out/<tag>: D, X1 … X4 × drums / bass / guitar / vocal / mix, the demo mutes masked, plus each setting's
`stim` render for fitting), [model] our reading. No vendor code, table, preset or coefficient list was copied: the
structures below are described in our words, LYNX's engines are written from them, and every constant in them was fitted
on our renders (dyn/fit, dyn/lab). No licence or protection was touched (the DSP DLLs are unprotected; demo mutes
masked by the scorer).

Every stand-in runs a **hidden law** (no control, no string, saved while non-zero; 0 for new instances and every project
saved before; only its Waves translator sets it). Gold renders: §8.

## 1. Renaissance Vox → Compressor's Vox law

[REA] (RVoxSproc, 34 states, 20 coefficients; RVox.dll writes them) — the Renaissance family's chain, specialised:
- detector: per channel the magnitude of the input and of three first-order all-pass copies (fixed coefficients), the
  largest of both channels;
- a gate envelope (instant up, the Gate release's one pole down) and a gate gain = min (1, envelope / gate threshold)
  (off at −80 dB);
- the gain computer in linear gain g = T / (p (1 − T) + T), T = the Compression knob as a gain (no ratio: one knob);
- its minimum over a look-ahead window (the smallest power of two ≥ 1 ms: 64 samples at 44.1 / 48 kHz, 128 at 96 kHz);
- a slow follower s of that gain (fixed fall / rise times), the output gain y = the deeper of gain and follower, released
  with a coefficient read from two 256-step tables (a shape by 2s − y, a speed-up by s); the gate multiplies y;
- two boxcars (27 and 38 samples at the 64-sample window) over the gain in fixed point; the audio delayed by the window;
- the make-up 0.99 · Gain / T: a full-scale peak always leaves at −0.09 dB (the bank's D renders: −0.09 dB at every level).
[EXEC] Fit (lab/fitvox.py, Nelder–Mead on 6 bank renders, sample ESR): follower 249.5 / 501.5 ms, release shape 444.0 ms,
speed-up corners 0.5 / 31.9 / 152.3 ms (the shape exponent 4 kept as read); mean −59 → −70 dB.
LYNX: core/RenDynamics.h `rendyn::Vox` (+ `GainChain`, shared with §2); Compressor hidden [21] compressorVoxLaw, [22] gate,
[23] gate release, [24] output gain; the threshold knob is Compressor's Threshold. Translator `translateWavesRVox`,
entry `[waves-rvox]` (names "Renaissance Vox", "RVox").

## 2. RDeEsser → Hush De-esser's Renaissance de-esser law

[REA] (RDeEsserSproc; control: the crossover design, the dynamics coefficients):
- a crossover at Freq: a second-order section squared gives the low band, the section's own all-pass minus it the high
  band (in phase, so low + high = the all-pass); High-pass mode: a low-pass section (w √2, Q = the hidden 0.42), Band-pass
  mode: a notch section (its complement is a band around Freq);
- two envelopes: the low band's peak (attack / release = the state's hidden 24.9 / 248.7 ms) and the sibilance detector's
  (the mid of the high bands through a second-order Butterworth high-pass at 0.6 · Freq, twice; 0.5 / 1 ms);
- the threshold rises with the low band: a = min (e_low + 10^(max (T, −12)/20), 2) · 10^((T − max (T, −12))/20) — an
  absolute threshold that becomes relative to the rest of the signal (the hidden −12 dB corner); gain = a / max (a, e_hi),
  floored at the Range;
- the Renaissance chain of §1 (follower 15 / 30 ms, the same tables), the delay by the window;
- Split: out = low + G · high; Wide-band: out = G · input.
[EXEC] Fit (lab/fitess.py): follower 15.05 / 30.03 ms, release shape 480.5 ms, detector share 0.600; −76 → −78 dB.
LYNX: `rendyn::DeEsser`; Hush hidden [29] essLaw … [34] (threshold, range, frequency, band-pass, wide-band in the
reference's units). Translator `translateWavesRDeEsser`, entry `[waves-rdeesser]`. Monitor (side-chain listen) and the
hidden detector values (the same in every state we have) are not translated.

## 3. MaxxBass / Renaissance Bass → Low Lifter's bass law

[REA] (MB2Sproc, RBassProc): one crossover at Frequency made of a first- and a second-order all-pass (low = their mean,
high = half their difference: a third-order Butterworth pair whose sum is the second-order all-pass with Q = 1); the
original low band scaled (MaxxBass: Original bass gain; RBass: Orig In / Out); the low band's mono sum, level-normalised by
two power-law envelopes, drives a recursive product (harmonics), kept in a high band above the crossover, added at the
Intensity / Harmonics gain; output gain.
[EXEC] The bank's MaxxBass states keep Harmonics at −∞ (the default): the reference is linear there — our sweep: 0.0 dB
everywhere for D, X1, X4; X2 (100 Hz, −6 dB) −6.0 / −2.0 / −0.3 dB at 25 / 100 / 150 Hz; X3 −6 dB flat. The all-pass
identified from D's impulse: −96 dB against the Q = 1 pair at 80 Hz. Renaissance Bass is strongly non-linear (D adds
+9.7 dB around 80 Hz on the −18 dBFS sweep).
LYNX: core/BassHarmonics.h `bassharm::Engine`: the exact crossover and the generator written from the read structure
(the mono low band × pre, normalised by (|b| (1 − f) + f)^−p₁ smoothed, times (1 + its own clamped high band × feedback ×
the output normaliser), that high band's own all-pass pair at Frequency / r, a second high band at Frequency, normalised
by ^−p₂, peak-limited, × level × Intensity). Fitted on the RBass bank by Nelder–Mead on ESR + third-octave spectrum
(lab/fitrb2.py, 15 renders; the floors 0.00398 and the 5 ms smoothing kept as read): pre 1.458, feedback 0.0565, limit
0.326, level 0.756, p₁ 0.510, p₂ 0.213, release 47.8 ms, r 4.89. (A first, simpler generator of ours — v·|v| + c·v³ —
reached 3.72 on the bank, this one 4.92.) MaxxBass's harmonics (not in any bank state) use the same generator at its
Harmonics gain — not probed. Low Lifter hidden [15] bassLaw … [21]. Translators
`translateWavesMaxxBass` / `translateWavesRBass`, entries `[waves-maxxbass]`, `[waves-rbass]`.

## 4. API-2500 → Compressor's bus law

[REA] (API_2500Proc; a circuit-style model): a side-chain from the input (New) or the output (Old: feedback), the Thrust /
link filters, a mean-square detector in the log domain, a knee polynomial (Hard / Med / Soft), an implicit RC solve for
the ballistics, gain = exp (…); output = make-up · ((1 − mix) + mix · gain); the auto make-up follows the threshold.
LYNX (our own, much simpler structure, core/BusComp2500.h `buscomp::Engine`): mean-square detector (fitted time), threshold
calibration, a quadratic soft knee per knee setting, dB-domain attack / release (the knobs × fitted scales), Thrust as a
first-order high-pass share in the side chain, feedback mode on the previous output, auto make-up K · (ref − threshold) ·
(1 − 1/ratio), the feedback type's loop gain = k · (ratio − 1), the detector threshold = cal + slope · knob. Fits:
lab/fitapi.py (18 renders, sample ESR −8.9 → −18.9 dB), lab/fitapi2.py (all 25, ESR + 8 × |level error|, the feedback
loop gain added), lab/fitapi3.py (the threshold's slope): cal −23.19 dB, slope 1.064, knees 23.4 / 13.0 / 9.6 dB, attack
× 2.68, release × 0.386, detector 9.9 ms, thrust 80.6 Hz × 0 / 0.64 / 0.70, make-up 0.336 · (10.78 − thr) · (1 − 1/r),
loop 0.521 · (r − 1). The fixed ballistics of the reference's RC network and its analogue extras (Analog, link shapes)
are not modelled. Compressor hidden [25] compressorApiLaw … [37].
Translator `translateWavesApi2500`, entry `[waves-api-2500]` (API-2500 removed from the generic `[waves-compressors]`).

## 5. C6 → Prism's C4 law + floating bands

[EXEC] The C6 state is the C4's layout (crossovers 0 … 2, Q 3, bands at 14 + 14 b) for its four crossover bands (b = 0 … 3)
plus two floating bands (b = 4, 5; frequency / Q at 98 / 99 and 102 / 103). The four bands run round 190mg's C4 law
unchanged (`translateWavesC4`). The floating bands (our model): a band-pass of the crossovers' sum at the band's frequency
(its Q × 0.67, fitted on the X2 renders: Q × 1.0 / 0.8 / 0.7 / 0.65 / 0.5 scanned; a threshold offset of −3 / +3 dB put
a file down), its level through the same gain law and
smoother as the bands, added back as (G − 1) · the delayed band-pass. Prism hidden c6Float0 + 8 k + field.
Translator `translateWavesC6`, entry `[waves-c6]`.

## 6. Vitamin → Aether's Vitamin law

[REA] (control code): the crossover design is the C4's (a squared biquad with a Q; the same formula as Prism's C4 law).
[EXEC] The default state is an all-pass (the sweep: 0.0 dB); its impulse fits a tree of four such crossovers whose sum is
their all-passes' product to −56 dB with each crossover's frequency × 0.944 / 0.957 / 0.938 / 0.934 and Q 0.617 / 0.574 /
0.562 / 0.626 (lab/fitvitap.py). The bands' gains (12 … 20) add a level-dependent part (the bank: X vs D −14 … −35 dB).
LYNX: core/VitaminLaw.h `vitamin::Engine`: the five bands, the direct path = their sum × Direct gain, each band with a gain
adds our own enhancement (a tanh-saturated copy of the band at a fitted drive and a per-band level, × the gain), fitted
on the 20 X renders (lab/fitvit2.py: drive 0.240, levels 0.0200 / 0.0182 / 0.0167 / 0.0316 / 0.0330). Aether hidden [10] vitLaw … Translator `translateWavesVitamin`, entry `[waves-vitamin]`. Punch
and the bands' widths are not translated.

## 7. Results (round 190mm bank, 25 files each; scorer scripts/fxparity.py with the demo-mute masks)

Before = /mnt/e/lynx-work/waves4_190mm/runs/base (no stand-in: n = 0, except API-2500: the generic Waves compressor
preset); after = dyn/runs/final (bin/fxr_dyn of this branch).

| reference → LYNX | files | before → after | scores after | sample ESR after |
|---|---|---|---|---|
| Renaissance Vox → Compressor (Vox law) | 25 | n = 0 → 9.52 | 13 × 10, 12 × 9 | −66 … −114 dB |
| RDeEsser → Hush De-esser (Renaissance de-esser law) | 25 | n = 0 → 9.92 | 23 × 10, 2 × 9 | −69 … −110 dB |
| C6 → Prism Multiband (C4 law + floating bands) | 25 | n = 0 → 8.56 | 2 × 10, 10 × 9, 13 × 8 | −31 … −75 dB |
| API-2500 → Compressor (bus law; own entry) | 25 | 2.36 → 5.88 (up 25 / same 0 / down 0) | 8 × 4, 7 × 2, 6 × 7, 5 × 11, 4 × 1 | −10 … −38 dB |
| MaxxBass → Low Lifter (bass law) | 25 | n = 0 → 9.56 | 18 × 10, 3 × 9, 4 × 8 | −46 … −95 dB |
| Renaissance Bass → Low Lifter (bass law) | 25 | n = 0 → 4.92 | 7 × 3, 6 × 1, 5 × 14, 4 × 5, 3 × 2 | −9 … −29 dB |
| Vitamin → Aether (Vitamin law) | 25 | n = 0 → 7.16 | 8 × 14, 7 × 4, 6 × 4, 5 × 3 | −18 … −47 dB |

What limits the rest: C6's floating bands are our own model (X2 at 8); API-2500 is a circuit model in the reference
(RC network, knee polynomials) against our simple law (feedback type and X4's drums weakest); Renaissance Bass's
harmonics are recursive and phase-sensitive (its spectrum is close, its waveform not); Vitamin's enhancement is our own
(D, the all-pass part, is at 8 everywhere).

## 8. Old projects, CPU, tests

- Gold (dyn/lab/gold.sh): Compressor (default, classic, FET, vintage; RComp S1 / X1, SSLComp X2, Pro-C 3 S2), Hush
  (default, classic, vocal voicings; Waves DeEsser X1 / X3, Pro-DS S1), Prism (default, deep; C4 D / X1 / X3, Pro-MB S2),
  Low Lifter (default, settings), Aether (default, classic, dynamic) at 44.1 / 48 / 96 kHz × vocal / mix / drums: base
  binary (origin/waves4_190mm 3d98edb7b) vs this branch: 225 renders, 0 differ.
- CPU (dyn/lab/cpu.sh: wall seconds, best of 5, one core, the 35 s stereo mix at 48 kHz, file I/O included; a plain
  copy 0.87 s): RVox X2 0.94, RDeEsser X4 1.11, C6 X2 2.86 (D 2.23: the C4 law itself), API-2500 X1 0.91 (the generic
  preset 0.91 before), MaxxBass X2 0.92, RBass X1 1.15, Vitamin X4 1.47.
- Tests: tests/Parity190mmDynTests.inc (6 tests: hidden laws off for new and old slots; RVox's static law against the
  reference's stairs (≤ 0.2 dB) and its latency / gate; RDeEsser's split, range and wide-band; MaxxBass's all-pass and
  original-bass curve against the reference's sweep (≤ 0.15 dB), RBass's harmonics; Vitamin's all-pass and direct gain;
  C6's floating band; API-2500's make-up and compression).

## 9. Not done

- RDeEsser Monitor (side-chain listen) and the hidden detector values (fixed in every state we have); Vitamin's Punch
  and band widths; API-2500's Analog, link shapes and L/R link; MaxxBass's harmonics (no bank state uses them: they run
  the RBass-fitted generator, unprobed); C6's ARC / Manual and Electro / Opto beyond the C4 law's own; the side-chain
  input of all of them.
- Rates: the Renaissance chains use the reference's window law (64 / 128 / 256 samples); none of the laws was probed at
  44.1 / 96 kHz in this round (the bank is 48 kHz).

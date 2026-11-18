# REA round 190lf — the Waves DSP binaries, third pass (owner's permission of 2026-11-17)

Scope: the brief's Waves list after rounds 190kt / 190ky — L2, CLA-2A, the three Waves reverbs, H-Delay, PuigTec EQP1A,
Doubler2 / NS1 / Waves Tune Real-Time and the CLA-76 leftovers. The coordinator took L2 and PuigTec (this file, §1–§5);
two sub-rounds took CLA-2A + CLA-76 ("dyn", `REA_rodada190lf_dyn.md`) and the reverbs + H-Delay ("verb",
`REA_rodada190lf_verb.md`). Tools as in 190ky: the rea MCP (Ghidra 12.1.4 headless, `decompile_all` into
`/mnt/e/lynx-work/waves2_190lf/ev/`), the bundles' ParamXML and black-box renders in Waves demo mode on refcap030's
fxmeasure host (hidden, no editor, no input sent, the trial lock only). Clean-room: what is recorded here is structure,
behaviour and parameter meaning; LYNX's code is written from this description and every constant it uses is fitted on our
own renders. No vendor table, preset or code was copied; no licence check was touched; the demo's periodic mutes are
masked out of every measurement.

## 1. A host finding: `set` does not reliably reach the Waves DSP [EXEC]

On refcap030's host, `defaults` followed by `set "<param>" v` left some Waves settings at the previous setting's state
(L2: Release changes ignored; PuigTec: one in four probe settings rendered with its predecessor's curve; `savecomponent`
after `defaults` + `set` returned the last loaded component unchanged). `loadcomponent <state>` is reliable, and a `set`
right after a `loadcomponent` worked (L2). Every probe of this round therefore runs from a component state: refcap182's
saved states, or states written by `lab/mkcomp.py` (refcap182's state with SETUP_A's RealWorld values replaced; the two
big-endian length fields are the file length − 34 and − 62). The sub-rounds checked their own probes (dyn: every setting
took effect; settext used for dials).

## 2. Waves L2 → Limiter, the L2 law [DECOMP: `waves190ky/ev/l2_gmcp/functions/0x18000112c.c`; EXEC: `cap/l2a…l2d`]

Structure (190ky §6, read again in detail): per sample the gain the input needs (`threshold / |x|` over the threshold,
stereo linked — confirmed black-box), the minimum of that over the look-ahead window (a log2 tree of running minima), a
follower of the minimum (attack / release coefficients by direction; the output takes the smaller of the two), a release
stage (instant down; up with a coefficient that is the manual release or, with ARC, a function of the gain and of the
follower), then the gain smoothed by two cascaded moving sums (integer accumulators) and applied to the delayed audio
with the make-up; IDR dither / noise shaping last. LYNX's own description of each part, measured:

| part | measurement | LYNX (core/LimiterLaw.h) |
|---|---|---|
| make-up | the small-signal gain is exactly ceiling − threshold (X1 2.700 dB, X2 5.700, X3 4.999); at Ceiling 0 dB the gain is 0.999 / threshold (−0.0087 dB) | Gain knob = ceiling − threshold, make-up min (ceiling, 0.999) / threshold; a threshold above the ceiling: hidden trim |
| window / smoothing | a single-sample spike on a DC carrier: the gain dips 64 samples before it, back 64 after; the dip shape = box(64) ∗ box(27) ∗ box(38) (max error 0.0005 linear) | window 64 at 48 kHz (= latency, the reference reports 64), moving averages of 27 and 38 samples, scaled with the rate |
| manual release | ARC off, the dial 10 / 100 / 1000 ms: a linear-gain recovery with τ = dial / ln 10 (the dial is the 90 % time); no follower | the same; the Release knob 10 … 1000 ms |
| ARC | bursts of 5 ms … 4 s at 3 … 20 dB of reduction: short peaks recover in ~2 ms to a small residue that fades in ~0.5 s; sustained limiting recovers slowly (−12 dB → −3.2 dB after 200 ms) | follower τ 107 ms down / 218 ms up; release rate exp (q0 + q1 u + q2 v + q3 uv + q4 u² + q5 v²), u = ln gain, v = (follower − gain) / follower; fitted on 8 probes: 0.061 dB rms over every sample after the lead (refcap182's scored renders not used) |
| dither | IDR (Type 1, Normal shaping at 24 / 16 bits) | not modelled (noise far below the scores' thresholds) |

The law is behind the Limiter's hidden parameter [3] (0 = the classic limiter, exactly as before; [4] ARC, [5] trim). The
c simulation (`lab/c/l2sim.c`) of the law predicted refcap182's programme renders at ESR −110 … −120 dB (D), −56 … −123 (X1),
−55 … −69 (X2), −85 … −115 (X3, manual 100 ms), −56 … −70 (X4) before the C++ port; the table in §4 is the C++ binary's.

## 3. Waves PuigTec EQP1A → Facet EQ [DECOMP: `ev/puigtec_gmcp`; EXEC: `cap/pta…ptf`, 1 000 impulse probes]

There was no substitute entry for PuigTec (refcap182 n = 0: a project with it lost the plug-in). Read: `PuigTecProc.A`
dispatches to a block processor with a resampling / block-convolution frame (frame sizes from log2 of the block), a
mains-hum generator (a triangle oscillator through shaping filters, the Mains switch) and an LCG noise source; the state
carries per-instance component tolerances (the RealWorld list's 0.96 … 1.38 values). Black-box: the impulse response is a
minimum-phase EQ curve behind a linear-phase resampler (peak at 149 samples at 48 kHz with pre-ringing), +1.10 dB of
insertion gain, −0.46 dB at 20 kHz; harmonics −50 dB at −4 dBFS (not modelled). The Low Frequency switch alone lifts the
top by up to 0.7 dB (20 Hz position). The knobs are strongly non-linear (Low Boost 2.75 → +1.2 dB, 5.5 → +7.2, 11 →
+16.6 at 100 Hz) and the low boost / atten interact (the boost-and-dip "trick": together they are not the sum).

RealWorld list (from crafted states): 0 output gain dB, 6 high boost, 7 bandwidth, 8 high frequency (1 … 7 = 3, 4, 5, 8,
10, 12, 16 kHz), 9 low atten, 10 atten select (1 … 3 = 5, 10, 20 kHz), 11 low frequency (1 … 4 = 20, 30, 60, 100 Hz),
12 low boost, 14 high atten (knobs 0 … 11), 15 in / out, 16 mains.

LYNX: eight Facet EQ bands (zero latency, minimum phase): the flat response (output + a high and a low shelf, 0.012 dB rms),
the Low Frequency switch's lift (two shelves per position), the low section on two shelves whose six values are cubic
polynomials of both knobs (fitted jointly on 80 probes per Low Frequency, Levenberg–Marquardt), the high boost on a bell
and the atten on a 12 dB/oct high shelf, both interpolated over a 1.375-step knob grid × five bandwidths (per-probe fits
≤ 0.25 dB rms). All values are LYNX's fits written by `lab/gen_pt.py` into `translateWavesPuigTec`. Facet's own code is
unchanged (its gold renders: docs/PARITY.md "Round 190lf").

## 4. Results (refcap182 programme, scorer `scripts/fxparity.py` of this branch = round 190ky's)

| reference → LYNX | files | before → after | up / same / down |
|---|---|---|---|
| L2 → Limiter (L2 law) | 25 | 7.32 → 9.52 | 25 / 0 / 0 |
| PuigTec EQP1A → Facet EQ | 25 | n = 0 → 7.60 | — |

L2 per setting: D 9.0 → 10.0, X1 7.2 → 9.8, X2 6.6 → 8.8, X3 6.0 → 10.0, X4 7.8 → 9.0. PuigTec: D 8.0, X1 8.0, X2 6.8,
X3 8.0, X4 7.2 (the predicted curve errors against the measured states: 0.01 / 0.13 / 0.20 / 0.01 / 0.18 dB rms). The
sub-rounds' results are in their files and in docs/PARITY.md "Round 190lf".

## 5. Not done

Doubler2 (4.48) and Waves Tune Real-Time (5.36): time (each a time-varying process that needs its own REA and probe
set; their baselines stay). NS1: a separate sub-round after the 04:56 crash (`REA_rodada190lf_ns1.md`: 0 % exact, 4.84 → 5.40; 20 … 100 % next). PuigTec's mains hum, the resampler's fractional delay and the tube stage's harmonics are not modelled.

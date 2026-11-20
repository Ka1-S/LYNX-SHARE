# REA round 190mt (0.1.30): Waves, part 5 — SSL G-Channel EQ, RChannel's RVox type, Q10 wide bells, API-560, RVerb

Owner's permission of 2026-11-17: clean-room analysis of third-party plug-ins for parity. Black-box identification on our own
demo-mode renders of the Waves V17 plug-ins (refcap030's hidden host, no editor, no input sent; two takes per probe so a
demo mute never hits both) plus one Ghidra session (headless, 5 min) on the API-560 and H-Reverb DSP libraries, read for
their structure and laws only. No vendor code, table, coefficient, preset or impulse response is in LYNX: every constant
LYNX ships is a fit on our probes. Work folder /mnt/e/lynx-work/waves5_190mt (HANDOFF.md, eq/, cap/, lab/, ev/, runs/).
Base: round 190mm's final binary and scores (bank wv190mm, masked).

## 1. SSL G-Channel: the G's own EQ sections (Ridge Strip, hidden G law)
- **Probes** (eq/lab/lists5.py, 7 900 × 2 takes): LF shelf at 15 frequencies × every dB from −17 to +17, HF shelf 14 × 34,
  LMF / HMF bells at 11 / 12 frequencies × 20 gains (±2 … ±20) × 10 Qs (0.1 … 3.5), the LMF ÷3 / HMF ×3 switches 11 / 12
  frequencies × 20 gains × 4 Qs.
- **Structure [EXEC]**: every band is ONE second-order section (Prony residual −100 … −150 dB). The bells are peaking
  sections (pole and zero pairs at the same frequency, DC and Nyquist gain 1) whose pole / zero Qs follow the gain
  (proportional Q: at Q 1.5 the zero-Q falls from 1.9 at −20 dB to 0.15 at +20 dB) and whose centre moves with gain and Q
  and falls below the dial above ~1 kHz (LMF at 2.5 kHz: 0.65 × the dial). The LF shelf is (s² + βs + Wz²)/(s² + βs + Wp²)
  with a shared β of 2π·2.3 … 10 kHz (a first-order slope below β: the analog shelf with its upper corner), the HF shelf
  its mirror; both overshoot their dial (+12 dB → +14.1 dB at DC) and the gain law kinks between ±6 and ±8 dB. The
  ±17 / ±20 dB ranges are the G's (the E voicing clamps at ±15).
- **LYNX**: echannel::Settings::gLaw (core/VoiceMatch.h) — each band's analog prototype taken from tables of LYNX's fits
  (core/SslGTables.inc, eq/lab/gentab_g.py: per probe a Steiglitz–McBride section on the response / the flat probe's,
  inverse-bilinear at 48 kHz, stored as ln of its five coefficients), multilinear in log-frequency / gain / Q, bilinear
  at the rate with the E voicing's rate scaling. Ridge Strip's hidden [87 … 99] (gEqLaw, the ten band values on the G's
  scales, ÷3 / ×3), saved while non-zero, set only by the SSL G-Channel translator; 0 = the E bands bit for bit.
  Full-path check (crafted states through the translator, eq/lab/gval.py): grid points −108 … −153 dB, between grid
  points −36 … −64 dB.
- Not changed: the G's high / low pass (they differ from the E's at the ends of their ranges: HP 200 Hz −26 dB, LP 3 kHz
  −14 dB; HP 80 Hz −60 dB) and the G compressor (X1 / X2 / X4 sit at 7 … 8 on the compressor now).

## 2. Renaissance Channel: the "RVox" type and the gate
- **Probes** (cap/out/rch, the 7 s stimulus, 16 settings): with the RVox type the ratio is ignored (0.5 / 1 / 2 / 50 give
  identical output) and so is ARC; attack and release still act; the static curve is the vocal compressor's family
  1 / (1 + p (1/t − c)) with NO make-up (−24.5 dB at 0 dBFS peak for a −24 dB threshold); c = 0.1 fitted on thresholds −24
  and −12 (the RVox plug-in's own law has c = 1).
- **Release**: out of deep reduction the type recovers fast and slows near none (after −9.5 dB: −6.5, −5.3, −4.4 dB over
  the first 30 ms at release 160 ms; at 1000 ms −8.9, −8.5, −8.2) — the Renaissance law's Opto release shape, not its
  Electro one.
- **LYNX**: Compressor's hidden [38] compressorRVoxType on the Renaissance law (RenSettings::voxCurve: that curve, no floor,
  no bend, the Opto release shape; the plain peak detector), set by the RChannel translator from RealWorld 44. Tried and not
  kept: the all-passed detector copies (X3 6.56 → 6.44), the manual (non-ARC) release (6.48), the release × 0.6 (same).
- **Gate** (RealWorld 57 on, 58 gate / expander, 59 threshold (−80 = −inf), 60 floor, 62 release) [EXEC]: its detector
  holds the last loud level and falls with max (0.14 s, release) (−18 → −40 dB in 0.35 s at release 22 and 100 ms; never
  within 0.2 s at 1 s); below the threshold the gain falls to the floor as a one-pole with the release as its time
  constant (≈ 9 dB per release time far from the floor), and opens within a millisecond; the expander mode is a 1:2
  expander down to the floor. LYNX: Compressor hidden [39 … 43] (on, threshold, floor, release, mode) before the
  Renaissance law, set by the RChannel translator when the gate is on with a finite threshold; on the probes the gate's
  closing time and ramp land within one 20 ms column (the expander closes ≈ 80 ms later than the reference's). The bank's
  X4 (−40 dB threshold) does not move: programme rarely falls under −40 dBFS; X4's error is its compressor's (as X1 / X2).

## 3. Q10: the bell's exact law
- **Probes** (eq/out/q10c: 14 frequencies 0.5 … 20 kHz × ±6 / ±12 dB × Q 0.5 … 15, Bell and PQ-Bell): the Bell is the
  cookbook peaking section (b1 = a1, gain and centre exact) with
  alpha = tan (min (π f / (Q fs), π / 4)) / (0.090904 (√G + 1/√G)) — exact to 4e-5 over 336 probes: the bandwidth
  follows f / Q and is capped at fs / 4 (round 190mm's bandwidth-prewarped law missed the wide bands above 3 kHz by
  −45 … −53 dB). PQ-Bell (eq/out/q10pq: 3 … 12 kHz × Q 0.5 / 1 × every 1.5 dB to ±18): the same section with
  alpha = θ (1 + β θ²) / k, θ as above, k round 190mm's constant-Q factor (refound to 4 digits) and β falling from 0.206 at
  1.5 dB to 0.0018 at 18 dB (fitted every 1.5 dB; alpha within 4e-3 at 1.5 dB, 1.3e-4 from 7.5 dB).
- **LYNX**: Facet's Waves design codes 14 q10BellTan and 15 q10PqBellX (codes 2 / 3 stay for round 190mm's projects); the
  Q10 translator uses them. Through the translator (crafted states): Bell 10 kHz Q 1 −30.7 → −98.5 dB, PQ 12 kHz / 9 kHz /
  16 kHz −33 / −19 / −50 → −81 / −95 / −94 dB.

## 4. API-560: a graphic-EQ network, oversampled, 65 samples late
- **Black box [EXEC]**: with every slider at 0 the output is the input delayed by exactly 65 samples (bypass); with any
  slider moved, a fixed linear-phase kernel appears (the same pre-ringing for every band), flat to 22 kHz (−1.1 dB at
  23 kHz, −5.9 dB at 23.9 kHz); time-invariant to −110 dB (impulse on an odd sample); 65 samples of latency at 44.1, 48
  and 96 kHz. Sliders in different groups multiply exactly (1 kHz + 2 kHz, 4 kHz + 8 kHz, 63 + 125 Hz: −110 dB); two in
  the same group (1 kHz + 16 kHz) do not (−7 dB): they interact. No single (or 8th-order) section fits one slider well.
- **Structure [DECOMP]** (ev/api560_gmcp, ev/api560_main): the input is zero-stuffed by M (the loop count) and runs
  through two groups of five second-order resonators in a graphic-EQ network — the group's node u = x − Σ p_k y_k, the
  output u + Σ (1 − p_k) y_k, each resonator fed by the node one sample late — then a decimating FIR. Each slider is a
  potentiometer p = 0.5 − g / B (clamped) that also loads its resonator: R = D / (N + p (1 − p) C D) (the analog
  prototype, bilinear at the oversampled rate).
- **LYNX** (core/Api560.h + Api560Law.inc, eq/lab/g560c.py / gen560.py): that network with LYNX's own constants fitted
  on our single-slider probes (M = 4 at 48 kHz: per band w, Q, a and a numerator s + d0 + d2 s²; B = 24.00 / 23.99 and
  C = 4.76 / 4.78 — the two groups alike): −70 dB (group A) / −62 dB (group B) over all probes, the bank's X1 … X4
  responses −53 … −100 dB per octave band (the 31 / 63 Hz bands are the weakest: −44 … −56 dB below 80 Hz); a Kaiser
  decimator (β 8, 40 base samples each side, cut-off 0.4924 fs: fitted to the reference's measured roll-off, 0.03 dB rms
  from 18 to 23.9 kHz) + a 25-sample line = 65 samples; a plain 65-sample delay with every slider at 0. Facet EQ's hidden [135] api560 law + [136 …] slider gains (saved while
  non-zero, set only by the API-560 translator); Facet's latency includes the 65 samples (PDC), old projects report 0.

## 5. RVerb / H-Reverb
Round 190lf measured the ceiling (RVerb exactly LTI; an independent reverb with the same envelope and third-octave
energy scores 3.6 … 3.8): only a sample-accurate early / late structure could pass it. The DSP libraries (RVerb's
decompiled by round 190lf, H-Reverb's in this round: ev/hreverb_gmcp, six 24 kB process variants) are circular-buffer
networks whose delay lengths, taps and gains come from tables chosen by the rate (buffer masks 2047 / 4095 / 8191). A
sample-accurate LYNX network would need those lengths and gains: reading them out is not allowed, and identifying a
full FDN of that size from impulse responses is a research project beyond this round's time box. Stopped here (the
brief's acceptable result); no reverb code changed.

## 6. Results (bank wv190mm, masked; before = round 190mm's final binary and scores)
| reference → LYNX | files | before → after | up / same / down |
|---|---|---|---|
| SSL G-Channel → Ridge Strip (+ G EQ law) | 25 | 6.44 → 8.20 | 17 / 8 / 0 |
| Q10 → Facet EQ (Bell / PQ-Bell laws) | 25 | 9.68 → 10.00 | 5 / 20 / 0 |
| API-560 → Facet EQ (API-560 law) | 25 | 8.44 → 9.72 | 19 / 6 / 0 |
| Renaissance Channel → Compressor (RVox type, gate) | 25 | 5.96 → 6.80 | 5 / 20 / 0 |
| the other 12 wv190mm tags, the 21 refcap182 Waves tags | 835 | same | 0 down |

## 7. Gates
- Gold (lab/gold190mt.sh, round 190mm's final binary vs this round's): Facet (default, bands, cuts, dynamics, natural
  phase, PuigTec, Pro-Q 4, REQ 6, API-550A, and the params round 190mm's Q10 / API-560 translations stored), Ridge Strip
  (default, SSL E-Channel ×2, the stored SSL G params, the omni law), Compressor (default, vintage, RComp ×2, RVox, RChannel
  X1, the stored RChannel X3 / X4 params) × 44.1 / 48 / 96 kHz × 3 signals: 234 renders, 0 mismatches.
- CPU (lab/cpu190mt.sh, 35 s stereo mix at 48 kHz, best of 5 wall clock, one core, load ≈ 5; a plain copy 0.60 … 0.76 s):
  API-560 X1 / X3 0.62 / 0.61 → 0.91 / 0.86 s (the 4× network + decimator, both channels as one vector: ≈ 0.8 % of a core
  more than round 190mm's Facet bands); SSL G X1 / X3 0.77 / 0.94 → 0.80 / 0.67, Q10 X1 0.99 → 0.58, RChannel X3 / X4
  0.66 / 0.62 → 0.58 / 0.59 s (same cost within the noise).
- Tests: tests/Parity190mtTests.inc (7: the laws hidden / inert / saved only while set, their shapes, the translators,
  API-560's 65-sample latency at 44.1 / 48 / 96 kHz and its plain delay when flat, the gate); mini runner 544 657 checks,
  0 failures.

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
- **LYNX**: Compressor's hidden [38] compressorRVoxType on the Renaissance law (RenSettings::voxCurve: that curve, no floor,
  no bend, the all-passed detector copies), set by the RChannel translator from RealWorld 44.
- **Gate** (RealWorld 57 on, 58 gate / expander, 59 threshold, 60 floor, 62 release): its detector falls from the last loud
  level for ≈ 0.35 s before a −40 dB threshold closes (independent of the release between 22 and 100 ms, never within
  0.2 s at 1 s); the gain then ramps to the floor (≈ 60 ms at 22 ms, ≈ 250 ms at 100 ms). Not modelled this round
  (X4's error is dominated by its compressor, as X1 / X2's).

## 3. Q10: the bell's exact law
- **Probes** (eq/out/q10c: 14 frequencies 0.5 … 20 kHz × ±6 / ±12 dB × Q 0.5 … 15, Bell and PQ-Bell): the Bell is the
  cookbook peaking section (b1 = a1, gain and centre exact) with
  alpha = tan (min (π f / (Q fs), π / 4)) / (0.090904 (√G + 1/√G)) — exact to 4e-5 over 336 probes: the bandwidth
  follows f / Q and is capped at fs / 4 (round 190mm's bandwidth-prewarped law missed the wide bands above 3 kHz by
  −45 … −53 dB). PQ-Bell's alpha also depends on f / Q but with a gain-dependent warp (kept on round 190mm's law).
- **LYNX**: Facet's Waves design code 14 q10BellTan (code 2 stays for round 190mm's projects); the Q10 translator uses it.

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
  responses −53 … −100 dB per octave band; a Kaiser decimator (β 8, 24 base samples) + a 41-sample line = 65 samples, a
  plain 65-sample delay with every slider at 0. Facet EQ's hidden [135] api560 law + [136 …] slider gains (saved while
  non-zero, set only by the API-560 translator); Facet's latency includes the 65 samples (PDC), old projects report 0.

## 5. RVerb / H-Reverb
Round 190lf measured the ceiling (RVerb exactly LTI; an independent reverb with the same envelope and third-octave
energy scores 3.6 … 3.8): only a sample-accurate early / late structure could pass it. The DSP libraries (RVerb's
decompiled by round 190lf, H-Reverb's in this round: ev/hreverb_gmcp, six 24 kB process variants) are circular-buffer
networks whose delay lengths, taps and gains come from tables chosen by the rate (buffer masks 2047 / 4095 / 8191). A
sample-accurate LYNX network would need those lengths and gains: reading them out is not allowed, and identifying a
full FDN of that size from impulse responses is a research project beyond this round's time box. Stopped here (the
brief's acceptable result); no reverb code changed.

## 6. Results (bank wv190mm, masked; before = round 190mm's final)
See the HANDOFF for the final table, CPU and gold counts.

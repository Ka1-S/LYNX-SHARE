# REA round 190mg (0.1.30): Waves DSP, fourth pass — coordinator part (C4, the Waves without a stand-in)

Owner's permission of 2026-11-17: clean-room static analysis of third-party binaries for parity. The structure was read,
every LYNX number was fitted on our own demo-mode probes; no vendor code, table or preset was read out (the C4's release
coefficient tables were located and left alone). Sub-rounds: ns1 (REA_rodada190mg_ns1.md), voc (…_voc.md), dyn (…_dyn.md),
ren (…_ren.md).

Scorer: scripts/fxparity.py with round 190lg's time window (scripts/flref_compare.py, cherry-picked 715b13037; refcap182 is
48 kHz only, so identical to the old one). Before = origin/waves2_190lf (2c893d3ca) on the same scorer.

## 1. Survey: the Waves with references
refcap182 holds 21 Waves references (refcap030 has none: demo-mode mutes). Four had no stand-in at all (a project lost the
plug-in): C4, Scheps Omni Channel 2, Abbey Road Plates, Atlas Reverb. Below 6: the three reverbs on Reeverb 2 (round 190lf:
a table ceiling near 3.6 … 3.8), Doubler2, Waves Tune, NS1 (sub-rounds). Common Waves without any reference: Renaissance EQ /
Vox / Channel / DeEsser / Bass, SSL G-Channel, API-2500 / 550 / 560, C6, Vitamin, MaxxBass, REQ, Q10 — installed in demo
mode; references would carry the demo's ~20 s mutes (scorer would need a mask: owner decision, see the HANDOFF).

## 2. Waves C4 → Prism Multiband, hidden C4 law
DSP: GenericMultiCoreProcessor.dll (C4NativeProcStereo.A / .P), control C4.dll (ev/: c4/ev/gmcp, c4/ev/ctl).
Structure (static analysis) and laws (black-box, probes c4/cap/p1 … p5, 1.5 s lead, demo mutes masked):
- **Crossovers**: each band is the all-pass chain times a real weight: with Q(s) = 1 + s/(q w) + (s/w)², low = 1/Q(s)²,
  all-pass = Q(−s)/Q(s), high = all-pass − low (in phase with the low output), w set so that both are −6 dB at the
  nominal frequency (bilinear, warped there; q = the Crossover Q control, 0.6 by default). Bands: low · AP2 · AP3,
  high1 · low2 · AP3, high1 · high2 · low3, high1 · high2 · high3. Model vs probes: flat −90 dB ESR, one band +6 dB −87 dB.
- **Static gain**: g = 1 / (1 + k · peak / threshold), the knee's cubic g + 2·knee·g²(1 − g) (≤ 1), band gain
  range + (1 − range)·g (linear). k = 0.5 · 2^(−R/12) at knee 0 (R = range dB, ±0.5 % on six ranges), times
  exp (s(R) · (knee/0.5)^0.78) with s(R) measured at knee 0.5 on nine ranges −24 … +18 dB. Every stair (25 of them:
  knee 0 / 0.5 / 0.75, ranges −24 … +18, thresholds −20 / −30 / −50, gain, output) within 0.003 dB rms. Stereo linked
  (a tone in L only compresses like both), peak detection (a two-tone at the same peak like a sine).
- **Time**: the gain computer's minimum over the look-ahead (64 samples up to 50 kHz, 128 at 96 kHz: latency reported by
  the host), attack, a fast release and a slow follower (the deeper of the two), two 32-sample box filters, the audio
  delayed by the look-ahead. Constants: fitted on 15 burst probes (1 kHz, 60, 150, 300 Hz; attack 0.5 … 100 ms, release
  5 … 1000 ms) — see the HANDOFF for the values and the per-probe ESR.
- LYNX: dyn::prism::c4Law (hidden [157], with the crossover Q at [158]; saved while non-zero, 0 = Prism's own engine for
  every project saved before), its own engine class (c4::Engine in DynamicsSuite.cpp). The translator (translateWavesC4,
  `[waves-c4]`) puts the RealWorld list on Prism's four bands in the C4's own units.

## 3. Abbey Road Reverb Plates → Foil Plate, Atlas Reverb → Atrium (new entries)
Both references are 100 % wet (no dry path in their states) with long tails. Their states' moving values (read from
refcap182's five states): AR Plates 324 pre-delay, 326 the decay in seconds (plate + Damper combined), 334 plate, 335
damper, 336 bass cut, 378 treble, 382 drive; Atlas 2 decay s, 17 pre-delay ms. Translated onto Foil Plate's measured plate
voicing / Atrium. The plates are convolution-like and strongly coloured per plate (impulse probes w5/cap: ±10 dB per
octave between plates), so only the decay, pre-delay and a common +5 dB high shelf (the plates are brighter above 5 kHz in
every state) are translated.

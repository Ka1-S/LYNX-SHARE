# REA round 190mm (0.1.30): Waves, fifth pass — coordinator part (NS1 0 %, demo-mode bank + masked scorer, the EQs)

Owner's permission of 2026-11-17: clean-room analysis of third-party plug-ins for parity. This part is black-box only (no
binary was opened): every structure below was identified on our own demo-mode renders of the Waves V17 plug-ins (refcap030's
hidden host, no editor, no input sent) and every LYNX number was fitted on them; one digitisation is a published design
(Orfanidis 1997). Sub-rounds: scheps (REA_rodada190mm_scheps.md), dyn (REA_rodada190mm_dyn.md). Work folder
/mnt/e/lynx-work/waves4_190mm (HANDOFF.md, cap/, eq/, lab/, runs/).

## 1. NS1 at 0 % (owner's decision)
Round 190mg's NS1 law kept Amount 0 on the zero law's bare pair (round 124's 60.07 Hz / 16 kHz all-pass pair, −54 dB from
the reference's). Both laws are unreleased, so 0 % now runs the NS1 engine's own open pair too: refcap182's D files
bass −52.6 → −79.7, mix −54.4 → −76.5, vocal −66.7 → −79.1, guitar −70.6 → −82.5 dB ESR (drums −40.0 → −40.3); NS1
7.04 → 7.24 (up 3, same 22, down 0). Quell without the NS1 law is untouched (gold renders, §6).

## 2. The demo-mode bank and the masked scorer (owner's decision)
- **Captures** (cap/): 16 plug-ins without any reference — REQ 2 / 4 / 6, RVox, RChannel, RDeEsser, RBass, SSL G-Channel,
  API-2500, API-550A, API-550B, API-560, C6, Vitamin, MaxxBass, Q10. Per plug-in: its parameter table and default state
  (`params`), D (defaults) + four musically typical settings X1 … X4 (cap/lab/settings.py, set as the plug-in's own texts and
  read back: every setting differs from D), refcap182's 35 s programme (drums / vocal / bass / guitar / mix) and refcap0124's
  7 s stimulus, one process per setting. Each render is made twice: take A after a reset and 1.5 s of silence, take B after
  9 s (the demo mutes fall 7.5 s elsewhere in their 20 s cycle).
- **The demo mutes**: ≈ 0.95 s of digital silence with ≈ 10 ms fades, every 20 s of processed audio (counted per process).
- **Masks** (cap/lab/mkmask.py): a mute is a run of digital silence (> 20 ms, both channels) in take A that take B plays;
  it is widened over the fades where the takes differ (5 ms windows, |A − B| > 1e-4 of B's level; ≤ 150 ms before, ≤ 4 s
  after for a plug-in whose detector saw the mute) plus 10 ms, and written next to the reference as `<name>.mask.json`
  (spans in reference samples). Outside the masks the two takes are identical for 15 of the 16 plug-ins (−600 dB); API-2500
  differs by −37 … −50 dB over its first seconds (its own slow settling after the 1.5 s lead).
- **Scorer** (scripts/fxparity.py, flref_compare.py): `pair()` reads the sidecar and `compare (…, mask=…)` drops those
  spans from both aligned signals before every metric (ESR, level, spectrum, envelope, worst window). References without a
  sidecar (every older bank) score exactly as before. Check: every D file of the transparent plug-ins scores 10 with the
  masks (they would score ~1 … 3 without).

## 3. Renaissance EQ (REQ 2 / 4 / 6) and Q10 → Facet EQ's Waves design law
Method (eq/): each band alone over a grid of frequencies / gains / Qs, impulse at 10 ms, two takes; a Prony fit of the exact
impulse response gives each section's coefficients (−110 … −176 dB residual), and the analog prototype + digitisation that
reproduce them were found by fitting (complex response, Nelder–Mead).
- **REQ bell**: (s² + s·G/q + 1) / (s² + s/q + 1) — the denominator's Q is the control for boosts and cuts alike, only the
  numerator moves — digitised with a prescribed Nyquist gain (Orfanidis, JAES 1997: DC 1, peak G at f0, Nyquist gain = the
  prototype's, bandwidth w0/q at the level √((1 + G²)/2)): −106 … −111 dB on the probes (−80 … −88 dB at 100 Hz, −50 dB at
  12 kHz with Q 0.7).
- **REQ shelves**: a shared s-term — LS(s) = (s² + b s + A wc²) / (s² + b s + wc²/A), HS = the mirror (A s² + b s + wc²) /
  (s²/A + b s + wc²), A = 10^(g/40) — bilinear, prewarped at wc; Q clamped to 0.707 … 1.414; wc = m·f (low) or f/m (high),
  b = β·wc with ln m a 2-D polynomial of (ln q, g/18) and β = cb(g/18)/q (fitted on 8 gains × 6 Qs: 1.2e-4 relative):
  −91 … −120 dB 50 Hz … 8 kHz (a low shelf above ~12 kHz −46 … −53 dB: its centre passes Nyquist).
- **REQ resonant shelves**: the cookbook shelf, Q clamped to 0.707 … 1.414, bilinear at the band frequency: −100 … −111 dB.
- **REQ cuts** (18 dB/oct): a pole at f × (s² + z f²)/(s² + s f/Q2 + f²) with z = 1 − 1/Q2 (a notch for Q2 > 1, a floor
  below), bilinear at f; ln Q2 a quartic of ln q (q clamped 0.707 … 1.414; 1.7e-5): −95 … −112 dB.
- **Q10**: shelves first order centred on f (bilinear, −110 dB), cuts 12 dB/oct Butterworth (bilinear, −110 dB), bells
  symmetric (cookbook form) with Qc = q · 0.0908542 (√G + 1/√G) (Bell) or q · k(|g|) (PQ-Bell, k measured every 1.5 dB) and a
  bandwidth-prewarped Q: −75 dB at low frequencies, −45 … −53 dB for wide bands above 3 kHz (the exact Q prewarp law not
  found: §7).
- LYNX: Facet EQ [126] = the Waves design law, [127 + b] = band b's design code (k/64: REQ bell / shelves / resonant shelves /
  cuts, Q10 bells / shelves / cuts); hidden, saved while non-zero, set only by the REQ and Q10 translators. The band's
  frequency / gain / Q fields hold the reference's own control values (Q10's Q / 4). [waves-req] / [waves-q10] (were a flat
  Parametric EQ under [waves-eqs], which keeps SSLEQ). Q10 with more than eight bands in keeps its cuts and the largest gains.

## 4. API-550A / 550B / 560, SSL G-Channel, RChannel
- **API**: the 550s' flat response is not flat (a first-order low shelf near 14 / 7 Hz, +0.5 dB at 20 Hz; a cut at Nyquist);
  the 560 is exactly a 65-sample delay (linear-phase oversampling) with every band at 0. Bands are proportional-Q and the
  560's gain law is strongly non-linear (±6 dB → ±2.3 dB peak, ±12 → ±12.3). Every control position was probed (550A 250,
  550B 420, 560 100 renders × 2 takes) and fitted with ONE Facet band in magnitude (rms 0.01 … 0.3 dB); the flat responses in
  magnitude and phase (550A −55 dB, 550B −62 dB weighted, three bands: Facet's frequency floor is 10 Hz). Tables: src/core/WavesApiEq.h (generated by eq/lab/gentab.py). The
  translators pick the fitted band of each control's position (the 550s are stepped; the 560 is interpolated between probed
  gains, its eight largest bands kept). API-550A and 550B share the state name "API-550": told apart by the sub-component
  (A5AS / A5BS). Facet EQ itself is unchanged.
- **SSL G-Channel**: the E channel's RealWorld layout (55 LMF ÷3, 56 HMF ×3 — found with crafted states; 26 EQ → side chain,
  31 analog, 40 input) on Ridge Strip's measured E console through the E-Channel translation. The G's EQ (proportional-Q
  bells overshooting to ±18 dB at ±15, eq/fit_sslg.json) is not the E's: X1 / X2 / X3 score 5.
- **RChannel**: its compressor block mirrors the Renaissance Compressor's list (42 attack, 43 release, 45 threshold, 46
  ratio, 47 gain, 52 ARC), so it plays on Compressor's Renaissance law (round 190mg); its EQ (the Renaissance EQ's four
  bands: band k at 5 k = on, type, gain, Hz, Q; 20 EQ on; 26 routing) runs on a new hidden channel EQ of that law
  (Compressor [48] on, [49] routing, [50 …] four bands as Facet design codes + values, digitised by Facet's Waves design
  law; 0 = every other Compressor): 5.16 (compressor alone) → 5.96. The gate is not carried. Tried and not kept: Ridge
  Strip's own engine with EQ, comp and gate mapped (4.88: its colouration costs the D files 10 → 8); the "RVox" compressor
  type (44) on sub-round dyn's Vox law with its make-up taken back (X3 2 → 1 … 3, mean 5.92: the channel's RVox mode keeps
  some make-up we could not pin down; X3 stays on the Renaissance law at 2).

## 5. Results (bank wv190mm, masked; before = fxr_base = round 190mg's final binary)
| reference → LYNX | files | before → after |
|---|---|---|
| NS1 → Quell (NS1 law at 0 %; refcap182) | 25 | 7.04 → 7.24 (up 3, down 0) |
| REQ 6 → Facet (Waves law) | 25 | 4.92 → 9.76 (up 20, same 5, down 0) |
| REQ 4 → Facet | 25 | 5.16 → 9.92 (up 20, same 5, down 0) |
| REQ 2 → Facet | 25 | 5.64 → 9.80 (up 20, same 5, down 0) |
| Q10 → Facet | 25 | 4.64 → 9.68 (up 20, same 5, down 0) |
| API-550A → Facet (tables) | 25 | n = 0 → 7.92 |
| API-550B → Facet (tables) | 25 | n = 0 → 8.04 |
| API-560 → Facet (tables) | 25 | n = 0 → 8.44 |
| SSL G-Channel → Ridge (E) | 25 | n = 0 → 6.44 |
| RChannel → Compressor (Renaissance law + channel EQ) | 25 | n = 0 → 5.96 |

## 6. Gates
- Gold (lab/gold190mm.sh, base vs this branch): Facet (default, bells / shelves / cuts, steep cuts, a dynamic band, natural
  phase, the PuigTec and Pro-Q 4 translations), Quell (default, classic, matched, zero law), Ridge Strip (default, the
  SSL E-Channel translation) × 44.1 / 48 / 96 kHz × 3 signals — see the HANDOFF for the counts (0 mismatches).
- CPU (lab/cpu190mm.sh): see the HANDOFF.
- Tests: tests/Parity190mmTests.inc (the law hidden / saved only while set / inert alone; the REQ bell's exact gain at f and
  its Nyquist gain, shelves, the Q clamp, the 18 dB/oct cut, Q10's first-order shelf; REQ / Q10 / API / SSL G translations),
  Parity190mgNs1Tests (0 % now the NS1 engine's pair), Parity190jh (Facet's stored count).

## 7. Not done / open
- Q10's bell Q prewarp: the cookbook bandwidth form is close (−75 dB) but wide bands above 3 kHz miss (−45 … −53 dB).
- SSL G's own EQ curves (identified as single biquads with gain-dependent real / complex poles: no Facet shape nulls
  them; a measured-section engine would) and G compressor; RChannel's gate and its RVox compressor mode.
- API-560's linear-phase oversampling (LYNX is minimum phase: X files −32 … −57 dB ESR at spectral distance ≤ 0.09 dB).

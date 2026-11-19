# Round 190kw: static REA of the FabFilter / Valhalla references (0.1.30 parity)

2026-11-17. Owner's permission (this round's brief): static reverse-engineering of third-party plug-in binaries for sound
parity, clean-room only — the laws are understood and re-written in LYNX's own code; no code, tables, presets or content
of the references are copied; nothing is bypassed; no debugger, no patching, static analysis only (Ghidra 12.1.4 headless,
objdump, a small RTTI reader). Evidence markers: [STATIC] read in the decompiled / disassembled binary (addresses are the
image's virtual addresses, image base 0x180000000), [EXEC] measured on renders (refcap030 bank, LYNX's fxrender),
[CODE] LYNX source.

Binaries (Windows x64 VST3, copied read-only to /mnt/e/lynx-work/rea190kw/bin/): FabFilter Pro-C 3 (sha256 03029eff…),
Pro-L 2, Pro-MB, Pro-G, Pro-Q 4, Pro-R 2, Saturn 2, Timeless 3; ValhallaVintageVerb. Lab and raw decompiler output:
/mnt/e/lynx-work/rea190kw/rea/ (out/*.c are working files, not shipped).

Method. The binaries are stripped but keep MSVC RTTI: `rea/rtti.py` walks type descriptors → complete-object locators →
vtables, which names every DSP class (e.g. `CleanStyle`, `Capacitor`, `Detector`, `SmoothCompressor_MainCompressorAlgorithm`).
Constructors are the functions that store a vtable address; per-style constants are stored there as raw doubles. Each
DSP object is a node of a small signal graph; nodes hold "smoothed parameter" objects whose value is set through one helper.

## 1. Pro-C 3 → Crest: the V1 styles (Clean, Classic, Opto) [STATIC] → built (revision 1)

Classes: `ProCCompressor` (vtable 0x1802237f8; constructor 0x1801696d0; per-block process 0x18016a640; parameter update
0x18016b520; graph wiring 0x18016b990), the style objects `CleanStyle` / `ClassicStyle` / `OptoStyle` (base `ProCV1Style`,
constructed together at 0x180169220). Internal style order (the `CompressorStyle` enum): 0 Clean, 1 Classic, 2 Opto,
3 Vocal, 4 Mastering, 5 Bus, 6 Punch, 7 Pumping, 8 … 13 the Pro-C 3 styles (the UI order differs: "Clean, Versatile,
Smooth, Punch, Upward, TTM, Op-El, Vari-Mu, Classic, Opto, Vocal, Mastering, Bus, Pumping", string at 0x18022afa0).

Per sample (0x18016a640, styles 0 … 2):
- level L = 20·log10 |side chain| (vectorised log at 0x18012cf30, floor −180 dB); overshoot d = L − threshold.
- Classic is a feedback design (style slot 5 returns 1 at 0x1800175a0): d ← (min(d, range) − GR) × ratio.
- target = knee(d) (slot 1, 0x18016c870); × (1 − 1/ratio) when ratio < 100 (0x18022cb30); ≤ range (not Classic); ≥ 0.
- the style's follower (slot 6) updates the reduction GR (dB): Clean 0x18016cd90, Classic 0x18016cfd0, Opto 0x18016d150.
- every follower step is c = 1 − exp(−ln 10 / T) (0x180169640; −ln 10 = float at 0x18020f64c), T in samples (attack and
  release seconds × the sample rate read at context+0x90): the time to cover 90 % of a step.

Knob laws (0x18016b520): attack position a (0 … 1) → a³; release position r → rel = tan(1.2 r²) / tan(1.2) (the odd CRT
function at 0x1801e0820 with ×0.38878 = 1 / tan 1.2 at 0x18022c4b8). The display's laws follow: attack 0.005 + 250 a³ ms,
release 10 + 2490 · rel ms (LYNX's translator table matches these exactly). Style constants (doubles written by the
constructor; base class 0x18016c7f0):

| | attack lo / min / span (s) | release base / dip / scale (s) | knee shape P1, P2, offset, scale |
|---|---|---|---|
| Clean | 0.0001 / 0.0002 / 0.2499 | 0.025 / 0.09 / 1.975 | (0.093, 0.287), (0.52, 0.822), −11, 0.83 |
| Classic | 0.003 / 0.003 / 0.997 | 0.04 / 0.13 / 2.46 | (0, 1.492), (0.541, 1.0), −3, 0.9 |
| Opto | 0.001 / 0.004 / 0.249 | 0.005 / 0.02 / 1.195 | (0.022, 0.284), (0.76, 0.943), −13.3, 0.7338 |

attack = lo + span·a³ (slot 2, 0x18016ca20); release = rel·scale + base − 0.9·dip·√((clamp(attack, min, lo + span) − min) /
(lo + span − min)), at least base / 10 (slot 3, 0x18016ca30).

Followers (A, R attack / release in samples, P the sample rate, x the target):
- Clean: slow stage s1 → x with T = R + 0.2 P falling, 100 A + 2 P rising; y = max(s1, x); main stage GR → y with T = R
  falling, else A + e^(max(GR − y, −25)) · (0.05 P − 0.2 A) (a fixed exp table over −25 … 0, `ProCV1Style::ExpFunction`,
  built at 0x18000a180; its functor is exp, 0x18010c350).
- Classic: s1 → x with T = 2 P − 1.8 A falling, 0.43 P + 100 A rising; y = max(s1, x); s2 → y with T = R falling, else
  A + 10 (2 P − A) e^(max(s2 − y, −25)); GR = s1 + s2.
- Opto: rising GR → x with T = A + e^(max(GR − x, −25)) (0.05 P − 0.2 A) and the auxiliary state set to GR; falling: the
  auxiliary state moves by c(R) · v, v = x − aux, compressed to −√(−v) past −1 dB; GR follows it with T = 4 ms.

Knee (slot 1): K = knob width. K = 0 → d; K = 24 → the style's shape S(d); else the quadratic knee q = ((K/2 + d)/K)² · K/2
inside ±K/2 (d outside) blended: K < 24: (S(d/s) · s − q) · s² + q, s = K/24; K ≥ 24: (K/72 − 1/3) · 1.5 · (q − S(d)) + S(d).
S(d) = B((d − offset)/48) · (d − offset) · scale for d > offset (else 0), B a cubic Bézier (0,0) → P1 → P2 → (1,1) solved
for x by bisection (0x18001afc0), straight past its ends (0x18001b320); sampled 8192 points over −20 … 100 dB and
extended linearly past 100 (0x18016cb40, 0x18016c990). LYNX recomputes the shape itself (`V1Knee`).

Not built: auto release for these styles (slot 4 0x18016cae0, a per-style table; every bank capture has it off): with
Auto release on, Clean / Classic / Opto keep LYNX's fitted law. Hold uses LYNX's own reduction hold (the reference's
`HoldProcessor` was not read; Hold is 0 in every capture).

## 2. Pro-C 3 → Crest: the V2 styles (Vocal, Mastering, Bus, Punch, Pumping) [STATIC] → built (revision 1)

These five styles are wired at run time out of FabFilter's generic DSP elements (the same classes exist in Pro-MB, Pro-G,
Pro-L 2, Pro-Q 4, Pro-R 2, Saturn 2: one shared library). `ProCCompressor`'s constructor (0x1801696d0) creates three
`Capacitor` followers (constructor 0x18010e5b0, process 0x18010eba0), one `Detector` gain computer (constructor 0x18010d210,
process 0x18010d7d0), a `DBConverter`, two `Mixer`s (0x18011f950), a `PeakHold`, an `EQFilter`, a `TransferCurveProcessor`
(0x180119440), a `VolumeFilter` and a `FloatParameterSource`; 0x18016b990 wires them per style and 0x18016b520 sets their
times from the knobs.

Elements (LYNX's own code: `v2Step`, `v2Knee`, `SlidingMax` in core/DynamicsSuite.cpp):
- Detector, dB domain: overshoot over the threshold through the quadratic knee of the knob's width, × (1 − 1/ratio) under
  100 : 1, a soft range (its knee is 0 here), ≤ range, ≥ 0; in the linear domain the output is the reduction share
  1 − 10^(−GR/20) (0x18010d7d0's tail). Vocal sets the detector's own mode (flag at +0x288): GR = 20 log10 (1 + |x| ·
  (10^(−threshold/20) − 1)) — no ratio, no knee.
- Capacitor: y += (x − y)(1 − exp(−ln 10 / (T · rate))), T in seconds. Rising: T = attack, blended with 0.1 · T · step by
  (1 − p1), plus p2 · e^(−step) for steps under 10. Falling: T = release (or k / (√y + 1) with auto release, k = 3.6 r² + 0.1),
  blended with −0.1 · T · step by (1 − p5), plus p6 · e^(step) for steps above −10 (exp table over −25 … 0,
  `Capacitor::Style::ExpFunction`, built at 0x1801989e0).
- Mixer mode 2: max of the inputs' magnitudes; mode 0: sum. TransferCurveProcessor mode 1: x^(15 p + 1). DBConverter mode 0:
  20 log10 |x| (floor −180); mode 2: −20 log10 |1 − x|. PeakHold: the maximum over the last 10 ms.

Knob laws: attack a³ · 0.25 s, release 2.5 · tan(1.2 r²)/tan(1.2) s ("relS"). Graphs (D = detector, Cn = followers):

| style | graph | constants |
|---|---|---|
| Vocal | D (linear, Vocal mode) → C0; C1 ← max(D, C0); GR = −20 log10 (1 − C1) | C0 0.25 / 0.5 s; C1 attack 0.4 a³, p5 0.25, release = min(0.5 √a³, 10) + rel + 0.005 + 10 · C0^5.5 |
| Mastering | D (linear) → PeakHold 10 ms → EQFilter (10 Hz) → C0; C1 ← max(D, C0); GR from C1 | C0 attack max(0, 0.025 − 0.1 relS) + 0.25 a³ + 0.025, release 0, p6 1; C1 0.25 a³ + 5e−5 / relS + 0.1 |
| Bus | D (dB) → C0; C1 ← max(D, C0); C2 ← max(D, C1) | C0 0.35 / 0.8 s; C1 release 0.1 relS + 0.015, p5 0; C2 0.25 a³ + 0.0003 / relS + 0.015, p1 0.5, p5 0.75, p6 0.3 |
| Punch | D (dB) → C0 | C0 0.25 a³ + 0.0001 / relS + 0.05, p1 0.9 |
| Pumping | level → C0 → C1 → dB → D → C2 | C0 release 0.5 relS + 0.025; C1 0.25 a³ / 0.5 relS + 0.025; C2 5e−5 / 0.5 relS + 0.005, p5 0.25 |

[EXEC] Two details were settled on renders: Pumping's first followers read the linked level (magnitude), not the signed
sample (the signed sample scored 6.73 → 6.73 on its 26 files, the magnitude 6.73 → 9.31, all up); Mastering's `EQFilter`
(shape enum not resolved) is modelled as a 10 Hz two-pole Butterworth low-pass (the frequency parameter is log2 Hz =
3.3219 → 10 Hz) — its 9 files 8.0 on the styles block.

## 3. What LYNX keeps out of the revision

- Old projects / presets: `crest::revision` is hidden (index = the info's size, no control, no string, saved while non-zero);
  `upgradeLoadedEffectParams` sets 0 for anything saved before. Revision 0 is the base commit bit for bit.
- Live's Compressor / Glue / Drum Buss / Channel EQ maps (AlsDevices*.cpp) and the plug-in substitutes' Crest entries
  (PluginSubstitutes.cpp) keep revision 0: they were measured on the fitted laws. The Pro-C 3 translator sets 1.
- Clean / Classic / Opto with Auto release on keep the fitted law (the reference's per-style auto-release table was not
  rebuilt). Versatile (Allround), Smooth, Upward, TTM (Two-way), Op-El and Vari-Mu are generated graph classes
  (`VersatileCompressor_MainCompressorAlgorithm`, `SmoothCompressor_MainCompressorAlgorithm_FeedbackAlgorithm`, …; their
  constructors are 2.7 … 9.5 KB of wiring): not rebuilt this round; they keep their fitted laws. Versatile is the
  reference's DEFAULT style (tp172b's D setting): rebuilding `VersatileCompressor_MainCompressorAlgorithm` (constructor
  0x180164ae0, 9.5 KB of node wiring; parameter update 0x1801673c0) is the next round's best Crest target.

## 4. Other targets: findings this round did not build

- **Pro-L 2 → Apex, 96 kHz block** [EXEC]: Dense / Loud / Safe report 6 057 samples of latency at 96 kHz (63 ms; 3 115 at
  48 kHz), Clean 1 033. Apex has one constant latency (329 samples at 48 kHz), so the reference's output lags LYNX's by
  5 476 samples at 96 kHz — more than `scripts/flref_compare.py`'s ±4 096-sample alignment search (`find_lag`), which is
  why those files score 3 … 5 at 96 kHz while the same settings at 44.1 / 48 kHz are bit-for-bit or 8+. The sound is not
  the cause; the window is in samples, not in time. [EXEC] The same base binary with the window at ±8 192 samples (85 ms
  at 96 kHz; lab copy of the scorer only) scores the sr96 block 6.21 → 7.54 (10 files up, 0 down). Not changed (a scorer
  change re-scores every bank: owner decision).
- **Pro-Q 4's dynamic auto threshold** [STATIC, read, not built]: `BandCompander` (vtable 0x1802403c0, process
  0x18014e820) with auto threshold on runs an object at +0x3f8 (0x180116530): the band signal's magnitude feeds a follower
  (and a second one while a start-up count runs); the detector then reads the band level RELATIVE to that follower
  (|x| / envelope, or a constant / envelope in its other mode), converted to dB, through the usual quadratic knee and ratio;
  three more followers smooth the reduction (0x3e0 / 0x3e8 / 0x3f0, their times set from the band's frequency and Q in
  0x18014ee20). Facet's fitted AutoLaw stays; the next round can rebuild it from this structure.
- **Pro-MB / Pro-G / Pro-DS / Pro-L 2** share the elements read in §2 (`Detector`, `Capacitor`, `PeakHold`; `Capacitor`
  vtables: Pro-G 0x1801d8690, Pro-R 2 0x1802082e0, Saturn 2 0x18020e628, Pro-Q 4 0x18023d5c8). Prism and Sentry were not
  re-modelled this round.
- **ValhallaVintageVerb** [STATIC + EXEC] (analysis: /mnt/e/lynx-work/rea190kw/rea/vv/; implementation: §9): knob laws
  (parameter table 0x180376f30, conversions 0x1803aba00: skewed controls min + (max − min)·v^(1/k), k = ln 0.5 /
  ln((mid − min)/(max − min)); Decay 0.2 … 70 s, 7 s at mid) and the equal-power Mix already match LYNX. Every mode's loop
  gain is the RT60 law 10^(−3 L / (fs T)) split over its N gain stages (0x1803a9670 and siblings; N = 2 halls, 5 Plate, 6
  most rooms, 16 Palace; per line with RT = 1.2 × Decay in Chamber1979 / Hall1984); the input enters after the gain stage,
  so the first half-lap never sees Decay. Size scales each mode's loop length by its own law (size functions
  0x1803a8590 … 0x18039aaf0). There is no per-mode output gain: the level follows the loop, E(T, s) = E0 / (1 −
  10^(−6 ℓ(s)/T)), and the early part (first 80 ms) does not change with Decay (Epoch's rises ~3 dB). 1970s runs the engine
  at half the host rate (a quarter at ≥ 60 kHz).
- **Timeless 3**: no state translator existed (the substitute ran Fruity Delay 3 at its defaults); see §10.

## 5. Saturn 2 → Ember: the recovered laws [STATIC] (analyst's report; implementation: §7)

Style factory 0x18017cb90 builds the 28 styles in UI order, each from one helper (0x180113ed0) taking a curve type, a power
P, a negative-side scale B, the drive range lo … hi (dB), a reference level and a trim. Curves (scalar reference 0x180113600;
SIMD twins e.g. 0x180141e40), on t ≥ 0: ArcTan (2/π)·atan(πt/2); DivAbs t/(1 + t); Tanh; Sine sin(π/2·min(t, 1)); Poly2
2t − t² (t ≤ 1); Poly3 1.5t − 0.5t³ (t ≤ 1); Rectify (Poly2, full wave); Tube — asymmetric and piecewise (a hard ceiling
above u = 0.32002, a quadratic between −0.08905 and 0.32002 with slope 3.9375 at 0, an exponential-like negative branch,
a floor below −1.75482; C¹ joins). Asymmetry and softness: y = sign(x) · f((β|x|)^P)^(1/P) / β with β = 1 above zero, B
below. The transformer styles and Breakdown add a level-dependent linear knee k = 0.5 − 0.49 (2z − z²), z = min(|v|/4, 1),
v the shaper input band-limited 70 Hz … 5 kHz (0x180113500, 0x180112f10).
Drive and auto gain (0x180114090, 0x1801140d0): pre-gain G = 10^((lo + (hi − lo)·d)/20), d the Drive knob 0 … 1; post-gain
10^(trim/20) · 2C / (f(G·C) − min(f(−G·C), 0)), C = 10^(ref/20): a sine at the style's reference level keeps its
peak-to-peak level ("increases the input gain and corrects the output gain correspondingly", the plug-in's own help).
Per-style pre / post EQ (emphasis / de-emphasis pairs; the amps' 4 … 8 shaping filters), the band tone stack (low shelf
120 Hz, bell 600 Hz, high shelf 2.5 kHz, high shelf 7 kHz; the four amp models 170 / 500 / 1500 / 4500 Hz), High Quality
(Off 1×, Good 8×, Superb 32× for the shaper), FoldBack y = (1 + d²)² / (1 + 99d²) · sin((1 + 99d²) x), BitCrusher (sample
and hold for d²·500 samples). Not recovered: the Dynamics curve's meaning, the exact feedback law, the crossover design,
the Smudge / Breakdown pitch internals.

## 6. Pro-R 2 → LuxeVerb's Space engine: the recovered laws [STATIC + EXEC] (analyst's report; implementation: §8)

True stereo (Channel constructor 0x1801539b0): each channel has a 16-line FDN (Hadamard 16), a 2-line FDN (Hadamard 2), a
16-line early network, early reflections, a Thickner, six tone filters; a CrossInput feeds each channel's input to the
other. Line gain 10^(−3 · len / T) (0x180119f20).
- Stereo Width (0x18017ce10; the state value is the raw knob 0 … 1.2, default 0.7): v = √|W − 1|; input M/S: side gain
  min((2·min(W, 1) − 0.8)² + 0.21, 1); output M/S: W < 0.7: Y = W/0.7, X = 1 + 0.8 (√(2 − Y²) − 1); W ≥ 0.7: X = 1 −
  1.25e, Y = 1 + 0.5e, e = max(W − 1, 0); the two channels' 16-line networks are linked by an orthogonal rotation
  √(1 − v²/2) / ±v/√2 around their Hadamard outputs (0x180154f20). Checked tap by tap on the irL captures: output side /
  mid 0 / .335 / .779 / 1 / 1.466 predicted and measured at W = 0 / .3 / .6 / .9 / 1.2.
- Space: displayed time 0.2 … 10 s; the network runs T = 1.15 × displayed × 4^DecayRate (0x180156940); the early network
  decays at T without the rate; the 2-line network at ≈ (200 + 0.00088 (T − 200))·rate ms; line lengths 5.18 ms ×
  1.12402^(k + n0 + j), k = floor(14 sin(π/2 · S^0.8)), n0 = 9 / 10 / 11 by Distance (0x180156d20); late level
  ((250/T_ms)^0.7 + 1.3) × (1 + 0.5 d) × a Distance / Character term (0x180157290).
- Brightness (0x1801564e0): darkness d = 1 − min(B/0.7, 1); four tone stages at 2^(14.136 − 4d) Hz, one at 2^(13.873 −
  2d) Hz, a 200 Hz stage of −8 (2.5 (B − 0.6))₊² dB (less a width term), in-loop absorption moved down d octaves.

## 7. Saturn 2 → Ember: implementation [EXEC] (branch rea190kw-sat, NOT merged: 33 files down — owner decision)

### Saturn 2 → Ember: the styles' own laws (round 190kw, implementation part)

Branch `rea190kw-sat` (off `origin/parity190js` d9bfc68ce). Work folder `/mnt/e/lynx-work/rea190kw/sat/`.

### What the static analysis gave (sat2/, written as LYNX's own code)

- Each of the 28 styles = a curve type with a power P and a negative-side scale B (styles_table.txt):
  `y = sign(x) · f((β|x|)^P)^(1/P) / β`, β = 1 for x > 0, B for x < 0. Curve types (the factory that builds the
  shapers names them): 0 arctan `(2/π)·atan(πt/2)`, 2 `t/(1+t)`, 4 tanh (with a "LL" variant taking a second input:
  the transformer / Breakdown styles), 6 sine `sin(π/2·min(t,1))`, 7 `2t − t²` (t ≤ 1), 8 `1.5t − 0.5t³` (t ≤ 1),
  11 the asymmetric tube curve (hard ceiling 0.62993, parabola 3.9375u − 6.153u², 12th-power negative knee, floor
  −1.17545), 12 triode (unused), 13 rectifier.
- Drive: the pre-gain is a 256-point table over the normalised drive d of `exp((lo + (hi − lo)·d) · ln10/20)`
  (constants read from the binary: ln10/20 = 0.115129, 1/ln2 for the filter frequencies).
- Level compensation (the post-gain table): `10^(trim/20) · 2C / (F(G·C) − min(F(−G·C), 0))`, C = 10^(ref/20).
- Per-style EQ before / after the shaper (style_setup.txt: `(bank, log2 Hz, dB, Q, type, slope index)` calls), the
  default band tone stack (low shelf 120 Hz Q 0.4159, bell 600 Hz Q 0.4714, high shelves 2.5 kHz Q 0.6034 and 7 kHz
  Q 1.0; the amps: 170 / 500 / 1500 / 4500 Hz) — Qs read from the binary's constants.
- High Quality: state index 698 (0 off, 1 good, 2 superb), read back from the refcap030 grid states.

### What LYNX runs (revision 1, hidden `ember::revision` = index 163; `lawRate` = 164)

- `core/EmberSaturnStyles.inc` (generated by `py/geninc.py` from the two analysis tables), `saturnCurve`,
  `saturnPreDb`, `saturnPostGain`, `lawCharacter` (MixSuite.h / .cpp).
- A band whose character is a law style: pre-EQ (base rate) → × G → the curve (oversampled by Ember's Oversampling, or
  at the base rate with `lawRate` = the reference's HQ off, delayed by the oversampler's latency so the PDC does not
  move) → Ember's DC blocker → × post-gain → post-EQ → the reference's tone stack (bass / mid / treble / presence) →
  Ember's band mix / level / pan, global tone, cuts, mix, output (unchanged).
- Filters: RBJ sections at the reference's Q (shelves with the Q form); cuts of slope index n = 6, 12, 18, 24, 30, 36,
  48 dB/oct as Butterworth sections with the most resonant pair's Q × q/√½ [model: the analysis gives the index, not the
  section layout].
- `fromSaturn2` sets the revision and `lawRate` (HQ off); Drive keeps its mapping (Ember's parameter = 30/36 × the
  reference's normalised drive; `referenceDrive (driveDb (v))` gives it back exactly).
- A new Ember gets revision 1; `upgradeLoadedEffectParams` sets 0 for anything saved before (savedCount ≤ 163).

### Lab (refcap030 grid subset: 28 styles × HQ off / superb at drive 25 %, 28 styles at 75 % / 96 kHz; 168 files)

| variant | mean | up / down vs base |
|---|---|---|
| base (round 150 characters) | 3.375 | — |
| law, all styles | 3.643 | 56 / 27 |
| law without the style EQ | 3.190 | 18 / 44 |
| law with B as 1/B | 3.625 | 56 / 26 |

The style EQ is clearly part of the law (without it 18 up / 44 down); B as given beats its inverse. Per style the law
scored up with no file down on the lab for styles 0 – 4, 6 – 9, 11, 14 – 17 and 24; on the whole bank Power Amp (16:
3 up / 2 down) and Breakdown (24: 3 / 6) were net level / down and went back to round 150's. The law scored
down for 5 / 18 / 19 (a steady −0.7 … −1.0 dB level offset — the "base" style set-up function may add a gain the
analysis did not show), 10 / 12 / 13 / 26 (Brit Rock, Smooth / Crunchy Amp: a 5.6 kHz segment +16 dB too loud — their
low-pass Q of 3.1 / the high-shelf Q convention; the rectifier saturates in the reference: it is not a plain |x|), 20 –
22 (the transformers: ESR −11 → −15 dB but the IMD spectrum 4 … 8 dB off: the dynamic "LL" knee is missing). Those
keep round 150 / 152's measured characters (`lawCharacter`). Smudge, Foldback and Destroy were never switched (their
extra stages were not recovered).

### Unrecovered / open

- The "LL" tanh's second input (the transformers' dynamic knee); the rectifier's curve; the base-style level offset;
  the filter Q convention of the amps' resonant low-passes; Dynamics, feedback, crossover design, Smudger / Breakdown
  pitch internals (LYNX's behaviour kept, as briefed); Superb's 32× (Ember stays at its 4×: 8× scored no better).

### refcap030 bank (819 files; base = parity190js fxr4; the law = 13 styles)

| block | files | mean before → after | up / same / down |
|---|---|---|---|
| grid | 588 | 3.55 → 3.93 | 158 / 428 / 2 |
| mode | 33 | 4.67 → 4.88 | 18 / 4 / 11 |
| pairs | 54 | 3.78 → 4.41 | 28 / 23 / 3 |
| sr44 | 20 | 5.55 → 6.10 | 9 / 8 / 3 |
| sr48 | 20 | 5.60 → 6.10 | 8 / 8 / 4 |
| sr96 | 20 | 5.60 → 6.10 | 8 / 10 / 2 |
| prog | 84 | 4.31 → 4.38 | 13 / 63 / 8 |
| **all** | 819 | 3.84 → **4.20** | 242 / 544 / 33 |

The 33 files down are 6 → 5 steps on broadband signals (25 × `mix`) of the tube / tape styles: ESR −3 … −11 → −17 …
−23 dB but the third-octave balance 0.4 → 0.8 dB (limit 0.6) — a steady −0.5 … −0.9 dB level offset (the reference's
small-signal gain is ~1 dB above the recovered compensation; Clean Tape at 25 %: ref +1.1 dB, LYNX +0.1 dB at −39 dBFS)
and +1.4 / +3.5 dB at 12.7 / 16 kHz. Finding where that ~1 dB comes from (a gain in the style base set-up or the output
stage not in the decompiles) would turn these into ups: worth a targeted REA look.
(Grid column "after": the bank binary with a series pow, identical to the shipped table curve within −100 dB; the
other blocks: the shipped binary; styles 16 / 24 now render byte-identical to the base, their base scores used.)

## 8. Pro-R 2 → LuxeVerb Space: implementation [EXEC] (branch rea190kw-r2, NOT merged: 43 files down — owner decision)

### Pro-R 2 → LuxeVerb Space (round 190kw, part r2)

Branch `rea190kw-r2` (off `parity190js` d9bfc68ce). Evidence: `/mnt/e/lynx-work/rea190kw/rea/pror2/` (d1.c, d2.c, dis.txt,
the constructors' listings, wav/ with the analyst's grid renders); lab: `/mnt/e/lynx-work/rea190kw/r2/` (lab/, gold/, mini/,
runs/, runs2/). LYNX's code was written from the laws below, not from the decompiled text.

### Laws recovered (static analysis) and how LYNX uses them

| Law (reference) | LYNX revision 1 (`core/SpaceVerb.h`, `Engine::set1 / process1`) |
| --- | --- |
| True stereo: each channel its own 16-line FDN (Hadamard 16) + 2-line FDN (Hadamard 2) + 16-line early network without a feedback matrix + early reflections + 6 tone filters; a CrossInput feeds each input into the other | Per channel: an 8-line FDN (Hadamard 8 — the law's 16 lengths split even / odd between the channels: 2 × 16 lines measured 2× the CPU), a 2-line FDN, an 8-line early network (each line feeds itself back), its own half of the early-reflection taps, its own diffusers and pre-delay; CrossInput gain 0.5477 · x · min (v / 0.5477, 1)² |
| Stereo Width W (raw 0 … 1.2): v = √\|W − 1\|; s_in = min ((2 min (W, 1) − 0.8)² + 0.21, 1); l_in = M + s_in S, r_in = M − s_in S; out X / Y (W < 0.7: u = W / 0.7, Y = u, X = 1 + 0.8 (√(2 − u²) − 1); else e = max (W − 1, 0), X = 1 − 1.25 e, Y = 1 + 0.5 e); feedback rotation √(1 − v²/2) / ± v / √2 (16-line) and √(1 − v/2) / ± √v / √2 (2-line) | As stated. The translator now passes W raw: `width` = min (W, 1), hidden `luxeVerbSpaceWidthExt` = (W − 1) / 0.2 |
| Space: displayed 0.2 … 10 s; network T = 1.15 × displayed × 4^DecayRate; early network at T without the rate; 2-line network (200 + 0.00088 (T − 200)) × rate ms | Space 0 knot 0.2 s (other knots unchanged); main network keeps tp172b's per-style RT scale (1.07 / 0.95 / 1.12 ≈ the measured RT); early network decays at 1.15 × displayed (no rate); 2-line network as stated |
| Line lengths L_j = 5.18 ms × 1.12402^(k + n0 + j), k = ⌊14 sin (π/2 · S^0.8)⌋, n0 = 9 / 10 / 11 by Distance, channel 1 + 0.5 | Left lines j = 0, 2, … 14, right lines 1, 3, … 15 (the same 16 lengths); early network 5.18 ms × 1.12402^(k + 2j + c) |
| Late level G = ((250 / T_ms)^0.7 + 1.3) (1 + 0.5 d) f10 (f4 / f10 from Character and Distance) | As stated, times a fitted factor 0.0761 · T^−0.944 (the reference's energy grows slower with T than an FDN's) and per-style trims |
| Brightness: d = 1 − min (B / 0.7, 1), e = max (2.5 (B − 0.6), 0); filters at 2^(14.136 − 4d) Hz, 2^(13.873 − 2d) Hz, 200 Hz shelf −8e² − 6 · (width term) dB; in-loop cuts moved down d octaves | Low shelf 200 Hz −8e² − 6 · 0.5 min (1.25x, 1.5) v dB; one-pole low pass at 2^(14.136 − 4d − 0.75); high shelf at 2^(13.873 − 2d) (Q 0.5) −18 d dB; a fixed 2.5 kHz shelf −3 dB. The unresolved shape enum was fitted: a one-pole low pass (the measured B = 0 curve flattens at −27 … −31 dB above 8 kHz; a biquad's keeps falling) |

Fitted where the law left a constant open (lab: the bank's component spectra, `lab/comp.py` + `lab/fit.py`): the late /
early / 2-line / early-reflection levels, Thickness on the whole body (−15.6 / +6.75 dB per unit), CrossInput x = 1
(left-only impulses across Stereo Width: x = 0 / 0.5 / 1 / 1.5 → 2.92 / 3.32 / 3.40 / 3.20), per-style colour (Vintage
low shelf −2 dB at 500 Hz, Plate −1 dB / −3 dB at 2.5 kHz / one pole at 9 kHz). The early reflections' extra level at
small Spaces fitted to zero (the early network carries it). Not used: the in-loop filters on the even lines (their
per-pass cuts would shorten the top far more than measured if read as per-pass gains; folded into the output shelf),
the early network's own tone filters.

### Hidden revision

`luxeVerbSpaceRevision` (= ducking + 1) and `luxeVerbSpaceWidthExt` (+ 2), saved while non-zero, no control. New
LuxeVerb instances (makeEffectSlot, FL state import) and translated Pro-R 2 states get revision 1; a project saved before
(`upgradeLoadedEffectParams`, saved count ≤ the revision's index) gets 0 and renders bit for bit as d9bfc68ce: 15 gold
renders (5 settings × 44.1 / 48 / 96 kHz) identical from the base objects and from the round's objects with revision 0
(`gold/gold_base.txt` = `gold/gold_new.txt`), now in `tests/Parity190kwR2Tests.inc`.

### Result (refcap030 bank, fxparity; base = d9bfc68ce)

All blocks 323 files 3.30 → 3.51 (89 up / 191 same / 43 down): grid 3.43 → 3.50, grid2 2.81 → 3.30 (Space × Width
2.16 → 3.32, Brightness 2.72 → 3.20, Space × Rate 3.12 → 3.48), mode 3.72 → 3.78, pairs 3.38 → 3.43, prog = 3.50,
sr44 4.44 → 4.12, sr48 4.12 → 3.94, sr96 4.06 → 4.19, styles = 4.00; refcap182 programme = 3.95; tp172b 3.83 → 3.75.
The downs (default D state 6 → 4, Ducking at long rates, Distance ≥ 0.75) are an owner decision (HANDOFF.md).

## 9. VintageVerb → Epoch: implementation [EXEC] (merged)

### 11. VintageVerb → Epoch Verb: the loop law built (implementation round, branch `rea190kw-vv`)

Built from §4's ValhallaVintageVerb laws, clean-room (LYNX's own code and tables; no reference code or data copied).

**What is built.** A hidden step past round 190it's revision: `verbs::epoch::loopLaw` (index 19 = paramCount + 1; no
control, no string, saved while non-zero). It is read only with the Measured law and revision 1. A new instance and the
VintageVerb translator (through `makeEffectSlot`) get 1; a project or preset saved before loads 0 (`upgradeLoadedEffectParams`:
saved count ≤ 19), so revision 0 and revision 1 render bit for bit as the base commit did.

With it on:
1. **The early part holds over Decay**: the early taps run at their long-decay level (×1.6) at every Decay. Before, they
   rose 3 dB from 2 s to 4 s (round 183b's `min(1.6, 1.13·√(decay/2))`). The reference's first 80 ms read −10.65 dB at
   0.51, 7 and 33 s.
2. **The level follows the reference's loop**: E(T, s) = E0 / (1 − 10^(−6 ℓ_k(s)/T)), with ℓ_k = c·L_k(1)/N_k per mode
   (§4's table; Sanctuary, Dirty / Chaotic Hall as halls, N = 2; Smooth Plate N = 5; Nonlin has no loop law, its gate sets
   the level) and the size law L_k(s)/L_k(1) = a + b·s^e. The trim is `levelDb_k(0.513 s) + [reference growth from
   (0.513 s, 100 %) to (T, s)] − [Epoch's own growth]`. Epoch's own growth over Decay was measured once per mode (impulse,
   60 s renders, knots 0.513 / 1 / 2 / 4 / 7 / 15 / 33 / 70 s, Size 100 %, the law's level off). Its own growth over Size
   was measured too: +1.2 / +2.0 dB at Size 50 / 25 % at any Decay, so (0.3 + 0.7 s)^−0.64. The base level per mode is
   the reference's impulse energy at 0.513 s (refcap030's mode sweep). The tone trims of revision 1 still fade out by
   1.41 s.
3. **Score-fitted offsets on top** (the stand-ins' spectra differ, so the best-scoring level is not always the energy
   match): Concert Hall +0.3 dB. Its own-growth knots are 0.1 / 0.6 / 0.2 / 0.9 dB lower at 2 / 4 / 7 / 15 s and 0.3 / 0.3 dB
   higher at 33 / 70 s, from the pairs / sr / programme / tp172d captures at 0.51 … 33 s. Room is −0.4 dB (its 1.41 s
   captures) and
   Bright Hall +1.25 dB. Grit Plate is −3.25 dB and Gated −2 dB. Drift Chamber keeps revision 1: under the law its test
   signal lost a step at every level.

**Scores** (refcap030, 154 files): mode 2.16 → 2.74 (56 up / 0 down), pairs 3.44 → 3.89 (8 / 0), sr48 3.50 → 3.70 (4 / 0),
sr96 3.60 → 3.70 (3 / 1: S1 impulse at 96 kHz 4 → 3). All: 2.67 → **3.12** (71 up / 82 same / 1 down). Other banks:
tp172d 3.73 = (0 / 0), verb183b captures 3.56 → 3.89 (6 / 0), refcap182 programme 3.48 → 3.64 (4 / 0). Gold 72 / 72
identical (revisions 0 and 1 at 44.1 / 48 / 96 kHz). CPU unchanged within noise. Mini runner (round 190it's test set +
Parity190kwVvTests): 0 failures.

**Not built (skipped):** the per-mode Size law in the DSP itself. Only the level follows it; Epoch's lines still scale by
0.3 + 0.7·Size. There is no capture to check the other modes at Size ≠ 100 %, and Palace's law shortens its loop 32×
at Size 0. RT = Decay with the first lap free of Decay, as a DSP change (Epoch's RT tables of round 183b / 190bz stay).
1970s at half the host rate and the mod-depth ×0.64. The early level at long decays: ours is 1.5 … 2 dB under the
reference's −10.65 dB at 7 … 33 s, because the law scales the whole wet path.

Evidence: `/mnt/e/lynx-work/rea190kw/vvimpl/` (lab/eg.py the growth measurement, lab/fit.py the table fit, lab/opt.py the
score-vs-offset scan, gold/, cpu/, runs/, runs2/, logs/).

## 10. Timeless 3 → Echoform: the state translator [EXEC + STATIC names] (merged)


Lab: /mnt/e/lynx-work/rea190kw/tl3 (py/ analysis scripts, rea/ static notes, base/ r*/ bank runs, cpu/ bench, mini/ tests).
Branch rea190kw-tl3 (off origin/parity190js d9bfc68ce).

### 1. State layout (FFBS v1, 1007 float values)

[EXEC] = read off refcap030 / refcap182 states whose settings were set through the trial's own parameters (capture logs
`get <Name> = <norm> (<text>)`), correlated value by value (py/st.py):

| index | parameter | stored as | evidence |
|---|---|---|---|
| 0 | Delay Time | the normalised knob (0 … 1 = 5 ms … 5 s) | [EXEC] equal to the `get` value in every setting |
| 4 | Delay Read Mode | 0 Tape / 1 Stretch | [EXEC] mode block |
| 6 | Ping Pong | 0 off / 1 L-R / 2 R-L | [EXEC] S2, G1 |
| 85 | Feedback | fraction 0 … 2 (= 0 … 200 %; norm × 2) | [EXEC] grid2 / pairs / sr |
| 90 | Filter 1 Freq | log2 Hz (norm 0 / 1 = 4.812 / 75616 Hz) | [EXEC] grid / pairs |
| 94 | Filter 1 Style | index 0 … 10 (Classic … Clean) | [EXEC] mode / grid |
| 157 | Stereo Width | 0 … 1 | [EXEC] S3 |
| 160 | Mix | −1 … 1 (= 0 … 100 %) | [EXEC] S2 (30 % → −0.4), S3 (40 % → −0.2) |
| 752 | Channel Mode | 0 L/R / 1 M/S | [EXEC] mode |

The rest by ORDER: the plug-in's parameter-set constructor (Ghidra, project `tl3kw`, function at 0x1801a8160, reached
from the "Delay Time" string; `rea/xref1.c`, `rea/filt.c`, `rea/filt2.c`) declares the parameters in this order, and the
[EXEC] indices land exactly on it (Delay Read Mode 4, Ping Pong 6, Feedback 85, Filter 1 at 90, Stereo Width 157, Mix 160):
0 Delay Time, 1 Delay Time Pan (range −2 … 2), 2 Delay Offset, 3 Delay Sync (Free, 1/2, 1/4, 1/8, 1/16 Note), 4 Read
Mode, 5 Freeze, 6 Ping Pong, 7 … 81 fifteen taps × 5, 82 / 83 / 84 main tap enabled / level / pan, 85 Feedback, 86 Feedback
Pan, 87 Feedback Cross Mix, 88 / 89 Feedback Invert L / R, 90 + 9 k filter k = 0 … 5 (Freq, Gain, Pan, Peak, Style,
Shape, Slope, Enabled, Used — the last two told apart by the captures: every capture's Filter 1 has Used = 0 and is not
heard), 144 Filter Routing, 145 / 146 Drive / on, 147 / 148 Lo-Fi / on, 149 / 150 Diffuse / on, 151 / 152 Dynamics / on,
153 … 156 Pitch Shift / on / mirroring / routing, 157 Stereo Width, 158 Wet Level, 159 Wet Pan, 160 Mix. The enumerations'
labels are the plug-in's own strings (Shape: Low Pass, High Pass, Band Pass, Bell, Low Shelf, High Shelf, Notch; Slope:
6 / 12 / 24 / 48 dB/oct; Style: Classic, Gentle, Raw, Tube, Metal, Easy Going, Smooth, Hard, Hollow, Extreme, Clean).
Only names, order and ranges were read; no code, table or coefficient was taken over.

### 2. What the reference does (black-box, refcap030 / refcap182 renders)

- **Filter 1 in the captures is inert**: grid (11 styles × 5 frequencies, ir / irlv) — all 55 responses identical to
  ±0.05 dB (the filter is not "used"). The grid block is 125 renders of S1.
- **Delay time law**: echo sample positions (48 / 96 kHz) and display texts: 0 → 5.000, 0.2 → 174.81, 0.247028 → 249.979,
  0.25 → 254.75, 0.29334 (default) → 350.0, 0.304714 → 375.0, 0.361589 → 500.0, 0.5 → 804.19, 0.75 → 2490, 0.8 → 3176.81,
  1 → 5000 ms. No simple closed form fits (power, exp + offset, log-polynomials ≥ 1 % off); LYNX uses a monotone cubic in
  log ms through these points. The echoes land on whole samples (804.19 ms = 38601 samples at 48 kHz, 77203 at 96 kHz).
- **Mix law**: dry at unity up to 50 %, wet = 2 × mix (S1 50 % → echo/impulse 0.914; S2 30 % → 0.548; S3 40 % → 0.731).
- **The echo**: flat to 15 kHz, a linear-phase low-pass on the read (−0.73 / −2.29 / −4.65 / −6.0 dB at 18 / 20 / 22 /
  24 kHz at 48 kHz; symmetric ringing) once per pass, and a one-pole low cut at 15.5 Hz in the loop (the echo's DC tail
  decays 0.99794 per sample at 48 kHz). The read filter fitted as a Kaiser-windowed sinc per rate (py/fitk2.py, the low
  cut in the model): 44.1 kHz half 12, cutoff 0.94 Nyquist, β 11; 48 kHz 8 / 0.9175 / 9.5; 96 kHz 4 / 0.8625 / 6.5;
  residual −44 … −49 dB under the echo.
- **Feedback**: per pass = the stored fraction (50 % → −6.04 dB at 1 kHz). Level-dependent only slightly (a 0 dBFS
  impulse's echo 0.08 dB lower than a −20 dBFS one); above 100 % the loop grows and settles at about ±6 (+15.6 dBFS):
  modelled as 5.5 tanh (x / 5.5).
- **Ping pong**: only the start side's own input enters (programme regression, S2 mix: left echo = 0.60 × left input,
  −0.03 × right), then each hop × feedback (S2: 0.274 / 0.103 / 0.040 for the L / R / L echoes at 40 %).

Confirmation without static help: refcap182/par/FabFilter_Timeless_3_*.tsv (the plug-in's own exposed VST3 parameter
list, 1144 entries) names every state index 0 … 1006 in the same order (e.g. 90 Filter 1 Freq, 95 Shape "Low Pass",
96 Slope "12 dB/oct", 98 Used "Unused", 158 Wet Level "0.00 dB", 160 Mix, 752 Channel Mode).

### 3. Built (branch rea190kw-tl3)

- Echoform hidden `lineLaw` (index 109): dry / wet law, per-rate read filter, whole-sample delays, 5.5 tanh limit, ping
  pong start side's own input. Off = bit-identical (6 Echoform settings + Fruity Delay 3 byte-compared, fxr4 vs fxr1).
- `fabfilter::fromTimeless3` + `timeless3DelayMs`, `translatorNamed ("timeless3")`; `[fabfilter-timeless]` → Echoform,
  `translate = timeless3`, preset = Timeless 3's default with `#109=1`.
- tests/Parity190kwTl3Tests.inc (5 tests); mini runner (64 test files, FxAudit173d left out for time): 545342 checks, 0 failures.

### 4. Gate

refcap030 bank (cmp_r2.txt): 2.37 → 6.93, up 274 / same 5 / down 0 (grid 1.94 → 7.68, grid2 2.16 → 5.88, mode 3.00 → 6.00,
pairs 1.75 → 5.03, sr44 3.38 → 7.94, sr48 3.44 → 7.94, sr96 3.56 → 7.56). refcap182 programme (prog_base.log /
prog_r2.log): 3.45 → 7.25, 20 / 0 / 0. CPU (cpu/bench.log): FD3 default 0.68 … 0.84 %, Echoform D / S1 / S2 1.50 … 2.12 %
(without the law 1.32 … 1.68 %).

Old projects: a .lynx saved with the old substitute holds kind = Fruity Delay 3 + its params + substituteFor; only slots
of kind `plugin` are substituted on load (PluginHost.cpp, the loop over `fx.kind != EffectKind::plugin`), so they keep
Fruity Delay 3 as saved. A .flp (or a .lynx saved with the real plug-in) opened without Timeless gets the new Echoform.

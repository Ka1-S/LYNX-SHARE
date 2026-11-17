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
  constructors are 2.7 … 7.4 KB of wiring): not rebuilt this round; they keep their fitted laws.

## 4. Other targets: findings this round did not build

- **Pro-L 2 → Apex, 96 kHz block** [EXEC]: Dense / Loud / Safe report 6 057 samples of latency at 96 kHz (63 ms; 3 115 at
  48 kHz), Clean 1 033. Apex has one constant latency (329 samples at 48 kHz), so the reference's output lags LYNX's by
  5 476 samples at 96 kHz — more than `scripts/flref_compare.py`'s ±4 096-sample alignment search (`find_lag`), which is
  why those files score 3 … 5 at 96 kHz while the same settings at 44.1 / 48 kHz are bit-for-bit or 8+. The sound is not
  the cause; the window is in samples, not in time. Not changed (a scorer change re-scores every bank: owner decision).
- **Pro-Q 4's dynamic auto threshold** [STATIC, read, not built]: `BandCompander` (vtable 0x1802403c0, process
  0x18014e820) with auto threshold on runs an object at +0x3f8 (0x180116530): the band signal's magnitude feeds a follower
  (and a second one while a start-up count runs); the detector then reads the band level RELATIVE to that follower
  (|x| / envelope, or a constant / envelope in its other mode), converted to dB, through the usual quadratic knee and ratio;
  three more followers smooth the reduction (0x3e0 / 0x3e8 / 0x3f0, their times set from the band's frequency and Q in
  0x18014ee20). Facet's fitted AutoLaw stays; the next round can rebuild it from this structure.
- **Pro-MB / Pro-G / Pro-DS / Pro-L 2** share the elements read in §2 (`Detector`, `Capacitor`, `PeakHold`; `Capacitor`
  vtables: Pro-G 0x1801d8690, Pro-R 2 0x1802082e0, Saturn 2 0x18020e628, Pro-Q 4 0x18023d5c8). Prism and Sentry were not
  re-modelled this round.
- **ValhallaVintageVerb → Epoch** and **Timeless 3**: not analysed this round (time went to Pro-C 3, Saturn 2 and Pro-R 2).

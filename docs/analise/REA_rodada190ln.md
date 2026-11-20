# REA round 190ln (0.1.30): third-party plug-in binaries for parity — Valhalla, Slate Digital, Eiosis, Kilohearts

Owner's permission of 2026-11-17: REA on third-party plug-in binaries for parity, clean-room. Static reads only (no
decryption, no dumps, no hooks, nothing near licence protection); nothing of the references is copied — no code, no
tables (the reference's own taper tables were seen and NOT read: the laws below run through the plug-ins' public value
texts), no presets, no content. LYNX's code is its own; every number in it is LYNX's fit on the references' renders.
Lab: /mnt/e/lynx-work/rea3_190ln (lab/, ev/, rea/, runs/; ev/*.c and rea/ are working files, not shipped); sub-rounds: vr/ (Valhalla reverbs, §7), khs/ (Kilohearts, §8).
Score: scripts/fxparity.py with round 190lg's time-based alignment window (±85 ms; /mnt/e/lynx-work/rea3_190ln/scorer =
origin/rea2_190lg's flref_compare / fxparity), the same copy for every before and after.

## 1. Readability survey [STATIC]

`.text` entropy and markers of each target (ev/: survey printed in the round log). Every target is readable code
(entropy 5.8 … 6.8; PACE-wrapped binaries sit at 8.000):

| target | .text | markers | note |
|---|---|---|---|
| Valhalla Delay, FreqEcho, FutureVerb, Plate, Room, Shimmer, SpaceModulator, Supermassive (UberMod, VintageVerb) | 2.6 … 4.0 MB, 6.41 … 6.47 | JUCE | readable; run unlicensed / demo |
| Slate FG-X 2, VBC FG-Grey | 44.1 / 27.3 MB, 5.81 / 6.53 | PACE, iLok, Fusion strings | readable code; the strings are the iLok licence client only |
| Eiosis AirEQ, E2Deesser | 41 / 32.6 MB, 6.6 / 6.8 | same | readable; RTTI kept (class names), no symbols |
| Slate Fresh Air, Kilohearts Disperser (and every kHs plug-in) | 0.2 MB, 6.49 | — | thin wrappers: the engine is a shared module (Kilohearts: HeartCore) |

## 2. Where the user value was (choice of targets)

The Slate / Eiosis EQ modules had **no translator**: a project that used Eiosis AirEQ, Slate's VM AirEQ Air / Earth or
Infinity EQ opened Facet EQ flat (round 190ar: "their fitted per-setting maps are not smooth enough to interpolate — a
translation in physical units is needed"). EQs are on most tracks of a real mix and their states are plain XML, so a
physical-unit translator is the biggest gain per hour here. FG-X 2 (7.6), Fresh Air (9), the VBC / FG compressors
(fitted maps, rounds 190ar / 190fx) already had translators. The Slate / Eiosis licences ended on 11-13 (the plug-ins load
but output digital silence), so no new probe renders: the banks are refcap0124 (one-control sweeps of a test stimulus),
refcap182 (programme renders and the settings' component states) and trial190k (lin / stereo probes of those settings).

## 3. Eiosis AirEQ (and VM AirEQ Air / Earth) -> Facet EQ [STATIC + EXEC fit]

Structure [STATIC] (rea/rtti.py on the binary; ev/aireq_rtti.txt, ev/aireq_d1.c, ev/aireq_dsp.c):
- Parameters are built in one function (0x18072a3b0) from range objects; continuous controls use
  `TableParameterTaper<gTableAirEq{BandFreq, BandQ, HPFreq, HPQ, LPFreq, LPQ, Strength}, 11>` (vtables 0x182152f98 …
  0x1821537a0): the conversion (e.g. 0x180745eb0, 0x180746150) maps v to v·10, takes the integer part as a knot index and
  interpolates **linearly in the unit** between that knot and the next (11 knots at v = k/10; clamped at both ends). The
  knot values were not read (that would be copying a table): LYNX's laws run through the plug-in's own value texts at
  k/8 (refcap0124/par, its public parameter API), linearly in the unit as the reference does.
- Gains, trims: linear ±18 dB (texts). Filters: classes `DfFilter<Bell>`, `DfFilter<TapedHS2Q>`, `DfFilter<TapedLS2Q>`,
  `AirEqHighPass`, `AirEqLowPass` (the display's), and the audio path `AmltSkeletonStereo<61, ProcessStateCache>` with
  nine sections: low cut (4 cascaded sub-filters), five bands (3 each), high cut (4), Earth / Air, each skipped when off
  (0x180751900 / 0x180750760). The coefficient design itself is in code reached only through the engine's parameter
  messages and was not located without a full auto-analysis of the 41 MB binary (not run: one Ghidra session machine-wide).
- State: XML archive (`AEqP-state` / VM `AEqA-state`) of normalised values; ids differ from the parameter names
  (LowCut* = HighPass*, HighCut* = LowPass*, BandNSteepOn = BandNNotchOn, ExcludeBandNStrength = BandNExcludeFromStrength,
  Character = GlobalCharacter; VM: LoCut* / HiCut* with Freq / Slope, StandaloneInput / OutputGain ±24 dB).

Shapes [EXEC fit] (lab/fr.py: sweep deconvolution of refcap0124 renders; lab/frprog.py: Welch ratio of refcap182
programme renders; lab/facetmodel.py: Facet's own analog prototypes, MEASURE_fabfilter §1.2; Nelder-Mead):
- **Bells** are second-order bells (each render within 0.03 … 0.07 dB rms of one Facet bell) whose peak and width follow
  the dial: peak = P(|g|)·q^0.35, Facet Q = C(|g|)·q^0.876 — at q = 1 the peak is 5.6 / 9.5 / 11.6 / 18.1 dB for a dial of
  3 / 6 / 9 / 18 dB (larger than the dial at small gains) and the band widens with gain (Facet Q 1.0 → 0.50). Fitted
  points: (6 dB, Q 1) 9.50 / 0.802; (±9, 1) 11.8 / 0.709, −11.1 / 0.731; (±18, 1) 18.0 / 0.504, −18.2 / 0.504; (−4, 2)
  −8.69 / 1.707; (3, 0.5) 4.40 / 0.548 (the law reproduces each within 0.3 dB / 0.02).
- **Band 1 / 5 as a shelf**: the cookbook shelf of slope parameter q (Facet low / high shelf at Q (q/0.7071)^(1/0.4697)),
  gain as dialled (8 kHz +4: 0.011 dB rms).
- **Earth / Air**: shelves whose shape moves with the gain (boost: the corner rises; the dial is amplified at small gains,
  +2.5 → +4 dB at 10 kHz): Facet shelves fitted per dial value (−18 … +18 from the sweeps, ±4.5 / +2.5 from the VM
  settings; within 0.01 … 0.6 dB rms) and interpolated between them (Hz in log, gain linear, Q in log). VM AirEQ Air's
  sweep renders equal Eiosis AirEQ's Air sweep exactly: one filter.
- **Cuts**: steep and shallow 12 … 36 dB/oct are Butterworth at Q 0.71 (Facet Q = q·√2); 6 dB/oct is first-order at Q 0.71;
  the 6 dB/oct high cut also takes the Q (a resonant section with a 6 dB/oct skirt): away from 0.7 Facet's 12 dB/oct cut
  at that Q scores best (Q sweep 1 … 2 → 5); the low cut did not gain from that and stays first-order. Slopes 6 / 12 / 18 /
  24 / 36 at the five steps.
- Strength scales every band's dial gain unless the band is excluded (its texts 0 … 150 %); global Character and
  Strength leave a flat curve flat (sweep renders = default).
- Not modelled: the bells' "steep" switch (a 0 dB band stays flat on the renders; no setting with a gain was captured),
  per-band Character (no capture with it on), the resonance of the shallow 6 dB low cut at Q ≠ 0.7.

## 4. Slate Infinity EQ -> Facet EQ [EXEC fit]

State `InfE-state`, 24 bands × (State disabled / enabled / bypass, Type, Frequency, Gain, Q1, Q2, Width, Slope, channel
mode + panning, dynamics), Gain (output), PhaseReverse, Bypass. Laws from its value texts: frequency = 10 Hz × 3000^v
(exact through all nine texts: log 10 Hz … 30 kHz, Facet's own law), gains ±36 dB linear, Q1/Q2 0.35·10^(v^1.13), width
0.15 … 108 semitones and slope 12 … 120 dB/oct through their texts; type index ⌊12 v⌋ (0 high pass, 1 low shelf, 2 bell,
3 high shelf, 4 low pass, 6 notch, 7 tilt). The refcap0124 sweeps are all flat (every band "Disabled" by default), so the
shapes come from refcap182's five settings (programme renders, Welch ratio): gains exactly as dialled; a bell's Width is
the cookbook bandwidth (26 st → Facet Q 0.859; fitted 0.82 … 0.87); shelves the cookbook shelf of slope Q1 (Q1 0.71 →
Facet Q 1.0, fitted 1.00 / 1.01); its cuts at Q1 0.71 peak at the corner (Facet Q 2·Q1 scored best: 4 → 6 on the high
pass) and its 24.7 dB/oct setting plays as Facet's 18 (fitted on the one setting with a steep cut); the tilt is a
first-order tilt shelf at 0.85 × the gain (one setting). Dynamics onto Facet's dynamic bands (range = target − gain,
threshold −90 … 0 dB, or Facet's automatic threshold).

## 5. Scores (before = LYNX before this round: Facet flat; after = the branch), round scorer

| reference → LYNX | bank | files | before → after | up / same / down |
|---|---|---|---|---|
| Eiosis AirEQ → Facet EQ | refcap0124 one-control sweeps (states synthesised from the default + the swept entry) | 154 | 6.18 → **8.93** | 71 / 83 / 0 |
| | refcap182 programme (D, S1 … S4 × 5 signals) | 25 | 4.20 → **8.52** | 20 / 5 / 0 |
| | trial190k probes (lin, stereo) | 10 | 3.70 → **8.40** | 8 / 2 / 0 |
| | all | 189 | 5.78 → **8.85** | 99 / 90 / 0 |
| Slate VM AirEQ Air → Facet EQ | sweeps / programme / probes | 31 / 25 / 10 | 2.87 → 8.10 / 6.32 → 8.12 / 4.20 → 7.70 | 51 / 15 / 0 (66 files 4.38 → **8.05**) |
| Slate VM AirEQ Earth → Facet EQ | sweeps / programme / probes | 31 / 25 / 10 | 3.35 → 7.90 / 5.52 → 7.08 / 5.10 → 6.60 | 50 / 16 / 0 (66 files 4.44 → **7.39**) |
| Slate Infinity EQ → Facet EQ | refcap182 programme (D, X1 … X5) / trial190k probes | 30 / 12 | 4.70 → 8.07 / 4.75 → 7.83 | 34 / 8 / 0 (42 files 4.71 → **8.00**) |

No file went down. Old projects: nothing of a LYNX effect changed (translators and table entries only); gold/gold.sh —
Facet EQ at its default and two band sets, the EQs still on the role entry (VM FG-73, SSL X-EQ 2), five Pro-Q 4
translations, at 44.1 / 48 / 96 kHz: 24 renders, 0 differ against the base binary. A project already saved by LYNX keeps
its saved Facet settings (substitutes are translated when the original's state is first read). CPU (cpu/bench.log):
the translated Facet costs what its bands cost, +0.04 … +0.10 s of user time per 35 s of stereo audio (≈ +0.1 … 0.3 % of
one core) over the flat Facet; latency 0 (zero-latency mode, unchanged).

## 6. Not done / next (Slate / Eiosis)
- Eiosis E2Deesser → Hush: its role entry plays Hush's earlier refit (tp172b 5.12 / prog182 6.95); not reworked here.
- AirEQ's per-band Character, the bells' "steep" switch, the shallow 6 dB low cut's resonance: no capture with them on;
  the licence ended 11-13, so they need either a licence or the coefficient design located statically (a full
  auto-analysis of the 41 MB binary, ~1 h of the machine-wide Ghidra session).
- Infinity EQ: its steep cuts and tilt rest on one setting each; Q2 (the shelves' second Q) not read.
- The other VM EQ modules (FG-73 / FG-N / FG-S / SD-PE1 / CS-EQ / CS-Lift) still open Facet flat: the same route (XML
  state + value texts + a shape fit per module) applies; their banks are thin (refcap0124 sweeps + 25 programme files each).

## 7. Valhalla

Survey (§1): every Valhalla binary is readable (JUCE, no protection). This round built the Plate part; Room was read
and left unchanged (below). Not reached: FutureVerb, Supermassive, Shimmer, Delay, FreqEcho, SpaceModulator (their stand-ins
Horizon, Nebula, Halo, Echoform, Spiral Echo, Orbit keep their black-box laws; rounds 100 / 101 / 135 / 183b / 190it).

### Valhalla reverbs: ValhallaRoom → Atrium, ValhallaPlate → Foil Plate (sub-round `rea3_190ln-vr`)

Static analysis of the official binaries (VST3 2.0.x / 1.6.x, copied read-only to `/mnt/e/lynx-work/rea3_190ln/vr/bin`), by the
owner's leave for clean-room parity work: the laws are understood and written as LYNX's own code; no code, table, preset or
other data of the plug-ins is copied. Readability first: `.text` entropy 6.46 (Room) / 6.47 (Plate), no PACE / iLok strings, no
packer. Neither keeps RTTI for its DSP (only the processor / editor classes), so the DSP was found from the processor's
vtable (`setParameter` → the DSP's per-parameter update, `processBlock` → the per-type process) and from the users of the RT60
constant ln 0.001 (float, `.rdata`). Headless Ghidra under the machine-wide lock; working files (`rea/*.c`) are not shipped.

### ValhallaPlate [STATIC] → built (`verbs::foil::reaLaw`, hidden)

- **Engine rate.** The tank runs at half the host rate (a quarter above 50 kHz): its band edge near 11 kHz at 44.1 / 48 kHz
  (rounds 100 / 172d measured the edge; Foil's 16-pole band limit stands for it). Not changed.
- **Low / High EQ.** Both are **first-order shelves**, one design: the bilinear transform (corner not pre-warped) of
  H(s) = (gLow + gHigh·s/ω0) / (1 + s/ω0), ω0 = 2π·f. Low EQ: gLow = 10^(dB/20), gHigh = 1, f = LowEQFreq (20 … 2000 Hz);
  High EQ: gLow = 1, gHigh = 10^(dB/20), f = HighEQFreq (200 … 20000 Hz); dB = −12 … +12, linear in the value. A boost
  therefore reaches far past f (+12 dB at 218 Hz still lifts 1 kHz by 2.3 dB), and a cut of the high one never reaches −12 dB
  inside the band (its zero lies above f). Round 183b had fitted second-order shelves to this by ear (half gain at
  ~0.47 / 0.5 × f, the high cut −9 dB). Applied to the wet output, both channels.
- **Decay per band.** Two sets of 16 lines share their lengths: one decays at Decay, the other — the bass, below a one-pole
  crossover at 200 Hz — at a longer time that equals Decay up to ~2.5 s, then grows to 1.4 × Decay by ~10 s and stays there.
  Two further gain sets use times of their own (one fixed, one rising slowly with Decay; their role — likely the highs above
  a ~3.2 kHz crossover — not traced). The output sums three tank taps; one of them is turned down at decays under ~3 s.
- **Built (LYNX's own law of that shape):** with the reaLaw on (revision 1, plate voicing off — the ValhallaPlate role), Foil's
  panel shelves are first order on that design and law (`Settings::shelfOrder = 1`, engine `firstOrder` shelf), and the bass
  decay multiplier is scaled by `reaBassRtMult(Decay) / reaBassRtMult(3 s)`, `reaBassRtMult(d) = 1 + 0.4·clamp((d − 2.5) / 7.5,
  0, 1)` (the voicings were fitted at the default 3 s). Tried and left out: the 200 Hz crossover for ours at 250 Hz (one file a
  step lower, none higher); the high-band times and the short-decay tap gain (no capture isolates them).

### ValhallaRoom [STATIC] → nothing built (findings only)

- Late network of 2 × 16 lines; each line's gain is the RT60 law 10^(−3 L / (fs·T)) for three times per line: Decay,
  RTBassMultiply × Decay and RTHighMultiply × Decay, plus a fourth set at Decay × f(RTBassMultiply) when the bass multiplier is
  above 1. One process function per type (12), all running on blocks of 64 samples with 8 + 2 modulation LFOs.
- HiCut: a second-order Butterworth low-pass (Q = 1/√2) on the output, and a first-order shelf to −40 dB at the same corner
  on four inner paths.
- Black-box against the bank (`lab/irlev.py`, `lab/stairs.py`, `lab/bandrt.py`): the level over Decay and every octave from
  125 Hz up are within ~1 dB already; the files that score 1 … 3 are the `stim` renders, whose energy is the 1 kHz staircase.
  Predicting the staircase from each side's own impulse response reproduces both outputs within ~0.4 dB, so the gap is linear:
  smoothed over ±4 Hz the reference's response is flat within ±1 dB, ours swings ±4 … 12 dB (its 8-line tank against the
  reference's 32 lines + modulation), and at 1 kHz it sits 3 … 5 dB under its mean on most types. More modulation (×3, matrix
  modulation 0.2 … 0.5 rad) moved the Large Room staircase by 1 … 1.5 dB only. A denser late tank is a DSP project of its own
  (owner decision below); the 63 Hz octave sits 3.5 … 5 dB under the reference at every setting (tone, not decay).

### Scores (refcap030 + the stand-ins' other banks; fxparity with round 190lg's ~85 ms alignment window, before and after)

| bank / block | files | before → after | up / same / down |
|---|---|---|---|
| ValhallaPlate mode | 48 | 3.54 → 3.54 | 0 / 48 / 0 |
| ValhallaPlate pairs | 36 | 4.03 → 4.14 | 4 / 32 / 0 |
| ValhallaPlate sr48 | 20 | 3.55 → 3.50 | 0 / 19 / 1 |
| ValhallaPlate sr96 | 20 | 3.85 → 3.85 | 0 / 20 / 0 |
| **refcap030 ValhallaPlate** | 124 | **3.73 → 3.76** | 4 / 119 / 1 |
| verb183b captures (Plate) | 18 | 3.78 → 3.83 | 1 / 17 / 0 |
| refcap182 programme (Plate) | 25 | 3.84 → 3.84 | 0 / 25 / 0 |
| tp172d (Plate) | 15 | 3.73 → 3.73 | 0 / 15 / 0 |
| ValhallaRoom (no change) | 116 | 3.31 / 4.22 / 3.44 / 3.38 (mode / pairs / sr48 / sr96) | — |

Ups: the Low EQ +7 dB impulse (spec 2.58 → 1.57 dB, rms −0.92 → −0.10 dB) and the three Decay 1.5 s music files. The one down
(sr48 S3 impulse 4 → 3, High EQ −4.8 dB) is an alignment artefact of the scorer: its octave bands moved < 0.1 dB, the third
octaves above 8 kHz by ≤ 1.7 dB toward the reference, but the cross-correlation lag of the diffuse tail jumped from +780 to
−3 534 samples (env 0.998 → 0.379, spec 1.74 → 2.92 measured on the misaligned pair).

## 8. Kilohearts

### Kilohearts (sub-round rea3_190ln-khs) — states decoded, 32 plug-ins on LYNX stand-ins [DATA + STATIC]

Lab and evidence: /mnt/e/lynx-work/rea3_190ln/khs/ (HANDOFF.md). Captures: /mnt/e/lynx-work/refcap030/Kilohearts/
(parameter tables `par/<tag>.tsv` with 41 texts per control, component states per setting `<tag>/st/{probe,sweep}/`).

**Licence state (decisive).** The Kilohearts plug-ins on this PC are Slate's iLok builds (their manuals: "The versions of
the plugins installed via the Slate Digital installers … uses the iLok licensing scheme"); the Slate trial ended 11-13.
Disperser's editor shows "No valid license found for Disperser" (refcap030/alive/Disperser.png, taken 11-15; nothing
clicked). Every plug-in (kHs Essentials, Faturator, Disperser) loads and renders, but **every render is the dry input**,
only delayed by the plug-in's latency: refcap030's scorer gives the dry input 10.00 against all 37 default renders
(`runs/b0`), and no parameter change (set, state load, 2 s message pump, transport playing) changes a sample (md5 of
25 Faturator sweep renders = its default; kHs Gain ±30 dB = 0.0 dB). So there is **no reference audio** for Kilohearts
here: the sweep / probe renders were deleted (they would mislead later rounds as references); the parameter tables and
the 1 008 component states stay (they are the plug-ins' own saves of their own parameters).

**State format [DATA].** A component state (and the VST2 chunk) is `u32 1, u32 size`, then a ZIP with one deflated entry
`state.json`: `{"meta", "version" {format, plugin build, sdk}, "preset", "model"}`. The model's top level holds every
setting in the plug-in's own units, as JSON strings, and `$type` names the effect (`disperser`, `faturator`, `chorus`,
`quick_eq`, `matrix`, `trance_gate` …). Read from the states saved after each control was moved through the public
parameter API (five values, every step of a switch):
- times in seconds (attack 0.023, delay_time 0.2), frequencies in Hz, gains and thresholds **linear** (threshold
  0.5011872 = −6 dB, gate 0.0316 = −30 dB, Gain 31.62 = +30 dB in both display modes), mixes 0 … 1, enumerations as
  names (`low_pass`, `denominator_8t`, `one_16th_t`, `overdrive` …);
- kHs Dynamics stores its ratios as slopes (1 / ratio: 0.0333 = 30:1, 2 = 1:2) and its attack / release as multiples
  of its own times; the Gate's range is the closed gate's share of attenuation (0.25 = 2.5 dB = −20·log10(1 − r));
- kHs Trance Gate keeps 8 patterns (`patterns[k].length`, `steps[i].on / tie`) and the selected one (1-based).

**Disperser's DSP [STATIC].** HeartCore.core_64 (the shared engine; .text entropy 6.40, readable; MSVC RTTI names the
classes `disperser::DisperserDsp` / `DisperserDspModel`). The DSP's process (0x180314620) builds one section from its
biquad designer (0x180baa630, case 7) with the cutoff and the pinch as Q and runs it `order` times in a row (a
cross-fade when the order changes). Case 7 is the cookbook all-pass: b0 = a2 = (1 − α)/(1 + α), b1 = a1 = −2 cos w0 /
(1 + α), b2 = a0 = 1, α = sin w0 / 2Q (bilinear, the cutoff clamped below Nyquist), realised as a state-variable section
(0x180314f30 converts the coefficients). So Disperser = `order` (0 … 32) identical second-order all-passes at the
cutoff (20 Hz … 20 kHz) with Q = pinch (0.5 … 16).

**What LYNX does now.** One translator (`translate = kilohearts`) for 32 plug-ins: it reads state.json (LYNX's own ZIP /
inflate and a small JSON reader) and sets the stand-in in the plug-in's units through the stand-in's own parameter
texts (a bracketed search over `formatEffectParam`, the middle of a rounded text's run). Mapping:

| Kilohearts | LYNX stand-in | translated |
|---|---|---|
| Disperser | Facet EQ, **new hidden all-pass cascade** (params 125–127, off = 0 for every project saved before) | order, cutoff, pinch (exact law, [STATIC]) |
| kHs Gain / Channel Mixer / Stereo | Utility | gain (linear → dB); the 2 × 2 matrix; width / mid as the M/S matrix + pan as balance |
| kHs Haas | Stereo Shaper | channel, delay |
| kHs Filter / 3-Band EQ / Formant Filter | Facet EQ | type, cutoff, Q (Facet's conventions), gain, slope (×n = 12n dB/oct, every section at Q); split shelves; two formant bells |
| kHs Ladder / Nonlinear Filter | Filter | type, cutoff, resonance / Q, 24 dB/oct |
| kHs Compressor / Compactor / Dynamics | Compressor | threshold, ratio, attack, release, make-up (Dynamics: the downward part only) |
| kHs Limiter | Limiter | post·limit_thr(pre·x) = limit_(thr·post)(pre·post·x) |
| kHs Clipper | Fruity Soft Clipper | threshold × input gain, output gain |
| kHs Gate | Sentry Gate | threshold, attack, hold, release, range, look-ahead |
| kHs Transient Shaper | Strike Shaper | attack / sustain (±100 % → ±12 dB: not measured), clip → guard |
| kHs Distortion, Faturator | Distortion | drive (Faturator 0 … 100 % → 0 … 24 dB: not measured), mix |
| kHs Bitcrush | Lo-fi | rate, bits, mix |
| kHs Frequency / Pitch Shifter | Frequency shifter / Pitch shift | shift; semitones + cents, grain, mix |
| kHs Chorus / Ensemble | Chorus | rate, depth, mix (Ensemble: detune → rate / depth) |
| kHs Phaser / Flanger | Fruity Phaser / Fruity Flanger | order → stages, rate, depth range, mix; delay, depth, rate, feedback (sign → invert), spread |
| kHs Reverb | Reeverb 2 | decay, size, damping → high damping, width, early, dry / wet |
| kHs Delay / Dual Delay | Echoform (Clean) | time or synced note (plain / dotted / triplet, else the time at 120 BPM), ping-pong / dual + ratio, feedback, duck, mix |
| kHs Tape Stop | Pocket Tape Stop | play, stop / start times |
| kHs Comb Filter | Pocket Comb Filter | cutoff → pitch (manual, no snap), feed-forward, polarity, mix |
| kHs Trance Gate | Volume gate | the selected pattern over 32 steps / bar, each run's mean ADS level, depth, attack, release, mix |

No stand-in yet (bypass, as before): kHs Ring Mod, Reverser, Resonator, Shaper, Phase Distortion; Multipass and Snap
Heap (containers: their snapins live in the same JSON model — a later round can map them, the snapin translators are
ready).

**Scores.** None can be given: without a licence the references are the dry input (a "score" against them would reward
bypass). Before = bypass (LYNX's behaviour for a missing plug-in), after = the stand-in; the laws come from the
plug-ins' own parameter texts, their manuals and, for Disperser, the static read above.

**Gate.** Old projects: Facet EQ with the cascade off renders byte for byte as the base binary — 117 / 117 renders
(18 Pro-Q 4 states through the Pro-Q translator + the defaults of all 21 stand-ins used, at 44.1 / 48 / 96 kHz;
`khs/gold/gold.log`). A project that holds a Kilohearts plug-in which is missing now opens a stand-in instead of a
bypassed slot (the purpose of the entries; the original reference is kept byte for byte as always). CPU (35 s stereo
48 kHz, one pinned core, best of 5 incl. file I/O; `khs/cpu/bench.log`): Facet default 0.19 → 0.18 s (unchanged);
Disperser on Facet at order 16 0.31 s, at 32 (its maximum) 0.44 s: ≈ +0.4 / +0.7 % of a core over a plain Facet.
Tests: tests/Parity190lnKhsTests.inc (4 tests); tests/Parity190jhTests.inc's stored-count check updated (three hidden
parameters after the automatic thresholds).

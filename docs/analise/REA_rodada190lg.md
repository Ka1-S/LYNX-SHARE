# Round 190lg: the scorer's alignment window, Saturn 2's small-signal offset, Pro-R 2's late level, Crest Versatile (0.1.30 parity)

2026-11-18. Continues round 190kw (docs/analise/REA_rodada190kw.md). Owner's permission as there: static analysis of
third-party plug-in binaries for sound parity, clean-room only — laws and structure written down in our own words and
re-written as LYNX's code; no code, tables, presets or content of the references copied; no protection bypassed. Evidence
markers as in 190kw: [STATIC] read in the decompiled / disassembled binary, [EXEC] measured on renders, [CODE] LYNX source.

## 1. The parity scorer's alignment window is now in time (owner decision 1) [CODE + EXEC]

`scripts/flref_compare.py` `find_lag` searched ±4 096 samples at every rate: 85 ms at 48 kHz but only 42.7 ms at 96 kHz,
so a reference with more latency than that (Pro-L 2's Dense / Loud / Safe report 6 057 samples = 63 ms at 96 kHz) could
not be aligned there. The window is now `lag_window()` = max (4 096, ⌈(4 096 / 48 000) · rate⌉) samples: 85.3 ms at 48,
88.2 and 96 kHz (8 192 at 96 kHz), 92.9 ms at 44.1 kHz (never narrower than the old 4 096). The search span keeps its old
default of 1 764 000 samples at every rate. Consequence: every result at 44.1 / 48 kHz is bit-identical to the old
scorer; only files above 48 kHz can change. All scoring from this round on uses the new window.

Re-baseline, SAME binary (rea190kw's fxrender `fxrG`) scored by the old and the new scorer:
- refcap030, every block of Pro-L 2 / Pro-Q 4 / Pro-MB / Pro-G at all rates: 966 files 7.94 → 7.98 (10 up / 956 same /
  0 down); every 44.1 / 48 kHz row byte-identical. The other banks (refcap182 programme, tp172b, tp172d, verb183b) are
  48 kHz only, so their baselines are unchanged by construction.
- refcap030, every 96 kHz file of every tag with 96 kHz captures (29 tag / block rows, 801 files): 4.53 → 4.57
  (10 up / 788 same / 3 down). Per row:

| bank (tag) | block | files | old window | new window | up / same / down | rows identical |
|---|---|---|---|---|---|---|
| Decapitator | sr96 | 24 | 4.46 | 4.46 | 0 / 24 / 0 | 12 |
| EchoBoy | sr96 | 20 | 4.90 | 4.90 | 0 / 20 / 0 | 15 |
| EchoBoyJr | sr96 | 20 | 4.15 | 4.15 | 0 / 20 / 0 | 15 |
| FabFilter_Pro_C_3 | sr96 | 20 | 7.65 | 7.65 | 0 / 20 / 0 | 8 |
| FabFilter_Pro_DS | sr96 | 15 | 7.13 | 7.13 | 0 / 15 / 0 | 10 |
| FabFilter_Pro_G | sr96 | 24 | 7.58 | 7.58 | 0 / 24 / 0 | 13 |
| FabFilter_Pro_L_2 | sr96 | 24 | 6.21 | 7.54 | 10 / 14 / 0 | 6 |
| FabFilter_Pro_MB | sr96 | 20 | 7.90 | 7.90 | 0 / 20 / 0 | 11 |
| FabFilter_Pro_Q_4 | sr96 | 15 | 9.73 | 9.73 | 0 / 15 / 0 | 10 |
| FabFilter_Pro_R_2 | grid2 | 15 | 3.53 | 3.53 | 0 / 15 / 0 | 12 |
| FabFilter_Pro_R_2 | sr96 | 16 | 4.06 | 4.06 | 0 / 16 / 0 | 10 |
| FabFilter_Saturn_2 | grid | 280 | 3.45 | 3.45 | 0 / 280 / 0 | 134 |
| FabFilter_Saturn_2 | sr96 | 20 | 5.60 | 5.60 | 0 / 20 / 0 | 10 |
| FabFilter_Timeless_3 | grid2 | 25 | 5.88 | 5.88 | 0 / 25 / 0 | 22 |
| FabFilter_Timeless_3 | sr96 | 16 | 7.56 | 7.56 | 0 / 16 / 0 | 12 |
| LittleAlterBoy | sr96 | 12 | 3.25 | 3.25 | 0 / 12 / 0 | 8 |
| LittlePlate | sr96 | 16 | 3.88 | 3.88 | 0 / 16 / 0 | 7 |
| LittlePrimalTap | sr96 | 20 | 3.90 | 3.90 | 0 / 20 / 0 | 15 |
| PanMan | sr96 | 15 | 5.20 | 5.20 | 0 / 15 / 0 | 10 |
| PrimalTap | sr96 | 20 | 3.15 | 3.15 | 0 / 20 / 0 | 15 |
| ValhallaDelay | sr96 | 16 | 4.06 | 4.06 | 0 / 16 / 0 | 12 |
| ValhallaFreqEcho | sr96 | 16 | 3.31 | 3.31 | 0 / 16 / 0 | 12 |
| ValhallaPlate | sr96 | 20 | 3.90 | 3.85 | 0 / 19 / 1 | 6 |
| ValhallaRoom | sr96 | 16 | 3.44 | 3.38 | 0 / 15 / 1 | 9 |
| ValhallaShimmer | sr96 | 16 | 3.69 | 3.69 | 0 / 16 / 0 | 11 |
| ValhallaSpaceModulator | sr96 | 20 | 4.40 | 4.40 | 0 / 20 / 0 | 15 |
| ValhallaSupermassive | sr96 | 20 | 5.70 | 5.70 | 0 / 20 / 0 | 14 |
| ValhallaUberMod | sr96 | 20 | 3.95 | 3.85 | 0 / 19 / 1 | 15 |
| ValhallaVintageVerb | sr96 | 20 | 3.70 | 3.70 | 0 / 20 / 0 | 12 |
| **all** | | 801 | 4.53 | 4.57 | 10 / 788 / 3 | 461 |

The ups are Pro-L 2's Dense / Loud / Safe states (sr96 6.21 → 7.54: D_mix 4 → 10, D_stim 4 → 8, S4_stim 4 → 8 …; the lag
found is now the reference's real 4 796 … 5 476 samples). The three downs are reverb / modulation tails whose
cross-correlation peak at a lag beyond the old window is spurious: ValhallaPlate sr96 S1_stim 4 → 3 (lag 3 724 → 8 141),
ValhallaRoom sr96 S2_stim 4 → 3 (327 → −8 103), ValhallaUberMod sr96 S1_stim 4 → 2 (0 → −7 013). They are scorer
artefacts of the wider search on diffuse material, not sound changes; listed for the owner.

## 2. Documentation: what the stand-ins were informed by (owner decision 2) [CODE]

`src/core/PluginSubstitutes.h` said the parameter maps never used decompiled third-party code; CONTRIBUTING.md,
docs/PLUGIN_FALLBACK.md, docs/PLUGINS.md ("Fitted") and docs/PARITY.md (round 128's sources) said the same. Since 2026-11,
by the project owner's decision, some stand-ins and translators (the FabFilter roles from round 190kw on) were also informed
by clean-room static analysis of the third-party binaries. All five now say so: laws and structure in our own words, no
third-party code / tables / presets / content copied, licence protections never bypassed, evidence in docs/analise/REA_*.md;
static analysis of a third-party binary only by the owner's decision. Statements about round 101's four delay /
modulation effects ("nothing in them was decompiled") and README's "studied from the outside" (FL Studio / Live) were left
as they are: still true for what they describe.

## 3. Saturn 2 → Ember: the ~1 dB small-signal offset found [STATIC + EXEC] → landed (merged)

Branch `rea2_190lg-sat` (off `origin/rea190kw-sat` 751779be4). Work folder `/mnt/e/lynx-work/rea2_190lg/sat/`.
Clean-room: static reading of the plug-in's code for its structure and laws, level probes on the refcap030 captures;
LYNX's code is written from the laws below, nothing copied.

### Method

- `probe/refbank.py`: the level-sweep response of every refcap030 Saturn 2 reference render (`lvsweep`: five 2 s log
  sweeps at −39 … −3 dBFS) per third octave and level (496 renders: grid / styles / mode / pairs / sr blocks).
- `py/law.py`: a Python mirror of LYNX's revision-1 law (curves, drive range, compensation, the style EQ sections); its
  analytic small-signal response matches LYNX's own render within 0.02 dB (Warm Tube 25 %, every band below 16 kHz).
- `py/sim.py`: the same law as a nonlinear model on the sweep (4× FFT oversampling), to test where a gain sits
  (`hyp.py`, `fitdrv.py`, `cliptest.py`); `py/qtest.py` compares the cut-filter Q conventions.

### Finding 1: the offset is the reference's channel soft clipper [STATIC + EXEC]

- The reference minus the law, at −39 dBFS, 150 Hz … 3 kHz: **+1.023 dB for Clean / Warm / Broken Tube, Clean / Warm /
  Old Tape, Gentle / Heavy Saturation, Tweed and the FX styles at drive 0** (coherent gain, cross-spectrum = power ratio,
  residual ESR −84 dB), identical at 44.1 / 48 / 96 kHz and High Quality off / good / superb; the ratio is 1.1249
  (Clean Tape, Gentle Saturation, Breakdown, Destroy) = **9/8**. **0.00 dB for the four Subtle styles and Gentle / Warm
  Transformer** (Subtle Tape: 1.0121 = its trim of +0.1 dB alone).
- A post gain of 9/8 alone fits Clean Tape at drive ≥ 0.5 at every level, but at low drive the reference also compresses
  more near 0 dBFS (a "drive shift" of +4.1 dB at drive 0, +0.6 dB at 25 %, 0 from 50 % on): a stage after the curve
  that saturates near full scale.
- Statically: `SaturnProcessor::SaturnChannel` (constructor 0x180179820) ends with a `SoftClipper` (constructor
  0x1801164b0, process 0x180116720, threshold listener 0x180116910): `u = clamp (k·x, ±1)`, `y = T · (1.5u − 0.5u³)`,
  `k = (1 / (12T²) + 2/3) / T`, T = 10^(threshold / 20), threshold 0 dB by default (constants 1.0 / 0.5 at 0x1801f8550 /
  0x1801f8560, 12 at 0x180216a0c, 2/3 at 0x180216620). Its small-signal gain is 1.5·k·T = **1 + 1 / (8T²) = 9/8** at
  T = 1, its ceiling T (0 dBFS) from |x| ≥ 4/3. (It runs oversampled when the channel's oversampler factor is ≥ 2.)
- With this clipper after the law the nonlinear model fits the reference's sweeps at **every level and drive within
  0.03 dB** (mid-band median per level, −39 … −3 dBFS): Clean / Warm Tube, Clean / Warm Tape, Gentle / Heavy Saturation at
  drive 0 / 25 / 50 % (`probe/clip_g0.txt`, `cliptest.py` runs in HANDOFF); Old Tape within 0.12 (0.44 at −27 dBFS),
  Tweed 0.2 … 0.26.
- The Subtle styles and the transformers fit with no clipper at all (with it they would be 1 dB louder and compress near
  0 dBFS). Why the channel's clipper does not act on them was not found in the static analysis (the style set-ups differ
  only in their EQ, the noise level of the NoiseFilter and the effect switches); LYNX applies it by style
  (`saturnClipStyle`) as measured.
- Not the cause (checked): the NoiseFilter (0x180115490: input + triangular noise × a signal-following level, −110 dB,
  −144 dB for the Subtle styles: additive and incoherent), the Compander (0x18011a200: gain 1 at Dynamics 0), the drive /
  compensation tables (0x180113ed0 / 0x180114090 / 0x1801140d0 / 0x180117090: 256 points over 0 … 1, exactly the
  recovered formulas), the curve implementations (the same curve object serves the compensation and the processing; Subtle
  Tape and Clean Tape share the sine path but differ), the style controller (0x180167300: set-up, cabinet, the Compander's
  cabinet flag only for the four amps).

### Finding 2: the +1.4 / +3.5 dB at 12.7 / 16 kHz is the cut filters' Q convention [EXEC]

- The reference's cut filters (low / high pass of slope index n) at Q 1 are Butterworth: LYNX's sections scaled the most
  resonant pair by q / √½ (Q 1 → a resonant 1.0), which gave +1.3 dB around 31 Hz (the 24 Hz high passes) and +1 … +5 dB
  at 12.7 … 16 kHz (the 18 / 20 kHz low passes of Warm Tube / Warm Tape, Old Tape's 7 kHz). With the pair × q (Q 1 =
  Butterworth) the small-signal residual of Clean / Warm Tube and Warm Tape is within 0.1 dB from 40 Hz to 16 kHz at
  44.1, 48 and 96 kHz (rms over the bands 1.00 → 0.13, 0.70 → 0.12, 0.55 → 0.08).

### Finding 3: the DC blocker [EXEC]

- After a loud Warm Tube passage the reference's output DC decays with τ ≈ 0.106 s (≈ 1.5 Hz one-pole: 0.0546 → 0.0213
  → 0.0083 → 0.0013 over 0 / 0.1 / 0.2 / 0.4 s); Ember's 8 Hz blocker (τ 20 ms) also took 0.3 dB off the 31 Hz band of
  every law style. The law bands now use a 1.5 Hz blocker.

### Still open

- The amps (Tweed … Power Amp): the clipper removes 1 dB of their offset, but their 4 … 8-filter EQs still miss by up to
  30 dB above 5 kHz (the bell / shelf Q conventions and slope indices of the amp sections); the transformers' dynamic knee;
  Broken Tube's level above −15 dBFS (−1.6 dB); why the Subtle / transformer styles skip the channel clipper.

Gate (time-based scorer before and after; base = rea190kw, Ember revision 0): refcap030 819 files 3.84 → 5.14 (439 up /
380 same / 0 down; round 190kw's law alone: 4.20, 33 down), refcap182 programme 6.16 → 7.88 (19 up / 0 down), tp172b 5.1 →
7.75 (20 up / 0 down). Old projects 48 / 48 byte-identical. Per block: docs/PARITY.md "Round 190lg: the ~1 dB small-signal
offset found".

## 4. Pro-R 2 → LuxeVerb Space revision 1: the levels refitted on time windows [STATIC + EXEC] → NOT merged (17 files down: owner decision)

Branch `rea2_190lg-r2` (off `rea190kw-r2` 435966430). Lab: `/mnt/e/lynx-work/rea2_190lg/r2/` (lab/svlab.cpp = core/SpaceVerb.h
alone, equal to fxrender; lab/tw.py time windows × bands; lab/comp3.py + lab/fit4.py component fit; lab/lab2.py real-score
bank runs; labruns/*.json). Scorer: round 190lg's time-based alignment window (63b3ce513) for before and after. Static
evidence: `/mnt/e/lynx-work/rea190kw/rea/pror2/` (d1.c) and this round's `th.c` (the Thickner's helpers, copy of the
ProR_2 Ghidra project). LYNX's code is written from the laws below, not from decompiled text.

### What was wrong in 190kw-r2 [EXEC]

Energy per time window (dB, mono sum, from the reference's onset; Space 50 %, Modern, 100 % wet), LYNX − reference:

| window (s) | 0.05–0.1 | 0.2–0.4 | 0.4–0.8 | 0.8–1.6 | 1.6–3.2 | 3.2–6.4 |
|---|---|---|---|---|---|---|
| rate × 0.25, 190kw-r2 | −1.5 | +8.9 | +26.1 | +53.0 | +104.8 | . |
| rate × 4, 190kw-r2 | +9.9 | −2.5 | −7.4 | −14.0 | −22.3 | −23.7 |
| rate × 0.25, this round | +1.4 | −0.2 | +5.3 | +11.5 | +24.3 | . |
| rate × 4, this round | +2.4 | −2.1 | −2.3 | −2.1 | −1.6 | −1.1 |

(At rate × 0.25 the remaining excess sits 70 … 125 dB under the peak window.)

Rendering the components alone showed why: the early network (1.15 × displayed time, no rate) carried the whole tail and
the late network sat 6 / 14 / 25 dB under the reference at rate × 0.25 / 1 / 4 (the fitted factor 0.0761 · T^−0.944 had
pushed it down; the fit only saw the total level and the spectrum, where the two networks trade freely). With the tail
in the wrong network a long rate died early, a short one rang 2.9 s, and Ducking (which dips the wet while the input
plays) left almost nothing after the dip at long rates (−8 … −16 dB).

### Laws used

- **Late level** [STATIC, re-read]: 0x180157290 sets the late gain from T at +0x2748, which 0x180156940 writes as
  rate × 1.15 × displayed (fVar3 · fVar5); the 2-line T (+0x668) is ((T − 200) · c + 200) × rate. The value at +0x39d8
  (1.15 × displayed, no rate) has a consumer that was not traced: the analyst's "early network decays at T without the
  rate" is not confirmed, and the captures show no rate-independent tail (rate × 0.25: −81.5 dB at 0.8–1.6 s, −150 dB
  at 1.6–3.2 s). LYNX keeps the law's form and shortens the early network to 0.3 × 1.15 × displayed (a build-up over the
  first ~100 ms, the windows the late network has not reached yet).
- **Distance** [EXEC]: the reference's early windows by Distance (Space 25 %, dB): 0–5 ms −11.7 / −18.2 / −23.4 / −52.6 /
  −41.0 at D = 0 / ¼ / ½ / ¾ / 1; 5–20 ms −10.6 … −33.2; 20–50 ms −16.5 … −27.0; 50–100 ms −17.2 … −19.9; the tail
  (0.1 s on) within 1.6 dB. LYNX's early network and 2-line network did not depend on Distance at all; they now fall
  with it (fitted dB polynomials), the early reflections more steeply, and the whole wet gets a fitted trim by Distance.
- **Thickness = the Thickner** [STATIC + EXEC]: class `Thickner` (vtable 0x18020c078; process 0x180152fe0, parameter
  update 0x1801532d0, constructor 0x180152c80). Amount u = max (t, 0): pre gain (20u − 12) dB (0x1802133d8 = 20,
  0x180213334 = 12, exp at 0x1801c9850 with ln 10 / 20), saturation (2/π) · atan (π/2 · x) (0x180121b30: vectorised atan,
  scale 1.5708, output × 0.63662), post gain −(1 + 0.1u)(20u − 12) dB, × the gain of a `Compander` (constructor
  0x1800ffbd0: two Detector / Capacitor pairs, one at −6 dB ratio 2 (12 / 36) with 0.5 s / 0.2 ms, one at −48 dB ratio 4
  (0 / 24) with 0.25 s / 5 ms; its amount = t × 0.5). Measured: an impulse's response moves uniformly by −13.2 dB per
  unit below 0 (every window, the early reflections included) and by about 6.8 (1 − (1 − t)²) dB above, while a music wet
  (median −25.8 dBFS short-term) moves by −0.2 dB at t = −0.6 and −1.4 dB at t = +0.6: the gain acts on a quiet wet only.
  LYNX: the measured gains on the body (late, 2-line, early; the reflections too below 0), applied in full while the wet's
  (l² + r²) / 2 envelope (20 ms / 300 ms) is under −38 dBFS and not at all above −28 dBFS. The Compander's detector
  semantics (knee / range fields) were not resolved, so this is a level gate fitted on the captures, not its circuit.

### Findings not built (for the next round)

- **The reference's early reflections are fixed taps** [EXEC]: at 44.1 / 48 / 96 kHz and every Space / Distance the
  impulse captures put single-sample reflections at L 6.69, 11.33, 18.35, 25.42 ms and R 8.31, 12.50, 21.15, 32.40 ms
  (levels change with Space and Distance, the times do not). LYNX's taps (tp172b's 12) do not line up, so fxparity's
  alignment (`find_lag`, cross-correlation of the mono sums) often locks onto a wrong lag (e.g. +1238 samples = LYNX's
  first 26 ms cut on most Space-25 % files), which moves rms / spectrum by 1 … 3 dB at random. Matching these taps should
  stabilise the alignment and the waveform match (ESR).
- **Steady 1 kHz tones are a modal lottery**: the stim and noise-burst signals end with a long 1 kHz sine that dominates
  their rms. A static FDN's response at one frequency is Rayleigh-distributed (±5 dB); the reference itself varies
  ±1.5 dB between identical captures (its modulation). 190kw-r2 had L / R +3.0 / −4.8 dB on that tone, this round
  +6.4 / −2.4, revision 0 −0.5 / −1.5: G4 (Rate × Ducking), the mode stims and S1 / D stims move with it.

## 5. Pro-C 3 → Crest: Versatile (the reference's default style) rebuilt as Allround under revision 1 [STATIC + EXEC] → landed (merged)

Branch `rea2_190lg-vers` (off `rea2_190lg`). Evidence: the coordinator's notes `vers/NOTES.md`, the decompiled files
`/mnt/e/lynx-work/rea190kw/rea/out/proc3_vers*.c`, `proc3_det.c`, `proc3_cap.c`, this round's `lin.c` (LinearCapacitor,
RandomModulationProducer, RangeLimiter; Ghidra project copy), `proc3_rtti.txt`. LYNX's code is written from the laws
below, not from decompiled text; the reference's piecewise-linear knob curves are not copied (LYNX fits its own smooth
laws to their shape).

### The graph [STATIC] (VersatileCompressor_MainCompressorAlgorithm: constructor 0x180164ae0, wiring 0x180167a00,
### parameter update 0x1801673c0, outer → inner 0x1801688b0)

x = the linked side-chain magnitude (as for round 190kw's V2 styles).
- slow: sA = 0.979424 x + mA · max (EQ204 (x), 0) (Mixer ac; RangeLimiter b0 = clamp (0, 10000); mA = Mixer b5 =
  0.020348 + 0.015089 · a RandomModulationProducer) → Detector a6 → Capacitor a5 (attack 0.0008 + 0.2492 a³ s, release
  0.01 + 0.968061 tan (1.2 r²) s, auto-release constant 2.049379 r² + 0.0905348, two step terms from the attack knob).
- fast: sB = 0.91 x + mB · min (x, 0) (Mixer ad, RangeLimiter ae = clamp (−10000, 0); mB = 0.09 + random) → EQFilter a9
  (2^12.64 ≈ 6.39 kHz) → Detector a7 → LinearCapacitor a8. With x a magnitude, min (x, 0) = 0: the random term never
  reaches the fast path.
- reduction = max (S, w · F) (Mixer aa, mode max), w = clamp (0.9 + 0.0397797 · F, 0.7, 1.06) (Mixer ab + RangeLimiter b3).
- LinearCapacitor (vtable 0x18021fe08, process 0x18010fd90): a slew limiter. Per sample the output moves toward its
  input by at most unit / (rate · T) — T = the attack time rising, the release time falling (each ≥ 0.1 sample),
  unit = 1 (24 in its other mode). Its two times come from the knob times through two piecewise-linear curves.
- RandomModulationProducer (process 0x18011b0d0, next value 0x18011b360): a 24-bit LFSR (taps 0x11d872b) drawing values,
  joined linearly or by a cubic (2t³ − 3t² form) at the node's rate (fields 0.011 / 7.822 and 0.008 / 9.059).

### [EXEC] what the bank settled (refcap030 Versatile files, new scorer; lab: env-overridable law in a lab build)
- Domain: the detectors' output read as dB of reduction compressed every file by 1.5 … 4 dB (36 of 38 down); read as
  the reduction share 1 − 10^(−GR / 20) (as round 190kw's Mastering / Vocal) the steady files reach ESR −42 … −50 dB.
- The attack knob's two step-dependent Capacitor terms (LYNX's p1 / p2 or p5 / p6 readings of the two curves, every
  assignment tried): all scored lower than leaving them neutral (6.50 / 5.55 / 7.13 against 7.87): not built.
- Release of the fast path: with LYNX's fit of the reference's curve the fast path held the reduction up on music
  (−0.3 … −0.6 dB, 4 files down); at half of it round 108's measured burst (attack 10 / release 200 ms, the gain back
  half-way after 61.6 ms on the reference) reads 59.5 ms (full curve 120 ms, no fast path 28 ms) and the bank is
  7.82 with no file down — taken.
- Random modulators: their mean (mA = 0.020348).
- EQ shapes (enum 0, the second value 0.12 / 0.16 unresolved): two-pole low-passes at 204 Hz / 6.39 kHz.

### LYNX's own smooth knob laws (fitted to the curves' shape, not their points)
- T3 (attack s): 0.08 · s / 0.02 up to 0.02 s, then 0.15 · (s / 0.2)^0.273.
- T4 (release s): 0.19 + 0.55 s + 0.03 s² (half of the fitted 0.38 + 1.1 s + 0.06 s²).

## 6. Pro-Q 4 → Facet EQ: the dynamic band's auto threshold on the reference's own detector [STATIC + EXEC] → landed (merged)

Branch `rea2_190lg-q4` (off `rea2_190lg` e821c2616). Evidence: `/mnt/e/lynx-work/rea190kw/rea/out/q4_bc.c`,
`q4_bcctor.c`, `q4_f.c` and this round's decompiles (copy of the ProQ_4 Ghidra project): `auto.c` (0x180116380 /
0x180116100 / 0x18012fda0 / 0x18012be30), `xref_bc.c` (the band that builds the compander), `k.c` (0x180129710).
LYNX's code is written from the laws below; the one knob table (0x180129710) is not copied — LYNX fits its own curve.

### The structure [STATIC]
- BandCompander (vtable 0x1802403c0, process 0x18014e820). With Auto on ([0x35], default 1) it runs a `NormalizerStub`
  (constructor 0x180116380, process 0x180116530): two followers (0x180116100: one-pole, coefficient
  1 − exp(−ln 10 / (T · rate)), attack T when rising, release when falling: 0x18012bfe0) with attack 0 and release 10 s,
  the output type 0 (|x| / envelope) and the dB flag on. Per sample: env = follower (|x|); out = |x| / max (env, 0.01)
  in dB (floor −40 dB). The second follower runs only while a start-up count is positive and then hands its state to
  the first (the count's setter was not traced: not built).
- The level then goes through the usual gain computer 0x18012fda0 (threshold, quadratic knee, ratio, range). The
  threshold / knee buses' values in Auto mode were not traced: LYNX keeps round 190js's fitted mapping (threshold
  = the peak level − |range|, knee = 1.53 |range|), which the captures confirm with the reference's own detector.
- Three more followers smooth the reduction ([0x7c] f0, [0x7d] f1, [0x7e] f2): y0 = f0 (GR), y1 = f1 (y0),
  y = f2 (max (y0, y1)). Their times (0x18014ee20) come from the compander's region (state values 17 / 18 of the band:
  its detection range in log2 Hz, 10 Hz … 20 kHz by default) and two knobs (BC +0x50 = attack %, +0x2d8 = release %):
  rel = 0.005 + 2.495 K (release % / 100) s (K: a 6-point table); u = 1 − 0.1 clamp (lo − 4.5, 0, 10);
  atk = (A² ((3.6e-5 u² + 6e-5) u² + 6e-5 u² + 1e-4))² · 0.59975 + 0.00025 s; centre = 2^((lo + hi) / 2);
  low = 100 / max (centre, 100); wide = (clamp (hi − lo, 2, 8) − 2) / 6.
  f0: attack 0, release (min (1, wide + low) · 0.02 + 0.015) s; f1: attack max (0.12, 0.3 − 30 atk), release
  1.29 rel + 0.25; f2: attack 0.01 wide + atk, release (0.25 low + 0.45) rel.

### [EXEC] what the bank settled (refcap030 grid2 = the auto sweep; gain trajectories in 50 ms frames)
- Envelope 10 s, floor −40 dB, knee 1.53 |range|: the minimum of the trajectory error (8 s / 12 s and knee 1.4 / 1.7
  all worse).
- With the reference's times at scale 1 the band released ~3× too slowly and attacked over 250 ms where the captures
  reach the whole range within 100 ms. Fitted scales: f1 × 0.65, f2 attack × 0.05, f2 release × 0.6 (mean trajectory
  error 0.33 dB → 0.16 dB). The uncertain link is the knob percent: LYNX holds attack / release in ms and maps them back
  (50 % = 9.22 / 268 ms, the translator's law).
- LYNX's release curve: K (x) = (e^(3.32 x) − 1) / (e^3.32 − 1) (own smooth fit, not the table's points).

## 7. Pro-MB → Prism Multiband: the band compander's own graph [STATIC + EXEC] → built (revision 1, branch `rea2_190lg-mb`, 0 files down)

Branch `rea2_190lg-mb` (off `rea2_190lg` 2fbced9c7). Evidence: this round's decompiles of a copy of the ProMB Ghidra
project (`/mnt/e/lynx-work/rea2_190lg/mb/out/`: `xref_bcvt.c` = the BandCompander constructor 0x1800f5210, `bc_proc.c`
= its process 0x1800f6210 and its updates 0x1800f6b20 / 0x1800f6820 / 0x1800f67d0 / listener 0x1800f6640, `nodes.c` =
the node constructors, `procs.c` = the node processes, `ph.c` = PeakHold), constants read with `rd.py`. LYNX's code is
written from the laws below; the one knob table (0x1800d8b50, 7 points) is not copied: LYNX fits its own smooth curve
to the shape of the composite release law.

### The graph [STATIC] (constructor 0x1800f5210)
Nodes (BandCompander offsets): DelayFilter +0x198 → DBConverter +0x188 → Detector +0x190 → Router +0x590 → PeakHold
+0x3a8 → EQFilter +0x5a0 → Capacitor A +0x170; Mixer +0x298 (mode 2 = max) of (A, EQFilter) → Capacitor B +0x178 →
Router +0x598 → × two parameter sources (+0x2a8: a 0 … 2 amount, default 1; +0x3b0: the output sign / scale) → 10^(x/20)
(the vectorised exp in 0x1800f6210). Capacitor B's release bus is driven by a Mixer +0x2a0 (sum): the release time
(+0x4d8) + w · Capacitor A (w = 0, or 0.007 s per dB in the inverted modes). Two StaticMixers (+0x5a8 / +0x5b0) give
36 − x for the Routers' second inputs.

Modes (0x1800f6820; DynamicsMode +0x60, range +0x120):
- compress with range < 0 (downward compression) or expand with range > 0 (upward expansion): Detector purpose 0
  (the compressor curve: overshoot over the threshold through the quadratic knee, × (1 − 1/ratio) under 100 : 1),
  its range = |range|, its soft-range width = 0.6 · knee + 12 dB; Routers straight; output sign −1 / +1. **Built.**
- the other two (upward compression, downward expansion): Detector purpose 1 (below the threshold, a one-sided knee,
  × (ratio − 1), with a hysteresis flag), range 36, soft width = knee; Routers take 36 − x; output × range / 36; B's
  release + 0.007 · A. **Built** (see [EXEC]).

Detector (process 0x1800c09a0, general path) [STATIC]: dB domain (GainChangeDomain 1); then the soft range: with
v = R − GR inside ± S/2, GR = R − ((S/2 + v) / S)² · S/2; GR ≤ R, ≥ 0. Purpose 1 (the inverted modes): under = threshold −
level; ≤ 0 → 0 (a flag that compares the overshoot with an unconnected bus, value 0: no hysteresis in this graph); inside
the knee (under / K)² · K/2, past it under − K/2; × (ratio − 1). The plug-in binds the band's ratio / range parameters to
the compander's properties directly (ProMBBand constructor 0x1801194a0), so the ratio is the plain ratio.

Times (0x1800f6b20; a = attack %, r = release %, 0 … 100):
- rel = 0.005 + 2.495 K'(r / 100) s; K = a 7-point knob curve, K' = K up to 0.7, then 0.7 + 4.333 (K − 0.7).
- Capacitor A: attack (1 − min (1, 0.05 a)) · 0.5 + 0.12 s, release (rel + 1) · 0.25 s.
- Capacitor B: attack 6e−9 a⁴ + 0.00025 s, release = rel (the bus), p5 = 1 − 0.7 (1 − min (1, 0.05 a)), p6 = 0.1 rel
  (the Capacitor class is byte-identical to Pro-C 3's: REA_rodada190kw §2's v2Step).
- Look-ahead switch on (+0xc8): lead = clamp (500 / max (centre, 100) + the band's look-ahead ms, 0.1, 20) ms, centre
  = 2^((lo + hi) / 2) of the band's edges (log2 Hz); the detector input is delayed by 20 − lead ms while the plug-in's
  latency is 20 ms (refcap030 manifest: 960 samples at 48 kHz; 0 with the switch off). Off: no delay, lead 0.1 ms.
- PeakHold (process 0x1800c32d0 / 0x1800c33b0: a sliding maximum, time in ms × 0.001 × rate): hold = 1.1 · lead +
  (r + 2) (1 − min (1, 0.05 a)) ms (switch off: 0.11 + the same knob term).
- EQFilter (shape 0, order property 1): frequency log2 (650 / lead) → 650 / lead Hz (switch off: 6.5 kHz). LYNX models
  it as a two-pole Butterworth low pass (the lab compared one pole).

Constants (rd.py, FabFilter_ProMB.dll): 0x1801a3124 6e−9, 0x1801a3154 0.00025, 0x1801a3670 2.495, 0x1801a3174 0.005,
0x1801a32e0 0.7, 0x1801a3700 4.333, 0x1801a31cc 0.05, 0x1801a32b4 0.5, 0x1801a3214 0.12, 0x1801a3250 0.25,
0x1801a33d8 0.1, 0x1801a38b0 500, 0x1801a3868 100, 0x1801a3540 1.1, 0x1801a3204 0.1, 0x1801a38bc 650,
0x1801a3388 0.02 (s, the delay line), 0x1801a380c 36, 0x1801a32c8 0.6, 0x1801a3768 12, 0x1801a317c 0.007,
0x1801a31a4 1/36, 0x1801a33f8 0.12 (s, PeakHold's buffer), 0x1801a3870 120 (ms, PeakHold's maximum).

### LYNX's own release law (fitted to the curve's shape, not its points)
rel (x) = 0.005 + 1.76 x² + 3.235 · (σ (x) − σ (0)) / (σ (1) − σ (0)) s, σ (x) = 1 / (1 + e^(−(x − 0.86) / 0.035)).

### [EXEC] what the bank settled (refcap030 Pro-MB, new scorer; band gain trajectories in 50 / 100 ms frames)
- The reference's laws at scale 1 match: the default bass band (S1: 30 – 200 Hz, −12 dB, 4 : 1, 20 / 20 %, knee 24 dB)
  on the mix: mean |band gain error| 1.11 dB (fxrG) → 0.02 … 0.05 dB; attack / release 20 … 80 % (PT pairs) 0.02 … 0.05 dB;
  look-ahead off 1.81 → 0.07 dB; S2's two bands 0.50 / 0.87 → 0.09 / 0.06 dB.
- The soft range is the main term: with round 150's 12 dB knee (lab `oldSoft`) the S1 mix error stays 0.96 dB.
- Inverted modes: the raw structural slope (ratio − 1) · |range| / 36 matches (S3 downward expansion stairs 4.97 → 0.04 dB,
  M_DynamicsMode 1 0.47 → 0.003, upward compression +18 dB 3.33 → 0.02 dB); reading the slope as the gain's own
  (ratio − 1 per dB) was 1.33 / 1.19 dB off.
- The EQFilter as a two-pole Butterworth low pass; no time scale was needed (unlike Pro-Q 4's round).

## 8. Pro-L 2 and Pro-G: not read (their styles are embedded presets) — owner decision

[STATIC, names only] Pro-L 2's style list is the strings `transparent.limiter`, `punchy.limiter` … `safe.limiter`; the
function at 0x180133d30 hands each name to 0x1800e4f10, which builds a `PresetPreloader` that loads the style from the
plug-in's internal preset store: each style is a preset (parameter set) of its generic `DynamicsEngine` (Detector /
Capacitor / PeakHold / AveragingFilter / TransferCurve / Saturator). Pro-G's `algo1.expander` … `algo6.expander` are built
the same way. Reading them would be reading the reference's presets, which the clean-room rule excludes, so they were not
read. Options for the owner: (a) allow reading only their structure (which elements, wired how) while every number stays
fitted black-box on the bank; (b) black-box fitting only on the weak spots (Pro-L 2: Style 0.571 stairs, S3, the pairs
block's Gain 0.8; recommended now); (c) leave them. Pro-L 2's sr96 block already moved 6.21 → 7.54 from the scorer
window alone (§1).

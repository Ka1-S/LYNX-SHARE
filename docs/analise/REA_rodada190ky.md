# REA round 190ky — the Waves DSP binaries, second pass (owner's permission of 2026-11-17)

Scope: the brief's Waves list in order of use in real projects — CLA-76, CLA-2A, L2, RVerb, H-Delay, PuigTec — on the
path round 190kt opened for SSLComp (`REA_rodada190kt.md` §1: the per-plugin DSP DLLs of the V17 bundles are small and
not wrapped). Tools: the rea MCP (Ghidra 12.1.4 headless, `decompile_all` into `/mnt/e/lynx-work/waves190ky/ev/`), the
bundles' `ParamXML`, and black-box renders in Waves demo mode on refcap030's fxmeasure host (hidden, no editor, no input
sent, the trial lock only). Clean-room as in 190kt: what is recorded here is structure, behaviour and parameter meaning;
LYNX's code is written from this description, and every constant LYNX uses is fitted on our own renders. The vendor's
tables (CLA-76's transfer tables and its time-constant / weight tables) were located, not read out and not used. No
licence check was touched; the demo's periodic mutes are masked out of our measurements.

## 1. What was read

| plugin | binary | functions | read | result |
|---|---|---|---|---|
| CLA-76 | `GenericMultiCoreProcessor.dll` (`CLA76Proc.A`), control `CLA-76.dll` (2 949 functions, coefficient setters) | §2 | structure complete | Crest's FET law (§3), CLA-76 on it |
| CLA-2A | `GenericMultiCoreProcessor.dll` (`LA2AProc.A`, 643 lines) | §5 | structure | not refit this round |
| L2 | `GenericMultiCoreProcessor.dll` (`L2IDRproc.A`, 1 668 lines) | §6 | structure | not refit this round |
| RVerb, H-Delay, PuigTec | bundles copied (`bin/`) | — | not opened | time |

## 2. Waves CLA-76 — the processing graph [DECOMP]

Sources: `ev/cla76_gmcp` (round 190kt, `CLA76Proc.A` = `functions/0x180001118.c`), `ev/cla76_dll/functions/0x18002c3c0.c`
(attack / release / ratio → coefficients), `0x180030a10.c` (the setters' order), `0x180030440.c` (mix / trim), the
bundle's ParamXML (106 parameters).

Per sample (the mono and the stereo-linked paths share it; stereo is linked — confirmed black-box, §4):
1. Parameter smoothing of the input and output gains (one-pole towards the target).
2. Analog extras (the Analog switch: hum and LCG noise through a colouring pair; off = 2 in the state).
3. Input gain, then two biquads (the input stage's tone), then the **FET gain** (a per-sample linear gain read from a
   table indexed by the detector's control voltage, linear interpolation; above a small negative voltage the table's
   first entry; under its bottom the gain 1).
4. The gained signal through two filtered paths and a rational soft clipper `c·x / (|x| + c)` (the amplifier), then an
   asymmetric saturator (different constants for the two half-waves, smoothed) — the output signal of the wet path.
5. **Side chain from that output (feedback)**: a biquad, a gain per ratio setting, full-wave rectified.
6. **Three parallel peak followers** on the rectified level (each: attack one-pole when the level is above it,
   multiplicative release otherwise), each with its own scale (weight); their sum plus a bias is the control voltage.
7. Output: wet × output gain blended with the dry input by the mix, trim on both (mix / trim setter).

Control DLL (`0x18002c3c0.c`): the attack and release knobs (1 … 7, fractional) interpolate per-revision tables:
three attack times, three release times and two weights (follower 1: w·v, follower 2: (1 − w)·v, follower 3: 1 − v,
v and w set by the attack knob) — so the attack knob also moves the followers' balance, and the release is
programme-dependent through three time constants. The ratio buttons select a transfer table and a side-chain gain
per revision (four buttons and "all"); `PARAM_OFF` (index 4) disables the reduction. State indices: 0 / 1 Input /
Output (dB magnitudes), 2 / 3 Attack / Release, 4 off, 56 revision (0 Bluey, 1 Blacky), 59 ratio (0 = 20, 1 = 12,
2 = 8, 3 = 4, 4 = all), 103 mix %, 104 trim dB, 105 auto make-up.

## 3. LYNX's FET law and its fits [EXEC: `lab/fit76s.py`, `lab/fit76t.py`, `lab/fit76io.py`, `lab/gen76.py`]

Crest Compressor, hidden `fetLaw` (DynamicsSuite.h): the detector hears the input level × the gain applied now
(feedback); three peak followers summed with weights into a control level L (dB); the gain (dB)
`−S1·W1·softplus((L − T1)/W1) − S2·W2·softplus((L − T2)/W2)`, floored at −range. A voicing (two revisions × five ratio
settings) carries the second term, the followers' multiples and weights; the knobs carry T1 (threshold), S1 (ratio − 1),
W1 (knee) and the fast follower's attack / release. LYNX's analytic law replaces the vendor's tables.

Probes (Waves demo mode, refcap030's host, the trial lock only; `cap/cla76*.txt`, 238 renders in four runs of 78 … 449 s — the first two superseded by the lead-in runs, §4;
mutes masked): 1 kHz stairs −50 … 0 dBFS at the five ratio settings × Input −36 / −24 / −12, the Input sweep (15
positions) and the Output sweep (11) at 20:1, pink-noise stairs, level steps −40 → −10 → −40 dBFS and a burst / long-step
release probe at every attack and release knob (4:1, Input −18), the same at all buttons (knobs 1 / 4 / 7), Bluey's
statics, a stereo-link probe, 60 Hz / 200 Hz / 5 kHz stairs, and refcap182's five settings on the probe stimuli.

| fit | data | result |
|---|---|---|
| steady law per voicing (`fit76s.py`: T1, W1, S1, T2, W2, S2; three starts, bounded) | stairs, 26 steps × 3 inputs (20:1 also the Input sweep) | rms 0.22 / 0.26 / 0.29 / 0.31 / 0.36 dB (Blacky 20 / 12 / 8 / 4 / all), 0.05 / 0.08 / 0.17 / 0.19 / 0.16 dB (Bluey) |
| Input / Output gains (`fit76io.py`) | the sweeps' small-signal plateau | tables (`io76.json`; §4) |
| timing at 20 / 12 / 8 (`fit76r.py`, per knob) | steps, release probes | two followers: slow 0.88 (attack 11.9 → 5.5 ms over knob 1 → 7, release 15.4 → 1.9 s), fast 0.12 (attack 0.03 × the slow one's, release 1 / 31 of the slow one's); 0.4 … 0.8 dB rms per probe |
| timing at 4:1 (`fit76j.py` → `fit76p.py`) | all 4:1 probes jointly, then refined on refcap182's two 4:1 settings (D, X1) | slow attack 29.7 → 0.66 ms, fast release 2.08 → 0.26 s (slow 18.6 → 2.4 s), fast weight 0.054; 1.17 dB rms on the probes, then 0.31 dB rms per 10 ms on the programme (from 0.77) |
| timing at all buttons (`fit76jA.py`) | the all-buttons probes | slow attack 5.3 → 0.16 ms, fast release 3.7 → 0.07 s, fast weight 0.09; 1.69 dB rms (knob 1 worst, knob 7 0.3 … 0.9) |
| wet-path tone (`tone76.py`, `fittone.py`) | third-octave balance of refcap182's 20 CLA-76 programme renders | low shelf −0.42 dB at 131 Hz, high shelf −0.82 dB at 16.9 kHz (−0.35 dB at 60 Hz; the probes' low-level response −0.37 dB) |

The voicings scale the timing by their slope (a law of this form moves the gain S1 dB per dB of the control level, while
the reference's recovery speed hardly changes with the ratio button): releases × S1 / S_ref, attacks × (1 + S1) /
(1 + S_ref), the fast follower's weight so its instant share of a recovery stays the same (`lab/gen76.py` writes every
table). Note on fit76p: it uses two of refcap182's five CLA-76 settings, which are also scored; the other three settings
(8:1, 12:1 Bluey, all buttons) rest on the probes only.

## 4. Black-box findings that change the translation [EXEC: `cap/`]

- **Host reset ramps.** After `reset` the plug-in's smoothed Input / Output gains start away from their targets and
  ramp (≈ 20 dB/s): renders that begin with signal show it for up to 1.5 s. Every probe of this round has a 1.5 s
  silent lead (`cap/stim/l*.wav`); round 190kt's SSLComp stimuli started at once (SSLComp has no such ramp at its
  defaults). refcap182's programme renders start after `reset` + 0.5 s settle, so their first second carries the ramp
  for settings far from the defaults (not modelled: a host artefact).
- **The Output knob is not dB-linear**: from −30 dB on its scale, −24 is +9.7 dB, −18 +17.1, −6 +28.6, 0 +29.5,
  −36 −14.7, −48 −29.7 (`lab/io76.json`). The Input knob is close to its scale (±1.5 dB). The old translation took both
  dB for dB.
- **Stereo is linked**: a loud left channel reduces the right one by the same amount (`d76_L_r4`).
- The ratio buttons' steady slopes over the knee (1 kHz, Blacky): about 13 / 9.6 / 6.6 / 4.7 : 1 for 20 / 12 / 8 / 4;
  "all" adds +1.3 dB of small-signal gain and a hard knee (≈ 12 : 1). Bluey: +3.2 dB of small-signal gain, knees 2 … 7 dB
  lower.

## 5. Waves CLA-2A — structure [DECOMP: `ev/cla2a_gmcp/functions/0x1800011a4.c`]

`LA2AProc.A` is a circuit-level model, much larger than CLA-76's: the input through a pre-emphasis pair, the side chain's
high-frequency emphasis (the Hi Freq control) and a rectifier feeding an electro-optical cell solved per sample (a
charge / discharge pair of nonlinear functions with their own state and table-driven transfer — `FUN_180001930` /
`FUN_180001980`), two dynamic stages whose gain follows `log10` of their own envelope (tube-like level-dependent
gain), an output transformer stage, and a limit / compress switch. Reproducing it needs a model of the cell's two-speed
memory; LYNX's Glow Leveler already has a measured optical law (rounds 152 / 165w / 165x / 190fx). Not refit this
round: refcap182 CLA-2A → Glow Leveler 5.57 (30 files) stays.

## 6. Waves L2 — structure [DECOMP: `ev/l2_gmcp/functions/0x18000112c.c`]

`L2IDRproc.A`: per sample, the gain a brickwall needs (`ceiling / |x|` above the ceiling), the minimum over the
look-ahead window through a log2 tree of running minima, a release follower with two rates (attack / release
selected by direction) whose release coefficient is read from two tables indexed by the depth (ARC's
programme-dependent release), then the gain smoothed by cascaded moving sums over the window (integer accumulators:
a triangular / CIC-shaped smoothing of the gain curve), applied to the delayed audio; the IDR quantizer adds TPDF or
shaped dither (an LCG, three noise-shaping modes) when Quantize is on. LYNX's Limiter has the same look-ahead brickwall
structure; its refcap182 score is 7.32 (25 files). Not refit this round.

## 7. Not done

RVerb, H-Delay, PuigTec (bundles copied, not opened), Valhalla FutureVerb, Slate / Eiosis: time. The CLA-76's
amplifier / output saturation stage (§2.4) is not modelled (its harmonics stay below −60 dB at the probes' levels);
the analog noise / hum, auto make-up and the host reset ramps neither.

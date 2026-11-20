# REA round 190nb — Kilohearts parity on the licensed kHs Essentials (0.1.30)

Lab, captures and logs: `/mnt/e/lynx-work/khs190nb/` (HANDOFF.md). Branch `khs190nb` off `wip/rollup030`.
Evidence classes: [STATIC] read in the binary (Ghidra, no execution), [MEASURED] black-box renders of the plug-in
through its public parameter API, [DATA] the plug-in's own saved state. Clean-room: LYNX's code restates the laws as
formulas; no Kilohearts code, table or preset is copied (the Phase Distortion's 90° pair is LYNX's own design).

## 1. Licence state (2026-11-19, after the owner's reinstall 17:16–17:20)

Kilohearts' own installer replaced Slate's iLok builds: every `.vst3` in `C:\Program Files\Common Files\VST3\Kilohearts\`
is a 247 KB loader of the shared engine `C:\ProgramData\Kilohearts\HeartCore.core_64` (76.6 MB, v2.4.5 / 2.4.6).
Alive test (hidden host, the stimulus at the default and with two controls moved; `alive/alive.txt`):

| plug-in | processes audio | note |
|---|---|---|
| the 35 kHs Essentials snapins | yes | every control acts |
| Disperser, Faturator | **no** (bit-exact dry, Faturator only delayed by its 38-sample latency) | paid, not licensed |
| Multipass, Snap Heap | **no** (bit-exact dry with lane gains −∞ / +10 dB, mute) | paid, not licensed |

No licence dialog appeared (the host stops itself and clicks nothing if one does). Disperser and Faturator stay on
round 190ln's laws; their references remain unavailable.

## 2. Captures and harness

- 1 650 reference renders of the 35 snapins (`ref/<tag>/`): the default on the stimulus, its kind's signals and three
  programme files (refcap182 drums / vocal / mix), and every control alone (five values, every step of a switch) on
  its kind's signals, each setting's component state saved beside it (round 190ln's `gensweep.py`, renders on).
- Song position: fxmeasure's play head runs on through every settle and render of a script, so a synced snapin saw a
  different bar phase in each render; `lab/songpos.py` replays the scripts and gives each render its position, which
  the scorer passes to fxrender (`--songpos`). Same harness for before and after.
- Scorer: round 190lg's time-window fxparity (copied to `scorer/`, as round 190ln).

## 3. The five snapins without a stand-in — Effector's hidden Kilohearts laws [STATIC + MEASURED]

HeartCore's RTTI names every snapin's DSP class (`ringmod::RingModDsp` …); virtual slot 14 is the block process,
slot 15 the reset. Read statically:

- **Resonator** (slot 14 = 0x180743390): f = 440 · 2^((pitch − 69) / 12), doubled for the odd series; one feedback
  comb whose total delay is one period 1 / (c f) (a 4-point Catmull-Rom read of the delay line), feedback
  g = 2^(−P / decay), negated for the odd series; the wet blends the comb's tap r and its input w = x + g r as
  g (intensity − 1) w + r (intensity 0 = a Schroeder all-pass, 1 = the bare resonance), its sign inverted for the
  saw series; out = (1 − mix) x + mix · wet. Built as is: **9.93 / 10** on its 27 files.
- **Ring Mod** (0x1807680b0, its oscillator 0x1807671f0, the modulator shaping 0x180768730): an internal sine at the
  frequency, the right / left phases ∓ spread / 4 of a turn; the rectifier = gains min (1, 1 + 2 r) on the positive
  half and min (1, 1 − 2 r) on the negative (anti-aliased in the plug-in by the antiderivative method);
  y = x · (1 + mix (bias − 1 + f (m))); everything at 2× oversampling. [MEASURED] the oscillator leads the input by
  3.5 samples (half the oversampler's 7-sample latency) and sleeps on silence (from a reset until the first block with
  sound, and from the second all-zero block on: the phase jump after a 24 000-sample gap fits within 2°).
- **Reverser** (0x18075c6d0): sections of L samples (the time, or steps × note at the tempo, the note table
  4 / 4t / 8 / 8t / 16 / 16t / 32 = 1, 2/3, 1/2, 1/3, 1/4, 1/6, 1/8 beat); two heads read the input backwards at
  twice the write speed from each section's start (new head, and the old one going on into the section before);
  equal-power fades over the crossfade's share (new sin θ, old cos θ, θ = π/2 · min (1, phase / crossfade));
  out = x + mix (wet − x). Synced: the sections follow the song position. [MEASURED] the heads read two samples
  behind the write, synced sections start two samples after the grid, and after a reset the Reverser waits for the
  first sample above 2^-20 (its unsynced sections start there): 26 of 34 files then null at −71 … −81 dB.
- **Phase Distortion** (0x18058fac0): an analytic pair (re, im) from two all-pass cascades, the modulator
  m = LP_tone (g · re) with g = 3 + normalize (1 / |re + j im| − 3) and a cookbook low pass (Q 0.707) at the tone,
  φ = 4π drive² m + bias · 2π ∓ spread · π, y = cos φ · re − sin φ · im, out = x + mix (y − x), oversampled. LYNX
  uses its own minimax 90° pair (4 + 4 sections, 2.6° worst over 15 Hz … 0.46 fs) at the base rate: the plug-in's own
  cascades differ in phase, so the ESR stays near 0 dB (spectra within 0.5 … 2 dB).
- **Shaper** (0x1807782a0): x · drive folded by the loop mode (hold / mirror / repeat), through the curve
  (model.curve.data.segments x / y [DATA]; LYNX joins the points with straight lines, the segments' tension is not
  modelled), the DC filter, out = x + mix (y − x), at 2× oversampling. [MEASURED on the identity curve] the
  oversampler ≈ 3.5 samples of delay with −0.6 dB at 10 kHz / −2 dB at 20 kHz (LYNX: 3-tap droop + half-sample
  Thiran all-pass); the DC filter ≈ a 30 Hz Butterworth high pass.

Hosting: FL's Effector (core/Effector.h) gets hidden parameters [12] law (k / 8) and [13 …] the law's values
(core/KiloheartsDsp.h); 0 = FL's Effector, so every project saved before keeps law 0 bit for bit. The visible Type
names the nearest Effector algorithm (Ring, Stutter, Vowel, Distort), Wet is the snapin's mix. The Reverser's and
Resonator's buffers are allocated only for a slot created with a law (makeEffect (slot)).

## 4. The earlier stand-ins: measured maps (translator revision 1)

| plug-in | finding | change |
|---|---|---|
| kHs Delay, Dual Delay | [MEASURED] linear mix (impulse at 25 / 50 / 75 %: dry 0.75 / 0.5 / 0.25, first echo 0.25 / 0.5 / 0.75); every echo one sample after its time (9 601 samples at 200 ms) | Echoform: Mix gives the dry 1 − m (cos law), Output brings the echoes to m (it scales the echoes only); times through Echoform's own time law + 1 / 48 ms |
| kHs Reverb | [MEASURED] equal-power mix (25 %: dry 0.924 = cos 22.5°) | Reeverb 2's Dry / Wet / Early through FL's level law (gain = (e^(2.398 x) − 1) / 10) instead of its percent texts (75 % read −6.2 dB) |
| kHs Formant Filter | [STATIC] VocalFilterDsp: two cookbook biquads in a row — a peak of 20·log10 Q dB at the lower cutoff (high pass with Lows off) and at the higher one (low pass with Highs off), both at Q; the shared designer (0x180bac490) is the RBJ cookbook (cases LP, BP, HP, notch, low shelf, peak, high shelf, all-pass) | the two peaks / cuts as read, no −6 dB trim |
| kHs Flanger | [STATIC] FlangerDsp: dry cos / wet sin of mix × 45° | Fruity Flanger Dry / Wet from that law, its 50 % default cross-feed off |
| kHs Transient Shaper | [MEASURED] Sustain is a level-independent gain on the steady part: 17.93 dB × sustain on every step of the stairs probe (−62 … −12 dBFS) | Strike's Output carries it (−20 … +6 dB), its Sustain stays 0 |

## 5. Scores (fxparity time window; files up / same / down per plug-in in the round's HANDOFF.md)

Before = round 190ln's stand-ins on the rollup base (bypass for the five without one), after = this round; bypass =
the plug-in's input scored against its render. 1 648 files (the 35 licensed snapins), the same scorer and song
positions for every column. Unchanged maps score exactly as before.

| plug-in | files | bypass | before | after | up / same / down |
|---|---|---|---|---|---|
| kHs 3 Band EQ | 30 | 6.80 | 8.57 | 8.57 | 0 / 30 / 0 |
| kHs Bitcrush | 40 | 3.35 | 3.27 | 3.27 | 0 / 40 / 0 |
| kHs Channel Mixer | 25 | 5.72 | 10.00 | 10.00 | 0 / 25 / 0 |
| kHs Chorus | 60 | 2.82 | 3.90 | 3.90 | 0 / 60 / 0 |
| kHs Clipper | 25 | 5.48 | 5.96 | 5.96 | 0 / 25 / 0 |
| kHs Comb Filter | 19 | 2.95 | 3.47 | 3.47 | 0 / 19 / 0 |
| kHs Compactor | 82 | 7.01 | 7.12 | 7.12 | 0 / 82 / 0 |
| kHs Compressor | 62 | 6.65 | 7.48 | 7.48 | 0 / 62 / 0 |
| kHs Delay | 98 | 2.57 | 2.95 | 6.23 | 87 / 7 / 4 |
| kHs Distortion | 72 | 2.75 | 3.10 | 3.10 | 0 / 72 / 0 |
| kHs Dual Delay | 114 | 2.93 | 3.25 | 3.30 | 10 / 98 / 6 |
| kHs Dynamics | 106 | 7.60 | 8.59 | 8.59 | 0 / 106 / 0 |
| kHs Ensemble | 52 | 4.08 | 3.58 | 3.58 | 0 / 52 / 0 |
| kHs Filter | 33 | 2.18 | 7.39 | 7.39 | 0 / 33 / 0 |
| kHs Flanger | 90 | 3.92 | 2.14 | 4.32 | 86 / 3 / 1 |
| kHs Formant Filter | 24 | 2.75 | 1.29 | 4.96 | 24 / 0 / 0 |
| kHs Frequency Shifter | 10 | 3.20 | 3.30 | 3.30 | 0 / 10 / 0 |
| kHs Gain | 12 | 7.00 | 8.33 | 8.33 | 0 / 12 / 0 |
| kHs Gate | 73 | 7.60 | 8.01 | 8.01 | 0 / 73 / 0 |
| kHs Haas | 12 | 4.00 | 9.00 | 9.00 | 0 / 12 / 0 |
| kHs Ladder Filter | 54 | 1.37 | 8.52 | 8.52 | 0 / 54 / 0 |
| kHs Limiter | 46 | 5.74 | 6.78 | 6.78 | 0 / 46 / 0 |
| kHs Nonlinear Filter | 56 | 2.18 | 4.62 | 4.62 | 0 / 56 / 0 |
| kHs Phase Distortion | 35 | 3.60 | 3.60 (bypass) | 4.11 | 15 / 18 / 2 |
| kHs Phaser | 70 | 3.11 | 3.61 | 3.61 | 0 / 70 / 0 |
| kHs Pitch Shifter | 30 | 8.17 | 8.13 | 8.13 | 0 / 30 / 0 |
| kHs Resonator | 27 | 3.33 | 3.33 (bypass) | 9.93 | 26 / 1 / 0 |
| kHs Reverb | 66 | 4.21 | 1.26 | 4.26 | 64 / 2 / 0 |
| kHs Reverser | 34 | 1.85 | 1.85 (bypass) | 7.44 | 32 / 1 / 1 |
| kHs Ring Mod | 30 | 2.20 | 2.20 (bypass) | 4.43 | 27 / 3 / 0 |
| kHs Shaper | 20 | 4.85 | 4.85 (bypass) | 6.85 | 15 / 5 / 0 |
| kHs Stereo | 20 | 5.60 | 7.75 | 7.75 | 0 / 20 / 0 |
| kHs Tape Stop | 21 | 9.52 | 9.71 | 9.71 | 0 / 21 / 0 |
| kHs Trance Gate | 50 | 1.48 | 3.94 | 3.94 | 0 / 50 / 0 |
| kHs Transient Shaper | 50 | 6.98 | 7.02 | 7.32 | 5 / 45 / 0 |
| **all** | 1648 | | 5.08 | 5.88 | 391 / 1243 / 14 |

The 14 files down: Delay 4 (feedback 100 %, bounce, ducking 75 / 100 %: Echoform's own feedback / ducking under the
new output trim), Dual Delay 6 (ESR better on every one, −0.7 → −9 … −11 dB, but the score's spectrum / level terms
step down), Flanger 1 (feedback 100 %, self-oscillation), Phase Distortion 2 (spread 75 / 100 %: the 90° pair's phase),
Reverser 1 (ms mode after a 9 s silence: the plug-in's sleep restarts its grid; not modelled).

CPU (35 s stereo 48 kHz, one pinned core, best of 5, user + sys incl. file I/O; `cpu/bench.log`): Effector law 0
unchanged (0.13 … 0.17 s, ±0.01 s noise); the laws 0.14 (Resonator) … 0.28 s (Phase Distortion) = 0.4 … 0.8 % of a core.
Gold: 153 renders (Facet through the Pro-Q translator, the 22 stand-ins' defaults, Effector's 12 types × 2 settings,
44.1 / 48 / 96 kHz), base vs round binary: 0 differ (`gold/gold.log`).

## 6. Not done / next

- Distortion (3.1), Bitcrush (3.3), Frequency Shifter (3.3), Comb Filter (3.5), Ensemble (3.6, below bypass 4.1),
  Phaser (3.6), Chorus (3.9), Trance Gate (3.9), Nonlinear Filter (4.6): their DSP is readable the same way (slot 14
  addresses in `rea/slot14.txt`, decompiles in `rea/out/laws.c`); each needs its own law (the stand-ins' structures
  differ: 3-voice chorus, 6-voice unison, typed distortion shapes, the phaser's cutoff).
- Phase Distortion / Shaper: the plug-in's own all-pass / oversampler phase (ESR); the Shaper's curve tension.
- Disperser, Faturator, Multipass, Snap Heap: unlicensed here (dry); Multipass / Snap Heap state is the same JSON
  model (lanes of snapins), mappable once a licence allows references.

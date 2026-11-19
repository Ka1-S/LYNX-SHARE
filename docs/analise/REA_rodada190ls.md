# REA round 190ls (0.1.30): Valhalla → LYNX stand-ins (ValhallaRoom → Atrium's dense late tank)

Owner's permission of 2026-11-17: REA on third-party plug-in binaries for parity, clean-room only. Static reads of the
official binaries (no decryption, no dumps, no hooks, nothing near licence code); nothing of the references is copied — no
code, no tables, no presets. LYNX's code is its own; every number in it is LYNX's fit on the references' renders.
Lab: /mnt/e/lynx-work/valhalla190ls (lab/, runs/, gold/, cpu/, mini/). Decompiler working files of round 190ln
(/mnt/e/lynx-work/rea3_190ln/vr/rea, Ghidra project `Room`) were re-read; no new Ghidra session was needed for Room.
Score: round 190lg's time-based alignment window (/mnt/e/lynx-work/rea3_190ln/scorer = origin/rea2_190lg's fxparity /
flref_compare), the same copy for every before and after. Bank runner: lab/bank.sh → lab/rc030q.py (rea3_190ln's rc030.py
with the quiet-window wait per item).

## 1. ValhallaRoom: the late network [STATIC]

Round 190ln found the late network's size (2 × 16 lines) and the RT60 law of its line gains. Read again this round
(`lr_proc.c`, the per-type process function at 0x1802888f0, and the gain update at 0x180266220):

- **Blocks of 64 samples.** Every line is read and written 64 samples at a time; the modulators are evaluated once per block
  (eight sines, each stored with its negation — sixteen offsets — and two more whose (½ ± v) pairs crossfade two inner taps).
- **Eight groups of four lines.** In each group two lines are read at a modulated fractional delay (base + one modulator,
  + v for one and − v for the other) and two at a fixed whole-sample delay: half the 32 lines move, in opposite pairs.
- **Per line, two one-pole crossovers** (corners at RTXover and RTHighXover): the line's output is split into low / mid /
  high and each part scaled by its RT60 gain 10^(−3 L / (fs·T)) for T = RTBassMultiply × Decay, Decay, RTHighMultiply ×
  Decay (190ln).
- An input chain of seven delay-and-mix stages per channel before the network (block-delay + a 4-point mixing step each).

What is LYNX's own: the line lengths (a near-geometric set of 32 ratios, 0.55 … 1.6 × the type's mean length), the
mixing (a 16-point Hadamard inside each half, the halves rotated into each other by Late cross × 45°), the loop filters
(LYNX's six-shelf per-band design, law 1). The modulation layout (two of every four lines moving, in opposite directions) is
the reference's structure, written as LYNX's own code on the 8-line tank's modulators.

## 2. Atrium's dense late tank (`Settings::dense`, `verbs::atrium::reaLaw`) [built]

Engine (core/LynxVerbs.cpp, law 1 only; an engine made with `dense = true` — Atrium's — allocates it):
- 32 lines in one frame-major buffer (`DenseBank`: one write position; the 32 writes of a sample are one contiguous store),
  lengths `denseRatio[j]` × the geometric mean of the type's 8-line set × Late size, whole samples, gliding 0.25 sample per
  sample when Late size moves; lines alternate between the two halves.
- Loop filter per line: law 1's six first-order shelves designed from the per-band loss of that line's length (the same
  `designLine` the 8-line tank uses, refactored out of `designLoop`, bit-identical), then the loop's band-limit / low-cut
  one-poles. No loop all-passes (the 8-line tank needs them for density; 32 lines don't).
- Mixing: a 16-point Hadamard per half (four butterfly stages, × ¼ folded into the cross rotation), the halves rotated into
  each other by Late cross × 45° (the matrix modulator swings it). Input: ±¼ of each channel on every line of its half;
  revision 1's four build groups on lines j & 3 (±½).
- Modulation: the reference's layout — in each group of four lines two move (modulator j / 4 of the 8-line tank's eight,
  one each way), two are read at whole samples.
- Output: two Walsh rows (22, 13) over all 32 lines, read at 1 / √8: the tank's stored energy does not depend on its size,
  so 1 / √32 left the late field 6 dB under the 8-line tank's (measured: −2.5 dB on the whole impulse with the early part);
  at 1 / √8 every front-end level holds.

Front-end (Atrium, `applyRoomLaw`, read with the engine law and revision 1 and `reaLaw` 1):
- `s.dense = 1`; the late lines' modulation depth × 4 (`roomModScale`): the reference's steady 1 kHz tone (refcap030's
  staircase, 28 stim renders of the type sweep and sr48 settings, `lab/tv.py`) swings by 2.03 dB (sd of 10 ms levels inside a
  step) and 1.86 dB from step to step; revision 1's 8-line tank swung 1.19 / 1.57 dB and sat 2.0 dB further under the input
  on average. The dense tank at × 4 (half its lines moving): 2.26 / 1.39 dB. (Every line moving at × 3 … 4 gave 2.04 … 2.09 dB
  and 0.6 dB more level on the tone for 4 % more instructions; the reference's layout was kept.)
- `roomTrim` per type (level + tone corrector per octave band) on top of revision 1's `atriumTrim`: two closed-loop passes of
  `lab/roomtrim.py` on the type sweep's impulses (third octaves around each band, 3-point smoothed; the residue after pass 2
  within ±1 dB). Large Room — the type of the pairs, sr and every gate bank's capture — 0.65 dB under the sweep's level and
  half the sweep's 63 Hz lift (+2 dB → +1 dB): at the full lift the music / bass renders of tp172d, refcap182 and verb183b ran
  0.5 … 1.7 dB over the reference (four files a step lower), at no lift the type sweep lost 0.1 (see §3). Dense Room (no
  capture) neutral.

Not built: the reference's block processing (64-sample blocks; LYNX's tank runs per sample), its input chain of seven
delay-and-mix stages (Atrium keeps its input all-passes, build-up taps and onset network), its per-line two-crossover
three-band gains (law 1's six shelves meet the same three-band law and the per-octave voicing).

## 3. Scores, gates [EXEC]

Base: origin/rea3_190ln (fxrender `bin/fxr_base` = rea3_190ln's merged build); after: this branch (`bin/fxr_n2`). JSON:
runs/b0 (base), runs/n2 (product), runs2/b0 / runs2/n2 (gate banks); the fit variants in runs/h* (lab/trim_*.txt).

| reference → LYNX | files | before → after | up / same / down |
|---|---|---|---|
| refcap030 ValhallaRoom mode (type sweep) | 48 | 3.31 → 3.58 | 14 / 30 / 4 |
| refcap030 ValhallaRoom pairs | 36 | 4.22 → 4.25 | 1 / 34 / 1 |
| refcap030 ValhallaRoom sr48 | 16 | 3.44 → 3.56 | 2 / 14 / 0 |
| refcap030 ValhallaRoom sr96 | 16 | 3.38 → 3.69 | 3 / 13 / 0 |
| **refcap030 ValhallaRoom** | 116 | **3.62 → 3.80** | 20 / 91 / 5 |
| tp172d (Room) | 12 | 3.58 → 3.67 | 1 / 11 / 0 |
| verb183b captures (Room) | 17 | 3.88 → 3.88 | 0 / 17 / 0 |
| refcap182 programme (Room) | 20 | 3.70 → 3.80 | 2 / 18 / 0 |

Downs, with evidence (one step each):
- mode M_type_0.354 / 0.521 / 0.562 / 0.729, `stim`: the file's level is the 1 kHz staircase, a steady tone through a
  time-varying tank — its level is the draw of the modulation at that frequency (the reference's own swings by 1.9 dB from
  step to step at one setting). Spectrum error 2.35 / 2.37 / 2.25 / 2.40 → 2.28 / 1.91 / 1.86 / 2.80 dB; rms −0.58 / −2.75 /
  −2.21 / −0.35 → −2.50 / −3.62 / −3.08 / −0.22 dB. Across the 32 staircase files the mean moved up (2.75 → 3.17 on the mode
  block's 24, 2.0 → 2.5 / 2.25 → 3.5 on sr48 / sr96).
- pairs PT_predelay0.2_decay0.8 `mix`: level onto the reference (rms +0.69 → −0.05 dB), the spectrum error over the 2.5 dB
  step by 0.02 (2.14 → 2.52 dB).

Fit variants (all on the dense tank, refcap030 Room 116 files / gate banks 49 files, base 3.62 / tp172d 3.58, cap 3.88,
prog182 3.70):

| variant | refcap030 | tp172d / cap / prog182 | note |
|---|---|---|---|
| 8-line tank, modulation × 8 | 3.81 (100 files, no sr96) | — | the staircase only |
| every line moving × 4, sweep trims (A) | 3.79 | 3.42 / 3.88 / 3.75 | music 0.7 … 1.5 dB loud |
| reference layout × 4, A | 3.82 | 3.67 / 3.76 / 3.75 | 4 gate files down (music / bass) |
| … Large Room 63 Hz half (H) | 3.78 | 3.75 / 3.88 / 3.75 | D_bass down |
| **… Large Room 63 / 125 Hz half (I, shipped)** | **3.80** | **3.67 / 3.88 / 3.80** | no gate file down |
| … Large Room −1.1 dB (G) | pairs 4.08 | — | dropped |
| … no 63 Hz lift anywhere (F) | 3.75 | 3.75 / 3.88 / 3.75 | the type sweep lost 0.08 |

Gates:
- Gold: gold/gold.sh — 81 / 81 fxrender renders byte-identical (base binary vs the round's with `--set 24=0`: ValhallaRoom
  states through the translator, Atrium's own settings with law 0 / revision 0 / 1, every other LYNX reverb and the delay /
  modulation stand-ins at 44.1 / 48 / 96 kHz). gold/gold_base.txt == gold/gold_new.txt (42 fingerprints of revisions 0 and
  1 from the base commit's LynxVerbs / Effects objects and the round's; in tests/Parity190lsTests.inc).
- CPU (cpu/bench.cpp, cpu/instr.log; Atrium at 48 kHz, 4 settings × 5 runs × 20 s, perf instruction counts): base 79.97 G,
  the round with the law off 80.21 G (+0.3 %: the dense branch test), dense tank 118.7 G (+48 %). The first version (one
  buffer per line, every line Hermite-read, loop-indexed Hadamard) was 170.6 G; the reference's half-moving layout, whole-
  sample reads for the still lines, one frame-major buffer and a vectorisable Hadamard brought it to 118.7 G.
- Mini runner (mini/all.inc = rea3_190ln's 82 test files + Parity190lsTests): 545 439 checks, 0 failures.

## 4. The other Valhalla stand-ins: base scores and findings (not built this round)

refcap030 means (base, round 190lg's scorer): Supermassive mode 3.23 / pairs 5.39 / sr48 5.60 / sr96 5.70; Shimmer 3.65 /
3.17 / 3.75 / 3.69; Delay 3.95 / 3.94 / 4.00 / 4.06; FreqEcho sr48 4.06 / sr96 3.31; SpaceModulator 2.85 / 4.33 / 4.40 /
4.40; UberMod 5.62 / 3.97 / 3.90 / 3.85; VintageVerb 2.16 / 3.44 / 3.50 / 3.60 (runs/b0).

- **ValhallaDelay → Echoform, Feedback 100 %** (pairs PT_Feedback0.5_Diffusion0.2 / 0.5 / 0.8, score 1, rms +10 … +12 dB):
  the reference's Tape echoes at 100 % (value 0.5 = 200 % × v) still fall by ~1.4 dB per 300 ms pass, Echoform's measured
  tape stage (fitted at 70 %: +0.67 dB in its feedback path) grows by ~0.2 dB per pass. At 160 % both grow at the same
  2.3 dB per pass and at 40 % both fall alike — a level law of the loop between 70 and 160 % (a record-level limit) is
  missing. Needs a few probe renders at 80 … 120 % (Valhalla runs) before a law; not built.
- **ValhallaFreqEcho at 96 kHz** (3.31 vs 4.06 at 48 kHz): the impulse's echoes and levels match at both rates; the loss is
  on steady tones through the shifted loop (the 1 kHz tone of the nburst signal: +3.5 dB at 48 kHz, −3.8 dB at 96 kHz against
  the reference, which reads the same at both rates). The tone's level is the phase of the loop at that frequency; the
  reference's own loop delay at the shortest Delay differs with the rate (12.28 ms at 48 kHz, 10.71 ms at 96 kHz after the
  dry impulse; ours 11.18 / 11.64). Not built.
- **ValhallaVintageVerb → Epoch Verb, Concert Hall at Decay 0.51 s** (the ColorMode sweep, 48 files at 3 / 1): the impulse is
  2.0 dB under the reference (2.7 dB in the first 80 ms), the 1 kHz staircase 6.9 dB under. Revision 1's trims leave Concert
  Hall on its 0.1.27 voicing (other banks); a short-decay trim needs its own hidden step. More modulation does not move the
  staircase here (× 3 / × 6: the tone 6.7 / 5.4 dB under). Not built.
- Supermassive's low files are the sixteen networks on Nebula's classic engine (the cloud law covers six); Shimmer's pairs at
  Feedback 80 % run 4 … 4.6 dB of spectrum error (Halo's shimmer loop). Not reached.

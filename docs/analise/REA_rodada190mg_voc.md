# Round 190mg, sub-round voc — Waves Doubler2 → Gemini Doubler, Waves Tune Real-Time → LYNX Tune

Owner's permission of 2026-11-17 for REA of third-party binaries, for parity; clean-room: structure and laws only, no
vendor code, tables or presets copied. Work folder `/mnt/e/lynx-work/waves3_190mg/voc` (HANDOFF.md, `ev/` decompiles,
`cap/` probes, `fit/` tracks, `runs/` scores, `lab/` scripts). Scorer: `scripts/fxparity.py` on refcap182 (48 kHz).

## 1. Doubler2 (Gemini Doubler)

### 1.1 What the binaries are

[REA] `Doubler.bundle/Contents/Win64/Doubler.dll` holds the parameter handling (`cDoubler_ALG`: it turns the RealWorld
values into the DSP's coefficient block); the bundle's own `GenericMultiCoreProcessor.dll` is the DSP
(`DoublerProcM` / `DoublerProcS` / `DoublerProcX`, each in a plain and an AVX build). Decompiled to `ev/doubler`,
`ev/doubler_gmcp` (one Ghidra session each, closed).

ParamXML: RealWorld 16 / 17 are `DETECTOR1/2_SOURCE`, 18 `DETECTOR_RANGE`, 20 … 25 `START_SEARCH` (64 smp),
`COR_TC` (20), `SEARCH_LIMIT` (32 smp), `ENERGY_THRESH` (−60 dB), `ZXING_THRESH` (−40 dB), `COR_THRESH` (−20 dB).

### 1.2 The read head (structure and laws)

[REA] Per voice one read head at an integer lag plus a fraction, linear interpolation between neighbouring samples
(the delay line holds 16-bit samples, clipped at ±1). Each sample the fraction moves by (ratio − 1) (the LFO, a
triangle between detune ± |depth| with period fs / rate, moves the ratio). Bounds, in samples (not time), with
W = 256 · 4^Range, S = START_SEARCH, L = SEARCH_LIMIT:

- the delay is at least W + S (320 samples at the defaults; without Align);
- free band: delay − W … delay + W; hard bounds delay ± (W + S − L) = ± 288;
- a detector per voice runs on the voice's source: the running mean square E and the running autocorrelation R_k at
  20 candidate lags floor (W · 2^(k/20) + 0.5), k = 0 … 19 (256 … 495 at Range 0), each a one-pole average with
  coefficient exp (−1 / (fs · COR_TC ms));
- once the head is past the free band it jumps back towards the delay by the candidate lag with the largest R_k when
  E < dB (ENERGY) = 0.001, or when the source at the head is near zero (|x| < dB (ZX) = 0.01) and
  max R > dB (COR) · E = 0.1 · E; at the hard bound the jump is forced;
- at a jump the old head keeps reading and fades out linearly; [EXEC] fitted on probes with the shelves off
  (`cap/p3.txt`, `lab/xfit.py`): 256 samples at all four jumps.

### 1.3 The phase: why the waveform cannot be matched

[EXEC] Probes on the hidden host (`cap/p1.txt`, `p2.txt`; Waves demo mode, 1.5 s silent lead):

- the head is NOT reset by the host's reset (p2: six renders of one noise after reset + 0 … 2 s of silence: six
  different head positions); in silence it keeps drifting and jumps freely (E below the threshold);
- a state load re-initialises it (p1: the same setting loaded twice, renders 1 and 6: the same head track within 1
  sample);
- the two voices of refcap182's D and S2 settings (same voices, direct on / off) follow the same track to 0.15 samples.

So the process is deterministic, but each refcap182 file starts from the history of the files rendered before it in
that process (drums, vocal, bass, guitar, mix with 0.5 s of silence between): the head positions at a file's start
cannot be known by a fresh instance. The waveform null (ESR) stays closed; what can match is the head's statistics.

### 1.4 LYNX

Hidden `voicetools::gemini::spliceLaw` (= paramCount, saved only while on; the Waves Doubler translator sets it, Align
off only): the measured head of §1.2 (W, the 20 lags, the thresholds, 256-sample fade) on LYNX's own delay line. Off: the
round-190do head, bit for bit. Before (round 190do's head): one head that jumps back by a fixed 252 samples with a
2-sample crossfade, the two voices of a setting jumping together.

### 1.5 Result (refcap182, 25 files; `runs/base` = fxr_base, `runs/s2` = this branch)

Mean 4.48 → 4.64, up 2 (S2_bass 4 → 6, X1_guitar 4 → 6), same 23, down 0. Mean ESR −2.9 → −3.6 dB, spec 0.42 → 0.36 dB,
env 0.949 → 0.962 (bass: D 0.83 → 0.96, X1 0.85 → 0.89; guitar S2 0.87 → 0.95). The rest is the phase of §1.3:
most files sit at 4 on env 0.95 … 0.98 (the direct + voice beating at the heads' unknown start positions).
CPU (35 s mix, one core, best of 5): Doubler2 D 1.37 → 1.27 s, S1 1.37 → 1.28 s (whole fxrender run; the 20-lag
detector costs less than round 190do's extra crossfade reads).

## 2. Waves Tune Real-Time (LYNX Tune)

### 2.1 Binaries

[REA] The DSP (`Waves Tune Real-Time.bundle/.../GenericMultiCoreProcessor.dll`, `TuneRealTimeProc`) decompiles to
11 000-line functions (`ev/wtrt`); not read in this round's time box: black-box only. Resources: `XRng/1000` lists the
detector ranges; the states' `Range` = 6 is "All", 80 … 900 Hz (LYNX Tune's Alto / Tenor, 80 … 1000 Hz, is the
nearest; unchanged).

### 2.2 What the reference does (refcap182, black-box)

[EXEC] `lab/vsdry.py`, `drylag.py`, `bands.py`, `segbands.py`:

- its latency is ~35 samples (LYNX Tune: 1204); it is spectrally transparent on every signal (guitar, vocal, bass
  within ±0.4 dB per third octave of the input except +1.6 dB at 99 … 198 Hz on the guitar);
- the in-tune bass passes as the input itself: −20 … −36 dB from the delayed dry signal per second (gain 1.004);
  LYNX Tune re-synthesised it through its overlap-add (−7 dB from dry);
- on the guitar's strums LYNX added 3 … 10 dB at 30 … 200 Hz (+15 dB at 16 kHz): overlap-add grain noise under
  chords; the reference leaves them as they came;
- `PARAM_WET_DRY_CNTRL` = 80 in every state, and the vocal renders hold ~20 % of the dry signal; translating Mix 80 %
  alone scored 5.36 → 4.40 (LYNX's corrected signal is not phase-coherent with its dry path), so it is not translated.

Tried and rejected (scores in `runs/`): strict tracking (YIN 0.05: 5.12), low latency (4.80), mix 0.8 (4.40), a
complementary 150 Hz low split from the dry signal (4.40: the same coherence problem), linear 10 / 30 ms fades
(5.28 / 4.88), equal-power fade (5.16), 40 ms hold (5.68 with X2_mix 6 → 4).

### 2.3 LYNX

Hidden `lynxtune::wavesLaw` (= count; the Waves Tune translator sets it): while the applied correction stays under
1 cent (or the sound is unvoiced) for 80 ms the output fades (5 ms one-pole, half-way between a linear and an
equal-power fade) to the delayed input itself; a correction of 3 cents or more brings the corrected signal back at once.
Off: the old sound, bit for bit. LYNX Tune's own defaults (and 190li's tunelab cases) never set it.

### 2.4 Result (refcap182, 25 files; `runs/t9`)

Mean 5.36 → 5.72, up 6 (D_drums, X1_drums, X3_drums 6 → 7; D_guitar 4 → 6; D_vocal, X3_vocal 4 → 6), same 19,
down 0. Bass ESR −6.7 → −21.2 … −22.6 dB (D, X1, X3; the reference's own distance from dry), guitar spec 1.74 → 0.27
… 0.30 dB (D, X1, X3). CPU (35 s vocal): Waves Tune D 2.27 → 2.05 s, X2 2.46 → 2.17 s.

# REA round 190mj (0.1.30): Valhalla → LYNX stand-ins, part 4; Crest's TTM

Owner's permission of 2026-11-17: REA on third-party plug-ins for parity, clean-room only — no code, tables or presets of the
references copied; LYNX's laws are written in its own words and fitted with its own curves. Parts 1–2 (Valhalla) are black-box:
new probe renders of the official plug-ins through refcap030's capture host (trial + GUI locks, idle gate, licence dialogs
never answered), LYNX's own fits. Part 3 (Crest) is static REA of the Pro-C 3 binary plus existing captures (sub-round).
Lab: /mnt/e/lynx-work/parity190mj (Horizon: hz/, Crest: crest/). Score: round 190lg's time-window scorer, the same copy before
and after. Before: `bin/fxr_b0` = origin/parity190mb (ebbadbe4e).

## 1. ValhallaSpaceModulator → Orbit: the reference's own voices (hidden `orbit::voiceLaw`, #8) [EXEC, built]

Black-box only: the official plug-in in refcap030's capture host (fxmeasure, trial + GUI locks, idle gate, no dialog
answered), round 190mj's own probes; LYNX's own code and fits. Static strings / RTTI of the dll name only the processor
and editor classes (the DSP objects are not polymorphic), so the structure was read from the audio.

### Probes (`lab/smprobe*.py`, captures `smcap/`, host logs `refcap030/rep/ValhallaSpaceModulator__p190mj_*`)
- Click trains (a click every 60 ms, L then R): every mode at Rate 0.99 / 0.10 Hz, Depth 2 / 10 / 30 ms, Manual 1.28 ms,
  Feedback 0 / +50 % (`A … F`), 77 renders.
- Still probes (Rate value 0.5 = 0 Hz: every voice stands still, the plug-in is then time-invariant to 3e-5): every mode
  at Feedback 0 / ±50 / 100 %, a click every 0.5 s on L and on R (`Z*`); Up and Doubler at Depth 0.2 / 0.35 / 0.8 ×
  Feedback +50 / −50 / +25 / +90 % (`Y*`).
- The value texts of Rate / Depth / Manual / Feedback at 41 values (`…__p190mj_texts.log`).

### What the probes show (LYNX's words)
- **The sweep** is one phase that runs on from load: a host `reset` does not restart it. Fitted on the exact tap delays
  (from the all-pass head, below) of 13 s at 0.1 Hz: rms error 0.002 samples. Six probes of one host session, with the
  rate changed between them, line up with one integrated phase within 0.02 cycle (mod ½, the two taps being alike). Rate
  law 10 |2 v − 1|^(10/3) Hz (0.99213 Hz at 0.75, 0.10106 at 0.626, both within 1e-4 of the fits; Orbit's old 3.333
  exponent gives 0.9943). The first render after a load sits 0.28 s of sweep behind the later ones (a start-up ramp; not
  modelled).
- **Fractional delays** are first-order all-pass (Thiran) interpolations: head h0 = g η, h1 = g (1 − η²), then × (−η),
  with η = (1 − d) / (1 + d) of the fraction d — flat in level at every position (linear interpolation dulls the top
  as the tap moves). Delays: 0.42 samples + |Manual| + Depth × the voice's shape; Depth × 0.99954 (2.0010 / 9.9954 /
  29.764 ms measured at 2 / 10 / 29.78 ms).
- **Up**: two taps half a cycle apart sweeping Depth → 0 (falling delay), sin² window on the phase (gain 1 at mid),
  right side a quarter cycle ahead — Orbit's measured sweep already had this shape.
- **Up / Down**: Up on the left, the falling copy on the right (both centred at load), through an inverting low shelf
  (+3.4 dB at the lows, −9.6 dB at the top, pole 0.93; fitted −43 dB on the still response).
- **TZFlange ±**: two taps of 1/√2 on independent smooth random paths over 0.5 … 2.5 × Depth (5 … 25 ms at 10 ms),
  both at 1.75 × Depth at load (the through-zero point); the − mode inverts one (still: they cancel exactly).
- **TriFlange**: one triangle-swept tap (0.14 … 1.17 × Depth) that reaches *both* sides, half the power each, through two
  different all-passes (L→L and L→R energies 0.500 / 0.500; mirrored for R).
- **Ocho**: Up's barber pole with four taps a quarter cycle apart at 1/√2.
- **Doubler**: one tap, a random-turning triangle over 0.35 … 0.97 × Depth. **VariUp / VariUpDown**: one hard-wrapping
  falling ramp (the right side rising in VariUpDown). **Ensemble / Symphonic**: two taps of 1/√2 on random paths.
- **Feedback**: the still loop responses (fb ±50 %) carry a negative low-frequency tail on every echo (fitted at −18 dB
  by a 2-pole loop section), but the same plug-in in motion (pairs at Depth 1.19 ms, 1.8 Hz; the mode sweep) sounds like
  a plain positive loop, 0.7 |x|^0.891 — the still behaviour did not carry over and is not used.

### Built (LYNX's own code, `EchoSuite.cpp`, Orbit::processVoices)
Free-running sweep (prepare only), Thiran taps, the voices above, Tri's two decorrelating all-passes (±0.6), the plain
loop, and the reference's own value law floor (11 v) − 1 for the mode (Orbit adds 0.001 so its own stored values land
safely; the reference's saved values just under k / 11 — verb183b's 0.2727 / 0.5454 — are the mode *below*: its spM3
capture is Up / Down, spM6 is TriFlange, which the translator had played as Through-zero + / Octo).
Gated per mode: the new voices run for Through-zero ±, Ocho, VariUpDown, Ensemble, Symphonic; Up, Up / Down, Tri,
Doubler and VariUp keep round 190mb's voices under the law (Tri's two-sided voice brought the E probe's spectra within
0.5 dB on both sides but took verb183b's single-impulse capture spM6 3 → 2) (their new voices sounded no closer on the gate banks — for
these modes the files are decided by the sweep's start phase, which a render cannot share with a capture made after
minutes of earlier settings).

### Not done / next
- The start phase: the captures' sweep phase follows the host session (one integrated phase from load); a harness that
  replays each capture's session history would let waveform scores (ESR) count — documented, not built (it changes the
  bank harness, not LYNX).
- Up / Down's shelf: right at Feedback 0 (refcap030 S2 ir / stim / stereo 2 … 3 → 4) but the Feedback-50 % mode sweep
  and S2's mix file went down: masked.
- The still-loop's low tail, VariUp's and the Doubler's timing.


## 2. ValhallaFutureVerb → Horizon, part 2: echo modes, Room / Chamber over Size [EXEC, built] (round 190mj, part hz)

Black-box only: new probe renders of the official plug-in through refcap030's host (one host process per mode / setting,
`lab/mkscr.py`, list `refcap030/lists/p190mjhz.lst`, 42 processes / 84 renders, every one 3 … 4 s from load, so no demo hole;
captures + component states in `hz/fvcap/Valhalla/ValhallaFutureVerb/{echo,room,st}`). Stimuli are LYNX's own
(`hz/lab/genstim.py`): `ep` (impulses at 0.5 / 0.05 / 0.005 / −0.5, 250 ms tone bursts at 100 Hz / 1 / 4 / 10 kHz × −40 / −20 /
−6 dB, pink bursts at −20 / −6 dB, a sweep) and `rp` (impulse at 0.5, at 0.05, a pink burst). Nothing of the reference is
copied; every law below is LYNX's own, fitted to measured levels.

### Echo modes (the echo alone: Reverb level 0, Echo level 100 %, Feedback 0 / 40 %)
- **Digital**: an impulse at 0.5, 0.05 or 0.005 leaves the same small pulse (−29 … −31 dB over the window, a ~7-sample linear
  ramp to ~0.017 and back, then a slow small undershoot): the output does not depend on the impulse's height. Tones pass:
  100 Hz / 1 kHz at −40 … −6 dB within ~1.3 dB, 4 kHz −4 dB under 1 kHz, 10 kHz −20 dB. With Feedback 40 % the second pass is
  −80 dB (the smooth small pulse does not get through again), while −40 dB high tones grow (hunting noise). LYNX's law: a
  slew-limited coder in the loop (two-pole 6 kHz pre low-pass, step 0.002 per sample at 48 kHz growing to 0.1 over 1 ms
  while it stays at its limit, 10 kHz post low-pass, −3.5 dB). The 0.5 impulse is right; the height independence and the
  repeats' death are not reproduced (listed). Round 190mb's "+19 dB" on the impulse: +19.2 → +2.2 dB.
- **Reverse Up / Reverse Down / Sparkle**: clicks 3 … 7 dB over the reference where low bursts sit level and 4 … 10 kHz bursts
  1 … 2 dB over: the reference's grains carry less energy for a click and for incoherent high tones. LYNX: a four-head
  shifter (`Shifter::process4`, heads a quarter window apart, sum × ½) for those three. Detune, Reverse, Reverse Up/Down and
  Swarm lost files with four heads and keep two.
- **Analog**: kills 10 kHz entirely (−73 dB), 4 kHz −4 dB: two-pole 3 kHz low-pass, +1 dB on the heard echo. **Reverse
  Up/Down**: two-pole 8 kHz, +2 dB. **Lo-fi**: two-pole 4 kHz, +1 dB.
- The echo-alone fit asks for +2 … +5 dB on most modes; on the probe bank (echo + Chamber, S2) that took files down — the
  reverb behind the echo was fitted with LYNX's echo level — so a voicing's gain only scales the heard echo (Level's share),
  never the loop or the reverb's feed. Clean and Tape keep their voicing (their fits lost on the bank).

### Room / Chamber (Size 0 / 25 / 50 / 75 / 100 % × Decay 0.15 / 0.29 / 0.45; `hz/lab/roomtab.py`, `roomfit.py`)
- The errors move with Size, not Decay. Before: Chamber's first 100 ms −4 dB at Size 0, +1.6 … +2.8 at Size 100 %; its tail
  (0.3 … 2 s) +2 … +3.4 dB at Size 25 … 50 %; Room's first 100 ms −9 … −12.6 dB at Size 0, +1.3 … +3.3 at Size 100 %. That
  is why one trim per mode (190mb's V6) took other files down.
- The reference's Chamber decays faster in its first ~0.4 s (−26 … −35 dB/s against −20 … −22 dB/s later); LYNX's tail is one
  slope. LYNX's law: per mode at five Sizes (linear in between) the onset network's level, the late tank's level and the wet
  gain (Room at 100 % also half its early taps). Fitted per Size on the probe renders, then eased until no gate-bank file went
  down (Chamber Size 50 %: late × 0.6 / +3 dB fitted → × 0.75 / +1.7 dB shipped; Room Size 50 … 100 % without the fit's +1 …
  +2 dB, which the short decays' music did not want).
- Not done: the double slope itself (a law with an early decay of its own), a decay-dependent Room level, and the reference's
  level dependence (its wet runs ~1.5 … 2 dB lower at −20 dB input than at 0 dB, impulses and music alike; LYNX's input
  compressor goes the other way). A lighter compressor (1.15 / 1.1 / 1.0 : 1) was tried: music up, but 6 … 28 files down.

### Gates (time-window scorer; before = parity190mj/bin/fxr_b0, after = hz/bin/fxr_h1)
| bank | files | before → after | up / same / down |
|---|---|---|---|
| probe bank mode | 99 | 2.66 → 2.86 | 18 / 81 / 0 |
| pairs | 36 | 2.61 → 2.69 | 3 / 33 / 0 |
| sr44 / sr48 / sr96 | 16 each | 2.25 / 2.19 / 2.25 → 2.44 / 2.38 / 2.44 | 3 / 13 / 0 each |
| verb183b captures | 62 | 2.82 → 2.89 | 4 / 58 / 0 |
| tp172d probes | 40 | 3.20 → 3.25 | 2 / 38 / 0 |
| refcap182 programme | 20 | 3.75 → 3.80 | 1 / 19 / 0 |
| tp172d matched | 12 | 3.58 = | 0 / 12 / 0 |
| this round's Room / Chamber probes (new references) | 60 | Chamber 3.17 → 3.20, Room 2.73 → 3.23 (fit-build runs) | 13 / 47 / 0 |

## 3. Pro-C 3 → Crest: TTM (LYNX's Two-way) rebuilt from the reference's band processors (revision 1) [STATIC + EXEC] (round 190mj, part crest)

Branch `parity190mj-crest` (off `parity190mb` ebbadbe4e). Evidence: round 190mb's decompile `crest/rea/ttm.c` (TTM ctor, band
processor ctor / update / wiring, TTM update / wiring), this round's `crest/rea/nodes.c` (one headless Ghidra run under the
machine-wide lock: CustomTransformFilter, AveragingFilter, VolumeFilter, PeakHold, Mixer op, Capacitor process, the
look-ahead smoothing's setter), objdump of FabFilter_ProC_3.dll (`/mnt/e/lynx-work/rea190kw/bin/`) for the knob update's float
constants and the plug-in level's calls, constants read with `rea190kw/rea/rd.py`. Everything below is in LYNX's own words;
LYNX's code is written from these laws. Addresses are image virtual addresses (base 0x180000000).

### The objects [STATIC]
- `TTMCompressor` (ctor 0x18015eb40; knob update 0x180160940; wiring 0x180161810): three `TTMCompressor_BandProcessor1`
  (+0x1c0 low, +0x1c8 mid, +0x1d0 high), two `CrossoverMultiBandModel`s (+0x1e8 for the audio, +0x208 for the side chain;
  3 bands, 24 dB / octave), three `Mixer`s (+0x1e0 the plain sum of the audio split, +0x1f8 the sum of the bands' audio
  outputs, +0x200 the bands' gains, operation 7, for the meter) and a dB → linear converter (+0x1d8, the meter's gain).
  It is built at the plug-in level (0x180194627, stored at +0x158 of the channel object), not in ProCCompressor's style
  switch.
- The second crossover's constructor frequencies (2^12.70 / 2^13.70 Hz) are overwritten by the knob update: BOTH crossovers
  run at 2^6.462166786 = 88.17 Hz and 2^11.287712097 = 2500.0 Hz (the ctor's first crossover had 2^6.4643). Its role: the
  side chain's own split.
- Band processor `TTMCompressor_BandProcessor1` (ctor 0x18015c5e0, update 0x18015e460, wiring 0x18015e8d0). Node slots:
  0x310 VolumeFilter (the gain × "depth"), 0x318 DBConverter dB → linear, 0x320 DBConverter linear → dB, 0x328 Detector
  D1 (purpose 0 compress, dB domain), 0x330 VolumeFilter (audio × optional gain input 0x338), 0x340 Capacitor, 0x350 Mixer
  (+1, −1, +1), 0x358 / 0x360 MultiFilter channel of a MultiVolumeFilter (its two volume inputs 0x348 / 0x388), 0x368
  `TTMCompressor_BandProcessor1_Pro_CHoldProcessor1` (0x18015bf80), 0x370 PeakHold, 0x378
  `TTMCompressor_BandProcessor1_Pro_CLookaheadSmoothing` (0x18015b890), 0x380 RectifyFilter, 0x390 / 0x398
  CustomTransformFilter (mode 1 / 2), 0x3a8 Detector D2 (purpose 1 expand, dB domain); 0x338 / 0x348 / 0x388 / 0x3a0 are
  optional slots (null in the ctor).
- Wiring (0x18015e8d0): side chain → rectify → [0x3a0] → hold → PeakHold (look-ahead window) → transform 1 → Capacitor →
  transform 2 → linear → dB → D1 and D2; Mixer = D2 − D1 + a FloatParameterSource (band property 0x178, 0) → look-ahead
  smoothing → × depth → dB → linear (0x318). Output 0 = the band's audio through the MultiVolumeFilter channel; output 1
  = the gain (dB) for the meter.
- The run-time wiring the brief asked for: the plug-in level (0x180195270 … 0x18019535b) creates, per band, two nodes
  (vtable 0x18022be88, a field set to 3; class not read — shared by both channels, so LYNX reads them as the stereo link)
  and installs them through TTM's setters 0x180161770 / 0x1801617c0 into BOTH channels' band slots
  0x348 / 0x388 (the MultiVolumeFilter's volume inputs; the setters' band index 0 / 1 / 2 = slots +0x1c8 / +0x1c0 /
  +0x1d0, i.e. mid / low / high as LYNX orders them). So each band's audio IS turned by that band's (stereo-linked)
  gain (as linked by those nodes): a true three-band compressor (as round 190al measured); TTM's Mixer +0x200 only feeds the meter. 0x18015e780 pairs
  the left / right band processors (their Capacitors and the shared MultiVolumeFilter).

### Generic elements read this round [STATIC]
- CustomTransformFilter (process 0x18011b860): mode 1 = x · |x| (signed square), mode 2 = sign (x) √|x|. With the
  Capacitor between them the band's level is an RMS-like reading: one follower on the power, back to the amplitude.
- AveragingFilter (ctor 0x18010c360, process 0x18010c770): a running mean of the last ⌊rate · 0.001 · L⌋ + 1 samples.
  LookaheadSmoothing = two of them in series, L = clamp (0.5 · look-ahead ms − 0.125, 0, 20) (setter 0x18015bee0).
- PeakHold (process 0x180110ca0): the maximum over its window (ms).
- HoldProcessor (ctor 0x18015bf80): PeakHold over the Hold time → a Capacitor with attack 0 and release = the held peak ×
  Hold (ms) × 1e−4 s (a VolumeFilter of the PeakHold's output feeds the release input): the peak is held, then falls fast.
- VolumeFilter (0x180133b30): input × volume (× an optional second input).

### Knob → band laws [STATIC] (0x180160940, 0x18015e460; float / double constants read)
TTM's own properties: attack position a (0 … 1; the attack is 0.25 s · a³), auto release, hold (0 … 500 ms), knee (0 … 72 dB),
look-ahead (0 … 20 ms), depth (0 … 2, 1), range (0 … 60 dB), ratio (1 … 100), release position r (release 2.5 s · tan
(1.2 r²) / tan (1.2), i.e. the knob's release), threshold. Per band (low / mid / high):
- follower attack (the Capacitor's, = 0.25 × the band property): 0.5 / 0.25 / 0.125 × the attack knob's time;
- follower release: 0.25 × the release knob's time on low and mid, 0.25 × 0.45 on high (the high band's property is
  0.4356 tan (1.2 r²) + 0.0045 s = 0.45 × the knob's 0.968 tan (1.2 r²) + 0.01 s); auto release T = k / (√y + 1),
  k = 3.6 (T_band / 2.5 s)² + 0.1, T_band the band's release property (y = the follower's power);
- D1 (compress, centred quadratic knee = the knob, × (1 − 1 / ratio) below 100 : 1, ≤ range): threshold the knob + 0 / + 3
  / − 2 dB, ratio clamp (0.6667 × ratio, 1, 100) / the same / the knob's ratio;
- D2 (expand, one-sided knee = the knob, ≤ 0.6 × clamp (range, 0, 60)): threshold the knob − 7 dB + 0.5 × knee, its ratio
  bus 2 − 1 / R_u, R_u = 1 + 3.17 √((clamp (ratio, 1, 100) − 1) / 99), so the lift's slope is 1 − 1 / R_u (0.24 / 0.36 /
  0.49 at 2 / 4 / 10 : 1 — round 190v's measured 0.25 / 0.36 / 0.50);
- the look-ahead feeds the PeakHold window and the smoothing; Hold the hold processor; depth the × after the Mixer.

### [EXEC] What the graph does not show
- A level-independent cut, THE SAME IN EVERY BAND and knee-independent: 0.6 · (1 − 1 / ratio) · √a · (threshold + 36) dB
  when positive. Measured on trial190k's TTM grid (thresholds −48 … −12, ratios 2 / 4 / 10, knees 0 … 72, attacks 0.25 /
  10 / 100 ms; 1 kHz, 100 Hz and 8 kHz stairs) with the graph law and no cut: the needed cut is 0 at −48 / −36 and
  proportional to (threshold + 36), to (1 − 1 / ratio) (0.353 / 0.351 / 0.351 × s at 2 / 4 / 10 : 1) and to √a (0.603 / 0.600
  / 0.601 at 0.25 / 10 / 100 ms); least squares 0.6007, residual 0.04 dB rms over 189 settings × 4 regions. It is
  Upward's cut (round 190mb: scale 1, threshold + the upward range) at scale 0.6. LYNX applies it as measured, with
  (threshold + the upward range 0.6 × min (range, 60)) as Upward's inference (every capture has range 60).
- The reference's own split frequency matters: round 190al's 88 Hz left 0.3° at 100 Hz (the 100 Hz stairs' residual −44 dB);
  88.17 Hz takes it to −50 dB.
- Oversampled, the reference runs its graph at the oversampled rate: its own output moves by −36 dB (2x) … −16 dB (32x)
  against the 1x render (LYNX's band law at the base rate: −80 dB, i.e. unchanged); an LR4 split at 96 kHz differs from
  one at 48 kHz by 0.007 rad at 1 kHz and 0.063 rad / 0.2 dB at 8 kHz. LYNX splits the audio at the oversampled rate (the
  band gains, computed at the base rate, brought there through the audio's own up path): the 2x / 4x stairs −36 / −34 →
  −48 / −47 dB, their tc −36 → −57 dB. At 16x / 32x the reference's own drift (−16 dB at 32x) is not followed (LYNX's
  top is 8x).

### Built (Crest revision 1, `TtmLaw`, detector case 13) and LYNX's own choices
Every law above, per band, at the base rate (the oversampled path: the audio split only). LYNX's own: the hold's
sliding maximum is a counter hold (the peak held for the Hold time, then the current level: no 500 ms buffer per band),
the look-ahead's PeakHold a running maximum (SlidingMax); the stereo link of the bands' gains is LYNX's linked detector
level (link · max (|L|, |R|) + (1 − link) · |own|), as the other V2 styles; issue #77's bounds on the lift are kept as
revision 0 has them — the fade under −80 dBFS (no lift by −100 dBFS: silence and noise floors are never raised, a safety
test pins it) and the threshold bound (a lift never takes the band's peak past the threshold) — but not revision 0's 30 dB
ceiling (the upward computer's 0.6 × range bounds it). The reference lifts quiet floors: without the fade the TTM files
score 8.62 instead of 8.54 (owner decision). Not tested on references: look-ahead > 0, Hold > 0, auto release, depth (no
capture sets them).

## 4. ValhallaShimmer, ValhallaUberMod (item 4): not reached
The round's time went to Orbit's voices, FutureVerb and TTM. Round 190mb's reading stands: UberMod's spectra sit within ~1 dB
(its scores are held by the modulated taps' time structure), Shimmer shows no systematic level error per mode; neither showed a
structural difference this round, so nothing was changed.

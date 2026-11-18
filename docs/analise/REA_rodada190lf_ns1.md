# REA round 190lf, sub-round ns1 — Waves NS1 → Quell Denoiser

Owner's permission of 2026-11-17 (REA on third-party binaries for parity, clean-room only). Nothing of the vendor's code,
tables, presets or content was copied: the DSP was decompiled for STRUCTURE, the parameter names come from the plug-in's
own parameter description (the interface every saved state refers to), every number LYNX uses was measured on our own
renders. Work folder /mnt/e/lynx-work/waves2_190lf/ns1/ (HANDOFF.md, lab/, cap/, runs/, ev/).

## 1. What REA showed (structure only)

- `NS1.bundle/Contents/Win64/GenericMultiCoreProcessor.dll` exports `NS1Proc` (the DSP: a mono and a stereo process
  routine); `NS1.dll` is the control side (`cNS1_ALG`): it turns the one knob into the DSP's parameter set.
- Time domain, zero latency, no FFT. Per sample:
  - a front end of two stages, each the difference of two recursive filters — the three-way split (low / middle / high)
    whose open state is an all-pass pair;
  - per band, three cascaded all-pass-based (Regalia–Mitra) sections `y = (x + A x)/2 + g (x − A x)/2` sharing one gain
    `g`: a bell cut that is exactly transparent at `g = 1`; the summed `(x − A x)` parts are the Listen ("difference")
    output;
  - per band detector: the band of the mid signal (two biquads in cascade minus a third), rectified, plus a tiny offset;
    a noise-floor tracker that slews multiplicatively (one factor up, one down, frozen below a gate floor); a level
    follower that slews the same way; a threshold placed between the floor and the level by a "position" parameter,
    lifted by a global voice-activity flag (a count of bands above their thresholds, with on / off hold counters);
    a soft weight `w = thr⁴ / (e⁴ + thr⁴)`; the gain `1 − (1 − g_min) w²`; one-pole open / close smoothing of the gain.
- The saved state (refcap182's five settings, RealWorld list; names from the plug-in's ParamXML) shows what the knob
  (RealWorld 229) drives — the derived values the control computes and stores:

  | knob | MIN_FC / MAX_FC (85 / 86) | band centres (0, 13, … 65) | HP_GAIN / LP_GAIN (206 / 203; 30 Hz / 12027 Hz) | ADAPT_THR_POSITION (226) | MAX_THR_ABOVE_NOISE (228) |
  |---|---|---|---|---|---|
  | 0 % (D) | 60 / 16000 | 60 183 560 1713 5235 16000 | 0 / 0 dB | 0 | 0 (never set) |
  | 20 % (X1) | 60 / 14000 | 60 179 531 1581 4705 14000 | −10 dB | 0.02 | −6 |
  | 35 % (X2) | 65 / 13500 | 65 189 549 1597 4644 13500 | −25 dB | 0.0325 | −6 |
  | 50 % (S1) | 70 / 13000 | 70 199 566 1608 4573 13000 | −40 dB | 0.045 | −6 |
  | 100 % (S2) | 110 / 8000 | 110 259 611 1440 3394 8000 | −70 dB | 0.8 | −6 |

  Every band keeps Q 1.1, max attenuation −17.9 dB, expander attack / release 2.4 / 12 ms at every setting.

## 2. Measurements (our renders)

- **0 % is the bare all-pass pair.** refcap182's D renders = the dry programme through the RBJ all-pass pair
  60.07 Hz Q 1.0 · 16 kHz Q 0.90 (round 124's measurement) to −40.0 (drums) / −52.6 (bass) / −70.6 (guitar) /
  −66.7 (vocal) / −54.4 (mix) dB ESR (lab/dres.py). Probes (cap/p1, loadcomponent of the D state, 1.5 s lead): pink noise
  steps at −40 / −20 / −50 / −30 dBFS → −55 … −60 dB from the pair, no level change at any step (lab/anap1.py). Nothing
  dynamic, no shelf: the "−8 dB from dry" of the brief is the all-pass pair's phase. Probe set p2 (lab/anap2.py): speech-
  like bursts over −55 dBFS noise −69 dB, noise + tone −56 … −76 dB (outside a demo mute), sine stairs flat to 0.0 dB
  from −40 dBFS up; the only traces at 0 %: −0.5 / −0.7 / −1.7 dB on a 1 kHz sine at −50 / −60 / −70 dBFS, and impulse
  tails that end in exact zeros (the DSP runs with flush-to-zero / denormals-are-zero set: −40 dB on isolated impulses).
  Not modelled (the programme files are 40 … 70 dB below them).
- Today's matched engine at 0 % adds a −3 dB 9 kHz shelf, +1.2 dB below 60 Hz and its expander (D: −16 … −29 dB ESR).
- The probe renders reproduce refcap182's: D vocal −110 dB, S1 mix −38 dB (the adaptive detectors' history differs at
  the start); so the refcap182 renders do hold the settings (the `defaults` + `set` problem does not bite NS1 there).
- 20 … 100 %: the split moves with the knob (table above); against the reference today's engine (fixed 60 Hz / 16 kHz)
  is off by 15 … 35° at 3 … 12 kHz, −55 … −70° at 19 kHz and +30 … +60° at 30 … 60 Hz (lab/tfcmp.py, lab/phref.py).
  The noise steps at 20 / 50 / 100 % show the slow adaptive floor (at 100 % −20 dBFS noise is taken from −6 to −50 dB
  within 3 s); the sine stairs (p2) show the level dependence: at 20 % a 150 Hz tone loses 14 dB at −70 dBFS and 0.3 dB
  at −10 dBFS, at 100 % a 1 kHz tone 30 dB at −70 and 2.3 dB at −10 dBFS — data for the next round's engine (cap/out).

## 3. LYNX: Quell's zero law (hidden [5], `voicetools::quell::zeroLaw`)

- On: Amount 0 is exactly the matched engine's all-pass pair (its open path, 60.0658 Hz Q 0.9998 · 16 kHz Q 0.8992);
  from 0 to 20 % (the first measured setting) the engine's output fades in over it — both run on the same all-pass
  skeleton, so the fade is phase-coherent (`o = a + (engine − a) · amount / 0.2`); from 20 % up the engine alone,
  bit for bit as before. No fitting: the pair is round 124's measurement, confirmed here.
- Off (0) for new instances and every project saved before (gold renders below); the NS1 translator sets it.
- Cost: one compare per sample; at Amount < 20 % one multiply-add per sample and channel.

### Table (refcap182 programme, 25 files; before = fxr_m2, after = this branch)

| file | before → after | ESR dB before → after |
|---|---|---|
| D_bass | 7 → 8 | −28.2 → −52.6 |
| D_drums | 5 → 8 | −15.8 → −40.0 |
| D_guitar | 7 → 10 | −29.1 → −70.6 |
| D_mix | 5 → 8 | −20.3 → −54.4 |
| D_vocal | 5 → 9 | −22.6 → −66.7 |
| X1 / X2 / S1 / S2 (20 files) | unchanged | unchanged (bit for bit) |

Mean 4.84 → 5.40; up 5, same 20, down 0.

- Gold renders (ns1/lab/gold.sh, base fxr_m2 = origin/waves2_190lf vs this branch): Quell default, the classic engine at
  4 amounts, the matched engine at 4 amounts × tracker law on / off, Listen, Output; × 44.1 / 48 / 96 kHz × vocal / mix /
  drums: 135 renders, 0 mismatches.
- CPU (35 s stereo mix, 48 kHz, one core, wall clock, interleaved best of 7; no perf counters on this machine): NS1 D
  2.10 → 2.13 s, S1 2.05 → 1.97 s — the same within the noise (the 20 … 100 % path is unchanged code, Amount 0 adds one
  multiply-add per sample and channel).
- Tests: tests/Parity190lfNs1Tests.inc (hidden / saved while non-zero; NS1 translation; Amount 0 = the pair to
  < −120 dB at 44.1 / 48 / 96 kHz while round 124's engine is not; the fade and 20 … 100 % bit for bit); the round-119
  NS1 translation test now expects three parameters.

## 4. Tried and NOT shipped (all on this sub-round's renders; numbers in runs/)

- **The split follows the knob inside the engine** (all split filters at fitted MIN/MAX-like frequencies + two shelves;
  w1): the fit (lab/fitskel.py, an exact frequency-domain LTI change of today's renders, all five files) predicted
  X1 mix −12.9 → −24.5 dB, but in the engine the gain-reduced low / high parts no longer match the moved open path:
  real renders 4.84 → 5.12 with 7 files down (S2 drums 4 → 1, rms +9 dB). Reverted.
- **Only the open path's pair moves** (the bands' split stays; w2): 4.84 → 5.36, 9 up, 5 down (S1_mix, X2_mix, S2 bass /
  drums / guitar). Reverted.
- **A causal correction after the unchanged engine** (two all-passes + two shelves per setting, lab/fitskel.py
  POST=1): with sane bounds it gains ≤ 3 dB on some files and loses on others (X1 vocal −17.2 → −15.7); without bounds it
  needs an +11.7 dB 16 kHz shelf and a 15 Hz Q 4 all-pass (curve fitting, rate-fragile). Not built.
- What the 20 … 100 % settings need is the reference's own structure: the band layout and the low / high cuts that move
  with the knob (table §1), the slewing floor tracker and the voice-activity flag (§1). That is a new engine, not a
  correction of round 124's; next round: build it from §1 and fit its slews / position law on probes (cap/p1 noise
  steps + longer conditioning sequences), then the programme.

## 5. Files

- src/core/VoiceTools.h / .cpp (zeroLaw), src/core/PluginSubstitutes.cpp (translateWavesNs1 sets it, returns 3),
  tests/Parity190lfNs1Tests.inc (+ its include line in tests/tests.cpp).
- Lab: ns1/lab (tfphase, tfcmp, phref, dres, ltires, corrpot, corrshape, fitlin, fitskel, nm, anap1, mkprobe, cmpruns,
  gold.sh, cpu.sh), probes ns1/cap (p1.txt, stim, out, rep), decompile ns1/ev/gmcp.

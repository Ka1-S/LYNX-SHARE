# REA round 190dw: FL Studio Mobile (.flm) import residues — effect laws on FLM's own engines, instrument residues

Clean-room: FL Studio Mobile's behaviour restated in our own words from a static study of the FL-hosted
`FL Studio Mobile_x64.dll` (Image-Line's own component; decompiles from rounds 188f / 188h / 190u / 190by already on disk,
a few read-only Ghidra turns this round). No Image-Line code, data, preset or sample is in the repository. Every law
below was checked against FL Studio Mobile's own output: FL's command-line render (`/R`) of FL's "FL Studio Mobile Rack"
plugins with the module set to each test setting, on our own stimuli (a 28 s staircase / drums / noise bursts / sweep /
music file and a click), no UI driven. Settings were mostly taken from FL's FLM sample songs. 16-bit references: about
−75 dB of error is a null.

## 1. Equalizer (module 3) → Pocket EQ

- Per band (four): gain (v − ½)·24 dB as shown, applied through A = 10^(dB/20) where the textbook shelf / peak takes
  10^(dB/40) (a band moves twice the dB its knob shows, ±24 dB); frequency 2^(9.25v + 5) Hz; bandwidth 7v + ¼ octaves
  (α = sin ω · sinh(ln 2/2 · BW · ω / sin ω)); kind ⌊2v⌋ = low shelf / peak / high shelf; channel ⌊4v⌋ = both, left only,
  right only, left +gain / right −gain, left −gain / right +gain (each side has its own coefficients).
- Designed at twice the running rate (ω capped at 3). Every input sample is held for two steps of a four-lane pipeline
  (lane k = band k; each lane takes the previous lane's output of the step before) and every second output is kept:
  a flat EQ is one sample late.
- Input + 1e-30; coefficients below 1e-9 in size set to 0; single precision.

## 2. Graphic EQ (module 7) → Pocket Graphic EQ

Eight bands at 80, 160, 375, 750, 1500, 3000, 6000, 12000 Hz (low shelf, six peaks, high shelf), one octave, gain
24v − 12 dB shown on the doubled amplitude law, designed at the running rate. Four pairs: inside a pair the second band
takes the first band's output of two samples before; pairs follow each other within the sample. The cascade is eight
samples late.

## 3. Filter (module 9) → Pocket Filter

Frequency 2^(10.3v + 4) Hz (16 Hz…20.2 kHz), Q = 2v² + 0.8, type ⌊2v⌋ low-pass / band-pass / high-pass, slope ⌊2v⌋ =
1, 2 or 4 pairs of equal biquads. Low / high-pass: y = (x₀ ± 2x₁ + x₂)·b − a₁y₁ − a₂y₂ with b = (1 ∓ cos ω)/(sin ω/Q + 2);
the low-pass output × 0.8 per pair. Band-pass: the constant-skirt form (peak level Q·…) designed in double. In each
pair the second biquad takes the first one's output of the sample before: `pairs` samples of delay. The coefficients
and the level are recomputed only when Frequency, Q or Type change: a Slope change alone switches the number of pairs
at once (FLM clicks there too; FxAudit allows it).

## 4. Autoduck (legacy, module 10) → Pocket Autoduck

The beat phase (song position in beats, mod 1, set at each block and advanced by BPM/60/fs per sample), a rise over
a = 0.5·Attack + 0.001 of the beat and a fall to the next beat, env^((Curve + 1)/2), gain 1 − Ducking²·env, a one-pole of
0.01 per sample starting at the first target. That is module 29's engine (Pocket Autoduck, round 189h) with Time 1,
Shift ½, Hold 0, Shape 0 and the depth √Ducking' = Ducking², i.e. Ducking' = Ducking⁴. No new engine.

## 5. The plate behind Reverb (legacy, 12), Reverb 2 (35) and Reverb (32) → Pocket Reverb Classic / 2 / Reverb

FLM's `fxReverb` is the plate of FL's Fruity Reeverb 2 (same twelve lines, taps, decay, bass and damping laws) with
these differences, all needed for a null:
- mono input ½(L + R)·input gain (FL: L + R);
- the input diffusers' feed-forward coefficient on stages 2 and 4 is a second value c₈ = exp(−31.398888·(L₂ + L₃)/(fs·d))
  (stages 1 and 3 and every feedback use c₄): stages 2 and 4 are not all-passes;
- the early-reflection line is written after the second stage (FL: the first) — this is what gives FLM its long, strong
  low end (Reverb (32) at its defaults: 125 Hz octave T20 5.8 s where FL's plate gives 2.1 s);
- early-reflection offsets truncated, ⌊E·3.4013603e-5·⌊(room + 3)·fs/103⌋⌋;
- predelay ⌊fs·ms/1000⌋ (FL quantises to fs·p/384);
- levels 10^(c/200) from integer controls ⌊(hi − lo)·v + lo + ½⌋, early reflections × 0.5, wet × 0.3, the wet glide a
  double one-pole of 0.001 per sample; output l = ER·erL + wet·tapsL (r likewise), then out = (l + r) ± (l − r)·width/100.
- Legacy (12): runs in place and adds onto the input, so the dry passes at unity plus input·DRY; DECAY is cubed;
  WIDTH → width; 13…16 mute input / dry / early reflections / wet; 17 bass, 18 crossover, 19 / 20 modulation; the 22nd
  saved value is not read by FLM (LYNX's slot has 20 parameters).
- Reverb 2 (35): the engine at 0 dB; the module mixes 1.25·WET·(full output) + 1.25·ER·(raw first-tap reflections),
  its own SEPARATION (≥ ½: a mid / side width; < ½: two sequential cross-feeds), PAN as a balance, SIDE INPUT (the right
  input negated for the plate only), DELAY in ms or, with TEMPO, round(16·DELAY) sixteenths shortened to fit 1000 ms;
  gains not smoothed.
- Reverb (32): the legacy controls with the engine's own −3.5 dB early reflections and −12 dB wet; out = dry·in +
  wet·(full output, balanced by PAN) + EARLY²·(raw reflections) with dry = p15·(MIX ≤ ½ ? 1 : 2 − 2·MIX), wet =
  p14·min(2·MIX, 1); the four gains glide by 0.01 per sample.
- The module gate: a module runs while its input is active and stops (then resets) after its output stays below 1e-5
  for 0.5 s; the Equalizer and Graphic EQ after 10 s of silent input, the Filter after 6 s.

## 6. Instrument residues (item 4)

Laws and measurements in `docs/analise/FLM_FORMAT.md` §7.9: the rack's MIDI to the modules (mod wheel slot, bend per
module, Aftertouch reaches no module, CC 7 / 8 / 10 / 64 roles, CCLK controller links), pad output links, GMSynth's
FLNG / F HP / RING / VOX effects, 3x Osc's custom-shape path.

## 7. Best way?

- **FL:** FL Studio desktop hosts these exact modules in its FL Studio Mobile Rack plugins; the A/B renders are FLM's
  own engine.
- **Ableton:** Live imports no .flm; its EQ Eight / Auto Filter / Reverb are its own designs.
- **Users:** people bring FLM songs to a desktop DAW to finish them and expect them to sound as on the phone (issue #88).
- **Verdict:** FLM-exact engines for imported modules (what FLM plays), as for the other Pocket effects. LYNX's
  Parametric EQ, Reeverb 2 and the rhythm gain stay as they are for everything else; projects saved with the earlier
  mapping keep it.

## 8. Measurements (before = the earlier mapping, after = this round; FLM inside FL as the reference)

34 settings (6 Equalizer, 4 Graphic EQ, 7 Filter, 4 Autoduck legacy, 4 Reverb legacy, 4 Reverb 2, 5 Reverb (32)), the
28 s stimulus through FL's FLM Rack and through LYNX's import of the same module on FL's own dry render. Score = the
0–10 scale of round 170i; ESR = error-to-signal ratio of the whole file; lag = LYNX's offset against FLM; the curve
error is the transfer function (Welch, 1/12-octave grid 25 Hz…19 kHz) or the Autoduck's 1 ms gain curve. LYNX's file is
clipped at full scale as FL's 16-bit file is. CPU (one instance, -O1, % of real time at 44.1 kHz): EQ / Graphic EQ /
Filter 0.08–0.28 (the Parametric EQ mapping 0.02–0.19), reverbs 0.24 (Reeverb 2 mapping 0.28–0.30), Autoduck 0.13.

| Set | Score before → after | ESR dB before → after | Lag (samples) before → after | Curve error before → after |
|---|---|---|---|---|
| eq_A | 7 → 10 | -29.0 → -71.4 | -1 → 0 | 0.41 → 0.002 dB (max of |H|) |
| eq_B | 6 → 10 | -19.4 → -70.7 | -1 → 0 | 3.45 → 0.001 dB (max of |H|) |
| eq_C | 5 → 10 | -18.6 → -72.6 | -1 → 0 | 4.01 → 0.002 dB (max of |H|) |
| eq_D | 8 → 10 | -41.3 → -73.1 | -1 → 0 | 0.09 → 0.002 dB (max of |H|) |
| eq_E | 5 → 9 | -14.6 → -66.1 | -1 → 0 | 7.34 → 0.001 dB (max of |H|) |
| eq_F | 3 → 10 | -3.8 → -82.6 | -1 → 0 | 11.87 → 0.001 dB (max of |H|) |
| geq_A | 4 → 9 | -11.2 → -61.5 | -8 → 0 | 6.99 → 0.015 dB (max of |H|) |
| geq_B | 8 → 9 | -35.8 → -69.9 | -8 → 0 | 0.45 → 0.001 dB (max of |H|) |
| geq_C | 8 → 10 | -43.8 → -74.5 | -8 → 0 | 0.30 → 0.001 dB (max of |H|) |
| geq_D | 2 → 10 | -7.8 → -71.7 | -8 → 0 | 7.45 → 0.006 dB (max of |H|) |
| flt_A | 3 → 10 | -9.1 → -77.0 | -2 → 0 | 2.61 → 0.003 dB (max of |H|) |
| flt_B | 7 → 10 | -28.4 → -76.6 | -2 → 0 | 0.36 → 0.033 dB (max of |H|) |
| flt_C | 5 → 10 | -12.9 → -75.1 | -2 → 0 | 1.83 → 0.002 dB (max of |H|) |
| flt_D | 8 → 9 | -42.5 → -68.3 | -1 → 0 | 0.08 → 0.013 dB (max of |H|) |
| flt_E | 8 → 10 | -42.6 → -84.6 | -2 → 0 | 0.29 → 0.041 dB (max of |H|) |
| flt_F | 10 → 10 | -70.8 → -70.8 | -2 → 0 | 0.05 → 0.047 dB (max of |H|) |
| flt_G | 7 → 10 | -27.5 → -71.7 | -4 → 0 | 0.39 → 0.017 dB (max of |H|) |
| ad_A | 5 → 10 | -19.3 → -71.7 | 0 → 0 | 0.85 → 0.01 dB (95th pct of the gain curve) |
| ad_B | 8 → 10 | -30.8 → -72.7 | 0 → 0 | 0.56 → 0.01 dB (95th pct of the gain curve) |
| ad_C | 8 → 10 | -41.1 → -74.2 | 0 → 0 | 0.14 → 0.00 dB (95th pct of the gain curve) |
| ad_D | 8 → 10 | -26.7 → -71.3 | 0 → 0 | 1.55 → 0.02 dB (95th pct of the gain curve) |
| rl_D | 7 → 10 | -19.0 → -75.0 | 0 → 0 | see the band table |
| rl_A | 6 → 10 | -16.8 → -77.4 | 0 → 0 | see the band table |
| rl_B | 8 → 10 | -20.3 → -74.8 | 0 → 0 | see the band table |
| rl_C | 8 → 10 | -20.2 → -75.3 | 0 → 0 | see the band table |
| r2_A | 7 → 10 | -13.8 → -75.3 | 0 → 0 | see the band table |
| r2_B | 3 → 10 | -4.8 → -75.4 | 0 → 0 | see the band table |
| r2_C | 6 → 10 | -14.8 → -74.1 | 0 → 0 | see the band table |
| r2_D | 7 → 10 | -16.3 → -75.7 | 0 → 0 | see the band table |
| rv_D | 6 → 10 | -6.1 → -75.3 | 0 → 0 | see the band table |
| rv_S1 | 4 → 10 | -4.2 → -72.0 | -4 → 0 | see the band table |
| rv_S2 | 2 → 9 | -2.0 → -70.0 | 26 → 0 | see the band table |
| rv_S3 | 6 → 10 | -4.5 → -75.2 | -1 → 0 | see the band table |
| rv_S4 | 4 → 10 | -5.5 → -76.4 | -4 → 0 | see the band table |

Reverbs, per octave band 125 Hz…8 kHz (the same files; wet = output − the dry part):

| Set | T20 per octave band, max |LX − FL| s | Tail level after the bursts, max dB | Wet level during the music, max dB |
|---|---|---|---|
| rl_D | 1.94 → 0.04 | 1.65 → 0.01 | 0.86 → 0.00 |
| rl_A | 0.08 → 0.00 | 0.57 → 0.00 | 2.03 → 0.00 |
| rl_B | 0.10 → 0.00 | 1.34 → 0.01 | 1.19 → 0.00 |
| rl_C | 5.75 → 0.31 | 13.89 → 0.19 | 2.28 → 0.01 |
| r2_A | 2.38 → 0.02 | 4.17 → 0.01 | 4.74 → 0.00 |
| r2_B | 2.65 → 0.06 | 3.46 → 0.10 | 4.22 → 0.00 |
| r2_C | 0.81 → 0.02 | 14.35 → 0.00 | 16.19 → 0.00 |
| r2_D | 3.33 → 0.01 | 6.77 → 0.03 | 3.44 → 0.00 |
| rv_D | 3.69 → 0.06 | 7.99 → 0.01 | 1.88 → 0.00 |
| rv_S1 | 3.58 → 0.04 | 7.74 → 0.01 | 2.14 → 0.00 |
| rv_S2 | 0.06 → 0.01 | 2.81 → 0.00 | 3.64 → 0.00 |
| rv_S3 | 3.50 → 0.07 | 5.14 → 0.01 | 2.61 → 0.00 |
| rv_S4 | 1.73 → 0.01 | 4.81 → 0.01 | 1.80 → 0.00 |

(rl_C decays fast: what is left there is the 16-bit floor at the end of its tail.)

Impulse responses (our click, FLM in FL, `tests/FlmEffects190dwData.inc`): with LYNX's output rounded to 16 bits as
FL's render is, 99.98–100 % of the samples are identical to FLM's (at most 1 LSB apart); |H| at ten frequencies within
0.03 dB (0.18 dB on the steepest skirts), the same latency to the sample, reverb band energies in three windows within
0.14 dB. These numbers are the checks of `tests/FlmEffects190dwTests.inc`.

GMSynth's four effects (item 4, six FLM renders, pass-through before): gain within 0.03 dB, third-octave spectrum
0.13–0.78 dB mean; RING and VOX with LFO keep an ESR near −2 dB (phase of their oscillators against the song, open).

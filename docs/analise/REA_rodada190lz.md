# REA round 190lz — Pro-R 2 → LuxeVerb Space revision 1: the taps' tone and the Space-½ level (owner decisions b + c)

Round 190lz (0.1.30). **Status: the re-gate is not met (9 files block it, list below), so the code is NOT merged:** it lives on
branch `pror2_190lz-taps` (190lp's taps merged + this refit); `pror2_190lz` carries these notes only. Black-box only: renders of the refcap030 / refcap182 / tp172b captures, no static analysis. LYNX's
code is written from the measurements below (clean-room). Lab: `/mnt/e/lynx-work/pror2_190lz/` (lab/: svlab = core/SpaceVerb.h
alone, band.py / msb.py / tw3.py / h3.py / coll.py / an3.py measurements, lab2.py + labg2.py bank runs, cmp2.py gate,
evid.py tone evidence; evidence/: PNGs + summary.json).

## Owner decisions taken into this round

1. **Pro-R 2 → LuxeVerb law (190lp owner decision 3): options b + c.** (b) one more refit of the music / stim level at
   Space ≤ ¼; (c) the three two-step drops on the long 1 kHz tone are a modal lottery, accepted with evidence. Then the
   re-gate: land only if every remaining down file is a one-step drop with a clearly better waveform match (xcorr
   +0.1 and ESR 1 dB lower) or one of the three accepted tone files; otherwise report the list.
2. **Apex (190lp owner decision 2): keep LYNX's constant latency.** Pro-L 2's per-style look-ahead (Modern / Aggressive /
   Safe / Bus ≈ 50 ms, latency up to ≈ 65 ms by style) is not followed: Apex keeps one constant latency for every style.
   Nothing to build; the smooth styles' look-ahead stays out of the fit.

## What the music files had in common [EXEC]

- **Both sides are linear.** At the S1 state (Space ¼) the reference's music and sweep renders match the prediction from
  its own impulse render within ±0.5 dB per octave (LYNX's within ±0.1); only the 1 kHz tone of `stim` sits +4.8 dB over
  the reference's own impulse prediction (its modulation). So the music excess is a spectral weighting, not a
  steady-state-vs-impulse effect.
- **The excess sits in the taps' window.** Third-octave error per time window after the impulse (S1, 48 kHz): 0 … 40 ms
  +3.4 / +2.6 / +4.2 / +3.4 / +2.1 dB at 125 / 250 / 500 / 1 k / 2 kHz and −0.4 / −0.9 at 4 / 8 kHz; 40 … 100 ms ≈ 0;
  100 … 300 ms ≈ −1; later ≈ +1. Nearer Distances more (Distance 0.2: +5 dB at 125 … 1 kHz, −2.4 at 8 kHz), none at
  decay rate × 2 … × 4 (mid band of the impulses +0.5 / 0.0 dB against +1.2 at × 1). The impulses' broadband level
  matched because it is carried by the highs, which were 1 dB low: music, whose energy sits at 100 Hz … 2 kHz, played
  1.5 … 2 dB loud.
- **Space ½ is quiet.** The impulse captures' broadband level at Space ½ sat 1.2 dB under the reference at every rate
  (median of 21 files; −1.8 … −1.1 dB from rate × ¼ to × 4); the Space-½ single cases (G0 S0.5 R1 ir, PX0 S0.5 R0.8 mix,
  PX3 S0.5 W0.8 mix) and the D state's wet (side −0.5 … −1 dB) are this deficit.
- **Distance 0.8 is quiet.** PX1 D0.8 impulses −1.7 dB (the taps are gone there; the distance table's ¾ point).

## LYNX (revision 1 only; revision 0 untouched)

- **The taps' own tone**: a bell −3 dB at 700 Hz (Q 0.5) and a +1.5 dB high shelf at 4 kHz on the tap sum (before the
  output tone filters), in full up to decay rate × 1 and fading out to × 4 (smoothstep in log2 rate).
- **The body by Space** (late, 2-line, early networks): +1.3 dB at Space ½, linear to 0 at ¼ and ¾ (`Fit1::spaceDb`).
- **Distance table** ¾ point 0.06 → 0.36 dB (`Fit1::distDb[3]`).
- CPU: two biquads per channel on the taps; the Space engine alone 3.5 … 4.5 % → 3.7 … 4.7 % of a core (two interleaved
  runs, load 14 … 18: within the noise).

Tried and dropped (lab, whole refcap030 bank): a −2 dB bell on the body at Space ≤ ¼ (31 downs: Distance ¾ impulses
lost their mids), the same bell on the taps without the rate fade (28 downs: the rate × 4 tone files), a −4 dB tap bell
(Distance 0 / Character ≥ ¾ impulses down), the body by Space with +0.5 dB at ¾ (PX0 S0.8 R0.8 mix down), a low-end
mid / side law per Space (35 … 90 Hz: the D impulses' low third octaves are single modes, spec got worse), a 600 Hz Q 0.7
bell (more downs).

## The 1 kHz tone (stim / nburst) [EXEC] — option (c)

`stim` (refcap0124) ends in a 1 kHz tone rising to 0 dBFS (2.5 … 4.5 s): its level carries the file's rms; `nburst`
holds a 1 kHz tone for 3 s. Every remaining stim / nburst down is decided by the 1 kHz octave alone (the other octaves
within ±1 dB), and the reference's own 1 kHz gain at one state is a draw:
- The S1 state (Space ¼) captured six times: wet 1 kHz gain against the input tone (3.5 … 4.5 s) −0.99 (48 kHz), −1.05
  (96 kHz), −5.36 (44.1 kHz), −2.49 (48 kHz, Character Tilt ½ = the identical state), −2.11 / −2.84 (Character Tilt 0 / 1,
  not a LYNX control): 4.4 dB of spread on the same state. LYNX after: −0.93 at 44.1 and at 48 kHz (inside the
  reference's range, 0.06 dB from its 48 / 96 kHz captures); before (fxr_m4): −6.2 / −6.3.
- Accepted files (spectrum and waveform match, evidence/*.png): G4 R0.5 D0 nburst 4 → 3 (was 4 → 2 in 190lp; xcorr
  0.63 → 0.90, ESR −0.8 → −1.6 dB, lag 480 → −2), M_CharacterTilt_0 stim 4 → 2 (xcorr 0.83 → 0.91, lag 431 → 0), sr44 S1
  stim 4 → 2 (xcorr 0.80 → 0.89, lag −440 → 2, spec 1.76 → 1.58).

## Gate (fxrender `fxr_lz`, time-window scorer both sides; before = fxr_m4 = revision 0)

| bank | files | before → 190lp taps → **190lz** | up / same / down (190lz) |
|---|---|---|---|
| grid | 30 | 3.43 → 3.50 → **3.50** | 2 / 28 / 0 |
| grid2 | 140 | 2.81 → 3.53 → **3.61** | 72 / 63 / 5 |
| mode | 18 | 3.72 → 3.67 → **3.67** | 3 / 12 / 3 |
| pairs | 72 | 3.38 → 3.68 → **3.82** | 24 / 45 / 3 |
| prog | 6 | 3.50 → 3.33 → **3.33** | 0 / 5 / 1 |
| sr44 / sr48 / sr96 | 16 each | 4.44 / 4.12 / 4.06 → 4.12 / 4.25 / 4.44 → **4.12 / 4.44 / 4.50** | 1/12/3, 3/12/1, 4/12/0 |
| styles | 9 | 4.00 → 4.00 → **4.00** | 0 / 9 / 0 |
| **refcap030 all** | 323 | 3.30 → 3.69 → **3.77** | **109 / 198 / 16** (190lp: 102 / 195 / 26) |
| refcap182 programme | 20 | 3.95 → 4.05 → **4.05** | 2 / 17 / 1 |
| tp172b | 12 | 3.83 → 4.00 → **4.00** | 1 / 11 / 0 |

Down files: 17 (190lp: 27). 5 qualify (one step, xcorr +0.13 … +0.43, ESR 1.1 … 2.9 dB lower: G0 S0.75 R0 ir,
G2 B0.5 T1 ir, G2 B0.75 T1 ir, PX1 D0.2 B0.2 mix, PX1 D0.2 B0.5 mix). 3 are the accepted tone files. **9 block the
merge rule**:

| file | score | cause |
|---|---|---|
| grid2 G4 R1 D0.75 nburst | 3 → 2 | the 1 kHz tone (rms −2.64 → −3.32; the 1 kHz octave alone −4.4 dB, the rate × 4 row of G4 sits at −3 … −4.7 before and after) |
| mode M_Character_0 stim | 4 → 3 | the 1 kHz tone (rms +1.67, limit 1.5; xcorr 0.92 → 0.95, ESR −1.6 → −3.8) |
| mode M_Character_1 stim | 4 → 3 | the 1 kHz tone (rms −2.54; Character 1 modulates the reference's arrivals) |
| sr44 D_stim | 6 → 4 | the 1 kHz tone at 44.1 kHz (rms +1.31; the 1 kHz octave +1.7, all others ±0.1; sr48 / sr96 D_stim fine) |
| sr44 D_ir | 6 → 4 | low single modes (third octaves 39 / 63 Hz −4 / −3 dB: spec 0.72, limit 0.6) |
| sr48 D_ir | 6 → 5 | the same (spec 0.61, limit 0.6) |
| pairs PX3 S0.5 W0.8 mix | 4 → 3 | Stereo Width ≈ 1 at Space ½ still −1.87 dB (limit 1.5) |
| prog R_Style0 drums | 4 → 3 | the kick's low side (63 Hz side +4.6 dB: rms +1.80) |
| refcap182 S1 drums | 4 → 3 | the same drums stem at S1 (rms +1.84) |

Of the eight files 190lp listed for (b), four came back (M_Character_0.5 mix, PX0 S0.2 R0.5 mix, PX2 Ch0.5 Th0.5 mix,
PX3 S0.2 W0.5 mix); the two drums files and two tone files (M_Character_0 stim, G4 R1 D0.75 nburst) did not. Of the four
single cases three came back (G0 S0.5 R1 ir, PX0 S0.5 R0.8 mix, PX1 D0.8 B0.8 ir); PX3 S0.5 W0.8 mix did not. New downs
against 190lp: M_Character_1 stim (tone) and G2 B0.5 T1 ir (qualifies).

Old projects: revision 0 byte-identical (gold 15 / 15; fxrender with the revision forced to 0 = fxr_m4 on D / S1 × ir /
mix × 44.1 / 48 / 96 kHz, 12 / 12); every other effect byte-identical to round 190lp's merged binary (158 / 158 renders).

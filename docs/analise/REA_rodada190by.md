# Round 190by: Pocket SuperSaw and FL Studio Mobile's remaining effects

Clean-room notes. Source of the laws: `FL Studio Mobile_x64.dll` as installed with FL Studio (the FL-hosted FL Studio
Mobile plugin; image base 0x180000000), read statically as behaviour and restated here as formulas, constants and
tables. No code, preset or sample of the plugin is reproduced. Marks: **PROVEN-REA** = read in the code or data;
**PROVEN-FILE** = read in the shipped files (counts and statistics only); **MEASURED** = also checked against a render
of FL Studio Mobile inside FL (FL's `/R` command-line render, no UI); **INFERRED**; **OPEN**.

## 1. Pocket SuperSaw (FLM module type 23)

Implementation: `src/core/PocketSuperSaw.{h,cpp}` (`InstrumentKind::pocketSuperSaw`, project name `pocketsupersaw`,
"Pocket SuperSaw" in the plugin list, like the Pocket effects of round 188h). The channel keeps FLM's own 0…1 values
in `ChannelDef::modelParams`: LYNX parameter k = FLM parameter k + 1 (FLM's 0, Enable, is the channel's mute), so
`FlmImport.cpp` (`superSawState`) copies the module's PRMS as they are. The page is LYNX's listed-parameter page
(`core/ModelParams.h`): one automatable knob per FLM parameter, with FLM's display texts; FLM's own touch layout is
not copied. Tests: `tests/PocketSuperSaw190byTests.inc`.

The engine follows appendix A law by law, in single precision: wave tables with exactly the harmonics below
Nyquist (no aliasing by construction: table h holds harmonics 1…h, the table an oscillator reads is picked from its
frequency), the dithered table read, 1…12 unison oscillators on the detune curve, sub and pulse, SuperSync, the
ladder filter recomputed every 24 samples, the three linear envelopes with squared level output, the per-voice LFO,
mod-wheel router, arpeggiator, distortion + trim and the tempo delay.

Differences from FLM, by design:

- one random generator per instance (FLM shares one between every SuperSaw of the process): random start phases
  (Re-trig Phases off), table dither and the random LFO shape are other draws of the same distributions;
- the channel's pitch (knob, fine tune, main pitch, the MIDI wheel through the channel's range) is added to every
  voice; FLM's own Pitch Bend Range (37) has no source in a LYNX project and is kept as a value only;
- the free-running LFO starts with the instrument, so its phase against the song is not FLM's (in FL's render FLM's is
  exactly one 512-sample host block ahead of a note at the song start).

### 1.1 Measured against FLM inside FL

37 parameter sets × 6 s phrases, FLM's SuperSaw rendered by FL (the Rack preset with its one module replaced, FL's
`/R` render, 44.1 kHz) against the same module imported from a `.flm` and rendered by LYNX's song renderer.

| Sets | What they vary | Result |
|---|---|---|
| 29 of 37 (Re-trig Phases on) | level, unison 2…12, detune, detune curve, unison pan, sub, pulse, cutoff 0 / 0.5 / 1, filter envelope, level envelope, SuperSync at Sync 0 / 0.5 / 1 and its envelope, distortion, trim, delay low / high-pass, ramp LFO → pitch, one-shot LFO → pulse width, arpeggiator, mono and poly glide, transpose, pan, note pan, 8-voice chords | energy within 0.03 dB, level over 125 ms windows within 0.02…0.06 dB (mean), third-octave spectrum within 0.02…0.30 dB (mean), same pitch, same left / right balance, the 10 ms level envelope (beating, LFO, arpeggio) correlates 0.998…1.000, waveforms line up to −17…−43 dB per note |
| U7, SUB, V8 | loud sets | FL's 16-bit file clips at full scale (LYNX's peak 1.39…1.72); the same sets at a lower Level (U7R, SU2, V8Q) are in the row above |
| D, U7P | Re-trig Phases off | random start phases: the same energy (−0.01 dB / +0.43 dB), beating at the same rates (modulation spectrum correlates 0.997 / 0.948), other draws |
| LFO | free sine LFO → cutoff | exact once LYNX renders one 512-sample block before the song (level 0.01 dB mean, 0.02 dB max; waveform −37 dB); as imported the LFO is that block late |
| LFS | tempo-synced square LFO → cutoff | level 0.35 dB mean: the square's edges fall inside different 125 ms windows by FL's block timing |
| LFR | random LFO → cutoff | other draws: same rate, modulation spectrum correlates 0.95 |

CPU (reference renderer, one core, −O1): 0.8…1.7 % of real time for the sets above (12 unison oscillators 1.7 %,
8-voice chords 1.5 %).

## 2. Pocket Comb Filter (FLM module type 40)

Implementation: `src/core/PocketFxD.cpp` (`Comb`, `CombDetector`), `EffectKind::pocketComb`, "Pocket Comb Filter" in
the mixer's effect list (Filter). LYNX parameter k = FLM parameter k + 1 (FLM's 0, Enable, is the slot's switch), as
for every Pocket effect. No FL desktop twin. 15 shipped presets use it (PROVEN-FILE: Manual ×4, Lowest Note ×8,
Detect Pitch ×3).

### 2.1 Parameters — PROVEN-REA

| FLM # | Name | Default | Law |
|---|---|---|---|
| 1 | Mode | 0 | `int(2v)`: Manual, Lowest Note, Detect Pitch |
| 2 | Pitch | 0.33 | Manual with Snap off: `f = 2499·v⁴ + 1` Hz |
| 3 | Feedback | 0.8 | `fb = v`, linear, no limiter (1 does not decay) |
| 4 | Mix | 0.75 | wet gain added to the untouched dry |
| 5 | Offset | 0.5 | `round(4·(v − 0.5))` octaves, −2…+2; not used in Manual |
| 6 | Smoothing | 0.25 | per-sample glide coefficient `(1 − v²)·0.1999 + 0.0001` |
| 7 | Snap | 1 | Manual only: play the note of 10 / 11 instead of Pitch |
| 8 | Polarity | 0 | `int(3v)`: bit 0 = left feedback negative, bit 1 = right |
| 9 | Asymmetry | 0.5 | `a = 0.1·(v − 0.5)`: left `f·(1 − a)`, right `f·(1 + a)` |
| 10 | Pitch Oct | 0.5 | `o = round(7v)` |
| 11 | Pitch Note | 0.4 | `k = round(11v)`; note `12·o + k` (69 = 440 Hz); defaults: 52, E3 |

### 2.2 Signal — PROVEN-REA, the delay law MEASURED

- Target frequency: Manual as above; Lowest Note = the lowest held key, remembered while none is held; Detect Pitch =
  the detector's held output. Outside Manual the Offset octaves apply. Clamped to 1…20000 Hz, then one glide per
  channel (`s ← s − (s − target)·c`, starting from 0 in a new module).
- The delay is FLM's tape loop (the one of Tape Delay, `core/PocketTape.h`): with N = int(fs), the loop takes
  `(N − 2)/s + 2` samples a turn and is read one sample later, so the comb's delay is **D = (N − 2)/s + 3 samples**,
  not fs/s (E3 at 44.1 kHz: 270.57 samples = 163.0 Hz, 19 cents flat; 1 kHz plays 936 Hz). The loop's speed follows
  its target by 0.1 % a sample, so a pitch change bends what is already in the loop, like tape.
- Per channel `w = in + sign·fb·wet` goes into the loop; `out = in + mix·wet`. No saturation.
- Detect Pitch: mid, or the louder side when the side signal is over 1.5× the mid (100 ms mean-square meters,
  1.2× hysteresis); a 1250 Hz one-pole low-pass; blocks of 2048 samples without overlap; a block above −50 dB gets a
  YIN estimate (difference function over lags 0…1023 with a 1024-sample window, cumulative-mean normalised, first
  dip under 0.15, else the global minimum, parabola through it); then an outlier filter at block rate (a running RMS
  of the estimates, k = exp(−4·2048/fs): an estimate three times off is dropped and the last accepted one kept; a
  jump of an octave or more has to be confirmed by the mean of the last three raw estimates).

### 2.3 Measured against FLM inside FL

FL's `/R` render of an FL project whose only effect is "FL Studio Mobile Rack FX" holding one Comb Filter module, on
round 188i's 28 s stimulus (sine steps, drums, noise bursts, sweep, music, tail), against LYNX's import of the same
module (`flmfx::map` → `tools/flmfxr`, one 512-sample block before the song as FLM gets inside FL). FL writes 16-bit
files, so about −75 dB is a null.

| Set | What it holds | Score | Error (ESR) |
|---|---|---|---|
| defaults (Manual, Snap, E3) | the delay law, mix, feedback | 10 | −76 dB |
| Pitch 0.5 free, fb 0.9, mix 1, no smoothing | free pitch, glide 0.2 | 10 | −74 dB |
| Pitch 0.7, Left −, Asymmetry 0.75 | polarity, asymmetry | 10 | −78 dB |
| Pitch 0.9, Both −, Asymmetry 1, Smoothing 0.8 | slow glide from 0 | 10 | −78 dB |
| Lowest Note, Offset −1, no key held | plays E2: the remembered key starts as the Pitch Oct / Note pair, not 48 | 10 | −75 dB |
| Detect Pitch, Offset +1, Smoothing 0.74, Right − | tracking on tones, sweep and music | 8 | −53 dB (sine steps −78, sweep −76, music −73, drums −42, bursts −30) |
| Detect Pitch, defaults otherwise | — | 4 | −8 dB |

The last set is not a fault to chase: **two FL renders of that same project differ from each other by the same
amount** (−8.8 dB, score 4, first difference in the sine steps), while the set above repeats bit for bit. The cause
found: a block that is silent except for an onset at its end has a difference function of zero at every small lag;
FLM computes it by FFT, so what it normalises there is rounding noise, the "first dip" lands on a lag of a few samples
(8.8 kHz), and the outlier filter then holds that value for about two seconds. LYNX has its own FFT and so its own
noise in such blocks.

Corrections to the static reading: the first key of Lowest Note (above). Not copied: on a block under the gate FLM
applies the Offset factor to the held frequency a second time for one sample.

CPU (one core, -O1 build, 28 s of stereo at 44.1 kHz): 0.26–0.40 % of real time in Manual / Lowest Note, 0.5–0.9 % in
Detect Pitch.

## 3. Pocket Multiband Compressor (FLM module type 36)

Implementation: `src/core/PocketFxD.cpp` (`Multiband`), `EffectKind::pocketMultiband`, "Pocket Multiband Compressor"
(Dynamics). 30 LYNX parameters, k = FLM's k + 1; the place of FLM's editor flag 4 is **Solo** (LYNX's own: Off / Low /
Mid / High; the import sets Off). No FL desktop twin (not Maximus, not Fruity Multiband Compressor): a three-band
feed-forward peak compressor working in the linear domain. Shipped modules exist with 27 and with 31 values; a
module with fewer values gets FLM's defaults for the rest.

### 3.1 Parameters — PROVEN-REA

FLM's numbering; the band blocks start at 6 (low), 13 (mid) and 20 (high).

| FLM # | Name | Default | Law |
|---|---|---|---|
| 1 | Master gain | 0.5 | `48v − 24` dB |
| 2, 3 | Low / high crossover | 0.3, 0.7 | `20·2^(9v)` Hz (20 Hz … 10.24 kHz; 130 Hz and 1576 Hz at the defaults) |
| 4 | (the editor's "solo the selected band" flag) | 0 | interface state; LYNX's Solo knob stands here |
| 5 | Dry mix | 0 | `v²`, added to every band's gain (see below) |
| b + 0 | Input gain | 0.5 | `48v − 24` dB |
| b + 1 | Threshold | 1 | `−48·(1 − v)` dB |
| b + 2 | Ratio | 0 | `30v + 1` |
| b + 3 | Attack | 0.3 | `0.05·v²` s (0 … 50 ms) |
| b + 4 | Release | 0.3 | `0.99v + 0.01` s (10 ms … 1 s) |
| b + 5 | Gain | 0.5 | `(2v − 1)·24` dB |
| b + 6 | State | 0 | `int(2v)`: On, Bypass (the band passes as split), Mute |
| 27, 28, 29 | Low / mid / high width | 0.5 | see below |
| 30 | Width mode | 0 | `int(v + 0.5)` |

### 3.2 Signal — PROVEN-REA

Three bands from two fourth-order Linkwitz–Riley-type crossovers: low against the rest at the low crossover, the
rest split again at the high one, and the low band through the high crossover's low-pass + high-pass sum (an
all-pass) so the three add up flat in level. Per band and sample, on the band's pair:

1. Input and output gain glide 1 % a sample.
2. Detector level `d = max(|gin·L|, |gin·R|)·gin` (the input gain acts twice on the detector's path).
3. A peak follower with release (`e1 = d` when d is higher, else `e1 −= e1·cr`), then the attack smoother
   `e2 += (e1 − e2)·ca`.
4. A hard knee **in amplitude, not in dB**: above the threshold `G = (thr + (e2 − thr)/ratio)/e2`, else 1.
5. `out = (dry + G)·gin·in·gout`: the dry mix is a parallel path inside each band (dry 1 without gain reduction
   is +6 dB).
6. Width. Mode 0: side × `clamp(2w)`, mid × `clamp(2·(1 − w))` (½ unchanged, 0 mono, 1 side only), w gliding. Mode 1:
   up to ½ the side is scaled by `w` around an unchanged mid; above ½, `L' = L − c·R`, `R' = R − c·L'` with
   `c = w − ½`.

The bands are summed and the master gain (gliding) applied. No limiter, no clipping.

The coefficients are `ca = 1 − 0.1^(1/(Ta·FS))` and `cr = 1 − 0.1^(1/(Tr·FS))` with **FS fixed at 44100 whatever the
running rate** (the band object's own rate field is never updated): at 48 or 96 kHz the ballistics run faster by the
rate ratio. LYNX copies that, so a project sounds as FLM plays it at that rate. Not copied: on the very first sample
after a reset FLM's dry term is still its "unset" marker, so that one sample has the gain G − 1.

### 3.3 Measured against FLM inside FL

Same method as §2.3. Eight sets (defaults; two bands muted; ratio 16 under −29 dB thresholds with input gain and
moved crossover; ratio 31 with the fastest attack and release; ratio 19 with the slowest; band gains with widths
0.2 / 0.8 / 1; a bypassed band with width mode 1; dry mix 1 with a muted band and the master gain down): all
**score 9 or 10, error −64…−78 dB** — a null at 16 bits in every set. CPU 0.7–1.1 % of real time.

## 4. Reverb (FLM module type 32) — mapped

`FlmEffects.cpp` `reverb32()`: no new engine. The module is the plate of types 12 and 35 (LYNX: Reeverb 2) behind
type 12's knobs with its own dry / wet stage — PROVEN-REA:

- DECAY (cubed), HIGH DAMP, PREDELAY, ROOM SIZE, DIFFUSION, WIDTH, LO-CUT, HI-CUT set the plate as in type 12. The
  controls the module does not reach stay at the plate's defaults: early reflections −3.5 dB, plate −12 dB, bass ×1,
  crossover 500 Hz.
- Output = `dry·in + W·pan·(plate + early reflections, through the width stage) + ER²·(raw early reflections)`,
  with `W = wetLevel·min(2·MIX, 1)`, `dry = dryLevel·(MIX ≤ ½ ? 1 : 2 − 2·MIX)`, `pan` = `clamp(2·(1 − PAN))` left
  and `clamp(2·PAN)` right, `ER` = EARLY REFL.; parameters 14 and 15 are the unnamed wet and dry levels.
- In Reeverb 2's terms: dry as above; wet `0.3·10^(−12/20)·W`; early `0.5·10^(−3.5/20)·W + 0.5·ER²`.

Reported as partial when used: PAN off centre; WIDTH other than 100 % together with EARLY REFL. (FLM's width does not
touch the raw early reflections, Reeverb 2's does); MOD AMOUNT above 0.

Measured (five sets, same method): dry level exact to the plate's integer control (under 0.1 dB); the reverb's
level on drums, noise, sweep and music within **0.4 dB** per channel, third-octave spectrum within 0.1 dB on average
(1.5 dB worst band; 2.2 dB with DIFFUSION 0.5), side/mid ratio within 0.4 dB, decay equal (−15.9 against −15.8 dB in
the half second after the music; −5.0 / −5.1 dB with DECAY 0.6). This settles the −12 dB / −3.5 dB reading. A
reverb's tail does not null: the 170i scores are 2–6 (ESR −2…−6 dB), driven by the sine steps, where single tones
differ by up to ±8 dB per channel between FLM's plate and Reeverb 2 (the modes of the two plates do not sit on the
same frequencies; the same holds for types 12 and 35, which were never rendered tone by tone before). PAN 0.8:
LYNX's left side is 7 dB too loud, as the partial note says. No shipped content uses type 32.

## 5. Tuned EQ (FLM module type 30) — mapped

`FlmEffects.cpp` `tunedEq()`: the Graphic EQ's eight bands (low shelf, six peaks, high shelf; a band moves twice the
dB its knob shows, `24v − 12` on the knob) on centres an octave apart, `110·2^((n − 9)/12 + i)` Hz with the Tuning
note `n = int(11v + 0.001)` (0 = C … 9 = A), all with the Bandwidth's `10v³ + 0.1` octaves — PROVEN-REA. Mapped to one
or two Parametric EQ slots like the Graphic EQ.

Measured (four sets): flat = exact; a mixed curve at the default Bandwidth: score 8, −39 dB (the high shelf near the
top of the band differs by under 1 dB from FLM's design); Bandwidth 0.2 and 1.0 (0.18 and 10.1 octaves): score 3, level
−2.3 / +2.5 dB — LYNX's EQ does not reach those shapes (peak Q 0.1…18, shelf slope 0.1…4), and the import now says so
("Tuned EQ bandwidth beyond LYNX's EQ range"). FLM's chain also delays the signal by 8 samples (four pairs of
filters, each handing over two samples late; MEASURED: lag −8); not reproduced. No shipped content uses type 30. An
own eight-biquad engine would make the extremes exact; left as a decision (see the handoff).

## 6. Pocket Tape Stop (FLM module type 42)

Implementation: `src/core/PocketFxD.cpp` (`TapeStop`), `EffectKind::pocketTapeStop`, "Pocket Tape Stop". LYNX
parameters: Stop time, Start time, Stop time synced, Start time synced, Play, Warp, Sync (FLM's 1, 2, 3, 4, 5, 6, 7).
No FL desktop twin (not Gross Beat, not Fruity Scratcher). No shipped content uses it.

PROVEN-REA:

- A stereo recorder of 2^ceil(log2(5·fs)) samples. While Play is on and no ramp runs, the output is the input, bit
  for bit. Play going off starts a ramp of Stop time: the output is the recording read at a speed that falls from 1
  to 0, so the pitch falls with it; at the end the last value dies away (two steps a sample of a one-pole with
  `a = 1 − 44.1/fs`, about 11 ms) into silence. Play going on starts a ramp of Start time: the read head speeds up
  from the input's present position, fades in through a 200 Hz one-pole, and over the last `min(Start time, 0.1 s)`
  cross-fades to the live input.
- Times: free `max(5v, 0.005)` s; synced `S_k / int(BPM)` s with k = round(9v) and S = 0, 15, 22.5, 30, 45, 60, 120,
  240, 480, 960 (¼ … 16 beats; 0 = the 5 ms floor), taken when the knob is set. Sync chooses the pair.
- Speed along a ramp of position u (0 stopped … 1 playing): `(B^u − 1)/(B − 1)`, `B = 200^(1 − 2·Warp)` (Warp ½ is a
  straight line; 1 keeps the speed and drops late; 0 drops early).
- A reversal in mid-ramp continues from the present speed and takes the matching part of the time.
- The module runs only while the song plays; stopped, the audio passes.

Measured: Play on = the input (−215 dB); Play off from the start = silence, as in FL. **The ramps are not
measured**: they need Play to change during the render, which a command-line render of an .flp cannot do with the
tooling we have (needs a live check). Unit tests hold the laws (speed curve by zero crossings, the decay, the
cross-fade back to the input bit for bit, reversal, synced times, 44.1 / 48 / 96 kHz). CPU 0.02 % of real time.

## Appendix A: the SuperSaw module (read in round 190ab, built in round 190by)

## A1. Host interface (vtable) — PROVEN-REA

| Slot | Address | Role |
|---|---|---|
| 65 | 0x1807f9cc0 | setter (switch on `id − 10`, byte table 0x1807fa188, dword table 0x1807fa13c); stores the value via the base setter 0x1807988a0 first, then the per-id side effects of §2 |
| 66 | base 0x180449d20 | getter (`+0x210`): the raw PRMS value. Most parameters are **read live** by the DSP through it |
| 67 | 0x1807f6860 | defaults |
| 68 | 0x1807f6b90 | names |
| 69 | 0x1807f6930 | display text |
| 70 | 0x1807f6ab0 | step counts |
| 71 | 0x1807f8590 | note `(key, velocity 0…1, pan 0…1)`; velocity 0 = note-off. Stores the velocity in a 128-entry held-key table (module +0x590) and a press counter (+0x790, global counter +0x990). If the arpeggiator is on: rebuild the arp list (0x1807f76a0) and play nothing directly. Else note-on 0x1807f8610 / note-off 0x1807f87b0 |
| 73 | 0x1807f84a0 | per-note offsets (pitch semitones, pan, velocity; 12 bytes) — written into **all 8 voices** (+0x148/+0x14c/+0x150), not only the new one; note-on clears them first |
| 74 | 0x1807f8d20 | mod wheel value → +0xef0 (raw), sets the LFO-recompute flag +0xa00 |
| 75 | 0x1807f8d30 | pitch bend → +0xeec = bend × 0.5 |
| 78 | 0x1807f9150 | render (adds into the host buffers) |
| 79 | 0x1807f99a0 | reset / sample-rate: delay buffers 2 × ⌊2·fs⌋ floats, clears held keys, resets the 8 voices, arp, LFO phase |
| 82 | 0x18047b020 | returns 9.0 (no audio effect; INFERRED UI size) |

Enable (parameter 0): the render returns at once when value < 0.5 (base setter flag).

## A2. Parameter table

`v` = PRMS value (float). "int(·)" = truncation **in float32 with the code's operation order** (mulss then addss then
cvttss2si). This matters: the presets store k/n values such as 0.09090909 (1/11), and the float32 product 11·v + 1
truncates to 2 where a double evaluation gives 1 (see §6 for the counts). Steps = slot 70; defaults = slot 67; display =
slot 69. All PROVEN-REA unless marked. Where the engine law column says "live", the DSP reads the value each control
tick; "setter" means a field is derived when the value is set.

### 2.1 Oscillator (1…18)

| k | Name | Default | Steps | Display | Engine law |
|---|---|---|---|---|---|
| 0 | Enable | 1 | 2 | — | v < 0.5: no render |
| 1 | Osc. Pulse Width | 1 | 0 | %d | live (normal mode): pw = clamp(v + [LFO dest 4]·L + [MW dest 3]·M, 0, 1); pulse offset = int(pw·2146435072) in the 2⁻³² read space ⇒ duty 0…50 % (v = 1 square, v = 0 silence) |
| 2 | Osc. Pulse Mode | 0 | 2 | On/Off | live: on if v > 0.5 ⇒ each oscillator reads saw(φ) − saw(φ + offset) |
| 3 | Osc. Sub Level | 0 | 0 | %d | live: sub gain s = v², sub on if s ≥ 0.001 (normal mode only) |
| 4 | Osc. Unisono | 0 | 12 | %d | live: n = clamp(int(11v + 1), 1, 12) oscillators; voice normaliser 10^(−0.05·n) (−1 dB per oscillator) |
| 5 | Osc. Unisono Detune | 0.2 | 0 | %d | live: d = clamp(v² + [LFO 5]·L + [MW 4]·M, 0, 1) / 12 octave (max ±1 semitone) |
| 6 | Osc. Unisono Pan | 0 | 0 | %d | live: spread a = v (a < 0.001 ⇒ 0, mono) |
| 7 | Osc. Unisono Detune Curve | 0.5 | 0 | %d | live: c = 2v − 1; exponent e = 1 + 4c (c ≥ 0) or 1 + 0.9c (c < 0), range 0.1…5 |
| 8 | Osc. Re-trig Phases | 0 | 2 | On/Off | note-on: off ⇒ a voice that starts from silence gets random phases (LCG `s·0x77a73631 − 0x0900c56c`, 12 draws); on (and SuperSync off) ⇒ every (re)trigger sets phase_u = u·int(p9²·4.2520177e9) (wrapping 32-bit) |
| 9 | Osc. Phases Offset | 0 | 0 | %d | see 8 (phase spread p9²·0.99 cycle per oscillator index) |
| 10 | SuperSync Mode | 0 | 0 | %d | setter: mode on iff v ≠ 0 (+0xa03); a change re-lays out the UI. Selects the oscillator of §3.2 |
| 11 | Osc. Sync | 0 | 0 | %d | live (SuperSync): base sync amount |
| 12 | Osc. Pulse Width (2nd) | 1 | 0 | %d | live (SuperSync): pulse width w' = clamp(0.5v + [LFO 4]·L + [MW 3]·M, 0, 1). The name function returns "Osc. Pulse Width" for both 1 and 12 |
| 13–16 | Osc. Sync Attack / Decay / Sustain / Release | 0 / 1 / 1 / 1 | 0 | %d | setter: sync ADSR (per 24-sample tick), times T(v) (§3.4), sustain = v |
| 17 | Osc. Sync EG Amount | 0.5 | 0 | bipolar % | live: amount = (2v − 1)³ |
| 18 | Osc. Sync Kbd Dir. | 0 | 2 | On/Off | live: on ⇒ the amount's sign flips for a note that is not higher than the note it glides from (voice flag +0x28 = from < key) |

### 2.2 Filter (19…27)

| k | Name | Default | Steps | Engine law |
|---|---|---|---|---|
| 19 | Filter Cutoff | 1 | 0 | live: c = clamp(v + [LFO 0]·L + p27·vel′ + [MW 0]·M, 0, 1) (vel′ = clamp(velocity + slot-73 offset, 0, 1)) |
| 20 | Filter Resonance | 0 | 0 | live: r = clamp(v + [LFO 1]·L + [MW 1]·M, 0, 1); ladder feedback 4r |
| 21 | Filter KBD | 0 | 0 | live: + (n − 48)·v/12 octave, n = the voice's glided note (no transpose/bend) — 1 oct/oct at v = 1 around key 48 |
| 22–25 | Filter EG Attack / Decay / Sustain / Release | 0 / 1 / 1 / 1 | 0 | setter: filter ADSR (per tick), T(v), sustain v |
| 26 | Filter EG Amount | 0.5 | 0 | live: ±8 octaves · (2v − 1) · env (linear) |
| 27 | Filter VEL | 0 | 0 | live: cutoff += v·vel′ (on the 0…1 scale, before the clamp) |

Cutoff in Hz: **fc = min(2^(10.3·c + 4 + 8·(2·p26 − 1)·Ef + (n − 48)·p21/12), 20000)** (c = 0 ⇒ 16 Hz, c = 1 ⇒ 20 kHz).

### 2.3 Level, pan, pitch (28…38)

| k | Name | Default | Steps | Display | Engine law |
|---|---|---|---|---|---|
| 28 | Level EG Attack | 0 | 0 | %d | note-on: rate from T(0.9·v) (§3.4) |
| 29 | Level EG Decay | 1 | 0 | %d | setter: T(v), per sample |
| 30 | Level EG Sustain | 1 | 0 | %d | setter: sustain level v (on the envelope value x; the output uses x²) |
| 31 | Level EG Release | 0 | 0 | %d | setter: T(0.85v + 0.05) (min 14.2 ms) |
| 32 | Level | 1 | 0 | %d | live: Lv = clamp(v + [LFO 6]·L + [MW 5]·M, 0, 1); voice gain × **Lv²** |
| 33 | Pan | 0.5 | 0 | `%.1f` of 200v − 100 | live: p = clamp(notePan − 0.5 + v + [LFO 7]·L + [MW 6]·M + slot-73 pan, 0, 1); gL = min(2(1 − p), 1), gR = min(2p, 1) (linear balance, centre 1/1) |
| 34 | Slide Held Mode | 0 | 2 | On/Off | note-on: on ⇒ glide only when another key is held; off ⇒ glide from the last note played (none before the first note / after a reset) |
| 35 | Slide Time | 0 | 0 | %d | t = (v/2)² s (max 0.25 s); no glide when t ≤ 0.001 |
| 36 | Transpose | 0.5 | 25 | `%d` of int(24v) − 12 | semitones int(24v) − 12 |
| 37 | Pitch Bend Range | 0.1 | 12 | %d | setter: R = int(11v + 1) semitones (1…12); default 0.1 ⇒ 2 |
| 38 | Slide Time Static | 1 | 2 | On/Off | on ⇒ the glide always lasts t; off ⇒ t per semitone (constant rate) |

### 2.4 LFO, arpeggiator, voices, effects, mod wheel (39…59)

| k | Name | Default | Steps | Engine law |
|---|---|---|---|---|
| 39 | LFO Shape | 0 | 4 | int(3v + 0.5): 0 sine sin(2πφ) (bipolar ±1); 1 falling ramp 1 − φ (0…1); 2 square 1 for φ < 0.5 else 0 (0…1); 3 random hold, one new uniform 0…1 value per cycle (LCG `s·0x4d66b561 + 0x16c0a8e8`, value u32·2⁻³²) |
| 40 | LFO Rate | 0 | 0 | setter: period = 30·(1 − v)³ + 1/30 s (0.0333 Hz … 30 Hz; display "%.2f Hz"); live + [MW 8]·M (clamped 0…1) when MW Dest = 8 |
| 41 | LFO Mode | 0 | 3 | int(2v): 0 free (a voice starting from silence takes the module's free-running phase +0xa30, so all voices are in phase), 1 one-shot (phase reset at each (re)trigger, holds at the end, φ = 1), 2 tempo-synced (φ = frac(songBeats / beatLen) while the host plays; period beatLen·60/tempo s) |
| 42 | LFO Beat | 0 | 10 | int(9v) → beatLen {0: 0.125, 1: 0.25, **2: 0.75, 3: 0.5**, 4: 1, 5: 2, 6: 4, 7: 8, 8: 16, 9: 32} beats (index 2 and 3 are in this order in the code) |
| 43 | (unnamed) | 0 | 0 | not read anywhere, 0 in all 175 modules |
| 44 | LFO Amount | 0.5 | 0 | a = 2·clamp(v + [MW 7]·M, 0, 1) − 1, amount A = sign(a)·a² |
| 45 | LFO Dest | 0 | 8 | int(7v + 0.5): 0 cutoff, 1 resonance, 2 pitch (L octaves), 3 sync amount, 4 pulse width, 5 unison detune, 6 level, 7 pan — each adds L to the 0…1 control before its clamp |
| 46 | LFO Fade In | 0 | 0 | fade gain 0 → 1 over T(v) s, restarted at each (re)trigger; L = shape · A · fade |
| 47 | ARP Rate | 0.2 | 6 | int(5v) → step {0.125, 0.25, 0.375, 0.5, 0.75, 1} beat (default ⇒ 1/16 note). A change re-syncs the arp |
| 48 | ARP Range | 0.5 | 4 | int(3v) extra octaves (0…3) |
| 49 | ARP Mode | 0 | 6 | int(5v): 0 off, 1 up, 2 down, 3 up-down (ends not repeated), 4 up-down (ends repeated), 5 random (uses the up list) |
| 50 | Monophonic Mode | 0 | 2 | v ≠ 0 ⇒ voice 0 only; switching it on resets voices 1…7 at the next render |
| 51 | Number Of Voices | 0.5 | 8 | setter: nv = int(7v + 1) (1…8; default 4) — the polyphony limit |
| 52 | Distortion | 0 | 0 | setter: on iff v > 0.005; drive k = 10^(2v) − 0.99 (0.01…99.01) |
| 53 | (unnamed: output trim) | 1 | 0 | setter: gain g = 10^(−0.9·(1 − v)) (display "%.1f dB" of int(−180·(1 − v))·0.1, "0 dB" at 1) |
| 54 | Delay Time | 0.775 | 5 | setter: int(4v) → {0.25, 0.375, 0.5, 0.75, 1} beat (default 0.75) |
| 55 | Delay Feedback | 0.4 | 0 | setter: fb = 0.99·v |
| 56 | Delay Mix | 0 | 0 | setter: m = v² |
| 57 | Delay Filter | 0.5 | 0 | live: §3.6 |
| 58 | (unnamed: MW Amount) | 0.5 | 0 | setter: Mamt = 2v − 1 |
| 59 | (unnamed: MW Dest) | 0 | 9 | setter: int(8v): 0 cutoff, 1 resonance, 2 sync, 3 pulse width, 4 detune, 5 level, 6 pan, 7 LFO amount, 8 LFO rate |

Mod-wheel term M = Mamt · wheel (wheel = slot-74 value, raw; MiniSynth's slot 74 is the mod wheel —
spec-minisynth-diff §1). Defaults that are 0 are not listed by slot 67 (it returns 0). Defaults rest on slot 67 alone: the eight song modules named "Default / Default" were edited by their authors (each
differs from slot 67 in several parameters, `files.txt`).

## A3. DSP

Per voice, the render runs a 24-sample control tick (voice counter +0x140, wraps at 24): LFO, glide, the three
modulation sources, filter coefficients, oscillator increments and tables, gain targets. Per sample: oscillators,
filters, amp envelope, output. The module sums 8 voices, then distortion/trim, then delay, then **adds** into the
host buffers. PROVEN-REA (0x1807f9150 render, 0x1807fa9e0 voice).

### 3.1 Wave tables (0x1807f2c30, built once, process-global at 0x1821f9378) — PROVEN-REA
- 1024 tables of 2048 floats; table k (0…1023) = Σ_{h=1}^{k+1} sin(2π·h·i/2048)/h (a falling saw with k + 1
  harmonics, built cumulatively in float32), then **all** tables are scaled by 1/max, max = the largest sample over
  all tables = 1.850404 (computed, `work/supersaw` session: 1/max = 0.540423). So a 1-harmonic table peaks at 0.540 and
  a full saw at ≈ 1.0 (Gibbs), its ramp body ±0.849. (The sin argument is accumulated in double, wrapped at 2π.)
- Table choice per oscillator per tick: k = clamp(int((fs/2) / f_osc), 1, 1023) (f_osc in Hz, including detune);
  the sub uses f_osc/2. So table k holds k + 1 harmonics, one above Nyquist at most (no table 0 in use).
- Read: index = (P + D) >> 21 with D = LCG & 0x3FFFFF, the LCG `s·0x0bb38435 + 0x3619636b` drawn **once per sample
  per voice** (shared by all oscillators of that voice; process-global state 0x1821f9370, shared with every other
  random draw of the module). The dither therefore moves the read by 0…2 table entries.

### 3.2 Oscillators

**Normal mode (SuperSync off, 0x1807fba10 + 0x1807fbbd0)** — PROVEN-REA
- n = int(11·p4 + 1) oscillators. Phase P_u (u32) advances by inc_u = int(ratio_u·f/fs · 0.5 · 4292870144) per sample
  (4292870144 = 2³² − 2²¹); the main read uses 2·P_u (so a full cycle), the sub reads P_u (one octave down) from its
  own table, gain s = p3², per oscillator.
- Detune ratio: for n > 1, x_u = u/(n − 1), ratio_u = 2^((2·x_u^e − 1)·d) with d and e from p5/p7 (§2.1); n = 1 ⇒ 1.
  So the spread is ±d octave (± p5² semitone at most), symmetric for c = 0, bunched to one side for c ≠ 0.
- Pulse mode: o = saw(2P + D) − saw(2P + D + pwOffset) (peak ≈ 2).
- Stereo (spread a ≥ 0.001 and n > 1): s_u = (2·x_u − 1)·a; s ≥ 0 ⇒ (gL, gR) = (1 − s, 1), else (1, 1 + s); left sum
  Σ o_u·gL_u, right sum Σ o_u·gR_u, the filter runs on L and R. Otherwise one mono sum feeds both channels.
- Voice normaliser N = 10^(−0.05·n).

**SuperSync mode (0x1807fb670)** — PROVEN-REA. No unison, no sub, no pulse flag, N = 1.
- Sync amount S = clamp(p11 + (±(2·p17 − 1)³)·Esync + [LFO 3]·L + [MW 2]·M, 0, 1); spacing w = 1/(5S + 1).
- Every tick the oscillators are laid out again from oscillator 0's current phase P₀: pairs i = 0, 1, … while
  i·w < 1 (at most 6 pairs = 12 oscillators): A_i at P₀ + i·w, B_i at P₀ + min(i·w + w·w′, 1) (cycles), where w′ is
  p12's width (§2.1); all with the same increment int(f/fs·4292870144), read at P (not 2P), weights A +1, B −1.
- Result: Σ_i [saw(φ + i·w) − saw(φ + i·w + w·w′)], a train of ⌈5S + 1⌉ pulses per period (the last one cut by the
  cycle end) — a hard-sync-like sweep as S moves; S = 0 ⇒ one pulse of width w′·cycle.

**Pitch** (per tick, 0x1807fa9e0): f = 440·2^((n_g − 69)/12) · 2^((R·bend·0.5 + int(24·p36) − 12)/12 + [LFO 2]·L +
offset/12) (n_g = glided note, 0 Hz when n_g < 1; R = bend range; bend = slot-75 argument; offset = slot-73 pitch).
Key 69 = 440 Hz.

**Glide**: per tick n_g moves toward the key by the step of §2.3 (static: (key − from)·24/(t·fs); else ±24/(t·fs)),
stopping on arrival.

### 3.3 Filter (0x1807fbf40 coefficients, voice render per sample) — PROVEN-REA
- Model: four one-pole stages in series with global feedback, u = x − 4r·y4; y1 += g(u − y1); y2 += g(y1 − y2);
  y3 += g(y2 − y3); y4 += g(y3 − y4) (each stage uses the already-updated previous stage), with
  **g = clamp(fc·6.25 / (4096·fs), 0, 1)**. This sub-step is written as a 4×4 matrix A and input vector b, and A, b are
  squared 12 times (A ← A², b ← A·b + b) ⇒ the exact composition of **4096 sub-steps per output sample**, input held.
  Recomputed per voice every 24 samples (no interpolation in between).
- Per sample: y ← A·y + b·x for the four states, then **stage 1 only** is softened: y1 ← y1 / (1 + 1e-5·y1²). Output =
  y4 (24 dB/oct low-pass). No other filter types.
- r = 1 ⇒ loop gain 4 ⇒ self-oscillation at the cutoff (INFERRED from the model, see O3).

### 3.4 Envelopes — PROVEN-REA (0x1807fa3f0 rate law, 0x1807fa1c0 setter, voice render)
- Time law: h(x) = 2^x − 1; **T(v) = 2^(5·h⁷(clamp(v, 0, 1))) − 1 seconds** (h applied 7 times). Values: T(0) = 0,
  T(0.01) = 2.70 ms, T(0.05) = 14.2 ms, T(0.1) = 30.2 ms, T(0.2) = 69 ms, T(0.3) = 122 ms, T(0.4) = 196 ms,
  T(0.5) = 305 ms, T(0.6) = 480 ms, T(0.7) = 798 ms, T(0.8) = 1.51 s, T(0.9) = 3.93 s, T(0.95) = 8.51 s, T(1) = 31 s.
- Step size = 1/(int(T·N) + 1) with N = fs (amp, per sample) or fs/24 (filter, sync, LFO fade: per tick).
- All segments are **linear**: attack 0 → 1 by +step, decay −step down to the sustain value, sustain, release
  −step to 0. A decay/release from level y lasts (y − target)·T.
- Amp: attack rate a = step(T(0.9·p28)); at note-on increment = (1 − x)·a (so the attack always lasts T from any
  start level), except that a voice still sounding gets the fixed step(T(0.01)) when that is slower than a (declick).
  Release reaching 0 resets the voice. **Output gain = x²**.
- Filter and sync envelopes restart from 0 at each (re)trigger; the amp envelope keeps its level (the attack restarts
  from it). Mono legato notes (§4) do not retrigger.

### 3.5 Level, velocity, pan per voice — PROVEN-REA
G = clamp(velocity + offset, 0, 1) · Lv² · N; target gains (gL, gR) = G·(min(2(1 − p), 1), min(2p, 1)) per tick;
the applied gains move toward them by 0.01 per sample (they start at the target on a fresh voice). Output L += y4_L ·
x² · gainL, R likewise. Velocity is linear (no curve).

### 3.6 Module effects (0x1807f8d50, 0x1807f8eb0) — PROVEN-REA unless marked
1. **Distortion / trim** (only on blocks where a voice played): if distortion on: y = tanh(k·x) · g / tanh(k);
   else if g < 1: y = g·x. (0x1808a1b34 is the CRT single-precision tanh: odd, x for tiny |x|, ±1 above |x| = 10 —
   INFERRED from its code shape.)
2. **Delay** (stereo, L and R independent, no ping-pong): length = int(beats · fs · 60/tempo) samples, capped at
   ⌊2·fs⌋. Per channel and sample: z = delayed sample; F = 2·p57; if F > 1: c = (F − 1)⁴, lp += (z − lp)·c, y = z − lp
   (high-pass, everything removed at p57 = 1); else c = F, lp += (z − lp)·c, y = lp (low-pass; p57 = 0.5 ⇒ c = 1,
   unfiltered). Write **(x + y)·fb** into the line; output x + m·y. Note: the first echo is already scaled by fb, so
   feedback 0 gives no echo at all. A tail counter keeps the delay running 4·fs samples after |out| < 0.001.

### 3.7 Voices, notes, arpeggiator — PROVEN-REA
- 8 voice objects (0x158 bytes each), polyphony nv = int(7·p51 + 1). Note-on (poly): the first free voice
  (velocity 0) among the first nv, else the one with the oldest start counter. Mono: voice 0; a new note while voice 0
  sounds and is not in release is legato (no envelope restart, glide per §2.3). Note-off (poly): every voice with that
  key goes to release. Mono note-off: if keys are still held, voice 0 moves (legato, glide) to the **highest** held key
  with its stored velocity; else release.
- Arpeggiator (0x1807f76a0 list, render loop): the held keys ascending with their velocities, repeated +12, +24, +36
  for the range; order by mode (§2.4 k 49). Step counter in samples: each step = beats·60/tempo·fs; at each step all
  sounding voices are released and the next list entry is played (note pan 0.5) through the normal note-on, so the gate
  is the full step. Index advances (+1, wraps) or, in mode 5, is a uniform random pick. An empty list or mode 0 releases
  all voices. A change of mode/range/rate rebuilds the list.
- The module's free LFO phase (+0xa30) advances by n·inc per block (inc = 1/(period·fs)).

## A6. Shipped data (PROVEN-FILE: the round's walk of the shipped files; float32-exact decodes)

20 song modules / 155 presets:
- Unison n: songs {1:2, 2:2, 3:1, 4:1, 5:5, 6:2, 7:2, 8:2, 10:1, 12:2}; presets {1:7, 2:33, 3:33, 4:9, 5:13, 6:13, 7:8,
  8:9, 9:3, 10:1, 11:1, 12:25}. Unison pan on in 18/20 and 149/155.
- SuperSync on: 2 songs / 48 presets. Pulse mode: 9 / 104. Sub on: 10 / 100. Re-trig phases: 4 / 62.
- Mono: 5 / 51. Voices nv: songs {1:2, 4:13, 8:5}, presets {1:37, 4:95, 8:23}.
- Arp: songs: 1 module (mode 2 down); presets: up 4, down 3, up-down 6, up-down-repeat 4 (random 0). Arp rate: mostly
  1/16 (index 1).
- LFO dest: songs {cutoff 10, res 2, pitch 1, PW 3, detune 4}; presets {cutoff 49, res 8, pitch 14, sync 32, PW 31,
  detune 13, level 2, pan 6}. LFO mode: songs {free 7, one-shot 2, sync 11}; presets {50, 20, 85}. Shapes: sine
  13/128, ramp 7/22, square 0/3, random 0/2.
- Distortion on 8 / 97; trim < 0 dB 8 / 95; delay mix > 0 11 / 131 (time index 3 = 0.75 beat most common).
- MW dest: songs {cutoff 13, res 3, PW 2, detune 1, level 1}; MW amount ≠ 0 in 7 songs. Bend range: 2 (13), 12 (6),
  1 (1). Transpose −12 / 0 / +12 only.
- Never used in the shipped content: parameter 43 (always 0), Kbd Dir in songs, arp random mode.
So every block of §3 is exercised by the shipped songs except the random arp, the square/random LFO shapes and
LFO → level/pan (presets only).

## A7. Open items — and the single FL render that settles each

Reference = FLM inside FL Studio (FL Studio Mobile plugin, one SuperSaw module), 44.1 kHz, the harness of 190n/190u.
- **O1 Overall level** (normaliser, x², Lv², −1 dB/osc, the FLM rack/wrapper gain): default module, key 60
  velocity 100, 1 s; RMS per channel vs the model.
- **O2 Table dither and phase start**: default module, unison 1, Re-trig on, Phases Offset 0, key 69: a null test
  against the model shows the dither (0…2 entries) and the start phase directly.
- **O3 Filter**: unison 1, cutoff 0.5, resonance 0 / 0.5 / 1, kbd 0: confirms g = fc·6.25/(4096·fs), the 4096-sub-step
  composition, self-oscillation at r = 1 and the stage-1 softening.
- **O4 Envelope times**: Level Attack 0.5 / Decay 0.5 / Sustain 0.5 / Release 0.5: the linear segments, x² output and
  T(v) (expected 0.305 s × 0.9-law for attack: T(0.45) ≈ 0.25 s).
- **O5 Bend scale** (slot 75 × 0.5 and range int(11v + 1)): one note with a full bend event (same open point as
  GMSynth O5; owned by the rack/wrapper spec).
- **O6 SuperSync**: SuperSync on, Osc. Sync 0 / 0.5 / 1: the pulse-train layout (1, 3.5→4, 6 pulses per period).
- **O7 tanh identity** (0x1808a1b34): Distortion 0.5 on a sine-like patch (unison 1, cutoff 0.2): tanh vs other odd
  shapers.
- **O8 Delay**: Delay Mix 1, Feedback 0 and 0.5, Filter 0.25 / 0.75: confirms the feedback-scaled first echo and the
  LP/HP split.
- **O9 Arp timing in FL**: the arp counts in samples from the block start; FL's block/tick split may shift steps (as
  the Transistor Bass tail did in 190u) — one arp phrase, mode 1, rate 1/16, against the model.
- O10 (no render, REA if wanted): who scales the mod-wheel value given to slot 74 (raw 0…1 assumed).

**Round 190by, after the renders of section 1.1**: O1 (level), O2 (dither and start phase), O3 (filter at
resonance 0, 0.5 and 1), O4 (envelope times), O6 (SuperSync), O7 (tanh), O8 (delay, low and high-pass) and O9
(arpeggiator timing) are MEASURED: every one of those sets is within 0.06 dB of FLM's level. Still OPEN: O5 (bend
scale) and O10 (mod-wheel scale) — FL's command-line render sends neither a wheel nor a bend to the plugin, and LYNX
feeds the engine the channel's own pitch instead of FLM's bend (section 1).

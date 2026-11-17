# Round 190ko — Live 12's Meld, Spectral Time and Spectral Resonator on LYNX's own engines (Braid, Timeprism, Harmonia)

Clean-room: Live 12.4.16 Trial was only played and listened to. Every law below comes from LYNX's own measurements of
Live's renders of probe sets (File › Export Audio/Video, All Individual Tracks, 48 kHz float) and from Live's own menus
as its GUI shows them; Live's program was not opened, decompiled or traced, and none of its content is used.
Material: `/mnt/e/lynx-work/meld190ko` — sets `sets/m1-render*.als` (303 Meld law tracks), `m2-render*.als` (the 84 Core
Library Meld presets, one phrase), `s1-render*.als` (89 Spectral Time / Resonator law tracks), `s2-render.als` (9
freezer tracks), 190kk's `k1` set (the 14 shipped Spectral presets on a drum loop); generators `s/tools/*.py`; analysis
`ana/*.py`; session notes `s/notes.md`; GUI shots `raw/`. Tags: [MEASURED] a render, [SHOT] Live's GUI, [DATA] saved sets
(142 Meld devices in the Core Library), [MODEL] LYNX's own choice where nothing was measured.

## 1. Meld → Braid (core/Braid.h, Braid.cpp, BraidParams.cpp)

### Menus and matrix [SHOT raw/l05, l06, f*, mx1…mx4, lf*, x*, g*]
- Oscillators (OscillatorType 0…24): Basic Shapes, Dual Basic Shapes, Noisy Shapes, Square Sync, Square 5th, Sub, Swarm
  Sine, Swarm Triangle, Swarm Saw, Swarm Square, Harmonic Fm, Fold Fm, Squelch, Simple Fm, Chip, Shepard's Pi, Tarp,
  Extratone, Noise Loop, Filtered Noise, Bitgrunge, Crackle, Rain, Bubble, Chord.
- Filters (FilterType 0…16): SVF 12dB, SVF 24dB, LP 12dB MS2, HP 12dB MS2, BP 12dB OSR, LP Crunch 12dB, LP Switched Res,
  Filther, Eq Peak, Eq Notch, Phaser, Redux, Vowel, Comb +, Comb -, Plate Resonator, Membrane Resonator. Macros: Q / L-B-H-N
  (SVF), Q / Drive (MS2, OSR, Crunch, Filther), Q / Lofi (Switched Res), Boost / Q (Eq Peak), Cut / Q (Eq Notch),
  Feedb / Spread (Phaser), Crush / Lofi (Redux), Q / Morph (Vowel), Feedb / Damp (Combs), Q / Ratio (Plate), Q / Damp (Membrane).
- LFO 1 generators: Basic Shapes (Shape, Fold), Ramp (Shape, Slope), Wander (Morph, Step), Alternate (L1, L2), Euclid
  (Steps, Pulses), Pulsate (Chance, Length). Its FX 0…18: None, Offset, Attenuverter (Scale), Gate (Time), Skew Unipolar,
  Skew Bipolar (Shape), Unipolarizer (Morph), Quantizer (Amount), S&H, Independent S&H (Rate), Clipper (Gain), Fade In,
  Fade Out (Ramp), Slew Down, Slew Up, Slew Up & Down (Slope), Trig Env (Decay), Comparator (Threshold), Scrambler
  (Permute). LFO 2: Sine, Tri, Saw Up, Saw Down, Rectangle, Random S&H.
- Matrix columns (ModulationAmounts.0…18): per engine A Env, M Env, LFO 1, LFO 1 FX, LFO 2 (A 0…4, B 5…9), then Vel,
  Pitch, PB, Note PB, Press, Mod W, Rand, Slide, Spread. Rows: the 25 targets per engine in TargetName order [DATA].
  Pitch Mod 1 = 48 semitones (Live shows the default PB row 0.1458 as "7", Note PB 1 as "48") [SHOT mx2, MEASURED q1x].

### Laws [MEASURED m1]
- Level: an engine at 0 dB, centre, Meld Volume −6 dB: a sine peaks at −15.1 dBFS → 0.354 per side before the volume.
  Each engine's output is high-passed at ≈ 18 Hz (a 261 Hz square droops 20 % per half cycle).
- Basic Shapes: Shape 0 / ⅓ / ⅔ / 1 = sine / triangle / saw / square, crossfaded between neighbours (harmonic levels
  within 0.5 dB at 0.25, 0.33, 0.66, 1); Tone narrows the square's pulse to one sample at 1 (rms −54.7 dB) [o00].
- Dual Basic Shapes: a second copy, Detune 1 = an octave, each at half level [o01]. Square Sync: the slave at
  1 + 2·F1 + 8·F2 − 2·F1·F2 times the note [o03]. Square 5th: the square rises to ×3 (½) and ×9 (1) [o04].
  Sub: an octave below; Aux adds a sine two octaves below [o05]. Swarms: eight voices in unison at Motion 0, Spacing 1
  spreads them over −24 / −12 / 0 / +19 semitones, 2.1 dB above one wave [o06…o09]. Harmonic Fm: the modulator at
  0.25 · 40^Ratio rounded to halves (0.25, 1.5, 10) [o10]. Simple Fm: ratio 4 · Ratio, index 2.9 · Amount [o13]. Squelch:
  two octaves below the note [o12]. Chip: a half-level square [o14]. Tarp: a sustained rounded saw an octave below; Decay
  does not change a held note [o16]. Filtered Noise: centre note · 2^(±1.95 oct), Width 1 = a narrow, louder band [o19].
  Bitgrunge: note · 2^(10 (Freq − ½)) [o20]. Crackle: −74 … −28 dB rms with Density / Intensity [o21]. Chord: −25 dB rms [o24].
  [MODEL] Noisy Shapes' Rough, Fold Fm, Shepard's Pi, Extratone, Noise Loop, Rain, Bubble, Chord shapes: LYNX's own
  algorithms with the names' behaviour; their levels and centroids only roughly fitted.
- Envelopes: Helix's measured segment law fits Meld's amp envelope (e0…e3 9.6 … 10.0); loop modes Trigger / Loop / AD Loop.
- Matrix: Vel is bipolar (64 = 0: no pitch change at velocity 64 [q10]); the Volume rows only attenuate:
  gain × (1 + Σ a (s − 1) / 2) (default Vel → Volume 50 %: 0 / −2.5 / −4 / −6 dB at 127 / 64 / 32 / 1 [pG]); the Pan rows
  count ±0.5 as a full side [pK]; Detune 1 = 1 semitone [q1y].
- Spread: one voice per note — the held notes of a chord take −0.25, −0.25, −0.5 … in the order they came (a single note's
  engines at ∓12.5 cents through the default ±0.5 detune rows) [pE, pJ]; Stacked Voices index 0 / 1 / 2 = 1 / 2 / 3 voices
  spread −1 … +1 (±50 cents), sharing the level (the same −15 dB rms) [pK, pF].
- Filters: SVF 12 exact; the driven circuits add up to +24 dB of Drive into a soft clip (+12 dB at ½) as one 12 dB stage
  [f02…f07]; the resonators ring at Q 0 [f15, f16] (their mode sets are [MODEL]).

### Gate (score 0…10, 190hs's score.py; before = the device dropped = a silent track = 0)
See the round's HANDOFF.md for the final numbers per set (m1 laws, m2 presets) and the CPU per voice.

## 2. Spectral Time → Timeprism (core/Timeprism.h on core/SpectralCore.h)
- Resolution menu: Low, Mid, High, Ultra (0…3) [SHOT res]; frames 512 / 1024 / 2048 / 4096, hop a quarter: a 250 ms
  delay of an impulse is ONE impulse (no smear) at 12288 samples (Ultra: 12 hops of 1024), 11776 (High), 12032 (Mid / Low);
  a 10 ms delay at Ultra = one hop [MEASURED s1 t00, t08, t09]. Timeprism reports the Ultra frame as its latency always.
- Time modes: Time, Synced (Notes), 16th, 16th Triplet, 16th Dotted [SHOT mode]; Notes index 3 = 1/24 bar, 15 = 1 bar,
  ascending through Live's LFO-sync list [MEASURED t10].
- Tilt in seconds over the log spectrum x (20 Hz … 20.48 kHz): Tilt > 0 delays by Tilt · x (1 at 20 kHz), < 0 by
  |Tilt| · (1 − x) [t03: +0.093 s per octave at 1]; Mask > 0 → × x^Mask, < 0 → × (1 − x)^(1.6 |Mask|) [t04]; Spray a fixed
  random 0 … Spray per bin [t05]; Stereo 1 mirrors the right channel's tilt and gives it its own spray [t05 st1].
- Shift moves every repeat, the first included: a 500 Hz tone echoes at 601 Hz (+100), the repeats at 700, 800 … with
  Feedback 0.7, each 3.1 dB down (feedback linear) [t06].
- The delay's Dry/Wet and the global Dry/Wet are equal-power (0.707 / 0.707 at 50 %); Input Send is a linear gain on the
  wet path [t07].
- Freezer: FadeType 0 = crossfade, 1 = envelope [SHOT dv-st: the X-Fade knob; with Onsets it shows Fade In in seconds];
  Retrigger runs only while Freeze is on (Freeze off passes the input [t21]); with Sync nothing sounds before the first
  interval [s2 u01]; a set saved frozen in Manual mode holds its first sound (the kick at −18 dB rms) [t20]; a freeze
  keeps the loudest of its first frames (LYNX's capture follows the onset's body). Onset sensitivity, the spectral
  re-synthesis and the envelope stacking are [MODEL] (s2 scores in the handoff).

## 3. Spectral Resonator → Harmonia (core/Harmonia.h)
- Freq: in Hz, or as a note when FrequencyDialMode = 1 (A1 = MIDI 45 = 110 Hz) [SHOT dv-sr, MEASURED r01].
- Partials at f0 · (1 + (n − 1)(1 + Stretch)): Stretch 1 = odd harmonics, −½ = half-harmonic steps [r04].
- Every partial's T60 = Decay (1.00 / 0.20 / 5.0 s) [r00, r02]; HF damping divides partial n's by 1 + 0.54 h^3.27 (n − 1),
  LF damping the lowest 10·l partials by 1 + 0.29 (10 l + 1 − n) [r03].
- Excitation: the input's energy around each partial, weighted down linearly to 0.9 of the spacing (a 500 Hz tone rings
  the 440 / 550 Hz partials at 0.36 / 0.44 of its level; an impulse every partial at −49 dB) [r00]; a partial rings on at
  least as loud as its excitation, then decays (the start level does not depend on Decay [r02]).
- Unison 0…3 → 1, 3, 5, 9 copies, ±2.9 % at index 1 and ±20 % at 3 with Amount ½, the centre 6 dB above the others
  [r08]; Dry/Wet equal power; Input Send ±24 dB into a limiter [r09, r10].
- [MODEL] Chorus / Wander / Granular partial modulation (r07: 5.6 … 8.4).
- Quality gate: a decay ≥ 15 s, unison 3 spread over 10 %, or granular modulation on a decay ≥ 5 s scored worse than
  leaving the device out on Live's preset renders → those stay dropped and listed.

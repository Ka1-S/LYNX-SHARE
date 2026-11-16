# Round 190ik — warp engine finish: Complex Pro, Texture, pitch-aware Beats (LYNX SHARE 0.1.30, BUILD-LIST 8)

Evidence: black-box renders of Ableton Live 12.4 Trial only — round 190gu's W1 set (80 clips) and this round's W2 set (108
clips: Complex Pro at −12…+12 st and 0.8 / 1.0 / 1.5× speed with Formants 0 / 50 / 100 and Envelope 32…256, Texture at
0.5…2× with Flux 0…100 and Grain Size 10…150, Beats on a tonal source). Sources: Live's Core Library vocal and the 190ei
drum loop as test stimuli (not shipped), plus two LYNX-made additive sources whose spectral envelope is known exactly (a
sung vowel with steps, a glide, vibrato, a re-articulation and a vowel change; four piano-like chords). No code, preset or
sample of Live is in the repo; the engines are LYNX's own (`src/core/WarpEngines.{h,cpp}`), on top of the Signalsmith
Stretch already vendored (MIT). Rubber Band was not used (GPL; nothing to gain over the measured approach below).

Work folder: `/mnt/e/lynx-work/warp190ik` (`live/` set + renders, `p/` harnesses, `ana/` analysis, `HANDOFF.md`).

## 1. Method

- `p/wrun.cpp`: LYNX's offline warp (`lynx::timeStretch`) set up as the .als importer sets a channel up; a clip transpose
  in a mode that does not render it offline goes through the sampler's real-time `VoicePitchShifter`, as in the app.
- `ana/m.py`: Live vs LYNX at 48 kHz mono over the clip's span — level, 10 ms envelope correlation / deviation, long-term
  spectrum deviation (24 log bands), frame log-spectral distance (40 bands, 2048/512), **formant shift** (the log-frequency
  lag that best aligns the two cepstrally smoothed envelopes, semitones) and, on drums / Beats, **transient sharpness**
  (peak in 0…12 ms over the 22 ms before each Live onset).
- Texture with Flux > 0 is random in Live and in LYNX: sample-wise metrics compare two random draws, so it is also judged
  by statistics (`ana/texstat.py`: the envelope's modulation depth around its 200 ms trend, the long-term spectrum).

## 2. Complex Pro

Facts (W2):
- No transposition: Complex Pro renders as Complex (Live vs Live: envelope 1.000, spectrum 0.07…0.56 dB). LYNX keeps
  Complex's render there.
- **Formants is linear in the shift**: on the vowel at +7 st the envelope moves 7.1 st with Formants 0, 3.4 with 50,
  0.1 with 100 (voice: 6.7 / 3.4 / 0.0).
- **Envelope is the envelope's resolution**: vowel +7, formants kept 4.8 / 1.2 / 0.1 / 0.1 st short of the source at
  Envelope 32 / 64 / 128 / 256 (chords, denser partials: 5.0 / 4.9 / 3.6 / 0.1).
- **Only pitched sound is corrected**: drums at +7 with Formants 100 vs 0 differ by 0.03 dB level, 0.78 dB spectrum (the
  voice by 1.4 / 3.9 dB).
- Up-shifted harmonics land within ±3 dB of the formant-kept spectrum; down-shifted ones keep the first formants but stay
  15…25 dB under it above them (a raise is limited).

Engine (`proStretch` + `warpengine::keepFormants`), warpLaw Measured only:
1. Signalsmith Stretch to `outFrames × 2^(st/12)` (100 ms blocks, overlap 4 — the voice shifter's analysis; 90 ms Complex
   blocks matched Live's Formants 0 renders less well: chords +7 spectrum 1.02 → 0.58 dB), along the warp map scaled by the
   transposition when there is one;
2. band-limited resampling back to `outFrames` (Kaiser sinc, cut-off lowered when read faster);
3. formant correction per 46 ms frame (quarter hop): the cepstral **true envelope** (Röbel & Rodet) of the mono power,
   lifter = Envelope / 4 samples at 44.1 kHz, the envelope read back at `bin × factor^(Formants/100)`; gain limited to
   −40 dB … +6 dB (up) / +60 dB (down: Live keeps the highs of a down-shift); held at the frame's fundamental below it;
   scaled by how tonal the frame is (spectral flatness over 300 Hz…8 kHz, 0.03 → 0 … 0.003 → 1: chords are tonal although
   barely periodic, drum hits stay as transposed);
4. level law (level pass): Live's output level after the correction is not the source's — up-shifts get quieter, down-
   shifts louder, depending on Formants and Envelope (W2, vs the source: voice / vowel / chords +7 −1.4 / −5.7 / −2.7 dB,
   −12 +2.2 / +0.8 / +4.0 dB; Formants 50 about half; Envelope 32 none). Fitted law of the effective shift u = semitones ×
   Formants × h(Envelope) (h: 0 at 32, 0.6 at 64, 1 at 128, 1.2 at 256): −0.69·u^0.75 dB up, +0.016·u² dB down; each frame's
   level is set to 0.65 × that law + 0.35 × the level the correction itself gives, scaled by the frame's tonality.

The lifter, the raise limits and the block were chosen by a grid on the 40 transposed r1.0 cells (`ana/grid*.txt`: score =
spectrum + LSD/2 + formant shift + |level|/2; before 10.70, first pass 7.34; lifter Envelope/2 9.19, no raise 8.26, ±24 dB
7.46, resample-before-stretch 7.56). Level pass (`ana/gridL*`, `spq.py`): a tilt-based level law (κ 0.5…1) 7.04…7.07, a
peak-interpolated log-frequency envelope worse (spectrum 4.6 → 5.0…5.6 on the 12-cell check), the fitted law with the
flatness weight and the down-shift raise 60 dB best (12-cell check: |level| 2.25 → 1.10, spectrum 4.59 → 3.59).

| Complex Pro (W1 + W2) | cases | \|level\| dB | env corr | env dev dB | spectrum dB | LSD dB | formant shift st | cells better / worse |
|---|---|---|---|---|---|---|---|---|
| transposed (Formants > 0) | 60 | 1.74 → **0.80** | 0.87 → 0.89 | 2.52 → 1.96 | 3.87 → 2.36 | 6.24 → 4.44 | 3.21 → 1.20 | 261 / 63 |
| Formants 0 | 8 | 0.20 → 0.11 | 0.98 → 0.99 | 1.35 → 1.02 | 0.53 → 0.43 | 3.00 → 2.68 | 0.58 → 0.44 | 27 / 6 |
| no transposition | 8 | unchanged (Complex) | | | | | | 0 / 0 |

Every mean improves; level within 0.8 dB of Live on average (by source: voice 0.65, drums 0.57, vowel 1.22, chords 0.95).
Octave-down chords: spectrum 6.2 (before) → 5.4 dB (first pass 11.7). Remaining worse cells (63): mostly envelope
correlation / deviation of single cells and drums at −12 (spectrum 3.0 → 5.2: the tonal frames of the drum loop take the
down-shift raise). Complex Pro transposes offline now: no real-time shifter runs for it.

## 3. Texture

Facts (W1 / W2, `ana/grains.py`, `gmodel.py`, `lvl.py`, `fluxfit*.py`):
- **Granular at the source's own speed**: with Flux 0 each grain k is anchored at (k + ½)·Grain Size ms of output and reads
  the source from where the warp puts the anchor (the voice's grain starts sit at 32.5 + 65 k ms within 0.05 ms at both
  speeds); grains are sine windows two periods long, not normalised (two overlap everywhere, their squares add to 1).
  That model renders Live's flux-0 voice at sample correlation 0.996 / 0.998 and the same level (±0.01 dB).
- **Flux scatters each grain's source position**; 25, 50 and 100 scatter alike (the spectrogram decorrelation from the
  flux-0 grains is 0.931…0.934 for all three), about ⅛ of the grain period from Grain Size 65 up, relatively less below.
  Between 0 and 25 the law is assumed linear (not rendered).
- A transposed Texture clip resamples its grains (its formants move: vowel +7 shifts 6.6 st).
- Below Grain Size 10 the period stops following the size one to one (level pass): 10 ms × (size / 10)^0.2 (7.2 ms at 2)
  matched the W1 Grain Size 2 renders best (exponents 0.1 / 0.2 / 0.35): voice spectrum 7.7 → 3.4 (r0.8) and 8.8 → 5.4 dB
  (r1.5), now under the spectral stretcher's 6.8 / 7.1; drums 3.0 → 2.8 / 1.0 → 1.9 (before 2.0 / 2.6). Grain Size 10 and
  up unchanged.

Engine (`warpengine::texture`, warpLaw Measured): the model above; the scatter is a fixed pseudo-random sequence per
source (renders repeat exactly); a transposition reads each grain `2^(st/12)` faster, rendered offline.

| Texture (W1 + W2) | cases | \|level\| dB | env corr | env dev dB | spectrum dB | LSD dB | formant shift st | cells better / worse |
|---|---|---|---|---|---|---|---|---|
| Flux 0 | 8 | 0.60 → 0.04 | 0.89 → 0.99 | 2.73 → 0.19 | 1.07 → 0.08 | 3.66 → 0.34 | 0.86 → 0.04 | 45 / 0 |
| Flux > 0 (random) | 48 | 0.42 → 0.31 | 0.90 → 0.88 | 2.44 → 2.39 | 1.44 → 1.06 | 4.41 → 3.72 | 1.09 → 0.79 | 140 / 85 |

Flux > 0, statistics: the envelope's modulation depth is off Live's by 1.45 → 0.21 dB on average (48 cells), the long-term
spectrum 1.44 → 1.21 dB (the spectral stretcher had almost no grain modulation). Worse cells there are envelope correlation / deviation of two random draws (e.g. vowel r0.8 flux 50: env corr
0.79 → 0.59 while its modulation depth goes 0.84 → 2.43 dB against Live's 2.17).

## 4. Beats: the pitch-aware detector (beatsLaw PitchAware)

`warpengine::pitchAwareCuts` on top of the Measured law's cuts:
1. YIN pitch track (decimated to ~11 kHz, 35 ms window, 5 ms hop, 60…1000 Hz);
2. a level cut is dropped when the plain level rises < 6 dB and the pitch moves around it (a glide or vibrato, not an
   attack);
3. legato pitch steps are added: the median pitch 15…60 ms after differs from the one before by ≥ 0.8 st, both windows are
   flat (a glide still moves inside them), the change lasts (60…150 ms either side), happens within 60 ms, and the level
   holds (not a fading end); the cut is where the pitch crosses the step's middle (12 ms YIN lag removed);
4. re-articulations of a held pitched sound are added: a ≥ 12 dB dip under a steady level that is back within 60 ms.

Gate — LYNX-made labelled bank (`tests/OnsetBank190ik.h`, 32 cases, 44.1 / 48 kHz, ±30 ms, one-to-one matching):

| category (cases) | Classic F (tp/fp/fn) | Measured F | PitchAware F |
|---|---|---|---|
| drums (4) | 1.000 (63/0/0) | 1.000 (63/0/0) | 1.000 (63/0/0) |
| drumbass (4) | 0.667 (49/39/10) | 0.653 (48/40/11) | 0.697 (54/42/5) |
| plucks (4) | 0.934 (57/1/7) | 0.934 (57/1/7) | 0.938 (60/4/4) |
| chords (4) | 1.000 (28/0/0) | 1.000 (28/0/0) | 1.000 (28/0/0) |
| legato (4) | 0.167 (4/0/40) | 0.167 (4/0/40) | 0.989 (43/0/1) |
| sung (4) | 0.533 (8/0/14) | 0.533 (8/0/14) | 1.000 (22/0/0) |
| glides (4) | 1.000 (4/0/0) | 1.000 (4/0/0) | 1.000 (4/0/0) |
| lowglide (4) | 0.421 (4/11/0) | 0.421 (4/11/0) | 0.800 (4/2/0) |
| **all (32)** | **0.781** (217/51/71) | **0.777** (216/52/72) | **0.906** (278/48/10) |

Nothing worse in any category; the bass-range glides' false cuts drop from 11 to 2.

Live's own Beats cuts on the voice (from the env-0 gaps, ms of source): 4274 5966 6082 6392 6510 6558 6747 6800 6979
7032 7930. Measured finds 2 of them (4271, 7924; plus 3700); PitchAware 4 (adds 5951, 6077; plus 209 and 3700). On the
vowel PitchAware cuts at the legato step (1506; Live 1496), the step down (5505; Live 5488) and once in the glide (2664,
where YIN jumps an octave: a known gap). Effect on the W1 / W2 Beats renders vs Live: 48 cells better, 14 worse (drums
identical; voice env corr r1.5 0.40 → 0.58, r0.8 Loop Off 0.69 → 0.81; vowel r1.5 −0.20 → 0.91; worse: voice spectrum /
LSD by 0.1…0.7 dB).

## 5. Old projects (law switch)

- `WarpModeSettings::beatsLaw` gains PitchAware (2, the default); `warpLaw` (new) Classic | Measured (default Measured)
  covers Texture and Complex Pro. The `.lynx` list gets an eleventh number; every save writes the mark `warpLaw190ik`; a
  project without it loads Beats Measured (or Classic, 190gx's rule) and the Classic Texture / Complex Pro; the Clip View
  shows a law chooser only for such channels (Beats: Classic / Measured / Pitch-aware; Texture and Complex Pro: Classic /
  Measured beside the Flux / Envelope label).
- **Proof**: every W1 / W2 case rendered by the pre-190ik code and by the new code on the old laws (Beats Classic and
  Measured, Texture / Complex Pro Classic): **216 of 216 renders identical** (SHA-256 of the samples; `ana/prove.py`,
  `prove-final.txt`).
- Cache keys: Measured Beats keeps its empty key (PitchAware adds `|pa`); granular Texture `|gx<grain>f<flux>`; Complex Pro
  `|pro<formants>,<envelope>` and its own engine print; Classic Texture keeps 190fm's keys.

## 6. .als

Import: Complex Pro (6) and Texture (2) clips with a transpose render it offline with the stretch (`stretchPitch`, ±48 st;
the clip plays untransposed), as Beats did; Formants / Envelope / Flux play (the "kept for export, not rendered" report
lines are gone). Export writes the offline transposition back as the clip's PitchCoarse / PitchFine.

## 7. CPU (offline renders, single thread, 10 s stereo, ×1.25)

Realtime factor = render time / output length (lower is better; load average ~16 on the machine while measured):

| mode | 44.1 kHz | 48 kHz |
|---|---|---|
| Complex (unchanged) | 0.037 | 0.042 |
| Complex Pro +7 st, 190ik (transposition + formants offline) | 0.153 | 0.158 |
| Complex Pro +7 st, Classic (stretch only; the transposition then costs a real-time shifter per voice) | 0.039 | 0.051 |
| Texture, 190ik granular | 0.0043 | 0.0055 |
| Texture, Classic (spectral) | 0.046 | 0.043 |
| Beats Measured | 0.0018 | 0.0019 |
| Beats PitchAware (pitch track) | 0.013 | 0.015 |
| Tones (unchanged) | 0.034 | 0.038 |

Everything new runs in `timeStretch` (the stretch cache: loader threads / message thread), never on the audio thread; a
transposed Complex Pro or Texture clip no longer runs a real-time pitch shifter per voice (≈ 2 % of a core each).

## 8. Issue #187: warped clips follow tempo automation

Evidence (this round, `/mnt/e/lynx-work/warp190ik/t187/`): one song made by LYNX — 120 BPM, a 4 s file with a click on
each of its 8 beats warped to 8 beats (keeps pitch, Complex / FL Stretch), a tempo automation ramp 120 → 90 BPM over
the clip — exported by LYNX's own .als / .flp writers and rendered by Live 12.4 (Export, Main) and FL 2026 (`FL64 /R
/Ewav`, no input). Click times (ms) against the ramp's beats (16·ln(120 / (120 − 3.75 b)) s):

| beat | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| ramp | 508.0 | 1032.6 | 1575.0 | 2136.5 | 2718.4 | 3322.2 | 3949.8 |
| Live 12.4 | 508.0 | 1032.5 | 1575.4 | 2136.5 | 2718.3 | 3322.8 | 3949.8 |
| FL 2026 | 505.5 | 1030.0 | 1572.3 | 2133.6 | 2715.4 | 3319.0 | 3946.3 |
| LYNX before | 500 | 1000 | 1500 | 2000 | 2500 | 3000 | 3500 |
| LYNX now | 507.6 | 1032.2 | 1574.6 | 2136.2 | 2718.0 | 3321.4 | 3949.5 |

Both follow the ramp beat for beat (FL a constant ~3 ms early). LYNX before: the stretched file played at the song's
fixed tempo, so the clicks ran ahead of the beats (450 ms by beat 7). LYNX now: within 0.5 ms of LYNX's own transport
(which itself matches the ramp within 0.2 ms; `p/t187.cpp`, click peaks less the click's own 2.0 ms peak offset).

LYNX now (`Instrument::setTempoRatio`, `RackRenderer::process`): every block the renderer passes the block's tempo over
the song's (tempo automation, pattern Tempo events, a dragged or Link tempo) and whether the tempo can move. A warped
sampler / audio-clip channel (`stretchBeats > 0`) reads its warped sample that much faster or slower — a voice without a
shifter changes its speed (a resampling channel's pitch follows, as FL's Resample), a pitch-keeping channel's voices go
through their pitch shifters, which consume the source `ratio` times faster and keep the pitch; when the ratio changes the
shifter's input moves by its output latency times the change, because at another input rate the latency covers another
stretch of the source (without it the ramp drifted 12 ms early by beat 7). In a song whose tempo can
move such voices take a shifter from the note's start (even untransposed), so no voice switches mid-note. The position
and the transport use the same block tempo, so the clip stays locked to the beats; a ratio change is a block's speed
step, never a jump of the read position. At a fixed tempo the ratio is exactly 1 and nothing is called differently:
the renders with the hook on and off are identical (test).

Tests (`tests/Warp190ikTests.inc`, `issue187_*`): beat lock of all 7 beats within 6 ms under 120 → 90 (pitch-keeping and
resampling channels), warp markers (0 / 0.9 / 2.1 / 3.0 s on beats 0 / 2 / 4 / 6) landing on their beats and bar line
under the ramp, the pitch kept at 440 Hz late in a 120 → 80 ramp (the resampling channel follows the tempo down), and the
fixed-tempo A/B (bit-identical with the hook off).

## 9. Left out / live checks

- Flux between 0 and 25 (assumed linear); the grain period below Grain Size 10 is a fit on the two Grain Size 2 renders
  (no render between 2 and 10).
- Complex Pro level law: fitted on voice / vowel / chords; the vowel stays the farthest (1.2 dB on average); drums at −12
  st take the down-shift raise on their tonal frames (spectrum 3.0 → 5.2 dB).
- The Beats detector does not follow Live's cuts at vibrato / vowel changes (Live cuts there; the brief asked for glides
  and vibrato not to cut). An octave error of the pitch track can still cut a glide.
- The real-time clip pitch of Playlist clips (FL layout) on a Complex Pro / Texture channel stays the voice shifter.

## 10. Best way?

FL has no Complex Pro / Texture counterparts (its stretch modes map to LYNX's General / Transients / Mono). Live's
behaviour is matched where measured; the detector deliberately goes beyond Live (glides and vibrato do not cut) because
the brief asked for it; a project saved before 0.1.30 keeps its Measured cuts (chooser in the Clip View).

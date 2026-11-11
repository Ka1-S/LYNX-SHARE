# Round 190bb (tune) — LYNX Tune: Breathiness (issue #103)

LYNX Tune is LYNX SHARE's own pitch corrector (no FL Studio equivalent: a LYNX-own item). Issue #103 ("LYNX Tune vocal
modeling") notes that the output section already has the formant shift and the throat length, and asks for a
**breathiness** control beside them, as throat-modelling vocal processors offer. This round adds it.

Clean-room: no third-party plugin was opened, hosted, measured or decompiled for this round, and no captured data is
used. The control is described by function ("adds breath noise to the voice") and built from textbook DSP only:

- the channel vocoder (H. Dudley, "The Vocoder", Bell Labs Record 18, 1939; U. Zölzer (ed.), DAFX, ch. 9);
- aspiration noise in breathy voices: mostly above the first formants, because the harmonic source falls about
  12 dB / octave while the turbulence noise is nearly flat, and modulated at the pitch (D. Klatt, L. Klatt, "Analysis,
  synthesis, and perception of voice quality variations among female and male talkers", JASA 87(2), 1990).

## 1. What was built

| item | where |
|---|---|
| `vocal::BreathNoise`: 19 third-octave bands (250 Hz … 16 kHz, fewer at low sample rates), four cascaded band-pass sections per band; the voice's power per band is followed (1.5 ms) and an independent white noise per band, through the same band-pass, is scaled to it; tilt +6 dB / octave around 2 kHz (−18 … +12 dB); the whole noise is never let over the voice's own band power. Four bands at a time with SSE / NEON (core/Simd.h), a plain loop elsewhere — the two give the same bits | src/core/VocalPitch.h, .cpp |
| LYNX Tune parameter `Breathiness` (appended last, default 0): gain = 2·v² (half travel = −12 dB). The stage works on the corrected (wet) voice after the high shelf and before the vibrato level / drive / mix, so the breath carries the formants where Formant shift and Throat put them; it is gated by the detector's voiced decision, delayed to the output (latency − the detector window's centre), with 10 ms fades. No latency of its own | src/core/LynxTune.h, .cpp |
| At 0 the stage is skipped (nothing of it runs); a level left over fades (≈ 150 ms), then the stage resets and is skipped again | LynxTune.cpp `breathOn` |
| Editor: BREATH, the third small knob of the voice-character group (SHIFT · THROAT · BREATH) in OUTPUT; the Transpose column moves 16 px right and is 8 px narrower | src/core/PluginLayout.cpp |
| "BREATH" in the 14 language tables | resources/i18n, src/ui/i18n |
| Tests (5 tests, 58 checks) | tests/TuneBreath190bbTests.inc |

## 2. Best way?

- **FL:** FL Studio's own pitch correctors have no breath control; nothing to match (not re-checked by REA this round —
  [inferred] from the earlier LYNX Tune rounds' parameter lists in docs/PARITY.md). This is a LYNX-own extra, as the
  owner's reply on the issue says.
- **Ableton:** no stock device with a breath control that I know of ([inferred], not checked this round).
- **Users:** the request itself (issue #103): breathiness is "one of the primary features of throat modeling software".
  No web research was done this round.
- **Options considered:** (a) noise through a per-frame LPC all-pole filter of the voice — the sharpest envelope, but
  its order depends on the sample rate (46 poles at 44.1 kHz, impractical at 192 kHz) and a frame analysis adds a
  block structure; (b) the channel vocoder used here — rate-independent, always stable, no frames or latency, and the
  short followers give the pitch-synchronous bursts for free; its price is third-octave resolution (§3);
  (c) multiplying the high-passed voice by noise — cheapest, but the noise then has no formants.
- **Verdict:** (b). Additive only: the harmonic part is not changed (a real breathy voice also loses upper harmonics —
  stage 2 below).

## 3. Measurements

Harness: `/mnt/e/lynx-work/iss190bb/tune/bench.sh hash|cpu|breath|formant` (tunebench.cpp against LynxTune.o +
VocalPitch.o built from the base 93c2aa774 and from the branch, g++ 15 -O2 -ffp-contract=off, no LTO) and
`mini-test.sh` (the round's tests). All [RUN] on this machine while other builds were running (load ≈ 33: the CPU
figures are best-of-40 and still noisy).

**Old projects sound the same.** 39 renders (13 settings — defaults, retune 0, ±12 st, formant shift, throat 72 /
150 %, formant preserve off, low latency, classic + drive, created vibrato, mix 50 % + trim, low male + in-scale —
× 44.1 / 48 / 96 kHz; each over five signals, stereo and mono, block sizes 128 / 256 / 441): the hash of every output
sample is identical between base and branch (`ALL a41b1aeb12d619ab`).

**The breath follows the formants** (synthetic vowels a / i / u at 110 and 220 Hz, cascade of five formants; noise =
output at 100 % − output at 0 %; third-octave bands that hold a harmonic and lie within 40 dB of the strongest band):

- bands at a formant peak: noise − voice − tilt = −2.4 … +3.5 dB;
- valleys beside a strong formant fill: up to +16 dB (a 1.6 kHz valley between formants at 1.1 and 2.4 kHz), and a weak
  third formant about an octave above a strong second one reads up to +11 dB: the bank spreads a tone by about
  −25 dB per octave upward (analysis leak × synthesis leak × tilt). Per vowel: mean offset +2.0 … +5.2 dB, rms
  deviation from it 2.2 … 8.6 dB, worst band 13.0 dB;
- on the tests' own vowels (three parallel formants) the bands centred on each formant are within 2.6 dB.

**Level** (voice-to-breath, dB, overall / above 2 kHz): 25 %: 26 … 39 / 14 … 16; 50 %: 14 … 27 / 2 … 4; 75 %: 7 … 20 /
−3 … −5; 100 %: 2 … 15 / −8 … −10. Monotonic on all six; 50 → 100 % is +12.0 dB. [model]: the law and the full level
are LYNX's own choice, not fitted to anything, and not judged by ear.

**Unvoiced / silence:** white noise in (never voiced): nothing added (−332 dB); a consonant after a vowel: < −40 dB
after 90 ms; silence in: output exactly 0.

**CPU** (stereo, ns per sample frame → % of one core in real time):

| | base | branch, 0 % | branch, 50 % | branch, 100 % |
|---|---|---|---|---|
| 44.1 kHz | 729 (3.22 %) | 711 (3.13 %) | 890 (3.93 %) | 1094 (4.82 %) |
| 96 kHz | 663 (6.37 %) | 740 (7.11 %) | 912 (8.76 %) | 1046 (10.04 %) |

The stage alone: 126 ns per sample (0.55 % of a core at 44.1 kHz; the cost does not depend on the control's value —
the 50 / 100 % columns differ only by the machine's load). An earlier run of the same binaries gave 658 / 639 / 835 /
755 ns at 44.1 kHz, so in context the stage read as +115 … +380 ns per frame (+0.5 … +1.7 % of a core). Base vs branch
at 0 % is within that run-to-run noise (the default path gained two comparisons per block). These are -O2 non-LTO
harness numbers under load, not release-build numbers; latency unchanged (the stage has none).

**The shifter's formant preservation** (the engine as it was — nothing changed here; asked for with this issue).
Envelope sampled at the harmonics, 200 … 5000 Hz, mean level removed, output vs input, rms dB (the same measure
between the input and the vowel synthesised directly at the target pitch = the method's own floor):

| shift | LYNX Tune | floor | formant preserve off |
|---|---|---|---|
| ±12 st (12 cases) | 0.00 … 1.22 (mean 0.29) | 0.00 … 1.18 | 12 … 42 |
| ±7 st (12 cases) | 0.66 … 4.04 (mean 2.0) | 0.48 … 2.08 | 8 … 28 |

An octave is the easy case for this measure (the harmonics of one are a subset of the other's); ±7 st needs
interpolation between harmonics, so the floor itself is 0.5 … 2 dB. Worst case: a 220 Hz "a" at ±7 st, 3.5 … 4.0 dB
rms (max 11 dB at one harmonic).

## 4. Not verified

- The editor was rendered once, headless (cloud Windows round build of 9d83544fe, run 42974468158, `round build:
  success`; `LYNX.exe --parityshot` with Breathiness at 60 %: /mnt/e/lynx-work/iss190bb/tune/shots/lynx-tune-breath.png):
  SHIFT · THROAT · BREATH sit side by side, the Transpose column beside them, nothing overlaps. Only at 100 % scale
  and the default theme; not driven by hand (no drag / automation / preset round trip in the app); the existing test
  `lynx_tune_editor_layout_round95` was not run here.
- Only the round's own tests ran (mini harness). The full suite — in particular FxAudit173d / CtlAudit (every control
  must change the render) and the Windows build — is left to the rollup.
- Nothing was judged by ear; real singing was not used (synthetic vowels only).
- ARM64: the sse2neon path was compiled (zig c++ -target aarch64-macos) but not run; on x86 the scalar path was
  compared with the SSE path (identical bits, 44.1 and 96 kHz). zig c++ (clang 18) also compiles both files clean for
  Linux and Windows.

## 4b. After the first CI run (run 42979078294 on fa5025569)

Two failures in `fxaudit173d_effect_controls_072`, both fixed:

1. **"B dir" changes nothing** (all three Core jobs). [code read] tools/FxAudit.h draws a random context ("rand:k")
   one value per control in the controls' order — the editor's items, then the parameters it doesn't show. The new
   knob sat before most of them, so every later control got the next one's draw and the recorded `"B dir" → rand:3`
   (tests/FxAudit173dKnown.inc) no longer made B an illegal sung note. Fix: `fxaudit::surveyedParamCount (kind)` —
   parameters added since the survey draw after all the others, so every recorded context of LYNX Tune (B dir rand:3,
   C# rand:0, D dir rand:5) has the settings it had, plus a random Breathiness. No known key was edited.
2. **NaN, macOS arm64 only.** [inferred from third_party/sse2neon/sse2neon.h:2678, not run on ARM here] with
   SSE2NEON_PRECISE_SQRT (core/Simd.h) `_mm_sqrt_ps` is a reciprocal-square-root estimate plus two Newton steps; for
   an input in the denormal range (a band's power where the voice has next to nothing) the squared estimate
   overflows and the result is inf / NaN. Fix: the stage takes its square root with `vsqrtq_f32` on ARM64 (exactly
   rounded, as `sqrtps` on x86 and the scalar path). On top, by construction: a non-finite or absurd voice sample or
   gain never enters the bank and a non-finite sample never leaves it. The audit test now prints which control gave a
   NaN, and the round's tests cover the denormal range (not silenced, finite).

The 39 hashes (still `ALL a41b1aeb12d619ab`) and the SSE / scalar comparison were run again after the fix.

## 5. Stage 2 (not built)

1. Leak compensation in the band analysis (subtract the neighbours' known leak) or a steeper bank, to keep formant
   valleys (≈ 7 dB better an octave away), measured the same way.
2. A breathy voice also loses upper harmonics: an optional harmonic tilt tied to the control (changes the voice, so
   it needs listening tests on real takes).
3. A whisper end of the travel (the harmonic part faded out, noise only) and breath on voiced consonants / note
   onsets (a breath "attack").
4. Per-note breath from the pitch editor (Newtone), and an import mapping if a translated third-party state ever
   carries a breath field (none of the states LYNX translates today does).
5. Tuning the law by ear on real vocals; a factory preset or two.

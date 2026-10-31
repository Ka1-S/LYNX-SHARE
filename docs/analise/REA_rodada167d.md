# REA round 167d — Kepler Exo on FL's own engine (v0.1.22)

Goal: Kepler Exo (FL 2026) played by LYNX on Kepler's engine port, clean-room from REA on FL's own
`Plugins/Fruity/Generators/Kepler Exo/engine_x64.dll` (SHA-256 `10ed2464…25d6c`), bit-exact or as close as measured;
"Kepler-2" presets / .flp import; editor in LYNX style. Data: `/mnt/e/lynx-work/exo167` (`xeng/` 188 factory engine
states, `run/kp.exe` EXEC harness from round 165g, `gh/exo_all.c` full Ghidra decompile of the Exo engine, `py/xcmp.py`
harness-vs-LYNX comparison, `py/xbat.py` / `py/xbatk.py` batteries, `run/f1test.c` / `run/adaat.c` unit harnesses that
call the DLL's own routines). Evidence tags: [DECOMP] decompile/disassembly of FL's DLL, [EXEC] the unmodified DLL
driven by our harness, [DATA] FL's factory presets.

| item | the brief | FL evidence | LYNX before | LYNX now |
|---|---|---|---|---|
| engine | "implement it on FL's own engine" | render `FUN_18001b7d0` = Kepler's `FUN_18001b940` + Exo blocks [DECOMP] | Kepler-1 only; Kepler Exo channels imported empty | Kepler-2 states on the same port: voice part bit-exact on 188 − 34 factory states, 186 below −90 dB (§1–§3); with every effect on 119 bit-exact, 175 below −90 dB (§5.4) |
| effects | "bit-identical or as close as measured" | §5 [DECOMP][EXEC] | none | saturation (both modes), chorus noise / gate, EQ, hyper chorus, delay and reverb, each verified against the DLL's own routines and in the engine (§5) |
| presets / .flp | "presets/.flp import" | plugin name "Kepler Exo", state "Kepler-2" [DATA] | not imported | InstrumentKind::keplerExo from .flp / .fst with the user's DLL (§6) |
| editor | "editor in LYNX style" | — | — | the channel's knob panel with 20 Exo parameters (§6) |

## 1. State and parameter set-up [DECOMP loader 0x18001efd0, defaults 0x18001d790, reset 0x18001ec40]
- "Kepler-2" state: u32 version at 0x19, u32 n at 0x1D, n floats (the 0x102-entry parameter list), then u32 matrix
  flag; flag 1 → u32 count and count (source, destination) int pairs (8 slots); version > 2 adds bytes (arp restart
  aligned, ×4 oversampling; version > 3 the alternative-oscillator byte). FL 2026 saves version 4 with 155 values (the
  harness's save of a new instance). States with ×4 oversampling or the alternative oscillator on are refused (none in
  FL's data).
- Life of the plug-in: constructor at 44.1 kHz with host values 0 → defaults reset (every value from the default table
  forwarded, velocity → volume 1, DCO1 sub phase 0.5, chorus noise 0) → state load (reset again, values forwarded,
  slots dirty; version < 2: an LFO1/LFO2 rate with BPM sync rewritten in the stored host value only) → host rate.
- The conversion constant of the old BPM-synced rate law is 0.020673936 in both DLLs (Kepler's and Exo's: byte search
  of the float) [DECOMP]; LYNX had 0.0206739 (round 33's rounded print) — fixed for Kepler too (it only touches states
  with version < 2 and BPM sync).
- Forwarder: base[i] = v; skipped while a matrix slot with a source drives i; else the global setter
  (`FUN_1800198a0`), which always ends with the 8 voice setters (`FUN_1800228a0`).

## 2. Modulation matrix [DECOMP 0x180019090 compile, vtables 0x1800eacb8 (global) / 0x1800eae58 (voice)]
- Compiled at the block start when dirty (after the first note-on). One entry per destination (first slot order),
  each slot to it a term; voice destinations = everything the global list does not hold (plus 0x5c…0x63).
- Voice matrix: every 0.001·fs samples and at note start; global matrix every 0x13c4 (= 0.001·fs) samples with the
  sources copied from the newest note's voice; value = clamp01(base + Σ source·amount), amounts 2·P − 1.
- A voice's "released" source starts at 1 (voice constructor `FUN_180021ee0` writes 1.0 at +0x2e64; note start
  `FUN_1800222e0` clears it) — LYNX started it at 0: presets routing "released" (Stickyfifth) diverged on the second
  note (−4.6 dB → −137 dB).
- The voice matrix also runs on idle voices, so their source values matter before a note starts. Read from the DLL's
  idle voice (source getter `FUN_180022650`, run under EXEC): key − 0.5 (+0x2e80) = 0.5 and toggle (+0x11cc) = 0.5,
  where LYNX had 0. Presets routing key → resonance (Twinklikeys) or toggle → cutoff (Brightsweep) went from −28 / −35 dB
  to bit-exact.

## 3. Voice and global DSP added for Exo [DECOMP]
DCO2 with hard sync (`FUN_180014f80` restart path), DCO1↔DCO2 cross-modulation shapes, FM shapes, VCA source
(Env1 / gate / Env2), LFO2 (rate 10^(4x)·0.1/fs, shapes `FUN_1800140c0`, its own LCG 55555, BPM-sync kappa), the arp
modes 3–8 (random with its LCG 1793462892, played order), the Juno-index option (0x5b), mono note-on/off
(`FUN_180018720` / `FUN_1800189b0`), glide, octave transpose, X/Y offsets.

The voice's FM / cross-modulation sine shape and LFO2's sine call the library's `sinf` (`FUN_1800c2a70`, re-created
as `ildsp::sinf_`, exact in both rounding modes); LYNX used the correctly rounded sine, 1 ulp apart on some inputs:
presets with filter FM on the sine shape (30 of the 36 non-exact ones) were −130 dB off, now bit-exact.

Battery without effects (`py/xbat.py nofx`: 3 overlapping notes, 66150 samples, blocks of 512, EQ/delay/reverb/
saturation/hyper chorus/chorus noise off) [EXEC]: **152 of 188 bit-exact, 186 below −90 dB** before the sinf fix
(every probed filter-FM preset exact after it); open: Unsettling 2 (−84 dB, LFO2 → cross-mod) and Obkeys (−34 dB with
the effects zeroed, bit-exact with its own effects on).

## 4. The library's float routines (the saturation needs their last bits) [DECOMP][EXEC]
The HQ saturation divides ln(cosh) differences by a step ≥ 10⁻³, so 1-ulp differences of the C runtime become
−70 dB errors. Re-created step by step and checked against the DLL's own routines called by our harness (`f1test.c`):
- `logf` 0x1800bfb90: near-1 path (|x − 1| < 1/16: atanh series in 2t) and a 129-entry table path; FMA variant when
  the CPU has FMA. Tables recomputed: 1/c, ln c split into a high part (12 low bits cleared) and a truncated low part.
- `tanhf` 0x1800c4004: odd rational below 1 (two coefficient sets split at 0.9), 1 − 2/(e^{2x} + 1) above with a
  32-entry 2^{j/32} table (high part recomputed: 14 low bits cleared; the low parts are not a rounding of anything
  simple — embedded as measured).
- `coshf` 0x1800bebf4: double cosh n·cosh f + sinh n·sinh f, degree-13/12 polynomials, 37-entry sinh n / cosh n
  tables (embedded: not the correctly rounded values), rounded in the current mode.
All three: 0 mismatches on 40 000–52 000 inputs each, in nearest and in toward-zero/FTZ/DAZ (the render's mode).

## 5. Effects [DECOMP render tail, gh/exo_all.c 18270–18560]
Order after the chorus: pre-saturation → EQ (3 bands) → hyper chorus → delay → reverb → post-saturation → main
volume / pan → output gain.

### 5.1 Saturation (0x86 on, 0x87 amount, 0x88 post, 0x98 HQ, default HQ on)
Drive target tan(x^{1/3}·1.55), smoothed per sample with 1 − 0.1^{20/fs}; z = clamp(drive, ±5); the tanh is the
[7/6] Padé fraction x(x⁶+378x⁴+17325x²+135135)/(28x⁶+3150x⁴+62370x²+135135) (constants filled at load from .rdata
0x1800edf50…); y = x·3·drive; normal mode: Padé(clamp y)/(3·Padé(z)); HQ: first-order antiderivative anti-aliasing
(`FUN_180021e00`: ln(cosh y / cosh y₋₁)/(y − y₋₁), tanh of the midpoint below a step of 10⁻³) × 1/(3·Padé(z)).
LYNX: battery with only the saturation on (81 states): 67 bit-exact, the other 14 between −77 and −96 dB — the
same states differ by −130 dB without the saturation (§3), the drive amplifies that [EXEC].

### 5.2 Chorus noise (0x46) and gate (0x47) [DECOMP `FUN_180001a60`, designer `FUN_180002690`]
Noise gain x²·(x/4)·0.0125 at chorus+0x18c, gate threshold 8× that at +0x1e0. With noise > 0: two LCG noises (seeds
from a library-wide counter: 333784 / 401782 for the first instance) through peaking biquads (6.5 kHz, Q 0.25, gain
2·LFO + 1, redesigned every 16 samples) added to the wet taps (after the feedback copy). Gate: output ≥ threshold →
hold 440 samples at gain 1, then gain × 0.99965 per sample.

- The chorus LFO's sine is the library's `sinf` too (call 0x1800c2a70 in `FUN_180001a60`); Kepler keeps its measured
  correctly rounded one. Noise biquad order from the disassembly: ((y1·a1 + a2·y2) + b2·x2) + (x·b0 + x1·b1). The
  object's counter reads 16 before its first sample (EXEC, chorus+0x238), so the first noise sample designs the
  filters (LYNX started at 0: 16 samples without noise). Battery keep=46 (14 states): 7 bit-exact, the rest −138 to
  −178 dB.

### 5.3 EQ, delay, reverb, hyper chorus [DECOMP][EXEC]
Ported as `src/core/KeplerExoEq.h`, `KeplerExoDelay.h`, `KeplerExoReverb.h`, `KeplerExoChorus.h`, each checked
float-for-float against the DLL's own functions called by our harnesses (`fxa/`, `fxb/`):
- EQ (flag 0x64, bands 0x65…0x6d; container `FUN_180006a50`, band setter `FUN_180006950` → designer `FUN_180004320`
  kinds 5/6/8, biquad inline at 0x18001c7a3): 0 mismatches in 60 000 band designs and 3 × 20 000 parameter steps
  (nearest; the setters run outside the render).
- Delay (0x6e…0x78; wrapper synth+0x1bdf8 `FUN_1800107b0`, ILDelay3 `FUN_180010b30` / `FUN_180011210`): 13 scenarios,
  6.8 M floats, 0 mismatches. The synth's rate set-up also writes the wrapper's update interval (int)(fs·0.002)
  (0x18001d4c7): the first time / offset crossfade starts at sample 88 at 44.1 kHz.
- Reverb (0x79…0x85; fxReverb synth+0x1c290, process `FUN_18000d630`): the class of LYNX's `ilfx::ReverbT` with two
  differences — parameter 4 is a feedback gain on the pre-delay line, and only the first early-reflection tap per
  side has a gain. Hyper chorus (0x89…0x94; wrapper synth+0x1cc78, banks `FUN_18000f3a0` / `FUN_18000f880`, initial
  LFO phases from the C runtime's `rand()`, 16 calls). 50 scenarios, 7.7 M floats, 0 mismatches.

### 5.4 The whole engine
Batteries against the DLL (3 overlapping notes, 66150 samples) with only one effect kept on (`py/xbatk.py`): EQ 134 of
170 bit-exact, 153 below −90 dB; delay 86 of 105, 104 below −90 dB; reverb 70 of 153, 151 below −90 dB; hyper chorus
116 of 142, 134 below −90 dB. All factory states with their own effects (`py/xbat.py`): **119 of 188 bit-exact, 175
below −90 dB**; the 13 others (−70 to −89 dB) are residuals of −137 to −178 dB (reverb, chorus noise) that the HQ
saturation's antiderivative step (a division by a step ≥ 10⁻³) amplifies by ~60 dB. Test
`kepler_exo_renders_like_the_dll`: the state FL 2026 saves for a new Kepler Exo (version 4, 155 values) with every
effect on and a matrix slot, bit-exact (energy and samples) against the DLL.

## 6. LYNX integration
- `InstrumentKind::keplerExo` ("keplerexo" in projects), the same `makeKeplerInstrument()` (the state decides the
  engine; a Kepler-2 state with Kepler's DLL or the reverse does not build).
- .flp / .fst import: plugin "Kepler Exo" → keplerExo with `%FLStudioFactoryData%\Plugins\Fruity\Generators\Kepler
  Exo\engine_x64.dll` when the state and the DLL are usable; no synth conversion exists (the channel stays empty
  otherwise). New channels: "Kepler Exo" in the native engine list (from the user's FL Default.fst), FL preset menu.
- Editor: the channel settings knob panel (Volume, Cutoff, Resonance, Env amount, ADSR, DCO2 level/detune, LFO1/2
  rate, Portamento, High-pass, Saturation, Delay amount/feedback, Reverb decay/wet, Hyper chorus mix).

## Where I stopped
- 2026-10-31 17:30 — all effects wired; batteries above; golden test added. Open: the reverb's in-engine −137 dB
  residual on some states (being checked), Unsettling 2, Obkeys with zeroed effects; tests + Windows build, landing.

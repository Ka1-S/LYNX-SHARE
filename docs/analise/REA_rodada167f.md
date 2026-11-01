# REA round 167f (flgap167) — FL plugins and engines LYNX SHARE still lacks

Date: 2026-10-31. Gap table: [`REA_rodada167f-gaps.md`](REA_rodada167f-gaps.md). Clean-room: FL Studio's own plug-in
DLLs are studied with Ghidra (headless, one instance machine-wide, `-max-cpu 2`) and run unmodified in our own harness
(round 154's `fx.c`); this document describes their behaviour in our own words.

Markers: **[DECOMP]** Ghidra, **[EXEC]** the DLL in our harness, **[DATA]** project / preset files, **[HYP]** not verified.

## 1. Emphasis

FL's Emphasis (`Plugins/Fruity/Effects/Emphasis/Emphasis_x64.dll`, SHA-256 `e64ca492…5e48`) is a Delphi front end over
the class `Limiter` of `Shared/ildsp_x64.dll` (SHA-256 `b96354e1…1760`; RTTI `LimiterImpl` → `Limiter`, created by the
export `Limiter_CreateInstance`, object size 0xA02A80 bytes) **[DECOMP]**. Emphasizer (round 154) uses the small
sibling `LimiterLite` of the same library.

### 1.1 Module structure **[DECOMP]**

The engine is built from the library's small DSP classes (all with the same interface: set rate, process a block,
prepare(rate, max block), reset): `PeakHold1`, `PeakHold8` (8 cascaded peak holds), `ExpSmoother1`, `ExpSmoother` (n
cascaded attack/release one-poles, n = the envelope order), `ExpSmoother8`, `GRStatic1` / `GRStatic8` (peak hold →
max(·, 1) → smoother → gain = min(1/x, 1)), `GRAdaptiveRel` (program-dependent release), `DelayInt` / `DelayLerp`
(2^18-sample rings), `Lowpass1` / `Highpass1` (one-pole), polyphase IIR half-band up/down samplers for 2×/4×/8×/16×
(two all-pass chains, output = (A + B)/2) and a true-peak estimator (polyphase FIR upsampling, max |·| over the
phases). Per channel (L/R or M/S) the object holds one instance of each detector chain.


### 1.2 Engine specification

What follows is our own description of the `Limiter` engine. It covers what the library does per block, in order,
with the formulas and constants. The port (`src/core/EmphasisEngine.{h,cpp}`) was written from it. We checked the
port against the unmodified library in our own harness (`tmp/flgap167/run/limh.c`, `cmp/matrix.py`) and the output
is **bit-identical** in every case we ran (2026-10-31: 26 states, 106 systematic, 350 random, 120 forced-plain-math,
2 long and 84 degenerate-input cases at 44.1/48/96 kHz, blocks 1…2048, mid-stream changes) **[EXEC]**. Structure
and constants **[DECOMP]**. The semantics of the extra parameters are inferred from the code and were confirmed by
running it **[EXEC]**.

**Numerics.** Everything runs in single precision with no fused multiply-add, in the operation order given here.
Some coefficients use the library's own double `exp`; the per-sample `ln` uses its single-precision `logf` (both are
ported in `IldspMath.h`). `expf` and `powf` are the round-60 ports. FTZ/DAZ are on (as in FL). Min / max follow the
SSE rule (`max(a, b)` gives `b` when either operand is NaN). This matters in two places (see the analysis and level
matching steps). Below, "one-pole low-pass at f" means `s = (s − x)·c + x` with `c = exp(−2π·f/sr)` (double exp,
rounded to float). An "attack/release smoother" uses the attack coefficient when the state is below the input and
the release coefficient otherwise. `F(x)` is truncation to an integer.

**Parameters** (index: meaning, how it is stored): 0 bypass (>0.5); 1 delta (rounded, deferred); 2 envelope order − 1
(rounded); 3 true peak (deferred); 4 M/S (deferred); 5 mode (rounded: 0 versatile, 1 loud, 2 transient, 3 steady);
6 input gain dB; 7 stereo linking 0…1; 8 emphasis 0…1; 9 output ceiling dB; 10 level matching (rounded);
11 oversampling 0…4 (rounded, deferred; factor `OSf = 2^p11`); 12 hardness 0…9 (rounded); 13 loudness meter off
(>0.5, meter only, no effect on the audio); 25 look-ahead off (deferred); 26 instant recovery (>0.5, deferred);
27 DC block (>0.5). Parameters 19, 20 and 22 are internal. The emphasis setter derives them: 19 is the release shape,
20 the attack shape, 22 the knee amount. Constructor values: p7 = p8 = 1, p11 = 2, p12 = 7, p26 = p27 = 1, rest 0;
ceiling gain 0.99966854.

**Setters.**
- *6 (gain g dB):* `Gin = expf(g·0.11512925)` (= 10^(g/20)). The release detector gets `thr = g` and
  `scale = expf(−g·0.11512925)`. Then 9 is re-applied.
- *9 (ceiling c dB):* without delta, `Gout = min(expf(c·0.11512925), 0.99966854)`. With delta, `Gout = 1/Gin`, or 1
  when level matching is on. 1 and 10 re-apply 9.
- *12 (hardness h):* `Ttot = powf(2, −h)·0.032`, `Tm = min(Ttot, 0.008)`, `F1 = floor(sr·Ttot)`, `F2 = floor(sr·0.05)`.
  - main look-ahead: `F2 − F1` samples.
  - oversampled look-ahead: `F1·OSf` samples at the oversampled rate. The OSf current at this moment is used: a
    later change of 11 alone keeps the old value.
  - oversampled peak limiter: hold `Ttot`, attack `A[h]·Ttot` with `A = {3, 2, 1.5, 1.25, 1.125, 1.0625, 1.03125,
    1.0156, 1.0078, 1.0039}` (≈ 1 + 2^(1−h)), release `Ttot`.
  - M/S limiter and its delay: time `Tm`.
- *4 (M/S on):* sets the M/S limiter and delay to `0.032·2^−clamp(h, 2, 9)` (0 at h = 9). Off: 8 ms. Whichever of 4
  and 12 runs last wins; the re-send order makes it 12.
- *11:* `OSf = 2^p11`. The oversampled limiter and its look-ahead get rate `max(OSf·sr, 44100)`.
- *5:* "low modes" flag = (mode ≤ 1). Then 8 and 6 are re-applied.
- *8 (emphasis e):*

  | mode | 22 (knee amount) | 20 (attack shape) | 19 (release shape) |
  |---|---|---|---|
  | 0 | 0 | 1 | 1 |
  | 1 | e | 0 | 1 |
  | 2 | 0 | e | e |
  | 3 | e | 0 | e |

- *20 (v):* `E = expf((0.8v + 0.2)·ln 10)`. Release-detector attack `Ta = (1 − E)·(−1/9)·10·(2^(1/n) − 1) + 0.05`
  (n = envelope order), in seconds. The constant is stored rounded (0x1.c71c72p-4).
- *19 (v):* `x = 1 − v`, squared in modes 2/3. `w = 2.5x² + 1`. Release times `R1 = 0.15w` and `R2 = 0.6w²`.
- *22 (v):* knee ratio `Kr = 1 − ln(10v + 1)/ln 11` (constant 0.41703). Knee threshold `Kt = (Gin − 1)·Kr + 1`.
- *2:* sets the cascade order n of the release detector and copies stage 0 to the others. Then 8 and 26 are re-applied.
- *26:* release multiplier `M = 10/(p2 + 1)`. Re-applies 8. Sets the detector's instant flag.

**Deferred parameters** (1, 3, 4, 11, 25, 26). While the output is audible, the value is stored and the engine fades
out. Once the fade level is below 1.19e-7 at the end of a block, it does three things: clears the delays and the
half-band states (the library's `reset`), re-sends 0…12, 25, 27, 26 (which applies the stored values), and fades back
in. While muted (also right after `prepare`), the values apply at once. Any change of 3, 4, 11, 25 or 26 recomputes
the latency. The integer dry delay is then set to it.

**Latency** (samples; equal to the dry delay):
`[p25 = 0 or p26 on ? F(F2 − F1)] + F(lookOs)/max(1, F(OSf)) + [M/S ? F(max(0, Tm·sr))] + F(max(0, 0.004·sr))`.
Here `lookOs = F1·OSf` is as stored by 12. In the usual full re-send order (12 after 11), the term is F(F1·OSf)/OSf.
Examples: 44.1 kHz default 2381, 48 kHz 2592.

**Process** (per block of n ≤ max; channel arrays L/R):
1. *DC block (27):* one-pole high-pass at 5 Hz on the input, in place. The output is `x − s`. Every 4th sample of the
   unrolled loop is computed as `−((s − x)·c)`, which rounds differently.
2. *Analysis (modes 0/1 only).* Per channel:
   - `lo` = low-pass at 320 Hz, `hi = x − lo`.
   - `El` = low-pass at 5 Hz of `lo²`, `Eh` = the same of `hi²`.
   - `r = √El / max(√Eh, 1.19e-7)`.
   - `a = k·(1 − T(r))` and `t = T(a)`, with k = 10 (mode 0) or 40 (mode 1). T is the [7/6] Padé tanh
     `x(x⁶ + 378x⁴ + 17325x² + 135135)/(28x⁶ + 3150x⁴ + 62370x² + 135135)`.
   - `m = min(t_L, t_R)` (NaN keeps t_L). "Busy" = both channels' oversampled-limiter activity (previous block) > 0.9.
   - Attack candidate `max(√m·Ta, floor)`, with floor 0.05 when instant recovery is on, else 0.5. A NaN from m < 0
     gives the floor.
   - The attack follows the candidate (or `Ta` when not busy) through a 1 kHz low-pass.
   - A 50 ms smoother tracks `max(m, 1e-6)` (or 1 when not busy) into a balance value b ≥ 0.

   At the end of the block, mode 0 sets the detector attack to the smoothed value and sets 22 to
   `√(√m·min(1, (24 − gain)/24))`. Mode 1 sets 22 to `b^(1/4)`.
3. *Dry path:* integer delay by the latency. When delta is on and OSf > 1 it also goes through the same up/down
   half-band chain as the main path, so the phases match.
4. *Input gain:* L/R gets `x·LP20(Gin)`. M/S gets `(L ± R)·LP20(Gin/2)`.
5. *Detector:* `d = |y|`, then linking `d = (max(dL, dR) − d)·link + d`, then the knee: above `Kt`,
   `d = (d − Kt)·Kr + Kt`.
6. *Release detector* (per channel), first part:
   - peak hold of 8 stages, each 0.05/8 s (a stage restarts when the input reaches its value or its count reaches
     the length).
   - Per sample, the release time is 5 ms with instant recovery on. Otherwise:
     - `s` = 20 Hz low-pass of `d·scale`.
     - `q = max(s, ε)/max(g, ε)`, where g is the previous output gain and ε = 1.19e-7.
     - `l1 = 20·log10 q` and `l2 = 20·log10 g`, both through `ln` × 8.6858896.
     - `k2 = −0.33·thr`.
     - multiplier `1 + M·√|l2 − k2|` when `l2 < k2` or `l1 < −thr/2`, else 1.
     - release = (R1 when `l1 > 6 − thr` and the previous first-stage gain is below the one before it; else R2) × the
       multiplier.
7. *Release detector*, second part:
   - the input `max(d, 1)` goes through the attack/release cascade of order n. Each stage's coefficient is
     `expf(−2π·k/(sr·T))`, with `k = 1/√(2^(1/n) − 1)`. Order 1 is a single one-pole with double-exp coefficients,
     and its release coefficient is recomputed every sample.
   - gain = `min(1/max(last stage, ε), 1)`.
8. *Look-ahead:* when p25 = 0 or instant recovery is on, `y` goes through the interpolating delay `F2 − F1`. The
   delay length glides toward the target with a 5 Hz one-pole and snaps to the integer within 0.1 %. Linear
   interpolation toward the next newer sample. The glide starts at 0 after construction. Then `y = gain·y`.
9. *Oversampled peak limiter:* used when OSf > 1. Up-sample ×OSf with half-band polyphase IIRs:
   - Stage cascades: 2x [12 coefficients]; 4x [12, 6]; 8x [12, 6, 4]; 16x [12, 6, 4, 4].
   - Each stage has two all-pass chains of `v = (v − y2)·c + x2` sections. The output is `(A(x) + B(previous x))/2`.
   - Input is scaled by OSf (2x: x + x). Each stage runs on the sample and then on a 0.
   - Coefficients are in `EmphasisEngine.cpp`; they are the DLL's .rdata values.

   Then `|·|` goes through the 8-stage limiter (peak hold 8×Ttot/8 → max(·, 1) → 8-stage smoother → `min(1/x, 1)`).
   Its smoother scale is `2π/√(2^(1/8) − 1)`. Its activity is the share of block samples where a 25 ms hold of the
   input is ≥ 1.0018. The gain of each base sample is the minimum over its OSf phases (taken from 1, in groups of 4).
   The up-sampled signal goes through the oversampled look-ahead, is multiplied by the gain, and is down-sampled (the
   stages in reverse, keeping every other output). For OSf = 1 the same runs at the base rate.
10. *M/S decode* (`L = M + S`, `R = M − S`). With M/S on, a 1-stage limiter follows (hold/attack/release `Tm` via
   set 12, double-exp coefficients), plus its delay.
11. *Final limiter:* the detector is the true peak (p3 on) or `|y|`. The 4 ms 8-stage limiter (hold, attack, release
    4 ms) follows, with a 4 ms integer delay of the audio, then the multiply.
    - True peak: a 4× polyphase FIR with 385 taps, `h[j] = sinc(j/4 − 48)·sin(πj/384)`. The argument `j/4 − 48` is
      rounded to float first. The sines need correct rounding in double; ours use double-double evaluation.
    - It is implemented as a scatter-add into a 392-float ring and outputs the max of the 4 phases. The FIR's own
      48-sample delay is not compensated.
12. *Ceiling:* `y·LP20(Gout)`.
13. *Level matching* (p10): `y·min(√LP0.125(x²)/√LP0.125(y²), 2)`. Here x is the input after the DC block (not
    delayed), and 0/0 (silence) gives 2.
14. *Delta* (p1): `y = dry − y`.
15. *Bypass:* `f` = LP20 of the bypass flag. `y = (dry − y)·f + y`. The bypassed signal keeps the latency.
16. *Fade* (not while bypassed):
    - a counter of enabled samples gives a step of 1 after 0.1 s (0 while a deferred change is waiting).
    - Two cascaded 100 Hz one-poles of the step multiply both channels. So the output is silent for the first 0.1 s
      after `prepare`.
    - "Muted" means the second pole is below 1.19e-7. A pending change is applied at the end of such a block.

**Not reproduced:** the meters (an RMS / loudness meter at 3.333 Hz and the gain-reduction readouts, disabled by
p13 > 0.5). They never touch the audio.

### 1.3 Front end **[EXEC]**

Our harness hooks the engine's vtable (prepare, setParam, process) while the unmodified plug-in runs. The front end
prepares the engine once with the host rate and a 2048-frame maximum, then sends the state's controls in the order
0…9, 11, 12, 10 and the three option bytes as engine parameters 26, 27, 25; each render call is one engine call on the
host block. Its output is the engine's output bit for bit (Default preset, 1 s of noise), so the front end adds
nothing to the audio path. Values sent (raw = the state's integer): bypass, delta, true peak, channel mode, mode,
level matching, oversampling, hardness and the options as the integer; envelope order − 1; input gain `24·raw/10000`
dB; linking and emphasis `raw/10000`; ceiling `20·log10((e^(x·ln 11) − 1)/10)` dB with `x = raw/10000` (the same
evaluation as Emphasizer's output control, round 154: bit-identical on all values we logged; −∞ at 0). The state:
`i32 version` (10; old presets 7–8), 13 `i32` controls, `i32` UI page, `i32` (v ≥ 7), then `u8` → engine 25 (v ≥ 9),
`u8` → 26, `u8` → 27 (v ≥ 10); a state without them sends 0. Before version 8 the envelope order is stored 0-based.
The panel's buttons are named LoudnessCompBtn, FastRecoveryBtn ("INSTANT RECOVERY") and DCBlockBtn ("DC BLOCK")
**[STR]**; engine 27 is the input DC blocker **[DECOMP]**.

## Where I stopped

- 2026-10-31 (3): front end mapped (§1.3, fxh.exe = fx.c + vtable hooks); LYNX side written (core/EmphasisFx,
  EffectKind::flEmphasis, editor, 15 languages, tests/Emphasis167fTests.inc with DLL goldens at 44.1/48 kHz); the engine
  port (core/EmphasisEngine, §1.2) in progress by a helper; Transporter and Wave Traveller helpers in their own
  worktrees (feature/flgap167-tp, feature/flgap167-wt).
- 2026-10-31 (2): Emphasis engine mapped (§1.1); own harness `tmp/flgap167/run/limh.c` → `/mnt/e/lynx-work/flgap167/limh.exe`
  drives `ildsp_x64.dll`'s `Limiter_CreateInstance` directly (engine params, memory dumps); the plug-in harness
  `fx.exe` (round 154) runs `emp.dll` with its states (`tmp/flgap167/states/emp`, 26 states). Decompilations in
  `tmp/flgap167/out/emp_*.c` (Ghidra project `~/ghproj/r54/ildsp`, `~/ghproj/f167/emp`).
- 2026-10-31: worktree `~/projetos/lynx-wt-flgap167` (feature/flgap167), data `/mnt/e/lynx-work/flgap167`, tools
  `~/.claude/jobs/f8789b49/tmp/flgap167`. Gap table written. Next: Ghidra on `Emphasis_x64.dll`.

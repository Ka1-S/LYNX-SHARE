# Behavioural analysis of FL Studio 2026: round 60 (REA) — LuxeVerb and Fruity Convolver: exact engines

> **Nature of this document.** Effect round in the line of `REA_rodada40.md` (Maximus) and `REA_rodada49.md`
> (Reeverb 2): replace LYNX's approximations of **LuxeVerb** and **Fruity Convolver** (both imported as the generic
> Freeverb-style `EffectKind::reverb` through the name table of `src/core/FlpImport.cpp`) with exact engines.
> *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode was copied: what is here are data
> layouts, formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve
> only to trace the evidence. No FLEX pack or encrypted content was touched; no Image-Line audio or preset data is
> stored in the repository (unit-test goldens are the DLLs' output for our own states and input; impulse responses
> are read at run time from the user's installation or from the project's own state).
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Inferences are marked as such;
> anything without evidence is **"not confirmed"**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code (`objdump -M intel` wherever the order of float operations matters) |
| **[DATA]** | checked in `.flp` files or read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying its code) |
| **[model]** | our own reference model (C++ or Python) built from DECOMP facts and compared with EXEC |
| **not confirmed** | not enough evidence in this round |

No manual page was read in this round.

- **Date:** 2026-10-15 (the first attempt was lost in a power failure before any commit; its scratch notes were reused
  and every result below was re-run). Scratch directory (paths relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r60/`
  (`cv/` holds the Convolver half).
- **Targets (LuxeVerb):**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/LuxeVerb/LuxeVerb_x64.dll` | `498d8f8dffbfda3fdb3b290e3c919d2764fdaa9729220bd5e18d7f53b0b819e6` | Delphi plugin: state, controls, the control → engine mapping, host events, render |
  | `Shared/ildsp_x64.dll` | `b96354e1ee4f096208432e9a31fd9838c9cc5cf36d4ac078f9cf5c94faaf1760` | C++ DSP library; export `Shimmer_CreateInstance` = the whole LuxeVerb engine |

- **Tools:** Ghidra 12.1.4 headless (`gh/imp.sh`, `gh/g.sh`, scripts `DecompAll.java`, `DecRange.java`); decompilations
  `out/lv_plug.c` (the plugin), `out/il_shim.c`, `out/il_shim2.c`, `out/setp.c`, `out/rate.c`, `out/proc.c` (the
  engine); `objdump -M intel` listings annotated with the constants they load (`py/ann.py`): `out/ctor.a.asm`,
  `out/rate.a.asm`, `out/setp.a.asm`, `out/proc.a.asm`, `out/pitch.a.asm`, `out/pinit.a.asm`, `out/dl.a.asm`,
  `out/hq.a.asm`, `out/m2.a.asm`, `out/reset.a.asm`, and the library's float routines `out/crt_*.a.asm`; constants
  with `py/rd.py`.
- **EXEC harnesses** (zig cc 0.14.1, x86_64-windows-gnu; same safety measures as the earlier rounds: unmodified DLLs,
  `SetErrorMode`, vectored fault handler, `TerminateProcess` at the end, `timeout`):
  - `run/fx.c` → `fx_<n>.exe`: the plugin through its FL interface with a minimal host (round 55's harness), extended
    with the host tempo event (`bpm=`, `ProcessEvent` id 0 sent after creation and after the rate set-up), the sample
    rate before the state (`pre=1`), in-place rendering (`inplace`), parameter events at sample positions (`p=n,k,v`,
    each logged as `#pe`), and an **engine-call logger** (`hook=1b8`, `hkf=<file>`): a copy of the engine object's
    virtual table, installed in the harness's own memory, forwards slots 1/2/3/5/10 to the original functions and logs
    each call with its arguments (and the input/sidechain samples of every process call). The DLLs' code is not
    touched.
  - `run/sh.c` → `sh.exe`: `ildsp_x64.dll`'s engine alone (`Shimmer_CreateInstance`, then rate / set / tempo / reset /
    process from a command file), with dumps of the engine object's memory (`dump`, `dumpp`) and calls of the
    library's float routines on our own inputs (`crt`).
- **Drivers (our own code):** `py/survey60.py` (installation states → `data/states60.pkl`), `py/fx.py`, `py/sh.py`
  (harness drivers, test signals), `py/lvparams.py` (each state's control values through the plugin's own text
  function), `py/ranges.py` (the controls' ranges read at run time), `py/mapsweep.py` + `py/mapcmp.py` (the
  control → engine mapping swept through the DLL), `py/replay.py` (plugin run with the engine-call log, replayed
  through a model), `py/cmpm.py`, `py/bis.py`, `py/dbg.py`, `py/bufcmp.py` (model ↔ engine memory comparisons,
  bisection of the first differing sample), `py/t_crt.py` + `cpp/crtcmp.cpp` (float routines), `py/e2e.py`,
  `py/e2erand.py`, `py/e2eauto.py` (LYNX ↔ DLL end to end), `py/gold.py` (unit-test goldens). Models:
  `cpp/lvmodel.cpp` (a byte-for-byte layout model of the engine object, used to localise differences against memory
  dumps), `cpp/ilmath.h` (float routines); LYNX's own engine is driven by `lx/lxrun.cpp` (engine core, the logged
  command language) and `lx/lxfx.cpp` (the whole effect from an FL state).

---

## 1. Where LuxeVerb's DSP lives **[DECOMP + EXEC]**

The plugin is a Delphi host. Its constructor (`FUN_0072df10`) resolves `Shimmer_CreateInstance` in
`Shared/ildsp_x64.dll` (`FUN_0072b710`) and keeps the returned object; every sample of audio is produced by that
object. The object's interface (virtual table, observed with the call logger and in `out/il_shim.c`):

| Slot | Call | Notes |
|---|---|---|
| 1 | sample rate (float) | (re)initialises everything (§4.1) and clears the lines |
| 2 | set parameter (index 0…48, float) | §3.2 / §4.3 |
| 3 | tempo (float BPM) | stored for the synced delays (the engine's own field and its three pitch shifters) |
| 4 | meter read (channel) | the plugin's peak display; no effect on the audio |
| 5 | process (frames, input, sidechain, output) | interleaved stereo, §4.2 |
| 10 | reset | clears lines, filter states and the feedback shifter |

The plugin adds nothing to the audio: with the logger, the plugin's output equals the engine's output for the logged
calls and samples (23/23 installation states bit-identical, `py/replay.py`, §6).

## 2. State and controls **[DECOMP `FUN_0072f610`, `FUN_0072f570`; DATA; EXEC]**

### 2.1 Layout

All little-endian: `i32 version` (7, 8 or 9 seen/written; < 10 accepted), `i32 sidechain input` (0 = none; the
plugin asks the host for that input buffer at render time, §3.4), five bytes — *predelay sync*, *feedback delay sync*
and three display switches (spectrum view options; drawing only) — then the controls as `i32` each: **38** below
version 8, **39** in version 8, **49** from version 9, which adds two bytes: *feed-forward delay 1 sync* and
*feed-forward delay 2 sync*. Saving writes version 9.

Before reading an older state the plugin sets controls 39…48 to 0, 0, 24, 20, 1719, 20, 1000, 192, 1000, 192 (and
control 38 to 1 below version 8). The two feed-forward sync flags are **not read** for versions < 9: the plugin then
applies whatever its stack held (EXEC: differs run to run); LYNX uses "off" (**not confirmed** what FL ends up with —
it only matters when such a state switches to the feed-forward pitch type later).

Installation survey (`py/survey60.py`): 23 LuxeVerb mixer slots in 8 projects (7 demo projects), 23 distinct states,
13 of version 7 and 10 of version 8.

### 2.2 Controls

Ranges read at run time from the plugin's controls (`py/ranges.py`); defaults are a freshly created plugin's values
(EXEC, no state).

| # | Control | Range | Default | # | Control | Range | Default |
|---|---|---|---|---|---|---|---|
| 0 | input gain (dB) | −600…1500 | 746 | 25 | envelope attack | 50…1000 | 50 |
| 1 | input high cut | 40…20000 | 12991 | 26 | envelope decay | 50…1000 | 400 |
| 2 | input low cut | 20…5000 | 20 | 27 | envelope offset | 0…100 | 50 |
| 3 | reverb on | 0…1 | 1 | 28 | envelope mode (off / → wet / → decay) | 0…2 | 1 |
| 4 | decay | 250…20000 | 13105 | 29 | envelope sidechain | 0…1 | 0 |
| 5 | predelay | 0…5000 | 1 | 30 | envelope low cut | 20…2000 | 301 |
| 6 | predelay (synced) | 1…768 | 192 | 31 | envelope smoothing | 10…1000 | 10 |
| 7 | brightness | 0…1000 | 850 | 32 | dry level | −600…1500 | 746 |
| 8 | character | 0…100 | 50 | 33 | wet level | −600…1500 | 746 |
| 9 | size | 1…100 | 20 | 34 | peak frequency | 40…10000 | 7726 |
| 10 | mod frequency | 0…1000 | 100 | 35 | peak gain | −1800…1800 | 0 |
| 11 | mod amount | 0…100 | 10 | 36 | peak Q | 25…5000 | 1859 |
| 12 | diffusion | 0…1000 | 1000 | 37 | stereo width | 0…200 | 125 |
| 13 | freeze mode (normal / freeze / sustain) | 0…2 | 0 | 38 | limiter | 0…1 | 1 |
| 14 | high quality | 0…1 | 1 | 39 | pitch type (feedback / feed-forward) | 0…1 | 0 |
| 15 | feedback on | 0…1 | 1 | 40 | feed-forward unpitched | 0…1000 | 0 |
| 16 | feedback pitch | −120…120 | 0 | 41 | feed-forward pitch 1 | −24…24 | 0 |
| 17 | feedback delay | 10…5000 | 1000 | 42 | feed-forward pitch 2 | −24…24 | 20 |
| 18 | feedback delay (synced) | 1…768 | 192 | 43 | feed-forward amount 1 | 0…1000 | 0 |
| 19 | feedback gain | 0…1000 | 700 | 44 | feed-forward amount 2 | 0…1000 | 20 |
| 20 | feedback high cut | 20…5000 | 1719 | 45 | feed-forward delay 1 | 10…5000 | 1000 |
| 21 | feedback low cut | 20…5000 | 20 | 46 | feed-forward delay 1 (synced) | 1…768 | 192 |
| 22 | feedback reverb mix | 0…1000 | 1000 | 47 | feed-forward delay 2 | 0…1000 | 1000 |
| 23 | envelope scale | −1000…1000 | 0 | 48 | feed-forward delay 2 (synced) | 1…768 | 192 |
| 24 | envelope threshold | −6000…0 | −4000 | | | | |

## 3. From the plugin to the engine

### 3.1 The sidechain buffer **[DECOMP `FUN_00731870`; EXEC]**

The render passes the engine `(frames, input, sidechain, output)` where *sidechain* is the host's buffer for the
state's sidechain input when that is > 0 and the host returns one, **else the output buffer itself**. The engine reads
the sidechain sample before writing the output sample, so with FL's in-place rendering (output = input) the envelope
sees the input; with separate buffers it sees whatever the output buffer held (EXEC: the harness's zeroed buffer —
the first random runs differed from LYNX only for states with *envelope sidechain* on until the harness rendered in
place).

### 3.2 Control → engine value **[DECOMP `FUN_00730100`, `FUN_0072fe70`, `FUN_0072deb0`; EXEC sweep]**

The control's position is `x = clamp((raw − min) · 1/(max − min), 0, 1)` in double, then single. The value sent to
engine index *k* (= the control's number) is (`curve(x, lo, hi, k) = lo + (hi − lo)·x^(1/k)` in double, evaluated as
exp(ln x / k), then single):

| Controls | Engine value |
|---|---|
| 0, 32, 33 | curve(x, −60, 15, 2) — dB |
| 1 | curve(x, 40, 20000, 1.5) — Hz |
| 2, 20, 21, 30, 34 | curve(x, min, max, 0.5) — Hz |
| 4 | curve(x, 0.25, 20, 0.4) — seconds |
| 5 | curve(x, 0, 500, 0.5) — ms |
| 6, 18, 46, 48 | x · 16 (single) — sixteenths |
| 8, 11 | raw · 0.01 (double) |
| 9, 31 | x · 99 + 1 (single) |
| 13, 28 | raw + 1 |
| 16 | (x − 0.5) · 24 (double) — semitones |
| 17, 45, 47 | x · 499 + 1 (single) — ms |
| 23 | x · 2 − 1 (single) |
| 24, 35, 37 | raw · 0.01 (single) |
| 25, 26, 38, 41, 42 | raw |
| 27 | x − 0.5 (double) |
| 36 | x · 4.75 + 0.25 (double) |
| the rest | x |

EXEC: every integer of every control's range (and a margin outside it; large ranges sampled at ~2000 points incl.
both ends) sent through the plugin's ProcessParam with the engine-call logger: **49 836 / 49 836 values
bit-identical** to LYNX's `luxeverb::engineValue` (`py/mapsweep.py`, `py/mapcmp.py`; ln/exp of the C library give the
same singles as the Delphi routines over all of them).

### 3.3 Host order **[DECOMP `FUN_0072e280` (dispatcher), `FUN_0072f610`, `FUN_0072fc40`; EXEC log]**

- **Sample rate** (dispatcher 4): engine rate (§4.1), then all 49 controls are sent again in order.
- **State load**: all 49 controls in order, then `FUN_0072fc40`.
- **`FUN_0072fc40`** (also run on every tempo event and from a few GUI updates of controls 3, 15, 28, 39): engine
  tempo, then one control of each delay pair — predelay 5 or 6, feedback delay 17 or 18, feed-forward delay 45 or 46,
  47 or 48 — the synced one when that pair's sync switch is on. The engine keeps one delay per pair, so the last value
  sent wins.
- **ProcessParam** of one control: that control only (also a delay's unsynced value while its sync is on, until the
  next tempo event).

Setting the rate re-initialises the engine (tempo back to 120, line lengths and envelope times to their defaults); the
plugin's full resend afterwards restores them. LYNX does the same sequence: rate, 49 controls, tempo, the four chosen
delays (EXEC: identical to the plugin with the rate set before the state, §6).

## 4. The engine (`ildsp_x64.dll`, `Shimmer_CreateInstance` → object of 0x191470 bytes) **[DECOMP + asm; EXEC dumps]**

Entry points: constructor `FUN_1801843b0`, rate `FUN_1801849a0`, set parameter `FUN_180186030`, process
`FUN_180187290`, reset `FUN_180188ac0`; pitch shifter: set-up `FUN_1801821f0`, set `FUN_180182910`, process
`FUN_180183180`; predelay read `FUN_180183f40`; diffuser `FUN_180184170`; detector high-pass `FUN_1801871b0`; the
line groups `FUN_180188c30`.

### 4.1 Set-up at a sample rate *fs*

- **Smoothers** (13): one-pole glides `c ← (target − c)·k + c`, `k = 1 − 0.37^(1/(rate·time))`. Rate *fs* for freeze
  gain, freeze mix (both 20 ms), envelope, width, brightness, decay; *fs*/16 for peak frequency/gain/Q, the input low
  and high cut and the feedback filters' frequencies (all 5 ms) — although they are stepped once **per sample** (the
  engine's update counter is never advanced, so the "every 16 samples" branch runs every time).
- **Lines:** 16 delay lines in 4 groups, each a power-of-two buffer ≥ 1 s; default lengths *fs* × a fixed factor per
  line (0.0199…0.1587); a length glide of 6.61/*fs* per sample; per-line damping (§4.2).
- **Diffuser:** per channel 8 all-passes of 4096 samples in series, delays *fs* × (0.0036…0.0161 s), gains 0.5…0.75.
- **Predelay:** per channel a 172 000-sample line read at a gliding position (glide 1.5·10⁻⁴ per sample, starting at
  1 sample after creation — so a long predelay delays the first wet output well past its own length while it glides).
- **Filters:** detector high-pass 20 Hz Q 0.75 (RBJ), the feedback path's DC blocker (pole a quadratic in *fs*) and
  10 kHz low-pass / high-pass pair, input one-pole low cut / high cut, output peak (1 kHz default).
- **Followers:** envelope attack/release coefficients exp(−220/fs) and exp(−11/fs) until the controls set them
  (exp(−2200/(ms·fs)) then); the output peak followers' release exp(−110/44100) and the limiter's glide
  1 − 0.37^(100/44100) are computed with the constructor's 44.1 kHz rate at every sample rate.
- **Three pitch shifters** (§4.4): feedback (+12 st, density 3.5, jitter 0.1/0.1, mode 3, clip of the grain sum),
  feed-forward 1 (+7 st) and 2 (+12 st) (density 2.25, mode 3, clip of the feedback signal).
- **Rotation:** 45°; size gain 1.8667; LFO 0.5 Hz; tempo 120; everything cleared.

### 4.2 One sample

1. **Envelope** (unless mode "off"): (L + R)/2 of the sidechain (switch on) or of the gained input, through the
   20 Hz (or *envelope low cut*) high-pass, rectified, attack/release follower, smoothed (*envelope smoothing*), in dB
   via the library's `log10f`.
2. **Input:** L, R × input gain. With pitch type *feed-forward* and *feedback on*: dry × *unpitched* + shifter 1 ×
   *amount 1* + shifter 2 × *amount 2* (both shifters read the gained input; each has its own delay).
3. **Input filters:** one-pole high-pass (low cut) then one-pole low-pass (high cut), coefficients from the smoothed
   frequencies every sample (bilinear, ω = π f / fs).
4. **Predelay and diffusion** (reverb on): predelay read (linear, or 4-point cubic in high quality), then
   `diffused · diffusion + predelayed · (1 − diffusion)`.
5. **Feedback injection** (pitch type *feedback*, feedback on): the previous feedback output × gain × 8 ×
   min(decay^−0.2, 1) × (pitch ≥ 0 ? 0.7·st/12 + 0.5 : 0.5 − 2·st/12) × freeze mix, added to both channels.
6. **Network** (reverb on): input × freeze gain; a 16-tap triangle LFO (phases i/16) modulates each line's length
   by ± *mod amount*·0.1·(fs/44.1 k); the decay glides (and, in envelope → decay mode, is offset by
   `scale·(max(env − threshold, 0) + offset·threshold)`, clamped 0.25…20 s); one line per sample gets new damping
   coefficients (a first-order shelf from the line length, decay and decay × brightness). Each line is read at its
   gliding fractional length (linear, or a 4-point cubic), damped, blended with the undamped value by the freeze mix;
   the 16 outputs go through a 16 × 16 matrix — the Kronecker product of four 2 × 2 rotations by *character* · 90° —
   plus the injection (mono to the first two lines, L/R alternately to the rest) and are written back. L and R are
   the sums of the even / odd lines × size gain / 8.
7. **Limiter:** peak followers on |L|, |R|; gain 1 / max(4 · glide(max(followers)), 1) (glide of the constant
   0.25 when the limiter is off).
8. **Feedback path** (pitch type *feedback*, feedback on): `mix·(L + R)/2 + (1 − mix)·(pre-limiter mono)` → feedback
   shifter (mono) → DC blocker → low-pass (*feedback high cut*) → high-pass (*feedback low cut*), each a biquad made of
   two identical one-pole sections with w = 4.8814·f/fs from the smoothed frequencies; the result feeds step 5 next
   sample.
9. **Output peak**: an all-pass-based peaking section, A = (2Q − sin ω)/(2Q + sin ω), b0/b2 = (1 + A)/2 ± gain·(1 − A)/2,
   b1 = a1 = −cos ω·(1 + A), a2 = A, coefficients every sample from the smoothed frequency, gain and Q (`sinf`/`cosf`).
10. **Wet level** = *wet level* dB, or in envelope → wet mode `2·scale·(max(env − threshold, 0) + offset·threshold) +
    wet`; gain 10^(dB/20) clamped to 0…5.6; dry and wet gains glide (45.45 s⁻¹); mid/side with width; out =
    wet-side signal · wet + input · dry.

Operation order, operand order and every constant were taken from the disassembly; the whole render runs with the
rounding the engine sets on entry: **toward zero, flush-to-zero and denormals-are-zero** (MXCSR | 0xE040), restored on
exit.

### 4.3 Parameters **[DECOMP `FUN_180186030`]**

Index *k* matches the control number. Notable side effects: *reverb* off clears the lines (and sets the freeze glides
to 1), on re-applies the freeze mode; *freeze mode* 1/2/3 = normal (gain 1, mix 1) / freeze (gain 0, mix 0) /
sustain (gain 1, mix 0); *feedback* off clears the feedback filters, DC blocker and shifter; *size* picks the 16 line
lengths among the primes below 20 000 (index `a·base_i + b` with a, b from a fitted curve of the size), sets the size
gain and the damping; *character* rebuilds the matrix; *envelope attack/decay* use the envelope's stored rate;
*dry level* is clamped to 0…5.6 linear.

### 4.4 Pitch shifter **[DECOMP `FUN_180183180`, `FUN_180182910`, `FUN_1801821f0`]**

Granular: the input (averaged with the previous sample when shifting up) is recorded in a 0.8 s ring; every
`grain length / density (+ jitter)` samples a new grain (up to 20) starts behind the write position (further back for
upward shifts) with a random offset, reading at the pitch ratio with a 4-point cubic under a sin² window; an optional
soft clip `ceiling · (2 − |t/2|)·t/2` (t = 0.2/ceiling · x, ±2 limits) of the grain sum or of the feedback signal; a
4-second delay line with a gliding read (ms or synced sixteenths) and a three-biquad filter (coefficients per sample
rate: 22.05, 44.1 (and every other rate), 48, 88.2, 96, 192 kHz) closes the loop according to the mode. Randomness is
the library's LCG (×0x343FD + 0x269EC3, bits 16…30) with a **process-wide** seed (initially 0x3920A): in FL every
shimmer engine in the process draws from it; LYNX keeps one per instance (**not confirmed** for several LuxeVerbs).

### 4.5 The library's float routines **[asm `out/crt_*.a.asm`; EXEC `py/t_crt.py`]**

`expf`, `powf`, `log10f`, `sinf`, `cosf`, `sincosf` (the vector sine used for coefficients), `fminf`, `fmaxf` of the
library's statically linked C runtime, with an FMA path chosen by a CPU-feature flag. Re-created in
`src/core/IldspMath.h` (tables generated from their mathematical definitions); EXEC: 46 000–114 000 inputs per routine
× three rounding modes (nearest, toward zero with FTZ/DAZ, both) — **0 mismatches** for all six.

## 5. What LYNX does with LuxeVerb

- `src/core/LuxeVerb.{h,cpp}`: `EffectKind::luxeVerb`, parameters = the 49 controls as (raw − min)/(max − min) + the
  four sync switches (53, so `maxEffectParams` grew from 48 to 64); `luxeVerbFromFl` (versions 7…9),
  `luxeverb::engineValue` (§3.2), `luxeverb::Engine` (FL's order of §3.3, then per block only the controls that
  changed; a tempo change re-sends the chosen delays), the engine of §4 in single precision with the library's float
  routines. The slot stays editable (generic editor, FL-unit texts).
- Import: `effectFromFl` routes "LuxeVerb" to the exact effect (the generic reverb mapping stays as the fallback for an
  unrecognised state); the state's sidechain input becomes the slot's sidechain (envelope source when *envelope
  sidechain* is on; a silent source is silence, no side input = the input, as FL renders in place); FL automation of
  controls 0…48 maps to the same parameter.
- Tests: `tests/LuxeVerbTests.inc` (`luxeverb_engine`): three of our own version-9 states (feedback shifter with
  envelope → decay and synced predelay; both feed-forward shifters with envelope → wet, limiter off, low quality, width
  200 %; reverb off with a negative feedback shift, sustain and filters) against DLL goldens, 35 mapping bit patterns
  from the DLL, the three state layouts and the parameter round trip.

## 6. LuxeVerb verification **[EXEC]**

| Comparison | Set | Result |
|---|---|---|
| Float routines vs the library (`py/t_crt.py`) | 6 routines × 3 MXCSR modes | 0 mismatches |
| Engine-call replay: plugin with the logger vs the layout model (`py/replay.py`) | 23 installation states, music 3 s, 44.1 kHz | 23/23 bit-identical (20/23 before the shifter's clip parameters 13/14, found by memory bisection) |
| The same replays through LYNX's engine core (`py/rplx.py`) | 23 states | 23/23 bit-identical |
| Control → engine mapping (`py/mapcmp.py`) | 49 836 control values | 49 836 bit-identical |
| LYNX effect (FL state → slot → engine) vs plugin, rate before the state, in place (`py/e2e.py`) | 23 states × {44.1 kHz/512/120 BPM, 48 kHz/333/87.5 BPM} | 46/46 bit-identical |
| Random version-9 states over every control and sync switch (`py/e2erand.py`) | 60 states over 22.05, 32, 44.1, 48, 88.2, 96, 192 kHz, blocks 64…1000, 4 tempos, 4 signals | 60/60 bit-identical |
| Random states with 6 control changes during the render (`py/e2eauto.py`) | 30 | 30/30 bit-identical |
| Unit test goldens (`py/gold.py`) | 3 states, 18 samples × 2 channels | bit-identical when generated |

## 7. Fruity Convolver: exact engine (`src/core/ConvolverFx.*`, `EffectKind::convolver`)

> Same rules as the rest of this round: clean-room, behavioural only, no Image-Line code or pseudocode, no Image-Line
> audio in the repository (the impulse response is read at run time from the user's installation, or from the WAV the
> FL project itself embeds; the unit-test goldens are the DLL's output for impulse responses generated by the test).
> Markers: **[DECOMP]** decompiled/disassembled code, **[DATA]** the installation's projects / the DLL's data,
> **[EXEC]** the installation's DLL run unmodified, **"not confirmed"** = not enough evidence.

- **Target:**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/Fruity Convolver/Fruity Convolver_x64.dll` | `c5c835c3d4593862022a29446d2523babcc26e613156be748581be55762b4921` | Delphi plugin: state, impulse-response editor and preparation, partitioned convolution, gains |

  The plugin needs no other DLL for its sound (its sample editor, resampler and FFT convolution are inside it).
- **Scratch:** `/home/kauan/.claude/jobs/f8789b49/tmp/r60/cv/`. Ghidra 12.1.4 headless decompilation `out/plug.c`
  (plugin code), `out/smp.c` / `out/lib78.c` (the sample-editing library inside the DLL), `out/norm.c` (level
  analysis), `objdump -M intel` → `out/cv.asm`; constants with `../py/rd.py`, runtime globals with the harness's `gd=`.
- **EXEC harness:** `run/fx.c` (round 55's effect harness: DLL unmodified, `SetErrorMode`, vectored fault handler,
  `timeout`, `TerminateProcess`) with `pre=1` (rate and block size before the state), `sleep=<ms>` (pumps the window
  messages: the plugin prepares the impulse response on a worker thread and installs it when done — 8 s was used for
  every measurement), `p=n,idx,val` automation, `arr=`/`pp=` memory peeks.
- **Drivers (our own code, `py/`):** `st.py` / `cvstate.py` (installation states, layout), `irs.py` (their impulse
  responses; `.wv` decoded with LYNX's own WavPack decoder `cpp/wvdump`), `cvh.py` (wet kernel of a state: an impulse in
  both channels, dry 0, wet 1000, separations 0), `kmodel.py` + `rsm.py` (Python model of the preparation),
  `mkstate.py` / `gold.py` (states of our own with embedded WAVs, test goldens), `t_cpp.py` (LYNX kernels vs DLL),
  `t_full.py` (LYNX engine vs DLL on music); C++ `cpp/cvrun.cpp` (LYNX's `ConvolverFx.cpp` on a file, with
  `CVAUTO=` automation and `CVFRESH=` fresh-instance gains), `cpp/ttest.cpp` (runs `tests/ConvolverTests.inc` alone).

Error measure: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels.

### C.1 State layout (`FUN_00875550`) **[DECOMP + DATA]**

| Field | Bytes | Meaning |
|---|---|---|
| version word | 4 | accepted when 10 ≤ word < 0xF0004; ≥ 0x10000 → `version = word >> 16` (FL 2026 writes 0xF0003 = version 15) |
| controls | 16 | 4 × int32: dry level, wet level (0…1000), input stereo separation, wet stereo separation (−1000…1000) |
| source flag | 1 | ≠ 0: the impulse response is a file named by the path; 0: a WAV follows |
| path | 1 (+4) + n | length byte (0xFF → a 32-bit length follows), then the FL path with `%FLStudioFactoryData%`-style variables |
| embedded WAV | 4 + n | only when the flag is 0: size, then a complete RIFF/WAVE file (the plugin's own edited/rendered copy) |
| editor header | 0x24 | selection and view (doubles, −1 = none, the length in seconds, a float 0.02): **no effect on the sound** — a selection written here leaves the kernel unchanged **[EXEC]** |
| envelopes | 4 × map | the editor's four time envelopes (map version 6; Δt in seconds, level 0…1, tension, segment mode — the envelope-map format decoded in round 51) |
| EQ curve | 1 map | map version 3 over 0…1 (frequency axis) |
| flags | 4 | bytes `[3]` = **normalise**; `[0]`, `[1]`, `[2]` are editor/host options (0, 0, 1 in every state) |
| int | 4 | only from version 15; 0 in every state (its handler is not decoded) |
| knobs | 5 × int32 (version ≥ 11; else 4 and the fifth = 1000) | editor page (GUI only), **predelay** (0…1000), **blend** (0…1000), **speed** (−1000…1000), **EQ amount** (0…1000) |

Installation states: 16 unique (13 demo files): 7 with a file of the installation (`.wv` under
`Data\Patches\Impulses` or `…\Misc\Used by demo projects\…`, `Default.wav`), 7 with an embedded WAV (16-bit and 32-bit
float), versions 14 and 15. One state carries a corrupt path (a repeated variable) but an embedded WAV, which is what the
plugin plays.

### C.2 Impulse-response preparation (`FUN_0087b750`) **[DECOMP + EXEC]**

On every state load / knob change the plugin rebuilds the kernel from the decoded sample, in this order:

1. **Decoding.** Integer PCM becomes float (÷ 32768 / ÷ 8388608), mono is used for both channels.
2. **Normalise** (flag byte 3): the loudest 300 ms window (`Round(0.3 · rate)` frames, sliding) of `L² + R²` gives
   `pk = √(max / (2·window))`; the sample is multiplied by `(float)(0.05 / pk)` — e.g. `0.52501595` on a test noise,
   equal to the DLL's to the float **[EXEC]**.
3. **Envelopes.** The second envelope is the **volume** (a constant level `y` multiplies the sample by `2y` **[EXEC]**).
   The first and third change the sound when they leave 0.5 (a constant 0.2 gives channel-dependent, not purely
   multiplicative results — panning and a third effect, **not decoded**); a constant fourth envelope has no effect
   **[EXEC]**. Every installation state has all four flat at 0.5.
4. **Blend knob `k`** (×0.001): the sample is mixed with its own self-convolution,
   `x·(1 − k/2) + (k/2)·g·(x ∗ x)` cut to the sample's length, `g = 1/√(max over channels of Σx²)` — fitted to
   −135 dB (the DLL's own float-FFT noise) on noise impulse responses with and without leading silence **[EXEC]**,
   the structure read in the DLL's sample-convolution tool (`FUN_006245c0`, `FUN_00624f90`, `FUN_00625690`,
   gain `FUN_006241b0`) **[DECOMP]**.
5. **Speed knob** (×0.001): `speed = (float) 2^k`; the sample is resampled from its own rate to `(float) fs · speed`
   (only when that differs) by the sample player's sinc resampler, quality "256 points": taps `m = −255…256` around
   the read position, weight `sin(w·d)/(π·d) · (0.36 + c + 0.64·c²)/2` with `d = f − m`, `c = cos(π·d/256)`,
   `w = π` (`π / speed` when shrinking); read positions accumulate `speed` step by step near the ends and are
   `p + k·speed` in blocks of 1024 elsewhere; frames before the start repeat the first, past the end the last; output
   length `Round(n · ratio)` **[DECOMP + EXEC]** (−111 dB on a sparse test impulse, see C.6 for the residual).
6. **EQ curve.** A flat curve at level `v` multiplies by `(3^(2v) − 1)/2` (exactly 1 at 0.5; a flat 0.2 gives 0.27592
   **[EXEC]**), whatever the EQ amount. A shaped curve with the EQ amount above 0 filters the sample with a
   linear-phase FIR designed from the curve and mixes the result by the EQ amount — decoded in C.7 (round 173d3).
7. **Trailing silence** (both channels below 2⁻²⁴) is cut; the length at this point plus the predelay is the **tail**:
   the number of samples the plugin keeps processing after its input falls silent.
8. **Predelay knob** (×0.001): `Round((float)(7^x − 1) · 83.333 · 0.001 · fs)` samples (0…500 ms). With the leading
   silence `lead`, `t = predelay + lead`; when `t ≥ 512` the largest power of two `≤ t` becomes a separate delay of the
   convolution engine and `t` keeps the remainder; then `lead − t` frames are removed from the start, or, when
   negative, `t − lead + 1` zeros are inserted — **one more than the difference** (measured: predelay 336 → 337, 2914 →
   2915; no extra sample without predelay) **[DECOMP + EXEC]**. The total delay is therefore predelay + lead + 1 whenever
   zeros are inserted.

The resulting stereo kernel is convolved channel by channel (no cross terms: an impulse in one channel never reaches the
other **[EXEC]**).

### C.3 Rendering (`FUN_0087a000`) **[DECOMP + EXEC]**

Per host block of `n` frames (interleaved), with the capture and preview features idle:

1. **Silence gate.** A block is "silent" when every sample is ≤ 1·10⁻⁶ in magnitude. A counter is reset to the tail on
   every non-silent block and decreased by `n` on silent ones; while `counter > −n` the block is processed, otherwise
   the input is copied to the output (dry at unity) and every gain jumps to its target.
2. **Input matrix** (input separation `s` ×0.001, unity gain, centre pan) applied into a work buffer, gains ramped.
3. **Convolution** (zero latency; the power-of-two delay of C.2 is absorbed by the partitioned engine).
4. **Dry**: the input times the dry gain `(e^(2.39789534·x) − 1)·0.1` (exactly 1 at 1000), ramped; a silent block
   passes unchanged.
5. **Wet matrix** added: gain `(1001^x − 1)·0.001` **× 44100 / host rate** (a factor the plugin keeps in its
   per-rate block; 0.91875 at 48 kHz, measured to 7 digits **[EXEC]**), wet separation.

The gain matrix and the ramps are the same library routines as Fruity Reeverb 2's (round 49 §3): separation > 0 merges
by `(5^s − 1)/4`, < 0 separates by `s`; per block the gain moves toward its target by at most 0.001 per sample, the
dry ramp in pairs of samples. A freshly created plugin starts its gains at 0, so if its very first block is not silent
the output fades in over ~1000 samples (LYNX reproduces it with `Engine::reset (true)` — confirmed to −136 dB — but
starts from the targets, as FL's mixer always runs silent blocks before playback) **[EXEC]**. Parameter display: dry/wet
in dB (`-INFdB` at 0), separations "Original", "N% merged" (> 0) or "N% separated" (< 0) **[EXEC]**.

### C.4 LYNX

- `convolverFromFl (state, read)`: parses the state; a file of the installation is read through the import's FL data
  reader (`%FLStudioFactoryData%\X` → `X`, `%FLStudioData%\X` → `Data\X`; user-data paths are not read); the slot's
  `state` keeps a blob `"LXCV" | 1 | FL state | file bytes`, so a saved LYNX project plays without the installation.
  Unsupported → `nullopt` (the importer keeps the generic reverb approximation): unreadable file, non-default first /
  third envelopes, a shaped fourth or volume envelope. (Shaped EQ curves are supported since round 173d3, C.7.)
- Kernel preparation in `buildKernel` (double precision where the DLL uses float FFTs/recurrences; stores in float as the
  DLL does); the engine is a zero-latency partitioned convolution (128-tap direct head, then uniform overlap-save levels
  of 128 / 1024 / 8192 / 16384 samples whose partitions never start before their own size), FFTs in double, spectra
  stored in float.
- Parameters: FL's four controls as `(raw − min)/(max − min)`, automatable; the knobs and envelopes stay in the state.
- Cost: kernel preparation on `prepare()` (a 9-s 48 kHz impulse response resampled to 44.1 kHz: ≈ 1–2 s, once).

### C.5 Verification

All comparisons: LYNX's C++ (`cpp/cvrun`, the repository's `ConvolverFx.cpp`) against the DLL, rate set before the
state, blocks of 512 unless stated, 1024 silent frames before the signal (FL's mixer runs silent blocks first).

**Installation states, their own controls and knobs, music 4 s** (`py/t_full.py`):

| State (index in `py/st.py`) | IR | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|---|
| 0 Astes (church, .wv 48 kHz, normalised) | file | −117.0 dB | −143.9 dB | −144.1 dB |
| 2 Astes (dulcimer, .wv 48 kHz) | file | −95.5 dB | −134.5 dB | — |
| 3–7 Bombs Away (embedded 16-bit WAVs) | embedded | −134.8 … −139.0 dB | −127.5 … −134.9 dB | −132.0 dB (3) |
| 8 Edlan (Room Medium .wv 48 kHz; predelay 45, speed −476) | file | −130.7 dB | −131.3 dB | −132.4 dB |
| 9 LollieVox (Default.wav, normalised) | file | −144.4 dB | −142.4 dB | −143.2 dB |
| 10 LollieVox (embedded float; predelay 114, blend 102, speed −1000) | embedded | −151.9 dB | −150.2 dB | DLL not reproducible |
| 11 Purps (embedded float; speed −372) | embedded | −138.0 dB | −138.2 dB | — |
| 14 Treben (forest .wv 48 kHz) | file | −101.0 dB | −135.5 dB | — |
| 15 Dubstep (Default.wav, separations −1000, speed +1000) | file | −136.8 dB | −136.5 dB | DLL not reproducible |
| 1, 12, 13 (shaped EQ curve, amount > 0; round 173d3, C.7) | file | −131.0 / −131.4 / −133.3 dB | −132.8 / −124.4 / −127.9 dB | — |

At 96 kHz the DLL's own output for states 10 and 15 changed from run to run (by +14 dB; spikes in its kernel beyond
~53 000 samples) — a timing problem of the plugin's background preparation in the harness, not a property of the
sound; those two cases are excluded. The 44.1 kHz residuals of states 0, 2 and 14 are the resampler residual of C.6
(48 kHz files).

**Wet kernels** (impulse in both channels, dry 0, wet 1000, separations 0; `py/t_cpp.py`): embedded / 44.1 kHz files
−134.3 … −137.1 dB (the DLL's float-FFT floor); resampled files −85.2 … −130.5 dB at 44.1 kHz; −123.5 … −136.3 dB
at 48 kHz.

**Synthetic cases:**

| Case | Result |
|---|---|
| blend 500 / 300 on noise IRs (with 5 and 50 leading zeros) — formula of C.2.4 | −135.6 … −135.8 dB |
| resampler on a sparse impulse (48 → 44.1 kHz), Python model | −111 dB |
| speed ±, predelay 45 / 300, normalise on/off, volume envelope 0.2, flat EQ 0.2 | as C.2 (exact sample counts, gains to the float) |
| block sizes 128 / 1000 / 4096 (state 3) | −136.5 / −135.8 / −135.8 dB |
| controls (500, 700, 300, −200), (0, 1000, −1000, 1000), (321, 999, 1000, −1000); dry only (1000, 0, 0, 0) | −134.8 / −136.2 / −132.8 dB; bit-identical |
| automation of all four controls at five block boundaries (state 9) | −133.0 dB |
| silence gate: 50 000 silent frames and a 10⁻⁷-level burst (state 3) | −135.9 dB |
| fresh instance, first block not silent (`Engine::reset (true)`) | −135.9 dB |
| unit-test states (`tests/ConvolverTests.inc`): 16-bit stereo normalised at 44.1 and 48 kHz; mono float 48 kHz with predelay, blend, speed, flat EQ 0.4, separations | −134.3 / −118.2 / −121.1 dB (goldens checked to 2·10⁻⁵) |

### C.6 Residuals and open items

- **Resampled impulse responses** (48 kHz files at 44.1 kHz and vice versa): during the first few hundred output
  samples, outputs whose read position falls within ~10⁻¹² of an integer differ by up to 0.2 % (the DLL builds the
  sine of the tap next to the read position from double-precision phases, which loses digits there); elsewhere −110 dB.
  Whole-kernel effect −85…−100 dB on the affected states; **not reproduced**.
- The first and third time envelopes, shaped volume envelopes and the int after the flags: **not decoded** (no
  installation state needs them). The EQ curve's filter is decoded in C.7.
- The sample's own regions (a `regn`/loop chunk in an embedded WAV) — the kernel step that keeps region 0 was seen in
  the code but not exercised (no installation WAV carries one): **not confirmed**.
- More than two channels in a WAV: LYNX uses the first two — **not confirmed**.
- FL's host order (rate before/after the state) does not matter here: the kernel is rebuilt at the rate in effect, and
  the gains snap on the first silent block.

### C.7 EQ curve (round 173d3) **[DECOMP + EXEC]**

Material: the round-60 decompilation (`FUN_0087b2b0` and the sample library's filter designer), the runtime values of
the plugin's globals and IPP entry points read through the EXEC harness, and wet kernels of the unmodified DLL on states
built here (scripts in `/mnt/e/lynx-work/audit173d3/cv/py`: `eq.py` Python model, `t3run.py` / `t5.py` comparisons,
`gold173.py` test goldens).

**Where.** Step 6 of C.2, right after the speed resampling and before the trailing-silence cut. The kernel builder asks
the curve whether it is flat (`FUN_00793a00`). Flat → the gain `(3^(2v) − 1)/2`, applied whatever the EQ amount, even 0.
Shaped → `FUN_0087b2b0`, which does nothing at all when the EQ amount (knob 4 × 0.001, a float) is 0 — not even a gain:
the kernel then equals the raw impulse response to −136.7 dB **[EXEC]**.

**Steps of the shaped curve** (`rate` = the sample's rate at this point, i.e. `(float)(fs · speed)`):

1. **Size.** `M = 2^b`, `b` = the smallest integer ≥ 1 with `2^b ≥ Round(2 · rate)`, clamped to 4…20 (M = 131 072 at
   44.1 and 48 kHz, 262 144 at 96 kHz). The response has M + 1 bins: 0 … Nyquist of a 2M-point transform.
2. **Frequency of each bin.** `x_k = (float)(k · rate / (M · 22050 · speed · 2))` — bin `k` at `k · fs / (2M)` Hz,
   divided by 22 050 — then the editor's frequency axis (`FUN_00637aa0` on the object built by `FUN_00637830` with
   `fmin = 440 · e^(−21 · ln2/12)` ≈ 130.81 Hz, `fmax = 22050`, curvature 1, flags 0): with `f = 22050 · x`,
   `c = fmax/fmin`, `p1 = ln 2 / ln(2 · fmax / fmin)` (≈ 0.119),
   `u = p1 + ln((f − fmin) · c / (fmax − fmin) + 1) / ln(c + 1) · (1 − p1)` for `f ≥ fmin`, else `u = f · p1 / fmin`
   (the bottom 12 % of the display is linear from 0 to 130.8 Hz, the rest logarithmic-like up to 22 050 Hz at the
   top). `u` is rounded to float. Bins above 22 050 Hz (at 48 kHz and higher) fall past the curve's end and take its
   last level.
3. **Curve.** FL's map: the Δx are scaled to a sum of 1 (Δx summing to 0.6 or 2 match the DLL only when scaled,
   −134.8 dB; unscaled −4.1 / +3.6 dB **[EXEC]**); a first point that starts after 0 behaves as if a point at 0 with its
   level came first **[EXEC]**. The map iterator (`FUN_00791500`) is FL's shared envelope law — the segment modes 0–12
   of round 66 §3.2 (Love Philter's `Curve`), with the point's prepared parameters in float and the evaluation in
   double. It is walked bin by bin, advanced by the difference of successive `u`.
4. **Gain** per bin: level `v` → `(float)((e^((float)(2v) · ln 3) − 1) · 0.5)` — the flat curve's law: 0 at the
   bottom (−inf), 1 at the middle, 4 (+12 dB) at the top.
5. **Smoothing** (`FUN_00624620`, width parameter 1/128). A Gaussian `g_j = e^(−½ (j·s)²)`, `j = 0 … 65 536`,
   `s = (float)(1 / (65 537 · (1/128) · 0.25))` (IPP `VectorSlope` / `Sqr` / `MulC(−0.5)` / `Exp`, float) is turned into
   a centred 131 072-tap FIR by the designer of step 6. The response (two channels: the gains and a phase of 0) is
   extended at each end by M + 1 copies of its end value, convolved with that FIR, and the middle M + 1 values are kept
   (float). In effect the response is blurred along the bins by a Gaussian of about 163 bins (≈ 27 Hz at 44.1 kHz).
6. **FIR design** (`FUN_00624910`). The M + 1 values are a real spectrum (the second channel is the phase:
   `re = g·cos φ`, `im = g·sin φ`, φ = 0 here) in IPP's packed format; an inverse real FFT of 2M points divided by 2M
   (IPP `DIV_INV_BY_N`), rotated by M (time 0 at tap M), then multiplied by a Hann window
   `w_i = 0.5 − 0.5 · cos(2π i / (2M + 1))`, `i = 1 … 2M` (`FUN_00636ca0` type 2 over 2M + 2 points, both end points
   dropped — so the window's peak sits half a tap before the FIR's centre). 2M taps, float, the filter's delay
   `(2M − 1) >> 1 = M − 1` compensated by the convolution.
7. **Filtering** (`FUN_00624f90`, float overlap-save FFTs of `2^⌈log2(2M + 32768)⌉` points): `y[t] = Σ_m h[m] ·
   x[t + M − m]` for `t = 0 … n − 1` — zero-phase, aligned, cut to the sample's length, zeros outside the sample.
8. **Mix** (`FUN_00625690`): the filtered sample times the amount `a`, plus the original times `1 − a` (the tool's wet
   and dry gains), stored as float.

The filtered sample then continues with C.2 steps 7–8 (trailing silence, leading silence and predelay), so the FIR's
pre-ringing into leading silence counts as signal there **[EXEC: chain cases below]**.

**State format of the curve's points** **[DATA]**: 24 bytes — Δx (double), level (double), tension (float), then the
bytes `mode, 0, 0, sign` where `sign` is what the curve's preparation computes (0 on the first point, 2 for a straight
segment — tension 0 in modes 0, 1, 7, 8, 9, 11, 12 —, 1 / 0xFF for a positive / negative tension). In the installation
states byte 22 of one point is 1 (an editor flag; no effect seen). The map's 20-byte trailer is `0, −1, −1, −1, −1`.

**LYNX** (`src/core/ConvolverFx.cpp`): `applyEqCurve` (steps 1–8; `EqCurve` is the map iterator, `FreqAxis` the
frequency axis, `centredFir` the designer, `smoothResponse` the smoother — its constant ends are added from running
sums of the smoother instead of padding the response). FFTs in double. `kernelSupported` no longer rejects shaped
curves. Cost on top of the kernel preparation: ≈ 0.2–0.3 s at 44.1/48 kHz, ≈ 2.5 s at 96 kHz (2^21-point transforms
for a 9-s impulse response). For the editor: `readEqCurve` / `writeEqCurve` (the curve of a slot blob; writing keeps
the rest of the FL state byte for byte), `eqCurveLevels` (the curve at evenly spaced x by the same law),
`eqFrequencyToX` / `eqXToFrequency` (the display's axis, `FUN_00637aa0` / `FUN_006379d0`) and `eqLevelGain`.

**Verification** (wet kernels as in C.5; LYNX's C++ through `cvrun`):

| Case | 44.1 kHz | 48 kHz |
|---|---|---|
| noise IR at the host rate, the curves of installation states 1 / 12 / 13, EQ amount 0 | −136.0 / −135.9 / −136.0 dB | −135.9 / −135.9 / −135.9 dB |
| … EQ amount 300 | −135.7 / −135.8 / −135.0 dB | −135.5 / −135.5 / −135.9 dB |
| … EQ amount 1000 | −134.9 / −135.1 / −132.2 dB | −134.7 / −134.9 / −135.3 dB |
| segment modes 1–12, tensions +0.4 / −0.6 (5-point curves) | −131.9 … −135.6 dB | |
| Δx summing to 0.6 / 2, a first point after 0 (Python model) | −134.8 / −134.8 / −134.6 dB | |
| 96 kHz IR at 96 kHz (M = 2^18), curve 12, amount 1000 | | −134.6 dB at 96 kHz |
| chain: 44.1 kHz IR with 40 leading zeros, normalised; knobs (predelay, blend, speed, EQ) = (0, 0, −300, 1000) / (80, 200, 0, 700) / (150, 0, 250, 1000) | −116.3 / −130.7 / −111.0 dB | −117.3 / −105.9 / −111.0 dB |
| the same chain with EQ amount 0 (the resampler residual of C.6) | −109.6 / −132.8 / −110.2 dB | −109.5 / −104.7 / −110.3 dB |
| installation states 1 / 12 / 13, wet kernel | −112.1 / −133.9 / −134.1 dB | −112.1 / −114.9 / −118.5 dB |
| … the same states with EQ amount 0 (resampler residual) | −117.0 / −145.5 / −145.5 dB | −117.0 / −110.5 / −110.5 dB |
| installation states 1 / 12 / 13, music 4 s through the engine (own controls) | −131.0 / −131.4 / −133.3 dB | −132.8 / −124.4 / −127.9 dB |
| unit-test states (`tests/ConvolverTests.inc`): 16-bit stereo IR with a 7-point curve (modes 0, 1, 2, 10), amount 600; mono float 48-kHz IR at 44.1 kHz with speed 150, predelay 60, a curve whose Δx sum to 2, amount 1000 | −131.3 dB (and −128.1 dB) | −111.8 dB (IR resampled) |

Every case that is not at the DLL's float-FFT floor (≈ −132…−136 dB) is a resampled one, and the same states with the
EQ amount at 0 show the same residual: the resampler's near-integer read positions of C.6, not the EQ.

Not confirmed: points with a negative Δx (the preparation clamps them to 0 as in the other maps — not run); a
non-default byte 22 or map trailer; host rates above 96 kHz (b = 20 caps the size at 2^20 bins once the rate passes
524 288 Hz).

## 8. Open items

- LuxeVerb: the feed-forward sync flags of version-7/8 states (uninitialised in the plugin) — LYNX: off.
- LuxeVerb: the shifters' random sequence is process-wide in FL (shared by every shimmer engine and advanced from
  plugin creation, also while FL is stopped); LYNX starts each instance at the library's initial seed when the
  effect is prepared, so grain jitter phases can differ from a given FL session (the shifters are only audible with
  feedback gain > 0 or the feed-forward type).
- LuxeVerb: FL's order of state load and sample-rate set at project load is assumed rate-first (as in the earlier
  rounds); the engine's state after either order is the same here because the plugin re-sends every control after a
  rate change.
- LuxeVerb: the plugin's own display of the synced delays ("beats:sixteenths" style) is not reproduced; LYNX shows
  sixteenths.
- Fruity Convolver: see §7 C.6 (resampled impulse responses at −85…−117 dB, the first/third time envelopes not
  decoded — those states keep the approximation, regions and > 2 channels not confirmed). The EQ curve is exact since
  round 173d3 (C.7).

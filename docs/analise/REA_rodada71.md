# Behavioural analysis of FL Studio 2026: round 71 (REA) — Hardcore, Distructor, Multiband Delay (missing native effects)

> **Nature of this document.** Decoding of native FL effects that LYNX imported only approximately or dropped:
> Hardcore (dropped until now), Distructor (imported as a generic distortion) and Multiband Delay (unmapped). *Clean-room*
> reverse engineering, behavioural only. No Image-Line code or pseudocode was copied: what is here are data layouts,
> formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to
> trace the evidence. No FLEX pack was opened; no Image-Line preset or audio data was copied into the repository (the
> Hardcore cabinet impulses are read from the user's installation at import time).
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Inferences are marked as such;
> what could not be established is marked "not confirmed".

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp`/`.fst` files, or a value read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-16. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r71/`.
- **Targets (Hardcore):**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/Hardcore/Hardcore_x64.dll` | `adcd8ded370decab57cdd2ada210501e02e8f392a3ed07bbcb5b0ab89526957c` | Hardcore (MSVC C++) |
  | `Plugins/Fruity/Generators/Toxic Biohazard/Toxic Biohazard_x64.dll` | (round 45) | comparison of the shared tape line / reverb |
  | `Shared/ildsp_x64.dll` | `b96354e1ee4f096208432e9a31fd9838c9cc5cf36d4ac078f9cf5c94faaf1760` | comparison of the CRT math |

- **Tools:** headless Ghidra 12.1.4 (project `~/gh-r71`, `gh/*.sh`, `out/hc*.c`), `objdump -M intel` (`hc/asm_*.txt`),
  `py/fcmp.py` (instruction-stream comparison of two functions, operands' addresses masked).
- **EXEC harness:** `run/fx.c` (round 61's effect harness, unmodified), driven by `py/fx.py`; `hc/hc.py` compares the DLL
  with LYNX's engine (`hc/cpp/hccmp.cpp`, `hc/cpp/dbg.cpp` for internal state); test scripts `hc/t1.py … t11.py`;
  `hc/teststate.py` builds the states and goldens of `tests/HardcoreTests.inc`.
- **Installation usage [DATA]** (`py/survey.py`): Hardcore 2 slots (render test, *Treben – Umziky*); Distructor 4 slots
  (DEDZII *Night Terror*, Edlan & Ella Noël *Song For You*, Sid De La Cruz *Secret Agent Rendezvous* ×2); Fruity Squeeze
  2 slots (already exact since round 29); Fruity Scratcher 1 (render test); Multiband Delay none (presets only).

Error measure everywhere: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels; "−∞" = bit-identical.

---

## 1. Hardcore

### 1.1 Plug-in and state **[DECOMP + DATA]**

`Hardcore_x64.dll` is an MSVC C++ build of a third-party guitar multi-effect wrapped for FL (classes `CFLWrapper` →
`HardcoreSynth`, a VST-style `AudioEffect`; pedals `CPedal_*`; the reverb is the `fxReverb` class of Toxic
Biohazard's rack, REA_rodada45 §5.5, and the tape line is the same `CTapeDelay`). Entry `CreatePlugInstance`
`0x180002340`; FL state I/O `FUN_180002f90`; the inner chunk reader `FUN_18002ad90`, writer `FUN_18002aca0`;
`setParameter` `FUN_18002a010`; sample-rate set-up `FUN_18002a5a0`; block process `FUN_18002a740`.

State (plugin record `0xD5`): `"SSLF"`, u32 size (0x1CC), then the chunk: `"1RCH"` (or `"DRCH"`, the demo build's tag),
**50 × f32** parameter values, 256 bytes of preset name (ANSI, zero padded). The reader calls `setParameter` for
indices 0…49 **in order**, which matters: box knobs (13…37) arrive before the box types (45…49), and a type change
builds its pedal and hands it the five stored knobs. Defaults (`FUN_18002a570`): box switches 39…43 = 1, EQ switch and
box types = 0, everything else 0.5.

| Index | Parameter | Value law |
|---|---|---|
| 0, 1 | input / output level | target `powf(10, v·2.4 − 1.2)` (±24 dB); the gain moves `g += (target − g)·0.001` per sample; it jumps to the target at the first block after a rate change |
| 2 | EQ on | ≠ 0; a change clears the 16 EQ filters at the next block |
| 3…10 | EQ bands 1…8 | `y = (v − 0.5) + v − 0.5`, gain `(float)(±y²·18.0)` dB (sign of y) |
| 11 | cabinet | `(int)v`: 0 = none, 1…20 = the installation's `Imp\Model A…T.wav` (two names carry a space before `.wav`) |
| 13 + 5b + k | box b knob k | handed to the pedal of box b |
| 39…43 | box on | 0 bypasses the box; a change marks the pedal for a reset before its next block |
| 45…49 | box type | `(int)(float)(int)v`, clamped to −1…11: 0 empty, 1 distortion "Model A", 2 chorus, 3 flanger, 4 phaser, 5 reverb, 6 delay, 7 noise gate, 8 equalizer, 9 compressor, 10 modulator, 11 tube drive |
| 12, 38, 44 | — | stored, unused |

FL's `ProcessParam` takes 0…256 integers (`×1/256`, clamped) and only calls `setParameter` when the value changes
(`FUN_1800034a0`) **[DECOMP + EXEC]**.

Installation use **[DATA]**: 2 mixer slots — the render test `System/Render tests/Hardcore.flp` (box 1 empty, box 5 delay [corrected in REA_rodada170i §5: boxes 1–5 = distortion, gate, chorus, delay, empty],
cabinet 4, preset "80s lead") and *Treben – Umziky – All the same* (box 1 distortion Model A, cabinet off).

### 1.2 Block **[DECOMP `FUN_18002a740` + asm]**

1. Per sample, each channel through a second-order high-pass at 30 Hz, Q 1 (the `FUN_18001d050` design, see §1.4), times
   the input gain (both channels with the gain before its update).
2. Boxes 1…5 in series: a pedal marked for reset is reset first; a pedal runs only when its box is on.
3. Cabinet convolution (§1.5).
4. EQ (when on): band by band over the whole block, each band left then right per sample.
5. Output gain (gain before its update).

The FL wrapper cuts host blocks into chunks of (buffer size − 4) samples; no result depends on the chunking
**[EXEC: 512 vs 256-sample host blocks identical]**.

### 1.3 Shared building blocks **[DECOMP + asm + EXEC]**

- **CRT math.** The statically linked `powf`, `sinf`, `cosf`, `expf` and the packed float sine pair are the same code as
  `Shared/ildsp_x64.dll`'s (instruction-for-instruction comparison), FMA path chosen at run time (the flag is set on
  this machine): LYNX uses the re-creations of `core/IldspMath.h` (REA_rodada60 §4.5). `tanf` (no FMA path) equals Toxic's
  `0x18006fa20`. Using Toxic's plain re-creations instead left 1-ulp differences in the phaser (cosf of small phases)
  and tube drive.
- **Biquads with a design cache**, all float with an explicit double for ω = f·2π/fs:
  - low-pass `FUN_18001d240` and high-pass `FUN_18001d050` (s, c from the packed sine pair): `inv = 1/(s + q)`,
    `a = (1 ∓ c)/(s/q + 2)`, `b1 = (c·inv)·(q·−2)`, `b2 = (q − s)·inv`; ticks
    `y = ((2x₁ + x + x₂)·a − y₁b1) − b2·y₂` and `y = ((x − 2x₁) + x₂)·a − y₁b1 − b2·y₂`;
  - band-pass `FUN_18001d420`, designed in **double** with a double sine pair (sin x, sin(|x| + π/2)):
    `d = 1/((0.5/q)·s + 1)`, `b0 = (float)((float)(s·0.5)·d)`, `a1 = (float)((float)(2c)·d)`,
    `b2 = (float)((float)(1 − s/(float)(2q))·d)`; tick `y = ((x − x₂)·b0 + y₁·a1) − b2·y₂`;
  - the cookbook peaking/shelf filter with a dirty flag (`FUN_18001d660` / `FUN_18001da00`) = Toxic's EQ band.
  - Every input and output below ±1e−9 is flushed to 0.
- **Tape line** (`CTapeDelay`, `FUN_180025ff0`) = Toxic's (REA_rodada45 §FX.3), with two optional internal filters that
  Hardcore never switches on.

### 1.4 The eleven pedals **[DECOMP + asm; every formula EXEC-verified]**

Knob k of a box is v_k (0…1). "x = L + R" is the mono sum.

| Type | Set-up / laws | Per sample |
|---|---|---|
| 1 Distortion "Model A" | k0 drive: `k = 99·v₀² + 1`, pre-gain `1/(8v₀ + 1)`; k1 comb length `(int)(n₂·v₁)` clamped below n₂ (n₂ = (int)((double)(8fs)·0.0002)); k2 high-pass Q `30v₂ + 1` at 85 Hz; k3 tone: peaking EQ 2500 Hz, bandwidth 3, gain `(v₃ − 0.5)·20` dB; band-passes 200 Hz and 1 kHz (Q 0.99) at the host rate; low-pass 500 Hz, Q 4; comb 1 = (int)((double)(8fs)·0.001) samples | a = (float)(BP200(x)·0.05), b = BP1k(x)·5; 4-point cubic interpolation between the two previous inputs of each path at t = 0, ⅛ … ⅞ (8× oversampling); each value soft clipped `1 − 10^(−k·v)(1 − v)` / `10^(k·v)(v + 1) − 1`; comb 1 feeds `b − 0.25·old`, mix `(float)(0.7·old + a)`; comb 2 (when its length ≠ 0) `m − 0.3·old`; low-pass (run at 8× without redesign) × pre-gain; after the 8 steps: high-pass, tone EQ; mono out |
| 2 Chorus | three tapes of `int(fs·0.02)·{1.1, 0.854, 1}` (truncated) samples, phases 0, 0.467, 0.778; rate `(float)(5v₁ + 0.1)`·{0.896, 1, 0.689}/fs; gain −v₂²; depth v₀³ | ticks tape 1, 3, 2 on x; L += y₂ + y₁, R += y₂ + y₃ |
| 3 Flanger | two tapes of (uint)(0.005fs) samples, phases 0 / 0.5; time `clamp(v₀², 0.01, 1)`; rate `v₁²·(10v₁) + 0.01` Hz; gain `−0.1 − 0.9v₂`; feedback −0.1; depth 1 | L += tape₁(L), R += tape₂(R) |
| 4 Phaser | lo = 50/fs, hi = 20000/fs; K = v₂²·(v₂/2); frequency `max(v₁², 1e−5)`; mix `max(v₃, 1e−5)`; phase step `(float)((v₀·88200)·v₀/fs)·0.0005` | Toxic's phaser (REA_rodada45 table, type 4) with a fixed feedback 0.454875678, float smoothers (0.001) and the phase wrapped at 2π in float; the reset also clears the LFO phase |
| 5 Reverb | `fxReverb` index 8 ← (float)(v₀³·0.6), 9 ← \|1 − v₁\|, 12 ← (float)(0.999v₂ + 0.001); reset sets 2 and 5 to 1 | in place; unlike Toxic's build **all early-reflection taps have gains**: left taps 1102, 2904, 3784, 4873, 5878, 7470, 9062 (7 used), right 837, 1423, 2865, 3938, 5278, 5978, 7532, 9263 (scaled by ((p₄+3)·fs/103)·3.40136e−5), gains min(L₀, R₀)·{0.5, 0.36}/tap, summed with the signs + + − + + + + (left) and + − + + − + + − (right) |
| 6 Delay | two 1-second tapes (phases 0 / 0.5); time clamp(v₀, 0.1, 1); feedback (float)(−0.95·v₁²); gain −((float)(0.9v₂·v₂) + 0.1); speed 1 Hz, depth (double)0.01f | as the flanger |
| 7 Noise gate | threshold `(float)pow(10, (1 − v₂)·−3.2)`; attack/release `(float)(8.82/((double)(float)(2v² + 0.05)·fs))` (v₀, v₁); hold (uint)(0.1fs) samples | per channel: below the threshold count samples, after the hold `g −= g·rel`, otherwise `g += (1 − g)·att`; g ≥ 1e−7; out = g·x |
| 8 Equalizer | low shelf 300 Hz, peaks 700 Hz / 1.8 kHz, high shelf 7 kHz, bandwidth 0.985; gain ±y²·10 dB (y as the master EQ) | band by band |
| 9 Compressor | threshold as the gate's (v₀); slope `1/((1 − v₁)·30 + 1)`; attack 8.82f/((2v₂² + 0.1)·fs), release 8.82f/((4.8v₃² + 0.2)·fs) | per channel: t = thr/((\|x\| − thr)·slope + thr) above the threshold, else 1; `env += (float)coef·(t − env)` (attack while t < 1); out = env·x |
| 10 Modulator | tape of (uint)(0.01fs) samples, time clamp(v₀², 0.01, 1), gain 1, no feedback, depth 1, speed r = 30v₃² Hz; LFO step r/fs | lfo = (float)cos(2π·phase); y = tape(x); `y·(1 − a·lfo·lfo²)`; L = (1 − p·lfo)·…, R = (p·lfo + 1)·…; a, p glide to v₁, v₂ (×0.001 per sample) |
| 11 Tube drive | drive `(float)((double)(v₂²·(10v₂)) + 0.01)`; low shelf 200 Hz (bw 2.5) gain (1 − v₀)·−28; high shelf 7 kHz (bw 2) gain (1 − v₁)·−28; low-pass 4 kHz Q 0.5; output gain 0.5/(1 − 10^(−4·drive)(1 − drive/2)) | x = lowshelf((R + L)·drive) → low-pass → soft clip (k = 8) → minus a slow follower (`f += 0.01·(x − f)` in double) → high shelf × output gain; mono out |

### 1.5 Cabinet **[DECOMP `FUN_180026f00`, `FUN_180027260`, WAV reader `FUN_18001c890` + DATA]**

- The file is read by the plug-in's own RIFF reader: float32 copied; integer PCM scaled by 2⁻¹⁵ (16 bits),
  **1.1920930376163597e−07** (24 bits — one ulp above 2⁻²³) or 2⁻³¹ (32 bits). The installation's 20 files: A…L mono float32,
  M…T stereo 24-bit, all 44.1 kHz.
- Samples × (float)(file rate / host rate); length `count = (int)(frames·host/file rate)`; taps = 4·(count/4 + 1), zero
  padded; tap i = linear interpolation at `i·(1/count)·(frames − 1)` × 0.2 (mono files feed both channels).
- Convolution in time, accumulated so that every output sums the **oldest input first**
  (`y = x[n−T+1]·h[T−1] + … + x[n]·h[0]`, float, no FMA). A rate change reloads the file; cabinet 0 unloads it.

### 1.6 Verification **[EXEC]**

Harness `run/fx.c` (rounds 50/61), `hc/hc.py` (DLL vs LYNX's engine through `hc/cpp/hccmp.cpp`), `hc/t*.py`:

| Test | Result |
|---|---|
| defaults, levels, EQ (all bands) | −∞ (bit-identical) |
| each of the 11 pedals alone, 2 random knob sets each (22 runs, 1 s of music) | −∞ ×22 |
| the 20 cabinets (5 checked by name: A, D, I, J, T) and cabinets inside random chains | −∞ |
| 24 random full chains (random types/switches/knobs/cabinet/EQ) at 44.1/48/96/22.05 kHz, host blocks 512/256/1024/100 | −∞ ×24 |
| automation: 10 runs × 12 random parameter changes (levels, EQ, knobs, box switches) | −∞ ×10 |
| switching each pedal type off/on, knob changes, type changes, cabinet off, EQ off mid-stream | −∞ ×11 |
| the 2 installation states (render test, Treben), 2 s of music | −∞ ×2 |

Not confirmed: the 8-bit and 64-bit WAV paths of the reader (not used by the installation; LYNX treats them as
unreadable); what the "demo" chunk tag changes in the registered build (it resets to defaults there).

### 1.7 LYNX

- `src/core/HardcoreFx.{h,cpp}`: `EffectKind::hardcore`; the engine above; slot params = FL's 50 values (cabinet / 20,
  types / 11); `EffectSlot::state` = the installation's 20 impulse files (≈290 kB, packed `"LXHC"`) so that the cabinet
  stays switchable in a saved project without FL. Import: `effectFromFl ("Hardcore", …, data)` reads them through the
  FL data reader; without the installation the engine runs with the cabinet off (import report note).
- `src/core/IlFxKit.h`: Toxic Biohazard's CRT re-creations, biquads, tape line and `fxReverb` moved out of
  `ToxicFx.cpp` and shared (templated on the CRT routines; Toxic keeps its plain ones, its tests unchanged).
- Tests: `tests/HardcoreTests.inc` (`hardcore_exact`): 3 states of our own (all 11 pedals, EQ on/off) + 2 cabinet
  states (installation files) against DLL goldens, the import/mixer path, parameter round trip.

---

## 2. Distructor

- **Targets:**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/Distructor/Distructor_x64.dll` | `00c1eabfff37453f3502407a7bdaab096af8e1a0e6d9072d613137183b11f264` | the plug-in (Delphi): state, module chain, final clipper |
  | `Plugins/Fruity/Effects/Distructor/module/Module_x64.dll` | `2ad3fd72c1bcb3df6f66573ff9a6868eb1d445e66c4ccf519e0aecd56c596eb7` | one module (Delphi): distortion, filter, chorus, amp/cabinet |

- **Tools:** headless Ghidra (`out/dis.c`, `out/dmod.c`, `dis/out/*.c`), `objdump -M intel`. EXEC: the same harness
  `run/fx.c` loading either DLL unmodified (`dis/fxd.py`), LYNX side `tools/distructorrender.cpp` (`dis/cpp/dr`, built by
  `dis/mk.sh`), comparisons `dis/cmp.py` and `dis/t*.py`; `dis/gold.py` prints the goldens of `tests/DistructorTests.inc`.
- **States:** the 4 installation slots (header, "Installation usage") and the 47 states of the plug-in database / presets (`dis/fst/`).

### 2.1 State **[DECOMP + DATA]**

The plug-in state (version ≤ 5) is: i32 version, i32 module count *n* (≤ 4), *n* module blocks, u8 "final clipper",
and from version 4 on an i32 count followed by the processing order (one i32 per module), then trailing editor bytes.
A module block is Module_x64's own state (versions 1…4; 231 bytes in version 4): i32 version, i32 type (0 distortion,
1 filter, 2 chorus, 3 amp/cabinet), i32 distortion algorithm (0…17), i32 oversampling (0…4 = ×1…×16), u8 remove DC, u8
bypass, i32 filter type (0…21), u8 smooth filter, i32 impulse (0…58), 49 × i32 controls, an i32 (version > 1, unused
here) and the chorus mode (version 4: 0 wide, 1 mono, 2 stereo). Controls 36/37/38 are input level, mix, output level
of every module (gains `raw/5000`, mix `raw/10000`).

### 2.2 Chain **[DECOMP `FUN_006a8a10` + EXEC]**

The block (interleaved L/R floats, at most 512 frames per call) runs through the modules in the stored order, then,
if enabled, through Image-Line's shared soft clipper (`FUN_005fdb10` family, x87) with threshold 0.78125: above the
threshold |x| bends into `thr + (1 − thr)·(1 − 2^((thr − |x|)·log₂e/(1 − thr)))`, sign kept.

Per module (`FUN_00734260` family): optional ×2^k oversampling (distortion and filter modules only) by cascaded
half-band stages of two branches of three first-order all-pass sections in double precision (tiny outputs below
2^−24 cleared per block); `s = x·in`; the module's processor; `y = (out_proc·mix + s·(1 − mix))·out` in float in that
order; down-sampling; for distortion modules with "remove DC" a DC blocker (x87, coefficient
`(44100/fs)·2.2835·10⁻⁵`, damping 0.96, states below 2^−24 cleared). Bypass leaves the block untouched.

Coefficients that depend on the sample rate are computed when the state is loaded, with the rate the host had then;
only the DC blocker and the chorus follow a later rate change **[EXEC]**. LYNX reproduces this (`stateRate`).

### 2.3 Distortion module **[DECOMP `FUN_00720150` and callees + EXEC]**

Eighteen algorithms: 0 Blood Overdrive's shaper (65 536-point half-sine table, block-rate smoothed controls, final
two-pole low pass), 1 Fast Dist, 2 soft clipper (gain law `(11^(raw/128) − 1)/10`), 3…13 the Harmor shapes (second-order
low pass in double, IPP vector math in FL), 14 wave folder and 15 aperture (both after an asymmetry bias and a crossover
offset, smoothed per block), 16 Abrasive (a band-passed noise from the plug-in's own MT19937, seeded 19650218,
including a quirk of its refill that reads one word before the table), 17 crusher (sample-rate reducer and bit
depth). Delphi run-time routines (`Int`, `Tanh`, `Power`, `Sign`) and x87 `2^x` are reproduced at the FPU precision
Delphi sets (53-bit mantissa).

### 2.4 Filter module **[DECOMP `FUN_0071cf80` and the filter classes + EXEC]**

Twenty-two types on controls 39 (frequency) and 40 (resonance/gain), optionally smoothed: one-pole LP/HP; a trapezoidal
state-variable filter (LP, HP, BP, notch, peak, low/high shelf, all pass); a four-pole low pass in two flavours; three
"Alt" types (×4 oversampled SVF loop with a soft clipper in its feedback); "Low pass 24 Alt" (four-pole state-space
filter in double whose transition matrix is the one-step matrix squared twelve times, saturating first state, DC
follower); two combs (16 384-float delay line, linear interpolation); "Vowel" (eight two-pole formant resonators per
channel blended between three vowel tables); "Phaser 1/2/3" (all-pass SVF chain swept by a triangle LFO with feedback,
wet/dry and a one-pole high pass, three fixed presets of the class's fourteen controls; its synced length control is
never set by the module and stays at 1/192 beat).

### 2.5 Chorus module **[DECOMP `FUN_0074b000` + EXEC]**

An instance of Fruity Delay 3's engine (`TChorusProcessor`, LYNX's `fldelay::Delay3Engine`, REA round 52) with 26
controls starting from a fixed set; the mode picks the delay type (0.33 / 0 / 0.66) and stereo spread (0.84 / 0.5 /
0.5); controls 43…48 map to rate (/768), depth, two times, feedback (both channels) and the tone control
((raw + 3000)/6000). The engine is created at 44.1 kHz and its rate set-up runs again at the host rate, as with Fruity
Delay 3 itself; it never syncs to the tempo.

### 2.6 Amp/cabinet module **[DECOMP `FUN_00734260`, impulse table + DATA]**

An input stereo-separation matrix (controls 41/42: separation > 0 merges with `(5^s − 1)/4`, < 0 separates), a stereo
convolution with the chosen impulse response, an output matrix with gain `44100/fs` and the impulse's level from the
module's 59-entry impulse table (headings without file included). The impulses are the installation's `.wv` files in
`Plugins/Fruity/Effects/Distructor/module/impulses` (55 files). FL convolves with IPP's float FFT (128-tap direct head
plus partitioned tails); LYNX uses the zero-latency partitioned convolver written for Fruity Convolver in double
precision, so this module matches to float rounding, not bit for bit.

### 2.7 Verification **[EXEC]**

`Distructor_x64.dll` / `Module_x64.dll` run unmodified in `run/fx.c` against `dis/cpp/dr` (LYNX), music signal,
44.1 kHz, 512-frame blocks unless stated (`dis/t*.py`).

| Test | Result |
|---|---|
| each distortion algorithm (0…17), 3 random control sets each (music and noise), ×1/×2/×4 oversampling, DC on/off (`t4`) | −∞ for 0…2 and 14…17; the Harmor shapes 3…12: −∞ or −133…−215 dB (last-bit differences of IPP's vector `tanh`/`exp`/`log`); 13 (bit-jam quantizer): −76…−126 dB — a last-bit difference of the logarithm moves a sample across a quantization step (with the default controls 37 of 44 100 samples differ by more than 10⁻⁶) |
| each of the 22 filter types, smoothing on/off, random frequency/resonance (`t9`) | −∞ (44/44) |
| chorus, the three modes, random controls, 44.1/48/96 kHz (`t11`) | −∞ |
| amp/cabinet, impulses 1/5/21/25/40/58 with separation, 44.1/48 kHz (`t12`) | −134.8…−142.8 dB (FFT rounding) |
| random distortion and filter states at 48 and 96 kHz, state loaded at 44.1 kHz before the rate change (`t13`) | −∞ except algorithm 13 (−87.3 / −98.5 dB) |
| the 4 installation slots (DEDZII, Edlan & Ella Noël, Sid De La Cruz ×2), 3 s, input ×1 and ×4 | −∞ (2 slots); −160.8 / −170.8 dB (a Harmor shape); −127.1 / −134.7 dB (cabinet) |
| the 47 database/preset states, 2 s | 22 −∞; 23 between −113.8 and −160.7 dB (all containing a cabinet or a Harmor shape); *Distortion Stereofuzz* −96.7 dB (cabinet followed by distortion); *Distortion Distructed* −55.1 dB (bit-jam algorithm 13 at ×2 oversampling feeding further distortion and a cabinet: its first module alone −61.1 dB, the other three −143…−∞) |
| `tests/DistructorTests.inc` goldens through the LYNX engine (blocks of 512) | pass |

### 2.8 LYNX

- `src/core/DistructorFx.{h,cpp}`: `EffectKind::distructor`. `EffectSlot::state` holds FL's state plus the cabinet
  impulse files the state uses (packed `"LXDT"`, read from the installation at import through the FL data reader), so a
  saved project keeps working without FL. Slot parameters: input / mix / output of each of the four modules and the
  final clipper; FL automation of module controls 36…38 maps onto them. Without a readable state the old generic
  distortion remains the fallback (import report entry).
- Tests: `tests/DistructorTests.inc` (`distructor_engine`: module states of our own per algorithm / filter type /
  chorus / cabinet against DLL goldens) and the real-file check in `flp_real_files` (every installation slot on the
  exact engine, impulses read).

Not confirmed: the Harmor shapes' last-bit agreement (IPP's vector `exp`/`tanh`/`log` rounding not reproduced, which
also decides the bit-jam quantizer's step flips); the
cabinet's IPP FFT rounding; editor-only bytes after the processing order.

---

## 3. Multiband Delay

> First decoding of FL Studio's **Multiband Delay** (LYNX had no equivalent: the slot was dropped on import). Clean-room,
> behavioural only: data layouts, formulas and behaviour in our own words; addresses (`FUN_xxxxxxxx`) only trace the
> evidence. No Image-Line code, preset or audio data was copied into the repository. Evidence markers as in round 61:
> **[DECOMP]** decompiled/disassembled code, **[DATA]** files of the installation or values read from a DLL's data,
> **[EXEC]** the installation's DLLs run unmodified in our harness. What could not be established is marked "not
> confirmed".

- **Scratch directory** (paths below relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r71/mbd/`.
- **Targets:**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/Multiband Delay/Multiband Delay_x64.dll` | `e6954afb70c8f46783951561d35cf5140701ad720f7684784df71c0f87079113` | Delphi front end (FL plugin, editor, state); hosts the engine as a VST 2 plug-in |
  | `Plugins/Fruity/Effects/Multiband Delay/engine_x64.dll` | `95ce161ff75b2f9d2f6d0fcb46807bb21af8f38744b419d24fa3ae8657fbb706` | the DSP: MSVC C++ VST 2 plug-in (class `TestPlug`, export `VSTPluginMain`), statically linked CRT and Intel IPP |

- **Tools:** headless Ghidra 12.1.4 (project `~/gh-r71-mbd`, `gh/imp.sh`, decompilations `out/mbdeng.c`, `out/mbd.c`),
  `objdump -M intel` (`asm/*.s`) for every routine whose operation order matters; our DFM reader `py/dfm.py`.
- **EXEC harness:** `run/fx.c` (round 61's effect harness: unmodified DLL, fault handler, `TerminateProcess`), extended
  with `tp=` (finds the engine's `TestPlug` object by its vtable in the process and dumps fields), `fcall=` kind `S`
  (two-lane double results), `fcsr=` (MXCSR around `fcall`) and `poke=` (writes a CRT dispatch flag in the *loaded*
  engine to exercise its non-FMA paths; the DLL files are never modified).
- **Drivers (our own code):** `py/fst.py` (states of the installation), `py/cmp.py` (DLL vs LYNX), `py/syn.py`
  (random states, rates, blocks, signals), `py/auto.py` (mid-stream changes), `py/teststate.py` (states and goldens of
  `tests/tests.cpp`), `cpp/mbdcmp.cpp` (LYNX's engine on the harness' input), `cpp/mcmp.cpp` / `cpp/tcmp.cpp` (CRT
  math replicas vs the engine's own functions called in place).

**Installation usage [DATA]:** no project of the installation uses the plugin. States: the 17 presets of
`Data/Patches/Plugin presets/Effects/Multiband Delay/` and the plugin-database `.fst` files (5 copies of one state). All
are version 4; two use the linear-phase crossover (*Arpeggiated Textures*, *Spring Glitter*); the "(morph)" presets
store morph 0 (meant for automation).

### 3.1 Front end and state **[DECOMP `FUN_006e4770`, `FUN_006e55a0`, `FUN_006e2fb0`, `FUN_006e5d20` + DATA (DFM) + EXEC]**

- `CreatePlugInstance` (`0x6e2a00`) creates the front end (VMT `0x6e28c8`: 1 Dispatcher, 3 SaveRestoreState
  `FUN_006e4770`, 6 ProcessParam `FUN_006e55a0`, 7 Eff_Render `FUN_006e65b0`, 14 NewTick), which loads `engine_x64.dll`
  as a VST 2 plug-in. Render = de-interleave → the engine's `processReplacing` → interleave; nothing else touches the
  audio (the per-band buffers requested from the host with message `0x3ea` feed the editor's band display — not
  confirmed further).
- **State** (saved as version 7; versions 1…7 read):

  | Field | Content |
  |---|---|
  | i32 | version |
  | i32 | count *n* (absent in version 1: 393), then *n* × i32 raw control values (396 when saved; parameters beyond *n* keep the editor's defaults) |
  | 2 × i32 | editor: selected target (delay/volume/pan) and **selected bank** |
  | u8 (v ≥ 4), u8 (v > 4), u8 (v > 5) | editor options |
  | 16 × 3 × i32 (v ≥ 4) | per band and target the editor mode (0 = on; a non-zero mode stops the cell's changes from reaching the engine) |

- **Parameters** (= the engine's 396): `bank·48 + target·16 + band` for the 8 banks × (delay, volume, pan) × 16 bands,
  then 384 delay scale (−6000…6000), 385 delay ×0.1 (0/1), 386 feedback (0…6000), 387 smoothing time (0…6000, default
  1500), 388 filter mode (0 gentle, 1 steep, 2 linear phase), 389 keep pitch (0/1), 390 morph (0…7000), 391 wet/dry
  (−128…128, default 128), 392 harmonic scale (0/1), 393 weighting (0/1), 394 high-pass frequency (1000…100000), 395 wet
  saturation (0 comp, 1 saturate, 2 clip). Ranges and defaults from the editor's DFM (control `Tag` = parameter).
- **Values handed to the engine:** a bank cell `float((double) raw / 2^30)`; a global
  `float(clamp((double)(raw − min)·(1/(max − min)), 0, 1))`. After a state load the editor re-sends the **selected
  bank's** cells through its drawing path: value `float(raw / 2^29)` clamped to 0…2, stored back as `Round(value·2^29)`
  and sent as that / 2^30, i.e. those cells end up clamped to 0…1 (only that bank) **[EXEC: engine parameters read back
  after loads]**. A grid snap of 1/24 in that path depends on an editor option (off in our runs; not confirmed when on).
- FL automation targets the same 396 indices; ProcessParam sends a global as above and a bank cell as raw / 2^30.
- Latency: switching the crossover to linear phase makes the engine call `setInitialDelay(1536)`, and the front end
  forwards it to FL with host message 30 = 1536 **[EXEC]**.

### 3.2 Engine structure **[DECOMP `FUN_1801d3b50`, `FUN_1801d48d0`, `FUN_1801d3240`, `FUN_1801d1ff0` + EXEC]**

- 16 bands handled as 8 **band pairs**, each a 4-lane float SIMD unit (band 2k left/right, band 2k + 1 left/right) with
  ten biquads, two FFT convolvers, one 4-lane delay line and 4-lane output gains. The constructor runs the sample-rate
  set-up at 44.1 kHz.
- Biquad (all ten, all modes): `y = (((x₁·b1 + x·b0) + b2·x₂) + y₁·a1) + a2·y₂` in float (`a1`, `a2` stored with the
  feedback sign), then shift.
- **Parameter set** (`FUN_1801d1ff0`): cells, feedback (386), morph, harmonic, weighting mark the voices for a recompute
  at the next render; 395 saturation type = 0 (< 0.333000004), 2 (≥ 0.666000009), else 3 ("saturate"); 387 smoothing
  coefficient of every delay line `float(1 − pow(0.09999999999999998, 1/(p·fs)))` (p = 0 → 1); 388 mode = gentle
  (< 0.333332986), linear (≥ 0.666665971), else steep, latency 1536 for linear; 389 keep pitch (§3.2.4).
- **Sample rate** (`FUN_1801d3240`, rounding to nearest): high-pass π/fs factor `float(1/fs)·π_f`; gain smoothing
  `float(1 − pow(0.09999999999999998, 20/fs))`; per pair a delay buffer of the next power of two ≥ `int(fs·1.0 + 0.5)`
  frames, zeroed; the sample rate of biquads 0…7 (**not of the gentle filters 8 and 9**, which therefore stay designed
  for 44.1 kHz at every rate — an FL quirk, reproduced); every parameter re-applied; compressor coefficients
  `1 − float(pow(0.1, 1/(t·fs)))` (attack 10 ms, release 100 ms); the crossover design (§3.2.1); keep-pitch fade step
  `1/float(fs·0.1)`; mix/high-pass smoothing `float(1 − pow(0.09999999999999998, 200/fs))`; dry delay of 1546 frames.

#### 3.2.1 Crossovers **[DECOMP `FUN_1801d28c0`, `FUN_1801d6150`, `FUN_1801d66e0`, sincos kernel `FUN_1801d9d40` (asm) + EXEC]**

Crossover frequencies F = 31, 47, 71, 112, 172, 264, 413, 624, 958, 1502, 2267, 3611, 5458, 8391, 12663, 18994 Hz
**[DATA]**.

- **Gentle** (one filter per band, gain 1 for band 0, ½ otherwise, Q = ½): band 0 first-order low pass at 31 Hz, bands
  1…14 constant-skirt band passes at F[k], band 15 first-order high pass at 18994 Hz; band 15 then also runs through a
  second filter, twice the all pass at 18994 Hz (`b = 2·(1 − s, −2c, 1 + s)/(1 + s)`, `a = (2c, s − 1)/(1 + s)`).
- **Steep** (eight sections): band 0 four low-pass sections at 31 Hz (Q via `0.5/Q` = 0.92387972…, 0.38268341…,
  alternating; the first scaled by `0.5 · 2.8283999`); bands 1…15 four high-pass sections at F[k−1] (Q 0.5411959886…,
  1.3065630197…, alternating; the first scaled by the same 1.4142) then four low-pass sections at F[k] (band 15: four all
  passes at 18994 Hz).
- Design (`FUN_1801d6150`): RBJ cookbook forms in double with `w = (f·2π)·(1/fs)` (band-0 steep: `194.778…/fs`),
  `α = (0.5/Q)·sin w`, coefficients `float(b/a0)`, `−float(a/a0)`, then `b·gain` in float; first-order forms with
  `t = float(tan(f·π/fs))`. The sine/cosine come from the engine's own two-lane polynomial sine evaluated at `w` and
  `|w| + π/2` (Cody–Waite reduction by π in four parts, degree-13 odd polynomial) — reproduced bit for bit
  (`cpp/mcmp.cpp`: 36126/36126); glibc's `sin`/`cos` differ in 30…70 % of the cases.
- **Linear phase:** per band a 1025-bin brick-wall mask (bins `int(2048/fs·F[k−1]) … int(2048/fs·F[k])`, band 15 up to
  Nyquist), turned into a zero-phase response centred on 1024 by IPP FFTs, the middle 1024 taps under a sine window
  `sin(i·π/1023)`; convolution by overlap-add, 1024-frame blocks, complex FFT of 2048, output read one block later
  (crossover latency 1024 + 512 = 1536, matched by the dry path).

#### 3.2.2 Per band **[DECOMP `FUN_1801d3f90` (asm) + EXEC]**

1. Crossover filter(s) of the mode on (L, R, L, R).
2. Delay line (not keep pitch): `d = (target − d)·coef + d` per band; `i = trunc d`, `f = d − i`; read
   `(1 − f)·buf[(w − i) & m] + f·buf[(w − i − 1) & m]`; write `clamp(read·fb, −1, 1) + filtered`; `w = (w + 1) & m`.
3. Output `read·gain`, then `gain = (target − gain)·c + gain` (c = the 20/fs smoothing).

#### 3.2.3 Recompute (inside the render) **[DECOMP `FUN_1801d2250` (asm) + EXEC]**

Morph `m = p·7`, bank `i = trunc m`, `f = m − i`, `g = 1 − f` (bank 8 is read with weight 0). Per band:
- volume `v = g·V_i + f·V_{i+1}`, pan `p = min(max(g·P_i + f·P_{i+1}, 0), 1)`; gains
  `((v²·(1.39229143v))·s − (0.327543139v·v)·s) + (v·s)·0.349678934` with `s = √(1 − p)` (left), `√p` (right); a band
  switched off by the editor gets 0.
- delay in samples (≥ 1): linear scale: `trunc((double)(|2·scale − 1|·d')·fs·k)`, `d' = d` for scale ≥ ½ else `1 − d`,
  `k = 1` or `0.10000000149011612` with ×0.1; harmonic scale:
  `trunc(min(powf(2, n·(−1/12) − (d − ½)·4)·0.00191112794, 1)·fs)`, `n = trunc((scale − ½)·96)`.
- feedback `p₃₈₆·0.95`, or with weighting `powf(0.1, 1/t)`, `t = lerp(T, 5·p₃₈₆·0.95)·fs / max(samples, trunc(fs·0.001))`,
  `T = 0, 0.02, 0.1, 0.5, 2.5, 10, 10` seconds **[DATA]**.

#### 3.2.4 Keep pitch **[DECOMP `FUN_1801d6800`, `FUN_1801d1750` + EXEC]**

Two integer taps per band and a cross-fade instead of the gliding delay: at the start of a render after a recompute
(and when no fade runs) the new delays go into the slot that fades in; the fade moves ±`1/(0.1·fs)` per sample
(100 ms); reads are tap 0 alone (fade 1), tap 1 alone (fade 0), else `(1 − a)·tap₁ + a·tap₀`. Switching the option on
fills both taps with the current targets.

#### 3.2.5 Output **[DECOMP `FUN_1801d1750` (asm) + EXEC]**

The render runs with **MXCSR | 0xE040 (flush to zero, denormals as zero, rounding toward zero)**, so everything of
§3.2.2–3.2.5 including the recompute rounds toward zero. Per sample: the band outputs summed lane by lane in pair order; L =
lanes 0 + 2, R = 1 + 3; keep-pitch fade step; high pass: smoothed parameter `s`, `f = powf(10, 2s)·10` (10 Hz…1 kHz),
`t = tanf(f·π/fs)`, `a0 = 1/(t + 1)`, `y = (a0·x − a0·x₁) − ((t − 1)·a0)·y₁`; saturation 0: compressor (envelope
`(|y| − e)·(attack or release) + e`, divide when the envelope exceeds 1), "saturate": `tanhf`, clip: ±1; dry delayed by
the latency through a 1546-frame ring; mix smoothed (200/fs) `out = m·wet + (1 − m)·dry`.

#### 3.2.6 CRT math **[DECOMP + EXEC `fcall`, both rounding modes, FMA and non-FMA paths]**

The engine's `powf` equals LYNX's `ildsp::powf_` (round 60) on all 80001 test inputs, its `tanf` (|x| ≤ π/4) equals
round 45's replica, its `tanhf` was decoded (single precision; exp table 2^(j/32) as a 9-bit head and a 33-bit tail,
regenerated from that definition) and matches 80001/80001 in round-to-nearest and toward-zero. Its double `pow`
(smoothing coefficients, computed once) differs from glibc in about 1 of 600 random arguments by one ulp (never at
22.05/32/44.1/48/88.2/96/192 kHz in our tests; not replicated).

### 3.3 What LYNX does

- **`EffectKind::multibandDelay`** (`core/MultibandDelayFx`): FL's raw state in `EffectSlot::state`, the twelve global
  controls as slot parameters ((raw − min)/range) so the generic editor shows and edits them and FL automation of
  parameters 384…395 maps onto them (bank-cell automation is not mapped). The engine follows FL's order of operations
  (construction at 44.1 kHz, state parameters, sample rate) and reports 1536 samples of latency in linear-phase mode.
- Linear phase: the FIR and the FFTs are computed in double precision with our own FFT, not IPP (§3.4).
- Added to the mixer's effect menu with a default state (bank cells: volume 1, pan centre, delay 0).

### 3.4 Verification **[EXEC]**

Error measure `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels, "−∞" = bit-identical; 2 s of a synthetic music signal
at 44.1 kHz, 512-frame blocks, unless stated.

| States | Result |
|---|---|
| 15 presets with gentle/steep crossover + 5 database copies | −∞ (all) |
| *Arpeggiated Textures*, *Spring Glitter* (linear phase) | −127.6 dB, −121.2 dB |
| 28 random states (cells, globals, every saturation / scale / weighting / keep-pitch combination) at 32, 44.1, 48, 96 kHz, blocks 1024, 512, 256, 100 | IIR crossovers −∞ (23/23); linear phase −119.7…−149.8 dB (5) |
| mid-stream changes of morph, keep pitch, scale, feedback, smoothing, mode, saturation, high pass, weighting, mix (5 runs × 2 rates/blocks) | −∞ while the crossover is IIR; −124.9…−131.2 dB with linear-phase segments |
| `tests/tests.cpp` `multiband_delay_exact`: two IIR states bit-exact through the mixer effect with 700/333-frame host blocks (goldens from the DLL with 512-frame blocks); one linear-phase state within 2·10⁻⁶ | pass |

### 3.5 Not confirmed

- The editor's grid snap when re-sending the selected bank (§3.1); the per-band buffers of host message `0x3ea`.
- Linear phase exactness: IPP's single-precision FFTs (`FUN_180001100`/`FUN_180001070`, multiply `FUN_180143d70`) were
  not reproduced; their rounding is the remaining −120…−150 dB.
- The engine's double `pow` at unusual sample rates (one-ulp differences, see §3.2.6).


---

## 4. Fruity Scratcher — not ported **[DATA + DECOMP]**

Round 20 §2 decoded the engine. Its only installation use (the render test) loads
`%FLStudioFactoryData%\Data\Patches\Packs\Loops\DL Breaker.wav`, which is an **Ogg Vorbis WAV (format 0x674F)**: the
plug-in asks the host for the sample already converted to 16-bit / 2 channels / 44.1 kHz (host interface methods at
+0x68 load, +0x70 data, +0x78 free; `FUN_0072be20`), so the result depends on FL's own Vorbis decoder and converter,
and the movement comes from `0xDF` pattern control events whose scaling is not confirmed. Neither can be reproduced
exactly, so the Scratcher stays unported (LYNX drops it with a report entry, as before).

## 5. Fruity Squeeze

Already LYNX's exact `EffectKind::squeeze` (round 29 §2); both installation slots import to it. Nothing to do.

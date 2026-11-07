# Behavioural analysis of FL Studio 2026: round 35 (REA) — Vocodex: the complete vocoder engine

> **Nature of this document.** Continuation of `REA_rodada14.md` §6, `REA_rodada20.md` §4.2 and `REA_rodada22.md` §1.6
> (Vocodex). *Clean-room* reverse engineering, behavioural only. No Image-Line (nor Intel IPP) code or pseudocode was
> copied: what is here are data layouts, formulas and behaviours described in our own words. Addresses
> (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No encrypted content was touched (no FLEX
> pack was opened). Plugin states used as test input (the Vocodex demo instances) were read from the user's own
> installation into the scratch directory only; no Image-Line audio was copied anywhere.
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation (**[DECOMP]**), from
> the official Image-Line manual (**[DOC]**, with URL), from the data (**[DATA]**) or from running the installation's
> own DLLs, unmodified (**[EXEC]**). Where there was not enough evidence, the item is marked **"not confirmed"**.
> Inferences made *from* the code are stated explicitly as **inference**. Results of our own numerical model (which
> implements only what is marked DECOMP/EXEC) are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-14. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Targets** (SHA-256 hashed against the installation in this round):

  | File | SHA-256 | Language | Role / key functions |
  |---|---|---|---|
  | `Plugins/Fruity/Effects/Vocodex/Vocodex_x64.dll` | `43d10692ea04457018138110a1a3035d99ed8b40a7f52e67a3109c22ca714bb0` (= rounds 14/20/22) | Delphi (`TTestPlug`, VMT `0x84e438`) | the effect. VMT slots: 1 Dispatcher `FUN_0084e660`, 3 SaveRestoreState `FUN_0084efb0`, 5 ProcessEvent `FUN_00853c30`, 6 ProcessParam `FUN_00853e80`, **7 Eff_Render `FUN_00856350`**; slots 8…19 are the base stub `0x40c5a0`. Band set-up `FUN_00851dd0`; allocation/draft decision `FUN_00851880`; reset `FUN_008537a0`; per-band processing `FUN_00855cd0` (+ filter helper `FUN_00855a60`, gain helpers `FUN_00855410`/`FUN_00855460`); pass-through `FUN_008557c0`; tail length `FUN_00853ce0`; pass-filter design `FUN_00853de0`; saturation table `FUN_00859150`; internal synth `FUN_008506a0`/`FUN_00850aa0`; sub-plugin wrapper `FUN_0084c570`/`FUN_0084caa0`/`FUN_0084ca00`; constructor `FUN_0084f730`; preview sample `FUN_00851370` |
  | same DLL, library code | — | Delphi + SIMD/x87 kernels | 64-bit band-pass `FUN_00626e20`, 32-bit stereo biquad `FUN_00626a70`, mono biquad `FUN_00626890`, biquad design `FUN_00626690`/`FUN_006260c0`/`FUN_00626730`, smoother stage `FUN_00606600` (x87) + set-up `FUN_00606730`/`FUN_006064e0`, peak follower class (VMT `0x787478`: `FUN_00787de0`, `FUN_00788010`, `FUN_0062b2f0`, sliding max `FUN_00787e50`), RMS class (VMT `0x787610`: `FUN_00788190`, `FUN_00788330`, moving mean `FUN_00788200` (x87)), consonant detector (VMT `0x7877b8`: `FUN_007883d0`, `FUN_007884c0`, `FUN_00788680`, `FUN_007887b0`, `FUN_0062b130`), half-band up/down `FUN_0077b080`/`FUN_0077b120` with kernels `FUN_0077ab60`/`FUN_0077ad30`, decimate-by-pairs `FUN_0062d3a0`, state rounding `FUN_006298a0`, draft FIFO `FUN_008373e0`/`FUN_00837510`, gain ramps `FUN_0062c760`/`FUN_006174b0`/`FUN_006175a0`/`FUN_0062a1b0`/`FUN_0062aee0`, pan law `FUN_0062c860`, waveshaper `FUN_0062bc80`, map evaluation `FUN_00783730`/`FUN_00782b50`, frequency scale `FUN_0062cb20`/`FUN_0062ccc0`/`FUN_0062cd90`, power helper `FUN_005fd490` |
  | `Shared/SG_x64.dll` | `ad567e65e2aac8314d23f2322f4d17e1b5529a09f64c43d596189582f006f6ac` | Delphi plugin | Soundgoodizer instance hosted by Vocodex (EXEC only) |
  | `Plugins/Fruity/Effects/Vocodex/Synthesizer/Sytrus_x64.dll` | `46ea9f910dcc315b0bb8dd3b67dfc8562ff427fc4d291cc5c5c2de07e6f922eb` (≠ `Generators/Sytrus/Sytrus_x64.dll` `d52a97c5…4f15`) | Delphi plugin | internal carrier synth (DECOMP of the Vocodex side only) |
  | `Shared/dsp_ippv2_x64.dll` | `4dbb9e954f5cc3c83e7d40872a7d616d5e6fb6280b3d377260c9438ae551e0d4` | Intel IPP build | vector primitives; `ippsRandUniform_Direct_32f` generic path `0x1815ed2e0` (disassembly) |
  | `Plugins/Fruity/Effects/Vocodex/Data/SG.dat` | `f67dd6a1472bdca169208b9ee8dc7a77d6315faac3e59809f83244b55d89ee53` | data | Soundgoodizer state loaded by the constructor |

- **Tools:** Ghidra 12.1.4 headless (`GHIDRA_HEADLESS_MAXMEM=2G`, run only when no other Ghidra was running), new
  project `/tmp/r35gh_Vcx` (full auto-analysis), runner `r35/g.sh`, scripts `r35/scripts/` (copies of round 33's);
  dumps `r35/out/main.c` (whole class range `0x84b000…0x85a000`), `lib.c`, `lib2.c`, `lib3.c`, `lib4.c` and their
  annotated versions `*_a.c` (`r35/py/annot.py` inlines static constants from the PE and runtime globals read by EXEC);
  `objdump` for instruction-level checks (x87 kernels, hidden call arguments: `r35/out/b1dd0.asm`, `r6350.asm`).
  Round 20's dumps (`r20/voc/vx_*.c`) were re-read.
- **EXEC harness** (our own C, Zig 0.14.1 → Windows x64; `SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|
  SEM_NOOPENFILEERRORBOX)`, vectored fault handler that ends the process, `TerminateProcess` at the end so DLL
  finalisation never runs, every run under `timeout`; no process or dialog was left behind):
  - `r35/run/vcx.c` → `vcx_<n>.exe` (built by `r35/run/build.sh` under a fresh name, because overwriting an `.exe` in
    place on the WSL share was unreliable). Loads `Vocodex_x64.dll` (or any Fruity effect DLL) unmodified, creates it
    with `CreatePlugInstance` and a minimal fake host, restores a state blob through its SaveRestoreState (read-only
    `IStream`), optionally sends Dispatcher messages (`disp=id,value`, e.g. ProcessMode), sets rate/block, parameters
    through ProcessParam (flag 1) and renders through Eff_Render. The fake host's **slot 36 (`+0x120`, side-input
    read)** returns the modulator file for index `modk` and the carrier file for index `cark` (block-aligned
    pointers), so ModIn/CarIn routing is exercised exactly as in FL (round 22 §1.2). The plugin's own input can be the
    modulator, the carrier, zero or a separate file. It also dumps plugin fields and arrays through pointer paths,
    resolves function-pointer globals to export names of the loaded modules (`ipp=`, `fp=`) and reads runtime
    globals (`gd=`, `dv=`, `pd=`).
  - `r35/run/rnd/rnd.c` → `rnd.exe`: calls the installation's `ippsRandUniform_Direct_32f` directly.
- **Dataset:** the 46 `.flp` under `Data/Demo projects` (parsed with `r7/flpev.py`; survey `r35/py/survey.py`, state
  parser `r35/py/vcxstate.py`); the 10 Vocodex instances extracted to `r35/data/states/*.bin`; routing with
  `r20/voc/route.py`.
- **Manual pages read:** `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Vocodex_parameters.htm`,
  `…/plugins/Vocodex_inputs.htm`, `…/plugins/Vocodex.htm`.

---

## 1. Demo survey **[DATA]**

Ten Vocodex instances live in the demo projects (all state version 8); routing resolved with the rule of round 22
§1.2 (side input k = k-th track routed into the Vocodex track, in track order).

| Demo (track) | MOD ← | CAR ← | Bands / order / unison | Draft (stored) | Wet / SG | Pass-through | Noise / contour | Maps edited |
|---|---|---|---|---|---|---|---|---|
| "Ookay – Thief" 27 and "NewStuff" 27 | own input (24 "Main Vox") | **internal synth** (tone `"PWM"`, notes 51, 63, 66, 73) | 47 / 2 / 0 | **on** | 2000 / 150 | Mod 12 800 (HP 55 000) | 0 / CAR + CONSONANTS → MOD | — |
| "SeamlessR – Bass Antics" 3 "Voc" | side 1 "Mod" | side 2 "Car" | 47 / 2 / **1** (pitch 650) | off | 1000 / 150 | Car 6300 (LP 14 304) | — | gain ×; ModPitch −1200, ModBW 2456, BW 653 |
| "Olbaid – Harmless Essentials" 71 | side 2 "Modulador" | side 1 "Carrier" | 47 / 2 / 0 | **on** | 1000 / 150 | Mod 12 800 (HP 55 000) | 0 / CONSONANTS → MOD | — |
| "Vocodex demo" 2 "VCX" | side 1 "Vocals" | own input | 24 (Bark map) / 2 / 0 | off | 1168 / 250 | — | 7600 (HP 58 000) / CONSONANTS → NOISE | band distribution |
| "JuiceBass – Menagerie" 7 | side 2 "Modulator" | side 1 "Carrier" | 47 / **4** / **1** (pitch 0) | off | 1000 / 150 | — | — | gain ×, band width, pitch; ModPitch −996, ModBW 1504 |
| "Treben – RawFL" 15 | own input | side 2 "BASS 6" | 47 / **1** / 0 | **on** | 1268 / 124 | Mod 13 400 (no HP) | 600 (HP 56 920) / — | ModBW 2624, BW 513 |
| "Treben – All the same" 27 / 29 / 30 | own input | own input (same signal) | 14 / 32 / 32, order 3 | off | 1000 / 226 / 546 / 546 | Mod 16 000 (HP 24 330 / 26 296) | — / CONSONANTS → MOD | Hold = Att = Rel = 0 |

Hold mode is PEAK (0) everywhere; MIN TIMES on, THREADED on and the "subtractive gate" flag (§2.1) on in all ten.
**Every demo has SG > 0** (Soundgoodizer active). The two "Immediate" style instances use the internal synth.

---

## 2. State and parameters

### 2.1 State layout **[DECOMP `FUN_0084efb0` + DATA]** (confirms round 20 §4.2.1, with these corrections/additions)

The layout of round 20 (version, 13 maps, 20 parameters, `0x24`-byte block, 13 option bytes, tone name, note list)
is confirmed. New readings:

- **Block `+0x170`:** `i32` hold mode (**0 = PEAK, 1 = RMS**, §4.2), `i32` filter order K (1…4), `i32` **unison
  order U (0…4, number of extra analysis voices; the UI shows U + 1, the engine clamps the UI value to 5 and stores
  value − 1)**, `i32` bands N (5…100), `i32` **Draft (0 = Draft button on)**, `i32` ModIn, `i32` CarIn, then bytes
  L-R encoding, MIN TIMES, TO MOD, THREADED, **subtractive-gate flag `+0x190`** (preset to 1 by the loader before the
  block is read, so the stored byte wins from version 5 on — see §12.2; toggled by the options-menu item
  "Subtractive denoising", `FUN_0084dc20`; meaning in §4.2), TO NOISE, FROM CAR, FROM CONSONANTS.
- **The 13 option bytes (`+0x1FC`)** select, per map m, the x-coordinate at which the map is read for a band:
  0 → the band's own frequency, 1 → the frequency **after the modulator pitch shift** (§3.1). Data: maps 3 and 4 use 1
  in every state. (Map 3 is always read at the shifted position regardless of its byte.)
- **Centred maps:** maps 1 (pan), 2 (gain offset) and 10 (pitch) are evaluated as `y − 0.5` (constructor flags
  `0x406`, default values 0.5); maps 3, 4, 11 default to 0, all others to 0.5 (mask `0x818`) **[DECOMP `FUN_0084f730`;
  EXEC: dumped pitch/pan/offset fields]**.
- **Map evaluation:** straight segments between points (all demo maps have zero tension). Curved segments (tension
  ≠ 0) go through the FL envelope curve code `FUN_00782b50` — decoded in round 173d3, §12.1 (the same curves as
  Love Philter's).
- Parameter values are clamped to each control's range on load **[DECOMP]**.

### 2.2 Parameter laws **[DECOMP `FUN_00853e80`, `FUN_00851dd0`; EXEC field dumps]** (completes round 20 §4.2.2)

`v` = stored integer. All arithmetic in float unless stated.

| Tag | Name | Law |
|---|---|---|
| 0 | Wet | `W = (7^(v/1000) − 1)/6` (1 at 1000) |
| 1 | SG | forwarded unchanged as **parameter 5** of the Soundgoodizer sub-plugin (§7); 0 = stage skipped |
| 2 | Mod pass-through | `G_m = (11^(v/12800) − 1)/10`; ×4 if TO MOD and FROM CAR (and 0 if, in addition, tag 13 = 1) |
| 3 | Mod HP | 0 = none; else 2 cascaded biquads at `f = 40·375^(v/65536)` Hz (§6.3) |
| 4 | Car pass-through | `G_c = (11^(v/12800) − 1)/10` |
| 5 | Car LP | 65 536 = none; else 2 cascaded biquads, same Hz law |
| 6 | Carrier noise | `A = 0.5·(7^(v/12800) − 1)/6`; ×4 if TO NOISE and FROM CAR |
| 7 | Carrier-noise HP | 0 = none; else 2 cascaded biquads, same Hz law |
| 8 / 9 / 11 | Hold / Attack / Release | `(501^(v/10000) − 1)` × 0.5 ms / 2 ms / 4 ms (samples = × fs), then per-band map multipliers and minimum times (§4) |
| 10 / 12 | Attack / Release curve | smoother order `2^(v/1000)` (1…4) (§4.4) |
| **13** | **Freeze** (round 20 called it "HoldEnv") | 1 = every band envelope held at its last value; consonant contour forced to 0; see tag 2 |
| 14 | Band width | `BW = (5^(v/1000) − 1)/2` |
| 15 | Flatness | `φ = v/100` |
| **16** | **Modulator band width** | analysis bands widened by `2^(v/1000)/4` (1 at 2000) |
| 17 | Modulator pitch | analysis bands multiplied by `2^(−v/1200)` (cents) |
| **18** | **Unison pitch** | voice detune `t·v/1000` **octaves**, `t` from §5 |
| **19** | **Unison pan** | voice pan spread `v/128` (§5) |

**Draft** is effective only when the Draft field is 0 **and** the host has not set bit value 2 of the ProcessMode
dispatcher message (`FUN_0084e660`: `+0x515 = (value & 2) ≠ 0`; `FUN_00851880`: `draft = Draft field = 0 and not
+0x515`) **[DECOMP + EXEC: `disp=1,2` turns draft off, `disp=1,16` does not]**. The manual says Draft "turns off
automatically during rendering when 'HQ for all plugins' is enabled" **[DOC parameters page]**; that this option is
the value-2 bit is an **inference**.

---

## 3. Band layout and filters

### 3.1 Band centres, widths and gains **[DECOMP `FUN_00851dd0`; EXEC: every field of every band and sub-filter of
two configurations dumped and matched by the model to ≤ 1e−6 relative (`r35/py/cmpsetup.py`, 0 mismatches)]**

Notation: band b = 0…N−1; voices v = −U…0 analyse the **modulator** (unison), v = 1 filters the **carrier**;
`C₂ = 440·2^(−33/12)` = 65.406 Hz.

1. **Centre:** `f_b = S(map8(b/(N−1)))`, at least 20 Hz, with the scale `S` of round 20 §4.2.3 (verified again).
   Octave position `o_b = log₂(f_b/C₂)`.
2. **Modulator pitch per voice:** `p_v = 2^(−ModPitch/1200 − 2·(map10 − 0.5) + t_v·FatPitch/1000)` (map read at the
   band position `x₀ = S⁻¹(f_b)`); carrier voice `p = 1`. Shifted position `x₁ = S⁻¹(f_b·p_v)`.
3. **Per-band map values** (read at `x₀` or `x₁` per option byte, §2.1), with `c₃(m) = (3^(2m) − 1)/2`:
   gain `G = c₃(map0)`; pan `P = 2·(map1 − 0.5)`; offset `O = ±(1025^|m| − 1)/1024`, `m = 2·(map2 − 0.5)`; gate
   threshold `T = (1025^map3 − 1)/1024`; per-band modulator pass-through `M = c₃(map4)/2`; hold/attack/release
   multipliers `2^(6·map − 3)` (×0.125…×8, maps 5/6/7); saturation mix `map11` (off when 0); width multiplier
   `2^(4·map9)`.
4. **Width:** `w = clamp(d·F, 1/60, 8)` octaves, where `d` = distance in octaves to the neighbour band (the mean of
   both sides for inner bands) and `F = BW/4·2^(4·map9)·(2^(ModBW/1000)/4 for modulator voices, 1 for the carrier)`.
5. **Gains:** `g₀ = (1 + ½·[RMS])·(1/(K − φ(K−1)) if K > 1)·√(4/√N)·2^(K−1)·3`; modulator voice gain
   `g_v = g₀·G·√(1/w)`, carrier gain `g_c = g₀·√(1/w_c)`. The gate threshold becomes `T/max(g_v, 2⁻¹⁶)` (negated
   when the subtractive flag is set). The per-band pass-through gain is `M·g₀·√(1/w)·¼`, multiplied in **once per
   modulator voice** (so with unison it is applied U + 1 times — as coded).
6. **Sub-filters** (K per voice): bandwidth `β = min(K·w, 2)`; if K > 1, `β ← β/(1 + φ/2)`, centre offset
   `δ_i = (i/(K−1) − ½)·φ·β` octaves, then for K = 3 the outer two use `β/2`, for K = 4 the outer two use `β·4/11`.
   Centre `f_i = C₂·p_v·2^(o_b + δ_i)`.
7. **Design:** constant-0-dB-peak band-pass, `α = sin ω·sinh(ln2/2·β·ω/sin ω)`, `b₀ = α/(1+α)`, `b₁ = 0`,
   `b₂ = −b₀`, `a₁ = −2cos ω/(1+α)`, `a₂ = (1−α)/(1+α)`, `ω = 2πf/fs` **[DECOMP `FUN_00626690`/`FUN_006260c0` type 2;
   EXEC coefficients to 1e−9]**. The design frequency is capped at 0.4·fs.

### 3.2 Multirate structure **[DECOMP `FUN_00856350`, `FUN_00855a60`, `FUN_0077b080/120`, kernels; EXEC: model match]**

- **Oversampled voices:** if any sub-filter of a voice has `f_i·2^(2β_i) > 11 050` Hz, that voice filters a **2×
  upsampled** copy of its input (designed at `f/2`), and its output is brought back to fs by **averaging consecutive
  pairs**. The 2× signal is made by a polyphase all-pass half-band pair (two 3-section all-pass chains; coefficients
  read at run time: A = 0.147377, 0.482469, 0.883005; B = 0.039152, 0.302647, 0.674616; the down-sampler sums the
  chains ×0.5) **[EXEC `pd=8df680`]**.
- **Draft only:** each band runs at the octave level `ℓ` (0…4): `ℓ` grows while `ℓ < 4` and `r·2^(ℓ+1) < 11 050`,
  where `r` is the highest `f_i·2^(2β_i)` of the band's voices (11 050 if a voice is oversampled). Level `ℓ` signals
  are made by successive half-band down-samplings; the band's filters are designed at `f·2^ℓ`, its envelope times and
  hold window are divided by `2^ℓ`; the per-level sums are recombined from the coarsest level up with half-band
  up-samplers (one state per level). Draft also re-blocks everything into multiples of **16 frames** through FIFOs
  (output FIFO pre-filled with 16 zero frames → **16 samples of latency**) **[DECOMP `FUN_008373e0`/`FUN_00837510`]**.
  Without Draft only the native level and the 2× copy exist.
- Band-pass filtering and summing are in **double precision**; the half-band kernels and the output-path biquads run
  on float buffers and round their state to float once per block (`FUN_006298a0`).

---

## 4. Envelope followers

Per band and per modulator voice (block of n frames at the band's level) **[DECOMP `FUN_00855cd0`; EXEC]**:

### 4.1 Detector

1. Band-pass the **stereo** modulator with the voice's K sections (64-bit).
2. If the per-band pass-through gain is non-zero, add the band signal × that gain straight into the output sum.
3. **PEAK (hold mode 0):** `e = max(|L|, |R|)` (float), then the **maximum over the current and W − 1 previous
   samples**. **RMS (hold mode 1):** `e = √(mean over W samples of (L² + R²)/2)` (running double-precision sum,
   float ring) **[DECOMP `FUN_00787e50`, `FUN_00788200`; EXEC: PEAK vs "W previous" alternative distinguished at
   −122 vs −57 dB]**.
   The manual's "modulator band analysis is performed on the mono sum of the L + R channels" **[DOC inputs page]** is
   therefore not what the code does: the bands see both channels and the detector combines them as above.
4. **Window W** = `round(max(hold, minT)·2^−ℓ)` samples (round-half-even), `hold = (501^(Hold/10000) − 1)·0.5 ms·fs·
   (hold map multiplier)`; `minT` (MIN TIMES on) = `fs/clamp(f₀·2^(−3−β₀), 40, 16000)` from the voice's first
   sub-filter (×2 in RMS mode).

### 4.2 Gate and saturation

- **Gate** (map 3): with the subtractive flag (all data) `e ← max(e − |T|, 0)`; without it values below T are zeroed.
- **Saturation** (only when map 11 ≠ 0; no demo): `e ← e·g_v·(1 − mix) + mix·4·table(e·g_v/4)/32768`, table of
  16 384 linear-interpolated steps holding `8191.75·c₃(map12(x))` at `x = ln(1 + 8u)/ln 9`, u = 0…1; the voice's pan
  gains then use 1 instead of `g_v` **[DECOMP `FUN_00859150`, `FUN_0062bc80`; EXEC −99 dB, −141 dB since round
  173d3]**. The kernel `FUN_0062bc80` runs on the x87 with extended precision and truncating rounding (control word
  `0x0F3F`): the table index is `trunc(x·scale)`, and the interpolation and `x·dry + v·wet` are evaluated in extended
  precision and rounded to float once at the end. The table itself is built with one map cursor stepped forward along
  the log-spaced x values (§12.1). The −99 dB of round 35 came from a float model of this kernel.

### 4.3 Smoothing **[DECOMP `FUN_00606730`, `FUN_006064e0`, `FUN_00606600` (x87); EXEC]**

- A cascade of one-pole stages `y ← x + c·(y − x)`, `c = c_att` when the input is ≥ the stage's value, else `c_rel`;
  `c = exp(ln 0.01/max(t, 1))` (t in samples: the stage covers 99 % of a step in t samples, i.e. −40 dB). The running
  value is kept in **double precision** (x87), output to float; at the end of each block values below 5.96·10⁻⁸ are
  zeroed. (Running it in float gives −93 dB instead of −158 dB.)
- Stage count `n = round(order + 0.5)` (round-half-even) for order `2^(Curve/1000)`; each stage's time is
  `t/√order`, the last fractional stage scaled by `1 − (n − order)`, later stages pass through.
- Attack `t_a = max((501^(Att/10000) − 1)·2 ms·fs·(att map), minT₂)`, release `t_r = max((501^(Rel/10000) − 1)·4 ms·
  fs·(rel map), minT₂)`, both ×2^−ℓ; `minT₂` (MIN TIMES) = `fs/clamp(f_c₀·2^(−β_c₀), 40, 800)` from the **carrier**
  voice's first sub-filter. These are the time laws of round 20, now confirmed as "time to 1 %".
- Freeze (tag 13 = 1): the envelope is replaced by its last value.

### 4.4 Voice output and band product

`pan = clamp(P (+ unison pan, §5), −1, 1)`; gain `g = (sat ? 1 : g_v)·g_c/(U + 1)`; `pan ≥ 0`: `R = g`,
`L = (1 − pan)·g`; `pan < 0`: `L = g`, `R = (1 + pan)·g`. The stereo envelope is the sum over voices, plus `O·g_c`
(clamped at 0 when O < 0). **Band output = stereo envelope × carrier band-pass (voice 1 on carrier + noise)**,
accumulated over bands in double precision, then converted to float **[DECOMP + EXEC]**.

---

## 5. Unison **[DECOMP `FUN_00851dd0` + table `0x8cc36c`; EXEC −139…−158 dB for U = 1, 2, 3, 4]**

U extra modulator voices (v = −U…0) analyse the same band at detuned positions; each has its own filters, follower
and smoother, and they are summed with the pan law above. Detune `t_v·FatPitch/1000` octaves with
`t = (−1, +1)` for U = 1, `(−1, 0, +1)` for U = 2, `(−1, +⅓, −⅓, +1)` for U = 3, `(−1, +½, 0, −½, +1)` for U = 4
(listed for v = 0, −1, −2, …). Pan offset `(((b − v) mod (U + 1))/U − ½)·FatPan/128` added to the map pan.
"SeamlessR" and "Menagerie" use U = 1 (Menagerie with FatPitch = FatPan = 0).

---

## 6. Carrier noise, contour, pass-through, output

### 6.1 Carrier noise **[DECOMP render; EXEC: generator bit-exact]**

- Per block, 2n uniform values in [−A, A] (interleaved L/R) from `ippsRandUniform_Direct_32f` with a 32-bit seed
  that starts at 0, persists across blocks and is reset by Flush. The generator (read in the IPP DLL's generic code
  path and **matched bit for bit** in three successive calls, `r35/py/ipprand.py`): a subtract-with-borrow sequence
  on 32-bit words (modulus correction 18) seeded from two steps of the 69 069/1 013 904 243 LCG, plus a separate
  LCG with that multiplier/increment starting at 1 131 199 209; each value = signed sum × (hi − lo)·2⁻³² + (lo + hi)/2.
- Optional HP (tag 7), multiplied by the contour if TO NOISE, then **added to the carrier used by the band filters
  only** (pass-through uses the clean carrier).

### 6.2 Contour **[DECOMP `FUN_00851880`, render, consonant class; EXEC −147…−162 dB]**

Active when (TO MOD or TO NOISE) and (FROM CAR or FROM CONSONANTS):
- **Consonants:** `s = L + R` of the modulator → high-pass 90 Hz (biquad with `α = sin ω`) → `+0.0079433`
  (`10^(−42/20)`) → count of sign changes over the last `round(8 ms·fs)` samples → `(count·0.00625)²`, clamped to 1
  (a zero-crossing-rate detector). Zero when Freeze = 1.
- **Car:** the carrier itself (multiplied by the consonant signal when both are on).
- Then `max(|L|, |R|)`, sliding maximum over `round(12 ms·fs)` samples, and a 3-stage smoother (attack 16 ms, release
  44 ms, order 3). The result multiplies the noise (TO NOISE) and/or the modulator pass-through (TO MOD).

### 6.3 Pass-through and output **[DECOMP `FUN_008557c0`, `FUN_00853de0`, `FUN_006174b0/75a0`; EXEC]**

- HP/LP sections: biquads with `α = sin ω·q`, `q = 0.356510` and `0.943793` (table `0x8cc3d0`); LP `b₁ = (1 − cos)/(1+α)`,
  `b₀ = b₂ = b₁/2`; HP `b₁ = −(1 + cos)/(1+α)`, `b₀ = b₂ = −b₁/2`.
- **Output = carrier pass-through (LP, ×G_c) + modulator pass-through (HP, ×contour, ×G_m) + W × vocoder sum.**
- Gains are ramped per block: linear across the block, slope limited to **44.1/fs per sample** (0.001 at 44.1 kHz)
  **[EXEC 22.05/44.1/48/96/192 kHz]**; all three start at 0 when the plugin is created (so the output fades in) and
  snap to their targets after a Flush or a tail bypass.
- **Tail bypass:** when CarNoise = 0 and no internal synth, if both inputs stay within ±10⁻⁶ for more than
  `round(0.5·fs + max(hold + release) + 16)` samples, each block simply copies the plugin's **own input** to the
  output (processing frozen) **[DECOMP `FUN_00853ce0` + render; EXEC]**.
- L-R encoding: own input L → modulator (both channels), R → carrier (both channels); side inputs ignored.
- THREADED distributes bands over worker threads with separate accumulators summed afterwards (only the floating-point
  summation order changes).

---

## 7. Soundgoodizer stage **[DECOMP constructor, wrapper, render; EXEC]**

- The constructor loads `SG.dll` → `Shared\SG_x64.dll` as a **sub-plugin** (its own `CreatePlugInstance`, Vocodex
  acting as its host), forwards rate/block/process-mode messages to it, and loads `Data\SG.dat` into it through the
  sub-plugin's SaveRestoreState. Tag 1 sets the sub-plugin's **parameter 5**. When SG > 0 the render passes the final
  Vocodex output **in place** through the sub-plugin's Eff_Render.
- **[EXEC]** For every non-synth demo instance, the DLL output with SG as stored equals *our model with SG = 0 followed
  by the installation's `SG_x64.dll` (default state, parameter 5 = SG)* to **−135.8…−154.3 dB** (`r35/py/t7.py`); the
  same chain on a test signal is bit-identical. In the harness the plugin path is not resolvable, so SG.dat was **not**
  loaded; in FL it is (DECOMP). With SG.dat loaded, the standalone SG differs from the default-state SG by ~5 dB on
  the same input, so the exact FL-side SG output is **not confirmed by EXEC**.
- The Soundgoodizer engine itself (a Maximus-type processor) is **not decoded** in this round. Omitting the stage costs
  0.5…8 dB of error on the demos.

---

## 8. Internal carrier synth **[DECOMP `FUN_008506a0`, `FUN_00850aa0`, wrapper, render; DATA; DOC]**

- **It is a Sytrus engine:** the tone selector loads `Synthesizer\sytrus.dll` (`_x64.dll` appended by the wrapper →
  `Plugins/Fruity/Effects/Vocodex/Synthesizer/Sytrus_x64.dll`, its own build) as a sub-plugin, and gives it the preset
  `Synthesizer\Data\Presets\<tone>.FST` (the `.FST` branch of `FUN_0084caa0`). The preset files are standard FL
  preset streams for plugin `"Sytrus"` (`FLhd`, `0xC9 "Sytrus"`) **[DATA: PWM.fst]**; 25 presets ship.
- **How it is fed:** the notes held on Vocodex's on-screen keyboard (stored in the state: Ookay/NewStuff `PWM` with
  51, 63, 66, 73; default list = 60) are synchronised with the synth through its TriggerVoice / Voice_Release: per
  note, initial and final levels pan 0, volume **0.5**, `Init.Pitch = 100·note`, `Final.Pitch = 100·note − 6000`,
  cut/res 0. Each block the carrier buffer is zeroed and the synth's Gen_Render writes into it; CarIn < 0 selects it.
  Manual: the synth "is provided for live 'demo' purposes", keys are latched with the mouse or played from a MIDI
  controller when Vocodex is focused, and "Vocodex does not respond to Piano roll notes" **[DOC inputs page]**. No MIDI
  handler is overridden in the plugin's VMT (slots 8…19 = base stub) **[DECOMP]**.
- **EXEC not achieved:** in the harness the preset list is empty (tone index −1), so no synth is created and the
  carrier is silence. With the synth replaced by an external carrier (CarIn → 2) the rest of the Ookay/NewStuff chain
  matches the model to −141 dB (draft) / −148 dB (HQ).
- (`Data\Vocodex.wv` is a preview sample played into the modulator when a tone is picked in the UI; not part of
  rendering.)

---

## 9. Reference model and verification **[model + EXEC]**

`r35/py/vcxm.c` (C, ~500 lines; `vcxm_head.c` + `vcxm_body.c`) implements §2–§6 for any state, rate and block size
(Draft on/off, PEAK/RMS, K = 1…4, U = 0…4, all maps with straight segments, noise, contour, pass-through, L-R, tail).
`r35/py/vocodex_ref.py` is the Python front-end (`process(state, mod, car, own, fs, block, modk, cark, draft)`) and has
`apply_sg_dll()` to run the installation's SG_x64.dll afterwards (EXEC helper, not a port). `r35/py/run_tests.sh`
regenerates `r35/py/results.txt`. Error = 20·log10(rms(model − DLL)/rms(DLL)); "peak" uses max |error| / max |DLL|.

| Case (1–1.5 s, 44.1 kHz, block 512 unless stated) | rms error | peak error |
|---|---|---|
| core, 24-band Bark map, order 2 | −157.8 dB | −141.5 dB |
| orders 1 / 3 (flatness 50) / 4 | −160.6 / −158.1 / −158.6 | ≤ −140 |
| unison U = 1 / 3 (pan) / 4 | −158.6 / −146.9 / −157.9 | ≤ −139 |
| ModPitch −1200, ModBW 2456, BW 653 | −156.7 | −145.7 |
| hold/att/rel long; curves 2000/1500; MIN TIMES off; zero times | −157.8 / −157.7 / −157.3 / −159.2 | ≤ −141 |
| RMS hold mode | −147.9 | −142.4 |
| non-subtractive gate; 5 bands; 100 bands | −157.9 / −155.6 / −158.1 | ≤ −139 |
| 22.05 kHz/256, 48 kHz/480, 96 kHz/1024, 192 kHz/2048, block 100 | −157.8 / −156.6 / −160.4 / −163.6 / −151.3 | ≤ −137.9 |
| mod / car / both pass-through | −147.8 / −141.9 / −145.9 | ≤ −141 |
| noise (no HP / HP), contour CAR→NOISE, CONS→NOISE, CONS→MOD, CAR+CONS→MOD+NOISE (two signals each) | −147.4…−162.7 | ≤ −138.7 |
| Draft: core, pass + noise, U = 2 K = 3, blocks 100 and 37 | −138.5…−142.8 | ≤ −133 |
| maps: gain, pan, offset, gate, per-band pass, hold, att, rel, sat mix | −142.3…−166.8 | ≤ −136 |
| maps: band width / pitch / custom band distribution | −123.1 / −126.9 / −110.5 | −107.8 |
| saturation curve map + mix 0.6 | −99.0 | −95.3 |
| all 13 option bytes = 1 + ModPitch | −138.3 | −135.6 |
| L-R encoding; tail bypass; silent start | −147.8 / −173.2 / −147.3 | ≤ −143 |

Demo instances (speech-like test modulator, chord carrier; SG forced to 0; "HQ" = ProcessMode 2 / `draft=0`):

| Instance | as stored | HQ |
|---|---|---|
| JuiceBass – Menagerie 7 | −166.8 dB | −166.8 dB |
| Olbaid – Harmless Essentials 71 | −142.9 (draft) | −162.3 |
| SeamlessR – Bass Antics 3 | −137.1 | −137.1 |
| Treben – RawFL 15 | −142.0 (draft) | −147.9 |
| Treben – All the same 27 / 29 / 30 | −145.9 / −147.6 / −147.6 | same |
| Vocodex demo 2 | −160.7 | −160.7 |
| Ookay – Thief 27, NewStuff 27 (synth → external carrier) | −141.0 (draft) | −148.2 |
| Ookay / NewStuff as stored | both outputs silent in the harness (no synth, contour from the silent carrier) | — |
| With SG as stored: model + SG_x64.dll vs DLL (8 instances) | −135.8…−154.3 dB | — |

The −95…−127 dB cases came from the map evaluator (FL's cursor arithmetic reproduced only to ~10⁻⁷ in x) and from
the saturation kernel's precision. Both are exact since round 173d3 (§12.4: every map case ≤ −141 dB).

---

## 10. Not decoded / open

- ~~Map segments with tension ≠ 0~~ — decoded in round 173d3 (§12.1).
- The Soundgoodizer engine and whether FL's instance ends up with `SG.dat` or its default state (EXEC with SG.dat
  path resolution not achieved).
- The internal Sytrus synth's sound (EXEC not achieved); the meaning of the host flags `+0x514`/`+0x519` beyond
  metering. (The subtractive flag's menu name, "Subtractive denoising", was found in round 173d3, §12.2.)
- The exact host option that sets ProcessMode bit value 2 (inference: "HQ for all plugins").

---

## Summary table (question → result)

| # | Question | Result |
|---|---|---|
| 1 | State layout | round 20 confirmed; hold mode 0 = PEAK / 1 = RMS; unison field = extra voices; Draft field 0 = on; 13 option bytes = per-map x-axis (band vs pitch-shifted position); maps 1/2/10 centred **[DECOMP + EXEC]** |
| 2 | Parameter laws | all 20 tags (§2.2), incl. tag 13 Freeze, 16 ModBW `2^(v/1000)/4`, 18 unison detune in 1/1000 octave, 19 pan `/128` **[DECOMP + EXEC]** |
| 3 | Band count / spacing | N = 5…100 at `S(map8(b/(N−1)))`, ≥ 20 Hz (default 82 Hz…20 kHz) **[DECOMP + EXEC]** |
| 4 | Filter type / order / width | K cascaded constant-0dB RBJ band-passes (64-bit), bandwidth `min(K·w, 2)` octaves spread by flatness, `w` = neighbour distance × width laws, 2× oversampling above 11.05 kHz edges, octave levels in Draft **[DECOMP + EXEC]** |
| 5 | Followers | PEAK = sliding max of max(|L|,|R|) / RMS = √(mean(L²+R²)/2) over the hold window; gate; 1…4-stage one-pole smoother, c = 0.01^(1/t) in double precision; minimum times from band frequencies **[DECOMP + EXEC]** |
| 6 | Noise / HP / LP / mix | IPP uniform noise (bit-exact), HP; pass-through LP/HP biquads (q 0.3565/0.9438); out = car pass + mod pass + Wet·vocoder, ramped 44.1/fs per sample **[DECOMP + EXEC]** |
| 7 | Contour | ZCR consonant detector and/or carrier, 12 ms peak + 16/44 ms smoother, ×4 gains with FROM CAR **[DECOMP + EXEC]** |
| 8 | Internal carrier | Sytrus build in `Vocodex/Synthesizer`, preset `<tone>.FST`, latched keyboard notes at volume 0.5; not executed **[DECOMP + DATA + DOC]** |
| 9 | Unison | U extra analysis voices, detune table × FatPitch/1000 oct, pan spread FatPan/128 **[DECOMP + EXEC]** |
| 10 | Soundgoodizer | sub-plugin SG_x64.dll, param 5 = SG, in place on the output; model + DLL = Vocodex to ≤ −135.8 dB; engine not decoded **[DECOMP + EXEC]** |
| 11 | Model accuracy | ≤ −137 dB on every demo instance (SG off), ≤ −95 dB on all 70+ cases **[model + EXEC]** |

---

## 11. For LYNX

1. **Replace the generic vocoder** used for Vocodex (`src/core/FlEffects.cpp` `vocodexEffect`, `src/core/Effects.cpp`
   class `Vocoder`, side-input plumbing of `Effects.h`/`Effects.cpp` already exists) by a port of `r35/py/vcxm_body.c`
   (C → C++, our own code): keep the state parser (`load_state`), `setup()` (band laws), the DSP objects (band-pass,
   biquads, half-band pair, peak/RMS followers, smoother, ramps, IPP-compatible noise generator, FIFOs) and
   `process()`. Important details for −140 dB parity: 64-bit band filters and band sums; double-precision smoother and
   RMS running sum; float rounding of half-band and output-biquad states at block end; ramps start at 0; slope
   44.1/fs; round-half-even for window/stage counts; maps 1/2/10 centred.
2. **Inputs:** modulator/carrier from ModIn/CarIn exactly as now (0 = own input, k = k-th routed track); L-R encoding
   uses the own input. Treat CarIn < 0 as the internal synth (item 4).
3. **Draft:** follow the stored Draft button for real-time playback; use draft = off for an offline render when the
   user asks for FL's "HQ for all plugins" behaviour (bit 2 of ProcessMode in FL; see §2.2). Draft adds 16 samples of
   latency (report it to PDC).
4. **Internal carrier (CarIn < 0, 2 demos):** instantiate LYNX's Sytrus with the preset read from the user's own
   installation, `Plugins/Fruity/Effects/Vocodex/Synthesizer/Data/Presets/<tone>.fst` (a normal Sytrus `.fst`; never
   copy it into the repository), and hold the state's notes permanently at velocity/volume 0.5, pitch = note.
   Exactness then depends on LYNX's Sytrus (round 31).
5. **Soundgoodizer (all demos):** when SG > 0, run LYNX's Soundgoodizer (round 8 approximation) on the Vocodex output
   with amount = SG and the state of `Vocodex/Data/SG.dat` (read from the user's installation). Exact parity needs a
   future round on `SG_x64.dll`.
6. Update `docs/STATUS.md` accordingly (band filters, maps, noise, HP/LP, unison and output level are now exact;
   Soundgoodizer and the internal synth remain approximations).

## 12. Maps and the subtractive gate (round 173d3)

**Date:** 2026-11-06. Same DLL as above (SHA-256 `43d10692…14bb0`, unchanged). New Ghidra project (full
auto-analysis, read-only runs), scratch `/mnt/e/lynx-work/audit173d3/vx/` (`out/d*.c` decompilations, `exec/` and
`golden/` EXEC runs). The round-35 EXEC harness `vcx.c` was rebuilt unchanged (Zig 0.14.1).

### 12.1 The map object **[DECOMP `FUN_00784790`, VMT `0x77ba50`, `FUN_0077bd00`, `FUN_0077ef50`, `FUN_0077fa70`, `FUN_00781c40`, `FUN_00782b50`, `FUN_00783730`, `FUN_007812a0`]**

Each of the 13 maps is an instance of FL's envelope library class in its "map" flavour (constructor `FUN_00784790`:
kind 3, length 1, option byte `0x20`). It is the same library code that Love Philter's maps use (round 66 §3.2,
round 31 §4.4); the differences are noted below.

- **Stored record** (load/save `FUN_0077bd00`, record version 3): `i32` kind (3), `i32` version (3), `i32` n, then n
  points of 24 bytes — `f64` Δx, `f64` level, `f32` tension, `u8` curve mode, two flag bytes, `i8` **sign** — then
  five `i32` (flags, on, decay, loop, sustain; Vocodex writes 0, −1, −1, −1, −1). The loader reads the rest of a record
  only when its stored kind is 3. In memory the level is kept as a **float** (a NaN level takes the previous point's, starting
  from 0); Δx stays double. The sign byte is not an input: the loader recomputes it (see below) and the saver writes
  the recomputed value (that is why stored files show `…02` on every point but the first). The map flavour drops
  decay / loop / sustain (sets them to −1) and clears the second flag byte.
- **Normalisation** (`FUN_0077ef50`, at every load): if the first Δx is positive a point (x = 0, same level) is
  inserted in front; then with `T = ΣΔx`, if `T > 0` every Δx is multiplied by `1/T` (skipped when that factor is
  exactly 1), otherwise a point at x = 1 with the last level is appended. Negative Δx are set to 0 afterwards.
- **Preparation** (`FUN_0077fa70`, per point i ≥ 1): `sign = 0` for a zero tension, else ±1 by the tension's sign
  bit; the curve constants of modes 0–12 exactly as round 66 §3.2 / round 31 §4.4 (step count
  `Round(0.001·(float(1000001^float(1 − 0.6|t|)) − 1))` for modes 3–6, +1 for stairs with t ≥ 0; mode 0/1
  `k = float(1000001^|t| − 1)·sgn t`, `1/|k|`, `ln(|k| + 1)`; mode 10 "smooth" from its neighbours; modes 11/12 from
  `1 − (1 − max(0.9|t|, 2⁻¹⁶))³`); `sign = 2` (straight) for modes 0, 1, 7, 8, 9, 11, 12 with t = 0 (mask `0x1B83`), for
  mode 10 when the level step is below 2⁻²⁴, and for mode 6 with no steps and t > 0.
- **Read** (`FUN_00783730`, used by the band set-up for every map): a cursor is started (`FUN_00781c40`: index 0;
  rate 1 and phase 1 when the first Δx is 0, else rate `1/Δx₀` and phase 0; start = level₀, delta 0) and advanced
  once by x (`FUN_00782b50`): `phase += x·rate`; while the phase's high word says ≥ 1: `phase = (phase − 1)/rate`
  (x when the rate is 0), start = level(idx), idx + 1 (not past the last point), delta = level(idx) − start, and the
  new rate `1/Δx` (rate 1 and phase + 1 for Δx ≤ 0; rate and phase 0 when the index did not move). The value is
  `start + shape(idx, phase)·delta`, **all in double** (Love Philter rounds start, delta and the shape to float; the
  map read here does not). `level(i) = double(float level) − c`, with c = 0.5 for the centred maps 1, 2, 10.
- **Shapes** (`FUN_00782b50`): identical formulas to round 66 for every mode (0 single curve, 1 double curve, 2 hold
  = 0 until the point, 3 stairs, 4 smooth stairs, 5 pulse, 6 wave, 7/8 single/double curve 2, 9 half sine, 10 smooth,
  11/12 single/double curve 3), with `sign = 2` giving the straight line, evaluated in double (the stairs product is
  float). Exp/Ln/Sin/Cos are the Delphi runtime's (`FUN_0040ad30`, `FUN_0040bf50`, `FUN_0040ba10`, `FUN_0040b920`).

**So the round-35 open item is closed:** curved map segments are FL's envelope curves, the same law as Love
Philter's, read once per band at set-up.

### 12.2 The subtractive flag **[DECOMP `FUN_0084efb0`, `FUN_0084dc20`, `FUN_0084de50`; DATA: the form resource]**

- **Menu:** Vocodex's options menu (the button `|Options` at the top of the form, popup `SynthResModule.OptionsMenu`)
  holds, in order: **"&Detect modulator noise level"** (`DetectNoiseMenu`), **"Subtractive denoising"**
  (`SubNoiseThresMenu`, AutoCheck), a separator, **"&About"**. The popup handler `FUN_0084de50` sets the check mark
  from `+0x190`; the click `FUN_0084dc20` copies the check mark into `+0x190` and re-runs the band set-up
  (`FUN_00851dd0`) under the plugin lock. So the flag's caption is **"Subtractive denoising"**.
- **Loader correction to §2.1:** the loader sets `+0x190 = 1` (and clears `+0x191…0x193`) *before* reading the
  `0x24`-byte block, so for state versions ≥ 5 the stored byte wins; only versions < 5 (shorter block) keep the forced
  1. All demo states store 1.
- **Meaning** (§4.2, unchanged): with the flag on the follower's envelope becomes `max(e − T, 0)`; off, values below T
  are zeroed. `T = (1025^m − 1)/1024 / max(g_v, 2⁻¹⁶)`, m = map 3 read at the pitch-shifted band position. A new
  instance's map 3 is flat at 0 (T = 0), so the switch does nothing until map 3 is drawn or detected.
- **"Detect modulator noise level"** (`FUN_0084dba0` → `FUN_00850e20(…, 0)`; the end `FUN_0085fce0` →
  `FUN_00850e20(…, 1)`): map 3 is reset flat at 0 and, for `Round(2·fs)` samples of playback (counted down in the
  render, which then posts a window message), each band's modulator voices record the maximum over blocks of
  (follower envelope before the gate × g_v). At the end every voice adds a point at its shifted position
  `x = S⁻¹(f·p)` (when 0 ≤ x ≤ 1) with level `clamp(ln(float(peak·1792) + 1)/ln 1025, 0, 1)` — i.e. a threshold of
  1.75 × the measured noise peak — then the first and last points copy their neighbours' levels, the points are
  sorted, the band set-up re-runs and the editor switches to map 3. Not ported in this round (it needs a 2-second
  measuring mode in LYNX's engine and an editor command).

### 12.3 How FL presents the maps **[DATA: form `TSynthEditorForm`; DECOMP `FUN_0084f730`, `FUN_00858de0`, `FUN_008604e0`]**

The map editor (`TMEnvEditor`) shows one map at a time, chosen in the combo `ArtSelect` (hint "|Editor target"). Its
items, with the section headers FL draws as separators, map to the map indices in order:

| # | Section | Caption | Centred | New instance | Editor colour (RGB) | "Follows pitch" button |
|---|---|---|---|---|---|---|
| 0 | Equalizer | Band gain multiplier | no | flat 0.5 | `#D2E168` | yes |
| 1 | Equalizer | Band panning | yes | flat 0.5 | `#D2E168` | yes |
| 2 | Equalizer | Band gain offset | yes | flat 0.5 | `#D2E168` | yes |
| 3 | Equalizer | **Modulator noise level** (the gate map) | no | flat 0 | `#D2E168` | — (always shifted) |
| 4 | Equalizer | Modulator pass-through | no | flat 0 | `#D2E168` | yes (on by default) |
| 5 | Envelope follower | Hold | no | flat 0.5 | `#5B00F4` | yes |
| 6 | Envelope follower | Envelope attack | no | flat 0.5 | `#5B00F4` | yes |
| 7 | Envelope follower | Envelope release | no | flat 0.5 | `#5B00F4` | yes |
| 8 | Spectral distribution | Band distribution | no | line S⁻¹(E2 = 440·2^(−29/12) Hz) → S⁻¹(20 kHz) = 0.142937… → 0.991756… | `#99F400` | — |
| 9 | Spectral distribution | Band width | no | flat 0.5 | `#99F400` | yes |
| 10 | Spectral distribution | Modulator pitch shift | yes | flat 0.5 | `#F4CA00` | — |
| 11 | Envelope saturation | Saturation mix | no | flat 0 | `#00F4AB` | yes |
| 12 | Envelope saturation | Saturation curve | no | line 0 → 1 | `#00F4AB` | — |

- Defaults: constructor masks `0x818` (maps 3, 4, 11 at 0) and `0x406` (maps 1, 2, 10 centred); two points per map
  (x = 0 and 1, tension 0, mode 0). Option bytes: a new instance has maps 3 and 4 on "Follows modulator pitch shift" (`FUN_0084ef70`, mask `0x18`;
  also applied to states older than version 4).
- **"Follows modulator pitch shift"** (the button `PitchShiftMapBtn`, `FUN_008604e0`) writes the shown map's option
  byte and re-runs the band set-up; it is hidden for maps 3, 8, 10, 12 (mask `0x1508`).
- **Editor grid** (`FUN_00858de0` → `FUN_0081e3b0`; reading the arguments as grid steps is an **inference**): x
  steps of 1/112 (map 8: one band, 1/(N − 1)); y steps of 1/32 with 8 sub-steps (map 8: 1/112; map 10: 1/24 = a
  semitone of its ±1 octave).
- **Point menu** (the envelope editor's, form strings): "&Delete", then "Mode": Hold (2), Smooth (10), Single curve
  (0), Single curve 2 (7), Single curve 3 (11), Double curve (1), Double curve 2 (8), Double curve 3 (12), Half sine
  (9), Stairs (3), Smooth stairs (4), Pulse (5), Wave (6) (value = stored mode). Tension is the point's curve handle.
- Every map edit (draw, point move, the pitch button, the subtractive item, Detect) re-runs `FUN_00851dd0` under the
  plugin lock; that set-up recomputes coefficients, gains, thresholds, follower windows and smoother constants but
  does **not** clear the filter states (the biquad design `FUN_00626690` only writes coefficients), so an edit does
  not restart the sound.

### 12.4 EXEC

The map cases start from a demo state (Olbaid 71, 47 bands, Draft off, SG 0) or, for the unit-test states, from FL's
default instance (24 bands); the modulator is on side input 1 and the carrier on side input 2, and the maps are
written by LYNX's `vocodex::withMap`. Runs last 0.5–1.5 s at 44.1 kHz with block 512. The modulator is noise bursts
plus a buzz (the demo rows use the round-35 test signals) and the carrier is four saws. LYNX = `vocodex::Engine` (the shipping code); error =
`20·log10(rms(LYNX − DLL)/rms(DLL))`.

| Case (47 bands unless noted) | Error (dB) | Peak error (dB) |
|---|---|---|
| base (demo state Olbaid 71, Draft off) | −162.0 | −143.6 |
| curves on gain / pan / offset maps | −170.0 | −146.0 |
| gate map line 0 → 0.5, subtractive on / off | −162.6 / −162.0 | −143.5 / −143.6 |
| curved gate map, subtractive on / off | −162.0 / −162.0 | −143.2 / −143.6 |
| curved band distribution | −157.1 | −144.3 |
| curved pitch and band width maps | −160.1 | −144.7 |
| saturation mix only; + straight curve; + smooth curve | −142.4 / −141.1 / −141.7 | −137.5 / −136.2 / −138.8 |
| every curve mode 0…12 on one map, two tension sets | −162.7 / −158.8 | −147.3 / −144.8 |
| curved hold / attack / release maps | −162.2 | −143.6 |
| the six unit-test states (FL's default instance, 24 bands, 0.5 s) | −142.5…−163.2 | — |
| the six demo instances with stored maps (SG 0) | −141.7…−163.8 | — |

In FL itself the subtractive flag changes the output by −36.8 dB (gate line 0 → 0.5) and −22.1 dB (curved gate map,
unit-test state) relative to the signal, so the switch is audible once map 3 is drawn.

### 12.5 For LYNX (done in this round)

- `core/Vocodex`: the maps keep tension and curve mode per point; `MapReader` is the map read of §12.1 (normalisation,
  float levels, every curve mode, double-precision cursor) and the engine reads every map through it. New API:
  `readMaps`, `withMap` (FL's record layout, sign byte as FL's loader computes it, header / trailer / install data
  kept), `withMapFollowsPitch`, `mapValue`, `mapInfo` (captions, sections, centring, defaults), `defaultMap`,
  `MapCurve` (FL's mode numbers).
- The effect takes an edited state without a rebuild (`acceptState`): the new set-up is prepared off the audio thread
  and takes over the running filters, followers, smoothers, FIFOs and gain ramps (`Engine::takeOver`), as FL's
  in-place band set-up does. A new layout (band count, order, unison, hold mode, Draft), carrier or SG amount still
  builds a new instance.
- Open: the editor UI (captions above), "Detect modulator noise level" (§12.2).

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| `FUN_00782b50`, `FUN_00781c40`, `FUN_00783730`, `FUN_0077fa70`, `FUN_0077ef50`, `FUN_0077bd00` | map read: cursor, curve shapes, preparation, normalisation, record load/save (§12.1) | DECOMP + EXEC |
| `FUN_0084dc20`, `FUN_0084de50`, form `SubNoiseThresMenu` | "Subtractive denoising" menu item ↔ `+0x190` (§12.2) | DECOMP + DATA |
| `FUN_00850e20`, `FUN_0084dba0`, `FUN_0085fce0` | "Detect modulator noise level" (§12.2) | DECOMP |
| `FUN_0084f730`, `FUN_008604e0`, `FUN_00858de0`, form `ArtSelect` | map captions, defaults, follows-pitch button, editor grid (§12.3) | DECOMP + DATA |
| `FUN_0084efb0` | state loader: block sizes per version, option bytes, subtractive flag preset to 1 (stored byte wins from version 5, §12.2), tone and notes | DECOMP |
| `FUN_00853e80` | parameter laws, filters for tags 3/5/7, ×4 contour factors, SG forwarding | DECOMP |
| `FUN_00851880`, `FUN_0084e660` (ProcessMode branch) | draft decision, levels 5/1, follower class per hold mode, contour objects | DECOMP + EXEC (`disp=1,2`) |
| `FUN_00851dd0` (+ `b1dd0.asm` for hidden `2^x` arguments) | centres, pitch, maps, widths, gains, sub-filters, oversampling, levels, windows, smoother times, tail | DECOMP + EXEC dumps (`cmpsetup.py`) |
| `FUN_0084f730` masks `0x818`/`0x406` | map defaults and centring | DECOMP + EXEC |
| `FUN_00626690`, `FUN_006260c0`, `FUN_00853de0`, table `0x8cc3d0` | band-pass and LP/HP designs | DECOMP + EXEC |
| `FUN_00626e20`, `FUN_00626a70`, `FUN_00626890`, `FUN_006298a0` | biquad kernels, state rounding | DECOMP |
| `FUN_0077ab60`, `FUN_0077ad30`, `FUN_0062d3a0`, global `0x8df680` | half-band pair, pair averaging, coefficients | DECOMP + EXEC |
| `FUN_00855cd0`, `FUN_00855a60`, `FUN_00855410/460` | per-band chain | DECOMP |
| `FUN_00787e50`, `FUN_0062b2f0`, `FUN_00788200`, `FUN_00788330` | PEAK sliding max, RMS mean (x87) | DECOMP + EXEC |
| `FUN_00606730`, `FUN_006064e0`, `FUN_00606600`, global `0x8dd2c8` = ln 0.01 | smoother stages (x87) | DECOMP + EXEC |
| `FUN_007884c0`, `FUN_00788680`, `FUN_0062b130` | consonant (zero-crossing) detector | DECOMP + EXEC |
| `FUN_008557c0`, `FUN_006174b0`, `FUN_006175a0`, `FUN_0062c760`, `FUN_0062a1b0`, `FUN_0062aee0`, host struct `+0x34` | pass-through, wet, ramps, slope 44.1/fs | DECOMP + EXEC |
| `FUN_00853ce0`, `FUN_0062b440` (threshold 1e−6) | tail bypass | DECOMP + EXEC |
| `FUN_008373e0`, `FUN_00837510` | draft FIFOs, 16-frame latency | DECOMP + EXEC |
| `dsp_ippv2_x64.dll` `0x1815ed2e0` + `rnd.exe` + `ipprand.py` | uniform generator, bit-exact | DECOMP + EXEC |
| `FUN_0084c570`, `FUN_0084caa0`, render tail; `t7.py` | SG sub-plugin, param 5; model + SG_x64.dll = Vocodex | DECOMP + EXEC |
| `FUN_008506a0`, `FUN_00850aa0`, VMT `0x84e438`; `Synthesizer/Data/Presets/*.fst` | internal Sytrus, preset path, voice parameters, no MIDI override | DECOMP + DATA |
| Manual pages (parameters, inputs, main) | Draft/HQ, MIDI/keyboard, "mono sum" statement, demo purpose | DOC |
| `survey.py`, `route.py`, `data/states/*.bin` | the 10 demo instances and their routing | DATA |
| `vcxm.c`, `vocodex_ref.py`, `t1…t7.py`, `results.txt` | model and the error table | model + EXEC |

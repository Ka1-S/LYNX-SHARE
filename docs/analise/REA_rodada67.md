# Behavioural analysis of FL Studio 2026: round 67 (REA) — Fruity Granulizer and Drumaxx: the complete engines

> **Nature of this document.** Same goal as rounds 47 (3x Osc) and 64 (SimSynth): describe a generator that LYNX did not
> play at all, precisely enough to port it as an exact instrument. *Clean-room* reverse engineering, behavioural only.
> No Image-Line code or pseudocode was copied: what is here are data layouts, formulas and behaviours described in our
> own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No encrypted content was
> touched and no Image-Line content (code, audio, tables) was copied into the repository.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Where there was not enough
> evidence, the item is marked **"not confirmed"**; inferences made *from* the code are stated as **inference**.
> Results of our own model/port (which implement only DECOMP/EXEC facts) are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our reference model or of LYNX's engine, which only implement DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Target** (copy in `r67/bin/`): `Plugins/Fruity/Generators/Fruity Granulizer/Fruity Granulizer_x64.dll`, SHA-256
  `8a7d8698bc6d2df1418dc0aaab30b670bfc414dd94e6476217b5fc107b3dfb86`, Delphi, image base 0x400000. Its `Plugin.nfo`
  has no `ps_dummy64bit` line and the DLL holds the whole engine: unlike Wasp (round 64 §1) it is **not** a 64-bit
  placeholder. The class is the framework's `TTestPlug` (as every Image-Line Delphi generator); `CreatePlugInstance`
  (`0x7514f0`) builds it with info record `0x7b13c8` (flags `0x31780431`, **103 parameters** = 12 own + 91 channel
  settings of the voice processor, see §3.4) **[DECOMP]**. The IPP routines it calls come from the installation's
  `Shared/dsp_ippv2_x64.dll`, resolved by name at load (`FUN_005ab450`; table of 420 names) **[DECOMP]**.
- **Tools:** Ghidra 12.1.4 headless (project `~/ghproj/r67_gr`), our scripts `r67/gh/scripts/DecompAll.java`,
  `DecAt.java`, `StrRefs.java` (strings with their referencing functions) and `RefsTo.java`; decompilations in
  `r67/out/gr_plug.c` (plugin unit `0x74c000…0x76c000`), `gr_lib.c` (the sample/processor library `0x5f7000…0x60f000`),
  `gr_fw.c`, `gr_fw2.c` (framework); `objdump -M intel` (`r67/out/gr_all.s`, `sinc.s`) wherever float order, the x87
  or SIMD lanes mattered; `r67/py/classes.py`, `pe.py` (Delphi VMT/PE readers from round 64).
- **EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `r67/run/gx.c` → `gx.exe`, a copy of round 64's `wx.c`
  (same safety measures: `SetErrorMode`, vectored fault handler ending the process, the installation's DLL loaded
  unmodified, minimal fake host, state restored through the plugin's own SaveRestoreState from a read-only `IStream`,
  `TerminateProcess`, `timeout`). Additions: host methods +0x68 (the sample prompt: returns a name), +0xb8
  (`GetSampleInfo`: hands our own synthetic sample, 16-bit or float, 1 or 2 channels, with `SmpRateConv`), +0xf0
  (`GetSampleRegion`), host data +0x68 (the scratch buffer the plugin renders grains into); `smp=` loads the sample through
  Dispatcher 10 after the rate; `seed=` writes the Delphi `RandSeed` (`0x76c010`) before the first note; `sinprobe=`
  runs the plugin's own `ippsSin_64f_A50` pointer on test data. `r67/run/ipp.c` → `ipp.exe` calls
  `ippsVectorSlope_64f` / `ippsSin_64f_A50` of `dsp_ippv2_x64.dll` directly. No process or dialog was left behind.
- **Drivers** (`python3 -I`, under `r67/py/`): `collect67.py` (states), `grrun.py` (EXEC), `granmodel.py` (the reference
  model), `cmp0.py`, `bat67.py` (model vs DLL, random states), `cppcmp.py` (LYNX's C++ vs DLL), `cm.py` (C++ vs model),
  `inst67.py` (installation states), `tkey.py` (key modes), `gold67.py` (test goldens), `dbg*.py` (single cases).
  Logs in `r67/out/` (`bat67_*.log`, `cpp67_*.log`).
- **Data:** `r67/out/states67.pkl` — every Fruity Granulizer 0xD5 state of the installation (plugin names compared
  case-insensitively: the presets spell it "Fruity granulizer"): **15 states, 10 distinct** — 8 presets in
  `Patches/Plugin presets/Generators/Fruity Granulizer` (versions 1, 2, 4, 5), the plugin-database entries and
  `System/GranulizerTrack.fst` (the defaults), the render test `System/Render tests/Fruity Granulizer.flp` (defaults,
  sample `Packs/Drums/Kicks/808 Kick.wav`) and the *Vocoder* template (version 7 with a snapshot, sample
  `Packs/Legacy/Vocals/FLS_SpokenWord 001.wav`). No demo project uses the plugin. The presets carry no sample.

---

## 1. State and parameters

### 1.1 SaveRestoreState (`FUN_007519f0`) **[DECOMP + DATA + EXEC]**

Loading reads, in order: int **version**; 48 bytes = 12 ints, the parameters 0…11 at `P+0x120` (a shorter stream leaves
the rest as they were: parameter 11 is preset to 0xf8 before the read, so version-1 states — 44 bytes, 10 parameters —
get 248 there); version ≥ 4: int **slice mode** (`P+0x164`); version ≥ 5: int **key mode** (`P+0x168`); version ≥ 7: an
int whose low byte is the **sync mode** (`P+0x1bb`), then the **364-byte snapshot** of the voice processor's channel
settings (round 42 §3.1; read only if all 364 bytes are there). Saving always writes version 7: the version, the 12
parameters, slice mode, key mode, *whether sync is active* (not the mode) and the snapshot. After loading, every
parameter is re-applied through ProcessParam and the slice mode change re-slices the sample (§2.7).

### 1.2 Parameters (`FUN_00752580`, laws `FUN_00752170`, `FUN_007521f0`, derived values `FUN_00752290`) **[DECOMP + objdump + EXEC]**

The knobs' ranges and defaults were read from the editor's wheels (`TVectorWheel` min/max/default at `+0x3a8…+0x3b0`)
in the running DLL, the names from the form's hints **[EXEC + DECOMP]**:

| # | Name (hint) | Range | Default | Use |
|---|---|---|---|---|
| 0 | Grain attack time | 50…500 | 204 | `A` = samples(ms(v)) |
| 1 | Grain hold time | 0…500 | 50 | `H` = samples(ms(v)) |
| 2 | Grain spacing | 125…1000 | 611 | grain period |
| 3 | Wave spacing | −1000…1000 | 611 | position step (negative: backwards) |
| 4 | Effect depth | 0…384 | 0 | position wobble depth |
| 5 | Effect speed | 4…200 | 180 | wobble phase step |
| 6 | Randomness | 0…256 | 0 | random grain positions |
| 7 | Stereo separation | 0…64 | 0 | alternate grains panned apart |
| 8 | (loop) "Repeat sound" | off/on | off | the voice loops the sample |
| 9 | (hold) | off/on | off | freeze: the position stops |
| 10 | Sample start | 0…4096 | 0 | start position |
| 11 | (transient hold) | 0…500 | 228 | used only with slices |

Laws, with the float/double conversions the code makes:
- `ms(v) = round(float(exp(float(v·0.002)·ln 51) − 1)·10)` (milliseconds; `ln` and `exp` in double);
- `pct(v) = sign(v)·round(double(float(exp(float(|v|·0.001)·ln 11) − 1))·76.8)`, where 256 = 100 % (611 → 256);
- `samples(ms) = float(ms·fs·double(0.001f))` with `fs` the integer host rate (Dispatcher 4, `FUN_0060adc0`);
- `AH = A + H`; grain spacing `G = max(5, (AH·pct(p2))·(1/256))` (float); wave spacing
  `W = float(double(AH·pct(p3))·(srate·(1/fs))·(1/256))` where `srate` is the sample's rate (round(`SmpRateConv`·host
  rate)) — at 100 % the grains advance through the sample in real time; depth `D = p4/256` (float), wobble step
  `float(π/(204 − p5))`; start = `p10·end/4096` (integer, truncated; `end` = the last frame), loop length
  `end − loopstart + 1`.

Examples at 44.1 kHz with the defaults: A = 1764.00012, H = 220.500015 (rounded per grain to 1764 and 221),
G = W = 1984.5 **[EXEC peek of `P+0x16c…0x198`]**.

### 1.3 Sync, slices, keys **[DECOMP]**

- **Sync** (`FUN_007524e0`): mode 1 is active when `P+0x19c` > 0, a beat count the host sends with Dispatcher 0x13
  (which also switches mode 2 → 1/0); then the wave spacing becomes `G·(end + 1)/round(beats·60·rate/tempo)` and the
  voice loops. What FL sends there (the sample's beats?) is **not confirmed**; LYNX keeps sync off.
- **Slice mode** (`FUN_007548c0`): 0 = no slices; 1 = the sample's regions, asked from the host (`GetSampleRegion`);
  2 = "auto slicing" by the framework's slicer (`FUN_007400a0`, `FUN_00740e80`, `FUN_00740210`, "Slicing..."). With
  slices, the grain logic aligns grains to the slice starts and uses the transient hold (parameter 11). **Not modelled**
  (2 presets: *Drumloops*, *Rearranging drumloops*; no project).
- **Key mode** (TriggerVoice): off (0) → every key plays from the start at the key's pitch. Otherwise the pitch is
  cancelled (`−100·k` cents with `k = round(Init.Pitch·0.01) − 60`) and the start moves by `end·k/24` (mode 1: two
  octaves across the sample), `(k/4)·60·srate/tempo` (mode 2: sixteenth notes at the project tempo, ProcessEvent 0; with
  sync beats the beat length instead) or the k-th slice start (mode 3); negative k counts as 0; the result is
  `round(start + offset) mod (end + 1)` **[DECOMP + EXEC for modes 1 and 2]**.

---

## 2. The voice

### 2.1 TriggerVoice (`FUN_00753580`) **[DECOMP + EXEC]**

Without a sample (`length < 1`) the plugin asks the host to kill the note and returns no voice. Otherwise: the
polyphony check (§2.6), then a voice record: grain list, grain count 0, next grain time 0, elapsed 0, slice −1,
position = start (float), wobble phase 0, flags "done" and "looped" clear, pitch offset from the key mode; then the
voice processor's voice (round 42/47).

### 2.2 Grain scheduling (Voice_Render `FUN_00753ea0`, first part) **[DECOMP + objdump + EXEC]**

At each render of n samples (the output is cleared first): if the voice is not done, `elapsed += n` and, **while
next < elapsed**, a grain is started:
1. its start is `round(position)` — unless randomness is on (`p6 > 0`), freeze is off and `p6 > 255 − Random(256)`:
   then `start += Random(looplen)` and, while beyond the end, `start −= looplen` (Delphi's `Random`: `RandSeed ×
   0x8088405 + 1`, `(u32·range) >> 32`; the DLL seeds it from the clock at load, `0x407bc9`, so FL's draws differ run
   to run);
2. the grain is created (§2.3) at offset `next − (elapsed − n)` in the block, with `round(A)`, `round(H)`;
3. the **very first grain** (next = 0 and position = 0) starts at full level: its counters skip the attack (counter 0,
   hold and end lengthened by A, step 0, gain 1);
4. `next += round(G)`;
5. unless frozen: `position −= round(|sin(phase)|·W·D)` if depth > 0 (Delphi's `Sin`), `phase += step`,
   `position += W`; past the end: without loop the voice is *done* (and finishes when its last grain ends), with loop it
   is *looped*; either way `position −= looplen` until within the sample; then, if not looped, a negative position wraps
   by `end + 1`; if looped, positions at or below the loop start move up by the loop length.

### 2.3 Grain start (`FUN_00753c70`) **[DECOMP + EXEC]**

Only when `start ≤ end`. The grain count increases; a sample reader (§3) is attached to the sample with the voice's loop
(parameter 8, the sample's loop points: the whole sample unless FL passes loop points), its position = start clamped to
the sample, its speed from `cents = Final.Pitch + key-mode offset` and `ratio = srate·(1/fs)`. Envelope: counter
starts at −A, hold length H, end at H + A, step `float(1/A)`, gain = step. Pan: `x87 law(p7/64, 1.42)` — L/R gains
`cos/sin((1 + pan)·π/4)·1.42` on the x87 (round 42 §3.5), **swapped for odd grain counts**, so successive grains
alternate sides.

### 2.4 Grain rendering (Voice_Render, second part) **[DECOMP + objdump + EXEC]**

For each grain in list order: if it has an offset, it renders from that sample of the block on (offset cleared). Its
reader fills the host's scratch buffer and returns how many frames it produced (fewer at the end of a non-looping
sample). The frames are cut into envelope segments: attack (counter < 0), hold (counter < H), release (counter < end);
each segment is added into the voice buffer with constant gains `g·L, g·R` (SSE `x·g + out`, `0x60b3f0`) or, when the
step is not 0, the SSE ramp (`0x60b5c0`: lanes [L₀, R₀, L₁, R₁] start at g·L and g·L + s·L, +2s per pair of frames, the
frames past the last multiple of 8 continue lane 0 with +s — the mixer of round 42 §3.5), after which
`g += float(len)·step`. At counter = H the step becomes `float(−1/(end − H))`, at counter = 0 it becomes 0. A grain ends
when its counter reaches the end or its reader ran out (non-looping, position past the end); the voice reports
"finished" when it is done and its last grain ended — the processor then asks the host to kill it.

### 2.5 The voice processor **[DECOMP + EXEC]**

Gen_Render (`FUN_00754f00`), Voice_Release (`FUN_00753870`), NewTick (`FUN_00754ff0`), the channel settings
(ProcessParam 12…102) and the gain/filter/end-fade path are the library's plugin voice processor exactly as 3x Osc uses
it (round 42 §3, round 47 §2.3/§3): same settings snapshot, rate ratio and ramp limit from Dispatcher 4
(`FUN_0060adc0`), release path (the end fade starts at once when the volume envelope is off), mixing of the voices.
The processor reads Final.Vol/Final.Pan; its pitch articulator offset is added to Final.Pitch, which grains read when
they start.

### 2.6 Polyphony (`FUN_007534e0`) **[DECOMP]**

With a max polyphony (`P+0xe0` > 0) reached, the first voice whose processor is not released (else the last one)
starts its end fade — as 3x Osc (round 47 §1.7).

### 2.7 The sample (Dispatcher 10, `FUN_00754b30`, framework `FUN_007397a0`) **[DECOMP + EXEC]**

FL hands the channel's sample as a handle (Dispatcher 10, Index). The plugin asks the host for it through host method
+0x68 (16-bit stereo format at the host rate as its preferred format) and reads it with `GetSampleInfo` (+0xb8,
`TSampleInfo` of the SDK: data, length, loop start/end, `SmpRateConv`, number of regions, beats, tempo, channels and
format, the last two set to −1 = "any"): the library's sample object keeps the data as delivered (16-bit or float, 1 or
2 channels), its rate = `round(SmpRateConv·host rate)`; without loop points the loop is the whole sample. Which format
FL delivers is **not confirmed**; for 16-bit content both formats give identical results (the library's paths differ
by exact powers of two) **[DECOMP + EXEC: 16-bit and float renders of the same sample are bit-identical]**.

---

## 3. The library's sample reader (class at `FUN_005fc5c0`) **[DECOMP + objdump + EXEC]**

### 3.1 Speed, quality, bounds (`FUN_005fd080`, `FUN_005fcfa0`, `FUN_005fce50`, `FUN_005fc8b0`, `FUN_005fc7c0`, `FUN_005fc750`)

- Speed = 1 when cents = 0 and ratio = 1, else `exp(cents·ln2/1200)·ratio` (double). Speed 1 with an integer position
  selects the **copy path** (no interpolation).
- Quality = interpolation points per side, a global set by the host's ProcessMode (Dispatcher 1, bits 8…23,
  `FUN_0060b020`): < 3 → 1 (linear), 3 → 3 (6-point Hermite), more → windowed sinc with that many points, ≤ 512. Its
  default is the table `0x79c294` = {1, 3, 8, 12, 16, 32, 64, 128} at index 4−1 → **12 points per side ("24-point
  sinc")** **[DECOMP + EXEC: 12 in the running DLL]**. What FL's host sends there is **not confirmed**; LYNX uses 12.
- Above unity speed the sinc's cutoff becomes `π/speed` (`2cos` of it kept for the recurrence) and the points
  `max(12, (round(speed) + 3) & ~3)` (≤ 512).
- Bounds: interpolation windows that would cross the sample start, end or loop are built in a small buffer with the
  edge frames **repeated** (or the loop wrapped: `FUN_005fd270/480/680`).

### 3.2 Render (`FUN_005fd890`)

Between `FUN_005a2b90` and `FUN_005a2b80` the **x87 control word is 0x73F: 64-bit precision, rounding toward −∞**
(SSE code keeps rounding to nearest) **[DECOMP]**. For each call: if `round(pos)` is near the start or the block would
reach the end, a careful per-frame path (`i = trunc(pos)`, fraction `pos − i`, `pos += speed`, end handling: stop when
not looping, wrap by the loop length otherwise; a frame that needs the edge buffer is interpolated at once and the path
is chosen again); otherwise the fast path: copy (16-bit: `ippsConvert_16s32f_Sfs`, ×2⁻¹⁵) or positions from
**`ippsVectorSlope_64f`**, then `pos += speed·n`. The IPP routine's arithmetic matters for fractions near 0/1: eight
lanes start at `pos + speed·l` and step by `speed·8`; the last `n mod 8` positions continue from lane 0's next value
adding `speed` one at a time **[EXEC `ipp.exe`: 60/60 random calls bit-identical to this rule; the plugin's own
pointer is the same routine]**.

### 3.3 The windowed sinc (`0x5f8f40` 16-bit stereo, `0x5f8a80` mono, `0x5f9880`/`0x5f93e0` float) **[DECOMP + objdump + EXEC]**

Per frame with fraction f (`ippsThreshold_LT_64f_I`: f < 10⁻²⁵ → 10⁻²⁵), a = π/points, b = the cutoff:
1. x87 (round down): `d₀ = f·(−a) + π/2`, `d₁ = d₀ − a`, `d₂ = f·b`, `d₃ = d₂ + b` (each stored as a double from the
   extended value);
2. `ippsSin_64f_A50` of the four (IPP's sine; it differs from the C library's by about one ulp in half the cases);
3. x87 (round down): window values `w₀ = sin d₀`, `w₁ = sin d₁`, `w₋₁ = w₀·2cos a − w₁`, `w₋₂ = w₋₁·2cos a − w₀`; the same
   recurrence on the sinc numerators with `2cos b`; denominators `fπ − π, fπ, fπ − 2π, fπ + π` (x87 `fldpi`); all
   stored as floats;
4. SSE, `points/2` iterations, four taps each (frames i + 1, i, i + 2, i − 1, then the next pairs outwards): the
   reciprocal of the denominators by **`rcpps` and one Newton step**; the numerator and window recurrences in float
   (`new = cur·2cos − prev`); window weight `(0.64·w)·w + w + 0.36`; coefficient `(num·rcp)·weight`; the taps
   accumulate per channel in two lanes; the denominators step by ∓2π;
5. output `(lane₂ + lane₀)·s` per channel, with `s = 2⁻¹⁶` for 16-bit data and 0.5 for float.
The mono kernels compute the same with both channels equal. Note the cancellation in step 3: at fractions within
~10⁻¹² of 1 the numerator `sin(b(f − 1))` comes from differences of O(1) values, so it carries a relative error of
~10⁻³ that depends on the last bits of IPP's sines — FL's own output is noisy there.

---

## 4. Verification

| Test | Result |
|---|---|
| Model (`granmodel.py`) vs DLL, 32 random states × random scenarios (1–2 notes, releases, pitch, 16-bit/float, mono/stereo, 22.05…96 kHz, sample rates ≠ host rate, sample loops, blocks 64…1000, fixed `RandSeed`) (`bat67.py`, `out/bat67_*.log`) | all within −129.6 dB before the release **[EXEC + model]** |
| The same with IPP's own sines in the model (`ippsin.py`) | the near-integer outliers disappear: −136.9 dB on the worst case (`dbg6.py`) |
| LYNX's C++ (`build-local/granulizerrender`) vs DLL, 100 random cases (`cppcmp.py`, `out/cpp67_*.log`) | **100/100 within −128.3 dB** before the release; with the end fade −86…−136 dB (the fade table, §5) |
| LYNX's C++ vs the model (C library sines in both), random cases (`cm.py`) | **bit-identical** |
| Every distinct installation state (10) with a synthetic sample, 44.1 kHz stereo and 48 kHz mono, two notes (`inst67.py`) | all within −131…−138 dB except the two slice-mode presets when the DLL found slices (−0.6 / −1.5 dB: slices not modelled) |
| Key modes 1 and 2 (`tkey.py`) | model within 9·10⁻⁸ of the DLL |
| Goldens of `tests.cpp` (`gold67.py exec`) | DLL vs C++ −130.5…−134.6 dB before the release |

---

## 5. LYNX port (`src/core/Granulizer.{h,cpp}`, InstrumentKind `granulizer`)

- The importer recognises "Fruity Granulizer" (any case), normalises every state version to version 7 with the
  channel's 0xDA/0xDB records as the processor snapshot (as 3x Osc), keeps the project's PPQ in `modelParams[0]` and
  resolves the channel's sample (0xC4) to `samplePath`; the rack loads it like a sampler's and hands it to the
  instrument (a changed sample rebuilds it). Without the sample the channel is silent, as in FL. The channel editor
  shows the 12 parameters.
- Host side as 3x Osc: Final.Vol = channel volume × Init.Vol, Final.Pan = note + channel pan, Final.Pitch =
  100·(key − 60) + channel and note cents, max polyphony 1 for mono channels, NewTick on the song's PPQ grid; the tempo
  for key mode 2 is the song's.
- x87 parts in `long double` with the control word forced to 64-bit precision and rounding down inside the reader's
  x87 code (`X87Down`, and explicit `fstp` stores where the compiler would convert a double with SSE); where long double
  is not the x87 format (MSVC, ARM) results are close but not bit-exact.
- By design / not modelled: slice modes and the slice key mode (§1.3), tempo sync, FL's interpolation quality message
  (24-point sinc used), `rcpps` and IPP's sines (exact reciprocal and the C library's sine: −128 dB, isolated
  near-integer samples about −87 dB), the end-fade table (exact cosine, as 3x Osc), the sample's own loop points (whole
  sample), the Hermite/linear qualities, one random seed per instrument (FL: per process, from the clock).

## 6. Drumaxx: target, state and parameters

- **Target** (copies in `r67/bin/`): `Plugins/Fruity/Generators/Drumaxx/Drumaxx_x64.dll` (Delphi wrapper and editor,
  SHA-256 `a4d71bbe4eeda64de1e0965cff48134903c751a1f79a75392c347dffcd5358c0`) and, next to it, `engine_x64.dll`
  (C++, classes named `DMSynth` / `DMPadGenerator` in its strings, SHA-256
  `e7fca7bf9843c07dde0adb770647f8f79df676b2e102b1d44ce9655971d56d94`, image base 0x180000000). The wrapper creates the
  engine through its exports and forwards notes, parameters, the rate and the state; the whole sound engine is in
  `engine_x64.dll` **[DECOMP + EXEC]**. Like the Granulizer it is **not** a 64-bit placeholder.
- **Tools:** Ghidra headless project `~/ghproj/r67_de` (whole engine decompiled to `r67/out/de_all.c`, split per function
  in `r67/out/dfn/`; signature-typed passes `de_sig.c`, `de_sig2.c` from our `r67/gh/scripts/SigDec.java`), `objdump -M
  intel` (`de_all.s`, `d8b0.s`, `e3b0.s`, `c970.s`) for every float operation order.
- **EXEC harness:** `r67/run/dm.c` → `dm.exe`, a copy of round 57's `tb.c` (same safety measures; the installation's
  DLLs loaded unmodified). `on3=` sets Init.Vol directly; `arr=` peeks engine memory through the wrapper (+0x140 → the
  wrapper object, +0x228 → `DMSynth`, +0x41c0 + 8·k → pad k). Probes calling single engine routines on our own inputs:
  `sc.exe` (the CRT `sincosf`), `pp.exe` (the CRT `powf`), `callf.exe` (round 57). Drivers in `r67/py/`: `dmrun.py`,
  `dmcmp.py` (C++ vs DLL), `dmbat.py` (all installation states), `dmpad.py`, `dmvar.py`, `dmprof.py`, `dmone.py`
  (single cases), `gold67d.py` (test goldens).
- **Data:** 137 Drumaxx states in the installation (133 distinct: presets, plugin database, the render test
  `System/Render tests/Drumaxx.flp` with keys 36/42/38); no demo project uses the plugin **[DATA]**.

### 6.1 State (`FUN_1800303a0`) **[DECOMP + DATA + EXEC]**

`"SSLF"`, u32 size, then the engine stream: a tag (`DMS3`/`DMS4`/`DMS5` or `DMD3…DMD5`; every installation state is
`DMS3` (130) or `DMS4` (7), 21 644 bytes), a 256-byte kit name, **720 floats** (16 pads × 44, then the master at
0x2c0…0x2cf), 16 patterns of 0x402 bytes (steps, then a length byte and the pattern's **trigger key** at +0x401 — 100 in
every installation state), 0x800 bytes (pad names), 16 cut-group bytes, 16 varint output buses (all 0 in the data).
Tags 1–2 go through older loaders (not in the data; not ported). Version 3 rescales every pad's "EG Retrig" by 1/3
(clamped to 0…1) after loading.

### 6.2 Parameters **[DECOMP + EXEC]**

Per pad (offset in the pad's 44): 0 Volume (gain v² + v²), 1 Pan (left 1 − p, right p), 2 Mute, 3 Solo, 4 Key (the
note that plays the pad), 5 Noise Retrig, 6 EG Retrig (the engine reads **pad 1's** 5 and 6 for every pad), 7–10
Mallet (level, decay, noise mix, noise seed), 11–17 Membrane (feedback, damping, tension, phase, material, size,
shape), 18–19 LOW (frequency, resonance), 20–22 SOF (frequency, decay, level), 23–28 MID (frequency, level, Q, frequency
modulation, level modulation, decay), 29–31 HIGH (frequency, gain, Q), 32 Lo-Fi, 33 Hold, 34 Pitch, 35–38 the four
velocity-modulation targets (a parameter index − 6, 0 = none), 39–42 their values. A modulated parameter j is
`(1 − vel)·P[j] + vel·V` where V is the value of the last target slot naming j (`FUN_180035700`). Master: 0x2c1 level
(`10^(1.6·p)/4`), 0x2c3 EQ on, 0x2c4…0x2cb the 8 band gains (`(p − 0.5)·16` dB), 0x2cc limiter on, 0x2cd limiter
drive (`10^(1.5·p)`), 0x2ce limiter release.

The per-note derivations (`FUN_18002c970`) are ported literally (float/double exactly as the code converts): cut = f²,
feedback = −f^0.3, size = trunc((1 − f)·512) ≤ 511, shape = trunc(128·f) in 1…128, mallet start `10^(−4(1−f))`, mallet
decay `((500f²)f² + 1)·10⁻⁴` with f = 1 − p, lo-fi steps `trunc((1 − f)^5·65536 + 4)` (off below 0.01), hold
`3f³ + 0.001` s, pitch `2^(2f − 1)`, LOW frequency `2^(11f + 2)` Hz and resonance `8f + 1`, HIGH `2^(4.1f + 8)` Hz with
Q `2(1 − f) + 0.5` and gain `(f − 0.5)·36` dB, MID `2^(9f + 5)` Hz, level ±16 dB, level modulation ±24 dB, frequency
modulation ±16, Q `4(1 − f) + 0.5`, envelope decay `((1000f²)f² + 1)·10⁻⁵`, EG retrig count trunc(3·p) (0…3), SOF decay
`((250(f/2)²)f² + 1)·10⁻⁵`, SOF level `f^8/2`.

## 7. Drumaxx: the pad generator **[DECOMP + objdump + EXEC]**

- **Rate and resampling** (`FUN_18002e3b0`): every pad runs at **38 kHz** and is read at the host rate with linear
  interpolation, step = (38000/fs)·pitch·2^(semitones/12) (the semitones are the note's Final.Pitch offset, clamped to
  ±12), in double, by sub-blocks of at most 256 output samples; left/right gains glide by 0.001 per sample toward
  `vol·(1 − pan)·Final.Vol`, `vol·pan·Final.Vol`.
- **Excitation:** a mallet impulse decaying toward 10⁻⁷ by the mallet decay each sample, times `(noise − ½)·mix +
  (1 − mix)` with noise from a 32-bit LCG (`×0xbb38435 + 0x3619636b`, top-to-float `·2⁻³²`). Each pad's initial seed
  steps a process-wide LCG (`×0x1c3c718d + 0xf50d3da5`) 1000 times per pad at construction (`FUN_18002c5b0`); with
  Noise Retrig the seed is reset to `trunc(p·(2³² − 1))` at every hit.
- **Membrane:** 128 delay lines of 4 lanes, line k of length 512 − 4k, the lanes offset by one sample each (the wrap
  shifts lanes 1–3, as the code does). Size/4 picks the first line and size + shape the last; each lane has a one-pole
  low-pass (the damping) and feedback; the outputs are weighted by a 512-entry table built from tension (phase step),
  phase and material (a power curve `|x|^e`, e from material) and summed in the code's lane order.
- **Sections in order:** the SOF resonator (four one-pole stages with a soft clipper `x/(0.005|x| + 1)` in double and
  a decaying feedback gain), the LOW high-pass (`FUN_18001bd70`, sine/cosine from the CRT's vector sine), the HIGH shelf,
  the MID peak (re-designed every 6 samples from its envelope; the envelope restarts up to "EG Retrig" times when it
  falls below 0.01), a clip to ±1, the hold/attack (`env += (1 − env)·0.1`) then release (`env −= env·0.001`) envelope,
  the lo-fi quantiser `trunc(y·n)/n`. A pad stops after 25 ms below 10⁻⁴ (after 500 ms if it never rose above it).
- **Biquads** (`FUN_18001bed0`): RBJ-style low shelf / peak / high shelf with A = `powf(10, g/20)`, ω limited to 3,
  bandwidth through `sinh(ln2/2 · q · ω / sin ω)` (double), coefficients below 10⁻⁹ flushed to 0; direct form I.

## 8. Drumaxx: host side, master and the C runtime's math **[DECOMP + EXEC]**

- **Notes** (`FUN_180034e00`, `FUN_180005fa0`): the wrapper turns Init.Vol into a MIDI velocity
  `trunc(127·ln(10·v + 1)/ln 21)`; velocity 0 is a note-off (it only stops a running pattern). Every unmuted pad
  whose Key equals the note plays (Solo: only soloed pads); a hit chokes the other pads of its cut group. Final.Vol
  scales the pad's target gains (`keyL = keyR`), **Final.Pan is ignored** (*round 169l: wrong — an artefact of this harness's
  ComputeLRVol stand-in `L = R = vol`; FL's law A gives `Vol·cos/sin((1 + pan)π/4)`, REA_rodada169l §1*), Final.Pitch − 100·(key − 60) gives the
  semitone offset. A note equal to a pattern's trigger key starts that pattern (not modelled, §9).
- **Master** (`FUN_180035840`, `FUN_180033910`): the 16 pads mix into an 8-band EQ (80·2^k Hz, Q 2, band 0 a low
  shelf, band 7 a high shelf, the others peaks; processed band 7 down to band 0, per channel), then the limiter (gain,
  a release curve `c²·c²` from the last overload, `c += release/fs`), then the output gain `level/2` reached by a
  0.005-per-sample glide from 0 when the plugin is created (`FUN_180024870`, `FUN_180025510`). EQ and limiter states
  reset when no pad is active.
- **C runtime math.** Bit-exactness needed the engine's own CRT routines (the FMA paths, chosen by the CPU flag at
  `0x180087544`, which this machine takes): `powf` (`0x180046690`: ln x in double from a 256-entry ln(1 + i/256) table
  and a cubic, or near 1 from `s = u/(u + 2)` and an odd series; then `e^t` from k = round(64t/ln 2), a 2^(j/64) table
  and a cubic; the near-1 branch falls into the unfused exp), `sinf`/`cosf` (`0x18005c0c0`/`0x18005b2e0`: argument
  reduction with π/2 in two parts and double polynomials) and the vector sine behind `sincosf` (`0x18005a370`/
  `0x1800599e0`). Our probes matched the ports on **200 000/200 000** `powf` pairs, 42 000/42 000 `sinf`/`cosf` and
  300/300 vector-sine inputs **[EXEC]**. The tables in LYNX are the exact mathematical values rounded to nearest,
  computed by us (`r67/pw/`), and equal the routine's. The double `sinh` (`0x18005c7b8`, lead/tail tables whose tails
  are not the rounded remainders) and `pow` (`0x180044ff0`) stay the C library's: they differ from the CRT in the last
  bit of a double in about 26 % / 0.2 % of the inputs, which never changed a float result in the tests below.

## 9. Drumaxx: verification and LYNX port

| Test | Result |
|---|---|
| LYNX's C++ (`drumaxxrender`) vs DLL, all 133 distinct installation states, random notes (2–8, random Init.Vol, Final.Vol, pitch offsets), 22.05…96 kHz, blocks 64…1000 (`dmbat.py`, three seeds, `out/dmbat6_*`, `dmbat7_*`, `dmbat8_*`) | **bit-identical in every case** **[EXEC + model]** |
| The same with the master EQ and limiter off (`DM_NOEQ=1`, `out/dmbatq6_*`) | **bit-identical** |
| Goldens of `tests.cpp` (our own synthetic kit, `gold67d.py`) | **bit-identical** to the DLL |

LYNX port (`src/core/Drumaxx.{h,cpp}`, InstrumentKind `drumaxx`): the importer recognises "Drumaxx" and keeps the raw
state in `pluginState` (an unreadable state leaves the channel unsupported); the instrument plays the pads on their
keys with the channel volume as Final.Vol, the channel and note cents as the pitch offset and the note velocity mapped
back to Init.Vol; the plugin's output gain starts settled (FL creates the plugin long before playback). The channel
editor shows the master level and the 16 pad volumes.

**Not modelled:** the pattern sequencer (patterns start on their trigger key — 100 in every installation state —
with swing) and its note-off stop; the separate mixer-track outputs (every pad plays on the master; all installation
states use the master); MIDI controllers; a demo-mode silence path present in the engine (never triggered in the EXEC
runs); the CRT's non-FMA paths (a CPU without FMA takes other `powf`/`sinf`/`cosf` code in the DLL: not checked); the
CRT's double `sinh`/`pow` (above); on Windows LYNX's `std::fma` must be a true fused operation.

## Evidence index

| Address | What | Marker |
|---|---|---|
| `CreatePlugInstance` `0x7514f0`, `FUN_00751ce0` | instance, info record `0x7b13c8`, editor wheels | DECOMP |
| `FUN_00751540` | Dispatcher (1 quality, 4 rate, 10 sample, 0x13 beats, 0x33/0x34 hints) | DECOMP |
| `FUN_007519f0` | SaveRestoreState | DECOMP + DATA |
| `FUN_00752580`, `FUN_00752170`, `FUN_007521f0`, `FUN_00752290`, `FUN_00752520` | parameters and laws | DECOMP + EXEC |
| `FUN_00753580`, `FUN_007534e0` | TriggerVoice, polyphony | DECOMP + EXEC |
| `FUN_00753ea0`, `FUN_00753c70` | Voice_Render, grain start | DECOMP + EXEC |
| `0x60b3f0`, `0x60b5c0`, `0x60db00` | grain mixers (SSE), x87 pan law | DECOMP + EXEC |
| `FUN_005fd890`, `FUN_005fce50`, `FUN_005fc8b0`, `FUN_005fd480` | sample reader | DECOMP + EXEC |
| `0x5f8f40`, `0x5f8c40`, `0x5f8ca0`, `0x5f8780`/`0x5f87e0`, `0x5f9880` | windowed sinc | DECOMP + objdump + EXEC |
| `FUN_005a2b90`, `0x795250` | x87 control word 0x73F in the reader | DECOMP |
| `FUN_0060b020`, `0x79cda0`, `0x79c294` | interpolation quality default (12) | DECOMP + EXEC |
| `0x407c10`, `0x76c010`, `0x407bc9` | Delphi Random, RandSeed, Randomize at load | DECOMP + EXEC |
| `dsp_ippv2_x64.dll` `ippsVectorSlope_64f`, `ippsSin_64f_A50` | IPP arithmetic | EXEC |
| `engine_x64.dll` `FUN_1800303a0` | Drumaxx state stream | DECOMP + DATA + EXEC |
| `FUN_18002c970`, `FUN_180035700` | pad parameter laws, velocity modulation | DECOMP + objdump + EXEC |
| `FUN_18002d1a0`, `FUN_18002d8b0`, `FUN_18002e3b0`, `FUN_18002c5b0` | trigger, 38 kHz generator, resampler, seeds | DECOMP + objdump + EXEC |
| `FUN_18001bd70`, `FUN_18001bed0` | LOW high-pass, shelf/peak designs | DECOMP + objdump + EXEC |
| `FUN_180034e00`, `FUN_180005fa0`, `FUN_180035840`, `FUN_180033910`, `FUN_180024870` | notes, velocity, master EQ/limiter, rate, output gain | DECOMP + EXEC |
| `0x180046690`, `0x18005c0c0`, `0x18005b2e0`, `0x18005a370`, `0x1800599e0`, `0x180087544` | CRT powf, sinf, cosf, vector sine, sincosf, FMA flag | objdump + EXEC |
| `0x18005c7b8`, `0x180044ff0` | CRT sinh, pow (not ported) | objdump + EXEC |

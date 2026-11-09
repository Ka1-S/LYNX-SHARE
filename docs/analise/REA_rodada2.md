# Behavioural analysis of FL Studio 2026: round 2 (REA)

> **Nature of this document.** Continuation of `docs/ANALISE_FL_STUDIO.md`. It is *clean-room* reverse
> engineering, behavioural only. No Image-Line code or pseudocode has been copied: what is here are
> descriptions, data layouts and formulas written in our own words. Addresses (`FUN_xxxxxxxx`),
> offsets and globals serve only to trace the evidence.

- **Target:** `~/projetos/daw-analise/FLEngine_x64.dll` (SHA-256 `6d9c5ab0…8e02`), Ghidra 12.1.4 via REA
- **Format validation:** the 165 `.flp` files in `FL Studio 2026/Data` (versions 10.9 … 26.1), read by our
  own event parser
- **Date:** 2026-10-13

| Marker | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation of the cited function(s) |
| **[STR]** | Inferred from strings, RTTI or DFM forms |
| **[DATA]** | Confirmed statistically in the demo `.flp` files |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

Convention: `T+0xNN` is an offset into the mixer track record (global array `G[0x15e06a0]`, stride 0x1478).

---

## 1. Latency compensation (PDC)

Answers open question no. 1 of the main document.

### 1.1 Functions

| Function | Role |
|---|---|
| `FUN_01100530` | Entry point of the recalculation: latency snapshot → propagation → compensation → global total **[DECOMP]** |
| `FUN_010fef10` | Snapshot: stores the latency reported by each plugin (generator or slot) in `plugin+0xC8`. A bypassed plugin reports **0** (`FUN_012b6930` / `FUN_013308f0`) **[DECOMP]** |
| `FUN_010ff820` → `FUN_010ff3b0` | Recursive propagation through the routing graph (DFS with memo; the sentinel `0x7FFFFFFF` in `T+0xC4` marks "not computed") **[DECOMP]** |
| `FUN_010ffca0` | Compensation: computes the length of every delay line **[DECOMP]** |
| `FUN_01100b10(track, flags)` | Latency in ms for the UI and for the master total (bit 1 = recursive through inputs, bit 2 = sum of slots, bit 4 = without manual-out) **[DECOMP]** |
| `FUN_006a3a90` / `FUN_006a3ac0` | Samples↔ms conversion **[DECOMP]** |
| `FUN_009289b0` (`TBasicDelay_32FS`) | Delay line **[DECOMP+STR]** |

### 1.2 Track fields used by PDC **[DECOMP]**

| Field | Type | Meaning |
|---|---|---|
| `T+0x1C8` | float ms | **Manual input latency.** If ≥ 0, it acts as a floor: `max(manual, generator latency)`. If < 0, its magnitude is *added* and the track input is delayed by it |
| `T+0x1CC` | float ms | **Manual output latency**, always added |
| `T+0xBC` | int smp | Own input latency (routed generators + manual) |
| `T+0xB8` | int smp | **Input alignment** = `max(T+0xBC, max over sources S→T of S+0xC4)` |
| `T+0xC0` | int smp | Sum of slot latencies + manual-out |
| `T+0xC4` | int smp | **Output latency** (arrival time at the track output) |
| `T+0xA0/0xA8/0xB0` | delay | Delay lines: meter/visual, input (manual < 0), physical output |
| `T+0x1D1` | byte (default 1) | "Delay the output of this track by the total mixer latency when it has an external output" |
| `T+0x1D0` | byte (default 1) | Latency compensation when recording external input |
| `T+0x1472` | tri-state | Track **excluded** from PDC (0 = no, 1 = yes, 2 = conditional) **[DECOMP; name HYP]** |

The two manual latency floats are exactly the fields that event `0xEC` stores (see §2.6). The
"XOR-obfuscated field" of the main document **is not obfuscation**: in files < 12.9.0 the value was
stored with the sign bit flipped (XOR `0x80000000`, `DAT_00f50e60`).

### 1.3 Propagation algorithm (`FUN_010ff3b0`) **[DECOMP]**

For each track T, in DFS:

1. `genLat` = largest latency among the generators whose destination is T (channel field `+0x258`, or extra
   plugin outputs in the list `+0x128`).
2. `T.inOwn = smp(manual ≥ 0 ? max(ms(genLat), manual) : ms(genLat) + |manual|)`.
3. `T.inAlign = max(T.inOwn, max(S.outLat for each S with route S→T))`. The route exists when the byte
   `S+0x398+T*8` is ≠ 0. If S has not been resolved yet, it recurses into it.
4. If global PDC is on, it walks the active slots in order accumulating `run`, which starts at
   `inAlign`:
   - `slot.inPos = run`
   - `slot.align = max(run, latency of the *sidechain* sources feeding that plugin)`
   - `run = slot.align + slot.lat`
5. `T.outLat = run + smp(manualOut)` and `T.sumLat = Σslot.lat + smp(manualOut)`.
6. `M` = maximum `outLat` over all tracks (except 501), which is the total mixer latency.

### 1.4 Where the delay lines live: **per-edge** compensation (`FUN_010ffca0`) **[DECOMP]**

Auto PDC applies the rule "maximum of the inputs minus the latency of each path" **on each edge of the
graph**, not at the track input:

| Location | Delay length |
|---|---|
| **Each send/route S→D** (array at `T+0x1344`, 0x18 B entries `{dest, level, levelRamp, delay*, tailCounter}`) | `D.inAlign − S.outLat`. Only created if > 0, with PDC on and the track not excluded |
| **Each generator output or extra plugin output** (list `TFXTrackSideIOList` at `T+0x128`, 0x28 B entries) | `destination.inAlign − (plugin.align + plugin.lat)` |
| **Each sidechain** (list at `T+0x130`) | `plugin.align − source.outLat` |
| Physical track output (`T+0xB0`) | `M − T.outLat`, if the track has an external output and `T+0x1D1` |
| Meter/visual (`T+0xA0`) | `M − T.outLat`, only to align the meter |
| Input (`T+0xA8`) | only when the manual input latency is negative |

When a length changes, the line is cleared and a "changed" flag is set.

**The delay line** (`TBasicDelay_32FS`) is a ring buffer of interleaved float32 stereo frames
(8 B/frame). The delay is an integer number of samples, without interpolation. Each sample is swapped in place between the
buffer and the stream **[DECOMP]**.

### 1.5 Totals, plugins and options

- The total in ms is `FUN_01100b10(0, 3)` and goes to `*PTR_DAT_015e2b30`. The total in samples, including the
  driver output latency, goes to `*PTR_DAT_015e3408` **[DECOMP]**.
- The plugin is informed (opcode 0x1E) of its input offset `inLat + slot.align` and of the remaining
  latency until the end of the chain **[DECOMP]**. This is what *PDC-aware* plugins use.
- **Tail hold:** `T+0x36C = T.inOwn + T.sumLat (+ SR/5 if the track EQ is active)`. Keeps the
  track processing after the signal disappears, so that the delay lines drain **[DECOMP]**.
- "Compensate for master latency" and "Delay never bypass latency comp": the strings only appear in
  DFM/tables, with no direct xref. The candidate for the second one is `T+0x1472` **[HYP]**.

### 1.6 "Trim PDC silence" on export (`FUN_00d18130`, `FUN_00d175b0`) **[DECOMP]**

- At the start of the render, each *writer* (one per stem; see §5) receives the accumulated latency of its track in
  samples.
- The first `latency` samples are **discarded**. The block in which the counter crosses zero is written from
  the exact point.
- After the end of the song, the render continues for **another `latency` samples**, so the ending is not
  cut off.
- Without the option, the counter is 0 and the PDC silence stays in the file.
- Without split, the master uses global counters (`0x15e4d90`/`0x15e3688`).

### Recommendations for LYNX (PDC)

1. **Per-edge model.** In topological order, compute `inAlign[T] = max(inOwn[T], outLat[S] ∀ S→T)` and
   `outLat[T] = inAlign[T] + Σlat(active slots) + manualOut[T]`. Put a `DelayLine` on **each edge**
   S→T of length `inAlign[T] − outLat[S]`, and the same for generator→track and for sidechains. The longest
   path never gets a delay.
2. **A bypassed plugin counts as latency 0.** Bypass, adding or removing a plugin, and a change of
   `getLatencySamples()` trigger a recalculation outside the audio thread. The result goes to the audio
   thread via atomic swap, and the lines whose length changed are cleared.
3. The delay line is a float stereo ring buffer with an integer delay. `juce::AudioBuffer` + a circular
   index is enough.
4. Offer per-track manual latency (input and output, in ms) and the option "align physical output to the total
   latency M".
5. On export, discard `outLat` samples from the start of each stem and render the same amount extra at the end.
   Leave it on by default.
6. Keep the track alive for `latency + tail` after silence (hold), otherwise the end of the delayed material
   is cut off.

---

## 2. Mixer: processing order, sends, pan, solo, smart disable

The track processor is `TMixerTrackModule` (unit `Mixer.TrackModule`, VMT `0x1276868`).
`Process` = `FUN_01276ce0` → **`FUN_01276ee0`** **[DECOMP+STR]**.

### 2.1 Order inside the track **[DECOMP]**

Before the tracks, in `FUN_00d0d4c0`, each generator goes through its PDC line and is **summed** into the buffer of the
destination track (under the track lock), which gets marked as "has signal". Then, inside the track:

1. **Physical input monitoring** (`FUN_01276b40`): adds the hardware input. Mode in `T+0x30`:
   0 = off, 1 = when armed, 2 = auto, 3 = always.
2. Updates "has signal" and checks whether there is a pending tail in the PDC lines.
3. Recording tap (`T+0x2F`): 0 = input, 1 = pre-FX, 2 = post-FX, 3 = post-EQ, 4 = post-fader,
   5 = final.
4. **Silent path:** with no signal, no tail and the hold at zero, the track skips everything, clears the output and the
   ramps *snap* to the target.
5. **10 slots in series** (`TPluginRackModule`, `FUN_00cf2a90`). Each slot does a **ramped dry/wet mix**
   (slot "mix" parameter, 0..12800).
6. **Built-in 3-band parametric EQ** (`T+0x1D4 + i*0x88`; freq =
   `10 + 159.9·(100^(x/65536) − 1)` Hz).
7. **Volume / pan / stereo separation / polarity / swap**, as a **ramped 2×2 matrix** (§2.2).
8. **Mute** (`T+0x18 == 0` clears) with a **mute fade** (`FUN_01277350`, ≈ 1 ms **[HYP]**).
9. Meter (with alignment delay `T+0xA0`).
10. **Sends/routes** (`FUN_01277980`): for each route, the gain is smoothed per block (linear ramp with a maximum
    step). It then copies, goes through the edge's PDC line and **sums into the destination** under the
    destination's lock. **All routes are post-fader and post-mute; there is no pre-fader on this path.**
11. Sidechains (`FUN_01277bd0`), extra plugin outputs (`FUN_012777e0`) and physical output (`T+0x74`).

Summarised order: **input → 10 FX (dry/wet) → EQ → fader/pan/sep → mute → meter → post-fader sends**.

### 2.2 Volume, pan and stereo separation (`FUN_0127b050`, applied in `FUN_006a13a0`) **[DECOMP]**

Fields: `T+0x384` volume (int, **unity = 12800**, max 16000), `T+0x388` pan (int ±6400),
`T+0x38C` separation (int ±64), `T+0x390` polarity, `T+0x391` L/R swap.

Output: `Lout = a·L + c·R`, `Rout = b·R + d·L`.

1. **Fader curve:** with `v = vol/12800`, `g = (11^v − 1)/10`. At v = 1, g = 1 (0 dB); at 16000
   (v = 1.25), ≈ +5.6 dB. Polarity negates g.
2. **Pan law** (global, `DAT_015f49a4`):
   - **Constant power** (default): `θ = (1+p)·π/4`, `gL = cos θ·g√2`, `gR = sin θ·g√2`. Gives 0 dB at the
     centre and +3 dB on the side being panned to.
   - **Linear** (mode 1): for p ≥ 0, `gR = g` and `gL = (1−p)·g`, and the symmetric case for p < 0.
3. **Stereo fold:** the attenuated channel does not vanish; half of it is folded to the other side. With
   `f = 1 − g_side/g`, the matrix gets `(1 − 0.5f)` direct and `0.5f` crossed. At hard pan this yields a mono sum.
4. **Separation:**
   - `sep > 0` (narrow): `s = (5^(sep/64) − 1)/4`. At +64 the signal becomes mono.
   - `sep < 0` (widen): `s = sep/64`. At −64, `L' = 1.5L − 0.5R` (Side ×2).
   - `s` is added to `f` on both sides, clamped ≤ 1.
5. **Swap** swaps the matrix rows. Fast path: unity + centre + sep 0 = identity.

### 2.3 Solo and mute **[DECOMP]**

- Bytes: `T+0x18` enabled, `T+0x1A` solo, `T+0x1B` *mute-lock*, `T+0x1C` saved state.
- **Solo is not an audio mask.** It rewrites the `enabled` of every track (`FUN_0127c2a0` →
  `FUN_0127bb10`). A track T stays active if:
  - T is the soloed track, or
  - (mode & 2) and **T routes transitively to the soloed one** (upstream sources), or
  - (mode & 1) and **the soloed one routes transitively to T** (downstream destinations, e.g. master/bus).
- The transitive closure is a DFS over the route bytes (`FUN_0127c640`/`FUN_0127c6f0`). The default mode
  (API) is 2, which includes the sources.
- Tracks with mute-lock do not change.
- On un-solo, it restores the saved state (if a global option is on) or re-enables everything.

**Answer to the question:** yes, solo keeps audible the tracks that route *to* the soloed one. The path
to the master keeps working because the master itself is not muted.

### 2.4 Smart disable (`FUN_00cf2f90` / `FUN_00cf3020`) **[DECOMP]**

- Only applies if the plugin has "Allow smart disable" (`plugin+0xF7`).
- Silence test: all samples of the block (L and R) with |x| ≤ **1.5849e-5 (≈ −96 dBFS)**.
- With silent input, the counter accumulates the block size. When it reaches **SR × 4 (4 s hold)**,
  the slot is **skipped**.
- The counter is reset if the input **or the output** is not silent, which preserves reverb and delay
  tails.
- The whole track only stops processing when it has no signal **and** the latency/tail hold (`T+0x36C`)
  has reached zero.

### 2.5 Routing: event `0xEB` **[DECOMP+DATA]** (dispatcher `FUN_00f46490`)

- The payload is an **array of boolean bytes**, one per destination track, truncated at the last destination
  used. Sizes seen: 1…39 B.
- Byte i ≠ 0 means "this track sends to track i". It goes to `T+0x398 + i*8`.
- **The send level is not here.** It is in event `0xE1` (§2.7).
- Index remapping in old files:
  - `< 8.5.0`: destination > 64 adds 35.
  - `< 12.9.0`: destination > 103 adds 397.
  - `< 24.2.99`, or without the file flag `+0x4447`: destination > 125 adds 375.
  - That way the old "Current"/extras land on 500/501.

### 2.6 Track flags and latency: event `0xEC` **[DECOMP+DATA]**

**12-byte** payload in versions ≥ 12.9.0 (5 bytes in very old files): `f32 latOut_ms`,
`u32 flags`, `f32 latIn_ms`.

| `flags` bit | Field | Meaning |
|---|---|---|
| 0x0001 | `T+0x390` | Polarity inverted |
| 0x0002 | `T+0x391` | L/R swap |
| 0x0004 | `T+0x13F5` | ? (almost always on) |
| 0x0008 | `FUN_012c1810` → `T+0x18` | **Enabled** (bit cleared = muted) |
| 0x0010 | `T+0x380 = !bit` | **Effects off** (the 10 slots) |
| 0x00C0 | `T+0x2D` | Dock: 0 = left, 1 = middle, 2 = right |
| 0x0400 | `T+0x2C` | Visual separator |
| 0x0800 | `T+0x1B` | Mute-lock |
| 0x1000 | `T+0x1A` | Solo |
| 0x2000 | `T+0x2E` | ? In files < 20.9 it sets the recording tap (1 if on, otherwise 3/5) **[HYP: "audio track/armed"]** |
| 0x4000 | `T+0x1D1 = !bit` | Do **not** delay the physical output by the total latency |
| 0x8000 | `T+0x1D0 = !bit` | Do **not** compensate recording latency |

Most common values in the demos: `0x4C` (enabled + dock middle + bit 4), `0x44C` (with separator), `0x204C`.
In files < 12.9.0 there was a single latency float with the sign flipped; bit 0x20 chose whether it was
output or input latency.

### 2.7 Mixer parameters and send levels: event `0xE1` + control IDs **[DECOMP+DATA]**

`0xE1` is an array of **12-byte** records `{u32 reserved, u32 controlID, i32 value}` (the same
format as the control events `0xD8`/`0xDF`, type 0 in `FUN_00f558e0`). At the end of the load, each record is
applied with `FUN_01046a70(controlID, value, 0x10011)`, the **universal automatable-parameter setter**.

**Encoding of the `controlID` (u32)**, which is the same address used by the automation links (§3.3):

| Range | Target |
|---|---|
| `< 0x10000000` | Channel Rack channel: `channel = id>>16`, `param = id & 0xFFFF` (0…12 = generic channel parameters; 0x10…0x14, 0x103… etc.) |
| `0x40000000 + n` | Global parameters (tempo, master pitch…) **[HYP for each n]** |
| `0x70000000 + (track << 22) + (slot << 16) + p` | **Mixer**: track 0…501, slot 0…9 |

Values of `p` for the mixer:

| `p` | Meaning | Range / default |
|---|---|---|
| `0x1F00` | Slot on | 0/1 |
| `0x1F01` | Slot mix (wet) | 0…12800, unity 12800 |
| `0x1FC0` | Track volume | unity 12800, max 16000 |
| `0x1FC1` | Pan | ±6400 |
| `0x1FC2` | Stereo separation | ±64 |
| `0x1FD0…0x1FD2` | Gain of the 3 EQ bands | |
| `0x1FD8…0x1FDA` | Frequency of the 3 bands | |
| `0x1FE0…0x1FE2` | Q of the 3 bands | |
| `0x2000 + d` | **Send level to track d** (d up to 0x1FF) | 0…16000, unity 12800 |
| `0x8000 + k` | Parameter k of the plugin in the slot | |

- In files < 24.2.99 the mixer class was `0x2…` with the track in 6 bits. The converter `FUN_00f45460`
  adds `0x50000000`, moves sends `0x1F40+d` to `0x2000+d` and sends the old track 126 to 501.
- In memory, the send lives at `T+0x394 + d*8 = {i32 level, u8 enabled}` **[DECOMP]**.
- With flag `0x20` in the setter, the value arrives **normalised** (0…2³⁰) and is mapped linearly to
  the parameter's `[min, max]`. This is the automation path **[DECOMP]**.

**Order of a track's events in the FLP [DATA]:** `0x95` (colour), `0x5F` (icon), `0xCC` (name),
`0xEC` (flags/latency), 10× [optional plugin: `0xC9` default name, `0xD4` header, `0xCB` name,
`0x80` colour, `0xD5` state] followed by `0x62` (slot index), then `0xEB` (routes), `0xA5`, `0xA6`,
`0x31`, `0x9A` (physical input), `0x93` (physical output). The track record **closes** at `0x93`. The
master has no name; the `0xCC` that comes right after it already belongs to track 1.

### Recommendations for LYNX (mixer)

1. Fixed order: **sum of inputs → 10 slots (each with dry/wet) → optional 3-band EQ → ramped 2×2
   matrix (vol/pan/sep/phase/swap) → mute with ~1 ms fade → meters → post-fader sends**. If we
   offer a pre-fader send, it is our own extension; FL does not have it.
2. Fader: `g = (11^v − 1)/10`, with `v = value/12800` ∈ [0; 1.25] and unity at 0.8 of the travel (12800/16000).
3. Two pan laws: "constant power" (sin/cos × √2) and "linear". Implement the 50% fold of the attenuated
   channel, because that is what gives FL's pan its characteristic sound.
4. Separation: exponential formula for narrowing and linear for widening.
5. Solo: compute `enabled` from the transitive closure of the graph, including sources by default. Respect
   mute-lock and save the state for restoring.
6. Smart disable: −96 dBFS, 4 s hold, testing input **and** output.
7. FLP importer: routes come from `0xEB` (booleans) and levels come from `0xE1` with `p = 0x2000+d`. Parse
   the `controlID` exactly as in the table above; it also serves to resolve automation
   targets.

---

## 3. Automation clips

### 3.1 Where they live in the FLP **[DECOMP+DATA]**

- Each automation clip is a **Channel Rack channel** with `ChanType` (event `0x15`) = **5**. The types
  seen in the demos are 2 = generator plugin, 4 = audio clip, 5 = automation clip.
- The points come in event **`0xEA`** inside the channel block. The load passes the blob to the channel's
  automation object (`channel+0x360`, virtual method +0x10 = stream). The save is `FUN_00f3e190`, which
  serialises that object and writes `0xEA` **[DECOMP]**.
- The **target** comes in event **`0xE3`** (20 B record, one per link; several links per clip are
  possible) **[DATA]**:

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | `(clip_channel << 16)`, where the high u16 is the index of the automation channel |
| 4 | u32 | 0 |
| 8 | u32 | **Target `controlID`**, same encoding as §2.7 (e.g. `0x72C51F01` = mix of slot 5 of track 11; `0x73068001` = parameter 1 of the plugin in slot 6 of track 12; `0x000D0004` = parameter 4 of channel 13) |
| 12 | u32 | Flags (8 in automation clip links) |
| 16 | u32 | ? (constant `0x1D5` in the demos) |

- Event `0xE2` has the same layout with flags 4. These are the **remote control/MIDI** links (e.g. field 0 =
  controller number). Both go to global lists (`PTR_DAT_015e0a00[id−0xE2]`) **[DECOMP]**.

### 3.2 Layout of event `0xEA` **[DATA + DECOMP of the point record]**

```
Offset  Size  Content
0       4     u32 = 1     (version?)
4       4     u32 = 0x40
8       4     u32 = 0x400 or 0x401   (flags?)
12      1     u8  = 0
13      4     u32 = 3
17      4     u32 N = number of points
21      24·N  points
21+24N  112   trailing block (LFO/clip configuration; identical in all clips of a project) [HYP]
```

Total size = 133 + 24·N. In the demos 157, 181, 205… appear **[DATA]**.

**Point (24 B)**, same point layout as the `TCustomMEnvelope` family (`FUN_00b97c40`):

| Offset | Type | Field |
|---|---|---|
| 0 | f64 | **Δx: distance to the previous point** (not an absolute position). The first point has Δx = 0 |
| 8 | f64 | **Normalised y value, 0…1** |
| 16 | f32 | **Tension**, −1…+1 |
| 20 | u8 | **Curve type** of the segment that *ends* at this point (see table) |
| 21 | u8 | Selected (cleared on load) |
| 22 | u8 | ? |
| 23 | i8 | Direction, a derived cache: 0, +1 (tension > 0), −1 (tension < 0), 2 (straight) |

- Absolute positions = cumulative sum of Δx. In the demos, Δx has fractions of 1/96 (PPQ 96), which indicates
  **units of beats (quarter notes)** **[HYP, strong]**.
- The direction byte is recomputed on load (`FUN_00b9ba60`) from the sign of the tension. If the tension is 0
  and the type is a curve, it becomes 2 (straight). It need not be reliable in the file.
- In memory, each point takes 0x20 B. The fields `+0x10/+0x14/+0x18` are coefficients precomputed
  from the tension and `+0x1C` is the relative position **[DECOMP]**.

### 3.3 Curve types and formulas (`FUN_00b9def0`, coefficients in `FUN_00b9ba60`) **[DECOMP+STR]**

The names come from the `PtCurveNMenu` menu (DFM). `t` ∈ [0,1] is the phase within the segment, `y0` and `y1` the
endpoint values and `v = y0 + F(t)·(y1 − y0)`:

| Type | Name | F(t), our description |
|---|---|---|
| 0 | Single curve | Exponential: `k = |tension|·ln(≈10⁶)`, `F = (e^{k t} − 1)/(e^k − 1)` with tension < 0, and the mirror `1 − (e^{k(1−t)} − 1)/(e^k − 1)` with tension > 0. Tension 0 → straight line |
| 1 | Double curve | The same curve applied on two symmetric halves (S) |
| 2 | Hold | F = 0: holds y0 until the next point (step) |
| 3 | Stairs | Steps: `round(t·n − offset)/n`, with the number of steps n derived from the tension |
| 4 | Smooth stairs | Steps with a cubic transition between them |
| 5 | Pulse | Square wave between y0 and y1, with the number of cycles derived from the tension |
| 6 | Wave | Sine (or triangle, depending on the sign) between y0 and y1, with cycles set by the tension |
| 7 | Single curve 2 | Polynomial curve: `t − (u⁶ − u)·tension`, with u = t or 1−t depending on the sign |
| 8 | Double curve 2 | Two-half version of type 7 |
| 9 | Half sine | Blend between straight line and half sine, with weight = tension |
| 10 | Smooth | Cubic Hermite using the slopes of the neighbouring segments (smooth spline through the points) |
| 11 | Single curve 3 | Hyperbolic: `A/(1 − B·t) − C`, with B derived from `1 − (1 − 0.9|tension|)³` |
| 12 | Double curve 3 | Two-half version of type 11 |

In the demos all clip points use type 0 with varied tensions **[DATA]**. For importing, it is enough to
implement 0, 1, 2 and 10; the others can fall back to a straight line.

### 3.4 Playback scheduling **[DECOMP]**

- The clip in the playlist spawns a **voice** `TMixVoiceAutomationClip` (unit `Mixer.Voices`, VMT `0xd1ff48`),
  with its own pool (`TDynamicVoiceArray<…AutomationClip>`).
- **Start** (`FUN_00d2a930`):
  - the length is `endOffset − startOffset` of the playlist item (item floats, §4.2);
  - positions the envelope at `startOffset / speed`.
- **Per sub-block** (`FUN_00d24d80`): `elapsed += nSamples × speed`, where the speed
  (samples → envelope units) is a double of the automation object, recomputed with the tempo.
  - While `elapsed < length`, it advances the envelope state (virtual method +0x38), which interpolates
    the current segment.
  - When it passes, the voice ends (`FUN_00d244e0`).
- The envelope advance (`FUN_00b9def0`) crosses point boundaries within a block, recomputes the segment
  rate as `speed/Δx` and handles loop and sustain (envelope indices `+0x4C/+0x50/+0x54`).
- The resulting value goes to the target through the universal setter `FUN_01046a70(controlID, value, flags)`. With
  flag `0x20` the value is normalised (×2³⁰) and mapped to the parameter's `[min, max]`.
- **Granularity:** automation is applied at sub-block boundaries. The engine slices the buffer at
  ticks (see main document §1.1), so the effective resolution is **1 tick**.

### Recommendations for LYNX (automation)

1. **Model:** `AutomationClip { vector<Ponto{double dx_beats; double y01; float tensao; uint8 tipo;}>;
   vector<ControlID> alvos; }`. In memory use absolute positions and convert to Δx only on import/export.
2. **Single target address (`ControlID`)** in FL style (class | track | slot | parameter). This
   simplifies serialisation, automation links, MIDI learn and the FLP importer all at once.
3. **Normalised 0…1 values** throughout automation, mapped to the parameter range in the setter. FL
   does this for every parameter, including volume, which is 0…16000 with unity 12800.
4. **Evaluation:** precompute per-segment coefficients on edit (like `FUN_00b9ba60`) and evaluate per
   block or sub-block. Start with Single curve (exponential), Double curve, Hold and Smooth.
5. **Scheduling:** one "voice" per active clip, advancing `nSamples × (beats/sample)` and applying the
   value at the start of each sub-block. For plugins with sample-accurate parameter automation, emit the
   points with an offset within the block.
6. **FLP importer:** channel with ChanType 5 → clip; points from `0xEA` (21 B header + 24·N + 112);
   targets from `0xE3` (`controlID` at +8); position and length in the playlist (§4.2).

---

## 4. Note and playlist records in the new versions

### 4.1 Note: event `0xE0`, **24 bytes in all versions** **[DECOMP+DATA]**

In the 165 demos, **all** `0xE0` events have a size that is a multiple of 24, including those from 24.x, 25.x and 26.x.
The mapping below comes from the note→object conversion of the Piano Roll scripting API (`FUN_00c3ed10`, names
`number time length pan velocity color fcut fres pitchofs slide porta group muted selected release repeats`)
**[DECOMP]**:

| Off | Type | Field | Range / default |
|---|---|---|---|
| 0x00 | u32 | Position (ticks) | |
| 0x04 | u16 | Flags: **slide = `(flags & 0xF) == 8`**. Bit 0x4000 is always on in new files | `0x4000` / `0x4008` |
| 0x06 | u16 | Channel index in the rack | |
| 0x08 | u32 | Duration (ticks) | |
| 0x0C | u16 | Key (`number`) | 0…131 |
| 0x0E | u16 | Group (`group`, note grouping id) | 0 |
| 0x10 | u8 | Fine pitch: `pitchofs = value − 120` | 120 = centre |
| 0x11 | u8 | `repeats` (repetitions/ratchet) | 0 |
| 0x12 | u8 | Release (/128) | 64 |
| 0x13 | u8 | bits 0–3 **colour/MIDI channel (0…15)**; 0x10 **porta**; 0x20 **muted**; 0x80 **selected** | |
| 0x14 | u8 | Pan (/128) | 64 |
| 0x15 | u8 | Velocity (/128) | 100 |
| 0x16 | u8 | Mod X / "fcut" (/255) | 128 |
| 0x17 | u8 | Mod Y / "fres" (/255) | 128 |

Conversions of old files on load **[DECOMP]**:
- `< 3.1.6`: fine pitch = 120.
- `< 5.3.0`: the low nibble of `+0x13` is shifted `<<2`.
- `< 6.5.0`: release = 64.

**[DATA]** In demos ≥ 24: flags `0x4000` (13,282 notes) and `0x4008` (slide, 156); `+0x13` with 0x20
(muted, 364) and 0x80 (selected, 72); `repeats` with values 4, 10, 11, 12, 13.

### 4.2 Playlist item: event `0xE9` **[DECOMP+DATA]**

The record size (`FUN_00f558e0`, type 2) is decided by the version and by a **u32 of file flags
in event `0xA9`**:

| Condition | Size |
|---|---|
| version < 20.99.1 | 32 B |
| ≥ 20.99.1 and `(flagsA9 & 2) == 0` (includes files without `0xA9`) | **60 B** |
| ≥ 20.99.1, bit 2 on and `(flagsA9 & 8) == 0` | **80 B** |
| ≥ 20.99.1, bits 2 and 8 on | current native size: **88 B** (confirmed on a real FL 26.1.3 project with `0xA9 = 15`: 724 items, round gf92) |

In the demos: without `0xA9` → 60 B; `0xA9 = 7` → 80 B (24.2…25.2); `0xA9 = 15` (25.2.99/26.1) → native,
but both files are empty **[DATA]**. Bit 1 of `0xA9` also converts a channel field
int→float.

Layout (validated on 10,576 records of 80 B):

| Off | Type | Field |
|---|---|---|
| 0x00 | u32 | Position (ticks) |
| 0x04 | u16 | `0x5000` (base) |
| 0x06 | u16 | **Item:** < 0x5000 → channel index (audio clip or automation clip); ≥ 0x5000 → pattern `idx − 0x5000` |
| 0x08 | u32 | Length (ticks) |
| 0x0C | u16 | Playlist track, inverted: `track = 499 − value` **[HYP from the value; the load adds +100 in files < 9.?]** |
| 0x0E | u16 | Group |
| 0x10 | u16 | 120 (default clip fine pitch) |
| 0x12 | u16 | Flags: 0x0040 default; **0x2000 = muted** **[DECOMP, round 182e]** (205 cases); 0x8000 = selected (round 180c) |
| 0x14 | 4×u8 | 64, 100, 128, 128: default clip pan, vel, modX, modY |
| 0x18 | f32 | **Offset start** (−1 = none). In automation clips, it is the start point within the envelope |
| 0x1C | f32 | **Offset end** (−1 = none) |
| 0x20 | u32 | **Unique clip ID**, regenerated on load by an atomic counter (`FUN_0105f2e0`) |
| 0x24 | f32 | Fade-in? Cleared on load when `(flagsA9 & 4) == 0` and the item is of a specific type **[HYP]** |
| 0x28 | f32 | Fade-in tension? **[HYP]** |
| 0x2C | f32 | Fade-out? **[HYP]** |
| 0x30 | f32 | Fade-out tension? **[HYP]** |
| 0x34 | f32 | Clip gain, linear (default 1.0) **[HYP, strong]** |
| 0x38 | u32 | Fade/crossfade flags (0…3, `0x8008`, `0x800A`) **[HYP]** |
| **60–79: only in the 80 B record** | | Initialised in `FUN_01064e20` when absent |
| 0x3C | u32 | 0 |
| 0x40 | f64 | **1.0** by default: clip time/stretch factor **[HYP]** |
| 0x48 | u32 | 0 |
| 0x4C | i32 | −1 (in native; ignored in the 80 B one) |
| 0x50 | u32 | 0 (native only) |
| 0x54 | u32 | 0 (native 88 B only; seen 0 or small values) |

Round gf92 (real FL 26.1.3 project): +0x24 and +0x2C are the fade-in / fade-out lengths in **milliseconds**
(923.08 = 2 beats at 130 BPM; 461.5 = 1 beat), +0x34 the clip gain (linear: 0.05 … 1.65 seen; 0 on some clips).
LYNX imports them since then (`PlaylistItem::fadeInMs/fadeOutMs/gain` → `Clip::fadeIn/fadeOut/gain`).

### 4.3 Playlist track: event `0xEE` (70 B) + name in `0xEF` **[DECOMP+DATA]**

- There are 500 records per file, each followed by a `0x2B` (u8).
- Fields:
  - `u32 index` (1…500)
  - `u32 colour` (if it is `0x565148`, the track uses the default colour)
  - `u32 icon`
  - `u8 enabled`
  - `f32 height` (1.0)
  - `f32` (−32 / −16) at +0x11
  - `u8` at +0x15
  - block at +0x16
  - u8 at +0x2E…+0x30
  - u32 at +0x31/+0x35/+0x39
  - u8 at +0x3D
  - bit 0 of +0x42
- The load accepts shorter versions of the record, depending on the size.

### Recommendations for LYNX (format)

1. Note: fixed 24 B. Implement slide (`flags&0xF==8`), porta (bit 0x10 of +0x13), muted (0x20), colour
   (low nibble) and repeats (+0x11). In `flpdump`, print these fields for validation.
2. Playlist: choose the record size by **version + `0xA9`**, not by "size÷N", because 80×3 and
   60×4 are ambiguous (both give 240). Regression tests must cover the 3 sizes.
3. Item ≥ 0x5000 = pattern; < 0x5000 = channel (audio or automation clip).
4. For automation clips, use the start and end offsets (+0x18/+0x1C) to trim the envelope.

---

## 5. Export / render dialog

Source: the `TRenderForm` (new) and `TWAVRenderForm` (old, read by the render driver
`FUN_00d18130`) forms decoded from the DFM **[STR]**, plus the decompilation of the render **[DECOMP]**.

### 5.1 Options (default value in parentheses) **[STR]**

| Group | Options |
|---|---|
| Mode | Song, Pattern |
| **Tail** | **Cut remainder** (0, default), **Leave remainder** (1), **Wrap remainder** (2) |
| Processing | Enable master effects (on), Enable insert effects (on), **Trim PDC silence (on)**, Split mixer tracks (off; sub-options "Include Master" (on) and "Include Current" (off)) |
| Quality: resampling | 2-point linear, 6-point Hermite, 16/24/32/64/128/256/512-point sinc (groups Low/Average/High) |
| Quality: other | Dithering (off, "Dithered 16Bit output"), HQ for all plugins (on), Disable maximum polyphony (on) |
| Format | WAV (16 int / 24 int / 32 float), FLAC (16/24, compression 0–8, default 5), MP3 (bitrate 0–13), OGG (0–15), M4A |
| Channels | Stereo, Mono (merged), Mono (left only), Mono (right only) |
| Extras | Tempo info (ACID-type chunk), note markers, loop markers (on), playlist markers (on) |
| **Mastering** | In place of normalisation: LUFS target from −25 to −8, default −9; per-platform presets (Spotify, Apple Music, YouTube, CD…); requires FL Cloud |

**There is no peak normalisation in the 2026 render** **[STR]**. "Normalize" only appears in clips, in
Edison and in knobs.

### 5.2 Behaviour **[DECOMP]**

- **Cut (0):** at the tick where the song ends (`FUN_00d0e840`), the state goes to "stop". Only the
  PDC counters keep running.
- **Leave (1):** after the end, it keeps rendering the tail until the **first block in which the whole master
  is below ≈ −96 dBFS peak** (`FUN_00d0eb10`/`FUN_00c4d260`; threshold `exp(k·−96)`), with a **cap of
  30 s** (constant 30000 ms).
- **Wrap (2):** it is **not** mixing the tail over the start of the file. It is **pre-roll**:
  - the render starts with writing turned off;
  - at the first end, writing turns on (`FUN_00d14880`) and the loop goes back to the start;
  - the file receives the **second pass**, which already contains the tails of the first;
  - the second end cuts.
- **Trim PDC:** see §1.6.
- **Quality** (`FUN_01366a70`): writes the interpolation mode into all channels. With "HQ for all
  plugins", it uses flags 0x13 instead of 0x11.
- **Disable max poly:** saves each channel's polyphony limit, zeroes it (unlimited) and restores it at the end.
- **Master/insert FX:** turns the effects on or off via `FUN_0127c3e0`.
- **Split:**
  - one writer per track (1…N; master and Current optional);
  - file name = base + track name;
  - the writer lives in the mixer record at `+0x118`.
- **Mono:** merged sums (probably (L+R)/2 **[HYP]**); left/right extract one channel.
- **Dither** (`FUN_012c7510`), only on the **16-bit** path: TPDF (difference of two uniforms,
  ±1 LSB) with **2nd-order noise shaping via error feedback**,
  `tmp = x·32767 + 0.5·(2e₁ − e₂)`, then `q = round(tmp + noise)`, `e₂ = e₁`, `e₁ = tmp − q`, and
  saturation. Without dither, it only clamps and scales by 32767.

### Recommendations for LYNX (export)

1. Implement **Wrap as pre-roll**: render one pass without writing and write the second. It is simple and
   faithful to FL.
2. Leave: stop at the first block with peak < −96 dBFS, with a 30 s cap.
3. Trim PDC per stem, on by default (§1.6).
4. TPDF dither + 2nd-order shaping only for 16-bit integers. Keep the error in float.
5. During the render, switch the samplers' interpolation to the chosen quality, turn on plugin HQ
   and disable polyphony limits; restore everything at the end.
6. Normalisation is optional. If done, use a two-pass peak. Current FL uses a LUFS target, but
   it depends on the cloud service.

---

## 6. Piano roll: tool parameters **[STR]**

Only what was cheap to extract from the forms. There are no decompiled algorithms here.

- **Note properties:** fine pitch 0…240 (120 = centre), Mod X/Y 0…255 (128), release 0…128 (64),
  vel 0…128 (100), pan 0…128 (64), slide, porta, colour/MIDI channel 1…16, repeat 0…96.
- **Quantize:**
  - strength per property (pan, vel, modX, rel, modY, pitch) 0…128 (128);
  - start 0…128 (64), duration 0…128 (64), sensitivity/window 12…192 (36);
  - duration modes: Leave duration, Leave end time, Quantize duration, Quantize end time;
  - groove template;
  - shortcuts "Quick quantize" and "Quick quantize start times".
- **Arpeggiator:**
  - range Normal/Flip/Alternate, 0…6 octaves;
  - sync Time/Block/Chord;
  - time mul 1…7, gate 0…48, "Group notes".
- **Chop:** pattern, time mul, Absolute; quick chop at bars/beats/1/2…1/32.
- **Flam:** time −192…192 (−64), vel 0…128 (85); presets Flam/Early flam/Double stroke.
- **Articulator (Legato):**
  - multiply 10…100%, variation, gap 0…240 ticks, chop chords;
  - presets Legato/Portato/Staccato.
- **Strumizer:** start/vel/end with strength and tension; preserve end; trigger ahead.
- **Randomizer:** levels ±128 bipolar, seed, scale/chord, octave, range, population, length,
  "Glue same notes", "Random portamento".
- **Level scaling:** center, multiply (8192 = 1.0), offset, tension.
- **Others:** Flip, Claw machine, LFO (sine/tri/square, lin/log), Fill (Euclidean).

Recommendation: for LYNX, prioritise Quantize (start + duration + strength + window) and the event
editor with the properties above. Arp, Strum, Flam and Chop can be implemented later as
pure transformations over the note list.

---

## 7. Questions that remain open

1. The exact meaning of the header (21 B) and trailer (112 B) of `0xEA`, and the class of the object
   `channel+0x360`. The point is identical to `TCustomMEnvelope`, but the header does not match the stream of
   `TMEnvelope`.
2. Fields 0x24…0x38 of the playlist item (fades and clip gain): confirm by decompilation.
3. "Compensate for master latency", "Delay never bypass latency comp" and the UI value that corresponds to
   each pan law.
4. The exact constant of the tail's −96 dB threshold (read at runtime) and the placement of the Wrap pre-roll.
5. Bit 0x4000 in the note flags, bit 0x4 of `0xEC` and field +16 (`0x1D5`) of the automation links.

## Appendix B: new functions (traceability)

| Address | Inferred role | Confidence |
|---|---|---|
| `FUN_01100530` | Recomputes PDC (snapshot → propagation → compensation) | DECOMP |
| `FUN_010ff3b0` | Per-track latency propagation (inAlign/outLat) | DECOMP |
| `FUN_010ffca0` | Per-edge delay line lengths | DECOMP |
| `FUN_01100b10` | Latency in ms of track/master | DECOMP |
| `FUN_009289b0` | `TBasicDelay_32FS` (stereo ring buffer) | DECOMP |
| `FUN_01276ee0` | `TMixerTrackModule.Process`: internal track order | DECOMP |
| `FUN_0127b050` / `FUN_006a13a0` | Vol/pan/sep/phase/swap matrix and its application | DECOMP |
| `FUN_01277980` | Post-fader sends with ramp + per-edge PDC | DECOMP |
| `FUN_0127bb10` | Solo via transitive closure of the graph | DECOMP |
| `FUN_00cf2f90` / `FUN_00cf3020` | Smart disable (−96 dB, 4 s hold) | DECOMP |
| `FUN_01046a70` | Universal setter by `controlID` (load of `0xE1`, automation, MIDI) | DECOMP |
| `FUN_00f45460` | Converts old mixer `controlID`s to the 500-track scheme | DECOMP |
| `FUN_00f558e0` | Array record size (note 24, control 12, playlist 32/60/80/native) | DECOMP |
| `FUN_00f55690` | Playlist post-load (clip IDs, extension defaults) | DECOMP |
| `FUN_01064e20` | Defaults of the playlist item's extended block (+0x3C…) | DECOMP |
| `FUN_00c3ed10` | Note (24 B) → scripting API object (field map) | DECOMP |
| `FUN_00b97c40` | `TCustomMEnvelope` stream (24 B points) | DECOMP |
| `FUN_00b9ba60` | Per-point curve coefficient precomputation | DECOMP |
| `FUN_00b9def0` | Envelope advance and evaluation (13 curve types) | DECOMP |
| `FUN_00d24d80` / `FUN_00d2a930` | `TMixVoiceAutomationClip`: per-block advance and start with offset | DECOMP |
| `FUN_00f3e190` | Channel save (writes `0xEA`) | DECOMP |
| `FUN_00d18130` / `FUN_00d175b0` | Render driver: tail, Trim PDC, split | DECOMP |
| `FUN_012c7510` | TPDF dither with 2nd-order shaping (16 bits) | DECOMP |
| `FUN_01366a70` | Interpolation quality and HQ in the render | DECOMP |

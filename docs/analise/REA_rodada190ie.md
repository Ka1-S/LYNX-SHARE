# REA round 190ie: delays under tempo automation (issue #185)

Issue #185 (karatmep232): "When adding automation clips for the BPM, instruments using the Delay effect become
distorted. Doesn't happen with Reverb." Clean-room: FL's binaries were read statically (Ghidra 12.1.4 through the REA
tools, unmodified DLLs). No Image-Line code is in the repo, only facts in our own words.

Markers: **[DECOMP]** read in the decompiled code; **[EXEC]** measured by running LYNX's own code; **[DATA]** read
from the reporter's files.

## 1. The report [DATA]

The video (27 s, 1080p, `/mnt/e/lynx-work/bug190ie/dl/delay.mp4`, sha256 `2f9a5c84…6ae3bc3`) shows the following.

- A Tempo clip on track 4 ramps the tempo up over the first bars, drawn in the automation editor.
- One pattern plays through Insert 1, whose slot 1 is the basic **Delay** (the slot reads "Delay").
- In the audio's spectrogram (`frames/spec.png`), from 12.5 to 22 s the notes carry broadband vertical streaks up to
  16 kHz: clicks.
- At about 23 s the Delay is switched off, and from there the notes are clean harmonics.

## 2. How the tempo reaches an effect [EXEC, code]

`RackRenderer` works out the tempo once per audio block, at the block's start, from the tempo map and the Tempo
clips. Every effect then gets that value in `process (…, bpm)`. A Tempo clip therefore moves the tempo in small steps,
one per block: 120 → 140 BPM over 4 s at 48 kHz in blocks of 256 is about 0.027 BPM per step.

## 3. Root cause [EXEC]

The basic Delay (`EffectKind::delay`, LYNX's own) worked out its length at every block as
`(size_t) (beats · 60 / bpm · rate)` and read its line that many whole samples back. Each tempo step therefore
**jumped the read position** by a few samples: a discontinuity at every block, at the block rate (86 … 750 Hz). The
feedback then fed it back into the line. Reverb has no tempo-dependent length, which is why it stayed clean.

The measure (`tests/Bug190ieTests.inc`) is Δ⁴, the fourth difference of the output: a high-pass that leaves the test
chord at about −110 dBFS and turns a step of size a into peaks of about 6a. The ramp runs 120 → 140 BPM and is
compared with the same render at a steady tempo.

| Basic Delay (0.1.28) | Δ⁴ energy vs steady | Δ⁴ peaks |
|---|---|---|
| default, 44.1 / 48 kHz, blocks 64 … 512 | +60 … +71 dB | −20 … −35 dBFS |
| ping-pong, Feedback 0.8 | +65 … +76 dB | −13 … −29 dBFS |
| 1/16, Feedback 0.9 | +75 … +81 dB | −16 … −22 dBFS |

At a steady tempo the peaks are −107 … −110 dBFS.

## 4. What FL does

### 4.1 Fruity Delay 3: glide, or crossfade with Keep pitch [DECOMP]

Engine `Fruity Delay 3/engine_x64.dll`, sha256 `0b05f78f…7828891` (the same file as REA_rodada52). The function at
`0x180008ff0` was re-read in this round.

- **Keep pitch off (FL's default preset).** Per sample, each of the four tap lengths moves
  `d ← d + (target − d) · k` toward its whole-sample target. It snaps onto the target once `|target − d| < 0.001`.
- **The DSP reads the gliding double.** In LYNX's port this read is linear interpolation (`FruityDelay.cpp`).
- **The glide coefficient** comes from Time smoothing: `k = 1 − 0.01^{1/(T·rate)}`, with T in seconds
  (REA_rodada52 §3.2).
- **FL's default preset** has Time smoothing at raw 500 of 1000, which is T = 0.5 s, and Keep pitch at 0. So by
  default FL glides in about 0.5 s, like a tape.
- **Keep pitch on.** A change computes the other tap set and crossfades the two linearly over `0.1 · rate` samples.
- **Tempo changes take the same path.** The wrapper's tempo event sets the recompute flag (`+0x2b8`). REA_rodada52
  §5 verified LYNX's engine against the DLL with a tempo change of 128 → 97 BPM mid-render: 30 of 30 states
  bit-identical.

FL's own delay therefore never jumps its read position on a tempo change. By default it glides.

### 4.2 Fruity Delay 2: the line is rebuilt [DECOMP]

`Fruity Delay 2_x64.dll`, sha256 `d9a7674d…a6f93d`.

- `0x006bfea0` recomputes the delay from the tempo: `round(time · rate · 0.3125 / bpm)`.
- `0x006bb1f0` compares the new size with the old one. If they differ, `0x006bb190` sets the line's length again
  (Delphi SetLength), calls the line's virtual reset, and puts the read pointer at the start and the write pointer one
  delay ahead.
- So FL's Delay 2 restarts its line whenever a tempo change alters the length by a sample.
- LYNX's exact port (`Delay2Engine::set`) does the same, which removes the repeats while the tempo moves rather than
  clicking (Δ⁴ −110 dBFS on the ramp; §5).
- **Unchanged:** it is FL's behaviour.
- **Still open from REA_rodada52 §6:** whether the virtual reset zeroes the samples. Either way, the read position
  restarts.

### 4.3 FL Studio Mobile's Trance Delay

REA_rodada182c (F5, round 188h) documents FLM's rings, which are as long as the delay. It also documents that a Time
or Mode change clears them. **No evidence covers a tempo change.**

With LYNX's per-block tempo steps, the rings were re-cut at every block. This was the loudest case of the survey: Δ⁴
peaks of −3 … +5 dBFS.

Since FLM gives no evidence here, LYNX picks the cleanest behaviour that adds no artefacts: FL's Fruity Delay 3 glide
from §4.1.

### 4.4 Nebula (LYNX's, in Valhalla Supermassive's role)

The reference is black-box and its tempo-change behaviour was not measured. LYNX's two engines already glide their line
lengths at a fixed 0.25 sample per sample.

Under per-block tempo steps that became a burst and then a stop at every block: a buzz at the block rate. The cloud
network's moving all-pass readers made it worse, and the classic engine's loop all-passes also jumped once per block.
Δ⁴ peaks were −31 … −47 dBFS against −87 … −111 at a steady tempo.

The cleanest behaviour that adds no artefacts is to let the lines move at the tempo's own pace.

## 5. Survey of every built-in effect [EXEC]

`bug190ie_survey_tempo_ramp_all_effects` runs with `LYNX_SURVEY_190IE=1`. It covers 150 rows: each built-in effect
with its default settings, plus a second row with its tempo sync on where the effect has such a control. Each row
renders at a steady 120 BPM, a steady 140 BPM and a ramp 120 → 140, at 48 kHz in blocks of 256. The logs are in
`/mnt/e/lynx-work/bug190ie/logs/survey2-before.log` and `survey3-after.log`.

24 rows depend on the tempo. Before this round, the ones that clicked were:

| Effect | Δ⁴ energy, ramp vs steady | Δ⁴ peak on the ramp (steady) | Fix |
|---|---|---|---|
| Delay (default) | +71.0 dB | −22.5 dBFS (−109.5) | glide (§6.1) |
| Pocket Trance Delay (default) | +78.0 dB | −2.6 dBFS (−108.8) | glide (§6.2) |
| Nebula (Sync on) | +41.3 dB | −41.8 dBFS (−87.1) | spread (§6.3) |
| Lattice Mod (Sync on) | +13.3 dB | −80.3 dBFS (−88.9) | none: already glides over its Smoothing time (round 173d) |
| Spiral Echo (Sync on) | +1.5 dB | −99.2 dBFS (−106.0) | none: already glides (0.05 / 0.175 s) |

The rest of the tempo-dependent rows have no clicks.

- Fruity Delay 3 (+6.0 dB) is FL's glide, which bends the pitch on purpose.
- Fruity Delay 2, Fruity Delay Bank and Fruity Delay drop or restart their line, as FL does (§4.2).
- Echoform, Horizon, LuxeVerb, Vintage Phaser, Auto-pan, the Volume gate and the Pocket modulation effects are within
  a few dB of their steady renders.

## 6. What LYNX does now

### 6.1 Basic Delay (`core/Effects.cpp`)

- The first block, and any move of the Time knob, take the new length at once, as before.
- A tempo change glides with FL's Delay 3 default: `k = 1 − 0.01^{1/(0.5·rate)}`, snapped within 0.001 sample, read
  with linear interpolation.
- Once the length lands, the loop is the old whole-sample loop.

### 6.2 Pocket Trance Delay (`core/PocketFx.cpp`)

- The rings as long as the delay became rings of the largest length, read D samples behind the write position. At a
  steady tempo these are the same samples. FLM's Mirror pre-delay ring of D reads D − 1 samples late, and is kept that
  way.
- Time and Mode changes still clear the lines, as FLM does.
- A tempo change glides as in §6.1.

### 6.3 Nebula (`core/LynxVerbs.cpp`, `core/NebulaCloud.cpp`)

A block counts as a tempo change when Sync is on, the tempo moved, and Sync, Note and Delay did not. In such a block,
each line's change spreads evenly over the block instead of the fixed 0.25-sample glide. The classic engine's loop
all-passes follow their gliding lines.

Every other change keeps the fixed glide, through the new `cloud::Controls::spread` and `verbs::Settings::lengthSpread`
fields (0 = as before).

## 7. Proof [EXEC]

### 7.1 At a steady tempo nothing changes

- **All 150 survey rows:** the steady 120 BPM render's hash is identical between the base (origin/main `baaa2dae7`)
  and the fix.
- **Basic Delay:** bit-identical against a verbatim copy of 0.1.28's loop (`OldDelay`), over 4 settings × rates ×
  tempos × blocks.
- **Pocket Trance Delay:** bit-identical against a verbatim copy of 0.1.28's loop (`OldTranceDelay`), over 3 modes ×
  4 times × 3 rates/tempos/blocks.
- **Nebula's parity bank** (tp172d, Valhalla Supermassive, 15 renders): the base and fix renders are identical file
  for file. Its score stays at mean 6.3, min 4.

### 7.2 Under the ramp (Δ⁴ peak; energy vs steady)

| Effect | 0.1.28 | Now |
|---|---|---|
| Delay, 3 settings × 2 rates × blocks 64 / 256 / 512 | −13 … −35 dBFS; +60 … +81 dB | −91 … −106 dBFS; −0.5 … +2.6 dB |
| Pocket Trance Delay, Stereo / Ping-pong / Mirror, Feedback 0.7 | +3 … +5 dBFS; +79 … +83 dB | −79 … −94 dBFS; +2 … +12 dB |
| Nebula synced, cloud and classic engines, Mod depth 0.5 / 0 | −31 … −47 dBFS; +41 … +70 dB | −67 … −110 dBFS; −2 … +25 dB |

A tempo step of 120 → 140 lands the Delay exactly on the new length: an impulse 2 s later echoes 15 428 samples later,
the same sample as a delay that ran at 140 from the start.

### 7.3 CPU

Thread time, best of 7, 30 s at 48 kHz, blocks of 512. The bench is `/mnt/e/lynx-work/bug190ie/mini/bench.inc`, with
logs in `logs/bench*.log`.

| Effect | Steady tempo | During a tempo ramp |
|---|---|---|
| Delay | same, 0.036 % | 0.038 → 0.07 % (interpolated read while gliding) |
| Pocket Trance Delay | same, 0.03 % | 0.03 → 0.08 % |
| Nebula, synced | same: cloud 1.1 %, classic 2.1 % | within noise |

## 8. Open

- **Fruity Delay 2:** whether FL's line reset zeroes the samples (§4.2). LYNX clears them, which makes no difference at a
  steady tempo. A live FL check with a Tempo clip would settle what FL plays while the tempo moves.
- **Pocket Trance Delay:** FLM's behaviour on a tempo change has no evidence (§4.3). LYNX glides.

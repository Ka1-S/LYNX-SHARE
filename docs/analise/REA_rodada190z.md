# REA round 190z — what FL's FPC plays per pad and layer (issue #135)

Target: `FL Studio 2026/Plugins/Fruity/Generators/FPC/FPC_x64.dll` (Ghidra project r69 `fpc`, read-only headless
decompiles in `/mnt/e/lynx-work/fpc190z/rea/`: `trig.c`, `range.c`, `env.c`, `lib.c`, `lib2.c`, `ed.c`), checked against
FL renders of two projects made for this round (`/mnt/e/lynx-work/fpc190z/flp/z1_env.flp`, `z2_layers.flp`: FL's render
test `FPC.flp` with the FPC state and the notes replaced by `p/mk.py`; sine / tone samples in
`C:\Users\kauan\lynx-test\r190z`). Clean-room: behaviour and formulas only. Completes REA_rodada9 §1.3–§1.6.

## 1. The voice (FUN_0086b480 trigger, FUN_0086a2d0 render) [DECOMP]

- A host voice holds pad groups (one per triggered pad) holding layer voices. The velocity index is
  `min(127, max(0, round(velocity · 128)))`.
- A layer plays when `low ≤ v < high` (or `v = 127` and `high = 127`) and it has a sample. With pad flag bit 3 ("one
  at a time") and at least two layers, FUN_00869db0 picks one (§3); otherwise every layer that holds the velocity plays
  together.
- Each layer voice keeps a tick counter (`P+0x120` samples, §2) started at −tick, so the first render call is a tick.
  On a tick: the volume envelope's current level is taken (`+0xa3`) and the envelope stepped once; when it reaches its
  end (stage 9) the voice is marked finished (unless a running pan envelope still plays). The pan envelope's level × 2
  is taken and stepped; pan = clamp(2·pan env + layer pan / 128, −1, 1).
- Gains: `ComputeLRVol` (host, project pan law) three times — (pad pan, pad level), (pan, layer level), (the host's
  FinalLevels pan and volume) — and L = host·pad·layer·volume envelope (R alike). A layer without bit 0 ("Scale volume
  with velocity") divides both by InitLevels.Vol (the velocity's level). A choked pad group or a finished voice takes
  the target 0. The gains ramp linearly from the current value to the target over the tick; the very first tick jumps.
  The voice is removed once both gains are 0, or when the sample ends.
- Pitch = host pitch + (60 − note)·100 + (layer tune + pad tune)·9.375 cents (REA_rodada9). FCut / FRes are not read.
- Choke: a new pad with cut group g flags every playing pad group whose "cut by" is g; groups made in the current block
  (`+0x31`) are spared. A flagged voice goes to 0 over its next tick. (Not the host's declicker.)
- Note off (FUN_008700f0): every layer voice of that host voice releases its volume and pan envelope, only if enabled.
  Without an enabled envelope a note off changes nothing (one-shot).

## 2. Envelopes [DECOMP + EXEC]

FPC's envelope classes are the articulator library (TMEnvelope; the same code Love Philter and Slicex carry,
`core/FlCurves.inc`): constructor FUN_0083c240 (time scale 1, capability byte `(b & 0xEC) | 0x4C`: decay point kept,
loop / sustain kept, arpeggio bytes cleared), volume reset FUN_0083c330 (FL's default 4 points, decay 1, sustain 2),
pan reset FUN_0083c3f0 (offset 0.5, one point 0.5). Stored record (chunk 0x68): `kind 1, version 3, n`, n × (f64 time,
f64 level, f32 tension, u8 mode, 3 bytes), then **flags, enabled, decay, loop, sustain** (FUN_00782e20 reads these
20 bytes) and the four knobs **ATT, DEC, SUS, REL** (FUN_0078aec0, bit 3 of the capability byte). So the "active"
switch of REA_rodada9 §1.6 is the second tail value (FL writes −1 for on).

- **Time**: FUN_00873060(48): x = 48·rate/44100, tick = max(4, 4·round(x/4)) samples (48 at 44.1 kHz, 52 at 48 kHz),
  time scale per tick = 2·tick/rate (FUN_00872ff0), so a point's time unit is **0.5 s**; no tempo mode is wired.
  EXEC: the kits' decay (1 → 0 in 1.25) ends at 0.625 s in FL's render.
- **Tension / modes**: the library's shapes (FUN_00789090 is FlCurves' evaluation; modes 0–12).
- **Knobs**: value/128 (FUN_0078af90). ATT scales the segments up to the decay point, DEC after it, REL the segments
  after the sustain point once released (DEC there when there is no sustain point). SUS lifts every level after the
  decay point by its weight (clipped 0…1) — from the start when there is a sustain point but no decay point
  (FUN_0078b0e0).
- **D / S**: the decay point ends the ATT part; the sustain point holds while the note is held (with a loop start,
  the loop repeats); on release the level leaves from where it is (the lift is re-based, FUN_00788ed0) into the
  segments after S.
- **Volume** level is the gain; **pan** level is read around 0.5.

EXEC (z1_env, FL /R 16-bit, per-sample comparison of `h/envsim.cpp` = `core/FpcEnvelope` + the tick/ramp model):

| Pad | Envelope | ESR | worst |
|---|---|---|---|
| 36 | kits' decay 1 → 0, 1.25, t 0.2 | −76.1 dB | 0.50 LSB |
| 38 | FL default ADSR, held 3 s | −79.3 dB | 0.51 LSB |
| 40 | same, ATT 64 DEC 192 SUS 64 REL 256 | −83.6 dB | 0.53 LSB |
| 41 | tensions −0.6 / double curve 0.7 / mode 7 | −78.2 dB | 0.51 LSB |
| 43 | pan envelope centre → left → right | −85.9 dB | 1.31 LSB |
| 45 | sustain without decay point, SUS 64 (left side) | −82.9 dB | 0.50 LSB |
| 46 | two overlapping hits, each its own envelope | −79.2 dB | 0.50 LSB |
| 47 | loop 1…3, released mid-loop | −80.1 dB | 0.52 LSB |

EXEC (z2_layers, model `p/z2sim.py`, `p/z2choke.py`, `p/z2tones.py`):

| Pad | What | Result |
|---|---|---|
| 36 | layer pan −64 | −84.2 dB, 0.51 LSB (constant-power law, centre 1 × FPC's 1/(2√2)) |
| 38 | pad pan −40 × layer pan +128 | −87.4 dB, 0.51 LSB (the two laws multiply) |
| 40 | pan envelope 0.5 → 0 + layer pan 50 | −91.5 dB, 0.52 LSB |
| 41 / 43 | velocity 50, Scale volume off / on | −94.6 / −92.5 dB (off = the level of velocity 100) |
| 45 | bit 3 off, three overlapping layers | all three sound |
| 46 | Cycle backward | 550, 330, 220, 550, 330, 220 (last layer first) |
| 47 | Cycle, ranges 0–64 / 32–100 / 64–127 / 100–127 | every pick as §3 (a previous pick outside the range restarts at the first) |
| 48 | Random, 30 hits | 9 repeats (≈ 1/3) |
| 49 | Random (avoid previous), 30 hits | 0 repeats |
| 50 / 51 | choke | one-tick linear fade from the cut voice's next tick: 0.51 LSB (from the cut sample: −33 dB) |

## 3. Layer modes (FUN_00869db0) [DECOMP]

Among the layers holding the velocity, with `prev` = the position of the last pick in that list (−1 if absent):
0 Random = trunc(u · 2⁻³² · count); 1 Random (avoid previous) = trunc(u · 2⁻³² · (count − 1)), +1 when ≥ prev and
0 ≤ prev < count − 1 (with no previous, the last layer is never drawn); 2 Cycle backward = prev − 1, wrapping to the
last; 3 Cycle = prev + 1, wrapping to 0. A single layer is always picked. The pick is remembered per pad.

## 4. Pad / layer flags [DECOMP]

- Layer bit 0 = **Scale volume with velocity**: the editor's third pad switch shows the selected layer's bit and sets
  it on every layer of the pad (FUN_0085e290; control `+0x738`). Pad flag bit 2 (`pad+0x26`) is read only by the
  editor's layer-range code (FUN_0085e320 and the layer move / delete handlers reset it) — no effect on the sound.
- Layer bit 1 = **Reverse**: the editor's reverse handler sets it and reloads the layer's sample through the sample
  cache (reversed data, played from its start). EXEC: z2 pad 52 = the reversed file to 0.51 LSB.
- Layer region (`+0x44`): start / end from the WAV's region list (not played by LYNX yet).

## 5. Gaps found and what LYNX does now (round 190z)

| FL plays | LYNX before | LYNX now |
|---|---|---|
| volume / pan envelopes (switch, ticks, tension, D/S, knobs, release) | ignored | `core/FpcEnvelope` + per-voice shaper |
| layer pan | ignored | constant-power law × the pad's (EXEC: pad 43's pan to 1.31 LSB) |
| Scale volume with velocity off | velocity always applied | velocity's level divided out |
| all layers that hold the velocity (bit 3 off) | one, taking turns | all |
| Random, Random (avoid previous), Cycle backward | Cycle only | all four |
| choke: one tick linear | channel declicker (10 ms raised cosine) | one tick |
| reversed layer | forward | reversed |
| no voice limit per pad | 16 per pad | 16 per layer |
| region index, Lock layers / Test pad range (editor only) | — | not played (region) |

# Round 167h (v0.1.22) — REA gap-hunt part 2: the Sampler's channel events and FL's declicking

Continues round 167g (`REA_rodada167g.md`, `REA_rodada167g-gaps.md`). Clean-room: Ghidra on FL Studio 2026's own
`FLEngine_x64.dll` (the 167g read-only project `/mnt/e/lynx-work/r167g`, headless scripts in `tmp/r167h/scripts`),
Delphi RTTI tables read with `tmp/r167h/py/mtab.py` (method tables: handler name → address) and `fld.py` (published
field tables: form field offset → control name), FL's own demo projects [DATA] (`tmp/r167h/py/chsurvey.py`). Behaviour
and formats only; nothing of FL's code is copied.

Markers: **[DECOMP addr]** Ghidra pseudocode, **[RTTI]** a Delphi method / field table, **[DFM]** a form resource
string, **[DATA]** the 165 projects in FL's Data folder, **[FL render]** FL's command-line render, **[TEST]** LYNX tests.

## 1. Which Sampler control each channel event is

The channel settings form (TChanForm) holds its channel in field +0x12b4. Its event handlers [RTTI method table at
0xf57e7c…0xf587d5] copy each control's value into the channel object, then call the precompute FUN_010001c0 →
FUN_00ff4920 → **FUN_00ff0db0**. The published field table at 0xb556ed [RTTI] names the controls:

| Control (FL caption) | Form field [RTTI] | Handler [RTTI → DECOMP] | Channel field | .flp event (saver FUN_00f3e190 / loader FUN_00f46490 [DECOMP]) |
|---|---|---|---|---|
| Remove DC offset (`CenterDCBox`) | 0x890 | ReverseSampleBoxClick 0xf66bf0 | +0x28d | 0xD7 byte 0x09 |
| Normalize (`NormalizeBox`) | 0x898 | (same) | +0x2d4 | 0xD7 byte 0x50 |
| Reverse polarity (`FlipYBox`) | 0x8a0 | (same) | +0x2d5 | 0xD7 byte 0x51 |
| Fade stereo (`FadeBox`) | 0x8a8 | (same) | +0x25d | 0x46 bit 0 |
| Reverse (`ReverseSampleBox`) | 0x8b0 | (same) | +0x25e | 0x46 bit 1 |
| Swap stereo (`ReverseStereoBox`) | 0x8b8 | (same) | +0x25c | 0x46 bit 8 (×256) |
| Fade in (`FXAttackWheel`) | 0x8c0 | FXAttackWheelChange 0xf66590 | +0x244 | 0x4C (u16) |
| Fade out (`FXDecayWheel`) | 0x8c8 | (same) | +0x240 | 0x4B (u16) |
| Crossfade (`FXSmoothLoopWheel`) | 0x8e0 | (same) | +0x2d8 | 0xD7 i32 at 0x54 |
| Trim (`FXTrimWheel`) | 0x8f0 | (same) | +0x2dc | 0xD7 i32 at 0x58 |
| Declicking mode (`DeclickModeSelect`) | 0x1050 | DeclickModeSelectChange 0xf69150 | +0x2d7 | 0xD7 byte 0x53 |
| Use loop points (`LoopBox`) | 0x830 | LoopBoxClick 0xf67f70 ("enable/disable loop points") | sample +0x14d | 0x8F bit 3 (old files: 0x13) |
| Ping pong loop (`PingPongBox`) | 0x838 | PingPongBoxClick 0xf68de0 ("enable/disable ping pong loop") | sample +0x14c | 0x14 (u8) |
| Resample (`ResampleBox`) / Keep on disk / Load regions / Load ACID markers | 0x800 / 0x7f8 / 0x808 / 0x810 | KeepOnDiskBoxClick 0xf6cd10 | sample +0xbd / +0xf8 / +0xf9 / +0xfa | 0x8F bits 0 / 8 / 1 / 2 (0x29 = the old Load regions) |
| Legacy panel (`EffectsTargetSelect` = 1 shows `FXLegacyPanel`): Boost `FXPreampWheel`, `FXWheel`, ring mod `FXSineVolWheel` / `FXSineWheel`, low-pass `FXCutWheel` / `FXResWheel`, stereo delay `FXStDelWheel`, reverb `FXReverbWheel` + `RevTypeSelect`, `FX3Wheel`, clip `FXPreampClipCheckBox` | 0xf80 … 0x1018 | FXPreampWheelChange 0xf66710, FXWheelChange 0xf6f6c0, FXSineVolWheelChange 0xf6f3f0, FXSineWheelChange 0xf6f460, FXCutWheelChange 0xf666a0, FXResWheelChange 0xf6f8b0, FXStDelWheelChange 0xf6f4d0, FXReverbWheelRelease 0xf6f950, RevTypeSelectChange 0xf66cf0, FX3WheelChange 0xf67cc0, FXPreampClipCheckBoxClick 0xf5ba60 | +0x23c, +0x228, +0x22c / +0x230, +0x234 / +0x238, +0x248, +0x250 / +0x254, +0x24c, +0x25f | 0x4A, 0x45, 0x83 (lo / hi 16 bits), 0x47 / 0x53, 0x55, 0x8B (lo / hi), 0x56, 0x46 bit 2 |

Cross-check: the same 0xD7 record already gave LYNX "main pitch" at byte 0x0B and "add to key" at 0x3F; the form's
`PitchableBox` (0x908) and `AddToKeyBox` (0x920) handler PitchableBoxClick 0xf67bc0 writes +0x28f and +0x2c3 =
0xD7 base +0x284 plus exactly those bytes [DECOMP], so the 0xD7 base is right.

Other channel events: **0x20** = channel +0x892 bit 0 (saved only by the full-project saver) — not traced to a
control yet; **0x30** = +0x224 (0 in every demo); **0x32** = +0x1fc (1 in 1 171 channels); **0xAA** = +0x714, a count
of target links (strings 0xF5…0xFF follow when > 0; −1 / 0 in the demos); **0x33** = bits (0 in every demo);
**0x9B** does not occur in the demos.

How often the demos use them [DATA, 3 442 channels]: Reverse on 89 channels, Normalize on 134, Fade out on 140, Fade
in on 48, Trim on 61, Crossfade on 10, declicking mode ≠ 0 on 700, Use loop points on 1 400, Swap stereo 4, Remove DC 5,
Reverse polarity 2, Fade stereo 2, ping pong 1; the legacy knobs ≤ 20 channels each.
LYNX before: **none of them was imported** — those 89 reversed samples played forward, normalized ones at their file
level, faded ones without the fade, every sample with a loop in its file looped even with "Use loop points" off.

## 2. FL's precompute (FUN_00ff0db0 [DECOMP]), in its order

N = the sample's frames, per channel c, i = 0 … N−1, values as floats (the effects step converts to float first).

1. **Region crop**: s0 = round(start·(N−1)), s1 = s0 + round(end·(1−start)·(N−1)) (start / end = 0xD7 f64 at 0x80 /
   0x88 — the end is a fraction of what follows the start); a region shorter than 41 frames is widened by 41. The
   sample is cut to [s0, s1]; then, when start > 0, a declick-in of the new start, when end < 1 a declick-out of the
   new end: FUN_00699ca0 → FUN_006a09b0 with factor 0.1 → length round(rate·0.005·0.1) in / round(rate·0.04·0.1) out
   (constants 0.005 / 0.04 at 0x13ec8fc) → FUN_006a0760 (the "declick" ramp, §3).
2. Resample to the project rate when "Resample" is on, time-stretch (FUN_00ff0640).
3. **Ping pong** (sample +0x14c with a loop [ls, le]): the loop's frames le−1 … ls+1 are appended reversed after le and
   the loop end moves to le + (le − ls − 1).
4. Legacy stereo delay (+0x248 ≠ 2048).
5. **Reverse** (+0x25e): the whole (cropped) sample reversed (FUN_00698420).
6. When any effect below is on (DC, normalize, polarity, fade stereo, low-pass, ring mod, FX ≠ 128, FX3 ≠ 256, boost,
   in, out, crossfade, clip), per channel and frame: (legacy FX3 warp, ring mod, low-pass), then
   - **Fade in** (+0x244 = a, 0…1024): gain = 1 − (N−1−i)·a / (256·(N−1)), ≤ 0 → 0 (a ramp that reaches 1 at the
     sample's end; a = 256 is a ramp over the whole sample, 1024 over its last quarter);
   - **Fade out** (+0x240 = b): gain = 1 − i·b / (256·(N−1)), ≤ 0 → 0;
   - boost ×(1 + v/16), clip to ±1;
   then legacy FX (one-pole feedback), **Fade stereo**: (converted to stereo) left × cos(i·π/2/(N−1)), right ×
   cos((N−1−i)·π/2/(N−1)) — cos is FUN_0040bba0 (quadrant table: cos kernel / −sin / −cos / sin);
   **Remove DC** (FUN_006971f0): each channel minus its own mean; **Reverse polarity** (FUN_00698860): −x;
   **Normalize** (FUN_00697510 with 1.0): × 1 / peak, peak = the largest |x| over both channels (FUN_00692980);
   **Crossfade** (+0x2d8 = x with a loop [ls, le]): n = (le − ls + 1)·x / 2048 frames; for k < n:
   y[le−n+1+k] = y[le−n+1+k]·(1 − k/n) + y[ls+k]·k/n, then the loop start moves to ls + n (with no loop and x > 0
   the whole sample becomes the loop first).
7. **Swap stereo** (+0x25c): left ↔ right (FUN_00698a40).
8. Legacy reverb (+0x250).
9. **Trim** (+0x2dc = t): threshold t / 32768; the first frame whose max |L|, |R| ≥ threshold (searching up to the
   loop start, or the end) and the last one (down to the loop end, or that first frame) — the sample is cut to them.

## 3. Declicking modes and "Fast declick for cut groups" (0x28)

FL's sample-rate setup FUN_012c6ae0 [DECOMP] builds 7 declickers (FUN_00977f50: attack shape + ms, release shape + ms)
in the array at 0x16ba078: [0] none / 10 ms, [1] 4 ms / 10 ms "no bleeding" type, [2] 4 ms / 20 ms, [3] 20 / 20 ms,
[4] 100 / 100 ms, [5] 200 / 200 ms (curve 5), **[6] none / 2 ms**. A sampler voice takes declicker
`[channel +0x2d7]` (= the declicking mode, 0xD7 byte 0x53) when it starts (FUN_00d277d0 / FUN_00d24400 [DECOMP]).
The voice-cut methods FUN_00d24160 / FUN_00d241a0 [DECOMP]: when the cut flag is set and
`*(*(*PTR_015e4790 + 0x10) + 0x18)` is on, the voice's declicker becomes **[6]**, the 2 ms one. That flag is the
loader's 0x28 (`> 0`, also sets the Settings form's field 0xf00) [DECOMP FUN_00f46490] and FastDeclickBoxClick
0x1358d30 [RTTI → DECOMP] writes it from `FastDeclickBox`, whose DFM entry is
`forms.options.projgeneralpage.projadvpanel.fastdeclickbox`, caption "Fast declick for cut groups", hint "Use faster
ramping when cutting notes" [DFM] — Options › Project › General, the advanced panel next to the pan law.
Release shape 3 (FUN_00977870 [DECOMP]): a raised cosine over n = max(1, round(ms·0.001·rate)) frames,
g = (1 + cos(k·π/(n+1))) / 2.

## Where I stopped

- Done: REA of §1–§3 (decompilations in `tmp/r167h/h1n.c`, `pc2.c`, `pc3.c`, `pc4.c`, `dc1.c`, `dc2.c`, `voice.c`;
  tables `tmp/r167h/chanform-methods.txt`, `chanform-fields.txt`).
- Next: implement the Sampler's precomputed effects + loop flags import / engine / UI, the fast declick option; one FL
  render session to verify (both locks); then the Options-dialog gap table (part 2 of the brief).
- Rerun: `tmp/r167h/g.sh <out.c> 0 <hex addr…>` (headless decompile, Ghidra lock), `SCRIPT=Refs.java tmp/r167h/g.sh
  <out> <addr>`, `python3 tmp/r167h/py/mtab.py <HandlerName>`, `python3 tmp/r167h/py/chsurvey.py "<FL Data>"`.

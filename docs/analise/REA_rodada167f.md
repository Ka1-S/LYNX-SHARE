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

(Detailed per-stage spec below, in progress.)

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

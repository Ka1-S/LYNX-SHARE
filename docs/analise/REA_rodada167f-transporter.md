# REA round 167f, sub-round flgap167-tp — Transporter (FL's transient looper)

Evidence markers: [DECOMP addr] disassembly / Ghidra of the installation's
`Plugins\Fruity\Effects\Transporter\Transporter_x64.dll` (the Delphi front end, SHA-256 `12d1116e…aadda`) and
`Shared\ildsp_x64.dll` (the engine library, SHA-256 `b96354e1…1760`, the same file rounds 54/60/154/165g analysed);
[EXEC] small Windows harnesses run under WSL interop: `tlh.exe` loads `ildsp_x64.dll` and drives the Transporter engine
through its own vtable, `fxt.exe` loads the whole plug-in DLL as an FL host would (state restore, sample rate,
512-frame Render calls) and logs every value the front end sends the engine; [DATA] tables / constants read from the
DLLs; [TEST] LYNX's tests; [HYP] not proven.

Method note (clean-room): the code is read for behaviour only (structure, constants, parameter laws, routing). This
document states that behaviour in our own words; `src/core/TransporterFl.cpp` implements it from this document. No
Image-Line code, art or data is in the repository. LYNX's output is compared to FL's DLLs run as a black box.

## 1. What the plug-in is
FL's "Transporter" is a transient-triggered looper. A detector finds transients in a side signal; every transient
(or a beat of the song's grid, or an Update button) restarts two loopers, A and B, that replay recent audio (the
segment since the previous trigger, or a set length), possibly reversed or an octave up / down. The loops are mixed
with the dry signal and also go out as a stereo send (left = loop A, right = loop B). [DECOMP ildsp 0x18018c4e0]
The engine is a Faust program (its compile string names the class `TransientLooperFaust`) wrapped by a small C++
class (`TransientLooperImpl`) in ildsp. [DATA ildsp strings]

## 2. Front end (Transporter_x64.dll)

### 2.1 State (restore 0x713760) [DECOMP]
- i32 version. Version > 8: the state is ignored.
- Controls: 22 × i32 if version ≥ 8, else 20 × i32 (then control 21, "ignore probability", is set to 0).
- u8 flush-when-disabled (kept in the plug-in, used by the SetEnabled dispatcher case), u8 look-ahead (read, unused).
- i32 Source (0 left, 1 right, 2 left + right), i32 Stereo (0 mono, 1 stereo), i32 send track, i32 input track
  (versions ≥ 8 for the tracks; v3 states end after the first u8 — FL then reads Source / Stereo from
  uninitialised locals, LYNX uses the defaults 2 / 1, which is what the EXEC run gave).
- After the restore every control is re-applied through the parameter path (control 12 Flush only when it changed).
- FL's "Default" preset: 12800 12800 12800 0 0 0 512 500 8 512 512 0 0 0 0 0 0 0 0 0 21 0, Source 2, Stereo 1. [DATA preset]

### 2.2 The 22 controls: raw range and the value sent to the engine [DECOMP + EXEC]
norm = (raw − min) / (max − min), computed in double, stored as float.

| # | control | raw range | engine value |
|---|---|---|---|
| 0–2 | dry level, loop A level, loop B level | 0…12800 | dB: 0 at the top; else c = (e^(norm·2.3978953) − 1)·0.1 (exp in double), dB = 20·log10(c), −∞ at 0 |
| 3, 4, 5 | reverse, octave-shift, down/up probability | 0…10000 | norm |
| 6 | spread / auto-pan | 0…1024 | (norm − 0.5)·2 (negative = auto-pan depth) |
| 7 | minimum length A | 20…2000 | norm·1.98 + 0.02 (seconds) |
| 8 | A/B ratio | 0…36 | norm·9 + 1 |
| 9 | transient sensitivity | 0…1024 | norm |
| 10 | bouncing rate | 0…1024 | (norm − 0.5)·2 |
| 11, 12, 14–18 | freeze, flush, bypass, update A, update B, update A/B, deterministic | 0/1 | raw |
| 13 | loop mode Auto / Bouncing / Adaptive / Tempo | 0…3 | raw |
| 19 | tempo length | 0…20 | raw (note index) |
| 20 | tempo trigger | 0…21 | raw (21 = off) |
| 21 | ignore probability | 0…10000 | norm |

Logged values (fxt, float bits): level 11000 → −3.28441858 (0xc05233ea), 5362 → −15.2364492 (0xc173c87f), 5675 →
0xc1672269, 1 → 0xc2bd17f4, 12799 → 0xbaea9c6e; probability 4969 → 0.496899992 (0x3efe69ad); spread 499 →
−0.025390625; length 2000 → 2, 20 → 0x3ca3d70a; ratio 12 → 4; sensitivity 1024 → 1. Full sweeps of every raw value
of controls 0, 3, 6, 7, 8, 9 matched the formulas bit for bit (12801 values each). [EXEC]

### 2.3 Render (0x714700) and dispatcher (0x712ee0) [DECOMP]
- Per Render call (FL's block, 512 frames in the EXEC host): the beat position = the host's song position in ticks
  (an integer) × 1/PPQ, then frac(beats / 4)·4 — the beat within a 4-beat bar, constant for the whole block, given
  to the engine as a per-sample input.
- Detector input: the input track's buffer (state's input track, via the host's GetInBuffer) when one is set, else
  the plug-in's own input.
- If the send track is > 0, the engine's two send channels are added into that track's buffer (GetOutBuffer).
- Sample-rate change: the engine is prepared only once (first time). The latency reported to PDC is the engine's
  look-ahead: trunc(rate · 0.005) (220 at 44.1 kHz).
- Flush (dispatcher case 2) and SetEnabled(off) when flush-when-disabled is set: the engine's state is cleared and
  control 12 (Flush) is set to 1.
- Tempo: the host's tempo goes to engine index 22. Source → index 24, Stereo → 25, look-ahead (always 1) → 23.

## 3. Engine (ildsp) [DECOMP 0x18018fc50 setParam, 0x180192070 wrapper, 0x18018afd0 constants, 0x18018b5d0 clear, 0x18018c4e0 compute]

### 3.1 Wrapper
- Processes in chunks of at most 16384 frames. A flush request fades the output: fade = (fade − target)·k + target
  per sample, k = exp(−30π / fs), target 0 while a flush is pending (1 when bypassed); output = fade² · program output.
  When the fade is below 1e−7 the loop buffers are cleared, the program's "flush" input is pulsed (the write position
  restarts) and the request ends.
- Tempo mode (13 = 3): the program runs in Bouncing mode with bouncing rate 0, and minimum length A =
  240 / tempo × noteValue[tempo length]. Tempo trigger: multiplier = 0.25 / noteValue[trigger], 0 for "off".
  Note values (22 entries) [DATA 0x1803dd5b0]: 1, 1/2 … 1/32; dotted ×1.5; triplets ×2/3; quintuplets ×4/5 (as
  1/5 … 1/80); 0.
- Constructor defaults: tempo 60.

### 3.2 Constants (per sample rate, fs clamped 1…192000)
- One-pole smoothers by tan: k = t/(t+1) with t = tan(31.415926/fs) (5 Hz, bypass) or
  t = tan(62.831852/fs) (10 Hz) for the gain / pan / threshold smoothers (two-step "zero-delay" form: value = s + k·(x − s),
  state = s + 2k·(x − s)).
- Dry delay (look-ahead) = fs · 0.005 samples (when look-ahead is on); the ring buffers are 1024 long.
- Loop buffers: 20 s (fs · 20), allocated for 192 kHz.
- Trigger hold = fs · 0.02 − 1 samples; minimum gap of the meter's peak = fs · 0.01.
- Band split: Linkwitz-Riley crossovers made of state-variable filters with tan(ω/fs) for ω = 149.44017,
  355.43063, 845.36127, 2010.6193, 4782.0854, 11373.78, 27051.56 (≈ 24, 57, 135, 320, 761, 1810, 4305 Hz), damping
  √2, the high branch = x − 2√2·bp − low.
- Envelope followers: attack k = 1/(14444.787/fs + 1), release k = 1/(288.89572/fs + 1), ratio step 14.444787/fs.
- Sine table: 65536 entries sin(k · 2π/65536) (class init, float).

### 3.3 Per sample
1. Bypass cross-fade (5 Hz smoother, squared); dry signal delayed by the look-ahead; dry gain 10^(dB/20) smoothed (10 Hz).
2. Write position pos = (previous + 1) mod (fs·20) (mod 1 while a flush pulse is active).
3. Detector signal: left, right or (left + right)·√½ by Source; split into 8 bands. For each band: energy x²
   through four one-poles (attack when rising, release when falling) → slow level; ratio r = √slow / previous fast
   magnitude (min 1e−3); a second four-stage follower whose attack is fast (8388608·ratio step) when ln r is above the
   threshold, else slow (20·ratio step); magnitude m = √fast; onset when ln(max(m, 1e−3)) rose by more than 0.5 since
   the previous sample. Threshold = 1.5 + (1 − sensitivity)²·6.5, smoothed (10 Hz).
4. Trigger = (number of onset bands) AND (hold counter ≥ hold) — a bitwise and, so only an odd number of
   simultaneous band onsets fires (FL's quirk, kept). The hold counter restarts on a trigger.
5. Update buttons: rising edges of update A, B and A/B (A/B fires both). The trigger density (trigger or update)
   through four one-poles (k = 1/(1.8055984/fs + 1)) gives the Adaptive length A = min(|minLenA − 1/(fs·density)|, 2).
6. Beat grid: b = tempoMul · beat; when frac(b) wraps (decreases) in Bouncing / Tempo mode, the beat trigger is armed.
7. Random draws: a 32-bit LCG (×0x41c64e6d + 0xd3dbe645), 8 draws per sample, each mapped to int·2^−32 + 0.5.
   Deterministic mode replaces the draws by comparisons of fmod(pos, 2fs) with 2fs·probability.
8. Each looper (A uses draws 1/7/5/3, B 0/6/4/2 for ignore / reverse / octave / down-up):
   - length in samples: fs·lenA (B: fs·lenA/ratio) or the Adaptive length;
   - restart = (trigger after at least length − 1 samples since the last restart, unless frozen) OR the beat trigger
     (same length rule) OR the update edge, AND not ignored (a draw above the ignore probability);
   - on restart: the previous start becomes the segment start, the current position the new start; reverse and the
     octave (rate 0.5 or 2) are drawn;
   - Bouncing: the length restarts at fs·len and shrinks by e^(−rate/2) each time it has played once; Auto /
     Adaptive: length = (start − last start) wrapped to the 20 s buffer; minimum 48 samples;
   - phase = (phase + 1) mod length (0 at a restart); read = last start + rate·phase (reverse: start − rate·phase),
     wrapped; the input (mono mid (L+R)·√½, or L for A and R for B in stereo) is written at pos;
   - de-click: a four-stage envelope (k from the look-ahead time) closes for the look-ahead after every jump of the
     read position (phase reset, length change, or read within 2 samples of the write position);
   - loop gain 10^(dB/20) smoothed (10 Hz) × the envelope; the loop output goes through the look-ahead delay too.
9. Pan: spread ≥ 0 sets a fixed position; spread < 0 runs a sine (from the table) at up to 20 Hz (rate ∝ |spread|),
   smoothed. pan = (p·depth + 1)/2; A is panned to pan, B to 1 − pan.
10. Output L = (pan·A + dry·gain + (1 − pan)·B)·g + dry·(1 − g), R mirrored; g = the bypass fade. Sends: g·env_A·A, g·env_B·B.

## 4. LYNX implementation
- `src/core/TransporterFl.{h,cpp}` (namespace `lynx::transporter`): State parse (§2.1), controls → engine values
  (§2.2), beat from ticks (§2.3), `Engine` (§3) and the mixer effect `EffectKind::flTransporter` (25 parameters: the 22
  controls over their ranges, Source, Stereo, Send to insert). Uses `core/IldspMath` (the library's own expf / logf /
  tanf / powf / sinf, bit-exact) and FTZ/DAZ like the plug-in (MXCSR 0x9fe0).
- Effect: the engine is prepared on the first block after a sample-rate change; `reset()` = FL's flush; latency =
  trunc(rate·0.005); the side-chain input (mixer route / insert input) is the detector, else the effect's own input;
  the two sends go to the slot's send output (Song::fruitySendTarget: the Send-to parameter); the beat is computed per
  512-frame chunk from LYNX's song ticks and tempo.
- FL import (`effectFromFl "Transporter"`, FlpImport post-pass): the controls, Source, Stereo; the input track →
  the slot's side-chain source; the send track → the k-th route of the insert (report line "loop send routed to the
  insert's routes in order: unverified"); automation of the 22 controls (FlParamMap, flRawRange).
- Editor: generated layout (LEVELS / LOOP / BIAS / MODE sections, 640×262), i18n in all languages.

## 5. Verification
- Engine vs `ildsp_x64.dll` (tlh.exe, 4 output channels incl. sends): default + 12 feature cases (bypass, flush,
  freeze, reverse, mono, pan, source right, deterministic, bouncing, tempo, triggers, mode 1), plus a random stress
  of 190 cases (rates 22050–96000, blocks 64–4096, mid-stream parameter changes): **max diff 0 (bit-exact)**. [EXEC]
- Whole plug-in (fxt.exe + Transporter_x64.dll, 44.1 kHz, 512-frame Render calls, 48000 frames of LCG noise in eight
  loudness steps) vs LYNX's effect path: 6 states (demo song v8, Default v3, v4 Tempo, v4 Adaptive deterministic, v8
  mono auto-pan, v8 Bouncing source right): **max diff 0**. [EXEC]
- [TEST] `tests/Transporter167fTests.inc`: tp167f_state_parsing_and_params, tp167f_control_values_match_fl (float
  bits logged from FL), tp167f_effect_matches_fl_plugin (6 FNV-1a goldens of FL's plug-in output),
  tp167f_engine_matches_fl_library (2 goldens of the engine incl. sends, side-chain, beat grid, timed switches),
  tp167f_reset_latency_and_project_round_trip.

## 6. Not exact / open
- Send track → LYNX insert: FL addresses a mixer track; LYNX maps it to the k-th route of the insert. [HYP]
- FL's host silence handling around plug-ins is not modelled; LYNX always processes.
- The beat input is derived per 512-frame chunk from LYNX's ticks (FL: per host block, integer ticks).
- Update / Flush buttons latch in LYNX's parameter model (FL's buttons are momentary; the engine reacts to the rising
  edge either way, a second press needs the value to return to 0).
- Input-track buffer missing: LYNX feeds zeros to the detector (FL would read whatever the buffer holds).
- v3 states: FL reads Source / Stereo from uninitialised memory; LYNX uses 2 / 1.

## Where I stopped
- 2026-10-31: spec, engine, effect, import, layout, i18n and tests done on `feature/flgap167-tp`
  (commits 15ddb6c5 IldspMath logf/tanf, 488bb89c engine, then the wiring + tests commit). Not pushed, not landed
  (the round's coordinator lands it).
- Rerun: `scripts/build-local.sh` (via coord/buildslot.sh; the tp167f_* tests); scratch harnesses in
  `~/.claude/jobs/f8789b49/tmp/flgap167tp/` (py/sweep.sh, py/stress.py, py/goldtp.py), data in `/mnt/e/lynx-work/flgap167tp/`.
- Next: verify the send-track mapping on a real FL project with a Transporter send; momentary buttons in the editor.

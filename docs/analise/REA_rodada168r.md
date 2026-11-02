# REA round 168r — designed editors and LYNX preset banks, group E (v0.1.23)

Scope: every effect of group E that still opened as a generated knob page now has a designed editor, and every engine
that runs without FL Studio's files has a LYNX-made preset bank (◀ ▶ cycle LYNX's bank, then the user's presets, then
FL's factory presets when FL is installed).

| Group | Plugins |
|---|---|
| FL effects (FL's layout, LYNX style) | Pitcher, Love Philter, Peak Controller, Spreader, Fruity Fast LP, Frequency Shifter, Low Lifter, Distructor, Fruity Bass Boost, Fruity Delay (1), Fruity 7 Band EQ, Fruity Center, Fruity Phase Inverter |
| LYNX built-ins (LYNX's own design) | EQ, Delay, Reverb, Limiter, Filter, Distortion, Chorus, Utility, Lo-fi, Crossover, Pitch shift, Frequency shifter, Rhythmic envelope, Volume gate |

Method (same as REA_rodada168n §1): FL's own plugin DLLs were read with REA (PE resources, Delphi `TPF0` forms,
Ghidra on Peak Controller); FL's Artwork bitmaps were *viewed* as layout reference only. Nothing of FL's artwork,
bitmaps or presets is in the repository: the editors are drawn with LYNX's skin, the banks are LYNX's own settings.

Evidence tags: **[FORM]** read from the plugin DLL's `TSYNTHEDITORFORM` RCDATA resource (positions, sizes, control
kinds, tags); **[GHIDRA]** decompiled from FL's DLL; **[ICON]** inferred from an Artwork bitmap seen as reference;
**[LYNX]** LYNX's own design decision.

## 1. Tools

- `peres.py` lists a PE's RCDATA resources; `dfm.py` / `dfmc.py` decode Delphi `TPF0` streams into a compact tree
  (class, name, Left/Top/Width/Height, Tag, captions). Kept in the round's scratch folder (tools, not product code).
- Ghidra (one instance, under `~/.cache/lynx-ghidra.lock`) on a copy of `Fruity Peak Controller.dll`
  (sha256 `e73ec8da…`, copied to `/mnt/e/lynx-work/design168r/rea/`).

## 2. FL's forms (layout reference)

### 2.1 Fruity Fast LP, Fruity Bass Boost — 335 × 55 [FORM]
Two bevels 160 × 45 at x = 5 and x = 170 (FL's "Fruity" cells, 165 px apart). Fast LP: CUTOFF and RESONANCE, each a
wheel + value slider. Bass Boost: FREQ (wheel + read-out) and AMOUNT (wheel + slider).
LYNX: the same two cells (`fruityGrid` / `fruityCell`), plus a measured response strip (`fxresponse`) under them [LYNX].

### 2.2 Fruity Delay (1) — 500 × 105 [FORM]
Row 1: INPUT, FEEDBACK, CUTOFF (wheel + slider). Row 2: TEMPO, STEPS (1…16), MODE (wheel + read-out).
LYNX: the same 3 × 2 grid; STEPS a 16-step value, MODE 3 steps.

### 2.3 Fruity 7 Band EQ — 340 × 150 [FORM]
Seven vertical sliders ±1800 (= ±18 dB) at x 15, 62, 110, 157, 205, 252, 300, 29 × 123; labels 63 … 8000 Hz at y 130.
LYNX: the seven faders at FL's x positions and the band labels, plus a measured response strip [LYNX].

### 2.4 Fruity Center — 170 × 55; Fruity Phase Inverter — 170 × 55 [FORM]
Center: one bevel with a STATUS switch (off / on). Phase Inverter: one bevel with a STATE wheel (none / left / right /
both). LYNX: the switch as two tabs, the state as tabs.

### 2.5 Spreader — 540 × 255 [FORM]
Enable at (25, 5); Spread knob at (16, 33), Separation at (16, 101), LF bypass at (16, 167), knobs 44 px; Mono at
(12, 234). LYNX: the left column of the four controls, and a stereo-field display (`spreadfield`) in the space FL uses
for its picture: the spread / separation as the width of the image, the bypassed low band drawn mono.

### 2.6 Frequency Shifter — 460 × 220 [FORM]
Frequency wheel 88 px at (41, 21); visualizer at (120, 21) 157 × 61; Range selector at (161, 80) (20 kHz / 200 Hz /
Tempo); Type selector at (271, 80) (Shifter HQ / Shifter / Ring modulator); bottom row at y 147: Left shape, Right
shape (48 px), Start phase, L/R phase, Feedback, Mix, Stereo (38 px).
LYNX: the same arrangement; the visualizer (`fshift`) draws the harmonics of a 220 Hz tone and where the shift moves
them (both side bands for the ring modulator).

### 2.7 Low Lifter — 680 × 280 [FORM]
BandVisual at (6, 6) 668 × 182; the settings strip at y 200 carries the controls with tags 0–14, the same order as
LYNX's parameters. LYNX: the strip in FL's order, the band picture (`lowlifter`) in FL's place.

### 2.8 Distructor — 924 × 385 (main) and 200 × 321 (module) [FORM]
Main form: four module panels, peak meters, SOFT CLIP OUTPUT, SHOW PEAKMETERS, a module menu (Remove / Add
Distortion / Filter / Chorus / Speaker). The module form (`module/Module_x64.dll`): section selector, Param1–6 wheels,
algorithm combo, oversampling, DC, enable; In / DryWet / Out wheels (32 px) at y 285.
LYNX: four module columns with the module's name on top (read from the FL state: `distructor::parseBlob`,
`moduleName`) and In / Mix / Out at the bottom, the soft clipper at the right. The modules' own controls come from the
FL state (a preset or a project); with no FL state the first column says so.

### 2.9 Love Philter — 670 × 400 [FORM]
Eight unit enable boxes, filter-unit tabs, cutoff / resonance / envelope wheels, L/B/H sliders, a waveshaper, in / out
/ pan, the envelope editor, the Mod X / Y pad, master / LFO / follower sliders, IEF attack / release.
LYNX: the master column (level, LFO, envelope follower level / attack / release, pattern, the Mod X / Y pad) and the
eight units as a grid (cutoff, resonance, envelope, pan, volume per unit). The units' filter types, envelopes and
routing come from the FL state and play as FL's.

### 2.10 Peak Controller [FORM]
Panels PEAK (7, 7, 188 × 83: Base, Vol, Tension, Decay) and LFO (203, 7, 311 × 83: Base, Vol, Tension, Shape combo
Sine / Triangle / Square / Saw / Random, Speed, Phase); a meter 493 × 69; Ramp and Mute buttons.
LYNX: both panels, the live meter (`peakmeter`: the peak, the LFO and their sum over the last seconds), Ramp / Mute.

### 2.11 Pitcher [no form]
Pitcher's editor is drawn in C++ (no `TPF0` form); its BG1 / BG2 artwork (523 × 354) was viewed as reference only.
LYNX: the 12 note toggles (lit = the voice is pulled to them) over a bypass row, then Speed, Fine, Gender, Formant,
Reference (A3 Hz), MIDI, Octaves and Harmonize; the harmonize-only controls (voice pans, stereo spread, mix, velocity)
appear only while Harmonize is on.

## 3. LYNX's own built-ins [LYNX]

Each gets LYNX's design: a picture of what the setting does, the controls grouped under it.

| Engine | Picture | Notes |
|---|---|---|
| EQ | measured response (`fxresponse`) | THREE-BAND EQ section |
| Delay | the echoes on two bars (`delaytaps`), ping-pong as left up / right down, darker as damping grows | ECHO / TONE |
| Reverb | predelay gap and decay tail (`reverbspace`), bright line = damping | SPACE / TONE / LEVEL |
| Limiter | the existing limiter graph | LIMITER |
| Filter | measured response | FILTER: type tabs, cutoff, Q, mix |
| Distortion | transfer curve of one sine cycle (`fxtransfer`) | |
| Chorus | the two voices' delay sweep (`chorusvoices`) | CHORUS |
| Utility | the stereo matrix wiring (`utilitymatrix`), thickness = gain, blue = inverted | LEVEL / STEREO MATRIX |
| Lo-fi | transfer curve | LO-FI |
| Crossover | measured response | HIGH-PASS / LOW-PASS |
| Pitch shift | keyboard with the interval (`pitchshift`) | |
| Frequency shifter | the shifted harmonics (`fshift`) | |
| Rhythmic envelope | one period of the gain curve with its points (`rhythmcurve`), two pages (points 1–6 / 7–12) | |
| Volume gate | the 32 steps as faders (two bars of sixteenths) | STEPS / GATE |

The pictures that measure (`fxresponse`, `fxtransfer`) run a fresh instance of the slot's engine off the audio
thread (`core/EffectResponse.h`), cached per setting.

## 4. Peak Controller: the LFO (REA of FL's DLL) [GHIDRA]

Before this round LYNX's Peak Controller had only the peak follower and used the LFO base as a constant. FL's DLL:

- Controls at `+0x120 + 4i` (10 int32): 0 Base, 1 Vol (amount), 2 Tension, 3 Decay, 4 LFO base, 5 LFO amount,
  6 LFO tension, 7 LFO speed, 8 LFO shape, 9 LFO phase. Mute at `+0x198`, Ramp at `+0x199`.
- Saved state = the 10 int32 + 2 bytes (byte 40 Mute, byte 41 Ramp; e.g. FL's `Randomness.fst` has Ramp = 1).
- LFO amount: the same law as the peak amount but scaled 0.2 (peak: 0.4); the same tension curve (`FUN_006fcef0`).
- LFO value = (table[phase >> 18] + 1) · 0.5; the phase accumulator (32 bits) gains 2³² / period-ticks per host tick
  (`FUN_005b23a0`); period ticks = (1001^(speed/65536) − 1) · 0.128 · PPQ (`FUN_006fd180`).
- Shape 4 (Random) reads a separate table with the period × 16384: one new value per period.
- While the song plays the phase = songPosTicks · increment + phase << 16 (`FUN_006fd260`): the LFO is locked to the
  song; otherwise it runs free.
- Mute (`+0x198`) zeroes the audio passing through; Ramp (`+0x199`) limits each output's change to 0x8000000 (1/8 of
  the range) per tick (`FUN_006fdaf0`).
- Outputs: 0 = peak + LFO, 1 = peak, 2 = LFO, clipped to 0…1.
- Wave shapes: FL reads the host's LFO tables; their exact shapes are taken from `Artwork/LFOShape.bmp`'s icons [ICON]:
  sine, triangle (low → high at ½ → low), square (high first half), rising saw.

LYNX (`core/Effects.cpp` PeakController): all of the above on LYNX's song position (960 PPQ) with FL's 96-tick
host rate for Ramp; the import (`core/FlEffects.cpp`) reads the LFO controls and both flags. Seven new parameters
(LFO amount, LFO tension, LFO speed, LFO shape, LFO phase, Mute, Ramp) follow the round-17 five; older projects load
with them at their defaults (LFO amount 0 = the old constant LFO base).

## 5. Preset banks

Decision (coordinator's question): LYNX's effect banks live in the **existing effect preset mechanism**
(`core/EffectPresets.cpp`, text table), not in 168n's instrument-only `core/LynxBanks`. This round's banks are in
`core/EffectBanks168r.inc`, appended to the built-in table at parse time; `kindNamed` now prefers an exact-case match
(LYNX's "Frequency shifter" vs FL's "Frequency Shifter").

Banks (all LYNX's own settings, written in the editor's units): Fruity Fast LP, Fruity Bass Boost, Fruity Delay,
Fruity 7 Band EQ, Fruity Center, Fruity Phase Inverter, Spreader, Frequency Shifter, Low Lifter, Peak Controller,
Pitch correction (folders Correction / Major keys / Minor keys / Effects), EQ, Delay, Reverb, Limiter, Filter,
Distortion, Chorus, Utility, Lo-fi, Crossover, Pitch shift, Frequency shifter, Rhythmic envelope, Volume gate.
Love Philter and Distructor play FL's state: they have no LYNX bank (their FL presets stay listed when FL is
installed; without FL the Distructor page says the modules come from an FL preset or project).

Sanity check (`design168r_every_engine_has_a_lynx_bank_that_renders`): every preset resolves every value, and a test
mix (saw chord + kick + noise, −6 dBFS) through it is not silent (RMS ≥ 0.003), does not clip (peak ≤ 1.0) and has no
NaN.

◀ ▶ (`PresetBar::stepPreset`): LYNX's factory bank → the user's presets → FL's presets of the plugin (when installed).

## 6. Tests

`tests/Design168rTests.inc`: layouts place every parameter; banks resolve and render; Peak Controller LFO swing,
amount 0 = base, Ramp step limit, Mute, song lock; FL import of the LFO and flags; response / transfer pictures.
The existing `plugin_layouts_name_real_parameters` now also covers the 13 FL kinds of this round.

## Where I stopped

- 2026-11-02 (paused by the coordinator for round 168w's faster pipeline). Code, tests, translations (15 languages)
  and this document are on wip/design168r, merged with origin/main efb8344d.
- Last results (before the fixes below): core tests 2108424 checks, 81 failures, all from this round; Windows build
  failed on one error in `ui/DesignDisplays.inc` (`auto` deduced as double / float in one declaration).
- Fixed since, not yet rebuilt: bank values written `= #0.5` were cut by the table's comment rule (`#` after a space)
  → `=#0.5`; Lo-fi rates set to the values the engine shows (44100 / n Hz); the bank render test now runs the song
  position (Rhythmic envelope pumps were silent at position 0); the Peak Controller test's LFO base is 0 (FL's LFO
  adds base … base + amount); response test sets Bass Boost / Fast LP explicitly and prints the measured dB;
  `ownLayoutKinds` count 24 → 38; Center and Phase Inverter cells get a 72 px body with a one-line description
  (the generic layout test needs > 60 px); the generic layout test now prints which item leaves the body (one
  item of the 13 FL kinds did — find it on the next run); the DesignDisplays compile error.
- Exact next step on RESUME: core tests (build-local with FL Data) → fix the remaining failures (the item outside the
  body, check the Bass Boost / Fast LP dB lines) → Windows build → before / after shots (`LYNX.exe --design168rshot`,
  before with `LYNX_GENERATED_LAYOUTS=1`) in `/mnt/e/lynx-work/design168r/{before,after}` → review at 100 % / 200 %
  → land via coord/land.sh after 168n's shared infrastructure is on main.

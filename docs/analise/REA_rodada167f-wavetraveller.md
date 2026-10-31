# REA round 167f — Wave Traveller (FL Studio's scratching sampler)

Clean-room specification of FL Studio 2026's **Wave Traveller** generator, written in our own words from the analysis of
the installation's own binary, `Plugins/Fruity/Generators/Wave Traveller/Wave Traveller_x64.dll` (MD5
3255e93d583664764fa6f7be7ba9f7ad, image base 0x400000; function names below are Ghidra's default `FUN_<address>`),
and from running that DLL unmodified in our own host harness. No code, art or data of FL is copied into the
repository; the tests read FL's files from the user's installation at test time only.

Evidence markers: **[DECOMP]** read from the decompiled code; **[EXEC]** observed running the DLL in our harness;
**[DATA]** read from FL's own files (states saved in the installation's projects, presets).

LYNX implementation: `src/core/WaveTraveller.{h,cpp}` (engine, state, presets, instrument), `src/ui/WaveTravellerEditor.cpp`
(editor), import in `src/core/FlpImport.cpp`, tests `wave_traveller_*` in `tests/tests.cpp`, comparison tool
`tools/wavetravellerrender.cpp`.

## 1. What the plugin is

Wave Traveller plays one sample along a user-drawn **position curve**: over the note's time, the curve says where in
the sample (between Start and End) the playback is. Going up plays forward, going down plays backwards, flat holds
— a turntable "scratch". Each of the **120 keys** (C0 … B9) has its own patch (curves and knobs); a note plays the
patch of its key, and the key does **not** transpose [DECOMP FUN_00754fd0]. A second curve sets the volume, and a row
of mute switches gates the sound along the curve.

Plugin object [DECOMP]: the vtable used by FL's host calls slot 1 Dispatcher (0x74a540), 3 SaveRestoreState
(0x74c030), 5 ProcessEvent (0x74c540; event 0 = tempo, stored in the engine), 6 ProcessParam (0x74c5a0; the plugin has
no automatable parameters), 8 Gen_Render (0x74c410), 9 TriggerVoice (0x74c1b0), 10 Voice_Release (0x74c2d0), 11
Voice_Kill (0x74c310). The engine object lives in the plugin object (constructor FUN_007516a0).

## 2. The saved state

A sequence of tagged records: `i32 tag, i32 size, payload` [DECOMP FUN_0074c030 / FUN_0074aee0 / FUN_0074b340 /
FUN_0074bfc0; DATA: the states in `System/Render tests/Wave Traveller.flp`, the demo `Sid De La Cruz - Secret Agent
Rendezvous.flp` and the plugin database `.fst`]. Unknown tags are skipped by their size.

- **1000 — header.** size = L + 12; `i32 1000005` (version), `i32 selected key` (the key the editor shows — it is
  *not* a root note; LYNX's earlier fallback read it as one, which was wrong), `i32 L`, L bytes of the sample path (FL's
  path variables, e.g. `%FLStudioFactoryData%\Data\Patches\Packs\Drums\Snares\808 Snare.wav`; empty = no sample).
- **2000 — one per key, all 120 are saved.** `i32 key`, then floats: speed in beats (default 4), absolute speed
  (default 1), attack ms (1), release ms (1), start (0), end (1); `i32 spline type` (0 tempo based, 1 absolute speed);
  `i32 n` + n bytes of the preset name; `i32 m` + m mute entries of 8 bytes (a byte "sound on" flag padded to 4, a
  float position 0…1); `i32 p` + p wave points of 60 bytes; `i32 q` + q volume points of 60 bytes. The record's size
  field does not match its content (it counts 8 bytes per point) — readers must parse the record, not trust the size.
- **3000 — end.**

A point record (60 bytes): +8 x, +12 y, +0x14/+0x18 first handle (relative x, y), +0x20/+0x24 second handle,
+0x28 tension, +0x30 segment type (of the segment that starts at this point), +0x38 a flag that FL's default points
carry as 1. The other bytes are kept as they are on a round trip.

Default patch [DECOMP + DATA]: wave (0,0)–(1,1), volume (0,1)–(1,1), segment type 1, tension 0.5, handles
(−0.042, 0) and (0.042, 0), one mute entry "sound on at 0".

## 3. Curve → table (FUN_007558c0) [DECOMP, EXEC]

When a patch is loaded, each curve is turned into a table of float values that the voices read. A writer index
starts at 1; each segment between points P0 and P1 first steps the index back by one (so it overwrites the previous
segment's last value) and then writes `cnt = round_half_even((x1 − x0) · 1023f)` values, each clamped on its bit
pattern (negative → 0; above 1.0, infinities and NaN → 1). The table's length N is the final index − 1.
With `y0, y1` the segment's ends and `pi = 3.1415926` (the plugin's own constant), per type of P0:

1. **Linear**: y starts at y0 and grows by `(y1 − y0)/(cnt − 1)` (float) per value.
2. **Half cosine**: phase steps `pi/(cnt − 1)`; `y = y0 + (y1 − y0)/2 · (cos(phase + pi) + 1)`.
3. **Cubic Bezier**: control points are the ends plus their handles × 3 (P0's second handle, P1's first). 256
   points of the cubic are computed for t = i/255 (double); the table then walks x in even steps
   `fx = (i+1)(x1 − x0)/(cnt − 1) + x0` and linearly interpolates the Bezier's y at fx between the two points around
   it, the slope taken as `(float)(dy/dx)` in double; the walk stops early when fx passes x1.
4. **Sine quarter**: phase steps `(pi/2)/(cnt − 1)`; `y = y0 + (y1 − y0) · sin(phase)`.
5. **Ease-in cosine quarter**: same steps; `y = y0 + (y1 − y0) · (cos(phase + pi) + 1)`.
6. **Tension**: `a = (1 − 2T)/T²`; phase steps `1/(cnt − 1)`;
   `y = y0 + (y1 − y0)/a · (exp(phase · ln(a + 1)) − 1)`. With T exactly 0.5 the plugin takes a linear branch that
   divides by `cnt − 2` instead of `cnt − 1` (a quirk: the segment overshoots slightly before the clamp).
8. **Zero**: cnt zeros.
Other types (0, 7, …) write nothing, so the table also loses the value the segment would have shared.

## 4. The sample [DECOMP FUN_00757e00, EXEC]

The plugin asks the host for its sample with `LoadSample`, asking for **16-bit stereo at 44.1 kHz**; it converts the
frames to float pairs (value / 32768) and appends one guard frame made of the first frame's right value, twice.
`SmpRateConv` (the host's rate conversion factor for the sample) is used only by the absolute speed mode.
Without a sample the plugin refuses notes (silence).

## 5. The voices

**Trigger (FUN_00754fd0)** [DECOMP, EXEC]: key = round(Init.Pitch / 100); keys ≥ 120 play nothing. With M = sample
length and N = the wave table's length:

- tempo based: `step = tempo · (N − 1) / (speedBeats · rate · 60)` (float);
- absolute: `step = SmpRateConv · absSpeed · (N − 1) / ((M − 1) · (end − start))`;
- pitch: `d = (FinalPitch − (key − 60) · 100) / 1200`; if FinalPitch < 0, `step /= (1 − d)`, else `step *= (1 + d)`
  — so the channel's pitch and the note's fine pitch change the speed, the key itself does not;
- sample span: `s0 = trunc(start · (M − 1))`, `s1 = trunc(end · (M − 1))`;
- attack increment `1000 / (attackMs · rate)` per sample, release decrement `1000 / (releaseMs · rate)`;
- the initial gains are FL's pan law of (Final.Pan, Final.Vol · volume[0]).

**Per sample** [DECOMP FUN_00752740 / FUN_00754010, EXEC]: the read position t (double) advances by step. Mute
entries switch the gate when `at ≤ t/(N − 1)`; the volume index follows t. Envelope states: 0 attack (to 1), 1
sustain, 2 release (the voice ends when it goes below 0), 3 end fade: once t passes N − 1 the voice plays its last
256 output samples **backwards** with a linear fade (256 − i)/256 and then ends. The sample position is
`(wave[i] + frac · (wave[i+1] − wave[i])) · (s1 − s0) + s0`. Gains move towards their target by `g = 0.99 g + 0.01 target`
(gate off: `g = 0.99 g`). Every voice has a DC blocker `y = x − x₋₁ + (1 − 126/rate) · y₋₁` (a y whose magnitude bits
are zero is set to +0). Voices are summed newest first. The plugin runs with FTZ + DAZ (MXCSR 0x9fe0) [EXEC].

Two interpolation modes, chosen by the Dispatcher's ProcessMode message [DECOMP 0x74a540, EXEC]:

- **live** (FUN_00752740): a 6-point polynomial around the position; gain target = pan law of
  (Final.Pan, Final.Vol) × the volume curve;
- **render** (flag 0x10 with interpolation index 3/4/5 → 64/128/256 sinc taps per side; FL's render default is
  index 5 → 256): windowed sinc with a cos² window, cutoff `min(1, 1/|last position delta|) · pi`; pan = Init.Pan +
  Final.Pan; **the volume curve is not applied** in this mode (FL quirk, reproduced).

## 6. Presets (.spln) [DECOMP FUN_00763c60 writer / FUN_007650a0 reader, DATA: the 19 presets of the installation]

`u32 4000, u32 1000005, u32 spline type`, floats: tempo-speed knob, absolute-speed knob, attack knob, release knob
(0…1 knob positions), start, end; wave points (count + 60-byte records); volume points; mute count + entries. FL's
writer writes one mute entry fewer than it counts (a bug); LYNX's reader then takes an empty mute list.
Knob → value [DECOMP FUN_007579e0 / 757b10 / 757b60]: tempo speed `k · 7.875 + 0.125` beats, absolute `k · 3.75 + 0.25`,
attack `k · 499 + 1` ms, release `k · 999 + 1` ms.

## 7. The editor (FL's form, DFM `TSynthEditorForm`) [DATA: the DLL's form resources and strings]

Release / Speed / Attack / Start / End wheels, a speed mode selector ("Tempo based spline" / "Absolute speed based
spline"), a wave / volume curve switch, a mute painting strip, a note selector keyboard (per-key patches), snap, a
presets menu. LYNX's editor interprets this layout in its own design (dark theme): key strip, five knobs, mode
selector, position / volume curve view with draggable points (double click adds / removes; right click sets the
segment type and tension), mute strip, FL presets read from the user's installation, copy / reset of a key's patch.

## 8. Verification [EXEC]

Our harness (`wx.c`, scratch, not in the repository) loads the DLL, feeds it the state, a sample (via its LoadSample
host callback, 16-bit stereo), tempo, notes with Init/Final parameters, renders in 512-frame blocks and dumps the
float output; `tools/wavetravellerrender` renders the same scenario with LYNX's engine. Results, compared bit by bit:

- installation render test `Wave Traveller.flp` (808 Snare): **0 differing bits**;
- demo `Secret Agent Rendezvous` states (keys 60–69 single and overlapping notes with pans, pitch offsets and
  releases, two samples): **0**;
- a crafted state with every segment type, volume curves, mutes, attack / release, absolute mode, types 0 / 7, a
  tiny Bezier, and all 19 installation presets on keys 64–82: live **0**; render mode 64 and 256 taps (with Init.Pan)
  **0**; 48 kHz host at tempo 97.5 **0** (absolute mode with SmpRateConv = 44100/48000).

The test goldens (`wave_traveller_matches_fl_engine`) are FNV-1a hashes of the DLL's output float bits for scenarios
LYNX reproduces exactly.

## 9. Not exact / hypotheses

- **[HYP] SmpRateConv** at host rates other than 44.1 kHz: LYNX uses 44100 / rate (the sample is handed at 44.1 kHz).
  With that value the DLL and LYNX agree bit for bit; FL's real host value was not observed (FL is never launched).
- **[HYP] the host's conversion** of samples that are not 16-bit / 44.1 kHz: LYNX rounds to 16 bits and resamples
  linearly to 44.1 kHz; FL's own converter was not analysed.
- A reversed span in absolute mode (end < start) makes FL read outside its table (garbage values); LYNX clamps the
  table index instead.
- FL's voice list has no limit; LYNX keeps 64 voices per channel (the oldest is stolen).
- Pitch changes after the note starts (pitch bend, slides) do not change a sounding voice's speed in FL either (the
  step is computed at trigger) [DECOMP]; LYNX matches.

Where I stopped: spec complete; engine, import, save/load, editor, i18n and tests done; next would be FL's own
converter for non-44.1 kHz samples and the real SmpRateConv value.

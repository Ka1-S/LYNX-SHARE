# Round 168x — issues #40 (Maximus In/Out graph) and #41 (record an insert after its effects), v0.1.23

Both issues by Carlrune20 (CachyOS, AUR 0.1.21, FL Studio 26 under wine).

Evidence markers: **[DECOMP]** Ghidra through REA on FL's own binaries (clean-room: behaviour only), **[STR]** strings of
FL's binaries, **[DATA]** FL's own files (demo projects, presets), **[SHOT]** a driven-FL screenshot of an earlier round,
**[CODE]** LYNX source before this round, **[TEST]** unit test.

| item | reporter's words | FL evidence | LYNX before | LYNX now |
|---|---|---|---|---|
| #40 | "I cannot create new points or move points in the In/Out volume graph editor. The rest of the knobs work just fine, as well as the band selector." | §1: Maximus' graph is FL's shared envelope editor (Maximus_x64.dll MouseDown `FUN_0081e410`, the same code as Gross Beat's `FUN_00821760`, REA_rodada168m §2) | the "maxcurve" display was a picture: `itemAt` never hit-tested it and no code could write a curve back — on every OS, not a Linux / wine problem | FL's gestures on the graph; every edit written into the band's envelope in FL's own state layout; the engine plays the edited curve |
| #41 | "Arming a mixer insert and recording (Shift+R) captures only the dry input. There's no way to record the signal after the insert's effects … Suggestion: a per-insert option for the recording source … Input / Post-effects / Both" | §2: FL has exactly this option: the insert's "Record" positions, six of them, Post EQ by default | only "External input only" (the device input, `AudioEngine` pushed it before the render) | FL's six positions in the insert's input menu, Post EQ by default, tapped inside the insert in FL's order, latency-aligned; plus "Also record the dry input as a second take (LYNX)" for the DI + amp case |

## 1. #40 — Maximus' In/Out graph

### 1.1 Why the mouse did nothing [CODE]

`PluginEditors.cpp` `NativeEffectEditor::itemAt` hit-tests only the display ids it lists ("eq", "xy", "waveshaper", …);
"maxcurve" was not among them, so a click on the graph never reached any handler, and the paint code (`id == "maxcurve"`)
only drew `maximusfx::curvePoints` as straight lines. Nothing Linux- or wine-specific: the same on Windows. The knobs and
the band selector are ordinary controls, which is why they worked. (Same root cause as round 165b's WaveShaper and round
168m's Gross Beat: a display left paint-only.)

### 1.2 FL's gestures [DECOMP Maximus_x64.dll, FL Studio 2026]

`Maximus_x64.dll` (sha256 acc7affb…dc1) carries FL's shared envelope editor; the undo names "add point", "delete point",
"move point", "move tension", "default tension" are referenced from its MouseDown `FUN_0081e410` (button argument 0 left,
1 right, 2 middle; shift state 1 Shift, 2 Alt, 4 Ctrl). It is the same routine round 168m read in Gross Beat:

| gesture | FL (undo name, branch in `FUN_0081e410`) |
|---|---|
| right-click on empty space | "add point" (`0x8204ea`): x at the mouse; without snap the level sticks to the curve when within **8 px** (`lVar10 < 8`), Shift = level on the curve; the point gets the editor's new-point type (`editor+0x15e0`) and the last used tension (`editor+0x15dc`); the drag continues while the button is held |
| right-click a point | the point's popup menu (`FUN_006989b0`) |
| Alt+right-click a point | "delete point" |
| right-click a tension handle | "default tension" (tension 0, last tension 0) |
| Alt+left on a handle | "default tension" |
| left-drag a point | "move point" |
| left-drag a tension handle | "move tension" (the last used tension follows) |
| (any) | `FUN_0081e280` warps the pointer onto the point it grabbed (`SetPhysicalCursorPos`) — not reproduced (it can't work on Wayland and LYNX never warps the pointer in editors) |

[STR] in the same DLL: "The first point of the envelope can't be removed.", "The automation envelope must have at least
%d point(s).", the hint "^^In: %s out: %s^" (the graph's hint shows the input and output values).

[DATA] FL's Maximus presets (`Data/Patches/Plugin presets/Effects/Maximus/**.fst`, Plugin database `Maximus.fst`): every
band curve is a map stream (`kind 3, version 2|3, n`, n × {Δx f64, level f64, tension f32, type, 0, 0, direction}, 20-byte
trailer) whose Δx sum to 1; types 0 (single curve) and 7 (single curve 2) only; the default is
(0, 0) → (0.5, 0.5) → (1, 0.5): the identity up to 0 dBFS, then flat (a limiter).

### 1.3 LYNX now [CODE] [TEST]

- `core/HarmlessFx`: `maximusCurve` / `withMaximusCurve` read and write envelope e of a Maximus state in place (the map's
  header and trailer kept; a point given back unchanged keeps its stored record bit for bit, so write → read → write is
  identical, including FL's double levels and direction bytes); `maximusCurveShape` steps the engine's own curve
  evaluator (the one the gain tables are built from) so the drawn curve is the played one, tension included.
- `core/MaximusFx`: `curveForEditing`, `withCurve`, `addCurvePoint`, `moveCurvePoint`, `removeCurvePoint`,
  `curveShape`, `curveLevelText` (axis value → dB: 0.5 = 0 dBFS).
- `core/PluginLayout`: `plugui::displayTakesMouse` — the list of display graphs the window hit-tests now lives in one
  testable place, with "maxcurve" in it.
- `ui/MaximusCurveEditor.inc`: the graph edits the selected band's curve with the gestures of §1.2 (hover highlights,
  hint bar, value tip "In −6.0 dB  Out −3.2 dB", point menu: Delete, Mode, Copy / Paste value, Type in value…). The first
  and the last point only move up and down (the curve spans the whole input range: FL's presets all end at x = 1).
- Edits go through `editSlot` (undoable); the effect is rebuilt from the new state like a knob edit.
- Tests `tests/Maximus168xTests.inc`: the graph takes the mouse (fails on main: no such list, "maxcurve" not hit-tested);
  add / move / delete / tension written in FL's layout and read back; a raised curve makes a quiet sine louder through
  the engine; every Maximus preset FL ships writes back unchanged (with FL Data).

Left (stated in the reply): the point menu offers only FL's modes LYNX's Maximus engine plays (Single curve, Single
curve 2); FL's other envelope modes (hold, stairs, pulse…) would need their evaluation in the engine first.

## 2. #41 — what an armed insert records

### 2.1 FL [STR] [DECOMP of earlier rounds] [DATA] [SHOT]

- [STR] `FLEngine_x64.dll` (FL Studio 2026): "Recording: ", "External input only", "External and mixer input only",
  "Post effects", "Post EQ", "Post level and panning", "Post track", "Monitoring: ", "|Recording and monitoring
  settings"; option names `PostFXRecApplyLevelAndMute` ("Apply track level and mute when recording", the mixer's Disk
  recording menu) and `RollingAudioBufferPreFX`.
- [DATA] REA_rodada88: the popup `AudioRecorderRoutingMenu` of the insert's input box: "Record": External input only /
  External and mixer input / Post effects / Post EQ / Post level and panning / Post track; then "Monitor external input".
- [DECOMP] REA_rodada165f-mixer §4 (`FUN_012aada0`): track byte +0x2F = 0 … 5 in that order, saved as .flp 0xA5;
  "External input only" without an input warns "won't record anything unless you select an input!" (the other
  positions record without an input).
- [DECOMP] REA_rodada2 §2.1 / REA_rodada6 §1.4 (`TMixerTrackModule` process `FUN_01276ee0`): the insert runs input
  monitoring → recording tap (+0x2F) → 10 slots → **post-FX tap** (`FUN_01277550`) → EQ → **post-EQ tap**
  (`FUN_012775a0`) → fader / pan / separation / polarity / swap → **post-fader tap** (`FUN_012775f0`) → mute → routes.
- [DATA] the FL 2026 demo projects: 24 of them save 0xA5 = 3 on all 127 inserts, 15 on 18, …; only old ones save 0.
  [SHOT] REA_rodada165-issue11c: the Playlist audio track of a fresh insert hints "Recording: Post EQ, Monitoring: When
  armed". **FL 2026's default is Post EQ.**

So FL already does what the issue asks (Input = "External input only", Post-effects = "Post effects"); "Both" (dry and
processed as two takes) does not exist in FL.

### 2.2 LYNX now [CODE] [TEST]

- Model: `MixerTrack::recordSource` (`RecordSource`, FL's six values in FL's order, default **Post EQ**) and
  `recordDryToo` (LYNX extra). Saved in .lynx (`recordSource` when not Post EQ, `recordDryToo`), compared by the undo
  history; .flp 0xA5 imported (a file without it keeps Post EQ).
- Renderer (`RackRenderer::processTrack`): the taps in FL's order — External and mixer input after the insert's inputs
  are summed (device input when monitored + routed audio), Post effects after the slots, Post EQ after the built-in EQ,
  Post level and panning = the insert's level matrix without the mute (its own ramp), Post track after the mute. A
  tapped insert that is muted still runs its effects (the positions before the mute keep recording). The taps write
  into `LiveInput::taps` (16 preallocated stereo buffers, nothing allocated on the audio thread); with no take tapped,
  `tapCount` is 0 and the renderer does no tap work.
- `AudioEngine`: "External input only" takes are pushed from the device input before the render (as before); tapped
  takes after the render, the take lock held through the render so every take gets every block. A take tapped after
  the slots drops its first *L* frames and is closed with *L* frames of silence, L = `insertEffectsLatency` (the slots'
  latency of that insert from the PDC plan): the wet take lines up with the input to the sample. A take tapped after
  the slots plays back through a new free insert (through its own insert it would get the effects twice) — LYNX's
  choice, FL's routing of such a clip was not measured. The positions other than "External input only" don't need an
  input (resampling an insert works).
- LYNX extra **"Also record the dry input as a second take (LYNX)"**: with a position other than External input only,
  the device input is recorded too, as "<insert> DI" (the DI for re-amping), on its own Playlist track through the insert.
- UI: the insert's input menu (Mixer, right-click the input box / arm) gets FL's "Record" section before "Monitor
  external input", plus the LYNX item; the arm hint says "Recording: Post EQ" like FL's. 15 languages.
- Tests `tests/Rec168xTests.inc`: FL's default and byte values; the taps through an insert with +6 dB in a slot, +6 dB
  in the EQ and the fader at half follow FL's order (and the mute only silences Post track); the slots' latency used to
  align a wet take.

Not done: FL's global "Apply track level and mute when recording" (`PostFXRecApplyLevelAndMute`, the Disk recording menu)
— LYNX's Post level and Post track positions already give both; the rolling buffer (`RollingAudioBufferPreFX`) is
another feature.

## GUI checks (Windows, real mouse) [SHOT]

- #40: Maximus opened from an FL project. Right-drag on empty space added a point that followed the mouse. Left-drag moved a point. Dragging a tension handle bent the segment. Right-clicking a point opened "Point 3/4: In … Out …" with Delete, the two modes, Copy/Paste value and Type in value. Delete removed the point.
- #41: 3x Osc → insert 1 (Maximus), no audio input. The insert's input box menu showed FL's Record section with Post EQ ticked. I picked Post effects; the arm hint read "Insert 1 - Recording: Post effects". Rec + Play (record "Everything"), then Stop, gave "Insert 1_Take1" on a new Playlist track that plays through a free insert (Insert 2).
  - The WAV holds the limited Maximus output: RMS 0.79 at a 0.9997 peak while the notes played, then silence.
  - "External input only" would have recorded nothing, because the insert had no audio input.
- I closed LYNX through its window (Discard, Keep recording). Nothing of mine was left on the desktop.

## Where I stopped

- 2026-11-02: #40 and #41 implemented with tests. The first build hit a corrupted header read (faulty RAM), so I rebuilt.
- 2026-11-03: Both Windows GUI checks pass. In build-local, the latency test failed because it gave latencies to slots that held no effects. I fixed the test.
- Then: notes and replies written. Final build-local, then push feature/issues4041x. The coordinator lands it after 0.1.22.

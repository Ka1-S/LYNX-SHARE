# REA round 190hg: Slicex — slide notes and porta, display modes and the keyboard button (#172, #157)

Continues REA_rodada190h / 190l / 190r / 190ac / 190aj. FL Studio 2026's `Slicex_x64.dll` (copy
`/mnt/e/lynx-work/slicex188c/rea/slicex.dll`, the analysed Ghidra project of round 188c) was read statically with
Ghidra 12.1.4 (headless, read-only, under the Ghidra lock), its form resources (`TSynthEditorForm`, `TMEnvEditor`) and
its Delphi method and field tables; FL itself rendered four edited copies of a Slicex project (`/R`, under the GUI and FL
locks). Clean-room: behaviour in our own words, no Image-Line code, bitmaps or fonts. Marks: **[DEC]** read in the
decompiled code, **[FORM]** form resource, **[EXEC]** FL ran it, **[HYP]** not confirmed. Work folder:
`/mnt/e/lynx-work/slicex190hg/` (rea/ decompiles, slide/ projects and FL's renders, edit/ LYNX editor runs, shots/).

## 1. Slide notes and the channel's porta (#157)

- [DEC, round 190h Part 3] Every render call of a Slicex voice takes the host's Final pitch of the voice and adds it to
  the voice's own pitch: `cents = region pitch + (60 − key) · 100 + FinalLevels.Pitch + master pitch`. The key (the
  region it plays) comes from the note's Init pitch, once, at the trigger. So whatever moves a sounding voice's Final
  pitch bends the slice without choosing another region: FL's slide notes and the channel's porta / mono glides.
- [EXEC] `slide/sl_base.flp` (Slicex_vel64's channel, one note C5 for 2 beats) and three copies with slide notes:
  `sl_up` (a slide to G5 at tick 24, 24 ticks long), `sl_down` (F4 at tick 24, 48 ticks), `sl_two` (D5 with a slide up
  an octave and one back). FL's renders (`slide/fl/*.wav`) are sample-identical to the plain note up to frame 5513
  (tick 24.002) and differ from there: the slice is bent (up: it plays out sooner; down: later). FL's Slicex follows
  slide notes.
- LYNX: `SlicexInstrument` says it follows slides and the channel's porta (the rack then sends it slide notes and
  porta glides instead of new notes). With nothing gliding the render path is the old one, so old projects render
  bit-identically.

### 1.1 FL's slide law, measured on its renders [EXEC]

Tracking the playback speed in FL's `sl_up` (a window-by-window fit of FL's output against the unbent slice) shows the
pitch rising in equal steps, not a straight line, and reaching the target at the slide's end. A model fitted on FL's
own output (`p/slidestep.py`, then the engine harness `p/h/slideh` that renders through LYNX's Slicex engine):

- the host moves the voice's Final pitch once per tick of the song (the project's PPQ, 96: 229.6875 frames at 120 BPM);
- a slide n ticks long makes n + 1 equal steps: step k (k = 0…n) sets `from + (to − from) · (k + 1) / (n + 1)` at the
  frame nearest to the song's tick (start + k); the first step comes with the slide's start, the last at its end;
- linear in cents; steps of n instead of n + 1, or a smooth glide, are 15–40 dB further from FL.

| render (FL /R vs LYNX renderwav) | before (slides ignored) | linear glide, 256-frame steps | FL's per-tick law |
|---|---|---|---|
| sl_base (no slide) | −69.9 dB | −69.9 dB | −69.9 dB |
| sl_up (+7 semitones, 24 ticks) | −8.3 dB | −26.9 dB | **−68.9 dB** |
| sl_down (−7 semitones, 48 ticks) | −10.4 dB | −30.3 dB | **−68.7 dB** |
| sl_two (+12 over 36 ticks, back over 24) | −7.0 dB | −13.7 dB | **−61.7 dB** |

(esr against FL's 16-bit file; −70 dB is that file's floor.) The tick is FL's default PPQ (96) at the channel's tempo;
a project with another PPQ is assumed to step on its own ticks [HYP: not rendered]. Other native generators in LYNX
still glide linearly (core/HostSlide.h): if FL's host steps their Final pitch the same way, which this measure
suggests (it is the host's), their slides would want the same law — not changed here (their sound would change
without a render of each to prove it).

## 2. Display modes and View keyboard ("switching layers", #172)

[FORM] FL's top right: `KbBtn` (25 × 30 at 574, 61; hint "View keyboard"; down by default) and `DispModeSelect`
(108 × 30 at 604, 61; four cells, hint "Display mode", cell hints "full", "no audio editor", "no region settings",
"compact"). Both are saved in the state's options block: the display mode as the int at 0x10, the keyboard as the
byte at 0x2c (LYNX's `oDisplayMode`, `oKeyboard`; both were already read and written byte for byte).

[DEC] What they do (`CompactBtnClick` 0x891960 → relayout 0x898b50 → deck layout 0x897c60; `KbBtnClick` 0x8941d0;
the form's field table gives the controls: ArtPanel +0x830, DeckPanel +0x718, KbPanel +0x9b8, the decks' editors at
deck + 0x18):

| row (form px) | shown when |
|---|---|
| top panel 0…99 | always |
| ArtPanel 99…297 (region settings, articulator) | display mode 0 or 1 |
| DeckPanel 296…362 (the Edison-style tool row) | display mode 0 |
| decks, from DeckPanel's bottom (else ArtPanel's, else ArtPanel's top), 9 px apart | mode 0: the current deck 163 px; the other one 37 px, only with the dual deck on. Modes 1 and 2: every deck that has a sample, 53 px (85 px when the other deck is empty). Mode 3: none |
| KbPanel, 54 px, from the bottom of the last row shown | the keyboard button is down |

- The window's height ends with the last row (the form is resized).
- Outside mode 0 FL presses the decks' own "Freeze editing" button (their editing stops); back in mode 0 each deck gets
  the freeze state it had.
- In compact mode KbPanel's top is ArtPanel's top minus a metric of FL's skin object; [EXEC] FL's window puts it at 99,
  so the metric is 0 at 100 %.
- Choosing the current deck (A / B) makes that deck the 163 px one (mode 0).
- [DEC] A deck that is not the expanded one (`TMEnvEditor` relayout 0x852430: +0x110a off) hides its scroller, its
  envelope / LFO panels and its button row: only the wave with its region tags is drawn.
- [EXEC] FL session (round 190hg, `fl/shots`, 22:50–22:58 under the GUI and FL locks, `hg_base.flp` = 190ac's
  ac_base): each mode and the two buttons were clicked and photographed. KbPanel's top measured from the keys: full +
  dual 571, single deck 525, "no audio editor" 382, "no region settings" 184, compact 99 — the table above, to the
  pixel. Deck B as the current deck: deck A becomes the 37 px strip above it.

LYNX: the same rows from the same rules (`SlicexEditor::rows`), the two controls at FL's places with LYNX's drawing;
the channel window follows the editor's height (`ChannelFrame::childBoundsChanged`).

## 3. The articulator editor's buttons and menus

[FORM] The articulator editor is FL's shared `TMEnvEditor` (its own form resource in the DLL). Its button row (GlobalPanel,
right-aligned) has, visible in Slicex's articulator editor: Freeze ("Freeze editing / hide points", off), Step ("Step
editing", hold Shift for pulse mode; off), Snap ("Snap to grid", off) and Slide ("Slide succeeding points / regions",
on, drawn yellow); at its left end the options menu (▾). The form's fields (field table): FreezeBtn +0xf58, StepBtn
+0xf68, SnapBtn +0xf70, SlideBtn +0xf78; their click handler (0x85b8b0) copies them to the editor's flags +0x1106
(frozen), +0x1100 (step), +0x1108 (snap), +0x1107 (slide).

- [DEC] Point drag (EnvVisualMouseMove 0x853c40): with Slide on, the dragged point is only kept after the previous
  point and every later point moves by the same time; with Slide off, the point is kept between its two neighbours and
  the others keep their times. LYNX's drag did the first only; the Slide button now chooses.
- [DEC] Freeze (0x85b8f0): no point editing, the points are not drawn.
- [DEC] Point menu (form): Delete; "Mode" with the 13 curve modes in the order Hold, Smooth, Single curve, Single
  curve 2, Single curve 3, Double curve, Double curve 2, Double curve 3, Half sine, Stairs, Smooth stairs, Pulse, Wave;
  "ADSR section" (Decay, Loop start, Sustain / loop end); Copy value / Paste value (0x85d9b0 / 0x85d9e0: the hot point's
  level as a number on the clipboard; paste sets the selected points' level, clamped to 0…1) and "Type in value...".
  LYNX: the same order and entries except "Type in value..." (FL's value dialog was not read); paste acts on the
  clicked point (LYNX's graph has no point selection).
- [DEC] Options menu, "Flags" (shown for the LFO): "Bipolar LFO tension" = the LFO curve's flag bit 2 cleared
  (0x84d510), "Frozen LFO" = bit 3, and checking it also sets bit 1, the global phase (0x850fd0). [EXEC] photographed
  in FL (`shots/fl/lfomenu.png`). The engine already plays both bits; LYNX's ▾ shows these two (only on the LFO part).

Not built (needs more than this round could read; facts kept for the next one):
- **Snap to grid** (articulator editor and decks). The snap function (0x84daf0) rounds a time to the x axis' grid step
  and offset (editor +0x13b4 / +0x13c4) when Snap is on (Alt inverts it, 0x864fe0) and the axis allows it (+0x13ee);
  other deck modes: regions (the nearest region edge, 0 or the sample's end within 12 px), samples (a whole frame),
  zero-crossing (0x62b9e0), pitch period (0x62be20). The grid step comes from the ruler (0x844700: the smallest
  multiple of the axis' base step that lies ≥ 12 px apart on x, ≥ 8 px on y, and divides / is divided by the larger
  steps) and the base steps the plugin sets for each editor, which depend on the zoom: FL's articulator editor has a
  fixed time zoom with a scroller, LYNX's fits the curve to the graph, so the same step law would give other steps.
  The Snap menu of the deck (grid, regions, samples, zero-crossing, pitch period; choosing one presses the deck's Snap
  button, 0x865420) acts on selections and envelope points; FL's region tags were never snapped in rounds 190ac / 190aj's
  FL sessions.
- **Step editing** (drawing points along the drag, pulse mode with Shift): a long routine of its own (0x853c40,
  step branch).
- The rest of the options menu (State, Points selection, Tools, Options, View) and the time scroller / zoom.

## 4. Best way?

- **FL:** as above.
- **Ableton:** Simpler's slicing mode has no slide notes; pitch bend / glide come from MPE or the Glide mode of
  Simpler's Classic mode, not in Slice mode. Live's device views have one fixed layout per device (Simpler's
  "Sample" and "Controls" tabs), no display modes.
- **Users:** not gathered this round.
- **Verdict:** FL-exact defaults. No alternative proposed.

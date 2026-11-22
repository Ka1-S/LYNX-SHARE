# REA round 190pm: what FL has for note repeats / conditions, and Windows touch in JUCE (research190oe P19, P23)

Clean-room. Only names, strings and observed behaviour are used; no code was copied.

## 1. FL Studio 2026: per-note probability, conditions, ratchets [DATA]

Target: `FLEngine_x64.dll` (FL Studio 2026, 22,227,016 bytes). A string scan was run over ASCII and UTF-16 for
"robabilit", "Chance", "Condition", "ratchet", "Trig cond" and "occurrence".

- **Probability.** "Note occurrence probability" appears twice. Both are the Piano roll's **Randomize** tool:
  - in the hint list beside "Note length variation" and "Note select options";
  - in the form resource `forms.prrandomform.patternbox.noteprobwheel`, a `TVectorWheel NoteProbWheel`.

  So it is the chance that Randomize *creates* a note. It is not a stored property of a note. FL's 24-byte note record
  (REA_rodada2 §4.1, REA_rodada190ma §3-4) has no probability field.
- **Conditions and ratchets.** No string for a trig condition, a fill or a ratchet count. "Condition" only shows up in
  Delphi RTL and Windows API names.
- **Repeat.** The only per-note repeat is the **rate** of issue #208 (REA_rodada190ma §3). It is byte 0x11, an index
  into the 14 rates at 0x154a138, and FL strikes the note again at that rate for as long as it lasts.

**Consequence for LYNX.**
- Chance and Velocity Range stay what round 187c made them: Live 12's.
- Ratchets (a count over the note's length, Bitwig's Repeats) and Conditions (Elektron's / Bitwig's occurrence and
  recurrence) are LYNX additions next to them.
- When a note has FL's rate, the rate wins.
- The .flp export writes a ratchet as FL's rate when one of the 14 rates fills the note exactly and there is no
  velocity bend. Examples: 4 strikes on a 1/4 note = 1/16 (Step), rate 7; 3 strikes on a 1/4 note = 1/8 triplet, rate 6.
  Any other ratchet is written as its strikes, notes of their own.
- Conditions have no FL field. They are reported in the export's notes.

## 2. Live 12 [DATA, round 187c]

- `MidiNoteEvent` carries Probability and VelocityDeviation (and NoteProbabilityGroups). The .als export keeps them on
  every strike of a ratchet.
- No ratchet count and no pass condition exist in a Live 12.4 set. Ratchets are exported as notes; Conditions are
  listed in the export's notes.

## 3. Windows touch in JUCE 8.0.4 (the patched copy LYNX builds with) [SRC]

From `modules/juce_gui_basics/native/juce_Windowing_windows.cpp`:

- **Registration.** Each window is registered with `RegisterTouchWindow (hwnd, 0)`. On Windows 8 and later the
  pointer API (`WM_POINTERDOWN / UPDATE / UP`, `canUsePointerAPI`) is used first; `WM_TOUCH` is the fallback.
- **One source per finger.** Each finger becomes a `MouseInputSource` of type `touch`, keyed by its pointer id
  (`currentTouches.getIndexOfTouch`). That gives multi-touch: several sources down at once, each delivering its own
  mouseDown / mouseDrag / mouseUp to the component under it, with the left-button flag only.
- **Pinch.** `handleMagnifyGesture` comes only from `WM_GESTURE` GID_ZOOM. A window registered for raw touch does not
  get WM_GESTURE, so a pinch on a touch screen never reaches `mouseMagnify`. (Precision touchpads send Ctrl+wheel,
  which `gesture::pinchZoomFactor` already reads.)
- **No system gestures.** Windows' press-and-hold right click is not generated for a raw-touch window. GID_PAN,
  GID_TWOFINGERTAP and GID_PRESSANDTAP are ignored.

LYNX's Touch mode therefore reads the fingers itself (`core/TouchGestures.h`):
- a long press gives the context menu;
- two sources down together give the pan and the pinch.

`GetSystemMetrics (SM_MAXIMUMTOUCHES)` tells LYNX whether a touch screen is there, so it can suggest the mode.

FL Studio itself uses VCL's touch support. FLEngine has `Touch.InteractiveGestures` (igZoom, igPan, igRotate,
igTwoFingerTap) on its knobs and wheels and `TabletOptions` with toPressAndHold on its captions [DATA, same scan].
FL 2026 has no Touch mode screen of its own to match, so the Simple layout's mode follows FL Studio Mobile and BandLab:
- finger-sized targets;
- a long press for the menu;
- two fingers to scroll;
- a pinch to zoom.

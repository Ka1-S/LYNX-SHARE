# REA round 170e5: Loop record off in Pattern mode, record switches at 1280 px, the step-edit cursor

Branch feature/toolbar170e5 (from origin/main after 170e3). Follows the coordinator's decision on the open 170e3 item:
"Loop record off in Pattern mode behaves like FL".

## FL evidence (FLEngine_x64.dll, read-only, clean-room; Ghidra headless, tmp/r170e)

- Loop record is one global byte (PTR 0x15e0c58), toggled by OptionsLoopRecordActionExecute [DECOMP 0xf1e2a0].
- FUN_011d4800 [DECOMP, tmp/r170e/looprefs.txt] sets the play end `PTR_DAT_015e4628 = 0x7fffffff` ("never") when
  `recording > 0 && loopRecord == 0`, in both modes. Without that, in Pattern mode (`0ef0 == 0`) the end comes from
  the pattern's length. So with the switch off the pattern position runs on past the pattern's end: the pattern does
  not start over while recording.
- FUN_00e4e970 [DECOMP, tmp/r170e/e4e970.txt]: only an editor time selection (d74/d78 >= 0) sets the start and end
  again. Otherwise flag 2 leaves the end at 0xffffffff and flag 4 sets it to the length (0xb1c). LYNX's Pattern mode has
  no time-selection loop, so this exception does not apply.
- FUN_0126c040 names the takes "recorded items (loop %d)" only with the switch on (one group per pass).
  FUN_0126bdf0 / FUN_011ca830 handle the stop with it off (position >= end flag, then the position reset).
- The notes stay at the position they were played. FL's pattern length follows its notes (auto length), so the
  pattern grows to the bar after the last note.

Step edit:
- OptionsStepEditActionExecute [DECOMP 0xf1e740] passes the checked state to the focused editor (vtable +0xb0).
- The step-edit step is the toolbar's main snap: [STR] hint "|^d^^BkSp^Main snap / quantization / step edit step".
- FL has no cursor or caret of its own for step edit. The only strings are the switch's names ([STR] "Step edit",
  "Step editing mode", hint "^^Ctrl+E^Step editing mode", tmp/r170e/stepstr.txt: no code references them besides
  the hint table). The other "step editing" reference (FUN_00cce3b0) is the undo name of a mouse edit in an editor,
  not a cursor. The insert point is FL's playback position marker, which the Piano roll draws
  ([STR] "Scrolls to reach playback marker"). [INFERENCE] So the "real cursor" is that marker, shown while stopped.

## What changed in LYNX

| # | item | before | now |
|---|---|---|---|
| 1 | Loop record off in Pattern mode | the pattern always looped and a take folded into it | the pattern plays once from the start of the pass the take began in, then the position runs on (silence after the old end, no repeat). Notes stay where they were played, so the pattern grows. The Piano roll playhead runs on past the end. When the take ends (Stop / record off), the pattern loops again. Loop record on (the default) is unchanged. |
| 2 | record switches at 1280 px | dropped at 1280 (kept from 1366 by the 170e3 compact level) | a second compact level: the snap and pattern captions keep only their icons, the hint column shrinks to the menu's width, the tempo and time displays get 6 + 8 px narrower, and the gap is 11. The record group stays. |
| 3 | step-edit cursor | the Piano roll drew the playhead only while playing, so a stopped step edit showed no insert point | in Step edit the Piano roll shows the position marker while stopped (line + ruler triangle), moves it a step after each note or chord, and Follow keeps it in view |
| 4 | step-edit step | the Piano roll's own snap | the main (toolbar) snap, like FL. A snap with no size of its own (Line, Events) uses the Piano roll's grid, and one step when that is free too |
| 5 | tooltips / hint | Loop record described Song mode only | they describe Pattern mode too (3 strings, 15 languages) |

Code:
- core/RecordLoop.h: `patternLinear`, `patternOrigin`.
- core/LiveInput.h: `patternOrigin` atomic.
- core/Scheduler: `schedulePatternLoop(..., linearOrigin)`, which emits once from the origin with the same sample
  maths, and plays notes past the old length too.
- core/RackRenderer: passes the origin.
- core/Recorder: `NoteRecorder::setPatternOrigin`, so notes are placed at `tick - origin`.
- engine/AudioEngine: `beginTake(at)` sets the origin, it is cleared when the take ends, plus `patternLinearOrigin()`.
- core/TransportEdit `patternPlayhead(..., linearOrigin)`.
- ui/PianoRollComponent: playhead in Step edit and during a linear take.
- core/StepEdit `stepLength`; ui/MainComponent: main snap step, second compact level.

Tests (tests/Toolbar170eTests.inc), toolbar170e5_*:
- the rule and origin;
- the pattern plays once from the origin (no repeat, same sample, late notes play);
- a take grows a 1-bar pattern to 3 bars (and folds with the switch on);
- the playhead runs on;
- the step is the main snap.

Not done / limits:
- Audio takes in Pattern mode keep looping the pattern (only note takes are linear). FL's play end is global, but
  LYNX's Pattern-mode audio recording is a separate path.
- A pattern with a fixed length (not auto) keeps its length. The notes past it are kept, but they play only during
  such a take.

## Where I stopped

See the round report.

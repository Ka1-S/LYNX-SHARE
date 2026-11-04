# Round 170e2 (toolbar170e2): a bigger toolbar review, as a producer coming from FL

User (2026-11-04, after compare-170e.png): "faz uma revisão maior da barra de ferramentas" — the whole top bar and every
window header, as an FL producer would use them: clear icons + tooltips, grouping that reads at a glance, FL users
find things where they expect (recording toggles, pattern/song, tempo tap, master volume/pitch), customisation stays,
1366 px to 4K, light and dark. Familiar, not a clone (LYNX's own icon style and layout stay). Builds on round 170e
(docs/analise/REA_rodada170e.md).

## Where I stopped

- Waiting for toolbar170e to land, then merge origin/main and queue `train.sh toolbar170e2`.
- Done: recording panel (4 x 2), Blend and Tempo tapper buttons, priorities, filter / arrangement icons, 1366 and 4K
  snapshots, light-theme snapshot run, comparison sheet.
- Rerun: `tmp/coord/buildslot.sh tmp/coord/bl-wt.sh toolbar170e2`; Windows: `OUT=~/projetos/lynx-wt-toolbar170e2/build-win
  tmp/coord/winbuild.sh scripts/build-windows-zig.sh`; pictures `LYNX.exe --snapshot` (fresh LYNX_PROFILE_DIR,
  LYNX_TEST_MODE=1; `LYNX_SNAPSHOT_THEME=light.lynxtheme` for the light run) into /mnt/e/lynx-work/toolbar170e2.

## FL evidence (FLEngine_x64.dll DFM `TToolbarForm`, read-only; tmp/r170e/toolbarform.dfm.txt)

The toolbar's button row (Left order): MetronomeBtn "Metronome" (Ctrl+M), StartOnInputBtn "Start on input" (Ctrl+I),
PrecountBtn "Recording precount" (Ctrl+P), BlendRecordBtn "Blend recorded notes" (Ctrl+B), LoopRecordBtn "Loop record",
then the windows (Playlist F5, Piano roll F7, Channel rack F6, Mixer F9, Browser Alt+F8, Project picker, Plugin picker
F8), ViewTTBtn "Tempo tapper" (Ctrl+F5), Touch controller, Undo, Save as, Export, Audio editor, One-click audio
recording, Project info, ... KbToMIDIBtn "Typing keyboard to piano" (Ctrl+T), AutoScrollBtn, StepEditBtn "Step edit"
(Ctrl+E), Stop sound, Multilink, Shift/Ctrl/Alt modifier buttons, Cut/Copy/Paste, Add, ParamCtrlBigBtn "Quick tweak
last tweaked parameter". Panels: Transport, Volume, Pitch, Position, Time, Pattern, Snap, Hint, CPU, Monitor.
Reference shot: C:\Users\kauan\lynx-test\flref\full4.png (FL 2026).

## Per item

| item | FL | LYNX before | LYNX now |
|---|---|---|---|
| recording toggles visible | metronome, start on input, precount, blend, loop record side by side | metronome, count-in, wait for input over what-to-record, typing piano, MIDI light (3 x 2); Overdub / Replace only in the record menu | 4 x 2: metronome, wait for input, count-in, **Blend** over recording options, typing piano, **Tempo tapper**, MIDI light. Blend = FL's "Blend recorded notes" (on = overdub, off = replace; saved like the other recording options) |
| tempo tap | ViewTTBtn on the toolbar | only View / tempo menu | toolbar button (own icon: an arrow tapping a pad), F-key shown in its tip |
| narrow windows (1366) | — | the recording panel dropped before New/Open/Save | the file buttons drop first (they are in File / Edit and on keys); 1366 shows transport, master, recording, snap/pattern, windows |
| Channel Rack filter box | text | "All" only | funnel icon + name |
| Playlist arrangement box | text | "Arrangement 1" only | arrangement icon + name |
| 4K, light | — | not pictured | s-desktop-3840.png / s-toolbar-3840.png, and a light-theme run (after-light/) |

Not done (no LYNX feature behind them yet, or not toolbar work): FL's Loop record and Step edit switches (LYNX has no
such recording modes; punch / pre-roll is LYNX's own), Shift/Ctrl/Alt touch modifier buttons, Cut/Copy/Paste buttons.

## Checks

- build-local: 2,438,225 checks, 0 failures (new test toolbar170e2_recording_options_outlast_the_file_buttons).
- Windows build: ok; `--snapshot` dark and light (fresh profiles, LYNX_TEST_MODE=1) at 1280 / 1366 / 1600 / 1920 / 2560 / 3840.
- Sheet: /mnt/e/lynx-work/toolbar170e2/compare-170e2.png (FL reference strip, before, after 170e, after 170e2 dark / light, 1366, 4K).

# Round 170e3 (toolbar170e3): a real toolbar pass

Coordinator review of compare-170e2.png (2026-11-04): 170e2 looked almost the same as before. Asked for: (1) clear groups
and a sensible use of a 4K width, (2) bigger icons with one stroke, a clear "on" state, tooltips with the shortcut,
(3) the recording switches FL users look for (count-in, loop record, step edit, blend, wait for input, metronome, punch)
side by side, with a small menu for the rest, (4) Pattern/Song, tempo with a tapper, master volume / pitch prominent,
(5) window headers in the same language, (6) light theme, 1366 and 3840 checks. LYNX's own look stays (familiar, not a
clone). Builds on 170e / 170e2.

## Where I stopped

- Waiting for the coordinator's look at /mnt/e/lynx-work/toolbar170e4/compare-170e4.png before landing (do not queue).
- Done: everything below; first sheet /mnt/e/lynx-work/toolbar170e3/compare-170e3.png (FL strip, before = main after 170e2,
  after = this branch; 1920, 1366, light, 3840 in two halves, Piano roll and Playlist headers).
- Rerun: `tmp/coord/buildslot.sh tmp/coord/bl-wt.sh toolbar170e3`; Windows `OUT=~/projetos/lynx-wt-toolbar170e3/build-win
  tmp/coord/winbuild.sh scripts/build-windows-zig.sh`; pictures `LYNX.exe --snapshot` (fresh LYNX_PROFILE_DIR,
  LYNX_TEST_MODE=1; `LYNX_SNAPSHOT_THEME=light.lynxtheme` for light) into /mnt/e/lynx-work/toolbar170e3/after(-light);
  sheet `python3 tmp/r170e/sheet3.py`.
- Not done: Loop record in Pattern mode (FL lets the pattern run on past its end while recording with Loop record off;
  LYNX's Pattern mode always loops the pattern, see below). A visible step-edit cursor in the Piano roll (step edit moves
  the song position, which the Piano roll already draws).

## FL evidence (FLEngine_x64.dll, read-only, clean-room)

- Toolbar buttons [DFM TToolbarForm, tmp/r170e/toolbarform.dfm.txt]: MetronomeBtn, StartOnInputBtn, PrecountBtn,
  BlendRecordBtn, LoopRecordBtn ("Loop record", Action ShortcutsModule.OptionsLoopRecordAction), StepEditBtn
  ("Ctrl+E Step edit"), ViewTTBtn (tempo tapper).
- Loop record [DECOMP 0xf1e2a0 OptionsLoopRecordActionExecute]: toggles one global byte (PTR 0x15e0c58).
  Readers [Refs.java → tmp/r170e/looprefs.txt]: FUN_011d4800 sets the play end to 0x7fffffff while recording
  (`0 < recording && loopRecord == 0`), so the song / pattern plays on; FUN_0126c040 names the takes
  "recorded items (loop %d)" only when it is on (one group per pass); FUN_0126bdf0 / FUN_011ca830 take the position
  back to the end when recording stops with it off.
- Step edit [DECOMP 0xf1e740 OptionsStepEditActionExecute]: hands the checked state to the editor object (vtable +0xb0);
  behaviour as recorded in REA_rodada169k #4 (stopped: played notes typed at the position, which moves on by the snap).
- Blend recorded notes = Ctrl+B, Step edit = Ctrl+E [DFM ShortcutsModule, round 167j table].

## Per item

| # | asked | LYNX before (170e2) | now |
|---|---|---|---|
| 1 | groups at a glance | hairlines between the panels | each panel (transport, master, recording, snap & pattern, windows, file, meters) in its own quiet well; "Separators" off = hairlines as before |
| 1 | 4K at 100 % | a ~1700 px hole before the meters | medium icons grow to large on a window >= 2400 px; the hint bar takes up to 1000 px; up to 28 px more air per group; the output monitor grows; what is left trails after the meters instead of opening a hole |
| 1 | 1366 / 1280 | the recording panel dropped | a compact layout first (4 switches per row, typing piano and MIDI light out, tighter gaps and displays): transport, master, recording, snap / pattern and windows stay |
| 2 | bigger icons | 16 px on 28 px rows | 19 px on 31 px rows (small 15/26, large 22/37); editor tool strips 18 px (were 16); one stroke as before |
| 2 | clear "on" | accent wash 16 % | 24 % wash + accent icon |
| 2 | tooltips with keys | most | the new switches too: Blend (Ctrl+B), Step edit (Ctrl+E), Loop record, Punch (keys assignable) |
| 3 | recording switches | metronome, wait, count-in, blend, menu, typing piano, tapper, MIDI light | **metronome, wait for input, count-in, blend, typing piano / loop record, step edit, punch, recording options menu, MIDI light** (5 x 2) |
| 3 | Loop record | only implicit (a loop region always looped) | `RecordOptions::loopRecord` (saved, on by default = as before). Off: Song-mode recording plays straight through the loop region, notes and audio stay where they were played (core/RecordLoop.h) |
| 3 | Step edit | missing | Ctrl+E / toolbar / record menu: with the transport stopped, MIDI and typing-keyboard notes go into the current pattern at the position, a Piano-roll snap step long; chords on one step; the position moves on when every key is up (core/StepEdit.h, engine queue from the score logger) |
| 3 | Punch | in the record menu only | a toolbar switch (same option) |
| 4 | tempo tapper | a button in the recording grid | beside the BPM display |
| 5 | headers | tool strips loose in the title row | the tools sit in one well, bigger icons; snap boxes have the magnet (170e) |

## Second pass (coordinator review of compare-170e3.png)

| asked | now |
|---|---|
| 4K: no dead zone | the spare width goes to the output monitor (40 %, up to 900 px), the snap / pattern boxes (20 %, up to 220), the song position (15 %, up to 120) and the hint bar (25 %, up to 500), then evenly between the panels (no cap): nothing is left at the right end |
| editor headers | title rows 32 px (were 28), tool icons 20 px (were 16), the tools in three wells: drawing (pencil, brush, drum brush) / changing (delete, mute, knife, slip) / looking (select, zoom, listen); the tool in use has a 30 % red wash; tooltips name the tool and its key |
| transport the visual centre | play / stop / record 45 px buttons (were 37 x 34), the song position 26 px digits in a 140 px display (were 17 px / 110), shrinking to fit when the toolbar is compact |
| Pattern / Song | drawn as one switch of two halves (a shared outlined well, the playing mode lit) |
| master knobs | two big knobs side by side, name and value under each (compact toolbar: the small knobs over each other) |
| one-line hint | the idle hint is "Point at anything for help [F1] shortcuts [F5]-[F9] windows [Space] play/stop" (the typing piano's state shows on its button) |

Sheet: /mnt/e/lynx-work/toolbar170e4/compare-170e4.png (+ full-1920-*.png, full-3840-*.png).

## Checks

- build-local: see the round report (tests toolbar170e3_*: loop record rule and a second pass kept in place, step edit
  writer, Ctrl+E / Ctrl+B without conflicts, icon sizes and wide windows).
- Windows build + `--snapshot` dark and light at 1280 / 1366 / 1600 / 1920 / 2560 / 3840.

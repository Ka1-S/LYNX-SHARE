# REA round 168t2: typing keyboard to piano, measured in FL 2026 (issue #34 follow-up)

Sources: FL's own DFM resources (FLEngine_x64.dll, dump in /mnt/e/lynx-work/r167j/dfm-all.txt) and one FL 2026
session (launched once under the wingui + fl locks, registry backed up and restored, closed by its PID). Input was
sent with SendInput and scan codes (keybd_event Ctrl chords did not reach JUCE or FL).

## DFM

- `ShortcutsModule/OptionsTypingKbToPianoAction`: Caption "Typing keyboard to piano", Checked = True, ShortCut 16468 (Ctrl+T).
- `FruityLoopsMainForm/MainMenu/OptionsMenu/KBToMIDIMenu`: the action, in Options › Switches before Metronome.
- `ToolbarForm/BottomToolbar/KbToMIDIBtn` (Tag 105), hint "Ctrl+T Typing keyboard to piano".
- `ToolbarForm/ToolbarRightMenu/KbToMIDIMenuItem` (Tag 105): one entry of a menu holding every toolbar button by Tag
  (metronome, start on input, the window buttons...). A right-click on the toolbar background did not open it (that
  shows the toolbar options: Edit, Separators, Hint bar, presets) nor did one on the button, so it is most likely the
  list of buttons that do not fit (not proven).
- `FruityLoopsMainForm/KbToMIDIPopup`: "-Root Note" KeyOfs0..11 (Tag 0..11), "-Octave" KbC2..KbC6 (Tag -2..2, KbC4 = 0
  is the default, hints "Starts at octave N"), "-Options" Reset ("Reset the options to their default values"),
  "-Layout" (filled at run time).
- `FruityLoopsMainForm/EEMenu/EEMarkerMainMenu/EEMarkerAddAutoMenu`: "Add auto", ShortCut 16468 (Ctrl+T) as well.

## Measured in FL

| Test | FL 2026 |
|---|---|
| Fresh start | Options › Typing keyboard to piano is checked, the toolbar button lit |
| Playlist focused, Ctrl+T | an "Auto" time marker appears at the start; the typing keyboard stays on |
| Typing on, Playlist focused, P then E | hint bar "Note off: E6", "Note off: E5"; the tool stays the same |
| Typing on, Piano roll focused, K | nothing (no note, the channel stays 808 Kick) |
| Typing on, Piano roll, R | "Note off: F5" (a note, not record) |
| Typing off, Piano roll, K | next channel (808 Kick -> 808 Clap) |
| Right-click on the button | Root Note C..B (C checked), Octave 2..6 (4 checked), Options: Reset, Layout: Layout FPC (C4), Layout Janko, Layout Performance mode, Layout Piano (checked), Layout Slicex (C4), then scales (Major, Major + Chords, ..., Other Whole Tone) |

So while the typing keyboard is on, FL gives every plain letter to it (note keys play, K did nothing); Ctrl/Alt
chords and F-keys still work. With FL's names (C5 = MIDI 60): Q = C5, E = E5, R = F5, P = E6, so the lower row
Z starts at C4 = MIDI 48 at the default octave 4.

## LYNX before / after

- Default: off -> on (as FL). Saved settings keep a user's own choice.
- Notes: Z = 60 (an octave above FL) -> Z = 48 at octave 4, moved by Root note and Octave.
- Right-click on the button: nothing -> Root note, Octave, Options › Reset, Layout (Piano only).
- Plain letters while it is on: already swallowed like FL (KeyMap: only `whilePiano` actions).
- Ctrl+T with the Playlist focused: already "Add auto" like FL (Playlist context wins); elsewhere the toggle.

## Left

- FL's other layouts (FPC, Janko, Performance mode, Slicex, the scale layouts) are not in LYNX.
- `L` (pattern/song) is marked `whilePiano` in LYNX; FL was not measured for it in this session.

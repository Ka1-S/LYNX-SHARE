# REA round 169f: contributor PRs #57, #58, #59 (LordVersA) checked against FL 2026

Sources: FL 2026's own FLEngine_x64.dll (Ghidra project ~/ghproj/r165i "fle.dll", headless decompiles with its
DecAt / Xr / StrRefs helpers; Delphi published-method table read for the handler addresses), its DFM dump
(/mnt/e/lynx-work/r167j/dfm-all.txt) and FL's `System/Config/Typing to piano/*.scr` files (read as data, not shipped).
FL session: one, 2026-11-03 20:45-20:48 (wingui + fl locks, registry backed up and restored, FL closed by its
PID 7160, nothing saved). Shots in `C:\Users\kauan\lynx-test\r169f\` (s*.png full screen, c*.png crops).

## PR #57 — typing keyboard layouts

Reporter's words (PR): "Layout Piano stays LYNX's current two rows ... L is LYNX's whilePiano pattern/song key";
"Layout Performance mode is left out because it launches Playlist clips"; "FREE vs LOCK: both are treated as no
note"; "I haven't checked in FL whether Root note / Octave also shift the FPC/Slicex layouts".

FL evidence:
- FUN_01182670 (loads the layout): fills the key table with -2 (memset 0xfe), then reads `<layout name>.scr` from
  `System\Config\Typing to piano` line by line into the slots of scan codes 2..0x35 (up to 10 notes per line). The
  default name is "Layout Piano" (FUN_0121ac70 / FUN_01182670 fall back to it), so FL's Piano layout IS the file.
- FUN_00661f70 (parses one line): "FREE" = -2, "LOCK" = -1, a number = that note, a note name = name + 12 x octave
  (C5 = 60); several separated by commas = a chord.
- FUN_011828c0 (key down): for each note of the key's slot, note = table + root (KeyOfs, 0x16bcbac) + 12 x octave
  offset (KbC2..KbC6 = -2..2, 0x16bcba8), kept when 0..127, played with FUN_01181620 (note, velocity 100, channel 0,
  port -1); a per-note count (0x16bd11c) retriggers a note already held. It returns "handled" for any slot that is not
  FREE (so LOCK and notes swallow the key, FREE leaves it to the shortcuts). FUN_01182a20 (key up) mirrors it.
  So Root note and Octave move every layout (FPC and Slicex included).
- FUN_01181620 (the MIDI note entry point, also the MIDI input's): when the channel (0 = MIDI channel 1 for the typing
  keyboard) is the "MIDI channel used for playlist live mode" (0x15c26ac, -1 = off) and Performance mode is on in Song
  mode, the note launches track note / 12, column note % 12 (FUN_01221c00), 120..127 being the offset / modifier keys
  (as LYNX's perf::MidiMapping, round 165m). Layout Performance mode's numbers (0..4, 48..52, 12..16, ...) are exactly
  those launch notes: tracks 1..8, columns 1..5.
- KbToMIDIResetClick 0x121ac70: releases the keys, Root note C, Octave 4 and Layout "Layout Piano".
- FL's Layout menu (shot c3.png): Layout FPC (C4), Layout Janko, Layout Performance mode, Layout Piano (checked),
  Layout Slicex (C4), Major, Major + Chords, Major Bebop, ... Other Whole Tone (by name: "Major" first).
- FL session: typing keyboard on, Layout Piano, `L` pressed -> hint "Note off: C#5 / velocity: 100", PAT stays (c2.png).

LYNX before (PR as submitted): Piano = LYNX's two rows; Performance mode missing; FREE and LOCK alike (every plain key
swallowed); Reset kept the layout; menu order "Major + Chords" ... "Major" (file order with the extension).

LYNX now: every layout is FL's file as data (Piano too: L ; , . / [ ] = play, 1 4 8 - F K ' are LOCK, A ` \ FREE);
LOCK keys are swallowed, FREE keys reach the shortcuts as with the typing keyboard off; Root note / Octave move every
layout; Layout Performance mode is listed and its notes (any layout's) launch clips when the playlist live mode
channel is 1 and Performance mode is on, else they play the selected channels; Reset puts Layout Piano back; menu in
FL's order. `L` (pattern / song) is now a note while the typing keyboard is on with Layout Piano, as in FL.

## PR #58 — Mute removed steps

Reporter's words (PR): "FL's exact rule for clicking a step that is muted while the option is off isn't measured.
This PR unmutes it"; "It is saved as a UI setting ... Whether FL saves it per project hasn't been checked."

FL evidence:
- SSMuteRemovedStepsMenuClick 0x120ce20 toggles MainForm+0x351c; FUN_0119fad0's settings list saves it as
  "MuteRemovedSteps" with TimeDisp_Beat, FlatPluginMenu, ChanRackMixerTrk... (a global setting, not the project);
  FUN_011c4xxx resets it to 0 (off by default). In SSMenu it is in the "Options" part.
- FUN_012d4140 (SetStep: channel, step, on): goes over the channel's notes in the step's cell. Option on: on clears the
  muted bit (0x20 of the note's flags byte), off sets it, for every note there; with none there it adds a note (muted
  when turning off). Option off: off deletes every note there (muted ones too); on leaves them alone and adds a new
  note (velocity 100). FUN_012d4330 (painting steps) does the same.

LYNX before (PR as submitted): option off + a muted step clicked = unmuted; option off + a step holding a muted and an
unmuted note turned off = the first note found toggled (could unmute instead of delete).

- FL session: option on, step 1 of 808 Kick on then off -> it draws exactly like an empty step (c6.png); option off,
  step clicked again -> lit (c7.png), and the Piano roll then holds TWO notes there: dragging the top one away left the
  other at the step (c10.png). So a muted note is drawn as an off step, and option off adds a new note.

LYNX now: Pattern::toggleStep follows FL's SetStep (option on: mute / unmute all the step's notes; option off: delete
them all / add a new note). The item is under an "Options" header like FL's. A muted note no
longer draws its step in the "note off the grid" colour (cellHasNote skips muted notes): off, as in FL.

## PR #59 — Piano roll play button

Reporter's words (PR): "Right-click while playing stops playback and goes back to where it last started ... Left-click
while the song is playing pauses it (it does not switch to Pattern mode mid-play). That case wasn't measured in FL."

FL evidence:
- EEPlayBtnClick 0xe4bd40: sets the Pattern / Song switch (MainForm+0x770) to Pattern (unless the editor's mode at
  +0xb18 is 2), whether playing or not, then sets the toolbar play button (MainForm+0x790) to the roll button's own
  state: the transport's play / pause. The Channel Rack's SSPlayBtnClick 0x1041cc0 is the same with Pattern always.
- EEPlayBtnMouseDown 0xe4bda0: right button -> FUN_011ca520 (0, 0), the very call of the toolbar's StopBtnClick
  0x1030470 and of SSPlayBtnMouseDown 0x1041d10: the right-click is the transport's Stop.

- FL session: Pattern mode, a click on the Piano roll ruler at bar 2 step 9, Play, Stop -> 2:09:01 (where playback
  started), Stop again -> 1:01:00 (c1416.png, c1718.png). The play button itself was not visible in this FL's Piano roll
  toolbar (maximized), so it was not clicked; its handlers are proven by the decompile above.

LYNX before (PR as submitted): click in Song mode while playing paused without switching to Pattern mode.

LYNX now: click = Pattern mode (also while the song plays), then play / pause; the right-click keeps the PR's rule
(back to where playback started, again: to the start), which is FL's Stop as measured.

Follow-up (not in these PRs): LYNX's own Stop button, in Pattern mode, goes to the pattern start on the first press
(transportedit::stopTarget), while FL's goes back to where playback started first (measured above).

## LYNX check (Windows, the three PRs together, fresh profile, test mode)

Typing menu: the 31 layouts in FL's order, Layout Piano checked; L plays a note and PAT stays. Channel Rack options:
"OPTIONS > Mute removed steps"; with it on a clicked-off step draws off. Piano roll: in Song mode the roll's play
button switched to Pattern and played; right-click stopped it at 1:1:1 (shots l4..l8).

## Where I stopped

- Done: REA, one FL session, fixes on feature/pr57/58/59, build-local of each, the Windows check above.
- Next: coord/train.sh pr57 pr58 pr59, then the Stop follow-up as its own round.

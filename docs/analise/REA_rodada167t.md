# Round 167t (zoom167t): issue #26 — Piano roll vertical zoom with Ctrl + middle button + drag

matdinu (issue #26, "piano roll vertical zoom"): "vertically zoom in with ctrl + scroll button + drag up / down — to see
the notes better".

## Where I stopped

- Done: FL measured statically (REA/Ghidra on FLEngine_x64.dll, no FL launch), implemented in the Piano roll and the
  Playlist, tests in tests/Zoom167tTests.inc.
- Windows build (OUT inside Linux: an OUT on NTFS breaks the case-sensitive Dbghelp.h shim) checked by hand (3).
- build-local with FL Data: 1890888 checks, only the embedded i18n table was stale (regenerated). Landing via land.sh.
- Rerun: `tmp/coord/buildslot.sh tmp/coord/bl-wt.sh zoom167t`; Windows: `OUT=/mnt/e/lynx-work/zoom167t/build-win
  tmp/coord/winbuild.sh scripts/build-windows-zig.sh`. Ghidra project copy: /mnt/e/lynx-work/zoom167t (flea, fle.dll);
  scripts and decompiles in tmp/zoom167t (g.sh, d1..d10.c, py/meth.py, fields.py, dyn.py, props.py, dfm.py).

## 1. FL 2026 evidence [DECOMP] [DFM] [RTTI] [WHATSNEW]

FL Studio 2026 (FLEngine_x64.dll of 2026-10-08). FL was not launched: every point below comes from the binary.

### 1.1 Which code handles the editors' mouse

- RTTI: the Piano roll, the Playlist and the event editor are one form class, `TEventEditForm` (VMT 0xe2f2f0). Its
  published methods include `PianoPanelMouseDown` 0xe5cd20, `EventPanelMouseDown` 0xe4bdd0, `TimePanelMouseDown`
  0xe56bf0, `MIDIKbMouseDown` 0xe84110, `FormMouseWheel` 0xe82d20, `VZoomSelectChange` 0xe83a50,
  `VZoomSelectRelease` 0xe4fcc0.
- DFM `TEventEditForm`: `TimePanel` = the ruler (TQuickPaintBox, 21 px high, OnMouseDown = TimePanelMouseDown);
  `TimeScroller` = the time scroll bar (TQuickScroller, Options nbSlideKnob + nbCanSelect, MoveSpeed 0.01);
  `VZoomSelect` = the vertical zoom knob (TPaintWheel, Min 1, Max 50, MoveSpeed 4, SlowMotionDiv 4, cursor crSizeNS,
  hint "Change clip size" in the Playlist).
- The note grid is a `TEventPanel_PR` created at run time; the Playlist's clip grid is `TEventPanel_AU` and the picker
  `TEventPanel_Picker`, both subclasses of `TEventPanel_PR`.

### 1.2 Middle button on the grid: FUN_00e4bb50 (called by PianoPanelMouseDown / EventPanelMouseDown when Button = mbMiddle)

| modifiers at the press | FL does |
|---|---|
| none | hand pan in both axes (panel mode 3, cursor 0x18) |
| Ctrl (or a press within the double-click time after the last Alt / double middle press) | only on a `TEventPanel_PR` (the Piano roll notes, the Playlist clips, the picker): stores the key (Playlist: the track) under the mouse in `VZoomSelect.Tag` (virtual 0x238 = y to key) and hands the drag to `VZoomSelect` (FUN_00818950 sends a left button up to the panel and a left button down to the knob). In the event lane: nothing |
| Alt, or a double middle-click | hands the press to `TimeScroller` (FUN_006efe40, button 2) = the scroll bar's middle-drag zoom (1.4) |

The ruler (`TimePanelMouseDown`) hands every middle press to `TimeScroller` the same way. The keyboard
(`MIDIKbMouseDown`, Piano roll only) does the Ctrl branch on a plain middle press: middle-drag on the keyboard = the
same vertical zoom.

### 1.3 The vertical zoom knob drag (TCustomWheel, VMT 0x7c0680)

- MouseDown 0x7cca60 (dynamic -44) hides the pointer and lets it travel without limits (FUN_007f4ca0: ShowCursor(0),
  wrap at the screen edges with SetPhysicalCursorPos); MouseMove 0x7cd350 (dynamic -45) reads the travel since the
  last move (FUN_007f4ec0: previous - current), divided by the UI scale. The constructor (0x7cb850) sets the knob
  vertical (byte +0x458 = 1), so only the vertical travel counts, up = positive; the horizontal one only drives a
  `LinkedWheel`, which VZoomSelect has not.
- MoveBy 0x7cc1d0: travel x MoveSpeed (4), x SlowMotionDiv (4) unless the shift state hits the slow-motion mask
  (+0x478 = 0x14 = ssCtrl | ssRight, set by FUN_007cbd00 for MouseButton = mbLeft); the sum is kept in +0x448 (not
  clamped) and value = Min + round (sum / (SlowMotionDiv x 16)); SetValue 0x7cd870 clamps to Min..Max.
  So: one zoom level per 4 px of travel, per 16 px while Ctrl is held (and Ctrl IS held during Ctrl+middle-drag
  unless it is released), and going past a limit needs the travel back before it moves again.
- VZoomSelectChange 0xe83a50 -> FUN_00e83850: key height = 16 + 2 x level px (`DAT_0152c3b4` = 16, never written;
  levels 1..50 = 18..116 px), hint "Zoom level %d". The anchor: in the Piano roll the centre of the key stored in Tag
  (FUN_00e8dd80 (Tag + 0.5)) is kept at the same screen y (the vertical scroller is moved by new y - old y); in the
  Playlist the top of the track under the mouse (virtual 0x240 of TEventPanel_AU). Without a Tag (wheel / drag on the
  knob itself) the anchor is the middle of the notes in view, or the middle of the view. VZoomSelectRelease resets
  Tag. The level is remembered for the next Piano roll (`DAT_0152c428`).

### 1.4 The time scroll bar's middle-drag (TQuickScroller, VMT 0x6eb208)

- MouseDown 0x6efaa0 with mbMiddle: mode 2; with no LinkedScroller (TimeScroller has none; the only writer of +0x6ac
  is FUN_0079da80, another control) the span (+0x598) and the centre (+0x590) at the press are kept.
- MouseMove 0x6ef410, mode 2, scroller with nbCanSelect (enum order nbInteger, nbCanSelect, ... -> bit 2): the
  vertical travel of each move is clamped to +-32 px and summed; span = span at the press x 2^(sum x MoveSpeed) with
  MoveSpeed 0.01 (100 px up = half the span = twice the zoom), centred on the centre; unless Ctrl is held the
  horizontal travel moves the centre by min (half the span, range / 4) x 0.01 per px (rightwards = later).

### 1.5 Wheel (FormMouseWheel 0xe82d20) — for completeness

Over the notes with Alt: the property of the notes under / nearest the mouse (FUN_00e82270, "increase %s"); Ctrl:
time zoom at the mouse; Shift: horizontal scroll; plain: vertical scroll; right button held: next / previous tool;
Alt+wheel = vertical zoom only in the Playlist (`+0xb18 == 2`). The Piano roll has no wheel gesture for the vertical
zoom other than the wheel over the zoom knob itself.

WhatsNew.htm: 11.0.3 "New: support for ctrl+middle mouse button zoom in Piano roll/Playlist"; 9.7 "Middle click on
the Piano roll keyboard to resize it", "Improved zooming from side keyboard in Piano roll".

## 2. Per item

| reporter's words | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| Ctrl + middle button + drag up/down = vertical zoom of the Piano roll | 1.2, 1.3 | Ctrl+middle-drag panned like a plain middle-drag; vertical zoom only by middle-drag on the keyboard (8 px of travel per px of key height), Ctrl+Alt+wheel and View › Zoom; keys 6..32 px | Ctrl+middle-drag on the notes = vertical zoom: vertical travel only, up = taller, 1 level (2 px) per 16 px while Ctrl is held, per 4 px without; the centre of the key under the mouse stays put; pointer hidden and unbounded during the drag; keys 6..116 px (FL 18..116, LYNX keeps its shorter keys) |
| (same gesture in FL's Playlist) | 1.2 (TEventPanel_AU inherits TEventPanel_PR) | Ctrl+middle-drag panned | Ctrl+middle-drag on the clips = track height zoom around the top of the track under the mouse, same speeds |
| (keyboard middle-drag) | 1.2, 1.3 (normal speed) | 8 px per px | FL's speed: 4 px per level, Ctrl = fine; anchor = the key's centre |
| (middle-drag on the ruler, Alt+middle-drag on the grid) | 1.2, 1.4 | panned | time zoom: up = closer (100 px = x2) around the view's centre, sideways scrolls (not with Ctrl); Piano roll and Playlist |
| (Ctrl+Alt+wheel, View › Zoom › Taller / Shorter keys) | not FL (1.5: Alt+wheel over the notes edits them, which LYNX already does first) | 6..32 px | LYNX-own, kept, now up to 116 px |

## 3. Windows check (real mouse, fresh profile, test mode)

LYNX.exe of this branch on the --showcase demo, Windows 11 at 1920x1080, SetCursorPos + mouse_event middle button +
keybd_event Ctrl/Alt (C:\Users\kauan\lynx-test\zoom167t\drag.ps1, shots g0..g9.png there):

| step | result |
|---|---|
| Piano roll (Reese Bass), keys 14 px; Ctrl+middle-drag on an empty cell of the C3 row, 160 px up (g1 -> g2) | keys 34 px (10 levels, fine speed), C3 still at y 386, notes readable with their names |
| middle-drag on the keyboard at C3, 40 px down (g3) | keys back to 14 px (normal speed: 4 px per level), C3 at y 386 |
| middle-drag on the ruler, 100 px up (g4) | one bar 318 -> 636 px, bar 3 (the view's centre) stays at x 1121 |
| Playlist: Ctrl+middle-drag on Bass, 160 px up (g5 -> g6) | tracks 38 -> 58 px; the Bass top moved 271 -> 297 because the 16 short tracks first fit in the view (no room to scroll, as with Alt+wheel) |
| Ctrl+middle-drag on Melody, 64 px down, with room to scroll (g7 -> g8) | tracks 84 -> 80 px, the Melody clip top stays at y 464 |
| close (WM_CLOSE) | closed normally, desktop clean (g9) |

Not done: FL's double middle-click (time zoom drag) and the "press shortly after a double middle-click counts as Ctrl"
detail; FL's anchor without a Tag (the middle of the notes in view) since LYNX has no zoom knob to drag.

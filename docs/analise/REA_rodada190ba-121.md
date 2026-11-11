# REA round 190ba, issue #121: custom sprite sheets for LYNX Dance (FL's Fruity Dance format)

Issue #121 asks for loading "Fruity Dance spritesheets" (a PNG with a same-named txt file, as in FL's own tutorial).
Before this round LYNX Dance always drew LYNX's own character (REA_rodada165e §6: "no picture-file dancers"); round
188j only changed how the dancers are put on screen. This round reads how FL loads and plays a sheet and gives LYNX
Dance the same format. No FL artwork is copied: LYNX's own lynx stays the default dancer, tests and shots use sheets
made by LYNX.

Evidence levels: **[DECOMP]** read in `Fruity dance_x64.dll` (FL Studio 2026, Ghidra project r68_dance, headless,
read-only), **[DATA]** files of the FL installation or the plugin's form resource, **[MANUAL]** FL's online manual
(Fruity Dance page), **[CODE]** LYNX.

## 1. FL's format

- There is no `.flam` or other container: a sheet is a picture file plus a text file. **[DATA]** the installation has
  `Plugins/Fruity/Generators/Fruity Dance/Artwork/Dance.png` (1760 x 2560, RGBA) + `Dance.txt` (ten lines: Waiting,
  Stepping, Jumping, Zombie, Waving, Hula, Windmill, Zitabata, Dervish, Held; CR LF between them, none after the last)
  and `Count.png` (800 x 100) + `Count.txt` ("Count").
- **[DECOMP]** `FUN_007179c0` loads a sheet. The sheet object is made (`FUN_00717880`) with **8 columns** (a
  constant). The names file is the picture's path with `.txt` in place of the extension; when it does not exist FL
  reads its own `Dance.txt`. Rows = the number of lines, at least 1. Frame = (width / 8) x (height / rows) in whole
  pixels; a picture narrower than 8 or lower than `rows` pixels has its canvas enlarged first. The load fails when
  `width mod frameWidth != 0` or `height mod frameHeight != 0`: `FUN_00717d10` then loads FL's own sheet instead and
  `FUN_0071a410` shows **"Error|Bitmap size is incorrect."** On displays scaled below 100 % the picture is resampled.
- **[MANUAL]** "fixed at 8 cells wide", "each row is a separate 8 cell animation sequence", "the last row is always
  the 'held by mouse' animation", the text file "must contain a name for each row", PNG with transparency (JPG, GIF
  and BMP load too).
- **[DECOMP]** Moves (`FUN_0071a410`): the move list is the names without the last one, with "Freestyle" put first;
  the move parameter is clamped to `rows - 1`; value 0 = Freestyle, value v = row v - 1. The key table is filled for
  the sheet: keys 59 ... 59 + rows - 1 (at most 127) = values 0 ..., keys 48 ... 48 - (rows - 1) (at least 0) = the
  same mirrored, then 53 = hide and 54 = show.
- **[DECOMP]** Animation (`FUN_0071b290`, the plugin's idle call; `FUN_00716980`, the paint):
  - phase = frac (host time x speed); column = floor (phase x 8); speed = 0.5 x (1 + |v| x 3 / 96) or 0.5 divided by
    that (the Speed slider, -96 ... 96).
  - The shown row is set when the phase wraps (a cycle begins) or, with "Sync changes" off and a move other than
    Freestyle, at once: host not playing (the flag set by dispatcher id 12, the plugin SDK's `FPD_SetPlaying`) = row 0; Freestyle = a random row
    `Random (rows - 2) + 1` (the second ... the one before the last; 0 with fewer than three rows), drawn again at
    every wrap; else row = value - 1. "Mirror horizontally" negates the row: the picture is flipped left-right.
  - Blend: parameter 3 / 256 (default 64 = 0.25) is the part at the end of each frame that cross-fades to the next
    frame (alpha in steps of 1/255); the next frame after column 7 is column 0 of the row that is about to start.
    **Parameter 3 is this "Blend" slider (form resource: `AnimBlurWheel`, hint "Motion blending"), not a size**:
    LYNX read it as a scale until now (corrected, §3).
  - While the dancer is dragged with the mouse the last row is shown, unmirrored, with blend 0.7.
  - The window is the frame's size with its bottom centre at the saved x, y; a "Reflect" slider adds a faded mirror
    image below (or a bevel when negative), default 64.
- **[DECOMP]** State (`FUN_00719ef0`): int version (2 when saved); 7 ints (move, mirrored, shown, blend, speed, x, y);
  int reflection; 4 bytes (Sync changes, Keep in front, On desktop, Solid); version 2 then has the sheet's file name
  (`FUN_00714f60` / `FUN_007150a0`): a length byte (255 = an int with the length follows) and the characters, control
  characters at the end dropped; empty = FL's own sheet. The name goes through a host call on load and another on save
  (host ids 14 and 28, taken to be FL's path translation) - what FL writes for a file inside its own folders was
  not captured (see "Not verified").
- **[DATA] + [DECOMP]** Where the user loads a sheet: the editor's `OpenBtn`, a folder-glyph button with the hint
  **"Load sprite sheet..."** (`FUN_0071c2c0`: an open-picture dialog starting at the current file). A picture file
  (.PNG, .BMP, .JPG) dropped on the plugin (`FPD_LoadFile`, id 18) loads the same way; `FPD_CollectFile` (15) reports
  the file for zipped projects.

## 2. What LYNX does now [CODE]

- `core/FruityDance.{h,cpp}` (no JUCE): `sheetNames`, `sheetNamesPath`, `sheetLayout` (FL's size rule),
  `sheetMoveNames` / `sheetMoveCount` / `sheetHeldRow`, `actionForKey (key, moves)` (the key table for any sheet) and
  `SheetAnimator` (the row / column / cross-fade rules above; its random rows are seeded per channel, so a project
  always dances the same). `Settings` gains `sheet` (the picture's path) and `blend`; LYNX's text state writes them
  only when used, so existing projects' states are unchanged.
- `ui/DanceView.{h,cpp}`: `dancer::sheetFor` reads the picture (PNG, JPEG, GIF) and its names file (FL's ten names
  when there is none), refuses a picture that fails FL's size rule, and reads the files again when they change (a
  sheet being drawn updates on screen within a second). A channel with a usable sheet shows its frames - on the
  stage, in its own window over docked panels (round 188j) and in the editor's preview - at the picture's pixel size
  times LYNX's Size, bottom centre at the dancer's place; clicks go through the frame's transparent pixels. A missing
  or unusable file falls back to LYNX's dancer (the editor says "Could not read ...").
- Editor: **"Load sprite sheet..."** (FL's button and wording), "LYNX's dancer" (back to the own character; FL has no
  such button because its own dancer is a sheet too), a **Blend** slider (FL's, for sheets), the move list and the key
  help follow the sheet's names. A picture with a wrong size shows FL's message "Bitmap size is incorrect." plus one
  line saying what the size must be. The dancer's right-click menu lists the sheet's moves.
- FL's rules apply to sheet dancers on the stage: the first row while the song is stopped, the chosen move (or
  Freestyle's random rows) while it plays, changes at the start of a cycle, the last row while dragged. The editor's
  preview always plays the chosen move. LYNX's own character keeps its behaviour.
- .flp import: `dance::fromFl` reads version 2's file name (and the blend; the move value is no longer clamped to
  LYNX's eleven moves). .flp export: a dancer edited in LYNX used to be written as LYNX's text state, which FL cannot
  read; `dance::toFl` now writes FL's version 2 state (settings, FL's default display options, the sheet's file).
- `--danceshot prefix` also makes a sheet from LYNX's own poses (`prefix-sheet.png` + `.txt`), loads it back, and
  writes `prefix-sheet-frames.png` (the animation with blending, mirrored, held), `prefix-sheet-editor.png`,
  `prefix-sheet-stage.png` and `prefix-sheetreport.txt` (`sheet_result=OK`).

## 3. Differences from FL, on purpose

- LYNX's default dancer is its own vector character, not a sheet (FL's artwork is not copied).
- A sheet with one row lists only "Freestyle" (FL lists the row's name too, but the value is clamped back to
  Freestyle).
- The Size control scales a sheet (FL has no size control); the outfit colour applies to LYNX's character only.
- Not built: "Sync changes" off, the Reflect slider, Solid off, Keep in front / On desktop (see REA_rodada188j),
  dropping a picture on the channel, BMP files (JUCE has no BMP reader), FL's resampling on displays below 100 %.

## 4. Tests

`tests/Dance190baTests.inc` (three tests, 4008 checks; with the corrected `dance165_state_keys_and_poses` 5655 checks,
0 failures in the round's mini harness against `core/FruityDance.cpp`): names file parsing, FL's two sheets' sizes
and the size rule, the move list and key table for 1 ... 128 rows, state round trips (LYNX text, FL version 1 / 2,
long and cut names, export bytes), and the animation rules (frame order, cross-fade steps, change at the cycle's
start, stopped = first row, Freestyle's range, the held row).

Windows (cloud round build 42979719881 of 707fa909e, "completed success"): `LYNX.exe --danceshot` writes
`sheet_result=OK` - the sheet made from LYNX's poses loads (6 rows, frames 120 x 150), a picture 3 pixels too narrow
and a missing file are refused, `sheet_mirror_flips=1` (2271 of 2271 solid pixels match the flipped frame),
`sheet_body_solid=36/36`, `stage_dancers=3`. Found by these pictures: Windows' renderer drew a negative horizontal
scale unflipped, so a mirrored frame is flipped as a picture of its own; and the contact sheets were saved before
their Graphics was finished (blank files since round 165e).

## 5. Not verified

- FL itself was not run this round (no GUI driving): the rules come from the decompiled code and the manual. A sheet
  made by LYNX's `--danceshot` loads by FL's rule, but was not opened in FL.
- The character set of the file name in FL's state (read as UTF-8, else Latin-1) and what FL's path translation
  writes for a sheet inside FL's own folders: a project saved by FL with a custom sheet would settle both.
- The host time unit behind the phase (LYNX keeps round 165e's "one cycle = two beats at speed 1").

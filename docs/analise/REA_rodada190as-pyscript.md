# REA round 190as: Piano roll scripts (FL's `.pyscript`, module `flpianoroll`)

FL Studio 2026 (`FLEngine_x64.dll`, 22 227 016 bytes, md5 `fce4ea557685c0aeeeeb126013066c25`). Clean-room: behaviour
and formats only; LYNX's code is its own. FL's shipped scripts were read in place and are not in this repository.

**Evidence tags.** [DOC] FL's own text: `System\Config\Piano roll scripts\Piano roll script reference.txt` and the
online manual (`pianoroll_menu.htm`, `pianoroll_scripting_api.htm`, read 2026-11-11). [DATA] strings and tables in the
binary. [CODE] decompiled functions (Ghidra 12.1.4 headless on round 187a's analysed project, read-only; addresses are
image addresses). [EXEC] FL running, screenshots in `flgap190as/shots/`.

Before this round LYNX had the embedded Python (`core/Python.h`, pocketpy), FL's VFX Script and Edison's audio scripts
(`core/EdisonScript.h`, round 167c). Piano roll scripts were missing: 0 hits for `flpianoroll` on origin/main
`cd7b9f206` and in the 0.1.26 queue; the Tools menu had LYNX's own "More tools" dialogs in the place of FL's script
column (round 143, A1).

## 1. Where FL keeps the scripts and how it lists them

| What | FL | Proof |
|---|---|---|
| Factory scripts | `<install>\System\Config\Piano roll scripts\*.pyscript` (5 scripts + the reference text) | [DATA] `System\Config\Piano roll scripts`; the folder |
| User scripts | `Documents\Image-Line\FL Studio\Settings\Piano roll scripts`; subfolders become submenus | [DOC] API page "files" |
| Downloaded | `Documents\Image-Line\Downloads\Piano roll scripts` | [DOC] |
| Menu | Piano roll › Tools (wrench): second column **Scripts**: *Run last script again* (Ctrl+Alt+Y, grey until a script ran), *Open script folder...*, a separator, then the scripts by name | [EXEC] `pyscript-fl-tools-menu.png`; [DOC] menu page |
| Form | the script's own title in the caption with a **Preview** switch (on) and the close cross; the description; the controls in two columns (name, value box, knob / combo); **Reset**, **Regenerate** left, **Accept** right. *Edit script* is in the form's resource but hidden for a factory script | [EXEC] `pyscript-fl-script-dialog.png`; [DATA] `TScriptDialog` resource: `PreviewCheckBox` (hint "Automatic preview when controls are modified", Checked), `OKBtn` "Accept", `EditBtn` "Edit script", `ResetBtn` "Reset", `RegenerateBtn` "Regenerate", theme id `forms.pianorollscriptform` |
| Form memory | values are stored per script under `PRScripts\<script name>` when `restoreFormValues` is set | [CODE] `FUN_00d99bc0` |

## 2. The API surface

Names from the binary's type tables ([DATA], next to `flpianoroll.Pattern`, `flpianoroll.Note`,
`flpianoroll.Marker`, `flpianoroll.TUtils`), which hold more than the reference text:

- **Note** (16 typed members, table at `01502b58`): `number`, `time`, `length`, `group` (int), `pan`, `velocity`,
  `release`, `fcut`, `fres` (float), `color`, `pitchofs`, `repeats` (int), `slide`, `porta`, `muted`, `selected`
  (bool); method `clone`.
- **Marker**: `time`, `mode`, `tsnum`, `tsden`, `scale_root` (int members), `name`, `scale_helper` (properties).
- **score** (type `Pattern`): `getNote`, `addNote`, `deleteNote`, `getMarker`, `addMarker`, `deleteMarker`, `clear`,
  `clearNotes`, `clearMarkers`, `getTimelineSelection`, `setTimelineSelection`, `getDefaultNoteProperties`,
  `getNextFreeGroupIndex`, `loadScoreFromFile` (undocumented); properties `noteCount`, `markerCount`, `length`
  (undocumented), `PPQ`, `tsnum`, `tsden`, `snap_root_note`, `snap_scale_helper`.
- **Utils** (type `TUtils`): `ProgressMsg`, `ShowMessage`, `log`.
- **ScriptDialog**: `AddInput`, `AddInputKnob`, `AddInputKnobInt`, `AddInputCombo`, `AddInputText`,
  `AddInputCheckbox`, `AddInputSurface`, `GetInputValue`, `SetText`, `Execute`, each also with a lower-case first
  letter; `setNormalizedValue`, `addGroup`, `endGroup` (lower case only); `restoreFormValues`.

## 3. What the script sees ([CODE])

- **The score is built from the whole pattern** (`FUN_00c40420`): every note becomes a Python `Note`, but each carries
  a hidden flag (object +0x44) that is set only when the note belongs to the Piano roll's channel **and** (the
  pattern has no selected note **or** the note is selected). `noteCount` / `getNote` go through an index list of the
  flagged notes (`FUN_00c3efe0`, `FUN_00c3f430`, `FUN_00c3f0a0`). So: with a selection the script sees only the
  selected notes of the channel, otherwise all of the channel's notes. The score remembers whether a selection existed
  (+0x20).
- **Notes are live objects.** `getNote(i)` returns the stored object itself (same object on every call); `addNote(n)`
  stores the very object passed in (reference added, no copy: `FUN_00c3f210`), so changes made after `addNote` count.
  FL's own "Note repeater sprinkler" script relies on this.
- `getNote` / `getMarker` with an index out of range return `None` (no exception); `deleteNote` / `deleteMarker` raise
  "Invalid note index" / "Invalid marker index".
- **Members are typed slots** (CPython member table: type codes 1 int, 3 float, 0xe bool): no clamping when set; an
  int member refuses a float and a bool member refuses anything but `True` / `False` (CPython's own rule for such
  members). This is why FL's scripts write `int(...)` everywhere.
- `Note()` (`FUN_00c3ee20`): number 0, time 0, length 0, group 0, pan 0.5, velocity 0.78125 (100 / 128; the text says
  0.8), release 0.5, color 0, fcut = fres = 128 / 255, pitchofs 0, flags false, repeats 0. `Marker()`: time 0, name
  empty, mode 0, tsnum 4, tsden 4.
- A note read from the pattern (`FUN_00c3ed10`): pan, velocity, release = byte / 128; fcut, fres = byte / 255;
  pitchofs = byte − 120; color = low 4 bits; slide / porta / muted / selected from the flag bits; repeats = its byte.
- `clearNotes(all)` (`FUN_00c3f470`): removes the visible notes; without `all`, and when a selection existed at the
  start, only those still selected. `clearMarkers(all)` (`FUN_00c3f520`): all markers, or without `all` the markers
  whose time lies inside the time range of those notes. `clear(all)` = both.
- `getNextFreeGroupIndex` (`FUN_00c3fa20`): the smallest index from 1 not used by **any** note of the pattern,
  visible or not. `length`: the latest note end among the visible notes. `getTimelineSelection`: the Piano roll's
  time selection (end −1 without one, [DOC]). `getDefaultNoteProperties`: the draw tool's current note.
- **Writing back** (`FUN_00c40770`, `FUN_00c40a30`): the pattern's notes are replaced by the score's list. pan,
  velocity and release are stored as value × 128 in a byte, fcut / fres as value × 255, pitchofs as |pitchofs + 120|,
  color & 15, repeats clamped to 0..14, a marker's mode clamped to 0..12. The timeline selection is applied when the
  script changed it.
- **Run** (`FUN_00d99e60`): the file runs as `__main__`. With a `createDialog` function: it is called, must return a
  `ScriptDialog` ("no dialog was created in createDialog" / "createDialog function needs to return a ScriptDialog
  instance"), its `Execute` shows the form, and `apply(form)` runs for the preview and the result; closing the form
  restores the score saved at the start. Without `createDialog` the script just runs. Errors: "Error|There was an
  error in the Python script:". `Utils.ShowMessage` is a modal "Python message" box; the manual warns that calling it
  in `apply()` pops up on every control change.

Not decided by the evidence: which exception class the "Invalid ... index" errors use (LYNX: IndexError), and what
`FUN_0040c660` does with values outside the byte range (LYNX clamps).

## 4. LYNX (this round)

- `core/PianoRollScript.{h,cpp}`: LYNX's own `flpianoroll` module (Python source + a small native host) on the
  embedded interpreter, with the rules of §3: channel / selection visibility, live notes, typed members, FL's
  defaults, FL's storage rounding, `None` / "Invalid note index". `scoreOf()` and `apply()` map a pattern's channel to
  the score and back; LYNX-only note data (chance, velocity range...) survives through the note a script note came
  from (also through `clone()`). Ticks are LYNX's (`score.PPQ` = 960; FL's default is 96 and it goes up to 960).
- `core/ScriptDialog.{h,cpp}`: the ScriptDialog form, shared with Edison's scripts (moved, not rewritten).
- Sandbox: no files, programs, network or modules from disk (pocketpy is built without `os`); a time limit (20 s for a
  run, 5 s for a preview) stops a runaway script. `score.loadScoreFromFile` is refused. FL has neither limit.
- UI (`ui/PianoRollScripts.cpp`, `ui/ScriptPanel.h`): Tools › second column **Scripts** in FL's order (*Run last
  script again* Ctrl+Alt+Y, *Open script folder...*, the scripts): LYNX's own four scripts, *My scripts*
  (`Documents/LYNX/Piano roll scripts`, subfolders as submenus) and FL's three folders when FL is installed (read in
  place). The form is the "Configure script" panel LYNX already had for Edison, with FL's **Accept** and Preview on:
  the preview shows in the Piano roll while the controls move; Accept keeps exactly what the preview shows as one
  undo step; closing the form puts the notes back.
- Differences kept, to review: LYNX's form has one column of sliders where FL has two columns of knobs; the Piano
  roll is not blocked while the form is open; `repeats`, `scale_root` and `scale_helper` are kept for the script but
  not stored (LYNX has no note repeats; scale markers carry no scale data yet); a note's length is at least one tick.
- Tests (`tests/PianoRollScript190Tests.inc`): the reference's example, units and defaults, live notes, selection
  rules, typed members, markers and the timeline selection, the preview style and FL's error texts, timeout, sandbox,
  LYNX's scripts, and FL's five shipped scripts run unmodified from the FL installation (skipped without one).

## 5. Best way?

- **FL:** Python scripts in the Tools menu with an automatic form and live preview (§1–§3).
- **Ableton:** Live 12's MIDI Tools are Max for Live devices (Transformations / Generators) in the clip view, also
  with live preview and an apply step; writing one needs Max, not a text file (`ableton/STUDY.md`, round 190o).
- **Users:** scripting is what keeps people on a DAW (Reaper ~900 API functions, Logic's Scripter; CROSS-DAW F39). FL
  users share `.pyscript` files on Image-Line's Piano roll scripting forum, so running them unmodified is the value.
- **Verdict:** FL-exact API and menu. No alternative proposed; the sandbox and the time limit are LYNX additions that
  FL scripts never notice.

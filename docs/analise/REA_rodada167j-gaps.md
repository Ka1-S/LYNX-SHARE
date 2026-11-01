# Round 167j (v0.1.22) — gap table: FL's menus, shortcuts, editor and mixer menus, render dialog

Companion of `REA_rodada167j.md`. FL side: the DFM menu trees of FL Studio 2026, the ShortcutsModule action lists
(MainMenuActions, ToolbarActions, PlaylistMenuActions: 125 default shortcuts) and the `WAVRenderForm` render dialog,
all read from FL's own binaries (clean-room: captions and keys only). Value: H / M / L; effort: S / M / L.

**Done in round 167j** (FL-exact):
- Piano roll: Shift+Up/Down = a semitone, Ctrl+Up/Down = an octave, Shift+Left/Right = one grid step
  (`noteedit::arrowMove`); Ctrl+D = deselect; Alt+M = mute, Shift+Alt+M = unmute (Ctrl+M is left to the metronome).
- Patterns: Clone = Ctrl+Shift+C (Alt+C kept as a second key), Delete = Ctrl+Shift+Del, "Find first empty" =
  Shift+F4, "Find next empty (no naming)" = Ctrl+F4. The Playlist and Piano roll no longer swallow Ctrl+Shift+C /
  Ctrl+Shift+Del / Ctrl+Shift+Ins, so the pattern keys reach the key map while they have focus.
- File › Export: MP3 = Ctrl+Shift+R, MIDI = Ctrl+Shift+M (Ctrl+R stays on "Last exported format(s)").
- Render dialog: "Enable insert effects".
- Options: Panning law, Play truncated notes on transport, Reset plugins on transport, Select first channel with
  notes when the pattern changes (see `REA_rodada167j.md`).

Sources: FL = extracted DFM text (mainmenu.txt, sc-fl.txt, menus-editors.txt, menus-mixer.txt, dfm-all.txt /WAVRenderForm).
LYNX = worktree lynx-wt-flopt167j: `src/core/KeyMap.cpp` (sc-lynx.txt), `src/ui/MainComponent.cpp` (`getMenuForIndex`, `makePatternMenu`, `addRecordOptions`, `runAction`),
`src/ui/PianoRollComponent.cpp` (`showMoreMenu`, `toolsMenu`, `selectMenu`, `handleKey`), `src/ui/PlaylistComponent.cpp` (`handleKey`, track/clip/marker menus),
`src/ui/MixerComponent.cpp`, `src/ui/ExportDialog.cpp` + `src/core/ExportOptions.h`.
"grep:" = a string you can grep in that file. Excluded (another agent): Channel Rack menus, MIDI Out.

**Not planned (one line):** FL Cloud (Save and back up, Manage FL Cloud plugins, Sounds, Mastering, Distribution, My account, Upload samples, cloud status hint), account/registration/shop (Unlock, Buy, Account settings, Manage accounts), Upload / Upload to YouTube / export Upload panel (SoundCloud/YouTube), Video file export, Gopher (Alt+F1), Nebula picker, News/Downloads/Notifications.

Deliberate LYNX choices (not bugs): Ctrl+N = New project (FL: Save new version; LYNX save new version = Ctrl+Alt+S, round 118); Ctrl+H = Undo history (FL: Stop sound; LYNX has Stop sound in Tools › Macros, no key).

## 1. Main menu

| FL item (caption, key) | LYNX now | Gap | Value | Effort |
|---|---|---|---|---|
| File › New | MainComponent getMenuForIndex grep: `tr ("New") + shortcutText ("file.new")` | same (+ LYNX "New project...") | - | - |
| File › New from template | grep: `"New from template"` | same | - | - |
| File › Open... Ctrl+O | grep: `"Open...") + shortcutText ("file.open")`, Ctrl+O | same | - | - |
| File › Save Ctrl+S / Save as Ctrl+Shift+S | grep: `"Save as...")`, same keys | same | - | - |
| File › Save new version Ctrl+N | grep: `"Save new version"`, Ctrl+Alt+S | different key (deliberate) | - | - |
| File › Save to new project folder / Save as template | grep: `"Save to new project folder..."`, `"Save as template..."` | same | - | - |
| File › Delete unused files and save project | grep: `"Delete unused recordings and save..."` | same function, different caption | L | S |
| File › Import › MIDI file | grep: `imports.addItem (tr ("MIDI file...")` | same | - | - |
| Export › Wave Ctrl+R | grep: `exports.addItem (tr ("Wave file...")`; Ctrl+R is on "Last exported format(s)..." | key on a different item (FL: Ctrl+R = Wave) | M | S |
| Export › MP3 Ctrl+Shift+R | grep: `"MP3 file..."`, no key | missing key | M | S |
| Export › OGG / FLAC | grep: `"OGG file..."`, `"FLAC file..."` | same | - | - |
| Export › MIDI file Ctrl+Shift+M | grep: `exportMidiFile()`, no key | missing key | M | S |
| Export › M4A file | not found (Format enum has wav/mp3/ogg/flac only) | missing | L | M |
| Export › Last exported format(s) | grep: `"Last exported format(s)..."` (Ctrl+R) | same item, LYNX key is FL's Wave key | - | - |
| Export › Master... Ctrl+Alt+M | not found | missing | L | M |
| Export › All playlist tracks / track groups (song start / track start / time selection) | not found in File menu (playlist "Consolidate track" exists per track) | missing | M | M |
| Export › Zipped loop package / Project data files | grep: `"Project with samples (.zip)..."`, `"Project data files (collect...)"` | same (captions differ) | - | - |
| Export › Project bones | not found | missing | L | M |
| Recent projects / Revert to last backup / Exit | grep: `"Recent projects"`, `"Revert to last backup..."`, `"Exit"` | same | - | - |
| Edit › Undo Ctrl+Z | grep: `"Undo") + shortcutText ("edit.undo")` | same | - | - |
| Edit › Undo backwards Ctrl+Alt+Z | not found (LYNX has Redo Ctrl+Y / Ctrl+Shift+Z) | missing key/item (LYNX Redo is a different model) | M | S |
| Edit › Cut / Copy / Paste | not in LYNX Edit menu (only in editors' handleKey) | missing from menu | L | S |
| Add › Channel / More plugins / Plugin picker F8 | Add menu grep: `addSectionHeader (tr ("Channel"))`; F8 = panel.quickAdd | same (F8 = quick-add palette) | - | - |
| Add › Browse plugin database / all plugins / presets | not found as Add items (Shift+F8 view.pluginDatabase in View) | different place | L | S |
| Add › Refresh plugin list (fast scan) / Manage plugins | Options grep: `"Scan for plugins ({0})"`, `"Plugin manager..."` (also in Add) | different place | L | S |
| Add › Automation for last tweaked parameter | grep: `"Automation for last tweaked parameter ({0})"` | same | - | - |
| Add › Pattern | grep: `menu.addItem (tr ("Pattern")` | same | - | - |
| Patterns › Find first empty Shift+F4 | not found | missing | M | S |
| Patterns › Find next empty F4 | makePatternMenu grep: `"Next empty pattern (and name it)"`, F4 | same | - | - |
| Patterns › Find next empty (no naming) Ctrl+F4 | not found | missing | L | S |
| Patterns › Select in playlist | not found | missing | M | S |
| Patterns › Rename and color F2 | grep: `"Rename and color...") + shortcutText ("pattern.rename")` | same | - | - |
| Patterns › Change color / Random color | grep: `addSubMenu (tr ("Color"), colours)`; no "Random color" in pattern menu | partly (no random) | L | S |
| Patterns › Open in project browser Ctrl+Shift+P | not found | missing | L | M |
| Patterns › Group / Rename group / Ungroup | not found | missing | L | M |
| Patterns › Set time signature | grep: `"Song time signature"` (song-wide, not per pattern) | different | L | M |
| Patterns › Transpose... | not found in pattern menu | missing | L | S |
| Patterns › Insert one Ctrl+Shift+Ins | grep: `shortcutText ("pattern.insert")`, Ctrl+Shift+Ins | same | - | - |
| Patterns › Clone Ctrl+Shift+C | grep: `"Clone") + shortcutText ("pattern.clone")`, Alt+C | different key | M | S |
| Patterns › Delete Ctrl+Shift+Del | grep: `"Delete pattern"`, no key | missing key | M | S |
| Patterns › Move up/down Ctrl+Shift+Up/Down | grep: `pattern.moveUp`, same keys | same | - | - |
| Patterns › Split by channel | grep: `"Split by channel (one new pattern per channel)"` | same | - | - |
| Patterns › Quick render / Render as audio clip / Render and replace | not found | missing | M | M |
| View › Playlist F5 / Channel rack F6 / Piano roll F7 / Mixer F9 / Browser Alt+F8 | View menu loop `panelName (panel) + shortcutText (panelAction (panel))` | same | - | - |
| View › Project picker Ctrl+F8 | not found | missing | L | M |
| View › Tempo tapper Ctrl+F5 | grep: `"Tempo tapper") + shortcutText ("view.tempoTapper")` | same | - | - |
| View › Touch controller Alt+F7 | grep: `"Touch keyboard"`, no key | missing key | L | S |
| View › Script output | not found | n/a (no scripting) | L | L |
| View › Toolbars (Hide Ctrl+F11, Hint bar, ...) | Options grep: `addSubMenu (tr ("Toolbar"), makeToolbarMenu())`; Ctrl+F11 view.toolbar | different place (Options vs View) | L | S |
| View › Plugin performance monitor | grep: `"Plugin performance monitor..."` (Tools + Options) | different place | L | S |
| View › Close all windows F12 / plugin windows Alt+F12 | grep: `shortcutText ("view.closeWindows")`, `view.closePluginWindows` | same | - | - |
| View › Close all unfocused windows Ctrl+F12 | not found | missing | L | S |
| View › Align all channel editors Shift+F12 | not found | missing | L | S |
| View › Arrange windows › Realign Ctrl+Shift+H, layouts | grep: `"Reset layout"`, `"Save layout as..."`, `"Load layout"` ; no Ctrl+Shift+H | partly, missing key | L | S |
| View › Background (image / color) | grep: `"Set image wallpaper..."`, `"Set background color..."` | same (no dynamic/HTML wallpaper) | - | - |
| View › Undo history | Edit grep: `"Undo history...") + shortcutText ("edit.history")` | different place | L | S |
| View › Plugin database Shift+F8 | grep: `"Plugin database (in the Browser)"` | same | - | - |
| Options › MIDI settings F10 / Audio / General / File / Theme | grep: `"MIDI settings...") + shortcutText ("view.midiSettings")` etc. | same | - | - |
| Options › Manage plugins / Manage shortcuts | grep: `"Plugin manager..."`, `"Manage shortcuts..."` | same | - | - |
| Options › Project general settings / Project info F11 | grep: `"Project settings (tempo, time signature)..."`, `"Project info..."` | same | - | - |
| Options › Debugging log | not found | missing | L | S |
| Options › Enable MIDI remote control / master sync | not found as menu toggles | missing (MIDI Out excluded) | L | M |
| Options › Typing keyboard to piano Ctrl+T | View grep: `"Typing keyboard as a piano"`, Ctrl+T | different place | L | S |
| Options › Metronome Ctrl+M | grep: `"Metronome") + shortcutText ("transport.metronome")` | same | - | - |
| Options › Recording precount Ctrl+P | Options › Recording grep: `"Count-in") + shortcutText ("record.countIn")` | same key, inside submenu | - | - |
| Options › Start on input Ctrl+I | grep: `"Wait for the first note to start"` + record.waitForInput Ctrl+I | same | - | - |
| Options › Blend recorded notes Ctrl+B | Recording submenu grep: `"Overdub: add to the pattern"` (no key) | different (no Ctrl+B) | M | S |
| Options › Step edit Ctrl+E | not found | missing | M | M |
| Options › Loop record | not found as menu item (core/TakeRecording.h exists) | not found in menu | M | S |
| Options › Auto scrolling | grep: `menu.addItem (tr ("Auto scrolling")` | same | - | - |
| Options › Enable groups Shift+Alt+G | not found | missing | L | M |
| Options › Multilink to controllers Ctrl+J | not found | missing | M | M |
| Tools › Browser find Ctrl+F | grep: `"Browser find") + shortcutText ("browser.search")` | same | - | - |
| Tools › One-click audio recording | grep: `"One-click audio recording: arm the selected insert..."` (Shift+R record.quickAudio) | same (playlist target) | - | - |
| Tools › Remix a song / Control creator | not found | not planned-ish (AI stem / controller tool) | L | L |
| Tools › Riff machine / Chord progression tool | Piano roll Tools grep: `NoteTool::riffMachine`, Alt+E/Alt+P | different place (not in main Tools) | L | S |
| Tools › Save audio / MIDI clipboard to browser | not found | missing | L | M |
| Tools › Dump score log (2/5/10/20/30 min) / Clear log | grep: `"Dump score log to selected pattern"`, `"Clear score log"` (in Options) | different place | L | S |
| Tools › Audio logger (enable, settings, dump to editor) | Options grep: `"Audio logger (record everything the Master plays)"` | different place, no length/position settings | L | M |
| Tools › Last tweaked / Next to last tweaked | grep: `makeLastTweakedMenu (0)` | same | - | - |
| Tools › Stop sound Ctrl+H | grep: `"Stop sound (all notes off)"`, no key | deliberate (Ctrl+H = history) | - | - |
| Tools › Cancel recording | not found | missing | M | S |
| Tools › Prepare for performance mode / Select unused channels / Switch smart disable | grep: `"Prepare for performance mode..."`, `"Select unused channels"`, `"Switch smart disable for all plugins"` | same | - | - |
| Tools › Purge unused audio clips | not found | missing | L | S |
| Tools › Switch all / selected audio clip channels to (stretch modes) | not found | missing | L | M |
| Tools › Lock / Unlock all playlist tracks to content, Reset empty playlist tracks | not found | missing | L | M |
| Tools › Switch all plugin editors attached/detached, keyboard focus, declicking | not found | missing | L | M |
| Help › Help index / What's new / Diagnostics / About | grep: `"Shortcuts") + shortcutText ("help.shortcuts")`, `"Report a problem..."`, `"About LYNX SHARE"` | partly (no help index / what's new) | L | S |

## 2. Shortcuts (FL default vs LYNX default)

Context: G = global (KeyMap), PR = piano roll, PL = playlist, hard = hard-coded in `handleKey`.

| FL key: caption | LYNX now | Gap | Value | Effort |
|---|---|---|---|---|
| Space: play/stop, L: pattern/song, R: record | KeyMap transport.playStop / toggleMode / record | same | - | - |
| Ctrl+O / Ctrl+S / Ctrl+Shift+S | file.open / save / saveAs | same | - | - |
| Ctrl+N: Save new version | Ctrl+N file.new; Ctrl+Alt+S saveNewVersion | deliberate | - | - |
| Ctrl+R: Wave file | Ctrl+R file.export (last formats) | different target | M | S |
| Ctrl+Shift+R: MP3 | none | missing | M | S |
| Ctrl+Shift+M: MIDI export | none | missing | M | S |
| Ctrl+Alt+M: Export master | none | missing | L | M |
| Ctrl+Alt+Z: Undo backwards | none (Ctrl+Y / Ctrl+Shift+Z redo, LYNX extra) | missing | M | S |
| Ctrl+Z undo | edit.undo | same | - | - |
| F10 MIDI settings, F11 Project info, F12, Alt+F12, Ctrl+F11, Ctrl+F5, F5/F6/F7/F9, Alt+F8, F8, Shift+F8 | KeyMap view.* / panel.* | same | - | - |
| Ctrl+F8: Project picker | none | missing | L | M |
| Ctrl+F12 / Shift+F12 / Ctrl+Shift+H | none | missing | L | S |
| Alt+F7: Touch controller | none | missing | L | S |
| Ctrl+T typing piano, Ctrl+M metronome, Ctrl+P precount, Ctrl+I start on input | view.typingPiano / transport.metronome / record.countIn / record.waitForInput | same (global) | - | - |
| Ctrl+B: Blend recorded notes | none global; PR/PL hard Ctrl+B = duplicate (= FL editor Ctrl+B) | missing global | M | S |
| Ctrl+E: Step edit | none | missing | M | M |
| Shift+Alt+G: Enable groups / Ctrl+J: Multilink | none | missing | L/M | M |
| Ctrl+F: Browser find | browser.search | same | - | - |
| Ctrl+H: Stop sound | edit.history | deliberate | - | - |
| F4 / F2 / Ctrl+Shift+Ins / Ctrl+Shift+Up/Down | pattern.nextEmpty / rename / insert / moveUp/Down | same | - | - |
| Shift+F4 / Ctrl+F4 | none | missing | M/L | S |
| Ctrl+Shift+C: Clone pattern | Alt+C pattern.clone (global) | different key; Alt+C is FL's editor "Change color" | M | S |
| Ctrl+Shift+Del: Delete pattern | none | missing | M | S |
| Ctrl+Shift+P: open in project browser | none | missing | L | M |
| (EE) Ctrl+A / Ctrl+X / Ctrl+C / Ctrl+V / Del | PR/PL hard `letter == 'A'` etc., deleteKey | same | - | - |
| (EE) Ctrl+D: Deselect | not found in PR/PL handleKey or KeyMap | missing | H | S |
| (EE) Shift+R: Select one at random | global Shift+R = record.quickAudio | conflict | M | S |
| (EE) Shift+M: Select more at random / Shift+O: Select odd | PR selectMenu grep: `"Odd notes (every other one)"`, `"Random (half of the notes)"`, no key | missing key | L | S |
| (EE) Shift+C select by color / Shift+I invert | pianoRoll.selectByColour / invertSelection | same (PR only; PL lacks) | L | S |
| (EE) Ctrl+Enter / Ctrl+Left / Ctrl+Right: time selection | none | missing | M | S |
| (EE) PgUp/PgDn, Shift+1..5 zoom | pianoRoll.zoom*/playlist.zoom* | same | - | - |
| (EE) Shift+6: Zoom on performance clips | playlist.zoomPerformance, no key | missing key | L | S |
| (EE) Shift+0: Center | pianoRoll/playlist.centrePlayhead Shift+0 | same | - | - |
| (EE) Ctrl+B: Duplicate | PR/PL hard `cmd && letter == 'B'` duplicateSelection | same | - | - |
| (EE) Shift+Left/Right: Shift left/right | PR hard: plain Left/Right nudge by grid, Shift = bar | different key | M | S |
| (EE) Ctrl+Shift+Left/Right: Rotate | PR hard `cmd && shift && (code == juce::KeyPress::leftKey` | same | - | - |
| (EE) Shift+Up/Down: Transpose semitone; Ctrl+Up/Down: octave | PR hard: plain Up/Down = semitone, Shift+Up/Down = octave | different keys (Shift means octave in LYNX) | H | S |
| (EE) Shift+D: Discard lengths | PR hard Shift+D = `noteedit::shrinkToGrid` ("Shrink to grid") | same key, maybe different semantics (check) | L | S |
| (EE) Ctrl+Shift+V: Make unique | PL clip menu `"Make unique") + "\tShift+V+drag"`, no key | missing key | L | S |
| (EE) Ctrl+Alt+Home: Allow resizing from left | PR hard `cmd && alt && code == juce::KeyPress::homeKey` | same (PR) | - | - |
| (EE) Alt+C: Change color | pianoRoll.colourSelected Alt+C (PR); global Alt+C = pattern.clone in PL | same in PR, conflict elsewhere | M | S |
| (EE) Alt+M: Mute / Shift+Alt+M: Unmute | PR hard `cmd && letter == 'M'` toggleMute (Ctrl+M) | different key; Ctrl+M is FL's metronome/EE Import MIDI | M | S |
| (EE) Ctrl+Ins / Ctrl+Del: insert/delete space | PL hard `cmd && code == juce::KeyPress::insertKey` / deleteKey | same (PL) | - | - |
| (EE) Ctrl+Alt+Ins: Slice insert space | not found | missing | L | S |
| (EE) Ctrl+I: Insert current controller value | not found (global Ctrl+I = wait for input) | missing | L | M |
| (EE) Ctrl+G: Merge pattern clips (PL) / Glue (PR) | PR hard Ctrl+G join; PL: not found (PL menu `"Merge clips"` no key) | PL missing key | M | S |
| (EE) Ctrl+Shift+G / Ctrl+Alt+G: merge similar / automation clips | not found | missing | L | M |
| (EE) 7 / 8 / 9: pitch audio clip -1/+1 semitone, reverse | not found in PL handleKey (clip menu has `"Reverse"`, `"Pitch... (semitones...)"`) | missing keys | M | S |
| (EE) Alt+E/P/L/Q/U/A/S/F/W/K/Y/R/X/O tool dialogs | pianoRoll.* same keys | same | - | - |
| (EE) Ctrl+L quick legato / Ctrl+U quick chop | PR hard `letter == 'L'`, `'U'` | same | - | - |
| (EE) Ctrl+Q quick quantize / Shift+Q starts | pianoRoll.quickQuantize / quantizeStarts; playlist.quantizeStarts Ctrl+Q | same | - | - |
| (EE) Ctrl+Alt+C / Ctrl+Shift+Alt+C: consolidate selection | not found (PL track menu `"Consolidate track"` no key) | missing | M | M |
| (EE) Ctrl+Shift+B: Clone selected playlist tracks | not found (track menu `"Clone"`) | missing key | L | S |
| (EE) Alt+B note grid highlights / Alt+N background waveform | not found | missing | L | M |
| (EE) Alt+V ghost channels / Ctrl+Alt+V editable ghosts | PR hard `alt && ! cmd && letter == 'V'`, `alt && cmd && letter == 'V'` | same | - | - |
| (EE) Shift+G / Alt+G group / ungroup | pianoRoll.groupNotes / ungroupNotes | same (PR) | - | - |
| (EE) Alt+T / Ctrl+T / Shift+Alt+T / Shift+T: time markers (add one, auto, time sig, place loop) | PL marker menu grep: `"Add marker here..."`, `"Time signature change here"`, `"Place loop (from this marker to the next)"`; no keys | missing keys (Ctrl+T conflicts with global typing piano in FL itself) | M | S |
| (EE) Shift+N: Stamp none | not found | missing | L | S |
| (EE) Ctrl+P: Performance mode (PL) | playlist.performanceMode Ctrl+P | same; PR hard Ctrl+P = togglePorta (LYNX-own) | - | - |
| LYNX-only keys | Ctrl+Space pause, Shift+Space, Alt+Num*/Num/, Num+/-, F1 sheet, Enter maximise, Shift+S ripple, Shift+Del ripple delete, tool letters P/B/N/D/T/C/E/Z/Y/S, G/H/J/K channel stepping | extras (FL has tool letters too, not in extracted table) | - | - |

## 3. Piano roll / Playlist options menus (EEMenu, PLTrackMenu, EEMarkerMenu, TimeMenu)

| FL item (caption, key) | LYNX now | Gap | Value | Effort |
|---|---|---|---|---|
| File › Import MIDI file Ctrl+M / Export as MIDI Ctrl+Shift+M | PR grep: `"Import .mid into this channel..."`, `"Export pattern as .mid..."` (MIDI submenu), no keys | missing keys | M | S |
| File › Open/Save/Browse score, Paste from / Copy to MIDI clipboard, Export as score sheet | not found | missing | L | M |
| Edit › Cut/Copy/Paste/Duplicate/Delete | PL clip menu grep: `"Cut") + "\tCtrl+X"`; PR via keys only | partly (no PR Edit menu items) | L | S |
| Edit › Delete all | not found | missing | L | S |
| Edit › Shift/Rotate/Transpose items | PR showMoreMenu grep: `"Rotate left within the bar"`; no shift/transpose items | partly | L | S |
| Edit › Discard lengths Shift+D | grep: `"Shrink to grid") + "\tShift+D"` | similar (check semantics) | L | S |
| Edit › Make unique / Make unique as sample | PL grep: `"Make unique"` | partly (no "as sample") | L | M |
| Edit › Allow resizing from left | PR grep: `"Resize notes from the left edge too") + "\tCtrl+Alt+Home"` | same | - | - |
| Edit › Allow clips nudging / Dropping locks to content / Drop audio on track headers | not found | missing | L | M |
| Edit › Change color / Mute / Unmute | PR grep: `"Colour the selected notes"`; PL `"Change color"` | partly | L | S |
| Edit › Insert space / Slice insert / Delete space / Trim selection | PL hard Ctrl+Ins/Del only; no menu items; Trim not found | partly | M | S |
| Edit › Turn into automation clip | not found | missing | L | M |
| Edit › Merge pattern / similar / automation clips | PL grep: `"Merge clips"` | partly | L | M |
| Edit › Pitch -1/+1 semitone, Reverse (7/8/9) | PL clip menu grep: `"Reverse"`, `"Pitch... (semitones, cents, keep length)"` | partly, no keys | M | S |
| Edit › Automatic crossfades | PL grep: `"Automatic crossfades (current project)"` | same | - | - |
| Edit › Create new clips with manual fades / Snap fade handles / Create variants when stretching / Select tracks when samples dropped | not found | missing | L | S |
| Tools › Built-in note tools (Riff..LFO) with FL keys | PR toolsMenu grep: `tool (NoteTool::arpeggiate, "Alt+A")` (REA round 143 order) | same | - | - |
| Tools › Fit to current tempo | PL clip grep: `"Fit to tempo"` | same (clip menu) | - | - |
| Tools › Consolidate playlist selection (from selection / song start) | PL track menu grep: `"Consolidate track"` (track start / song start) | partly (track, not selection) | M | M |
| Tools › Export all playlist tracks / track groups | not found | missing | M | M |
| Tools › Clone selected playlist track(s) | PL track grep: `"Clone"` | same, no key | L | S |
| Tools › Scripts (Run last script, Open script folder) | not found (LYNX has built-in "More tools") | n/a | L | L |
| Stamp (Only one, none, auto chord top down/bottom up) | PR grep: `"Chord stamp: the clicked key is the top note"` | partly | L | S |
| View › Grid color/contrast/invert, track separators, time segments | not found in PR/PL menus | missing | L | M |
| View › Keep labels on screen, content in titlebars, behind clips, shadow, rounded | not found | missing | L | M |
| View › Resize all tracks 33..200% | PL track menu Size grep: `"Every track"` | same | - | - |
| View › Hide collapsed grouped tracks, fade/gain previews, gain value/scale | not found (PL has gain handle `"Gain: {0}"`) | missing | L | S |
| View › Performance clip/track progress | KeyMap playlist.perfClipProgress / perfTrackProgress | same | - | - |
| View › Mini playlist preview, Precise time indicator, Incremental scrolling | not found | missing | L | M |
| View › Note grid highlights Alt+B / Scale highlighting root+scale / Snap PR notes to scale | PR grep: `"Snap notes to the scale"`, `"Fold to the scale..."`; no Alt+B | partly | M | S |
| View › Show chord name for selection | not found | missing | M | S |
| View › Ghost channels Alt+V / Editable ghosts Ctrl+Alt+V | PR grep: `"Show ghost notes") + "\tAlt+V"` | same | - | - |
| View › Background waveform Alt+N | not found | missing | L | M |
| View › Keyboard options (styles, key labels), Swap panels, Flip separator | PR keyboard menu grep: `"Rename this key..."`, `"Label the keys with the General MIDI drum map"`; others not found | partly | L | M |
| Snap (Main / Events / Markers) | EditorTools grep: `"Main (toolbar snap)"` | partly | - | - |
| Select › Deselect / All / Random / Color / Odd / Muted / Overlapping / Stacked / Invert | PR selectMenu grep: `"Deselect"`, `"Muted notes"`, `"Stacked notes (hidden duplicates)"`; PL `"Overlapping clips ({0})"` | same (no keys for some, see §2) | - | - |
| Select › Select by selected source | PL grep: `"Select all clips of this source"` | same | - | - |
| Select › Time around / previous / next time; Magic lasso | not found | missing | M | S |
| Group › Group / Ungroup | PR grep: `"Group the selected notes"` | same (PR) | - | - |
| Zoom › all items | PR/PL zoom submenus grep: `"Zoom: one bar") + "\tShift+1"` | same; Shift+6 missing | L | S |
| Time markers › Add one / Add two / Add every / jump to next bar / time sig / key marker / place loop / change to marker loop / start/stop recording | PL grep: `"Add marker here..."`, `"Add one every"`, `"Add jump to next bar"`, `"Change to marker loop"`, `"Add a Start recording marker here"` | mostly same; no keys; "Add two" / "Add auto" not found | M | S |
| EEMarkerMenu › Rename / Delete / Place loop / action list (Marker skip, pause, Key/Scale...) | PL grep: `"Rename marker..."`, `"Remove marker"`, `"Marker action"` | same | - | - |
| EEMarkerMenu › Move content left / right | not found | missing | L | S |
| Picker panel (Enable, side, width, sort, select unused) | not found in PL | missing | M | L |
| Edit patterns in (auto / channel rack / piano roll / event editor) | not found | missing | L | S |
| Quantize song jumps | PL grep: `"Quantize song jumps"` | same | - | - |
| Performance mode Ctrl+P / Prepare for performance mode | KeyMap playlist.performanceMode; Tools `"Prepare for performance mode..."` | same | - | - |
| Lock/Unlock all tracks to content, Reset empty tracks, Collapse/Expand all grouped tracks | not found | missing | L | M |
| PLTrackMenu › Rename/color, Change color, Random color, Auto name, Reset, Track mode, Performance settings, Size, Group with above, Ungroup, Auto color group, Collapse, Current clip source, Consolidate, Mute/Unmute all clips, Insert one, Clone, Delete, Move up/down | PL grep: `"Group with above track"`, `"Track mode"`, `"Performance settings"`, `"Mute all clips"` etc. | same | - | - |
| PLTrackMenu › Change icon, Auto name clips, Lock to this size, Lock to content, Send pattern instances here, Show ghost notes, Select audio input / Arm / Disarm recording | not found | missing | M (arm/input) / L | S-M |
| TimeMenu › Bar:Beat:Tick / Min:Sec:Cs / SMPTE modes | Toolbar.cpp grep: `SMPTE` (time display) | same (toolbar) | - | - |
| AutoscrollPopupMenu › Continuous scrolling | not found (follow on/off only) | missing | L | S |

## 4. Mixer menus (FXForm)

| FL item (caption, key) | LYNX now (MixerComponent.cpp) | Gap | Value | Effort |
|---|---|---|---|---|
| Disk recording › Render armed / selected tracks (Alt+R / Shift+Alt+R) | grep: `"Render armed tracks to wave file(s)..."`, `"Render selected tracks to wave file(s)..."`, no keys | missing keys | M | S |
| Disk recording › Auto-unarm / Auto-create audio clip | grep: `"Auto-unarm (rendering the armed tracks disarms them)"` | same | - | - |
| Disk recording › Latency compensation / Ignore silent takes / Auto name playlist tracks / Group loop recorded takes / Apply track level and mute | not found | missing | L | M |
| Disk recording › Format 16/24/32 | not found in mixer menu | missing | L | S |
| PDC › Set for all tracks Ctrl+P / Automatic / Compensate automations | grep: `"Reset manual latency on all tracks"` only | partly | L | M |
| Link all parameters... | not found | missing | L | M |
| View › Waveforms Alt+W, Names at top, Routing cables, compact/small plugin list, inspector side, Colorful mixer, Alt highlighting, lines, external input levels | not found (LYNX has own view items: `"Show only used inserts"`) | missing | L | M |
| Trim unused tracks | grep: `"Remove unused inserts at the end"` | same (caption differs) | - | - |
| Switch smart disable for all plugins | main Tools menu `"Switch smart disable for all plugins"` | different place | L | S |
| Rename, color and icon F2 / Change color / Random color | grep: `"Rename and color..."`, `"Random color"`; no F2 in mixer context | missing key, no icon | M | S |
| Gradient color / Change icon | not found | missing | L | S |
| Assign to / Unassign from new Audio track(s) | not found | missing | M | M |
| File › Open / Save state (Ctrl+Shift+S) / Browse states | grep: `"Open mixer track state..."`, `"Save mixer track state as..."`, `"Browse states"` | same, no key | L | S |
| Open audio editor Ctrl+E / Open audio editor and record Shift+E | grep: `"Open audio editor"`, no keys; "and record" not found | missing keys/item | L | S |
| Reset selected track(s) to default | grep: `"Reset selected track(s) to default"` | same | - | - |
| Channel routing (Ctrl+L / Ctrl+Shift+L) | grep: `"Route selected channels to this track"` + KeyMap mixer.linkChannels | same | - | - |
| Track routing (route/sidechain selected ↔ this, Create submix) | grep: `"Sidechain selected to this track only"`, `"Create submix..."` | same | - | - |
| Select › Channels routed (Alt+L), Tracks this one is routed to, Tracks routed to this one (Ctrl+Alt+L), Group, All (Ctrl+A) | grep: `"Channels routed to this track"`, `"Tracks routed to this one"`; KeyMap Alt+L | missing Ctrl+Alt+L, Ctrl+A keys; "Channels and playlist tracks routed" not found | L | S |
| Reset routing / Unroute | grep: `"Unroute tracks routed to the selected track(s)"` | same | - | - |
| Solo selected S / Alt solo Alt+S | grep: `"Solo selected track(s)"`, no key; Alt solo not found | missing | M | S |
| Arm / Disarm selected | grep: `"Arm selected track(s)"` | same | - | - |
| Compensate for master latency | grep: `"Compensate for master latency"` (delay menu) | same | - | - |
| Group › Separator / Create group / Auto color group | grep: `"Separator"`, `"Create group"`, `"Auto color group"` | same | - | - |
| Layout › Dock to, Move left/right (Alt+Left/Right), Delete (Del), Insert one/multiple/from state | grep: `"Dock to"`, `"Insert multiple..."`; KeyMap mixer.moveLeft | same | - | - |
| CPU › Allow threaded processing | grep: `"Allow threaded processing"` | same | - | - |
| OutMenu › Reset mapping / Auto map | not found (output target menu `"Audio output target"`) | missing | L | S |
| SendMenu › route/sidechain to this track (only) | grep: `"Send only here"`, `"Sidechain only (no audio)..."` | same | - | - |
| DelayMenu › Reset / ms / samples / beats / Set from / Bypass track latency compensation | grep: `"Set in ms..."`, `"Bypass track latency compensation"` | same; "Ping..." not found | L | S |
| AudioRecorderRoutingMenu › record source (external only, post FX, post EQ...) / Monitor external input | grep: `"Monitor external input"`, `"Audio input source"`; record source points not found | partly | M | M |
| AddMixerTracksMenu › Add one / Add multiple / Increase number to / Add from state | grep: `"Add one"`, `"Add multiple..."` | partly | L | S |
| Remove all Edison instances | n/a (no Edison) | n/a | - | - |

## 5. Export dialog (WAVRenderForm vs ExportDialog.cpp / ExportOptions.h)

| FL option | LYNX now | Gap | Value | Effort |
|---|---|---|---|---|
| Mode Song / Pattern | `enum class Range` song / pattern / selection | same (+selection) | - | - |
| Tail: Cut / Leave / Wrap remainder | `enum class TailMode` cut/leave/wrap | same | - | - |
| Total time label, Master muted warning | grep: `"Length: {0} bars"`, `"The Master insert is muted: the file will be silent"` | same | - | - |
| Save tempo information (acid) | not found | missing | M | S |
| Save note markers / loop markers / playlist markers | not found | missing | M | M |
| Trim PDC silence | `bool trimPdc` | same | - | - |
| Split mixer tracks (+ include Master / Current) | `Stems::mixerTracks`, `stemsIncludeMaster` ("Also the full mix") | partly: "Current" track toggle not found | L | S |
| Enable master effects / insert effects | `masterEffects`, `insertEffects` | same | - | - |
| Resampling (interpolation quality list) | not found | missing | L | M |
| Dithering | `DitherMode` none/tpdf/shaped | same (+shaped) | - | - |
| HQ for all plugins / Disable maximum polyphony | `hqAllPlugins`, `disableMaxPolyphony` | same | - | - |
| Formats WAV/MP3/OGG/FLAC tickable together | `moreFormats` mask (grep: `"Also save"`) | same | - | - |
| MID box | not in dialog (separate File › Export › MIDI) | different place | L | S |
| M4A box | not found | missing | L | M |
| WAV bit depth 16/24/32f, FLAC bit depth 16/24, FLAC level, MP3/OGG bitrate | `wavBits`, `flacBits`, `flacLevel`, `mp3Kbps`, `oggKbps` | same | - | - |
| Channels Stereo / Mono merged / left / right | `enum class Channels` | same | - | - |
| Show file(s) when complete | `showWhenDone` | same | - | - |
| Render minimized | not found | n/a (low) | L | S |
| Upload panel | not planned | - | - | - |
| LYNX extras | sample rate, loops 1..16, normalize, channel stems, plugin warm-up, real-time hardware render | extras | - | - |

## Top 10 gaps worth implementing (FL-exact, small)

1. Piano roll Ctrl+D = Deselect (missing).
2. Piano roll transpose keys: Shift+Up/Down = semitone, Ctrl+Up/Down = octave; Shift+Left/Right = shift (LYNX uses plain arrows / Shift = octave).
3. Piano roll mute keys Alt+M / Shift+Alt+M (LYNX Ctrl+M).
4. Export keys: Ctrl+R = Wave, Ctrl+Shift+R = MP3, Ctrl+Shift+M = MIDI.
5. Pattern keys: Ctrl+Shift+C clone (LYNX Alt+C), Ctrl+Shift+Del delete, Shift+F4 find first empty, Ctrl+F4 next empty no naming.
6. Ctrl+Alt+Z Undo backwards.
7. Playlist audio clip keys 7 / 8 / 9 (pitch -1/+1, reverse) and Ctrl+G merge clips.
8. Editor selection keys: Shift+M more random, Shift+O odd, Ctrl+Enter / Ctrl+Left / Ctrl+Right time selection (resolve Shift+R conflict with record.quickAudio).
9. Time marker keys Alt+T add, Shift+Alt+T time signature, Shift+T place loop (items exist in PL marker menu).
10. Mixer keys: S solo, Alt+S alt solo, F2 rename, Alt+R / Shift+Alt+R render armed/selected; plus export "Save tempo information" / marker options.

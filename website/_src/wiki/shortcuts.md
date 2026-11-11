---
title: Shortcuts
summary: The default keyboard shortcuts of LYNX SHARE 0.1.25, by window.
status: full
---
These are the default keys in 0.1.25 alpha. Press `F1` in the app for the shortcut sheet.

To change a shortcut, open `Options › Manage shortcuts`. You can search by action or by key, add or change a shortcut, and reset one or all of them. Your changes are saved in `keys.json` in the settings folder (`%APPDATA%\LYNX` on Windows, `~/Library/Application Support/LYNX` on macOS, `~/.config/LYNX` on Linux).

> On macOS the app shows the keys with Mac names: Ctrl is shown as ⌘ and Alt as ⌥.

While the typing keyboard piano is on, its keys play notes and most single-letter shortcuts are paused (`L` still works). Turn it off with `Ctrl+T`.

## Transport and recording

| Key | Action |
|---|---|
| `Space` | Play, or stop and go back to where playback started |
| `Ctrl+Space` | Play or pause |
| `Shift+Space` | Play, or stop at the start of the song or pattern |
| `L` | Switch between Pattern mode and Song mode |
| `R` | Recording on or off |
| `Shift+R` | Record audio now: arms the selected insert and records |
| `Ctrl+M` | Metronome on or off |
| `Ctrl+P` | Count-in before recording on or off |
| `Ctrl+I` | Recording waits for the first note, on or off |
| `Ctrl+B` | Blend recorded notes on or off |
| `Ctrl+E` | Step edit on or off |
| `Ctrl+H` | Stop sound (all notes off) |
| `Ctrl+T` | Typing keyboard to piano on or off |
| `Alt+Numpad *` / `Alt+Numpad /` | Jump to the next or previous song marker |

Some of these keys do something else inside an editor that has focus. In the Piano roll, `Ctrl+M` imports a MIDI file and `Ctrl+B` duplicates. In the Playlist, `Ctrl+P` is Performance mode.

## Files and editing

| Key | Action |
|---|---|
| `Ctrl+N` | New project |
| `Ctrl+O` | Open project |
| `Ctrl+S` | Save |
| `Ctrl+Shift+S` | Save as |
| `Ctrl+Alt+S` | Save new version |
| `Ctrl+R` | Export audio |
| `Ctrl+Shift+R` | Export an MP3 file |
| `Ctrl+Shift+M` | Export a MIDI file |
| `Ctrl+Z` | Undo. Pressed again, it redoes that step |
| `Ctrl+Alt+Z` | Undo backwards, step after step |
| `Ctrl+Y` or `Ctrl+Shift+Z` | Redo |
| `Ctrl+Alt+H` | Undo history |
| `Ctrl+F` | Search the Browser |

## Windows

| Key | Action |
|---|---|
| `F5` | Playlist |
| `F6` | Channel rack |
| `F7` | Piano roll |
| `F9` | Mixer |
| `Alt+F8` | Browser |
| `F8` | Plugin picker |
| `Shift+F8` | Plugin database (in the Browser) |
| `Enter` | Maximize or restore the Playlist |
| `F10` | MIDI settings |
| `F11` | Project info |
| `F12` | Close all windows (plugins, channel settings, tools) |
| `Alt+F12` | Close all plugin windows |
| `Ctrl+F12` | Close all windows except the focused one |
| `Alt+F7` | Touch keyboard |
| `Ctrl+F5` | Tempo tapper |
| `F1` | Shortcut sheet |

## Patterns

| Key | Action |
|---|---|
| `Numpad +` / `Numpad -` | Next, previous pattern |
| `Numpad 1` to `Numpad 9` | Select pattern 1 to 9 |
| `F4` | Next empty pattern, and name it |
| `Ctrl+F4` | Next empty pattern, without naming |
| `Shift+F4` | First empty pattern, and name it |
| `F2` | Rename and colour the pattern |
| `Ctrl+Shift+C` | Clone the pattern |
| `Ctrl+Shift+Ins` | Insert an empty pattern before the current one |
| `Ctrl+Shift+Del` | Delete the pattern |
| `Ctrl+Shift+Up` / `Ctrl+Shift+Down` | Move the pattern up, down |

## Channel rack

| Key | Action |
|---|---|
| `Up` / `Down` | Select the channel above, below |
| `Shift+Up` / `Shift+Down` | Extend the selection |
| `Alt+Up` / `Alt+Down` | Move the selected channels |
| `Ctrl+A` | Select all channels |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Cut, copy, paste the selected channels' steps |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Rotate the steps left, right |
| `Alt+C` | Clone the selected channels |
| `Alt+Del` | Delete the selected channels |
| `Alt+G` | Group the selected channels |
| `Ctrl+L` | Send the selected channels to free Mixer inserts |
| `Ctrl+K` | Step graph lanes on or off |
| `1` to `0` | Mute or unmute channels 1 to 10 |
| `Ctrl+1` to `Ctrl+0` | Solo channels 1 to 10 |

## Piano roll

| Key | Action |
|---|---|
| `P` `B` `N` `D` `T` `C` `E` `Z` `Y` | Pencil, Brush, Drum brush, Eraser, Mute, Knife, Lasso, Zoom, Listen |
| `S` | Slide notes on or off (new notes) |
| `G` / `K` | Previous, next channel |
| `H` / `J` | Previous, next channel with notes |
| `Ctrl+A` | Select all |
| `Ctrl+D` or `Esc` | Deselect |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy, cut, paste |
| `Ctrl+B` | Duplicate |
| `Del` | Delete the selection |
| `Up` / `Down` | Move the selection a semitone |
| `Ctrl+Up` / `Ctrl+Down` | Move the selection an octave |
| `Left` / `Right` | Move the selection one grid step |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Rotate the notes within the bar |
| `Backspace` | Snap off or on |
| `Ctrl+Q` | Quick quantize (starts and lengths) |
| `Shift+Q` | Quantize starts |
| `Ctrl+U` | Chop to the grid |
| `Ctrl+G` | Join touching notes |
| `Ctrl+L` | Legato |
| `Shift+D` | Shrink the notes to the grid |
| `Ctrl+P` | Porta on or off for the selection |
| `Alt+M` / `Shift+Alt+M` | Mute, unmute the selection |
| `Alt+V` | Ghost notes on or off |
| `Shift+I` | Invert the selection |
| `Shift+G` / `Alt+G` | Group, ungroup the selected notes |
| `Alt+C` | Colour the selected notes with the note colour |
| `Shift+C` | Select the notes of the same colour |
| `Shift+N` | Chord stamp: none (single notes) |
| `Ctrl+Ins` / `Ctrl+Del` | Insert, delete space (the time selection) |
| `Ctrl+M` | Import a MIDI file |
| `Ctrl+Shift+M` | Export as a MIDI file |
| `PageUp` / `PageDown` | Zoom in, out |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoom to 1, 4, 16 bars |
| `Shift+4` | Zoom to show everything |
| `Shift+5` | Zoom to the selection |
| `Shift+0` | Centre the view on the playhead |

Note tool dialogs in the Piano roll:

| Key | Tool |
|---|---|
| `Alt+A` | Arpeggiate |
| `Alt+S` | Strum |
| `Alt+Y` | Flip |
| `Alt+R` | Randomise |
| `Alt+X` | Scale levels |
| `Alt+K` | Limit to a range |
| `Alt+Q` | Quantize |
| `Alt+U` | Chop |
| `Alt+F` | Flam |
| `Alt+W` | Claw machine |
| `Alt+O` | LFO |
| `Alt+L` | Articulate |
| `Alt+E` | Riff machine |
| `Alt+P` | Generate chord progression |

## Playlist

| Key | Action |
|---|---|
| `P` `B` `D` `T` `C` `E` `Z` `Y` `S` | Pencil, Brush, Eraser, Mute, Knife, Lasso, Zoom, Listen, Slip |
| `Ctrl+A` | Select all |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy, cut, paste |
| `Ctrl+B` | Duplicate |
| `Del` | Delete the selected clips |
| `Shift+Del` | Delete the selected clips and close the gaps |
| `Shift+S` | Ripple editing on or off |
| `Backspace` | Snap off or on |
| `Alt+M` / `Shift+Alt+M` | Mute, unmute the selected clips |
| `Shift+M` | Stretch audio when resizing audio clips, on or off |
| `Ctrl+Shift+V` | Make the selected clips unique |
| `Ctrl+G` | Merge pattern clips |
| `Ctrl+Shift+G` | Merge similar pattern clips |
| `Ctrl+Alt+G` | Merge automation clips |
| `Ctrl+Shift+B` | Clone the selected Playlist tracks |
| `Ctrl+Q` | Quantize the clips' starts |
| `7` / `8` | Pitch the selected audio clips one semitone down, up |
| `9` | Reverse the selected audio clips |
| `Ctrl+Ins` / `Ctrl+Del` | Insert, remove the time selection's length on all tracks |
| `Ctrl+Left` / `Ctrl+Right` | Select the previous, next time |
| `Ctrl+Enter` | Select time around the selection |
| `Alt+T` | Add a time marker |
| `Ctrl+T` | Add a marker automatically |
| `Shift+Alt+T` | Add a time signature change |
| `Shift+T` | Place loop marker |
| `Ctrl+Alt+C` | Consolidate the Playlist selection |
| `Ctrl+P` | Performance mode on or off |
| `PageUp` / `PageDown` | Zoom in, out |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoom to 1, 4, 16 bars |
| `Shift+4` | Zoom to show everything |
| `Shift+5` | Zoom to the selection |
| `Shift+0` | Centre the view on the playhead |

## Mixer

| Key | Action |
|---|---|
| `Alt+Left` / `Alt+Right` | Move the selected inserts left, right |
| `F2` | Rename and colour the selected insert |
| `S` | Solo the selected inserts |
| `Alt+S` | Solo the selected inserts with the inserts that feed them |
| `Ctrl+L` | Link the selected channels to the selected insert |
| `Ctrl+Shift+L` | Link the selected channels to inserts starting from the selected one |
| `Alt+L` | Select the channels linked to the selected insert |
| `Del` | Delete the plugin in the selected effect slot |
| `Ctrl+E` | Open the audio editor |
| `Shift+E` | Open the audio editor and record |
| `Alt+R` | Render the armed inserts to wave files |
| `Shift+Alt+R` | Render the selected inserts to wave files |

## Browser

| Key | Action |
|---|---|
| `Up` / `Down` | Previous, next file (with preview) |
| `Shift+Up` / `Shift+Down` | Previous, next file into the selected channel |
| `Enter` | Load the file into the selected channel |
| `Left` | Close the folder, or go to the parent folder |
| `Right` | Open the folder |
| `F2` | Rename the selected plugin (Plugin database) |

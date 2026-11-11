---
title: Shortcuts
summary: The default keyboard shortcuts of LYNX SHARE 0.1.26, by window.
status: full
---
![The Manage shortcuts window: a searchable list of actions with the keys of each](/assets/img/wiki/shortcuts.webp)

These are the default keys in 0.1.26 alpha. `Help › Shortcuts` (`F1`) shows the shortcut sheet inside the app. `Options › Manage shortcuts...` opens the editor in the picture, where you change them.

> On macOS the app shows the keys with Mac names: Ctrl is shown as ⌘ and Alt as ⌥.

## Change a shortcut

1. Choose `Options › Manage shortcuts...`.
2. Type an action or a key, such as `Ctrl+L`, in the search box. The list beside it limits the search to one place, and `Changed only` shows the ones you changed.
3. Click the action.
4. Click `Change` to replace its keys, or `Add shortcut` to give it a second one, then press the new keys. `Esc` cancels.
5. If the keys are already used in the same place, a message names the other action and lets you move them.

`Remove` clears the keys of the action. `Reset to default` restores one action and `Reset all...` restores every one. Your changes are saved in `keys.json` in the settings folder (`%APPDATA%\LYNX` on Windows, `~/Library/Application Support/LYNX` on macOS, `~/.config/LYNX` on Linux). `Open keys.json` opens that file.

## Transport and recording

| Key | What it does |
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

| Key | What it does |
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

| Key | What it does |
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

## Clip View and Device View

These keys work in the Clip-launch workflow only.

| Key | What it does |
|---|---|
| `Ctrl+Alt+L` | Show or hide the detail view |
| `Ctrl+Alt+3` | Clip View |
| `Ctrl+Alt+4` | Device View |
| `Shift+Tab` | Switch between Clip View and Device View |
| `Left` / `Right` | Device View: select the previous, next device |
| `Del` or `Backspace` | Device View: delete the selected effect |

## Patterns

| Key | What it does |
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

| Key | What it does |
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

| Key | What it does |
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

### Note tools

Each key opens the dialog of one note tool in the Piano roll.

| Key | What it does |
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
| `Ctrl+Alt+Y` | Run the last script again |

## Playlist

| Key | What it does |
|---|---|
| `P` `B` `D` `T` `C` `E` `Z` `Y` `S` | Pencil, Brush, Eraser, Mute, Knife, Lasso, Zoom, Listen, Slip |
| `Ctrl+A` | Select all |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy, cut, paste |
| `Ctrl+B` | Duplicate |
| `Del` | Delete the selected clips |
| `Shift+Del` | Delete the selected clips and close the gaps |
| `Shift+Left` / `Shift+Right` | Move the selected clips one snap step left, right |
| `Shift+Up` / `Shift+Down` | Move the selected clips one track up, down |
| `Alt+Left` / `Alt+Right` | Nudge the selected clips left, right by a fine step, without snap. `Shift+Alt` does the same |
| `Ctrl+D` | Deselect clips and time |
| `Shift+I` | Invert the selection |
| `Shift+C` | Select the clips of the selected source |
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
| `Ctrl+Alt+Ins` | Slice the clips and insert space |
| `Ctrl+Alt+Home` | Resizing clips from the left, on or off |
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

| Key | What it does |
|---|---|
| `Alt+Left` / `Alt+Right` | Move the selected inserts left, right |
| `F2` | Rename and colour the selected insert |
| `S` | Solo the selected inserts |
| `Alt+S` | Solo the selected inserts with the inserts that feed them |
| `Ctrl+L` | Link the selected channels to the selected insert |
| `Ctrl+Shift+L` | Link the selected channels to inserts starting from the selected one |
| `Alt+L` | Select the channels linked to the selected insert |
| `Del` | Delete the selected inserts. LYNX SHARE asks first |
| `Ctrl+E` | Open the audio editor |
| `Shift+E` | Open the audio editor and record |
| `Alt+R` | Render the armed inserts to wave files |
| `Shift+Alt+R` | Render the selected inserts to wave files |

## Browser

| Key | What it does |
|---|---|
| `Up` / `Down` | Previous, next file (with preview) |
| `Shift+Up` / `Shift+Down` | Previous, next file into the selected channel |
| `Enter` | Load the file into the selected channel |
| `Left` | Close the folder, or go to the parent folder |
| `Right` | Open the folder |
| `F2` | Rename the selected plugin (Plugin database) |

## Good to know

- While the typing keyboard piano is on, its keys play notes and most single-letter shortcuts are paused (`L` still works). Turn it off with `Ctrl+T`.
- A shortcut works either everywhere or in one window. One that belongs to a window wins over the same keys that work everywhere, while that window has focus.
- Some actions have no default key, for example the Quick-add palette. Give them one in the editor.
- In the Playlist, the keys that move clips move every clip when none is selected. The fine step of `Alt+Left` and `Alt+Right` is one pixel at the current zoom.
- LYNX SHARE's shortcuts also work while a plugin window has focus, for example `Space`. Plain letters and digits go to the plugin first, and `Esc` closes its window.

## Related

- [A tour of the window](/wiki/tour/)
- [Settings](/wiki/settings/)
- [MIDI controllers](/wiki/midi-controllers/)
- [Coming from FL Studio](/wiki/coming-from-fl-studio/)

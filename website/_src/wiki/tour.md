---
title: A tour of the window
summary: The toolbar, the hint bar, the five windows and how they move.
status: full
---
![The main window with the demo project: toolbar on top, Browser on the left, Playlist behind and the Channel rack floating over it](/assets/img/wiki/desktop.webp)

LYNX SHARE has one main window. A toolbar runs along the top. Under it is a desktop that holds five windows: the Browser, the Playlist, the Channel rack, the Piano roll and the Mixer. On a fresh start the Browser is on the left, the Playlist fills the rest and the Channel rack floats over it. The Piano roll and the Mixer are hidden until you call them.

## Find your way around

1. Point the mouse at any control. The hint bar under the menus says what it does and which mouse buttons and keys it takes.
2. Press `F6` to hide the Channel rack, and `F6` again to show it.
3. Press `F9` to show the Mixer, then `F7` for the Piano roll.
4. Drag a window by its title strip to move it. Double-click the title to maximize it, and again to restore it.
5. Choose `View › Reset layout` to put everything back.

## The toolbar

![The toolbar: menus and hint bar, transport, master knobs, recording options, snap and pattern, window buttons, file buttons, chord display, meters and output monitor](/assets/img/wiki/toolbar-1920.webp)

From left to right, with the default layout:

| Group | What it holds |
|---|---|
| Menu and hint bar | The menus `File`, `Edit`, `Add`, `Patterns`, `View`, `Options`, `Tools` and `Help`, with the hint bar under them. Always first |
| Transport | The `Pattern` and `Song` mode buttons, Play, Stop and Record, the tempo in BPM and the position as bar, beat and step |
| Master volume and pitch | The `MASTER` volume knob and the `PITCH` knob, in cents |
| Recording options | Metronome, count-in, loop record, step edit and the other recording switches |
| Snap and pattern | The main snap (`Line (follows zoom)` by default), the current pattern, a button for a new pattern and the pattern menu |
| Window shortcuts | One button per window, and the plugin picker |
| File and undo | New, Open, Save, Export, Undo and Redo |
| Chord detection | Names the chord being played on a MIDI device or the typing keyboard |
| CPU, memory and polyphony | Audio CPU load, memory in use and the number of voices |
| Output monitor | An oscilloscope of the output. A click steps through its other modes |

A group named `Multilink to controllers` exists too and is hidden by default.

`Options › Toolbar` changes the toolbar: `Edit toolbar (drag panels to move, add or remove them)`, `Separators`, `Hide the toolbar panels`, `Presets`, a switch for each group, and `Icon size` (small, medium or large).

## Transport

| Control | Key | What it does |
|---|---|---|
| Pattern mode | `L` | Plays the current pattern in a loop. Right-click the button to show the Channel rack |
| Song mode | `L` | Plays the Playlist arrangement. Right-click the button to show the Playlist |
| Play | `Space` | Plays. Pressed again, it stops and goes back to where it started. `Ctrl+Space` pauses |
| Stop | | Stops and returns to the start. Double-click also sends all notes off |
| Record | `R` | Recording on or off. See [Recording](/wiki/recording/) |

## The hint bar

At rest the hint bar reads `Point at anything for help`, with `F1` for the shortcut sheet, `F5` to `F9` for the windows and `Space` for play and stop. Over a control it names the control and lists what dragging, `Ctrl`, `Alt` and right-click do there. Long jobs such as the background plugin scan report their progress in it.

## The five windows

| Window | Key | What it is for |
|---|---|---|
| [Playlist](/wiki/playlist/) | `F5` | Arranging pattern, audio and automation clips into a song |
| [Channel rack](/wiki/channel-rack/) | `F6` | One row per instrument, with the steps of the current pattern |
| [Piano roll](/wiki/piano-roll/) | `F7` | The notes of one channel in the current pattern |
| [Mixer](/wiki/mixer/) | `F9` | Inserts with faders, effects and routing |
| [Browser](/wiki/browser/) | `Alt+F8` | Samples, presets, the project's contents and the plugin database |

Each key shows the window and gives it focus, or hides it when it already has focus. `F8` alone opens the plugin picker, which is not one of the five. The toolbar's window buttons do the same as the keys. Right-clicking one shows that window maximized. `Enter` maximizes or restores the Playlist.

## How windows move and dock

Every window has a title strip. On its left is a menu button with the window's own options, then `Detached (own window)`, `Maximized` and `Close`. On its right are the maximize and close buttons.

- Drag the title strip to move a window. It snaps to the other windows and to the edges. Hold `Alt` to move without snapping.
- Drag an edge to resize.
- `Detached (own window)` turns the window into a separate system window, for a second monitor. A detached window has a `Dock` button that puts it back. `View › Detached panels (own window)` lists all five.
- `View › Bring back off-screen panels` recovers a window that ended up outside the desktop.
- `View › Save layout as...` stores the arrangement under a name, and `View › Load layout` brings one back.

The Browser is the background of the desktop: it stays docked at the side and the other windows always stay above it.

## Good to know

- Until you move, resize or detach something, the windows take their default places again whenever the main window changes size. After that, your arrangement is remembered and scales with the window.
- `F12` closes the plugin, channel settings and tool windows. It does not close the five main windows.
- While the typing keyboard piano is on (`Ctrl+T`), letter keys play notes and most single-letter shortcuts are paused.
- `View › Layout style` switches between the Pattern workflow (the default) and the Clip-launch workflow, which adds a clip grid beside the Playlist.

## Related

- [Your first song](/wiki/first-song/)
- [Layouts](/wiki/layouts/)
- [Shortcuts](/wiki/shortcuts/)
- [Settings](/wiki/settings/)

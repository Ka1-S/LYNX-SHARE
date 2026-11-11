---
title: Undo and history
summary: Step back through your edits, and find the backups LYNX SHARE keeps.
status: full
---
![The Undo history window: six steps from Start to Add notes, with Mute: Kick as the current one](/assets/img/wiki/history.webp)

Every edit to the project is a step in the undo history: a note you add, a clip you move, a fader, a routing change, the tempo. `Edit › Undo history...` (`Ctrl+Alt+H`) shows the steps as a list. Separately, LYNX SHARE saves backup copies of the project while you work.

## Go back to an earlier step

1. Choose `Edit › Undo history...`, or press `Ctrl+Alt+H`.
2. The list shows the steps with the newest at the bottom. The first row, `Start`, is the project as it was opened.
3. Click a step. The project goes back to the state right after it, and the later steps turn grey.
4. Click a grey step to go forward again.
5. Make a new edit to continue from the step you chose. The grey steps are then gone.

## The undo keys

`Ctrl+Z` does not work as in most programs. It undoes the last step, and pressed again it redoes it, so it switches between the last two states. To walk further back, use `Ctrl+Alt+Z`.

| Command | Key | What it does |
|---|---|---|
| `Undo` | `Ctrl+Z` | Undoes the last step. After an undo it redoes the next step, and the menu item reads `Undo (redo)` |
| `Undo backwards` | `Ctrl+Alt+Z` | Always steps back, one step per press |
| `Redo` | `Ctrl+Y` or `Ctrl+Shift+Z` | Steps forward |
| `Undo history...` | `Ctrl+Alt+H` | Opens the list |

After each undo or redo, the hint bar names the step and shows the level, for example `Level 2/3`.

If you prefer the usual behaviour, turn on `Alternate undo mode (classic undo, instead of undo/redo on the same shortcut)` in `Options › General settings...`. `Ctrl+Z` then always steps back.

## What is a step and what is not

- Edits to the project are steps: notes, patterns, clips, channels, mixer levels and routing, effects, automation, tempo, mutes.
- A recording is a step. So is merging a part in an [online session](/wiki/online-sessions/), and `Comp these takes`.
- Things that do not change the project are not steps: zooming, scrolling, moving windows, and the program's own settings.
- Saving and exporting are not undone. The files they wrote stay.
- LYNX SHARE's own plugin windows have a separate undo and redo for that plugin's changes, next to the A and B settings.

## Settings

Both are in `Options › General settings...`.

| Setting | What it does |
|---|---|
| `Maximum undo levels` | How many steps are kept: 25, 50, 100, 200, 500 or 1000. The default is 100 |
| `Alternate undo mode` | Makes `Ctrl+Z` a classic undo |

## Backups

LYNX SHARE saves a backup copy of the open project every 2 minutes by default, and keeps the 10 newest per project. A backup is a normal `.lynx` file in the `autosave` folder inside the settings folder, named after the project with `(autosaved on ...)` and the date and time.

Each time you save over an existing project file, the version that is replaced is copied to the same folder with `(overwritten on ...)` in its name.

| Command | What it does |
|---|---|
| `File › Revert to last backup...` | Replaces the open project with its newest backup, after asking. The project file itself only changes when you save |
| `File › Open autosave folder` | Shows the backups in the file manager |
| `File › Save new version` (`Ctrl+Alt+S`) | Saves as `Song_2`, `Song_3` and so on, next to the current file |

The `Backup` section of `Options › General settings...` has these settings:

| Setting | What it does |
|---|---|
| `Save a backup copy` | `Never`, or an interval in minutes |
| `Backups kept per project` | 1, 3, 5, 10, 20 or 50 |
| `Wait with backups while the song plays or a MIDI keyboard is being played` | Holds a backup back so it does not interrupt you |
| `Longest wait between backups` | 5 to 60 minutes. A backup is made after this time even while you play |

If the last session ended unexpectedly, the next start offers to open the newest backup that session saved.

## Good to know

- The history is not saved with the project. Opening a project or starting a new one empties it.
- When the history is full, the oldest steps are dropped.
- In a live online session, undo and redo only bring back your own edits, never other people's.
- With `Save a backup copy` on `Never`, the copies made when you save over a file are still kept.
- The settings folder is `%APPDATA%\LYNX` on Windows, `~/Library/Application Support/LYNX` on macOS and `~/.config/LYNX` on Linux.

## Related

- [Shortcuts](/wiki/shortcuts/)
- [Settings](/wiki/settings/)
- [File formats](/wiki/file-formats/)
- [Troubleshooting](/wiki/troubleshooting/)
- [What only LYNX SHARE does: crash safety](/why/#crash)

---
title: Browser
summary: Find and preview samples, presets and plugins, and drag them into the project.
status: full
---
The Browser lists your sample folders, the contents of the current project and the plugins LYNX SHARE knows. Press `Alt+F8` to show or hide it. `F8` alone opens the plugin picker, a separate full-screen view of every instrument and effect.

## Tabs

| Tab | Shows |
|---|---|
| ALL | The current project and every sample folder |
| PROJECT | Only what the current project uses: patterns, effects, generators, automation, audio files |
| STARRED | The files and folders you starred (right-click, then `Starred`) |

## Browse and preview

- Click a folder to open or close it. Right-click it for a menu (collapse all, show in the file manager).
- Click an audio file to hear it. The preview strip under the list shows its waveform. Click the strip to hear the file again, or drag from it as if it were the file.
- With the Browser focused, `Up` and `Down` step through the files and preview each one. `Left` closes a folder or goes to its parent, `Right` opens it.

## Use a sample

| Action | Result |
|---|---|
| Double-click | New channel with the sample |
| Drag to the Channel rack | New channel, or drop on a row to load it there |
| Drag to the Playlist | Audio clip |
| `Enter` | Loads the file into the selected channel |
| `Shift+Up` / `Shift+Down` | Steps to the previous or next file and loads it into the selected channel |

The last one lets you audition different samples in place while the pattern plays.

## Other file types

- **Project**: double-click opens it.
- **MIDI file**: double-click opens it as a project.
- **SoundFont or SFZ**: click previews it, double-click makes a LYNX Soundfont channel (or loads it into the selected one).
- **Mixer preset**: double-click loads it onto the selected Mixer inserts, or drag it onto an insert.
- **Effect preset**: drag it onto a Mixer insert.

## Plugin database

The Plugin database entry lists LYNX SHARE's built-in instruments and effects and the installed plugins. `Shift+F8` jumps to it.

- Double-click an instrument to add it as a new channel, or drag it onto the Channel rack.
- Double-click an effect to add it to the selected Mixer insert, or drag it onto an insert or an effect slot.
- `F2` renames the selected plugin.

## The PROJECT tab

Patterns listed here can be clicked to make them the current pattern, double-clicked to open them in the [Piano roll](/wiki/piano-roll/), or dragged into the [Playlist](/wiki/playlist/). The current project section also lists the undo history: click a step to go back to it.

## Search

Press `Ctrl+F` to put the cursor in the search box. It searches the selected folder (or every folder) and below. In the PROJECT tab it searches the project's items.

All words must match. The box also takes `OR`, a minus sign to exclude a word, and `*` wildcards:

```
kick -loop
snare OR clap
hat*.wav
```

The folders are indexed in the background. `Refresh search index` in the Browser's menu rebuilds the index.

## Your own folders

Use `Add folder` in the Browser's folder list to add a folder of samples, projects or SoundFonts. Removing a folder from the list does not delete it from the disk. `Options › File settings` has the search folders.

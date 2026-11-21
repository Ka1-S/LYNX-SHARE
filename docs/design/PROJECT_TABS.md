# Several projects at once ("project tabs") — design and stages

Issue #232 (round 190ns). A LYNX SHARE extra: FL Studio 2026 opens one project per program window (opening another
replaces it; a second FL window is a second copy of the program). The request: keep several `.lynx` / `.flp` projects
open in one LYNX SHARE, copy items between them and compare mixes or masters of an album.

## What other programs do

From the programs' manuals and release notes (only FL Studio and Live are installed on the study PC):

| Program | Several projects | Which one plays | Between projects |
|---|---|---|---|
| FL Studio 2026 | no; a second copy of the program can run | each copy has its own audio device | the clipboard of one copy is not the other's (except text) |
| Ableton Live 12 | no; one Set per program (opening one closes the other) | the open Set | the browser lists another Set's tracks: drag one in to import it |
| Bitwig Studio | yes, a tab per project | only the active project's audio engine runs; the others are switched off (their plugins unloaded) until activated | copy / paste and drag clips, devices and tracks between tabs |
| Studio One | yes, songs open side by side in tabs | the active song | copy / paste between songs |
| Cubase | yes, several projects open | only the "activated" project plays | copy / paste and drag between projects |
| REAPER | yes, project tabs | the active tab; an option lets background tabs keep playing | copy / paste between tabs |

The common pattern: one window, a tab per project, **one project plays at a time** (the audio device and the CPU go to
the active one), copy and paste work across tabs. The cost every program pays is memory: an open project keeps its
samples and, unless it is switched off, its plugins.

## What it costs in LYNX SHARE

- Everything in the main window (Playlist, Channel Rack, Piano Roll, Mixer, Browser, detail views, dialogs) is built
  around one `AudioEngine` and one `Song`, passed by reference when each panel is made. Real tabs need either a full set
  of panels per tab (memory and start-up time per tab) or panels that can be pointed at another engine (every panel,
  every cached index, every timer).
- One audio device: only the active tab's engine may be connected; switching tabs must stop playback, disconnect one
  engine and connect the other without a click (the engine already supports being stopped and rewired for the audio
  settings page).
- Memory: each inactive project keeps its samples (the sample pool is per engine) and its plugin instances. A large
  project with 70 plugins is 1-3 GB. Like Bitwig, inactive tabs should be able to unload their plugins (their state
  kept as on save) and load them again on activation.
- Undo, autosave, crash recovery, the title bar, recent files, Online Sessions and remote control are all per
  project today but kept by `MainComponent` as single members: each becomes per tab.

## Stages

1. **Open in new window (done in 0.1.31).** `File › Open in new window ›` *Open...*, *Start-up project* and the ten
   recent projects. It starts another LYNX SHARE window (its own process, as a second start already is) with that
   project; the one open stays as it is. Each window has its own audio device (a driver that only one program may use
   at a time, e.g. ASIO, plays in the first window only), its own undo and autosave. Cost: one more process (the
   program's own memory plus the project's samples and plugins). No change to how one window works.
2. **Copy and paste between windows (done in 0.1.31, round 190nz).** Every in-window copy also goes to the system
   clipboard as LYNX's own text: a first line `LYNX SHARE clipboard 1`, a line with the kind, a stamp and the process,
   then a versioned JSON document (`src/engine/CrossClipboard.h`). A document larger than 64 KB (plugin states, many
   notes) goes to a file in the settings folder's `Clipboard` folder (the four newest are kept) and the clipboard holds
   its path. The project parts are written by the project's own JSON code, so a copy keeps whatever a `.lynx` keeps.
   A paste first looks there: when the newest LYNX copy on the system clipboard was made by another window (or before a
   restart) it pastes that one; otherwise the window's own clipboard pastes as before (text copied in another program
   stays text). Works on Windows, Linux (X11; when the window that copied has closed and nothing keeps its clipboard,
   the copy is read from the folder) and macOS. What travels:
   - Piano roll notes (Ctrl+C, Copy ghost notes): pasted on the channel being edited.
   - Playlist and clip grid clips with what they play (`src/core/CrossPaste.h`): the patterns and their notes, the audio
     clip channels (their samples by path, not copied), the automation clips (a target on a channel follows the
     channel), clip envelopes and grooves. A channel the other project already has (same name and same instrument,
     plugin or sample) is that project's own; the others are added at the end of the Channel Rack. Pasting the same
     copy again adds no second pattern or channel. One undo step.
   - Channel Rack steps (Ctrl+C / Ctrl+V, FL's), and new in the channel menu: **Copy channel / Copy N channels** and
     **Paste channels** (the instruments with their plugins' current state and settings, pasted below the selection as
     new channels; their insert numbers kept when the project has them).
   - Mixer track menu › File › **Copy mixer track state** / **Paste mixer track state** (the effects with their plugins'
     current state, level, pan, EQ, name and colour; routing stays).
   - Selected automation points (Playlist, Ctrl+C / Ctrl+V on a curve).

   The windows also know of each other (`src/ui/LynxWindows.h`): each writes an entry in the settings folder's
   `Windows` folder and has an inbox (a FIFO on Linux and macOS, a named pipe on Windows; a thread that waits, no timer,
   no network socket). While two or more are open the menu bar has **WINDOW** before HELP: the open windows (each with
   the device it plays through), a click brings one to the front; Open in new window.... An audio driver only one program
   may use at a time (ASIO, an exclusive device): the second window shows a bar under the toolbar, "<device> plays in
   another LYNX SHARE window (<project>): only one program can use this audio device at a time", with **Make this window
   the one that plays** (`src/ui/WindowAudio.h`): the other window stops, lets the device go and shows the same bar; this
   one opens it. A window recording or rendering says no. The device saved in Settings › Audio stays the one both
   windows want: the window without it never saves its stand-in over it (in stage 1 the second window did). When the
   window that had the device closes, the one that let it go takes it back.

   Not built: dragging a pattern, clip or channel from one window into another (the design did not include it; the
   operating system's drag and drop between two processes would need a drag format of its own on each system). Copy and
   paste covers the same moves.
3. **Tabs in one window** (estimate: 4-6 weeks, a release of its own). A tab strip above the toolbar; a tab = one
   engine + one set of panels, created on first view; only the active tab is connected to the audio device; switching
   stops playback (a setting may keep the old tab playing until the new one starts, as REAPER's option); "Unload plugins
   of inactive tabs" on by default; Ctrl+Tab switches; closing a tab asks to save. Needs: an engine per tab, the
   panels' timers paused while hidden (no idle cost), per-tab undo / autosave / crash recovery, a memory meter per tab.
   Risks: memory with several large projects, plugins that misbehave when two instances of themselves live in one
   process with one suspended.

## Owner decision

Stages 1 and 2 are built. Stage 3 is the real "tabs" and is large: build it (0.1.32 or later), or stay with windows?
Recommended: stay with windows for now, stage 3 only if users still ask after using copy and paste between windows.

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
2. **Copy and paste between windows** (estimate: 3-4 days). Clipboard items (notes, Playlist clips, patterns, channels,
   mixer track states, automation) also go to the system clipboard as LYNX data (versioned JSON with the samples'
   paths), so a paste in another window or a later session works. The in-window clipboard stays as it is.
3. **Tabs in one window** (estimate: 4-6 weeks, a release of its own). A tab strip above the toolbar; a tab = one
   engine + one set of panels, created on first view; only the active tab is connected to the audio device; switching
   stops playback (a setting may keep the old tab playing until the new one starts, as REAPER's option); "Unload plugins
   of inactive tabs" on by default; Ctrl+Tab switches; closing a tab asks to save. Needs: an engine per tab, the
   panels' timers paused while hidden (no idle cost), per-tab undo / autosave / crash recovery, a memory meter per tab.
   Risks: memory with several large projects, plugins that misbehave when two instances of themselves live in one
   process with one suspended.

## Owner decision

Stage 1 is built. Stage 2 is cheap and useful for the album case. Stage 3 is the real "tabs" and is large: build it
(0.1.32 or later), or stay with windows? Recommended: stage 2 next, stage 3 only if users still ask after using windows.

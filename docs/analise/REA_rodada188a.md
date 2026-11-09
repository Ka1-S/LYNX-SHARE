# Round 188a: issue #116, docked plugin windows, slice 2 (all panels native)

Design: `REA_rodada181c.md` (approved). Slice 1: `REA_rodada186a.md` §2. This slice is step (c) of that design.

## 1. What FL does (FL 2026 v26.1.7, 2026-11-09 07:59, shots E:\lynx-work\dock188a\shots\fl-*.png)
Default template, the 808 Kick's Sampler opened from the Channel rack (docked, FL's default), Mixer already shown:
- fl-03: the plugin window opens in front of the Channel rack and the Mixer.
- fl-04: a click on the Mixer's title brings the Mixer in front of the plugin; the rack stays behind it.
- fl-05: a click on the plugin's title brings it back in front of the Mixer.
- fl-06: F7 shows the Piano roll (maximised) in front of everything, the plugin included. fl-07: F7 again hides it,
  the plugin is where it was.
- fl-08: F5 shows the Playlist in front of the plugin. fl-09: a click on the plugin brings it back in front of the
  Playlist. fl-10: a click on the Playlist's title strip brings the Playlist in front again.
- Detached (from 181c, f116h–j): a detached plugin window stays on top whatever panel is clicked.

So every floating panel behaves like the Channel rack: the clicked (or called) one comes in front of the docked
plugin windows, the others keep their place.

## 2. What LYNX does now (LYNX_DOCKED_PLUGINS=1; without it nothing changes)
- `Workspace::isNativePanel`: the Playlist, Channel rack, Piano roll and Mixer frames are native child windows of the
  main window (unless detached). The Browser, a background panel always below them, stays a plain component.
- `applyLayout` no longer brings every native panel to the front: a native panel moves only when it is out of order,
  just above the panel before it (`nativechild::keepAbove`), so a docked plugin window keeps its place after any
  layout change. A panel shown by its key (F5..F9, `show` / `toggle`) or maximised comes in front of the docked
  plugin windows, as in fl-06 / fl-08. A click on a panel brings it in front (slice 1).
- The hover value tip (Settings › General) also reads controls in native panels.
- `--plugwintest` dock mode now runs the real main component (real panels) and checks, for the Channel rack,
  Playlist, Piano roll and Mixer: native child, plugin in front when opened, the panel in front after its click, the
  others still behind the plugin, the plugin in front after its click; then a layout change elsewhere (Browser hidden
  and shown) keeps the plugin in front, F9's `show (mixer)` brings the Mixer in front, and the Detached round trip
  (owned top-level window above every clicked panel, saved flag, docked again). `LYNX_PLUGWIN_STEP_MS` sets how
  long each state is held (default 3000 ms, for screenshots).
- `--plugwintest` with an instrument: the test adds the channel after the song's own channels and now looks for the
  plugin in that channel (it looked at channel 0, so instruments were "not hosted").

## 3. Best way?
- **FL:** above (shots fl-03…fl-10, 181c f116e–j).
- **Ableton:** Live has no floating panels; plugin windows are separate top-level windows that always float over the
  main window (Options › "Auto-Hide Plug-In Windows", "Multiple Plug-In Windows"). That is LYNX's Detached.
- **Users:** FL users get lost when a docked plugin goes behind a panel and ask for "detached by default" / a hotkey
  (forum.image-line.com/viewtopic.php?p=1874369 "Detach all plugins doesn't work", p=2031644 "Plugins always on top",
  p=1692944 "Always visible option for plugin windows").
- **Verdict:** FL-exact docking as the default (the approved design), keeping the per-plugin Detached toggle. Worth
  adding later, as an option: FL's General › "Detach all new plugins" (seen in 186a), which covers the users'
  "detached by default" request in FL's own way.

## 4. Known limits left
- Lightweight overlays drawn over the workspace (setup overlay, drag highlights painted by the main component) stay
  under the native panels; drag images are desktop windows, so drags show.
- No fade when a native panel opens or closes. DPI changes are not passed to docked editors yet.
- The .flp Detached bit is still not mapped (step d).

## 5. Proof (cloud build run 42605112824, "round build: success")
- `--plugwintest` dock mode, ValhallaFreqEcho (effect) and LYNX FM10 (instrument, the case that was "not hosted"):
  every check `=1`, `dock_result=OK`, `result=OK` (E:\lynx-work\dock188a\rep-effA.txt, rep-instA.txt).
- `--selftest`: result=OK.
- Idle CPU of the plain app, 30 s after 15 s settling, alternating runs: off 14.1 % / 7.9 %, docked 7.1 % / 14.4 %
  of one core. The spread is between runs, not between modes: no idle cost measured.
- FL | LYNX sheets: E:\lynx-work\dock188a\shots\116-4…116-11.

## 6. Slice 3: docked by default (coordinator: approved once the visible regressions are fixed)
- **Default flipped:** `nativechild::dockingEnabled()` is on unless `LYNX_DOCKED_PLUGINS=0` (then every plugin window is
  detached and the panels are plain components, as before #116).
- **Setup wizard** (first start, Options › Setup wizard): hosted in a native child window of its own
  (`nativechild::makeOverlayHost`), opaque, showing a picture of the window as drawn (PrintWindow with the full content, so
  plugin editors are in it) with the wizard's dimmer and card on top. It stays in front when a panel or plugin is
  brought forward (`raiseOverlays`). Without docking, or before the main window has a native window, it is the plain
  overlay as before.
- **Drops:** `matchWindowOrderToScreen` (called when a drag starts) now orders JUCE's desktop list by the native
  stacking order of the main window's child windows too, so JUCE's drop target search (from the end of that list)
  finds the panel or plugin in front.
- **DPI:** JUCE positions a child window with the scale of the display at its logical position taken as a screen
  point, and child windows never get WM_DPICHANGED. `nativechild::place` now puts each native child at its bounds in
  the main window's physical pixels (UI zoom included), and on a main-window scale change (`Workspace::PeerWatch`
  listens to the main peer) every native child is placed again and its peer's scale listeners are called, which is
  what JUCE's hosted VST2/VST3 editors listen to (`NativeScaleFactorNotifier`).
  Machine limit: both monitors here run at 100 %, so a real cross-display DPI change was not reproduced; the test calls
  the main peer's scale listeners as WM_DPICHANGED does and also switches the UI zoom to 125 %.
- **FL's "Detach all new plugins"** (FL: General › Miscellaneous, off by default, fl-04-misc.png; renamed from "Detach
  all plugins" in FL 2025, WhatsNew 16374, so it concerns new plugins only): Settings › General › Editing, off. The
  engine marks the plugins an edit adds (an id the song has more of than before, with no saved state yet) as Detached
  (`detachNewPlugins`, core/Song.h); loaded and cloned plugins keep their own flag. Strings in all 14 languages.
- Proof (cloud build 42618768839 "round build: success", selftest OK): `rep-effD.txt` / `rep-instD.txt` all checks 1,
  dock_result=OK, result=OK,
  incl. setup_overlay_* (native child, above plugin and every panel, stays in front), drop_order_matches_screen,
  native_children_placed_at_125, docked_editor_told_the_scale; `--windowtest` result=OK with docking on by default.
  Core test `detach_new_plugins_marks_only_the_plugins_an_edit_added`. Idle CPU, plain app, 30 s each, alternating:
  LYNX_DOCKED_PLUGINS=0 13.8 / 8.8 / 5.9 %, docked (default) 8.7 / 11.9 / 10.8 % of one core: inside the run-to-run
  spread (no timer was added; the native children only repaint what changes).

### Left open (not visible regressions, can follow)
- No fade for native panels when they open or close; the .flp Detached bit.
- The Fruity Dance stage (`DanceStage`, dancers drawn over the workspace by the main component) stays under the native
  panels: it is transparent and animated, so it needs a layered (per-pixel alpha) window. Only projects with a dance
  channel see it. Needs a decision (see the round report).

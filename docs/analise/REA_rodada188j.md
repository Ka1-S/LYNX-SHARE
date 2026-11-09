# Round 188j: issue #116 slice 4, the Fruity Dance dancers in front of the docked panels and plugins

Design: `REA_rodada181c.md`; slices 2 and 3: `REA_rodada188a.md` (its "Left open" note on the dance stage is this slice).

## 1. What FL does (FL 2026 v26.1.7, 2026-11-09 10:43, shots E:\lynx-work\dance188j\shots\fl-*.png, window lists fl\wins*.txt)
- Each dancer is a window of its own: class `TSynthOSDForm`, a top-level **layered** window (WS_EX_LAYERED, per-pixel
  alpha through `UpdateLayeredWindow`, which `Fruity dance_x64.dll` loads at run time) the size of the dancer
  (110 x 170 at the default size), WS_EX_TOOLWINDOW (no taskbar entry).
- Fruity Dance's page has two window options, both on for a new channel (fl-10): **Keep in front** (hint "Keep window on
  top": WS_EX_TOPMOST) and **On desktop** (hint "Don't minimize window along with application": no owner; off = owned by
  FL's application window).
- fl-10: a new Fruity Dance's dancer stands in front of its own plugin window and the panels. fl-12: with "Keep in front"
  off (WS_EX_TOPMOST gone) and "On desktop" on, the dancer is behind FL's whole main window (a plain desktop window).
- A dancer whose saved place is not readable (the LYNX text state imported into FL) is placed far off the screen.

## 2. What LYNX does now
- LYNX's dancers belong to the main window (their place is in its coordinates, round 165e). With docked plugin windows
  (the default since slice 3) the panels and plugin editors are native child windows, and the stage drawn by the main
  component was hidden under them.
- Each shown dancer now has a window of its own, like FL's: a small native child window of the main window over the
  dancer's reach, not opaque, so JUCE makes it a layered window with per-pixel alpha (Windows 8+ allows layered child
  windows; LYNX's manifest declares Windows 10). It is a "floater" (`nativechild::addFloater`): `raiseOverlays`, which runs
  on every stacking change of the panels and docked plugins, brings the floaters in front, then the overlays (the setup
  wizard stays in front of the dancers); twice a second the stage also checks that nothing is above them
  (`keepFloatersInFront`). Moving or minimising the main window carries them (child windows), the UI zoom places them
  again (`followParentScale`).
- The mouse: the window's transparent pixels let clicks through to the panel or plugin under them (layered-window hit
  testing); only the body takes it (drag, right-click menu), as before. No keyboard focus.
- No shown dancer: no window. A project without a dance channel keeps the stage's 2 Hz scan only.
- Without a native main window (pictures, `--snapshot`), with docking off (LYNX_DOCKED_PLUGINS=0), or on Linux, the stage
  draws the dancers as before. Linux: X11 child windows are drawn without blending (a transparent child shows black),
  so the dancers stay under the native panels there. macOS: native children are subviews, which blend; the same path
  as Windows (built by CI, not run here).
- Differences from FL, kept on purpose: LYNX has no "Keep in front" / "On desktop" options; its dancers stay inside the
  main window (in front of everything in it) and do not float over other programs or detached plugin windows.

## 3. Verification
- `--plugwintest` dock mode, three more steps: a dance channel whose dancer stands on the docked plugin window:
  `dancer_window_native_child`, `dancer_window_layered`, `dancer_above_plugin`, `dancer_above_<panel>` for the four
  panels; after a panel click, F7 and a click on the plugin `dancer_stays_in_front`; `dancer_takes_its_body_clicks`
  (WindowFromPoint on an opaque body pixel = the dancer's window), `dancer_lets_clicks_through` (on a transparent pixel
  = the window under it), `stage_leaves_the_mouse`; the channel removed: `dancer_window_gone`.

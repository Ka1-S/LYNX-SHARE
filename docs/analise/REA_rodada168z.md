# Round 168z: issue #45 (context menus on multi-monitor Linux, KDE Plasma Wayland)

LYNX-own fix (no FL parity question: FL has no Linux build). JUCE 8.0.4 talks X11 to XWayland.

## Reporter's words | evidence | LYNX before | LYNX now

### (a) "Undocking any panel into its own window, or opening a settings menu makes most context menus not open about 90% of the time"
- Evidence (JUCE 8.0.4 sources, `juce_PopupMenu.cpp` MouseSourceState::checkButtonState / MenuWindow::doesAnyJuceCompHaveFocus,
  `juce_XWindowSystem_linux.cpp` handleFocusIn/OutEvent, `juce_ComponentPeer.cpp` handleFocusGain,
  `juce_ModalComponentManager.cpp` bringModalComponentsToFront): every 50 ms an open menu closes itself ("the app lost
  the focus") when no LYNX window has the X focus (`Process::isForegroundProcess()` = one static flag on Linux, cleared by
  any FocusOut) and the mouse is not over the menu. A menu opens beside the mouse, so the mouse is not over it.
  With a second LYNX window, a right-click in the window that is not focused makes KWin move the focus to it; when it
  arrives JUCE sees that window blocked by the (modal) menu and calls bringModalComponentsToFront(), which gives the X
  focus to the menu's override-redirect window. XWayland/KWin hand it back, the next FocusIn repeats it: the focus
  ping-pongs and the 20 Hz check often lands in a "nobody has it" moment. One window only: the click never moves the
  focus, which is why it only starts after undocking a panel or opening a settings window.
- Not reproduced here (no KWin/XWayland in WSL; Xvfb has no window manager). The reporter is asked to test 0.1.23.
- Now (cmake/LynxPatchJuce.cmake, Linux/BSD only):
  - fix 8: a window that ignores key presses (menus, tooltips) never takes the X focus (keys still reach the modal menu).
  - fix 7: the mouse over one of LYNX's own windows counts as "still LYNX's", so the focus moving between LYNX
    windows can't close a menu. Alt+Tab away with the mouse resting on LYNX keeps the menu until the mouse leaves LYNX
    or clicks (accepted trade-off).
- ui.log (the UI trace) now starts with the display list JUCE sees (area, scale, main), for multi-monitor reports.

### (b) "context submenus that would exceed the primary monitor's boundaries appear on a wrong monitor ... they self-close"
- Evidence: video 2 (MIDI CC #21's submenu opens left of the CC list, #22's on the monitor at the right). The CC
  #0..#31 list is ~32 rows tall and reaches the bottom of the primary; JUCE picks the display under the item that opened
  the submenu (`getParentArea (target.getCentre())`), LYNX's own placement (round 167p, Theme.cpp) the display holding
  most of the parent menu, and only when it recognised the item (JUCE's submenu exactly beside it).
- Now: every submenu is kept on the display of the ROOT menu, i.e. of the window the menu came from:
  - core/MenuPlacement.h `displayFor()` (pure, tested): display containing the root menu's centre; overlapping display
    rectangles → the one holding most of the root menu, then the first (main); none → the one holding most of it, then
    the nearest. Theme.cpp skips JUCE displays with an empty totalArea: JUCE 8.0.4's Displays::updateToLogical leaves a
    display it can't place beside the others at totalArea 0,0,0,0 with its userArea at 0,0 over the main display —
    seen here under Xvfb + Xinerama (ui.log: `display 0 0 0 0 user 0 0 1280 1024`), so JUCE's own display list can
    put a phantom screen over the primary.
  - Theme.cpp preparePopupMenuWindow: the hovered item opens the submenu even when JUCE did not put it beside the item;
    the screen is displayFor(root menu).
  - JUCE fix 6 (all platforms): JUCE's own submenu position/size uses the root menu's display too (single monitor:
    unchanged).
- Tests: tests/Menus168zTests.inc (the reporter's 3-monitor layout: CC #22's submenu inside the primary, a sweep of
  every CC row and 3 submenu sizes, right/bottom monitors, overlapping lists, negative coordinates, no display).

## Round 168z2: mixed scale factors (coordinator follow-up)
- The reporter's monitors: 2560x1440 @115 % (primary), 1920x1080 @100 % (right, top-aligned), 1920x1080 @115 % (below).
- How JUCE sees them (JUCE 8.0.4 sources): XWayland gives X11 clients one pixel space; with Plasma's default "apply
  scaling themselves" X pixels = logical x 1.15 (the 100 % monitor is 2208x1242 there), with "scaled by the system"
  X pixels = KDE's logical pixels. JUCE's per-monitor scale on Linux comes from XSettings Gdk/WindowScalingFactor
  (KDE: integer), else dconf/gsettings, else round(dpi / 96) per output, times LYNX's UI zoom; then
  Displays::updateToLogical lays the monitors out by exact physical contact. Menus, windows and Displays all use those
  logical coordinates, so the placement math is scale-independent as long as the display list is right.
- Gap found: a monitor that touches no other one exactly (rounding of fractional scaling, or Xvfb's overlapping
  Xinerama screens) is never reached by updateToLogical: totalArea 0,0,0,0 and userArea at 0,0 over the primary (seen
  in our Xvfb ui.log). JUCE fix 9 (cmake/LynxPatchJuce.cmake, juce_Displays.cpp, all platforms, only that case) now gives
  it its physical rectangle / its scale; Theme.cpp still skips empty displays.
- Tests: menus168z2_reporter_layout_with_scales: both KDE X11 modes and a JUCE per-monitor 2x case, UI zoom 1/1.15/
  1.25/2, a root menu on each monitor, a 3-level submenu chain from every 7th row: every submenu inside the root's
  monitor; a 2 px gap layout keeps a right-monitor menu on the right monitor.

## Checks
- build-local: 1 934 007 checks, 0 failures. Linux GUI rebuilt after the last change; Xvfb run repeated.
- Linux GUI build of this branch (system g++, patched JUCE copy): all four JUCE fixes applied and compiled.
- Xvfb :93 with two Xinerama screens (1600x900 + 1280x1024, JUCE reads them as a 1600x900 main + a phantom 0,0 one):
  channel menu → Insert (flipped left, no room on the right) → FL engines (moved up to the 900 px display bottom, not
  the 1024 px X root). A menu stays open when the X focus is moved to the root window with the mouse over LYNX
  (new build). The old build (main, Oct 31) also kept it there: Xvfb has no window manager, so (a)'s KWin focus
  ping-pong can't be reproduced here.
- Windows (this branch's LYNX.exe, fresh profile, test mode, real mouse): channel menu, Insert beside the outer edge,
  FL engines third level moved up to fit; with the Channel Rack at the right edge the menu shifts left and the
  submenus still open beside it. Shots `/mnt/e/lynx-work/menus45z/s/w2s..w8s.png`.

## Round 168z3: (a) reproduced with KWin and fixed (coordinator follow-up)
- Setup (VPS, Ubuntu 26.04): Xvfb 1920x1080 + kwin_x11 as the window manager, LYNX with a fresh profile and test
  mode, main window 1900x1060, Channel Rack undocked (≡ > Detached). xdotool clicks the rack (or an empty spot of the
  main window), then File on the main menu bar (press and release 0.1 s apart); a menu = a new visible X window.
  The realtime-hint flag is pre-created (the "Real-time priority" notice is modal and blocked menus at random).
- Results (File menus opened):

  | build | docked | rack clicked first | main clicked first |
  |---|---|---|---|
  | 0.1.22 (AppImage binary) | 10/10 | 0/20 | 1/10 |
  | main 05f38e80 (JUCE fixes 6-9) | 10/10 | 0/20 | 0/10 |
  | + fix 10 only | 10/10 | 0/20 | 0/10 |
  | + fixes 10 and 11 (this branch) | 10/10 | 20/20 | 10/10 |

- Cause 1 (fix 10): LYNX keeps undocked panels above the main window (matchWindowOrderToScreen, toFront (false)
  after every click in the main window). JUCE's X11 toFront ignores "false" and always sends _NET_ACTIVE_WINDOW, so
  KWin activated the panel: the keyboard focus left the main window after each click there. Now toFront (false) only
  raises (XMapRaised). After the fix the focus stays on the main window; the panel still stays above it.
- Cause 2 (fix 11), found with a traced build: the menu was created, then dismissed at once by
  inputAttemptWhenModal. JUCE calls it for every ConfigureNotify on a window with a native title bar, or on the WM
  frame around one of our windows ("the title bar is being dragged"); KWin sends those for the undocked panel while the
  menu opens (restack, re-sent geometry). Now only a window/frame whose geometry really changed dismisses the menus.
- Screens: `/mnt/e/lynx-work/menus45z3/` (dbg2-rack-open.png: File menu open with the undocked rack above).

## Where I stopped
- 168z/168z2 landed (387df51d, 05f38e80). 168z3: fixes 10 and 11 verified on the VPS (table above); land with
  train.sh menus45z3; the reporter to test 0.1.23.

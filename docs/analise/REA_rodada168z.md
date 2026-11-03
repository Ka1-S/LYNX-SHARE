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

## Where I stopped
- Done: code + tests, wip pushed (wip/menus45z).
- Done: build-local, Linux GUI build + Xvfb, Windows GUI check. Next: land with train.sh menus45z; reporter to test 0.1.23 for (a).

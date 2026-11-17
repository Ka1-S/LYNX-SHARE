# REA round 190jz: the docked Browser, audio clip fade handles, Home (issues #192, #194, #195)

Sources: FL Studio 2026 (the study install) driven under the GUI and FL locks on 2026-11-16 23:30-23:43, FL's form
resources (`E:\lynx-work\edscr167\dfmall`: TFruityLoopsMainForm, TSampleListForm, TEventEditForm, TShortcutsModule),
FL's strings (`E:\lynx-work\userfix190he\flengine-strings.txt`), FL's online manual and FL's saved files. Shots:
`E:\lynx-work\issues190jz\shots\fl\fl-19x-*.png`, raw frames in `E:\lynx-work\issues190jz\gui\raw`. Clean-room: only
behaviour and measurements; LYNX draws its own vectors.

## 1. #192 The Browser is docked, and a maximized window fills what it leaves

- [DFM] The main form has two dock sites, `SBLeftDockSite` (Align = alLeft, DockSite = True) and `SBRightDockSite`
  (alRight), sharing `SBLeftDockSiteDockDrop / DockOver / UnDock`. The Browser form (`TSampleListForm`) has
  `DragKind = dkDock` and `OnStartDock / OnEndDock`. Its View menu has `Auto-hide` and `Hide while dragging (when detached)`.
  The manual (Browser page): "The Browser can be docked to either the left or right side of the main window, by dragging it
  to either side of the screen"; "undock the Browser and drop it anywhere on the screen"; Close = Alt+F8.
- [GUI] `fl-192-browser-left-playlist-max.png`: the maximized Playlist starts where the docked Browser ends.
  `fl-192-browser-closed-playlist-max.png`: after Alt+F8 it fills the whole width.
- [GUI] Restored windows are placed in the desktop beside the dock site and move with it:
  `fl-192-restored-windows-browser-shown.png` → `-closed.png`: with the Browser closed the (maximized) Mixer starts at 0
  and the restored Playlist's left edge moved left by the Browser's width while its right edge (at the window's right
  edge) stayed, so it got wider. Shown again, both went back.
- [GUI] Dragging the Browser's title draws a drop outline (`fl-192-browser-drag-outline.png`); let go away from an edge it
  becomes a floating window (`fl-192-browser-detached.png`, the other windows take the whole desktop); let go on the right
  edge it docks there (`fl-192-browser-right-playlist-max.png`: the maximized Playlist fills the left part; a restored
  Playlist that touched the right edge kept touching the new edge, the Browser's left side). Dragged to the left edge it
  docks back (`fl-192-browser-left-again.png`).

LYNX before: the Browser was a background panel at the left; a maximized panel filled the whole desktop and covered it.
LYNX now (`core/Workspace.h` `Dock`, `freeArea`, `maximisedArea`, `followFreeArea`; `ui/Workspace.cpp`): docked left
(default) or right, full height at its edge, width set by its inner edge; maximized panels fill the free area; the other
panels move with the free area by FL's rule above; the title drag docks it at the side it is let go on (off the main
window: detached there, LYNX's own window as before); Window options › Docked left / Docked right. Layout files get a
`dock left|right|none` line; a file without it is read as before (a full-height Browser at an edge is docked there, one
moved elsewhere stays free and maximized panels cover it, the old behaviour). The snapshot / --paintbench / --uiperf views
keep the whole desktop (the Browser undocked behind them), so pictures and numbers stay comparable.

Not done (FL items not asked for in #192): the Browser's Auto-hide, the drop outline while dragging, a detached Browser that
docks by being dragged onto the main window's edge (LYNX: the dock button or Window options).

## 2. #194 Fade handles, tension and hints [GUI + saved files]

- Manual (Playlist › Audio Clips): "Fade handles are the triangles on the upper edge of the Clip", "Drag horizontally",
  "Hold (Alt) ... snap", "If you hold (Alt) then click, it will reset the handle"; "Tension handles are the circles on the
  Envelopes. Drag vertically"; "Reset - (Alt/Opt+Click)"; "In manual mode fade length will remain as set".
  (`NewClipFadeEditBtn` "Shift+F Show fade editing controls" exists in the DFM with Visible = False; FL 2026 shows the
  handles without it.)
- Measured on FL 2026 (track at 3x height, 1920x1080, 100 %): no handle while the mouse is elsewhere
  (`fl-194-idle-clip.png`); over the clip, a right-angle triangle of ~4 px in the waveform's colour (#85C696 on that clip)
  in each top corner of the body under the header (`fl-194-hover-clip.png`); the hovered one white (#F3FFFF), ~7 px
  (`fl-194-hover-fade-handle.png`), with a 1 px light line down the clip (`fl-194-hover-fade-out-manual.png`).
- A drag draws the curve in the waveform's colour (~1.6 px, no shading; the waveform itself is not scaled: "fade previews"
  off) with the tension circle at the middle of the fade: a ~7 px ring on the curve, hovered a ~12 px disc (#C1FFD2 with a
  #E2EEEF rim) (`fl-194-drag-fade-in.png`, `fl-194-hover-tension.png`).
- Hints: hovering a handle "Fade out: 0'00.000'' (auto)  Hold Alt to snap" (not manual) / "Fade out: 0'00.112''  Hold Alt
  to snap" (manual); dragging "Fade in: 0'15.750''  Hold Alt to snap"; the circle "Tension: 0%", dragged "Tension: 15%".
- Dragging a handle turns the side manual: FL's saved item had fade flags 0x1 after the fade-in drag.
- Tension: FL's Type in value field reads "Tension ±0-1 or %". Values typed in and saved (item +0x24 fade-in ms, +0x28 its
  tension): v 0.25 → 7.0899, 0.5 → 516.8467, 1.0 → 2672339.25, which is raw = 0.1·(e^(17.10105·v) − 1) to 5 digits
  (Thief's −812.933 = v −0.527, FL's "−53 %"). A drag moves v by about 1/174 per pixel (156 px up: 0 → 90 %, limited at
  100 %); up raises the curve's middle. The voice's curve is round 180d's model (`clipops::fadeTensionCurve`).
- The circle's menu (`fl-194-tension-menu.png`): Reset fade in tension, Create crossfade (greyed alone), Copy value, Paste
  value, Type in value..., Curve shape: Smooth curve / Single curve.

LYNX before: the handles existed (round 189i) but were never drawn, their hit area ignored the header's real height, the
curve was drawn straight for FL's S-curve fades and without the tension, there was no tension handle, the drag hint was
"Setting the fade" and a drag left the side automatic. LYNX now: all of the above as measured (`PlaylistComponent`
`paintFadeHandles`, `fadeTensionMenu`, `clipops::fadeTensionFromHandle / fadeTensionHandle / fadeLevel`). The values are
saved where they were (Clip::fadeIn/fadeOut ticks, fadeInTension/fadeOutTension raw, linearFades, flFadeFlags in .lynx
and .flp item +0x24..+0x38). Export = playback: core test `issues190jz_clip_fades_render_as_drawn_and_the_same_in_any_block_size`
and the self-test's export parity project now has a faded clip with both tensions.

## 3. #195 Home [GUI + DFM + manual]

- Manual, Keyboard shortcuts › Playlist: "Home | Move playback marker to start". `TShortcutsModule` has no action with
  ShortCut 36 (Home): the windows take the key themselves.
- [GUI] (`fl-195-*`): song position 65:04 → Home with the Playlist focused → 1:01:00; with the Piano roll focused → 1:01:00;
  with the Mixer or the Channel rack focused nothing happens. While playing, Home jumps to the start and the song goes on.
  The forum's "L twice" (with the typing keyboard off) is the Pattern / Song switch pressed twice; not checked this round.
- LYNX: `playlist.toStart` and `pianoRoll.toStart` on Home, `transport.toStart` (no key) for the shortcut editor; the
  position goes to 0 and becomes the mark a later Stop returns to (a ruler click at the start, issue #169). LYNX extra
  (off by default, Settings › LYNX extras and the Stop button's menu): REAPER's W, free in FL's main windows (FL binds
  only Alt+W, the Piano roll's Claw machine); the typing keyboard keeps W while it is on.

## Best way?

- FL: as above. Live: the browser is a fixed side panel and the arrangement fills the rest (the Clip-launch layout gets the
  same through the docked Browser); Live draws clip fades as handles at the clip's top corners too. REAPER: W and Home go to
  the start; fade handles at the item's top corners with a curve-shape menu. Users (#192, #194, #195) asked for exactly FL's
  behaviour; nothing here departs from FL except the W key, which stays an off-by-default extra.

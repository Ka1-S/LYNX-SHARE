# Small fixes LYNX applies to the JUCE sources before they are compiled (idempotent: a patched file is left alone).
#
# 1. Linux drag & drop from a Wayland file manager (issue #11, round 165s). On GNOME (mutter) and KDE (KWin) Wayland the
#    compositor's XWayland bridge sends XdndEnter with XDND version 5 to every X11 window, whatever version the window
#    announced in XdndAware. JUCE 8.0.4 (and JUCE's develop branch, 2026-10) ignores any XdndEnter above version 3, so
#    no drop from Nautilus/Dolphin ever reached LYNX. Versions 4 and 5 only add fields a version 3 target may ignore
#    (XdndProxy, XdndFinished's result), so versions 3..5 are accepted now.
# 2. GTK 4 apps (Nautilus 4x+) offer a file drag as "text/plain;charset=utf-8" BEFORE "text/uri-list"; JUCE took the
#    first offered type it knows, got the paths as plain text and delivered a text drag that no file drop target wants.
#    "text/uri-list" is preferred whenever the source offers it.
# 3. A drop JUCE could not take (no type it knows, or the version check above) left `finishAfterDropDataReceived` set,
#    so the NEXT drag was "finished" as soon as its data arrived on XdndEnter (JUCE asks for it early) and dropped at
#    the enter message's meaningless position: one failed drag made the next good one land nowhere. Reset on enter.
#    (1-3: juce_gui_basics/native/juce_DragAndDrop_linux.cpp, compiled on Linux/BSD only.)
# 4. Every platform, juce_gui_basics/windows/juce_ComponentPeer.cpp: a file drop that no component took (an unsupported
#    file, or dropped where nothing accepts files) kept `lastDragAndDropCompUnderMouse`, so the next drag that starts
#    over that same component (the Playlist next to the file manager) never looked for a target and was refused.
# 5. Every platform, juce_gui_basics/menus/juce_PopupMenu.cpp (issue #18, round 167p): LYNX opens a submenu beside its
#    parent menu's outer edge (Theme.cpp preparePopupMenuWindow), so in a two-column menu the mouse crosses the other
#    column on its way. JUCE keeps the open submenu only while the mouse stays in a thin triangle towards it; a pixel
#    of drift highlighted the other column's item under the mouse and closed the submenu. The way across now also
#    counts as "moving towards the submenu": the band of the item's row (one row above and below), between the item
#    and the submenu, while the mouse moves towards it. Resting there (JUCE's 350 ms) still picks the item under it.
# 6. Every platform, juce_PopupMenu.cpp (issue #45, round 168z): a submenu took the display under the item that opened
#    it, so a tall submenu reaching into the monitor below (or a confused XWayland display list) sent the next submenu
#    to another monitor. A submenu now stays on the display of its ROOT menu, i.e. of the window the menu came from.
# 7. Linux/BSD only, juce_PopupMenu.cpp (issue #45): JUCE closes a menu ("the app lost the focus") as soon as none of
#    its windows has the X focus and the mouse is not over the menu. With KWin/XWayland and more than one LYNX window
#    (an undocked panel, a settings window) the focus moves between our windows, and through the menu window itself
#    (fix 8), for a moment after the click that opens a menu, so most menus closed before they were seen. The mouse
#    over one of LYNX's own windows now also counts as "still LYNX's".
# 8. Linux/BSD only, juce_gui_basics/native/juce_Windowing_linux.cpp (issue #45): a window that ignores key presses
#    (a popup menu, a tooltip) never takes the X focus. When a LYNX window got the focus while a menu was open, JUCE
#    handed the focus to the menu's override-redirect window, which the compositor gives back: the focus ping-ponged
#    between the two and the menu saw "no focus". Keys still reach the menu (JUCE sends them to the modal component).
# 9. Every platform, juce_gui_basics/desktop/juce_Displays.cpp (issue #45, round 168z2): Displays::updateToLogical
#    places each monitor beside one it touches exactly (in physical pixels). A monitor that touches none (a gap or an
#    overlap, e.g. XWayland's rounding with KDE's fractional 115 %, or Xvfb's Xinerama screens) was left with an empty
#    totalArea and its userArea at 0,0, a phantom screen over the main one; its windows and menus had no display. It
#    now gets its physical rectangle divided by its scale (exact when all monitors share one scale).
# 10. Linux/BSD only, juce_gui_basics/native/juce_XWindowSystem_linux.cpp (issue #45, round 168z3): toFront (false)
#    raises the window without activating it. JUCE always sent _NET_ACTIVE_WINDOW, so the WM gave the focus to every
#    window LYNX only meant to keep above the main one (an undocked panel, after each click in the main window): the
#    main window lost the focus right after the click on its menu bar, and the menu closed (0 of 20 opened with KWin).
# 11. Linux/BSD only, same file (issue #45, round 168z3): a ConfigureNotify on a window with a native title bar (or on
#    the WM frame around one of our windows) dismissed every open popup menu, meant for "the title bar is being
#    dragged". KWin also sends it when it only restacks or re-sends a frame (with an undocked panel: while a menu of the
#    main window opens), so the menu closed at once. Only a window that really moved or changed size dismisses now.
# 12. macOS only, juce_graphics/native/juce_CoreGraphicsContext_mac.mm (issue #11, round 170c): a gradient (or image)
#    fill of an EMPTY path — the outline of a space when text is drawn with a gradient, an empty spectrum — called
#    CGContextClip on an empty path, which Quartz ignores ("empty path"), and then drew the gradient over the whole
#    clip: the whole component. Facet EQ's plugin title ("Facet EQ", a gradient text) flooded the editor pink. An
#    empty path is now skipped, and clipping to an empty path clips everything away (as the other renderers do).
# 13. Every platform, juce_PopupMenu.cpp (round 170a): menu accelerator letters like FL's ('&' letters, e.g. D = Delete).
#    An item whose text has the mark U+0001 before a character (core/MenuMnemonic.h) is picked by typing that key
#    while the menu is open: the first enabled item with that key runs (a submenu opens, its first item highlighted),
#    as in FL (round 170a [SHOT]: "Cu&t itself" and "Cu&t" share T in the channel menu; T runs "Cut itself").
#
# 14. Every platform, juce_audio_processors/format_types/juce_VST3Common.h (issue #70, "notes keep on cutting off when two or more
#    play", LABS): JUCE's VST3 host gives every note on and note off the noteId -1. A VST3 plugin that tells its voices
#    apart by noteId (the VST3 way; -1 is "not given") can take each new note for the same note again and move its
#    voice to it: everything played as one voice. FL gives each note its own id, and the same id to its note off
#    (Fruity Wrapper's VST3 note on 0x84d0e0: noteId = the voice's id; -1 only for its "no voice" sentinel; REA 173a).
#    MidiEventList now numbers the host's note ons (per plugin, counting up) and gives each note off the id of the
#    oldest sounding note on the same channel and key (poly pressure: that note's id too; none: -1 as before).
# 15. Windows only, juce_events/native/juce_Messaging_windows.cpp (issue #32, round 174a): the message loop's
#    DispatchMessage goes through `lynxDispatchMessageHook` when LYNX sets one. LYNX's hook (platform/CrashGuard.cpp
#    dispatchGuarded) runs a message for a third-party plugin's own window inside an SEH guard: a fault in the plugin's
#    window procedure (the reporter's synth editor, 0xC0000005 under DispatchMessageW) closes that editor instead of
#    ending LYNX. Without a hook (tests, other programs) it is JUCE's DispatchMessage as before.
# 16. Windows only, juce_graphics/native/juce_Direct2DImage_windows.cpp (round 174e2): every Graphics drawing into a
#    Direct2D image ends with a full GPU -> CPU readback of the image into its software copy (JUCE keeps both). For
#    the Playlist's and the Mixer's cached pictures (redrawn while scrolling, 1920x1000 px) that readback cost more than
#    the drawing. A context made while juce::lynxSkipImageReadback is set skips it; LYNX sets it only around images it
#    never reads back (the picture is only drawn to the window; a new device gets a full repaint anyway).
#    Round 190bm (0.1.25: the Mixer and the Playlist black): see fix 22.
# 17. Windows only, juce_gui_basics/native/juce_FileChooser_windows.cpp (round 180a): at the first selection change in a
#    native file dialog JUCE types Home and End with SendInput (its workaround for a cut-off file name), into whatever
#    window has the focus at that moment: another program, if the user moved on while the dialog opened. The keys now go
#    out only when the foreground window is one of LYNX's own. Without them the name may just show cut off.
# 18. Every platform, juce_audio_processors/format_types/juce_VST3Common.h (issue #63, note fine pitch on VST3): FL's
#    wrapper rounds a note's pitch (key + fine pitch) to a key and sends the rest as the VST3 note on's tuning (Fruity
#    Wrapper 0x84d0e0 / 0x879a00, REA_rodada188g). JUCE builds note ons from MIDI (tuning 0); MidiEventList now asks
#    LYNX's hook for each note on's tuning while LYNX has set a context on the thread (engine/Vst3NoteTuning.h). Needs
#    fix 14 (it goes into its note numbering).
# 19. Every platform, juce_graphics/fonts/juce_SimpleShapedText.cpp (issue #142, round 190d): text shaped with a font
#    that has no typeface gets no glyphs. For a line break that is "unsafe" (every one is, with no glyphs) JUCE 8.0.4
#    shapes the text up to the break again and walks back from the end of those glyphs to skip trailing spaces with
#    `while (--it != glyphs.begin())`: on an empty vector that starts before its beginning and never stops (SIGSEGV
#    at LYNX+0xf94c7a in 0.1.25, at start-up on NixOS, where JUCE's own font list had no monospaced family). No
#    glyphs: nothing to walk over. LYNX also gives every font a typeface now (ui/Theme.cpp getTypefaceForFont).
# 20. Windows only, juce_audio_devices/native/juce_WASAPI_windows.cpp (issue #134, round 190y): every new Windows Audio
#    device object (a change of output or input device, the first open) asks the driver about each channel count from
#    64 down to the mix format's, at every rate and in 7 sample formats: 434 IAudioClient::IsFormatSupported calls for
#    a stereo device, each a round trip to the Windows audio service, on the message thread. In shared mode the answer
#    can never raise the count (the engine answers with its mix format, whose channel count is the start value), so the
#    loop only costs time: 1.1 s on this PC's USB codec, and about 85 s on the reporter's Realtek (its hang report shows
#    the UI thread in this loop; ~0.15 s per call). Shared and low-latency shared modes now take the mix format's count;
#    exclusive mode still asks (there the driver does answer for more channels).
# 21. Every platform, juce_audio_processors/format_types/juce_VST3PluginFormat.cpp (round 177b, 190an): a VST3 plugin that
#    asks the host for a parameter's context menu (IComponentHandler3::createContextMenu with a ParamID, e.g. a
#    right-click on a knob) gets the host's items through `lynxVst3ParamMenuHook` (LYNX: Create automation clip, Link to
#    controller…, as on its own knobs). They are the first items of the IContextMenu, with their own target, so both
#    kinds of plugin offer them: one that lets the host show the menu (popup) and one that copies the host's items
#    into its own menu (getItemCount / getItem / executeMenuItem, e.g. Surge XT). JUCE's menu held the plugin's items
#    only, and its getItem compared tags where the SDK passes an index. Without a hook, or for a menu without a
#    parameter, it is JUCE's menu as before.
# 22. Windows only, juce_graphics/native/juce_Direct2DImage_windows.cpp (round 190bm; a 0.1.25 user saw the Mixer and
#    the Playlist black): fix 16 left the image's software copy as allocated (black), and JUCE reads that copy whenever
#    the image is drawn by anything but the Direct2D device it was painted on: a window drawn by the software renderer
#    (LYNX's GPU rendering switched off, which LYNX also does by itself after a crash in the graphics driver), a window
#    on a monitor of another adapter than the first, the pages of a new device after the adapters changed. The
#    readback is now put off, not dropped: the device bitmap that holds the picture is remembered (beside the class:
#    its header is also compiled into juce_gui_basics from the unpatched tree, so its layout must stay) and read back
#    the moment the copy is needed. The flag applies to one image (`lynxNoReadbackImage`), not to every Graphics made
#    meanwhile. A picture whose device is gone is counted (`lynxImageContentLost`; the caches repaint), and reading
#    from such a device returns false instead of reading through a null mapping.
# 23. Windows only, juce_gui_basics/native/juce_Windowing_windows.cpp (round 190cj; the LYNX Dance dancer was never on
#    the stage): a transparent (per-pixel alpha, layered) CHILD window, which LYNX's dancers are. (a) Every repaint ends
#    in UpdateLayeredWindow with the window's SCREEN position as its "new position"; for a child window the system
#    takes that position in the parent's client area, so each repaint moved the window by the parent's client offset
#    on the screen (116, 62 for a window at 108, 31: after a minute the dancer was 95900 pixels away). A child window
#    now gets no position there (it stays where it is). (b) setBounds added the parent's screen position to such a
#    child's place and then read the result back as the component's bounds; a child window is now placed like any
#    other child, in its parent's client area. Top-level windows are untouched.
# 24. Every platform, juce_audio_processors/format_types/juce_VST3Common.h + juce_VST3PluginFormat.cpp (issue #155,
#    0.1.25/0.1.26: Spectral Compressor crashed LYNX on an insert, Linux): JUCE's VST3 host passes an INACTIVE bus with
#    its channel count and null sample pointers (HostBufferMapper::associateBufferTo). The SDK allows that ("if an audio
#    bus is currently inactive, the actual sample buffer addresses are safe to be null", ivstaudioprocessor.h), but
#    plugins that ignore activateBus read them anyway: every nih-plug VST3 copies each aux input into its own buffers
#    (wrapper/util/buffer_management.rs create_buffers: memcpy from null, SIGSEGV on the audio thread). Since issue #109
#    (142979eb7) LYNX keeps a plugin's sidechain bus off until something feeds it, so every nih-plug plugin with a
#    sidechain input crashed as soon as it processed. An inactive bus's channels now point at silence: one zeroed
#    stretch of the block's length per channel (outputs too: a plugin may write there), cleared for every block,
#    sized by prepareToPlay's block size (a longer block grows it). The bus stays inactive for the plugin
#    (activateBus 0, silence flags set), so plugins that follow activation (Waves, issue #109) are unchanged.
# 25. Every platform, juce_audio_processors/format_types/juce_VST3PluginFormat.cpp (issue #152, "Arturia: the sound
#    follows my controller, the plugin's knobs don't"): a VST3 plugin takes MIDI CCs through IMidiMapping, which JUCE's
#    host turns into parameter changes for the PROCESSOR only. The VST3 SDK leaves the rest to the host: the edit
#    controller (the plugin's own window) never heard of the change and the host's parameter value (LYNX's knobs,
#    links, automation recording) stayed stale. The mapped change now also goes to the edit controller (JUCE's
#    parameter dispatcher, on the message thread) and to the host's value and listeners, as JUCE already does for a
#    plugin's own output parameter changes.
# 26. Every platform, juce_audio_processors/format_types/juce_VST3Common.h (round 190fg, the 190ej follow-up): fix 14 gives
#    a note off the id of the OLDEST sounding note of its key. Of two overlapping notes of one key that end inner note
#    first (LIFO: a long note with a short one inside it), the inner note's end then carried the outer note's id, and a
#    VST3 that keys its voices by noteId ended the outer voice and left the inner one sounding. FL ends each note's own
#    voice (round 190ej [EXEC]; its wrapper sends a note off with the id of the voice it ends, REA 173a). LYNX knows which
#    note each note on and note off belongs to (NoteEvent::noteId); MidiEventList now asks `lynxNoteTagHook` (set by LYNX,
#    engine/Vst3NoteTuning.h, while fix 18's context is set) for that tag of each note on and note off it converts, keeps
#    it beside the id it gave the note on, and gives a tagged note off the id of the note on with the same tag. A note off
#    without a tag (a live key, all notes off) or with an unknown tag keeps fix 14's oldest-note rule.
# 27. Every platform, juce_graphics/fonts/juce_SimpleShapedText.cpp (round 190gr; 26 in its branch): every ShapedTextOptions (one per text
#    JUCE shapes: each drawText, drawFittedText, GlyphArrangement line) asked the OS for the display language. On Linux
#    that is setlocale () with a walk over the LC_* / LANG variables (getenv, ~1 % of a whole-frame paint); on Windows
#    a DLL load and GetUserPreferredUILanguages. The display language is now read once per run (the same text shaped
#    with the same language: the same glyphs).
# 28. Windows only, juce_graphics/native/juce_DirectX_windows.h + juce_gui_basics/juce_gui_basics.cpp (round 190go, 26 in its branch;
#    start-up): JUCE makes a Direct3D 11 + Direct2D device for EVERY DXGI adapter at the first Direct2D image (LYNX's
#    splash logo), the "Microsoft Basic Render Driver" (WARP, the software rasterizer, which has no monitor) included:
#    part of the 0.40-0.44 s and ~50 MB before the splash shows. A software adapter is now used only when no hardware
#    adapter gave a device (a PC or a remote session with WARP alone keeps it). Hardware adapters keep their order (the
#    first is still the default). juce_gui_basics.cpp includes the header by its relative path, so both modules compile
#    the same (patched) inline code (a lone copy of juce_gui_basics, LYNX-bridge32's x86 one, uses the include path).
# 29. Windows only, juce_audio_devices/native/juce_ASIO_windows.cpp (round 190go, 27 in its branch; start-up): the ASIO thread. JUCE
#    calls an ASIO driver on whichever thread opens, closes or resets the device (the message thread), and creating
#    and opening "FL Studio ASIO" took ~0.8 s of the start-up's critical path (the driver object ~0.25-0.3 s, its own
#    signature check; JUCE's start / 80 ms / stop "because Cubase does it" ~0.27 s; the first callback ~0.1 s). Every
#    call into a driver now runs on one thread of its own (COM: an apartment-threaded driver must stay on the thread
#    that made it), with a message loop for the driver's windows and timers; the caller waits, answering messages sent
#    to its windows (all messages while the driver's control panel is open). LYNX can then make and open its device
#    on a worker while the main window is built (engine/DllAsioDevice.h, prewarmAsioDevice). The audio callbacks still
#    come from the driver's own thread. LYNX_ASIO_THREAD=0: as before (the caller's thread; no prewarm either).
# 30. Windows only, juce_graphics/native/juce_Direct2DImage_windows.cpp (round 190hd, 28 in its branch; start-up): a thread can ask for
#    software images (`juce::lynxSoftwareImagesOnThisThread (true)`): every image it makes after that, a decoded PNG
#    included, is a plain software image instead of a Direct2D one. The splash card draws on a thread of its own with the
#    CPU (ui/Splash.cpp); before, its logo was the process's first Direct2D image, so the Direct3D 11 + Direct2D device
#    (~0.35-0.4 s) was made before the card could show. Other threads: as before.
# 31. Windows only, juce_audio_devices/native/juce_ASIO_windows.cpp (round 190hd, 29 in its branch; start-up): JUCE starts an ASIO driver,
#    sleeps 80 ms and stops it while it initialises the device ("because Cubase does it"); for "FL Studio ASIO" that is
#    ~0.27 s of the start-up's critical path (its stop ends a Windows Audio stream). The ASIO protocol does not ask for
#    it (init, channels, buffer sizes, createBuffers, start). `juce::lynxAsioQuickInit` (set by LYNX's prewarm for the
#    one device it makes ahead, engine/DllAsioDevice.cpp) skips it for that device only; a device that then fails to
#    open is made again the usual way (AudioEngine::openAudioDevice). Every other open: as JUCE does.
# 32. Windows only (26 in round 190gv's branch), juce_gui_basics/native/juce_VBlank_windows.cpp + juce_Windowing_windows.cpp + windows/
#    juce_ComponentPeer.cpp + windows/juce_VBlankAttachment.cpp + juce_graphics/native/juce_Direct2DHwndContext_windows.cpp
#    (round 190gv; an empty LYNX used 12-13 % of a core at rest, 4.8 % of it JUCE's vertical-blank thread): every window
#    was a listener of its display's vertical blanks for ever, so each display's thread waited for every vertical blank
#    and posted it to the message thread (144 a second on a 144 Hz display), which ran each window's paint check
#    (Direct2D: its device checks too) with nothing to paint. A window now listens only while it needs them: while it
#    has a VBlankAttachment (LYNX's FrameTickers while they animate) or a frame still to paint or to present. After one
#    vertical blank with nothing to do its display's thread sleeps (no wait, no message) until a repaint, a paint
#    message, a shown window, a new renderer, a new VBlankAttachment or a window moving to that display wakes it; the
#    next vertical blank is then posted as before, so a repaint is painted on the same vertical blank as before.
#    LYNX_VBLANK_ALWAYS=1: every vertical blank, as before.
# 33. Windows only (27 in round 190gv's branch), juce_graphics/native/juce_Direct2DHwndContext_windows.cpp (round 190gv; LYNX showed ~75 screen
#    updates a second while playing on a 144 Hz display, FL 143): JUCE 8.0.4 presents a painted frame only after a
#    per-window thread has seen the swap chain's waitable object and posted a message to the message thread, so a
#    frame painted on a vertical blank before that message arrived waited for the next one. Backported from JUCE's
#    later versions (JUCE's own code, same licence): no thread; when a frame is due the frame is painted only when it
#    can be presented at once; nothing to paint = no frame (and no device checks). A present that failed is painted
#    again on the next vertical blank. (This alone still gave ~75 updates a second: fix 34 is what was missing.)
#    The compositor signals the waitable object a little after the vertical blank, so a due frame waits up to 4 ms
#    for it (it returns as soon as it is signalled; no wait after two empty waits in a row, e.g. a minimised window).
#    Round 190hi: polling it at once, as JUCE's later versions do, missed a vertical blank now and then (139 screen
#    updates a second; 124-137 in the 0.1.28 rollup; 143 with the wait, FL 143.8). LYNX_D2D_WAIT_MS=0..16 (0 = polled).
#    LYNX_D2D_PACING=old: 8.0.4's pacing.
# 34. Windows only (28 in round 190gv's branch), juce_gui_basics/native/juce_Windowing_windows.cpp (round 190gv): the compositor shows a native child
#    window's new Direct2D frame only when its top-level window presents too. LYNX's docked panels (issue #116: the
#    Playlist, Channel rack, Mixer... are native children of the main window) therefore moved at the main window's own
#    rate, its toolbar's 72 Hz: ~75 screen updates a second while playing on a 144 Hz display (140 with docking off,
#    143 with every animation at 144 Hz). After a child window's frame the top-level window paints the one pixel under
#    the child's corner (hidden by the child) and presents in the same vertical blank. LYNX_CHILD_NUDGE=0: not.
# 35. Every platform (32 in round 190hf's branch), juce_gui_basics/mouse/juce_DragAndDropContainer.cpp (issue #174, round 190hf): an internal drag
#    (a sample from LYNX's Browser) turns into the system's file drag only where JUCE finds no component of the app
#    under the mouse. A hosted plugin's editor is a native window of the plugin (Windows HWND, X11 window via XEmbed,
#    NSView) inside one of LYNX's windows, so JUCE saw LYNX's component there and the plugin never got the file.
#    `lynxExternalDropAtHook` (set by LYNX, ui/ExternalDrag.cpp) says that a screen point is a plugin's own editor (or
#    another program's window above LYNX's, the desktop): there the drag counts as outside (over a plugin after 300 ms
#    instead of 700, and even when a LYNX drop target is behind it), and goes on as the file drag the plugin's own drop
#    target gets.
#    With the hook it alone decides: on X11 JUCE's Desktop::findComponentAt found nothing over LYNX's own windows
#    while the drag image (a window of ours that lets the mouse through) was under the mouse. Without it: JUCE's test.
# 36. Linux/BSD only (33 in round 190hf's branch), juce_gui_basics/native/juce_DragAndDrop_linux.cpp (issue #174): JUCE's XDND source sends a drag to
#    the first XdndAware window under the pointer from the root down. Every JUCE window is XdndAware, so a plugin editor
#    embedded in a window (its own XdndAware X window, reparented into the host's XEmbed window: in LYNX's plugin window,
#    or in the window of LYNX's separate plugin process) never got it: the host's window took the drop and had nobody to
#    give it to. The search now goes on below an XdndAware window to the deepest XdndAware window under the pointer, the
#    one that receives it, as OLE (Windows) and AppKit (macOS) pick the innermost registered window. A program with one
#    XdndAware window (most) is reached as before.
#
# 37. Every platform, juce_gui_basics/menus/juce_PopupMenu.cpp + widgets/juce_ListBox.cpp (issue #182, round 190ib): type-to-jump.
#    JUCE's menus and lists take no typed letters. A typed character (no Ctrl / Cmd / Alt) now asks `lynxTypeAheadHook`
#    (set by LYNX, ui/TypeAhead.h; the rule is core/TypeAhead.h) which row to go to: the first letter jumps to the next
#    item that starts with it, more letters typed within a second narrow it to a longer prefix. A menu highlights the
#    item (Enter runs it, a submenu opens with Right as before); fix 13's accelerator letters still win in menus that
#    have them, except while a prefix is being typed (within a second of a key that jumped); a space typed then is part of
#    the prefix (JUCE: Space runs the item). A ListBox selects the row (and scrolls to it) when its model names its rows (getNameForRow; JUCE's
#    default "Row N" names are left alone). Null hook: JUCE's keys only.
#
# 38. Linux/BSD only (37 is round 190ib's), juce_gui_basics/native/juce_Windowing_linux.cpp (issue #188, round 190ir): a
#    minimised LYNX window (Settings, Tap tempo...) kept swallowing the clicks on the main window where it had been.
#    JUCE's X11 peer decides whether a point is "in" a window by asking every JUCE window above it in JUCE's own list
#    (not the X server's stacking) whether it covers that point, from its bounds alone. A minimised window is still
#    `isVisible()` for JUCE and keeps its bounds, and LYNX keeps its secondary windows above the main window in that list
#    (matchWindowOrderToScreen, issue #12), so the main window answered "not mine" there and MouseInputSource dropped
#    the click (findComponentAt -> nullptr). Nothing in the X server or the window manager is involved, so it happens
#    with every WM: KWin on X11 or Wayland (LYNX runs through XWayland there), GNOME, openbox (reproduced on Xvfb).
#    A window above now covers the point only while it is on the screen: mapped and viewable, and not minimised
#    (_NET_WM_STATE_HIDDEN). The X round trips run only for a point inside such a window's bounds.
#
# 39. Every platform, juce_audio_processors/format_types/juce_VST3Common.h + juce_VST3PluginFormat.cpp (round 190jg,
#    BUILD-LIST 18): sample-accurate automation. JUCE's VST3 host sends each parameter's value once per block, at offset
#    0 (AudioProcessorParameter::setValue has no time). LYNX's renderer applies automation once per piece of up to 128
#    samples; with this fix the changes inside a piece (a ramp every 16 samples, a jump at its own sample) reach the
#    plugin's IParamValueQueue as points at their sample offsets, from four thread-local declarations LYNX sets while
#    the plugin's block runs (engine/TimedParameters.h). Unset (another host, the option off): JUCE's behaviour.
# 40. Every platform (39 in round 190je's branch), juce_gui_basics/menus/juce_PopupMenu.cpp (round 190je, design190ia FW-13): long menus in FL's columns.
#    JUCE splits a menu without column breaks only when it no longer fits on the screen, and then into columns with the
#    same number of items each, wherever that falls (in the middle of a group). FL splits its long context menus into
#    two columns at a group (the Channel rack menu: 305 px, LYNX's 951). Such a menu now asks `lynxTallMenuBreakHook`
#    (set by LYNX, ui/Theme.cpp; the rule is core/ThemePalette.h tallMenuBreak) where to break: menus taller than 70 % of
#    the screen split once, before a section header or after a separator, where the two columns come out most even. An
#    answer of -1 (or a null hook) leaves JUCE's own split.
# 41. Linux/BSD only (39 in round 190jx's branch), juce_gui_basics/native/juce_Windowing_linux.cpp (round 190jx): the X11 backend of LYNX's moving
#    marks (ui/MotionOverlay.h, ui/MotionOverlayX11.h) draws a playhead with the X server over a window's pixels and
#    keeps the pixels under it. It must know when JUCE replaced pixels there: after the repaint manager blits its
#    rectangles to a window, `lynxX11AfterBlitHook` (set by LYNX) gets the window and the rectangles (window pixels), and
#    the marks there are drawn again over the new pixels, in the same order of requests on the same connection.
#    `lynxX11Display()` hands LYNX JUCE's display connection (so its requests are ordered with JUCE's). Null hook: nothing.
# 42. Every platform ("fix 190kq" in round qafix190kq's branch), juce_gui_basics/menus/juce_PopupMenu.cpp (round
#    qafix190kq, QA of 0.1.30): fix 40's split skips a menu whose target component has the property
#    "lynxOneColumnMenu" set (the menu bar's View menu: LYNX opens a submenu beside its parent's outer edge, issue
#    #18, so "Layout style" in the first of two columns had its submenu beyond the second one). Such a menu stays one
#    column while it fits on the screen; taller, JUCE's own split applies.
#
# 43. Windows only ("fix 190la" in round 190la's branch), juce_graphics/native/juce_DirectX_windows.h (round 190la, perf A/B switch): with
#    LYNX_D3D_DRIVER_THREADS=0 the Direct3D 11 device JUCE's Direct2D windows draw with is made with
#    D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS: the graphics driver stops handing each frame's commands
#    to threads of its own. Measured on Thief playing (RTX 3060): NVIDIA's thread 353 -> 4 Mcycles/s, but the message
#    thread +316, so the process total is the same; 11 fewer threads and -27 % wake-ups. Off by default (the message
#    thread is the UI's frame budget); the default creates the device as JUCE does.
#
# 44. Windows only ("fix 190la-b" in round 190la's branch), juce_core/threads/juce_WaitableEvent.cpp (round 190la, perf): libc++ (the zig toolchain's)
#    implements a timed condition-variable wait on Windows with SleepConditionVariableSRW and the time left truncated
#    to whole milliseconds (thread_win32.cpp __libcpp_condvar_timedwait): with under 1 ms left it does not sleep, and
#    wait_for's loop spins until the deadline. Every JUCE timed wait (WaitableEvent::wait (ms): Thread::wait, the timer
#    thread, thread pools, LYNX's prefetcher) ended in such a spin: the JUCE timer thread alone used ~150 Mcycles/s
#    (~4 % of a core) with LYNX at rest (Frida + QueryThreadCycleTime, round 190la). The wait now sleeps whole
#    milliseconds rounded up (the event may be seen up to ~1 ms later than its deadline, never earlier).
#    scripts/build-windows-zig.sh patches juce_core for this one file.
#
# 45. Every platform ("fix 190ms" in round paint2_190ms's branch), juce_graphics/native/juce_RenderingHelpers.h
#    (round 190ms, perf of JUCE's software renderer; both parts give the same pixels by construction):
#    a) fillPath built each path's edge table over the whole clip's bounds: a table row per pixel row of the clip
#       (allocated, cleared, sorted and walked) for every shape, so a 20 px rounded rectangle in a whole-window repaint
#       cost as much as ~1000 empty rows. The table now covers the clip's rows from one above the path's bounds to one
#       below (all the clip's columns): the rows outside held no edge, and an edge point's row is never outside the
#       path's rows. The columns stay the clip's: EdgeTable extrapolates a nearly flat segment from its rounded start
#       row, so an edge point can land pixels beyond the path's bounds and is clamped to the table's columns (a cut of
#       the columns too changed 3 pixels of a filter curve by 1 level; --selftest paint_caches checks both).
#    b) ImageFill::copyRow (an opaque RGB image drawn into an ARGB image at full opacity, e.g. LYNX's cached keycap and
#       step strips): the same pixel conversion as PixelARGB::blend (PixelRGB) = set, one 4-byte load per pixel instead
#       of three byte loads (little-endian platforms where PixelRGB is stored b, g, r; others keep JUCE's loop).
#
# 46. Every platform ("fix 190ms-glyph" in round paint2_190ms's branch): JUCE's two text caches of 128 entries
#    (juce_LruCache.h's default) hold 1024 (round 190ms, perf; owner's decision on round 190ml's Helix window):
#    a) juce_graphics/contexts/juce_GraphicsContext.cpp, the shaped lines Graphics::drawText / drawFittedText /
#       drawSingleLineText / drawMultiLineText keep per (font, text, area, justification...): a window with more than
#       128 distinct labels (Helix+ 2's channel window, round 190ml) shaped every label again on every whole repaint
#       (fontconfig fallback look-ups included: 67 % of that window's paint; 22-25 ms a frame, 3-5 ms with either half
#       of it hidden). drawSingleLineText's key now holds its justification flags too (it was left out of the key, so
#       two calls that differ only there could get each other's line). The four keys compare their cheap fields
#       (position / area, justification, numbers) first, then the text, the font last: JUCE compared the font
#       (FontOptions, strings first: ~1000 instructions) first, and a 1024-entry tree is three levels deeper (Windows
#       inserts50 Playlist +4 % before this). Same fields, so the same equal keys and lines.
#    b) juce_graphics/native/juce_RenderingHelpers.h, the software renderer's glyphs (each glyph's outline as an edge
#       table, per font and size): every whole frame of the main views evicted and rebuilt glyphs (-6 ... -12 %
#       paint instructions a whole frame with 1024). Its keys are ordered by the glyph, then a hash of the font's name,
#       style, height and width, then the font: JUCE compared whole FontOptions (strings first) at every tree node of
#       the same glyph, ~5 compares of ~1000 instructions per glyph drawn (12 % of a frame; with 1024 entries the
#       deeper tree made Clip-launch's Arrangement +9 % slower before this). Equal keys stay JUCE's equal keys.
#    Both caches are pure functions of their keys, so the pixels are the same; the memory is a few MB at most.
#
# 47. Every platform ("fix 190oh" in round online_host_190oh's branch),
#    juce_audio_processors/format_types/juce_VST3PluginFormat.cpp (round 190oh, P3: FL's wrapper Settings › GUI): the
#    VST3 editor window's two size conversions (componentToVST3Rect / vst3ToComponentRect) leave the display's scale out
#    when LYNX marked the editor "lynxScaleEditorDimensions" (FL's "Scale editor dimensions", or a DPI-unaware window):
#    the size the plugin reports is then taken in logical pixels. An editor LYNX did not mark is sized as before.
#
# 48. Windows only ("fix 190os" in round perf6_190os's branch), juce_graphics/native/juce_DirectX_windows.h,
#    on top of fix 43: the Direct3D 11 device has no driver threads by default when LYNX may use 4 logical CPUs or
#    fewer (LYNX_D3D_DRIVER_THREADS=1: always the driver's threads; =0: never, as fix 43). Measured with a song that
#    overloads a PC with 2 CPUs (big200 at 64 samples, the safe-overload guard engaged): the message thread spent 80-100 %
#    of every frame gap over 50 ms waiting in NVIDIA's driver for its own thread, which got the CPU only between the
#    audio threads; with no driver threads 51.4 fps and the longest gap 46-70 ms (from 45.0 / 252 ms). With 12 CPUs the
#    same with or without (69-72 fps): there the driver's threads stay. perf190os/HANDOFF.md.
#
# 49. Linux/BSD only ("fix 190ou" in round bugs_190ou's branch),
#    juce_gui_basics/native/juce_XWindowSystem_linux.cpp (issue #254): X11 reports a horizontal wheel (a tilt wheel, a
#    mouse's second wheel, a touchpad's sideways edge) as buttons 6 (left) and 7 (right), which JUCE dropped: the
#    horizontal wheel did nothing on Linux. They now come as a wheel event with deltaX = +/-50/256 (6 = left = positive,
#    as Windows' WM_MOUSEHWHEEL gives it through JUCE) and deltaY 0, not smooth.
#
# 50. ("fix 50" in round lv2_190pk's branch; the LV2 plugin pack on Linux):
#  a. Linux/BSD only, juce_gui_basics/native/juce_XWindowSystem_linux.cpp: WM_PROTOCOLS, WM_TAKE_FOCUS, WM_DELETE_WINDOW
#     and _NET_WM_PING are created when missing, as JUCE already does for its other atoms. On an X server no window
#     manager ever ran on (a fresh Xvfb, CI's xvfb-run) they don't exist, so every new window set its WM_PROTOCOLS
#     property with atom 0: BadAtom on X_ChangeProperty. A standalone JUCE app ignores X errors (its own handler); a
#     plugin's host has Xlib's default handler, which ends the process: plugexporttest ended on LYNX FM10's and LYNX
#     Model's LV2 editors. isIconic / isHidden no longer read WM_STATE / _NET_WM_STATE when those atoms don't exist
#     (BadAtom on X_GetProperty, the next error the LV2 editors hit there).
#  b. Every platform (compiled only for LV2), juce_audio_plugin_client/juce_audio_plugin_client_LV2.cpp: JUCE's LV2
#     wrapper enables every bus and declares the ports of a bus that is off by default (the effects' side chain)
#     lv2:connectionOptional, i.e. a host may connect them to NULL, but it copied the input from there: the 6 LV2 effects
#     crashed in their first run() under JUCE's own LV2 host, which connects NULL for each channel its buffer lacks. An
#     unconnected input is silence now, and an unconnected output is skipped while the processor is suspended. The
#     .ttl files it writes (juce_LV2ManifestHelper) give parameter ranges and scale points as decimals ("1.0": "1" is an
#     integer, which lv2lint failed on every parameter), and an effect its LV2 class when the build defines
#     JucePlugin_LV2Class (cmake/LynxPlugins.cmake, from the VST3 category; else lv2:Plugin, as before).
#
# 51. Every platform (compiled only for LV2; round 190qd, the 0.1.31 release, owner OK 11-23),
#    juce_audio_plugin_client/juce_audio_plugin_client_LV2.cpp: the plugin's .ttl that JUCE's LV2 manifest helper writes
#    names its licence, `doap:license <http://spdx.org/licenses/AGPL-3.0-or-later>` (LYNX's plugins are AGPL-3.0-or-later,
#    LICENSE). JUCE wrote none, and lv2lint warned on every LV2 plugin of the pack ("no license specified").#
# 52. Linux/BSD only ("fix 190rb" in round qa_190rb's branch; round 190rb, the 0.1.32 quality release, in 0.1.31.2), juce_events/native/juce_Messaging_linux.cpp:
#    the event loop gathers the file-descriptor callbacks that are ready, then calls them one after the other. One of
#    them can unregister another's (the X11 events close a plugin's editor; the last VST3 editor takes JUCE's VST3 run
#    loop with it, whose destructor unregisters its descriptors; a CLAP plugin's descriptors go with the plugin), and the
#    gathered copy still called the unregistered callback, whose owner was gone: two 0.1.31.1 Linux crash reports
#    (an unordered_map look-up on a freed VST3 RunLoop::Impl right after the main loop's callback). A callback that is
#    no longer registered when its turn comes is skipped now.
#
# Used by CMake (lynx_patch_juce on the fetched JUCE) and by scripts/build-windows-zig.sh, which runs this file with
# `cmake -DJUCE_DIR=<a copy of JUCE's juce_gui_basics module> -P cmake/LynxPatchJuce.cmake`.

# One fix: `old` (lines joined with "|NL|") replaced by `new` unless `marker` is already in the file.
macro(lynx_juce_fix name marker old new)
    string(FIND "${text}" "${marker}" at_marker)
    if(at_marker EQUAL -1)
        string(REPLACE "|NL|" "${nl}" old_text "${old}")
        string(REPLACE "|NL|" "${nl}" new_text "${new}")
        string(FIND "${text}" "${old_text}" at_old)
        if(at_old EQUAL -1)
            message(WARNING "LYNX: ${file} changed; re-check fix '${name}' in cmake/LynxPatchJuce.cmake (issue #11)")
        else()
            string(REPLACE "${old_text}" "${new_text}" text "${text}")
            set(changed TRUE)
            message(STATUS "LYNX: JUCE fix applied: ${name}")
        endif()
    endif()
endmacro()

function(lynx_patch_juce_file file)
    if(NOT EXISTS "${file}")
        message(WARNING "LYNX: ${file} not found; JUCE's drag & drop fixes (issue #11) are not applied")
        return()
    endif()

    file(READ "${file}" text)
    set(nl "\n")
    string(FIND "${text}" "\r\n" at_crlf)
    if(NOT at_crlf EQUAL -1)
        set(nl "\r\n") # JUCE's sources use CRLF in some checkouts
    endif()
    set(changed FALSE)

    if(file MATCHES "juce_DragAndDrop_linux.cpp$")
        lynx_juce_fix("XDND 5 sources" "LYNX patch (issue #11): mutter/KWin"
            "if (dndCurrentVersion < 3 || dndCurrentVersion > XWindowSystemUtilities::Atoms::DndVersion)"
            "// LYNX patch (issue #11): mutter/KWin's XWayland bridges always send version 5|NL|        if (dndCurrentVersion < 3 || dndCurrentVersion > 5)")

        lynx_juce_fix("text/uri-list first" "LYNX patch (issue #11): files as text/uri-list"
            "        for (int i = 0; i < srcMimeTypeAtomList.size() && dragAndDropCurrentMimeType == 0; ++i)|NL|            for (int j = 0;"
            "        // LYNX patch (issue #11): files as text/uri-list whenever offered (GTK 4 lists text/plain first)|NL|        for (int i = 0; i < srcMimeTypeAtomList.size() && dragAndDropCurrentMimeType == 0; ++i)|NL|            if (srcMimeTypeAtomList[i] == atoms.allowedMimeTypes[3])|NL|                dragAndDropCurrentMimeType = atoms.allowedMimeTypes[3];|NL||NL|        for (int i = 0; i < srcMimeTypeAtomList.size() && dragAndDropCurrentMimeType == 0; ++i)|NL|            for (int j = 0;")

        lynx_juce_fix("no stale drop state" "LYNX patch (issue #11): not left over"
            "        dragInfo.clear();|NL|        srcMimeTypeAtomList.clear();|NL||NL|        dragAndDropCurrentMimeType = 0;"
            "        dragInfo.clear();|NL|        srcMimeTypeAtomList.clear();|NL|        finishAfterDropDataReceived = false; // LYNX patch (issue #11): not left over from a failed drop|NL||NL|        dragAndDropCurrentMimeType = 0;")

        lynx_juce_fix("XDND target: the deepest aware window" "LYNX patch (issue #174): a deeper XdndAware window"
            "        if (isWindowDnDAware (target))|NL|            return target;|NL|"
            "        if (isWindowDnDAware (target))|NL|        {|NL|            // LYNX patch (issue #174): a deeper XdndAware window under the pointer (a plugin's editor embedded through|NL|            // XEmbed) is the target, not the host window around it|NL|            ::Window lynxChild = None, lynxRoot;|NL|            int lynxPos;|NL|            unsigned int lynxMask;|NL||NL|            if (X11Symbols::getInstance()->xQueryPointer (getDisplay(), target, &lynxRoot, &lynxChild, &lynxPos, &lynxPos, &lynxPos, &lynxPos, &lynxMask)|NL|                && lynxChild != None)|NL|                if (const auto deeper = externalFindDragTargetWindow (lynxChild); deeper != None)|NL|                    return deeper;|NL||NL|            return target;|NL|        }|NL|")
    elseif(file MATCHES "juce_DragAndDropContainer.cpp$")
        lynx_juce_fix("external drop hook: declaration" "LYNX patch (issue #174): what LYNX sees at a place on screen"
            "bool juce_performDragDropText (const String&, bool& shouldStop);|NL|"
            "bool juce_performDragDropText (const String&, bool& shouldStop);|NL||NL|// LYNX patch (issue #174): what LYNX sees at a place on screen: 0 one of its own components, 1 outside the app (another|NL|// program's window, the desktop), 2 a hosted plugin's own editor (a native window of the plugin). 1 and 2 count as|NL|// outside: the drag goes on as the system's file drag there (over a plugin after 300 ms, elsewhere after JUCE's 700).|NL|// nullptr: JUCE's test only.|NL|int (*lynxExternalDropAtHook) (Point<int> screenPos) = nullptr;|NL|")
        lynx_juce_fix("external drop hook: over a plugin" "LYNX patch (issue #174): over a hosted plugin's editor"
            "            auto now = Time::getCurrentTime();|NL||NL|            if (getCurrentlyOver() != nullptr)|NL|                lastTimeOverTarget = now;|NL|            else if (now > lastTimeOverTarget + RelativeTime::milliseconds (700))|NL|                checkForExternalDrag (details, screenPos);"
            "            auto now = Time::getCurrentTime();|NL|            // LYNX patch (issue #174): over a hosted plugin's editor no target of ours counts (it is the plugin's)|NL|            const auto lynxPlace = lynxExternalDropAtHook != nullptr ? lynxExternalDropAtHook (screenPos) : 0;|NL|            const auto lynxOutside = lynxPlace != 0;|NL||NL|            if (getCurrentlyOver() != nullptr && ! lynxOutside)|NL|                lastTimeOverTarget = now;|NL|            else if (now > lastTimeOverTarget + RelativeTime::milliseconds (lynxPlace == 2 ? 300 : 700))|NL|                checkForExternalDrag (details, screenPos, lynxOutside);")
        lynx_juce_fix("external drop hook: the test" "LYNX patch (issue #174): `outside`"
            "    void checkForExternalDrag (DragAndDropTarget::SourceDetails& details, Point<int> screenPos)|NL|    {|NL|        if (! hasCheckedForExternalDrag)|NL|        {|NL|            if (Desktop::getInstance().findComponentAt (screenPos) == nullptr)"
            "    // LYNX patch (issue #174): `outside`: the hook said the point is a plugin's editor or another program's window.|NL|    // With the hook it alone decides (on X11 findComponentAt found nothing over our own windows: the drag image|NL|    // under the mouse, a window of ours, counted as covering them).|NL|    void checkForExternalDrag (DragAndDropTarget::SourceDetails& details, Point<int> screenPos, bool outside = false)|NL|    {|NL|        if (! hasCheckedForExternalDrag)|NL|        {|NL|            if (lynxExternalDropAtHook != nullptr ? outside : Desktop::getInstance().findComponentAt (screenPos) == nullptr)")
    elseif(file MATCHES "juce_PopupMenu.cpp$")
        lynx_juce_fix("submenu corridor" "LYNX patch (issue #18): the way across"
            "        return areaTowardsSubMenu.contains (newGlobalPos.toFloat());|NL|    }"
            "        if (areaTowardsSubMenu.contains (newGlobalPos.toFloat()))|NL|            return true;|NL||NL|        // LYNX patch (issue #18): the way across a two-column menu to a submenu beside its outer edge|NL|        if (auto* item = window.currentChild.getComponent())|NL|        {|NL|            const auto itemBounds = item->getScreenBounds();|NL|            const auto towardsRight = itemScreenBounds.getX() > window.getX();|NL|            const auto from = towardsRight ? itemBounds.getRight() : itemScreenBounds.getRight();|NL|            const auto to = towardsRight ? itemScreenBounds.getX() : itemBounds.getX();|NL|            return newGlobalPos.y >= itemBounds.getY() - itemBounds.getHeight() && newGlobalPos.y < itemBounds.getBottom() + itemBounds.getHeight()|NL|                   && newGlobalPos.x >= from - 2 && newGlobalPos.x <= to + 2|NL|                   && (towardsRight ? newGlobalPos.x >= lastMousePos.x : newGlobalPos.x <= lastMousePos.x);|NL|        }|NL||NL|        return false;|NL|    }")

        lynx_juce_fix("submenu on the root menu's display" "LYNX patch (issue #45): a submenu stays"
            "    Rectangle<int> getParentArea (Point<int> targetPoint, Component* relativeTo = nullptr)|NL|    {|NL|"
            "    Rectangle<int> getParentArea (Point<int> targetPoint, Component* relativeTo = nullptr)|NL|    {|NL|        // LYNX patch (issue #45): a submenu stays on the display of its root menu (the window it came from)|NL|        if (parent != nullptr && relativeTo == nullptr && options.getParentComponent() == nullptr)|NL|        {|NL|            auto* root = parent;|NL|            while (root->parent != nullptr)|NL|                root = root->parent;|NL|            targetPoint = root->getBounds().getCentre();|NL|        }|NL||NL|")

        lynx_juce_fix("menu kept while the mouse is over LYNX" "LYNX patch (issue #45): the mouse over"
            "    bool doesAnyJuceCompHaveFocus()|NL|    {|NL|"
            "    bool doesAnyJuceCompHaveFocus()|NL|    {|NL|       #if JUCE_LINUX || JUCE_BSD|NL|        // LYNX patch (issue #45): the mouse over one of our windows = still ours (X focus moving between them)|NL|        {|NL|            const auto mousePos = Desktop::getInstance().getMainMouseSource().getScreenPosition();|NL||NL|            for (int i = ComponentPeer::getNumPeers(); --i >= 0;)|NL|            {|NL|                auto& comp = ComponentPeer::getPeer (i)->getComponent();|NL||NL|                if (comp.isShowing() && comp.getLocalBounds().toFloat().contains (comp.getLocalPoint (nullptr, mousePos)))|NL|                    return true;|NL|            }|NL|        }|NL|       #endif|NL||NL|")
        lynx_juce_fix("menu accelerator letters" "LYNX patch (round 170a): menu accelerator letters"
            "    bool keyPressed (const KeyPress& key) override|NL|    {|NL|        if (key.isKeyCode (KeyPress::downKey))"
            "    // LYNX patch (round 170a): menu accelerator letters (core/MenuMnemonic.h marks the letter with U+0001)|NL|    bool lynxMenuMnemonic (const KeyPress& key)|NL|    {|NL|        const auto mods = key.getModifiers();|NL|        const auto typed = CharacterFunctions::toLowerCase (key.getTextCharacter());|NL|        if (typed <= 32 || mods.isCtrlDown() || mods.isCommandDown() || mods.isAltDown())|NL|            return false;|NL||NL|        Array<ItemComponent*> matches;|NL|        for (auto* child : items)|NL|        {|NL|            const auto& text = child->item.text;|NL|            const auto at = text.indexOfChar ((juce_wchar) 1);|NL|            if (at >= 0 && CharacterFunctions::toLowerCase (text[at + 1]) == typed|NL|                && (canBeTriggered (child->item) || hasActiveSubMenu (child->item)))|NL|                matches.add (child);|NL|        }|NL||NL|        if (matches.isEmpty())|NL|            return false;|NL||NL|        disableTimerUntilMouseMoves();|NL||NL|        // FL: the first item with the key (in menu order) runs, even when another one has the same key|NL|        auto* only = matches.getFirst();|NL|        setCurrentlyHighlightedChild (only);|NL||NL|        if (hasActiveSubMenu (only->item))|NL|        {|NL|            if (showSubMenuFor (only) && isSubMenuVisible())|NL|                activeSubMenu->selectNextItem (MenuSelectionDirection::current);|NL|        }|NL|        else|NL|        {|NL|            triggerCurrentlyHighlightedItem();|NL|        }|NL||NL|        return true;|NL|    }|NL||NL|    bool keyPressed (const KeyPress& key) override|NL|    {|NL|        if (key.isKeyCode (KeyPress::downKey))")
        lynx_juce_fix("menu accelerator keys" "LYNX patch (round 170a): accelerator letters"
            "            dismissMenu (nullptr);|NL|        }|NL|        else|NL|        {|NL|            return false;|NL|        }"
            "            dismissMenu (nullptr);|NL|        }|NL|        else if (! lynxMenuMnemonic (key)) // LYNX patch (round 170a): accelerator letters|NL|        {|NL|            return false;|NL|        }")
        lynx_juce_fix("tall menus in two columns: the hook" "LYNX patch (round 190je): long menus in FL's columns"
            "namespace PopupMenuSettings|NL|{|NL|"
            "// LYNX patch (round 190je): long menus in FL's columns (cmake/LynxPatchJuce.cmake, fix 40). Set by LYNX (ui/Theme.cpp):|NL|// the item after which a menu of `count` items (`heights`; `kinds` 0 item, 1 separator, 2 section header) breaks into a|NL|// second column on a screen `screenHeight` tall, -1 for JUCE's own split. Null: JUCE's split only.|NL|int (*lynxTallMenuBreakHook) (const int* heights, const int* kinds, int count, int screenHeight) = nullptr;|NL||NL|namespace PopupMenuSettings|NL|{|NL|")
        lynx_juce_fix("tall menus in two columns: the split" "LYNX patch (round 190je): a tall menu without column breaks"
            "        numColumns = numBreaks + 1;|NL||NL|        if (numBreaks == 0)|NL|            insertColumnBreaks (maxMenuW, maxMenuH);"
            "        numColumns = numBreaks + 1;|NL||NL|        // LYNX patch (round 190je): a tall menu without column breaks splits in two at a group, as FL's (fix 40)|NL|        if (numBreaks == 0 && lynxTallMenuBreakHook != nullptr && items.size() > 3)|NL|        {|NL|            std::vector<int> lynxHeights, lynxKinds;|NL|            for (auto* child : items)|NL|            {|NL|                lynxHeights.push_back (child->getHeight());|NL|                lynxKinds.push_back (child->item.isSeparator ? 1 : child->item.isSectionHeader ? 2 : 0);|NL|            }|NL||NL|            const auto lynxAt = lynxTallMenuBreakHook (lynxHeights.data(), lynxKinds.data(), (int) lynxHeights.size(), maxMenuH + 24);|NL||NL|            if (isPositiveAndBelow (lynxAt, items.size() - 1))|NL|            {|NL|                items[lynxAt]->item.shouldBreakAfter = true;|NL|                numColumns = 2;|NL|            }|NL|        }|NL||NL|        if (numColumns == 1)|NL|            insertColumnBreaks (maxMenuW, maxMenuH);")
        lynx_juce_fix("one-column menus (fix 42)" "LYNX patch (fix 42): not a menu whose target"
            "        // LYNX patch (round 190je): a tall menu without column breaks splits in two at a group, as FL's (fix 40)|NL|        if (numBreaks == 0 && lynxTallMenuBreakHook != nullptr && items.size() > 3)"
            "        // LYNX patch (round 190je): a tall menu without column breaks splits in two at a group, as FL's (fix 40)|NL|        // LYNX patch (fix 42): not a menu whose target asks for one column (the property \"lynxOneColumnMenu\": the menu|NL|        // bar's View menu); JUCE's own split below still applies to it when it is taller than the screen|NL|        const auto* lynxTarget = options.getTargetComponent();|NL|        const auto lynxOneColumn = lynxTarget != nullptr && (bool) lynxTarget->getProperties().getWithDefault (\"lynxOneColumnMenu\", false);|NL||NL|        if (numBreaks == 0 && lynxTallMenuBreakHook != nullptr && items.size() > 3 && ! lynxOneColumn)")
        lynx_juce_fix("menu type-to-jump" "LYNX patch (round 190ib, issue #182): type-to-jump"
            "    bool keyPressed (const KeyPress& key) override|NL|    {|NL|        if (key.isKeyCode (KeyPress::downKey))"
            "    // LYNX patch (round 190ib, issue #182): type-to-jump (fix 37): the item whose text starts with what was typed|NL|    bool lynxMenuTypeAhead (const KeyPress& key)|NL|    {|NL|        extern int (*lynxTypeAheadHook) (const void*, juce_wchar, int, int, const std::function<String (int)>&);|NL|        const auto mods = key.getModifiers();|NL|        const auto typed = key.getTextCharacter();|NL|        if (lynxTypeAheadHook == nullptr || typed < 32 || mods.isCtrlDown() || mods.isCommandDown() || mods.isAltDown())|NL|            return false;|NL||NL|        Array<ItemComponent*> rows;|NL|        for (auto* child : items)|NL|            if (canBeTriggered (child->item) || hasActiveSubMenu (child->item))|NL|                rows.add (child);|NL||NL|        const auto row = lynxTypeAheadHook (this, typed, rows.indexOf (currentChild.getComponent()), rows.size(),|NL|                                            [&rows] (int i) { return rows[i]->item.text; });|NL|        if (! isPositiveAndBelow (row, rows.size()))|NL|            return false;|NL||NL|        lynxTypedAt = Time::getMillisecondCounter();|NL|        disableTimerUntilMouseMoves();|NL|        ensureItemComponentIsVisible (*rows[row], -1);|NL|        setCurrentlyHighlightedChild (rows[row]);|NL|        return true;|NL|    }|NL||NL|    // a prefix is being typed (a key within a second of the last one that jumped): its next letters go to it first, not to|NL|    // the accelerator letters|NL|    uint32 lynxTypedAt = 0;|NL|    bool lynxTypingNow() const { return lynxTypedAt != 0 && Time::getMillisecondCounter() - lynxTypedAt < 1000; }|NL||NL|    bool keyPressed (const KeyPress& key) override|NL|    {|NL|        if (key.isKeyCode (KeyPress::downKey))")
        lynx_juce_fix("menu type-to-jump keys" "LYNX patch (round 190ib): type-to-jump after them"
            "        else if (! lynxMenuMnemonic (key)) // LYNX patch (round 170a): accelerator letters"
            "        else if (! (lynxTypingNow() ? lynxMenuTypeAhead (key) || lynxMenuMnemonic (key) : lynxMenuMnemonic (key) || lynxMenuTypeAhead (key))) // LYNX patch (round 170a): accelerator letters; LYNX patch (round 190ib): type-to-jump after them")
        lynx_juce_fix("menu type-to-jump: space inside a prefix" "LYNX patch (round 190ib): a space inside a typed prefix"
            "        else if (key.isKeyCode (KeyPress::returnKey) || key.isKeyCode (KeyPress::spaceKey))|NL|        {|NL|            triggerCurrentlyHighlightedItem();"
            "        else if (key.isKeyCode (KeyPress::returnKey) || (key.isKeyCode (KeyPress::spaceKey) && ! lynxTypingNow())) // LYNX patch (round 190ib): a space inside a typed prefix is typed|NL|        {|NL|            triggerCurrentlyHighlightedItem();")
    elseif(file MATCHES "juce_ListBox.cpp$")
        lynx_juce_fix("list type-to-jump: the hook" "LYNX patch (round 190ib, issue #182): type-to-jump"
            "bool ListBox::keyPressed (const KeyPress& key)|NL|{|NL|"
            "// LYNX patch (round 190ib, issue #182): type-to-jump (cmake/LynxPatchJuce.cmake, fix 37). Set by LYNX (ui/TypeAhead.h): the|NL|// row to go to after `typed` in a list (or menu) of `count` rows, -1 for none. Null: JUCE's keys only.|NL|int (*lynxTypeAheadHook) (const void* owner, juce_wchar typed, int current, int count, const std::function<String (int)>& textOf) = nullptr;|NL||NL|static int lynxListTypeAheadRow (const void* owner, ListBoxModel* model, const KeyPress& key, int current, int count)|NL|{|NL|    const auto mods = key.getModifiers();|NL|    const auto typed = key.getTextCharacter();|NL|    if (lynxTypeAheadHook == nullptr || model == nullptr || count <= 0 || typed < 32 || mods.isCtrlDown() || mods.isCommandDown() || mods.isAltDown())|NL|        return -1;|NL||NL|    if (model->getNameForRow (0) == \"Row 1\") // (JUCE's default names: the model doesn't name its rows)|NL|        return -1;|NL||NL|    return lynxTypeAheadHook (owner, typed, current, count, [model] (int row) { return model->getNameForRow (row); });|NL|}|NL||NL|bool ListBox::keyPressed (const KeyPress& key)|NL|{|NL|")
        lynx_juce_fix("list type-to-jump: the keys" "LYNX patch (round 190ib): a typed character selects"
            "        selectRangeOfRows (0, std::numeric_limits<int>::max());|NL|    }|NL|    else|NL|    {|NL|        return false;|NL|    }"
            "        selectRangeOfRows (0, std::numeric_limits<int>::max());|NL|    }|NL|    else if (const auto lynxRow = lynxListTypeAheadRow (this, model, key, lastRowSelected, totalItems); lynxRow >= 0)|NL|    {|NL|        selectRow (lynxRow); // LYNX patch (round 190ib): a typed character selects the row that starts with it (fix 37)|NL|    }|NL|    else|NL|    {|NL|        return false;|NL|    }")
    elseif(file MATCHES "juce_Direct2DImage_windows.cpp$")
        lynx_juce_fix("image contexts without readback" "LYNX patch (round 174e2): no readback flag"
            "/*  Returns new software bitmap storage with content matching the provided hardware bitmap. */"
            "// LYNX patch (round 174e2): no readback flag. Contexts made while it is set do not copy the image back to its|NL|// software copy when they end (cmake/LynxPatchJuce.cmake, fix 15)|NL|bool lynxSkipImageReadback = false;|NL||NL|/*  Returns new software bitmap storage with content matching the provided hardware bitmap. */")
        lynx_juce_fix("image contexts without readback (end)" "LYNX patch (round 174e2): readback skipped"
            "            endFrame();|NL|            readFromDirect2DBitmap (storedContext, storedTarget, backup);"
            "            endFrame();|NL|            if (! skipReadback) // LYNX patch (round 174e2): readback skipped when asked|NL|                readFromDirect2DBitmap (storedContext, storedTarget, backup);")
        lynx_juce_fix("image contexts without readback (member)" "LYNX patch (round 174e2): the flag when made"
            "        ImagePixelData::Ptr backup;|NL|    };"
            "        ImagePixelData::Ptr backup;|NL|        const bool skipReadback = lynxSkipImageReadback; // LYNX patch (round 174e2): the flag when made|NL|    };")
        lynx_juce_fix("image readback put off (definitions)" "LYNX patch (round 190bm): the readback put off"
            "bool lynxSkipImageReadback = false;|NL|"
            "bool lynxSkipImageReadback = false;|NL||NL|// LYNX patch (round 190bm): the readback put off (cmake/LynxPatchJuce.cmake, fix 22). The flag above applies to one|NL|// image only (lynxNoReadbackImage, its pixel data), and a skipped readback is only put off: the device bitmap that|NL|// holds the picture is remembered and read back when the software copy is needed (the image drawn by a software|NL|// renderer, another device's pages, a software context). A picture that could not be read back any more is counted.|NL|const ImagePixelData* lynxNoReadbackImage = nullptr;|NL|int lynxImageContentLost = 0;|NL||NL|struct LynxPendingReadback|NL|{|NL|    ComSmartPtr<ID2D1DeviceContext1> context;|NL|    ComSmartPtr<ID2D1Bitmap1> bitmap;|NL|    ImagePixelData::Ptr backing;|NL|};|NL||NL|static std::atomic<int> lynxPendingCount { 0 };|NL||NL|static CriticalSection& lynxPendingLock()|NL|{|NL|    static CriticalSection lock;|NL|    return lock;|NL|}|NL||NL|static std::map<const ImagePixelData*, LynxPendingReadback>& lynxPendingReadbacks()|NL|{|NL|    static std::map<const ImagePixelData*, LynxPendingReadback> pending;|NL|    return pending;|NL|}|NL||NL|static void lynxForgetPending (const ImagePixelData* image)|NL|{|NL|    if (lynxPendingCount.load() == 0)|NL|        return;|NL||NL|    LynxPendingReadback released; // (its COM objects are released outside the lock)|NL|    const ScopedLock lock (lynxPendingLock());|NL|    auto& pending = lynxPendingReadbacks();|NL||NL|    if (const auto found = pending.find (image); found != pending.end())|NL|    {|NL|        released = found->second;|NL|        pending.erase (found);|NL|        --lynxPendingCount;|NL|    }|NL|}|NL||NL|static void lynxSetPending (const ImagePixelData* image,|NL|                            ComSmartPtr<ID2D1DeviceContext1> context,|NL|                            ComSmartPtr<ID2D1Bitmap1> bitmap,|NL|                            ImagePixelData::Ptr backing)|NL|{|NL|    const ScopedLock lock (lynxPendingLock());|NL|    auto& pending = lynxPendingReadbacks();|NL||NL|    if (pending.find (image) == pending.end())|NL|        ++lynxPendingCount;|NL||NL|    pending[image] = LynxPendingReadback { context, bitmap, backing };|NL|}|NL||NL|/*  Brings the software copy of the image up to date, unless its picture lives on exceptOnDevice. */|NL|static void lynxReadBackPending (const ImagePixelData* image, ID2D1Device1* exceptOnDevice)|NL|{|NL|    if (lynxPendingCount.load() == 0)|NL|        return;|NL||NL|    LynxPendingReadback toRead;|NL||NL|    {|NL|        const ScopedLock lock (lynxPendingLock());|NL|        auto& pending = lynxPendingReadbacks();|NL|        const auto found = pending.find (image);|NL||NL|        if (found == pending.end())|NL|            return;|NL||NL|        if (exceptOnDevice != nullptr && getDeviceForContext (found->second.context) == exceptOnDevice)|NL|            return;|NL||NL|        toRead = found->second;|NL|        pending.erase (found);|NL|        --lynxPendingCount;|NL|    }|NL||NL|    if (! readFromDirect2DBitmap (toRead.context, toRead.bitmap, toRead.backing))|NL|        ++lynxImageContentLost;|NL|}|NL|")
        lynx_juce_fix("image readback from a device that went away" "LYNX patch (round 190bm): a device that went away"
            "    readableBitmap->CopyFromBitmap (&dstPoint, bitmap, &srcRect);|NL|"
            "    // LYNX patch (round 190bm): a device that went away gives nothing to read (was: a read through a null mapping)|NL|    if (readableBitmap == nullptr || FAILED (readableBitmap->CopyFromBitmap (&dstPoint, bitmap, &srcRect)))|NL|        return false;|NL||NL|    {|NL|        D2D1_MAPPED_RECT probe{};|NL||NL|        if (FAILED (readableBitmap->Map (D2D1_MAP_OPTIONS_READ, &probe)) || probe.bits == nullptr)|NL|            return false;|NL||NL|        readableBitmap->Unmap();|NL|    }|NL|")
        lynx_juce_fix("image readback put off (image deleted)" "LYNX patch (round 190bm): nothing left to read"
            "Direct2DPixelData::~Direct2DPixelData()|NL|{|NL|"
            "Direct2DPixelData::~Direct2DPixelData()|NL|{|NL|    lynxForgetPending (this); // LYNX patch (round 190bm): nothing left to read back|NL|")
        lynx_juce_fix("image readback put off (another device)" "LYNX patch (round 190bm): another device's pages"
            "    const auto device1 = getDeviceForContext (context);|NL||NL|    if (device1 == nullptr)|NL|        return pagesForDevice.end();|NL|"
            "    const auto device1 = getDeviceForContext (context);|NL||NL|    if (device1 == nullptr)|NL|        return pagesForDevice.end();|NL||NL|    // LYNX patch (round 190bm): another device's pages are filled from the software copy: it is brought up to date|NL|    lynxReadBackPending (this, device1);|NL|")
        lynx_juce_fix("image readback put off (software context)" "LYNX patch (round 190bm): the software context draws"
            "        for (auto& pair : pagesForDevice)|NL|            pair.second.markOutdated();|NL||NL|        return backingData->createLowLevelContext();"
            "        lynxReadBackPending (this, nullptr); // LYNX patch (round 190bm): the software context draws over the picture|NL||NL|        for (auto& pair : pagesForDevice)|NL|            pair.second.markOutdated();|NL||NL|        return backingData->createLowLevelContext();")
        lynx_juce_fix("image readback put off (pixels read)" "LYNX patch (round 190bm): the copy is read"
            "    backingData->initialiseBitmapData (bitmap, x, y, mode);|NL|"
            "    lynxReadBackPending (this, nullptr); // LYNX patch (round 190bm): the copy is read (or partly written) from here|NL|    backingData->initialiseBitmapData (bitmap, x, y, mode);|NL|")
        lynx_juce_fix("image readback put off (context end)" "LYNX patch (round 190bm): put off, not dropped"
            "            if (! skipReadback) // LYNX patch (round 174e2): readback skipped when asked|NL|                readFromDirect2DBitmap (storedContext, storedTarget, backup);"
            "            if (! skipReadback) // LYNX patch (round 174e2): readback skipped when asked|NL|                readFromDirect2DBitmap (storedContext, storedTarget, backup);|NL||NL|            // LYNX patch (round 190bm): put off, not dropped: read back when the software copy is needed|NL|            if (skipReadback)|NL|                lynxSetPending (self.get(), storedContext, storedTarget, backup);|NL|            else|NL|                lynxForgetPending (self.get());")
        lynx_juce_fix("image readback put off (one image)" "LYNX patch (round 190bm): for one image"
            "        const bool skipReadback = lynxSkipImageReadback; // LYNX patch (round 174e2): the flag when made|NL|"
            "        // LYNX patch (round 174e2): the flag when made|NL|        // LYNX patch (round 190bm): for one image only (a Graphics made on another image meanwhile reads back as ever)|NL|        const bool skipReadback = lynxSkipImageReadback && lynxNoReadbackImage == self.get();|NL|")
        lynx_juce_fix("software images on a thread that asks for them" "LYNX patch (round 190hd): software images"
            "ImagePixelData::Ptr NativeImageType::create (Image::PixelFormat format, int width, int height, bool clearImage) const|NL|{|NL|    SharedResourcePointer<DirectX> directX;|NL|"
            "// LYNX patch (round 190hd): software images on a thread that asks for them (cmake/LynxPatchJuce.cmake, fix 30)|NL|static thread_local bool lynxSoftwareImages = false;|NL|void lynxSoftwareImagesOnThisThread (bool on) { lynxSoftwareImages = on; }|NL||NL|ImagePixelData::Ptr NativeImageType::create (Image::PixelFormat format, int width, int height, bool clearImage) const|NL|{|NL|    if (lynxSoftwareImages) // LYNX patch (round 190hd): this thread draws with the CPU (no Direct2D device made for it)|NL|        return new SoftwarePixelData { format, width, height, clearImage };|NL||NL|    SharedResourcePointer<DirectX> directX;|NL|")
    elseif(file MATCHES "juce_Windowing_linux.cpp$")
        lynx_juce_fix("no X focus for menu windows" "LYNX patch (issue #45): never takes the X focus"
            "    void grabFocus() override|NL|    {|NL|"
            "    void grabFocus() override|NL|    {|NL|        // LYNX patch (issue #45): never takes the X focus when it ignores key presses (a popup menu, a tooltip)|NL|        if ((getStyleFlags() & windowIgnoresKeyPresses) != 0)|NL|            return;|NL||NL|")
        lynx_juce_fix("a minimised window covers nothing" "LYNX patch (issue #188): a window above"
            "            if (auto* peer = static_cast<LinuxComponentPeer*> (otherPeer))|NL|                if (peer->contains (globalToLocal (*peer, localToGlobal (*this, localPos.toFloat())).roundToInt(), true))|NL|                    return false;|NL|"
            "            // LYNX patch (issue #188): a window above covers the point only while it is on the screen (a minimised|NL|            // window keeps its bounds and stays visible for JUCE; the X round trips only inside its bounds)|NL|            if (auto* peer = static_cast<LinuxComponentPeer*> (otherPeer))|NL|                if (peer->contains (globalToLocal (*peer, localToGlobal (*this, localPos.toFloat())).roundToInt(), true)|NL|                    && peer->lynxIsOnScreen())|NL|                    return false;|NL|")
        lynx_juce_fix("X11 blits told to LYNX: the hook" "LYNX patch (round 190jx): the hook LYNX's moving marks"
            "class LinuxComponentPeer final : public ComponentPeer,"
            "// LYNX patch (round 190jx): the hook LYNX's moving marks (ui/MotionOverlayX11.h) use to draw again over pixels JUCE|NL|// replaced: the window and the rectangles just blitted to it, in window pixels (cmake/LynxPatchJuce.cmake, fix 41)|NL|void (*lynxX11AfterBlitHook) (unsigned long window, const RectangleList<int>& blitted) = nullptr;|NL|void* lynxX11Display() { return XWindowSystem::getInstance()->getDisplay(); }|NL||NL|class LinuxComponentPeer final : public ComponentPeer,")
        lynx_juce_fix("X11 blits told to LYNX: after a blit" "LYNX patch (round 190jx): the window's pixels"
            "                for (auto& i : originalRepaintRegion)|NL|                   XWindowSystem::getInstance()->blitToWindow (peer.windowH, image, i, totalArea);|NL|"
            "                for (auto& i : originalRepaintRegion)|NL|                   XWindowSystem::getInstance()->blitToWindow (peer.windowH, image, i, totalArea);|NL||NL|                // LYNX patch (round 190jx): the window's pixels there are new (fix 41)|NL|                if (lynxX11AfterBlitHook != nullptr)|NL|                    lynxX11AfterBlitHook ((unsigned long) peer.windowH, originalRepaintRegion);|NL|")
        lynx_juce_fix("a minimised window covers nothing: on the screen" "LYNX patch (issue #188): on the screen"
            "    bool contains (Point<int> localPos, bool trueIfInAChildWindow) const override|NL|    {|NL|"
            "    // LYNX patch (issue #188): on the screen = mapped, viewable (its ancestors mapped too) and not minimised|NL|    bool lynxIsOnScreen() const|NL|    {|NL|        if (windowH == 0)|NL|            return false;|NL||NL|        {|NL|            XWindowSystemUtilities::ScopedXLock xLock;|NL|            XWindowAttributes atts;|NL||NL|            if (! X11Symbols::getInstance()->xGetWindowAttributes (XWindowSystem::getInstance()->getDisplay(), windowH, &atts)|NL|                || atts.map_state != IsViewable)|NL|                return false;|NL|        }|NL||NL|        return ! XWindowSystem::getInstance()->isMinimised (windowH);|NL|    }|NL||NL|    bool contains (Point<int> localPos, bool trueIfInAChildWindow) const override|NL|    {|NL|")
    elseif(file MATCHES "juce_Displays.cpp$")
        lynx_juce_fix("monitors that touch no other" "LYNX patch (issue #45): a monitor that touches"
            "        // All of the nodes should have a parent|NL|        jassert (node.parent != nullptr);|NL|"
            "        // LYNX patch (issue #45): a monitor that touches no other one keeps its own place (physical / scale)|NL|        if (node.parent == nullptr)|NL|            node.logicalArea = node.display->totalArea.toDouble() / node.display->scale;|NL|")
    elseif(file MATCHES "juce_XWindowSystem_linux.cpp$")
        lynx_juce_fix("raise without activating" "LYNX patch (issue #45): raise only"
            "void XWindowSystem::toFront (::Window windowH, bool) const|NL|{|NL|    jassert (windowH != 0);|NL|"
            "void XWindowSystem::toFront (::Window windowH, bool makeActive) const|NL|{|NL|    jassert (windowH != 0);|NL||NL|    // LYNX patch (issue #45): raise only, without activating, when the window should not become active|NL|    if (! makeActive)|NL|    {|NL|        XWindowSystemUtilities::ScopedXLock xLock;|NL|        XWindowAttributes atts;|NL||NL|        if (X11Symbols::getInstance()->xGetWindowAttributes (display, windowH, &atts) && atts.map_state == IsViewable)|NL|            X11Symbols::getInstance()->xMapRaised (display, windowH);|NL||NL|        X11Symbols::getInstance()->xSync (display, False);|NL|        return;|NL|    }|NL||NL|")

        lynx_juce_fix("menus kept when a window is only restacked" "LYNX patch (issue #45): only a window that moved"
            "void XWindowSystem::dismissBlockingModals (LinuxComponentPeer* peer) const|NL|{|NL|"
            "// LYNX patch (issue #45): only a window that moved or changed size dismisses the menus (not a restack)|NL|static bool lynxWindowMovedOrResized (const XConfigureEvent& event)|NL|{|NL|    static std::map<std::pair<::Window, bool>, Rectangle<int>> lastGeometry;|NL|    const Rectangle<int> geometry (event.x, event.y, event.width, event.height);|NL|    const auto [it, isNew] = lastGeometry.try_emplace (std::make_pair (event.window, event.send_event != 0), geometry);|NL||NL|    if (isNew)|NL|        return false;|NL||NL|    const auto changed = it->second != geometry;|NL|    it->second = geometry;|NL|    return changed;|NL|}|NL||NL|void XWindowSystem::dismissBlockingModals (LinuxComponentPeer* peer) const|NL|{|NL|")

        lynx_juce_fix("title bar dragged, not restacked" "LYNX patch (issue #45): a title bar that moved"
            "    if ((peer->getStyleFlags() & ComponentPeer::windowHasTitleBar) != 0)|NL|        dismissBlockingModals (peer);"
            "    // LYNX patch (issue #45): a title bar that moved, not a restack|NL|    if ((peer->getStyleFlags() & ComponentPeer::windowHasTitleBar) != 0 && lynxWindowMovedOrResized (confEvent))|NL|        dismissBlockingModals (peer);")

        lynx_juce_fix("frame moved, not restacked" "LYNX patch (issue #45): a frame that moved"
            "    if (configure.window != peerHandle && isParentWindowOf (configure.window, peerHandle))|NL|        dismissBlockingModals (peer);"
            "    // LYNX patch (issue #45): a frame that moved, not a restack|NL|    if (configure.window != peerHandle && isParentWindowOf (configure.window, peerHandle) && lynxWindowMovedOrResized (configure))|NL|        dismissBlockingModals (peer);")

        lynx_juce_fix("WM_PROTOCOLS atoms created (fix 50)" "LYNX patch (fix 50): created when missing"
            "    protocols                    = getIfExists (display, \"WM_PROTOCOLS\");|NL|    protocolList [TAKE_FOCUS]    = getIfExists (display, \"WM_TAKE_FOCUS\");|NL|    protocolList [DELETE_WINDOW] = getIfExists (display, \"WM_DELETE_WINDOW\");|NL|    protocolList [PING]          = getIfExists (display, \"_NET_WM_PING\");|NL|"
            "    // LYNX patch (fix 50): created when missing. On an X server no window manager ever ran on (a fresh Xvfb)|NL|    // these atoms don't exist yet, and every new window set its WM_PROTOCOLS property with atom 0: BadAtom, which ends|NL|    // a plugin's host (JUCE installs its X error handler only in a standalone app)|NL|    protocols                    = getCreating (display, \"WM_PROTOCOLS\");|NL|    protocolList [TAKE_FOCUS]    = getCreating (display, \"WM_TAKE_FOCUS\");|NL|    protocolList [DELETE_WINDOW] = getCreating (display, \"WM_DELETE_WINDOW\");|NL|    protocolList [PING]          = getCreating (display, \"_NET_WM_PING\");|NL|")

        lynx_juce_fix("no WM_STATE read with atom 0 (fix 50)" "LYNX patch (fix 50): no WM_STATE atom"
            "    XWindowSystemUtilities::ScopedXLock xLock;|NL|    XWindowSystemUtilities::GetXProperty prop (display, w, atoms.state, 0, 64, false, atoms.state);|NL|"
            "    // LYNX patch (fix 50): no WM_STATE atom (no window manager ever ran), so no window has that property; asking with|NL|    // atom 0 was a BadAtom error|NL|    if (atoms.state == None)|NL|        return false;|NL||NL|    XWindowSystemUtilities::ScopedXLock xLock;|NL|    XWindowSystemUtilities::GetXProperty prop (display, w, atoms.state, 0, 64, false, atoms.state);|NL|")
        lynx_juce_fix("no _NET_WM_STATE read with atom 0 (fix 50)" "LYNX patch (fix 50): no _NET_WM_STATE atom"
            "    XWindowSystemUtilities::ScopedXLock xLock;|NL|    XWindowSystemUtilities::GetXProperty prop (display, w, atoms.windowState, 0, 128, false, XA_ATOM);|NL|"
            "    // LYNX patch (fix 50): no _NET_WM_STATE atom (no EWMH window manager ever ran), so no window has that property|NL|    if (atoms.windowState == None)|NL|        return false;|NL||NL|    XWindowSystemUtilities::ScopedXLock xLock;|NL|    XWindowSystemUtilities::GetXProperty prop (display, w, atoms.windowState, 0, 128, false, XA_ATOM);|NL|")

        lynx_juce_fix("horizontal wheel (buttons 6 / 7) (fix 49)" "LYNX patch (fix 49, issue #254)"
            "    auto mapIndex = (uint32) (buttonPressEvent.button - Button1);|NL|"
            "    // LYNX patch (fix 49, issue #254): buttons 6 / 7 are the horizontal wheel (a tilt wheel, a second wheel), left /|NL|    // right; JUCE dropped them. deltaX > 0 = left, as Windows' WM_MOUSEHWHEEL comes through JUCE|NL|    if (buttonPressEvent.button == 6 || buttonPressEvent.button == 7)|NL|    {|NL|        MouseWheelDetails wheel;|NL|        wheel.deltaX = buttonPressEvent.button == 6 ? 50.0f / 256.0f : -50.0f / 256.0f;|NL|        wheel.deltaY = 0.0f;|NL|        wheel.isReversed = false;|NL|        wheel.isSmooth = false;|NL|        wheel.isInertial = false;|NL||NL|        peer->handleMouseWheel (MouseInputSource::InputSourceType::mouse, getLogicalMousePos (buttonPressEvent, peer->getPlatformScaleFactor()),|NL|                                getEventTime (buttonPressEvent), wheel);|NL|        return;|NL|    }|NL||NL|    auto mapIndex = (uint32) (buttonPressEvent.button - Button1);|NL|")
    elseif(file MATCHES "juce_audio_plugin_client_LV2.cpp$")
        lynx_juce_fix("LV2: optional audio inputs may stay unconnected (fix 50)" "LYNX patch (fix 50): an input port"
            "        for (auto i = 0, end = processor->getTotalNumInputChannels(); i < end; ++i)|NL|            audio.copyFrom (i, 0, ports.getBufferForAudioInput (i), audio.getNumSamples());|NL|"
            "        // LYNX patch (fix 50): an input port the host left unconnected is silence. JUCE declares the ports of a bus that|NL|        // is off by default (a side chain) lv2:connectionOptional, so a host may connect them to NULL, and copying from|NL|        // there crashed (JUCE's own LV2 host does it for every channel its buffer doesn't have)|NL|        for (auto i = 0, end = processor->getTotalNumInputChannels(); i < end; ++i)|NL|        {|NL|            if (const auto* input = ports.getBufferForAudioInput (i))|NL|                audio.copyFrom (i, 0, input, audio.getNumSamples());|NL|            else|NL|                audio.clear (i, 0, audio.getNumSamples());|NL|        }|NL|")
        lynx_juce_fix("LV2: parameter ranges as decimals (fix 50)" "LYNX patch (fix 50): decimals"
            "            os << \"\\\\tlv2:default \" << defaultValue << \" ;\\\\n\"|NL|                  \"\\\\tlv2:minimum \" << min << \" ;\\\\n\"|NL|                  \"\\\\tlv2:maximum \" << max;|NL|"
            "            // LYNX patch (fix 50): decimals (\"0.0\", not \"0\": an integer literal is no atom:Float; lv2lint failed every|NL|            // parameter on it)|NL|            const auto lynxDecimal = [] (float v) { const String text (v); return text.containsAnyOf (\".eE\") ? text : text + \".0\"; };|NL||NL|            os << \"\\\\tlv2:default \" << lynxDecimal (defaultValue) << \" ;\\\\n\"|NL|                  \"\\\\tlv2:minimum \" << lynxDecimal (min) << \" ;\\\\n\"|NL|                  \"\\\\tlv2:maximum \" << lynxDecimal (max);|NL|")
        lynx_juce_fix("LV2: scale points as decimals (fix 50)" "LYNX patch (fix 50): a decimal"
            "                          \"\\\\t\\\\trdf:value \" << value << \" ;\\\\n\"|NL|"
            "                          \"\\\\t\\\\trdf:value \" << lynxDecimal (value) << \" ;\\\\n\" // LYNX patch (fix 50): a decimal|NL|")
        lynx_juce_fix("LV2: the effect's plugin class (fix 50)" "LYNX patch (fix 50): the effect's class"
            "             #else|NL|              \"lv2:Plugin\"|NL|             #endif|NL|"
            "             #elif defined (JucePlugin_LV2Class) // LYNX patch (fix 50): the effect's class (lv2:EQPlugin, ...)|NL|              JucePlugin_LV2Class|NL|             #else|NL|              \"lv2:Plugin\"|NL|             #endif|NL|")
        lynx_juce_fix("LV2: the plugin's licence in its .ttl (fix 51)" "LYNX patch (fix 51): the licence"
            "              \"\\\\tlv2:minorVersion \" << minorVersion << \" ;\\\\n\"|NL|"
            "              \"\\\\tdoap:license <http://spdx.org/licenses/AGPL-3.0-or-later> ;\\\\n\" // LYNX patch (fix 51): the licence (LYNX's plugins are AGPL-3.0-or-later)|NL|              \"\\\\tlv2:minorVersion \" << minorVersion << \" ;\\\\n\"|NL|")
        lynx_juce_fix("LV2: unconnected outputs while suspended (fix 50)" "LYNX patch (fix 50): an output port"
            "                    const auto ptr = ports.getBufferForAudioOutput (i);|NL|                    std::fill (ptr, ptr + numSteps, 0.0f);|NL|"
            "                    const auto ptr = ports.getBufferForAudioOutput (i);|NL||NL|                    if (ptr != nullptr) // LYNX patch (fix 50): an output port the host left unconnected|NL|                        std::fill (ptr, ptr + numSteps, 0.0f);|NL|")
    elseif(file MATCHES "juce_CoreGraphicsContext_mac.mm$")
        lynx_juce_fix("no gradient flood from an empty path" "LYNX patch (issue #11): an empty path"
            "        CGContextReplacePathWithStrokedPath (context.get());|NL||NL|    ScopedCGContextState scopedState (context.get());"
            "        CGContextReplacePathWithStrokedPath (context.get());|NL||NL|    // LYNX patch (issue #11): an empty path draws nothing (Quartz ignores a clip to it: the fill flooded everything)|NL|    if (CGContextIsPathEmpty (context.get()))|NL|        return;|NL||NL|    ScopedCGContextState scopedState (context.get());")

        lynx_juce_fix("clip to an empty path" "LYNX patch (issue #11): clipping to an empty path"
            "void CoreGraphicsContext::clipToPath (const Path& path, const AffineTransform& transform)|NL|{|NL|    createPath (path, transform);|NL|"
            "void CoreGraphicsContext::clipToPath (const Path& path, const AffineTransform& transform)|NL|{|NL|    createPath (path, transform);|NL||NL|    // LYNX patch (issue #11): clipping to an empty path leaves nothing (Quartz would ignore it)|NL|    if (CGContextIsPathEmpty (context.get()))|NL|    {|NL|        CGContextClipToRect (context.get(), CGRectZero);|NL|        lastClipRect.reset();|NL|        return;|NL|    }|NL|")
    elseif(file MATCHES "juce_RenderingHelpers.h$")
        lynx_juce_fix("path edge tables over the path's own rows (fix 45)" "LYNX patch (round 190ms, fix 45): the edge table over the clip's rows"
            "            if (path.getBoundsTransformed (trans).getSmallestIntegerContainer().intersects (clipRect))|NL|                fillShape (*new EdgeTableRegionType (clipRect, path, trans), false);"
            "            // LYNX patch (round 190ms, fix 45): the edge table over the clip's rows from one above the path's to one below|NL|            // (all its columns), not over every row of the clip (a row per pixel row of the clip for every shape); the|NL|            // same pixels (cmake/LynxPatchJuce.cmake)|NL|            const auto lynxPathArea = path.getBoundsTransformed (trans).getSmallestIntegerContainer();|NL||NL|            if (lynxPathArea.intersects (clipRect))|NL|            {|NL|                const auto lynxNear = [] (int v) { return v > -0x1000000 && v < 0x1000000; };|NL|                const auto lynxArea = lynxNear (lynxPathArea.getY()) && lynxNear (lynxPathArea.getBottom())|NL|                                          ? Rectangle<int>::leftTopRightBottom (clipRect.getX(), jmax (clipRect.getY(), lynxPathArea.getY() - 1),|NL|                                                                                clipRect.getRight(), jmin (clipRect.getBottom(), lynxPathArea.getBottom() + 1))|NL|                                          : clipRect;|NL|                fillShape (*new EdgeTableRegionType (lynxArea, path, trans), false);|NL|            }")
        lynx_juce_fix("RGB rows into ARGB images (fix 45)" "LYNX patch (round 190ms, fix 45): an RGB row"
            "            else|NL|            {|NL|                do|NL|                {|NL|                    dest->blend (*src);|NL|                    dest = addBytesToPointer (dest, destStride);|NL|                    src  = addBytesToPointer (src, srcStride);|NL|                } while (--width > 0);|NL|            }"
            "            else|NL|            {|NL|               #if JUCE_LITTLE_ENDIAN && ! JUCE_ANDROID|NL|                // LYNX patch (round 190ms, fix 45): an RGB row into an ARGB one, as PixelARGB::blend (PixelRGB) = set does it|NL|                // (0xff000000 | r << 16 | g << 8 | b), one 4-byte load per pixel (cmake/LynxPatchJuce.cmake)|NL|                if constexpr (std::is_same_v<DestPixelType, PixelARGB> && std::is_same_v<SrcPixelType, PixelRGB>|NL|                              && PixelRGB::indexB == 0 && PixelRGB::indexG == 1 && PixelRGB::indexR == 2)|NL|                {|NL|                    if (destStride == 4 && srcStride == 3 && width > 0)|NL|                    {|NL|                        auto* d = reinterpret_cast<uint8*> (dest);|NL|                        auto* s = reinterpret_cast<const uint8*> (src);|NL||NL|                        for (int i = 0; i < width - 1; ++i)|NL|                        {|NL|                            uint32 v;|NL|                            memcpy (&v, s + 3 * i, 4);|NL|                            v |= 0xff000000u;|NL|                            memcpy (d + 4 * i, &v, 4);|NL|                        }|NL||NL|                        const auto* last = s + 3 * (width - 1); // (its fourth byte would be past the row)|NL|                        const auto v = 0xff000000u | (uint32) last[0] | ((uint32) last[1] << 8) | ((uint32) last[2] << 16);|NL|                        memcpy (d + 4 * (width - 1), &v, 4);|NL|                        return;|NL|                    }|NL|                }|NL|               #endif|NL||NL|                do|NL|                {|NL|                    dest->blend (*src);|NL|                    dest = addBytesToPointer (dest, destStride);|NL|                    src  = addBytesToPointer (src, srcStride);|NL|                } while (--width > 0);|NL|            }")
        lynx_juce_fix("glyph cache keys found by a hash of the font (fix 46)" "LYNX patch (round 190ms, fix 46): a hash"
            "        return cache.get (Key { font, glyphNumber }, [] (const auto& key)"
            "        return cache.get (Key { font, glyphNumber, Key::hashOf (font) }, [] (const auto& key)")
        lynx_juce_fix("glyph cache key order (fix 46)" "LYNX patch (round 190ms, fix 46): the glyph, a hash"
            "    struct Key|NL|    {|NL|        Font font;|NL|        int glyph;|NL||NL|        bool operator< (const Key& other) const|NL|        {|NL|            if (glyph < other.glyph)|NL|                return true;|NL||NL|            if (other.glyph < glyph)|NL|                return false;|NL||NL|            return GraphicsFontHelpers::compareFont (font, other.font);|NL|        }|NL|    };"
            "    struct Key|NL|    {|NL|        Font font;|NL|        int glyph;|NL|        uint64 fontHash; // LYNX patch (round 190ms, fix 46): a hash of what tells fonts apart, see operator<|NL||NL|        static uint64 hashOf (const Font& f)|NL|        {|NL|            const auto bits = [] (float v) { uint32 b; memcpy (&b, &v, sizeof (b)); return (uint64) b; };|NL|            auto h = (uint64) f.getTypefaceName().hashCode64();|NL|            h = (h ^ (uint64) f.getTypefaceStyle().hashCode64()) * 0x100000001b3ull;|NL|            h = (h ^ bits (f.getHeight())) * 0x100000001b3ull;|NL|            return (h ^ bits (f.getHorizontalScale())) * 0x100000001b3ull;|NL|        }|NL||NL|        // LYNX patch (round 190ms, fix 46): the glyph, a hash of the font's name, style, height and width, then|NL|        // the font itself (a strict weak order whose equal keys are JUCE's equal keys). JUCE compared whole FontOptions|NL|        // (strings first) at every node of the same glyph: ~1000 instructions each, ~5 per look-up, 12 % of a frame.|NL|        bool operator< (const Key& other) const|NL|        {|NL|            if (glyph != other.glyph)|NL|                return glyph < other.glyph;|NL||NL|            if (fontHash != other.fontHash)|NL|                return fontHash < other.fontHash;|NL||NL|            if (font == other.font) // (the same shared font, or equal options: equal keys)|NL|                return false;|NL||NL|            return GraphicsFontHelpers::compareFont (font, other.font);|NL|        }|NL|    };")
        lynx_juce_fix("software glyph cache of 1024 (fix 46)" "LYNX patch (round 190ms, fix 46): 1024 glyphs"
            "    LruCache<Key, std::vector<GlyphLayer>> cache;|NL|    CriticalSection lock;"
            "    // LYNX patch (round 190ms, fix 46): 1024 glyphs (LruCache's default 128 is fewer than one busy window|NL|    // shows, so a whole repaint evicted and rebuilt its own glyphs; cmake/LynxPatchJuce.cmake)|NL|    LruCache<Key, std::vector<GlyphLayer>, 1024> cache;|NL|    CriticalSection lock;")
    elseif(file MATCHES "juce_GraphicsContext.cpp$")
        lynx_juce_fix("text arrangement caches of 1024 (fix 46)" "LYNX patch (round 190ms, fix 46): 1024 shaped lines"
            "        LruCache<ArrangementArgs, ConfiguredArrangement> cache;"
            "        // LYNX patch (round 190ms, fix 46): 1024 shaped lines, not LruCache's default 128 (a busy window shows|NL|        // more labels than that and shaped them all again every whole repaint; cmake/LynxPatchJuce.cmake)|NL|        LruCache<ArrangementArgs, ConfiguredArrangement, 1024> cache;")
        lynx_juce_fix("drawSingleLineText's key holds its flags (fix 46)" "LYNX patch (round 190ms, fix 46): the flags"
            "        auto tie() const noexcept { return std::tie (font, text, startX, baselineY); }"
            "        // LYNX patch (round 190ms, fix 46): the flags are part of the line; the cheap fields first, the font|NL|        // last (cmake/LynxPatchJuce.cmake)|NL|        auto tie() const noexcept { return std::tie (startX, baselineY, flags, text, font); }")
        lynx_juce_fix("drawMultiLineText's key, cheap fields first (fix 46)" "LYNX patch (round 190ms, fix 46): key order, multi-line text"
            "        auto tie() const noexcept { return std::tie (font, text, startX, baselineY, maximumLineWidth, justification, leading); }"
            "        // LYNX patch (round 190ms, fix 46): key order, multi-line text: the cheap fields first, the font last|NL|        auto tie() const noexcept { return std::tie (startX, baselineY, maximumLineWidth, justification, leading, text, font); }")
        lynx_juce_fix("drawText's key, cheap fields first (fix 46)" "LYNX patch (round 190ms, fix 46): key order, drawText"
            "        auto tie() const noexcept { return std::tie (font, text, area, justificationType, useEllipsesIfTooBig); }"
            "        // LYNX patch (round 190ms, fix 46): key order, drawText: the cheap fields first, the font last|NL|        auto tie() const noexcept { return std::tie (area, justificationType, useEllipsesIfTooBig, text, font); }")
        lynx_juce_fix("drawFittedText's key, cheap fields first (fix 46)" "LYNX patch (round 190ms, fix 46): key order, drawFittedText"
            "        auto tie() const noexcept { return std::tie (font, text, area, justification, maximumNumberOfLines, minimumHorizontalScale); }"
            "        // LYNX patch (round 190ms, fix 46): key order, drawFittedText: the cheap fields first, the font last|NL|        auto tie() const noexcept { return std::tie (area, justification, maximumNumberOfLines, minimumHorizontalScale, text, font); }")
    elseif(file MATCHES "juce_SimpleShapedText.cpp$")
        lynx_juce_fix("no walk back over no glyphs" "LYNX patch (issue #142): no glyphs"
            "                        auto it = glyphs.cend();|NL||NL|                        while (--it != glyphs.begin())"
            "                        auto it = glyphs.cend();|NL||NL|                        // LYNX patch (issue #142): no glyphs (a font without a typeface), nothing to walk back over|NL|                        if (glyphs.empty())|NL|                            return it;|NL||NL|                        while (--it != glyphs.begin())")
        lynx_juce_fix("display language read once" "LYNX patch (round 190gr): the display language"
            "    String language = SystemStats::getDisplayLanguage();"
            "    // LYNX patch (round 190gr): the display language, read once (it was the OS call at every shaped text)|NL|    String language = [] { static const String once = SystemStats::getDisplayLanguage(); return once; }();")
    elseif(file MATCHES "juce_Messaging_windows.cpp$")
        lynx_juce_fix("dispatch hook" "LYNX patch (issue #32): the message dispatch hook"
            "CheckEventBlockedByModalComps isEventBlockedByModalComps = nullptr;"
            "CheckEventBlockedByModalComps isEventBlockedByModalComps = nullptr;|NL||NL|// LYNX patch (issue #32): the message dispatch hook (LYNX's crash guard for plugin windows; MSG* as const void*)|NL|void (*lynxDispatchMessageHook) (const void*) = nullptr;")
        lynx_juce_fix("dispatch through the hook" "LYNX patch (issue #32): through the hook"
            "                TranslateMessage (&m);|NL|                DispatchMessage (&m);"
            "                TranslateMessage (&m);|NL||NL|                // LYNX patch (issue #32): through the hook when LYNX set one|NL|                if (lynxDispatchMessageHook != nullptr)|NL|                    lynxDispatchMessageHook (&m);|NL|                else|NL|                    DispatchMessage (&m);")
    elseif(file MATCHES "juce_ASIO_windows.cpp$")
        lynx_juce_fix("the ASIO thread" "LYNX patch (round 190go): the ASIO thread"
            "static void sendASIODeviceChangeToListeners (ASIOAudioIODeviceType*);|NL||NL|//==============================================================================|NL|class ASIOAudioIODevice final : public AudioIODevice,"
            "static void sendASIODeviceChangeToListeners (ASIOAudioIODeviceType*);|NL||NL|// LYNX patch (round 190go): the ASIO thread. Every call into an ASIO driver is made on one thread of its own, the one|NL|// that created the driver (an apartment-threaded COM object stays in its creator's apartment), with a message loop for|NL|// the driver's windows and timers. LYNX opens its device on a worker while its main window is built; creating the|NL|// device, open, close, the control panel and a driver's reset request all run here while the caller waits (answering|NL|// messages sent to its own windows meanwhile; every message, as a modal loop does, for the control panel).|NL|// LYNX_ASIO_THREAD=0 in the environment: JUCE's way (the caller's thread).|NL|struct LynxAsioThread|NL|{|NL|    static LynxAsioThread* get()|NL|    {|NL|        static LynxAsioThread* instance = []() -> LynxAsioThread*|NL|        {|NL|            wchar_t value[8] = {};|NL|            if (GetEnvironmentVariableW (L\"LYNX_ASIO_THREAD\", value, 8) > 0 && value[0] == L'0')|NL|                return nullptr;|NL|            auto* t = new LynxAsioThread(); // (kept for the whole process)|NL|            return t->window != nullptr ? t : nullptr;|NL|        }();|NL|        return instance;|NL|    }|NL||NL|    void call (const std::function<void()>& fn, bool pumpAll)|NL|    {|NL|        if (GetCurrentThreadId() == threadId)|NL|        {|NL|            fn();|NL|            return;|NL|        }|NL||NL|        Job job { &fn, CreateEventW (nullptr, TRUE, FALSE, nullptr) };|NL||NL|        if (job.done == nullptr || ! PostMessageW (window, WM_APP + 190, 0, (LPARAM) &job))|NL|        {|NL|            if (job.done != nullptr)|NL|                CloseHandle (job.done);|NL||NL|            fn(); // (the thread is not there: as JUCE does)|NL|            return;|NL|        }|NL||NL|        for (;;)|NL|        {|NL|            const auto result = MsgWaitForMultipleObjectsEx (1, &job.done, INFINITE, pumpAll ? QS_ALLINPUT : QS_SENDMESSAGE, MWMO_INPUTAVAILABLE);|NL||NL|            if (result == WAIT_OBJECT_0)|NL|                break;|NL||NL|            MSG m;|NL||NL|            if (pumpAll)|NL|            {|NL|                while (PeekMessageW (&m, nullptr, 0, 0, PM_REMOVE))|NL|                {|NL|                    TranslateMessage (&m);|NL|                    DispatchMessageW (&m);|NL|                }|NL|            }|NL|            else|NL|            {|NL|                PeekMessageW (&m, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);|NL|            }|NL|        }|NL||NL|        CloseHandle (job.done);|NL|    }|NL||NL|private:|NL|    struct Job|NL|    {|NL|        const std::function<void()>* fn;|NL|        HANDLE done;|NL|    };|NL||NL|    LynxAsioThread()|NL|    {|NL|        auto ready = CreateEventW (nullptr, TRUE, FALSE, nullptr);|NL||NL|        std::thread ([this, ready]|NL|        {|NL|            ::CoInitialize (nullptr);|NL|            WNDCLASSW wc {};|NL|            wc.lpfnWndProc = &LynxAsioThread::windowProc;|NL|            wc.hInstance = GetModuleHandleW (nullptr);|NL|            wc.lpszClassName = L\"LynxAsioThread\";|NL|            RegisterClassW (&wc);|NL|            window = CreateWindowExW (0, wc.lpszClassName, L\"\", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);|NL|            threadId = GetCurrentThreadId();|NL|            SetEvent (ready);|NL||NL|            MSG m;|NL||NL|            while (GetMessageW (&m, nullptr, 0, 0) > 0)|NL|            {|NL|                TranslateMessage (&m);|NL|                DispatchMessageW (&m);|NL|            }|NL|        }).detach();|NL||NL|        WaitForSingleObject (ready, INFINITE);|NL|        CloseHandle (ready);|NL|    }|NL||NL|    static LRESULT CALLBACK windowProc (HWND h, UINT message, WPARAM w, LPARAM l)|NL|    {|NL|        if (message == WM_APP + 190)|NL|        {|NL|            auto* job = reinterpret_cast<Job*> (l);|NL|            (*job->fn)();|NL|            SetEvent (job->done);|NL|            return 0;|NL|        }|NL||NL|        return DefWindowProcW (h, message, w, l);|NL|    }|NL||NL|    HWND window = nullptr;|NL|    DWORD threadId = 0;|NL|};|NL||NL|template <typename Fn>|NL|static void lynxAsioCall (Fn&& fn, bool pumpAll = false)|NL|{|NL|    if (auto* t = LynxAsioThread::get())|NL|        t->call (std::function<void()> (std::forward<Fn> (fn)), pumpAll);|NL|    else|NL|        fn();|NL|}|NL||NL|//==============================================================================|NL|class ASIOAudioIODevice final : public AudioIODevice,")
        lynx_juce_fix("ASIO driver made on the ASIO thread" "LYNX patch (round 190go): the driver is made"
            "        jassert (currentASIODev[slotNumber] == nullptr);|NL|        currentASIODev[slotNumber] = this;|NL||NL|        openDevice();|NL|    }"
            "        jassert (currentASIODev[slotNumber] == nullptr);|NL|        currentASIODev[slotNumber] = this;|NL||NL|        // LYNX patch (round 190go): the driver is made (and opened) on the ASIO thread|NL|        lynxAsioCall ([this] { openDevice(); });|NL|    }")
        lynx_juce_fix("ASIO driver released on the ASIO thread" "LYNX patch (round 190go): released on the ASIO thread"
            "        close();|NL|        JUCE_ASIO_LOG (\"closed\");|NL||NL|        if (! removeCurrentDriver())"
            "        close();|NL|        JUCE_ASIO_LOG (\"closed\");|NL||NL|        // LYNX patch (round 190go): released on the ASIO thread|NL|        bool lynxRemoved = true;|NL|        lynxAsioCall ([this, &lynxRemoved] { lynxRemoved = removeCurrentDriver(); });|NL||NL|        if (! lynxRemoved)")
        lynx_juce_fix("ASIO open on the ASIO thread" "LYNX patch (round 190go): opened on the ASIO thread"
            "    String open (const BigInteger& inputChannels,|NL|                 const BigInteger& outputChannels,|NL|                 double sr, int bufferSizeSamples) override|NL|    {|NL|        if (isOpen())"
            "    String open (const BigInteger& inputChannels,|NL|                 const BigInteger& outputChannels,|NL|                 double sr, int bufferSizeSamples) override|NL|    {|NL|        // LYNX patch (round 190go): opened on the ASIO thread|NL|        String lynxResult;|NL|        lynxAsioCall ([&] { lynxResult = lynxOpen (inputChannels, outputChannels, sr, bufferSizeSamples); });|NL|        return lynxResult;|NL|    }|NL||NL|    String lynxOpen (const BigInteger& inputChannels,|NL|                     const BigInteger& outputChannels,|NL|                     double sr, int bufferSizeSamples)|NL|    {|NL|        if (isOpen())")
        lynx_juce_fix("ASIO close on the ASIO thread" "LYNX patch (round 190go): the callback stops"
            "    void close() override|NL|    {|NL|        error.clear();|NL|        stopTimer();|NL|        stop();|NL|"
            "    void close() override|NL|    {|NL|        // LYNX patch (round 190go): the callback stops on the caller's thread, the driver closes on the ASIO thread|NL|        stopTimer();|NL|        stop();|NL|        lynxAsioCall ([this] { lynxClose(); });|NL|    }|NL||NL|    void lynxClose()|NL|    {|NL|        error.clear();|NL|        stop();|NL|")
        lynx_juce_fix("ASIO control panel on the ASIO thread" "LYNX patch (round 190go): the control panel"
            "            asioObject->controlPanel();|NL|"
            "            // LYNX patch (round 190go): the control panel on the ASIO thread (this one answers every message meanwhile)|NL|            lynxAsioCall ([this] { asioObject->controlPanel(); }, true);|NL|")
        lynx_juce_fix("ASIO channel names after a reset on the ASIO thread" "LYNX patch (round 190go): the channel names"
            "                  currentSampleRate, currentBlockSizeSamples);|NL||NL|            reloadChannelNames();|NL|"
            "                  currentSampleRate, currentBlockSizeSamples);|NL||NL|            // LYNX patch (round 190go): the channel names from the ASIO thread|NL|            lynxAsioCall ([this] { reloadChannelNames(); });|NL|")
        lynx_juce_fix("ASIO quick init for the start-up's device" "LYNX patch (round 190hd): no start / stop at init"
            "                        // start and stop because cubase does it..|NL|                        err = asioObject->start();|NL|                        // ignore an error here, as it might start later after setting other stuff up|NL|                        JUCE_ASIO_LOG_ERROR (\"start\", err);|NL||NL|                        Thread::sleep (80);|NL|                        asioObject->stop();|NL|"
            "                        // LYNX patch (round 190hd): no start / stop at init for the device the start-up makes ahead|NL|                        // (fix 31: lynxAsioQuickInit, set by LYNX's prewarm for its one device); every other open as JUCE does|NL|                        if (! lynxAsioQuickInit.exchange (false))|NL|                        {|NL|                        // start and stop because cubase does it..|NL|                        err = asioObject->start();|NL|                        // ignore an error here, as it might start later after setting other stuff up|NL|                        JUCE_ASIO_LOG_ERROR (\"start\", err);|NL||NL|                        Thread::sleep (80);|NL|                        asioObject->stop();|NL|                        }|NL|")
        lynx_juce_fix("ASIO quick init flag" "LYNX patch (round 190hd): the quick init flag"
            "static void sendASIODeviceChangeToListeners (ASIOAudioIODeviceType*);|NL|"
            "static void sendASIODeviceChangeToListeners (ASIOAudioIODeviceType*);|NL||NL|// LYNX patch (round 190hd): the quick init flag (cmake/LynxPatchJuce.cmake, fix 31)|NL|std::atomic<bool> lynxAsioQuickInit { false };|NL|")
    elseif(file MATCHES "juce_WASAPI_windows.cpp$")
        lynx_juce_fix("shared mode keeps the mix format's channels" "LYNX patch (issue #134): shared mode"
            "        if (fallbackNumChannels >= maxNumChannelsToQuery)|NL|            return fallbackNumChannels;|NL|"
            "        if (fallbackNumChannels >= maxNumChannelsToQuery)|NL|            return fallbackNumChannels;|NL||NL|        // LYNX patch (issue #134): shared mode never answers with more channels than its mix format, so the|NL|        // 434+ IsFormatSupported round trips below only cost time (85 s on a Realtek), on the message thread|NL|        if (! isExclusiveMode (deviceMode))|NL|            return fallbackNumChannels;|NL|")
    elseif(file MATCHES "juce_VST3PluginFormat.cpp$")
        lynx_juce_fix("VST3 inactive bus silence: block size" "LYNX patch (issue #155): the block size"
            "        inputBusMap .prepare (createChannelMappings (true));|NL|        outputBusMap.prepare (createChannelMappings (false));|NL||NL|        setStateForAllMidiBuses (true);|NL|"
            "        // LYNX patch (issue #155): the block size, for the silence behind inactive buses|NL|        inputBusMap .prepare (createChannelMappings (true), estimatedSamplesPerBlock);|NL|        outputBusMap.prepare (createChannelMappings (false), estimatedSamplesPerBlock);|NL||NL|        setStateForAllMidiBuses (true);|NL|")
        lynx_juce_fix("VST3 host menu hook" "LYNX patch (round 177b): the host's items in a parameter's menu"
            "class VST3PluginInstance;|NL||NL|struct VST3HostContext final : public Vst::IComponentHandler,"
            "class VST3PluginInstance;|NL||NL|// LYNX patch (round 177b): the host's items in a parameter's menu (set by LYNX; null = none)|NL|void (*lynxVst3ParamMenuHook) (AudioPluginInstance&, int, PopupMenu&) = nullptr;|NL||NL|struct VST3HostContext final : public Vst::IComponentHandler,")
        lynx_juce_fix("VST3 host items in the menu" "LYNX patch (round 177b): the host's own items"
            "        tresult PLUGIN_API popup (UCoord x, UCoord y) override;|NL|"
            "        tresult PLUGIN_API popup (UCoord x, UCoord y) override;|NL||NL|        // LYNX patch (round 177b): the host's own items. They are the first items of the list, with a target of their|NL|        // own, so a plugin that shows this menu (popup) and a plugin that reads the items into its own menu (getItem,|NL|        // then the target's executeMenuItem) both offer them.|NL|        struct LynxHostItems final : public Vst::IContextMenuTarget|NL|        {|NL|            virtual ~LynxHostItems() {}|NL||NL|            JUCE_DECLARE_VST3_COM_REF_METHODS|NL|            JUCE_DECLARE_VST3_COM_QUERY_METHODS|NL||NL|            tresult PLUGIN_API executeMenuItem (Steinberg::int32 tag) override|NL|            {|NL|                const auto index = (int) tag - (int) firstTag;|NL||NL|                // (after the plugin's menu has closed: the action may open a window of the host)|NL|                if (isPositiveAndBelow (index, (int) actions.size()) && actions[(size_t) index] != nullptr)|NL|                    MessageManager::callAsync (actions[(size_t) index]);|NL||NL|                return kResultOk;|NL|            }|NL||NL|            enum { firstTag = 0x4c590001 };|NL|            std::vector<std::function<void()>> actions; // one per item that is not a separator (null: a caption)|NL|            Atomic<int> refCount;|NL|        };|NL||NL|        void lynxAddHostItems (const PopupMenu& menu, LynxHostItems* hostTarget)|NL|        {|NL|            for (PopupMenu::MenuItemIterator it (menu); it.next();)|NL|            {|NL|                const auto& source = it.getItem();|NL|                ItemAndTarget entry;|NL|                zerostruct (entry.item);|NL|                entry.target = addVSTComSmartPtrOwner (static_cast<IContextMenuTarget*> (hostTarget));|NL||NL|                if (source.isSeparator)|NL|                {|NL|                    entry.item.flags = Item::kIsSeparator;|NL|                    items.add (entry);|NL|                    continue;|NL|                }|NL||NL|                // a caption or a submenu's name is a disabled line; a submenu's items follow it (no groups: plugins|NL|                // that copy the items differ in how they read them)|NL|                const auto runs = source.isEnabled && ! source.isSectionHeader && source.subMenu == nullptr && source.action != nullptr;|NL|                toString128 (entry.item.name, source.text);|NL|                entry.item.tag = (Steinberg::int32) ((int) LynxHostItems::firstTag + (int) hostTarget->actions.size());|NL|                entry.item.flags = (runs ? 0 : Item::kIsDisabled) | (source.isTicked ? Item::kIsChecked : 0);|NL|                hostTarget->actions.push_back (runs ? source.action : std::function<void()>());|NL|                items.add (entry);|NL||NL|                if (source.subMenu != nullptr)|NL|                    lynxAddHostItems (*source.subMenu, hostTarget);|NL|            }|NL|        }|NL||NL|        void lynxFill (Vst::ParamID paramId); // the host's items for that parameter (lynxVst3ParamMenuHook)|NL|")
        lynx_juce_fix("VST3 menu for a parameter" "LYNX patch (round 177b): a menu for a parameter"
            "    Vst::IContextMenu* PLUGIN_API createContextMenu (IPlugView*, const Vst::ParamID*) override|NL|    {|NL|        if (plugin == nullptr)|NL|            return nullptr;|NL||NL|        auto* result = new ContextMenu (*plugin);|NL|"
            "    Vst::IContextMenu* PLUGIN_API createContextMenu (IPlugView*, const Vst::ParamID* lynxParam) override|NL|    {|NL|        if (plugin == nullptr)|NL|            return nullptr;|NL||NL|        auto* result = new ContextMenu (*plugin);|NL|        // LYNX patch (round 177b): a menu for a parameter starts with the host's items|NL|        if (lynxParam != nullptr)|NL|            result->lynxFill (*lynxParam);|NL|")
        lynx_juce_fix("VST3 host items filled" "LYNX patch (round 177b): fills the host's items"
            "tresult VST3HostContext::ContextMenu::popup (UCoord x, UCoord y)|NL|{|NL|"
            "// LYNX patch (round 177b): fills the host's items of a parameter's menu, then a separator|NL|void VST3HostContext::ContextMenu::lynxFill (Vst::ParamID paramId)|NL|{|NL|    if (lynxVst3ParamMenuHook == nullptr)|NL|        return;|NL||NL|    if (auto* param = owner.getParameterForID (paramId))|NL|    {|NL|        PopupMenu hostMenu;|NL|        lynxVst3ParamMenuHook (owner, static_cast<AudioProcessorParameter*> (param)->getParameterIndex(), hostMenu);|NL||NL|        if (hostMenu.getNumItems() > 0)|NL|        {|NL|            hostMenu.addSeparator();|NL|            lynxAddHostItems (hostMenu, new LynxHostItems());|NL|        }|NL|    }|NL|}|NL||NL|tresult VST3HostContext::ContextMenu::popup (UCoord x, UCoord y)|NL|{|NL|")
        lynx_juce_fix("VST3 menu item by index" "LYNX patch (round 190an): getItem takes an index"
            "        tresult PLUGIN_API getItem (Steinberg::int32 tag, Item& result, IContextMenuTarget** target) override|NL|        {|NL|"
            "        tresult PLUGIN_API getItem (Steinberg::int32 tag, Item& result, IContextMenuTarget** target) override|NL|        {|NL|            // LYNX patch (round 190an): getItem takes an index (the SDK's IContextMenu::getItem (index, item, target)):|NL|            // a plugin that copies the host's items into its own menu walks 0 .. getItemCount() - 1. JUCE only|NL|            // compared tags, so such a plugin found nothing (or the wrong item).|NL|            if (isPositiveAndBelow ((int) tag, items.size()))|NL|            {|NL|                auto& indexed = items.getReference ((int) tag);|NL|                result = indexed.item;|NL||NL|                if (target != nullptr)|NL|                    *target = indexed.target.get();|NL||NL|                return kResultTrue;|NL|            }|NL||NL|")
        lynx_juce_fix("VST3 MIDI-mapped parameter follows in the editor" "LYNX patch (issue #152): a MIDI CC the plugin maps"
            "        void setValueWithoutUpdatingProcessor (float newValue)|NL|        {"
            "        // LYNX patch (issue #152): a MIDI CC the plugin maps to this parameter (IMidiMapping) already reached the|NL|        // processor as a parameter change; the edit controller (the plugin's own window) and the host's value follow it|NL|        void lynxFollowMidiMapping (float newValue)|NL|        {|NL|            pluginInstance.parameterDispatcher.push (vstParamIndex, newValue);|NL|            setValueWithoutUpdatingProcessor (newValue);|NL|        }|NL||NL|        void setValueWithoutUpdatingProcessor (float newValue)|NL|        {")
        lynx_juce_fix("VST3 MIDI-mapped parameter reaches the editor" "LYNX patch (issue #152): the editor and the host follow"
            "                    queue->append ({ (Steinberg::int32) time, (float) paramValue });|NL|"
            "                    queue->append ({ (Steinberg::int32) time, (float) paramValue });|NL||NL|                // LYNX patch (issue #152): the editor and the host follow the mapped change (the SDK leaves it to the|NL|                // host: the plugin's knob stayed still while its sound changed, e.g. Arturia's MIDI-learned controls)|NL|                if (auto* lynxParam = getParameterForID ((Vst::ParamID) controlID))|NL|                    lynxParam->lynxFollowMidiMapping ((float) paramValue);|NL|")
        lynx_juce_fix("VST3 sample-accurate automation: the VST3 index" "LYNX patch (round 190jg, fix 39): the parameter's VST3 index"
            "        void setValueWithoutUpdatingProcessor (float newValue)|NL|"
            "        // LYNX patch (round 190jg, fix 39): the parameter's VST3 index (timed changes in processAudio)|NL|        Steinberg::int32 lynxVstIndex() const noexcept { return vstParamIndex; }|NL||NL|        void setValueWithoutUpdatingProcessor (float newValue)|NL|")
        lynx_juce_fix("VST3 sample-accurate automation" "LYNX patch (round 190jg, fix 39): the automation's changes inside"
            "        cachedParamValues.ifSet ([&] (Steinberg::int32 index, float value)|NL|        {|NL|            inputParameterChanges->set (cachedParamValues.getParamID (index), value, 0);|NL|        });|NL|"
            "        cachedParamValues.ifSet ([&] (Steinberg::int32 index, float value)|NL|        {|NL|            inputParameterChanges->set (cachedParamValues.getParamID (index), value, 0);|NL|        });|NL||NL|        // LYNX patch (round 190jg, fix 39): the automation's changes inside this block, at their sample offsets (LYNX's|NL|        // engine/TimedParameters.h; indices as getParameters()). The last one is the parameter's value afterwards.|NL|        for (int k = 0; k < lynxTimedParamCount; ++k)|NL|        {|NL|            const auto& list = getParameters();|NL|            const auto index = lynxTimedParamIndices[k];|NL|            if (! isPositiveAndBelow (index, list.size()))|NL|                continue;|NL|            if (auto* p = dynamic_cast<VST3Parameter*> (list[index]))|NL|            {|NL|                const auto value = jlimit (0.0f, 1.0f, lynxTimedParamValues[k]);|NL|                inputParameterChanges->set (cachedParamValues.getParamID (p->lynxVstIndex()), value, jlimit (0, numSamples - 1, lynxTimedParamOffsets[k]));|NL|                cachedParamValues.exchangeWithoutNotifying (p->lynxVstIndex(), value);|NL|                parameterDispatcher.push (p->lynxVstIndex(), value);|NL|            }|NL|        }|NL|")
        lynx_juce_fix("VST3 editor size: Scale editor dimensions (fix 47)" "LYNX patch (round 190oh, fix 47): FL's"
            "        const auto combinedScale = nativeScaleFactor * getDesktopScaleFactor();"
            "        // LYNX patch (round 190oh, fix 47): FL's wrapper \"Scale editor dimensions\" (or a DPI-unaware window): the|NL|        // plugin's reported size is taken in logical pixels (src/platform/NativeEmbed.h, setEditorScaling)|NL|        const auto combinedScale = ((bool) getProperties()[\"lynxScaleEditorDimensions\"] ? 1.0f : nativeScaleFactor) * getDesktopScaleFactor();")
    elseif(file MATCHES "juce_Windowing_windows.cpp$")
        lynx_juce_fix("layered child window keeps its place when repainted" "LYNX patch (round 190cj): a child window's position"
            "        UpdateLayeredWindow (hwnd, nullptr, &pos, &size, sourceHdc, &p, 0, &bf, ULW_ALPHA);"
            "        // LYNX patch (round 190cj): a child window's position is in its parent's client area, not on the screen:|NL|        // given the screen position, each repaint moved a transparent child window by the parent's client offset.|NL|        // No position: it stays where it is.|NL|        const auto lynxIsChild = (GetWindowLongPtr (hwnd, GWL_STYLE) & WS_CHILD) != 0;|NL|        UpdateLayeredWindow (hwnd, nullptr, lynxIsChild ? nullptr : &pos, &size, sourceHdc, &p, 0, &bf, ULW_ALPHA);")
        lynx_juce_fix("layered child window placed in its parent's client area" "LYNX patch (round 190cj): a child window is placed"
            "            if (auto parentHwnd = GetParent (hwnd))|NL|            {|NL|                auto parentRect = convertPhysicalScreenRectangleToLogical"
            "            // LYNX patch (round 190cj): a child window is placed in its parent's client area like any other child|NL|            // (the parent's screen position was added to it); other windows as before|NL|            if (auto parentHwnd = (GetWindowLongPtr (hwnd, GWL_STYLE) & WS_CHILD) != 0 ? HWND {} : GetParent (hwnd))|NL|            {|NL|                auto parentRect = convertPhysicalScreenRectangleToLogical")
        lynx_juce_fix("vertical blanks only while needed: renderer's frame" "LYNX patch (round 190gv): something still to paint"
            "    virtual void onVBlank() = 0;|NL|    virtual void handleShowWindow() = 0;|NL|"
            "    virtual void onVBlank() = 0;|NL|    virtual void handleShowWindow() = 0;|NL||NL|    // LYNX patch (round 190gv): something still to paint (the window keeps its vertical blanks meanwhile)|NL|    virtual bool lynxHasPendingFrame() const { return true; }|NL|")
        lynx_juce_fix("vertical blanks only while needed: the window says" "LYNX patch (round 190gv): vertical blanks only while needed"
            "                              , private ComponentPeer::VBlankListener|NL|                              , private Timer"
            "                              , private ComponentPeer::VBlankListener|NL|                              , public LynxVBlankDemand // LYNX patch (round 190gv): vertical blanks only while needed|NL|                              , private Timer")
        lynx_juce_fix("vertical blanks only while needed: registered" "LYNX patch (round 190gv): says what it needs"
            "        getNativeRealtimeModifiers = getMouseModifiers;|NL|"
            "        getNativeRealtimeModifiers = getMouseModifiers;|NL|        lynxVBlankDemands()[static_cast<VBlankListener*> (this)] = this; // LYNX patch (round 190gv): says what it needs|NL|")
        lynx_juce_fix("vertical blanks only while needed: unregistered" "LYNX patch (round 190gv): nothing more to say"
            "        VBlankDispatcher::getInstance()->removeListener (*this);|NL|"
            "        VBlankDispatcher::getInstance()->removeListener (*this);|NL|        lynxVBlankDemands().erase (static_cast<VBlankListener*> (this)); // LYNX patch (round 190gv): nothing more to say|NL|")
        lynx_juce_fix("vertical blanks only while needed: repaint wakes" "LYNX patch (round 190gv): the vertical blanks while this window"
            "    void repaint (const Rectangle<int>& area) override|NL|    {|NL|        if (renderContext != nullptr)|NL|            renderContext->repaint ((area.toDouble() * getPlatformScaleFactor()).getSmallestIntegerContainer());|NL|    }|NL|"
            "    void repaint (const Rectangle<int>& area) override|NL|    {|NL|        lynxWakeVBlank(); // LYNX patch (round 190gv)|NL||NL|        if (renderContext != nullptr)|NL|            renderContext->repaint ((area.toDouble() * getPlatformScaleFactor()).getSmallestIntegerContainer());|NL|    }|NL||NL|    // LYNX patch (round 190gv): the vertical blanks while this window needs them (cmake/LynxPatchJuce.cmake, fix 32):|NL|    // a VBlankAttachment, or a frame still to paint. A repaint, a paint message, a shown window, a new renderer or a|NL|    // new VBlankAttachment wake them.|NL|    void lynxWakeVBlank()|NL|    {|NL|        if (std::exchange (lynxVBlankAsleep, false))|NL|            VBlankDispatcher::getInstance()->lynxWake (*this);|NL|    }|NL||NL|    bool lynxWantsVBlanks() override|NL|    {|NL|        return lynxHadVBlankListeners || (renderContext != nullptr && renderContext->lynxHasPendingFrame());|NL|    }|NL||NL|    bool lynxHadVBlankListeners = true;|NL|")
        lynx_juce_fix("vertical blanks only while needed: attachments seen" "LYNX patch (round 190gv): set by callVBlankListeners"
            "    void onVBlank (double timestampSec) override|NL|    {|NL|        callVBlankListeners (timestampSec);|NL|"
            "    void onVBlank (double timestampSec) override|NL|    {|NL|        callVBlankListeners (timestampSec);|NL|        lynxHadVBlankListeners = lynxVBlankListenersCalled; // LYNX patch (round 190gv): set by callVBlankListeners|NL|")
        lynx_juce_fix("vertical blanks only while needed: paint message wakes" "LYNX patch (round 190gv): the area Windows asks for"
            "    void handlePaintMessage()|NL|    {|NL|        if (renderContext != nullptr)|NL|            renderContext->handlePaintMessage();|NL|"
            "    void handlePaintMessage()|NL|    {|NL|        lynxWakeVBlank(); // LYNX patch (round 190gv): the area Windows asks for is painted on a vertical blank (Direct2D)|NL||NL|        if (renderContext != nullptr)|NL|            renderContext->handlePaintMessage();|NL|")
        lynx_juce_fix("vertical blanks only while needed: shown window wakes" "LYNX patch (round 190gv): a shown window"
            "                    if (renderContext != nullptr)|NL|                        renderContext->handleShowWindow();|NL|"
            "                    if (renderContext != nullptr)|NL|                        renderContext->handleShowWindow();|NL||NL|                    lynxWakeVBlank(); // LYNX patch (round 190gv): a shown window paints on the next vertical blanks|NL|")
        lynx_juce_fix("vertical blanks only while needed: GDI frame" "LYNX patch (round 190gv): rectangles not handed to Windows yet"
            "    void onVBlank() override {}|NL||NL|    void handleShowWindow() override {}|NL|"
            "    void onVBlank() override {}|NL||NL|    void handleShowWindow() override {}|NL||NL|    // LYNX patch (round 190gv): rectangles not handed to Windows yet (dispatchDeferredRepaints, on a vertical blank)|NL|    bool lynxHasPendingFrame() const override { return ! deferredRepaints.isEmpty(); }|NL|")
        lynx_juce_fix("vertical blanks only while needed: Direct2D declaration" "LYNX patch (round 190gv): the Direct2D window context"
            "class D2DRenderContext : public RenderContext|NL|{"
            "// LYNX patch (round 190gv): the Direct2D window context has a frame still to paint (juce_Direct2DHwndContext_windows.cpp)|NL|bool lynxD2DHwndHasPendingFrame (const Direct2DHwndContext&);|NL||NL|class D2DRenderContext : public RenderContext|NL|{")
        lynx_juce_fix("vertical blanks only while needed: Direct2D frame" "LYNX patch (round 190gv): its context's frame"
            "    void onVBlank() override|NL|    {|NL|        handleDirect2DPaint();|NL|    }|NL|"
            "    void onVBlank() override|NL|    {|NL|        handleDirect2DPaint();|NL|    }|NL||NL|    // LYNX patch (round 190gv): its context's frame still to paint|NL|    bool lynxHasPendingFrame() const override|NL|    {|NL|        return direct2DContext != nullptr && direct2DContext->lynxHasPendingFrame();|NL|    }|NL|")
        lynx_juce_fix("vertical blanks only while needed: wrapped contexts" "LYNX patch (round 190gv): a frame still to paint"
            "        virtual Direct2DMetrics::Ptr getMetrics() const = 0;|NL|    };"
            "        virtual Direct2DMetrics::Ptr getMetrics() const = 0;|NL|        virtual bool lynxHasPendingFrame() const = 0; // LYNX patch (round 190gv): a frame still to paint|NL|    };")
        lynx_juce_fix("vertical blanks only while needed: swap chain context" "LYNX patch (round 190gv): the swap chain's frame"
            "        Direct2DMetrics::Ptr getMetrics() const override|NL|        {|NL|            return ctx.metrics;|NL|        }|NL|"
            "        Direct2DMetrics::Ptr getMetrics() const override|NL|        {|NL|            return ctx.metrics;|NL|        }|NL||NL|        // LYNX patch (round 190gv): the swap chain's frame|NL|        bool lynxHasPendingFrame() const override|NL|        {|NL|            return lynxD2DHwndHasPendingFrame (ctx);|NL|        }|NL|")
        lynx_juce_fix("vertical blanks only while needed: layered context" "LYNX patch (round 190gv): areas not painted yet"
            "        Direct2DMetrics::Ptr getMetrics() const override|NL|        {|NL|            return bitmapRenderer.getMetrics();|NL|        }|NL|"
            "        Direct2DMetrics::Ptr getMetrics() const override|NL|        {|NL|            return bitmapRenderer.getMetrics();|NL|        }|NL||NL|        // LYNX patch (round 190gv): areas not painted yet|NL|        bool lynxHasPendingFrame() const override|NL|        {|NL|            return ! deferredRepaints.isEmpty();|NL|        }|NL|")
        lynx_juce_fix("vertical blanks only while needed: attachment wakes" "LYNX patch (round 190gv): a VBlankAttachment joined this window"
            "void HWNDComponentPeer::setCurrentRenderingEngine (int e)|NL|{"
            "// LYNX patch (round 190gv): a VBlankAttachment joined this window: its vertical blanks start again|NL|// (juce_VBlankAttachment.cpp, patched)|NL|void lynxVBlankListenerAdded (ComponentPeer& peer)|NL|{|NL|    if (auto* hwndPeer = dynamic_cast<HWNDComponentPeer*> (&peer))|NL|        hwndPeer->lynxWakeVBlank();|NL|}|NL||NL|void HWNDComponentPeer::setCurrentRenderingEngine (int e)|NL|{")
        lynx_juce_fix("vertical blanks only while needed: new renderer wakes" "LYNX patch (round 190gv): the new renderer gets"
            "        renderContext = contextDescriptors[e].construct (*this);|NL|"
            "        renderContext = contextDescriptors[e].construct (*this);|NL|        lynxWakeVBlank(); // LYNX patch (round 190gv): the new renderer gets its vertical blanks|NL|")
        lynx_juce_fix("child window frames shown: the top-level presents too" "LYNX patch (round 190gv, fix 34): the compositor shows"
            "    bool lynxHadVBlankListeners = true;|NL|"
            "    bool lynxHadVBlankListeners = true;|NL||NL|    // LYNX patch (round 190gv, fix 34): the compositor shows a native child window's new Direct2D frame only when its|NL|    // top-level window presents as well: LYNX's docked panels (native children of the main window) moved at the main|NL|    // window's own rate (its toolbar's 72 Hz: ~75 screen updates a second on a 144 Hz display, 140 with docking off).|NL|    // After a child's frame, the top-level window paints one pixel under that child (hidden by it) and presents in|NL|    // the same vertical blank. LYNX_CHILD_NUDGE=0: not.|NL|    void lynxFramePresented()|NL|    {|NL|        static const bool nudge = SystemStats::getEnvironmentVariable (\"LYNX_CHILD_NUDGE\", {}) != \"0\";|NL||NL|        if (! nudge || (GetWindowLongPtr (hwnd, GWL_STYLE) & WS_CHILD) == 0)|NL|            return;|NL||NL|        const auto root = GetAncestor (hwnd, GA_ROOT);|NL|        auto* top = getOwnerOfWindow (root);|NL||NL|        if (top == nullptr || top == this || top->renderContext == nullptr)|NL|            return;|NL||NL|        POINT corner { 0, 0 };|NL|        MapWindowPoints (hwnd, root, &corner, 1);|NL|        const auto scale = top->getPlatformScaleFactor();|NL|        top->repaint (Rectangle<int> ((int) (corner.x / scale), (int) (corner.y / scale), 1, 1));|NL|        top->dispatchDeferredRepaints();|NL|        top->renderContext->onVBlank();|NL|    }|NL|")
        lynx_juce_fix("child window frames shown: after a frame" "LYNX patch (round 190gv, fix 34): a child window's frame"
            "        if (auto* ctx = direct2DContext->startFrame ((float) peer.getPlatformScaleFactor()))|NL|        {|NL|            peer.handlePaint (*ctx);|NL|            direct2DContext->endFrame();|NL|        }"
            "        if (auto* ctx = direct2DContext->startFrame ((float) peer.getPlatformScaleFactor()))|NL|        {|NL|            peer.handlePaint (*ctx);|NL|            direct2DContext->endFrame();|NL|            peer.lynxFramePresented(); // LYNX patch (round 190gv, fix 34): a child window's frame is shown at once|NL|        }")
    elseif(file MATCHES "juce_WaitableEvent.cpp$")
        lynx_juce_fix("timed waits without the sub-millisecond spin (fix 44)" "LYNX patch (round 190la, fix 44): libc++ on Windows"
            "            if (! condition.wait_for (lock, std::chrono::duration<double, std::milli> { timeOutMilliseconds },|NL|                                      [this] { return triggered == true; }))|NL|            {|NL|                return false;|NL|            }"
            "           #if JUCE_WINDOWS && defined (_LIBCPP_VERSION)|NL|            // LYNX patch (round 190la, fix 44): libc++ on Windows hands SleepConditionVariableSRW the time left truncated to whole|NL|            // milliseconds, so under 1 ms left it does not sleep and wait_for spins until the deadline: the last part of|NL|            // every timed wait (the JUCE timer thread: ~4 % of a core at rest). Whole milliseconds, rounded up, instead.|NL|            const auto lynxEnd = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration> (std::chrono::duration<double, std::milli> { timeOutMilliseconds });|NL||NL|            while (! triggered)|NL|            {|NL|                const auto lynxLeft = lynxEnd - std::chrono::steady_clock::now();|NL||NL|                if (lynxLeft <= std::chrono::steady_clock::duration::zero())|NL|                    return false;|NL||NL|                const auto lynxMs = std::chrono::ceil<std::chrono::milliseconds> (lynxLeft).count();|NL|                SleepConditionVariableSRW (reinterpret_cast<PCONDITION_VARIABLE> (condition.native_handle()),|NL|                                           reinterpret_cast<PSRWLOCK> (mutex.native_handle()),|NL|                                           (DWORD) jlimit<decltype (lynxMs)> (1, 0x7ffffffe, lynxMs), 0);|NL|            }|NL|           #else|NL|            if (! condition.wait_for (lock, std::chrono::duration<double, std::milli> { timeOutMilliseconds },|NL|                                      [this] { return triggered == true; }))|NL|            {|NL|                return false;|NL|            }|NL|           #endif")
    elseif(file MATCHES "juce_DirectX_windows.h$")
        lynx_juce_fix("no driver threads for the Direct2D device (fix 43)" "LYNX patch (round 190la, fix 43): no driver threads"
            "        const auto creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;"
            "        // LYNX patch (round 190la, fix 43): LYNX_D3D_DRIVER_THREADS=0: no driver threads for these frames (A/B; default as before)|NL|        const auto creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT|NL|                                   | (SystemStats::getEnvironmentVariable (\"LYNX_D3D_DRIVER_THREADS\", {}) == \"0\" ? D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS : 0);")
        lynx_juce_fix("no driver threads with 4 CPUs or fewer (fix 48)" "lynxNoDriverThreads() ? D3D11_CREATE_DEVICE"
            "                                   | (SystemStats::getEnvironmentVariable (\"LYNX_D3D_DRIVER_THREADS\", {}) == \"0\" ? D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS : 0);"
            "                                   | (lynxNoDriverThreads() ? D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS : 0);"
        )
        lynx_juce_fix("no driver threads with 4 CPUs or fewer, the rule (fix 48)" "LYNX patch (round 190os, fix 48, on fix 43): the rule"
            "    static Ptr create (ComSmartPtr<ID2D1Factory2> d2dFactory, ComSmartPtr<IDXGIAdapter1> dxgiAdapterIn)|NL|    {"
            "    // LYNX patch (round 190os, fix 48, on fix 43): the rule. The graphics driver's own threads (NVIDIA's \"threaded optimization\")|NL|    // take the frames' commands off the message thread, which then waits for them at the present; with 4 logical CPUs|NL|    // or fewer for LYNX they compete with the audio threads, and a frame waited 100-250 ms behind them while a song|NL|    // overloaded a 2-CPU PC (round 190os). There the message thread does that work itself. LYNX_D3D_DRIVER_THREADS=1:|NL|    // always the driver's threads; =0: never (fix 43's A/B switch). More CPUs: as JUCE does.|NL|    static bool lynxNoDriverThreads()|NL|    {|NL|        const auto choice = SystemStats::getEnvironmentVariable (\"LYNX_D3D_DRIVER_THREADS\", {});|NL||NL|        if (choice == \"0\" || choice == \"1\")|NL|            return choice == \"0\";|NL||NL|        DWORD_PTR process = 0, system = 0;|NL||NL|        if (! GetProcessAffinityMask (GetCurrentProcess(), &process, &system))|NL|            return false;|NL||NL|        int cpus = 0;|NL||NL|        for (auto mask = process; mask != 0; mask &= mask - 1)|NL|            ++cpus;|NL||NL|        return cpus > 0 && cpus <= 4;|NL|    }|NL||NL|    static Ptr create (ComSmartPtr<ID2D1Factory2> d2dFactory, ComSmartPtr<IDXGIAdapter1> dxgiAdapterIn)|NL|    {"
        )
        lynx_juce_fix("no WARP device beside a hardware adapter" "LYNX patch (round 190go): a software adapter"
            "        for (UINT i = 0;; ++i)|NL|        {|NL|            ComSmartPtr<IDXGIAdapter1> dxgiAdapter;|NL||NL|            if (factory->EnumAdapters1 (i, dxgiAdapter.resetAndGetPointerAddress()) == DXGI_ERROR_NOT_FOUND)|NL|                break;|NL||NL|            if (const auto adapter = DxgiAdapter::create (d2dFactory, dxgiAdapter))|NL|            {|NL|                adapterArray.add (adapter);|NL|                listeners.call ([adapter] (DxgiAdapterListener& l) { l.adapterCreated (adapter); });|NL|            }|NL|        }|NL|"
            "        // LYNX patch (round 190go): a software adapter (WARP, the \"Microsoft Basic Render Driver\") gets its devices only|NL|        // when no hardware adapter gave any; before, every adapter got a Direct3D 11 + Direct2D device at start-up|NL|        std::vector<ComSmartPtr<IDXGIAdapter1>> lynxSoftwareAdapters;|NL|        const auto lynxAdd = [this] (ComSmartPtr<IDXGIAdapter1> dxgiAdapter)|NL|        {|NL|            if (const auto adapter = DxgiAdapter::create (d2dFactory, dxgiAdapter))|NL|            {|NL|                adapterArray.add (adapter);|NL|                listeners.call ([adapter] (DxgiAdapterListener& l) { l.adapterCreated (adapter); });|NL|            }|NL|        };|NL||NL|        for (UINT i = 0;; ++i)|NL|        {|NL|            ComSmartPtr<IDXGIAdapter1> dxgiAdapter;|NL||NL|            if (factory->EnumAdapters1 (i, dxgiAdapter.resetAndGetPointerAddress()) == DXGI_ERROR_NOT_FOUND)|NL|                break;|NL||NL|            DXGI_ADAPTER_DESC1 lynxDesc {};|NL||NL|            if (dxgiAdapter != nullptr && SUCCEEDED (dxgiAdapter->GetDesc1 (&lynxDesc))|NL|                && ((lynxDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || (lynxDesc.VendorId == 0x1414 && lynxDesc.DeviceId == 0x8c)))|NL|            {|NL|                lynxSoftwareAdapters.push_back (dxgiAdapter);|NL|                continue;|NL|            }|NL||NL|            lynxAdd (dxgiAdapter);|NL|        }|NL||NL|        if (adapterArray.isEmpty())|NL|            for (auto& dxgiAdapter : lynxSoftwareAdapters)|NL|                lynxAdd (dxgiAdapter);|NL|")
    elseif(file MATCHES "juce_gui_basics.cpp$")
        lynx_juce_fix("the DirectX adapters from the patched header" "LYNX patch (round 190go): the DirectX adapters"
            " #include <juce_graphics/native/juce_DirectX_windows.h>|NL|"
            " // LYNX patch (round 190go): the DirectX adapters as juce_graphics compiles them (fix 28; same inline code). A|NL| // copy of this module alone (LYNX-bridge32's x86 copies) has no juce_graphics beside it: the include path's then.|NL| #if __has_include (\"../juce_graphics/native/juce_DirectX_windows.h\")|NL|  #include \"../juce_graphics/native/juce_DirectX_windows.h\"|NL| #else|NL|  #include <juce_graphics/native/juce_DirectX_windows.h>|NL| #endif|NL|")
    elseif(file MATCHES "juce_FileChooser_windows.cpp$")
        lynx_juce_fix("file dialog keys only into LYNX" "LYNX patch (round 180a): only into LYNX"
            "                    SendInput ((UINT) std::size (inputs), inputs, sizeof (INPUT));"
            "                    // LYNX patch (round 180a): only into LYNX, never into a program the user moved on to|NL|                    DWORD foregroundProcess = 0;|NL|                    GetWindowThreadProcessId (GetForegroundWindow(), &foregroundProcess);|NL||NL|                    if (foregroundProcess == GetCurrentProcessId())|NL|                        SendInput ((UINT) std::size (inputs), inputs, sizeof (INPUT));|NL|")
    elseif(file MATCHES "juce_VST3Common.h$")
        lynx_juce_fix("VST3 note ids" "LYNX patch (issue #70): each note its own noteId"
            "        toEventList (result, midiBuffer, &mapping, callback);|NL|    }"
            "        toEventList (result, midiBuffer, &mapping, callback);|NL||NL|        // LYNX patch (issue #70): each note its own noteId (as FL), its note off the same one|NL|        if (auto* list = dynamic_cast<MidiEventList*> (&result))|NL|            list->lynxNumberNotes();|NL|    }|NL||NL|    // A plugin that keys its voices by noteId took every note (all -1) as one note. Each note on gets the next id;|NL|    // a note off (or poly pressure) the id of the oldest sounding note on its channel and key, else -1.|NL|    void lynxNumberNotes() noexcept|NL|    {|NL|        const ScopedLock sl (events.getLock());|NL|        for (auto& e : events)|NL|        {|NL|            if (e.type == Steinberg::Vst::Event::kNoteOnEvent)|NL|            {|NL|                auto& key = lynxKeys[jlimit (0, 15, (int) e.noteOn.channel)][jlimit (0, 127, (int) e.noteOn.pitch)];|NL|                const auto id = lynxNextId;|NL|                lynxNextId = lynxNextId >= 0x7fffffff ? 0 : lynxNextId + 1;|NL|                if (key.count == (int) key.ids.size()) // (notes that never ended: the oldest is forgotten)|NL|                {|NL|                    std::rotate (key.ids.begin(), key.ids.begin() + 1, key.ids.end());|NL|                    --key.count;|NL|                }|NL|                key.ids[(size_t) key.count++] = id;|NL|                e.noteOn.noteId = id;|NL|            }|NL|            else if (e.type == Steinberg::Vst::Event::kNoteOffEvent)|NL|            {|NL|                auto& key = lynxKeys[jlimit (0, 15, (int) e.noteOff.channel)][jlimit (0, 127, (int) e.noteOff.pitch)];|NL|                e.noteOff.noteId = -1;|NL|                if (key.count > 0)|NL|                {|NL|                    e.noteOff.noteId = key.ids[0];|NL|                    std::rotate (key.ids.begin(), key.ids.begin() + 1, key.ids.end());|NL|                    --key.count;|NL|                }|NL|            }|NL|            else if (e.type == Steinberg::Vst::Event::kPolyPressureEvent)|NL|            {|NL|                const auto& key = lynxKeys[jlimit (0, 15, (int) e.polyPressure.channel)][jlimit (0, 127, (int) e.polyPressure.pitch)];|NL|                e.polyPressure.noteId = key.count > 0 ? key.ids[0] : -1;|NL|            }|NL|        }|NL|    }|NL||NL|    struct LynxKey|NL|    {|NL|        std::array<Steinberg::int32, 4> ids {};|NL|        int count = 0;|NL|    };|NL|    std::array<std::array<LynxKey, 128>, 16> lynxKeys {};|NL|    Steinberg::int32 lynxNextId = 0;")
        lynx_juce_fix("VST3 note tuning hook" "LYNX patch (issue #63): the note tuning hook"
            "//==============================================================================|NL|class MidiEventList  : public Steinberg::Vst::IEventList"
            "// LYNX patch (issue #63): the note tuning hook. LYNX (engine/Vst3NoteTuning.h, the same two declarations) sets|NL|// the hook and, while a plugin's block runs on this thread, the context: each note on then gets its tuning from it.|NL|inline float (*lynxNoteTuningHook) (void* context, int channel, int key) = nullptr;|NL|inline thread_local void* lynxNoteTuningContext = nullptr;|NL||NL|//==============================================================================|NL|class MidiEventList  : public Steinberg::Vst::IEventList")
        lynx_juce_fix("VST3 inactive bus silence: room" "LYNX patch (issue #155): room for the silence"
            "    void prepare (std::vector<ChannelMapping> arrangements)|NL|    {|NL|        mappings = std::move (arrangements);|NL|"
            "    void prepare (std::vector<ChannelMapping> arrangements, int lynxMaxSamples = 0)|NL|    {|NL|        mappings = std::move (arrangements);|NL|        // LYNX patch (issue #155): room for the silence behind inactive buses (lynxSilence), for blocks up to lynxMaxSamples|NL|        lynxSilentFloat.assign (lynxInactiveChannels() * (size_t) jmax (1, lynxMaxSamples), 0.0f);|NL|")
        lynx_juce_fix("VST3 inactive bus silence: pointers" "LYNX patch (issue #155): an inactive bus's channels"
            "        int channelIndexOffset = 0;|NL||NL|        for (size_t i = 0; i < mappings.size(); ++i)|NL|        {|NL|            const auto& mapping = mappings[i];|NL|            associateBufferTo (busBuffers[i], get (detail::Tag<FloatType>{})[i], source, mapping, channelIndexOffset);|NL|"
            "        int channelIndexOffset = 0;|NL|        const auto lynxStride = (size_t) jmax (1, source.getNumSamples());|NL|        auto* lynxNextSilent = lynxSilence (detail::Tag<FloatType>{}, lynxStride);|NL||NL|        for (size_t i = 0; i < mappings.size(); ++i)|NL|        {|NL|            const auto& mapping = mappings[i];|NL|            associateBufferTo (busBuffers[i], get (detail::Tag<FloatType>{})[i], source, mapping, channelIndexOffset);|NL||NL|            // LYNX patch (issue #155): an inactive bus's channels point at silence, not at null. The SDK allows null|NL|            // there, but plugins that ignore activateBus (every nih-plug VST3: Spectral Compressor's sidechain) copy|NL|            // from those pointers: SIGSEGV in memcpy on the audio thread.|NL|            if (! mapping.isActive())|NL|            {|NL|                for (auto& channel : get (detail::Tag<FloatType>{})[i])|NL|                {|NL|                    channel = lynxNextSilent;|NL|                    lynxNextSilent += lynxStride;|NL|                }|NL|            }|NL|")
        lynx_juce_fix("VST3 inactive bus silence: storage" "LYNX patch (issue #155): the silence behind inactive buses"
            "    auto& get (detail::Tag<float>)    { return floatBusMap; }|NL|"
            "    auto& get (detail::Tag<float>)    { return floatBusMap; }|NL||NL|    // LYNX patch (issue #155): the silence behind inactive buses: `stride` zeroed samples per inactive channel,|NL|    // cleared again for every block (a plugin may write into it). prepare() sizes it; a longer block grows it here.|NL|    size_t lynxInactiveChannels() const|NL|    {|NL|        size_t n = 0;|NL|        for (const auto& m : mappings)|NL|            n += m.isActive() ? 0 : m.size();|NL|        return n;|NL|    }|NL||NL|    template <typename FloatType>|NL|    FloatType* lynxSilence (detail::Tag<FloatType> tag, size_t stride)|NL|    {|NL|        auto& silence = lynxSilenceStore (tag);|NL|        const auto needed = lynxInactiveChannels() * stride;|NL|        if (silence.size() < needed)|NL|            silence.resize (needed);|NL|        std::fill (silence.begin(), silence.begin() + (std::ptrdiff_t) needed, FloatType {});|NL|        return silence.data();|NL|    }|NL||NL|    auto& lynxSilenceStore (detail::Tag<float>)  { return lynxSilentFloat; }|NL|    auto& lynxSilenceStore (detail::Tag<double>) { return lynxSilentDouble; }|NL||NL|    std::vector<float>  lynxSilentFloat;|NL|    std::vector<double> lynxSilentDouble;|NL||NL|")
        lynx_juce_fix("VST3 note tuning" "LYNX patch (issue #63): the note's tuning"
            "                e.noteOn.noteId = id;|NL|"
            "                e.noteOn.noteId = id;|NL|                // LYNX patch (issue #63): the note's tuning (FL's wrapper sends a note's fine pitch there)|NL|                if (lynxNoteTuningHook != nullptr && lynxNoteTuningContext != nullptr)|NL|                    e.noteOn.tuning = lynxNoteTuningHook (lynxNoteTuningContext, (int) e.noteOn.channel, (int) e.noteOn.pitch);|NL|")
        lynx_juce_fix("VST3 note tags: hook" "LYNX patch (round 190fg): the note tag hook"
            "inline thread_local void* lynxNoteTuningContext = nullptr;"
            "inline thread_local void* lynxNoteTuningContext = nullptr;|NL|// LYNX patch (round 190fg): the note tag hook. LYNX (engine/Vst3NoteTuning.h, the same declaration) answers, while the|NL|// context above is set, which of its notes a converted note on / note off belongs to (0: none).|NL|inline unsigned int (*lynxNoteTagHook) (void* context, bool noteOn, int channel, int key) = nullptr;")
        lynx_juce_fix("VST3 note tags: note on" "LYNX patch (round 190fg): the LYNX note this note on starts"
            "                if (key.count == (int) key.ids.size()) // (notes that never ended: the oldest is forgotten)|NL|                {|NL|                    std::rotate (key.ids.begin(), key.ids.begin() + 1, key.ids.end());|NL|                    --key.count;|NL|                }|NL|                key.ids[(size_t) key.count++] = id;|NL|                e.noteOn.noteId = id;"
            "                if (key.count == (int) key.ids.size()) // (notes that never ended: the oldest is forgotten)|NL|                {|NL|                    std::rotate (key.ids.begin(), key.ids.begin() + 1, key.ids.end());|NL|                    std::rotate (key.tags.begin(), key.tags.begin() + 1, key.tags.end());|NL|                    --key.count;|NL|                }|NL|                // LYNX patch (round 190fg): the LYNX note this note on starts, kept beside its id|NL|                key.tags[(size_t) key.count] = lynxNoteTag (true, (int) e.noteOn.channel, (int) e.noteOn.pitch);|NL|                key.ids[(size_t) key.count++] = id;|NL|                e.noteOn.noteId = id;")
        lynx_juce_fix("VST3 note tags: note off" "LYNX patch (round 190fg): a note off LYNX tagged"
            "                e.noteOff.noteId = -1;|NL|                if (key.count > 0)|NL|                {|NL|                    e.noteOff.noteId = key.ids[0];|NL|                    std::rotate (key.ids.begin(), key.ids.begin() + 1, key.ids.end());|NL|                    --key.count;|NL|                }"
            "                e.noteOff.noteId = -1;|NL|                // LYNX patch (round 190fg): a note off LYNX tagged ends the note on with the same tag; else the oldest|NL|                const auto tag = lynxNoteTag (false, (int) e.noteOff.channel, (int) e.noteOff.pitch);|NL|                int at = 0;|NL|                for (int i = 0; tag != 0 && i < key.count; ++i)|NL|                    if (key.tags[(size_t) i] == tag)|NL|                    {|NL|                        at = i;|NL|                        break;|NL|                    }|NL|                if (key.count > 0)|NL|                {|NL|                    e.noteOff.noteId = key.ids[(size_t) at];|NL|                    std::rotate (key.ids.begin() + at, key.ids.begin() + at + 1, key.ids.end());|NL|                    std::rotate (key.tags.begin() + at, key.tags.begin() + at + 1, key.tags.end());|NL|                    --key.count;|NL|                }")
        lynx_juce_fix("VST3 note tags: storage" "LYNX patch (round 190fg): the LYNX note each id belongs to"
            "    struct LynxKey|NL|    {|NL|        std::array<Steinberg::int32, 4> ids {};|NL|        int count = 0;|NL|    };"
            "    struct LynxKey|NL|    {|NL|        std::array<Steinberg::int32, 4> ids {};|NL|        std::array<unsigned int, 4> tags {}; // LYNX patch (round 190fg): the LYNX note each id belongs to (0: none)|NL|        int count = 0;|NL|    };|NL||NL|    // LYNX patch (round 190fg): which LYNX note a converted note on / off belongs to (0 when LYNX has not set the hook)|NL|    static unsigned int lynxNoteTag (bool noteOn, int channel, int key) noexcept|NL|    {|NL|        return lynxNoteTagHook != nullptr && lynxNoteTuningContext != nullptr ? lynxNoteTagHook (lynxNoteTuningContext, noteOn, channel, key) : 0u;|NL|    }")
        lynx_juce_fix("VST3 sample-accurate automation: declarations" "LYNX patch (round 190jg, fix 39): timed parameter changes"
            "inline unsigned int (*lynxNoteTagHook) (void* context, bool noteOn, int channel, int key) = nullptr;"
            "inline unsigned int (*lynxNoteTagHook) (void* context, bool noteOn, int channel, int key) = nullptr;|NL|// LYNX patch (round 190jg, fix 39): timed parameter changes for the block running on this thread (LYNX's|NL|// engine/TimedParameters.h has the same four declarations: keep them identical). juce_VST3PluginFormat.cpp adds them to|NL|// the block's parameter queues at their sample offsets.|NL|inline thread_local int lynxTimedParamCount = 0;|NL|inline thread_local const int* lynxTimedParamIndices = nullptr;|NL|inline thread_local const int* lynxTimedParamOffsets = nullptr;|NL|inline thread_local const float* lynxTimedParamValues = nullptr;")
    elseif(file MATCHES "juce_VBlank_windows.cpp$")
        lynx_juce_fix("vertical blanks only while needed: demands" "LYNX patch (round 190gv): a window listens to the vertical blanks only while"
            "namespace juce|NL|{|NL||NL|class VBlankThread : private Thread,"
            "namespace juce|NL|{|NL||NL|// LYNX patch (round 190gv): a window listens to the vertical blanks only while it needs them (cmake/LynxPatchJuce.cmake,|NL|// fix 32). A listener that says what it needs (the window peers) is asked after each of its vertical blanks; when no|NL|// listener of a display needs the next one, that display's thread sleeps (no wait for the vertical blank, no message|NL|// posted) until one of them wakes it again (lynxWake: a repaint, a VBlankAttachment, a listener that joins).|NL|// LYNX_VBLANK_ALWAYS=1: every vertical blank, as before (A/B measurements).|NL|struct LynxVBlankDemand|NL|{|NL|    virtual ~LynxVBlankDemand() = default;|NL|    virtual bool lynxWantsVBlanks() = 0; // (message thread, after its onVBlank)|NL|    bool lynxVBlankAsleep = false;       // its display's thread sleeps (message thread)|NL|};|NL||NL|static std::map<const ComponentPeer::VBlankListener*, LynxVBlankDemand*>& lynxVBlankDemands()|NL|{|NL|    static std::map<const ComponentPeer::VBlankListener*, LynxVBlankDemand*> demands; // (message thread)|NL|    return demands;|NL|}|NL||NL|static bool lynxVBlankAlways()|NL|{|NL|    static const bool always = SystemStats::getEnvironmentVariable (\"LYNX_VBLANK_ALWAYS\", {}).isNotEmpty();|NL|    return always;|NL|}|NL||NL|extern bool lynxVBlankListenersCalled; // juce_ComponentPeer.cpp (patched): the last callVBlankListeners had a listener|NL|extern int lynxD2DFramesStarted, lynxD2DNotReady, lynxD2DPresents; // juce_Direct2DHwndContext_windows.cpp (patched)|NL||NL|class VBlankThread : private Thread,")
        lynx_juce_fix("vertical blanks only while needed: a listener joins" "LYNX patch (round 190gv): a listener that joins"
            "    void addListener (VBlankListener& listener)|NL|    {|NL|        listeners.push_back (listener);|NL|    }|NL|"
            "    void addListener (VBlankListener& listener)|NL|    {|NL|        listeners.push_back (listener);|NL|        lynxWake(); // LYNX patch (round 190gv): a listener that joins gets the next vertical blank|NL|    }|NL||NL|    // LYNX patch (round 190gv): back from sleep, the next vertical blank is posted again|NL|    void lynxWake()|NL|    {|NL|        {|NL|            const std::scoped_lock lock { mutex };|NL||NL|            if (! lynxIdle)|NL|                return;|NL||NL|            lynxIdle = false;|NL|        }|NL||NL|        condvar.notify_one();|NL|    }|NL|")
        lynx_juce_fix("vertical blanks only while needed: asleep" "LYNX patch (round 190gv): asleep while no listener"
            "        for (;;)|NL|        {|NL|            if (output->WaitForVBlank() == S_OK)|NL|            {"
            "        for (;;)|NL|        {|NL|            // LYNX patch (round 190gv): asleep while no listener needs a vertical blank|NL|            {|NL|                std::unique_lock lock { mutex };|NL|                condvar.wait (lock, [this] { return ! lynxIdle || threadState == ThreadState::exit; });|NL||NL|                if (threadState == ThreadState::exit)|NL|                    return;|NL|            }|NL||NL|            if (output->WaitForVBlank() == S_OK)|NL|            {")
        lynx_juce_fix("vertical blanks only while needed: not posted" "LYNX patch (round 190gv): fell asleep meanwhile"
            "                if (threadState == ThreadState::exit)|NL|                    return;|NL||NL|                threadState = ThreadState::sleep;|NL|                triggerAsyncUpdate();"
            "                if (threadState == ThreadState::exit)|NL|                    return;|NL||NL|                if (lynxIdle) // LYNX patch (round 190gv): fell asleep meanwhile (the wait at the top of the loop)|NL|                    continue;|NL||NL|                threadState = ThreadState::sleep;|NL|                triggerAsyncUpdate();")
        lynx_juce_fix("vertical blanks only while needed: asked" "LYNX patch (round 190gv): does any listener need the next one"
            "        for (auto& listener : listeners)|NL|            listener.get().onVBlank (timestampSec);|NL||NL|        {|NL|            const std::scoped_lock lock { mutex };|NL||NL|            if (threadState == ThreadState::sleep)|NL|                threadState = ThreadState::paint;|NL|        }"
            "        for (auto& listener : listeners)|NL|            listener.get().onVBlank (timestampSec);|NL||NL|        // LYNX patch (round 190gv): does any listener need the next one? (one that says nothing always does)|NL|        auto& lynxDemands = lynxVBlankDemands();|NL|        const auto lynxWanted = lynxVBlankAlways()|NL|                             || std::any_of (listeners.cbegin(), listeners.cend(), [&lynxDemands] (const auto& l)|NL|                                {|NL|                                    const auto found = lynxDemands.find (&l.get());|NL|                                    return found == lynxDemands.end() || found->second->lynxWantsVBlanks();|NL|                                });|NL||NL|        if (! lynxWanted)|NL|            for (auto& l : listeners)|NL|                if (const auto found = lynxDemands.find (&l.get()); found != lynxDemands.end())|NL|                    found->second->lynxVBlankAsleep = true;|NL||NL|        {|NL|            const std::scoped_lock lock { mutex };|NL||NL|            if (threadState == ThreadState::sleep)|NL|                threadState = ThreadState::paint;|NL||NL|            if (! lynxWanted)|NL|                lynxIdle = true;|NL|        }")
        lynx_juce_fix("vertical blanks only while needed: state" "LYNX patch (round 190gv): no listener needs"
            "    ThreadState threadState = ThreadState::paint;|NL|    std::condition_variable condvar;"
            "    ThreadState threadState = ThreadState::paint;|NL|    bool lynxIdle = false; // LYNX patch (round 190gv): no listener needs the next vertical blank (under mutex)|NL|    std::condition_variable condvar;")
        lynx_juce_fix("vertical blanks only while needed: wake a listener's display" "LYNX patch (round 190gv): wakes the thread of the display"
            "    void removeListener (const ComponentPeer::VBlankListener& listener)|NL|    {|NL|        for (auto it = threads.begin(); it != threads.end(); ++it)"
            "    // LYNX patch (round 190gv): wakes the thread of the display the listener is on|NL|    void lynxWake (const ComponentPeer::VBlankListener& listener)|NL|    {|NL|        for (auto& thread : threads)|NL|        {|NL|            if (thread->hasListener (listener))|NL|            {|NL|                thread->lynxWake();|NL|                return;|NL|            }|NL|        }|NL|    }|NL||NL|    void removeListener (const ComponentPeer::VBlankListener& listener)|NL|    {|NL|        for (auto it = threads.begin(); it != threads.end(); ++it)")
        lynx_juce_fix("vertical blanks only while needed: statistics" "LYNX patch (round 190gv): LYNX_D2D_STATS=<file>"
            "    void handleAsyncUpdate() override|NL|    {|NL|        const auto timestampSec = lastVBlankEvent / 1000.0;|NL|"
            "    void handleAsyncUpdate() override|NL|    {|NL|        const auto timestampSec = lastVBlankEvent / 1000.0;|NL|        lynxWriteStats(); // LYNX patch (round 190gv): LYNX_D2D_STATS=<file>|NL|")
        lynx_juce_fix("vertical blanks only while needed: statistics writer" "LYNX patch (round 190gv): diagnostics, every 2 s"
            "    //==============================================================================|NL|    ComSmartPtr<IDXGIOutput> output;|NL|    HMONITOR monitor = nullptr;"
            "    // LYNX patch (round 190gv): diagnostics, every 2 s while vertical blanks are posted: per second, the vertical blanks|NL|    // posted (all displays), the Direct2D window frames painted, not ready, presented (juce_Direct2DHwndContext_windows.cpp)|NL|    static void lynxWriteStats()|NL|    {|NL|        static const auto path = SystemStats::getEnvironmentVariable (\"LYNX_D2D_STATS\", {});|NL|        static int posted = 0;|NL|        static double since = 0.0;|NL|        static int frames0 = 0, notReady0 = 0, presents0 = 0;|NL||NL|        if (path.isEmpty())|NL|            return;|NL||NL|        ++posted;|NL|        const auto now = Time::getMillisecondCounterHiRes();|NL||NL|        if (since <= 0.0)|NL|            since = now;|NL||NL|        if (now - since < 2000.0)|NL|            return;|NL||NL|        const auto seconds = (now - since) / 1000.0;|NL|        File (path).appendText (String (now / 1000.0, 1) + \" vblanks/s \" + String (posted / seconds, 1)|NL|                                + \" frames/s \" + String ((lynxD2DFramesStarted - frames0) / seconds, 1)|NL|                                + \" notready/s \" + String ((lynxD2DNotReady - notReady0) / seconds, 1)|NL|                                + \" presents/s \" + String ((lynxD2DPresents - presents0) / seconds, 1) + newLine);|NL|        posted = 0;|NL|        since = now;|NL|        frames0 = lynxD2DFramesStarted;|NL|        notReady0 = lynxD2DNotReady;|NL|        presents0 = lynxD2DPresents;|NL|    }|NL||NL|    //==============================================================================|NL|    ComSmartPtr<IDXGIOutput> output;|NL|    HMONITOR monitor = nullptr;")
    elseif(file MATCHES "juce_VBlankAttachment.cpp$")
        lynx_juce_fix("vertical blanks only while needed: attachment hook" "LYNX patch (round 190gv): wakes the window's vertical blanks"
            "namespace juce|NL|{|NL||NL|VBlankAttachment::VBlankAttachment (Component* c, std::function<void()> callbackIn)"
            "namespace juce|NL|{|NL||NL|#if JUCE_WINDOWS|NL|// LYNX patch (round 190gv): wakes the window's vertical blanks (juce_Windowing_windows.cpp, patched)|NL|void lynxVBlankListenerAdded (ComponentPeer&);|NL|#endif|NL||NL|VBlankAttachment::VBlankAttachment (Component* c, std::function<void()> callbackIn)")
        lynx_juce_fix("vertical blanks only while needed: attachment joins" "LYNX patch (round 190gv): the window's vertical blanks start"
            "            peer->addVBlankListener (this);|NL|"
            "            peer->addVBlankListener (this);|NL|           #if JUCE_WINDOWS|NL|            lynxVBlankListenerAdded (*peer); // LYNX patch (round 190gv): the window's vertical blanks start again|NL|           #endif|NL|")
    elseif(file MATCHES "juce_Messaging_linux.cpp$")
        lynx_juce_fix("unregistered descriptor callbacks are not called (fix 52)" "LYNX patch (fix 52)"
            "        // CriticalSection should be available during the callback|NL|        for (auto& fn : callbackStorage)|NL|            (*fn)();|NL||NL|        return ! callbackStorage.empty();|NL|    }"
            "        // CriticalSection should be available during the callback|NL|        for (auto& fn : callbackStorage)|NL|        {|NL|            // LYNX patch (fix 52): an earlier callback of this round may have unregistered this one, and its owner may|NL|            // be gone (a VST3 plugin's run loop, deleted with the plugin's last editor): only a callback still|NL|            // registered is called|NL|            if (! lynxStillRegistered (fn))|NL|                continue;|NL||NL|            (*fn)();|NL|        }|NL||NL|        return ! callbackStorage.empty();|NL|    }|NL||NL|    bool lynxStillRegistered (const std::shared_ptr<std::function<void()>>& fn) const|NL|    {|NL|        const ScopedLock sl (lock);|NL||NL|        for (const auto& registered : callbacks)|NL|            if (registered.second == fn)|NL|                return true;|NL||NL|        return false;|NL|    }")
    elseif(file MATCHES "juce_Direct2DHwndContext_windows.cpp$")
        lynx_juce_fix("Direct2D present pacing: declarations" "LYNX patch (round 190gv, fix 33): JUCE 8.0.4 paced"
            "namespace juce|NL|{|NL||NL|struct Direct2DHwndContext::HwndPimpl : public Direct2DGraphicsContext::Pimpl|NL|{"
            "namespace juce|NL|{|NL||NL|// LYNX patch (round 190gv): a window keeps its vertical blanks while its frame is still to paint|NL|// (juce_Windowing_windows.cpp asks; cmake/LynxPatchJuce.cmake, fix 32)|NL|struct LynxPendingFrame|NL|{|NL|    virtual ~LynxPendingFrame() = default;|NL|    virtual bool lynxHasPendingFrame() const = 0;|NL|};|NL||NL|static std::map<const Direct2DHwndContext*, const LynxPendingFrame*>& lynxPendingFrames()|NL|{|NL|    static std::map<const Direct2DHwndContext*, const LynxPendingFrame*> frames; // (message thread)|NL|    return frames;|NL|}|NL||NL|bool lynxD2DHwndHasPendingFrame (const Direct2DHwndContext& context)|NL|{|NL|    const auto found = lynxPendingFrames().find (&context);|NL|    return found == lynxPendingFrames().end() || found->second->lynxHasPendingFrame();|NL|}|NL||NL|// LYNX patch (round 190gv, fix 33): JUCE 8.0.4 paced the presents with a thread per window that posted a message to|NL|// the message thread on each swap chain event; a frame painted before that message arrived was presented only later,|NL|// so a window that repaints every frame got about every other vertical blank. As in JUCE's later versions (backported|NL|// from JUCE's own code), the swap chain's waitable object is polled when a frame is due, and a frame is painted only|NL|// when it can be presented at once. LYNX_D2D_PACING=old: the thread as in 8.0.4 (A/B measurements).|NL|// LYNX patch (round 190gv): counters for LYNX_D2D_STATS=<file> (diagnostics; juce_VBlank_windows.cpp writes them)|NL|int lynxD2DFramesStarted = 0, lynxD2DNotReady = 0, lynxD2DPresents = 0, lynxD2DPresentFails = 0;|NL||NL|static bool lynxOldPacing()|NL|{|NL|    static const bool old = SystemStats::getEnvironmentVariable (\"LYNX_D2D_PACING\", {}) == \"old\";|NL|    return old;|NL|}|NL||NL|struct Direct2DHwndContext::HwndPimpl : public Direct2DGraphicsContext::Pimpl,|NL|                                        private LynxPendingFrame|NL|{")
        lynx_juce_fix("Direct2D present pacing: no swap chain thread" "LYNX patch (round 190gv, fix 33): no thread"
            "        if (swapChainThread == nullptr)|NL|            if (auto* e = swap.getEvent())"
            "        if (swapChainThread == nullptr && lynxOldPacing()) // LYNX patch (round 190gv, fix 33): no thread (unless asked)|NL|            if (auto* e = swap.getEvent())")
        lynx_juce_fix("Direct2D present pacing: teardown" "LYNX patch (round 190gv, fix 33): a new swap chain"
            "        compositionTree.reset();|NL|        swapChainThread = nullptr;|NL|"
            "        compositionTree.reset();|NL|        swapChainThread = nullptr;|NL|        lynxDidWait = false; // LYNX patch (round 190gv, fix 33): a new swap chain, a new waitable object|NL|")
        lynx_juce_fix("Direct2D present pacing: ready when it can present" "LYNX patch (round 190gv, fix 33): painted only when"
            "        ready &= swap.canPaint();|NL|        ready &= compositionTree.has_value();|NL|"
            "        ready &= swap.canPaint();|NL|        ready &= compositionTree.has_value();|NL||NL|        // LYNX patch (round 190gv, fix 33): painted only when it can be presented at once|NL|        if (! lynxOldPacing())|NL|            ready = ready && lynxCanPresent();|NL|")
        lynx_juce_fix("Direct2D present pacing: the waitable object polled" "LYNX patch (round 190gv, fix 33): the swap chain's waitable object"
            "    bool swapEventReceived = false;|NL|"
            "    bool swapEventReceived = false;|NL||NL|    // LYNX patch (round 190gv, fix 33): the swap chain's waitable object was signalled since the last present, so it may|NL|    // present again|NL|    bool lynxDidWait = false;|NL||NL|    // A frame is due on a vertical blank, but the compositor takes the last present a little after that vertical blank|NL|    // and only then signals the object. Polled at once (as JUCE's later versions do), it missed now and then: 139 screen|NL|    // updates a second on a 144 Hz display, 124-137 in the 0.1.28 rollup, whose message thread reaches the vertical|NL|    // blank sooner (round 190hi). So a due frame waits up to 4 ms for it (LYNX_D2D_WAIT_MS=0..16; 0 = polled): 143,|NL|    // as FL. It returns as soon as the object is signalled, and never waits after two empty waits in a row (a window the|NL|    // compositor takes no frames from, e.g. minimised: no waiting on every vertical blank).|NL|    int lynxMissedWaits = 0;|NL||NL|    static DWORD lynxPresentWaitMs()|NL|    {|NL|        static const DWORD ms = [] {|NL|            const auto v = SystemStats::getEnvironmentVariable (\"LYNX_D2D_WAIT_MS\", {});|NL|            return (DWORD) (v.isEmpty() ? 4 : jlimit (0, 16, v.getIntValue()));|NL|        }();|NL|        return ms;|NL|    }|NL||NL|    bool lynxCanPresent()|NL|    {|NL|        if (lynxDidWait)|NL|            return true;|NL||NL|        auto* event = swap.getEvent();|NL||NL|        if (event == nullptr)|NL|            return lynxDidWait = true;|NL||NL|        const auto wait = lynxMissedWaits < 2 ? lynxPresentWaitMs() : (DWORD) 0;|NL|        lynxDidWait = WaitForSingleObjectEx (event->getHandle(), wait, TRUE) == WAIT_OBJECT_0;|NL|        lynxMissedWaits = lynxDidWait ? 0 : lynxMissedWaits + 1;|NL|        return lynxDidWait;|NL|    }|NL||NL|    // LYNX patch (round 190gv): fix 32's question (areas still to paint)|NL|    bool lynxHasPendingFrame() const override|NL|    {|NL|        return ! deferredRepaints.isEmpty();|NL|    }|NL||NL|    const Direct2DHwndContext* lynxOwner = nullptr;|NL|")
        lynx_juce_fix("Direct2D present pacing: registered" "LYNX patch (round 190gv): answers fix 32"
            "        : Pimpl (ownerIn),|NL|          hwnd (hwndIn)|NL|    {|NL|    }|NL||NL|    ~HwndPimpl() override = default;"
            "        : Pimpl (ownerIn),|NL|          hwnd (hwndIn)|NL|    {|NL|        lynxOwner = &ownerIn; // LYNX patch (round 190gv): answers fix 32's question|NL|        lynxPendingFrames()[lynxOwner] = this;|NL|    }|NL||NL|    ~HwndPimpl() override|NL|    {|NL|        lynxPendingFrames().erase (lynxOwner);|NL|    }")
        lynx_juce_fix("Direct2D present pacing: nothing to paint" "LYNX patch (round 190gv, fix 33): nothing to paint"
            "        setSize (getClientRect());|NL||NL|        auto* savedState = Pimpl::startFrame (dpiScale);"
            "        setSize (getClientRect());|NL||NL|        // LYNX patch (round 190gv, fix 33): nothing to paint in the window, no frame (and no device checks on every|NL|        // vertical blank)|NL|        if (! lynxOldPacing())|NL|        {|NL|            deferredRepaints.clipTo (getClientRect());|NL||NL|            if (deferredRepaints.isEmpty())|NL|                return nullptr;|NL|        }|NL||NL|        auto* savedState = Pimpl::startFrame (dpiScale);")
        lynx_juce_fix("Direct2D present pacing: present when painted" "LYNX patch (round 190gv, fix 33): checkPaintReady already knows"
            "        if (swap.getBuffer() == nullptr || dirtyRegionsInBackBuffer.isEmpty() || ! swapEventReceived)|NL|            return;"
            "        // LYNX patch (round 190gv, fix 33): checkPaintReady already knows it may present (old pacing: the event's message)|NL|        if (swap.getBuffer() == nullptr || dirtyRegionsInBackBuffer.isEmpty() || (lynxOldPacing() && ! swapEventReceived))|NL|            return;")
        lynx_juce_fix("Direct2D present pacing: after a present" "LYNX patch (round 190gv, fix 33): painted again on the next"
            "        jassertquiet (SUCCEEDED (hr));|NL||NL|        if (FAILED (hr))|NL|            return;|NL||NL|        // We managed to present a frame, so we should avoid rendering anything or calling|NL|        // present again until that frame has been shown on-screen.|NL|        swapEventReceived = false;|NL|"
            "        jassertquiet (SUCCEEDED (hr));|NL||NL|        if (FAILED (hr))|NL|        {|NL|            // LYNX patch (round 190gv, fix 33): painted again on the next vertical blank (nothing else presents it now)|NL|            if (! lynxOldPacing())|NL|                deferredRepaints.add (dirtyRegionsInBackBuffer);|NL||NL|            return;|NL|        }|NL||NL|        // We managed to present a frame, so we should avoid rendering anything or calling|NL|        // present again until that frame has been shown on-screen.|NL|        swapEventReceived = false;|NL|        lynxDidWait = false; // LYNX patch (round 190gv, fix 33): wait for the waitable object again|NL|")
        lynx_juce_fix("Direct2D present pacing: counted frames" "LYNX patch (round 190gv): counted for LYNX_D2D_STATS (frames)"
            "        if (savedState == nullptr)|NL|            return nullptr;|NL||NL|        // If a new frame is starting"
            "        if (savedState == nullptr)|NL|        {|NL|            ++lynxD2DNotReady; // LYNX patch (round 190gv): counted for LYNX_D2D_STATS (frames)|NL|            return nullptr;|NL|        }|NL||NL|        ++lynxD2DFramesStarted;|NL||NL|        // If a new frame is starting")
        lynx_juce_fix("Direct2D present pacing: counted presents" "LYNX patch (round 190gv): counted for LYNX_D2D_STATS (presents)"
            "        lynxDidWait = false; // LYNX patch (round 190gv, fix 33): wait for the waitable object again|NL|"
            "        lynxDidWait = false; // LYNX patch (round 190gv, fix 33): wait for the waitable object again|NL|        ++lynxD2DPresents;   // LYNX patch (round 190gv): counted for LYNX_D2D_STATS (presents)|NL|")
    else()
        lynx_juce_fix("drop target search after an unused drop" "LYNX patch (issue #11): a drop nobody took"
            "bool ComponentPeer::handleDragDrop (const ComponentPeer::DragInfo& info)|NL|{|NL|    handleDragMove (info);|NL|"
            "bool ComponentPeer::handleDragDrop (const ComponentPeer::DragInfo& info)|NL|{|NL|    handleDragMove (info);|NL|    // LYNX patch (issue #11): a drop nobody took must not leave the component under the mouse remembered|NL|    const ScopeGuard forgetComponentUnderMouse { [this] { lastDragAndDropCompUnderMouse = nullptr; } };|NL|")
        if(WIN32 OR CMAKE_SCRIPT_MODE_FILE) # (Windows only: a Linux or macOS configure leaves the file as it is)
            lynx_juce_fix("vertical blanks only while needed: listeners called" "LYNX patch (round 190gv): whether the last call had a listener"
                "void ComponentPeer::callVBlankListeners (double timestampSec)|NL|{|NL|"
                "#if JUCE_WINDOWS|NL|// LYNX patch (round 190gv): whether the last call had a listener (juce_Windowing_windows.cpp: a window keeps its|NL|// vertical blanks only while it needs them; cmake/LynxPatchJuce.cmake, fix 32)|NL|bool lynxVBlankListenersCalled = false;|NL|#endif|NL||NL|void ComponentPeer::callVBlankListeners (double timestampSec)|NL|{|NL|   #if JUCE_WINDOWS|NL|    lynxVBlankListenersCalled = ! vBlankListeners.isEmpty();|NL|   #endif|NL|")
        endif()
    endif()

    if(changed)
        file(WRITE "${file}" "${text}")
    endif()
endfunction()

# `juce_dir`: JUCE's checkout (or any folder with modules/juce_gui_basics)
function(lynx_patch_juce juce_dir)
    lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/native/juce_DragAndDrop_linux.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/windows/juce_ComponentPeer.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/mouse/juce_DragAndDropContainer.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/menus/juce_PopupMenu.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/widgets/juce_ListBox.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/native/juce_Windowing_linux.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/desktop/juce_Displays.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/native/juce_XWindowSystem_linux.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_graphics/native/juce_CoreGraphicsContext_mac.mm")
    lynx_patch_juce_file("${juce_dir}/modules/juce_graphics/native/juce_Direct2DImage_windows.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_graphics/fonts/juce_SimpleShapedText.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_graphics/native/juce_RenderingHelpers.h")
    lynx_patch_juce_file("${juce_dir}/modules/juce_graphics/contexts/juce_GraphicsContext.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_audio_processors/format_types/juce_VST3Common.h")
    lynx_patch_juce_file("${juce_dir}/modules/juce_audio_processors/format_types/juce_VST3PluginFormat.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_events/native/juce_Messaging_windows.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_events/native/juce_Messaging_linux.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_audio_plugin_client/juce_audio_plugin_client_LV2.cpp")
    # (Windows only, so a Linux or macOS configure never rewrites it in a JUCE checkout that others share)
    if(WIN32 OR CMAKE_SCRIPT_MODE_FILE)
        lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/native/juce_FileChooser_windows.cpp")
        lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/native/juce_Windowing_windows.cpp")
        lynx_patch_juce_file("${juce_dir}/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp")
        lynx_patch_juce_file("${juce_dir}/modules/juce_audio_devices/native/juce_ASIO_windows.cpp")
        lynx_patch_juce_file("${juce_dir}/modules/juce_graphics/native/juce_DirectX_windows.h")
        lynx_patch_juce_file("${juce_dir}/modules/juce_core/threads/juce_WaitableEvent.cpp")
        lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/juce_gui_basics.cpp")
        lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/native/juce_VBlank_windows.cpp")
        lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/windows/juce_VBlankAttachment.cpp")
        lynx_patch_juce_file("${juce_dir}/modules/juce_graphics/native/juce_Direct2DHwndContext_windows.cpp")
    endif()
endfunction()

# run as a script: cmake -DJUCE_DIR=<dir> -P cmake/LynxPatchJuce.cmake
if(CMAKE_SCRIPT_MODE_FILE AND DEFINED JUCE_DIR)
    lynx_patch_juce("${JUCE_DIR}")
endif()

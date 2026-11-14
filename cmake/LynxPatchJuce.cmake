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
# 26. Windows only, juce_gui_basics/native/juce_VBlank_windows.cpp + juce_Windowing_windows.cpp + windows/
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
# 27. Windows only, juce_graphics/native/juce_Direct2DHwndContext_windows.cpp (round 190gv; LYNX showed ~75 screen
#    updates a second while playing on a 144 Hz display, FL 143): JUCE 8.0.4 presents a painted frame only after a
#    per-window thread has seen the swap chain's waitable object and posted a message to the message thread, so a
#    frame painted on a vertical blank before that message arrived waited for the next one. Backported from JUCE's
#    later versions (JUCE's own code, same licence): no thread; when a frame is due the waitable object is polled
#    and the frame is painted only when it can be presented at once (a due frame waits up to 4 ms for it: the
#    compositor signals it a little after the vertical blank; LYNX_D2D_WAIT_MS); nothing to paint = no frame
#    (and no device checks). A present that failed is painted again on the next vertical blank.
#    LYNX_D2D_PACING=old: 8.0.4's pacing.
# 28. Windows only, juce_gui_basics/native/juce_Windowing_windows.cpp (round 190gv): the compositor shows a native child
#    window's new Direct2D frame only when its top-level window presents too. LYNX's docked panels (issue #116: the
#    Playlist, Channel rack, Mixer... are native children of the main window) therefore moved at the main window's own
#    rate, its toolbar's 72 Hz: ~75 screen updates a second while playing on a 144 Hz display (140 with docking off,
#    143 with every animation at 144 Hz). After a child window's frame the top-level window paints the one pixel under
#    the child's corner (hidden by the child) and presents in the same vertical blank. LYNX_CHILD_NUDGE=0: not.
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
    elseif(file MATCHES "juce_Windowing_linux.cpp$")
        lynx_juce_fix("no X focus for menu windows" "LYNX patch (issue #45): never takes the X focus"
            "    void grabFocus() override|NL|    {|NL|"
            "    void grabFocus() override|NL|    {|NL|        // LYNX patch (issue #45): never takes the X focus when it ignores key presses (a popup menu, a tooltip)|NL|        if ((getStyleFlags() & windowIgnoresKeyPresses) != 0)|NL|            return;|NL||NL|")
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
    elseif(file MATCHES "juce_CoreGraphicsContext_mac.mm$")
        lynx_juce_fix("no gradient flood from an empty path" "LYNX patch (issue #11): an empty path"
            "        CGContextReplacePathWithStrokedPath (context.get());|NL||NL|    ScopedCGContextState scopedState (context.get());"
            "        CGContextReplacePathWithStrokedPath (context.get());|NL||NL|    // LYNX patch (issue #11): an empty path draws nothing (Quartz ignores a clip to it: the fill flooded everything)|NL|    if (CGContextIsPathEmpty (context.get()))|NL|        return;|NL||NL|    ScopedCGContextState scopedState (context.get());")

        lynx_juce_fix("clip to an empty path" "LYNX patch (issue #11): clipping to an empty path"
            "void CoreGraphicsContext::clipToPath (const Path& path, const AffineTransform& transform)|NL|{|NL|    createPath (path, transform);|NL|"
            "void CoreGraphicsContext::clipToPath (const Path& path, const AffineTransform& transform)|NL|{|NL|    createPath (path, transform);|NL||NL|    // LYNX patch (issue #11): clipping to an empty path leaves nothing (Quartz would ignore it)|NL|    if (CGContextIsPathEmpty (context.get()))|NL|    {|NL|        CGContextClipToRect (context.get(), CGRectZero);|NL|        lastClipRect.reset();|NL|        return;|NL|    }|NL|")
    elseif(file MATCHES "juce_SimpleShapedText.cpp$")
        lynx_juce_fix("no walk back over no glyphs" "LYNX patch (issue #142): no glyphs"
            "                        auto it = glyphs.cend();|NL||NL|                        while (--it != glyphs.begin())"
            "                        auto it = glyphs.cend();|NL||NL|                        // LYNX patch (issue #142): no glyphs (a font without a typeface), nothing to walk back over|NL|                        if (glyphs.empty())|NL|                            return it;|NL||NL|                        while (--it != glyphs.begin())")
    elseif(file MATCHES "juce_Messaging_windows.cpp$")
        lynx_juce_fix("dispatch hook" "LYNX patch (issue #32): the message dispatch hook"
            "CheckEventBlockedByModalComps isEventBlockedByModalComps = nullptr;"
            "CheckEventBlockedByModalComps isEventBlockedByModalComps = nullptr;|NL||NL|// LYNX patch (issue #32): the message dispatch hook (LYNX's crash guard for plugin windows; MSG* as const void*)|NL|void (*lynxDispatchMessageHook) (const void*) = nullptr;")
        lynx_juce_fix("dispatch through the hook" "LYNX patch (issue #32): through the hook"
            "                TranslateMessage (&m);|NL|                DispatchMessage (&m);"
            "                TranslateMessage (&m);|NL||NL|                // LYNX patch (issue #32): through the hook when LYNX set one|NL|                if (lynxDispatchMessageHook != nullptr)|NL|                    lynxDispatchMessageHook (&m);|NL|                else|NL|                    DispatchMessage (&m);")
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
            "    void repaint (const Rectangle<int>& area) override|NL|    {|NL|        lynxWakeVBlank(); // LYNX patch (round 190gv)|NL||NL|        if (renderContext != nullptr)|NL|            renderContext->repaint ((area.toDouble() * getPlatformScaleFactor()).getSmallestIntegerContainer());|NL|    }|NL||NL|    // LYNX patch (round 190gv): the vertical blanks while this window needs them (cmake/LynxPatchJuce.cmake, fix 26):|NL|    // a VBlankAttachment, or a frame still to paint. A repaint, a paint message, a shown window, a new renderer or a|NL|    // new VBlankAttachment wake them.|NL|    void lynxWakeVBlank()|NL|    {|NL|        if (std::exchange (lynxVBlankAsleep, false))|NL|            VBlankDispatcher::getInstance()->lynxWake (*this);|NL|    }|NL||NL|    bool lynxWantsVBlanks() override|NL|    {|NL|        return lynxHadVBlankListeners || (renderContext != nullptr && renderContext->lynxHasPendingFrame());|NL|    }|NL||NL|    bool lynxHadVBlankListeners = true;|NL|")
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
        lynx_juce_fix("child window frames shown: the top-level presents too" "LYNX patch (round 190gv, fix 28): the compositor shows"
            "    bool lynxHadVBlankListeners = true;|NL|"
            "    bool lynxHadVBlankListeners = true;|NL||NL|    // LYNX patch (round 190gv, fix 28): the compositor shows a native child window's new Direct2D frame only when its|NL|    // top-level window presents as well: LYNX's docked panels (native children of the main window) moved at the main|NL|    // window's own rate (its toolbar's 72 Hz: ~75 screen updates a second on a 144 Hz display, 140 with docking off).|NL|    // After a child's frame, the top-level window paints one pixel under that child (hidden by it) and presents in|NL|    // the same vertical blank. LYNX_CHILD_NUDGE=0: not.|NL|    void lynxFramePresented()|NL|    {|NL|        static const bool nudge = SystemStats::getEnvironmentVariable (\"LYNX_CHILD_NUDGE\", {}) != \"0\";|NL||NL|        if (! nudge || (GetWindowLongPtr (hwnd, GWL_STYLE) & WS_CHILD) == 0)|NL|            return;|NL||NL|        const auto root = GetAncestor (hwnd, GA_ROOT);|NL|        auto* top = getOwnerOfWindow (root);|NL||NL|        if (top == nullptr || top == this || top->renderContext == nullptr)|NL|            return;|NL||NL|        POINT corner { 0, 0 };|NL|        MapWindowPoints (hwnd, root, &corner, 1);|NL|        const auto scale = top->getPlatformScaleFactor();|NL|        top->repaint (Rectangle<int> ((int) (corner.x / scale), (int) (corner.y / scale), 1, 1));|NL|        top->dispatchDeferredRepaints();|NL|        top->renderContext->onVBlank();|NL|    }|NL|")
        lynx_juce_fix("child window frames shown: after a frame" "LYNX patch (round 190gv, fix 28): a child window's frame"
            "        if (auto* ctx = direct2DContext->startFrame ((float) peer.getPlatformScaleFactor()))|NL|        {|NL|            peer.handlePaint (*ctx);|NL|            direct2DContext->endFrame();|NL|        }"
            "        if (auto* ctx = direct2DContext->startFrame ((float) peer.getPlatformScaleFactor()))|NL|        {|NL|            peer.handlePaint (*ctx);|NL|            direct2DContext->endFrame();|NL|            peer.lynxFramePresented(); // LYNX patch (round 190gv, fix 28): a child window's frame is shown at once|NL|        }")
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
    elseif(file MATCHES "juce_VBlank_windows.cpp$")
        lynx_juce_fix("vertical blanks only while needed: demands" "LYNX patch (round 190gv): a window listens to the vertical blanks only while"
            "namespace juce|NL|{|NL||NL|class VBlankThread : private Thread,"
            "namespace juce|NL|{|NL||NL|// LYNX patch (round 190gv): a window listens to the vertical blanks only while it needs them (cmake/LynxPatchJuce.cmake,|NL|// fix 26). A listener that says what it needs (the window peers) is asked after each of its vertical blanks; when no|NL|// listener of a display needs the next one, that display's thread sleeps (no wait for the vertical blank, no message|NL|// posted) until one of them wakes it again (lynxWake: a repaint, a VBlankAttachment, a listener that joins).|NL|// LYNX_VBLANK_ALWAYS=1: every vertical blank, as before (A/B measurements).|NL|struct LynxVBlankDemand|NL|{|NL|    virtual ~LynxVBlankDemand() = default;|NL|    virtual bool lynxWantsVBlanks() = 0; // (message thread, after its onVBlank)|NL|    bool lynxVBlankAsleep = false;       // its display's thread sleeps (message thread)|NL|};|NL||NL|static std::map<const ComponentPeer::VBlankListener*, LynxVBlankDemand*>& lynxVBlankDemands()|NL|{|NL|    static std::map<const ComponentPeer::VBlankListener*, LynxVBlankDemand*> demands; // (message thread)|NL|    return demands;|NL|}|NL||NL|static bool lynxVBlankAlways()|NL|{|NL|    static const bool always = SystemStats::getEnvironmentVariable (\"LYNX_VBLANK_ALWAYS\", {}).isNotEmpty();|NL|    return always;|NL|}|NL||NL|extern bool lynxVBlankListenersCalled; // juce_ComponentPeer.cpp (patched): the last callVBlankListeners had a listener|NL|extern int lynxD2DFramesStarted, lynxD2DNotReady, lynxD2DPresents; // juce_Direct2DHwndContext_windows.cpp (patched)|NL||NL|class VBlankThread : private Thread,")
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
    elseif(file MATCHES "juce_Direct2DHwndContext_windows.cpp$")
        lynx_juce_fix("Direct2D present pacing: declarations" "LYNX patch (round 190gv, fix 27): JUCE 8.0.4 paced"
            "namespace juce|NL|{|NL||NL|struct Direct2DHwndContext::HwndPimpl : public Direct2DGraphicsContext::Pimpl|NL|{"
            "namespace juce|NL|{|NL||NL|// LYNX patch (round 190gv): a window keeps its vertical blanks while its frame is still to paint|NL|// (juce_Windowing_windows.cpp asks; cmake/LynxPatchJuce.cmake, fix 26)|NL|struct LynxPendingFrame|NL|{|NL|    virtual ~LynxPendingFrame() = default;|NL|    virtual bool lynxHasPendingFrame() const = 0;|NL|};|NL||NL|static std::map<const Direct2DHwndContext*, const LynxPendingFrame*>& lynxPendingFrames()|NL|{|NL|    static std::map<const Direct2DHwndContext*, const LynxPendingFrame*> frames; // (message thread)|NL|    return frames;|NL|}|NL||NL|bool lynxD2DHwndHasPendingFrame (const Direct2DHwndContext& context)|NL|{|NL|    const auto found = lynxPendingFrames().find (&context);|NL|    return found == lynxPendingFrames().end() || found->second->lynxHasPendingFrame();|NL|}|NL||NL|// LYNX patch (round 190gv, fix 27): JUCE 8.0.4 paced the presents with a thread per window that posted a message to|NL|// the message thread on each swap chain event; a frame painted before that message arrived was presented only later,|NL|// so a window that repaints every frame got about every other vertical blank. As in JUCE's later versions (backported|NL|// from JUCE's own code), the swap chain's waitable object is polled when a frame is due, and a frame is painted only|NL|// when it can be presented at once. LYNX_D2D_PACING=old: the thread as in 8.0.4 (A/B measurements).|NL|// LYNX patch (round 190gv): counters for LYNX_D2D_STATS=<file> (diagnostics; juce_VBlank_windows.cpp writes them)|NL|int lynxD2DFramesStarted = 0, lynxD2DNotReady = 0, lynxD2DPresents = 0, lynxD2DPresentFails = 0;|NL||NL|static bool lynxOldPacing()|NL|{|NL|    static const bool old = SystemStats::getEnvironmentVariable (\"LYNX_D2D_PACING\", {}) == \"old\";|NL|    return old;|NL|}|NL||NL|struct Direct2DHwndContext::HwndPimpl : public Direct2DGraphicsContext::Pimpl,|NL|                                        private LynxPendingFrame|NL|{")
        lynx_juce_fix("Direct2D present pacing: no swap chain thread" "LYNX patch (round 190gv, fix 27): no thread"
            "        if (swapChainThread == nullptr)|NL|            if (auto* e = swap.getEvent())"
            "        if (swapChainThread == nullptr && lynxOldPacing()) // LYNX patch (round 190gv, fix 27): no thread (unless asked)|NL|            if (auto* e = swap.getEvent())")
        lynx_juce_fix("Direct2D present pacing: teardown" "LYNX patch (round 190gv, fix 27): a new swap chain"
            "        compositionTree.reset();|NL|        swapChainThread = nullptr;|NL|"
            "        compositionTree.reset();|NL|        swapChainThread = nullptr;|NL|        lynxDidWait = false; // LYNX patch (round 190gv, fix 27): a new swap chain, a new waitable object|NL|")
        lynx_juce_fix("Direct2D present pacing: ready when it can present" "LYNX patch (round 190gv, fix 27): painted only when"
            "        ready &= swap.canPaint();|NL|        ready &= compositionTree.has_value();|NL|"
            "        ready &= swap.canPaint();|NL|        ready &= compositionTree.has_value();|NL||NL|        // LYNX patch (round 190gv, fix 27): painted only when it can be presented at once|NL|        if (! lynxOldPacing())|NL|            ready = ready && lynxCanPresent();|NL|")
        lynx_juce_fix("Direct2D present pacing: the waitable object polled" "LYNX patch (round 190gv, fix 27): the swap chain's waitable object"
            "    bool swapEventReceived = false;|NL|"
            "    bool swapEventReceived = false;|NL||NL|    // LYNX patch (round 190gv, fix 27): the swap chain's waitable object was signalled since the last present, so it may|NL|    // present again|NL|    bool lynxDidWait = false;|NL||NL|    // A frame is due on a vertical blank, but the compositor takes the last present a little after that vertical blank|NL|    // and only then signals the object: polled at once, it missed by a fraction of a millisecond on every other vertical|NL|    // blank (~72 presents a second at 144 Hz). So a due frame waits up to a few milliseconds for it (lynxPresentWaitMs;|NL|    // LYNX_D2D_WAIT_MS=0..16), unless the last two waits found nothing (a window the compositor does not take frames|NL|    // from, e.g. minimised: no waiting on every vertical blank).|NL|    int lynxMissedWaits = 0;|NL||NL|    static DWORD lynxPresentWaitMs()|NL|    {|NL|        static const DWORD ms = [] {|NL|            const auto v = SystemStats::getEnvironmentVariable (\"LYNX_D2D_WAIT_MS\", {});|NL|            return (DWORD) (v.isEmpty() ? 4 : jlimit (0, 16, v.getIntValue()));|NL|        }();|NL|        return ms;|NL|    }|NL||NL|    bool lynxCanPresent()|NL|    {|NL|        if (lynxDidWait)|NL|            return true;|NL||NL|        auto* event = swap.getEvent();|NL||NL|        if (event == nullptr)|NL|            return lynxDidWait = true;|NL||NL|        const auto wait = lynxMissedWaits < 2 ? lynxPresentWaitMs() : (DWORD) 0;|NL|        lynxDidWait = WaitForSingleObjectEx (event->getHandle(), wait, TRUE) == WAIT_OBJECT_0;|NL|        lynxMissedWaits = lynxDidWait ? 0 : lynxMissedWaits + 1;|NL|        return lynxDidWait;|NL|    }|NL||NL|    // LYNX patch (round 190gv): fix 26's question (areas still to paint)|NL|    bool lynxHasPendingFrame() const override|NL|    {|NL|        return ! deferredRepaints.isEmpty();|NL|    }|NL||NL|    const Direct2DHwndContext* lynxOwner = nullptr;|NL|")
        lynx_juce_fix("Direct2D present pacing: registered" "LYNX patch (round 190gv): answers fix 26"
            "        : Pimpl (ownerIn),|NL|          hwnd (hwndIn)|NL|    {|NL|    }|NL||NL|    ~HwndPimpl() override = default;"
            "        : Pimpl (ownerIn),|NL|          hwnd (hwndIn)|NL|    {|NL|        lynxOwner = &ownerIn; // LYNX patch (round 190gv): answers fix 26's question|NL|        lynxPendingFrames()[lynxOwner] = this;|NL|    }|NL||NL|    ~HwndPimpl() override|NL|    {|NL|        lynxPendingFrames().erase (lynxOwner);|NL|    }")
        lynx_juce_fix("Direct2D present pacing: nothing to paint" "LYNX patch (round 190gv, fix 27): nothing to paint"
            "        setSize (getClientRect());|NL||NL|        auto* savedState = Pimpl::startFrame (dpiScale);"
            "        setSize (getClientRect());|NL||NL|        // LYNX patch (round 190gv, fix 27): nothing to paint in the window, no frame (and no device checks on every|NL|        // vertical blank)|NL|        if (! lynxOldPacing())|NL|        {|NL|            deferredRepaints.clipTo (getClientRect());|NL||NL|            if (deferredRepaints.isEmpty())|NL|                return nullptr;|NL|        }|NL||NL|        auto* savedState = Pimpl::startFrame (dpiScale);")
        lynx_juce_fix("Direct2D present pacing: present when painted" "LYNX patch (round 190gv, fix 27): checkPaintReady already knows"
            "        if (swap.getBuffer() == nullptr || dirtyRegionsInBackBuffer.isEmpty() || ! swapEventReceived)|NL|            return;"
            "        // LYNX patch (round 190gv, fix 27): checkPaintReady already knows it may present (old pacing: the event's message)|NL|        if (swap.getBuffer() == nullptr || dirtyRegionsInBackBuffer.isEmpty() || (lynxOldPacing() && ! swapEventReceived))|NL|            return;")
        lynx_juce_fix("Direct2D present pacing: after a present" "LYNX patch (round 190gv, fix 27): painted again on the next"
            "        jassertquiet (SUCCEEDED (hr));|NL||NL|        if (FAILED (hr))|NL|            return;|NL||NL|        // We managed to present a frame, so we should avoid rendering anything or calling|NL|        // present again until that frame has been shown on-screen.|NL|        swapEventReceived = false;|NL|"
            "        jassertquiet (SUCCEEDED (hr));|NL||NL|        if (FAILED (hr))|NL|        {|NL|            // LYNX patch (round 190gv, fix 27): painted again on the next vertical blank (nothing else presents it now)|NL|            if (! lynxOldPacing())|NL|                deferredRepaints.add (dirtyRegionsInBackBuffer);|NL||NL|            return;|NL|        }|NL||NL|        // We managed to present a frame, so we should avoid rendering anything or calling|NL|        // present again until that frame has been shown on-screen.|NL|        swapEventReceived = false;|NL|        lynxDidWait = false; // LYNX patch (round 190gv, fix 27): wait for the waitable object again|NL|")
        lynx_juce_fix("Direct2D present pacing: counted frames" "LYNX patch (round 190gv): counted for LYNX_D2D_STATS (frames)"
            "        if (savedState == nullptr)|NL|            return nullptr;|NL||NL|        // If a new frame is starting"
            "        if (savedState == nullptr)|NL|        {|NL|            ++lynxD2DNotReady; // LYNX patch (round 190gv): counted for LYNX_D2D_STATS (frames)|NL|            return nullptr;|NL|        }|NL||NL|        ++lynxD2DFramesStarted;|NL||NL|        // If a new frame is starting")
        lynx_juce_fix("Direct2D present pacing: counted presents" "LYNX patch (round 190gv): counted for LYNX_D2D_STATS (presents)"
            "        lynxDidWait = false; // LYNX patch (round 190gv, fix 27): wait for the waitable object again|NL|"
            "        lynxDidWait = false; // LYNX patch (round 190gv, fix 27): wait for the waitable object again|NL|        ++lynxD2DPresents;   // LYNX patch (round 190gv): counted for LYNX_D2D_STATS (presents)|NL|")
    else()
        lynx_juce_fix("drop target search after an unused drop" "LYNX patch (issue #11): a drop nobody took"
            "bool ComponentPeer::handleDragDrop (const ComponentPeer::DragInfo& info)|NL|{|NL|    handleDragMove (info);|NL|"
            "bool ComponentPeer::handleDragDrop (const ComponentPeer::DragInfo& info)|NL|{|NL|    handleDragMove (info);|NL|    // LYNX patch (issue #11): a drop nobody took must not leave the component under the mouse remembered|NL|    const ScopeGuard forgetComponentUnderMouse { [this] { lastDragAndDropCompUnderMouse = nullptr; } };|NL|")
        if(WIN32 OR CMAKE_SCRIPT_MODE_FILE) # (Windows only: a Linux or macOS configure leaves the file as it is)
            lynx_juce_fix("vertical blanks only while needed: listeners called" "LYNX patch (round 190gv): whether the last call had a listener"
                "void ComponentPeer::callVBlankListeners (double timestampSec)|NL|{|NL|"
                "#if JUCE_WINDOWS|NL|// LYNX patch (round 190gv): whether the last call had a listener (juce_Windowing_windows.cpp: a window keeps its|NL|// vertical blanks only while it needs them; cmake/LynxPatchJuce.cmake, fix 26)|NL|bool lynxVBlankListenersCalled = false;|NL|#endif|NL||NL|void ComponentPeer::callVBlankListeners (double timestampSec)|NL|{|NL|   #if JUCE_WINDOWS|NL|    lynxVBlankListenersCalled = ! vBlankListeners.isEmpty();|NL|   #endif|NL|")
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
    lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/menus/juce_PopupMenu.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/native/juce_Windowing_linux.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/desktop/juce_Displays.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/native/juce_XWindowSystem_linux.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_graphics/native/juce_CoreGraphicsContext_mac.mm")
    lynx_patch_juce_file("${juce_dir}/modules/juce_graphics/native/juce_Direct2DImage_windows.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_graphics/fonts/juce_SimpleShapedText.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_audio_processors/format_types/juce_VST3Common.h")
    lynx_patch_juce_file("${juce_dir}/modules/juce_audio_processors/format_types/juce_VST3PluginFormat.cpp")
    lynx_patch_juce_file("${juce_dir}/modules/juce_events/native/juce_Messaging_windows.cpp")
    # (Windows only, so a Linux or macOS configure never rewrites it in a JUCE checkout that others share)
    if(WIN32 OR CMAKE_SCRIPT_MODE_FILE)
        lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/native/juce_FileChooser_windows.cpp")
        lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/native/juce_Windowing_windows.cpp")
        lynx_patch_juce_file("${juce_dir}/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp")
        lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/native/juce_VBlank_windows.cpp")
        lynx_patch_juce_file("${juce_dir}/modules/juce_gui_basics/windows/juce_VBlankAttachment.cpp")
        lynx_patch_juce_file("${juce_dir}/modules/juce_graphics/native/juce_Direct2DHwndContext_windows.cpp")
    endif()
endfunction()

# run as a script: cmake -DJUCE_DIR=<dir> -P cmake/LynxPatchJuce.cmake
if(CMAKE_SCRIPT_MODE_FILE AND DEFINED JUCE_DIR)
    lynx_patch_juce("${JUCE_DIR}")
endif()

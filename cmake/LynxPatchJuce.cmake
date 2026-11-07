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
    elseif(file MATCHES "juce_Messaging_windows.cpp$")
        lynx_juce_fix("dispatch hook" "LYNX patch (issue #32): the message dispatch hook"
            "CheckEventBlockedByModalComps isEventBlockedByModalComps = nullptr;"
            "CheckEventBlockedByModalComps isEventBlockedByModalComps = nullptr;|NL||NL|// LYNX patch (issue #32): the message dispatch hook (LYNX's crash guard for plugin windows; MSG* as const void*)|NL|void (*lynxDispatchMessageHook) (const void*) = nullptr;")
        lynx_juce_fix("dispatch through the hook" "LYNX patch (issue #32): through the hook"
            "                TranslateMessage (&m);|NL|                DispatchMessage (&m);"
            "                TranslateMessage (&m);|NL||NL|                // LYNX patch (issue #32): through the hook when LYNX set one|NL|                if (lynxDispatchMessageHook != nullptr)|NL|                    lynxDispatchMessageHook (&m);|NL|                else|NL|                    DispatchMessage (&m);")
    elseif(file MATCHES "juce_VST3Common.h$")
        lynx_juce_fix("VST3 note ids" "LYNX patch (issue #70): each note its own noteId"
            "        toEventList (result, midiBuffer, &mapping, callback);|NL|    }"
            "        toEventList (result, midiBuffer, &mapping, callback);|NL||NL|        // LYNX patch (issue #70): each note its own noteId (as FL), its note off the same one|NL|        if (auto* list = dynamic_cast<MidiEventList*> (&result))|NL|            list->lynxNumberNotes();|NL|    }|NL||NL|    // A plugin that keys its voices by noteId took every note (all -1) as one note. Each note on gets the next id;|NL|    // a note off (or poly pressure) the id of the oldest sounding note on its channel and key, else -1.|NL|    void lynxNumberNotes() noexcept|NL|    {|NL|        const ScopedLock sl (events.getLock());|NL|        for (auto& e : events)|NL|        {|NL|            if (e.type == Steinberg::Vst::Event::kNoteOnEvent)|NL|            {|NL|                auto& key = lynxKeys[jlimit (0, 15, (int) e.noteOn.channel)][jlimit (0, 127, (int) e.noteOn.pitch)];|NL|                const auto id = lynxNextId;|NL|                lynxNextId = lynxNextId >= 0x7fffffff ? 0 : lynxNextId + 1;|NL|                if (key.count == (int) key.ids.size()) // (notes that never ended: the oldest is forgotten)|NL|                {|NL|                    std::rotate (key.ids.begin(), key.ids.begin() + 1, key.ids.end());|NL|                    --key.count;|NL|                }|NL|                key.ids[(size_t) key.count++] = id;|NL|                e.noteOn.noteId = id;|NL|            }|NL|            else if (e.type == Steinberg::Vst::Event::kNoteOffEvent)|NL|            {|NL|                auto& key = lynxKeys[jlimit (0, 15, (int) e.noteOff.channel)][jlimit (0, 127, (int) e.noteOff.pitch)];|NL|                e.noteOff.noteId = -1;|NL|                if (key.count > 0)|NL|                {|NL|                    e.noteOff.noteId = key.ids[0];|NL|                    std::rotate (key.ids.begin(), key.ids.begin() + 1, key.ids.end());|NL|                    --key.count;|NL|                }|NL|            }|NL|            else if (e.type == Steinberg::Vst::Event::kPolyPressureEvent)|NL|            {|NL|                const auto& key = lynxKeys[jlimit (0, 15, (int) e.polyPressure.channel)][jlimit (0, 127, (int) e.polyPressure.pitch)];|NL|                e.polyPressure.noteId = key.count > 0 ? key.ids[0] : -1;|NL|            }|NL|        }|NL|    }|NL||NL|    struct LynxKey|NL|    {|NL|        std::array<Steinberg::int32, 4> ids {};|NL|        int count = 0;|NL|    };|NL|    std::array<std::array<LynxKey, 128>, 16> lynxKeys {};|NL|    Steinberg::int32 lynxNextId = 0;")
    else()
        lynx_juce_fix("drop target search after an unused drop" "LYNX patch (issue #11): a drop nobody took"
            "bool ComponentPeer::handleDragDrop (const ComponentPeer::DragInfo& info)|NL|{|NL|    handleDragMove (info);|NL|"
            "bool ComponentPeer::handleDragDrop (const ComponentPeer::DragInfo& info)|NL|{|NL|    handleDragMove (info);|NL|    // LYNX patch (issue #11): a drop nobody took must not leave the component under the mouse remembered|NL|    const ScopeGuard forgetComponentUnderMouse { [this] { lastDragAndDropCompUnderMouse = nullptr; } };|NL|")
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
    lynx_patch_juce_file("${juce_dir}/modules/juce_audio_processors/format_types/juce_VST3Common.h")
    lynx_patch_juce_file("${juce_dir}/modules/juce_events/native/juce_Messaging_windows.cpp")
endfunction()

# run as a script: cmake -DJUCE_DIR=<dir> -P cmake/LynxPatchJuce.cmake
if(CMAKE_SCRIPT_MODE_FILE AND DEFINED JUCE_DIR)
    lynx_patch_juce("${JUCE_DIR}")
endif()

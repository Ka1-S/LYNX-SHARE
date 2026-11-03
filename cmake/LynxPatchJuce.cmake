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
    elseif(file MATCHES "juce_Windowing_linux.cpp$")
        lynx_juce_fix("no X focus for menu windows" "LYNX patch (issue #45): never takes the X focus"
            "    void grabFocus() override|NL|    {|NL|"
            "    void grabFocus() override|NL|    {|NL|        // LYNX patch (issue #45): never takes the X focus when it ignores key presses (a popup menu, a tooltip)|NL|        if ((getStyleFlags() & windowIgnoresKeyPresses) != 0)|NL|            return;|NL||NL|")
    elseif(file MATCHES "juce_Displays.cpp$")
        lynx_juce_fix("monitors that touch no other" "LYNX patch (issue #45): a monitor that touches"
            "        // All of the nodes should have a parent|NL|        jassert (node.parent != nullptr);|NL|"
            "        // LYNX patch (issue #45): a monitor that touches no other one keeps its own place (physical / scale)|NL|        if (node.parent == nullptr)|NL|            node.logicalArea = node.display->totalArea.toDouble() / node.display->scale;|NL|")
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
endfunction()

# run as a script: cmake -DJUCE_DIR=<dir> -P cmake/LynxPatchJuce.cmake
if(CMAKE_SCRIPT_MODE_FILE AND DEFINED JUCE_DIR)
    lynx_patch_juce("${JUCE_DIR}")
endif()

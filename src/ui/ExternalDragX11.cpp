// Issue #174, Linux / BSD: another program's window at a point of the screen (ui/ExternalDrag.cpp's placeAt). A file of
// its own: X11's headers define Status, None, Bool... as macros, which clash with LYNX's own names.
#define JUCE_GUI_BASICS_INCLUDE_XHEADERS 1
#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_LINUX || JUCE_BSD
 #include <algorithm>
 #include <cstring>
 #include <vector>
 #include <unistd.h>

namespace lynx::externaldrag
{
// X11 tells no program which windows of OTHER programs are in front of its own (JUCE's peers only test LYNX's own
// windows against each other), so another program's window over a LYNX window (a plugin's window in a separate
// process, a file manager) is found here: the top-most window at the point among the root's children, then among its
// children, and so on; the first that carries _NET_WM_PID (JUCE sets it on every window it makes, other toolkits on
// their top-level windows) names its process. LYNX's own windows that let the mouse through (the drag image under the
// pointer) are passed over.
bool otherProgramAt (juce::Point<int> screenPos)
{
    auto* system = juce::XWindowSystem::getInstanceWithoutCreating();
    auto* display = system != nullptr ? system->getDisplay() : nullptr;
    if (display == nullptr)
        return false;

    std::vector<::Window> passThrough;
    for (int i = 0; i < juce::ComponentPeer::getNumPeers(); ++i)
        if (auto* peer = juce::ComponentPeer::getPeer (i); peer != nullptr && (peer->getStyleFlags() & juce::ComponentPeer::windowIgnoresMouseClicks) != 0)
            passThrough.push_back ((::Window) (juce::pointer_sized_uint) peer->getNativeHandle());

    const auto physical = juce::Desktop::getInstance().getDisplays().logicalToPhysical (screenPos);
    auto* x = juce::X11Symbols::getInstance();
    juce::XWindowSystemUtilities::ScopedXLock lock;
    const auto pidAtom = juce::XWindowSystemUtilities::Atoms::getIfExists (display, "_NET_WM_PID");
    if (pidAtom == None)
        return false;

    const auto root = x->xRootWindow (display, x->xDefaultScreen (display));
    auto window = root;
    for (int depth = 0; depth < 12; ++depth) // (a window manager's frame, its client, a toolkit's inner windows)
    {
        ::Window rootReturn = None, parent = None, *children = nullptr;
        unsigned int count = 0;
        if (x->xQueryTree (display, window, &rootReturn, &parent, &children, &count) == 0)
            return false;

        ::Window found = None;
        for (int i = (int) count - 1; i >= 0 && found == None; --i) // (top-most first)
        {
            const auto child = children[i];
            if (std::find (passThrough.begin(), passThrough.end(), child) != passThrough.end())
                continue;
            XWindowAttributes a {};
            if (x->xGetWindowAttributes (display, child, &a) == 0 || a.map_state != IsViewable || a.c_class == InputOnly)
                continue;
            int left = 0, top = 0;
            ::Window unused = None;
            x->xTranslateCoordinates (display, child, root, 0, 0, &left, &top, &unused);
            if (physical.x >= left && physical.y >= top && physical.x < left + a.width && physical.y < top + a.height)
                found = child;
        }
        if (children != nullptr)
            x->xFree (children);
        if (found == None)
            return false; // the bare root (the desktop) or a window without a process: JUCE's own test decides

        window = found;
        juce::XWindowSystemUtilities::GetXProperty prop (display, window, pidAtom, 0, 1, false, XA_CARDINAL);
        if (prop.success && prop.data != nullptr && prop.actualFormat == 32 && prop.numItems == 1)
        {
            unsigned long pid = 0;
            std::memcpy (&pid, prop.data, sizeof (pid));
            return pid != (unsigned long) ::getpid();
        }
    }
    return false;
}
} // namespace lynx::externaldrag
#endif

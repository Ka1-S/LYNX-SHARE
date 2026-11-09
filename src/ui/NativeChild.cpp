#include "ui/NativeChild.h"
#include "ui/WindowOwner.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace lynx::nativechild
{
namespace
{
// `area`'s top-left in its top-level component (the main window's client area, where its native children are placed)
juce::Point<int> originOf (const juce::Component& area)
{
    const auto* top = area.getTopLevelComponent();
    return top != nullptr && top != &area ? top->getLocalPoint (&area, juce::Point<int>()) : juce::Point<int>();
}

void* topHandle (const juce::Component& area)
{
    const auto* top = area.getTopLevelComponent();
    const auto* peer = top != nullptr ? top->getPeer() : nullptr;
    return peer != nullptr ? peer->getNativeHandle() : nullptr;
}

// the origin the component was last placed with (its bounds are relative to the main window's client area)
juce::Point<int> placedOrigin (const juce::Component& c)
{
    const auto& p = c.getProperties();
    return { (int) p.getWithDefault ("lynxNativeX", 0), (int) p.getWithDefault ("lynxNativeY", 0) };
}

struct KeyForwarder final : juce::KeyListener
{
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override { return runSecondaryWindowKey (key); }
};

KeyForwarder& keyForwarder()
{
    static KeyForwarder forwarder;
    return forwarder;
}
} // namespace

bool dockingEnabled()
{
    static const bool on = juce::SystemStats::getEnvironmentVariable ("LYNX_DOCKED_PLUGINS", {}).trim() == "1";
    return on;
}

bool attach (juce::Component& c, juce::Component& area)
{
    if (isAttached (c))
        return true;
    auto* handle = topHandle (area);
    if (handle == nullptr || c.getParentComponent() != nullptr)
        return false;
    const auto bounds = c.getBounds();
    // no title bar, no taskbar entry, no shadow: a plain child window the system clips to the main window
    c.addToDesktop (0, handle);
    c.getProperties().set ("lynxNativeChild", true);
    c.getProperties().set ("lynxNativeParent", (juce::int64) (juce::pointer_sized_int) handle);
    place (c, area, bounds);
    return true;
}

void detach (juce::Component& c)
{
    if (! isAttached (c))
        return;
    const auto bounds = c.getBounds() - placedOrigin (c);
    for (const auto* key : { "lynxNativeChild", "lynxNativeParent", "lynxNativeX", "lynxNativeY" })
        c.getProperties().remove (key);
    c.removeFromDesktop();
    c.setBounds (bounds);
}

bool isAttached (const juce::Component& c)
{
    return c.isOnDesktop() && (bool) c.getProperties().getWithDefault ("lynxNativeChild", false);
}

void place (juce::Component& c, const juce::Component& area, juce::Rectangle<int> boundsInArea)
{
    if (! isAttached (c))
    {
        c.setBounds (boundsInArea);
        return;
    }
    const auto origin = originOf (area);
    c.getProperties().set ("lynxNativeX", origin.x);
    c.getProperties().set ("lynxNativeY", origin.y);
    c.setBounds (boundsInArea + origin);
}

juce::Rectangle<int> boundsIn (const juce::Component& c, const juce::Component&)
{
    return isAttached (c) ? c.getBounds() - placedOrigin (c) : c.getBounds();
}

void refresh (juce::Component& c, juce::Component& area)
{
    if (! isAttached (c))
        return;
    const auto bounds = boundsIn (c, area);
    const auto parent = (juce::pointer_sized_int) (juce::int64) c.getProperties().getWithDefault ("lynxNativeParent", (juce::int64) 0);
    if (parent != (juce::pointer_sized_int) topHandle (area))
    {
        // the main window's native window was made again: the old child window went with it
        const auto visible = c.isVisible();
        detach (c);
        if (! attach (c, area))
            return;
        c.setVisible (visible);
    }
    place (c, area, bounds);
}

void keepAbove (juce::Component& c, juce::Component* below)
{
    if (below == nullptr || ! isAttached (c) || ! isAttached (*below))
        return;
    auto* self = c.getWindowHandle();
    auto* under = below->getWindowHandle();
    if (self == nullptr || under == nullptr || self == under || isAbove (self, under))
        return;
   #if JUCE_WINDOWS
    // SetWindowPos puts a window just below its "insert after" window: the one right above `below`, or the top
    const auto above = GetWindow (static_cast<HWND> (under), GW_HWNDPREV);
    SetWindowPos (static_cast<HWND> (self), above != nullptr ? above : HWND_TOP, 0, 0, 0, 0,
                  SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
   #else
    c.toFront (false);
   #endif
}

void forwardUnusedKeys (juce::Component& c)
{
    c.addKeyListener (&keyForwarder()); // (JUCE adds a listener once)
}

bool isAbove (void* a, void* b)
{
   #if JUCE_WINDOWS
    if (a == nullptr || b == nullptr)
        return false;
    for (auto h = GetWindow (static_cast<HWND> (a), GW_HWNDNEXT); h != nullptr; h = GetWindow (h, GW_HWNDNEXT))
        if (h == static_cast<HWND> (b))
            return true;
   #else
    juce::ignoreUnused (a, b);
   #endif
    return false;
}

void* parentOf (void* window)
{
   #if JUCE_WINDOWS
    return window != nullptr ? (void*) GetAncestor (static_cast<HWND> (window), GA_PARENT) : nullptr;
   #else
    juce::ignoreUnused (window);
    return nullptr;
   #endif
}

void* ownerOf (void* window)
{
   #if JUCE_WINDOWS
    return window != nullptr ? (void*) GetWindow (static_cast<HWND> (window), GW_OWNER) : nullptr;
   #else
    juce::ignoreUnused (window);
    return nullptr;
   #endif
}
} // namespace lynx::nativechild

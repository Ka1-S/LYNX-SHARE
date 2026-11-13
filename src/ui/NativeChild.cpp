#include "ui/NativeChild.h"
#include "ui/WindowOwner.h"
#include "core/DockStack.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

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

// JUCE keeps a peer's scale listeners to itself (protected); a child window's are only called on WM_DPICHANGED, which
// child windows never get. Reaching the member through a derived class's name is plain C++.
struct PeerScaleListeners : juce::ComponentPeer
{
    static void notify (juce::ComponentPeer& peer, double scale)
    {
        (peer.*(&PeerScaleListeners::scaleFactorListeners)).call ([scale] (juce::ComponentPeer::ScaleFactorListener& l) { l.nativeScaleFactorChanged (scale); });
    }
};

// `bounds` (the component's bounds: in the main window's client area, LYNX's zoomed units) in the parent's physical
// pixels (times the UI zoom = the peer's logical pixels, times the display's scale = physical)
#if JUCE_WINDOWS
RECT physicalRect (const juce::Component& c, const juce::ComponentPeer& peer, juce::Rectangle<int> bounds)
{
    const auto scale = peer.getPlatformScaleFactor() * (double) c.getDesktopScaleFactor();
    const auto b = bounds.toDouble() * scale;
    return { juce::roundToInt (b.getX()), juce::roundToInt (b.getY()), juce::roundToInt (b.getRight()), juce::roundToInt (b.getBottom()) };
}

// the native window's place in its parent's client area, physical pixels
RECT rectInParent (HWND hwnd, HWND parent)
{
    RECT now {};
    GetWindowRect (hwnd, &now);
    MapWindowPoints (nullptr, parent, reinterpret_cast<POINT*> (&now), 2);
    return now;
}
#endif

// JUCE places a child window from its logical bounds with the scale of the display at that point taken as a screen
// point, which is not the parent's when the main window is on another display: put it at `bounds` in the parent's own
// scale. `bounds` is what the caller wants, not what JUCE reads back: for a transparent (layered) child JUCE 8.0.4 adds
// the parent's screen position when it places it, and its read-back then replaces the component's bounds (round 190cj).
void placePhysically (juce::Component& c, juce::Rectangle<int> bounds)
{
   #if JUCE_WINDOWS
    auto* peer = c.getPeer();
    auto* hwnd = peer != nullptr ? static_cast<HWND> (peer->getNativeHandle()) : nullptr;
    auto* parent = hwnd != nullptr ? GetParent (hwnd) : nullptr;
    if (parent == nullptr)
        return;
    const auto want = physicalRect (c, *peer, bounds);
    const auto now = rectInParent (hwnd, parent);
    if (now.left == want.left && now.top == want.top && now.right == want.right && now.bottom == want.bottom)
        return;
    SetWindowPos (hwnd, nullptr, want.left, want.top, want.right - want.left, want.bottom - want.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
   #else
    juce::ignoreUnused (c, bounds);
   #endif
}

void placePhysically (juce::Component& c) { placePhysically (c, c.getBounds()); }

// the attached children of one native window, from the bottom of their stacking order up
std::vector<juce::Component*> attachedChildrenOf (void* parent, const juce::Component* except = nullptr)
{
    std::vector<juce::Component*> out;
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* c = desktop.getComponent (i); c != nullptr && c != except && isAttached (*c)
            && (juce::pointer_sized_int) (juce::int64) c->getProperties().getWithDefault ("lynxNativeParent", (juce::int64) 0) == (juce::pointer_sized_int) parent)
            out.push_back (c);
   #if JUCE_WINDOWS
    std::stable_sort (out.begin(), out.end(), [] (juce::Component* a, juce::Component* b) { return isAbove (b->getWindowHandle(), a->getWindowHandle()); });
   #endif
    return out;
}

std::vector<juce::Component::SafePointer<juce::Component>>& overlays()
{
    static std::vector<juce::Component::SafePointer<juce::Component>> list;
    return list;
}

std::vector<juce::Component::SafePointer<juce::Component>>& floaters()
{
    static std::vector<juce::Component::SafePointer<juce::Component>> list;
    return list;
}

void dropDeleted (std::vector<juce::Component::SafePointer<juce::Component>>& list, const juce::Component* also = nullptr)
{
    list.erase (std::remove_if (list.begin(), list.end(), [also] (const auto& p) { return p == nullptr || p.getComponent() == also; }), list.end());
}

// see makeOverlayHost
class OverlayHost final : public juce::Component
{
public:
    OverlayHost (std::unique_ptr<juce::Component> c, juce::Component& o) : content (std::move (c)), over (o)
    {
        setOpaque (true); // (a child window with no transparency: what is under it is in the picture)
        addAndMakeVisible (*content);
        overlays().emplace_back (this);
    }

    ~OverlayHost() override
    {
        auto& list = overlays();
        list.erase (std::remove_if (list.begin(), list.end(), [this] (const auto& p) { return p == nullptr || p.getComponent() == this; }), list.end());
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
        if (backdrop.isValid()) // (the screen's pixels when captured from the window: drawn at the component's size)
            g.drawImage (backdrop, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    }

    void resized() override
    {
        takePicture();
        content->setBounds (getLocalBounds());
    }

    void takePicture()
    {
        if (getWidth() <= 0 || getHeight() <= 0)
            return;
        if (isOnDesktop())
            return; // (resized while shown: the picture it has, stretched; capturing now would picture the overlay)
        if (captureWindow())
            return;
        backdrop = juce::Image (juce::Image::RGB, getWidth(), getHeight(), true);
        juce::Graphics g (backdrop);
        g.drawImageAt (over.createComponentSnapshot (over.getLocalBounds().withSize (getWidth(), getHeight()), true, 1.0f), 0, 0);
        const auto origin = originOf (over);
        for (auto* child : attachedChildrenOf (topHandle (over), this))
            if (child->isVisible())
                g.drawImageAt (child->createComponentSnapshot (child->getLocalBounds(), true, 1.0f),
                               child->getX() - origin.x, child->getY() - origin.y);
    }

    // The main window's client area as it is drawn, plugin editors included (their own native windows are not in a
    // component snapshot): PrintWindow with the full content (DirectX / composition surfaces too).
    bool captureWindow()
    {
       #if JUCE_WINDOWS
        auto* hwnd = static_cast<HWND> (topHandle (over));
        if (hwnd == nullptr)
            return false;
        RECT client {};
        GetClientRect (hwnd, &client);
        const auto w = (int) (client.right - client.left), h = (int) (client.bottom - client.top);
        if (w <= 0 || h <= 0)
            return false;
        auto* screen = GetDC (nullptr);
        auto* dc = CreateCompatibleDC (screen);
        BITMAPINFO info {};
        info.bmiHeader.biSize = sizeof (info.bmiHeader);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h; // top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        auto* bitmap = CreateDIBSection (screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        bool ok = false;
        if (bitmap != nullptr && bits != nullptr)
        {
            auto* old = SelectObject (dc, bitmap);
            constexpr UINT clientOnly = 0x1, fullContent = 0x2; // PW_CLIENTONLY, PW_RENDERFULLCONTENT (Windows 8.1+)
            ok = PrintWindow (hwnd, dc, clientOnly | fullContent) != FALSE;
            if (ok)
            {
                // the part of the client area `over` covers, in physical pixels
                const auto scale = (double) w / juce::jmax (1, over.getTopLevelComponent()->getWidth());
                const auto origin = originOf (over);
                const auto area = juce::Rectangle<int> (juce::roundToInt (origin.x * scale), juce::roundToInt (origin.y * scale),
                                                        juce::roundToInt (getWidth() * scale), juce::roundToInt (getHeight() * scale))
                                      .getIntersection ({ 0, 0, w, h });
                backdrop = juce::Image (juce::Image::RGB, juce::jmax (1, area.getWidth()), juce::jmax (1, area.getHeight()), false);
                juce::Image::BitmapData out (backdrop, juce::Image::BitmapData::writeOnly);
                const auto* src = static_cast<const juce::uint8*> (bits);
                for (int y = 0; y < area.getHeight(); ++y)
                    for (int x = 0; x < area.getWidth(); ++x)
                    {
                        const auto* px = src + ((size_t) (y + area.getY()) * (size_t) w + (size_t) (x + area.getX())) * 4; // BGRA
                        out.setPixelColour (x, y, juce::Colour (px[2], px[1], px[0]));
                    }
            }
            SelectObject (dc, old);
        }
        if (bitmap != nullptr)
            DeleteObject (bitmap);
        DeleteDC (dc);
        ReleaseDC (nullptr, screen);
        return ok;
       #else
        return false;
       #endif
    }

    std::unique_ptr<juce::Component> release()
    {
        removeChildComponent (content.get());
        return std::move (content);
    }

private:
    std::unique_ptr<juce::Component> content;
    juce::Component& over;
    juce::Image backdrop;
};

#if JUCE_WINDOWS
// Round 190cj: a transparent child window (a dancer) takes the mouse wherever its picture is not fully transparent:
// the system decides that from the pixels, before JUCE's hitTest is asked, and a click JUCE then finds no component
// for is simply lost. So the window answers the system's own hit test: where the component's hitTest says no (FL's
// "Solid" off, the dancer's arms and reflection) it is "transparent", and the click goes to the window under it.
struct ClickThrough
{
    WNDPROC original = nullptr;
    juce::Component::SafePointer<juce::Component> component;
};

std::map<HWND, ClickThrough>& clickThroughWindows()
{
    static std::map<HWND, ClickThrough> windows;
    return windows;
}

LRESULT CALLBACK clickThroughProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto& windows = clickThroughWindows();
    const auto it = windows.find (hwnd);
    if (it == windows.end())
        return DefWindowProc (hwnd, message, wParam, lParam);
    const auto original = it->second.original;
    if (message == WM_NCHITTEST)
    {
        if (auto* c = it->second.component.getComponent())
            if (auto* peer = c->getPeer())
            {
                POINT p { (int) (short) LOWORD (lParam), (int) (short) HIWORD (lParam) };
                ScreenToClient (hwnd, &p);
                const auto scale = juce::jmax (0.01, peer->getPlatformScaleFactor() * (double) c->getDesktopScaleFactor());
                if (! c->hitTest (juce::roundToInt (std::floor (p.x / scale)), juce::roundToInt (std::floor (p.y / scale))))
                    return HTTRANSPARENT;
            }
    }
    else if (message == WM_NCDESTROY)
    {
        SetWindowLongPtr (hwnd, GWLP_WNDPROC, (LONG_PTR) original);
        windows.erase (it);
    }
    return CallWindowProc (original, hwnd, message, wParam, lParam);
}

void passMissedClicksThrough (juce::Component& c)
{
    auto* hwnd = static_cast<HWND> (c.getWindowHandle());
    if (hwnd == nullptr || clickThroughWindows().count (hwnd) != 0)
        return;
    auto& entry = clickThroughWindows()[hwnd];
    entry.component = &c;
    entry.original = (WNDPROC) SetWindowLongPtr (hwnd, GWLP_WNDPROC, (LONG_PTR) clickThroughProc);
}
#endif

#if JUCE_WINDOWS
// Round 190ev (issue #152): the attached components that come in front on any click inside them, and what runs then.
struct ClickRaise
{
    juce::Component::SafePointer<juce::Component> component;
    std::function<void()> onClick;
};

std::vector<ClickRaise>& clickRaises()
{
    static std::vector<ClickRaise> list;
    return list;
}

bool staysInFront (const juce::Component* c)
{
    if (c == nullptr || ! c->isVisible())
        return true; // (a hidden sibling covers nothing)
    for (auto* list : { &floaters(), &overlays() })
        for (const auto& o : *list)
            if (o.getComponent() == c)
                return true;
    return false;
}

void raiseClicked (juce::Component::SafePointer<juce::Component> safe)
{
    auto* c = safe.getComponent();
    if (c == nullptr || ! isAttached (*c) || ! c->isVisible())
        return;
    const auto parent = (void*) (juce::pointer_sized_int) (juce::int64) c->getProperties().getWithDefault ("lynxNativeParent", (juce::int64) 0);
    const auto siblings = attachedChildrenOf (parent);
    if (dockstack::needsRaise (siblings, c, [] (juce::Component* w) { return staysInFront (w); }))
    {
        c->toFront (false);
        raiseOverlays();
    }
    for (const auto& r : clickRaises())
        if (r.component == safe && r.onClick != nullptr)
        {
            auto run = r.onClick; // (it may close the window and change the list)
            run();
            break;
        }
}

// A button pressed anywhere in the UI thread's windows: when it lands in a native window INSIDE a registered attached
// component (a window of a plugin's own view), that component comes in front (after this message: the hook does not
// move windows itself). A click on the component's own native window goes through JUCE (its mouse listeners).
LRESULT CALLBACK clickHookProc (int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && wParam == PM_REMOVE && lParam != 0)
    {
        const auto* msg = reinterpret_cast<const MSG*> (lParam);
        switch (msg->message)
        {
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_XBUTTONDOWN:
            case WM_NCLBUTTONDOWN:
            case WM_NCRBUTTONDOWN:
            {
                auto& list = clickRaises();
                std::erase_if (list, [] (const ClickRaise& r) { return r.component == nullptr; });
                if (list.empty() || msg->hwnd == nullptr)
                    break;
                std::vector<HWND> ancestry;
                for (auto h = msg->hwnd; h != nullptr && ancestry.size() < 24; h = GetAncestor (h, GA_PARENT))
                    ancestry.push_back (h);
                const auto at = dockstack::clickedWindow (ancestry, [&list] (HWND h) {
                    return std::any_of (list.begin(), list.end(), [h] (const ClickRaise& r) { return r.component->getWindowHandle() == (void*) h; });
                });
                if (at <= 0) // (none, or the component's own window: JUCE's mouse listeners take it)
                    break;
                for (const auto& r : list)
                    if (r.component->getWindowHandle() == (void*) ancestry[(std::size_t) at])
                    {
                        juce::MessageManager::callAsync ([safe = r.component] { raiseClicked (safe); });
                        break;
                    }
                break;
            }
            default:
                break;
        }
    }
    return CallNextHookEx (nullptr, code, wParam, lParam);
}

void installClickHook()
{
    static HHOOK hook = nullptr;
    if (hook == nullptr)
        hook = SetWindowsHookExW (WH_GETMESSAGE, clickHookProc, nullptr, GetCurrentThreadId());
}
#endif

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
    static const bool on = juce::SystemStats::getEnvironmentVariable ("LYNX_DOCKED_PLUGINS", {}).trim() != "0";
    return on;
}

bool attach (juce::Component& c, juce::Component& area, int styleFlags)
{
    if (isAttached (c))
        return true;
    auto* handle = topHandle (area);
    if (handle == nullptr || c.getParentComponent() != nullptr)
        return false;
    const auto bounds = c.getBounds();
    // no title bar, no taskbar entry, no shadow: a plain child window the system clips to the main window. (Round 190ev:
    // a docked LYNX editor window keeps the flags it was attached with, also when it is attached again.)
    if (styleFlags < 0)
        styleFlags = (int) c.getProperties().getWithDefault ("lynxNativeFlags", 0);
    c.getProperties().set ("lynxNativeFlags", styleFlags);
    c.addToDesktop (styleFlags, handle);
    c.getProperties().set ("lynxNativeChild", true);
    c.getProperties().set ("lynxNativeParent", (juce::int64) (juce::pointer_sized_int) handle);
   #if JUCE_WINDOWS
    if (! c.isOpaque()) // (a floater: per-pixel transparency)
        passMissedClicksThrough (c);
   #endif
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
    placePhysically (c, boundsInArea + origin);
}

bool isPlacedAt (const juce::Component& c, const juce::Component& area, juce::Rectangle<int> boundsInArea)
{
   #if JUCE_WINDOWS
    auto* peer = c.getPeer();
    auto* hwnd = peer != nullptr ? static_cast<HWND> (peer->getNativeHandle()) : nullptr;
    auto* parent = hwnd != nullptr ? GetParent (hwnd) : nullptr;
    if (parent == nullptr || ! isAttached (c))
        return true; // (no native window to move)
    const auto want = physicalRect (c, *peer, boundsInArea + originOf (area));
    const auto now = rectInParent (hwnd, parent);
    return now.left == want.left && now.top == want.top && now.right == want.right && now.bottom == want.bottom;
   #else
    juce::ignoreUnused (c, area, boundsInArea);
    return true;
   #endif
}

juce::Rectangle<int> boundsIn (const juce::Component& c, const juce::Component&)
{
    return isAttached (c) ? c.getBounds() - placedOrigin (c) : c.getBounds();
}

bool reattachIfNeeded (juce::Component& c, juce::Component& area)
{
    if (! isAttached (c))
        return false;
    const auto parent = (juce::pointer_sized_int) (juce::int64) c.getProperties().getWithDefault ("lynxNativeParent", (juce::int64) 0);
    if (parent == (juce::pointer_sized_int) topHandle (area))
        return false;
    const auto visible = c.isVisible();
    detach (c);
    if (! attach (c, area))
        return false;
    c.setVisible (visible);
    return true;
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

void followParentScale (juce::Component& area)
{
    for (auto* c : attachedChildrenOf (topHandle (area)))
    {
        placePhysically (*c);
        if (auto* peer = c->getPeer())
            PeerScaleListeners::notify (*peer, peer->getPlatformScaleFactor());
        c->repaint();
    }
}

std::unique_ptr<juce::Component> makeOverlayHost (std::unique_ptr<juce::Component>& content, juce::Component& over)
{
    if (content == nullptr || topHandle (over) == nullptr || content->getParentComponent() != nullptr)
        return nullptr;
    auto host = std::make_unique<OverlayHost> (std::move (content), over);
    host->setBounds (over.getLocalBounds());
    if (! attach (*host, over))
    {
        content = host->release(); // (back to the caller, as it was)
        return nullptr;
    }
    host->setVisible (true);
    host->toFront (true);
    return host;
}

void notifyScaleListenersForTest (juce::Component& top)
{
    if (auto* peer = top.getPeer())
        PeerScaleListeners::notify (*peer, peer->getPlatformScaleFactor());
}

bool placedPhysically (const juce::Component& c)
{
   #if JUCE_WINDOWS
    auto* peer = c.getPeer();
    auto* hwnd = peer != nullptr ? static_cast<HWND> (peer->getNativeHandle()) : nullptr;
    auto* parent = hwnd != nullptr ? GetParent (hwnd) : nullptr;
    if (parent == nullptr)
        return false;
    const auto scale = peer->getPlatformScaleFactor() * (double) c.getDesktopScaleFactor();
    const auto b = c.getBounds().toDouble() * scale;
    RECT now {};
    GetWindowRect (hwnd, &now);
    MapWindowPoints (nullptr, parent, reinterpret_cast<POINT*> (&now), 2);
    return std::abs (now.left - b.getX()) <= 1.0 && std::abs (now.top - b.getY()) <= 1.0 && std::abs (now.right - b.getRight()) <= 1.0
           && std::abs (now.bottom - b.getBottom()) <= 1.0;
   #else
    juce::ignoreUnused (c);
    return true;
   #endif
}

std::vector<juce::Component*> attachedChildren (void* parentWindow)
{
    return attachedChildrenOf (parentWindow);
}

void raiseOverlays()
{
    for (auto* list : { &floaters(), &overlays() })
        for (auto& o : *list)
            if (o != nullptr && o->isVisible() && isAttached (*o))
                o->toFront (false);
}

bool supportsFloaters()
{
   #if JUCE_WINDOWS || JUCE_MAC
    return true;
   #else
    return false;
   #endif
}

void addFloater (juce::Component& c)
{
    auto& list = floaters();
    dropDeleted (list, &c);
    list.emplace_back (&c);
}

void removeFloater (juce::Component& c)
{
    dropDeleted (floaters(), &c);
}

void keepFloatersInFront()
{
   #if JUCE_WINDOWS
    dropDeleted (floaters());
    dropDeleted (overlays());
    std::vector<HWND> front;
    for (auto* list : { &floaters(), &overlays() })
        for (auto& o : *list)
            if (o->isVisible() && isAttached (*o))
                if (auto* h = static_cast<HWND> (o->getWindowHandle()))
                    front.push_back (h);
    for (auto* h : front)
        for (auto above = GetWindow (h, GW_HWNDPREV); above != nullptr; above = GetWindow (above, GW_HWNDPREV))
            if (IsWindowVisible (above) && std::find (front.begin(), front.end(), above) == front.end())
            {
                raiseOverlays();
                return;
            }
   #endif
}

void raiseOnAnyClick (juce::Component& c, std::function<void()> onClick)
{
   #if JUCE_WINDOWS
    auto& list = clickRaises();
    std::erase_if (list, [&c] (const ClickRaise& r) { return r.component == nullptr || r.component.getComponent() == &c; });
    list.push_back ({ &c, std::move (onClick) });
    installClickHook();
   #else
    juce::ignoreUnused (c, onClick);
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

juce::Rectangle<int> nativeRectInParent (const juce::Component& c)
{
   #if JUCE_WINDOWS
    auto* hwnd = static_cast<HWND> (c.getWindowHandle());
    auto* parent = hwnd != nullptr ? GetParent (hwnd) : nullptr;
    if (parent == nullptr)
        return {};
    const auto r = rectInParent (hwnd, parent);
    return { (int) r.left, (int) r.top, (int) (r.right - r.left), (int) (r.bottom - r.top) };
   #else
    juce::ignoreUnused (c);
    return {};
   #endif
}

juce::Rectangle<int> nativeParentClientSize (const juce::Component& c)
{
   #if JUCE_WINDOWS
    auto* hwnd = static_cast<HWND> (c.getWindowHandle());
    auto* parent = hwnd != nullptr ? GetParent (hwnd) : nullptr;
    if (parent == nullptr)
        return {};
    RECT r {};
    GetClientRect (parent, &r);
    return { 0, 0, (int) (r.right - r.left), (int) (r.bottom - r.top) };
   #else
    juce::ignoreUnused (c);
    return {};
   #endif
}

juce::Rectangle<int> nativeScreenRect (void* window)
{
   #if JUCE_WINDOWS
    if (window == nullptr)
        return {};
    RECT r {};
    GetWindowRect (static_cast<HWND> (window), &r);
    return { (int) r.left, (int) r.top, (int) (r.right - r.left), (int) (r.bottom - r.top) };
   #else
    juce::ignoreUnused (window);
    return {};
   #endif
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

void* windowAt (const juce::Component& c, juce::Point<int> localPoint)
{
   #if JUCE_WINDOWS
    auto* peer = c.getPeer();
    auto* hwnd = peer != nullptr ? static_cast<HWND> (peer->getNativeHandle()) : nullptr;
    if (hwnd == nullptr)
        return nullptr;
    const auto scale = peer->getPlatformScaleFactor() * (double) c.getDesktopScaleFactor();
    const auto inPeer = peer->getComponent().getLocalPoint (&c, localPoint).toDouble() * scale;
    POINT p { juce::roundToInt (inPeer.x), juce::roundToInt (inPeer.y) };
    ClientToScreen (hwnd, &p);
    return (void*) WindowFromPoint (p);
   #else
    juce::ignoreUnused (c, localPoint);
    return nullptr;
   #endif
}

int hitCodeAt (const juce::Component& c, juce::Point<int> localPoint)
{
   #if JUCE_WINDOWS
    auto* peer = c.getPeer();
    auto* hwnd = peer != nullptr ? static_cast<HWND> (peer->getNativeHandle()) : nullptr;
    if (hwnd == nullptr)
        return 0;
    const auto scale = peer->getPlatformScaleFactor() * (double) c.getDesktopScaleFactor();
    POINT p { juce::roundToInt ((localPoint.x + 0.5) * scale), juce::roundToInt ((localPoint.y + 0.5) * scale) };
    ClientToScreen (hwnd, &p);
    return (int) SendMessage (hwnd, WM_NCHITTEST, 0, MAKELPARAM (p.x, p.y));
   #else
    juce::ignoreUnused (c, localPoint);
    return 0;
   #endif
}

void* innermostChild (void* window)
{
   #if JUCE_WINDOWS
    auto h = static_cast<HWND> (window);
    for (int depth = 0; h != nullptr && depth < 24; ++depth)
    {
        const auto child = GetWindow (h, GW_CHILD);
        if (child == nullptr)
            break;
        h = child;
    }
    return h != static_cast<HWND> (window) ? (void*) h : nullptr;
   #else
    juce::ignoreUnused (window);
    return nullptr;
   #endif
}

void postClickForTest (void* window, juce::Point<int> clientPoint)
{
   #if JUCE_WINDOWS
    auto h = static_cast<HWND> (window);
    DWORD pid = 0;
    GetWindowThreadProcessId (h, &pid);
    if (h == nullptr || pid != GetCurrentProcessId())
        return;
    const auto at = MAKELPARAM (clientPoint.x, clientPoint.y);
    PostMessageW (h, WM_LBUTTONDOWN, MK_LBUTTON, at);
    PostMessageW (h, WM_LBUTTONUP, 0, at);
   #else
    juce::ignoreUnused (window, clientPoint);
   #endif
}

bool isLayered (void* window)
{
   #if JUCE_WINDOWS
    return window != nullptr && (GetWindowLongPtr (static_cast<HWND> (window), GWL_EXSTYLE) & WS_EX_LAYERED) != 0;
   #else
    juce::ignoreUnused (window);
    return false;
   #endif
}
} // namespace lynx::nativechild

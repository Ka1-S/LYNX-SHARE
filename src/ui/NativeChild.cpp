#include "ui/NativeChild.h"
#include "ui/WindowOwner.h"

#include <algorithm>
#include <cmath>
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

// JUCE places a child window from its logical bounds with the scale of the display at that point taken as a screen
// point, which is not the parent's when the main window is on another display: put it at its bounds in the parent's own
// scale (JUCE reads the child's bounds back that way: client pixels / the parent's scale).
void placePhysically (juce::Component& c)
{
   #if JUCE_WINDOWS
    auto* peer = c.getPeer();
    auto* hwnd = peer != nullptr ? static_cast<HWND> (peer->getNativeHandle()) : nullptr;
    auto* parent = hwnd != nullptr ? GetParent (hwnd) : nullptr;
    if (parent == nullptr)
        return;
    // (component bounds are in LYNX's zoomed units: times the UI zoom = the peer's logical pixels, times the display's
    // scale = physical)
    const auto scale = peer->getPlatformScaleFactor() * (double) c.getDesktopScaleFactor();
    const auto b = c.getBounds().toDouble() * scale;
    const auto x = juce::roundToInt (b.getX()), y = juce::roundToInt (b.getY());
    const auto w = juce::roundToInt (b.getRight()) - x, h = juce::roundToInt (b.getBottom()) - y;
    RECT now {};
    GetWindowRect (hwnd, &now);
    MapWindowPoints (nullptr, parent, reinterpret_cast<POINT*> (&now), 2);
    if (now.left == x && now.top == y && now.right - now.left == w && now.bottom - now.top == h)
        return;
    SetWindowPos (hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
   #else
    juce::ignoreUnused (c);
   #endif
}

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
    placePhysically (c);
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
    for (auto& o : overlays())
        if (o != nullptr && o->isVisible() && isAttached (*o))
            o->toFront (false);
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

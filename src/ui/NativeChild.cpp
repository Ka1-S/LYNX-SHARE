#include "ui/NativeChild.h"
#include "ui/WindowOwner.h"
#include "ui/ShotShowing.h"
#include "core/DockStack.h"
#include "core/DockingPolicy.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
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
// pixels (times the UI zoom = the peer's logical pixels, times the display's scale = physical).
// Issue #178 (round 190hx): the position and the size are rounded each on its own, as JUCE does for its own windows
// (Rectangle::toNearestInt). Rounding the right and bottom EDGES made the size depend on the position at a scale that is
// not a whole number (150 %: a 101-unit-wide window was 152 pixels at x = 0 and 151 at x = 1), so every step of a drag
// also resized the native window: Windows then showed its Direct2D swap chain's new, undrawn buffers (other programs'
// old pictures, white stripes) until the next frame, a rapid flicker while a panel or a docked window moved.
#if JUCE_WINDOWS
RECT physicalRect (const juce::Component& c, const juce::ComponentPeer& peer, juce::Rectangle<int> bounds)
{
    const auto scale = peer.getPlatformScaleFactor() * (double) c.getDesktopScaleFactor();
    const auto r = dockstack::physicalPlace (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), scale);
    return { r.x, r.y, r.x + r.w, r.y + r.h };
}

// Issue #197 (round 190kl): `want` keeps the native window's present size when that is within a pixel of the exact
// size (core/DockStack.h keepsSize): placing a window again only moves it
void keepNativeSize (const juce::Component& c, const juce::ComponentPeer& peer, juce::Rectangle<int> bounds, RECT now, RECT& want)
{
    const auto scale = peer.getPlatformScaleFactor() * (double) c.getDesktopScaleFactor();
    const auto w = (int) (now.right - now.left), h = (int) (now.bottom - now.top);
    if (dockstack::keepsSize (w, h, bounds.getWidth(), bounds.getHeight(), scale))
    {
        want.right = want.left + w;
        want.bottom = want.top + h;
    }
}

// LYNX's own exact placement is under way (placePhysically): the window procedure leaves its position alone
bool placingExactly = false;

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
    auto want = physicalRect (c, *peer, bounds);
    const auto now = rectInParent (hwnd, parent);
    keepNativeSize (c, *peer, bounds, now, want); // (issue #197: a move is never also a resize)
    if (now.left == want.left && now.top == want.top && now.right == want.right && now.bottom == want.bottom)
        return;
    const juce::ScopedValueSetter<bool> exact (placingExactly, true); // (this place stands: placeAtOnce leaves it)
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
// Round 190jy: told after a native child moved, was resized, shown, hidden or restacked (ui/NativeChild.h)
std::function<void()>& stackingListener()
{
    static std::function<void()> listener;
    return listener;
}

void stackingChanged()
{
    if (auto& l = stackingListener(); l != nullptr)
        l();
}
#endif

#if JUCE_WINDOWS
// Every attached child window goes through one window procedure in front of JUCE's (one per window, so the original is
// put back in the right order when the window goes):
// - Round 190cj: a transparent child window (a dancer) takes the mouse wherever its picture is not fully transparent:
//   the system decides that from the pixels, before JUCE's hitTest is asked, and a click JUCE then finds no component
//   for is simply lost. So the window answers the system's own hit test: where the component's hitTest says no (FL's
//   "Solid" off, the dancer's arms and reflection) it is "transparent", and the click goes to the window under it.
// - Issue #161: JUCE answers WM_MOUSEACTIVATE from the clicked window's own component, and a panel (the Playlist, the
//   Piano roll...) does not take the keyboard focus on a click, so JUCE said "don't activate": back from another app,
//   a click in a panel left LYNX's main window inactive and Space went nowhere. clickActivatesWindow() decides it.
struct Subclassed
{
    WNDPROC original = nullptr;
    juce::Component::SafePointer<juce::Component> component;
    bool clickThrough = false;
    bool focusAfterActivation = false; // (activated by this click: the main window gets the system focus at its button-down)
};

std::map<HWND, Subclassed>& subclassedWindows()
{
    static std::map<HWND, Subclassed> windows;
    return windows;
}

// Round 190jy: the holes each attached child window has now (its window region), physical pixels in that window
std::map<HWND, std::vector<juce::Rectangle<int>>>& holesNow()
{
    static std::map<HWND, std::vector<juce::Rectangle<int>>> holes;
    return holes;
}

bool pretendInactive = false; // (tests: the window answers as when LYNX is in the background)

// the top-level window the child belongs to is the foreground window (the active one of the active app)
bool rootIsForeground (HWND hwnd)
{
    if (pretendInactive)
        return false;
    auto* root = GetAncestor (hwnd, GA_ROOT);
    return root != nullptr && GetForegroundWindow() == root;
}

bool isButtonDownNotice (WORD event) noexcept
{
    return event == WM_LBUTTONDOWN || event == WM_RBUTTONDOWN || event == WM_MBUTTONDOWN || event == WM_XBUTTONDOWN || event == WM_POINTERDOWN;
}

void raiseForForeignClick (HWND frame, LPARAM pointInFrame);

// Round 190he (issue #152, "the windows still flicker"): JUCE moves a child window to a place it works out with the
// scale of the display at that point taken as a SCREEN point, and with SWP_FRAMECHANGED (the whole window redrawn) on
// every move; LYNX then put it at its real place in the parent (placePhysically). With a display scale other than 100 %
// every move or resize of a panel or docked window showed it first at the wrong place, then at the right one: a flash
// on each step of a drag. The window now goes straight to its place in the parent's own pixels (the component's bounds
// are already the new ones when JUCE asks), without the frame change (a child window has no frame to recalculate).
void placeAtOnce (juce::Component& c, WINDOWPOS& pos)
{
    if (placingExactly || ((pos.flags & SWP_NOMOVE) != 0 && (pos.flags & SWP_NOSIZE) != 0))
        return;
    auto* peer = c.getPeer();
    if (peer == nullptr || ! isAttached (c) || GetParent (pos.hwnd) == nullptr || (GetWindowLongPtr (pos.hwnd, GWL_STYLE) & WS_CHILD) == 0)
        return;
    auto want = physicalRect (c, *peer, c.getBounds());
    if (auto* parent = GetParent (pos.hwnd))
        keepNativeSize (c, *peer, c.getBounds(), rectInParent (pos.hwnd, parent), want); // (issue #197)
    if ((pos.flags & SWP_NOMOVE) == 0)
    {
        pos.x = want.left;
        pos.y = want.top;
    }
    if ((pos.flags & SWP_NOSIZE) == 0)
    {
        pos.cx = want.right - want.left;
        pos.cy = want.bottom - want.top;
    }
    pos.flags &= ~(UINT) SWP_FRAMECHANGED;
}

LRESULT CALLBACK childProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto& windows = subclassedWindows();
    const auto it = windows.find (hwnd);
    if (it == windows.end())
        return DefWindowProc (hwnd, message, wParam, lParam);
    const auto original = it->second.original;
    auto* c = it->second.component.getComponent();
    if (message == WM_NCHITTEST && it->second.clickThrough)
    {
        if (c != nullptr)
            if (auto* peer = c->getPeer())
            {
                POINT p { (int) (short) LOWORD (lParam), (int) (short) HIWORD (lParam) };
                ScreenToClient (hwnd, &p);
                const auto scale = juce::jmax (0.01, peer->getPlatformScaleFactor() * (double) c->getDesktopScaleFactor());
                if (! c->hitTest (juce::roundToInt (std::floor (p.x / scale)), juce::roundToInt (std::floor (p.y / scale))))
                    return HTTRANSPARENT;
            }
    }
    else if ((message == WM_MOUSEACTIVATE || message == WM_POINTERACTIVATE) && c != nullptr
             && ! c->getMouseClickGrabsKeyboardFocus() && clickActivatesWindow (*c, rootIsForeground (hwnd)))
    {
        it->second.focusAfterActivation = true;
        return MA_ACTIVATE; // (the system activates the main window; JUCE would have said MA_NOACTIVATE)
    }
    else if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN
             || message == WM_POINTERDOWN)
    {
        // the main window is active now; the system normally gave it the focus too (JUCE then hands it back to the
        // control that had it, or the main window takes it: MainComponent::shouldReclaimKeyboardFocus). If nothing in it
        // has the focus, the keys would only reach it as "system" keys: the focus goes to the main window itself.
        if (std::exchange (it->second.focusAfterActivation, false))
            if (auto* root = GetAncestor (hwnd, GA_ROOT); root != nullptr && GetForegroundWindow() == root)
                if (auto* focus = GetFocus(); focus == nullptr || GetAncestor (focus, GA_ROOT) != root)
                    SetFocus (root);
    }
    else if (message == WM_WINDOWPOSCHANGING && c != nullptr && lParam != 0)
        placeAtOnce (*c, *reinterpret_cast<WINDOWPOS*> (lParam));
    else if (message == WM_PARENTNOTIFY && c != nullptr && isButtonDownNotice (LOWORD (wParam)))
        raiseForForeignClick (hwnd, lParam);
    else if (message == WM_WINDOWPOSCHANGED && lParam != 0)
    {
        // Round 190jy: the holes of the windows below a docked editor drawn by the main window follow every move,
        // resize, show, hide and restacking of the main window's native children (not the region changes themselves)
        const auto flags = reinterpret_cast<const WINDOWPOS*> (lParam)->flags;
        const auto result = CallWindowProc (original, hwnd, message, wParam, lParam);
        if ((flags & SWP_NOZORDER) == 0 || (flags & SWP_NOMOVE) == 0 || (flags & SWP_NOSIZE) == 0 || (flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW)) != 0)
            stackingChanged();
        return result;
    }
    else if (message == WM_NCDESTROY)
    {
        SetWindowLongPtr (hwnd, GWLP_WNDPROC, (LONG_PTR) original);
        windows.erase (it);
        holesNow().erase (hwnd);
    }
    return CallWindowProc (original, hwnd, message, wParam, lParam);
}

void subclass (juce::Component& c, bool clickThrough)
{
    auto* hwnd = static_cast<HWND> (c.getWindowHandle());
    if (hwnd == nullptr)
        return;
    auto& windows = subclassedWindows();
    if (const auto it = windows.find (hwnd); it != windows.end())
    {
        it->second.component = &c;
        it->second.clickThrough = it->second.clickThrough || clickThrough;
        return;
    }
    auto& entry = windows[hwnd];
    entry.component = &c;
    entry.clickThrough = clickThrough;
    entry.original = (WNDPROC) SetWindowLongPtr (hwnd, GWLP_WNDPROC, (LONG_PTR) childProc);
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
                    // (round 190jy: a component drawn by the main window has the main window's handle: it is no attached window)
                    return std::any_of (list.begin(), list.end(), [h] (const ClickRaise& r) { return isAttached (*r.component) && r.component->getWindowHandle() == (void*) h; });
                });
                if (at <= 0) // (none, or the component's own window: JUCE's mouse listeners take it)
                    break;
                for (const auto& r : list)
                    if (isAttached (*r.component) && r.component->getWindowHandle() == (void*) ancestry[(std::size_t) at])
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

// Round 190he (issue #152 again, 0.1.27: "no window can be put over the Channel rack"): the hook above sees only the
// windows of LYNX's UI thread. A plugin view made by another thread, or in another process (a bridged 32-bit plugin, a
// plugin run in its own process), gets its clicks from the system directly, so a click inside it never raised its
// docked window: the Channel rack (or any panel) clicked before stayed over it. Windows tells every ancestor of the
// clicked child window about a button press (WM_PARENTNOTIFY), whatever thread or process made the child: the docked
// window raises itself for those. Clicks in this thread's own windows stay with the hook (no second raise).
void raiseForForeignClick (HWND frame, LPARAM pointInFrame)
{
    POINT p { (int) (short) LOWORD (pointInFrame), (int) (short) HIWORD (pointInFrame) };
    ClientToScreen (frame, &p);
    auto* clicked = WindowFromPoint (p);
    if (clicked == nullptr || clicked == frame || ! IsChild (frame, clicked))
        return;
    if (! dockstack::clickNeedsParentNotice (GetWindowThreadProcessId (clicked, nullptr), GetCurrentThreadId()))
        return;
    auto& list = clickRaises();
    for (const auto& r : list)
        if (r.component != nullptr && r.component->getWindowHandle() == (void*) frame)
        {
            juce::MessageManager::callAsync ([safe = r.component] { raiseClicked (safe); });
            return;
        }
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

    // Round 190fc (0.1.27 RC H2): a key-down that nothing in an attached window used is posted on to the parent window by
    // JUCE (HWNDComponentPeer: WM_KEYDOWN -> doKeyDown false -> forwardMessageToParent). The main window's peer then
    // finds no WM_CHAR of its own and makes a key press of it for the focused component, which is still the one in this
    // window: its keys reached the shortcuts twice, so Space played and stopped at once ("Space does nothing" after a
    // click on a control of the Simple dock or title bar). The key press itself still comes here (the WM_CHAR, or
    // JUCE's own for keys without a character) and keyPressed forwards it once, so the key-down is taken as used.
    bool keyStateChanged (bool isKeyDown, juce::Component* origin) override
    {
       #if JUCE_WINDOWS
        return isKeyDown && origin != nullptr && isAttached (*origin);
       #else
        juce::ignoreUnused (isKeyDown, origin);
        return false;
       #endif
    }
};

KeyForwarder& keyForwarder()
{
    static KeyForwarder forwarder;
    return forwarder;
}
} // namespace

bool dockingEnabled()
{
    // Issue #165 (Linux, Wayland desktop through XWayland, and LYNX's own private X server): an X11 child window in the
    // main window takes no keyboard focus for its text fields (the SoundFont's preset search box typed nothing), a
    // closed or moved one leaves its picture behind until something else repaints there, and it has no frame of its
    // own. The docking glue (click raise, placement, stacking checks) is written for Win32 only, so on Linux and macOS
    // plugin and channel windows are the system's own windows ("Detached", as before issue #116) unless
    // LYNX_DOCKED_PLUGINS=1 (core/DockingPolicy.h).
    static const bool on = [] {
        const auto value = juce::SystemStats::getEnvironmentVariable ("LYNX_DOCKED_PLUGINS", {}).trim().toStdString();
       #if JUCE_WINDOWS
        return dockpolicy::dockingOn (value, true);
       #else
        return dockpolicy::dockingOn (value, false);
       #endif
    }();
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
    subclass (c, ! c.isOpaque()); // (not opaque: a floater, with per-pixel transparency)
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
    auto want = physicalRect (c, *peer, boundsInArea + originOf (area));
    const auto now = rectInParent (hwnd, parent);
    keepNativeSize (c, *peer, boundsInArea + originOf (area), now, want); // (issue #197)
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
    // (the right and bottom edges: the position's rounding plus a kept size within a pixel, round 190kl)
    return std::abs (now.left - b.getX()) <= 1.0 && std::abs (now.top - b.getY()) <= 1.0 && std::abs (now.right - b.getRight()) < 1.5
           && std::abs (now.bottom - b.getBottom()) < 1.5;
   #else
    juce::ignoreUnused (c);
    return true;
   #endif
}

namespace
{
bool coverCheckWanted()
{
    static const bool on = juce::SystemStats::getEnvironmentVariable ("LYNX_COVERCHECK", {}).trim() != "0";
    return on;
}

#if JUCE_WINDOWS
bool occlusionCheckWanted()
{
    static const bool on = juce::SystemStats::getEnvironmentVariable ("LYNX_OCCLUSION", {}).trim() != "0";
    return on;
}

// a window's rectangle as it shows (DWM's frame bounds: without Windows 10/11's invisible resize borders)
RECT shownRect (HWND w)
{
    using GetAttribute = HRESULT (WINAPI*) (HWND, DWORD, PVOID, DWORD);
    static const auto getAttribute = [] {
        auto* dwm = LoadLibraryW (L"dwmapi.dll");
        return dwm != nullptr ? (GetAttribute) (void*) GetProcAddress (dwm, "DwmGetWindowAttribute") : nullptr;
    }();
    RECT r {};
    if (getAttribute == nullptr || FAILED (getAttribute (w, 9 /* DWMWA_EXTENDED_FRAME_BOUNDS */, &r, sizeof (r))))
        GetWindowRect (w, &r);
    return r;
}

bool cloaked (HWND w)
{
    using GetAttribute = HRESULT (WINAPI*) (HWND, DWORD, PVOID, DWORD);
    static const auto getAttribute = [] {
        auto* dwm = LoadLibraryW (L"dwmapi.dll");
        return dwm != nullptr ? (GetAttribute) (void*) GetProcAddress (dwm, "DwmGetWindowAttribute") : nullptr;
    }();
    DWORD value = 0;
    return getAttribute != nullptr && SUCCEEDED (getAttribute (w, 14 /* DWMWA_CLOAKED */, &value, sizeof (value))) && value != 0;
}

// Round 190la: a top-level window (the main window, a detached panel, a plugin window) whose visible part (on its
// monitor) lies wholly under ONE visible, opaque top-level window of any program above it in the stacking order (a
// maximised browser over LYNX while a song plays): nothing it paints reaches the screen, so its live views need not step
// and its GPU playhead need not be committed (the frame steps resume within the idle poll's 50 ms when it shows again).
// Windows that may let it show through never count: layered or click-through windows (overlays, translucent terminals),
// windows without a redirection surface (DirectComposition content may be translucent), cloaked windows (other virtual
// desktops, hidden store apps), minimised ones, and windows with a region (a hole). Message thread; the answer is kept
// for 8 ms (every ticker of a frame asks about the same window). LYNX_OCCLUSION=0: never (A/B).
bool occludedByOtherWindows (HWND top)
{
    if (top == nullptr || ! occlusionCheckWanted() || IsIconic (top))
        return false;
    static HWND lastTop = nullptr;
    static double lastMs = -1.0e9;
    static bool lastAnswer = false;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (top == lastTop && now - lastMs < 8.0)
        return lastAnswer;

    auto answer = false;
    auto own = shownRect (top);
    MONITORINFO mi {};
    mi.cbSize = sizeof (mi);
    if (auto* monitor = MonitorFromWindow (top, MONITOR_DEFAULTTONULL); monitor != nullptr && GetMonitorInfoW (monitor, &mi))
    {
        // (a window across two monitors: only the monitor it is mostly on is looked at if it lies wholly there)
        RECT onMonitor {};
        if (IntersectRect (&onMonitor, &own, &mi.rcMonitor) && EqualRect (&onMonitor, &own))
            own = onMonitor;
    }
    if (own.right > own.left && own.bottom > own.top)
    {
        auto steps = 0;
        for (auto above = GetWindow (top, GW_HWNDPREV); above != nullptr && ++steps < 400; above = GetWindow (above, GW_HWNDPREV))
        {
            if (! IsWindowVisible (above) || IsIconic (above))
                continue;
            const auto ex = GetWindowLongPtr (above, GWL_EXSTYLE);
            if ((ex & (WS_EX_LAYERED | WS_EX_TRANSPARENT | 0x00200000L /* WS_EX_NOREDIRECTIONBITMAP */)) != 0)
                continue;
            const auto r = shownRect (above);
            if (r.left > own.left || r.top > own.top || r.right < own.right || r.bottom < own.bottom)
                continue;
            if (cloaked (above))
                continue;
            if (auto* region = CreateRectRgn (0, 0, 0, 0))
            {
                const auto type = GetWindowRgn (above, region);
                DeleteObject (region);
                if (type != ERROR)
                    continue;
            }
            answer = true;
            break;
        }
    }
    lastTop = top;
    lastMs = now;
    lastAnswer = answer;
    return answer;
}
#endif
} // namespace

std::atomic<int>& coveredSkips()
{
    static std::atomic<int> n { 0 };
    return n;
}

bool coveredBySiblingWindows (const juce::Component& c)
{
   #if JUCE_WINDOWS
    if (! coverCheckWanted())
        return false;
    auto* peer = c.getPeer();
    auto* hwnd = peer != nullptr ? static_cast<HWND> (peer->getNativeHandle()) : nullptr;
    if (hwnd == nullptr)
        return false;
    // round 190la: the top-level window it is in, wholly under another program's window (or another LYNX window)
    if ((GetWindowLongPtr (hwnd, GWL_STYLE) & WS_CHILD) == 0)
        return occludedByOtherWindows (hwnd);
    // what of the window can show: its rectangle, clipped by each parent's client area on the way up (screen pixels)
    RECT visible {};
    if (! GetWindowRect (hwnd, &visible))
        return false;
    for (auto w = hwnd; (GetWindowLongPtr (w, GWL_STYLE) & WS_CHILD) != 0;)
    {
        const auto parent = GetParent (w);
        if (parent == nullptr)
            return false;
        RECT client {};
        GetClientRect (parent, &client);
        MapWindowPoints (parent, nullptr, reinterpret_cast<POINT*> (&client), 2);
        if (! IntersectRect (&visible, &visible, &client))
            return true; // (scrolled or moved out of its parent: nothing shows)
        // a visible sibling above `w` that holds all of it: an opaque window (not layered: the floaters and the tour's
        // dimmed layer let what is under them show) with no region of its own (a region can leave a hole: round 190jy's
        // docked editors drawn by the main window)
        for (auto above = GetWindow (w, GW_HWNDPREV); above != nullptr; above = GetWindow (above, GW_HWNDPREV))
        {
            if (! IsWindowVisible (above) || (GetWindowLongPtr (above, GWL_EXSTYLE) & (WS_EX_LAYERED | WS_EX_TRANSPARENT)) != 0)
                continue;
            RECT r {};
            if (! GetWindowRect (above, &r) || r.left > visible.left || r.top > visible.top || r.right < visible.right || r.bottom < visible.bottom)
                continue;
            if (auto* region = CreateRectRgn (0, 0, 0, 0))
            {
                const auto type = GetWindowRgn (above, region);
                DeleteObject (region);
                if (type != ERROR)
                    continue; // (it has a region: perhaps a hole over us)
            }
            return true;
        }
        w = parent;
        if ((GetWindowLongPtr (w, GWL_STYLE) & WS_CHILD) == 0)
            return occludedByOtherWindows (w); // (round 190la)
    }
    return false;
   #else
    juce::ignoreUnused (c);
    return false;
   #endif
}

juce::String describeCover (const juce::Component& c)
{
   #if JUCE_WINDOWS
    auto* peer = c.getPeer();
    auto* hwnd = peer != nullptr ? static_cast<HWND> (peer->getNativeHandle()) : nullptr;
    if (hwnd == nullptr)
        return "no window";
    const auto rectText = [] (const RECT& r) { return juce::String (r.left) + "," + juce::String (r.top) + " " + juce::String (r.right - r.left) + "x" + juce::String (r.bottom - r.top); };
    RECT visible {};
    GetWindowRect (hwnd, &visible);
    juce::String out;
    out << "window " << juce::String::toHexString ((juce::pointer_sized_int) hwnd) << " " << rectText (visible)
        << ((GetWindowLongPtr (hwnd, GWL_STYLE) & WS_CHILD) != 0 ? " child" : " top-level") << (IsWindowVisible (hwnd) ? "" : " INVISIBLE")
        << " showing " << (c.isShowing() ? 1 : 0) << " covered " << (coveredBySiblingWindows (c) ? 1 : 0);
    for (auto w = hwnd; (GetWindowLongPtr (w, GWL_STYLE) & WS_CHILD) != 0;)
    {
        const auto parent = GetParent (w);
        if (parent == nullptr)
            break;
        RECT client {};
        GetClientRect (parent, &client);
        MapWindowPoints (parent, nullptr, reinterpret_cast<POINT*> (&client), 2);
        IntersectRect (&visible, &visible, &client);
        out << " | in parent " << juce::String::toHexString ((juce::pointer_sized_int) parent) << " visible " << rectText (visible) << ", above:";
        for (auto above = GetWindow (w, GW_HWNDPREV); above != nullptr; above = GetWindow (above, GW_HWNDPREV))
        {
            if (! IsWindowVisible (above))
                continue;
            RECT r {};
            GetWindowRect (above, &r);
            const auto ex = GetWindowLongPtr (above, GWL_EXSTYLE);
            auto type = ERROR;
            if (auto* region = CreateRectRgn (0, 0, 0, 0))
            {
                type = GetWindowRgn (above, region);
                DeleteObject (region);
            }
            wchar_t cls[64] {};
            GetClassNameW (above, cls, 63);
            out << " [" << juce::String (cls).substring (0, 24) << " " << rectText (r) << ((ex & WS_EX_LAYERED) != 0 ? " layered" : "")
                << ((ex & WS_EX_TRANSPARENT) != 0 ? " transparent" : "") << (type != ERROR ? " region " + juce::String ((int) type) : juce::String())
                << (r.left <= visible.left && r.top <= visible.top && r.right >= visible.right && r.bottom >= visible.bottom ? " HOLDS" : "") << "]";
        }
        w = parent;
    }
    return out;
   #else
    return c.isShowing() ? "showing" : "not showing";
   #endif
}

bool showsOnScreen (const juce::Component& c)
{
    return (c.isShowing() || ui::showingForShots()) && ! coveredBySiblingWindows (c);
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
   #if JUCE_WINDOWS
    stackingChanged(); // (round 190jy: after every stacking change the callers make)
   #endif
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

void passInputThrough (juce::Component& c)
{
   #if JUCE_WINDOWS
    if (auto* hwnd = static_cast<HWND> (c.getWindowHandle()); hwnd != nullptr && isAttached (c))
        SetWindowLongPtr (hwnd, GWL_EXSTYLE, GetWindowLongPtr (hwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
   #else
    juce::ignoreUnused (c);
   #endif
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

bool clickActivatesWindow (const juce::Component& c, bool windowIsForeground)
{
    if (c.getMouseClickGrabsKeyboardFocus())
        return true; // (JUCE's own answer)
    if (c.isCurrentlyBlockedByAnotherModalComponent())
        return false; // (a modal dialog: JUCE decides, as before)
    return ! windowIsForeground;
}

int mouseActivateAnswerForTest (const juce::Component& c, bool asInactive)
{
   #if JUCE_WINDOWS
    auto* hwnd = static_cast<HWND> (c.getWindowHandle());
    auto* root = hwnd != nullptr ? GetAncestor (hwnd, GA_ROOT) : nullptr;
    if (root == nullptr)
        return 0;
    const auto was = std::exchange (pretendInactive, asInactive);
    const auto answer = (int) SendMessage (hwnd, WM_MOUSEACTIVATE, (WPARAM) root, MAKELPARAM (HTCLIENT, WM_LBUTTONDOWN));
    pretendInactive = was;
    if (const auto it = subclassedWindows().find (hwnd); it != subclassedWindows().end())
        it->second.focusAfterActivation = false; // (no button-down follows a question asked by a test)
    return answer;
   #else
    juce::ignoreUnused (c, asInactive);
    return 0;
   #endif
}

juce::String clickActivationSelfTest()
{
    // a panel: its clicks don't take the keyboard focus (the Playlist, the Piano roll: PanelFrame)
    juce::Component panel;
    panel.setWantsKeyboardFocus (false);
    panel.setMouseClickGrabsKeyboardFocus (false);
    panel.setOpaque (true);
    panel.setSize (300, 200);
    juce::Component field; // (a control that takes the focus on a click, as JUCE's components do by default)
    if (! clickActivatesWindow (panel, false))
        return "ERROR a click in a panel of an inactive window does not activate it";
    if (clickActivatesWindow (panel, true))
        return "ERROR a click in a panel of the active window activates it again (the focus would move)";
    if (! clickActivatesWindow (field, true) || ! clickActivatesWindow (field, false))
        return "ERROR a control that takes the focus on a click no longer activates its window";
    {
        juce::Component dialog;
        dialog.enterModalState (false);
        const auto blocked = clickActivatesWindow (panel, false);
        dialog.exitModalState (0);
        if (blocked)
            return "ERROR a click blocked by a modal dialog activates the window";
    }
   #if JUCE_WINDOWS
    if (dockingEnabled())
    {
        // the main window's stand-in: a native window, never shown (so it is never the foreground window either)
        juce::Component host;
        host.setOpaque (true);
        host.setBounds (100, 40, 640, 480);
        host.addToDesktop (juce::ComponentPeer::windowHasTitleBar);
        if (! attach (panel, host))
            return "ERROR no native child window";
        const auto inactive = mouseActivateAnswerForTest (panel, true);
        const auto hidden = mouseActivateAnswerForTest (panel, false); // (a hidden window is not the foreground one)
        detach (panel);
        if (inactive != MA_ACTIVATE || hidden != MA_ACTIVATE)
            return "ERROR WM_MOUSEACTIVATE answered " + juce::String (inactive) + "/" + juce::String (hidden) + " (MA_ACTIVATE = 1)";
    }
   #endif
    return "OK";
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

void stopForwardingUnusedKeys (juce::Component& c)
{
    c.removeKeyListener (&keyForwarder());
}

void stopRaiseOnAnyClick (juce::Component& c)
{
   #if JUCE_WINDOWS
    std::erase_if (clickRaises(), [&c] (const ClickRaise& r) { return r.component == nullptr || r.component.getComponent() == &c; });
   #else
    juce::ignoreUnused (c);
   #endif
}

void placeJustAbove (juce::Component& c, juce::Component* below)
{
   #if JUCE_WINDOWS
    auto* self = isAttached (c) ? static_cast<HWND> (c.getWindowHandle()) : nullptr;
    if (self == nullptr)
        return;
    constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    auto* under = below != nullptr && isAttached (*below) ? static_cast<HWND> (below->getWindowHandle()) : nullptr;
    if (under == nullptr)
    {
        SetWindowPos (self, HWND_BOTTOM, 0, 0, 0, 0, flags);
        return;
    }
    if (under == self)
        return;
    auto* above = GetWindow (under, GW_HWNDPREV);
    if (above == self)
        return; // (already there)
    SetWindowPos (self, above != nullptr ? above : HWND_TOP, 0, 0, 0, 0, flags); // (just below `above` = just above `under`)
   #else
    juce::ignoreUnused (c, below);
   #endif
}

juce::String editorHolesSelfTest()
{
   #if JUCE_WINDOWS
    if (! dockingEnabled())
        return "OK (not available: docking is off)";
    juce::Component host, lower, upper;
    host.setOpaque (true);
    host.setBounds (100, 40, 640, 480);
    host.addToDesktop (juce::ComponentPeer::windowHasTitleBar);
    for (auto* c : { &lower, &upper })
    {
        c->setOpaque (true);
        c->setBounds (10, 10, 300, 200);
        if (! attach (*c, host))
            return "ERROR no native child window";
        c->setVisible (true);
    }
    juce::String error;
    auto* hl = static_cast<HWND> (lower.getWindowHandle());
    placeJustAbove (upper, &lower);
    if (! isAbove (upper.getWindowHandle(), hl))
        error << "placeJustAbove: upper not above lower; ";
    placeJustAbove (upper, nullptr);
    if (isAbove (upper.getWindowHandle(), hl))
        error << "placeJustAbove (bottom): upper still above; ";
    const juce::Rectangle<int> hole (20, 30, 50, 40);
    setHoles (lower, { hole });
    if (holesOf (lower).size() != 1 || ! cutsInto (hl, hole) || ! cutsInto (hl, hole.withSize (5, 5)) || cutsInto (hl, { 100, 100, 10, 10 }))
        error << "the hole is not the window's region; ";
    {
        auto* region = CreateRectRgn (0, 0, 0, 0);
        const auto type = GetWindowRgn (hl, region);
        const auto inHole = PtInRegion (region, 30, 40) != FALSE, outside = PtInRegion (region, 5, 5) != FALSE;
        DeleteObject (region);
        if (type == ERROR || inHole || ! outside)
            error << "region type " << (int) type << " hole " << (int) inHole << " rest " << (int) outside << "; ";
    }
    setHoles (lower, { hole }); // (the same: nothing happens)
    setHoles (lower, {});
    if (! holesOf (lower).empty() || cutsInto (hl, hole))
        error << "the hole stayed; ";
    for (auto* c : { &upper, &lower })
        detach (*c);
    return error.isEmpty() ? juce::String ("OK") : "ERROR " + error.trim();
   #else
    return "OK (not available: Windows only)";
   #endif
}

void setStackingListener (std::function<void()> listener)
{
   #if JUCE_WINDOWS
    stackingListener() = std::move (listener);
   #else
    juce::ignoreUnused (listener);
   #endif
}

void setHoles (juce::Component& c, const std::vector<juce::Rectangle<int>>& holes)
{
   #if JUCE_WINDOWS
    auto* hwnd = isAttached (c) ? static_cast<HWND> (c.getWindowHandle()) : nullptr;
    if (hwnd == nullptr)
        return;
    auto& now = holesNow();
    const auto it = now.find (hwnd);
    if (it == now.end() ? holes.empty() : it->second == holes)
        return;
    if (holes.empty())
    {
        now.erase (hwnd);
        SetWindowRgn (hwnd, nullptr, TRUE);
        return;
    }
    RECT r {};
    GetWindowRect (hwnd, &r);
    auto* region = CreateRectRgn (0, 0, (int) (r.right - r.left), (int) (r.bottom - r.top));
    for (const auto& h : holes)
    {
        auto* hole = CreateRectRgn (h.getX(), h.getY(), h.getRight(), h.getBottom());
        CombineRgn (region, region, hole, RGN_DIFF);
        DeleteObject (hole);
    }
    now[hwnd] = holes;
    if (SetWindowRgn (hwnd, region, TRUE) == 0) // (the system owns the region once it is set)
    {
        DeleteObject (region);
        now.erase (hwnd);
    }
   #else
    juce::ignoreUnused (c, holes);
   #endif
}

std::vector<juce::Rectangle<int>> holesOf (const juce::Component& c)
{
   #if JUCE_WINDOWS
    auto* hwnd = isAttached (c) ? static_cast<HWND> (c.getWindowHandle()) : nullptr;
    const auto it = hwnd != nullptr ? holesNow().find (hwnd) : holesNow().end();
    return it != holesNow().end() ? it->second : std::vector<juce::Rectangle<int>> {};
   #else
    juce::ignoreUnused (c);
    return {};
   #endif
}

bool cutsInto (void* window, juce::Rectangle<int> rectInWindow)
{
   #if JUCE_WINDOWS
    auto* hwnd = static_cast<HWND> (window);
    if (hwnd == nullptr || rectInWindow.isEmpty())
        return false;
    auto* region = CreateRectRgn (0, 0, 0, 0);
    bool cut = false;
    if (const auto type = GetWindowRgn (hwnd, region); type != ERROR && type != NULLREGION)
    {
        RECT r {};
        GetWindowRect (hwnd, &r);
        const auto inside = rectInWindow.getIntersection ({ 0, 0, (int) (r.right - r.left), (int) (r.bottom - r.top) });
        if (! inside.isEmpty())
        {
            auto* wanted = CreateRectRgn (inside.getX(), inside.getY(), inside.getRight(), inside.getBottom());
            auto* outside = CreateRectRgn (0, 0, 0, 0);
            cut = CombineRgn (outside, wanted, region, RGN_DIFF) != NULLREGION;
            DeleteObject (outside);
            DeleteObject (wanted);
        }
    }
    DeleteObject (region);
    return cut;
   #else
    juce::ignoreUnused (window, rectInWindow);
    return false;
   #endif
}

juce::Rectangle<int> physicalInTop (const juce::Component& area, juce::Rectangle<int> boundsInArea)
{
    const auto* top = area.getTopLevelComponent();
    const auto* peer = top != nullptr ? top->getPeer() : nullptr;
    if (peer == nullptr)
        return {};
    const auto scale = peer->getPlatformScaleFactor() * (double) top->getDesktopScaleFactor();
    const auto inTop = top->getLocalArea (&area, boundsInArea);
    const auto r = dockstack::editorRect (inTop.getX(), inTop.getY(), inTop.getWidth(), inTop.getHeight(), scale);
    return { r.x, r.y, r.w, r.h };
}

juce::Rectangle<int> rectInParentWindow (const juce::Component& c)
{
   #if JUCE_WINDOWS
    auto* hwnd = isAttached (c) ? static_cast<HWND> (c.getWindowHandle()) : nullptr;
    auto* parent = hwnd != nullptr ? GetParent (hwnd) : nullptr;
    if (parent == nullptr)
        return {};
    const auto r = rectInParent (hwnd, parent);
    return juce::Rectangle<int>::leftTopRightBottom ((int) r.left, (int) r.top, (int) r.right, (int) r.bottom);
   #else
    juce::ignoreUnused (c);
    return {};
   #endif
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

bool pressSpaceForTest (void* fallback)
{
   #if JUCE_WINDOWS
    auto to = GetFocus();
    DWORD pid = 0;
    if (to != nullptr)
        GetWindowThreadProcessId (to, &pid);
    if (to == nullptr || pid != GetCurrentProcessId())
        to = static_cast<HWND> (fallback);
    if (to == nullptr)
        return false;
    const auto scan = MapVirtualKeyW (VK_SPACE, MAPVK_VK_TO_VSC);
    PostMessageW (to, WM_KEYDOWN, VK_SPACE, (LPARAM) (1u | (scan << 16)));
    PostMessageW (to, WM_KEYUP, VK_SPACE, (LPARAM) (1u | (scan << 16) | (1u << 30) | (1u << 31)));
    const auto until = GetTickCount64() + 120;
    MSG m;
    while (GetTickCount64() < until)
    {
        while (PeekMessageW (&m, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage (&m);
            DispatchMessageW (&m);
        }
        Sleep (5);
    }
    return true;
   #else
    juce::ignoreUnused (fallback);
    return false;
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

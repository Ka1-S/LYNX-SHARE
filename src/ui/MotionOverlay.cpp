// GPU overlay for moving marks (round 190ip): see MotionOverlay.h.

#include "ui/MotionOverlay.h"

#include "ui/ImageReadback.h"
#include "ui/NativeChild.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <typeinfo>
#include <utility>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <d3d11.h>
 #include <dxgi.h>
 #include <dxgi1_2.h>
 #include <dcomp.h>
#elif JUCE_MAC
 #include "ui/MotionOverlayMac.h"
#elif JUCE_LINUX || JUCE_BSD
 #include "ui/MotionOverlayX11.h"

#include <vector>

namespace juce
{
// cmake/LynxPatchJuce.cmake fix 41 (round 190jx), in juce_Windowing_linux.cpp; weak: without the fix they are null
extern void (*lynxX11AfterBlitHook) (unsigned long window, const RectangleList<int>& blitted) __attribute__ ((weak));
void* lynxX11Display() __attribute__ ((weak));
} // namespace juce
#endif

namespace lynx::ui
{
std::atomic<int>& MotionOverlay::framesShown()
{
    static std::atomic<int> n { 0 };
    return n;
}
std::atomic<int>& MotionOverlay::fallbacks()
{
    static std::atomic<int> n { 0 };
    return n;
}
std::atomic<int>& MotionOverlay::picturesPainted()
{
    static std::atomic<int> n { 0 };
    return n;
}

namespace
{
// the message-thread time show() takes (LYNX_OVERLAY_STATS), microseconds
std::atomic<long long>& showMicros()
{
    static std::atomic<long long> us { 0 };
    return us;
}

// round 190kr: the view of the last show() (LYNX_OVERLAY_STATS; message thread)
juce::Component::SafePointer<juce::Component>& lastOwner()
{
    static juce::Component::SafePointer<juce::Component> c;
    return c;
}

// round 190kr: show() calls whose view's window was wholly covered (nothing shown, nothing committed)
std::atomic<int>& coveredFrames()
{
    static std::atomic<int> n { 0 };
    return n;
}

struct ScopedTime
{
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    ~ScopedTime() { showMicros() += (long long) (1000.0 * (juce::Time::getMillisecondCounterHiRes() - t0)); }
};

bool overlayWanted()
{
    static const bool on = [] {
        const auto* v = std::getenv ("LYNX_OVERLAY");
        return v == nullptr || juce::String (v).trim() != "0";
    }();
    return on;
}

#if JUCE_WINDOWS
// Round 190lv: a live picture (the toolbar's output display) as a layered child window drawn with GDI, instead of a
// composition swap chain. Measured (Windows, Thief playing, per-thread cycles): each Present of the 190kz swap chain cost
// the GPU driver's thread about 1 M cycles, about half of that thread's work while the song plays (~140 of ~280 Mc/s);
// a layered window's new picture is handed to the window manager (UpdateLayeredWindow), with no Direct3D call in LYNX's
// process (FL Studio draws all of its UI with GDI: its driver thread does nothing). The pixels are the same picture,
// premultiplied as the swap chain took it. LYNX_LIVE_GDI=0: the swap chain, as in round 190kz.
bool liveGdiWanted()
{
    static const bool on = [] {
        const auto* v = std::getenv ("LYNX_LIVE_GDI");
        return v == nullptr || juce::String (v).trim() != "0";
    }();
    return on;
}

constexpr const wchar_t* liveGdiClass = L"LynxLivePicture";
constexpr const wchar_t* liveGdiProp = L"LynxLivePicture";

LRESULT CALLBACK liveGdiProc (HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
        case WM_NCHITTEST: return HTTRANSPARENT; // (the mouse goes to the view under it: its clicks and hints)
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        default: return DefWindowProcW (h, m, w, l);
    }
}

bool liveGdiClassReady()
{
    static const bool ok = [] {
        WNDCLASSEXW c {};
        c.cbSize = sizeof (c);
        c.lpfnWndProc = liveGdiProc;
        c.hInstance = GetModuleHandleW (nullptr);
        c.lpszClassName = liveGdiClass;
        return RegisterClassExW (&c) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    return ok;
}

// one live picture's window and its bitmap (message thread)
struct LiveGdiWindow
{
    HWND window = nullptr, parent = nullptr;
    HDC memory = nullptr;
    HBITMAP bitmap = nullptr, previous = nullptr;
    void* bits = nullptr;
    int bw = 0, bh = 0;
    RECT placed { 0, 0, 0, 0 }, shownClip { 0, 0, 0, 0 };
    bool visible = false, fresh = false; // fresh: the window shows the bitmap's pixels

    ~LiveGdiWindow() { destroy(); }

    void destroy()
    {
        if (window != nullptr)
        {
            RemovePropW (window, liveGdiProp);
            DestroyWindow (window);
        }
        window = nullptr;
        parent = nullptr;
        visible = false;
        placed = { 0, 0, 0, 0 };
        freeBitmap();
    }

    void freeBitmap()
    {
        if (memory != nullptr)
        {
            SelectObject (memory, previous);
            DeleteDC (memory);
        }
        if (bitmap != nullptr)
            DeleteObject (bitmap);
        memory = nullptr;
        bitmap = previous = nullptr;
        bits = nullptr;
        bw = bh = 0;
        fresh = false;
    }

    void hide()
    {
        if (window != nullptr && visible)
            ShowWindow (window, SW_HIDE);
        visible = false;
    }

    // the picture (premultiplied ARGB, w x h) shown with its top-left corner at (x, y) of the parent window's client
    // area, only the part inside `clip` (picture pixels)
    bool show (HWND parentWindow, const juce::Image& picture, int x, int y, juce::Rectangle<int> clip)
    {
        const auto w = picture.getWidth(), h = picture.getHeight();
        clip = clip.getIntersection ({ 0, 0, w, h });
        if (clip.isEmpty())
        {
            hide();
            return true;
        }
        if (window != nullptr && (parent != parentWindow || ! IsWindow (window)))
            destroy();
        if (window == nullptr)
        {
            if (! liveGdiClassReady())
                return false;
            window = CreateWindowExW (WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE, liveGdiClass, L"", WS_CHILD | WS_CLIPSIBLINGS,
                                      0, 0, 1, 1, parentWindow, nullptr, GetModuleHandleW (nullptr), nullptr);
            if (window == nullptr)
                return false;
            SetPropW (window, liveGdiProp, (HANDLE) 1);
            parent = parentWindow;
        }
        if (bitmap == nullptr || bw != w || bh != h)
        {
            freeBitmap();
            BITMAPINFO info {};
            info.bmiHeader.biSize = sizeof (info.bmiHeader);
            info.bmiHeader.biWidth = w;
            info.bmiHeader.biHeight = -h; // (top-down, as the image's rows)
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            bitmap = CreateDIBSection (nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            memory = CreateCompatibleDC (nullptr);
            if (bitmap == nullptr || memory == nullptr || bits == nullptr)
            {
                freeBitmap();
                return false;
            }
            previous = (HBITMAP) SelectObject (memory, bitmap);
            bw = w;
            bh = h;
        }
        auto changed = false;
        {
            // (a frame whose pixels are the ones shown already, at the same place, is not handed over again: no work for
            // the window manager either, e.g. the scope's flat line in a quiet passage)
            const juce::Image::BitmapData pixels (picture, juce::Image::BitmapData::readOnly);
            for (int row = 0; row < h; ++row)
            {
                auto* dst = static_cast<std::uint8_t*> (bits) + (std::size_t) row * (std::size_t) w * 4;
                if (changed || std::memcmp (dst, pixels.getLinePointer (row), (std::size_t) w * 4) != 0)
                {
                    std::memcpy (dst, pixels.getLinePointer (row), (std::size_t) w * 4);
                    changed = true;
                }
            }
        }
        const RECT want { x + clip.getX(), y + clip.getY(), x + clip.getRight(), y + clip.getBottom() };
        const RECT clip0 { clip.getX(), clip.getY(), clip.getRight(), clip.getBottom() };
        if (! changed && visible && fresh && EqualRect (&want, &placed) && EqualRect (&clip0, &shownClip))
            return true;
        GdiFlush();
        if (! EqualRect (&want, &placed))
        {
            SetWindowPos (window, HWND_TOP, want.left, want.top, want.right - want.left, want.bottom - want.top, SWP_NOACTIVATE | SWP_NOREDRAW);
            placed = want;
        }
        SIZE size { want.right - want.left, want.bottom - want.top };
        POINT from { clip.getX(), clip.getY() };
        BLENDFUNCTION blend { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        if (! UpdateLayeredWindow (window, nullptr, nullptr, &size, memory, &from, 0, &blend, ULW_ALPHA))
        {
            fresh = false;
            return false;
        }
        fresh = true;
        shownClip = clip0;
        if (! visible)
        {
            ShowWindow (window, SW_SHOWNA);
            visible = true;
        }
        return true;
    }
};
#endif

#if JUCE_LINUX || JUCE_BSD
bool hookInstalled(); // (below: fix 41's hook, which also counts JUCE's blits for the statistics)
#endif

// why the last show() fell back (LYNX_OVERLAY_STATS; Linux / macOS backend): a short reason, and the component that
// covered the mark when that was it
juce::String& lastFallback()
{
    static juce::String why; // (message thread)
    return why;
}

// LYNX_OVERLAY_STATS=<file>: per second, the frames the overlays showed, the show() calls that fell back, the pictures
// painted (every 2 s while something calls show()); X11: also JUCE's blits to the windows (presents) and their pixels
void writeStats()
{
    static const auto path = juce::SystemStats::getEnvironmentVariable ("LYNX_OVERLAY_STATS", {});
    if (path.isEmpty())
        return;
    static double since = 0.0;
    static int shown0 = 0, fell0 = 0, pictures0 = 0;
    static long long micros0 = 0;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (since <= 0.0)
        since = now;
    if (now - since < 2000.0)
        return;
    const auto s = (now - since) / 1000.0;
    const auto shown = MotionOverlay::framesShown().load(), fell = MotionOverlay::fallbacks().load(),
               pictures = MotionOverlay::picturesPainted().load();
    juce::String extra;
#if JUCE_LINUX || JUCE_BSD
    static x11overlay::BlitStats blits0;
    if (hookInstalled())
    {
        const auto b = x11overlay::blitStats();
        extra << " blits/s " << juce::String ((double) (b.blits - blits0.blits) / s, 1) << " blit_Mpx/s "
              << juce::String ((double) (b.pixels - blits0.pixels) / s / 1.0e6, 3);
        blits0 = b;
    }
#endif
    static int covered0 = 0;
    const auto covered = coveredFrames().load();
    extra << " covered/s " << juce::String ((covered - covered0) / s, 1);
    covered0 = covered;
    if (lastOwner() != nullptr) // (round 190kr: the last view that asked, and how the cover check saw its window)
        extra << " last view " << juce::String (typeid (*lastOwner()).name()) << ": " << nativechild::describeCover (*lastOwner());
    if (fell != fell0 && lastFallback().isNotEmpty())
        extra << " last fallback: " << lastFallback();
    juce::File (path).appendText (juce::String (now / 1000.0, 1) + " overlay frames/s " + juce::String ((shown - shown0) / s, 1)
                                  + " fallbacks/s " + juce::String ((fell - fell0) / s, 1) + " pictures " + juce::String (pictures - pictures0)
                                  + " show_ms/s " + juce::String ((double) (showMicros().load() - micros0) / 1000.0 / s, 2) + extra
                                  + juce::newLine);
    since = now;
    shown0 = shown;
    fell0 = fell;
    pictures0 = pictures;
    micros0 = showMicros().load();
}
} // namespace

#if JUCE_WINDOWS

namespace
{
template <typename T>
struct Com
{
    T* p = nullptr;
    Com() = default;
    Com (const Com&) = delete;
    Com& operator= (const Com&) = delete;
    Com (Com&& o) noexcept : p (std::exchange (o.p, nullptr)) {}
    Com& operator= (Com&& o) noexcept
    {
        if (this != &o)
        {
            reset();
            p = std::exchange (o.p, nullptr);
        }
        return *this;
    }
    ~Com() { reset(); }
    void reset()
    {
        if (p != nullptr)
            std::exchange (p, nullptr)->Release();
    }
    T** put()
    {
        reset();
        return &p;
    }
    T* operator->() const noexcept { return p; }
    explicit operator bool() const noexcept { return p != nullptr; }
};

// The Direct3D device the overlay pictures live on and the DirectComposition device that shows them. Made once, on a
// background thread the first time an overlay is asked for (so the first Play does not wait ~20-60 ms for a device);
// until then show() falls back. Lost (a driver reset): made again, at most a few times.
struct Devices final
{
    enum State { none, making, ready, failed };

    Com<ID3D11Device> d3d;
    Com<ID3D11DeviceContext> context;
    Com<IDCompositionDevice> dcomp;
    std::atomic<int> state { none };
    int remakes = 0;

    static Devices& get()
    {
        static Devices* d = new Devices(); // (never deleted: the views' overlays may go after any shutdown step)
        return *d;
    }

    bool make()
    {
        // (round 190la, A/B switch as JUCE's Direct2D device: cmake/LynxPatchJuce.cmake fix 43; LYNX_D3D_DRIVER_THREADS=0:
        // no driver threads for these frames; default as before)
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT
                           | (juce::SystemStats::getEnvironmentVariable ("LYNX_D3D_DRIVER_THREADS", {}) == "0" ? (UINT) D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS : 0u);
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
                                             D3D_FEATURE_LEVEL_9_3 };
        Com<ID3D11Device> device;
        Com<ID3D11DeviceContext> ctx;
        auto hr = D3D11CreateDevice (nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, (UINT) std::size (levels), D3D11_SDK_VERSION,
                                     device.put(), nullptr, ctx.put());
        if (FAILED (hr)) // (11.1 unknown to an old runtime)
            hr = D3D11CreateDevice (nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels + 1, (UINT) std::size (levels) - 1,
                                    D3D11_SDK_VERSION, device.put(), nullptr, ctx.put());
        if (FAILED (hr))
            return false;
        Com<IDXGIDevice> dxgi;
        if (FAILED (device->QueryInterface (__uuidof (IDXGIDevice), (void**) dxgi.put())))
            return false;
        Com<IDCompositionDevice> comp;
        if (FAILED (DCompositionCreateDevice (dxgi.p, __uuidof (IDCompositionDevice), (void**) comp.put())))
            return false;
        d3d = std::move (device);
        context = std::move (ctx);
        dcomp = std::move (comp);
        return true;
    }

    // message thread: ready now? (else starts making it, once)
    bool isReady()
    {
        const auto s = state.load (std::memory_order_acquire);
        if (s == ready)
            return true;
        if (s == none)
        {
            state = making;
            juce::Thread::launch ([this] { state.store (make() ? ready : failed, std::memory_order_release); });
        }
        return false;
    }

    bool makeNow()
    {
        if (state.load (std::memory_order_acquire) == ready)
            return true;
        if (state.load() == making)
        {
            for (int i = 0; i < 400 && state.load (std::memory_order_acquire) == making; ++i)
                juce::Thread::sleep (5);
            return state.load (std::memory_order_acquire) == ready;
        }
        state.store (make() ? ready : failed, std::memory_order_release);
        return state.load() == ready;
    }

    // a call failed with the device gone: everything made on it is dropped (Generation) and it is made again later
    void lost()
    {
        ++generation;
        dcomp.reset();
        context.reset();
        d3d.reset();
        state = ++remakes <= 3 ? none : failed;
    }

    int generation = 0;
};

// One topmost composition target per window, shared by that window's overlays (an HWND can have one)
struct Target
{
    HWND hwnd = nullptr;
    Com<IDCompositionTarget> target;
    Com<IDCompositionVisual> root;
    int users = 0, generation = -1;
};

std::map<HWND, Target>& targets()
{
    static std::map<HWND, Target> t; // (message thread)
    return t;
}

Target* useTarget (HWND hwnd)
{
    auto& dev = Devices::get();
    auto& all = targets();
    auto found = all.find (hwnd);
    if (found != all.end() && found->second.generation != dev.generation)
    {
        all.erase (found); // (made on a lost device)
        found = all.end();
    }
    if (found == all.end())
    {
        Target t;
        t.hwnd = hwnd;
        t.generation = dev.generation;
        if (FAILED (dev.dcomp->CreateTargetForHwnd (hwnd, TRUE, t.target.put())) || FAILED (dev.dcomp->CreateVisual (t.root.put()))
            || FAILED (t.target->SetRoot (t.root.p)))
            return nullptr;
        found = all.emplace (hwnd, std::move (t)).first;
    }
    ++found->second.users;
    return &found->second;
}

void releaseTarget (HWND hwnd)
{
    auto& all = targets();
    if (const auto found = all.find (hwnd); found != all.end() && --found->second.users <= 0)
    {
        if (found->second.target)
            found->second.target->SetRoot (nullptr);
        all.erase (found);
        if (auto& dev = Devices::get(); dev.dcomp)
            dev.dcomp->Commit();
    }
}
} // namespace

struct MotionOverlay::Impl final : private juce::Timer, private juce::ComponentListener
{
    // Sub-pixel positions of the mark. A playhead zoomed out moves a few pixels a second (Thief's whole song on screen:
    // ~5 px/s, 0.035 px a frame at 144 Hz), and the painted playhead shows each such step (its anti-aliased edge).
    // Measured (round 190ip, pixels that changed a second, Thief, Playlist in front, painted: 134-140): pictures cached at
    // 1/8 px -> 93, at 1/32 px -> 115-125; one picture painted again at each 1/128 px -> 0.7 ms a frame on the message
    // thread (10 % of a core: rejected). So the pictures are cached at 1/8 px and the rest of the way (< 1/8 px) is a
    // translation of the visual that the compositor samples linearly: the mark moves on every frame, nothing is painted.
    // LYNX_OVERLAY_PHASES=0: painted again at each 1/128 px; 1..64: that many cached phases (A/B).
    // LYNX_OVERLAY_SUBPIXEL=0: no translation (the cached phases only).
    static constexpr int maxPhases = 64;
    static constexpr int exactSteps = 128;
    static bool subPixel()
    {
        static const bool on = juce::SystemStats::getEnvironmentVariable ("LYNX_OVERLAY_SUBPIXEL", {}) != "0";
        return on;
    }

    static int phases()
    {
        static const int n = [] {
            const auto v = juce::SystemStats::getEnvironmentVariable ("LYNX_OVERLAY_PHASES", {});
            return v.isEmpty() ? 8 : juce::jlimit (0, maxPhases, v.getIntValue());
        }();
        return n;
    }

    Impl (MotionOverlay& o, juce::Component& ownerIn, Painter p, bool over)
        : overlay (o), owner (ownerIn), painter (std::move (p)), overChildren (over)
    {
        owner.addComponentListener (this);
    }

    ~Impl() override
    {
        owner.removeComponentListener (this);
        stopTimer();
        drop();
    }

    bool show (const Mark& mark)
    {
        lastOwner() = &owner;
        writeStats();
        const ScopedTime timed;
        if (! place (mark))
        {
            ++fallbacks();
            if (showing || inTree)
                hideNow (false);
            return false;
        }
        ++framesShown();
        showing = true;
        lastShowMs = juce::Time::getMillisecondCounterHiRes();
        hideAtMs = 0.0;
        if (! isTimerRunning())
            startTimerHz (20);
        return true;
    }

    void hide (bool afterFrames)
    {
        if (! showing && ! inTree)
            return;
        showing = false;
        if (afterFrames && inTree)
        {
            hideAtMs = juce::Time::getMillisecondCounterHiRes() + 50.0; // (a few frames: the view's paint is on screen first)
            if (! isTimerRunning())
                startTimerHz (20);
            return;
        }
        hideNow (false);
    }

    // Puts the mark's picture there. False: the view paints it.
    bool place (const Mark& mark)
    {
        auto& dev = Devices::get();
        const auto gdi = live && liveGdiWanted(); // (round 190lv: no Direct3D device, no composition target)
        if (! overlayWanted() || (! gdi && ! dev.isReady()))
            return false;
        auto* peer = owner.getPeer();
        if (peer == nullptr || ! owner.isShowing() || peer->isMinimised() || windowDrawnByCpu (owner))
            return false;
        auto& top = peer->getComponent();
        const auto hwnd = (HWND) peer->getNativeHandle();
        if (hwnd == nullptr)
            return false;
        // round 190kr (QA-11): the view's window lies wholly under other panels' windows (the Playlist behind Thief's
        // maximised Channel rack): nothing to show and nothing for the view to paint. Before, the visual was moved and
        // committed every frame there, and the main window's frames fell from 143 to 86 a second.
        if (nativechild::coveredBySiblingWindows (owner))
        {
            ++coveredFrames();
            if (inTree)
                hideNow (false);
            return true;
        }

        // no rotation or shear between the view and its window; a plain scale is fine
        for (auto* c = &owner; c != nullptr; c = c->getParentComponent())
            if (c->isTransformed() && (c->getTransform().mat01 != 0.0f || c->getTransform().mat10 != 0.0f))
                return false;
        // window pixels: top-level coordinates x topScale; owner coordinates: ownerPos + L x s
        const auto topScale = (double) peer->getPlatformScaleFactor() * (double) top.getDesktopScaleFactor();
        const auto s = topScale * (double) juce::Component::getApproximateScaleFactorForComponent (&owner)
                       / juce::jmax (0.0001, (double) juce::Component::getApproximateScaleFactorForComponent (&top));
        const auto ownerPos = top.getLocalPoint (&owner, juce::Point<float>()).toDouble() * topScale;

        // what is visible of the clip (the view's parents clip it too), in top-level coordinates
        auto clipTop = top.getLocalArea (&owner, mark.clip.getIntersection (owner.getLocalBounds()));
        for (auto* c = owner.getParentComponent(); c != nullptr && c != &top; c = c->getParentComponent())
            clipTop = clipTop.getIntersection (top.getLocalArea (c, c->getLocalBounds()));
        clipTop = clipTop.getIntersection (top.getLocalBounds());
        const auto clip = owner.getLocalArea (&top, clipTop);
        const auto markArea = mark.extent.toFloat().translated (mark.x, 0.0f).getSmallestIntegerContainer().getIntersection (clip);
        if (markArea.isEmpty())
        {
            if (inTree) // (none of it is in view: nothing to show, nothing for the view to paint)
                hideNow (false);
            return true;
        }
        // The visual is in the topmost target of the view's own window (a docked panel: its native child window). The
        // system composes it with that window, so the sibling windows in front of it (other panels, docked plugin
        // windows, floaters) cover it as they cover the view. Measured (round 190ip): a DirectComposition commit there is
        // shown at once; a child window's Direct2D frames are not (cmake/LynxPatchJuce.cmake fix 34). The 74 screen
        // updates first seen with Thief were the Playlist behind the maximised Channel rack (nothing moves on screen
        // but the toolbar), and the 93 with the Playlist in front were the 1/8-pixel phases (see phases()).
        // LYNX_OVERLAY_TARGET=root (A/B): the main window's topmost target, shifted to the child's place, with the
        // sibling windows in front of the child checked by hand.
        static const bool inRoot = juce::SystemStats::getEnvironmentVariable ("LYNX_OVERLAY_TARGET", {}) == "root";
        auto targetHwnd = hwnd;
        POINT shift { 0, 0 };
        if (inRoot && (GetWindowLongPtr (hwnd, GWL_STYLE) & WS_CHILD) != 0)
        {
            targetHwnd = GetAncestor (hwnd, GA_ROOT);
            if (targetHwnd == nullptr)
                return false;
            MapWindowPoints (hwnd, targetHwnd, &shift, 1);
        }
        const auto cover = covered (top, markArea, hwnd, targetHwnd, topScale);
        if (cover == Cover::part)
            return false;
        if (cover == Cover::whole)
        {
            if (inTree) // (another panel's window is in front of all of it: nothing to show, nothing for the view to paint)
                hideNow (false);
            return true;
        }
        if (! gdi && ! attachTo (peer, targetHwnd))
            return false;

        // the picture of this phase, painted once per look: the mark at the reference place + phase / 8 of a pixel
        const auto xWin = ownerPos.x + (double) mark.x * s;
        const auto whole = std::floor (xWin);
        const auto exact = phases() == 0;
        const auto steps = exact ? exactSteps : phases();
        const auto phase = juce::jlimit (0, steps - 1, (int) std::floor ((xWin - whole) * steps));
        const auto left = (int) std::floor ((double) mark.extent.getX() * s) - 1;
        const auto topY = (int) std::floor (ownerPos.y + (double) mark.extent.getY() * s) - 1;
        const auto w = (int) std::ceil ((double) mark.extent.getWidth() * s) + 3, h = (int) std::ceil ((double) mark.extent.getHeight() * s) + 3;
        const auto reference = (int) std::floor ((double) clipTop.getCentreX() * topScale); // (where the painter clips nothing of it)
        const PictureKey key { mark.look, w, h, reference, topY, s, ownerPos.x - std::floor (ownerPos.x), ownerPos.y };
        if (! (key == pictureKey))
        {
            pictureKey = key;
            for (auto& p : pictures)
                p.drawn = false;
        }
        auto& picture = pictures[exact ? 0 : (std::size_t) phase];
        if (exact && picture.drawn && picture.phase != phase)
            picture.drawn = false; // (one picture, painted again at the new phase)
        if (live) // (round 190kz) painted again and presented on every frame, in its own swap chain (190lv: or window)
        {
            const auto originX = reference + left;
            const auto at = juce::AffineTransform::scale ((float) s).translated ((float) (ownerPos.x - originX), (float) (ownerPos.y - topY));
            const auto xAt = ((double) reference + (double) phase / steps - ownerPos.x) / s;
            if (gdi) // (painted again each frame anyway: at its exact sub-pixel place, the window at a whole pixel)
                paintScratch (w, h, at, (float) (((double) reference + (xWin - whole) - ownerPos.x) / s));
            else if (! paintLive (w, h, at, (float) xAt))
                return false;
        }
        else if (! picture.drawn)
        {
            const auto originX = reference + left;
            const auto at = juce::AffineTransform::scale ((float) s).translated ((float) (ownerPos.x - originX), (float) (ownerPos.y - topY));
            const auto xAt = ((double) reference + (double) phase / steps - ownerPos.x) / s;
            if (! paintPicture (picture, w, h, at, (float) xAt))
                return false;
            picture.phase = phase;
        }

        // moved there: whole window pixels (the phase is in the picture), clipped to the visible clip
        const auto offsetX = (float) (whole + left + shift.x), offsetY = (float) (topY + shift.y);
        const auto clipWin = (clipTop.toDouble() * topScale).translated ((double) shift.x, (double) shift.y);
        const D2D_RECT_F local { (float) std::round (clipWin.getX()) - offsetX, (float) std::round (clipWin.getY()) - offsetY,
                                 (float) std::round (clipWin.getRight()) - offsetX, (float) std::round (clipWin.getBottom()) - offsetY };
        // the rest of the way, under one phase step, as a translation the compositor samples linearly
        const auto rest = exact || ! subPixel() ? 0.0 : (xWin - whole) - (double) phase / steps;
        D2D_MATRIX_3X2_F shiftRest {};
        shiftRest._11 = 1.0f;
        shiftRest._22 = 1.0f;
        shiftRest._31 = (float) rest;
        if (gdi)
        {
            // (round 190lv) the window at whole pixels (the sub-pixel part is in the picture); its clip as a part of it
            const auto clipPicture = juce::Rectangle<float>::leftTopRightBottom (local.left, local.top, local.right, local.bottom)
                                         .getSmallestIntegerContainer();
            if (! gdiWindow.show (targetHwnd, scratch, (int) offsetX, (int) offsetY, clipPicture))
            {
                gdiWindow.destroy();
                return false;
            }
            inTree = true;
            ++picturesPainted();
            return true;
        }
        if (live)
        {
            // the swap chain's presents show the new pictures by themselves: the visual changes (and is committed) only
            // when its place, its clip or its swap chain changed
            const LiveKey placed { offsetX, offsetY, local.left, local.top, local.right, local.bottom, (float) rest, chainSerial };
            if (inTree && placed == liveKey)
                return true;
            auto done = SUCCEEDED (visual->SetContent (chain.p)) && SUCCEEDED (visual->SetOffsetX (offsetX))
                        && SUCCEEDED (visual->SetOffsetY (offsetY)) && SUCCEEDED (visual->SetClip (local))
                        && SUCCEEDED (visual->SetTransform (shiftRest));
            if (done && ! inTree)
                done = inTree = SUCCEEDED (target->root->AddVisual (visual.p, TRUE, nullptr));
            if (done)
                done = SUCCEEDED (dev.dcomp->Commit());
            if (! done)
            {
                deviceLost();
                return false;
            }
            liveKey = placed;
            return true;
        }
        auto ok = SUCCEEDED (visual->SetContent (picture.surface.p)) && SUCCEEDED (visual->SetOffsetX (offsetX))
                  && SUCCEEDED (visual->SetOffsetY (offsetY)) && SUCCEEDED (visual->SetClip (local))
                  && SUCCEEDED (visual->SetTransform (shiftRest));
        if (ok && ! inTree)
            ok = inTree = SUCCEEDED (target->root->AddVisual (visual.p, TRUE, nullptr));
        if (ok)
            ok = SUCCEEDED (dev.dcomp->Commit());
        if (! ok)
        {
            deviceLost();
            return false;
        }
        if (targetHwnd == hwnd)
            nudgeTopLevel (hwnd);
        return true;
    }

    // LYNX_OVERLAY_NUDGE=1 (A/B measurements): a docked panel's window is a native child; as for its Direct2D frames
    // (cmake/LynxPatchJuce.cmake fix 34) the main window also paints the one pixel under the child's corner
    static void nudgeTopLevel (HWND hwnd)
    {
        static const bool nudge = juce::SystemStats::getEnvironmentVariable ("LYNX_OVERLAY_NUDGE", {}) == "1";
        if (! nudge || (GetWindowLongPtr (hwnd, GWL_STYLE) & WS_CHILD) == 0)
            return;
        const auto root = GetAncestor (hwnd, GA_ROOT);
        for (int i = 0; i < juce::ComponentPeer::getNumPeers(); ++i)
            if (auto* top = juce::ComponentPeer::getPeer (i); top != nullptr && (HWND) top->getNativeHandle() == root)
            {
                POINT corner { 0, 0 };
                MapWindowPoints (hwnd, root, &corner, 1);
                const auto scale = top->getPlatformScaleFactor() * (double) top->getComponent().getDesktopScaleFactor();
                top->repaint ({ (int) (corner.x / scale), (int) (corner.y / scale), 1, 1 });
                return;
            }
    }

    // Something in the view's window over the mark: a later sibling of the view or of one of its parents, the view's own
    // children (when the mark is under them), or a native child window (a topmost target is drawn over them).
    // part: something covers the mark (the view paints it); whole: an opaque window in front of all of it (nobody needs to
    // draw it: the Playlist behind a maximised Channel rack)
    enum class Cover { none, part, whole };

    Cover covered (juce::Component& top, juce::Rectangle<int> area, HWND hwnd, HWND targetHwnd, double topScale) const
    {
        const auto covers = [&] (const juce::Component& c, const juce::Component& parent) {
            if (! c.isVisible() || c.getAlpha() <= 0.0f || (bool) c.getProperties().getWithDefault ("lynxOverlayIgnores", false))
                return false;
            return owner.getLocalArea (&parent, c.getBounds()).intersects (area);
        };
        if (! overChildren)
            for (auto* child : owner.getChildren())
                if (covers (*child, owner))
                    return Cover::part;
        for (auto* c = &owner; c != &top && c->getParentComponent() != nullptr; c = c->getParentComponent())
        {
            auto* parent = c->getParentComponent();
            for (int i = parent->getIndexOfChildComponent (c) + 1; i < parent->getNumChildComponents(); ++i)
                if (covers (*parent->getChildComponent (i), *parent))
                    return Cover::part;
        }
        const auto areaPhys = (top.getLocalArea (&owner, area).toDouble() * topScale).getSmallestIntegerContainer();
        // (round 190jy: a hole in the view's own window — a docked editor the main window draws is in front of it — needs
        // nothing here: the window's region clips its topmost target as it clips its frames. Falling back to the repaint
        // path there made a Playlist behind the maximised Channel rack repaint its playhead at the display's rate.)
        for (auto child = GetWindow (hwnd, GW_CHILD); child != nullptr; child = GetWindow (child, GW_HWNDNEXT))
        {
            if (! IsWindowVisible (child) || GetPropW (child, liveGdiProp) != nullptr) // (round 190lv: a live picture's window)
                continue;
            RECT r {};
            GetWindowRect (child, &r);
            POINT corners[2] { { r.left, r.top }, { r.right, r.bottom } };
            MapWindowPoints (nullptr, hwnd, corners, 2);
            const auto childRect = juce::Rectangle<int>::leftTopRightBottom (corners[0].x, corners[0].y, corners[1].x, corners[1].y);
            if (! childRect.intersects (areaPhys))
                continue;
            // round 190jy: a child window below a docked editor the main window draws (the view: that editor) has a
            // hole where the editor is; where its region leaves the mark free it covers nothing of it
            if (auto* region = CreateRectRgn (0, 0, 0, 0))
            {
                const auto type = GetWindowRgn (child, region);
                const auto inChild = areaPhys.getIntersection (childRect) - childRect.getPosition();
                const RECT probe { inChild.getX(), inChild.getY(), inChild.getRight(), inChild.getBottom() };
                const auto free = type != ERROR && type != NULLREGION && RectInRegion (region, &probe) == FALSE;
                DeleteObject (region);
                if (free)
                    continue;
            }
            return Cover::part;
        }
        // in the top-level window's target: the windows above the view's window (and above each of its parents) up to it
        if (targetHwnd != hwnd)
        {
            POINT corners[2] { { areaPhys.getX(), areaPhys.getY() }, { areaPhys.getRight(), areaPhys.getBottom() } };
            MapWindowPoints (hwnd, nullptr, corners, 2);
            const auto onScreen = juce::Rectangle<int>::leftTopRightBottom (corners[0].x, corners[0].y, corners[1].x, corners[1].y);
            for (auto w = hwnd; w != nullptr && w != targetHwnd; w = GetParent (w))
                for (auto above = GetWindow (w, GW_HWNDPREV); above != nullptr; above = GetWindow (above, GW_HWNDPREV))
                {
                    if (! IsWindowVisible (above))
                        continue;
                    RECT r {};
                    GetWindowRect (above, &r);
                    const auto rect = juce::Rectangle<int>::leftTopRightBottom (r.left, r.top, r.right, r.bottom);
                    if (rect.contains (onScreen) && (GetWindowLongPtr (above, GWL_EXSTYLE) & WS_EX_LAYERED) == 0)
                        return Cover::whole;
                    if (rect.intersects (onScreen))
                        return Cover::part;
                }
        }
        return Cover::none;
    }

    bool attachTo (juce::ComponentPeer* peer, HWND hwnd)
    {
        auto& dev = Devices::get();
        if (target != nullptr && (peer != attachedPeer || hwnd != attachedHwnd || generation != dev.generation))
            drop();
        if (target == nullptr)
        {
            target = useTarget (hwnd);
            if (target == nullptr)
                return false;
            attachedPeer = peer;
            attachedHwnd = hwnd;
            generation = dev.generation;
            if (FAILED (dev.dcomp->CreateVisual (visual.put()))
                || FAILED (visual->SetBitmapInterpolationMode (DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR)))
            {
                drop();
                return false;
            }
        }
        return true;
    }

    struct Picture
    {
        Com<IDCompositionSurface> surface;
        int w = 0, h = 0, phase = -1;
        bool drawn = false;
    };

    bool paintPicture (Picture& p, int w, int h, const juce::AffineTransform& at, float x)
    {
        auto& dev = Devices::get();
        if (! p.surface || p.w != w || p.h != h)
        {
            p.surface.reset();
            if (FAILED (dev.dcomp->CreateSurface ((UINT) w, (UINT) h, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, p.surface.put())))
            {
                deviceLost();
                return false;
            }
            p.w = w;
            p.h = h;
        }
        // the view's own paint code draws the mark into a software picture whose pixel (0, 0) is the picture's corner
        if (scratch.isNull() || scratch.getWidth() != w || scratch.getHeight() != h)
            scratch = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        else
            scratch.clear (scratch.getBounds());
        auto& image = scratch;
        {
            juce::Graphics g (image);
            g.addTransform (at);
            painter (g, x);
        }
        POINT corner {};
        Com<IDXGISurface> dxgi;
        if (FAILED (p.surface->BeginDraw (nullptr, __uuidof (IDXGISurface), (void**) dxgi.put(), &corner)))
        {
            deviceLost();
            return false;
        }
        Com<ID3D11Texture2D> texture;
        auto ok = SUCCEEDED (dxgi->QueryInterface (__uuidof (ID3D11Texture2D), (void**) texture.put()));
        if (ok)
        {
            const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
            const D3D11_BOX box { (UINT) corner.x, (UINT) corner.y, 0, (UINT) (corner.x + w), (UINT) (corner.y + h), 1 };
            dev.context->UpdateSubresource (texture.p, 0, &box, pixels.data, (UINT) pixels.lineStride, 0);
        }
        p.surface->EndDraw();
        if (! ok)
            return false;
        p.drawn = true;
        ++picturesPainted();
        return true;
    }

    // the live picture, painted in software (premultiplied ARGB)
    void paintScratch (int w, int h, const juce::AffineTransform& at, float x)
    {
        if (scratch.isNull() || scratch.getWidth() != w || scratch.getHeight() != h)
            scratch = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        else
            scratch.clear (scratch.getBounds());
        juce::Graphics g (scratch);
        g.addTransform (at);
        painter (g, x);
    }

    // (round 190kz) the live picture: painted in software, copied into the swap chain's back buffer, presented without
    // waiting (a frame the compositor has not taken yet is simply replaced by the next one)
    bool paintLive (int w, int h, const juce::AffineTransform& at, float x)
    {
        auto& dev = Devices::get();
        if (! chain || chainW != w || chainH != h)
        {
            chain.reset();
            Com<IDXGIDevice> dxgiDevice;
            Com<IDXGIAdapter> adapter;
            Com<IDXGIFactory2> factory;
            if (FAILED (dev.d3d->QueryInterface (__uuidof (IDXGIDevice), (void**) dxgiDevice.put())) || FAILED (dxgiDevice->GetAdapter (adapter.put()))
                || FAILED (adapter->GetParent (__uuidof (IDXGIFactory2), (void**) factory.put())))
                return false;
            DXGI_SWAP_CHAIN_DESC1 desc {};
            desc.Width = (UINT) w;
            desc.Height = (UINT) h;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.Scaling = DXGI_SCALING_STRETCH;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
            if (FAILED (factory->CreateSwapChainForComposition (dev.d3d.p, &desc, nullptr, chain.put())))
            {
                chain.reset();
                return false;
            }
            chainW = w;
            chainH = h;
            ++chainSerial;
        }
        paintScratch (w, h, at, x);
        Com<ID3D11Texture2D> buffer;
        if (FAILED (chain->GetBuffer (0, __uuidof (ID3D11Texture2D), (void**) buffer.put())))
        {
            deviceLost();
            return false;
        }
        {
            const juce::Image::BitmapData pixels (scratch, juce::Image::BitmapData::readOnly);
            dev.context->UpdateSubresource (buffer.p, 0, nullptr, pixels.data, (UINT) pixels.lineStride, 0);
        }
        const auto hr = chain->Present (0, DXGI_PRESENT_DO_NOT_WAIT);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            deviceLost();
            return false;
        }
        ++picturesPainted();
        return true;
    }

    void hideNow (bool lostIt)
    {
        const auto was = showing;
        showing = false;
        hideAtMs = 0.0;
        if (inTree && target != nullptr && visual)
        {
            target->root->RemoveVisual (visual.p);
            if (auto& dev = Devices::get(); dev.dcomp)
                dev.dcomp->Commit();
        }
        gdiWindow.hide();
        inTree = false;
        stopTimer();
        if (lostIt && was && overlay.onHidden)
            overlay.onHidden();
    }

    void drop()
    {
        if (inTree && target != nullptr && visual && Devices::get().generation == generation)
            target->root->RemoveVisual (visual.p);
        inTree = false;
        visual.reset();
        chain.reset();
        gdiWindow.destroy();
        liveKey = {};
        for (auto& p : pictures)
            p = {};
        pictureKey = {};
        scratch = {};
        if (target != nullptr && Devices::get().generation == generation)
            releaseTarget (attachedHwnd); // (a target of a lost device was dropped by useTarget already, or is now)
        else if (const auto found = targets().find (attachedHwnd); target != nullptr && found != targets().end() && found->second.generation == generation)
            targets().erase (found);
        target = nullptr;
        attachedPeer = nullptr;
        attachedHwnd = nullptr;
    }

    void deviceLost()
    {
        const auto was = showing;
        inTree = false;
        showing = false;
        Devices::get().lost();
        drop();
        if (was && overlay.onHidden)
            overlay.onHidden();
    }

    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (hideAtMs > 0.0 && now >= hideAtMs)
        {
            hideNow (false); // (the view paints the mark already)
            return;
        }
        // nobody moved it for a while (the view stopped asking: hidden, closed, its window gone): it goes, and the view is told
        if (showing && now - lastShowMs > 100.0)
        {
            hideNow (true);
            return;
        }
        if (! showing && hideAtMs <= 0.0)
            stopTimer();
    }

    // the view moved, was resized, hidden or taken out of its window: the overlay goes at once (the view paints the mark)
    void componentMovedOrResized (juce::Component&, bool, bool) override { lostPlace(); }
    void componentVisibilityChanged (juce::Component&) override { lostPlace(); }
    void componentParentHierarchyChanged (juce::Component&) override { lostPlace(); }
    void lostPlace()
    {
        if (showing || inTree)
            hideNow (true);
    }

    MotionOverlay& overlay;
    juce::Component& owner;
    Painter painter;
    bool overChildren;

    Target* target = nullptr;
    juce::ComponentPeer* attachedPeer = nullptr;
    HWND attachedHwnd = nullptr;
    int generation = -1;
    Com<IDCompositionVisual> visual;
    std::array<Picture, maxPhases> pictures;
    // what the pictures were painted for (a change paints them again)
    struct PictureKey
    {
        std::uint64_t look = 0;
        int w = 0, h = 0, reference = 0, topY = 0;
        double scale = 0.0, ownerFractionX = -1.0, ownerY = 0.0;
        bool operator== (const PictureKey&) const = default;
    };
    PictureKey pictureKey;
    juce::Image scratch; // (the software picture, kept: painted again at each 1/128 px the mark moves)
    // live pictures (round 190kz): their swap chain and where the visual was last put
    struct LiveKey
    {
        float x = 0.0f, y = 0.0f, l = 0.0f, t = 0.0f, r = 0.0f, b = 0.0f, rest = 0.0f;
        int serial = -1;
        bool operator== (const LiveKey&) const = default;
    };
    Com<IDXGISwapChain1> chain;
    int chainW = 0, chainH = 0, chainSerial = 0;
    LiveGdiWindow gdiWindow; // (round 190lv) the live picture as a layered child window
    LiveKey liveKey;
    bool live = false;
    bool showing = false, inTree = false;
    double lastShowMs = 0.0, hideAtMs = 0.0;
};

bool MotionOverlay::available()
{
    return overlayWanted() && Devices::get().state.load() != Devices::failed;
}

juce::String MotionOverlay::selfTest()
{
    if (! overlayWanted())
        return "OK (off: LYNX_OVERLAY=0)";
    if (! prepareNow())
        return "OK (no device: the views paint their marks)";
    auto& dev = Devices::get();
    juce::Component host;
    host.setOpaque (true);
    host.setBounds (120, 40, 400, 300);
    host.addToDesktop (juce::ComponentPeer::windowHasTitleBar); // (never shown)
    auto* peer = host.getPeer();
    if (peer == nullptr)
        return "ERROR no window";
    int painted = 0;
    MotionOverlay overlay (host, [&painted] (juce::Graphics& g, float x) {
        ++painted;
        g.setColour (theme::colour::playhead);
        g.fillRect (juce::Rectangle<float> (x - 0.5f, 0.0f, 1.5f, 300.0f));
    });
    Mark mark { 100.25f, { -8, 0, 16, 300 }, host.getLocalBounds(), 1 };
    if (overlay.show (mark) || overlay.isShowing())
        return "ERROR shown in a window that is not showing";
    // what show() does once the window shows: a target, a visual, the picture of a phase, a commit
    auto& impl = *overlay.impl;
    const auto hwnd = (HWND) peer->getNativeHandle();
    if (! impl.attachTo (peer, hwnd))
        return "ERROR no composition target";
    const auto s = (double) peer->getPlatformScaleFactor();
    if (! impl.paintPicture (impl.pictures[2], 30, 304, juce::AffineTransform::scale ((float) s).translated (-80.0f, 0.0f), 100.25f) || painted != 1)
        return "ERROR picture not painted";
    auto ok = SUCCEEDED (impl.visual->SetContent (impl.pictures[2].surface.p)) && SUCCEEDED (impl.target->root->AddVisual (impl.visual.p, TRUE, nullptr))
              && SUCCEEDED (dev.dcomp->Commit());
    impl.inTree = ok;
    impl.hideNow (false);
    impl.drop();
    if (! ok)
        return "ERROR commit";
    if (! targets().empty())
        return "ERROR target kept";
    // round 190lv: a live picture's layered window under the host's window: made, given a picture (a part of it), hidden,
    // gone, and the cover check does not count it
    if (liveGdiWanted())
    {
        juce::Image picture (juce::Image::ARGB, 40, 20, true, juce::SoftwareImageType());
        {
            juce::Graphics g (picture);
            g.setColour (theme::colour::playhead);
            g.fillRoundedRectangle (1.0f, 1.0f, 38.0f, 18.0f, 4.0f);
        }
        LiveGdiWindow window;
        if (! window.show (hwnd, picture, 10, 12, { 2, 0, 30, 20 }) || window.window == nullptr || ! window.visible)
            return "ERROR live window";
        RECT r {};
        GetWindowRect (window.window, &r);
        if (r.right - r.left != 30 || r.bottom - r.top != 20 || GetPropW (window.window, liveGdiProp) == nullptr) // (x 2, width 30)
            return "ERROR live window place";
        if (! window.show (hwnd, picture, 10, 12, { 2, 0, 30, 20 })) // (the next frame: the same window)
            return "ERROR live window frame";
        const auto made = window.window;
        window.hide();
        if (IsWindowVisible (made))
            return "ERROR live window not hidden";
        window.destroy();
        if (IsWindow (made))
            return "ERROR live window kept";
        return "OK (live window OK)";
    }
    return "OK";
}

bool MotionOverlay::prepareNow()
{
    return overlayWanted() && Devices::get().makeNow();
}

#elif JUCE_LINUX || JUCE_BSD || JUCE_MAC

// Linux / BSD (round 190jx): the X server draws the mark over the view's window and keeps the pixels under it
// (ui/MotionOverlayX11.h). LYNX runs through XWayland on Wayland desktops, so this is the path there too.
//
// Why this way (round 190jx; measured with a stand-alone X client on Xvfb, 1200 frames of an 18 x 800 px mark moving
// 0.6 px a frame): a native ARGB child window moved with XMoveWindow costs the server about as much, but each whole-pixel
// step uncovers a strip of the window under it (719 Expose events, 575,200 px), which JUCE would paint and blit again at
// every step; the save-under above needs no paint and no image at all (the client side: 22-33 ms of CPU for the 1200
// frames, against 134-227 ms for putting the columns). An OpenGL layer would put the whole view in a GL window.
// macOS (round 190jx, ui/MotionOverlayMac.h): the same steps with a CALayer over the view's layer; off by default
// (LYNX_OVERLAY_MAC=1): checked headless on the CI's macOS runner only.
namespace
{
#if JUCE_MAC
using Sprite = maclayer::Sprite;
using SpriteRect = maclayer::Rect;
bool backendReady() { return maclayer::enabled(); }
double windowScale (juce::ComponentPeer& peer) { return maclayer::backingScale (peer.getNativeHandle()); }
bool nativeOver (void* handle, juce::Rectangle<int> r) { return maclayer::subviewOver (handle, { r.getX(), r.getY(), r.getWidth(), r.getHeight() }); }
#else
void afterBlitHook (unsigned long window, const juce::RectangleList<int>& blitted)
{
    std::array<x11overlay::Rect, 16> rects {};
    std::vector<x11overlay::Rect> many;
    auto* list = rects.data();
    const auto n = blitted.getNumRectangles();
    if (n > (int) rects.size())
    {
        many.resize ((std::size_t) n);
        list = many.data();
    }
    int i = 0;
    for (const auto& r : blitted)
        list[i++] = { r.getX(), r.getY(), r.getWidth(), r.getHeight() };
    x11overlay::afterBlit (window, list, n);
}

// JUCE's display and fix 41's hook (weak: a JUCE without the fix links, and the views paint their marks)
bool hookInstalled()
{
    static const bool on = [] {
        if (&juce::lynxX11AfterBlitHook == nullptr || juce::lynxX11Display == nullptr)
            return false;
        auto* display = juce::lynxX11Display();
        if (display == nullptr)
            return false;
        x11overlay::setDisplay (display);
        juce::lynxX11AfterBlitHook = afterBlitHook;
        return true;
    }();
    return on;
}

bool x11Ready()
{
    return hookInstalled() && x11overlay::ready();
}

using Sprite = x11overlay::Sprite;
using SpriteRect = x11overlay::Rect;
bool backendReady() { return x11Ready(); }
double windowScale (juce::ComponentPeer& peer) { return peer.getPlatformScaleFactor(); }
bool nativeOver (void*, juce::Rectangle<int>) { return false; } // (the X server clips the drawing to what is visible)
#endif
} // namespace

struct MotionOverlay::Impl final : private juce::Timer, private juce::ComponentListener
{
    // the same sub-pixel scheme as on Windows (see there): pictures cached at 1/8 px, the rest a translation that the
    // X server samples bilinearly (XRender)
    static constexpr int maxPhases = 64;
    static bool subPixel()
    {
        static const bool on = juce::SystemStats::getEnvironmentVariable ("LYNX_OVERLAY_SUBPIXEL", {}) != "0";
        return on;
    }
    static int phases()
    {
        static const int n = [] {
            const auto v = juce::SystemStats::getEnvironmentVariable ("LYNX_OVERLAY_PHASES", {});
            return v.isEmpty() ? 8 : juce::jlimit (1, maxPhases, v.getIntValue());
        }();
        return n;
    }

    Impl (MotionOverlay& o, juce::Component& ownerIn, Painter p, bool over)
        : overlay (o), owner (ownerIn), painter (std::move (p)), overChildren (over)
    {
        owner.addComponentListener (this);
    }

    ~Impl() override
    {
        owner.removeComponentListener (this);
        stopTimer();
        leaveWindow();
    }

    bool show (const Mark& mark)
    {
        lastOwner() = &owner;
        writeStats();
        const ScopedTime timed;
        if (! place (mark))
        {
            ++fallbacks();
            if (showing || sprite.drawn())
                hideNow (false);
            return false;
        }
        ++framesShown();
        showing = true;
        lastShowMs = juce::Time::getMillisecondCounterHiRes();
        hideAtMs = 0.0;
        if (! isTimerRunning())
            startTimerHz (20);
        return true;
    }

    void hide (bool afterFrames)
    {
        if (! showing && ! sprite.drawn())
            return;
        showing = false;
        if (afterFrames && sprite.drawn())
        {
            // the view paints the mark again now; JUCE's blit of it only refreshes the pixels kept under ours, which go
            // in a few frames (then the window shows the painted one)
            sprite.setShowing (false);
            hideAtMs = juce::Time::getMillisecondCounterHiRes() + 50.0;
            if (! isTimerRunning())
                startTimerHz (20);
            return;
        }
        hideNow (false);
    }

    bool place (const Mark& mark)
    {
        // (the reason only for LYNX_OVERLAY_STATS: no strings made per frame otherwise)
        static const bool explain = juce::SystemStats::getEnvironmentVariable ("LYNX_OVERLAY_STATS", {}).isNotEmpty();
        auto why = [] (const auto& reason) {
            if (explain)
                lastFallback() = reason();
            return false;
        };
        if (! overlayWanted() || ! backendReady())
            return why ([] { return juce::String ("off or not available"); });
        auto* peer = owner.getPeer();
        // (X11: isMinimised() is a round trip to the server, ~0.2 ms a frame; a minimised window is unmapped and the
        // server draws nothing on it, and JUCE repaints it when it comes back, which the blit hook sees)
#if JUCE_MAC
        if (peer == nullptr || ! owner.isShowing() || peer->isMinimised())
#else
        if (peer == nullptr || ! owner.isShowing())
#endif
            return why ([] { return juce::String ("not showing"); });
        auto& top = peer->getComponent();
        auto* const window = peer->getNativeHandle();
        if (window == nullptr)
            return why ([] { return juce::String ("no native window"); });
        for (auto* c = &owner; c != nullptr; c = c->getParentComponent())
            if (c->isTransformed() && (c->getTransform().mat01 != 0.0f || c->getTransform().mat10 != 0.0f))
                return why ([] { return juce::String ("rotated"); });
        // window pixels: top-level coordinates x topScale (as on Windows; macOS: points x the backing scale)
        const auto topScale = windowScale (*peer) * (double) top.getDesktopScaleFactor();
        const auto s = topScale * (double) juce::Component::getApproximateScaleFactorForComponent (&owner)
                       / juce::jmax (0.0001, (double) juce::Component::getApproximateScaleFactorForComponent (&top));
        const auto ownerPos = top.getLocalPoint (&owner, juce::Point<float>()).toDouble() * topScale;

        auto clipTop = top.getLocalArea (&owner, mark.clip.getIntersection (owner.getLocalBounds()));
        for (auto* c = owner.getParentComponent(); c != nullptr && c != &top; c = c->getParentComponent())
            clipTop = clipTop.getIntersection (top.getLocalArea (c, c->getLocalBounds()));
        clipTop = clipTop.getIntersection (top.getLocalBounds());
        const auto clip = owner.getLocalArea (&top, clipTop);
        const auto markArea = mark.extent.toFloat().translated (mark.x, 0.0f).getSmallestIntegerContainer().getIntersection (clip);
        if (markArea.isEmpty())
        {
            if (sprite.drawn())
                hideNow (false);
            return true;
        }
        // components of the window over the mark: the view paints it (the X server itself clips it to what of the window
        // is visible: native child windows, sibling windows, other programs' windows)
        if (const auto cover = coveredByComponents (top, markArea); cover.whole)
        {
            if (sprite.drawn()) // (nothing of it in view: nothing to show, nothing for the view to paint)
                hideNow (false);
            return true;
        }
        else if (cover.by != nullptr)
            return why ([&] {
                return "covered by " + cover.by->getName() + " (" + typeid (*cover.by).name() + ") at "
                       + top.getLocalArea (cover.by->getParentComponent(), cover.by->getBounds()).toString() + ", the mark at "
                       + top.getLocalArea (&owner, markArea).toString();
            });
        if (nativeOver (window, (top.getLocalArea (&owner, markArea).toDouble() * topScale).getSmallestIntegerContainer()))
            return why ([] { return juce::String ("a native view over it"); });
        if (attachedPeer != peer || attachedWindow != window)
        {
            leaveWindow();
            attachedPeer = peer;
            attachedWindow = window;
        }

        const auto xWin = ownerPos.x + (double) mark.x * s;
        const auto whole = std::floor (xWin);
        const auto steps = phases();
        const auto phase = juce::jlimit (0, steps - 1, (int) std::floor ((xWin - whole) * steps));
        const auto left = (int) std::floor ((double) mark.extent.getX() * s) - 1;
        const auto topY = (int) std::floor (ownerPos.y + (double) mark.extent.getY() * s) - 1;
        const auto w = (int) std::ceil ((double) mark.extent.getWidth() * s) + 3, h = (int) std::ceil ((double) mark.extent.getHeight() * s) + 3;
        const auto reference = (int) std::floor ((double) clipTop.getCentreX() * topScale);
        const PictureKey key { mark.look, w, h, reference, topY, s, ownerPos.x - std::floor (ownerPos.x), ownerPos.y };
        if (! (key == pictureKey))
        {
            pictureKey = key;
            drawn.fill (false);
        }
        if (! drawn[(std::size_t) phase])
        {
            const auto originX = reference + left;
            const auto at = juce::AffineTransform::scale ((float) s).translated ((float) (ownerPos.x - originX), (float) (ownerPos.y - topY));
            const auto xAt = ((double) reference + (double) phase / steps - ownerPos.x) / s;
            if (scratch.isNull() || scratch.getWidth() != w || scratch.getHeight() != h)
                scratch = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
            else
                scratch.clear (scratch.getBounds());
            {
                juce::Graphics g (scratch);
                g.addTransform (at);
                painter (g, (float) xAt);
            }
            const juce::Image::BitmapData pixels (scratch, juce::Image::BitmapData::readOnly);
            if (! sprite.upload (phase, pixels.data, w, h, pixels.lineStride))
                return why ([] { return juce::String ("picture not sent"); });
            drawn[(std::size_t) phase] = true;
            ++picturesPainted();
        }
        const auto rest = subPixel() ? (xWin - whole) - (double) phase / steps : 0.0;
        const auto clipWin = (clipTop.toDouble() * topScale);
        const auto cx = (int) std::round (clipWin.getX()), cy = (int) std::round (clipWin.getY());
        const SpriteRect clipRect { cx, cy, (int) std::round (clipWin.getRight()) - cx, (int) std::round (clipWin.getBottom()) - cy };
        sprite.setShowing (true);
#if JUCE_MAC
        const auto placed = sprite.place (window, phase, (int) whole + left, topY, rest, clipRect);
#else
        const auto placed = sprite.place ((unsigned long) (juce::pointer_sized_uint) window, phase, (int) whole + left, topY, rest, clipRect);
#endif
        return placed || why ([] { return juce::String ("not drawn"); });
    }

    // the component of the window over the mark, if any; whole: an opaque one in front of all of it (nobody needs to
    // draw the mark: a panel over the view, as the Piano roll behind a floating Channel rack)
    struct Cover
    {
        const juce::Component* by = nullptr;
        bool whole = false;
    };
    Cover coveredByComponents (juce::Component& top, juce::Rectangle<int> area) const
    {
        const auto covers = [&] (const juce::Component& c, const juce::Component& parent) {
            if (! c.isVisible() || c.getAlpha() <= 0.0f || (bool) c.getProperties().getWithDefault ("lynxOverlayIgnores", false))
                return false;
            return owner.getLocalArea (&parent, c.getBounds()).intersects (area);
        };
        if (! overChildren)
            for (auto* child : owner.getChildren())
                if (covers (*child, owner))
                    return { child, false };
        for (auto* c = &owner; c != &top && c->getParentComponent() != nullptr; c = c->getParentComponent())
        {
            auto* parent = c->getParentComponent();
            for (int i = parent->getIndexOfChildComponent (c) + 1; i < parent->getNumChildComponents(); ++i)
                if (auto* sibling = parent->getChildComponent (i); covers (*sibling, *parent))
                    return { sibling, sibling->isOpaque() && sibling->getAlpha() >= 1.0f && owner.getLocalArea (parent, sibling->getBounds()).contains (area) };
        }
        return {};
    }

    // the window this view was drawn on is still JUCE's: put the pixels back; gone: forget it (the server freed it)
    void leaveWindow()
    {
        if (attachedPeer != nullptr && juce::ComponentPeer::isValidPeer (attachedPeer) && attachedPeer->getNativeHandle() == attachedWindow)
            sprite.restore();
        else
            sprite.forget();
        attachedPeer = nullptr;
        attachedWindow = nullptr;
    }

    void hideNow (bool lostIt)
    {
        const auto was = showing;
        showing = false;
        hideAtMs = 0.0;
        if (sprite.drawn())
        {
            if (attachedPeer != nullptr && juce::ComponentPeer::isValidPeer (attachedPeer))
                sprite.restore();
            else
                sprite.forget();
        }
        stopTimer();
        if (lostIt && was && overlay.onHidden)
            overlay.onHidden();
    }

    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (hideAtMs > 0.0 && now >= hideAtMs)
        {
            hideNow (false);
            return;
        }
        if (showing && now - lastShowMs > 100.0)
        {
            hideNow (true);
            return;
        }
        if (! showing && hideAtMs <= 0.0)
            stopTimer();
    }

    void componentMovedOrResized (juce::Component&, bool, bool) override { lostPlace(); }
    void componentVisibilityChanged (juce::Component&) override { lostPlace(); }
    void componentParentHierarchyChanged (juce::Component&) override { lostPlace(); }
    void lostPlace()
    {
        if (showing || sprite.drawn())
            hideNow (true);
    }

    MotionOverlay& overlay;
    juce::Component& owner;
    Painter painter;
    bool overChildren;

    Sprite sprite;
    juce::ComponentPeer* attachedPeer = nullptr;
    void* attachedWindow = nullptr;
    std::array<bool, maxPhases> drawn {};
    struct PictureKey
    {
        std::uint64_t look = 0;
        int w = 0, h = 0, reference = 0, topY = 0;
        double scale = 0.0, ownerFractionX = -1.0, ownerY = 0.0;
        bool operator== (const PictureKey&) const = default;
    };
    PictureKey pictureKey;
    juce::Image scratch;
    bool showing = false;
    bool live = false; // (round 190kz: no effect on X11 / macOS)
    double lastShowMs = 0.0, hideAtMs = 0.0;
};

bool MotionOverlay::available()
{
    return overlayWanted() && backendReady();
}

bool MotionOverlay::prepareNow()
{
    return overlayWanted() && backendReady();
}

juce::String MotionOverlay::selfTest()
{
#if JUCE_MAC
    // the layer is checked whether or not it is on (it is off by default: the views paint their marks)
    juce::Component host;
    host.setBounds (120, 40, 400, 300);
    host.addToDesktop (juce::ComponentPeer::windowHasTitleBar); // (never shown)
    auto* peer = host.getPeer();
    if (peer == nullptr)
        return "ERROR no window";
    if (const juce::String why (maclayer::selfTest (peer->getNativeHandle())); why.isNotEmpty())
        return "ERROR " + why;
    MotionOverlay overlay (host, [] (juce::Graphics& g, float x) { g.fillRect (juce::Rectangle<float> (x - 0.5f, 0.0f, 1.5f, 300.0f)); });
    if (overlay.show ({ 100.25f, { -8, 0, 16, 300 }, host.getLocalBounds(), 1 }) || overlay.isShowing())
        return "ERROR shown in a window that is not showing";
    if (! overlayWanted() || ! maclayer::enabled())
        return "OK (Core Animation layer checked headless; off by default: LYNX_OVERLAY_MAC=1 turns it on)";
    return "OK (Core Animation layer checked headless; on: LYNX_OVERLAY_MAC=1)";
#else
    if (! overlayWanted())
        return "OK (off: LYNX_OVERLAY=0)";
    if (! hookInstalled())
        return "OK (not available: no X display or JUCE without fix 41; the views paint their marks)";
    if (! x11overlay::ready())
        return "OK (not available: " + juce::String (x11overlay::whyNot()) + "; the views paint their marks)";
    if (const juce::String why (x11overlay::selfTestOnServer()); why.isNotEmpty())
        return "ERROR " + why;
    // a view whose window is not on screen: the overlay must not take its mark
    juce::Component host;
    host.setBounds (120, 40, 400, 300);
    host.addToDesktop (juce::ComponentPeer::windowHasTitleBar); // (never shown)
    MotionOverlay overlay (host, [] (juce::Graphics& g, float x) { g.fillRect (juce::Rectangle<float> (x - 0.5f, 0.0f, 1.5f, 300.0f)); });
    if (overlay.show ({ 100.25f, { -8, 0, 16, 300 }, host.getLocalBounds(), 1 }) || overlay.isShowing())
        return "ERROR shown in a window that is not showing";
    return "OK (X11: drawn, half a pixel moved, clipped, drawn again after a blit, put back)";
#endif
}

#else // not available; the views paint their marks as before

struct MotionOverlay::Impl
{
    Impl (MotionOverlay&, juce::Component&, Painter, bool) {}
    bool show (const Mark&)
    {
        writeStats();
        ++fallbacks();
        return false;
    }
    void hide (bool) {}
    bool showing = false;
    bool live = false; // (round 190kz: no effect here)
};

bool MotionOverlay::available()
{
    return false;
}

bool MotionOverlay::prepareNow()
{
    return false;
}

juce::String MotionOverlay::selfTest()
{
    juce::Component host;
    host.setBounds (0, 0, 200, 100);
    MotionOverlay overlay (host, [] (juce::Graphics&, float) {});
    return overlay.show ({ 50.0f, { -8, 0, 16, 100 }, host.getLocalBounds(), 1 }) ? "ERROR shown" : "OK (not available: the views paint their marks)";
}

#endif

namespace
{
// LYNX_LOCATOR_PROBE: see MotionOverlay.h
class LocatorProbe final : public juce::Component, private juce::Timer
{
public:
    explicit LocatorProbe (std::function<double()> p) : position (std::move (p))
    {
        setOpaque (true);
        setSize (560, 320);
        locator.onHidden = [this] { repaint (column (shownAt)); };
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        // a picture like a plugin editor's (gradients, a grid of cells, a curve, text): every pixel, costly enough
        using namespace theme::colour;
        g.setGradientFill (juce::ColourGradient (bg2, 0.0f, 0.0f, bg0, 0.0f, (float) getHeight(), false));
        g.fillAll();
        const auto graph = graphArea();
        g.setColour (bg1);
        g.fillRoundedRectangle (graph.toFloat(), 6.0f);
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 8; ++j)
            {
                const auto cell = juce::Rectangle<float> ((float) graph.getX() + (float) i * graph.getWidth() / 16.0f, (float) graph.getY() + (float) j * graph.getHeight() / 8.0f,
                                                          (float) graph.getWidth() / 16.0f, (float) graph.getHeight() / 8.0f).reduced (2.0f);
                g.setColour (((i + j) & 1) != 0 ? bg3.withAlpha (0.6f) : bg2.withAlpha (0.8f));
                g.fillRoundedRectangle (cell, 3.0f);
            }
        juce::Path curve;
        for (int i = 0; i <= 400; ++i)
        {
            const auto t = (float) i / 400.0f;
            const auto y = (float) graph.getCentreY() - std::sin (t * 12.566f) * std::exp (-t) * (float) graph.getHeight() * 0.4f;
            const auto x = (float) graph.getX() + t * (float) graph.getWidth();
            i == 0 ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);
        }
        g.setColour (accent.withAlpha (0.85f));
        g.strokePath (curve, juce::PathStrokeType (2.0f));
        g.setColour (textPrimary);
        g.setFont (theme::font (theme::type::body));
        for (int i = 0; i < 4; ++i)
            g.drawText ("Time " + juce::String (i + 1), graph.getX() + i * graph.getWidth() / 4, graph.getBottom() + 6, graph.getWidth() / 4, 18,
                        juce::Justification::centred);
        if (shownAt >= 0.0 && ! locator.isShowing())
            paintLocator (g, xOf (shownAt));
    }

private:
    juce::Rectangle<int> graphArea() const { return getLocalBounds().reduced (20).withTrimmedBottom (30); }
    float xOf (double p) const { return (float) graphArea().getX() + (float) p * (float) graphArea().getWidth(); }
    juce::Rectangle<int> column (double p) const
    {
        return p >= 0.0 ? juce::Rectangle<int> ((int) xOf (p) - 8, graphArea().getY(), 12, graphArea().getHeight()) : juce::Rectangle<int>();
    }

    void paintLocator (juce::Graphics& g, float x)
    {
        const auto graph = graphArea().toFloat();
        g.setGradientFill (juce::ColourGradient (theme::colour::playhead.withAlpha (0.0f), x - 6.0f, 0.0f, theme::colour::playhead.withAlpha (0.45f), x, 0.0f, false));
        g.fillRect (juce::Rectangle<float> (x - 6.0f, graph.getY(), 6.0f, graph.getHeight()));
        g.setColour (theme::colour::playhead);
        g.fillRect (juce::Rectangle<float> (x, graph.getY(), 1.0f, graph.getHeight()));
    }

    void timerCallback() override
    {
        const auto p = position != nullptr ? position() : -1.0;
        if (p == shownAt)
            return;
        const auto before = column (shownAt);
        shownAt = p;
        const auto graph = graphArea();
        MotionOverlay::Mark mark { xOf (juce::jmax (0.0, p)), { -7, graph.getY(), 9, graph.getHeight() }, graph, 1 };
        const auto gpu = stepMark (locator, p >= 0.0, mark, false);
        if (! gpu.onOverlay || gpu.flipped)
            repaint (before.getUnion (column (p)));
    }

    std::function<double()> position;
    double shownAt = -1.0;
    MotionOverlay locator { *this, [this] (juce::Graphics& g, float x) { paintLocator (g, x); } };
};
} // namespace

void openLocatorProbe (std::function<double()> position)
{
    openToolWindow (std::make_unique<LocatorProbe> (std::move (position)), "Locator probe", "lynx-locator-probe");
}

MotionOverlay::MotionOverlay (juce::Component& owner, Painter painter, bool overChildren)
    : impl (std::make_unique<Impl> (*this, owner, std::move (painter), overChildren))
{
}

MotionOverlay::~MotionOverlay() = default;

bool MotionOverlay::show (const Mark& mark)
{
    return impl->show (mark);
}

void MotionOverlay::hide (bool afterFrames)
{
    impl->hide (afterFrames);
}

bool MotionOverlay::isShowing() const noexcept
{
    return impl->showing;
}

void MotionOverlay::setLive (bool on) noexcept
{
    impl->live = on;
}
} // namespace lynx::ui

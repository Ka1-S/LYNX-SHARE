#include "ui/Splash.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

#include "core/StartupSound.h"
#include "engine/PluginHost.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

#include <cstring>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #undef near // (windows.h; the drawing below has a variable of that name)
 #undef far
#endif

namespace lynx
{
namespace
{
double clamp01 (double x) { return juce::jlimit (0.0, 1.0, x); }
double easeOutCubic (double u) { return 1.0 - std::pow (1.0 - clamp01 (u), 3.0); }

double easeOutBack (double u)
{
    u = clamp01 (u);
    constexpr double c1 = 1.5, c3 = c1 + 1.0;
    return 1.0 + c3 * std::pow (u - 1.0, 3.0) + c1 * std::pow (u - 1.0, 2.0);
}

// a ball dropped on the floor: 0 = up, 1 = resting; it touches the floor at u = 1/2.75, 2/2.75, 2.5/2.75, 1
double easeOutBounce (double u)
{
    u = clamp01 (u);
    constexpr double n1 = 7.5625, d1 = 2.75;

    if (u < 1.0 / d1)
        return n1 * u * u;
    auto arc = [] (double v, double centre, double floor) { return n1 * (v - centre) * (v - centre) + floor; };
    if (u < 2.0 / d1)
        return arc (u, 1.5 / d1, 0.75);
    if (u < 2.5 / d1)
        return arc (u, 2.25 / d1, 0.9375);
    return arc (u, 2.625 / d1, 0.984375);
}

// the drop lasts so that its first touch is the sound's landing "pop"
constexpr double dropTime = startup::impactTime * 2.75;
constexpr double touches[] = { dropTime / 2.75, dropTime * 2.0 / 2.75, dropTime * 2.5 / 2.75 };
constexpr double touchStrength[] = { 1.0, 0.35, 0.12 };

double impactAt (double t)
{
    double k = 0.0;
    for (int i = 0; i < 3; ++i)
        if (t >= touches[i])
            k += touchStrength[i] * std::exp (-(t - touches[i]) / 0.07);
    return k;
}
} // namespace

// --- options --------------------------------------------------------------------------------------

StartupOptions StartupOptions::load()
{
    StartupOptions options;
    juce::StringArray lines;
    lines.addLines (PluginHost::settingsFolder().getChildFile ("ui-options.txt").loadFileAsString());

    for (const auto& line : lines)
    {
        const auto key = line.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto on = line.fromFirstOccurrenceOf ("=", false, false).trim() != "0";

        if (key == "splash")
            options.splash = on;
        else if (key == "startupSound")
            options.sound = on;
    }

    return options;
}

std::shared_ptr<const SampleData> makeStartupSample()
{
    auto sound = startup::makeSound (48000.0);
    auto sample = std::make_shared<SampleData>();
    sample->left = std::move (sound.left);
    sample->right = std::move (sound.right);
    sample->sampleRate = sound.sampleRate;
    return sample;
}

// --- the splash -----------------------------------------------------------------------------------

SplashScreen::SplashScreen()
    : logoMark (logoImage())   // the app icon by w3teal (512 px, src/ui/LogoData.h)
{
    setOpaque (false);
    setSize (width, height);
    status = tr ("Starting the audio engine...");
}

SplashScreen::~SplashScreen()
{
    stopTimer();
}

void SplashScreen::showOnDesktop()
{
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        setCentrePosition (display->userArea.getCentre());

    addToDesktop (0); // no title bar, no taskbar entry
    setAlwaysOnTop (true);
    setVisible (true);
    toFront (false);
    startTimerHz (60);
}

void SplashScreen::startClock()
{
    clockStartMs = juce::Time::getMillisecondCounterHiRes();
    time = 0.0;
}

double SplashScreen::remainingDuration (double sinceProcessStart, double landing)
{
    return juce::jmax (landing, minimumDuration - juce::jmax (0.0, sinceProcessStart));
}

double SplashScreen::getElapsed() const
{
    return clockStartMs < 0.0 ? 0.0 : (juce::Time::getMillisecondCounterHiRes() - clockStartMs) / 1000.0;
}

void SplashScreen::setStatus (const juce::String& text)
{
    status = text;
    repaint();
}

void SplashScreen::finish (std::function<void()> whenGone)
{
    onGone = std::move (whenGone);
    fadeStartMs = juce::Time::getMillisecondCounterHiRes();

    if (! isTimerRunning())
        startTimerHz (60);
}

void SplashScreen::setTimeForSnapshot (double seconds)
{
    time = seconds;
    repaint();
}

void SplashScreen::timerCallback()
{
    time = getElapsed();

    if (fadeStartMs >= 0.0)
    {
        const auto f = (juce::Time::getMillisecondCounterHiRes() - fadeStartMs) / 1000.0 / fadeOutTime;
        setAlpha ((float) (1.0 - clamp01 (f)));

        if (f >= 1.0)
        {
            stopTimer();
            setVisible (false);
            removeFromDesktop();

            if (auto gone = std::move (onGone))
                juce::MessageManager::callAsync (std::move (gone)); // may delete this splash
            return;
        }
    }

    repaint();
}

void SplashScreen::paint (juce::Graphics& g)
{
    if (clockStartMs >= 0.0 && fadeStartMs < 0.0) // (the drop's frames: see getDropFrames)
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (getElapsed() <= 1.05)
        {
            if (lastDropPaintMs >= 0.0)
                longestDropGapMs = juce::jmax (longestDropGapMs, now - lastDropPaintMs);
            ++dropFrames;
        }
        lastDropPaintMs = now;
    }

    SplashFrame frame;
    frame.t = time;
    frame.barDuration = barDuration;
    frame.status = status;
    frame.subtitle = tr ("Digital audio workstation");
    frame.version = tr ("Version {0}", { JUCE_APPLICATION_VERSION_STRING });
    frame.logo = &logoMark;
    drawSplashFrame (g, frame);

    if (auto first = std::exchange (onFirstFrame, nullptr))
    {
        firstFrameTime = std::chrono::steady_clock::now();
        juce::MessageManager::callAsync (std::move (first)); // (after this frame is on the screen)
    }
}

std::array<juce::Font, 4> makeSplashFonts()
{
    std::array<juce::Font, 4> fonts { juce::Font (juce::FontOptions (78.0f, juce::Font::bold)), juce::Font (juce::FontOptions (30.0f, juce::Font::bold)),
                                      juce::Font (theme::font (theme::type::title)), juce::Font (theme::font (theme::type::small)) };
    for (auto& f : fonts)
        f.getTypefacePtr(); // (looked up here, once)
    return fonts;
}

void drawSplashFrame (juce::Graphics& g, const SplashFrame& frame)
{
    using namespace theme;
    const auto t = frame.t;
    std::optional<std::array<juce::Font, 4>> made;
    if (frame.fonts == nullptr)
        made = makeSplashFonts();
    const auto* fonts = frame.fonts != nullptr ? frame.fonts : made->data();
    const juce::Image noLogo; // (an empty image: nothing to free)
    const auto& logoMark = frame.logo != nullptr ? *frame.logo : noLogo;
    const auto card = juce::Rectangle<float> (0.0f, 0.0f, (float) SplashScreen::width, (float) SplashScreen::height).reduced (0.5f);
    juce::Path cardShape;
    cardShape.addRoundedRectangle (card, 14.0f);

    // card: dark, a touch lighter at the top
    g.setGradientFill (juce::ColourGradient (colour::bg3, 0.0f, 0.0f, colour::bg0, 0.0f, card.getBottom(), false));
    g.fillPath (cardShape);

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (cardShape);

    // the logo's resting place, and its drop
    const auto markHeight = 184.0f;
    const auto markWidth = markHeight * (float) logoMark.getWidth() / (float) juce::jmax (1, logoMark.getHeight());
    const auto restX = 168.0f, restBottom = 270.0f;
    const auto fall = 300.0f;
    const auto lift = (float) (1.0 - easeOutBounce (t / dropTime)) * fall;
    const auto impact = (float) impactAt (t);
    const auto settled = (float) easeOutCubic ((t - touches[0]) / 0.5);

    // warm glow behind it: wakes up with the first touch and stays
    {
        const auto glow = juce::jlimit (0.0f, 0.5f, (t < touches[0] ? 0.05f * (float) clamp01 (t / touches[0]) : 0.05f + 0.13f * settled) + 0.2f * impact);
        juce::ColourGradient grad (colour::accent.withAlpha (glow), restX, restBottom - 90.0f, colour::accent.withAlpha (0.0f), restX + 210.0f,
                                   restBottom - 90.0f, true);
        g.setGradientFill (grad);
        g.fillRect (card);
    }

    // a ripple on the floor at the first touch
    if (t > touches[0] && t < touches[0] + 0.7)
    {
        const auto s = (float) ((t - touches[0]) / 0.7);
        const auto r = 24.0f + 150.0f * (float) easeOutCubic (s);
        g.setColour (colour::accent.withAlpha (0.4f * (1.0f - s)));
        g.drawEllipse (restX - r, restBottom - r * 0.16f, 2.0f * r, r * 0.32f, 1.5f);
    }

    // shadow: grows and darkens as the logo comes down
    {
        const auto near = 1.0f - juce::jlimit (0.0f, 1.0f, lift / fall);
        const auto w = 0.62f * markWidth * (0.35f + 0.65f * near) * (1.0f + 0.15f * impact);
        g.setColour (juce::Colours::black.withAlpha (0.55f * near * (float) clamp01 (t / 0.15)));
        g.fillEllipse (restX - w / 2.0f, restBottom - 7.0f, w, 12.0f);
    }

    // the logo: fades in, falls tilted, squashes a little on each touch and settles upright
    if (logoMark.isValid() && t > 0.0)
    {
        const auto angle = juce::degreesToRadians ((float) (-26.0 * (1.0 - easeOutBack (t / 0.95))));
        const auto squashY = 1.0f - 0.10f * impact, squashX = 1.0f + 0.06f * impact;
        const auto transform = juce::AffineTransform::scale (markWidth / (float) logoMark.getWidth() * squashX,
                                                             markHeight / (float) logoMark.getHeight() * squashY)
                                   .translated (-markWidth * squashX / 2.0f, -markHeight * squashY)
                                   .rotated (angle)
                                   .translated (restX, restBottom - lift);
        g.setOpacity ((float) clamp01 (t / 0.18));
        g.drawImageTransformed (logoMark, transform, false);
        g.setOpacity (1.0f);
    }

    // "LYNX SHARE" (v0.1.20 rename): "LYNX" big, "SHARE" under it in the accent colour, spaced out to the same
    // width; the letters slide in one after another
    {
        const juce::String word ("LYNX"), word2 ("SHARE");
        const auto& big = fonts[0];
        const auto& small = fonts[1];
        const auto left = 300.0f;
        const auto baseline = 150.0f, baseline2 = 188.0f;

        auto lynxWidth = 0.0f;
        for (int i = 0; i < word.length(); ++i)
            lynxWidth += juce::GlyphArrangement::getStringWidth (big, word.substring (i, i + 1)) + 3.0f;
        lynxWidth -= 3.0f;
        auto shareWidth = 0.0f;
        for (int i = 0; i < word2.length(); ++i)
            shareWidth += juce::GlyphArrangement::getStringWidth (small, word2.substring (i, i + 1));
        const auto tracking = juce::jmax (2.0f, (lynxWidth - shareWidth) / (float) juce::jmax (1, word2.length() - 1));

        const auto drawWord = [&] (const juce::String& w, const juce::Font& f, float y, float gap, double start, juce::Colour c)
        {
            g.setFont (f);
            auto x = left;
            for (int i = 0; i < w.length(); ++i)
            {
                const auto letter = w.substring (i, i + 1);
                const auto p = (float) easeOutCubic ((t - start - 0.075 * i) / 0.30);
                if (p > 0.0f)
                {
                    g.setColour (c.withMultipliedAlpha (p));
                    g.drawSingleLineText (letter, juce::roundToInt (x + (1.0f - p) * 26.0f), juce::roundToInt (y));
                }
                x += juce::GlyphArrangement::getStringWidth (f, letter) + gap;
            }
        };
        drawWord (word, big, baseline, 3.0f, 0.50, colour::textPrimary);
        drawWord (word2, small, baseline2, tracking, 0.68, colour::accent);

        // an accent underline that grows after the words
        const auto line = (float) easeOutCubic ((t - 0.95) / 0.35);
        if (line > 0.0f)
        {
            g.setColour (colour::accent);
            g.fillRoundedRectangle (302.0f, baseline2 + 12.0f, 54.0f * line, 4.0f, 2.0f);
        }

        const auto info = (float) easeOutCubic ((t - 1.05) / 0.35);
        if (info > 0.0f)
        {
            g.setColour (colour::textSecondary.withAlpha (info));
            g.setFont (fonts[2]);
            g.drawText (frame.subtitle, 302, (int) baseline2 + 24, 240, 22, juce::Justification::centredLeft);
            g.setColour (colour::textSecondary.withAlpha (info)); // (round 190je: 7:1; textDisabled read 3.0:1 on the card)
            g.setFont (fonts[3]);
            g.drawText (frame.version, 302, (int) baseline2 + 48, 240, 18, juce::Justification::centredLeft);
        }
    }

    // status and progress along the bottom
    {
        const auto bar = juce::Rectangle<float> (24.0f, card.getBottom() - 20.0f, card.getWidth() - 48.0f, 3.0f);
        g.setColour (colour::bg4);
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (colour::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) easeOutCubic (juce::jmin (1.0, t / juce::jmax (0.1, frame.barDuration)))), 1.5f);
        g.setColour (colour::textSecondary); // (round 190je, design190ia SG-23: was textDisabled, 3.1:1)
        g.setFont (fonts[3]);
        g.drawText (frame.status, juce::Rectangle<float> (24.0f, bar.getY() - 24.0f, card.getWidth() - 48.0f, 18.0f), juce::Justification::centredLeft);
    }

    // hairline border on top of everything
    g.setColour (colour::borderStrong);
    g.strokePath (cardShape, juce::PathStrokeType (1.0f));
}
// --- the card on a thread of its own (Windows, round 190hd) --------------------------------------
//
// The card was a JUCE window on the message thread: the start-up could not build the main window or open the audio
// device while the logo dropped (each would stop the drop), and its logo was the process's first Direct2D image, so
// the Direct3D 11 + Direct2D device (~0.35-0.4 s) was made before the card could show. Here the same drawing
// (drawSplashFrame) runs on a thread of its own with the CPU (JUCE's software renderer, JUCE patch 30: no Direct2D
// image on this thread) into a layered window of its own (UpdateLayeredWindow, as JUCE's own transparent windows do
// without Direct2D): the card shows within a few tens of milliseconds and its 60 frames a second no longer depend on
// what the message thread does. Same size, place, drop, timing and fade as the JUCE card. LYNX_SPLASH_THREAD=0: the
// JUCE card.
#if JUCE_WINDOWS
} // namespace lynx

namespace juce
{
void lynxSoftwareImagesOnThisThread (bool on); // cmake/LynxPatchJuce.cmake fix 30
}

namespace lynx
{
namespace
{
double steadySeconds()
{
    return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

class ThreadedSplash final : public StartupSplash
{
public:
    ThreadedSplash()
        : fonts (makeSplashFonts()),
          subtitle (tr ("Digital audio workstation")),
          version (tr ("Version {0}", { JUCE_APPLICATION_VERSION_STRING })),
          dropStatus (tr ("Loading the workspace...")),
          status (tr ("Starting the audio engine..."))
    {
        wake = CreateEventW (nullptr, FALSE, FALSE, nullptr);
    }

    ~ThreadedSplash() override
    {
        quit = true;
        if (wake != nullptr)
            SetEvent (wake);
        if (thread.joinable())
            thread.join();
        if (wake != nullptr)
            CloseHandle (wake);
    }

    void showOnDesktop() override
    {
        if (! thread.joinable())
            thread = std::thread ([this] { run(); });
    }

    void startClock() override { clockStart.store (steadySeconds()); }

    double getElapsed() const override
    {
        const auto start = clockStart.load();
        return start < 0.0 ? 0.0 : steadySeconds() - start;
    }

    void setStatus (const juce::String& text) override
    {
        const std::scoped_lock hold (lock);
        status = text;
    }

    void setBarDuration (double seconds) override { barDuration.store (seconds); }

    void raise() override
    {
        raiseAsked = true;
        SetEvent (wake);
    }

    void finish (std::function<void()> whenGone) override
    {
        {
            const std::scoped_lock hold (lock);
            onGone = std::move (whenGone);
        }
        fadeStart.store (steadySeconds());
        SetEvent (wake);
    }

    int getDropFrames() const override { return dropFrames.load(); }
    double getLongestDropGapMs() const override { return longestDropGapMs.load(); }

    std::chrono::steady_clock::time_point getFirstFrameTime() const override
    {
        return std::chrono::steady_clock::time_point (std::chrono::steady_clock::duration (firstFrameTicks.load()));
    }

    bool drawsOnItsOwnThread() const override { return true; }

    void whenLanded (double seconds, std::function<void()> then) override
    {
        {
            const std::scoped_lock hold (lock);
            onLanded = std::move (then);
        }
        landingAt.store (seconds);
    }

    double getLongestDropGapAt() const override { return longestDropGapAt.load(); }

    juce::String getLongestDropGapDetail() const override
    {
        const std::scoped_lock hold (lock);
        return longestGapDetail;
    }

private:
    // runs `f` on the message thread unless this card is gone by then (it is deleted there)
    void post (std::function<void (ThreadedSplash&)> f)
    {
        juce::MessageManager::callAsync ([weak = std::weak_ptr<int> (alive), self = this, f = std::move (f)] {
            if (! weak.expired())
                f (*self);
        });
    }

    void run()
    {
        juce::lynxSoftwareImagesOnThisThread (true);
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        // its window in physical pixels, like JUCE's (per-monitor DPI awareness)
        using SetContext = HANDLE (WINAPI*) (HANDLE);
        if (auto* user32 = GetModuleHandleW (L"user32.dll"))
            if (auto set = reinterpret_cast<SetContext> (reinterpret_cast<void*> (GetProcAddress (user32, "SetThreadDpiAwarenessContext"))))
                set (reinterpret_cast<HANDLE> (static_cast<std::intptr_t> (-4))); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2

        const auto logo = decodeLogoImage();

        // centred in the main display's work area (JUCE's card: the primary display's user area)
        const auto monitor = MonitorFromPoint (POINT { 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info {};
        info.cbSize = sizeof (info);
        GetMonitorInfoW (monitor, &info);
        UINT dpiX = 96, dpiY = 96;
        using GetDpi = HRESULT (WINAPI*) (HMONITOR, int, UINT*, UINT*);
        if (auto* shcore = LoadLibraryW (L"shcore.dll"))
            if (auto get = reinterpret_cast<GetDpi> (reinterpret_cast<void*> (GetProcAddress (shcore, "GetDpiForMonitor"))))
                if (FAILED (get (monitor, 0 /* MDT_EFFECTIVE_DPI */, &dpiX, &dpiY)))
                    dpiX = 96;
        const auto scale = juce::jmax (1.0, (double) dpiX / 96.0);
        const auto w = juce::roundToInt (SplashScreen::width * scale), h = juce::roundToInt (SplashScreen::height * scale);
        const auto& work = info.rcWork;
        const auto x = (work.left + work.right) / 2 - w / 2, y = (work.top + work.bottom) / 2 - h / 2;

        const auto instance = GetModuleHandleW (nullptr);
        WNDCLASSEXW windowClass {};
        windowClass.cbSize = sizeof (windowClass);
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW (nullptr, MAKEINTRESOURCEW (32512)); // (IDC_ARROW)
        windowClass.lpszClassName = L"LYNXSplashCard";
        RegisterClassExW (&windowClass);
        // no title (nothing looks for it), no taskbar entry, never activated
        const auto window = CreateWindowExW (WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass.lpszClassName, L"",
                                             WS_POPUP, x, y, w, h, nullptr, nullptr, instance, nullptr);

        BITMAPINFO bitmapInfo {};
        bitmapInfo.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = w;
        bitmapInfo.bmiHeader.biHeight = -h; // (top-down)
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        const auto dib = window != nullptr ? CreateDIBSection (nullptr, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        const auto memoryDc = CreateCompatibleDC (nullptr);
        const auto oldBitmap = dib != nullptr ? SelectObject (memoryDc, dib) : nullptr;
        // (high resolution where Windows has it: 60 frames a second on the dot)
        auto timer = CreateWaitableTimerExW (nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
        if (timer == nullptr)
            timer = CreateWaitableTimerW (nullptr, FALSE, nullptr);

        if (window == nullptr || dib == nullptr || bits == nullptr)
        {
            // (no card: the start-up goes on all the same)
            firstFrameTicks.store (std::chrono::steady_clock::now().time_since_epoch().count());
            post ([] (ThreadedSplash& s) { if (auto first = std::exchange (s.onFirstFrame, nullptr)) first(); });
            while (! quit && fadeStart.load() < 0.0)
                WaitForSingleObject (wake, 50);
            if (! quit)
                post ([] (ThreadedSplash& s) { s.gone(); });
            cleanUp (window, memoryDc, oldBitmap, dib, timer);
            return;
        }

        juce::Image frame (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        // every glyph the card will draw, once, before its first frame (an outline looked up mid-drop waited for the
        // font system while the message thread loaded the window's fonts: a frame of ~0.1 s)
        {
            SplashFrame warm;
            warm.t = 1.6;
            warm.status = dropStatus + " " + status;
            warm.subtitle = subtitle;
            warm.version = version;
            warm.logo = &logo;
            warm.fonts = fonts.data();
            juce::Graphics g (frame);
            g.addTransform (juce::AffineTransform::scale ((float) scale));
            drawSplashFrame (g, warm);
        }
        constexpr double period = 1.0 / 60.0;
        auto next = steadySeconds();
        auto shown = false, faded = false;
        auto lastDropPaint = -1.0;

        while (! quit)
        {
            const auto now = steadySeconds();
            const auto start = clockStart.load(), fade = fadeStart.load();
            const auto t = start < 0.0 ? 0.0 : now - start;
            auto alpha = 1.0;
            if (fade >= 0.0)
            {
                const auto f = (now - fade) / SplashScreen::fadeOutTime;
                alpha = 1.0 - clamp01 (f);
                if (f >= 1.0)
                {
                    faded = true;
                    break;
                }
            }
            if (start >= 0.0 && fade < 0.0 && t <= 1.05) // (the drop's frames, as the JUCE card counts them)
            {
                if (lastDropPaint >= 0.0 && (now - lastDropPaint) * 1000.0 > longestDropGapMs.load())
                {
                    longestDropGapMs.store ((now - lastDropPaint) * 1000.0);
                    longestDropGapAt.store (t);
                    const std::scoped_lock hold (lock); // (what the frame before it spent its time on)
                    longestGapDetail = "draw " + juce::String (spent[0], 1) + " ms, show " + juce::String (spent[1], 1) + " ms, messages "
                                       + juce::String (spent[2], 1) + " ms, wait " + juce::String (spent[3], 1) + " ms";
                }
                ++dropFrames;
                lastDropPaint = now;
            }
            if (const auto landing = landingAt.load(); start >= 0.0 && landing >= 0.0 && t >= landing)
            {
                landingAt.store (-1.0);
                post ([] (ThreadedSplash& s) {
                    std::function<void()> then;
                    {
                        const std::scoped_lock hold (s.lock);
                        then = std::move (s.onLanded);
                    }
                    if (then)
                        then(); // (may delete this card)
                });
            }

            auto mark = steadySeconds();
            const auto lap = [&mark] (double& into) { const auto n = steadySeconds(); into = (n - mark) * 1000.0; mark = n; };
            SplashFrame look;
            look.t = t;
            look.barDuration = barDuration.load();
            {
                const std::scoped_lock hold (lock);
                look.status = status;
            }
            look.subtitle = subtitle;
            look.version = version;
            look.logo = &logo;
            look.fonts = fonts.data();
            frame.clear (frame.getBounds());
            {
                juce::Graphics g (frame);
                g.addTransform (juce::AffineTransform::scale ((float) scale));
                drawSplashFrame (g, look);
            }
            {
                const juce::Image::BitmapData pixels (frame, juce::Image::BitmapData::readOnly);
                for (int row = 0; row < h; ++row) // (premultiplied BGRA, as UpdateLayeredWindow wants)
                    std::memcpy (static_cast<std::uint8_t*> (bits) + (std::size_t) row * (std::size_t) w * 4, pixels.getLinePointer (row), (std::size_t) w * 4);
            }
            lap (spent[0]);
            POINT source { 0, 0 }, place { x, y };
            SIZE size { w, h };
            BLENDFUNCTION blend { AC_SRC_OVER, 0, (BYTE) juce::roundToInt (alpha * 255.0), AC_SRC_ALPHA };
            UpdateLayeredWindow (window, nullptr, &place, &size, memoryDc, &source, 0, &blend, ULW_ALPHA);
            if (raiseAsked.exchange (false))
                SetWindowPos (window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (! std::exchange (shown, true))
            {
                ShowWindow (window, SW_SHOWNOACTIVATE);
                firstFrameTicks.store (std::chrono::steady_clock::now().time_since_epoch().count());
                post ([] (ThreadedSplash& s) { if (auto first = std::exchange (s.onFirstFrame, nullptr)) first(); });
            }
            lap (spent[1]);

            MSG message;
            while (PeekMessageW (&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage (&message);
                DispatchMessageW (&message);
            }
            lap (spent[2]);

            // the next tick of a 60 Hz clock (a tick missed is skipped, as JUCE's timer does)
            next += period;
            const auto later = steadySeconds();
            while (next <= later)
                next += period;
            LARGE_INTEGER due;
            due.QuadPart = -juce::jmax ((LONGLONG) 1, (LONGLONG) ((next - later) * 1.0e7)); // (relative, 100 ns units)
            // (the timeout: the tick plus a little, in case the timer never signals; never a long wait)
            const auto timeoutMs = (DWORD) juce::jmax (1, (int) std::ceil ((next - later) * 1000.0) + 2);
            if (timer != nullptr && SetWaitableTimer (timer, &due, 0, nullptr, nullptr, FALSE))
            {
                const HANDLE waitFor[] = { timer, wake };
                MsgWaitForMultipleObjects (2, waitFor, FALSE, timeoutMs, QS_ALLINPUT);
            }
            else
            {
                MsgWaitForMultipleObjects (1, &wake, FALSE, timeoutMs, QS_ALLINPUT);
            }
            lap (spent[3]);
        }

        ShowWindow (window, SW_HIDE);
        cleanUp (window, memoryDc, oldBitmap, dib, timer);
        if (faded && ! quit)
            post ([] (ThreadedSplash& s) { s.gone(); });
    }

    static void cleanUp (HWND window, HDC memoryDc, HGDIOBJ oldBitmap, HBITMAP dib, HANDLE timer)
    {
        if (window != nullptr)
            DestroyWindow (window);
        if (oldBitmap != nullptr)
            SelectObject (memoryDc, oldBitmap);
        if (dib != nullptr)
            DeleteObject (dib);
        if (memoryDc != nullptr)
            DeleteDC (memoryDc);
        if (timer != nullptr)
            CloseHandle (timer);
    }

    // (message thread) the fade has ended
    void gone()
    {
        std::function<void()> then;
        {
            const std::scoped_lock hold (lock);
            then = std::move (onGone);
        }
        if (then)
            then(); // (may delete this card)
    }

    const std::array<juce::Font, 4> fonts;
    const juce::String subtitle, version, dropStatus;
    mutable std::mutex lock; // guards status, onGone, onLanded and longestGapDetail
    juce::String status;
    std::function<void()> onGone, onLanded;
    std::atomic<double> landingAt { -1.0 }, longestDropGapAt { -1.0 };
    double spent[4] {}; // (the splash thread) the last frame's draw, show, messages, wait, ms
    juce::String longestGapDetail; // (guarded by lock)
    std::atomic<double> clockStart { -1.0 }, fadeStart { -1.0 }, barDuration { SplashScreen::minimumDuration };
    std::atomic<bool> quit { false }, raiseAsked { false };
    std::atomic<int> dropFrames { 0 };
    std::atomic<double> longestDropGapMs { 0.0 };
    std::atomic<std::chrono::steady_clock::rep> firstFrameTicks { 0 };
    HANDLE wake = nullptr;
    std::shared_ptr<int> alive = std::make_shared<int> (0);
    std::thread thread; // (last)
};
} // namespace
#endif

juce::Array<juce::File> writeSplashShots (const juce::File& folder)
{
    juce::Array<juce::File> written;
    folder.createDirectory();
    const auto fonts = makeSplashFonts();
    const auto gpuLogo = decodeLogoImage();
#if JUCE_WINDOWS
    juce::lynxSoftwareImagesOnThisThread (true);
#endif
    const auto cpuLogo = decodeLogoImage();
#if JUCE_WINDOWS
    juce::lynxSoftwareImagesOnThisThread (false);
#endif
    for (const auto scale : { 1.0f, 1.5f })
        for (const auto t : { 0.0, 0.29, 0.58, 1.0, 1.6 })
            for (const auto cpu : { false, true })
            {
                const juce::SoftwareImageType software;
                const juce::NativeImageType native;
                const juce::ImageType& type = cpu ? static_cast<const juce::ImageType&> (software) : static_cast<const juce::ImageType&> (native);
                juce::Image image (juce::Image::ARGB, juce::roundToInt (SplashScreen::width * scale), juce::roundToInt (SplashScreen::height * scale), true, type);
                {
                    juce::Graphics g (image);
                    g.addTransform (juce::AffineTransform::scale (scale));
                    SplashFrame frame;
                    frame.t = t;
                    frame.barDuration = 1.05;
                    frame.status = tr ("Loading the workspace...");
                    frame.subtitle = tr ("Digital audio workstation");
                    frame.version = tr ("Version {0}", { JUCE_APPLICATION_VERSION_STRING });
                    frame.logo = cpu ? &cpuLogo : &gpuLogo;
                    frame.fonts = fonts.data();
                    drawSplashFrame (g, frame);
                }
                const auto file = folder.getChildFile ("splash-" + juce::String (juce::roundToInt (scale * 100)) + "-" + juce::String (t, 2) + (cpu ? "-cpu" : "-gpu") + ".png");
                file.deleteFile();
                if (juce::FileOutputStream out (file); out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out))
                    written.add (file);
            }
    return written;
}

std::unique_ptr<StartupSplash> makeStartupSplash()
{
#if JUCE_WINDOWS
    if (juce::SystemStats::getEnvironmentVariable ("LYNX_SPLASH_THREAD", {}) != "0")
        return std::make_unique<ThreadedSplash>();
#endif
    return std::make_unique<SplashScreen>();
}
} // namespace lynx

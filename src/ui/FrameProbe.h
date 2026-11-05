#pragma once

// Frame timing probe (round 173f, discussion #10 "is the DAW fluid?"): a transparent component on top of the main
// window that sees every frame the window paints. JUCE paints a window region by region and skips the paint() of a
// component its opaque children cover, so the window's own paint() misses most frames (a meter or a fader repaints
// only its rectangle); this probe covers the whole window, never draws and never takes the mouse, so its paint() runs
// once per painted frame with that frame's dirty area as the clip.
// Installed only by --uiperf and LYNX_TRACE_UI (ui.log then gets the frames per second and the longest gap).

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <functional>
#include <vector>

namespace lynx::ui
{
class FrameProbe final : public juce::Component
{
public:
    FrameProbe()
    {
        setInterceptsMouseClicks (false, false);
        setWantsKeyboardFocus (false);
        setOpaque (false);
        setAlwaysOnTop (true);
        setAccessible (false);
    }

    // message thread: the time of the frame (ms, Time::getMillisecondCounterHiRes) and the bounds of its dirty area
    std::function<void (double, juce::Rectangle<int>)> onFrame;

    void paint (juce::Graphics& g) override
    {
        if (onFrame)
            onFrame (juce::Time::getMillisecondCounterHiRes(), g.getClipBounds());
    }
};

// Frame times → the numbers a report prints: frames per second, the gaps between frames (p50 / p95 / p99 / max) and
// how many gaps a user sees as a hitch (> 50 ms).
struct FrameStats
{
    int frames = 0;
    double fps = 0.0, p50 = 0.0, p95 = 0.0, p99 = 0.0, max = 0.0;
    int hitches = 0;

    static FrameStats of (const std::vector<double>& times, double spanMs)
    {
        FrameStats s;
        s.frames = (int) times.size();
        s.fps = spanMs > 0.0 ? 1000.0 * (double) times.size() / spanMs : 0.0;
        std::vector<double> gaps;
        for (std::size_t i = 1; i < times.size(); ++i)
            gaps.push_back (times[i] - times[i - 1]);
        if (gaps.empty())
            return s;
        std::sort (gaps.begin(), gaps.end());
        const auto at = [&gaps] (double q) { return gaps[std::min (gaps.size() - 1, (std::size_t) (q * (double) gaps.size()))]; };
        s.p50 = at (0.5);
        s.p95 = at (0.95);
        s.p99 = at (0.99);
        s.max = gaps.back();
        s.hitches = (int) std::count_if (gaps.begin(), gaps.end(), [] (double g) { return g > 50.0; });
        return s;
    }

    juce::String describe() const
    {
        return juce::String (frames) + " frames, " + juce::String (fps, 1) + " fps, gap p50 " + juce::String (p50, 1) + " / p95 "
               + juce::String (p95, 1) + " / p99 " + juce::String (p99, 1) + " / max " + juce::String (max, 1) + " ms, gaps > 50 ms "
               + juce::String (hitches);
    }
};
} // namespace lynx::ui

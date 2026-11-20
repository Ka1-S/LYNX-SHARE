#pragma once

// Live's insert marker (round 190mk, details DL-70; capture 190mf B1 films/livemark, live-insert-marker.png,
// live-status-hover.png): a click on an empty spot of an Arrangement lane puts a 1 px vertical line over that lane's
// height (17 px, the 2 px separator below it stays). It does not blink on and off: it pulses smoothly between cyan
// (3,195,213) and white as a triangle, ~1200 ms a period (600 ms each way). The status bar names it while the pointer
// is over the lanes: "Insert Mark 63.1.1 (Time: 1:29:102)" (bars.beats.sixteenths, then m:ss:ms). Clip-launch layout,
// Live's look only. No JUCE: pure logic (the colour as 0xAARRGGBB, the texts as std::string).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace lynx::insertmark
{
constexpr double periodMs = 1200.0;
constexpr std::uint32_t cyan = 0xff03c3d5; // (3,195,213)
constexpr std::uint32_t white = 0xffffffff;

// 0 = cyan, 1 = white: a triangle starting cyan at the click, white at half a period
inline double pulse (double msSinceSet) noexcept
{
    if (! (msSinceSet > 0.0))
        return 0.0;
    const auto phase = std::fmod (msSinceSet, periodMs) / periodMs; // 0..1
    return phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0;
}

inline std::uint32_t colourAt (double msSinceSet) noexcept
{
    const auto t = pulse (msSinceSet);
    const auto mix = [t] (int a, int b) { return (std::uint32_t) std::lround ((double) a + ((double) b - (double) a) * t); };
    return 0xff000000u | (mix (3, 255) << 16) | (mix (195, 255) << 8) | mix (213, 255);
}

// "63.1.1": Live's bars.beats.sixteenths (all 1-based); `beatTicks` = the beat of the meter in force (a quarter in 4/4)
inline std::string positionText (int bar, std::int64_t ticksIntoBar, std::int64_t beatTicks, std::int64_t sixteenthTicks)
{
    if (beatTicks <= 0 || sixteenthTicks <= 0)
        return std::to_string (bar) + ".1.1";
    const auto into = ticksIntoBar < 0 ? 0 : ticksIntoBar;
    const auto beat = into / beatTicks + 1;
    const auto sixteenth = (into % beatTicks) / sixteenthTicks + 1;
    return std::to_string (bar) + "." + std::to_string (beat) + "." + std::to_string (sixteenth);
}

// "1:29:102": minutes, seconds (2 digits), milliseconds (3 digits), as Live's status bar
inline std::string timeText (double seconds)
{
    const auto ms = (long long) std::floor ((seconds > 0.0 ? seconds : 0.0) * 1000.0 + 1.0e-6);
    char text[48];
    std::snprintf (text, sizeof (text), "%lld:%02lld:%03lld", ms / 60000, (ms / 1000) % 60, ms % 1000);
    return text;
}
} // namespace lynx::insertmark

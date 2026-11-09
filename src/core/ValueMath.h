#pragma once

// The arithmetic of LYNX's control contract (docs/WORKFLOW_FL.md, P1 items 7 and 15): how a drag,
// a fine drag or a wheel notch changes a value, the tempo display's digit drag, and tap tempo.
// Header-only, no JUCE.

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "core/Song.h"

namespace lynx::controls
{
constexpr double fineFactor = 0.1;      // Ctrl+drag / Ctrl+wheel move 10x less
constexpr double ultraFineFactor = 0.01; // Ctrl+Shift: 100x less (round 177d, LYNX's own: precise thresholds and gains)

// How much a drag or wheel step is scaled by the modifiers held: Ctrl = fine, Ctrl+Shift = ultra-fine.
inline double fineScale (bool ctrl, bool shift) noexcept { return ! ctrl ? 1.0 : shift ? ultraFineFactor : fineFactor; }
constexpr double wheelStep = 1.0 / 50;  // one wheel notch = 2% of the range

// Value after dragging `pixelsUp` pixels (positive = up/right) from `start`; `pixelsForRange` pixels
// sweep the whole range.
inline double dragValueScaled (double start, double pixelsUp, double minimum, double maximum, double pixelsForRange, double scale) noexcept
{
    const auto perPixel = (maximum - minimum) / std::max (1.0, pixelsForRange) * scale;
    return std::clamp (start + pixelsUp * perPixel, minimum, maximum);
}
inline double dragValue (double start, double pixelsUp, double minimum, double maximum, double pixelsForRange, bool fine) noexcept
{
    return dragValueScaled (start, pixelsUp, minimum, maximum, pixelsForRange, fine ? fineFactor : 1.0);
}

// How many wheel notches a wheel event moved (never 0 for a move). `notchSize` is what one notch of a plain mouse wheel
// gives (JUCE's deltaY: 120 × 0.5 / 256 on Windows, 50 / 256 on Linux / X11; macOS lines are smaller): a notch moves
// a control by one wheel step, as in FL (round 168s, issue #32: one notch over an FL fader = 2 % [SHOT] r168s c_f07;
// LYNX counted 1.95 notches on Windows and the faders jumped twice as far).
inline double wheelNotches (double delta, double notchSize) noexcept
{
    if (delta == 0.0)
        return 0.0;
    const auto n = std::max (1.0, std::round (std::abs (delta) / std::max (1.0e-6, notchSize)));
    return delta > 0.0 ? n : -n;
}

// Wheel steps that move a mixer slot (round 170b, Busy Works on a MacBook trackpad): a mouse wheel's notch is one step;
// a trackpad's (`smooth`) small deltas add up in `accumulator` until one step's worth (`smoothStep`), and its inertia
// (the glide after the fingers leave) moves nothing, so one swipe moves the effect a slot or two, not to the end.
// Returns the signed steps to apply now (positive = up).
inline int slotWheelSteps (double& accumulator, double delta, bool smooth, bool inertial, double smoothStep = 0.3) noexcept
{
    if (inertial || delta == 0.0)
        return 0;
    if (! smooth)
    {
        accumulator = 0.0;
        return delta > 0.0 ? 1 : -1;
    }
    if ((accumulator > 0.0) != (delta > 0.0))
        accumulator = 0.0; // (the direction changed)
    accumulator += delta;
    const auto steps = (int) (accumulator / smoothStep);
    accumulator -= steps * smoothStep;
    return steps;
}

// Value after `notches` wheel notches (positive = up). `interval` > 0: at least one interval per notch.
inline double wheelValueScaled (double value, double notches, double minimum, double maximum, double scale, double interval = 0.0) noexcept
{
    auto delta = notches * (maximum - minimum) * wheelStep * scale;

    if (interval > 0.0 && std::abs (delta) < interval && delta != 0.0)
        delta = delta < 0.0 ? -interval : interval;

    return std::clamp (value + delta, minimum, maximum);
}
inline double wheelValue (double value, double notches, double minimum, double maximum, bool fine, double interval = 0.0) noexcept
{
    return wheelValueScaled (value, notches, minimum, maximum, fine ? fineFactor : 1.0, interval);
}

// A wheel that scrolls a view keeps scrolling it (round 169c, issue #32: "the mixer faders react to the mouse wheel
// accidentally while scrolling"). LYNX's mixer scrolls its strips with the wheel over the parts that are no control
// (FL's mixer does not scroll there at all [SHOT] r169c f03–f05), so a spin that started on a strip's blank part and
// then passed over a fader, knob or the EQ moved that control. The scroll goes on while the wheel keeps turning (events
// less than `holdMs` apart); a wheel over a control after a pause changes it, as in FL.
struct WheelScrollLatch
{
    static constexpr double holdMs = 400.0;
    double lastScrollMs = -1.0e12;
    void scrolled (double nowMs) noexcept { lastScrollMs = nowMs; }
    bool scrolling (double nowMs) const noexcept { return nowMs >= lastScrollMs && nowMs - lastScrollMs < holdMs; }
};

inline double snapToInterval (double value, double minimum, double interval) noexcept
{
    return interval > 0.0 ? minimum + std::round ((value - minimum) / interval) * interval : value;
}

// --- tempo -------------------------------------------------------------------------------------

inline double clampTempo (double bpm) noexcept
{
    return std::clamp (bpm, minTempo, maxTempo);
}

// The tempo display is dragged by its digits: on the whole number one BPM per `pixelsPerStep` pixels,
// on the decimals 0.01 BPM per pixel (Ctrl: ten times finer on either).
inline double tempoDrag (double startBpm, double pixelsUp, bool onDecimals, bool fine) noexcept
{
    constexpr double pixelsPerStep = 4.0;
    double delta = onDecimals ? pixelsUp * 0.01 : std::trunc (pixelsUp / pixelsPerStep);

    if (fine)
        delta = onDecimals ? pixelsUp * 0.001 : pixelsUp / pixelsPerStep * 0.1;

    auto bpm = startBpm + delta;
    bpm = onDecimals || fine ? std::round (bpm * 1000.0) / 1000.0 : std::round (bpm * 100.0) / 100.0;
    return clampTempo (bpm);
}

inline double halfTempo (double bpm) noexcept { return clampTempo (bpm / 2.0); }
inline double doubleTempo (double bpm) noexcept { return clampTempo (bpm * 2.0); }

// Tap tempo: the tempo follows the average interval of the recent taps. A pause longer than
// `resetMs` starts over. A tempo is available from the second tap on.
class TapTempo
{
public:
    static constexpr double resetMs = 2000.0;
    static constexpr std::size_t maxTaps = 8;

    // `timeMs`: a monotonic clock. Returns the tempo once there are at least two taps.
    std::optional<double> tap (double timeMs)
    {
        if (! taps.empty() && (timeMs - taps.back() > resetMs || timeMs <= taps.back()))
            taps.clear();

        taps.push_back (timeMs);

        if (taps.size() > maxTaps)
            taps.erase (taps.begin());

        if (taps.size() < 2)
            return std::nullopt;

        const auto interval = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        return clampTempo (std::round (60000.0 / interval * 100.0) / 100.0);
    }

    int count() const noexcept { return (int) taps.size(); }
    void reset() { taps.clear(); }

    // What the tempo tapper shows: waiting for the second tap, tapping (the tempo still moves), or stable (at least
    // four taps whose intervals all stay within 4% of their mean).
    enum class State
    {
        waiting,
        tapping,
        stable
    };

    State state() const noexcept
    {
        if (taps.size() < 2)
            return State::waiting;
        if (taps.size() < 4)
            return State::tapping;

        const auto mean = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        for (std::size_t i = 1; i < taps.size(); ++i)
            if (std::abs ((taps[i] - taps[i - 1]) - mean) > 0.04 * mean)
                return State::tapping;
        return State::stable;
    }

    // The time of the last tap (ms), or a negative value before the first.
    double lastTap() const noexcept { return taps.empty() ? -1.0 : taps.back(); }

private:
    std::vector<double> taps;
};

// The tempo tapper's "Sync": a tap during playback puts the playhead on the nearest beat (ticks, core PPQ).
inline double nearestBeatTicks (double ticks) noexcept
{
    return std::max (0.0, std::round (ticks / (double) ppq) * (double) ppq);
}

// The tempo tapper's nudge buttons: while one is held the tempo runs `percent` faster or slower to drift the beat.
inline double nudgedTempo (double bpm, bool faster, double percent = 4.0) noexcept
{
    return clampTempo (bpm * (1.0 + (faster ? percent : -percent) / 100.0));
}

// --- utility volumes (metronome, Browser preview) ----------------------------------------------

// A utility volume knob: 0..125%, 100% = the default level. The gain follows the square of the knob (−6 dB at
// about 71%, −12 dB at 50%), so the knob's travel feels even to the ear.
constexpr double maxUtilityVolume = 1.25;

inline float utilityGain (double volume) noexcept
{
    if (! std::isfinite (volume))
        return 1.0f;
    const auto v = std::clamp (volume, 0.0, maxUtilityVolume);
    return (float) (v * v);
}

inline float metronomeGain (double volume) noexcept { return utilityGain (volume); }
inline float previewGain (double volume) noexcept { return utilityGain (volume); }

inline double clampUtilityVolume (double volume) noexcept
{
    return std::isfinite (volume) ? std::clamp (volume, 0.0, maxUtilityVolume) : 1.0;
}
} // namespace lynx::controls

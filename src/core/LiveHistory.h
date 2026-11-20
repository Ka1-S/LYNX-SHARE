#pragma once
// Round 190kl (issue #197): the history behind a scrolling live display (the Peak Controller's outputs), advanced by the
// time that passed, not by the number of timer calls. JUCE drops timer calls while the message thread is busy (a knob
// being dragged rebuilds the song and repaints the editor), so a display that moved one column per call slowed down
// and sped up with the editing. Here the columns sit on a fixed clock (one per 33 ms): a late call adds every column it
// missed, drawn as a straight line from the last value to the new one, so the picture always scrolls at the same speed.
// JUCE-free (tests/U190klTests.inc).

#include <array>
#include <cmath>
#include <cstdint>

namespace lynx::livehistory
{
template <int Channels, int Length>
struct History
{
    static constexpr int length = Length;
    static constexpr double columnMs = 33.0; // ~30 columns a second: the editors' timer interval (JUCE's startTimerHz (30))
    std::array<std::array<float, Channels>, Length> values {};
    int pos = 0, count = 0;
    long long column = -1; // the clock's column of the newest value (-1 = none yet)
    double originMs = 0.0;  // where the clock's columns start: half a column before the first call, so a caller that
                            // runs at about the column rate (the editors' 30 Hz timer) never sits on a boundary and its
                            // jitter does not turn into 0, 2, 0, 2 columns a call

    void push (const std::array<float, Channels>& v) noexcept
    {
        values[(std::size_t) pos] = v;
        pos = (pos + 1) % Length;
        count = count < Length ? count + 1 : Length;
    }

    const std::array<float, Channels>& newest() const noexcept { return values[(std::size_t) ((pos + Length - 1) % Length)]; }

    // The columns up to `nowMs` (any steady clock in ms) with `v` as the newest value; returns how many were added
    // (0 = the picture did not move). The first call adds one. A clock that went back starts the history's clock
    // again from there.
    int advance (double nowMs, const std::array<float, Channels>& v) noexcept
    {
        if (column < 0 || count == 0 || nowMs < originMs + (double) column * columnMs)
        {
            originMs = nowMs - columnMs * 0.5;
            column = 0;
            push (v);
            return 1;
        }
        const auto now = (long long) std::floor ((nowMs - originMs) / columnMs);
        const auto due = now - column;
        if (due <= 0)
            return 0;
        column = now;
        const auto n = (int) (due < Length ? due : Length);
        const auto from = newest();
        for (int k = 1; k <= n; ++k)
        {
            // (a gap longer than the picture: only its end is seen, still the straight line towards `v`)
            const auto t = (float) (due - n + k) / (float) due;
            std::array<float, Channels> x {};
            for (int c = 0; c < Channels; ++c)
                x[(std::size_t) c] = from[(std::size_t) c] + (v[(std::size_t) c] - from[(std::size_t) c]) * t;
            push (x);
        }
        return n;
    }

    // value `i` (0 = the oldest of the `count` held), channel `c`
    float at (int i, int c) const noexcept { return values[(std::size_t) ((pos - count + i + 2 * Length) % Length)][(std::size_t) c]; }
};
} // namespace lynx::livehistory

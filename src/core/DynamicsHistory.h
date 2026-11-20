#pragma once

// FL's scrolling dynamics displays (round 190jd, live check 190iw Q7: q7-fl-limiter-playing-sheet.png,
// q7-fl-maximus-playing-sheet.png): while the song plays, Fruity Limiter's analysis display and Maximus' Monitor graph
// scroll right to left — the input level as a light fill, the part of it above the output (what was taken off) in
// purple, and the gain envelope as a white line; the static lines (Limiter: ceiling white, threshold cyan; Maximus: the
// blue 0 dB line), the knobs and the curves stay still. Measured: ~87 px a second at 100 % on both (the Limiter's
// first column crosses 385 px in 4.47 s, Maximus' 369 px in 4.17 s); nothing moves while stopped.
//
// The vertical axis is FL's dynamics level axis: v = log3 (1 + 2 * level) / 2 (0 = silence, 0.5 = 0 dBFS, 1 = +12 dB,
// level 4), the inverse of the plugins' law3 control law, so the Limiter's ceiling knob (0..+12 dB) sits at its own
// position (the capture's white line at 49 % = 0 dB). No JUCE: pure logic, fed with the slot's input / output peaks.

#include <algorithm>
#include <cmath>
#include <vector>

namespace lynx::dynhistory
{
constexpr double flPixelsPerSecond = 87.0; // at 100 % UI scale: one column per pixel
constexpr double columnMs = 1000.0 / flPixelsPerSecond;

// FL's level axis (0..1 from the bottom) of a linear level.
inline float axis (float level) noexcept
{
    if (! (level > 0.0f))
        return 0.0f;
    return std::clamp ((float) (std::log (1.0 + 2.0 * (double) level) / std::log (3.0) / 2.0), 0.0f, 1.0f);
}

struct Column
{
    float in = 0.0f;  // the input's peak (linear)
    float out = 0.0f; // the output's peak (linear)
    float gain = 1.0f; // out / in while there is input, else the last one (FL's line runs on through the gaps)
    bool started = false; // there was input at or before this column (FL's gain line starts with the first sound)
};

class History
{
public:
    explicit History (int capacityIn = 1024) : columns ((std::size_t) std::max (2, capacityIn)) {}

    // the loudest input / output since the last column (linear peaks, from the audio side's meters)
    void feed (float inPeak, float outPeak) noexcept
    {
        pendingIn = std::max (pendingIn, std::max (0.0f, inPeak));
        pendingOut = std::max (pendingOut, std::max (0.0f, outPeak));
    }

    // `elapsedMs` of playback: the columns it adds (one per columnMs; the fed peaks go into all of them). Call only
    // while the transport plays: stopped, the display stands still.
    int advance (double elapsedMs) noexcept
    {
        clock += std::clamp (elapsedMs, 0.0, 1000.0);
        int added = 0;
        while (clock >= columnMs)
        {
            clock -= columnMs;
            Column c;
            c.in = pendingIn;
            c.out = pendingOut;
            c.gain = pendingIn > 1.0e-4f ? std::clamp (pendingOut / pendingIn, 0.0f, 4.0f) : lastGain;
            seen = seen || pendingIn > 1.0e-4f;
            c.started = seen;
            lastGain = c.gain;
            head = (head + 1) % (int) columns.size();
            columns[(std::size_t) head] = c;
            count = std::min (count + 1, (int) columns.size());
            ++added;
        }
        if (added > 0)
            pendingIn = pendingOut = 0.0f;
        total += added;
        return added;
    }

    int size() const noexcept { return count; }
    long long pushed() const noexcept { return total; } // columns ever added (the painter repaints when it changes)
    int capacity() const noexcept { return (int) columns.size(); }

    // the column `age` columns back (0 = the newest); only for age < size()
    const Column& at (int age) const noexcept
    {
        const auto n = (int) columns.size();
        return columns[(std::size_t) (((head - age) % n + n) % n)];
    }

    void clear() noexcept
    {
        count = 0;
        clock = 0.0;
        pendingIn = pendingOut = 0.0f;
        lastGain = 1.0f;
        seen = false;
    }

private:
    std::vector<Column> columns;
    int head = 0, count = 0;
    long long total = 0;
    double clock = 0.0;
    float pendingIn = 0.0f, pendingOut = 0.0f, lastGain = 1.0f;
    bool seen = false;
};
} // namespace lynx::dynhistory

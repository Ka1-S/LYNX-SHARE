#pragma once

// A generated control record for the DVS tests (core/Dvs.h): what a timecode vinyl / CD plays, written from the
// format's description (REA_rodada167c §1.2) — a sine carrier on both channels a quarter cycle apart, each cycle's
// peak loud (bit 1) or quiet (bit 0) to spell the register's bit sequence.

#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "core/Dvs.h"

namespace dvstest
{
struct Signal
{
    std::vector<float> left, right;
    std::vector<double> place; // the record's place under the needle at each frame (seconds, as the decoder names it)
};

// `speed(t)`: the record's speed at time t (1 = normal, negative = backwards), from `start` seconds on the record
inline Signal make (const lynx::dvs::Format& f, double start, const std::function<double (double)>& speed, int frames,
                    double rate, float amp = 0.5f, float quiet = 0.7f)
{
    // the cycles the signal can reach, and their bits (the register's newest bit at each cycle)
    double c = (start + f.offset) * f.resolution;
    double lo = c, hi = c;
    {
        double x = c;
        for (int i = 0; i < frames; ++i)
        {
            x += speed (i / rate) * f.resolution / rate;
            lo = std::min (lo, x);
            hi = std::max (hi, x);
        }
    }
    const auto first = (std::int64_t) std::max (0.0, std::floor (lo) - 2.0);
    const auto last = (std::int64_t) std::ceil (hi) + 2;
    std::vector<std::uint8_t> bits ((std::size_t) (last - first + 1));
    auto s = f.seed;
    for (std::int64_t i = 0; i <= last; ++i)
    {
        if (i >= first)
            bits[(std::size_t) (i - first)] = (std::uint8_t) (s & 1u);
        s = lynx::dvs::nextState (s, f);
    }

    Signal out;
    out.left.resize ((std::size_t) frames);
    out.right.resize ((std::size_t) frames);
    out.place.resize ((std::size_t) frames);
    const bool flip = (f.flags & 0x10) != 0, leftPrimary = (f.flags & 0x40) != 0;
    for (int i = 0; i < frames; ++i)
    {
        const auto cycle = (std::int64_t) std::floor (c);
        const auto phase = 2.0 * 3.141592653589793 * (c - (double) cycle);
        const auto bit = cycle >= first && cycle <= last ? bits[(std::size_t) (cycle - first)] : 0;
        const auto a = (double) amp * (bit != 0 ? 1.0 : (double) quiet);
        const auto primary = (float) (a * std::sin (phase));
        const auto secondary = (float) (a * (flip ? std::cos (phase) : -std::cos (phase)));
        out.left[(std::size_t) i] = leftPrimary ? primary : secondary;
        out.right[(std::size_t) i] = leftPrimary ? secondary : primary;
        out.place[(std::size_t) i] = c / f.resolution - f.offset;
        c += speed (i / rate) * f.resolution / rate;
    }
    return out;
}
} // namespace dvstest

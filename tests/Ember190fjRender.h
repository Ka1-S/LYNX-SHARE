#pragma once

// Round 190fj: the render the Ember law tests hash (tests/Ember190fjTests.inc) — also built against the base commit's
// objects to make the classic goldens (/mnt/e/lynx-work/ember190fj/golden).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/Effects.h"

namespace ember190fj
{
struct Render
{
    std::uint64_t hash;
    double energy, a, b;
};

// 1.5 s in blocks of 256: a 1 kHz sine rising from −30 to 0 dBFS (0 … 0.5 s), 110 Hz + 2.5 kHz at −6 dBFS (0.5 … 1 s),
// a −12 dBFS noise burst (1 … 1.2 s), silence. Left = the signal, right = 0.7 × it plus a quiet 330 Hz tone.
inline Render render (const lynx::EffectSlot& slot, double sr)
{
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    const int n = (int) (1.5 * sr), block = 256;
    std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
    unsigned s = 190u;
    auto noise = [&s] { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 8388608.0f - 1.0f; };
    constexpr double tau = 6.283185307179586;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        double v = 0.0;
        if (t < 0.5)
            v = std::pow (10.0, (-30.0 + 60.0 * t) / 20.0) * std::sin (tau * 1000.0 * t);
        else if (t < 1.0)
            v = 0.25 * std::sin (tau * 110.0 * t) + 0.25 * std::sin (tau * 2500.0 * t);
        else if (t < 1.2)
            v = 0.25 * noise();
        l[(std::size_t) i] = (float) v;
        r[(std::size_t) i] = (float) (0.7 * v + (t < 1.2 ? 0.02 * std::sin (tau * 330.0 * t) : 0.0));
    }
    for (int off = 0; off < n; off += block)
        fx->process (l.data() + off, r.data() + off, std::min (block, n - off), slot.params, 120.0);
    Render out { 1469598103934665603ull, 0.0, l[(std::size_t) (n / 4)], r[(std::size_t) (n * 3 / 5)] };
    for (int i = 0; i < n; ++i)
        for (const float v : { l[(std::size_t) i], r[(std::size_t) i] })
        {
            std::uint32_t u;
            std::memcpy (&u, &v, 4);
            for (int b = 0; b < 4; ++b)
            {
                out.hash ^= (u >> (8 * b)) & 0xffu;
                out.hash *= 1099511628211ull;
            }
            out.energy += (double) v * v;
        }
    return out;
}
} // namespace ember190fj

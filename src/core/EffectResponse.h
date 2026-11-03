#pragma once

// Measured pictures of a built-in effect for its editor (round 168r, header-only so the core tests reach it): the
// magnitude response of a fresh instance of the slot's engine with the slot's settings (an impulse after the engine
// settled), and its input → output curve over one cycle of a full-scale sine. Nothing here runs on the audio thread.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/Effects.h"

namespace lynx::fxresponse
{
// Magnitude (dB) at each frequency of `hz`, from the impulse response of a fresh instance. The engine first runs on
// silence for `settle` samples so its parameter glides end; the impulse is 0.25 (well inside every engine's linear
// range), the response is read over `length` samples. Empty when the slot has no engine.
inline std::vector<float> responseDb (const EffectSlot& slot, const std::vector<double>& hz, double sampleRate = 48000.0, int settle = 8192,
                                      int length = 8192)
{
    std::vector<float> out;
    auto fx = makeEffect (slot);
    if (fx == nullptr)
        return out;
    fx->prepare (sampleRate);
    fx->reset();
    constexpr int block = 256;
    std::vector<float> l ((std::size_t) block), r ((std::size_t) block);
    for (int done = 0; done < settle; done += block)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        fx->process (l.data(), r.data(), block, slot.params, 120.0);
    }
    std::vector<float> ir ((std::size_t) length);
    const auto latency = std::max (0, fx->latencySamples());
    for (int done = 0; done < length + latency; done += block)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        if (done == 0)
            l[0] = r[0] = 0.25f;
        fx->process (l.data(), r.data(), block, slot.params, 120.0);
        for (int i = 0; i < block; ++i)
        {
            const auto at = done + i - latency;
            if (at >= 0 && at < length)
                ir[(std::size_t) at] = 0.5f * (l[(std::size_t) i] + r[(std::size_t) i]) * 4.0f;
        }
    }
    out.reserve (hz.size());
    for (const auto f : hz)
    {
        const auto w = 6.283185307179586 * f / sampleRate;
        double re = 0.0, im = 0.0;
        for (int n = 0; n < length; ++n)
        {
            re += ir[(std::size_t) n] * std::cos (w * n);
            im -= ir[(std::size_t) n] * std::sin (w * n);
        }
        out.push_back ((float) (10.0 * std::log10 (std::max (1e-12, re * re + im * im))));
    }
    return out;
}

// One cycle of the output (left channel, `points` samples) for a full-scale 93.75 Hz sine at 48 kHz after 0.5 s:
// a waveshaper's curve is read as output against the input sine.
inline std::vector<float> transferCycle (const EffectSlot& slot, int points = 512, double sampleRate = 48000.0)
{
    std::vector<float> out;
    auto fx = makeEffect (slot);
    if (fx == nullptr)
        return out;
    fx->prepare (sampleRate);
    fx->reset();
    const auto period = (int) std::lround (sampleRate / 93.75); // 512 samples at 48 kHz
    constexpr int block = 256;
    std::vector<float> l ((std::size_t) block), r ((std::size_t) block), last ((std::size_t) period);
    const auto total = ((int) (sampleRate * 0.5) / period + 1) * period;
    const auto latency = std::max (0, fx->latencySamples());
    int written = 0;
    for (int done = 0; done < total + latency; done += block)
    {
        for (int i = 0; i < block; ++i)
            l[(std::size_t) i] = r[(std::size_t) i] = (float) std::sin (6.283185307179586 * (double) ((done + i) % period) / (double) period);
        fx->process (l.data(), r.data(), block, slot.params, 120.0);
        for (int i = 0; i < block; ++i)
        {
            const auto at = done + i - latency;
            if (at >= total - period && at < total)
            {
                last[(std::size_t) (at - (total - period))] = l[(std::size_t) i];
                ++written;
            }
        }
    }
    out.resize ((std::size_t) points);
    for (int p = 0; p < points; ++p)
        out[(std::size_t) p] = last[(std::size_t) std::min (period - 1, p * period / points)];
    return out;
}
} // namespace lynx::fxresponse

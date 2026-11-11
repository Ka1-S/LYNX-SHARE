#pragma once

// The Master's loudness readout (round 190am, LYNX's own; off by default): short-term and integrated loudness in LUFS
// and the true peak in dBTP of what leaves the Master, measured by the meter the export and Apex Limiter already use
// (core/Loudness.h, ITU-R BS.1770-4 / EBU R 128).
//
// Real-time rules:
//  - off costs one relaxed atomic load per block and measures nothing;
//  - on, the audio thread runs the meter over the block (no allocation, no lock) and publishes four numbers once per
//    100 ms; the UI only reads atomics;
//  - a reset, switching it on and a new sample rate all start the measurement again on the audio thread.
//
// No JUCE: tested in tests/Extras190amTests.inc.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/Loudness.h"

namespace lynx
{
class MasterLoudness
{
public:
    struct Reading
    {
        float momentary = (float) loudness::silence;  // LUFS, the last 400 ms
        float shortTerm = (float) loudness::silence;  // LUFS, the last 3 s
        float integrated = (float) loudness::silence; // LUFS, gated, since the last reset
        float truePeak = (float) loudness::silence;   // dBTP, the highest since the last reset
        bool operator== (const Reading&) const = default;
    };

    // a value the meter has nothing to say about yet (silence, or less than 400 ms / 3 s measured)
    static constexpr bool measured (float value) noexcept { return value > -100.0f; }

    // --- control side (UI thread) ---
    void setEnabled (bool on) noexcept
    {
        if (on && ! enabled.load (std::memory_order_relaxed))
            reset(); // a fresh measurement each time it is switched on
        enabled.store (on, std::memory_order_relaxed);
        if (! on)
            publish ({});
    }
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }
    // starts the integrated loudness and the true-peak maximum again (the audio thread does it at its next block)
    void reset() noexcept
    {
        wanted.fetch_add (1, std::memory_order_relaxed);
        publish ({});
    }
    Reading read() const noexcept
    {
        return { momentary.load (std::memory_order_relaxed), shortTerm.load (std::memory_order_relaxed),
                 integrated.load (std::memory_order_relaxed), truePeak.load (std::memory_order_relaxed) };
    }

    // --- audio thread ---
    void push (const float* left, const float* right, int n, double sampleRate) noexcept
    {
        if (! enabled.load (std::memory_order_relaxed))
            return;
        if (const auto want = wanted.load (std::memory_order_relaxed); want != done || sampleRate != rate)
        {
            done = want;
            rate = sampleRate;
            meter.prepare (sampleRate);
            sinceUpdate = 0;
            period = std::max (1, (int) std::lround (sampleRate * 0.1));
        }
        meter.process (left, right, n);
        sinceUpdate += n;
        if (sinceUpdate < period)
            return;
        sinceUpdate %= period;
        if (wanted.load (std::memory_order_relaxed) != done)
            return; // a reset arrived during this block: the cleared reading stays until the next measurement
        publish ({ (float) meter.momentary(), (float) meter.shortTermLoudness(), (float) meter.integratedLoudness(),
                   (float) meter.truePeakDb() });
    }

private:
    void publish (const Reading& r) noexcept
    {
        momentary.store (r.momentary, std::memory_order_relaxed);
        shortTerm.store (r.shortTerm, std::memory_order_relaxed);
        integrated.store (r.integrated, std::memory_order_relaxed);
        truePeak.store (r.truePeak, std::memory_order_relaxed);
    }

    std::atomic<bool> enabled { false };
    std::atomic<unsigned> wanted { 1 }; // the measurement asked for; `done` is the one the audio thread runs
    std::atomic<float> momentary { (float) loudness::silence }, shortTerm { (float) loudness::silence },
        integrated { (float) loudness::silence }, truePeak { (float) loudness::silence };
    // audio thread only
    loudness::Meter meter;
    unsigned done = 0;
    double rate = 0.0;
    int sinceUpdate = 0, period = 4800;
};

// How the mixer prints a loudness or true-peak value: one decimal ("-14.2"), "--.-" while there is nothing to show.
inline std::string formatLoudness (float value)
{
    if (! MasterLoudness::measured (value))
        return "--.-";
    char text[16];
    std::snprintf (text, sizeof (text), "%.1f", (double) value);
    return std::strcmp (text, "-0.0") == 0 ? "0.0" : text;
}
} // namespace lynx

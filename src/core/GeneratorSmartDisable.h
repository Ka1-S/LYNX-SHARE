#pragma once

// FL's smart disable for a generator (a hosted instrument), round 190ok (owner decision D2). FL's rule, REA round 190of
// (FLEngine FUN_00cf3310 -> FUN_00cf35a0, rea/NOTES-smart-disable.md): while the generator's "Smart disable" is on
// and it has no events pending, each block it renders is tested for silence (every output sample at or under -96 dB);
// silent blocks add their length to a counter, and once the counter reaches the hold (4 s: the sample rate << 2, as
// for the effect slots) the generator is not called at all until it has events again. LYNX's reading of what FL leaves
// open: an event (note, MIDI message) or an audible block starts the count again, so only 4 s of continuous silence
// with nothing sent to the plugin puts it to sleep.
//
// The global switch (Settings › Audio › CPU › "Smart disable") and the wrapper's "Allow smart disable" both gate it,
// as for the effect slots. Header-only, no JUCE; the audio thread owns a State.

#include <cmath>
#include <cstdint>

namespace lynx::gensmart
{
inline constexpr float silenceLevel = 1.5849e-5f; // -96 dBFS (RackRenderer::silenceLevel)
inline constexpr double holdSeconds = 4.0;

// samples of the hold at `sampleRate` (FL: rate << 2)
constexpr std::int64_t holdSamples (double sampleRate) noexcept
{
    return sampleRate > 0.0 ? (std::int64_t) (sampleRate * holdSeconds) : (std::int64_t) (44100 * 4);
}

// true when every sample of the `channels` buffers (`n` each) is at or under -96 dB
inline bool silent (const float* const* channels, int numChannels, int n) noexcept
{
    for (int c = 0; c < numChannels; ++c)
    {
        const auto* x = channels[c];
        if (x == nullptr)
            continue;
        for (int i = 0; i < n; ++i)
            if (! (std::abs (x[i]) <= silenceLevel)) // (a NaN counts as sound: the plugin is not put to sleep)
                return false;
    }
    return true;
}

struct State
{
    std::int64_t silentSamples = 0;

    // the generator is asleep: not called while nothing is sent to it
    bool asleep (std::int64_t hold) const noexcept { return silentSamples >= hold; }
    // after a rendered block of `n` samples: `events` = something was sent to it for this block, `quiet` = its output
    // was silent
    void rendered (int n, bool events, bool quiet) noexcept
    {
        if (events || ! quiet)
            silentSamples = 0;
        else
            silentSamples += n;
    }
    // something is sent to it (it wakes and counts from zero)
    void wake() noexcept { silentSamples = 0; }
};
} // namespace lynx::gensmart

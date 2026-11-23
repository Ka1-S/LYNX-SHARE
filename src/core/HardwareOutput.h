#pragma once

// Hardware output targets of the mixer (REA round 156). FL's "Audio output target" sends an insert straight to
// device outputs besides its routing to the Master; LYNX lists every output channel alone too (mono: the sum
// (L+R)/2, the left side only or the right side only), which FL cannot do.
//
// Realtime path: the renderer adds each targeted insert's post-fader output (delayed to line up with the Master)
// into per-device-channel buses here; the device callback adds the buses into the open output channels and
// clears them. Nothing here allocates after construction.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// One choice of the output menu.
struct OutputTarget
{
    int channel = -1;  // first device output channel (0-based)
    OutputMode mode = OutputMode::stereo;
    std::string name;  // "Out 1 - Out 2", "Out 3"
};

// The choices for a device whose output channels are called `channelNames`: the stereo pairs (1-2, 3-4, ...)
// first, then every channel alone (mono, sum), like inputSources() lists inputs.
inline std::vector<OutputTarget> outputTargets (const std::vector<std::string>& channelNames)
{
    std::vector<OutputTarget> out;
    const auto count = (int) channelNames.size();

    for (int c = 0; c + 1 < count; c += 2)
        out.push_back ({ c, OutputMode::stereo, channelNames[(std::size_t) c] + " - " + channelNames[(std::size_t) c + 1] });

    for (int c = 0; c < count; ++c)
        out.push_back ({ c, OutputMode::monoSum, channelNames[(std::size_t) c] });

    return out;
}

// Device channels a target uses: { first, count } (count 0 = none).
inline std::pair<int, int> outputChannelsOf (int channel, OutputMode mode) noexcept
{
    if (channel < 0)
        return { 0, 0 };
    return { channel, mode == OutputMode::stereo ? 2 : 1 };
}

// Whether the device (with `deviceChannels` output channels) has every channel of the target.
inline bool outputAvailable (int channel, OutputMode mode, int deviceChannels) noexcept
{
    const auto [first, count] = outputChannelsOf (channel, mode);
    return count > 0 && first + count <= deviceChannels;
}

// The target's name, in English ("(none)", "Out 3 (mono, L)", "Out 7 - Out 8 (missing)"). Channels the device
// doesn't have are numbered from 1 and marked missing. `isMaster`: outputDefault is then the first pair.
inline std::string outputTargetName (int channel, OutputMode mode, const std::vector<std::string>& channelNames, bool isMaster)
{
    if (channel == outputDefault && isMaster)
        channel = 0, mode = OutputMode::stereo;
    if (channel < 0)
        return "(none)";

    auto nameOf = [&] (int c) {
        return c < (int) channelNames.size() ? channelNames[(std::size_t) c] : "Out " + std::to_string (c + 1);
    };

    std::string name = mode == OutputMode::stereo ? nameOf (channel) + " - " + nameOf (channel + 1) : nameOf (channel);

    if (mode == OutputMode::monoSum)
        name += " (mono)";
    else if (mode == OutputMode::monoLeft)
        name += " (mono, L)";
    else if (mode == OutputMode::monoRight)
        name += " (mono, R)";

    if (! outputAvailable (channel, mode, (int) channelNames.size()))
        name += " (missing)";

    return name;
}

// Adds a stereo signal into its target: `a` = the target's first channel, `b` = the second (stereo only; may be
// null for mono). Mono sum = (L+R)/2, so a centred source keeps its level on the single output.
inline void addToOutput (const float* left, const float* right, OutputMode mode, float* a, float* b, int n) noexcept
{
    if (a == nullptr || left == nullptr || right == nullptr)
        return;

    switch (mode)
    {
        case OutputMode::stereo:
            for (int i = 0; i < n; ++i)
                a[i] += left[i];
            if (b != nullptr)
                for (int i = 0; i < n; ++i)
                    b[i] += right[i];
            break;
        case OutputMode::monoSum:
            for (int i = 0; i < n; ++i)
                a[i] += 0.5f * (left[i] + right[i]);
            break;
        case OutputMode::monoLeft:
            for (int i = 0; i < n; ++i)
                a[i] += left[i];
            break;
        case OutputMode::monoRight:
            for (int i = 0; i < n; ++i)
                a[i] += right[i];
            break;
    }
}

// Per-device-output-channel buses filled by the renderer (inserts with a target) and drained by the callback.
struct HardwareOutput
{
    static constexpr int maxChannels = 64;
    static constexpr int capacity = 8192; // samples per callback piece (larger device blocks: outputs skipped)

    HardwareOutput() : buffers ((std::size_t) maxChannels * capacity, 0.0f) {}

    // set by the engine: true only while a device plays live (not during an export)
    std::atomic<bool> active { false };
    // round 190nc (DL-110): the Cue Volume as a gain, for the cued tracks' routes to the Cue Out (set by the engine)
    std::atomic<float> cueGain { 1.0f };

    // renderer: adds into channel `c` at `offset` within the current callback piece
    float* bus (int c, int offset, int n) noexcept
    {
        if (c < 0 || c >= maxChannels || offset < 0 || offset + n > capacity)
            return nullptr;
        dirty |= (std::uint64_t) 1 << c;
        return buffers.data() + (std::size_t) c * capacity + offset;
    }

    // callback: adds the buses into the device outputs (`slotOf[c]` = callback index of device channel c, −1 =
    // closed: that channel's sound is dropped) and clears them. `outputs` already point at this piece.
    template <typename SlotOf>
    void drainInto (float* const* outputs, int numOutputs, const SlotOf& slotOf, int n) noexcept
    {
        if (dirty == 0)
            return;

        n = std::min (n, capacity);

        for (int c = 0; c < maxChannels; ++c)
        {
            if ((dirty & ((std::uint64_t) 1 << c)) == 0)
                continue;

            auto* data = buffers.data() + (std::size_t) c * capacity;
            const auto slot = slotOf (c);

            if (slot >= 0 && slot < numOutputs && outputs[slot] != nullptr)
                for (int i = 0; i < n; ++i)
                    outputs[slot][i] += data[i];

            std::fill (data, data + capacity, 0.0f); // the whole bus: a piece may have been written past n
        }

        dirty = 0;
    }

    // drops whatever was written (an export ran, the device changed)
    void clear() noexcept
    {
        if (dirty != 0)
            std::fill (buffers.begin(), buffers.end(), 0.0f);
        dirty = 0;
    }

private:
    std::vector<float> buffers; // planar, maxChannels × capacity
    std::uint64_t dirty = 0;    // audio thread only
};
} // namespace lynx

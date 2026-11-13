#pragma once

// External audio input through the mixer (REA_rodada88): the sources an insert can take its input from, when the
// input is monitored, and the block of input the audio thread hands to the renderer.

#include <algorithm>
#include <array>
#include <atomic>
#include <string>
#include <utility>
#include <vector>

#include "core/Meters.h"
#include "core/Song.h"

namespace lynx
{
// One choice of an insert's "Audio input source": a stereo pair of device input channels, or one channel.
struct InputSource
{
    int channel = -1; // first device input channel (0-based)
    bool stereo = false;
    std::string name; // "In 1 - In 2" style, from the device's channel names
};

// The choices for a device whose input channels are called `channelNames` (in device order): the stereo pairs
// (1-2, 3-4, ...) first, then every channel alone, as FL lists them. A single-channel device gives one mono entry.
inline std::vector<InputSource> inputSources (const std::vector<std::string>& channelNames)
{
    std::vector<InputSource> out;
    const auto count = (int) channelNames.size();

    for (int c = 0; c + 1 < count; c += 2)
        out.push_back ({ c, true, channelNames[(std::size_t) c] + " - " + channelNames[(std::size_t) c + 1] });

    for (int c = 0; c < count; ++c)
        out.push_back ({ c, false, channelNames[(std::size_t) c] });

    return out;
}

// The name of an insert's input (empty when it has none). A channel the device doesn't have (another device
// is open now) still gets a name, numbered from 1.
inline std::string inputSourceName (int channel, bool stereo, const std::vector<std::string>& channelNames)
{
    if (channel < 0)
        return {};

    auto nameOf = [&] (int c) {
        return c < (int) channelNames.size() ? channelNames[(std::size_t) c] : "In " + std::to_string (c + 1);
    };

    return stereo ? nameOf (channel) + " - " + nameOf (channel + 1) : nameOf (channel);
}

// An FL project's insert input (.flp 0x9A) on a device with `deviceChannels` inputs. FL numbers the entries of its own
// input list, which is inputSources()'s order (REA round 157c [FL]: on a two-input device 0 = "In 1 - In 2", 1 = "In 1",
// 2 = "In 2"): the stereo pairs first, then every channel alone. An index past this device's list (FL had a device with
// more inputs) gives the first channel alone, the usual microphone input. -1 for none.
inline InputSource flInputSource (int flIndex, int deviceChannels)
{
    if (flIndex < 0)
        return {};

    const auto n = deviceChannels > 0 ? deviceChannels : 2;
    const auto pairs = n / 2;

    if (flIndex < pairs)
        return { 2 * flIndex, true, {} };
    if (flIndex - pairs < n)
        return { flIndex - pairs, false, {} };
    return { 0, false, {} };
}

// Issue #152 ("I pick a physical input on insert X and it records another insert's signal"): FL 2026's inserts record
// "Post EQ" by default (RecordSource), i.e. everything that reaches the insert, not only the input. When an external
// input is chosen on an insert that records anything else than "External input only", FL asks: "You've selected an
// external input. Do you want to switch to External input only to avoid recording internal audio on this track?"
// (FLEngine_x64.dll, FL Studio 2026: "External input|You've selected an external input. Do you want to switch to
// |External input only| to avoid recording internal audio on this track?"). The Master has no record arm.
inline bool asksExternalInputOnly (int track, int channel, RecordSource source) noexcept
{
    return track > 0 && channel >= 0 && source != RecordSource::externalInput;
}

// Live check 190ew ISS152-1 (FL 2026, shot ISS152-1-external-input-question.png): FL asks only when the insert goes from
// no input to an input; changing one input for another does not ask. The box has FL's "Remember my choice" check
// (message "RecAutoSwitchExternalInput", "Auto switch to external input only" in Manage messages).
inline bool asksExternalInputOnly (int track, int channelBefore, int channelNow, RecordSource source) noexcept
{
    return channelBefore < 0 && asksExternalInputOnly (track, channelNow, source);
}

// Whether an insert hears its external input now.
inline bool inputMonitored (InputMonitor mode, bool armed, bool playing, bool recording) noexcept
{
    switch (mode)
    {
        case InputMonitor::off: return false;
        case InputMonitor::whenArmed: return armed;
        case InputMonitor::automatic: return armed && (recording || ! playing);
        case InputMonitor::on: return true;
    }

    return false;
}

// What a left-click on an insert's arm switch (the Mixer's, or an audio track's record button in the Playlist) does.
// FL 2026 (measured, round 165 issue11d): it arms at once, with or without an audio input source — no prompt, the file
// name is automatic; the missing input is reported when recording starts. A second click disarms. The Master has none.
enum class ArmClick { none, arm, disarm };
inline ArmClick armClick (int track, bool armed) noexcept
{
    if (track <= 0)
        return ArmClick::none;
    return armed ? ArmClick::disarm : ArmClick::arm;
}

// Device input channels an insert's source uses: { first, count } (count 0 = none or not available).
inline std::pair<int, int> inputChannelsOf (const MixerTrack& track, int deviceChannels) noexcept
{
    if (track.inputChannel < 0 || track.inputChannel >= deviceChannels)
        return { 0, 0 };

    return { track.inputChannel, track.inputStereo && track.inputChannel + 1 < deviceChannels ? 2 : 1 };
}

// Shared between the audio callback (writes the block's input pointers, then calls the renderer on the same
// thread), the renderer (reads them) and the UI (arm switches, input meters).
struct LiveInput
{
    static constexpr int maxChannels = 64;

    // audio thread, valid only during one render call
    std::array<const float*, maxChannels> channels {};
    int numChannels = 0;

    // UI -> audio
    std::array<std::atomic<bool>, maxMixerTracks> armed {};
    std::atomic<bool> recording { false };
    // audio takes are recording in Song mode without a loop region: the song plays once, then silence (no start over
    // at its end), so the take is one piece (round 163)
    std::atomic<bool> linearSong { false };
    // punching on the loop region (round 163): the region is not looped either, playback runs through the punch-out
    // into the post-roll
    std::atomic<bool> noLoopRegion { false };
    // Pattern mode, a note take with Loop record off (round 170e5): the transport tick the pattern plays once from
    // (no repeats; FL's "never" play end), < 0: off, the pattern loops
    std::atomic<double> patternOrigin { -1.0 };

    // audio -> UI: peak of each insert's input source, monitored or not
    MeterBank meters;

    // Recording taps (round 168x, issue #41: FL's "Record" positions after the input). While a take records insert
    // `track` at `point` (RecordSource), the renderer copies the insert's signal at that point into `data` (the audio
    // callback's block: sample i of the block at index i). The UI sets track / point before the takes are published
    // and clears them after; the buffers are allocated once here and never resized.
    struct Tap
    {
        std::atomic<int> track { -1 };
        std::atomic<int> point { 0 };
        std::array<std::vector<float>, 2> data;
    };
    static constexpr int maxTaps = 16;
    static constexpr int tapCapacity = 16384; // frames per callback block (more: the block is lost to silence)
    std::array<Tap, maxTaps> taps;
    std::atomic<int> tapCount { 0 }; // taps in use; 0 = the renderer does no tap work at all

    LiveInput()
    {
        for (auto& t : taps)
            for (auto& d : t.data)
                d.assign ((std::size_t) tapCapacity, 0.0f);
    }

    // renderer (audio thread): insert t's signal at `point`, samples [offset, offset + n) of the callback block
    void tap (int t, RecordSource point, const float* l, const float* r, int offset, int n) noexcept
    {
        if (offset < 0 || offset + n > tapCapacity)
            return;
        for (auto& k : taps)
            if (k.track.load (std::memory_order_relaxed) == t && k.point.load (std::memory_order_relaxed) == (int) point)
            {
                std::copy (l, l + n, k.data[0].data() + offset);
                std::copy (r, r + n, k.data[1].data() + offset);
            }
    }
    // where the tap of insert t at `point` takes samples [offset, offset + n) of the block (nullptrs: not tapped)
    std::pair<float*, float*> tapDestination (int t, RecordSource point, int offset, int n) noexcept
    {
        if (offset < 0 || offset + n > tapCapacity)
            return { nullptr, nullptr };
        for (auto& k : taps)
            if (k.track.load (std::memory_order_relaxed) == t && k.point.load (std::memory_order_relaxed) == (int) point)
                return { k.data[0].data() + offset, k.data[1].data() + offset };
        return { nullptr, nullptr };
    }
    // whether insert t is tapped at all (and at which points, as a bit set of RecordSource values)
    unsigned tappedPoints (int t) const noexcept
    {
        unsigned bits = 0;
        if (tapCount.load (std::memory_order_relaxed) == 0)
            return 0;
        for (const auto& k : taps)
            if (k.track.load (std::memory_order_relaxed) == t)
                bits |= 1u << k.point.load (std::memory_order_relaxed);
        return bits;
    }

    const float* channel (int c, int offset) const noexcept
    {
        return c >= 0 && c < numChannels && channels[(std::size_t) c] != nullptr ? channels[(std::size_t) c] + offset : nullptr;
    }
};
} // namespace lynx

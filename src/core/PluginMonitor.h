#pragma once

// The Plugin performance monitor's logic (FL's "Plugin performance monitor", interpreted; REA_rodada84): what each
// instrument and effect slot costs, as a share of the real time it had (100% = one core fully busy for that audio),
// from the rack's own counters (core/RackRenderer.h ProcessTimes). No JUCE.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::perfmon
{
constexpr int channelKeys = 1024;                                  // key of channel c = c
inline int effectKey (int track, int slot) noexcept { return channelKeys + track * maxEffectSlots + slot; }
constexpr int keyCount = channelKeys + maxMixerTracks * maxEffectSlots;

// Something that is timed: a channel's instrument or an effect slot.
struct Source
{
    int key = 0;
    std::string name;   // the plugin / instrument / effect
    std::string where;  // "Channel: Kick", "Insert 3 › slot 2"
    bool plugin = false; // a third-party plugin (VST3/CLAP)
    int channel = -1, track = -1, slot = -1; // where it is (channel, or insert + slot)
};

// Every instrument and effect slot of the song. `instrumentName` names a built-in instrument (default: the channel's
// name); plugins show their own name.
std::vector<Source> sources (const Song& song, const std::function<std::string (const ChannelDef&)>& instrumentName = {});

struct Row
{
    Source source;
    double current = 0.0; // % of real time, smoothed
    double average = 0.0; // since the last reset
    double peak = 0.0;    // highest `current` since the last reset
    // round 190of: the longest single call since the last reset (ms) and the calls that took more than half of the
    // time their own audio lasts (what makes the sound stutter)
    double longestMs = 0.0;
    std::uint64_t spikes = 0;
};

// Round 190of: the per-call counters the rack keeps besides the busy time (core/RackRenderer.h ProcessTimes):
// `longestNs[key]` the longest call since the last update (taken), `spikes[key]` the calls over half their audio (counted up)
struct CallStats
{
    std::vector<std::uint32_t> longestNs;
    std::vector<std::uint64_t> spikes;
};

enum class SortBy
{
    current,
    average,
    peak,
    name,
    longest, // round 190of
    spikes
};

class Monitor
{
public:
    // `busyNs[key]`, `audioNs` and `wallNs` are the rack's accumulated counters (ever-growing); the first call only
    // takes the starting point.
    void update (const std::vector<Source>& list, const std::vector<std::uint64_t>& busyNs, std::uint64_t audioNs, std::uint64_t wallNs,
                 const CallStats* calls = nullptr);
    void reset(); // averages and peaks start again from the next update

    // Rows matching every word of `find` (name or place, any case), sorted (highest first; name: A→Z).
    std::vector<Row> rows (const std::string& find = {}, SortBy sort = SortBy::current) const;

    double rackCurrent() const noexcept { return rack; } // the whole rack, % of real time
    double rackPeak() const noexcept { return rackMax; }

private:
    struct Stat
    {
        Source source;
        std::uint64_t lastBusy = 0, totalBusy = 0;
        double current = 0.0, peak = 0.0;
        std::uint64_t longestNs = 0, lastSpikes = 0, spikes = 0; // (round 190of)
        bool fresh = true;
    };
    std::map<int, Stat> stats;
    std::uint64_t lastAudio = 0, lastWall = 0, totalAudio = 0;
    bool started = false;
    double rack = 0.0, rackMax = 0.0;
};
} // namespace lynx::perfmon

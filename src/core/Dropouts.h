#pragma once

// Round 190gg (B5, LYNX-own): dropouts counted and explained. Users report crackles "while the meter reads low" and
// cannot tell a plugin that is too slow from a driver or a system that is late (forums.steinberg.net 820816, 955503;
// round 190gc's complaint #6). The CPU meter already showed FL's underrun count (RtStats::underrunCount); this keeps,
// on the message thread, how many there were since the start, when the last one happened and why:
//   - processing: a block took longer than the buffer (or the load came near it): the slowest channel / effect call of
//     that block is named (the audio thread packs it with pack(): no lock, no allocation);
//   - system: the driver counted a dropout, or called late, while every block stayed well under its deadline: not a
//     plugin's fault; a larger buffer or another driver helps.
// Plain C++ (no JUCE): the core tests check the counting and the causes.

#include "core/RtStats.h"

#include <algorithm>
#include <cstdint>

namespace lynx::dropouts
{
// The slowest call of a block, in one atomic word: nanoseconds (40 bits) and the unit (24 bits).
// Units: 0 = none, channel c = c + 1, mixer slot `key` (track * maxEffectSlots + slot) = effectBase + key.
constexpr std::uint32_t effectBase = 0x100000u;
constexpr std::uint64_t pack (std::uint64_t ns, std::uint32_t unit) noexcept
{
    return (std::min<std::uint64_t> (ns, (std::uint64_t (1) << 40) - 1) << 24) | (unit & 0xFFFFFFu);
}
constexpr std::uint64_t nanosecondsOf (std::uint64_t packed) noexcept { return packed >> 24; }
constexpr std::uint32_t unitOf (std::uint64_t packed) noexcept { return (std::uint32_t) (packed & 0xFFFFFFu); }
constexpr std::uint32_t channelUnit (int channel) noexcept { return (std::uint32_t) channel + 1u; }
constexpr std::uint32_t effectUnit (std::size_t key) noexcept { return effectBase + (std::uint32_t) key; }
constexpr bool isChannel (std::uint32_t unit) noexcept { return unit > 0 && unit < effectBase; }
constexpr int channelOf (std::uint32_t unit) noexcept { return (int) unit - 1; }
constexpr std::size_t effectKeyOf (std::uint32_t unit) noexcept { return (std::size_t) (unit - effectBase); }

// The audio thread (any worker): this call took `ns`; the block's slowest stays in `slot` (relaxed, lock-free)
inline void raiseSlowest (std::atomic<std::uint64_t>& slot, std::uint64_t ns, std::uint32_t unit) noexcept
{
    const auto v = pack (ns, unit);
    auto current = slot.load (std::memory_order_relaxed);
    while (nanosecondsOf (v) > nanosecondsOf (current) && ! slot.compare_exchange_weak (current, v, std::memory_order_relaxed))
    {
    }
}

enum class Cause
{
    none,
    processing, // a block over its deadline, or the load near it
    system      // the driver / system was late while the load stayed low
};

// Under this highest load (1 = the whole deadline) in the polling window, a dropout is the system's, not the
// processing's (the CPU meter turns amber at 60 %, red at 85 %).
constexpr double lowLoad = 0.7;

// What the message thread reads a few times a second
struct Poll
{
    double nowMs = 0.0;             // a monotonic clock
    std::uint64_t overBudget = 0;   // RtStats: blocks over their deadline since the start
    std::uint64_t lateCalls = 0;    // RtStats: late device calls since the start
    int deviceXruns = -1;           // the driver's own count (−1: it does not report one)
    double windowMaxLoad = 0.0;     // RtStats::takeDropoutWindowMaxLoad (since the previous poll)
    std::uint64_t lastOverSlowest = 0; // RtStats snapshot: the last over-deadline block's slowest call (pack())
    double lastOverLoad = 0.0;
};

class Log
{
public:
    // Returns how many dropouts this poll added (0: none). The count is FL's underrun count (the driver's when it reports
    // one, else LYNX's estimate), kept from growing backwards when the driver's restarts with a new device.
    int update (const Poll& p) noexcept
    {
        const auto raw = RtStats::underrunCount (p.overBudget, p.lateCalls, p.deviceXruns);
        // (a driver may count a dropout a poll after the slow block: the previous poll's window counts too)
        const auto overGrew = p.overBudget > (started ? overBefore : 0);
        const auto recentMax = std::max (p.windowMaxLoad, previousWindow);
        std::uint64_t added = 0;
        if (! started)
            added = raw; // (dropouts before the first poll still count)
        else if (raw > lastRaw)
            added = raw - lastRaw;
        started = true;
        lastRaw = raw;
        overBefore = lastOver;
        lastOver = p.overBudget;
        previousWindow = p.windowMaxLoad;
        if (added == 0)
            return 0;
        total += added;
        lastAtMs = p.nowMs;
        const auto busy = overGrew || recentMax >= lowLoad;
        lastCause = busy ? Cause::processing : Cause::system;
        (busy ? processingCount : systemCount) += added;
        lastLoad = overGrew ? std::max (p.lastOverLoad, recentMax) : recentMax;
        lastSlowest = overGrew ? p.lastOverSlowest : 0;
        return (int) std::min<std::uint64_t> (added, 1u << 30);
    }

    std::uint64_t count() const noexcept { return total; }
    std::uint64_t processing() const noexcept { return processingCount; }
    std::uint64_t system() const noexcept { return systemCount; }
    double lastAt() const noexcept { return lastAtMs; } // 0 = none yet
    Cause cause() const noexcept { return lastCause; }  // of the last one
    double load() const noexcept { return lastLoad; }   // the highest load around the last one
    std::uint64_t slowest() const noexcept { return lastSlowest; } // pack(); 0 = not known (a system dropout)

    // Seconds since the last dropout, rounded down; −1 = none yet
    long long secondsSince (double nowMs) const noexcept
    {
        return total == 0 ? -1 : (long long) (std::max (0.0, nowMs - lastAtMs) / 1000.0);
    }

private:
    bool started = false;
    std::uint64_t lastRaw = 0, lastOver = 0, overBefore = 0, total = 0, processingCount = 0, systemCount = 0, lastSlowest = 0;
    double lastAtMs = 0.0, lastLoad = 0.0, previousWindow = 0.0;
    Cause lastCause = Cause::none;
};
} // namespace lynx::dropouts

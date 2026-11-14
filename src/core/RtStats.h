#pragma once

// Real-time health of the audio callback (REA round 87): how long each callback took against its deadline (the
// buffer's duration), so a crackle can be told apart as "the processing was too slow" (over budget) or "the device
// was late calling us" (gap between callbacks). The audio thread records with relaxed atomics (no locks, no
// allocation); the UI, the log and --rtstress read snapshots.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace lynx
{
class RtStats
{
public:
    // Audio thread. `startMs`: when the callback began (a monotonic clock in ms), `usedMs`: how long it took,
    // `budgetMs`: the buffer duration (numSamples / sampleRate).
    // `slowest` (round 190gg): the block's slowest channel / effect call (core/Dropouts.h pack(); 0 = not measured).
    void record (double startMs, double usedMs, double budgetMs, std::uint64_t slowest = 0) noexcept
    {
        callbacks.fetch_add (1, std::memory_order_relaxed);
        const auto load = budgetMs > 0.0 ? usedMs / budgetMs : 0.0;
        const auto permille = (std::uint32_t) std::min (100000.0, std::max (0.0, load * 1000.0));
        raise (maxLoadPermille, permille);
        raise (windowMaxPermille, permille);
        raise (dropoutWindowPermille, permille);
        totalUsedUs.fetch_add ((std::uint64_t) (usedMs * 1000.0), std::memory_order_relaxed);
        totalBudgetUs.fetch_add ((std::uint64_t) (budgetMs * 1000.0), std::memory_order_relaxed);
        histogram[(std::size_t) std::min<std::uint32_t> (permille / 100, bins - 1)].fetch_add (1, std::memory_order_relaxed);

        if (load >= 1.0)
        {
            lastOverSlowest.store (slowest, std::memory_order_relaxed);
            lastOverPermille.store (permille, std::memory_order_relaxed);
            overBudget.fetch_add (1, std::memory_order_relaxed); // (after: a reader that sees the count sees these)
            lastOverMs.store (startMs, std::memory_order_relaxed);
        }
        else if (load >= 0.8)
            nearBudget.fetch_add (1, std::memory_order_relaxed);

        // the device called later than one buffer after the previous call (plus a margin): it may have run dry
        const auto previous = lastStartMs.exchange (startMs, std::memory_order_relaxed);
        if (previous > 0.0 && budgetMs > 0.0 && startMs - previous > budgetMs * 1.9 + 2.0)
        {
            lateCalls.fetch_add (1, std::memory_order_relaxed);
            lastLateMs.store (startMs, std::memory_order_relaxed);
        }
    }

    // FL's "%d underruns" in the CPU panel: the driver's own count when it reports one (>= 0), else LYNX's estimate
    // (blocks over the deadline + late calls).
    static std::uint64_t underrunCount (std::uint64_t overBudget, std::uint64_t lateCalls, int deviceXruns) noexcept
    {
        return deviceXruns >= 0 ? (std::uint64_t) deviceXruns : overBudget + lateCalls;
    }

    // A pause in the calls that is not a glitch (device restarted, stopped): forget the previous start.
    void deviceRestarted() noexcept { lastStartMs.store (0.0, std::memory_order_relaxed); }

    static constexpr std::uint32_t bins = 16; // load histogram in 10 % steps; the last bin is >= 150 %

    struct Snapshot
    {
        std::uint64_t callbacks = 0, overBudget = 0, nearBudget = 0, lateCalls = 0;
        double maxLoad = 0.0;       // since the last reset (1 = the whole deadline)
        double averageLoad = 0.0;   // processing time / deadline over everything since the last reset
        double lastOverMs = 0.0, lastLateMs = 0.0;
        std::array<std::uint64_t, bins> histogram {};
        // round 190gg: the last block over its deadline: its slowest channel / effect (core/Dropouts.h) and its load
        std::uint64_t lastOverSlowest = 0;
        double lastOverLoad = 0.0;
    };

    Snapshot snapshot() const noexcept
    {
        Snapshot s;
        s.callbacks = callbacks.load (std::memory_order_relaxed);
        s.overBudget = overBudget.load (std::memory_order_relaxed);
        s.nearBudget = nearBudget.load (std::memory_order_relaxed);
        s.lateCalls = lateCalls.load (std::memory_order_relaxed);
        s.maxLoad = maxLoadPermille.load (std::memory_order_relaxed) / 1000.0;
        const auto budget = (double) totalBudgetUs.load (std::memory_order_relaxed);
        s.averageLoad = budget > 0.0 ? (double) totalUsedUs.load (std::memory_order_relaxed) / budget : 0.0;
        s.lastOverMs = lastOverMs.load (std::memory_order_relaxed);
        s.lastLateMs = lastLateMs.load (std::memory_order_relaxed);
        for (std::size_t i = 0; i < bins; ++i)
            s.histogram[i] = histogram[i].load (std::memory_order_relaxed);
        s.lastOverSlowest = lastOverSlowest.load (std::memory_order_relaxed);
        s.lastOverLoad = lastOverPermille.load (std::memory_order_relaxed) / 1000.0;
        return s;
    }

    // Round 190gg: the highest load since the previous call, for the dropout log (its own window: the CPU meter takes
    // takeWindowMaxLoad). Message thread.
    double takeDropoutWindowMaxLoad() noexcept { return dropoutWindowPermille.exchange (0, std::memory_order_relaxed) / 1000.0; }

    // The highest load since the previous call (for a meter that shows peaks between UI frames).
    double takeWindowMaxLoad() noexcept { return windowMaxPermille.exchange (0, std::memory_order_relaxed) / 1000.0; }

    // Any thread (the counters are restarted, not synchronised with a callback in flight).
    void reset() noexcept
    {
        for (auto* a : { &callbacks, &overBudget, &nearBudget, &lateCalls, &totalUsedUs, &totalBudgetUs })
            a->store (0, std::memory_order_relaxed);
        maxLoadPermille.store (0, std::memory_order_relaxed);
        windowMaxPermille.store (0, std::memory_order_relaxed);
        dropoutWindowPermille.store (0, std::memory_order_relaxed);
        lastOverSlowest.store (0, std::memory_order_relaxed);
        lastOverPermille.store (0, std::memory_order_relaxed);
        lastOverMs.store (0.0, std::memory_order_relaxed);
        lastLateMs.store (0.0, std::memory_order_relaxed);
        for (auto& h : histogram)
            h.store (0, std::memory_order_relaxed);
    }

private:
    static void raise (std::atomic<std::uint32_t>& slot, std::uint32_t v) noexcept
    {
        auto current = slot.load (std::memory_order_relaxed);
        while (v > current && ! slot.compare_exchange_weak (current, v, std::memory_order_relaxed))
        {
        }
    }

    std::atomic<std::uint64_t> callbacks { 0 }, overBudget { 0 }, nearBudget { 0 }, lateCalls { 0 };
    std::atomic<std::uint64_t> totalUsedUs { 0 }, totalBudgetUs { 0 };
    std::atomic<std::uint32_t> maxLoadPermille { 0 }, windowMaxPermille { 0 }, dropoutWindowPermille { 0 }, lastOverPermille { 0 };
    std::atomic<std::uint64_t> lastOverSlowest { 0 };
    std::atomic<double> lastStartMs { 0.0 }, lastOverMs { 0.0 }, lastLateMs { 0.0 };
    std::array<std::atomic<std::uint64_t>, bins> histogram {};
};
} // namespace lynx

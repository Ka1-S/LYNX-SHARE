#pragma once

// FL's Options › Audio › "Safe overloads" (FLEngine 2026: SafeOverloadBox in the audio page's output panel, checked by
// default, hint "No risk of GUI freezing when overloads occur"). TOP25 P24, rounds 190of (measured) and 190ok (built).
//
// LYNX's UI never waits for the audio thread (the song reaches it as an immutable snapshot), so a heavy project cannot
// freeze the screen through a lock as FL's mixer could. What remains is CPU time: on a PC with no spare core (190of:
// big200 at 64 samples on 2 CPUs) the audio threads, which run at the system's real-time priorities, kept both CPUs
// busy through a real overload, and the message thread with the GPU driver's threads it waits on (pinned to their CPUs)
// got what was left: ~20 fps with stalls up to 0.7 s.
//
// The guard: when BOTH hold — the callbacks missed their deadline in most of the last quarter second, AND the message
// thread's 30 Hz pulse is late (the UI is starving) — the device callback gives up a slice of the CPU after each
// block (a high-resolution sleep after its output is written: the workers have no work meanwhile and sleep too). It
// stays engaged while the overload lasts and lets go 1.5 s after the last overloaded quarter second. Nothing changes
// while the audio keeps up, or while the UI keeps its pulse (a PC with spare cores): the rendered samples are never
// touched, only when the next block starts. The audio was breaking up anyway; the screen stays usable to fix it.
//
// Header-only logic (the tests drive it with their own clock); sleepFor is in OverloadGuard.cpp.

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace lynx
{
class OverloadGuard
{
public:
    // the decision's constants (fixed; the tests use them)
    static constexpr double windowMs = 250.0;     // the callbacks' window
    static constexpr double uiLateMs = 60.0;      // the 30 Hz pulse (33 ms) this late: the message thread starves
    static constexpr double releaseMs = 1500.0;   // engaged until this long after the last overloaded window
    static constexpr double yieldShare = 0.5;     // the slice given up: this share of the block's own processing time…
    static constexpr double minYieldMs = 0.5, maxYieldMs = 4.0; // …within these bounds

    // Settings › Audio › "Safe overloads" (FL's default: on). Any thread.
    void setEnabled (bool on) noexcept { enabled.store (on, std::memory_order_relaxed); }
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

    // Message thread: its 30 Hz pulse (the engine's timer), `nowMs` on the callbacks' clock.
    void uiPulse (double nowMs) noexcept { lastUiMs.store (nowMs, std::memory_order_relaxed); }

    // Audio thread, after every device callback (its output written): `usedMs` its processing time, `budgetMs` its
    // deadline. Returns how long to yield now (ms, 0 = none). No lock, no allocation.
    double afterCallback (double nowMs, double usedMs, double budgetMs) noexcept
    {
        if (windowStartMs <= 0.0 || nowMs < windowStartMs)
            windowStartMs = nowMs;
        ++windowCalls;
        if (budgetMs > 0.0 && usedMs >= budgetMs)
            ++windowOver;
        if (nowMs - windowStartMs >= windowMs)
        {
            lastWindowOverloaded = windowCalls >= 4 && windowOver * 2 > windowCalls;
            if (lastWindowOverloaded)
                lastOverloadMs = nowMs;
            windowStartMs = nowMs;
            windowCalls = windowOver = 0;
        }

        auto on = engagedFlag.load (std::memory_order_relaxed);
        if (! enabled.load (std::memory_order_relaxed))
            on = false;
        else if (! on)
        {
            const auto ui = lastUiMs.load (std::memory_order_relaxed);
            if (lastWindowOverloaded && ui > 0.0 && nowMs - ui > uiLateMs)
            {
                on = true;
                engagements.fetch_add (1, std::memory_order_relaxed);
            }
        }
        else if (nowMs - lastOverloadMs > releaseMs)
            on = false;
        engagedFlag.store (on, std::memory_order_relaxed);
        if (! on)
            return 0.0;
        const auto y = std::clamp (usedMs * yieldShare, minYieldMs, maxYieldMs);
        yieldedUs.fetch_add ((std::uint64_t) (y * 1000.0), std::memory_order_relaxed);
        return y;
    }

    // A pause in the callbacks (device restarted / stopped): the window starts again.
    void restart() noexcept
    {
        windowStartMs = 0.0;
        windowCalls = windowOver = 0;
        lastWindowOverloaded = false;
        engagedFlag.store (false, std::memory_order_relaxed);
    }

    bool engaged() const noexcept { return engagedFlag.load (std::memory_order_relaxed); }
    std::uint64_t timesEngaged() const noexcept { return engagements.load (std::memory_order_relaxed); }
    double yieldedMs() const noexcept { return (double) yieldedUs.load (std::memory_order_relaxed) / 1000.0; }

    // Sleeps the calling thread for `ms` (Windows: a high-resolution waitable timer, so a 0.5 ms slice is not the
    // scheduler's 15.6 ms tick; elsewhere nanosleep). For the audio thread under the guard only.
    static void sleepFor (double ms) noexcept;

private:
    std::atomic<bool> enabled { true }, engagedFlag { false };
    std::atomic<double> lastUiMs { 0.0 };
    std::atomic<std::uint64_t> engagements { 0 }, yieldedUs { 0 };
    // audio thread only
    double windowStartMs = 0.0, lastOverloadMs = -1.0e12;
    int windowCalls = 0, windowOver = 0;
    bool lastWindowOverloaded = false;
};
} // namespace lynx

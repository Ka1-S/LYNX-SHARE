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
// The guard: when BOTH hold — the callbacks missed their deadline in most of the last 100 ms, AND the message
// thread's 30 Hz pulse is late (the UI is starving) — the device callback gives up a slice of the CPU after each
// block (a high-resolution sleep after its output is written: the workers have no work meanwhile and sleep too). It
// stays engaged while the overload lasts (checked by a 250 ms probe without yielding every 2 s) and lets go 1.5 s after
// the last overloaded window or at a probe the audio passes. Nothing changes
// while the audio keeps up, or while the UI keeps its pulse (a PC with spare cores): the rendered samples are never
// touched, only when the next block starts. The audio was breaking up anyway; the screen stays usable to fix it.
//
// Header-only logic (the tests drive it with their own clock); sleepFor is in OverloadGuard.cpp.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace lynx
{
class OverloadGuard
{
public:
    // the decision's numbers (the tests use them; LYNX_SAFE_OVERLOADS="share=1,min=0.5,max=8,window=100,late=50,
    // release=1500" sets them for an A/B, AudioEngine at start)
    struct Params
    {
        // (round 190ok's A/B, big200 at 64 samples on 2 CPUs: half the processing time gave 33 fps, the same time 46, one and
        // a half 44 fps with the 99th-percentile gap at 84-93 ms instead of 114-160; perf190ok/HANDOFF.md)
        double windowMs = 100.0;   // the callbacks' window
        double uiLateMs = 50.0;    // the 30 Hz pulse (33 ms) this late: the message thread starves
        double releaseMs = 1500.0; // engaged until this long after the last overloaded window
        double yieldShare = 1.5;   // the slice given up: this share of the block's own processing time…
        double minYieldMs = 0.5, maxYieldMs = 10.0; // …within these bounds
        bool needUiLate = true;    // false ("ui=0"): engaged on the audio's overload alone
        // the probe: while engaged, every probeEveryMs the audio runs probeMs without yielding, and the guard lets go
        // when the callbacks then keep their deadline in most of it. Without it the yield could keep itself going: a
        // stopped project's tails die out in audio time, which the yield slows down, so the overload lasted (round
        // 190ok's A/B: the recovery step stayed engaged). A share that followed the UI instead recovered but gave the
        // screen less (33 fps instead of 46)
        double probeEveryMs = 2000.0, probeMs = 250.0;
    };
    const Params& params() const noexcept { return p; }
    // "key=value,…" (missing keys keep their value; any thread before the device starts)
    void configure (const char* spec) noexcept
    {
        if (spec == nullptr)
            return;
        for (const char* at = spec; *at != 0;)
        {
            const char* eq = at;
            while (*eq != 0 && *eq != '=' && *eq != ',')
                ++eq;
            if (*eq != '=')
            {
                at = *eq == ',' ? eq + 1 : eq;
                continue;
            }
            const auto key = std::string (at, eq);
            char* end = nullptr;
            const auto v = std::strtod (eq + 1, &end);
            if (key == "share") p.yieldShare = v;
            else if (key == "min") p.minYieldMs = v;
            else if (key == "max") p.maxYieldMs = v;
            else if (key == "window") p.windowMs = v;
            else if (key == "late") p.uiLateMs = v;
            else if (key == "release") p.releaseMs = v;
            else if (key == "ui") p.needUiLate = v != 0.0;
            else if (key == "probe") p.probeMs = v;
            else if (key == "probeevery") p.probeEveryMs = v;
            at = end;
            while (*at != 0 && *at != ',')
                ++at;
            if (*at == ',')
                ++at;
        }
    }

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
        if (nowMs - windowStartMs >= p.windowMs)
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
            if (lastWindowOverloaded && (! p.needUiLate || (ui > 0.0 && nowMs - ui > p.uiLateMs)))
            {
                on = true;
                probeAtMs = nowMs + p.probeEveryMs;
                probing = false;
                engagements.fetch_add (1, std::memory_order_relaxed);
            }
        }
        else if (nowMs - lastOverloadMs > p.releaseMs)
            on = false;
        else if (probing)
        {
            // the probe: no yield; its callbacks decide
            ++probeCalls;
            if (budgetMs > 0.0 && usedMs >= budgetMs)
                ++probeOver;
            if (nowMs - probeStartMs >= p.probeMs)
            {
                probing = false;
                probeAtMs = nowMs + p.probeEveryMs;
                if (! (probeCalls >= 4 && probeOver * 2 > probeCalls))
                    on = false; // the audio keeps up without yielding: recovered
            }
            if (on)
            {
                engagedFlag.store (on, std::memory_order_relaxed);
                return 0.0;
            }
        }
        else if (nowMs >= probeAtMs)
        {
            probing = true;
            probeStartMs = nowMs;
            probeCalls = probeOver = 0;
            engagedFlag.store (on, std::memory_order_relaxed);
            return 0.0;
        }
        engagedFlag.store (on, std::memory_order_relaxed);
        if (! on)
            return 0.0;
        const auto y = std::clamp (usedMs * p.yieldShare, p.minYieldMs, p.maxYieldMs);
        yieldedUs.fetch_add ((std::uint64_t) (y * 1000.0), std::memory_order_relaxed);
        return y;
    }

    // A pause in the callbacks (device restarted / stopped): the window starts again.
    void restart() noexcept
    {
        windowStartMs = 0.0;
        windowCalls = windowOver = 0;
        lastWindowOverloaded = false;
        probing = false;
        engagedFlag.store (false, std::memory_order_relaxed);
    }

    bool engaged() const noexcept { return engagedFlag.load (std::memory_order_relaxed); }
    std::uint64_t timesEngaged() const noexcept { return engagements.load (std::memory_order_relaxed); }
    double yieldedMs() const noexcept { return (double) yieldedUs.load (std::memory_order_relaxed) / 1000.0; }

    // Sleeps the calling thread for `ms` (Windows: a high-resolution waitable timer, so a 0.5 ms slice is not the
    // scheduler's 15.6 ms tick; elsewhere nanosleep). For the audio thread under the guard only.
    static void sleepFor (double ms) noexcept;

private:
    Params p;
    std::atomic<bool> enabled { true }, engagedFlag { false };
    std::atomic<double> lastUiMs { 0.0 };
    std::atomic<std::uint64_t> engagements { 0 }, yieldedUs { 0 };
    // audio thread only
    double windowStartMs = 0.0, lastOverloadMs = -1.0e12;
    int windowCalls = 0, windowOver = 0;
    bool lastWindowOverloaded = false;
    bool probing = false;
    double probeAtMs = 0.0, probeStartMs = 0.0;
    int probeCalls = 0, probeOver = 0;
};
} // namespace lynx

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
// stays engaged while the overload lasts and lets go 0.5 s after the last overloaded window, or at once when the
// transport stops (round 190os: the stopped song's tails then render at full speed instead of 2.5x slower; a
// project that stays overloaded while stopped — live input through heavy plugins — engages it again after 3 s).
// Nothing changes
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
        // engaged until this long after the last overloaded window (round 190os: 1.5 s -> 0.5 s, so the screen and the
        // audio are back to normal within 1 s of the overload's end: perf190os/HANDOFF.md)
        double releaseMs = 500.0;
        // round 190os: the transport stopped lets go at once, and the guard does not engage again for this long while
        // the song stays stopped (the tails of what played: they end by themselves, faster without the yield)
        double stopGraceMs = 3000.0;
        double yieldShare = 1.5;   // the slice given up: this share of the block's own processing time…
        double minYieldMs = 0.5, maxYieldMs = 10.0; // …within these bounds
        bool needUiLate = true;    // false ("ui=0"): engaged on the audio's overload alone
        // the probe (probeEveryMs > 0): while engaged, every probeEveryMs the audio runs probeMs without yielding, and the
        // guard lets go when the callbacks then keep their deadline in most of it. Round 190ok added it for a stopped
        // project's tails, which die out in audio time, so the yield kept them (and itself) going. Off since round
        // 190os: the stop now lets go at once, and the processing time it judges does not include the yield, so the
        // windows above see the same; each probe starved the screen for its 250 ms (perf190os/HANDOFF.md)
        double probeEveryMs = 0.0, probeMs = 250.0;
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
            else if (key == "stopgrace") p.stopGraceMs = v;
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
    // deadline, `playing` whether the transport runs (round 190os). Returns how long to yield now (ms, 0 = none). No
    // lock, no allocation.
    double afterCallback (double nowMs, double usedMs, double budgetMs, bool playing = true) noexcept
    {
        if (windowStartMs <= 0.0 || nowMs < windowStartMs)
            windowStartMs = nowMs;
        ++windowCalls;
        const auto over = budgetMs > 0.0 && usedMs >= budgetMs;
        if (over)
        {
            ++windowOver;
            clearSinceMs = -1.0;
        }
        else if (clearSinceMs < 0.0)
            clearSinceMs = nowMs;
        if (nowMs - windowStartMs >= p.windowMs)
        {
            // (round 190os: or at least two late callbacks in a window that had fewer than 4: callbacks that each take
            // longer than a quarter of the window — a deeper overload — counted too)
            lastWindowOverloaded = windowOver * 2 > windowCalls && (windowCalls >= 4 || windowOver >= 2);
            if (lastWindowOverloaded)
                lastOverloadMs = nowMs;
            windowStartMs = nowMs;
            windowCalls = windowOver = 0;
        }
        // round 190os: the transport's stop (seen here, on the audio thread)
        if (playing)
            stoppedAtMs = -1.0e12;
        else if (wasPlaying)
            stoppedAtMs = nowMs;
        const auto justStopped = wasPlaying && ! playing;
        wasPlaying = playing;

        auto on = engagedFlag.load (std::memory_order_relaxed);
        if (! enabled.load (std::memory_order_relaxed))
            on = false;
        else if (! on)
        {
            const auto ui = lastUiMs.load (std::memory_order_relaxed);
            if (lastWindowOverloaded && (! p.needUiLate || (ui > 0.0 && nowMs - ui > p.uiLateMs)) && nowMs - stoppedAtMs > p.stopGraceMs)
            {
                on = true;
                probeAtMs = nowMs + p.probeEveryMs;
                probing = false;
                engagements.fetch_add (1, std::memory_order_relaxed);
            }
        }
        else if (justStopped)
        {
            on = false;
            noteRelease (nowMs, releasedOnStop, 0.0);
        }
        else if (nowMs - lastOverloadMs > p.releaseMs)
        {
            on = false;
            noteRelease (nowMs, releasedOnLoad, clearSinceMs >= 0.0 ? nowMs - clearSinceMs : 0.0);
        }
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
                {
                    on = false; // the audio keeps up without yielding: recovered
                    noteRelease (nowMs, releasedOnProbe, clearSinceMs >= 0.0 ? nowMs - clearSinceMs : 0.0);
                }
            }
            if (on)
            {
                setState (on, 2);
                return 0.0;
            }
        }
        else if (p.probeEveryMs > 0.0 && nowMs >= probeAtMs)
        {
            probing = true;
            probeStartMs = nowMs;
            probeCalls = probeOver = 0;
            setState (on, 2);
            return 0.0;
        }
        setState (on, on ? 1 : 0);
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
        clearSinceMs = -1.0;
        setState (false, 0);
    }

    bool engaged() const noexcept { return engagedFlag.load (std::memory_order_relaxed); }
    std::uint64_t timesEngaged() const noexcept { return engagements.load (std::memory_order_relaxed); }
    double yieldedMs() const noexcept { return (double) yieldedUs.load (std::memory_order_relaxed) / 1000.0; }

    // Round 190os, for --uiperf and the tests: the last release — when (the callbacks' clock), why, and how long after the
    // callbacks kept their deadline again it came (0 for a stop: at the first callback after it). The state: 0 off,
    // 1 yielding, 2 engaged in a probe (no yield).
    enum Reason { notReleased = 0, releasedOnStop = 1, releasedOnLoad = 2, releasedOnProbe = 3 };
    double lastReleaseAtMs() const noexcept { return releaseAt.load (std::memory_order_relaxed); }
    int lastReleaseReason() const noexcept { return releaseReason.load (std::memory_order_relaxed); }
    double lastReleaseLagMs() const noexcept { return releaseLag.load (std::memory_order_relaxed); }
    const std::atomic<int>& stateTag() const noexcept { return state; }

    // Sleeps the calling thread for `ms` (Windows: a high-resolution waitable timer, so a 0.5 ms slice is not the
    // scheduler's 15.6 ms tick; elsewhere nanosleep). For the audio thread under the guard only.
    static void sleepFor (double ms) noexcept;

private:
    void setState (bool on, int tag) noexcept
    {
        engagedFlag.store (on, std::memory_order_relaxed);
        state.store (tag, std::memory_order_relaxed);
    }
    void noteRelease (double nowMs, Reason why, double lagMs) noexcept
    {
        probing = false;
        releaseAt.store (nowMs, std::memory_order_relaxed);
        releaseReason.store (why, std::memory_order_relaxed);
        releaseLag.store (lagMs, std::memory_order_relaxed);
    }

    Params p;
    std::atomic<bool> enabled { true }, engagedFlag { false };
    std::atomic<int> state { 0 }, releaseReason { notReleased };
    std::atomic<double> releaseAt { 0.0 }, releaseLag { 0.0 };
    std::atomic<double> lastUiMs { 0.0 };
    std::atomic<std::uint64_t> engagements { 0 }, yieldedUs { 0 };
    // audio thread only
    double windowStartMs = 0.0, lastOverloadMs = -1.0e12;
    int windowCalls = 0, windowOver = 0;
    bool lastWindowOverloaded = false;
    bool probing = false;
    double probeAtMs = 0.0, probeStartMs = 0.0;
    int probeCalls = 0, probeOver = 0;
    double clearSinceMs = -1.0;     // the first callback within its deadline since the last late one (-1: the last was late)
    bool wasPlaying = false;
    double stoppedAtMs = -1.0e12;   // the transport's last stop while it stays stopped (-1e12: playing, or never played)
};
} // namespace lynx

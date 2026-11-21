// Round 190os (0.1.31 perf round 3): the safe-overload guard's release (core/OverloadGuard.h) — at once when the transport
// stops, within 1 s of the overload's end while playing — and the windows of a deeper overload.

#include "core/OverloadGuard.h"

namespace
{
// 64 samples at 48 kHz: a callback every 1.333 ms; `used` processing ms per callback, the UI starving unless uiOk
struct GuardRun
{
    static constexpr double budget = 64.0 / 48.0;
    lynx::OverloadGuard g;
    double now = 1000.0, pulse = 1000.0;
    GuardRun() { g.uiPulse (now); }
    // runs `ms` of callbacks; returns the time of the first callback after which the guard was off (-1: never)
    double run (double ms, double used, bool playing, bool uiOk = false)
    {
        double offAt = -1.0;
        for (const auto end = now + ms; now < end;)
        {
            if (uiOk && now - pulse >= 33.0)
                g.uiPulse (pulse = now);
            const auto y = g.afterCallback (now, used, budget, playing);
            if (offAt < 0.0 && ! g.engaged())
                offAt = now;
            now += std::max (budget, used) + y;
        }
        return offAt;
    }
};
} // namespace

TEST (perf190os_overload_guard_lets_go_at_once_when_the_transport_stops)
{
    using namespace lynx;
    GuardRun r;
    r.run (600.0, 2.0 * GuardRun::budget, true);
    CHECK (r.g.engaged() && r.g.timesEngaged() == 1);
    CHECK (r.g.stateTag().load() == 1);
    // the stop: the very next callback yields nothing, though the tails keep missing the deadline and the UI starves
    const auto stopAt = r.now;
    CHECK (r.g.afterCallback (r.now, 2.0 * GuardRun::budget, GuardRun::budget, false) == 0.0);
    CHECK (! r.g.engaged() && r.g.lastReleaseReason() == OverloadGuard::releasedOnStop && r.g.lastReleaseAtMs() == stopAt);
    CHECK (r.g.stateTag().load() == 0);
    r.now += 2.0 * GuardRun::budget;
    // tails for 2.5 s (within the stop's grace): never engaged again, no yield
    const auto yieldedBefore = r.g.yieldedMs();
    r.run (2500.0, 2.0 * GuardRun::budget, false);
    CHECK (! r.g.engaged() && r.g.timesEngaged() == 1 && r.g.yieldedMs() == yieldedBefore);
    // still overloaded while stopped after the grace (live input through heavy plugins): engaged again
    r.run (1000.0, 2.0 * GuardRun::budget, false);
    CHECK (r.g.engaged() && r.g.timesEngaged() == 2);
    // stopped and engaged, then played and stopped again: let go at the stop again
    r.run (300.0, 2.0 * GuardRun::budget, true);
    CHECK (r.g.engaged());
    r.g.afterCallback (r.now, 2.0 * GuardRun::budget, GuardRun::budget, false);
    CHECK (! r.g.engaged() && r.g.lastReleaseReason() == OverloadGuard::releasedOnStop);
    // a device that starts with the transport stopped (never played): a steady overload engages it at once
    GuardRun idle;
    idle.run (600.0, 2.0 * GuardRun::budget, false);
    CHECK (idle.g.engaged());
}

TEST (perf190os_overload_guard_lets_go_within_1_s_of_the_overloads_end_while_playing)
{
    using namespace lynx;
    for (const auto heavy : { 2.0, 5.0, 12.0 }) // (12x: 16 ms callbacks, fewer than 4 in a 100 ms window)
    {
        GuardRun r;
        r.run (2000.0, heavy * GuardRun::budget, true);
        CHECK (r.g.engaged());
        // the load ends while the song plays on: off within 1 s of the first callback that kept its deadline
        const auto clearAt = r.now;
        const auto offAt = r.run (3000.0, 0.5 * GuardRun::budget, true);
        CHECK (offAt > clearAt && offAt - clearAt < 1000.0);
        CHECK (r.g.lastReleaseReason() == OverloadGuard::releasedOnLoad);
        CHECK (r.g.lastReleaseLagMs() > 0.0 && r.g.lastReleaseLagMs() < 1000.0 && std::abs (r.g.lastReleaseLagMs() - (offAt - clearAt)) < 2.0 * GuardRun::budget);
        // and stays off
        CHECK (! r.g.engaged() && r.g.timesEngaged() == 1);
    }
    // a deep overload (60 ms callbacks: fewer than 4 in a 100 ms window, every one late): still an overload, engaged
    GuardRun deep;
    deep.run (1000.0, 60.0, true);
    CHECK (deep.g.engaged());
}

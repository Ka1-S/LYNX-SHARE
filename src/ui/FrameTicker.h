#pragma once

// Display-rate animation (round 174e). The playhead, the meters and the other live views used to move on fixed 30 Hz
// timers, so on a 144 Hz display they moved at most 30 times a second (FL Studio: ~115-144). A FrameTicker calls its
// view's frame step at the display's refresh instead (juce::VBlankAttachment: one per window, on the display that
// window is on), and only while the view animates:
//  - attached: one step per vertical blank (at most `maxHz`, 170 by default: a 240 Hz display gets every other one;
//    capped tickers all run on the same frames; Settings › General › Animation rate caps them all at 60 or 30);
//  - after ~0.3 s of steps that changed nothing (transport stopped, meters fallen) it detaches; a light idle poll
//    (20 Hz, the same step; one timer for every ticker since round 190gd) brings it back as soon as something moves,
//    so an idle view costs no repaint at all;
//  - the poll also covers a window that gets no vertical blank (no peer yet, a platform without them).
// The step returns true while something moved. It repaints only what changed (each view does its own dirty rects).
//
// LYNX_FRAME_HZ=<n> (diagnostics): no vertical blank, a plain n Hz timer (30 = the behaviour before this round), for
// A/B measurements with one build.

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/FramePacing.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <vector>

namespace lynx::ui
{
class FrameTicker final
{
public:
    // owner: the view (its window's refresh drives the ticker). step (nowMs): one animation step, true while it moved.
    FrameTicker (juce::Component& ownerIn, std::function<bool (double)> stepIn, double maxHzIn = 170.0)
        : owner (ownerIn), step (std::move (stepIn)), maxHz (maxHzIn)
    {
        Poll::add (this);
    }

    ~FrameTicker() { Poll::remove (this); }

    // something started to move (Play, a new meter level): run at the display rate from the next refresh
    void wake()
    {
        if (fixedHz() <= 0 && vblank.isEmpty())
            attach();
    }

    bool isAttached() const noexcept { return ! vblank.isEmpty(); }

    // diagnostics (--uiperf, --fluidperf): steps run by all tickers, and how many are attached now
    static std::atomic<int>& stepsRun()
    {
        static std::atomic<int> n { 0 };
        return n;
    }
    static std::atomic<int>& attachedNow()
    {
        static std::atomic<int> n { 0 };
        return n;
    }

    // Settings › General › Animation rate (round 174e2): 0 = the display's refresh, else at most this many frames a
    // second (60, 30) for every ticker, still on the display's vertical blanks (message thread)
    static int& rateLimit()
    {
        static int hz = 0;
        return hz;
    }

    // LYNX_FRAME_HZ: a fixed rate instead of the display's (0: the display's)
    static int fixedHz()
    {
        static const int hz = [] {
            const auto* v = std::getenv ("LYNX_FRAME_HZ");
            return v != nullptr ? juce::jlimit (0, 1000, std::atoi (v)) : 0;
        }();
        return hz;
    }

private:
    static constexpr int idleHz = 20;
    static constexpr int quietFramesToDetach = 40; // (~0.3 s at 144 Hz)

    void attach()
    {
        vblank = juce::VBlankAttachment (&owner, [this] (double seconds) { onVBlank (seconds); });
        attachedNow().fetch_add (1, std::memory_order_relaxed);
        quiet = 0;
        lastVBlankMs = juce::Time::getMillisecondCounterHiRes();
    }

    void detach()
    {
        if (vblank.isEmpty())
            return;
        vblank = juce::VBlankAttachment();
        attachedNow().fetch_sub (1, std::memory_order_relaxed);
    }

    bool run (double nowMs)
    {
        stepsRun().fetch_add (1, std::memory_order_relaxed);
        return step != nullptr && step (nowMs);
    }

    void onVBlank (double seconds)
    {
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();
        lastVBlankMs = nowMs;
        // at most maxHz (or the Animation rate), on the same frames for every ticker (one window present per frame
        // instead of one per ticker): the display's frame period is measured from the vertical blanks' own clock
        const auto frameMs = seconds * 1000.0;
        auto& period = framePeriodMs();
        if (const auto delta = frameMs - lastFrameMs; lastFrameMs > 0.0 && delta > 2.0 && delta < 50.0)
            period = period <= 0.0 ? delta : period + 0.05 * (juce::jmin (delta, period * 1.5) - period);
        if (frameMs == lastFrameMs)
            return; // (the same frame again)
        lastFrameMs = frameMs;
        if (period > 0.0)
        {
            // below the refresh, the frames where this cap's next one is due (core/FramePacing.h): 72 Hz on a 144 Hz
            // display = every other frame, 60 Hz = every 2nd / 3rd, the same frames for every ticker with that cap
            const auto cap = rateLimit() > 0 ? juce::jmin (maxHz, (double) rateLimit()) : maxHz;
            if (! pacerFor (cap).runs (frameMs, period, cap))
                return;
        }

        if (owner.isShowing() && run (nowMs))
            quiet = 0;
        else
            ++quiet; // (detached by the timer: not from inside the attachment's own callback)
    }

    // One pacer per cap, shared by the tickers with that cap (they run on the same frames). Message thread.
    static framepace::Pacer& pacerFor (double capHz)
    {
        static std::map<int, framepace::Pacer> pacers;
        return pacers[juce::roundToInt (capHz * 10.0)];
    }

    static double& framePeriodMs()
    {
        static double ms = 0.0; // (message thread)
        return ms;
    }

    // The idle poll of every ticker (round 190gd): one timer for all of them. Each ticker had its own 20 Hz timer, on
    // its own phase: five views woke JUCE's timer thread and the message thread ~100 times a second with nothing
    // moving; now 20. Message thread; made by the first ticker, deleted after the last one.
    struct Poll final : private juce::Timer
    {
        std::vector<FrameTicker*> tickers;
        bool polling = false, orphaned = false;

        static Poll*& instance()
        {
            static Poll* p = nullptr;
            return p;
        }
        static void add (FrameTicker* t)
        {
            auto*& p = instance();
            if (p == nullptr)
                p = new Poll();
            p->tickers.push_back (t);
            if (! p->isTimerRunning())
                p->startTimerHz (fixedHz() > 0 ? fixedHz() : idleHz);
        }
        static void remove (FrameTicker* t)
        {
            auto*& p = instance();
            if (p == nullptr)
                return;
            p->tickers.erase (std::remove (p->tickers.begin(), p->tickers.end(), t), p->tickers.end());
            if (! p->tickers.empty())
                return;
            p->stopTimer();
            if (p->polling)
                p->orphaned = true; // (deleted when its own callback returns)
            else
                delete p;
            p = nullptr;
        }
        void timerCallback() override
        {
            polling = true;
            const auto list = tickers; // (a step may delete views, and their tickers with them)
            for (auto* t : list)
                if (! orphaned && std::find (tickers.begin(), tickers.end(), t) != tickers.end())
                    t->poll();
            polling = false;
            if (orphaned)
                delete this;
        }
    };

    void poll()
    {
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();
        if (fixedHz() > 0) // diagnostics: a plain timer
        {
            if (owner.isShowing())
                run (nowMs);
            return;
        }
        if (isAttached() && quiet > quietFramesToDetach)
            detach();
        if (isAttached() && nowMs - lastVBlankMs < 150.0)
            return; // the display drives it
        // idle (or a window without vertical blanks): the same step at the poll's rate; moving → the display's rate
        if (owner.isShowing() && run (nowMs) && ! isAttached())
            attach();
    }

    juce::Component& owner;
    std::function<bool (double)> step;
    double maxHz;
    juce::VBlankAttachment vblank;
    int quiet = 0;
    double lastVBlankMs = 0.0, lastFrameMs = 0.0;
};

// Time-based falloff for display ballistics written per 30 Hz tick: `perTick` applied for `elapsedMs` of display time
// (a 144 Hz frame falls by perTick^(30/144), the same dB per second as before)
inline float fallFor (float perTick, double elapsedMs)
{
    return (float) std::pow ((double) perTick, juce::jlimit (0.0, 30.0, elapsedMs / (1000.0 / 30.0)));
}
} // namespace lynx::ui

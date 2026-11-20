#pragma once

// Display-rate animation (round 174e). The playhead, the meters and the other live views used to move on fixed 30 Hz
// timers, so on a 144 Hz display they moved at most 30 times a second (FL Studio: ~115-144). A FrameTicker calls its
// view's frame step at the display's refresh instead (juce::VBlankAttachment: one per window, on the display that
// window is on), and only while the view animates:
//  - attached: one step per vertical blank (at most `maxHz`, 170 by default: a 240 Hz display gets every other one;
//    capped tickers all run on the same frames; Settings › General › Animation rate caps them all at Balanced / 60 /
//    30, on every n-th frame);
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
#include "ui/NativeChild.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <typeinfo>
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
    // FL's FPS readout ("Show FL Studio user interface screen refresh rate", 143 on a 144 Hz display in FL 2026): the rate
    // LYNX's moving views are drawn at — the display's refresh measured from its vertical blanks, under Settings › General ›
    // Animation rate (LYNX_FRAME_HZ when set); 0 before the first vertical blank
    static int uiFrameRate()
    {
        if (fixedHz() > 0)
            return fixedHz();
        const auto period = framePeriodMs();
        if (period <= 0.0)
            return 0;
        const auto refresh = 1000.0 / period;
        const auto n = rateLimit() != 0 ? framepace::evenDivisor (refresh, rateLimit() < 0 ? framepace::balancedCap : (double) rateLimit()) : 1;
        return juce::roundToInt (refresh / (double) juce::jmax (1, n));
    }

    // Settings › General › Animation rate (round 174e2): 0 = the display's refresh, else at most this many frames a
    // second (60, 30) for every ticker, still on the display's vertical blanks; -1 = Balanced (round 190ip: the refresh
    // halved until it is at most 100 Hz). Evenly paced: every n-th frame (message thread)
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
        const auto moved = step != nullptr && step (nowMs);
        if (perOwnerWanted()) // (round 190kr, --uiperf: which live views step, and how often they move)
        {
            auto& counts = perOwner()[typeid (owner).name()];
            ++counts.first;
            counts.second += moved ? 1 : 0;
        }
        return moved;
    }

public:
    // diagnostics (round 190kr, LYNX_UIPERF_TICKERS=1): per view type, the steps run and the steps that moved (message thread)
    static bool perOwnerWanted()
    {
        static const bool on = std::getenv ("LYNX_UIPERF_TICKERS") != nullptr || std::getenv ("LYNX_TICKER_LOG") != nullptr;
        return on;
    }
    static std::map<std::string, std::pair<long long, long long>>& perOwner()
    {
        static std::map<std::string, std::pair<long long, long long>> m;
        return m;
    }

private:

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
            // Settings › General › Animation rate (round 190ip): every n-th frame, evenly (core/FramePacing.h), the same
            // frames for every ticker; a ticker's own lower cap (maxHz) still holds
            const auto n = rateLimit() != 0 ? framepace::evenDivisor (1000.0 / period, rateLimit() < 0 ? framepace::balancedCap : (double) rateLimit()) : 1;
            // (2 % slack: the toolbar's own 72 Hz cap on a 144 Hz display is the same rate as Balanced, and must run on the
            // same frames, not on the other half: round 190ip measured 141 updates a second for that, 72 with it)
            if (n > 1 && maxHz >= 0.98 * 1000.0 / period / n)
            {
                if (! dividerFor (n).runs (frameMs, period, n))
                    return;
            }
            // below the refresh, the frames where this cap's next one is due (core/FramePacing.h): 72 Hz on a 144 Hz
            // display = every other frame, the same frames for every ticker with that cap
            else if (! pacerFor (maxHz).runs (frameMs, period, maxHz))
                return;
        }

        if (showing() && run (nowMs))
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

    static framepace::Divider& dividerFor (int n)
    {
        static std::map<int, framepace::Divider> dividers;
        return dividers[n];
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
            writeLog();
            polling = true;
            const auto list = tickers; // (a step may delete views, and their tickers with them)
            for (auto* t : list)
                if (! orphaned && std::find (tickers.begin(), tickers.end(), t) != tickers.end())
                    t->poll();
            polling = false;
            if (orphaned)
                delete this;
        }

        // round 190kr: LYNX_TICKER_LOG=<file>, every 2 s: each view type's frame steps per second (run / moved), the
        // steps left out because the view was covered, and for every ticker whose view is showing how the cover check
        // sees its window (ui/NativeChild.h describeCover)
        void writeLog()
        {
            static const auto path = juce::SystemStats::getEnvironmentVariable ("LYNX_TICKER_LOG", {});
            if (path.isEmpty())
                return;
            const auto now = juce::Time::getMillisecondCounterHiRes();
            if (logSince <= 0.0)
            {
                logSince = now;
                logStart = perOwner();
                logCovered = nativechild::coveredSkips().load();
                return;
            }
            if (now - logSince < 2000.0)
                return;
            const auto s = (now - logSince) / 1000.0;
            juce::String text;
            text << juce::String (now / 1000.0, 1) << " tickers attached " << attachedNow().load() << ":";
            for (const auto& [name, counts] : perOwner())
            {
                const auto before = logStart.count (name) != 0 ? logStart.at (name) : std::pair<long long, long long> {};
                text << " " << juce::String (name).fromLastOccurrenceOf ("::", false, false).retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")
                     << " " << juce::String ((double) (counts.first - before.first) / s, 0) << "/" << juce::String ((double) (counts.second - before.second) / s, 0);
            }
            text << "; covered skips " << juce::String ((double) (nativechild::coveredSkips().load() - logCovered) / s, 0) << "/s\n";
            for (auto* t : tickers)
                if (t->owner.isShowing())
                    text << "    " << juce::String (typeid (t->owner).name()) << ": " << nativechild::describeCover (t->owner) << "\n";
            juce::File (path).appendText (text);
            logSince = now;
            logStart = perOwner();
            logCovered = nativechild::coveredSkips().load();
        }
        double logSince = 0.0;
        std::map<std::string, std::pair<long long, long long>> logStart;
        int logCovered = 0;
    };

    void poll()
    {
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();
        if (fixedHz() > 0) // diagnostics: a plain timer
        {
            if (showing())
                run (nowMs);
            return;
        }
        if (isAttached() && quiet > quietFramesToDetach)
            detach();
        if (isAttached() && nowMs - lastVBlankMs < 150.0)
            return; // the display drives it
        // idle (or a window without vertical blanks): the same step at the poll's rate; moving → the display's rate
        if (showing() && run (nowMs) && ! isAttached())
            attach();
    }

    // round 190kr (QA-11): a view whose docked panel lies wholly under other panels' windows (the Playlist behind
    // Thief's maximised Channel rack) is "showing" for JUCE, but nothing it paints reaches the screen: its steps wait
    // (the idle poll looks again 20 times a second, and the first step after it shows again catches up)
    bool showing() const
    {
        if (! owner.isShowing())
            return false;
        if (nativechild::coveredBySiblingWindows (owner))
        {
            nativechild::coveredSkips().fetch_add (1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    juce::Component& owner;
    std::function<bool (double)> step;
    double maxHz;
    juce::VBlankAttachment vblank;
    int quiet = 0;
    double lastVBlankMs = 0.0, lastFrameMs = 0.0;
};

// A step at every vertical blank, attached only while it is asked for (round 190gv). A window whose VBlankAttachments
// are all gone gets no vertical blank at all while it has nothing to paint (cmake/LynxPatchJuce.cmake, fix 32), so a
// view keeps none for an animation that is not running. start(): from the next vertical blank, until `step` returns
// false (then it detaches after that callback, never from inside it).
class OnDemandVBlank final : private juce::AsyncUpdater
{
public:
    OnDemandVBlank (juce::Component& ownerIn, std::function<bool()> stepIn) : owner (ownerIn), step (std::move (stepIn)) {}
    ~OnDemandVBlank() override { cancelPendingUpdate(); }

    void start()
    {
        cancelPendingUpdate(); // (a detach still due is not needed any more)
        if (attachment.isEmpty())
            attachment = juce::VBlankAttachment (&owner, [this] {
                if (step == nullptr || ! step())
                    triggerAsyncUpdate();
            });
    }

    bool isAttached() const noexcept { return ! attachment.isEmpty(); }

private:
    void handleAsyncUpdate() override { attachment = juce::VBlankAttachment(); }

    juce::Component& owner;
    std::function<bool()> step;
    juce::VBlankAttachment attachment;
};

// Time-based falloff for display ballistics written per 30 Hz tick: `perTick` applied for `elapsedMs` of display time
// (a 144 Hz frame falls by perTick^(30/144), the same dB per second as before)
inline float fallFor (float perTick, double elapsedMs)
{
    return (float) std::pow ((double) perTick, juce::jlimit (0.0, 30.0, elapsedMs / (1000.0 / 30.0)));
}
} // namespace lynx::ui

#pragma once

// UI responsiveness diagnostics (REA round 151). With LYNX_TRACE_UI set, %APPDATA%\LYNX\ui.log receives
//  - every message-thread stall above 16 ms (a watchdog thread pings the message loop every 4 ms and measures how
//    long the answer took), with the slowest paint / step named by uitrace::Scope seen during it;
//  - each Scope (a paint of a big view, a heavy UI step) that took longer than 8 ms;
//  - once a second: the slowest stall, the number of paints and their slowest, and the audio load (from `summary`).
// Off (the default), a Scope costs one relaxed atomic load.

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../platform/Paths.h"

#include <atomic>
#include <functional>
#include <memory>

namespace lynx::uitrace
{
inline bool enabled()
{
    static const bool on = std::getenv ("LYNX_TRACE_UI") != nullptr;
    return on;
}

struct State
{
    std::atomic<double> worstScopeMs { 0.0 };
    std::atomic<int> paints { 0 };
    std::atomic<double> worstStallMs { 0.0 };
    std::atomic<int> frames { 0 };              // frames the main window painted (ui/FrameProbe.h, round 173f)
    std::atomic<double> worstFrameGapMs { 0.0 }; // the longest time between two of them
    double lastFrameMs = 0.0;                    // (message thread)
    juce::CriticalSection lock;
    juce::String worstScopeName, pending; // pending: log lines written by the watchdog thread
    std::function<juce::String()> summary;
};

inline State& state()
{
    static State s;
    return s;
}

// The main window painted a frame (message thread; ui/FrameProbe.h). Gaps over 1 s are idle time, not a hitch.
inline void frame (double nowMs)
{
    auto& s = state();
    s.frames.fetch_add (1, std::memory_order_relaxed);
    if (const auto gap = nowMs - s.lastFrameMs; s.lastFrameMs > 0.0 && gap < 1000.0 && gap > s.worstFrameGapMs.load (std::memory_order_relaxed))
        s.worstFrameGapMs.store (gap, std::memory_order_relaxed);
    s.lastFrameMs = nowMs;
}

// Scopes slower than this are logged (LYNX_TRACE_UI_MIN=<ms>, default 8)
inline double logThresholdMs()
{
    static const double ms = [] {
        const auto* v = std::getenv ("LYNX_TRACE_UI_MIN");
        return v != nullptr ? juce::jmax (0.0, juce::String (v).getDoubleValue()) : 8.0;
    }();
    return ms;
}

inline void append (const juce::String& line)
{
    const juce::ScopedLock sl (state().lock);
    state().pending << juce::String (juce::Time::getMillisecondCounterHiRes() / 1000.0, 3) << "  " << line << "\n";
}

// Times a block of the message thread: { uitrace::Scope s ("Playlist paint"); ... }
class Scope
{
public:
    explicit Scope (const char* nameIn, bool isPaint = true, juce::Rectangle<int> areaIn = {}) : name (nameIn), area (areaIn), paint (isPaint)
    {
        if (enabled())
            start = juce::Time::getMillisecondCounterHiRes();
    }
    ~Scope()
    {
        if (start <= 0.0)
            return;
        const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
        auto& s = state();
        if (paint)
            s.paints.fetch_add (1, std::memory_order_relaxed);
        if (ms > s.worstScopeMs.load (std::memory_order_relaxed))
        {
            s.worstScopeMs.store (ms, std::memory_order_relaxed);
            const juce::ScopedLock sl (s.lock);
            s.worstScopeName = name;
        }
        if (ms > logThresholdMs())
            append (juce::String (ms, 1) + " ms  " + name + (area.isEmpty() ? juce::String() : "  [" + area.toString() + "]"));
    }
    Scope (const Scope&) = delete;
    Scope& operator= (const Scope&) = delete;

    double elapsedMs() const { return start > 0.0 ? juce::Time::getMillisecondCounterHiRes() - start : 0.0; }

private:
    const char* name;
    juce::Rectangle<int> area;
    bool paint;
    double start = 0.0;
};

// Parts of one paint, summed (message thread only): { uitrace::Part p (uitrace::part::waveform); ... } then
// uitrace::flushParts ("Playlist") logs "Playlist parts: waveform 12.1 ms x240, ..." and clears them.
enum class part { frame, title, waveform, outline, notes, lanes, batch, batchWaves, count };
inline const char* partName (part p)
{
    static const char* names[] = { "frame", "title", "waveform", "outline", "notes", "lanes", "batch", "batch-waves" };
    return names[(int) p];
}
struct PartTally
{
    double ms[(int) part::count] {};
    int n[(int) part::count] {};
};
inline PartTally& parts()
{
    static PartTally t;
    return t;
}
class Part
{
public:
    explicit Part (part p) : which (p)
    {
        if (enabled())
            start = juce::Time::getMillisecondCounterHiRes();
    }
    ~Part()
    {
        if (start <= 0.0)
            return;
        parts().ms[(int) which] += juce::Time::getMillisecondCounterHiRes() - start;
        ++parts().n[(int) which];
    }
    Part (const Part&) = delete;
    Part& operator= (const Part&) = delete;

private:
    part which;
    double start = 0.0;
};
inline void flushParts (const char* owner, double paintMs)
{
    if (! enabled())
        return;
    auto& t = parts();
    if (paintMs > logThresholdMs())
    {
        juce::String line;
        line << owner << " parts:";
        for (int i = 0; i < (int) part::count; ++i)
            if (t.n[i] > 0)
                line << " " << partName ((part) i) << " " << juce::String (t.ms[i], 1) << " ms x" << t.n[i] << ",";
        append (line);
    }
    t = {};
}

// The watchdog: started once by the main window when tracing is on.
class Monitor : private juce::Thread
{
public:
    Monitor() : juce::Thread ("LYNX ui trace") {}
    ~Monitor() override { stopThread (2000); }

    void start (std::function<juce::String()> summary)
    {
        if (! enabled())
            return;
        state().summary = std::move (summary);
        // the displays as JUCE sees them (issue #45: menus on the wrong monitor of a multi-monitor setup)
        juce::String displays;
        for (const auto& d : juce::Desktop::getInstance().getDisplays().displays)
            displays << "display " << d.totalArea.toString() << " user " << d.userArea.toString() << " scale " << d.scale
                     << (d.isMain ? " main" : "") << "\n";
        platform::settingsFile ("ui.log").replaceWithText ("LYNX UI trace: stalls > 16 ms, scopes > 8 ms, a summary per second\n" + displays);
        startThread();
    }

private:
    struct Ping
    {
        std::atomic<bool> answered { true };
        std::atomic<double> sent { 0.0 };
    };

    void run() override
    {
        auto ping = std::make_shared<Ping>();
        double lastSummary = juce::Time::getMillisecondCounterHiRes();
        double secondWorst = 0.0;
        while (! threadShouldExit())
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            if (ping->answered.load())
            {
                ping->answered = false;
                ping->sent = now;
                juce::MessageManager::callAsync ([ping] {
                    const auto waited = juce::Time::getMillisecondCounterHiRes() - ping->sent.load();
                    auto& s = state();
                    if (waited > s.worstStallMs.load())
                        s.worstStallMs = waited;
                    if (waited > 16.0)
                    {
                        juce::String during;
                        {
                            const juce::ScopedLock sl (s.lock);
                            during = s.worstScopeName;
                        }
                        append ("STALL " + juce::String (waited, 1) + " ms (slowest scope this second: " + during + ")");
                    }
                    ping->answered = true;
                });
            }
            if (now - lastSummary >= 1000.0)
            {
                auto& s = state();
                secondWorst = s.worstStallMs.exchange (0.0);
                juce::String audio;
                if (s.summary != nullptr)
                    audio = s.summary();
                juce::String name;
                {
                    const juce::ScopedLock sl (s.lock);
                    name = s.worstScopeName;
                    s.worstScopeName = {};
                }
                append ("second: worst stall " + juce::String (secondWorst, 1) + " ms, frames " + juce::String (s.frames.exchange (0))
                        + " (longest gap " + juce::String (s.worstFrameGapMs.exchange (0.0), 1) + " ms), paints " + juce::String (s.paints.exchange (0))
                        + ", slowest scope " + juce::String (s.worstScopeMs.exchange (0.0), 1) + " ms " + name + "  " + audio);
                lastSummary = now;
                juce::String text;
                {
                    const juce::ScopedLock sl (s.lock);
                    text.swapWith (s.pending);
                }
                if (text.isNotEmpty())
                    platform::settingsFile ("ui.log").appendText (text);
            }
            wait (4);
        }
    }
};
} // namespace lynx::uitrace

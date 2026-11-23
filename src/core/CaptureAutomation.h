#pragma once

// Capture automation (round 190op, research190oe P7; LYNX extra, named after Live 13's announced "Capture Automation" so
// it can become Live-exact once Live 13 can be studied, P16). The automation sibling of Capture MIDI (core/Recorder.h,
// round 170d): knob, fader and button moves made while the transport plays but nothing records are kept in a buffer of
// the last minutes; "Capture automation" turns the last continuous gesture of each moved control into automation, in
// the layout's own automation form (AudioEngine::captureAutomation), one undo step.
//
// The buffer lives on the message thread only (AudioEngine::updateAutomationRecording samples the moved controls about
// 30 times a second, as automation recording does); the audio thread never sees it. No JUCE: tested in
// tests/TrackStates190opTests.inc.

#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

#include "core/Song.h"

namespace lynx::captureauto
{
struct Entry
{
    AutomationTarget target;
    double ms = 0.0;      // when (juce::Time::getMillisecondCounterHiRes)
    double tick = 0.0;    // the transport position then (as RecordedValue::tick: unwrapped, before loops are folded)
    float value = 0.0f;   // 0..1
    bool songMode = true; // the transport played the song (else a pattern)
    // the Clip-launch layout: the grid clip playing on the control's track then (its pattern, its own position), -1: none
    int clipPattern = -1;
    double clipTick = 0.0;
    std::int64_t clipEnd = 0;
    int pass = 0; // round 190pa: the play pass it was made in (Buffer::notePlaying), 0: unknown
};

// Round 190pa (P21): one stretch of playing without recording (from a start to a stop), in transport positions as
// Entry::tick; Latch holds a gesture's last value to the pass's end, Write also from its start.
struct Pass
{
    int id = 0;
    double startTick = 0.0, endTick = 0.0;
};

constexpr double defaultWindowMs = 2.0 * 60.0 * 1000.0; // the last two minutes
constexpr double mergeMs = 10.0;                        // at most one point per control per 10 ms
constexpr std::size_t maxEntries = 200000;              // (a hard bound: about 30 controls moved for two minutes)

class Buffer
{
public:
    double windowMs = defaultWindowMs;

    // A sample of a moved control; a sample of the same control less than 10 ms after its last one replaces it
    void add (const Entry& e)
    {
        for (auto it = list.rbegin(), stop = list.rbegin() + (std::ptrdiff_t) std::min<std::size_t> (list.size(), 64); it != stop; ++it)
            if (it->target == e.target)
            {
                if (e.ms - it->ms < mergeMs && e.songMode == it->songMode && e.clipPattern == it->clipPattern && e.tick >= it->tick)
                {
                    *it = e;
                    trim (e.ms);
                    return;
                }
                break;
            }
        list.push_back (e);
        trim (e.ms);
    }

    // Drops what is older than the window (and anything past the hard bound)
    void trim (double nowMs)
    {
        while (! list.empty() && (nowMs - list.front().ms > windowMs || list.size() > maxEntries))
            list.pop_front();
    }

    // Round 190pa: the transport plays (not recording) at `tick` this frame / has stopped. A play after a stop, or a jump
    // back, starts a new pass. currentPass(): the id new entries get (0 while stopped).
    void notePlaying (double tick)
    {
        if (! playing || passes.empty() || tick < passes.back().endTick)
        {
            passes.push_back ({ ++passCounter, tick, tick });
            if (passes.size() > maxPasses)
                passes.pop_front();
            playing = true;
            return;
        }
        passes.back().endTick = tick;
    }
    void noteStopped() noexcept { playing = false; }
    int currentPass() const noexcept { return playing && ! passes.empty() ? passes.back().id : 0; }
    const Pass* pass (int id) const noexcept
    {
        for (const auto& p : passes)
            if (p.id == id)
                return &p;
        return nullptr;
    }

    void clear() { list.clear(); }
    bool empty() const noexcept { return list.empty(); }
    std::size_t size() const noexcept { return list.size(); }
    const std::deque<Entry>& entries() const noexcept { return list; }

private:
    static constexpr std::size_t maxPasses = 256;
    std::deque<Entry> list;
    std::deque<Pass> passes;
    int passCounter = 0;
    bool playing = false;
};

struct Gesture
{
    AutomationTarget target;
    std::vector<Entry> points; // oldest first
};

// Whether `b` continues the gesture `a` belongs to: the same transport mode and clip, and at most `gapTicks` later
// in the transport position (a jump back, a new play or a pause longer than that starts a new gesture).
inline bool continues (const Entry& a, const Entry& b, std::int64_t gapTicks) noexcept
{
    return a.songMode == b.songMode && a.clipPattern == b.clipPattern && b.tick >= a.tick && b.tick - a.tick <= (double) gapTicks;
}

// The last continuous gesture of every control moved after `afterMs` (newer than the last capture), in the order the
// controls were first moved. `gapTicks`: the longest pause inside one gesture (two bars).
inline std::vector<Gesture> lastGestures (const std::deque<Entry>& entries, double afterMs, std::int64_t gapTicks)
{
    std::vector<Gesture> out;
    for (const auto& e : entries)
    {
        if (e.ms <= afterMs)
            continue;
        auto it = std::find_if (out.begin(), out.end(), [&e] (const Gesture& g) { return g.target == e.target; });
        if (it == out.end())
        {
            out.push_back ({ e.target, { e } });
            continue;
        }
        if (! continues (it->points.back(), e, gapTicks))
            it->points.clear(); // a new gesture: only the last one is captured
        it->points.push_back (e);
    }
    return out;
}
} // namespace lynx::captureauto

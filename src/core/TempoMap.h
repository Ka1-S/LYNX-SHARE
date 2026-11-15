#pragma once

// Issue #86 (round 178a): the tempo at a song position, with FL's pattern Tempo events (docs/analise/REA_rodada178a.md
// §3, FL's time map FUN_012c9e20). A pattern can hold a Tempo event lane (PatternEvents with the tempo target, values
// 0..1 = 10..522 BPM like the tempo automation):
// - pattern mode: the current pattern's Tempo events, looped with the pattern; before its first event, the project tempo;
// - song mode: the pattern clips' Tempo events and the tempo automation clips. Inside a tempo automation clip the clip
//   wins; elsewhere the latest write holds (a pattern clip's event, or the value a tempo clip ended with). Nothing is
//   restored when a pattern clip ends. Before any write, the project tempo (or the first tempo clip's value, as LYNX's
//   tempo automation already did).
// JUCE-free, header-only and allocation-free: the audio thread calls it once per block when a song has tempo events.

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/Song.h"

namespace lynx::tempomap
{
// The pattern's Tempo lane with events, or nullptr.
inline const PatternEvents* tempoLane (const Pattern& p) noexcept
{
    for (const auto& lane : p.events)
        if (lane.target.kind == AutomationTargetKind::tempo && ! lane.events.empty())
            return &lane;
    return nullptr;
}

inline bool hasTempoEvents (const Song& song) noexcept
{
    return std::any_of (song.patterns.begin(), song.patterns.end(), [] (const Pattern& p) { return tempoLane (p) != nullptr; });
}

// The last tempo automation (the one that holds the slot; all of them play by the clip rule, issue #97), or −1.
inline int tempoAutomation (const Song& song) noexcept
{
    for (int i = (int) song.automations.size() - 1; i >= 0; --i)
        if (song.automations[(std::size_t) i].target.kind == AutomationTargetKind::tempo && ! song.automations[(std::size_t) i].isLive())
            return i;
    return -1;
}

// Whether the tempo can move while the song plays (song mode: tempo clips or pattern Tempo events; pattern mode: the
// current pattern's Tempo events).
inline bool active (const Song& song) noexcept
{
    if (song.mode != PlayMode::song)
        return song.currentPattern >= 0 && song.currentPattern < (int) song.patterns.size()
               && tempoLane (song.patterns[(std::size_t) song.currentPattern]) != nullptr;
    return tempoAutomation (song) >= 0 || hasTempoEvents (song);
}

struct Write
{
    double tick = -std::numeric_limits<double>::infinity();
    float value = std::numeric_limits<float>::quiet_NaN();
    bool any() const noexcept { return ! std::isnan (value); }
};

// The last event of `lane` with from <= tick <= at, or nullptr.
inline const PatternEvent* lastEvent (const PatternEvents& lane, double at, double from = -1.0) noexcept
{
    const auto it = std::upper_bound (lane.events.begin(), lane.events.end(), at, [] (double t, const PatternEvent& e) { return t < (double) e.tick; });
    if (it == lane.events.begin())
        return nullptr;
    const auto& e = *(it - 1);
    return (double) e.tick >= from ? &e : nullptr;
}

// The latest Tempo event pattern clip `c` has played at or before song tick `tick` (FL plays a clip's events only while
// the position is inside it), into `best` when later than what it holds.
inline void clipWrite (const Clip& c, const PatternEvents& lane, std::int64_t patternLength, double tick, Write& best) noexcept
{
    if (tick < (double) c.start || c.length <= 0)
        return;
    const auto t = std::min (tick, (double) c.end() - 1.0e-6);
    const auto offset = (double) c.offset;
    const auto loops = stretchedClipsLoop() && patternLength > 0 && c.offset + c.length > patternLength;
    if (! loops)
    {
        if (const auto* e = lastEvent (lane, t - (double) c.start + offset, offset))
        {
            const auto at = (double) c.start + (double) e->tick - offset;
            if (at > best.tick)
                best = { at, e->value };
        }
        return;
    }
    // a stretched clip loops its pattern (LYNX's stretchedClipsLoop, issue #25): each pass plays the events before the
    // pattern's length, the first pass from the clip's offset
    const auto length = (double) patternLength;
    const auto pos = t - (double) c.start + offset;
    const auto pass = std::floor (pos / length);
    const auto inPass = pos - pass * length;
    const auto* e = lastEvent (lane, std::min (inPass, length - 1.0e-6), pass == 0.0 ? offset : -1.0);
    auto at = 0.0;
    if (e != nullptr)
        at = (double) c.start - offset + pass * length + (double) e->tick;
    else if (pass > 0.0 && (e = lastEvent (lane, length - 1.0e-6, pass == 1.0 ? offset : -1.0)) != nullptr)
        at = (double) c.start - offset + (pass - 1.0) * length + (double) e->tick;
    if (e != nullptr && at > best.tick)
        best = { at, e->value };
}

// The tempo (BPM) at `tick`: a song position in song mode (inside the song's length), the pattern position (counting
// its loops from 0) in pattern mode.
inline double tempoAt (const Song& song, double tick) noexcept
{
    const auto base = song.bpm;
    if (song.mode != PlayMode::song)
    {
        if (song.currentPattern < 0 || song.currentPattern >= (int) song.patterns.size())
            return base;
        const auto& pattern = song.patterns[(std::size_t) song.currentPattern];
        const auto* lane = tempoLane (pattern);
        if (lane == nullptr)
            return base;
        const auto length = (double) std::max<std::int64_t> (1, pattern.effectiveLength (song.meter()));
        const auto pass = std::floor (std::max (0.0, tick) / length);
        const auto* e = lastEvent (*lane, std::max (0.0, tick) - pass * length);
        if (e == nullptr && pass > 0.0)
            e = lastEvent (*lane, length - 1.0e-6); // the previous loop's last event holds
        return e != nullptr ? tempoFromValue (e->value) : base;
    }

    // the tempo automations (issue #97, the rule of every target, RackRenderer's automationValueAt): inside clips the
    // one that started last wins (same start: the lowest automation); after them, the value the clip that ended last
    // ended with (written at its end); before the first clip, the first clip's first value (round 178a, lowest priority;
    // unverified against FL for the tempo: the other targets have their own value there)
    Write automation;
    if (tempoAutomation (song) >= 0)
    {
        const Clip* inside = nullptr;
        const Clip* ended = nullptr;
        const Clip* first = nullptr;
        for (const auto& c : song.playlist)
        {
            if (c.kind != ClipKind::automation || c.automation < 0 || c.automation >= (int) song.automations.size() || c.length <= 0
                || c.muted || song.isTrackMuted (c.track))
                continue;
            const auto& a = song.automations[(std::size_t) c.automation];
            if (a.target.kind != AutomationTargetKind::tempo || a.isLive())
                continue;
            if (first == nullptr || c.start < first->start || (c.start == first->start && c.automation < first->automation))
                first = &c;
            if (tick < (double) c.start)
                continue;
            if (tick < (double) c.end())
            {
                if (inside == nullptr || c.start > inside->start || (c.start == inside->start && c.automation < inside->automation))
                    inside = &c;
            }
            else if (ended == nullptr || c.end() > ended->end() || (c.end() == ended->end() && c.start > ended->start))
                ended = &c;
        }
        if (inside != nullptr)
            return tempoFromValue (song.automations[(std::size_t) inside->automation].playAt (tick - (double) inside->start + (double) inside->offset, tick));
        if (ended != nullptr)
            automation = { (double) ended->end(), song.automations[(std::size_t) ended->automation].playAt ((double) (ended->length + ended->offset), (double) ended->end()) };
        else if (first != nullptr)
            automation.value = song.automations[(std::size_t) first->automation].playAt ((double) first->offset, (double) first->start);
    }

    Write events;
    if (hasTempoEvents (song))
        for (const auto& c : song.playlist)
            if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size() && ! c.muted && ! song.isTrackMuted (c.track))
            {
                const auto& pattern = song.patterns[(std::size_t) c.pattern];
                if (const auto* lane = tempoLane (pattern))
                    clipWrite (c, *lane, pattern.effectiveLength (song.meter()), tick, events);
            }

    if (events.any() && (! automation.any() || events.tick > automation.tick))
        return tempoFromValue (events.value);
    if (automation.any())
        return tempoFromValue (automation.value);
    return base;
}
} // namespace lynx::tempomap

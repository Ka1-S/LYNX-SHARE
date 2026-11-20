#pragma once

// Tempo markers on the Playlist ruler (round 190hq, slice B11; community complaint #22 "no tempo map"). LYNX extra: FL
// Studio 2026 has no tempo marker (its ruler marker actions are 0…11, none of them a tempo: FLEngine's EEMarkerMode0…11
// menus, docs/analise/REA_rodada165f-markers.md §1.1, rechecked in round 190hq) and changes the tempo with a tempo
// automation clip; Live 12.4 changes it with the Main track's tempo automation in the Arrangement and with a scene's
// tempo in the Session view (core/ClipLauncher.h, rounds 190ef / 190em).
//
// The markers ARE a tempo automation clip, so they write the tempo map every other part of LYNX already plays
// (core/TempoMap.h): one automation flagged `tempoMarkers`, one clip of it from the song's start, a point per marker.
// The first point (tick 0) is the tempo before the first marker; each marker's point holds its tempo until the next
// (FL's Hold curve), or the segment before it ramps into it ("glide", a straight Single curve). After the clip's end the
// last tempo holds, as after any tempo clip. Older LYNX releases, the .flp export and FL itself read the clip as a plain
// tempo automation clip, so a project plays the same there.
// JUCE-free, header-only; UI thread (edits go through AudioEngine::editSong).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/FlCurve.h"
#include "core/PlaylistOps.h"
#include "core/Song.h"

namespace lynx::tempomarkers
{
struct Mark
{
    std::int64_t tick = 0; // song position
    double bpm = 120.0;
    bool glide = false; // the tempo ramps from the previous marker into this one
    bool operator== (const Mark&) const = default;
};

// The automation that holds the markers, or −1.
inline int automationIndex (const Song& song) noexcept
{
    for (int i = 0; i < (int) song.automations.size(); ++i)
        if (song.automations[(std::size_t) i].tempoMarkers && song.automations[(std::size_t) i].target.kind == AutomationTargetKind::tempo)
            return i;
    return -1;
}

// Its clip in the current arrangement, or nullptr.
inline const Clip* clipOf (const Song& song, int automation) noexcept
{
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::automation && c.automation == automation)
            return &c;
    return nullptr;
}
inline Clip* clipOf (Song& song, int automation) noexcept
{
    return const_cast<Clip*> (clipOf (static_cast<const Song&> (song), automation));
}

// The markers (song positions, sorted), without the start point; empty when there are none.
inline std::vector<Mark> list (const Song& song)
{
    std::vector<Mark> out;
    const auto a = automationIndex (song);
    const auto* clip = a >= 0 ? clipOf (song, a) : nullptr;
    if (clip == nullptr)
        return out;
    const auto& points = song.automations[(std::size_t) a].points;
    for (std::size_t i = 1; i < points.size(); ++i)
        out.push_back ({ clip->start - clip->offset + points[i].tick, tempoFromValue (points[i].value), points[i - 1].mode != flcurve::hold });
    return out;
}

// The tempo before the first marker (the start point), or the song's tempo without markers.
inline double startTempo (const Song& song)
{
    const auto a = automationIndex (song);
    if (a < 0 || song.automations[(std::size_t) a].points.empty())
        return song.bpm;
    return tempoFromValue (song.automations[(std::size_t) a].points.front().value);
}

namespace detail
{
inline double clampTempo (double bpm) noexcept { return std::clamp (bpm, minTempo, maxTempo); }

// The clip reaches one bar past the last marker (the tempo then holds anyway; the bar keeps the last tag on the clip).
inline void fitClip (Song& song, int a)
{
    auto* clip = clipOf (song, a);
    if (clip == nullptr)
        return;
    const auto& points = song.automations[(std::size_t) a].points;
    const auto last = points.empty() ? 0 : points.back().tick;
    clip->offset = 0;
    clip->length = std::max<std::int64_t> (song.barTicks(), last + song.barTicks());
}

// the segment that ends at point `i` (≥ 1): Hold, or a straight ramp
inline void setSegment (std::vector<AutomationPoint>& points, std::size_t i, bool glide) noexcept
{
    if (i == 0 || i >= points.size())
        return;
    points[i - 1].mode = glide ? (std::uint8_t) flcurve::singleCurve : (std::uint8_t) flcurve::hold;
    points[i - 1].tension = 0.0f;
}

inline void sortPoints (std::vector<AutomationPoint>& points)
{
    std::stable_sort (points.begin() + (points.empty() ? 0 : 1), points.end(),
                      [] (const AutomationPoint& x, const AutomationPoint& y) { return x.tick < y.tick; });
}
} // namespace detail

// The markers' automation and clip, made when missing: a clip at the song's start on the first free Playlist track,
// starting at the song's tempo. Returns the automation's index.
inline int ensure (Song& song)
{
    if (const auto a = automationIndex (song); a >= 0)
    {
        if (clipOf (song, a) == nullptr) // (its clip was deleted: put it back at the start)
        {
            Clip c;
            c.kind = ClipKind::automation;
            c.automation = a;
            c.track = std::max (0, trackmodes::firstFreeTrack (song));
            song.playlist.push_back (c);
            detail::fitClip (song, a);
        }
        return a;
    }
    Automation a;
    a.name = "Tempo markers";
    a.target = { AutomationTargetKind::tempo, 0, 0, 0 };
    a.tempoMarkers = true;
    AutomationPoint start;
    start.tick = 0;
    start.value = valueFromTempo (detail::clampTempo (song.bpm));
    start.mode = flcurve::hold;
    a.points.push_back (start);
    song.automations.push_back (a);
    const auto index = (int) song.automations.size() - 1;
    Clip c;
    c.kind = ClipKind::automation;
    c.automation = index;
    c.track = std::max (0, trackmodes::firstFreeTrack (song));
    c.start = 0;
    song.playlist.push_back (c);
    detail::fitClip (song, index);
    return index;
}

// A tempo change at `tick` (a marker already there takes the new tempo). Returns the marker's index in list().
inline int add (Song& song, std::int64_t tick, double bpm, bool glide = false)
{
    const auto a = ensure (song);
    auto& points = song.automations[(std::size_t) a].points;
    const auto* clip = clipOf (song, a);
    const auto at = std::max<std::int64_t> (0, tick - (clip != nullptr ? clip->start - clip->offset : 0));
    const auto value = valueFromTempo (detail::clampTempo (bpm));
    if (at == 0) // on the song's start: the start tempo
    {
        points.front().value = value;
        return -1;
    }
    auto it = std::find_if (points.begin() + 1, points.end(), [at] (const AutomationPoint& p) { return p.tick == at; });
    if (it == points.end())
    {
        AutomationPoint p;
        p.tick = at;
        p.value = value;
        p.mode = flcurve::hold;
        points.push_back (p);
        detail::sortPoints (points);
        it = std::find_if (points.begin() + 1, points.end(), [at] (const AutomationPoint& q) { return q.tick == at; });
    }
    it->value = value;
    const auto i = (std::size_t) (it - points.begin());
    detail::setSegment (points, i, glide);
    // the segment after it keeps its own mode; a new last point holds
    if (i + 1 == points.size())
        points[i].mode = flcurve::hold;
    detail::fitClip (song, a);
    return (int) i - 1;
}

inline bool valid (const Song& song, int marker) noexcept
{
    const auto a = automationIndex (song);
    return a >= 0 && marker >= 0 && marker + 1 < (int) song.automations[(std::size_t) a].points.size();
}

inline void setTempo (Song& song, int marker, double bpm)
{
    if (! valid (song, marker))
        return;
    song.automations[(std::size_t) automationIndex (song)].points[(std::size_t) marker + 1].value = valueFromTempo (detail::clampTempo (bpm));
}

inline void setStartTempo (Song& song, double bpm)
{
    if (const auto a = automationIndex (song); a >= 0 && ! song.automations[(std::size_t) a].points.empty())
        song.automations[(std::size_t) a].points.front().value = valueFromTempo (detail::clampTempo (bpm));
}

inline void setGlide (Song& song, int marker, bool glide)
{
    if (! valid (song, marker))
        return;
    detail::setSegment (song.automations[(std::size_t) automationIndex (song)].points, (std::size_t) marker + 1, glide);
}

// Moves a marker to `tick` (after the song's start; onto another marker: it takes that one's place). Returns its new
// index in list().
inline int move (Song& song, int marker, std::int64_t tick)
{
    if (! valid (song, marker))
        return marker;
    const auto a = automationIndex (song);
    auto& points = song.automations[(std::size_t) a].points;
    const auto* clip = clipOf (song, a);
    const auto at = std::max<std::int64_t> (1, tick - (clip != nullptr ? clip->start - clip->offset : 0));
    const auto moved = points[(std::size_t) marker + 1];
    const auto glide = points[(std::size_t) marker].mode != flcurve::hold;
    const auto nextGlide = (std::size_t) marker + 2 < points.size() && points[(std::size_t) marker + 1].mode != flcurve::hold;
    points.erase (points.begin() + marker + 1);
    // (the segment that ended at it now ends at the next marker, with that marker's own glide)
    if ((std::size_t) marker + 1 < points.size())
        detail::setSegment (points, (std::size_t) marker + 1, nextGlide);
    points.erase (std::remove_if (points.begin() + 1, points.end(), [at] (const AutomationPoint& p) { return p.tick == at; }), points.end());
    auto p = moved;
    p.tick = at;
    points.push_back (p);
    detail::sortPoints (points);
    const auto it = std::find_if (points.begin() + 1, points.end(), [at] (const AutomationPoint& q) { return q.tick == at; });
    const auto i = (std::size_t) (it - points.begin());
    detail::setSegment (points, i, glide);
    if (i + 1 == points.size())
        points[i].mode = flcurve::hold;
    detail::fitClip (song, a);
    return (int) i - 1;
}

// Removes a marker; the last one takes the automation and its clip away (the song's tempo is then the start tempo).
inline void remove (Song& song, int marker)
{
    if (! valid (song, marker))
        return;
    const auto a = automationIndex (song);
    auto& points = song.automations[(std::size_t) a].points;
    const auto nextGlide = (std::size_t) marker + 2 < points.size() && points[(std::size_t) marker + 1].mode != flcurve::hold;
    points.erase (points.begin() + marker + 1);
    if ((std::size_t) marker + 1 < points.size()) // (the next marker keeps its own glide)
        detail::setSegment (points, (std::size_t) marker + 1, nextGlide);
    if (points.size() <= 1)
    {
        song.bpm = tempoFromValue (points.empty() ? valueFromTempo (song.bpm) : points.front().value);
        song.removeAutomation (a);
        return;
    }
    points.back().mode = flcurve::hold;
    detail::fitClip (song, a);
}

// The marker whose tag is nearest `tick` within `slack` ticks, or −1.
inline int near (const Song& song, std::int64_t tick, std::int64_t slack)
{
    const auto marks = list (song);
    int best = -1;
    std::int64_t distance = slack + 1;
    for (int i = 0; i < (int) marks.size(); ++i)
        if (const auto d = std::llabs (marks[(std::size_t) i].tick - tick); d < distance)
        {
            distance = d;
            best = i;
        }
    return best;
}

// "128 BPM", "92.5 BPM" (the tag's text: at most two decimals, no trailing zeros)
inline std::string tempoText (double bpm)
{
    char text[32];
    std::snprintf (text, sizeof (text), "%.2f", bpm);
    std::string s = text;
    while (! s.empty() && s.back() == '0')
        s.pop_back();
    if (! s.empty() && s.back() == '.')
        s.pop_back();
    return s + " BPM";
}
} // namespace lynx::tempomarkers

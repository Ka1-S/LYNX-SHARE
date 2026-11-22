#pragma once

// Range edits on chosen tracks (round 190pa, research190oe P14; REAPER's razor edits, Live's time selection over some
// tracks). A LYNX extra: a time range that covers only some Playlist tracks (or one automation lane of a track) and the
// commands on just that part of the arrangement: cut, copy, paste, delete, duplicate, split at its edges, move, and the
// plan of a render to audio (core/QuickRender.h). FL's own time selection (Song::loopStart / loopEnd, every track) and
// its commands stay as they are.
//
// What a range holds on its tracks: the parts of their clips inside it (pattern, audio and automation clips, cut at the
// edges as Live's and REAPER's razor edits do) and the curve of each automation lane of those tracks (core/AutoLanes.h,
// the lanes under a track in the three layouts). Nothing is rippled: the other tracks stay exactly as they were, and the
// range's tracks keep their timing after it (REAPER's default, ripple off).
//
// Plain edits on a Song, each run inside AudioEngine::editSong (one undo step). JUCE-free and header-only; tested in
// tests/AutoModes190paTests.inc.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/ArrangeOps.h"
#include "core/Song.h"

namespace lynx::rangeedit
{
using Range = TrackRange; // (core/Song.h: Song::trackRange)

// A range made from two corners (the tracks between and including them; the ticks in either order).
inline Range fromCorners (std::int64_t tickA, std::int64_t tickB, int trackA, int trackB, int laneAutomation = -1)
{
    Range r;
    r.from = std::max<std::int64_t> (0, std::min (tickA, tickB));
    r.to = std::max<std::int64_t> (0, std::max (tickA, tickB));
    for (int t = std::max (0, std::min (trackA, trackB)); t <= std::max (trackA, trackB); ++t)
        r.tracks.push_back (t);
    r.laneAutomation = trackA == trackB ? laneAutomation : -1;
    return r;
}

// The automations whose lanes the range holds: every lane of its tracks, or the one lane.
inline std::vector<int> laneAutomations (const Song& song, const Range& r)
{
    std::vector<int> out;
    if (r.laneAutomation >= 0)
    {
        if (r.laneAutomation < (int) song.automations.size())
            out.push_back (r.laneAutomation);
        return out;
    }
    for (const auto t : r.tracks)
        for (const auto a : arrange::laneAutomations (song, t))
            if (std::find (out.begin(), out.end(), a) == out.end())
                out.push_back (a);
    return out;
}

// The lane automation's owner track (the first track whose lanes hold it), -1: none.
inline int laneOwner (const Song& song, int automation)
{
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
        for (const auto& lane : song.playlistTracks[(std::size_t) t].lanes)
            if (lane.automation == automation)
                return t;
    return -1;
}

// What a range copy holds; clip starts count from the range's start, tracks from its top track.
struct RangeClip
{
    std::int64_t length = 0;
    int trackCount = 0;
    std::vector<Clip> clips;
    struct Lane
    {
        int automation = -1;
        int track = 0;                       // from the top track
        std::vector<AutomationPoint> points; // from the range's start, a point on each edge
    };
    std::vector<Lane> lanes;
    bool empty() const noexcept { return length <= 0 || (clips.empty() && lanes.empty()); }
};

inline bool inRange (const Range& r, const Clip& c) noexcept
{
    return r.laneAutomation < 0 && r.covers (c.track) && c.length > 0 && c.start < r.to && c.end() > r.from;
}

inline RangeClip copyRange (const Song& song, const Range& r)
{
    RangeClip out;
    if (r.empty())
        return out;
    out.length = r.to - r.from;
    out.trackCount = r.tracks.back() - r.tracks.front() + 1;
    for (const auto& c : song.playlist)
        if (inRange (r, c))
            if (auto x = arrange::cropped (c, r.from, r.to))
            {
                x->start -= r.from;
                x->track -= r.top();
                x->uid = 0;
                out.clips.push_back (*x);
            }
    for (const auto a : laneAutomations (song, r))
    {
        const auto& points = song.automations[(std::size_t) a].points;
        if (points.empty())
            continue;
        if (auto piece = arrange::detail::piece (points, r.from, r.to); ! piece.empty())
            out.lanes.push_back ({ a, std::max (0, laneOwner (song, a) - r.top()), std::move (piece) });
    }
    return out;
}

namespace detail
{
// [from, to) of one curve is cleared: the points inside go, a point on each edge keeps the curve outside as it was
// (inside, a straight line joins the two edge values)
inline bool clearCurve (std::vector<AutomationPoint>& p, std::int64_t from, std::int64_t to)
{
    if (p.empty() || to <= from)
        return false;
    const auto inside = std::any_of (p.begin(), p.end(), [=] (const AutomationPoint& x) { return x.tick > from && x.tick < to; });
    if (! inside)
        return false;
    const auto left = automationCurveAt (p, (double) from), right = automationCurveAt (p, (double) to);
    const auto hadLeft = arrange::detail::hasPointAt (p, from), hadRight = arrange::detail::hasPointAt (p, to);
    p.erase (std::remove_if (p.begin(), p.end(), [=] (const AutomationPoint& x) { return x.tick > from && x.tick < to; }), p.end());
    if (! hadLeft)
        p.push_back ({ from, left, 0.0f });
    if (! hadRight)
        p.push_back ({ to, right, 0.0f });
    arrange::detail::sortPoints (p);
    return true;
}

inline bool trackExists (const Song& song, int track) noexcept
{
    return track >= 0 && track < std::max<int> (500, (int) song.playlistTracks.size()); // (FL's Playlist has 500 rows)
}
} // namespace detail

// Delete: what the range holds goes (clips cut at its edges, the parts inside removed; lane curves cleared inside).
// Returns how many clips and lanes changed.
inline int clearRange (Song& song, const Range& r)
{
    if (r.empty())
        return 0;
    int changed = 0;
    std::vector<Clip> kept;
    kept.reserve (song.playlist.size() + 4);
    for (const auto& c : song.playlist)
    {
        if (! inRange (r, c))
        {
            kept.push_back (c);
            continue;
        }
        ++changed;
        if (c.start < r.from)
            if (auto left = arrange::cropped (c, c.start, r.from))
            {
                left->crossfadeIn = c.crossfadeIn;
                left->uid = c.uid;
                kept.push_back (*left);
            }
        if (c.end() > r.to)
            if (auto right = arrange::cropped (c, r.to, c.end()))
            {
                right->crossfadeOut = c.crossfadeOut;
                right->uid = c.start < r.from ? 0 : c.uid;
                kept.push_back (*right);
            }
    }
    song.playlist = std::move (kept);
    for (const auto a : laneAutomations (song, r))
        if (detail::clearCurve (song.automations[(std::size_t) a].points, r.from, r.to))
            ++changed;
    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    return changed;
}

// Cut: a copy, then Delete.
inline RangeClip cutRange (Song& song, const Range& r)
{
    auto out = copyRange (song, r);
    clearRange (song, r);
    return out;
}

// The range a paste of `clip` at `at` onto `topTrack` covers.
inline Range pastedRange (const RangeClip& clip, std::int64_t at, int topTrack)
{
    Range r;
    r.from = std::max<std::int64_t> (0, at);
    r.to = r.from + clip.length;
    for (int t = 0; t < std::max (1, clip.trackCount); ++t)
        r.tracks.push_back (std::max (0, topTrack) + t);
    if (clip.clips.empty() && clip.lanes.size() == 1 && clip.trackCount <= 1)
        r.laneAutomation = clip.lanes.front().automation;
    return r;
}

// Paste: `clip` is laid at `at` with its top track on `topTrack`; what the place it lands on held goes first (as
// Delete), the other tracks are untouched. A lane's curve lands in the same lane when the target track holds it (a
// parameter's lane belongs to its track): `skippedLanes` counts the lanes that had no place. Returns the covered range.
inline Range pasteRange (Song& song, std::int64_t at, int topTrack, const RangeClip& clip, int* skippedLanes = nullptr)
{
    auto target = pastedRange (clip, at, topTrack);
    if (skippedLanes != nullptr)
        *skippedLanes = 0;
    if (clip.empty())
        return target;
    // the lanes that land: the same automation on the target track
    std::vector<const RangeClip::Lane*> landing;
    for (const auto& lane : clip.lanes)
    {
        const auto track = target.top() + lane.track;
        const auto ok = lane.automation >= 0 && lane.automation < (int) song.automations.size() && laneOwner (song, lane.automation) == track;
        if (ok)
            landing.push_back (&lane);
        else if (skippedLanes != nullptr)
            ++*skippedLanes;
    }
    // the place is cleared: the clips of the target tracks, and the lanes that receive a curve
    {
        auto clipsOnly = target;
        if (clipsOnly.laneAutomation < 0)
        {
            const auto lanesBefore = laneAutomations (song, clipsOnly);
            std::vector<std::vector<AutomationPoint>> saved;
            for (const auto a : lanesBefore)
                saved.push_back (song.automations[(std::size_t) a].points);
            clearRange (song, clipsOnly);
            for (std::size_t i = 0; i < lanesBefore.size(); ++i) // (only lanes that get a curve are replaced)
                song.automations[(std::size_t) lanesBefore[i]].points = saved[i];
        }
    }
    for (auto c : clip.clips)
    {
        const auto valid = c.kind == ClipKind::pattern ? c.pattern >= 0 && c.pattern < (int) song.patterns.size()
                           : c.kind == ClipKind::audio ? c.channel >= 0 && c.channel < (int) song.channels.size()
                                                       : c.automation >= 0 && c.automation < (int) song.automations.size();
        c.track += target.top();
        if (! valid || ! detail::trackExists (song, c.track))
            continue;
        c.start += target.from;
        c.uid = 0;
        song.playlist.push_back (c);
    }
    for (const auto* lane : landing)
        arrange::detail::lay (song.automations[(std::size_t) lane->automation].points, target.from, lane->points);
    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    return target;
}

// Duplicate: the range is repeated right after itself on its tracks (what lay there is replaced, nothing moves).
// Returns the new range (the copy), which a repeated Duplicate continues from.
inline Range duplicateRange (Song& song, const Range& r)
{
    if (r.empty())
        return r;
    const auto copy = copyRange (song, r);
    auto out = pasteRange (song, r.to, r.top(), copy);
    out.tracks = r.tracks; // (a range over tracks 1 and 3 repeats over 1 and 3)
    out.laneAutomation = r.laneAutomation;
    return out;
}

// Split: the clips of the range's tracks that cross its edges are cut there. Returns how many cuts were made.
inline int splitAtRange (Song& song, const Range& r)
{
    if (r.empty() || r.laneAutomation >= 0)
        return 0;
    int cuts = 0;
    for (const auto edge : { r.from, r.to })
        for (std::size_t i = 0, n = song.playlist.size(); i < n; ++i)
        {
            auto& c = song.playlist[i];
            if (! r.covers (c.track) || c.length <= 0 || c.start >= edge || c.end() <= edge)
                continue;
            auto right = splitClip (c, edge);
            right.uid = 0;
            song.playlist.push_back (right);
            ++cuts;
        }
    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    return cuts;
}

// Move (a drag of the range): its content goes `deltaTicks` later and `deltaTracks` lower; the clips are cut at its
// edges first. A lane's curve moves along its own lane only (a lane belongs to its track's parameter): moved to other
// tracks, the lanes stay where they are. Returns the range where the content now lies.
inline Range moveRange (Song& song, const Range& r, std::int64_t deltaTicks, int deltaTracks)
{
    if (r.empty() || (deltaTicks == 0 && deltaTracks == 0))
        return r;
    deltaTicks = std::max (deltaTicks, -r.from);
    deltaTracks = std::max (deltaTracks, -r.top());
    auto copy = copyRange (song, r);
    if (deltaTracks != 0) // (the lanes stay)
    {
        copy.lanes.clear();
        auto clipsOnly = r;
        const auto lanes = laneAutomations (song, r);
        std::vector<std::vector<AutomationPoint>> saved;
        for (const auto a : lanes)
            saved.push_back (song.automations[(std::size_t) a].points);
        clearRange (song, clipsOnly);
        for (std::size_t i = 0; i < lanes.size(); ++i)
            song.automations[(std::size_t) lanes[i]].points = saved[i];
    }
    else
        clearRange (song, r);
    auto out = pasteRange (song, r.from + deltaTicks, r.top() + deltaTracks, copy);
    out.tracks.clear();
    for (const auto t : r.tracks)
        out.tracks.push_back (t + deltaTracks);
    out.laneAutomation = deltaTracks == 0 ? r.laneAutomation : -1;
    return out;
}

// What a range holds that a render can play: the indices of its tracks' clips crossing it (pattern and audio; the
// render plays them through the range, cut at its edges: core/QuickRender.h planRange).
inline std::vector<int> renderClips (const Song& song, const Range& r)
{
    std::vector<int> out;
    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (inRange (r, c) && c.kind != ClipKind::automation && ! c.muted && ! song.isTrackMuted (c.track))
            out.push_back (i);
    }
    return out;
}

// --- The Playlist's switch ----------------------------------------------------------------------------------------
// Ctrl+Alt+drag in the Playlist grid draws a range (FL 2026's Playlist gives Ctrl+Alt+drag no action of its own: Ctrl+
// drag selects, Alt only turns snap off; checked against FL's online manual's Playlist page, round 190pa). The Pattern
// layout offers it as a LYNX extra (Settings › LYNX extras, off by default); the Clip-launch layout's Arrangement always.
inline bool& patternLayoutOn()
{
    static bool on = false;
    return on;
}
} // namespace lynx::rangeedit

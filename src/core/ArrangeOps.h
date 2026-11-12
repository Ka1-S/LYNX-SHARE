#pragma once

// Arrangement commands of round 190cg (Ableton Live's Arrangement, the rows of rea190bk/COVERAGE-LIVE-2.md; evidence per
// command in /mnt/e/lynx-work/warp190cg/HANDOFF.md). They are additions: FL's workflow keeps its own commands.
//  - the loop switch: the Playlist's time selection is the loop (FL); Song::loopOff keeps the range and stops looping it;
//  - the time commands Cut / Paste / Duplicate Time, which carry the clips, the markers and the lane automation;
//  - Crop clips to the time selection;
//  - folding a track to a thin row;
//  - "Automation follows clips": the lane automation under a clip moves or copies with it;
//  - a track group as a bus: the members' inserts play through the header's insert.
// Plain edits on a Song, each run inside AudioEngine::editSong (one undo step). JUCE-free and header-only.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/MixerOps.h"
#include "core/NoteEdit.h"
#include "core/Song.h"

namespace lynx::arrange
{
// --- The loop switch ------------------------------------------------------------------------------------------------

// The loop's length times `factor` (2 = double, 0.5 = halve), from its start; never under `minimum` ticks.
inline bool scaleLoop (Song& song, double factor, std::int64_t minimum = ppq / 4)
{
    if (! song.hasLoop() || ! (factor > 0.0))
        return false;
    const auto length = std::max<std::int64_t> (minimum, (std::int64_t) std::llround ((double) (song.loopEnd - song.loopStart) * factor));
    if (length == song.loopEnd - song.loopStart)
        return false;
    song.loopEnd = song.loopStart + length;
    return true;
}

// The loop given by its start and length (the Control Bar's fields); a length of 0 or less removes it.
inline void setLoop (Song& song, std::int64_t start, std::int64_t length)
{
    song.loopStart = std::max<std::int64_t> (0, start);
    song.loopEnd = length > 0 ? song.loopStart + length : song.loopStart;
    if (song.loopEnd <= song.loopStart)
        song.loopStart = song.loopEnd = 0;
}

// --- Lane automation in time ----------------------------------------------------------------------------------------

// The automations that sit in a lane of `track` (-1: of any track), each once. A lane's points count from the song start.
inline std::vector<int> laneAutomations (const Song& song, int track = -1)
{
    std::vector<int> out;
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
        if (track < 0 || t == track)
            for (const auto& lane : song.playlistTracks[(std::size_t) t].lanes)
                if (lane.automation >= 0 && lane.automation < (int) song.automations.size()
                    && std::find (out.begin(), out.end(), lane.automation) == out.end())
                    out.push_back (lane.automation);
    return out;
}

namespace detail
{
inline void sortPoints (std::vector<AutomationPoint>& p)
{
    std::stable_sort (p.begin(), p.end(), [] (const AutomationPoint& a, const AutomationPoint& b) { return a.tick < b.tick; });
}
inline bool hasPointAt (const std::vector<AutomationPoint>& p, std::int64_t tick)
{
    return std::any_of (p.begin(), p.end(), [tick] (const AutomationPoint& x) { return x.tick == tick; });
}
// the curve between `from` and `to` as points counted from `from`, with a point on each edge
inline std::vector<AutomationPoint> piece (const std::vector<AutomationPoint>& p, std::int64_t from, std::int64_t to)
{
    std::vector<AutomationPoint> out;
    if (p.empty() || to <= from)
        return out;
    out.push_back ({ 0, automationCurveAt (p, (double) from), 0.0f });
    for (const auto& x : p)
        if (x.tick > from && x.tick < to)
            out.push_back ({ x.tick - from, x.value, x.tension });
        else if (x.tick == from)
            out.front() = { 0, x.value, x.tension };
    out.push_back ({ to - from, automationCurveAt (p, (double) to), 0.0f });
    return out;
}
// `piece` laid over [at, at + its length]: what was there goes, the curve outside keeps its values up to the edges
inline void lay (std::vector<AutomationPoint>& p, std::int64_t at, const std::vector<AutomationPoint>& piece)
{
    if (piece.size() < 2)
        return;
    const auto end = at + piece.back().tick;
    const auto before = p.empty() ? piece.front().value : automationCurveAt (p, (double) at);
    const auto after = p.empty() ? piece.back().value : automationCurveAt (p, (double) end);
    const auto hadBefore = std::any_of (p.begin(), p.end(), [at] (const AutomationPoint& x) { return x.tick < at; });
    const auto hadAfter = std::any_of (p.begin(), p.end(), [end] (const AutomationPoint& x) { return x.tick > end; });
    p.erase (std::remove_if (p.begin(), p.end(), [at, end] (const AutomationPoint& x) { return x.tick >= at - 1 && x.tick <= end + 1; }), p.end());
    if (hadBefore && at > 0)
        p.push_back ({ at - 1, before, 0.0f });
    for (const auto& x : piece)
        p.push_back ({ at + x.tick, x.value, x.tension });
    if (hadAfter)
        p.push_back ({ end + 1, after, 0.0f });
    sortPoints (p);
}
inline void clampFades (Clip& c)
{
    c.fadeIn = std::clamp<std::int64_t> (c.fadeIn, 0, c.length);
    c.fadeOut = std::clamp<std::int64_t> (c.fadeOut, 0, c.length - c.fadeIn);
}
} // namespace detail

// `length` ticks open at `at` in every lane: the points from there on move right, the value at `at` holds across the gap.
inline void insertLaneTime (Song& song, std::int64_t at, std::int64_t length)
{
    if (length <= 0)
        return;
    for (const auto a : laneAutomations (song))
    {
        auto& p = song.automations[(std::size_t) a].points;
        const auto crossing = std::any_of (p.begin(), p.end(), [at] (const AutomationPoint& x) { return x.tick < at; })
                              && std::any_of (p.begin(), p.end(), [at] (const AutomationPoint& x) { return x.tick >= at; });
        const auto value = p.empty() ? 0.0f : automationCurveAt (p, (double) at);
        const auto had = detail::hasPointAt (p, at);
        for (auto& x : p)
            if (x.tick >= at)
                x.tick += length;
        if (crossing)
        {
            p.push_back ({ at, value, 0.0f });
            if (! had)
                p.push_back ({ at + length, value, 0.0f });
            detail::sortPoints (p);
        }
    }
}

// [from, to) leaves every lane: the points inside go, the later ones move left, the curve keeps its value at each edge.
inline void deleteLaneTime (Song& song, std::int64_t from, std::int64_t to)
{
    if (to <= from)
        return;
    for (const auto a : laneAutomations (song))
    {
        auto& p = song.automations[(std::size_t) a].points;
        if (p.empty())
            continue;
        const auto inside = std::any_of (p.begin(), p.end(), [=] (const AutomationPoint& x) { return x.tick >= from && x.tick < to; });
        const auto left = automationCurveAt (p, (double) from), right = automationCurveAt (p, (double) to);
        const auto hadBefore = std::any_of (p.begin(), p.end(), [from] (const AutomationPoint& x) { return x.tick < from; });
        const auto hadAfter = std::any_of (p.begin(), p.end(), [to] (const AutomationPoint& x) { return x.tick >= to; });
        p.erase (std::remove_if (p.begin(), p.end(), [=] (const AutomationPoint& x) { return x.tick >= from && x.tick < to; }), p.end());
        for (auto& x : p)
            if (x.tick >= to)
                x.tick -= to - from;
        if (inside || (hadBefore && hadAfter))
        {
            if (hadBefore && from > 0 && ! detail::hasPointAt (p, from - 1) && inside)
                p.push_back ({ from - 1, left, 0.0f });
            if (! detail::hasPointAt (p, from) && (hadAfter || inside))
                p.push_back ({ from, right, 0.0f });
            detail::sortPoints (p);
        }
    }
}

// --- Time commands ----------------------------------------------------------------------------------------------------

// The part of `c` inside [from, to), or nothing. A cut edge loses its fade.
inline std::optional<Clip> cropped (const Clip& c, std::int64_t from, std::int64_t to)
{
    if (c.length <= 0 || c.end() <= from || c.start >= to || to <= from)
        return std::nullopt;
    auto x = c;
    if (x.start < from)
    {
        x = splitClip (x, from);
        x.fadeIn = 0;
    }
    if (x.end() > to)
    {
        splitClip (x, to);
        x.fadeOut = 0;
    }
    x.crossfadeIn = x.crossfadeOut = 0;
    detail::clampFades (x);
    return x;
}

// A stretch of the arrangement: what Cut Time and Copy Time keep and Paste Time inserts.
struct TimeClip
{
    std::vector<Clip> clips;     // starts count from the range's start
    std::vector<Marker> markers; // ticks likewise
    struct Lane
    {
        int automation = -1;
        std::vector<AutomationPoint> points; // from the range's start, with a point on each edge
    };
    std::vector<Lane> lanes;
    std::int64_t length = 0;
    bool empty() const noexcept { return length <= 0; }
};

inline TimeClip copyTime (const Song& song, std::int64_t from, std::int64_t to)
{
    TimeClip out;
    if (to <= from)
        return out;
    out.length = to - from;
    for (const auto& c : song.playlist)
        if (auto x = cropped (c, from, to))
        {
            x->start -= from;
            x->uid = 0;
            out.clips.push_back (*x);
        }
    for (const auto& m : song.markers())
        if (m.tick >= from && m.tick < to)
        {
            out.markers.push_back (m);
            out.markers.back().tick -= from;
        }
    for (const auto a : laneAutomations (song))
        if (auto points = detail::piece (song.automations[(std::size_t) a].points, from, to); ! points.empty())
            out.lanes.push_back ({ a, std::move (points) });
    return out;
}

// Paste Time: `time` is inserted at `at`; the clips crossing `at` are cut there and everything later moves right.
inline void pasteTime (Song& song, std::int64_t at, const TimeClip& time)
{
    if (time.empty() || at < 0)
        return;
    noteedit::sliceInsertTime (song.playlist, song.markers(), at, time.length);
    insertLaneTime (song, at, time.length);
    for (auto c : time.clips)
    {
        const auto valid = c.kind == ClipKind::pattern   ? c.pattern >= 0 && c.pattern < (int) song.patterns.size()
                           : c.kind == ClipKind::audio   ? c.channel >= 0 && c.channel < (int) song.channels.size()
                                                         : c.automation >= 0 && c.automation < (int) song.automations.size();
        if (! valid)
            continue;
        c.start += at;
        song.playlist.push_back (c);
    }
    for (auto m : time.markers)
    {
        m.tick += at;
        song.addMarker (m);
    }
    for (const auto& lane : time.lanes)
        if (lane.automation >= 0 && lane.automation < (int) song.automations.size())
            detail::lay (song.automations[(std::size_t) lane.automation].points, at, lane.points);
    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
}

// Delete Time with the lanes: [from, to) leaves the arrangement and the rest closes up.
inline void deleteTime (Song& song, std::int64_t from, std::int64_t to)
{
    if (to <= from)
        return;
    noteedit::deleteTime (song.playlist, song.markers(), from, to);
    deleteLaneTime (song, from, to);
}

// Cut Time: Copy Time, then Delete Time.
inline TimeClip cutTime (Song& song, std::int64_t from, std::int64_t to)
{
    auto out = copyTime (song, from, to);
    deleteTime (song, from, to);
    return out;
}

// Duplicate Time: the range is repeated right after itself, pushing what follows. Its locators (plain markers) are
// repeated too (round 190cy; Live 12.4.16 live check 190cw shot 03b: Duplicate Time over a range holding locator "L12"
// gave a second "L12" one range later); signature changes and FL's marker actions inside it are not.
inline void duplicateTime (Song& song, std::int64_t from, std::int64_t to)
{
    auto time = copyTime (song, from, to);
    time.markers.erase (std::remove_if (time.markers.begin(), time.markers.end(),
                                        [] (const Marker& m) { return m.isSignature() || m.action != Marker::none; }),
                        time.markers.end());
    pasteTime (song, to, time);
}

// Crop: each of `clips` (Playlist indices) that reaches into [from, to) keeps only its part inside it. Returns how many
// changed. Clips outside the range stay as they are.
inline int cropClips (Song& song, const std::vector<int>& clips, std::int64_t from, std::int64_t to)
{
    int changed = 0;
    for (const auto i : clips)
    {
        if (i < 0 || i >= (int) song.playlist.size())
            continue;
        auto& c = song.playlist[(std::size_t) i];
        if (const auto x = cropped (c, from, to); x && (x->start != c.start || x->length != c.length))
        {
            c = *x;
            ++changed;
        }
    }
    return changed;
}

// --- Folded tracks ----------------------------------------------------------------------------------------------------

inline bool isFolded (const Song& song, int track) noexcept
{
    return track >= 0 && track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].folded;
}

// Folds or unfolds `tracks` (rows are added up to the highest one). Returns how many changed.
inline int setFolded (Song& song, const std::vector<int>& tracks, bool folded)
{
    int changed = 0;
    for (const auto t : tracks)
    {
        if (t < 0)
            continue;
        if (t >= (int) song.playlistTracks.size())
        {
            if (! folded)
                continue;
            song.playlistTracks.resize ((std::size_t) t + 1);
        }
        changed += std::exchange (song.playlistTracks[(std::size_t) t].folded, folded) != folded;
    }
    return changed;
}

// Every track that has a header entry, folded or unfolded at once.
inline int setAllFolded (Song& song, bool folded)
{
    int changed = 0;
    for (auto& t : song.playlistTracks)
        changed += std::exchange (t.folded, folded) != folded;
    return changed;
}

// --- Automation follows clips -----------------------------------------------------------------------------------------

// A clip of `track` that spanned [from, to) moved by `delta` ticks (or was copied there): the lane automation of the
// track under it goes with it. The place it lands on is replaced. After a move the place it left holds a straight line
// between the values at its edges. Returns how many lanes changed.
inline int carryLaneAutomation (Song& song, int track, std::int64_t from, std::int64_t to, std::int64_t delta, bool copy)
{
    if (to <= from || delta == 0 || from + delta < 0)
        return 0;
    int changed = 0;
    for (const auto a : laneAutomations (song, track))
    {
        auto& p = song.automations[(std::size_t) a].points;
        if (p.empty())
            continue;
        const auto piece = detail::piece (p, from, to);
        if (! copy)
        {
            const std::vector<AutomationPoint> flat { { 0, piece.front().value, 0.0f }, { to - from, piece.back().value, 0.0f } };
            p.erase (std::remove_if (p.begin(), p.end(), [=] (const AutomationPoint& x) { return x.tick >= from && x.tick <= to; }), p.end());
            for (const auto& x : flat)
                p.push_back ({ from + x.tick, x.value, x.tension });
            detail::sortPoints (p);
        }
        detail::lay (p, from + delta, piece);
        ++changed;
    }
    return changed;
}

// --- A track group as a bus -------------------------------------------------------------------------------------------

// The header of the group `track` belongs to, and its last member; {-1, -1} when the track is in no group.
struct Group
{
    int header = -1, last = -1;
    bool valid() const noexcept { return header >= 0 && last > header; }
};
inline Group groupOf (const Song& song, int track)
{
    const auto& tracks = song.playlistTracks;
    if (track < 0 || track >= (int) tracks.size())
        return {};
    auto header = track;
    while (header > 0 && tracks[(std::size_t) header].grouped)
        --header;
    auto last = header;
    while (last + 1 < (int) tracks.size() && tracks[(std::size_t) last + 1].grouped)
        ++last;
    return last > header ? Group { header, last } : Group {};
}

// The insert a Playlist track plays through: an audio track's mixer track, an instrument track's channel's; -1 = none.
inline int insertOf (const Song& song, int track)
{
    if (track < 0 || track >= (int) song.playlistTracks.size())
        return -1;
    const auto& t = song.playlistTracks[(std::size_t) track];
    int insert = -1;
    if (t.mode == PlaylistTrackMode::audio)
        insert = t.mixerTrack;
    else if (t.mode == PlaylistTrackMode::instrument)
        insert = t.channel >= 0 && t.channel < (int) song.channels.size() ? song.channels[(std::size_t) t.channel].mixerTrack : t.mixerTrack;
    return insert > 0 && insert < (int) song.mixer.size() ? insert : -1;
}

// The bus of the group `track` belongs to: the header's insert, when every member insert plays only into it. -1 = none.
inline int groupBus (const Song& song, int track)
{
    const auto g = groupOf (song, track);
    if (! g.valid())
        return -1;
    const auto bus = insertOf (song, g.header);
    if (bus <= 0)
        return -1;
    int members = 0;
    for (int t = g.header + 1; t <= g.last; ++t)
        if (const auto i = insertOf (song, t); i > 0 && i != bus)
        {
            const auto& m = song.mixer[(std::size_t) i];
            if (m.toMaster || m.sends.empty() || m.sends.front().target != bus)
                return -1;
            ++members;
        }
    return members > 0 ? bus : -1;
}

// Makes the group of `track` a bus (Live's group track): the header gets an insert of its own when it has none (a new
// one named after it, the header becoming an audio track of that insert, so its mute, solo, fader and effects are the
// group's), and each member's insert plays into it instead of the Master; its other sends stay. Members without an
// insert are left as they are. Returns the bus insert, or -1 when the group has no member insert to route (or no room).
inline int makeGroupBus (Song& song, int track)
{
    const auto g = groupOf (song, track);
    if (! g.valid())
        return -1;
    std::vector<int> members;
    for (int t = g.header + 1; t <= g.last; ++t)
        if (const auto i = insertOf (song, t); i > 0 && std::find (members.begin(), members.end(), i) == members.end())
            members.push_back (i);
    if (members.empty())
        return -1;
    auto bus = insertOf (song, g.header);
    if (bus > 0 && std::find (members.begin(), members.end(), bus) != members.end())
        return -1; // (the header shares an insert with a member: nothing to sum into)
    if (bus <= 0)
    {
        if (mixerops::roomForInserts (song) < 1)
            return -1;
        bus = mixerops::ensureInsert (song, std::max (1, (int) song.mixer.size()));
        auto& header = song.playlistTracks[(std::size_t) g.header];
        song.mixer[(std::size_t) bus].name = header.name.empty() ? std::string ("Group") : header.name;
        song.mixer[(std::size_t) bus].colour = header.colour;
        header.mode = PlaylistTrackMode::audio;
        header.mixerTrack = bus;
    }
    int routed = 0;
    for (const auto i : members)
    {
        if (! song.routes (i, bus) && ! song.canRoute (i, bus))
            continue; // (it would feed itself)
        song.setRoute (i, bus, true);
        auto& m = song.mixer[(std::size_t) i];
        m.toMaster = false;
        // the bus first: groupBus and the Mixer read a summing group from the first send
        const auto at = std::find_if (m.sends.begin(), m.sends.end(), [bus] (const MixerSend& s) { return s.target == bus; });
        if (at != m.sends.end())
            std::rotate (m.sends.begin(), at, at + 1);
        ++routed;
    }
    return routed > 0 ? bus : -1;
}

// A member leaves its group's bus (ungrouped, or the bus is given up): its insert plays into the Master again.
inline bool leaveGroupBus (Song& song, int insert, int bus)
{
    if (insert <= 0 || insert >= (int) song.mixer.size() || bus <= 0)
        return false;
    auto& m = song.mixer[(std::size_t) insert];
    const auto before = m.sends.size();
    m.sends.erase (std::remove_if (m.sends.begin(), m.sends.end(), [bus] (const MixerSend& s) { return s.target == bus; }), m.sends.end());
    if (m.sends.size() == before)
        return false;
    m.toMaster = true;
    return true;
}
} // namespace lynx::arrange

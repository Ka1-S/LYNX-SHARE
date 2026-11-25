#pragma once

// Issue #191 point 3 (round 190iu): automation as foldable child lanes under the instrument's track instead of tracks of
// their own. LYNX has had the lanes since round 177b (core/PlaylistOps.h autolanes: Playlist menu › Automation lanes, off
// in the Pattern layout, on in the Clip-launch layout as in Live's Arrangement; each track's lanes fold with its arrow;
// "Automate in lane" on a control; "Move clip into lane" / "Lane to automation clip" on one clip; the .flp export turns
// lanes into clips). What was missing for the issue: the automation clips a project already has (recorded envelopes,
// imported FL projects) stay on tracks of their own. `clipsToLanes` moves, in one undo step, every automation clip
// whose control belongs to a track — the instrument track of its channel, or the track linked to its mixer insert — into
// that track's lane; clips of controls no track owns stay where they are. Playlist menu › "Move automation clips into
// their tracks' lanes" (shown while lanes are on). No JUCE: tested in tests/Workflow190iuTests.inc.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "core/PlaylistOps.h"
#include "core/Song.h"

namespace lynx::laneview
{
// The track that owns `target`: one that already has a lane for it; the instrument track of its channel; a track
// linked to its mixer insert (a channel's own insert counts). -1: none (tempo, the Master, an unlinked insert...).
inline int ownerTrack (const Song& song, const AutomationTarget& target)
{
    const auto& tracks = song.playlistTracks;
    for (int t = 0; t < (int) tracks.size(); ++t)
        for (const auto& l : tracks[(std::size_t) t].lanes)
            if (l.automation >= 0 && l.automation < (int) song.automations.size() && song.automations[(std::size_t) l.automation].target == target)
                return t;
    int channel = -1, insert = -1;
    switch (target.kind)
    {
        case AutomationTargetKind::channelVolume:
        case AutomationTargetKind::channelPan:
        case AutomationTargetKind::channelParam:
        case AutomationTargetKind::channelAdjust:
            channel = target.index;
            if (channel >= 0 && channel < (int) song.channels.size())
                insert = song.channels[(std::size_t) channel].mixerTrack;
            break;
        case AutomationTargetKind::mixerVolume:
        case AutomationTargetKind::mixerPan:
        case AutomationTargetKind::effectParam:
        case AutomationTargetKind::effectMix:
        case AutomationTargetKind::mixerSend:
            insert = target.index;
            break;
        default:
            break;
    }
    if (channel >= 0)
        for (int t = 0; t < (int) tracks.size(); ++t)
            if (tracks[(std::size_t) t].mode == PlaylistTrackMode::instrument && tracks[(std::size_t) t].channel == channel)
                return t;
    if (insert > 0)
        for (int t = 0; t < (int) tracks.size(); ++t)
            if (tracks[(std::size_t) t].mode != PlaylistTrackMode::unassigned && tracks[(std::size_t) t].mixerTrack == insert)
                return t;
    return -1;
}

// Round 190qu (Discord 11-24, kainproctor: after "Move automation clips into their tracks' lanes" on a big FL import the
// automations "act weird or stop working completely"). What the first version (round 190iu) got wrong, each fixed here:
//  - between two clips the lane went back to the flat value it was made with; FL holds the value the last clip ended on
//    (the engine's rule, RackRenderer automationValueAt and Song::targetValueAt), and before the first clip the control
//    keeps its own value;
//  - an imported FL clip's MIN / MAX knobs (Automation::rerange) and the scale of its control (a mapped effect
//    parameter: one target link with a scale, round 190ov) were dropped: the lane played the raw points;
//  - muted clips, clips of muted tracks, muted automation clip channels, LFO clips, Performance-zone clips, clips its
//    steps start (autoNotes) and clips linked to several controls were moved too, so things that were silent started to
//    play (or the other way round);
//  - overlapping clips of one control (the one started last plays) were pasted over each other in list order;
//  - every lane was a NEW automation appended after the project's own: past the engine's 1024 automation slots
//    (RackState::maxAutomations, a big FL project) a lane never played at all. A lane now reuses the slot of one of its
//    clips' automations when nothing else uses it.
// The lane is built from what plays (playedCurve), so the song sounds the same; one undo step takes it back, and
// "Turn automation lanes back into clips" (lanesToClips) gives clips again.

// What an automation clip drives: the automation's target, or its one target link's (a clip linked to several controls is
// none of a lane's: AutomationTargetKind::none).
inline AutomationTarget drivenTarget (const Automation& a)
{
    if (a.links.empty())
        return a.target;
    if (a.links.size() == 1 && a.links.front().formula.empty())
        return a.links.front().target;
    return {};
}

// The value automation `a` puts on its target at clip time `t` (offset included): its curve, its MIN / MAX knobs and its
// link's scale, as the engine plays it (the LFO is not: clips with one are not moved).
inline float playedValue (const Automation& a, double t)
{
    auto v = a.valueAt (t);
    if (a.reranged())
        v = a.rerange (v);
    if (a.links.size() == 1)
        v = a.links.front().scaled (v);
    return v;
}

// Per automation: it is a lane of a track (its own clips, if any, don't play as clips: RackState's laneAutomations).
inline std::vector<char> laneFlags (const Song& song)
{
    std::vector<char> flags (song.automations.size(), 0);
    for (const auto& t : song.playlistTracks)
        for (const auto& l : t.lanes)
            if (l.automation >= 0 && l.automation < (int) flags.size())
                flags[(std::size_t) l.automation] = 1;
    return flags;
}

// Whether clip `c` of the current arrangement plays automation the engine drives from the Playlist now (unmuted clip,
// track and automation channel, not a live link, not a lane's, not launched in Performance mode). `lanes`: laneFlags.
inline bool clipPlays (const Song& song, const Clip& c, const std::vector<char>& lanes)
{
    if (c.kind != ClipKind::automation || c.gridSlot >= 0 || c.length <= 0 || c.automation < 0 || c.automation >= (int) song.automations.size() || c.muted
        || song.isTrackMuted (c.track) || song.inPerformanceZone (c) || ((std::size_t) c.automation < lanes.size() && lanes[(std::size_t) c.automation] != 0))
        return false;
    const auto& a = song.automations[(std::size_t) c.automation];
    return ! a.isLive() && ! a.muted && ! a.points.empty();
}

// Automation `a` is started by steps / notes on its Channel rack row (FL's automation clip row, round 190ow).
inline bool triggeredByNotes (const Song& song, int a)
{
    for (const auto& p : song.patterns)
        for (const auto& n : p.autoNotes)
            if (n.channel == a)
                return true;
    return false;
}

// Clip `index` can go into a lane: it plays, its automation is a plain one (no LFO, at most one link without a formula,
// not the ruler's tempo markers, not started by notes) and a track owns its control.
inline bool movable (const Song& song, int index, const std::vector<char>& lanes)
{
    if (index < 0 || index >= (int) song.playlist.size() || ! clipPlays (song, song.playlist[(std::size_t) index], lanes))
        return false;
    const auto a = song.playlist[(std::size_t) index].automation;
    const auto& automation = song.automations[(std::size_t) a];
    const auto target = drivenTarget (automation);
    return target.kind != AutomationTargetKind::none && target.kind != AutomationTargetKind::tempo && ! automation.lfo.on && ! automation.tempoMarkers
           && ! triggeredByNotes (song, a) && ownerTrack (song, target) >= 0;
}

// The automation clips of the current arrangement `clipsToLanes` would move (indices into Song::playlist).
inline std::vector<int> movableClips (const Song& song)
{
    std::vector<int> out;
    std::vector<char> triggered (song.automations.size(), 0);
    for (const auto& p : song.patterns)
        for (const auto& n : p.autoNotes)
            if (n.channel >= 0 && n.channel < (int) triggered.size())
                triggered[(std::size_t) n.channel] = 1;
    std::vector<int> owners (song.automations.size(), -2); // (per automation: its owner track, found once)
    const auto lanes = laneFlags (song);
    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (! clipPlays (song, c, lanes) || triggered[(std::size_t) c.automation] != 0)
            continue;
        const auto& automation = song.automations[(std::size_t) c.automation];
        const auto target = drivenTarget (automation);
        if (target.kind == AutomationTargetKind::none || target.kind == AutomationTargetKind::tempo || automation.lfo.on || automation.tempoMarkers)
            continue;
        auto& owner = owners[(std::size_t) c.automation];
        if (owner == -2)
            owner = ownerTrack (song, target);
        if (owner >= 0)
            out.push_back (i);
    }
    return out;
}

// The lane automation track `track` has for `target`, -1: none.
inline int laneOf (const Song& song, int track, const AutomationTarget& target)
{
    if (track < 0 || track >= (int) song.playlistTracks.size())
        return -1;
    for (const auto& l : song.playlistTracks[(std::size_t) track].lanes)
        if (l.automation >= 0 && l.automation < (int) song.automations.size() && song.automations[(std::size_t) l.automation].target == target)
            return l.automation;
    return -1;
}

// Douglas-Peucker on a run of straight points: the points a straight line between their neighbours gives within `eps`
// (0..1 of the control) go. The two ends always stay.
inline std::vector<AutomationPoint> simplify (const std::vector<AutomationPoint>& in, float eps = 1.0e-3f)
{
    if (in.size() <= 2)
        return in;
    std::vector<char> keep (in.size(), 0);
    keep.front() = keep.back() = 1;
    std::vector<std::pair<std::size_t, std::size_t>> stack { { 0, in.size() - 1 } };
    while (! stack.empty())
    {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b <= a + 1)
            continue;
        const auto span = (double) (in[b].tick - in[a].tick);
        auto worst = -1.0;
        std::size_t at = a;
        for (auto k = a + 1; k < b; ++k)
        {
            const auto x = span > 0.0 ? (double) (in[k].tick - in[a].tick) / span : 0.0;
            const auto line = (double) in[a].value + ((double) in[b].value - (double) in[a].value) * x;
            if (const auto d = std::abs ((double) in[k].value - line); d > worst)
            {
                worst = d;
                at = k;
            }
        }
        if (worst > (double) eps)
        {
            keep[at] = 1;
            stack.push_back ({ a, at });
            stack.push_back ({ at, b });
        }
    }
    std::vector<AutomationPoint> out;
    for (std::size_t k = 0; k < in.size(); ++k)
        if (keep[k] != 0)
            out.push_back (in[k]);
    return out;
}

// The curve (song ticks, the target's own 0..1) a lane needs to play what `target` plays now: clips `clips` (indices into
// Song::playlist; the clips of the target that play) decide by the engine's rule — inside clips the one started last
// (a tie: the lower automation, then the earlier clip), between them the value the clip that ended last ended on — and
// where no clip plays, `lane` (a lane the track has already, which plays there) or, before the first clip, the control's
// own value `own` (NaN: the first clip's start value). A clip's part is sampled as it plays (its curve modes, tension,
// MIN / MAX and its control's scale included: every 1/16 beat, each of its points and the tick before) and simplified
// back to the points a straight line needs (a straight clip keeps its own points), so a part cut out of a curved FL
// segment sounds as it did.
inline std::vector<AutomationPoint> playedCurve (const Song& song, const std::vector<int>& clips, const Automation* lane, float own)
{
    std::vector<AutomationPoint> out;
    if (clips.empty())
        return lane != nullptr ? lane->points : out;
    std::vector<std::int64_t> edges;
    for (const auto i : clips)
    {
        edges.push_back (song.playlist[(std::size_t) i].start);
        edges.push_back (song.playlist[(std::size_t) i].end());
    }
    std::sort (edges.begin(), edges.end());
    edges.erase (std::unique (edges.begin(), edges.end()), edges.end());

    const auto add = [&out] (std::int64_t tick, float value, float tension = 0.0f, std::uint8_t mode = 0) {
        value = std::clamp (value, 0.0f, 1.0f);
        if (! out.empty() && tick <= out.back().tick)
        {
            if (tick < out.back().tick)
                return;
            out.back() = { tick, value, tension, mode }; // (the later one at the same tick wins)
            return;
        }
        out.push_back ({ tick, value, tension, mode });
    };
    // the lane's own points in [from, to), with its values at both ends
    const auto addLane = [&] (std::int64_t from, std::int64_t to) {
        add (from, lane->valueAt ((double) from));
        for (const auto& p : lane->points)
            if (p.tick > from && p.tick < to)
                add (p.tick, p.value, p.tension, p.mode);
        if (to - 1 > from && to != std::numeric_limits<std::int64_t>::max())
            add (to - 1, lane->valueAt ((double) (to - 1)));
    };
    const auto startValue = [&song] (const Clip& c) { return playedValue (song.automations[(std::size_t) c.automation], (double) c.offset); };
    const auto endValue = [&song] (const Clip& c) {
        return playedValue (song.automations[(std::size_t) c.automation], (double) (c.length + c.offset));
    };

    // before the first clip
    const auto& first = [&]() -> const Clip& {
        const Clip* f = nullptr;
        for (const auto i : clips)
            if (f == nullptr || song.playlist[(std::size_t) i].start < f->start)
                f = &song.playlist[(std::size_t) i];
        return *f;
    }();
    if (lane != nullptr)
        addLane (0, edges.front());
    else if (edges.front() > 0)
    {
        const auto before = std::isnan (own) ? startValue (first) : own;
        add (0, before);
        if (edges.front() - 1 > 0)
            add (edges.front() - 1, before); // (held up to the first clip, not a ramp into it)
    }

    for (std::size_t e = 0; e < edges.size(); ++e)
    {
        const auto from = edges[e];
        const auto to = e + 1 < edges.size() ? edges[e + 1] : std::numeric_limits<std::int64_t>::max();
        const Clip* playing = nullptr;
        int playingIndex = -1;
        const Clip* ended = nullptr;
        for (const auto i : clips)
        {
            const auto& c = song.playlist[(std::size_t) i];
            if (c.start <= from && c.end() > from)
            {
                if (playing == nullptr || c.start > playing->start || (c.start == playing->start && (c.automation < playing->automation
                                                                                                      || (c.automation == playing->automation && i < playingIndex))))
                    playing = &c, playingIndex = i;
            }
            else if (c.end() <= from && (ended == nullptr || c.end() > ended->end() || (c.end() == ended->end() && c.start > ended->start)))
                ended = &c;
        }
        if (playing == nullptr)
        {
            if (lane != nullptr)
                addLane (from, to);
            else if (ended != nullptr)
            {
                const auto held = endValue (*ended);
                add (from, held);
                if (to != std::numeric_limits<std::int64_t>::max() && to - 1 > from)
                    add (to - 1, held);
            }
            continue;
        }
        const auto& a = song.automations[(std::size_t) playing->automation];
        const auto shift = playing->start - playing->offset; // song tick = clip time + shift
        const auto last = std::min (to, playing->end()) - 1;  // (the interval's last tick)
        // what plays, sampled: every 1/16 beat, each point of the curve and the tick before it (a jump stays a jump: FL's
        // Hold / Stairs / Pulse segments), the interval's two ends; simplify() takes back what a straight line gives
        std::vector<std::int64_t> ticks { from, last };
        for (auto t = (from / (ppq / 16) + 1) * (ppq / 16); t < last; t += ppq / 16)
            ticks.push_back (t);
        for (const auto& p : a.points)
            for (const auto t : { p.tick + shift - 1, p.tick + shift })
                if (t > from && t < last)
                    ticks.push_back (t);
        std::sort (ticks.begin(), ticks.end());
        ticks.erase (std::unique (ticks.begin(), ticks.end()), ticks.end());
        std::vector<AutomationPoint> part;
        for (const auto t : ticks)
            part.push_back ({ t, std::clamp (playedValue (a, (double) (t - shift)), 0.0f, 1.0f), 0.0f, 0 });
        for (const auto& p : simplify (part))
            add (p.tick, p.value);
    }
    // a flat run keeps its two ends only
    std::vector<AutomationPoint> slim;
    for (std::size_t k = 0; k < out.size(); ++k)
    {
        const auto flatBefore = k > 0 && out[k - 1].value == out[k].value && out[k - 1].mode == 0 && out[k - 1].tension == 0.0f;
        const auto flatAfter = k + 1 < out.size() && out[k + 1].value == out[k].value && out[k].mode == 0 && out[k].tension == 0.0f;
        if (! (flatBefore && flatAfter))
            slim.push_back (out[k]);
    }
    return slim;
}

// Moves the clips `chosen` (indices into Song::playlist; the ones that are not `movable` are left) into lanes of the
// tracks that own their controls, in one edit: per control the lane plays what played before (playedCurve over every clip
// of that control that plays, the ones left as clips included: they still win where they play). An automation whose clips
// all moved goes (or becomes the lane: it keeps its slot). Returns how many clips moved.
inline int clipsIntoLanes (Song& song, std::vector<int> chosen)
{
    const auto lanes = laneFlags (song);
    chosen.erase (std::remove_if (chosen.begin(), chosen.end(), [&song, &lanes] (int i) { return ! movable (song, i, lanes); }), chosen.end());
    std::sort (chosen.begin(), chosen.end());
    chosen.erase (std::unique (chosen.begin(), chosen.end()), chosen.end());
    if (chosen.empty())
        return 0;

    struct Group
    {
        AutomationTarget target;
        int owner = -1, lane = -1;
        std::vector<int> moved;   // the chosen clips of this control
        std::vector<int> sources; // their automations
        std::vector<AutomationPoint> points;
        std::string name;
        std::uint32_t colour = 0;
    };
    std::vector<Group> groups;
    for (const auto i : chosen)
    {
        const auto a = song.playlist[(std::size_t) i].automation;
        const auto target = drivenTarget (song.automations[(std::size_t) a]);
        auto it = std::find_if (groups.begin(), groups.end(), [&target] (const Group& g) { return g.target == target; });
        if (it == groups.end())
        {
            Group g;
            g.target = target;
            g.owner = ownerTrack (song, target);
            g.lane = laneOf (song, g.owner, target);
            it = groups.insert (groups.end(), std::move (g));
        }
        it->moved.push_back (i);
        if (std::find (it->sources.begin(), it->sources.end(), a) == it->sources.end())
            it->sources.push_back (a);
    }
    // every clip of each control that plays (the ones that stay clips too), in one pass
    std::vector<std::vector<int>> playing (groups.size());
    for (int i = 0; i < (int) song.playlist.size(); ++i)
        if (const auto& c = song.playlist[(std::size_t) i]; clipPlays (song, c, lanes))
        {
            const auto target = drivenTarget (song.automations[(std::size_t) c.automation]);
            for (std::size_t g = 0; g < groups.size(); ++g)
                if (groups[g].target == target)
                    playing[g].push_back (i);
        }
    for (std::size_t k = 0; k < groups.size(); ++k)
    {
        auto& g = groups[k];
        std::sort (g.sources.begin(), g.sources.end());
        const auto& named = song.automations[(std::size_t) g.sources.front()];
        g.name = named.name;
        g.colour = named.colour;
        const auto own = song.targetValue (g.target);
        g.points = playedCurve (song, playing[k], g.lane >= 0 ? &song.automations[(std::size_t) g.lane] : nullptr,
                                own >= 0.0f ? own : std::numeric_limits<float>::quiet_NaN());
    }

    // the clips go (from the last: the indices before stay valid)
    for (auto it = chosen.rbegin(); it != chosen.rend(); ++it)
        song.playlist.erase (song.playlist.begin() + *it);

    // still used after the move: by a clip of any list, a lane, a track version's lane, steps
    std::vector<char> used (song.automations.size(), 0);
    song.forEachClipList ([&used] (const std::vector<Clip>& list) {
        for (const auto& k : list)
            if (k.kind == ClipKind::automation && k.automation >= 0 && k.automation < (int) used.size())
                used[(std::size_t) k.automation] = 1;
    });
    for (const auto& t : song.playlistTracks)
    {
        for (const auto& l : t.lanes)
            if (l.automation >= 0 && l.automation < (int) used.size())
                used[(std::size_t) l.automation] = 1;
        for (const auto& v : t.versions)
            for (const auto& l : v.lanes)
                if (l.automation >= 0 && l.automation < (int) used.size())
                    used[(std::size_t) l.automation] = 1;
    }
    for (const auto& p : song.patterns)
        for (const auto& n : p.autoNotes)
            if (n.channel >= 0 && n.channel < (int) used.size())
                used[(std::size_t) n.channel] = 1;

    std::vector<int> sources;
    for (auto& g : groups)
    {
        if (g.owner < 0)
            continue;
        if (g.lane >= 0)
        {
            song.automations[(std::size_t) g.lane].points = std::move (g.points);
            continue;
        }
        // the lane takes the slot of its lowest automation nothing else uses (the engine plays the first 1024 only)
        int slot = -1;
        for (const auto a : g.sources)
            if (used[(std::size_t) a] == 0)
            {
                slot = a;
                break;
            }
        if (slot >= 0)
        {
            auto& a = song.automations[(std::size_t) slot];
            used[(std::size_t) slot] = 1;
            const auto uid = a.uid;
            a = Automation {};
            a.uid = uid;
            a.name = g.name;
            a.colour = g.colour;
            a.target = g.target;
            a.points = std::move (g.points);
        }
        else
        {
            Automation a;
            a.name = g.name;
            a.colour = g.colour;
            a.target = g.target;
            a.points = std::move (g.points);
            song.automations.push_back (std::move (a));
            slot = (int) song.automations.size() - 1;
            used.push_back (1);
        }
        if ((int) song.playlistTracks.size() <= g.owner)
            song.playlistTracks.resize ((std::size_t) g.owner + 1);
        song.playlistTracks[(std::size_t) g.owner].lanes.push_back ({ slot, 0 });
        song.playlistTracks[(std::size_t) g.owner].lanesFolded = false;
        sources.insert (sources.end(), g.sources.begin(), g.sources.end());
    }
    for (const auto& g : groups)
        if (g.lane >= 0)
            sources.insert (sources.end(), g.sources.begin(), g.sources.end());
    std::sort (sources.rbegin(), sources.rend());
    sources.erase (std::unique (sources.begin(), sources.end()), sources.end());
    for (const auto a : sources) // (from the last: the indices before stay valid)
        if (a < (int) used.size() && used[(std::size_t) a] == 0)
            song.removeAutomation (a);
    return (int) chosen.size();
}

// "Move automation clips into their tracks' lanes": every movable clip of the current arrangement. Returns how many.
inline int clipsToLanes (Song& song)
{
    return clipsIntoLanes (song, movableClips (song));
}

// How many lanes the tracks have (what "Turn automation lanes back into clips" turns).
inline int laneCount (const Song& song)
{
    int n = 0;
    for (const auto& t : song.playlistTracks)
        n += (int) t.lanes.size();
    return n;
}

// The middle ground (Live's arrangement automation with Bitwig's clip automation, round 190qu): the part [from, to) of
// lane `lane` of `track` becomes an automation clip of its own, drawn under the track (cliplanes) on a track that holds
// only automation clips of this control and is free there, else on the first empty unassigned track. The lane keeps its
// points: the clip plays the same values and wins over the lane where it plays, so nothing sounds different, and the
// clip can be moved, copied and edited on its own. Returns the clip's index, -1 when nothing was made.
inline int laneRangeToClip (Song& song, int track, int lane, std::int64_t from, std::int64_t to)
{
    if (track < 0 || track >= (int) song.playlistTracks.size() || lane < 0 || lane >= (int) song.playlistTracks[(std::size_t) track].lanes.size()
        || from < 0 || to <= from)
        return -1;
    const auto a = song.playlistTracks[(std::size_t) track].lanes[(std::size_t) lane].automation;
    if (a < 0 || a >= (int) song.automations.size())
        return -1;
    const auto source = song.automations[(std::size_t) a]; // (a copy: the list grows)
    Automation part;
    part.target = source.target;
    part.colour = source.colour;
    part.name = source.name;
    part.points.push_back ({ 0, source.valueAt ((double) from), 0.0f });
    for (std::size_t k = 0; k < source.points.size(); ++k)
        if (const auto& p = source.points[k]; p.tick > from && p.tick < to)
            part.points.push_back ({ p.tick - from, p.value, p.tension, p.mode });
    // the first point continues the shape of the segment it was cut from
    for (std::size_t k = 0; k + 1 < source.points.size(); ++k)
        if (source.points[k].tick <= from && source.points[k + 1].tick > from)
        {
            part.points.front().tension = source.points[k].tension;
            part.points.front().mode = source.points[k].mode;
        }
    part.points.push_back ({ to - from, source.valueAt ((double) to), 0.0f });
    song.automations.push_back (std::move (part));

    Clip clip;
    clip.kind = ClipKind::automation;
    clip.automation = (int) song.automations.size() - 1;
    clip.start = from;
    clip.length = to - from;
    const auto freeOn = [&song, from, to] (int t) {
        for (const auto& c : song.playlist)
            if (c.track == t && c.gridSlot < 0 && c.start < to && c.end() > from)
                return false;
        return true;
    };
    clip.track = -1;
    // a track already holding this control's clips (cliplanes draws it under its owner)
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::automation && c.gridSlot < 0 && c.automation >= 0 && c.automation < (int) song.automations.size() - 1
            && song.automations[(std::size_t) c.automation].target == source.target && freeOn (c.track)
            && (c.track >= (int) song.playlistTracks.size() || song.playlistTracks[(std::size_t) c.track].mode == PlaylistTrackMode::unassigned))
        {
            clip.track = c.track;
            break;
        }
    if (clip.track < 0)
        for (int t = 0; t < 500 && clip.track < 0; ++t)
        {
            const auto unassigned = t >= (int) song.playlistTracks.size()
                                    || (song.playlistTracks[(std::size_t) t].mode == PlaylistTrackMode::unassigned && song.playlistTracks[(std::size_t) t].lanes.empty()
                                        && ! song.playlistTracks[(std::size_t) t].grouped);
            if (unassigned && std::none_of (song.playlist.begin(), song.playlist.end(), [t] (const Clip& c) { return c.track == t; }))
                clip.track = t;
        }
    if (clip.track < 0)
    {
        song.automations.pop_back();
        return -1;
    }
    song.playlist.push_back (clip);
    return (int) song.playlist.size() - 1;
}
} // namespace lynx::laneview

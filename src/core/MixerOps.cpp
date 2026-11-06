#include "core/MixerOps.h"
#include "core/ClipOps.h"

#include <algorithm>
#include <numeric>

namespace lynx::mixerops
{
void soloSelected (Song& song, const std::vector<int>& targets, bool alt)
{
    std::vector<int> t;
    for (auto x : targets)
        if (x > 0 && x < (int) song.mixer.size())
            t.push_back (x);
    if (t.empty())
        return;
    auto fed = [&] (int to) {
        for (int src = 1; src < (int) song.mixer.size(); ++src)
            if (src != to && song.routes (src, to))
                return true;
        return false;
    };
    // FL: "soloed" = every target soloed; "alt state" = every target soloed with its sources heard (no source: both)
    auto soloed = true, altState = true, anySources = false;
    for (auto x : t)
    {
        const auto& m = song.mixer[(std::size_t) x];
        const auto sources = fed (x);
        anySources = anySources || sources;
        soloed = soloed && m.solo;
        altState = altState && m.solo && (m.soloSources || ! sources);
    }
    bool solo;
    if (alt)
        solo = ! (soloed && altState);
    else
        solo = (altState && anySources) || ! soloed;
    for (std::size_t i = 1; i < song.mixer.size(); ++i)
    {
        const auto on = solo && std::find (t.begin(), t.end(), (int) i) != t.end();
        song.mixer[i].solo = on;
        if (on)
            song.mixer[i].soloSources = alt;
    }
}

namespace
{
bool isInsert (const Song& song, int t) noexcept
{
    return t > 0 && t < (int) song.mixer.size();
}

template <typename Fn>
void forEachSlot (std::vector<EffectSlot>& slots, Fn&& fn)
{
    for (auto& slot : slots)
    {
        fn (slot);
        for (auto& branch : slot.branches)
            forEachSlot (branch.chain, fn);
    }
}

bool isTrackTarget (AutomationTargetKind k) noexcept
{
    return k == AutomationTargetKind::mixerVolume || k == AutomationTargetKind::mixerPan || k == AutomationTargetKind::effectParam
           || k == AutomationTargetKind::effectMix || k == AutomationTargetKind::mixerSend;
}
} // namespace

namespace
{
// every reference to an insert (channels, Playlist tracks, sends, side-chains, automation) through `map`
template <typename Map>
void remapTrackReferences (Song& song, Map&& map)
{
    for (auto& c : song.channels)
        c.mixerTrack = map (c.mixerTrack);
    for (auto& pt : song.playlistTracks) // audio / instrument tracks (round 146a)
        if (pt.mixerTrack > 0)
            pt.mixerTrack = map (pt.mixerTrack);

    for (auto& track : song.mixer)
    {
        for (auto& send : track.sends)
            send.target = map (send.target);

        forEachSlot (track.effects, [&map] (EffectSlot& slot) {
            if (slot.sidechain > 0)
                slot.sidechain = map (slot.sidechain);
            if (slot.sidechain2 > 0)
                slot.sidechain2 = map (slot.sidechain2);
            for (auto& send : slot.sendTracks)
                if (send > 0)
                    send = map (send);
        });
    }

    song.forEachTarget ([&map] (AutomationTarget& t) {
        if (isTrackTarget (t.kind))
            t.index = map (t.index);
        if (t.kind == AutomationTargetKind::mixerSend && t.slot > 0) // (the route's destination)
            t.slot = map (t.slot);
    });

    for (auto& automation : song.automations)
        if (automation.source.track > 0)
            automation.source.track = map (automation.source.track);
}
} // namespace

void swapTracks (Song& song, int a, int b)
{
    if (a == b || ! isInsert (song, a) || ! isInsert (song, b))
        return;

    std::swap (song.mixer[(std::size_t) a], song.mixer[(std::size_t) b]);
    // a default name follows the position, as in FL (its unnamed tracks show "Insert <number>"): moving insert 16 to
    // 8 shows "Insert 8" there, not "Insert 16" (issue #47 screenshot, round 168h)
    for (const auto [now, was] : { std::pair { a, b }, std::pair { b, a } })
        if (auto& name = song.mixer[(std::size_t) now].name; name == "Insert " + std::to_string (was))
            name = "Insert " + std::to_string (now);
    remapTrackReferences (song, [a, b] (int t) { return t == a ? b : t == b ? a : t; });
}

bool moveTracks (Song& song, std::vector<int>& selection, int delta)
{
    const auto count = (int) song.mixer.size();
    std::sort (selection.begin(), selection.end());
    selection.erase (std::unique (selection.begin(), selection.end()), selection.end());
    selection.erase (std::remove_if (selection.begin(), selection.end(), [count] (int t) { return t <= 0 || t >= count; }), selection.end());

    if (selection.empty() || delta == 0 || (delta < 0 && selection.front() == 1) || (delta > 0 && selection.back() == count - 1))
        return false;

    if (delta < 0)
    {
        for (auto& t : selection) // left to right: each swaps with its left neighbour
        {
            swapTracks (song, t, t - 1);
            --t;
        }
    }
    else
    {
        for (auto it = selection.rbegin(); it != selection.rend(); ++it)
        {
            swapTracks (song, *it, *it + 1);
            ++*it;
        }
    }

    return true;
}

namespace
{
// default names follow their insert's number (after an insert or a delete moved them)
void renumberDefaultNames (Song& song, int from, int delta)
{
    for (int t = std::max (1, from); t < (int) song.mixer.size(); ++t)
    {
        auto& name = song.mixer[(std::size_t) t].name;
        const auto old = t - delta;
        if (old >= 1 && name == "Insert " + std::to_string (old))
            name = "Insert " + std::to_string (t);
    }
}
} // namespace

bool insertTrack (Song& song, int at)
{
    return insertTracks (song, at, 1) == 1;
}

int removeUnusedTrailingInserts (Song& song, int keep)
{
    int removed = 0;
    while ((int) song.mixer.size() - 1 > std::max (0, keep) && ! song.insertInUse ((int) song.mixer.size() - 1))
    {
        if (! deleteTrack (song, (int) song.mixer.size() - 1)) // clears it and drops the links to it (the count stays)
            break;
        song.mixer.pop_back();
        ++removed;
    }
    return removed;
}

bool deleteTrack (Song& song, int t)
{
    const auto count = (int) song.mixer.size();
    if (t < 1 || t >= count)
        return false;

    for (auto& c : song.channels)
        if (c.mixerTrack == t)
            c.mixerTrack = 0;
    for (auto& pt : song.playlistTracks) // an audio track loses its link (round 146a)
        if (pt.mixerTrack == t)
        {
            pt.mixerTrack = -1;
            if (pt.mode == PlaylistTrackMode::audio)
                pt.mode = PlaylistTrackMode::unassigned;
        }

    for (auto& track : song.mixer)
    {
        track.sends.erase (std::remove_if (track.sends.begin(), track.sends.end(), [t] (const MixerSend& s) { return s.target == t; }), track.sends.end());
        forEachSlot (track.effects, [t] (EffectSlot& slot) {
            if (slot.sidechain == t)
                slot.sidechain = 0;
            if (slot.sidechain2 == t)
                slot.sidechain2 = 0;
            for (auto& send : slot.sendTracks)
                if (send == t)
                    send = 0;
        });
    }

    for (auto& automation : song.automations)
        if (automation.source.track == t)
            automation.source.track = -1; // no longer live

    song.mixer[(std::size_t) t] = MixerTrack {};
    for (int i = t; i + 1 < count; ++i)
        swapTracks (song, i, i + 1);
    renumberDefaultNames (song, t, -1);
    song.mixer.back().name = "Insert " + std::to_string (count - 1);
    return true;
}

std::vector<int> linkChannels (Song& song, const std::vector<int>& channels, int track, bool startingHere)
{
    std::vector<int> result;

    if (song.mixer.empty())
        song.mixer = Song::defaultMixer (0);

    track = std::clamp (track, 0, (int) song.mixer.size() - 1);
    auto next = track;

    for (auto channel : channels)
    {
        if (channel < 0 || channel >= (int) song.channels.size())
        {
            result.push_back (0);
            continue;
        }

        while (next >= (int) song.mixer.size() && (int) song.mixer.size() < maxMixerTracks)
        {
            auto& added = song.mixer.emplace_back();
            added.name = "Insert " + std::to_string (song.mixer.size() - 1);
        }

        const auto target = std::min (next, (int) song.mixer.size() - 1);
        song.channels[(std::size_t) channel].mixerTrack = target;
        result.push_back (target);

        if (startingHere && track > 0)
            ++next;
    }

    return result;
}

int ensureInsert (Song& song, int track)
{
    track = std::clamp (track, 0, maxMixerTracks - 1);
    if (song.mixer.empty())
        song.mixer = Song::defaultMixer (0);
    while ((int) song.mixer.size() <= track)
    {
        const auto name = "Insert " + std::to_string (song.mixer.size());
        song.mixer.emplace_back().name = name;
    }
    return track;
}

void routeChannel (Song& song, int channel, int track)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return;
    song.channels[(std::size_t) channel].mixerTrack = ensureInsert (song, track);
}

std::vector<int> channelsOn (const Song& song, int track)
{
    std::vector<int> out;

    for (int c = 0; c < (int) song.channels.size(); ++c)
        if (song.channels[(std::size_t) c].mixerTrack == track)
            out.push_back (c);

    return out;
}

int routeTo (Song& song, const std::vector<int>& sources, int to, bool only)
{
    int routed = 0;

    for (auto from : sources)
    {
        if (! isInsert (song, from) || from == to || (to != 0 && ! song.canRoute (from, to) && ! song.routes (from, to)))
            continue;

        if (only)
        {
            auto& track = song.mixer[(std::size_t) from];
            track.sends.erase (std::remove_if (track.sends.begin(), track.sends.end(), [to] (const MixerSend& s) { return s.target != to; }),
                               track.sends.end());
            track.toMaster = to == 0;
        }

        song.setRoute (from, to, true);
        ++routed;
    }

    return routed;
}

int sidechainTo (Song& song, int from, int to)
{
    if (! isInsert (song, from) || ! isInsert (song, to) || from == to)
        return 0;

    if (! song.routes (from, to))
    {
        if (! song.canRoute (from, to))
            return 0;
        song.setRoute (from, to, true, 0.0f);
    }

    int linked = 0;
    for (auto& fx : song.mixer[(std::size_t) to].effects)
        if (listensToSidechain (fx.kind) && (fx.sidechain < 0 || fx.sidechain == from))
        {
            fx.sidechain = from;
            ++linked;
        }

    return linked;
}

bool listensToSidechain (EffectKind kind) noexcept
{
    return kind == EffectKind::compressor || kind == EffectKind::fruityLimiter || kind == EffectKind::fruityCompressor
           || kind == EffectKind::vocoder;
}

int soleSidechainSender (const Song& song, int to)
{
    if (! isInsert (song, to))
        return -1;
    int sender = -1;
    for (int t = 1; t < (int) song.mixer.size(); ++t)
        for (const auto& send : song.mixer[(std::size_t) t].sends)
            if (t != to && send.target == to && send.level <= 0.0f)
            {
                if (sender >= 0 && sender != t)
                    return -1;
                sender = t;
            }
    return sender;
}

bool linkAddedSidechain (Song& song, int to, int slot)
{
    if (! isInsert (song, to))
        return false;
    auto& effects = song.mixer[(std::size_t) to].effects;
    if (slot < 0 || slot >= (int) effects.size())
        return false;
    auto& fx = effects[(std::size_t) slot];
    // (dynamics only: a vocoder's side input is its modulator, a choice of its own)
    if (fx.kind == EffectKind::vocoder || ! listensToSidechain (fx.kind) || fx.sidechain >= 0)
        return false;
    const auto from = soleSidechainSender (song, to);
    if (from < 0)
        return false;
    fx.sidechain = from;
    return true;
}

bool acceptsSidechain (EffectKind kind) noexcept
{
    switch (kind)
    {
        case EffectKind::compressor: case EffectKind::fruityLimiter: case EffectKind::fruityCompressor:
        case EffectKind::vocoder: case EffectKind::vocodex: case EffectKind::facetEq: case EffectKind::apexLimiter:
        case EffectKind::hushDeEsser: case EffectKind::crestCompressor: case EffectKind::prismMultiband:
        case EffectKind::sentryGate: case EffectKind::luxeVerb: case EffectKind::patcher: case EffectKind::transmitter:
        case EffectKind::flTransporter:
            return true;
        default:
            return false;
    }
}

SidechainPick sidechainFrom (Song& song, int to, int slot, int from, bool second)
{
    if ((to != 0 && ! isInsert (song, to)) || song.mixer.empty() || slot < 0 || slot >= (int) song.mixer[(std::size_t) to].effects.size())
        return SidechainPick::invalid;
    if (from < 0)
    {
        auto& fx = song.mixer[(std::size_t) to].effects[(std::size_t) slot];
        (second ? fx.sidechain2 : fx.sidechain) = -1;
        return SidechainPick::cleared;
    }
    if (! isInsert (song, from) || from == to)
        return SidechainPick::invalid;
    // (the Master is processed after every insert: it needs no route)
    if (to != 0 && ! song.routes (from, to))
    {
        if (! song.canRoute (from, to))
            return SidechainPick::cycle;
        song.setRoute (from, to, true, 0.0f);
    }
    auto& fx = song.mixer[(std::size_t) to].effects[(std::size_t) slot];
    (second ? fx.sidechain2 : fx.sidechain) = from;
    if (fx.kind == EffectKind::vocoder)
        fx.params[16] = 1.0f; // separate inputs (otherwise L = modulator, R = carrier)
    return SidechainPick::linked;
}

namespace
{
// FL's "only": the source stops sending anywhere (its sends and its route to the Master)
void clearRoutes (Song& song, int from)
{
    auto& track = song.mixer[(std::size_t) from];
    track.sends.clear();
    track.toMaster = false;
}

bool routeOne (Song& song, int from, int to, RouteKind kind)
{
    if (! isInsert (song, from) || from == to || to < 0 || to >= (int) song.mixer.size())
        return false;
    const auto sidechain = kind == RouteKind::sidechain || kind == RouteKind::sidechainOnly;
    if (sidechain && to == 0)
        return false; // the Master has no side input
    if (to != 0 && ! song.routes (from, to) && ! song.canRoute (from, to))
        return false;

    if (kind == RouteKind::routeOnly || kind == RouteKind::sidechainOnly)
        clearRoutes (song, from);

    if (sidechain)
        sidechainTo (song, from, to);
    else
        song.setRoute (from, to, true);
    return true;
}
} // namespace

int routeSelectedTo (Song& song, const std::vector<int>& selected, int to, RouteKind kind)
{
    int routed = 0;
    for (auto from : selected)
        if (from != to && routeOne (song, from, to, kind))
            ++routed;
    return routed;
}

int switchSelectedRoute (Song& song, const std::vector<int>& selected, int to, bool on)
{
    int changed = 0;
    for (auto from : selected)
    {
        if (! isInsert (song, from) || from == to || to < 0 || to >= (int) song.mixer.size() || song.routes (from, to) == on)
            continue;
        if (on && to != 0 && ! song.canRoute (from, to))
            continue; // (a cycle)
        song.setRoute (from, to, on);
        if (on)
            song.setRouteLevel (from, to, 1.0f);
        ++changed;
    }
    return changed;
}

int routeToSelected (Song& song, int from, const std::vector<int>& selected, RouteKind kind)
{
    if (! isInsert (song, from))
        return 0;
    if (kind == RouteKind::routeOnly || kind == RouteKind::sidechainOnly)
    {
        // (only once, before the first route: FL clears this track's routes, then adds each selected one)
        clearRoutes (song, from);
        kind = kind == RouteKind::routeOnly ? RouteKind::route : RouteKind::sidechain;
    }
    int routed = 0;
    for (auto to : selected)
        if (to != from && routeOne (song, from, to, kind))
            ++routed;
    if (routed == 0 && song.mixer[(std::size_t) from].sends.empty())
        song.mixer[(std::size_t) from].toMaster = true; // nothing could be routed: never leave it without an output
    return routed;
}

bool hasDefaultName (const Song& song, int track)
{
    if (track < 0 || track >= (int) song.mixer.size())
        return false;
    const auto& name = song.mixer[(std::size_t) track].name;
    return name.empty() || name == "Insert " + std::to_string (track);
}

int createSubmix (Song& song, const std::vector<int>& selected, int to, const std::string& name)
{
    if (! isInsert (song, to))
        return 0;

    std::vector<int> routed;
    for (auto from : selected)
        if (from != to && routeOne (song, from, to, RouteKind::routeOnly))
            routed.push_back (from);
    if (routed.empty())
        return 0;

    const auto colour = song.mixer[(std::size_t) to].colour;
    for (auto t : routed)
    {
        song.mixer[(std::size_t) t].colour = colour;
        if (! name.empty() && hasDefaultName (song, t))
            song.mixer[(std::size_t) t].name = name;
    }
    if (! name.empty())
        song.mixer[(std::size_t) to].name = name + " master";

    // a group when the routed tracks are one contiguous run of one dock (and `to` joins it when it is next to it)
    const auto order = displayOrder (song);
    auto position = [&] (int t) { return (int) (std::find (order.begin(), order.end(), t) - order.begin()); };
    std::vector<int> places;
    for (auto t : routed)
        places.push_back (position (t));
    std::sort (places.begin(), places.end());
    const auto dock = song.mixer[(std::size_t) routed.front()].dock;
    const auto sameDock = std::all_of (routed.begin(), routed.end(), [&] (int t) { return song.mixer[(std::size_t) t].dock == dock; });
    if (sameDock && places.back() - places.front() + 1 == (int) places.size())
    {
        auto first = places.front(), last = places.back();
        const auto at = position (to);
        if (song.mixer[(std::size_t) to].dock == dock)
        {
            if (at == first - 1)
                first = at;
            else if (at == last + 1)
                last = at;
        }
        if (order[(std::size_t) first] > 0)
            song.mixer[(std::size_t) order[(std::size_t) first]].separator = true;
        if (last + 1 < (int) order.size() && order[(std::size_t) last + 1] > 0)
            song.mixer[(std::size_t) order[(std::size_t) last + 1]].separator = true;
    }
    return (int) routed.size();
}

int resetManualLatency (Song& song)
{
    int count = 0;
    for (auto& t : song.mixer)
        if (t.latencyMs != 0.0f)
        {
            t.latencyMs = 0.0f;
            ++count;
        }
    return count;
}

namespace
{
// rows back to "packed" (-1) when they are exactly 0, 1, 2... (old projects compare and save unchanged)
void tidyRows (std::vector<EffectSlot>& fx)
{
    for (int i = 0; i < (int) fx.size(); ++i)
        if (fx[(std::size_t) i].row != i && fx[(std::size_t) i].row != -1)
            return;
    for (auto& e : fx)
        e.row = -1;
}

// puts the effects of `track` in the order of `rows` (one row per effect, all different) and stores the rows
void applyRows (Song& song, int track, std::vector<int> rows)
{
    auto& fx = song.mixer[(std::size_t) track].effects;
    const auto n = (int) fx.size();
    std::vector<int> order (n);
    for (int i = 0; i < n; ++i)
        order[(std::size_t) i] = i;
    std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return rows[(std::size_t) a] < rows[(std::size_t) b]; });
    std::vector<int> at (n); // at[position] = original index now there
    for (int i = 0; i < n; ++i)
        at[(std::size_t) i] = i;
    for (int pos = 0; pos < n; ++pos)
    {
        const auto want = order[(std::size_t) pos];
        const auto k = (int) (std::find (at.begin() + pos, at.end(), want) - at.begin());
        if (k != pos)
        {
            song.swapEffectSlots (track, pos, k); // (automations follow)
            std::swap (at[(std::size_t) pos], at[(std::size_t) k]);
        }
    }
    for (int pos = 0; pos < n; ++pos)
        fx[(std::size_t) pos].row = rows[(std::size_t) at[(std::size_t) pos]];
    tidyRows (fx);
}
} // namespace

std::vector<int> effectRows (const std::vector<EffectSlot>& effects)
{
    const auto n = (int) effects.size();
    std::vector<int> rows ((std::size_t) n);
    int previous = -1;
    for (int i = 0; i < n; ++i)
    {
        const auto wanted = effects[(std::size_t) i].row;
        previous = wanted > previous ? wanted : previous + 1;
        rows[(std::size_t) i] = previous;
    }
    for (int i = n - 1; i >= 0; --i) // (never past the last slot)
        rows[(std::size_t) i] = std::min (rows[(std::size_t) i], i == n - 1 ? maxEffectSlots - 1 : rows[(std::size_t) i + 1] - 1);
    return rows;
}

int effectAtRow (const std::vector<EffectSlot>& effects, int row)
{
    const auto rows = effectRows (effects);
    const auto it = std::find (rows.begin(), rows.end(), row);
    return it == rows.end() ? -1 : (int) (it - rows.begin());
}

bool moveEffectToRow (Song& song, int track, int index, int row)
{
    if (track < 0 || track >= (int) song.mixer.size() || row < 0 || row >= maxEffectSlots)
        return false;
    auto& fx = song.mixer[(std::size_t) track].effects;
    if (index < 0 || index >= (int) fx.size())
        return false;
    auto rows = effectRows (fx);
    if (rows[(std::size_t) index] == row)
        return false;
    if (const auto other = effectAtRow (fx, row); other >= 0)
        rows[(std::size_t) other] = rows[(std::size_t) index];
    rows[(std::size_t) index] = row;
    applyRows (song, track, rows);
    return true;
}

int moveEffectBy (Song& song, int track, int index, int delta)
{
    if (track < 0 || track >= (int) song.mixer.size() || index < 0 || index >= (int) song.mixer[(std::size_t) track].effects.size() || delta == 0)
        return -1;
    const auto from = effectRows (song.mixer[(std::size_t) track].effects)[(std::size_t) index];
    const auto to = ((from + delta) % maxEffectSlots + maxEffectSlots) % maxEffectSlots; // (FL wraps around)
    if (! moveEffectToRow (song, track, index, to))
        return -1;
    return effectAtRow (song.mixer[(std::size_t) track].effects, to); // (the moved effect is the one there now)
}

int insertEffectAtRow (Song& song, int track, EffectSlot fx, int row)
{
    if (track < 0 || track >= (int) song.mixer.size() || row < 0 || row >= maxEffectSlots)
        return -1;
    auto& effects = song.mixer[(std::size_t) track].effects;
    if ((int) effects.size() >= maxEffectSlots || effectAtRow (effects, row) >= 0)
        return -1;
    auto rows = effectRows (effects);
    effects.push_back (std::move (fx));
    rows.push_back (row);
    applyRows (song, track, rows);
    return effectAtRow (song.mixer[(std::size_t) track].effects, row);
}

void removeEffectKeepRows (Song& song, int track, int index)
{
    if (track < 0 || track >= (int) song.mixer.size() || index < 0 || index >= (int) song.mixer[(std::size_t) track].effects.size())
        return;
    auto rows = effectRows (song.mixer[(std::size_t) track].effects);
    song.removeEffectSlot (track, index);
    rows.erase (rows.begin() + index);
    auto& fx = song.mixer[(std::size_t) track].effects;
    for (int i = 0; i < (int) fx.size(); ++i)
        fx[(std::size_t) i].row = rows[(std::size_t) i];
    tidyRows (fx);
}

bool moveEffectSlot (Song& song, int track, int from, int to)
{
    if (track < 0 || track >= (int) song.mixer.size())
        return false;

    const auto count = (int) song.mixer[(std::size_t) track].effects.size();

    if (from < 0 || from >= count || to < 0 || to >= count || from == to)
        return false;

    const auto rows = effectRows (song.mixer[(std::size_t) track].effects);
    for (auto s = from; s != to; s += to > from ? 1 : -1)
        song.swapEffectSlots (track, s, s + (to > from ? 1 : -1));
    auto& fx = song.mixer[(std::size_t) track].effects;
    for (int i = 0; i < count; ++i) // (the rows stay where they were)
        fx[(std::size_t) i].row = rows[(std::size_t) i];
    tidyRows (fx);

    return true;
}

void setAllEffectsEnabled (Song& song, int track, bool enabled)
{
    if (track >= 0 && track < (int) song.mixer.size())
        for (auto& fx : song.mixer[(std::size_t) track].effects)
            fx.enabled = enabled;
}

bool anyEffectEnabled (const Song& song, int track) noexcept
{
    if (track < 0 || track >= (int) song.mixer.size())
        return false;

    const auto& fx = song.mixer[(std::size_t) track].effects;
    return std::any_of (fx.begin(), fx.end(), [] (const EffectSlot& s) { return s.enabled; });
}
void resetToDefault (Song& song, const std::vector<int>& tracks)
{
    for (auto t : tracks)
        if (t >= 0 && t < (int) song.mixer.size())
        {
            auto& m = song.mixer[(std::size_t) t];
            m.volume = unityFader;
            m.pan = 0.0f;
            m.stereo = 0.0f;
            m.invertPhase = m.swapChannels = false;
            m.eq = TrackEq {};
            m.muted = m.solo = false;
        }
}

void resetRouting (Song& song, const std::vector<int>& tracks)
{
    for (auto t : tracks)
        if (t > 0 && t < (int) song.mixer.size())
        {
            auto& m = song.mixer[(std::size_t) t];
            m.sends.clear();
            m.toMaster = true;
            m.masterLevel = 1.0f;
        }
}

int unrouteInto (Song& song, const std::vector<int>& tracks)
{
    int removed = 0;
    for (int from = 1; from < (int) song.mixer.size(); ++from)
    {
        auto& m = song.mixer[(std::size_t) from];
        for (auto t : tracks)
        {
            if (t == from)
                continue;
            if (t == 0 && m.toMaster && ! m.sends.empty())
            {
                m.toMaster = false; // (it still goes somewhere else)
                ++removed;
            }
            const auto before = m.sends.size();
            m.sends.erase (std::remove_if (m.sends.begin(), m.sends.end(), [t] (const MixerSend& s) { return s.target == t; }), m.sends.end());
            removed += (int) (before - m.sends.size());
        }
        if (m.sends.empty() && ! m.toMaster)
            m.toMaster = true;
    }
    return removed;
}

std::vector<int> destinationsOf (const Song& song, int track)
{
    std::vector<int> out;
    if (track <= 0 || track >= (int) song.mixer.size())
        return out;
    const auto& m = song.mixer[(std::size_t) track];
    if (m.toMaster)
        out.push_back (0);
    for (const auto& s : m.sends)
        if (std::find (out.begin(), out.end(), s.target) == out.end())
            out.push_back (s.target);
    std::sort (out.begin(), out.end());
    return out;
}

std::vector<int> sourcesOf (const Song& song, int track)
{
    std::vector<int> out;
    for (int from = 1; from < (int) song.mixer.size(); ++from)
        if (from != track && song.routes (from, track))
            out.push_back (from);
    return out;
}

int insertTracks (Song& song, int at, int count)
{
    // one shift of every reference (round 166d: hundreds of inserts at once stay instant; it used to swap the new
    // insert into place one neighbour at a time)
    const auto tracks = (int) song.mixer.size();
    count = std::min (count, maxMixerTracks - tracks);
    if (at < 1 || at > tracks || count <= 0)
        return 0;

    remapTrackReferences (song, [at, count] (int t) { return t >= at ? t + count : t; });
    std::vector<MixerTrack> added ((std::size_t) count);
    for (int i = 0; i < count; ++i)
        added[(std::size_t) i].name = "Insert " + std::to_string (at + i);
    song.mixer.insert (song.mixer.begin() + at, std::make_move_iterator (added.begin()), std::make_move_iterator (added.end()));
    renumberDefaultNames (song, at + count, count);
    return count;
}

int roomForInserts (const Song& song) noexcept
{
    return std::max (0, maxMixerTracks - std::max (1, (int) song.mixer.size()));
}

std::vector<int> displayOrder (const Song& song)
{
    std::vector<int> order;
    for (auto dock : { MixerDock::left, MixerDock::middle, MixerDock::right })
        for (int t = 0; t < (int) song.mixer.size(); ++t)
            if (song.mixer[(std::size_t) t].dock == dock)
                order.push_back (t);
    return order;
}

std::vector<int> groupOf (const Song& song, int track)
{
    std::vector<int> out;
    if (track <= 0 || track >= (int) song.mixer.size())
        return out;
    const auto order = displayOrder (song);
    const auto at = (int) (std::find (order.begin(), order.end(), track) - order.begin());
    const auto dock = song.mixer[(std::size_t) track].dock;
    const auto inBlock = [&] (int i) { return i >= 0 && i < (int) order.size() && order[(std::size_t) i] > 0
                                              && song.mixer[(std::size_t) order[(std::size_t) i]].dock == dock; };
    auto first = at;
    while (! song.mixer[(std::size_t) order[(std::size_t) first]].separator && inBlock (first - 1))
        --first;
    auto last = at;
    while (inBlock (last + 1) && ! song.mixer[(std::size_t) order[(std::size_t) last + 1]].separator)
        ++last;
    for (auto i = first; i <= last; ++i)
        out.push_back (order[(std::size_t) i]);
    return out;
}

void createGroup (Song& song, const std::vector<int>& tracks, int current)
{
    const auto order = displayOrder (song);
    const auto picked = [&] (int t) { return std::find (tracks.begin(), tracks.end(), t) != tracks.end(); };
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        const auto t = order[i];
        if (t <= 0 || ! picked (t))
            continue;
        if (i == 0 || ! picked (order[i - 1]))
            song.mixer[(std::size_t) t].separator = true;
        if (i + 1 < order.size() && order[i + 1] > 0 && ! picked (order[i + 1]))
            song.mixer[(std::size_t) order[i + 1]].separator = true;
    }
    if (current >= 0 && current < (int) song.mixer.size() && song.mixer[(std::size_t) current].colour != 0)
        for (auto t : tracks)
            if (t > 0 && t < (int) song.mixer.size() && song.mixer[(std::size_t) t].colour == 0)
                song.mixer[(std::size_t) t].colour = song.mixer[(std::size_t) current].colour;
    // the group's name (REA round 149 [DECOMP FLEngine FUN_012aa200 → FUN_012be6f0, the track's name at +0xc]): the
    // selected tracks without a name take the current track's
    if (current >= 0 && current < (int) song.mixer.size() && ! song.mixer[(std::size_t) current].name.empty())
        for (auto t : tracks)
            if (t > 0 && t != current && t < (int) song.mixer.size() && song.mixer[(std::size_t) t].name.empty())
                song.mixer[(std::size_t) t].name = song.mixer[(std::size_t) current].name;
}

int autoColorGroup (Song& song, int track)
{
    int changed = 0;
    if (track <= 0 || track >= (int) song.mixer.size())
        return 0;
    const auto colour = song.mixer[(std::size_t) track].colour;
    for (auto t : groupOf (song, track))
        if (song.mixer[(std::size_t) t].colour != colour)
        {
            song.mixer[(std::size_t) t].colour = colour;
            ++changed;
        }
    return changed;
}

void setSeparator (Song& song, const std::vector<int>& tracks, bool on)
{
    for (auto t : tracks)
        if (t > 0 && t < (int) song.mixer.size())
            song.mixer[(std::size_t) t].separator = on;
}

void dockTracks (Song& song, const std::vector<int>& tracks, MixerDock dock)
{
    for (auto t : tracks)
        if (t >= 0 && t < (int) song.mixer.size())
            song.mixer[(std::size_t) t].dock = dock;
}

int firstTrackAfterUsed (const Song& song) noexcept
{
    int track = 0;
    for (const auto& c : song.playlist)
        track = std::max (track, c.track + 1);
    return track;
}

std::string insertDisplayName (const Song& song, int track)
{
    if (track <= 0 || track >= (int) song.mixer.size())
        return "Master";
    const auto& name = song.mixer[(std::size_t) track].name;
    return name.empty() ? "Insert " + std::to_string (track) : name;
}

int placeRenderedFiles (Song& song, const std::vector<RenderedFile>& files)
{
    int placed = 0;
    auto track = firstTrackAfterUsed (song);
    for (const auto& f : files)
    {
        if (f.path.empty() || ! (f.frames > 0.0) || ! (f.sampleRate > 0.0))
            continue;
        ChannelDef c;
        auto stem = f.path.substr (f.path.find_last_of ("/\\") == std::string::npos ? 0 : f.path.find_last_of ("/\\") + 1);
        if (const auto dot = stem.find_last_of ('.'); dot != std::string::npos && dot > 0)
            stem.resize (dot);
        c.name = stem;
        c.instrument = InstrumentKind::audioClip;
        c.samplePath = f.path;
        c.mixerTrack = 0; // FL: "---"
        song.channels.push_back (c);
        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = (int) song.channels.size() - 1;
        clip.track = track++;
        clip.start = 0;
        clip.length = std::max<std::int64_t> (1, clipops::fileTicks (song.channels.back(), f.frames, f.sampleRate, song.bpm));
        song.playlist.push_back (clip);
        ++placed;
    }
    if (placed > 0 && song.autoCrossfades)
        clipops::updateCrossfades (song);
    return placed;
}
} // namespace lynx::mixerops

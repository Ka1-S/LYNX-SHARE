#include "core/MixerOps.h"
#include "core/ClipOps.h"
#include "core/FlpExport.h"
#include "core/RackOps.h"
#include "core/Transmitter.h"
#include "core/SoundFont.h"
#include "core/TransporterFl.h"
#include "core/VoltageController.h"

#include <algorithm>
#include <cmath>
#include <functional>
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

// every effect slot, also inside containers and Patchers (a Fruity Send in a Patcher, a nested Patcher...)
template <typename Fn>
void forEachSlot (EffectSlot& slot, Fn& fn)
{
    fn (slot);
    for (auto& branch : slot.branches)
        for (auto& child : branch.chain)
            forEachSlot (child, fn);
    for (auto& node : slot.patcher.nodes)
        if (! node.generator)
            forEachSlot (node.effect, fn);
}

bool isTrackTarget (AutomationTargetKind k) noexcept
{
    return k == AutomationTargetKind::mixerVolume || k == AutomationTargetKind::mixerPan || k == AutomationTargetKind::effectParam
           || k == AutomationTargetKind::effectMix || k == AutomationTargetKind::mixerSend;
}

// A "send to insert" parameter stored as insert / (maxMixerTracks - 1) (Fruity Send's "Send to", the Transporter's
// loops send; 0 = none)
void remapTrackParam (float& value, const std::function<int (int)>& map)
{
    const auto track = (int) std::lround (std::clamp ((double) value, 0.0, 1.0) * (maxMixerTracks - 1));
    if (track <= 0)
        return;
    const auto to = map (track);
    value = to > 0 ? (float) to / (float) (maxMixerTracks - 1) : 0.0f;
}

// The extra outputs of a generator Patcher (doc.outputTracks: an insert, or -1 = "the k-th insert after the channel's
// own", FL's relative routing). A chosen insert follows its track; a relative output that would land on another insert
// after the move is pinned to the one it fed (the logical track), one that still lands right stays relative.
// `base` = the channel's insert before the move.
void remapPatcherOutputs (ChannelDef& channel, int base, const std::function<int (int)>& map)
{
    auto& doc = channel.patcher;
    int outputs = (int) doc.outputTracks.size();
    for (const auto& w : doc.wires)
        if (w.kind == PatchWire::Kind::audio && w.to == patchToHost)
            outputs = std::max (outputs, w.toPort);
    const auto newBase = std::max (0, map (base));
    for (int k = 1; k <= outputs; ++k)
    {
        if (k <= (int) doc.outputTracks.size() && doc.outputTracks[(std::size_t) k - 1] >= 0)
        {
            auto& t = doc.outputTracks[(std::size_t) k - 1];
            if (t > 0)
                t = std::max (0, map (t)); // (its insert deleted: the Master, as a channel's)
            continue;
        }
        const auto now = std::max (0, map (std::min (base + k, maxMixerTracks - 1)));
        if (now == std::min (newBase + k, maxMixerTracks - 1))
            continue;
        if ((int) doc.outputTracks.size() < k)
            doc.outputTracks.resize ((std::size_t) k, -1);
        doc.outputTracks[(std::size_t) k - 1] = now;
    }
}

// An effect / instrument imported from FL keeps FL's own state for the export (FlOrigin, exported while its fingerprint
// matches). The insert numbers that state holds are rewritten with the remap, so a moved track still exports to the
// track it was linked to and the rest of the state stays byte for byte (round 177a).
void putI32 (std::vector<std::uint8_t>& d, std::size_t at, int v)
{
    if (at + 4 <= d.size())
        for (int b = 0; b < 4; ++b)
            d[at + (std::size_t) b] = (std::uint8_t) (((std::uint32_t) v >> (8 * b)) & 0xFF);
}

void patchOriginTracks (EffectSlot& slot)
{
    auto& d = slot.flOrigin.state;
    if (slot.kind == EffectKind::fruitySend && slot.flOrigin.plugin == "Fruity Send") // pan, vol, dry, insert (REA_rodada105_send §1)
        putI32 (d, 12, std::max (0, fruitySendTarget (slot)));
    else if (slot.kind == EffectKind::transmitter && slot.flOrigin.plugin == "Transmitter") // version, 42 controls, the two sends
    {
        putI32 (d, 4 + 4 * (std::size_t) transmitter::count, slot.sendTracks[0]);
        putI32 (d, 8 + 4 * (std::size_t) transmitter::count, slot.sendTracks[1]);
    }
    else if (slot.kind == EffectKind::flTransporter && slot.flOrigin.plugin == "Transporter" && d.size() >= 4)
    {
        // version, 20 / 22 controls, two flag bytes, source, stereo, send track, input track (REA_rodada167f)
        const auto version = (int) ((std::uint32_t) d[0] | (std::uint32_t) d[1] << 8 | (std::uint32_t) d[2] << 16 | (std::uint32_t) d[3] << 24);
        const std::size_t controls = version >= 8 ? 22 : 20;
        putI32 (d, 4 + 4 * controls + 2 + 8, std::max (0, fruitySendTarget (slot)));
    }
}

void patchOriginTracks (ChannelDef& c)
{
    // FL's Soundfont Player: version, 13 controls, the font path (length byte, or 0xFF + i32), then the reverb and
    // chorus "send to" tracks (insert − 1, −1 = none), each followed by a flag byte (REA_rodada158 §1)
    if (c.instrument != InstrumentKind::soundfont || c.flOrigin.plugin != "Fruity Soundfont Player")
        return;
    auto& d = c.flOrigin.state;
    std::size_t at = 4 + 52;
    if (at >= d.size())
        return;
    std::size_t len = d[at++];
    if (len == 0xFF)
    {
        if (at + 4 > d.size())
            return;
        len = (std::size_t) ((std::uint32_t) d[at] | (std::uint32_t) d[at + 1] << 8 | (std::uint32_t) d[at + 2] << 16 | (std::uint32_t) d[at + 3] << 24);
        at += 4;
    }
    at += len;
    const auto fl = [&c] (int p) {
        const auto v = (int) std::lround (c.modelParams[(std::size_t) p]);
        return v > 0 ? v - 1 : -1;
    };
    if ((std::size_t) sf::pChorusTrack < c.modelParams.size())
    {
        putI32 (d, at, fl (sf::pReverbTrack));
        putI32 (d, at + 5, fl (sf::pChorusTrack));
    }
}

template <typename T>
bool originMatches (const T& x)
{
    return ! x.flOrigin.empty() && x.flOrigin.fingerprint == flpexport::fingerprint (x);
}
} // namespace

namespace
{
// Every reference to an insert through `map` (old insert -> its new number, or -1 when the insert was deleted), so a
// move, an insert or a delete never re-wires anything (round 177a): channels, Playlist tracks, sends, side-chains,
// slot sends (Transmitter, Fruity Send, Transporter, a Patcher's "To LYNX" outputs, also inside containers and
// Patchers), plugins' own "send to insert" settings (Soundfont reverb / chorus, Voltage Controller outputs),
// automation, links, initial values, pattern events and controller sources. FL keeps the mixer's own routes,
// channels, Playlist tracks and automation / links (FUN_012a4030, FUN_012cb6c0) but leaves the plugin-side numbers
// alone, so its Fruity Send / Patcher outputs end on another track; LYNX keeps those too (REA_rodada177a §1).
// A reference to a deleted insert is dropped: sends removed, side-chains and slot sends cleared, channels to the
// Master, automation / links without a target (FL marks them -1 too).
void remapTrackReferences (Song& song, const std::function<int (int)>& map)
{
    for (auto& c : song.channels)
    {
        const auto exportsOrigin = originMatches (c);
        if (c.instrument == InstrumentKind::patcher)
            remapPatcherOutputs (c, c.mixerTrack, map);
        c.mixerTrack = std::max (0, map (c.mixerTrack));
        if (c.freeze.insert > 0) // (round 190ff: a track freeze follows its insert; deleted, it plays live — -1)
            c.freeze.insert = map (c.freeze.insert) > 0 ? map (c.freeze.insert) : -1;

        if (c.instrument == InstrumentKind::soundfont)
            for (const auto p : { sf::pReverbTrack, sf::pChorusTrack })
                if ((std::size_t) p < c.modelParams.size() && c.modelParams[(std::size_t) p] > 0.5f)
                    c.modelParams[(std::size_t) p] = (float) map ((int) std::lround (c.modelParams[(std::size_t) p]));
        if (c.instrument == InstrumentKind::voltage)
            if (auto vs = voltage::parse (c.pluginState))
            {
                auto changed = false;
                for (auto& t : vs->tracks)
                    if (t > 0)
                    {
                        const auto to = std::max (0, map (t));
                        changed = changed || to != t;
                        t = to;
                    }
                if (changed)
                    c.pluginState = voltage::write (*vs);
            }
        if (exportsOrigin && flpexport::fingerprint (c) != c.flOrigin.fingerprint)
        {
            patchOriginTracks (c);
            if (c.instrument == InstrumentKind::voltage) // (its state is FL's own: the export writes it)
                c.flOrigin.state = c.pluginState;
            c.flOrigin.fingerprint = flpexport::fingerprint (c);
        }
    }
    for (auto& vca : song.vcas) // VCA members (round 177a slice 3)
    {
        for (auto& m : vca.members)
            m = map (m);
        vca.members.erase (std::remove_if (vca.members.begin(), vca.members.end(), [] (int m) { return m <= 0; }), vca.members.end());
        std::sort (vca.members.begin(), vca.members.end());
    }
    for (auto& pt : song.playlistTracks) // audio / instrument tracks (round 146a)
        if (pt.mixerTrack > 0)
        {
            pt.mixerTrack = map (pt.mixerTrack);
            if (pt.mixerTrack < 0 && pt.mode == PlaylistTrackMode::audio) // its insert deleted: the link goes
                pt.mode = PlaylistTrackMode::unassigned;
        }

    for (auto& track : song.mixer)
    {
        for (auto& send : track.sends)
            send.target = map (send.target);
        track.sends.erase (std::remove_if (track.sends.begin(), track.sends.end(), [] (const MixerSend& s) { return s.target <= 0; }),
                           track.sends.end());

        auto fix = [&map] (EffectSlot& slot) {
            const auto exportsOrigin = originMatches (slot);
            for (auto* source : { &slot.sidechain, &slot.sidechain2 })
                if (*source > 0)
                    *source = std::max (-1, map (*source));
            for (auto& send : slot.sendTracks)
                if (send > 0)
                    send = std::max (0, map (send));
            for (auto& t : slot.patcher.outputTracks)
                if (t > 0)
                    t = std::max (0, map (t));
            if (slot.kind == EffectKind::fruitySend)
                remapTrackParam (slot.params[3], map);
            else if (slot.kind == EffectKind::flTransporter)
                remapTrackParam (slot.params[(std::size_t) transporter::sendParam], map);
            if (exportsOrigin)
            {
                patchOriginTracks (slot); // (unchanged numbers write the same bytes)
                slot.flOrigin.fingerprint = flpexport::fingerprint (slot);
            }
        };
        for (auto& slot : track.effects)
            forEachSlot (slot, fix);
    }

    song.forEachTarget ([&map] (AutomationTarget& t) {
        if (! isTrackTarget (t.kind))
            return;
        t.index = map (t.index);
        if (t.kind == AutomationTargetKind::mixerSend && t.slot > 0) // (the route's destination)
            t.slot = map (t.slot);
        if (t.index < 0 || t.slot < 0)
            t = AutomationTarget {}; // its insert deleted: no target
    });
    song.removeDeadMidiLinks();

    for (auto& automation : song.automations)
        if (automation.source.track > 0)
            automation.source.track = map (automation.source.track); // -1 = no longer live
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

std::vector<bool> hiddenUnusedInserts (const Song& song, const std::set<int>& keep)
{
    const auto tracks = (int) song.mixer.size();
    std::vector<bool> hidden ((std::size_t) tracks + 1, false);
    for (int t = 1; t < tracks; ++t)
        hidden[(std::size_t) t] = keep.count (t) == 0 && ! song.insertInUse (t);
    return hidden;
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

    // one shift of every reference: the deleted insert's links go, the following inserts move one place left and a
    // blank insert takes the last place (the count stays)
    remapTrackReferences (song, [t] (int x) { return x == t ? -1 : x > t ? x - 1 : x; });
    song.mixer.erase (song.mixer.begin() + t);
    song.mixer.emplace_back();
    renumberDefaultNames (song, t, -1);
    song.mixer.back().name = "Insert " + std::to_string (count - 1);
    return true;
}

bool reconnectLostSend (Song& song, int track, int index, int to)
{
    if (! isInsert (song, track) || index < 0 || index >= (int) song.mixer[(std::size_t) track].lostSends.size() || to < 1
        || to >= (int) song.mixer.size() || to == track || (! song.routes (track, to) && ! song.canRoute (track, to)))
        return false;
    auto& t = song.mixer[(std::size_t) track];
    const auto lost = t.lostSends[(std::size_t) index];
    t.lostSends.erase (t.lostSends.begin() + index);
    if (const auto it = std::find_if (t.sends.begin(), t.sends.end(), [to] (const MixerSend& s) { return s.target == to; }); it != t.sends.end())
        it->level = lost.level;
    else
        t.sends.push_back ({ to, lost.level });
    return true;
}

void removeLostSends (Song& song, int track, int index)
{
    if (! isInsert (song, track))
        return;
    auto& lost = song.mixer[(std::size_t) track].lostSends;
    if (index < 0)
        lost.clear();
    else if (index < (int) lost.size())
        lost.erase (lost.begin() + index);
}

std::vector<int> playlistTracksLinkedTo (const Song& song, const std::vector<int>& tracks)
{
    const auto among = [&tracks] (int t) { return t > 0 && std::find (tracks.begin(), tracks.end(), t) != tracks.end(); };
    std::vector<int> result;
    for (int i = 0; i < (int) song.playlistTracks.size(); ++i)
    {
        const auto& pt = song.playlistTracks[(std::size_t) i];
        auto insert = -1;
        if (pt.mode == PlaylistTrackMode::audio)
            insert = pt.mixerTrack;
        else if (pt.mode == PlaylistTrackMode::instrument) // (FL: the channel's own mixer track)
            insert = pt.channel >= 0 && pt.channel < (int) song.channels.size() ? song.channels[(std::size_t) pt.channel].mixerTrack : pt.mixerTrack;
        if (among (insert))
            result.push_back (i);
    }
    return result;
}

namespace
{
// FL 21 (fix 19603): the tracks that sent into `t` send to `t`'s destinations instead (at their send's level)
void rerouteFeeders (Song& song, int t)
{
    const auto& gone = song.mixer[(std::size_t) t];
    const auto destinations = gone.sends;
    const auto toMaster = gone.toMaster;
    for (int x = 1; x < (int) song.mixer.size(); ++x)
    {
        if (x == t)
            continue;
        auto& track = song.mixer[(std::size_t) x];
        const auto it = std::find_if (track.sends.begin(), track.sends.end(), [t] (const MixerSend& s) { return s.target == t; });
        if (it == track.sends.end() || it->level <= 0.0f) // (a level-0 sidechain link carries no signal)
            continue;
        const auto level = it->level;
        if (toMaster)
            track.toMaster = true;
        for (const auto& d : destinations)
            if (d.target != x && d.target != t
                && std::none_of (track.sends.begin(), track.sends.end(), [&d] (const MixerSend& s) { return s.target == d.target; }))
                track.sends.push_back ({ d.target, level });
    }
}
} // namespace

int deleteTracks (Song& song, std::vector<int> tracks, LinkedPlaylistTracks linked, bool keepLostLinks)
{
    std::sort (tracks.begin(), tracks.end());
    tracks.erase (std::unique (tracks.begin(), tracks.end()), tracks.end());
    tracks.erase (std::remove_if (tracks.begin(), tracks.end(), [&song] (int t) { return t < 1 || t >= (int) song.mixer.size(); }), tracks.end());
    if (tracks.empty())
        return 0;

    const auto playlist = playlistTracksLinkedTo (song, tracks);
    std::vector<int> channels; // instrument tracks' channels, for "Delete tracks and channels"
    for (const auto i : playlist)
    {
        auto& pt = song.playlistTracks[(std::size_t) i];
        if (linked == LinkedPlaylistTracks::deleteToo && pt.mode == PlaylistTrackMode::instrument && pt.channel >= 0
            && pt.channel < (int) song.channels.size())
            channels.push_back (pt.channel);
        if (linked == LinkedPlaylistTracks::deleteToo) // FL resets the track: "Track N", unassigned
        {
            pt.name.clear();
            pt.colour = 0;
            pt.icon = 0;
        }
        pt.mode = PlaylistTrackMode::unassigned; // (FL "Unlink": the track keeps its name)
        pt.mixerTrack = -1;
        pt.channel = -1;
    }

    int deleted = 0;
    for (auto it = tracks.rbegin(); it != tracks.rend(); ++it) // from the right: the numbers still to delete stay valid
    {
        if (keepLostLinks) // (round 186c's option) each send into it stays, greyed, under the deleted insert's name
        {
            const auto name = song.mixer[(std::size_t) *it].name.empty() ? "Insert " + std::to_string (*it) : song.mixer[(std::size_t) *it].name;
            for (int x = 1; x < (int) song.mixer.size(); ++x)
                for (const auto& send : song.mixer[(std::size_t) x].sends)
                    if (x != *it && send.target == *it)
                        song.mixer[(std::size_t) x].lostSends.push_back ({ name, send.level });
        }
        rerouteFeeders (song, *it);
        if (deleteTrack (song, *it))
            ++deleted;
    }
    if (! channels.empty() && channels.size() < song.channels.size()) // (at least one channel stays, as in the Channel rack)
        removeChannels (song, channels);
    return deleted;
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

std::vector<int> insertInputs (const Song& song, int track)
{
    std::vector<int> list { track };
    for (int from = 1; from < (int) song.mixer.size(); ++from)
        if (from != track && song.routes (from, track))
            list.push_back (from);
    return list;
}

int sidechainIndexSource (const Song& song, int track, int k)
{
    if (k <= 0)
        return -1;
    const auto list = insertInputs (song, track);
    return k < (int) list.size() ? list[(std::size_t) k] : -1;
}

int sidechainIndexOf (const Song& song, int track, int source)
{
    const auto list = insertInputs (song, track);
    for (std::size_t k = 1; k < list.size(); ++k)
        if (list[k] == source)
            return (int) k;
    return 0;
}

bool listensToSidechain (EffectKind kind) noexcept
{
    return kind == EffectKind::compressor || kind == EffectKind::fruityLimiter || kind == EffectKind::fruityCompressor
           || kind == EffectKind::vocoder;
}

bool reportsGainReduction (EffectKind kind) noexcept
{
    switch (kind)
    {
        case EffectKind::compressor:
        case EffectKind::apexLimiter:
        case EffectKind::crestCompressor:
        case EffectKind::sentryGate:
        case EffectKind::quellDenoiser:
        case EffectKind::ridgeStrip:
        case EffectKind::glowLeveler:
        case EffectKind::hushDeEsser:
            return true;
        default:
            return false;
    }
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

int removeEffectsOfKind (Song& song, EffectKind kind)
{
    int removed = 0;
    for (int t = 0; t < (int) song.mixer.size(); ++t)
        for (int i = (int) song.mixer[(std::size_t) t].effects.size() - 1; i >= 0; --i)
            if (song.mixer[(std::size_t) t].effects[(std::size_t) i].kind == kind)
            {
                removeEffectKeepRows (song, t, i);
                ++removed;
            }
    return removed;
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
            m.lostSends.clear();
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

bool canFold (const Song& song, int track)
{
    const auto group = groupOf (song, track);
    if (group.size() < 2)
        return false;
    if (song.mixer[(std::size_t) group.front()].separator)
        return true;
    const auto order = displayOrder (song);
    const auto after = std::find (order.begin(), order.end(), group.back()) + 1;
    return after != order.end() && *after > 0 && song.mixer[(std::size_t) *after].separator
           && song.mixer[(std::size_t) *after].dock == song.mixer[(std::size_t) group.back()].dock;
}

int groupBus (const Song& song, const std::vector<int>& group)
{
    for (const auto bus : group)
    {
        auto all = true;
        for (const auto t : group)
        {
            if (t == bus)
                continue;
            const auto& m = song.mixer[(std::size_t) t];
            if (m.toMaster || m.sends.size() != 1 || m.sends.front().target != bus)
            {
                all = false;
                break;
            }
        }
        if (all)
            return bus;
    }
    return -1;
}

bool isFolded (const Song& song, int track)
{
    const auto group = groupOf (song, track);
    return ! group.empty() && song.mixer[(std::size_t) group.front()].folded && canFold (song, track);
}

std::vector<FoldShow> foldView (const Song& song)
{
    std::vector<FoldShow> out (song.mixer.size(), FoldShow::strip);
    for (std::size_t t = 1; t < song.mixer.size(); ++t)
    {
        if (! song.mixer[t].folded || out[t] != FoldShow::strip)
            continue;
        const auto group = groupOf (song, (int) t);
        if (group.empty() || group.front() != (int) t || ! canFold (song, (int) t))
            continue;
        const auto bus = groupBus (song, group);
        for (const auto m : group)
            out[(std::size_t) m] = m == bus ? FoldShow::strip : FoldShow::hidden;
        if (bus < 0)
            out[(std::size_t) group.front()] = FoldShow::tab;
    }
    return out;
}

bool toggleFold (Song& song, int track)
{
    if (! canFold (song, track))
        return false;
    const auto head = groupOf (song, track).front();
    auto& flag = song.mixer[(std::size_t) head].folded;
    flag = ! flag;
    return flag;
}

int foldAll (Song& song, bool fold)
{
    int changed = 0;
    for (int t = 1; t < (int) song.mixer.size(); ++t)
    {
        const auto group = groupOf (song, t);
        if (group.empty() || group.front() != t || ! canFold (song, t) || song.mixer[(std::size_t) t].folded == fold)
            continue;
        song.mixer[(std::size_t) t].folded = fold;
        ++changed;
    }
    return changed;
}

std::vector<int> vcasOf (const Song& song, int track)
{
    std::vector<int> out;
    const auto n = (int) song.vcas.size();
    for (int v = 0; v < n; ++v)
        if (std::binary_search (song.vcas[(std::size_t) v].members.begin(), song.vcas[(std::size_t) v].members.end(), track))
            for (int p = v, guard = 0; p >= 0 && p < n && guard <= n; p = song.vcas[(std::size_t) p].parent, ++guard) // (a cycle stops)
                if (std::find (out.begin(), out.end(), p) == out.end())
                    out.push_back (p);
    return out;
}

float vcaGain (const Song& song, int track)
{
    if (song.vcas.empty())
        return 1.0f;
    auto g = 1.0f;
    for (const auto v : vcasOf (song, track))
        g *= faderGain (song.vcas[(std::size_t) v].level);
    return g;
}

bool vcaMuted (const Song& song, int track)
{
    if (song.vcas.empty())
        return false;
    const auto list = vcasOf (song, track);
    return std::any_of (list.begin(), list.end(), [&song] (int v) { return song.vcas[(std::size_t) v].muted; });
}

int assignToVca (Song& song, const std::vector<int>& tracks, int vca)
{
    if (vca < 0 || vca >= (int) song.vcas.size())
        return 0;
    auto& members = song.vcas[(std::size_t) vca].members;
    int changed = 0;
    for (const auto t : tracks)
        if (isInsert (song, t) && ! std::binary_search (members.begin(), members.end(), t))
        {
            members.insert (std::lower_bound (members.begin(), members.end(), t), t);
            ++changed;
        }
    return changed;
}

int removeFromVca (Song& song, const std::vector<int>& tracks, int vca)
{
    if (vca < 0 || vca >= (int) song.vcas.size())
        return 0;
    auto& members = song.vcas[(std::size_t) vca].members;
    const auto before = members.size();
    members.erase (std::remove_if (members.begin(), members.end(),
                                   [&tracks] (int m) { return std::find (tracks.begin(), tracks.end(), m) != tracks.end(); }),
                   members.end());
    return (int) (before - members.size());
}

int createVca (Song& song, const std::vector<int>& tracks, const std::string& name)
{
    Vca vca;
    vca.name = name.empty() ? "VCA " + std::to_string (song.vcas.size() + 1) : name;
    song.vcas.push_back (vca);
    const auto index = (int) song.vcas.size() - 1;
    assignToVca (song, tracks, index);
    return index;
}

bool deleteVca (Song& song, int vca)
{
    if (vca < 0 || vca >= (int) song.vcas.size())
        return false;
    song.vcas.erase (song.vcas.begin() + vca);
    for (auto& v : song.vcas)
        v.parent = v.parent == vca ? -1 : v.parent > vca ? v.parent - 1 : v.parent;
    song.forEachTarget ([vca] (AutomationTarget& t) {
        if (t.kind != AutomationTargetKind::vcaLevel)
            return;
        if (t.index == vca)
            t = AutomationTarget {};
        else if (t.index > vca)
            --t.index;
    });
    song.removeDeadMidiLinks();
    return true;
}

std::vector<int> vcaReach (const Song& song, int vca)
{
    std::vector<int> out;
    for (int t = 1; t < (int) song.mixer.size(); ++t)
    {
        const auto list = vcasOf (song, t);
        if (std::find (list.begin(), list.end(), vca) != list.end())
            out.push_back (t);
    }
    return out;
}

bool mergeVcaIntoMembers (Song& song, int vca)
{
    if (vca < 0 || vca >= (int) song.vcas.size())
        return false;
    const auto gain = faderGain (song.vcas[(std::size_t) vca].level);
    if (gain == 1.0f)
        return false;
    auto scaled = [gain] (float position) { return std::clamp (faderPosition (faderGain (position) * gain), 0.0f, 1.0f); };
    for (const auto t : vcaReach (song, vca))
    {
        auto& m = song.mixer[(std::size_t) t];
        m.volume = scaled (m.volume);
        for (auto& a : song.automations) // the member's own volume automation carries the gain too
            if (a.target.kind == AutomationTargetKind::mixerVolume && a.target.index == t)
                for (auto& p : a.points)
                    p.value = scaled (p.value);
    }
    song.vcas[(std::size_t) vca].level = unityFader;
    return true;
}

namespace
{
int linkedInsert (const Song& song, const PlaylistTrack& pt)
{
    if (pt.mode == PlaylistTrackMode::audio)
        return pt.mixerTrack;
    if (pt.mode == PlaylistTrackMode::instrument)
        return pt.channel >= 0 && pt.channel < (int) song.channels.size() ? song.channels[(std::size_t) pt.channel].mixerTrack : pt.mixerTrack;
    return -1;
}
} // namespace

std::vector<int> playlistOrderInserts (const Song& song)
{
    std::vector<int> out;
    for (const auto& pt : song.playlistTracks)
        if (const auto t = linkedInsert (song, pt); t > 0 && t < (int) song.mixer.size() && std::find (out.begin(), out.end(), t) == out.end())
            out.push_back (t);
    return out;
}

std::vector<PlaylistGroupInserts> playlistGroupInserts (const Song& song)
{
    std::vector<PlaylistGroupInserts> groups;
    std::vector<int> seen;
    const auto& tracks = song.playlistTracks;
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        if (tracks[i].grouped)
            continue; // (a member: handled with its header)
        auto end = i + 1;
        while (end < tracks.size() && tracks[end].grouped)
            ++end;
        if (end == i + 1)
            continue; // an ungrouped track on its own
        PlaylistGroupInserts g;
        g.collapsed = tracks[i].collapsed;
        for (auto k = i; k < end; ++k)
            if (const auto t = linkedInsert (song, tracks[k]); t > 0 && t < (int) song.mixer.size() && std::find (seen.begin(), seen.end(), t) == seen.end())
            {
                seen.push_back (t);
                g.inserts.push_back (t);
            }
        if (! g.inserts.empty())
            groups.push_back (std::move (g));
    }
    return groups;
}

namespace
{
// the separator / fold flags followPlaylistGroups wants, per mixer track
void followedFlags (const Song& song, std::vector<bool>& separator, std::vector<bool>& folded)
{
    const auto count = song.mixer.size();
    separator.assign (count, false);
    folded.assign (count, false);
    for (std::size_t t = 0; t < count; ++t)
    {
        separator[t] = song.mixer[t].separator;
        folded[t] = song.mixer[t].folded;
    }
    for (const auto t : playlistOrderInserts (song)) // the follow owns these
        separator[(std::size_t) t] = folded[(std::size_t) t] = false;

    const auto order = displayOrder (song);
    const auto at = [&order] (int t) { return (int) (std::find (order.begin(), order.end(), t) - order.begin()); };
    for (const auto& g : playlistGroupInserts (song))
    {
        if (g.inserts.size() < 2)
            continue;
        auto positions = g.inserts;
        for (auto& p : positions)
            p = at (p);
        std::sort (positions.begin(), positions.end());
        const auto dock = song.mixer[(std::size_t) order[(std::size_t) positions.front()]].dock;
        auto runs = positions.back() - positions.front() + 1 == (int) positions.size();
        for (const auto p : positions)
            runs = runs && song.mixer[(std::size_t) order[(std::size_t) p]].dock == dock;
        if (! runs)
            continue; // not side by side: "Arrange inserts like the Playlist" puts them together
        const auto first = order[(std::size_t) positions.front()];
        separator[(std::size_t) first] = true;
        folded[(std::size_t) first] = g.collapsed;
        if (const auto next = positions.back() + 1; next < (int) order.size() && order[(std::size_t) next] > 0
                                                     && song.mixer[(std::size_t) order[(std::size_t) next]].dock == dock)
            separator[(std::size_t) order[(std::size_t) next]] = true;
    }
}
} // namespace

bool followsPlaylistGroups (const Song& song)
{
    std::vector<bool> separator, folded;
    followedFlags (song, separator, folded);
    for (std::size_t t = 1; t < song.mixer.size(); ++t)
        if (song.mixer[t].separator != separator[t] || song.mixer[t].folded != folded[t])
            return false;
    return true;
}

bool followPlaylistGroups (Song& song)
{
    std::vector<bool> separator, folded;
    followedFlags (song, separator, folded);
    auto changed = false;
    for (std::size_t t = 1; t < song.mixer.size(); ++t)
    {
        auto& m = song.mixer[t];
        changed = changed || m.separator != separator[t] || m.folded != folded[t];
        m.separator = separator[t];
        m.folded = folded[t];
    }
    return changed;
}

int arrangeLikePlaylist (Song& song)
{
    const auto linked = playlistOrderInserts (song);
    std::vector<int> wanted = linked; // the new order of inserts 1..n: the linked ones, then the rest as they are
    for (int t = 1; t < (int) song.mixer.size(); ++t)
        if (std::find (linked.begin(), linked.end(), t) == linked.end())
            wanted.push_back (t);

    // place by place, the wanted insert swaps in (swapTracks carries every reference along)
    std::vector<int> now (song.mixer.size()); // now[t] = which original insert sits at t
    std::iota (now.begin(), now.end(), 0);
    int moved = 0;
    for (int place = 1; place <= (int) wanted.size(); ++place)
    {
        const auto from = (int) (std::find (now.begin() + 1, now.end(), wanted[(std::size_t) place - 1]) - now.begin());
        if (from == place)
            continue;
        swapTracks (song, place, from);
        std::swap (now[(std::size_t) place], now[(std::size_t) from]);
    }
    for (int t = 1; t < (int) now.size(); ++t)
        moved += now[(std::size_t) t] != t ? 1 : 0;
    followPlaylistGroups (song);
    return moved;
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

std::uint32_t gradientColourStep (std::uint32_t from, std::uint32_t to, int k, int n) noexcept
{
    if (n < 2)
        return from;
    const auto amount = (k << 8) / (n - 1); // FL: shl 8, idiv
    std::uint32_t out = 0xff000000u;
    for (int shift = 0; shift <= 16; shift += 8)
    {
        const auto a = (int) ((from >> shift) & 0xffu), b = (int) ((to >> shift) & 0xffu);
        // FL blends with pmulhw on (b - a) * 16 and amount * 16: the high word, an arithmetic shift (floor)
        const auto c = a + (((b - a) * 16 * (amount * 16)) >> 16);
        out |= (std::uint32_t) std::clamp (c, 0, 255) << shift;
    }
    return out;
}

int gradientColour (Song& song, std::vector<int> tracks, std::uint32_t from, std::uint32_t to)
{
    std::sort (tracks.begin(), tracks.end());
    tracks.erase (std::unique (tracks.begin(), tracks.end()), tracks.end());
    tracks.erase (std::remove_if (tracks.begin(), tracks.end(), [&] (int t) { return t < 0 || t >= (int) song.mixer.size(); }),
                  tracks.end());
    const auto n = (int) tracks.size();
    if (n < 2)
        return 0;
    int changed = 0;
    for (int k = 0; k < n; ++k)
    {
        auto& colour = song.mixer[(std::size_t) tracks[(std::size_t) k]].colour;
        const auto next = gradientColourStep (from, to, k, n);
        if (colour != next)
        {
            colour = next;
            ++changed;
        }
    }
    return changed;
}

namespace
{
constexpr int audioTrackScan = 500; // FL: playlist tracks 1..500

std::vector<int> sortedInserts (const Song& song, std::vector<int> inserts)
{
    std::sort (inserts.begin(), inserts.end());
    inserts.erase (std::unique (inserts.begin(), inserts.end()), inserts.end());
    inserts.erase (std::remove_if (inserts.begin(), inserts.end(), [&] (int t) { return t <= 0 || t >= (int) song.mixer.size(); }),
                   inserts.end());
    return inserts;
}

// FL skips an insert that any Playlist track already points at (0x12a76e0: any mode), or whose one and only channel
// already has an instrument track (0x12a7620 with 0xffac90 / 0xffae00; audio clip channels don't count).
bool hasPlaylistTrack (const Song& song, int insert)
{
    for (const auto& pt : song.playlistTracks)
        if (pt.mode != PlaylistTrackMode::unassigned && pt.mixerTrack == insert)
            return true;
    int sole = -1, count = 0;
    for (int c = 0; c < (int) song.channels.size(); ++c)
        if (song.channels[(std::size_t) c].mixerTrack == insert)
        {
            sole = c;
            ++count;
        }
    if (count != 1 || song.channels[(std::size_t) sole].instrument == InstrumentKind::audioClip)
        return false;
    return std::any_of (song.playlistTracks.begin(), song.playlistTracks.end(), [sole] (const PlaylistTrack& pt) {
        return pt.mode == PlaylistTrackMode::instrument && pt.channel == sole;
    });
}

int audioTrackOf (const Song& song, int insert)
{
    for (int i = 0; i < (int) song.playlistTracks.size(); ++i)
    {
        const auto& pt = song.playlistTracks[(std::size_t) i];
        if (pt.mode == PlaylistTrackMode::audio && pt.mixerTrack == insert)
            return i;
    }
    return -1;
}
} // namespace

int assignNewAudioTracks (Song& song, std::vector<int> inserts)
{
    int assigned = 0;
    for (auto t : sortedInserts (song, std::move (inserts)))
    {
        if (hasPlaylistTrack (song, t))
            continue;
        for (int i = 0; i < audioTrackScan; ++i)
        {
            const auto untouched = i >= (int) song.playlistTracks.size()
                                   || (song.playlistTracks[(std::size_t) i].name.empty() && song.playlistTracks[(std::size_t) i].colour == 0
                                       && song.playlistTracks[(std::size_t) i].mode == PlaylistTrackMode::unassigned);
            if (! untouched || std::any_of (song.playlist.begin(), song.playlist.end(), [i] (const Clip& c) { return c.track == i; }))
                continue;
            if ((int) song.playlistTracks.size() <= i)
                song.playlistTracks.resize ((std::size_t) i + 1);
            auto& pt = song.playlistTracks[(std::size_t) i];
            const auto& insert = song.mixer[(std::size_t) t];
            pt.mode = PlaylistTrackMode::audio;
            pt.mixerTrack = t;
            pt.name = insert.name.empty() ? "Insert " + std::to_string (t) : insert.name;
            pt.colour = insert.colour;
            ++assigned;
            break;
        }
    }
    return assigned;
}

int unassignAudioTracks (Song& song, std::vector<int> inserts)
{
    int changed = 0;
    for (auto t : sortedInserts (song, std::move (inserts)))
        if (const auto i = audioTrackOf (song, t); i >= 0)
        {
            auto& pt = song.playlistTracks[(std::size_t) i];
            pt.mode = PlaylistTrackMode::unassigned;
            pt.mixerTrack = -1;
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
        c.sampler.declickMode = audioClipDeclickMode; // FL's AudioTrack.fst
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

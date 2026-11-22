#include "core/TrackImport.h"

#include <algorithm>
#include <map>
#include <set>

#include "core/ClipLauncher.h"
#include "core/Comping.h"
#include "core/Groove.h"
#include "core/MixerOps.h"
#include "core/NoteEdit.h"
#include "core/RackOps.h"
#include "core/SessionIds.h"

namespace lynx::trackimport
{
namespace
{
using IndexMap = std::map<int, int>;

int mapped (const IndexMap& map, int index) noexcept
{
    const auto it = map.find (index);
    return it != map.end() ? it->second : -1;
}

std::string shownName (const Song& song, int track)
{
    if (track >= 0 && track < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) track].name.empty())
        return song.playlistTracks[(std::size_t) track].name;
    return "Track " + std::to_string (track + 1);
}

bool isSampleInstrument (InstrumentKind k) noexcept
{
    return k == InstrumentKind::sampler || k == InstrumentKind::audioClip || k == InstrumentKind::granulizer || k == InstrumentKind::wavetraveller
           || k == InstrumentKind::fruityslicer || k == InstrumentKind::slicex || k == InstrumentKind::directwave;
}

void channelsOfPattern (const Pattern& p, std::set<int>& out)
{
    for (const auto& n : p.notes)
        out.insert (n.channel);
    for (const auto& l : p.channelLoops)
        out.insert (l.channel);
    for (const auto& lane : p.events)
        if (refs::targetsChannel (lane.target))
            out.insert (lane.target.index);
}

void channelsOfAutomation (const Automation& a, std::set<int>& out)
{
    if (refs::targetsChannel (a.target))
        out.insert (a.target.index);
    for (const auto& link : a.links)
        if (refs::targetsChannel (link.target))
            out.insert (link.target.index);
    if (a.source.channel >= 0)
        out.insert (a.source.channel);
}

// A target that comes in: a channel's follows the channel (gone: none); an insert's was renumbered before; the tempo, the
// Master, VCAs, the crossfader and modulators are the open project's own and are not taken over
bool targetComesIn (AutomationTarget& t, const IndexMap& channels)
{
    if (t.kind == AutomationTargetKind::none)
        return false;
    if (refs::targetsChannel (t))
    {
        t.index = mapped (channels, t.index);
        if (t.index < 0)
            t = {};
        return t.kind != AutomationTargetKind::none;
    }
    if (refs::targetsInsert (t))
        return t.index > 0; // (the Master stays the open project's)
    t = {};
    return false;
}

// Every clip list of one track: the Playlist's (the current arrangement), the grid's and its versions'
template <typename Fn>
void forEachClipOf (const Song& song, int track, Fn&& fn)
{
    for (const auto& c : song.playlist)
        if (c.track == track)
            fn (c);
    if (song.clipGrid)
        for (const auto& c : song.clipGrid->clips)
            if (c.track == track)
                fn (c);
    if (track >= 0 && track < (int) song.playlistTracks.size())
    {
        const auto& t = song.playlistTracks[(std::size_t) track];
        for (std::size_t v = 0; v < t.versions.size(); ++v)
            if ((int) v != t.activeVersion)
                for (const auto& c : t.versions[v].clips)
                    fn (c);
    }
}

// The automations a track's lanes play (its versions' too)
template <typename Fn>
void forEachLaneOf (const Song& song, int track, Fn&& fn)
{
    if (track < 0 || track >= (int) song.playlistTracks.size())
        return;
    const auto& t = song.playlistTracks[(std::size_t) track];
    for (const auto& l : t.lanes)
        fn (l.automation);
    for (std::size_t v = 0; v < t.versions.size(); ++v)
        if ((int) v != t.activeVersion)
            for (const auto& l : t.versions[v].lanes)
                fn (l.automation);
}

// an arriving channel: its own instance, no uid, no frozen audio, not a Simple pad or a Loop Starter channel
ChannelDef arriving (ChannelDef c)
{
    c.uid = 0;
    c.instance.renew();
    c.freeze = {};
    c.simplePad = {};
    c.loopStarterPattern = -1;
    c.loopStarterSlot.clear();
    c.loopStarterCategory.clear();
    return c;
}

int groupIn (Song& song, const Song& source, int group)
{
    if (group < 0 || group >= (int) source.channelGroups.size())
        return -1;
    const auto& name = source.channelGroups[(std::size_t) group];
    const auto it = std::find (song.channelGroups.begin(), song.channelGroups.end(), name);
    if (it != song.channelGroups.end())
        return (int) (it - song.channelGroups.begin());
    song.channelGroups.push_back (name);
    return (int) song.channelGroups.size() - 1;
}

bool sameGroove (Groove a, Groove b)
{
    a.id = b.id = 0;
    return a == b;
}

// a header that says something of its own: a name, lanes, a track mode, a group, takes, versions (an FL project keeps a
// colour and a size for each of its 500 tracks: those alone don't count)
bool meaningful (const PlaylistTrack& t)
{
    return ! t.name.empty() || ! t.lanes.empty() || t.mode != PlaylistTrackMode::unassigned || t.grouped || t.compTrack || t.takeLane
           || ! t.versions.empty();
}

// the last Playlist track of `song` with anything on it or in its header; -1 for none
int lastUsedTrack (const Song& song)
{
    int last = -1;
    song.forEachClipList ([&last] (const std::vector<Clip>& clips) {
        for (const auto& c : clips)
            last = std::max (last, c.track);
    });
    for (int t = (int) song.playlistTracks.size() - 1; t > last; --t)
        if (meaningful (song.playlistTracks[(std::size_t) t]))
        {
            last = t;
            break;
        }
    return last;
}

// a free insert of `song` none of `claimed` is, else a new one at the end (Master-only mixers and full ones: -1)
int freeInsert (Song& song, std::set<int>& claimed)
{
    if (song.mixer.empty())
        return -1;
    const auto current = mixerops::currentTrack (song);
    for (int t = 1; t < (int) song.mixer.size(); ++t)
        if (t != current && ! claimed.count (t) && ! song.insertInUse (t))
        {
            claimed.insert (t);
            return t;
        }
    if ((int) song.mixer.size() >= maxMixerTracks)
        return -1;
    MixerTrack added;
    added.name = "Insert " + std::to_string (song.mixer.size());
    if (current > 0) // (FL's Current track stays the last one)
    {
        song.mixer.insert (song.mixer.begin() + current, std::move (added));
        mixerops::keepCurrentLast (song);
        claimed.insert (current);
        return current;
    }
    song.mixer.push_back (std::move (added));
    claimed.insert ((int) song.mixer.size() - 1);
    return (int) song.mixer.size() - 1;
}

bool defaultInsertName (const std::string& name, int t)
{
    return name.empty() || name == "Insert " + std::to_string (t);
}
} // namespace

std::vector<SourceTrack> listTracks (const Song& source)
{
    std::vector<SourceTrack> out;
    int rows = (int) source.playlistTracks.size();
    source.forEachClipList ([&rows] (const std::vector<Clip>& clips) {
        for (const auto& c : clips)
            if (c.track < 10000)
                rows = std::max (rows, c.track + 1);
    });
    std::vector<SourceTrack> all ((std::size_t) rows);
    for (int t = 0; t < rows; ++t)
    {
        auto& s = all[(std::size_t) t];
        s.track = t;
        s.name = shownName (source, t);
        if (t < (int) source.playlistTracks.size())
        {
            const auto& pt = source.playlistTracks[(std::size_t) t];
            s.colour = pt.colour;
            s.compTrack = pt.compTrack;
            s.takeLane = pt.takeLane || comp::isTakeLane (source, t);
            s.lanes = (int) pt.lanes.size();
            s.versions = (int) pt.versions.size();
            s.depth = noteedit::folderDepth (source, t);
            if (pt.mixerTrack > 0 && pt.mixerTrack < (int) source.mixer.size())
                s.insert = source.mixer[(std::size_t) pt.mixerTrack].name.empty() ? "Insert " + std::to_string (pt.mixerTrack)
                                                                                    : source.mixer[(std::size_t) pt.mixerTrack].name;
        }
        s.folder = t + 1 < (int) source.playlistTracks.size() && source.playlistTracks[(std::size_t) t + 1].grouped
                   && noteedit::folderDepth (source, t + 1) > s.depth;
    }
    for (const auto& c : source.playlist)
        if (c.track >= 0 && c.track < rows)
        {
            auto& s = all[(std::size_t) c.track];
            (c.kind == ClipKind::pattern ? s.patternClips : c.kind == ClipKind::audio ? s.audioClips : s.automationClips)++;
            if (s.insert.empty() && c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) source.channels.size())
                if (const auto m = source.channels[(std::size_t) c.channel].mixerTrack; m > 0 && m < (int) source.mixer.size())
                    s.insert = source.mixer[(std::size_t) m].name.empty() ? "Insert " + std::to_string (m) : source.mixer[(std::size_t) m].name;
        }
    if (source.clipGrid)
        for (const auto& c : source.clipGrid->clips)
            if (c.track >= 0 && c.track < rows && c.length > 0)
                ++all[(std::size_t) c.track].gridClips;
    for (int t = 0; t < rows; ++t)
    {
        const auto& s = all[(std::size_t) t];
        const auto header = t < (int) source.playlistTracks.size() && meaningful (source.playlistTracks[(std::size_t) t]);
        if (s.patternClips + s.audioClips + s.automationClips + s.gridClips + s.lanes + s.versions > 0 || header)
            out.push_back (s);
    }
    return out;
}

std::vector<SourceChannel> looseChannels (const Song& source)
{
    std::set<int> used;
    source.forEachClipList ([&] (const std::vector<Clip>& clips) {
        for (const auto& c : clips)
        {
            if (c.kind == ClipKind::audio)
                used.insert (c.channel);
            else if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) source.patterns.size())
                channelsOfPattern (source.patterns[(std::size_t) c.pattern], used);
        }
    });
    for (const auto& t : source.playlistTracks)
        if (t.channel >= 0)
            used.insert (t.channel);
    std::vector<SourceChannel> out;
    for (int c = 0; c < (int) source.channels.size(); ++c)
        if (! used.count (c) && source.channels[(std::size_t) c].instrument != InstrumentKind::audioClip)
            out.push_back ({ c, source.channels[(std::size_t) c].name });
    return out;
}

std::vector<int> expandSelection (const Song& source, std::vector<int> tracks)
{
    std::set<int> set (tracks.begin(), tracks.end());
    const auto rows = (int) source.playlistTracks.size();
    for (const auto t : tracks)
    {
        if (t < 0 || t >= rows)
            continue;
        // a comp track's lanes, a folder's members: the tracks after it deeper than it, up to the next one that is not
        const auto depth = noteedit::folderDepth (source, t);
        const auto header = source.playlistTracks[(std::size_t) t].compTrack
                            || (t + 1 < rows && source.playlistTracks[(std::size_t) t + 1].grouped && noteedit::folderDepth (source, t + 1) > depth);
        if (! header)
            continue;
        for (int k = t + 1; k < rows && noteedit::folderDepth (source, k) > depth; ++k)
            set.insert (k);
    }
    return { set.begin(), set.end() };
}

int restoreMissingPaths (Song& source, const std::vector<std::pair<std::string, std::vector<std::string>>>& missing)
{
    int n = 0;
    for (const auto& [path, users] : missing)
        for (const auto& user : users)
            for (auto& c : source.channels)
                if (c.samplePath.empty() && isSampleInstrument (c.instrument) && (user == c.name || user == "Audio clip: " + c.name))
                {
                    c.samplePath = path;
                    ++n;
                }
    return n;
}

Result importTracks (Song& song, const Song& input, const std::vector<int>& chosen, const Options& options)
{
    Result result;
    const auto tracks = expandSelection (input, chosen);
    if (tracks.empty() && options.channels.empty())
        return result;
    const std::set<int> trackSet (tracks.begin(), tracks.end());

    // ---- what comes in, in the other project's numbers
    std::set<int> channels (options.channels.begin(), options.channels.end());
    std::set<int> patterns, automations;
    std::vector<std::pair<int, int>> extraAutomationClips; // (playlist index, source track) of automation clips on other tracks
    for (const auto t : tracks)
    {
        forEachClipOf (input, t, [&] (const Clip& c) {
            if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) input.patterns.size())
            {
                patterns.insert (c.pattern);
                channelsOfPattern (input.patterns[(std::size_t) c.pattern], channels);
            }
            else if (c.kind == ClipKind::audio && c.channel >= 0)
                channels.insert (c.channel);
            else if (c.kind == ClipKind::automation && options.withAutomation && c.automation >= 0 && c.automation < (int) input.automations.size())
            {
                automations.insert (c.automation);
                channelsOfAutomation (input.automations[(std::size_t) c.automation], channels);
            }
        });
        if (t < (int) input.playlistTracks.size() && input.playlistTracks[(std::size_t) t].channel >= 0)
            channels.insert (input.playlistTracks[(std::size_t) t].channel);
        if (options.withAutomation)
            forEachLaneOf (input, t, [&] (int a) {
                if (a >= 0 && a < (int) input.automations.size())
                    automations.insert (a);
            });
    }
    for (auto it = channels.begin(); it != channels.end();)
        it = *it < 0 || *it >= (int) input.channels.size() ? channels.erase (it) : std::next (it);
    // layer children of a layer that comes in
    for (bool grew = true; grew;)
    {
        grew = false;
        for (const auto c : std::vector<int> (channels.begin(), channels.end()))
            for (const auto k : input.channels[(std::size_t) c].layer.children)
                if (k >= 0 && k < (int) input.channels.size() && channels.insert (k).second)
                    grew = true;
    }

    // ---- the inserts: those the channels and tracks play through, then whatever they send to
    std::set<int> inserts;
    int sidechainsCut = 0;
    if (options.withInserts && ! song.mixer.empty())
    {
        for (const auto c : channels)
            if (const auto m = input.channels[(std::size_t) c].mixerTrack; m > 0 && m < (int) input.mixer.size())
                inserts.insert (m);
        for (const auto t : tracks)
            if (t < (int) input.playlistTracks.size())
                if (const auto m = input.playlistTracks[(std::size_t) t].mixerTrack; m > 0 && m < (int) input.mixer.size())
                    inserts.insert (m);
        for (bool grew = true; grew;)
        {
            grew = false;
            for (const auto m : std::vector<int> (inserts.begin(), inserts.end()))
            {
                const auto& mt = input.mixer[(std::size_t) m];
                for (const auto& s : mt.sends)
                    if (s.target > 0 && s.target < (int) input.mixer.size() && ! input.mixer[(std::size_t) s.target].current && inserts.insert (s.target).second)
                        grew = true;
                for (const auto& slot : mt.effects)
                    for (const auto t : slot.sendTracks)
                        if (t > 0 && t < (int) input.mixer.size() && ! input.mixer[(std::size_t) t].current && inserts.insert (t).second)
                            grew = true;
            }
        }
        inserts.erase (mixerops::currentTrack (input));
    }
    else if (options.withInserts)
        result.notes.push_back ("The open project has no mixer: every imported channel plays straight to the output");

    // automation clips elsewhere that drive what comes in (on tracks of their own)
    std::set<int> channelSet = channels;
    if (options.withAutomation)
        for (int i = 0; i < (int) input.playlist.size(); ++i)
        {
            const auto& c = input.playlist[(std::size_t) i];
            if (c.kind != ClipKind::automation || trackSet.count (c.track) || c.automation < 0 || c.automation >= (int) input.automations.size())
                continue;
            const auto& a = input.automations[(std::size_t) c.automation];
            const auto drives = [&] (const AutomationTarget& t) {
                return (refs::targetsChannel (t) && channelSet.count (t.index)) || (refs::targetsInsert (t) && t.index > 0 && inserts.count (t.index));
            };
            auto hit = drives (a.target);
            for (const auto& l : a.links)
                hit = hit || drives (l.target);
            if (hit)
            {
                extraAutomationClips.push_back ({ i, c.track });
                automations.insert (c.automation);
            }
        }

    // ---- the inserts get their numbers in the open project; the other project is renumbered to them
    auto source = input;
    IndexMap insertMap;
    std::set<int> claimed;
    for (const auto m : inserts)
    {
        const auto d = freeInsert (song, claimed);
        if (d < 0)
        {
            result.notes.push_back ("The mixer is full: " + (input.mixer[(std::size_t) m].name.empty() ? "Insert " + std::to_string (m) : input.mixer[(std::size_t) m].name)
                                    + " was not imported (its channels play to the Master)");
            continue;
        }
        insertMap[m] = d;
    }
    for (const auto m : inserts)
        for (const auto& slot : input.mixer[(std::size_t) m].effects)
            for (const auto sc : { slot.sidechain, slot.sidechain2 })
                if (sc > 0 && ! insertMap.count (sc))
                    ++sidechainsCut;
    mixerops::remapInsertReferences (source, [&insertMap] (int t) { return t == 0 ? 0 : mapped (insertMap, t); });
    for (const auto& [from, to] : insertMap)
    {
        auto track = source.mixer[(std::size_t) from];
        if (defaultInsertName (track.name, from))
            track.name = "Insert " + std::to_string (to);
        track.current = false;
        for (auto& slot : track.effects)
            slot.instance.renew();
        song.mixer[(std::size_t) to] = std::move (track);
        result.inserts.push_back (to);
    }
    if (sidechainsCut > 0)
        result.notes.push_back (std::to_string (sidechainsCut) + " sidechain input(s) from tracks that were not imported left empty");

    // ---- channels, at the end of the Channel Rack
    IndexMap channelMap;
    for (const auto c : channels)
    {
        auto copy = arriving (source.channels[(std::size_t) c]);
        copy.group = groupIn (song, source, copy.group);
        copy.layer.children.clear();
        if (song.mixer.empty() || copy.mixerTrack < 0 || copy.mixerTrack >= (int) song.mixer.size())
            copy.mixerTrack = 0;
        channelMap[c] = insertChannel (song, (int) song.channels.size(), std::move (copy));
        result.channels.push_back (channelMap[c]);
    }
    for (const auto c : channels)
        for (const auto k : source.channels[(std::size_t) c].layer.children)
            if (const auto m = mapped (channelMap, k); m >= 0 && m != channelMap[c])
                song.channels[(std::size_t) channelMap[c]].layer.children.push_back (m);

    // ---- patterns (with their event lanes), automations, clip envelopes, grooves
    IndexMap patternMap;
    for (const auto i : patterns)
    {
        auto p = source.patterns[(std::size_t) i];
        for (auto& n : p.notes)
            n.channel = mapped (channelMap, n.channel);
        p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(), [] (const Note& n) { return n.channel < 0; }), p.notes.end());
        for (auto& l : p.channelLoops)
            l.channel = mapped (channelMap, l.channel);
        p.channelLoops.erase (std::remove_if (p.channelLoops.begin(), p.channelLoops.end(), [] (const ChannelLoop& l) { return l.channel < 0; }),
                              p.channelLoops.end());
        for (auto& lane : p.events)
            if (! targetComesIn (lane.target, channelMap))
                lane.target = {};
        p.events.erase (std::remove_if (p.events.begin(), p.events.end(), [] (const PatternEvents& e) { return e.target.kind == AutomationTargetKind::none; }),
                        p.events.end());
        p.autoNotes.clear(); // (filled below, once the automations have their numbers here)
        p.uid = 0;
        p.sortNotes();
        patternMap[i] = (int) song.patterns.size();
        song.patterns.push_back (std::move (p));
    }
    result.patterns = (int) patternMap.size();

    IndexMap automationMap;
    int automationsLeftOut = 0;
    for (const auto i : automations)
    {
        auto a = source.automations[(std::size_t) i];
        auto keep = targetComesIn (a.target, channelMap);
        for (auto& link : a.links)
            if (! targetComesIn (link.target, channelMap))
                link.target = {};
        a.links.erase (std::remove_if (a.links.begin(), a.links.end(), [] (const auto& l) { return l.target.kind == AutomationTargetKind::none; }), a.links.end());
        keep = keep || ! a.links.empty();
        if (! keep)
        {
            ++automationsLeftOut;
            continue;
        }
        if (a.source.channel >= 0)
            a.source.channel = mapped (channelMap, a.source.channel);
        if (a.source.track > 0 && ! insertMap.empty() && std::none_of (insertMap.begin(), insertMap.end(), [&a] (const auto& m) { return m.second == a.source.track; }))
            a.source.track = -1;
        a.uid = 0;
        a.rackSlot = -1;
        automationMap[i] = (int) song.automations.size();
        song.automations.push_back (std::move (a));
    }
    result.automations = (int) automationMap.size();
    // the automation clip steps of the patterns (round 190ow's rows: Note::channel = the automation)
    for (const auto& [from, to] : patternMap)
    {
        auto& p = song.patterns[(std::size_t) to];
        for (auto n : source.patterns[(std::size_t) from].autoNotes)
            if ((n.channel = mapped (automationMap, n.channel)) >= 0)
                p.autoNotes.push_back (n);
        if (! p.autoNotes.empty())
            p.sortNotes();
    }
    if (automationsLeftOut > 0)
        result.notes.push_back (std::to_string (automationsLeftOut)
                                + " automation(s) of the tempo, the Master or something that was not imported left out (the open project keeps its own)");

    IndexMap envelopeMap;
    std::map<std::uint32_t, std::uint32_t> grooveMap;
    const auto envelopeOf = [&] (int e) {
        if (e < 0 || e >= (int) source.clipEnvelopes.size())
            return -1;
        if (! envelopeMap.count (e))
        {
            envelopeMap[e] = (int) song.clipEnvelopes.size();
            song.clipEnvelopes.push_back (source.clipEnvelopes[(std::size_t) e]);
        }
        return envelopeMap[e];
    };
    const auto grooveOf = [&] (std::uint32_t id) -> std::uint32_t {
        if (id == 0)
            return 0;
        if (const auto it = grooveMap.find (id); it != grooveMap.end())
            return it->second;
        const auto* g = groove::find (source.grooves, id);
        if (g == nullptr)
            return 0;
        const auto same = std::find_if (song.grooves.grooves.begin(), song.grooves.grooves.end(), [g] (const Groove& own) { return sameGroove (own, *g); });
        return grooveMap[id] = same != song.grooves.grooves.end() ? same->id : groove::add (song.grooves, *g);
    };
    // a clip of the other project with the open project's numbers (false: what it plays did not come in)
    const auto clipComesIn = [&] (Clip& c) {
        c.uid = 0;
        if (c.kind == ClipKind::pattern)
        {
            c.pattern = mapped (patternMap, c.pattern);
            c.groove = grooveOf (c.groove);
            return c.pattern >= 0;
        }
        if (c.kind == ClipKind::audio)
        {
            c.channel = mapped (channelMap, c.channel);
            c.envelope = envelopeOf (c.envelope);
            return c.channel >= 0;
        }
        c.automation = mapped (automationMap, c.automation);
        return c.automation >= 0;
    };

    // ---- the tracks, under the last used one
    const auto base = lastUsedTrack (song) + 1;
    IndexMap trackMap;
    for (std::size_t k = 0; k < tracks.size(); ++k)
        trackMap[tracks[k]] = base + (int) k;
    std::map<int, int> extraTracks; // source track of extra automation clips -> new track
    for (const auto& [clip, from] : extraAutomationClips)
        if (! extraTracks.count (from))
        {
            const auto at = base + (int) tracks.size() + (int) extraTracks.size();
            extraTracks[from] = at;
        }
    const auto rows = base + (int) tracks.size() + (int) extraTracks.size();
    if ((int) song.playlistTracks.size() < rows)
        song.playlistTracks.resize ((std::size_t) rows);

    for (const auto t : tracks)
    {
        const auto to = trackMap[t];
        auto header = t < (int) source.playlistTracks.size() ? source.playlistTracks[(std::size_t) t] : PlaylistTrack {};
        header.name = shownName (source, t);
        header.hidden = false;
        header.soloLock = false;
        // a group stays one only with its header
        if (header.grouped)
        {
            const auto h = noteedit::groupHeaderOf (source, t);
            if (h < 0 || ! trackSet.count (h))
                header.grouped = false, header.nest = 0;
        }
        if (header.channel >= 0)
            header.channel = mapped (channelMap, header.channel);
        if (header.mode == PlaylistTrackMode::instrument && header.channel < 0)
            header.mode = PlaylistTrackMode::unassigned;
        if (header.mixerTrack > 0 && header.mixerTrack >= (int) song.mixer.size())
            header.mixerTrack = -1;
        if (header.mode == PlaylistTrackMode::audio && header.mixerTrack <= 0)
            header.mode = PlaylistTrackMode::unassigned;
        std::vector<AutomationLane> lanes;
        for (const auto& l : header.lanes)
            if (const auto a = mapped (automationMap, l.automation); a >= 0)
                lanes.push_back ({ a, l.height });
        header.lanes = std::move (lanes);
        // its versions: their clips, lanes and chains in the open project's numbers, of the arrangement shown here
        for (std::size_t v = 0; v < header.versions.size(); ++v)
        {
            auto& version = header.versions[v];
            std::vector<Clip> clips;
            for (auto c : version.clips)
                if (clipComesIn (c) && (c.kind != ClipKind::automation || options.withAutomation))
                {
                    c.track = to;
                    c.start += options.at;
                    clips.push_back (c);
                }
            version.clips = std::move (clips);
            std::vector<AutomationLane> vl;
            for (const auto& l : version.lanes)
                if (const auto a = mapped (automationMap, l.automation); a >= 0)
                    vl.push_back ({ a, l.height });
            version.lanes = std::move (vl);
            for (auto& slot : version.chain)
                slot.instance.renew();
        }
        header.versionArrangement = song.currentArrangement;
        song.playlistTracks[(std::size_t) to] = std::move (header);
        result.tracks.push_back (to);
    }
    for (const auto& [from, to] : extraTracks)
    {
        auto& header = song.playlistTracks[(std::size_t) to];
        header = {};
        header.name = shownName (source, from);
        if (from < (int) source.playlistTracks.size())
            header.colour = source.playlistTracks[(std::size_t) from].colour;
        result.tracks.push_back (to);
    }

    // ---- the clips
    for (auto c : source.playlist)
    {
        if (! trackSet.count (c.track) || (c.kind == ClipKind::automation && ! options.withAutomation))
            continue;
        const auto to = trackMap[c.track];
        if (! clipComesIn (c))
            continue;
        c.track = to;
        c.start += options.at;
        c.gridSlot = -1;
        song.playlist.push_back (c);
        ++result.clips;
    }
    for (const auto& [index, from] : extraAutomationClips)
    {
        auto c = source.playlist[(std::size_t) index];
        if (! clipComesIn (c))
            continue;
        c.track = extraTracks[from];
        c.start += options.at;
        c.gridSlot = -1;
        song.playlist.push_back (c);
        ++result.clips;
    }
    // the clip grid's slots of the tracks (scene by scene; the open project gets the scenes it lacks)
    if (source.clipGrid)
        for (auto c : source.clipGrid->clips)
        {
            if (! trackSet.count (c.track) || c.gridSlot < 0 || c.gridSlot >= (int) source.clipGrid->slots.size())
                continue;
            const auto slot = source.clipGrid->slots[(std::size_t) c.gridSlot];
            const auto to = trackMap[c.track];
            if (c.length <= 0 || ! clipComesIn (c)) // (an empty slot without its stop button is left out)
                continue;
            auto& grid = launcher::ensureGrid (song);
            while ((int) grid.scenes.size() <= slot.scene)
            {
                const auto s = (int) grid.scenes.size();
                grid.scenes.push_back (s < (int) source.clipGrid->scenes.size() ? source.clipGrid->scenes[(std::size_t) s] : ClipGridScene {});
            }
            auto newSlot = slot;
            newSlot.groove = grooveOf (slot.groove);
            c.track = to;
            c.gridSlot = (int) grid.slots.size();
            grid.slots.push_back (newSlot);
            grid.clips.push_back (c);
            ++result.gridClips;
        }

    // ---- the report
    if (std::abs (input.bpm - song.bpm) > 0.005)
        result.notes.push_back ("The other project's tempo is " + std::to_string ((int) std::lround (input.bpm)) + " BPM; the clips keep their places in beats, at this project's tempo");
    if (input.beatsPerBar != song.beatsPerBar || input.beatUnit != song.beatUnit)
        result.notes.push_back ("The other project's time signature is " + std::to_string (input.beatsPerBar) + "/" + std::to_string (input.beatUnit)
                                + "; this project keeps its own");
    if (input.arrangements.size() > 1)
        result.notes.push_back ("From the other project's arrangement \"" + input.arrangements[(std::size_t) std::clamp (input.currentArrangement, 0, (int) input.arrangements.size() - 1)].name
                                + "\" (the one it opens on)");
    result.ok = ! result.tracks.empty() || ! result.channels.empty();
    return result;
}
} // namespace lynx::trackimport

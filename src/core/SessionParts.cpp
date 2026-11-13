#include "core/SessionParts.h"

#include <algorithm>
#include <map>
#include <unordered_map>

#include "core/SessionIds.h"

namespace lynx::parts
{
std::uint32_t ownerOfChannel (const ChannelDef& c) noexcept { return uids::tagOf (c.uid); }

namespace
{
std::string lower (std::string s)
{
    for (auto& ch : s)
        if (ch >= 'A' && ch <= 'Z')
            ch = (char) (ch - 'A' + 'a');
    return s;
}

// the last Playlist row anything uses (clips of every arrangement, named / coloured rows), -1 if none
int lastUsedRow (const Song& song)
{
    int last = -1;
    song.forEachClipList ([&] (const std::vector<Clip>& list) {
        for (const auto& c : list)
            last = std::max (last, c.track);
    });
    for (int r = 0; r < (int) song.playlistTracks.size(); ++r)
    {
        const auto& t = song.playlistTracks[(std::size_t) r];
        if (! t.name.empty() || t.colour != 0 || t.mode != PlaylistTrackMode::unassigned || t.muted)
            last = std::max (last, r);
    }
    return last;
}

int lastUsedInsert (const Song& song)
{
    int last = 0;
    for (int t = 1; t < (int) song.mixer.size(); ++t)
        if (song.insertInUse (t))
            last = t;
    return last;
}

std::uint64_t key (std::uint64_t uid) { return uid; }

template <typename List>
std::unordered_map<std::uint64_t, int> indexByUid (const List& list)
{
    std::unordered_map<std::uint64_t, int> out;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(std::size_t) i].uid != 0)
            out.emplace (key (list[(std::size_t) i].uid), i);
    return out;
}

int mapped (const std::vector<int>& map, int i) { return i >= 0 && i < (int) map.size() ? map[(std::size_t) i] : -1; }

void remapTarget (AutomationTarget& t, const std::vector<int>& channelMap, const std::map<int, int>& insertMap)
{
    if (refs::targetsChannel (t))
    {
        t.index = mapped (channelMap, t.index);
        if (t.index < 0)
            t.kind = AutomationTargetKind::none, t.index = 0;
    }
    else if (refs::targetsInsert (t))
    {
        if (auto it = insertMap.find (t.index); it != insertMap.end())
            t.index = it->second;
        if (t.kind == AutomationTargetKind::mixerSend)
            if (auto it = insertMap.find (t.slot); it != insertMap.end())
                t.slot = it->second;
    }
}

// the target belongs to participant `tag`'s area of `song` (his channel, or an insert of his range)
bool targetIsTheirs (const Song& song, const AutomationTarget& t, std::uint32_t tag, const SessionParticipant* who)
{
    if (refs::targetsChannel (t))
        return t.index >= 0 && t.index < (int) song.channels.size() && uids::tagOf (song.channels[(std::size_t) t.index].uid) == tag;
    if (refs::targetsInsert (t))
        return who != nullptr && who->ownsInsert (t.index);
    return false;
}
} // namespace

SessionParticipant* invite (Song& host, const std::string& hostName, const std::string& guestName, std::uint32_t colour, bool keepFull, int maxRecords)
{
    auto& session = host.session;
    if (session.find (session::hostTag) == nullptr)
    {
        SessionParticipant me;
        me.tag = session::hostTag;
        me.name = hostName.empty() ? std::string ("Host") : hostName;
        session.participants.insert (session.participants.begin(), me);
    }
    else if (! hostName.empty())
        session.find (session::hostTag)->name = hostName;

    for (auto& p : session.participants)
        if (p.tag != session::hostTag && lower (p.name) == lower (guestName))
        {
            p.colour = colour != 0 ? colour : p.colour;
            p.keepFull = keepFull;
            return &p;
        }
    if ((int) session.participants.size() >= std::max (maxRecords, session::maxParticipants))
        return nullptr;

    std::uint32_t tag = session::hostTag;
    int insertStart = lastUsedInsert (host) + 1, rowStart = lastUsedRow (host) + 1;
    for (const auto& p : session.participants)
    {
        tag = std::max (tag, p.tag);
        if (p.insertCount > 0)
            insertStart = std::max (insertStart, p.insertFirst + p.insertCount);
        if (p.rowCount > 0)
            rowStart = std::max (rowStart, p.rowFirst + p.rowCount);
    }
    SessionParticipant guest;
    guest.tag = tag + 1;
    guest.name = guestName;
    guest.colour = colour;
    guest.keepFull = keepFull;
    guest.insertFirst = std::clamp (insertStart, 1, maxMixerTracks - 1);
    guest.insertCount = std::clamp (maxMixerTracks - guest.insertFirst, 0, session::insertsPerGuest);
    guest.rowFirst = rowStart;
    guest.rowCount = session::rowsPerGuest;
    session.participants.push_back (guest);
    return &session.participants.back();
}

Song guestCopy (const Song& host, std::uint32_t guestTag)
{
    Song copy = host;
    copy.session.me = guestTag;
    return copy;
}

Song extract (const Song& song, std::uint32_t tag)
{
    const auto* who = song.session.find (tag);
    Song part;
    part.bpm = song.bpm;
    part.beatsPerBar = song.beatsPerBar;
    part.beatUnit = song.beatUnit;
    part.swing = song.swing;
    part.masterPitch = song.masterPitch;
    part.projectId = song.projectId;
    part.nextUid = song.nextUid;
    part.session = song.session;
    part.info.title = song.info.title;
    part.channelGroups = song.channelGroups;
    part.patterns.clear();
    part.arrangements.clear();
    part.playlist.clear();
    part.mixer.clear();
    part.mode = song.mode;

    auto isMine = [tag] (std::uint64_t uid) { return uid != 0 && uids::tagOf (uid) == tag; };

    // channels: his own, then stubs of the others his things refer to (added on demand)
    std::vector<int> channelMap (song.channels.size(), -1);
    for (int i = 0; i < (int) song.channels.size(); ++i)
        if (isMine (song.channels[(std::size_t) i].uid))
        {
            channelMap[(std::size_t) i] = (int) part.channels.size();
            part.channels.push_back (song.channels[(std::size_t) i]);
        }
    auto channelRef = [&] (int i) -> int {
        if (i < 0 || i >= (int) song.channels.size())
            return -1;
        if (channelMap[(std::size_t) i] < 0)
        {
            ChannelDef stub;
            const auto& c = song.channels[(std::size_t) i];
            stub.name = c.name;
            stub.uid = c.uid;
            stub.instrument = InstrumentKind::sampler;
            stub.muted = true;
            channelMap[(std::size_t) i] = (int) part.channels.size();
            part.channels.push_back (stub);
        }
        return channelMap[(std::size_t) i];
    };
    for (auto& c : part.channels)
        for (auto& child : c.layer.children)
            child = channelRef (child);

    // inserts: the mixer as long as the song's, only his range filled in (the others stay default and are never
    // written back); his channels' routing outside the range points at the host's inserts as it is
    part.mixer = Song::defaultMixer ((int) song.mixer.size() - 1);
    if (who != nullptr)
        for (int t = who->insertFirst; t < who->insertFirst + who->insertCount && t < (int) song.mixer.size(); ++t)
            if (t > 0)
                part.mixer[(std::size_t) t] = song.mixer[(std::size_t) t];

    // patterns: his, those with his notes slices, and stubs of those his clips play
    std::vector<int> patternMap (song.patterns.size(), -1);
    auto mineTarget = [&] (const AutomationTarget& t) { return targetIsTheirs (song, t, tag, who); };
    for (int p = 0; p < (int) song.patterns.size(); ++p)
    {
        const auto& src = song.patterns[(std::size_t) p];
        Pattern out;
        out.name = src.name;
        out.lengthTicks = src.lengthTicks;
        out.colour = src.colour;
        out.loopOn = src.loopOn;
        out.loopMode = src.loopMode;
        out.uid = src.uid;
        for (const auto& n : src.notes)
            if (n.channel >= 0 && n.channel < (int) song.channels.size() && isMine (song.channels[(std::size_t) n.channel].uid))
                out.notes.push_back (n), out.notes.back().channel = channelMap[(std::size_t) n.channel];
        for (const auto& l : src.channelLoops)
            if (l.channel >= 0 && l.channel < (int) song.channels.size() && isMine (song.channels[(std::size_t) l.channel].uid))
                out.channelLoops.push_back (l), out.channelLoops.back().channel = channelMap[(std::size_t) l.channel];
        for (const auto& lane : src.events)
            if (mineTarget (lane.target))
            {
                out.events.push_back (lane);
                if (refs::targetsChannel (lane.target))
                    out.events.back().target.index = channelMap[(std::size_t) lane.target.index];
            }
        if (isMine (src.uid) || ! out.notes.empty() || ! out.channelLoops.empty() || ! out.events.empty())
        {
            patternMap[(std::size_t) p] = (int) part.patterns.size();
            part.patterns.push_back (std::move (out));
        }
    }
    auto patternRef = [&] (int p) -> int {
        if (p < 0 || p >= (int) song.patterns.size())
            return -1;
        if (patternMap[(std::size_t) p] < 0)
        {
            Pattern stub;
            stub.name = song.patterns[(std::size_t) p].name;
            stub.uid = song.patterns[(std::size_t) p].uid;
            stub.lengthTicks = song.patterns[(std::size_t) p].lengthTicks;
            patternMap[(std::size_t) p] = (int) part.patterns.size();
            part.patterns.push_back (stub);
        }
        return patternMap[(std::size_t) p];
    };

    // automations of his targets (or made by him for one of his live sources)
    std::vector<int> automationMap (song.automations.size(), -1);
    for (int a = 0; a < (int) song.automations.size(); ++a)
    {
        const auto& src = song.automations[(std::size_t) a];
        const auto liveMine = src.source.channel >= 0 && src.source.channel < (int) song.channels.size()
                              && isMine (song.channels[(std::size_t) src.source.channel].uid);
        if (! mineTarget (src.target) && ! (isMine (src.uid) && liveMine))
            continue;
        auto out = src;
        if (refs::targetsChannel (out.target))
            out.target.index = channelMap[(std::size_t) out.target.index];
        out.source.channel = out.source.channel >= 0 ? channelRef (out.source.channel) : -1;
        automationMap[(std::size_t) a] = (int) part.automations.size();
        part.automations.push_back (std::move (out));
    }

    // clips he made, in every arrangement
    auto takeClips = [&] (const std::vector<Clip>& list) {
        std::vector<Clip> out;
        for (const auto& c : list)
        {
            if (! isMine (c.uid))
                continue;
            auto copy = c;
            if (c.kind == ClipKind::pattern)
                copy.pattern = patternRef (c.pattern);
            else if (c.kind == ClipKind::audio)
                copy.channel = channelRef (c.channel);
            else
                copy.automation = mapped (automationMap, c.automation);
            if ((c.kind == ClipKind::pattern && copy.pattern < 0) || (c.kind == ClipKind::audio && copy.channel < 0)
                || (c.kind == ClipKind::automation && copy.automation < 0))
                continue;
            out.push_back (copy);
        }
        return out;
    };
    for (int a = 0; a < (int) song.arrangements.size(); ++a)
    {
        Arrangement arrangement;
        arrangement.name = song.arrangements[(std::size_t) a].name;
        arrangement.uid = song.arrangements[(std::size_t) a].uid;
        if (a != song.currentArrangement)
            arrangement.clips = takeClips (song.arrangements[(std::size_t) a].clips);
        part.arrangements.push_back (std::move (arrangement));
    }
    if (part.arrangements.empty())
        part.arrangements.push_back ({ "Arrangement 1", {}, 0, 0, {} });
    part.currentArrangement = std::clamp (song.currentArrangement, 0, (int) part.arrangements.size() - 1);
    part.playlist = takeClips (song.playlist);

    // his rows' headers
    if (who != nullptr && who->rowCount > 0)
    {
        part.playlistTracks.resize ((std::size_t) std::max ((int) song.playlistTracks.size(), 0));
        for (int r = who->rowFirst; r < who->rowFirst + who->rowCount && r < (int) song.playlistTracks.size(); ++r)
            part.playlistTracks[(std::size_t) r] = song.playlistTracks[(std::size_t) r];
        for (auto& t : part.playlistTracks)
            t.channel = t.channel >= 0 && t.channel < (int) channelMap.size() && channelMap[(std::size_t) t.channel] >= 0
                            ? channelMap[(std::size_t) t.channel]
                            : -1;
    }

    // controller links and initial values of his targets
    for (const auto& l : song.midiLinks)
        if (mineTarget (l.target))
        {
            auto copy = l;
            if (refs::targetsChannel (copy.target))
                copy.target.index = channelMap[(std::size_t) copy.target.index];
            part.midiLinks.push_back (copy);
        }
    for (const auto& v : song.initialValues)
        if (mineTarget (v.target))
        {
            auto copy = v;
            if (refs::targetsChannel (copy.target))
                copy.target.index = channelMap[(std::size_t) copy.target.index];
            part.initialValues.push_back (copy);
        }

    if (part.patterns.empty())
        part.patterns.emplace_back().name = "Pattern 1";
    part.currentPattern = 0;
    return part;
}

MergeReport merge (Song& host, const Song& part, std::uint32_t tag)
{
    MergeReport report;
    if (tag == session::hostTag || tag == 0)
    {
        report.error = "a part of the host's own";
        return report;
    }
    if (! host.projectId.empty() && ! part.projectId.empty() && host.projectId != part.projectId)
    {
        report.error = "the part belongs to another project";
        return report;
    }

    // who: the host's record (his ranges as the host gave them), else the part's
    SessionParticipant who;
    if (const auto* p = part.session.find (tag))
        who = *p;
    if (const auto* p = host.session.find (tag))
    {
        const auto colour = who.colour;
        const auto name = who.name;
        who = *p;
        if (! name.empty())
            who.name = name;
        if (colour != 0)
            who.colour = colour;
    }
    who.tag = tag;
    report.guest = who.name;
    auto isTheirs = [tag] (std::uint64_t uid) { return uid != 0 && uids::tagOf (uid) == tag; };

    // 1. what an earlier merge of his left and the part no longer has (he deleted it since)
    {
        std::unordered_map<std::uint64_t, int> inPart;
        for (const auto& c : part.channels)
            if (isTheirs (c.uid))
                inPart.emplace (c.uid, 0);
        for (int i = (int) host.channels.size(); --i >= 0;)
            if (isTheirs (host.channels[(std::size_t) i].uid) && inPart.count (host.channels[(std::size_t) i].uid) == 0)
                host.removeChannel (i), ++report.removed;
    }
    {
        std::unordered_map<std::uint64_t, int> inPart;
        for (const auto& a : part.automations)
            if (a.uid != 0)
                inPart.emplace (a.uid, 0);
        for (int i = (int) host.automations.size(); --i >= 0;)
        {
            const auto& a = host.automations[(std::size_t) i];
            if (inPart.count (a.uid) != 0)
                continue;
            if (isTheirs (a.uid) || targetIsTheirs (host, a.target, tag, &who))
                host.removeAutomation (i), ++report.removed;
        }
    }
    host.forEachClipList ([&] (std::vector<Clip>& list) {
        const auto before = list.size();
        list.erase (std::remove_if (list.begin(), list.end(), [&] (const Clip& c) { return isTheirs (c.uid); }), list.end());
        (void) before; // (his clips are re-added below)
    });

    // 2. channels (his replace their counterparts by uid; stubs find theirs)
    std::vector<int> channelMap (part.channels.size(), -1);
    {
        auto byUid = indexByUid (host.channels);
        for (int i = 0; i < (int) part.channels.size(); ++i)
        {
            const auto& c = part.channels[(std::size_t) i];
            const auto found = byUid.find (c.uid);
            if (isTheirs (c.uid))
            {
                if (found != byUid.end())
                    host.channels[(std::size_t) found->second] = c, channelMap[(std::size_t) i] = found->second;
                else
                {
                    channelMap[(std::size_t) i] = (int) host.channels.size();
                    host.channels.push_back (c);
                }
                ++report.channels;
            }
            else if (found != byUid.end())
                channelMap[(std::size_t) i] = found->second;
        }
    }

    // 3. inserts of his range: written where they are, or moved to a free insert when the host routed one of his own
    // channels into it meanwhile
    std::map<int, int> insertMap;
    if (who.insertCount > 0)
    {
        auto hostUses = [&] (int t) {
            return std::any_of (host.channels.begin(), host.channels.end(), [&] (const ChannelDef& c) { return c.mixerTrack == t && ! isTheirs (c.uid); });
        };
        for (int t = std::max (1, who.insertFirst); t < who.insertFirst + who.insertCount && t < (int) part.mixer.size(); ++t)
        {
            const bool partUses = part.insertInUse (t)
                                  || std::any_of (part.channels.begin(), part.channels.end(), [&] (const ChannelDef& c) { return c.mixerTrack == t && isTheirs (c.uid); });
            if (! partUses)
                continue;
            int target = t;
            if (t < (int) host.mixer.size() && hostUses (t))
            {
                // the host's own claim skips reserved ranges, so this is a channel he routed there by hand
                auto saved = host.session;
                host.session.me = session::hostTag;
                target = host.claimFreeInsert();
                host.session = saved;
                if (target <= 0)
                    continue;
                report.movedInserts.emplace_back (t, target);
            }
            insertMap[t] = target;
        }
    }
    for (const auto& [from, to] : insertMap)
    {
        while ((int) host.mixer.size() <= to && (int) host.mixer.size() < maxMixerTracks)
            host.mixer.emplace_back().name = "Insert " + std::to_string (host.mixer.size());
        if (to >= (int) host.mixer.size())
            continue;
        auto track = part.mixer[(std::size_t) from];
        if (track.name == "Insert " + std::to_string (from))
            track.name = "Insert " + std::to_string (to);
        if (track.colour == 0)
            track.colour = who.colour;
        host.mixer[(std::size_t) to] = std::move (track);
        ++report.inserts;
    }

    // 4. patterns: his headers, the others found by uid (a host pattern deleted since that has his notes comes back
    // as a pattern of his)
    std::vector<int> patternMap (part.patterns.size(), -1);
    {
        auto byUid = indexByUid (host.patterns);
        for (int i = 0; i < (int) part.patterns.size(); ++i)
        {
            const auto& p = part.patterns[(std::size_t) i];
            const auto found = byUid.find (p.uid);
            if (found != byUid.end())
            {
                patternMap[(std::size_t) i] = found->second;
                if (isTheirs (p.uid))
                {
                    auto& h = host.patterns[(std::size_t) found->second];
                    h.name = p.name, h.lengthTicks = p.lengthTicks, h.colour = p.colour, h.loopOn = p.loopOn, h.loopMode = p.loopMode;
                    ++report.patterns;
                }
            }
            else if (isTheirs (p.uid) || ! p.notes.empty())
            {
                Pattern fresh;
                fresh.name = p.name, fresh.lengthTicks = p.lengthTicks, fresh.colour = p.colour, fresh.loopOn = p.loopOn, fresh.loopMode = p.loopMode;
                fresh.uid = p.uid;
                patternMap[(std::size_t) i] = (int) host.patterns.size();
                host.patterns.push_back (std::move (fresh));
                ++report.patterns;
            }
        }
    }

    // channel group indices of his channels (by name) and the references between his channels / to inserts
    for (int i = 0; i < (int) part.channels.size(); ++i)
    {
        const auto h = channelMap[(std::size_t) i];
        if (h < 0 || ! isTheirs (part.channels[(std::size_t) i].uid))
            continue;
        auto& c = host.channels[(std::size_t) h];
        if (c.group >= 0 && c.group < (int) part.channelGroups.size())
        {
            const auto& name = part.channelGroups[(std::size_t) c.group];
            auto it = std::find (host.channelGroups.begin(), host.channelGroups.end(), name);
            if (it == host.channelGroups.end())
                it = host.channelGroups.insert (host.channelGroups.end(), name);
            c.group = (int) (it - host.channelGroups.begin());
        }
        else
            c.group = -1;
        if (auto it = insertMap.find (c.mixerTrack); it != insertMap.end())
            c.mixerTrack = it->second;
        if (c.mixerTrack >= (int) host.mixer.size())
            c.mixerTrack = 0;
        std::vector<int> children;
        for (auto child : c.layer.children)
            if (const auto m = mapped (channelMap, child); m >= 0)
                children.push_back (m);
        c.layer.children = children;
        if (c.colour == 0)
            c.colour = who.colour;
    }
    // sends / side-chains of his inserts that point at other inserts of his range
    for (const auto& [from, to] : insertMap)
    {
        (void) from;
        Song view;
        view.mixer.push_back (host.mixer[(std::size_t) to]);
        refs::forEachInsertRef (view, [&] (int& t) {
            if (auto it = insertMap.find (t); it != insertMap.end())
                t = it->second;
        });
        host.mixer[(std::size_t) to] = view.mixer.front();
    }

    // 5. notes slices: his channels' notes everywhere are replaced by the part's
    std::vector<bool> theirsChannel (host.channels.size(), false);
    for (int i = 0; i < (int) host.channels.size(); ++i)
        theirsChannel[(std::size_t) i] = isTheirs (host.channels[(std::size_t) i].uid);
    auto theirs = [&] (int c) { return c >= 0 && c < (int) theirsChannel.size() && theirsChannel[(std::size_t) c]; };
    for (auto& p : host.patterns)
    {
        p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(), [&] (const Note& n) { return theirs (n.channel); }), p.notes.end());
        p.channelLoops.erase (std::remove_if (p.channelLoops.begin(), p.channelLoops.end(), [&] (const ChannelLoop& l) { return theirs (l.channel); }),
                              p.channelLoops.end());
        p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
                                        [&] (const PatternEvents& lane) { return targetIsTheirs (host, lane.target, tag, &who); }),
                        p.events.end());
    }
    for (int i = 0; i < (int) part.patterns.size(); ++i)
    {
        const auto h = patternMap[(std::size_t) i];
        if (h < 0)
            continue;
        auto& target = host.patterns[(std::size_t) h];
        const auto& src = part.patterns[(std::size_t) i];
        for (auto n : src.notes)
            if ((n.channel = mapped (channelMap, n.channel)) >= 0 && theirs (n.channel))
                target.notes.push_back (n), ++report.notes;
        for (auto l : src.channelLoops)
            if ((l.channel = mapped (channelMap, l.channel)) >= 0 && theirs (l.channel))
                target.channelLoops.push_back (l);
        for (auto lane : src.events)
        {
            remapTarget (lane.target, channelMap, insertMap);
            if (lane.target.kind != AutomationTargetKind::none)
                target.events.push_back (lane);
        }
        target.sortNotes();
    }

    // 6. automations
    std::vector<int> automationMap (part.automations.size(), -1);
    {
        auto byUid = indexByUid (host.automations);
        for (int i = 0; i < (int) part.automations.size(); ++i)
        {
            auto a = part.automations[(std::size_t) i];
            remapTarget (a.target, channelMap, insertMap);
            a.source.channel = a.source.channel >= 0 ? mapped (channelMap, a.source.channel) : -1;
            if (auto it = insertMap.find (a.source.track); it != insertMap.end())
                a.source.track = it->second;
            const auto found = byUid.find (a.uid);
            if (found != byUid.end())
            {
                host.automations[(std::size_t) found->second] = std::move (a);
                automationMap[(std::size_t) i] = found->second;
            }
            else
            {
                automationMap[(std::size_t) i] = (int) host.automations.size();
                host.automations.push_back (std::move (a));
            }
            ++report.automations;
        }
    }

    // 7. clips, row by row: his reserved rows move down past the host's when the host put clips there meanwhile
    {
        bool hostOnHisRows = false;
        host.forEachClipList ([&] (const std::vector<Clip>& list) {
            for (const auto& c : list)
                if (who.ownsRow (c.track) && ! isTheirs (c.uid))
                    hostOnHisRows = true;
        });
        if (hostOnHisRows)
        {
            int last = -1;
            host.forEachClipList ([&] (const std::vector<Clip>& list) {
                for (const auto& c : list)
                    last = std::max (last, c.track);
            });
            report.rowShift = last + 1 - who.rowFirst;
        }
    }
    auto rowOf = [&] (int r) { return who.ownsRow (r) ? r + report.rowShift : r; };
    std::vector<int> arrangementMap (part.arrangements.size(), host.currentArrangement);
    {
        auto byUid = indexByUid (host.arrangements);
        for (int i = 0; i < (int) part.arrangements.size(); ++i)
            if (auto it = byUid.find (part.arrangements[(std::size_t) i].uid); it != byUid.end())
                arrangementMap[(std::size_t) i] = it->second;
    }
    auto addClips = [&] (const std::vector<Clip>& list, int hostArrangement) {
        auto& target = hostArrangement == host.currentArrangement || hostArrangement < 0 || hostArrangement >= (int) host.arrangements.size()
                           ? host.playlist
                           : host.arrangements[(std::size_t) hostArrangement].clips;
        for (auto c : list)
        {
            if (c.kind == ClipKind::pattern)
                c.pattern = mapped (patternMap, c.pattern);
            else if (c.kind == ClipKind::audio)
                c.channel = mapped (channelMap, c.channel);
            else
                c.automation = mapped (automationMap, c.automation);
            if ((c.kind == ClipKind::pattern && c.pattern < 0) || (c.kind == ClipKind::audio && c.channel < 0)
                || (c.kind == ClipKind::automation && c.automation < 0))
                continue;
            c.track = rowOf (c.track);
            c.crossfadeIn = c.crossfadeOut = 0;
            target.push_back (c);
            ++report.clips;
        }
    };
    for (int i = 0; i < (int) part.arrangements.size(); ++i)
        if (i != part.currentArrangement)
            addClips (part.arrangements[(std::size_t) i].clips, arrangementMap[(std::size_t) i]);
    addClips (part.playlist, part.currentArrangement >= 0 && part.currentArrangement < (int) arrangementMap.size()
                                 ? arrangementMap[(std::size_t) part.currentArrangement]
                                 : host.currentArrangement);
    std::stable_sort (host.playlist.begin(), host.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });

    // 8. his rows' headers (a name and his colour on the first one he uses)
    if (who.rowCount > 0)
    {
        std::vector<bool> used ((std::size_t) who.rowCount, false);
        host.forEachClipList ([&] (const std::vector<Clip>& list) {
            for (const auto& c : list)
                if (isTheirs (c.uid))
                    if (const auto r = c.track - report.rowShift - who.rowFirst; r >= 0 && r < who.rowCount)
                        used[(std::size_t) r] = true;
        });
        bool first = true;
        for (int r = 0; r < who.rowCount; ++r)
        {
            const auto from = who.rowFirst + r, to = from + report.rowShift;
            PlaylistTrack header = from < (int) part.playlistTracks.size() ? part.playlistTracks[(std::size_t) from] : PlaylistTrack {};
            if (! used[(std::size_t) r] && header.name.empty())
                continue;
            if ((int) host.playlistTracks.size() <= to)
                host.playlistTracks.resize ((std::size_t) to + 1);
            header.channel = header.channel >= 0 ? mapped (channelMap, header.channel) : -1;
            if (auto it = insertMap.find (header.mixerTrack); it != insertMap.end())
                header.mixerTrack = it->second;
            if (header.colour == 0)
                header.colour = who.colour;
            if (first && header.name.empty())
                header.name = who.name;
            header.grouped = ! first;
            first = false;
            host.playlistTracks[(std::size_t) to] = header;
            ++report.rows;
        }
    }

    // 9. controller links and initial values of his targets
    host.midiLinks.erase (std::remove_if (host.midiLinks.begin(), host.midiLinks.end(), [&] (const MidiLink& l) { return targetIsTheirs (host, l.target, tag, &who); }),
                          host.midiLinks.end());
    host.initialValues.erase (std::remove_if (host.initialValues.begin(), host.initialValues.end(),
                                              [&] (const Song::InitialValue& v) { return targetIsTheirs (host, v.target, tag, &who); }),
                              host.initialValues.end());
    for (auto l : part.midiLinks)
    {
        remapTarget (l.target, channelMap, insertMap);
        if (l.target.kind != AutomationTargetKind::none)
            host.midiLinks.push_back (l);
    }
    for (auto v : part.initialValues)
    {
        remapTarget (v.target, channelMap, insertMap);
        if (v.target.kind != AutomationTargetKind::none)
            host.initialValues.push_back (v);
    }

    // 10. the participant's record (his name / colour as he sent them)
    if (auto* p = host.session.find (tag))
        p->name = who.name, p->colour = who.colour;
    else if ((int) host.session.participants.size() < session::maxParticipants)
        host.session.participants.push_back (who);

    uids::normalize (host);
    report.ok = true;
    return report;
}

Song keepOnlyPart (const Song& guest)
{
    Song out = guest;
    const auto me = guest.session.me;
    const auto* who = guest.session.self();
    auto mine = [me] (std::uint64_t uid) { return uid != 0 && uids::tagOf (uid) == me; };

    for (int i = (int) out.channels.size(); --i >= 0;)
        if (! mine (out.channels[(std::size_t) i].uid))
            out.removeChannel (i);
    for (int i = (int) out.automations.size(); --i >= 0;)
    {
        const auto& a = out.automations[(std::size_t) i];
        if (! targetIsTheirs (out, a.target, me, who) && ! mine (a.uid))
            out.removeAutomation (i);
        else if (! targetIsTheirs (out, a.target, me, who))
            out.automations[(std::size_t) i].target = {};
    }
    out.forEachClipList ([&] (std::vector<Clip>& list) {
        list.erase (std::remove_if (list.begin(), list.end(), [&] (const Clip& c) { return ! mine (c.uid); }), list.end());
    });
    for (int i = (int) out.patterns.size(); --i >= 0;)
    {
        auto& p = out.patterns[(std::size_t) i];
        p.events.erase (std::remove_if (p.events.begin(), p.events.end(), [&] (const PatternEvents& lane) { return ! targetIsTheirs (out, lane.target, me, who); }),
                        p.events.end());
        bool used = false;
        out.forEachClipList ([&] (const std::vector<Clip>& list) {
            for (const auto& c : list)
                used = used || (c.kind == ClipKind::pattern && c.pattern == i);
        });
        if (! mine (p.uid) && p.notes.empty() && ! used)
            out.removePattern (i);
    }
    for (int t = 1; t < (int) out.mixer.size(); ++t)
        if (who == nullptr || ! who->ownsInsert (t))
        {
            const bool routed = std::any_of (out.channels.begin(), out.channels.end(), [t] (const ChannelDef& c) { return c.mixerTrack == t; });
            auto& m = out.mixer[(std::size_t) t];
            if (routed)
            {
                m.effects.clear(); // another's insert his channel plays through: kept as a plain insert
                m.standIns.clear();
            }
            else
            {
                m = MixerTrack {};
                m.name = "Insert " + std::to_string (t);
            }
        }
    if (! out.mixer.empty())
    {
        auto& master = out.mixer[0];
        master.effects.clear();
        master.standIns.clear();
    }
    for (int r = 0; r < (int) out.playlistTracks.size(); ++r)
        if (who == nullptr || ! who->ownsRow (r))
            out.playlistTracks[(std::size_t) r] = PlaylistTrack {};
    out.midiLinks.erase (std::remove_if (out.midiLinks.begin(), out.midiLinks.end(), [&] (const MidiLink& l) { return ! targetIsTheirs (out, l.target, me, who); }),
                         out.midiLinks.end());
    out.initialValues.erase (std::remove_if (out.initialValues.begin(), out.initialValues.end(),
                                             [&] (const Song::InitialValue& v) { return ! targetIsTheirs (out, v.target, me, who); }),
                             out.initialValues.end());
    return out;
}
} // namespace lynx::parts

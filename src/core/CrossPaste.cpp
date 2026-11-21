#include "core/CrossPaste.h"

#include <algorithm>
#include <map>
#include <set>

#include "core/Groove.h"
#include "core/RackOps.h"
#include "core/SessionIds.h"

namespace lynx::crosspaste
{
namespace
{
using IndexMap = std::map<int, int>;

int mapped (const IndexMap& map, int index) noexcept
{
    const auto it = map.find (index);
    return it != map.end() ? it->second : -1;
}

// a target on a channel follows the channel; one whose channel did not come along goes silent
void remapTarget (AutomationTarget& t, const IndexMap& channels)
{
    if (! refs::targetsChannel (t))
        return;
    t.index = mapped (channels, t.index);
    if (t.index < 0)
        t = {};
}

void remapAutomation (Automation& a, const IndexMap& channels)
{
    remapTarget (a.target, channels);
    for (auto& link : a.links)
        remapTarget (link.target, channels);
    if (a.source.channel >= 0)
        a.source.channel = mapped (channels, a.source.channel);
    a.uid = 0;
}

void remapPattern (Pattern& p, const IndexMap& channels)
{
    for (auto& n : p.notes)
        n.channel = mapped (channels, n.channel);
    p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(), [] (const Note& n) { return n.channel < 0; }), p.notes.end());
    for (auto& l : p.channelLoops)
        l.channel = mapped (channels, l.channel);
    p.channelLoops.erase (std::remove_if (p.channelLoops.begin(), p.channelLoops.end(), [] (const ChannelLoop& l) { return l.channel < 0; }),
                          p.channelLoops.end());
    for (auto& lane : p.events)
        remapTarget (lane.target, channels);
    p.events.erase (std::remove_if (p.events.begin(), p.events.end(), [] (const PatternEvents& e) { return e.target.kind == AutomationTargetKind::none; }),
                    p.events.end());
    p.uid = 0;
    p.sortNotes();
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

// a channel that arrives in another project (or again in this one) is a new channel: its own instance, no uid, no
// frozen audio of the original, not a pad of a Simple Sampler track, not a Loop Starter channel
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

// the rack group named as the fragment's group `group` in `song` (added when missing); -1 for none
int groupIn (Song& song, const Song& fragment, int group)
{
    if (group < 0 || group >= (int) fragment.channelGroups.size())
        return -1;
    const auto& name = fragment.channelGroups[(std::size_t) group];
    const auto it = std::find (song.channelGroups.begin(), song.channelGroups.end(), name);
    if (it != song.channelGroups.end())
        return (int) (it - song.channelGroups.begin());
    song.channelGroups.push_back (name);
    return (int) song.channelGroups.size() - 1;
}

// a channel of the fragment as it goes into `song`: rack group by name, its insert number when the song has it
ChannelDef placed (Song& song, const Song& fragment, const ChannelDef& source)
{
    auto c = arriving (source);
    c.group = groupIn (song, fragment, source.group);
    if (song.mixer.empty() || c.mixerTrack < 0 || c.mixerTrack >= (int) song.mixer.size())
        c.mixerTrack = 0;
    return c;
}

template <typename T>
bool equalWithoutUid (T a, T b)
{
    a.uid = 0;
    b.uid = 0;
    return a == b;
}

bool sameGroove (Groove a, Groove b)
{
    a.id = b.id = 0;
    return a == b;
}
} // namespace

bool sameChannel (const ChannelDef& a, const ChannelDef& b)
{
    if (a.name != b.name || a.instrument != b.instrument)
        return false;
    switch (a.instrument)
    {
        case InstrumentKind::plugin: return a.plugin.id == b.plugin.id;
        case InstrumentKind::sampler:
        case InstrumentKind::audioClip: return a.samplePath == b.samplePath;
        default: return true;
    }
}

Song clipsFragment (const Song& song, const std::vector<Clip>& clips, const trackclips::Carried& carried, std::vector<int>* sourceChannels)
{
    Song f;
    f.bpm = song.bpm;
    f.beatsPerBar = song.beatsPerBar;
    f.beatUnit = song.beatUnit;
    f.patterns.clear();
    f.channels.clear();
    f.playlist.clear();

    // what the clips play, from the source song (or the carried copy of a track clip a cut may have deleted)
    const auto patternOf = [&] (int index) -> const Pattern* {
        for (const auto& [at, p] : carried.patterns)
            if (at == index)
                return &p;
        return index >= 0 && index < (int) song.patterns.size() ? &song.patterns[(std::size_t) index] : nullptr;
    };

    std::set<int> channels;
    for (const auto& c : clips)
    {
        if (c.kind == ClipKind::pattern)
        {
            if (const auto* p = patternOf (c.pattern))
                channelsOfPattern (*p, channels);
        }
        else if (c.kind == ClipKind::audio)
        {
            if (c.channel >= 0 && c.channel < (int) song.channels.size())
                channels.insert (c.channel);
        }
        else if (c.automation >= 0 && c.automation < (int) song.automations.size())
            channelsOfAutomation (song.automations[(std::size_t) c.automation], channels);
    }

    IndexMap channelMap;
    for (const auto ch : channels)
        if (ch >= 0 && ch < (int) song.channels.size())
        {
            channelMap[ch] = (int) f.channels.size();
            f.channels.push_back (song.channels[(std::size_t) ch]); // (its layer children: the fragment's indices below)
            if (sourceChannels != nullptr)
                sourceChannels->push_back (ch);
        }
    for (auto& c : f.channels)
    {
        for (auto& k : c.layer.children)
            k = mapped (channelMap, k);
        c.layer.children.erase (std::remove (c.layer.children.begin(), c.layer.children.end(), -1), c.layer.children.end());
        c.uid = 0;
    }
    f.channelGroups = song.channelGroups;

    IndexMap patternMap, automationMap, envelopeMap;
    std::set<std::uint32_t> grooves;
    for (auto c : clips)
    {
        if (c.kind == ClipKind::pattern)
        {
            const auto* p = patternOf (c.pattern);
            if (p == nullptr)
                continue;
            if (! patternMap.count (c.pattern))
            {
                auto copy = *p;
                remapPattern (copy, channelMap);
                patternMap[c.pattern] = (int) f.patterns.size();
                f.patterns.push_back (std::move (copy));
            }
            c.pattern = patternMap[c.pattern];
            if (c.groove != 0 && groove::find (song.grooves, c.groove) != nullptr)
                grooves.insert (c.groove);
            else
                c.groove = 0;
        }
        else if (c.kind == ClipKind::audio)
        {
            c.channel = mapped (channelMap, c.channel);
            if (c.channel < 0)
                continue;
            if (c.envelope >= 0 && c.envelope < (int) song.clipEnvelopes.size())
            {
                if (! envelopeMap.count (c.envelope))
                {
                    envelopeMap[c.envelope] = (int) f.clipEnvelopes.size();
                    f.clipEnvelopes.push_back (song.clipEnvelopes[(std::size_t) c.envelope]);
                }
                c.envelope = envelopeMap[c.envelope];
            }
            else
                c.envelope = -1;
        }
        else
        {
            if (c.automation < 0 || c.automation >= (int) song.automations.size())
                continue;
            if (! automationMap.count (c.automation))
            {
                auto copy = song.automations[(std::size_t) c.automation];
                remapAutomation (copy, channelMap);
                automationMap[c.automation] = (int) f.automations.size();
                f.automations.push_back (std::move (copy));
            }
            c.automation = automationMap[c.automation];
        }
        c.uid = 0;
        c.gridSlot = -1;
        f.playlist.push_back (c);
    }
    for (const auto id : grooves)
        if (const auto* g = groove::find (song.grooves, id))
            f.grooves.grooves.push_back (*g);
    f.grooves.amount = song.grooves.amount;
    return f;
}

std::vector<Clip> importClips (Song& song, const Song& fragment)
{
    // only what the fragment's clips play (a fragment read back from JSON may carry a default pattern of its own)
    std::set<int> patterns, automations, channels;
    for (const auto& c : fragment.playlist)
    {
        if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) fragment.patterns.size())
        {
            patterns.insert (c.pattern);
            channelsOfPattern (fragment.patterns[(std::size_t) c.pattern], channels);
        }
        else if (c.kind == ClipKind::audio)
            channels.insert (c.channel);
        else if (c.kind == ClipKind::automation && c.automation >= 0 && c.automation < (int) fragment.automations.size())
        {
            automations.insert (c.automation);
            channelsOfAutomation (fragment.automations[(std::size_t) c.automation], channels);
        }
    }

    // channels: the song's own where it has the same one, else a copy at the end of the rack
    IndexMap channelMap;
    std::vector<int> added; // fragment channels that came in as new channels
    for (const auto i : channels)
    {
        if (i < 0 || i >= (int) fragment.channels.size())
            continue;
        const auto& c = fragment.channels[(std::size_t) i];
        const auto it = std::find_if (song.channels.begin(), song.channels.end(), [&c] (const ChannelDef& own) { return sameChannel (own, c); });
        if (it != song.channels.end())
            channelMap[i] = (int) (it - song.channels.begin());
        else
        {
            auto copy = placed (song, fragment, c);
            copy.layer.children.clear();
            channelMap[i] = insertChannel (song, (int) song.channels.size(), std::move (copy)); // (at the end: no index moves)
            added.push_back (i);
        }
    }
    // the layer children of an added channel: the channels its children became (a channel the song had keeps its own)
    for (const auto i : added)
        for (const auto k : fragment.channels[(std::size_t) i].layer.children)
            if (const auto m = mapped (channelMap, k); m >= 0 && m != channelMap[i])
                song.channels[(std::size_t) channelMap[i]].layer.children.push_back (m);

    IndexMap patternMap;
    for (const auto i : patterns)
    {
        auto p = fragment.patterns[(std::size_t) i];
        remapPattern (p, channelMap);
        if (! p.trackClip) // (a track clip is always a clip of its own, as every copy of one)
        {
            const auto same = std::find_if (song.patterns.begin(), song.patterns.end(), [&p] (const Pattern& own) { return equalWithoutUid (own, p); });
            if (same != song.patterns.end())
            {
                patternMap[i] = (int) (same - song.patterns.begin());
                continue;
            }
        }
        patternMap[i] = (int) song.patterns.size();
        song.patterns.push_back (std::move (p));
    }

    IndexMap automationMap;
    for (const auto i : automations)
    {
        auto a = fragment.automations[(std::size_t) i];
        remapAutomation (a, channelMap);
        if (a.target.kind == AutomationTargetKind::vcaLevel && a.target.index >= (int) song.vcas.size())
            a.target = {};
        const auto same = std::find_if (song.automations.begin(), song.automations.end(), [&a] (const Automation& own) { return equalWithoutUid (own, a); });
        if (same != song.automations.end())
        {
            automationMap[i] = (int) (same - song.automations.begin());
            continue;
        }
        automationMap[i] = (int) song.automations.size();
        song.automations.push_back (std::move (a));
    }

    IndexMap envelopeMap;
    for (int i = 0; i < (int) fragment.clipEnvelopes.size(); ++i)
    {
        envelopeMap[i] = (int) song.clipEnvelopes.size();
        song.clipEnvelopes.push_back (fragment.clipEnvelopes[(std::size_t) i]);
    }

    std::map<std::uint32_t, std::uint32_t> grooveMap;
    for (const auto& g : fragment.grooves.grooves)
    {
        const auto same = std::find_if (song.grooves.grooves.begin(), song.grooves.grooves.end(), [&g] (const Groove& own) { return sameGroove (own, g); });
        grooveMap[g.id] = same != song.grooves.grooves.end() ? same->id : groove::add (song.grooves, g);
    }

    std::vector<Clip> out;
    for (auto c : fragment.playlist)
    {
        if (c.kind == ClipKind::pattern)
        {
            c.pattern = mapped (patternMap, c.pattern);
            if (c.pattern < 0)
                continue;
            const auto g = grooveMap.find (c.groove);
            c.groove = g != grooveMap.end() ? g->second : 0;
        }
        else if (c.kind == ClipKind::audio)
        {
            c.channel = mapped (channelMap, c.channel);
            if (c.channel < 0)
                continue;
            c.envelope = c.envelope >= 0 ? mapped (envelopeMap, c.envelope) : -1;
        }
        else
        {
            c.automation = mapped (automationMap, c.automation);
            if (c.automation < 0)
                continue;
        }
        c.uid = 0;
        c.gridSlot = -1;
        out.push_back (c);
    }
    return out;
}

Song channelsFragment (const Song& song, std::vector<int> channels)
{
    std::sort (channels.begin(), channels.end());
    channels.erase (std::unique (channels.begin(), channels.end()), channels.end());
    Song f;
    f.bpm = song.bpm;
    f.channels.clear();
    f.patterns.clear();
    IndexMap map;
    for (const auto ch : channels)
        if (ch >= 0 && ch < (int) song.channels.size())
        {
            map[ch] = (int) f.channels.size();
            f.channels.push_back (song.channels[(std::size_t) ch]);
        }
    for (auto& c : f.channels)
    {
        for (auto& k : c.layer.children)
            k = mapped (map, k);
        c.layer.children.erase (std::remove (c.layer.children.begin(), c.layer.children.end(), -1), c.layer.children.end());
        c.uid = 0;
    }
    f.channelGroups = song.channelGroups;
    return f;
}

std::vector<int> importChannels (Song& song, const Song& fragment, int at)
{
    at = std::clamp (at, 0, (int) song.channels.size());
    std::vector<int> added;
    for (int i = 0; i < (int) fragment.channels.size(); ++i)
    {
        auto c = placed (song, fragment, fragment.channels[(std::size_t) i]);
        c.layer.children.clear(); // (set below, once every pasted channel has its place)
        added.push_back (insertChannel (song, at + i, std::move (c)));
    }
    for (int i = 0; i < (int) fragment.channels.size(); ++i)
        for (const auto k : fragment.channels[(std::size_t) i].layer.children)
            if (k >= 0 && k < (int) added.size() && k != i)
                song.channels[(std::size_t) added[(std::size_t) i]].layer.children.push_back (added[(std::size_t) k]);
    return added;
}
} // namespace lynx::crosspaste

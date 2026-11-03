#include "core/StandIns.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/Scheduler.h"
#include "core/SessionIds.h"

namespace lynx::standins
{
namespace
{
bool docUsesPlugin (const PatcherDoc& doc)
{
    for (const auto& n : doc.nodes)
        if (n.generator ? channelUsesPlugin (n.channel) : slotUsesPlugin (n.effect))
            return true;
    return false;
}

void addRef (const PluginRef& ref, std::vector<const PluginRef*>& out)
{
    if (ref.id.empty() && ref.name.empty())
        return;
    for (const auto* r : out)
        if (r->id == ref.id && r->name == ref.name)
            return;
    out.push_back (&ref);
}

void slotPlugins (const EffectSlot& slot, std::vector<const PluginRef*>& out);
void docPlugins (const PatcherDoc& doc, std::vector<const PluginRef*>& out)
{
    for (const auto& n : doc.nodes)
        if (n.generator)
            pluginsOf (n.channel, out);
        else
            slotPlugins (n.effect, out);
}
void slotPlugins (const EffectSlot& slot, std::vector<const PluginRef*>& out)
{
    if (slot.kind == EffectKind::plugin)
        addRef (slot.plugin, out);
    for (const auto& b : slot.branches)
        for (const auto& s : b.chain)
            slotPlugins (s, out);
    docPlugins (slot.patcher, out);
}

bool anyMissing (const std::vector<const PluginRef*>& list, const std::function<bool (const PluginRef&)>& missing)
{
    return std::any_of (list.begin(), list.end(), [&] (const PluginRef* r) { return missing (*r); });
}

bool targetsTrack (const AutomationTarget& t, int track)
{
    return refs::targetsInsert (t) && t.index == track;
}

// A stem render hears only what the stem is of: no stand-ins of its own (they would play into it), the Master neutral
// (no effects, fader, pan, EQ or automation of it), no loop region, no marker actions, no performance zone.
void neutralise (Song& s)
{
    s.mode = PlayMode::song;
    s.loopStart = s.loopEnd = 0;
    s.performanceMode = false;
    for (auto& a : s.arrangements)
        a.loopStart = a.loopEnd = 0;
    for (auto& m : s.markers())
        if (! m.isSignature())
            m.action = Marker::none;
    for (auto& c : s.channels)
        c.standIns.clear();
    for (auto& t : s.mixer)
    {
        t.solo = false;
        t.standIns.clear();
    }
    if (! s.mixer.empty())
    {
        auto& master = s.mixer[0];
        master.effects.clear();
        master.volume = unityFader;
        master.pan = 0.0f;
        master.muted = false;
        master.stereo = 0.0f;
        master.invertPhase = master.swapChannels = false;
        master.eq = {};
    }
    for (auto& a : s.automations)
        if (targetsTrack (a.target, 0))
            a.target = {};
    s.initialValues.erase (std::remove_if (s.initialValues.begin(), s.initialValues.end(),
                                           [] (const Song::InitialValue& v) { return targetsTrack (v.target, 0); }),
                           s.initialValues.end());
}

// only `channel` heard, raw: its volume / pan at unity and centre (they apply live), straight to the Master
void soloChannelRaw (Song& s, int channel)
{
    for (int j = 0; j < (int) s.channels.size(); ++j)
        s.channels[(std::size_t) j].muted = j != channel;
    auto& c = s.channels[(std::size_t) channel];
    c.volume = 1.0f; // channelGain (1) = 1
    c.pan = 0.0f;
    c.mixerTrack = 0;
    auto own = [channel] (const AutomationTarget& t) {
        return (t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan) && t.index == channel;
    };
    for (auto& a : s.automations)
        if (own (a.target))
            a.target = {};
    for (auto& p : s.patterns)
        p.events.erase (std::remove_if (p.events.begin(), p.events.end(), [&] (const PatternEvents& lane) { return own (lane.target); }), p.events.end());
    s.initialValues.erase (std::remove_if (s.initialValues.begin(), s.initialValues.end(), [&] (const Song::InitialValue& v) { return own (v.target); }),
                           s.initialValues.end());
    neutralise (s);
}

bool playsInPattern (const Pattern& p, int channel)
{
    return std::any_of (p.notes.begin(), p.notes.end(), [channel] (const Note& n) { return n.channel == channel && ! n.muted; });
}

bool hasClips (const Song& song, int automation)
{
    bool found = false;
    song.forEachClipList ([&] (const std::vector<Clip>& list) {
        for (const auto& c : list)
            found = found || (c.kind == ClipKind::automation && c.automation == automation);
    });
    return found;
}

int lastRow (const Song& song)
{
    int last = -1;
    song.forEachClipList ([&] (const std::vector<Clip>& list) {
        for (const auto& c : list)
            last = std::max (last, c.track);
    });
    return last;
}

// the channel that plays a stem in place of `c`: its place in the mix (insert, volume, pan, mute) and nothing that
// would process the audio again (arpeggiator, Misc tab, envelopes: they are in the render)
ChannelDef standInChannel (const ChannelDef& c, const std::string& path)
{
    ChannelDef f;
    f.name = c.name;
    f.instrument = InstrumentKind::audioClip;
    f.samplePath = path;
    f.volume = c.volume;
    f.pan = c.pan;
    f.muted = c.muted;
    f.mixerTrack = c.mixerTrack;
    f.colour = c.colour;
    f.group = c.group;
    f.soloLock = c.soloLock;
    f.uid = c.uid;
    f.standIns = c.standIns;
    return f;
}
} // namespace

bool slotUsesPlugin (const EffectSlot& slot)
{
    if (slot.kind == EffectKind::plugin && ! slot.plugin.id.empty())
        return true;
    for (const auto& b : slot.branches)
        for (const auto& s : b.chain)
            if (slotUsesPlugin (s))
                return true;
    return docUsesPlugin (slot.patcher);
}

bool channelUsesPlugin (const ChannelDef& c)
{
    if (c.instrument == InstrumentKind::plugin)
        return ! c.plugin.id.empty();
    return c.instrument == InstrumentKind::patcher && docUsesPlugin (c.patcher);
}

bool insertUsesPlugin (const MixerTrack& t)
{
    return std::any_of (t.effects.begin(), t.effects.end(), [] (const EffectSlot& s) { return slotUsesPlugin (s); });
}

void pluginsOf (const ChannelDef& c, std::vector<const PluginRef*>& out)
{
    if (c.instrument == InstrumentKind::plugin)
        addRef (c.plugin, out);
    if (c.instrument == InstrumentKind::patcher)
        docPlugins (c.patcher, out);
}

void pluginsOf (const MixerTrack& t, std::vector<const PluginRef*>& out)
{
    for (const auto& s : t.effects)
        slotPlugins (s, out);
}

std::int64_t tailTicks (const Song& song) noexcept
{
    return (std::int64_t) 4 * ppq * std::max (1, song.beatsPerBar);
}

float stemGain() noexcept
{
    return channelGain (10000.0f / 12800.0f);
}

const StandIn* find (const std::vector<StandIn>& list, std::uint64_t patternUid, bool loop) noexcept
{
    for (const auto& s : list)
        if (s.pattern == patternUid && s.loop == loop)
            return &s;
    return nullptr;
}

ChannelRender channelPatternRender (const Song& song, int channel)
{
    ChannelRender r;
    if (channel < 0 || channel >= (int) song.channels.size())
        return r;
    r.tail = tailTicks (song);
    auto s = song;
    soloChannelRaw (s, channel);
    s.playlist.clear();
    std::int64_t at = 0;
    for (int p = 0; p < (int) song.patterns.size(); ++p)
    {
        if (! playsInPattern (song.patterns[(std::size_t) p], channel))
            continue;
        Segment seg;
        seg.pattern = p;
        seg.patternUid = song.patterns[(std::size_t) p].uid;
        seg.length = song.patterns[(std::size_t) p].effectiveLength (song.beatsPerBar);
        s.patterns[(std::size_t) p].lengthTicks = seg.length; // (fixed: the render's clips have that length)
        seg.once = at;
        seg.loopFrom = at + 2 * seg.length + r.tail;
        for (const auto start : { at, at + seg.length + r.tail, seg.loopFrom })
        {
            Clip clip;
            clip.pattern = p;
            clip.track = 0;
            clip.start = start;
            clip.length = seg.length;
            s.playlist.push_back (clip);
        }
        r.segments.push_back (seg);
        at = seg.loopFrom + seg.length + r.tail;
    }
    r.end = at;
    r.song = std::move (s);
    return r;
}

bool needsTimeline (const Song& song, int channel)
{
    for (int a = 0; a < (int) song.automations.size(); ++a)
    {
        const auto& t = song.automations[(std::size_t) a].target;
        if ((t.kind == AutomationTargetKind::tempo || (t.kind == AutomationTargetKind::channelParam && t.index == channel)) && hasClips (song, a))
            return true;
    }
    return false;
}

Song channelTimelineRender (const Song& song, int channel)
{
    auto s = song;
    if (channel >= 0 && channel < (int) s.channels.size())
        soloChannelRaw (s, channel);
    return s;
}

Song insertTimelineRender (const Song& song, int track)
{
    auto s = song;
    if (track <= 0 || track >= (int) s.mixer.size())
        return s;
    neutralise (s);
    auto& t = s.mixer[(std::size_t) track];
    t.sends.clear();
    t.toMaster = true;
    t.masterLevel = 1.0f;
    t.muted = false;
    t.solo = true; // it and whatever feeds it are heard
    t.volume = unityFader;
    t.pan = 0.0f;
    t.stereo = 0.0f;
    t.eq = {};
    t.invertPhase = t.swapChannels = false;
    t.outputChannel = outputDefault;
    for (auto& a : s.automations)
        if ((a.target.kind == AutomationTargetKind::mixerVolume || a.target.kind == AutomationTargetKind::mixerPan
             || a.target.kind == AutomationTargetKind::mixerSend)
            && a.target.index == track)
            a.target = {};
    return s;
}

bool hasAny (const Song& song) noexcept
{
    return std::any_of (song.channels.begin(), song.channels.end(), [] (const ChannelDef& c) { return ! c.standIns.empty(); })
           || std::any_of (song.mixer.begin(), song.mixer.end(), [] (const MixerTrack& t) { return ! t.standIns.empty(); });
}

Song project (const Song& song, const std::function<bool (const PluginRef&)>& missing, Projected* projected)
{
    if (projected != nullptr)
        *projected = {};
    // the quick answer for almost every project: nothing to stand in for
    if (! hasAny (song) || ! missing)
        return song;

    auto out = song;
    const bool songMode = song.mode == PlayMode::song;
    const int hiddenRow = lastRow (song) + 1; // a row of its own, past everything (never muted, never shown)
    // (a stem's tail never runs past the song's end: the song's length — where Song mode loops — stays the same)
    const auto songEnd = song.songLength();
    auto stemClip = [&] (int channel, int row, std::int64_t start, std::int64_t length, std::int64_t offset) {
        length = std::max<std::int64_t> (1, std::min (length, songEnd - start));
        Clip c;
        c.kind = ClipKind::audio;
        c.channel = channel;
        c.track = row;
        c.start = start;
        c.length = length;
        c.offset = offset;
        c.gain = stemGain();
        return c;
    };

    // inserts (Song mode): the timeline stem plays into the fader in place of the effects and of everything routed in
    if (songMode)
        for (int t = 1; t < (int) song.mixer.size(); ++t)
        {
            const auto& track = song.mixer[(std::size_t) t];
            const auto* stem = find (track.standIns, 0, false);
            if (stem == nullptr || ! insertUsesPlugin (track))
                continue;
            std::vector<const PluginRef*> list;
            pluginsOf (track, list);
            if (! anyMissing (list, missing))
                continue;
            for (auto& e : out.mixer[(std::size_t) t].effects)
                e.enabled = false;
            for (auto& c : out.channels)
                if (c.mixerTrack == t)
                    c.muted = true;
            for (auto& m : out.mixer)
            {
                m.sends.erase (std::remove_if (m.sends.begin(), m.sends.end(), [t] (const MixerSend& s) { return s.target == t; }), m.sends.end());
                for (auto& e : m.effects)
                    for (auto& k : e.sendTracks)
                        if (k == t)
                            k = 0;
            }
            ChannelDef h;
            h.name = track.name;
            h.instrument = InstrumentKind::audioClip;
            h.samplePath = stem->path;
            h.mixerTrack = t;
            h.volume = 1.0f;
            out.channels.push_back (h);
            out.playlist.push_back (stemClip ((int) out.channels.size() - 1, hiddenRow, 0, std::max<std::int64_t> (1, stem->lengthTicks), 0));
            if (projected != nullptr)
                projected->inserts.push_back (t);
        }

    // channels
    const auto lengths = patternLengths (song);
    auto dropNotes = [&] (int channel) {
        for (std::size_t p = 0; p < out.patterns.size(); ++p)
        {
            auto& pattern = out.patterns[p];
            if (pattern.lengthTicks <= 0 && p < lengths.size())
                pattern.lengthTicks = lengths[p]; // an automatic length must not change with the notes gone
            pattern.notes.erase (std::remove_if (pattern.notes.begin(), pattern.notes.end(), [channel] (const Note& n) { return n.channel == channel; }),
                                 pattern.notes.end());
            pattern.channelLoops.erase (std::remove_if (pattern.channelLoops.begin(), pattern.channelLoops.end(),
                                                        [channel] (const ChannelLoop& l) { return l.channel == channel; }),
                                        pattern.channelLoops.end());
        }
    };
    for (int c = 0; c < (int) song.channels.size(); ++c)
    {
        const auto& channel = song.channels[(std::size_t) c];
        if (channel.standIns.empty() || ! channelUsesPlugin (channel))
            continue;
        std::vector<const PluginRef*> list;
        pluginsOf (channel, list);
        if (! anyMissing (list, missing))
            continue;
        // (a channel routed into an insert that plays its own stand-in is in that stem already: it stays muted)
        const bool intoStem = out.channels[(std::size_t) c].muted && ! channel.muted;
        bool played = false;

        if (songMode)
        {
            const auto* timeline = find (channel.standIns, 0, false);
            const bool patternStems = std::any_of (channel.standIns.begin(), channel.standIns.end(), [] (const StandIn& s) { return s.pattern != 0 && ! s.loop; });
            dropNotes (c);
            if (timeline != nullptr && (needsTimeline (song, c) || ! patternStems))
            {
                out.channels[(std::size_t) c] = standInChannel (channel, timeline->path);
                out.playlist.push_back (stemClip (c, hiddenRow, 0, std::max<std::int64_t> (1, timeline->lengthTicks), 0));
                played = true;
            }
            else
            {
                out.channels[(std::size_t) c] = standInChannel (channel, {});
                std::map<int, int> stemChannel; // pattern -> the channel playing its stem
                for (const auto& clip : song.playlist)
                {
                    if (clip.kind != ClipKind::pattern || clip.pattern < 0 || clip.pattern >= (int) song.patterns.size())
                        continue;
                    const auto* stem = find (channel.standIns, song.patterns[(std::size_t) clip.pattern].uid, false);
                    if (stem == nullptr || clip.offset >= stem->lengthTicks)
                        continue;
                    const auto length = lengths[(std::size_t) clip.pattern];
                    auto len = clip.length;
                    if (clip.offset + clip.length >= length) // the clip reaches the pattern's end: its tail rings on
                        len += std::max<std::int64_t> (0, stem->lengthTicks - length);
                    len = std::min (len, stem->lengthTicks - clip.offset);
                    if (len <= 0)
                        continue;
                    auto it = stemChannel.find (clip.pattern);
                    if (it == stemChannel.end())
                    {
                        auto h = standInChannel (channel, stem->path);
                        h.uid = 0;
                        h.standIns.clear();
                        if (intoStem)
                            h.muted = true;
                        out.channels.push_back (h);
                        it = stemChannel.emplace (clip.pattern, (int) out.channels.size() - 1).first;
                    }
                    auto audio = stemClip (it->second, clip.track, clip.start, len, clip.offset);
                    audio.muted = clip.muted;
                    out.playlist.push_back (audio);
                    played = true;
                }
                // the channel's volume / pan automation drives the channels playing its stems too
                for (int a = 0, count = (int) song.automations.size(); a < count; ++a)
                {
                    const auto& target = song.automations[(std::size_t) a].target;
                    if ((target.kind != AutomationTargetKind::channelVolume && target.kind != AutomationTargetKind::channelPan) || target.index != c)
                        continue;
                    for (const auto& [pattern, h] : stemChannel)
                    {
                        (void) pattern;
                        auto copy = song.automations[(std::size_t) a];
                        copy.target.index = h;
                        copy.uid = 0;
                        out.automations.push_back (copy);
                        const auto index = (int) out.automations.size() - 1;
                        for (const auto& clip : song.playlist)
                            if (clip.kind == ClipKind::automation && clip.automation == a)
                            {
                                auto ac = clip;
                                ac.automation = index;
                                ac.uid = 0;
                                out.playlist.push_back (ac);
                            }
                    }
                }
            }
        }
        else
        {
            const auto p = std::clamp (song.currentPattern, 0, (int) song.patterns.size() - 1);
            const auto* loop = song.patterns.empty() ? nullptr : find (channel.standIns, song.patterns[(std::size_t) p].uid, true);
            dropNotes (c);
            out.channels[(std::size_t) c] = standInChannel (channel, loop != nullptr ? loop->path : std::string());
            if (loop != nullptr)
            {
                // one note over the whole pattern: its loop pass, retriggered each time the pattern loops (the "off"
                // at the end comes before the next pass's "on" on the same sample: EventBuffer::sortByOffset)
                Note n;
                n.channel = c;
                n.key = audioClipKeyBase + 1;
                n.start = 0;
                n.length = std::max<std::int64_t> (1, lengths[(std::size_t) p]);
                n.velocity = stemGain();
                auto& notes = out.patterns[(std::size_t) p].notes;
                notes.insert (notes.begin(), n);
                out.patterns[(std::size_t) p].sortNotes();
                played = true;
            }
        }
        if (intoStem)
            out.channels[(std::size_t) c].muted = true;
        (void) played; // (listed either way: its plugin is missing here and it stands in — silent where no stem fits)
        if (projected != nullptr)
            projected->channels.push_back (c);
    }
    return out;
}
} // namespace lynx::standins

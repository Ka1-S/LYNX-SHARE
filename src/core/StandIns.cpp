#include "core/StandIns.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include "core/DetailChain.h"
#include "core/NoteEdit.h"
#include "core/PatternBlocks.h"
#include "core/QuickRender.h"
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

// Anything drives a target: the renderer then works in its short automation chunks (RackState::hasAutomation).
bool playsAutomation (const Song& s) noexcept
{
    return std::any_of (s.automations.begin(), s.automations.end(), [] (const Automation& a) { return a.target.kind != AutomationTargetKind::none; })
           || std::any_of (s.patterns.begin(), s.patterns.end(), [] (const Pattern& p) {
                  return std::any_of (p.events.begin(), p.events.end(), [] (const PatternEvents& lane) { return lane.target.kind != AutomationTargetKind::none; });
              });
}

// A stem render hears only what the stem is of: no stand-ins of its own (they would play into it), the Master neutral
// (no effects, fader, pan, EQ or automation of it), no loop region, no marker actions, no performance zone.
void neutralise (Song& s)
{
    // (round 190ff: with the song's chunking, whatever automation is taken away below — Song::automationChunks)
    s.automationChunks = s.automationChunks || playsAutomation (s);
    s.mode = PlayMode::song;
    s.loopStart = s.loopEnd = 0;
    s.performanceMode = false;
    for (auto& a : s.arrangements)
        a.loopStart = a.loopEnd = 0;
    for (auto& m : s.markers())
        if (! m.isSignature())
            m.action = Marker::none;
    for (auto& c : s.channels)
    {
        c.standIns.clear();
        c.freeze = {}; // (a stem is rendered from the instruments themselves)
    }
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
    c.misc.levels.pan = 0.0f; // (the Levels adjustment's level and pan too, round 190dz)
    c.misc.levels.volume = 1.0f;
    c.mixerTrack = 0;
    auto own = [channel] (const AutomationTarget& t) {
        return (t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan
                || (t.kind == AutomationTargetKind::channelAdjust && (t.param == leveladjust::pan || t.param == leveladjust::volume)))
               && t.index == channel;
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

// what drives an insert's fader, pan or sends (they apply live; a track freeze renders the insert without them)
bool drivesInsertMix (const AutomationTarget& t, int track)
{
    return (t.kind == AutomationTargetKind::mixerVolume || t.kind == AutomationTargetKind::mixerPan || t.kind == AutomationTargetKind::mixerSend)
           && t.index == track;
}

// what drives an insert's effects (a track freeze has them in its stems)
bool drivesInsertEffects (const AutomationTarget& t, int track)
{
    return (t.kind == AutomationTargetKind::effectParam || t.kind == AutomationTargetKind::effectMix) && t.index == track;
}

// what drives a channel's volume and pan (the Levels adjustment's too)
bool drivesChannelLevel (const AutomationTarget& t, int channel)
{
    return (t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan
            || (t.kind == AutomationTargetKind::channelAdjust && (t.param == leveladjust::pan || t.param == leveladjust::volume)))
           && t.index == channel;
}

// every driver of `gone` targets loses its target: automation, pattern event lanes, initial values, modulator routes
void detach (Song& s, const std::function<bool (const AutomationTarget&)>& gone)
{
    for (auto& a : s.automations)
        if (gone (a.target))
            a.target = {};
    for (auto& p : s.patterns)
        for (auto& lane : p.events)
            if (gone (lane.target))
                lane.target = {};
    s.initialValues.erase (std::remove_if (s.initialValues.begin(), s.initialValues.end(), [&] (const Song::InitialValue& v) { return gone (v.target); }),
                           s.initialValues.end());
    for (auto& m : s.modulators)
        m.routes.erase (std::remove_if (m.routes.begin(), m.routes.end(), [&] (const ModRoute& r) { return gone (r.target); }), m.routes.end());
}

// Track freeze (round 190ff): only `channel` heard, with its volume and pan, through its insert's effects — the insert
// at unity, its EQ, pan, separation, polarity, swap, sends and VCAs left out (they apply live), straight to the Master
void soloChannelThroughInsert (Song& s, int channel)
{
    for (int j = 0; j < (int) s.channels.size(); ++j)
        s.channels[(std::size_t) j].muted = j != channel;
    const auto t = s.channels[(std::size_t) channel].mixerTrack;
    neutralise (s);
    if (t <= 0 || t >= (int) s.mixer.size())
        return;
    auto& m = s.mixer[(std::size_t) t];
    m.sends.clear();
    m.lostSends.clear();
    m.toMaster = true;
    m.masterLevel = 1.0f;
    m.muted = false;
    m.solo = false;
    m.volume = unityFader;
    m.pan = 0.0f;
    m.stereo = 0.0f;
    m.eq = {};
    m.invertPhase = m.swapChannels = false;
    m.outputChannel = outputDefault;
    for (auto& v : s.vcas)
        v.members.erase (std::remove (v.members.begin(), v.members.end(), t), v.members.end());
    detach (s, [t] (const AutomationTarget& x) { return drivesInsertMix (x, t); });
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
// (round 190ff: `levelsInStem` — a track freeze rendered the volume and pan: the stand-in plays at unity and centre)
ChannelDef standInChannel (const ChannelDef& c, const std::string& path, bool levelsInStem = false)
{
    ChannelDef f;
    f.name = c.name;
    f.instrument = InstrumentKind::audioClip;
    f.samplePath = path;
    f.volume = levelsInStem ? 1.0f : c.volume;
    f.pan = levelsInStem ? 0.0f : c.pan;
    f.misc.levels.pan = levelsInStem ? 0.0f : c.misc.levels.pan; // (the Levels adjustment's level and pan too, round 190dz; not Mod X / Y)
    f.misc.levels.volume = levelsInStem ? 1.0f : c.misc.levels.volume;
    f.muted = c.muted;
    f.mixerTrack = c.mixerTrack;
    f.colour = c.colour;
    f.group = c.group;
    f.soloLock = c.soloLock;
    f.uid = c.uid;
    f.standIns = c.standIns;
    f.freeze = c.freeze;
    // declicking off, not a new audio clip channel's mode 1: no attack reshapes the start of the audio, and the clip
    // voice ends with a sharp cut at its note's end — in Pattern mode the next pass continues it sample for sample
    // (a release there would fade the file's held last frame into the next pass: a click at every loop wrap)
    f.sampler.declickMode = 0;
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
    return 4 * song.barTicks();
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

ChannelRender channelPatternRender (const Song& song, int channel, bool withInsert)
{
    ChannelRender r;
    if (channel < 0 || channel >= (int) song.channels.size())
        return r;
    r.tail = tailTicks (song);
    auto s = song;
    if (withInsert)
        soloChannelThroughInsert (s, channel);
    else
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
        seg.length = song.patterns[(std::size_t) p].effectiveLength (song.meter());
        s.patterns[(std::size_t) p].lengthTicks = seg.length; // (fixed: the render's clips have that length)
        seg.once = at;
        // the loop pass is the last of `runs` passes back to back; through the insert's effects (round 190ff) four:
        // a delay's or reverb's tail longer than a pass reaches over several, the fourth pass is close to the steady loop
        const int runs = withInsert ? 4 : 2;
        const auto loopRun = at + seg.length + r.tail;
        seg.loopFrom = loopRun + (runs - 1) * seg.length;
        std::vector<std::int64_t> starts { at };
        for (int k = 0; k < runs; ++k)
            starts.push_back (loopRun + k * seg.length);
        for (const auto start : starts)
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

Song channelTimelineRender (const Song& song, int channel, bool withInsert)
{
    auto s = song;
    if (channel >= 0 && channel < (int) s.channels.size())
    {
        if (withInsert)
            soloChannelThroughInsert (s, channel);
        else
            soloChannelRaw (s, channel);
    }
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
    return std::any_of (song.channels.begin(), song.channels.end(), [] (const ChannelDef& c) { return ! c.standIns.empty() || c.freeze.on(); })
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
    out.automationChunks = out.automationChunks || playsAutomation (song); // (round 190ff: the song's own chunking)
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
    // what drove the instrument's own parameters (automation, pattern events, modulators: a pitch bend plays on any
    // instrument, an audio clip too) is in the stem already: it must not play a second time on the audio (round 190ay)
    auto dropParamDrivers = [&] (int channel) {
        auto own = [channel] (const AutomationTarget& t) { return t.kind == AutomationTargetKind::channelParam && t.index == channel; };
        for (auto& a : out.automations)
            if (own (a.target))
                a.target = {};
        for (auto& p : out.patterns)
            for (auto& lane : p.events)
                if (own (lane.target))
                    lane.target = {};
        out.initialValues.erase (std::remove_if (out.initialValues.begin(), out.initialValues.end(),
                                                 [&] (const Song::InitialValue& v) { return own (v.target); }),
                                 out.initialValues.end());
        for (auto& m : out.modulators)
            m.routes.erase (std::remove_if (m.routes.begin(), m.routes.end(), [&] (const ModRoute& r) { return own (r.target); }), m.routes.end());
    };
    for (int c = 0; c < (int) song.channels.size(); ++c)
    {
        const auto& channel = song.channels[(std::size_t) c];
        // a frozen channel (round 190ay) plays its own stems, whatever its instrument and installed or not
        bool frozen = channel.freeze.on();
        // a track freeze (round 190ff) that no longer holds (the channel moved to another insert, something else plays
        // into it now) plays live, instrument and effects: never the effects twice or someone else's audio without them
        if (frozen && channel.freeze.insert != 0 && ! freeze::trackFreezeHolds (song, c))
        {
            out.channels[(std::size_t) c].freeze = {};
            frozen = false;
        }
        // its stems hold the channel's volume and pan and the insert's effects
        const bool baked = frozen && channel.freeze.insert > 0;
        const auto& stems = frozen ? channel.freeze.stems : channel.standIns;
        if (! frozen)
        {
            if (channel.standIns.empty() || ! channelUsesPlugin (channel))
                continue;
            std::vector<const PluginRef*> list;
            pluginsOf (channel, list);
            if (! anyMissing (list, missing))
                continue;
        }
        // (a channel routed into an insert that plays its own stand-in is in that stem already: it stays muted)
        const bool intoStem = out.channels[(std::size_t) c].muted && ! channel.muted;
        bool played = false;
        dropParamDrivers (c);
        // Song mode with nothing arranged plays nothing (it only loops over the current pattern's length): a frozen
        // channel is silent there too, and adds no clip (a stem clip would become the arrangement)
        const bool unarranged = frozen && songMode && song.playlist.empty() && patternblocks::end (song) <= 0;

        if (unarranged)
        {
            dropNotes (c);
            out.channels[(std::size_t) c] = standInChannel (channel, {}, baked);
        }
        else if (songMode)
        {
            const auto* timeline = find (stems, 0, false);
            const bool patternStems = std::any_of (stems.begin(), stems.end(), [] (const StandIn& s) { return s.pattern != 0 && ! s.loop; });
            dropNotes (c);
            if (timeline != nullptr && (needsTimeline (song, c) || ! patternStems))
            {
                out.channels[(std::size_t) c] = standInChannel (channel, timeline->path, baked);
                out.playlist.push_back (stemClip (c, hiddenRow, 0, std::max<std::int64_t> (1, timeline->lengthTicks), 0));
                played = true;
            }
            else
            {
                out.channels[(std::size_t) c] = standInChannel (channel, {}, baked);
                std::map<int, int> stemChannel; // pattern -> the channel playing its stem
                for (const auto& clip : song.playlist)
                {
                    if (clip.kind != ClipKind::pattern || clip.pattern < 0 || clip.pattern >= (int) song.patterns.size())
                        continue;
                    const auto* stem = find (stems, song.patterns[(std::size_t) clip.pattern].uid, false);
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
                        auto h = standInChannel (channel, stem->path, baked);
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
                // the channel's volume / pan automation drives the channels playing its stems too (not when the
                // stems hold them: a track freeze)
                for (int a = 0, count = baked ? 0 : (int) song.automations.size(); a < count; ++a)
                {
                    const auto& target = song.automations[(std::size_t) a].target;
                    if ((target.kind != AutomationTargetKind::channelVolume && target.kind != AutomationTargetKind::channelPan
                         && ! (target.kind == AutomationTargetKind::channelAdjust
                               && (target.param == leveladjust::pan || target.param == leveladjust::volume)))
                        || target.index != c)
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
            const auto* loop = song.patterns.empty() ? nullptr : find (stems, song.patterns[(std::size_t) p].uid, true);
            dropNotes (c);
            out.channels[(std::size_t) c] = standInChannel (channel, loop != nullptr ? loop->path : std::string(), baked);
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
        if (baked)
        {
            // the volume / pan and the insert's effects are in the stems: what drove them has nothing to drive, and
            // the effects are not built at all (their plugins unloaded); the insert's EQ, fader, pan and sends stay
            const auto insert = channel.freeze.insert;
            detach (out, [c] (const AutomationTarget& t) { return drivesChannelLevel (t, c); });
            detach (out, [insert] (const AutomationTarget& t) { return drivesInsertEffects (t, insert); });
            auto& track = out.mixer[(std::size_t) insert];
            track.effects.clear();
            track.chainBlend = 1.0f;
        }
        if (intoStem)
            out.channels[(std::size_t) c].muted = true;
        (void) played; // (listed either way: its plugin is missing here and it stands in — silent where no stem fits)
        if (projected != nullptr && ! frozen)
            projected->channels.push_back (c);
    }
    return out;
}
} // namespace lynx::standins

namespace lynx::freeze
{
#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)
#endif
const char* whyNot (const Song& song, int channel) noexcept
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return TR_NOOP ("There is no such channel.");
    const auto& c = song.channels[(std::size_t) channel];
    switch (c.instrument)
    {
        case InstrumentKind::audioClip:
            return TR_NOOP ("An audio clip is audio already: there is nothing to freeze.");
        case InstrumentKind::controller:
        case InstrumentKind::midiOut:
        case InstrumentKind::layer:
        case InstrumentKind::dashboard:
        case InstrumentKind::voltage:
        case InstrumentKind::vfx:
        case InstrumentKind::fruitydance:
        case InstrumentKind::wasp:
        case InstrumentKind::videoplayer:
            return TR_NOOP ("This channel makes no sound of its own: there is nothing to freeze.");
        default:
            break;
    }
    if (c.instrument == InstrumentKind::plugin && c.plugin.id.empty())
        return TR_NOOP ("This channel's plugin is not loaded: there is nothing to render.");
    // audio that leaves the channel on other inserts would be folded into the one stem
    if (c.instrument == InstrumentKind::plugin
        && std::any_of (c.plugin.outputs.begin(), c.plugin.outputs.end(), [] (const PluginOutputRoute& r) { return r.offset != 0; }))
        return TR_NOOP ("This plugin sends outputs to other mixer inserts (Processing > Connections): one frozen audio file could not keep them apart.");
    if (c.instrument == InstrumentKind::patcher && ! c.patcher.outputTracks.empty())
        return TR_NOOP ("This Patcher sends outputs to other mixer inserts: one frozen audio file could not keep them apart.");
    for (const auto& other : song.channels)
        if (other.instrument == InstrumentKind::layer
            && std::find (other.layer.children.begin(), other.layer.children.end(), channel) != other.layer.children.end())
            return TR_NOOP ("A Layer channel plays this channel: remove it from the Layer to freeze it.");
    // a controller link (Peak Controller, Envelope Controller...) moves the instrument's own knobs from what plays
    // around it: the render of this channel alone would not hear that, and the frozen audio would differ
    for (const auto& a : song.automations)
        if (a.isLive() && a.target.kind == AutomationTargetKind::channelParam && a.target.index == channel)
            return TR_NOOP ("A controller is linked to this instrument's knobs and moves them live: frozen audio could not follow it.");
    // launched clips have no place on the timeline: a stem cannot follow them
    if (song.performanceMode)
        return TR_NOOP ("Performance mode launches clips live: a frozen channel could not follow them.");
    if (song.clipGrid)
        for (const auto& clip : song.clipGrid->clips)
            if (clip.kind == ClipKind::pattern && clip.pattern >= 0 && clip.pattern < (int) song.patterns.size()
                && std::any_of (song.patterns[(std::size_t) clip.pattern].notes.begin(), song.patterns[(std::size_t) clip.pattern].notes.end(),
                                [channel] (const Note& n) { return n.channel == channel; }))
                return TR_NOOP ("This channel plays in clips of the clip grid, launched live: a frozen channel could not follow them.");
    return nullptr;
}

void apply (Song& song, std::uint64_t channelUid, std::vector<StandIn> stems, std::string source, int insert)
{
    if (channelUid == 0)
        return;
    for (auto& c : song.channels)
        if (c.uid == channelUid)
        {
            c.freeze.stems = std::move (stems);
            c.freeze.source = std::move (source);
            c.freeze.insert = insert;
            return;
        }
}

void clear (Song& song, int channel)
{
    if (channel >= 0 && channel < (int) song.channels.size())
        song.channels[(std::size_t) channel].freeze = {};
}

bool any (const Song& song) noexcept
{
    return std::any_of (song.channels.begin(), song.channels.end(), [] (const ChannelDef& c) { return c.freeze.on(); });
}

Song thawed (Song song)
{
    for (auto& c : song.channels)
        c.freeze = {};
    return song;
}

void lighten (Song& s, int channel, int keepInsert)
{
    for (int j = 0; j < (int) s.channels.size(); ++j)
    {
        if (j == channel)
            continue;
        const auto& old = s.channels[(std::size_t) j];
        ChannelDef quiet;
        quiet.name = old.name;
        quiet.uid = old.uid;
        quiet.muted = true;
        s.channels[(std::size_t) j] = std::move (quiet);
    }
    for (int t = 0; t < (int) s.mixer.size(); ++t)
        if (t != keepInsert)
            for (auto& e : s.mixer[(std::size_t) t].effects)
            {
                EffectSlot idle;
                idle.kind = EffectKind::utility;
                idle.enabled = false;
                e = std::move (idle);
            }
    // what drove the swapped units has nothing to drive (round 190ff: the kept insert's drivers stay)
    auto gone = [channel, keepInsert] (const AutomationTarget& t) {
        const bool ofChannel = t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan
                               || t.kind == AutomationTargetKind::channelParam || t.kind == AutomationTargetKind::channelAdjust;
        return (ofChannel && t.index != channel) || (refs::targetsInsert (t) && ! (keepInsert > 0 && t.index == keepInsert));
    };
    for (auto& a : s.automations)
        if (gone (a.target))
            a.target = {};
    for (auto& p : s.patterns)
        for (auto& lane : p.events)
            if (gone (lane.target))
                lane.target = {};
    s.initialValues.erase (std::remove_if (s.initialValues.begin(), s.initialValues.end(), [&] (const Song::InitialValue& v) { return gone (v.target); }),
                           s.initialValues.end());
}
} // namespace lynx::freeze

// --- Track freeze (round 190ff) ---------------------------------------------------------------------------------------
namespace lynx::freeze
{
using namespace lynx::standins; // (the projection's helpers)
namespace
{
// an effect that listens to another insert (side-chain, a plugin input taken from elsewhere) or sends audio out of
// its insert (Transmitter, Fruity Send, a plugin output to another insert, a Patcher's outputs)
bool slotReachesOut (const EffectSlot& slot)
{
    if (slot.sidechain >= 0 || slot.sidechain2 >= 0 || slot.sendTracks[0] != 0 || slot.sendTracks[1] != 0)
        return true;
    if (slot.kind == EffectKind::plugin)
        for (const auto* list : { &slot.plugin.inputs, &slot.plugin.outputs })
            if (std::any_of (list->begin(), list->end(), [] (const PluginOutputRoute& r) { return r.offset != 0; }))
                return true;
    for (const auto& b : slot.branches)
        if (std::any_of (b.chain.begin(), b.chain.end(), [] (const EffectSlot& x) { return slotReachesOut (x); }))
            return true;
    if (! slot.patcher.outputTracks.empty())
        return true;
    return std::any_of (slot.patcher.nodes.begin(), slot.patcher.nodes.end(), [] (const PatchNode& n) { return ! n.generator && slotReachesOut (n.effect); });
}

bool slotSendsTo (const EffectSlot& slot, int insert)
{
    if (slot.sendTracks[0] == insert || slot.sendTracks[1] == insert
        || std::find (slot.patcher.outputTracks.begin(), slot.patcher.outputTracks.end(), insert) != slot.patcher.outputTracks.end())
        return true;
    for (const auto& b : slot.branches)
        if (std::any_of (b.chain.begin(), b.chain.end(), [insert] (const EffectSlot& x) { return slotSendsTo (x, insert); }))
            return true;
    return std::any_of (slot.patcher.nodes.begin(), slot.patcher.nodes.end(), [insert] (const PatchNode& n) { return ! n.generator && slotSendsTo (n.effect, insert); });
}

// why `insert` cannot go into `channel`'s track freeze; nullptr = it can
const char* insertTaken (const Song& song, int channel, int insert) noexcept
{
    if (insert <= 0 || insert >= (int) song.mixer.size())
        return nullptr;
    for (int j = 0; j < (int) song.channels.size(); ++j)
    {
        if (j == channel)
            continue;
        const auto& other = song.channels[(std::size_t) j];
        const bool routed = other.instrument == InstrumentKind::plugin
                            && std::any_of (other.plugin.outputs.begin(), other.plugin.outputs.end(),
                                            [&] (const PluginOutputRoute& r) { return r.offset != 0 && other.mixerTrack + r.offset == insert; });
        const bool patched = std::find (other.patcher.outputTracks.begin(), other.patcher.outputTracks.end(), insert) != other.patcher.outputTracks.end();
        if (other.mixerTrack == insert || routed || patched)
            return TR_NOOP ("Another channel plays into this track's mixer insert: freezing its effects would freeze that channel too. Give the channel an insert of its own.");
    }
    for (int m = 0; m < (int) song.mixer.size(); ++m)
    {
        if (m == insert)
            continue;
        const auto& track = song.mixer[(std::size_t) m];
        if (std::any_of (track.sends.begin(), track.sends.end(), [insert] (const MixerSend& x) { return x.target == insert; })
            || std::any_of (track.effects.begin(), track.effects.end(), [insert] (const EffectSlot& e) { return slotSendsTo (e, insert); }))
            return TR_NOOP ("Another mixer insert sends audio into this track's insert: a frozen track could not hear it.");
    }
    const auto& own = song.mixer[(std::size_t) insert];
    if (own.inputChannel >= 0)
        return TR_NOOP ("This track's mixer insert takes an audio input: a frozen track could not play it.");
    if (std::any_of (own.effects.begin(), own.effects.end(), [] (const EffectSlot& e) { return slotReachesOut (e); }))
        return TR_NOOP ("An effect on this track listens to or sends to another mixer insert: frozen audio could not keep that.");
    for (const auto& a : song.automations)
        if (a.isLive() && drivesInsertEffects (a.target, insert))
            return TR_NOOP ("A controller is linked to this track's effects and moves them live: frozen audio could not follow it.");
    return nullptr;
}
} // namespace

int trackInsert (const Song& song, int channel) noexcept
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return 0;
    const auto t = song.channels[(std::size_t) channel].mixerTrack;
    return t > 0 && t < (int) song.mixer.size() ? t : 0;
}

const char* whyNotTrack (const Song& song, int channel) noexcept
{
    if (const auto* why = whyNot (song, channel))
        return why;
    return insertTaken (song, channel, trackInsert (song, channel));
}

bool trackFreezeHolds (const Song& song, int channel) noexcept
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return false;
    const auto& c = song.channels[(std::size_t) channel];
    return c.freeze.on() && c.freeze.insert > 0 && c.freeze.insert < (int) song.mixer.size() && c.mixerTrack == c.freeze.insert
           && insertTaken (song, channel, c.freeze.insert) == nullptr;
}

int frozenByInsert (const Song& song, int insert) noexcept
{
    if (insert <= 0)
        return -1;
    for (int c = 0; c < (int) song.channels.size(); ++c)
        if (song.channels[(std::size_t) c].freeze.insert == insert && trackFreezeHolds (song, c))
            return c;
    return -1;
}

bool flatten (Song& song, int channel)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return false;
    const auto old = song.channels[(std::size_t) channel];
    if (! old.freeze.on() || old.freeze.insert < 0 || (old.freeze.insert > 0 && ! trackFreezeHolds (song, channel)))
        return false;
    const auto* whole = standins::find (old.freeze.stems, 0, false);
    if (whole == nullptr)
        return false;
    const bool baked = old.freeze.insert > 0;

    // where the channel played: the unmuted pattern clips (and FL's legacy pattern blocks) holding its notes
    std::vector<Clip> placed;
    auto clips = song.playlist;
    for (const auto& block : patternblocks::asClips (song))
        clips.push_back (block);
    for (const auto& clip : clips)
        if (clip.kind == ClipKind::pattern && clip.gridSlot < 0 && clip.pattern >= 0 && clip.pattern < (int) song.patterns.size() && ! clip.muted
            && ! song.isTrackMuted (clip.track) && playsInPattern (song.patterns[(std::size_t) clip.pattern], channel))
            placed.push_back (clip);
    if (placed.empty())
        return false;
    std::stable_sort (placed.begin(), placed.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });

    // the stem is the whole song from tick 0 (what the frozen channel played, cut at the song's end as it was played):
    // each clip plays its own stretch of it, then a tail clip carries what rings on, up to the next clip
    const auto songEnd = song.songLength();
    const auto tail = tailTicks (song);
    std::vector<Clip> audio;
    auto add = [&] (const Clip& from, std::int64_t a, std::int64_t b) {
        Clip c;
        c.kind = ClipKind::audio;
        c.channel = channel;
        c.track = from.track;
        c.start = a;
        c.length = b - a;
        c.offset = a;
        c.gain = stemGain();
        audio.push_back (c);
    };
    std::int64_t cursor = std::numeric_limits<std::int64_t>::min(), reach = cursor;
    for (std::size_t k = 0; k < placed.size(); ++k)
    {
        const auto& clip = placed[k];
        const auto a = std::max (clip.start, cursor), b = std::min (clip.end(), songEnd);
        if (b > a)
        {
            add (clip, a, b);
            cursor = b;
        }
        reach = std::max (reach, std::min (clip.end() + tail, songEnd));
        const auto next = k + 1 < placed.size() ? placed[k + 1].start : songEnd;
        const auto tailEnd = std::min (next, reach);
        if (tailEnd > cursor && cursor > std::numeric_limits<std::int64_t>::min())
        {
            add (clip, cursor, tailEnd);
            cursor = tailEnd;
        }
    }
    if (audio.empty())
        return false;

    // the channel becomes the audio clip channel of its stem; its notes leave the patterns (an automatic length stays)
    const auto lengths = patternLengths (song);
    for (std::size_t p = 0; p < song.patterns.size(); ++p)
    {
        auto& pattern = song.patterns[p];
        if (! std::any_of (pattern.notes.begin(), pattern.notes.end(), [channel] (const Note& n) { return n.channel == channel; }))
            continue;
        if (pattern.lengthTicks <= 0 && p < lengths.size())
            pattern.lengthTicks = lengths[p];
        pattern.notes.erase (std::remove_if (pattern.notes.begin(), pattern.notes.end(), [channel] (const Note& n) { return n.channel == channel; }),
                             pattern.notes.end());
        pattern.channelLoops.erase (std::remove_if (pattern.channelLoops.begin(), pattern.channelLoops.end(),
                                                    [channel] (const ChannelLoop& l) { return l.channel == channel; }),
                                    pattern.channelLoops.end());
    }
    detach (song, [channel] (const AutomationTarget& t) { return t.kind == AutomationTargetKind::channelParam && t.index == channel; });
    auto flat = standInChannel (old, whole->path, baked);
    flat.freeze = {};
    flat.standIns.clear();
    flat.name = old.name + " (Bounce)"; // (the reference names the bounced track and its clips so: Live 12 manual)
    song.channels[(std::size_t) channel] = std::move (flat);
    if (baked)
    {
        const auto insert = old.freeze.insert;
        detach (song, [channel] (const AutomationTarget& t) { return drivesChannelLevel (t, channel); });
        detach (song, [insert] (const AutomationTarget& t) { return drivesInsertEffects (t, insert); });
        song.mixer[(std::size_t) insert].effects.clear();
        song.mixer[(std::size_t) insert].chainBlend = 1.0f;
        song.mixer[(std::size_t) insert].chainPreset.clear();
    }

    // the pattern clips left with nothing to play go (Live's MIDI clips become the audio clips)
    auto silent = [&song] (const Clip& c) {
        if (c.kind != ClipKind::pattern || c.gridSlot >= 0 || c.pattern < 0 || c.pattern >= (int) song.patterns.size())
            return false;
        const auto& p = song.patterns[(std::size_t) c.pattern];
        return p.notes.empty() && std::none_of (p.events.begin(), p.events.end(), [] (const PatternEvents& lane) { return lane.target.kind != AutomationTargetKind::none; });
    };
    song.playlist.erase (std::remove_if (song.playlist.begin(), song.playlist.end(), silent), song.playlist.end());
    for (const auto& c : audio)
        song.playlist.push_back (c);
    return true;
}
} // namespace lynx::freeze

// --- Bounce to New Track, Consolidate (round 190ff) -------------------------------------------------------------------
namespace lynx::freeze
{
using namespace lynx::standins;
namespace
{
// the selected Playlist clips that play (not automation clips), all on one track; their span
const char* selectedOnOneTrack (const Song& song, const std::vector<int>& selected, std::vector<int>& clips, int& track, std::int64_t& start, std::int64_t& end)
{
    clips.clear();
    track = -1;
    for (const auto i : selected)
    {
        if (i < 0 || i >= (int) song.playlist.size())
            continue;
        const auto& c = song.playlist[(std::size_t) i];
        if (c.kind == ClipKind::automation || c.gridSlot >= 0)
            continue;
        if (track >= 0 && c.track != track)
            return TR_NOOP ("Select clips of one track only.");
        track = c.track;
        clips.push_back (i);
    }
    if (clips.empty())
        return TR_NOOP ("Select the clips first.");
    std::sort (clips.begin(), clips.end());
    start = std::numeric_limits<std::int64_t>::max();
    end = std::numeric_limits<std::int64_t>::min();
    for (const auto i : clips)
    {
        start = std::min (start, song.playlist[(std::size_t) i].start);
        end = std::max (end, song.playlist[(std::size_t) i].end());
    }
    return end > start ? nullptr : TR_NOOP ("Select the clips first.");
}

// the song playing only `clips` (and the automation clips) over [start, end)
Song onlyClips (Song s, const Song& song, const std::vector<int>& clips, std::int64_t start, std::int64_t end)
{
    s.playlist.clear();
    for (const auto i : clips)
        s.playlist.push_back (song.playlist[(std::size_t) i]);
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::automation && ! c.muted && ! song.isTrackMuted (c.track))
            s.playlist.push_back (c);
    std::stable_sort (s.playlist.begin(), s.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    for (auto& t : s.playlistTracks)
        t.muted = false;
    s.patternBlocksOn = false; // (FL's legacy blocks are not clips of the plan)
    s.mode = PlayMode::song;
    s.loopStart = start;
    s.loopEnd = std::max (end, start + 1);
    return s;
}
} // namespace

const char* planBounceToNewTrack (const Song& song, const std::vector<int>& selected, BouncePlan& plan)
{
    plan = {};
    if (const auto* why = selectedOnOneTrack (song, selected, plan.clips, plan.track, plan.start, plan.end))
        return why;
    const auto chain = detailchain::forTrack (song, plan.track);
    if (chain.channel < 0 || chain.channel >= (int) song.channels.size())
        return TR_NOOP ("This track plays several channels: bounce one track that plays one instrument or one audio channel.");
    for (const auto i : plan.clips)
    {
        std::vector<int> played;
        detailchain::channelsOfClip (song, song.playlist[(std::size_t) i], played);
        if (std::any_of (played.begin(), played.end(), [&] (int c) { return c != chain.channel; }))
            return TR_NOOP ("This track plays several channels: bounce one track that plays one instrument or one audio channel.");
    }
    plan.channel = chain.channel;
    // an audio channel is audio already, but its track's effects are not: the insert's rules hold for both
    if (song.channels[(std::size_t) chain.channel].instrument != InstrumentKind::audioClip)
        if (const auto* why = whyNot (song, chain.channel))
            return why;
    plan.insert = trackInsert (song, chain.channel);
    if (const auto* why = insertTaken (song, chain.channel, plan.insert))
        return why;
    return nullptr;
}

Song bounceRenderSong (const Song& song, const BouncePlan& plan)
{
    auto s = onlyClips (thawed (song), song, plan.clips, plan.start, plan.end);
    s = channelTimelineRender (s, plan.channel, plan.insert > 0);
    lighten (s, plan.channel, plan.insert > 0 ? plan.insert : -1);
    s.loopStart = plan.start; // (neutralise cleared the range)
    s.loopEnd = std::max (plan.end, plan.start + 1);
    return s;
}

namespace
{
// an insert nothing uses (no channel, Playlist track or send into it, no effects, its default name); 0 = none
int unusedInsert (const Song& song)
{
    for (int t = 1; t < (int) song.mixer.size(); ++t)
    {
        const auto& m = song.mixer[(std::size_t) t];
        if (! m.effects.empty() || ! (m.name.empty() || m.name == "Insert " + std::to_string (t)) || m.inputChannel >= 0)
            continue;
        if (std::any_of (song.channels.begin(), song.channels.end(), [t] (const ChannelDef& c) { return c.mixerTrack == t; })
            || std::any_of (song.playlistTracks.begin(), song.playlistTracks.end(), [t] (const PlaylistTrack& p) { return p.mixerTrack == t; })
            || std::any_of (song.mixer.begin(), song.mixer.end(), [t] (const MixerTrack& o) {
                   return std::any_of (o.sends.begin(), o.sends.end(), [t] (const MixerSend& x) { return x.target == t; });
               }))
            continue;
        return t;
    }
    return 0;
}
} // namespace

int applyBounceToNewTrack (Song& song, const BouncePlan& plan, const std::string& samplePath, std::int64_t lengthTicks)
{
    if (plan.channel < 0 || plan.channel >= (int) song.channels.size())
        return -1;
    // the reference mutes the source clips (Live 12 manual, "Bounce to Audio")
    for (const auto i : plan.clips)
        if (i >= 0 && i < (int) song.playlist.size())
            song.playlist[(std::size_t) i].muted = true;
    const auto below = plan.track + 1;
    noteedit::insertTrack (song, below);
    const auto source = song.channels[(std::size_t) plan.channel];
    ChannelDef ch;
    ch.name = quickrender::uniqueChannelName (song, source.name + " (Bounce)");
    ch.instrument = InstrumentKind::audioClip;
    ch.sampler.declickMode = 0; // (the render's own edges: no attack reshapes its start)
    ch.samplePath = samplePath;
    ch.volume = audioClipUnityVolume;
    ch.colour = source.colour;
    // the new track keeps the source's mixer (the reference: volume, pan and sends "preserved in the new track's
    // settings"): an insert of its own with the source insert's fader, pan, EQ, sends and routing, its effects not
    // (they are in the audio). No free insert: the Master, at unity.
    ch.mixerTrack = 0;
    if (plan.insert > 0 && plan.insert < (int) song.mixer.size())
        if (const auto t = unusedInsert (song); t > 0)
        {
            const auto& from = song.mixer[(std::size_t) plan.insert];
            auto& to = song.mixer[(std::size_t) t];
            to.name = ch.name;
            to.colour = from.colour;
            to.volume = from.volume;
            to.pan = from.pan;
            to.stereo = from.stereo;
            to.invertPhase = from.invertPhase;
            to.swapChannels = from.swapChannels;
            to.eq = from.eq;
            to.sends = from.sends;
            to.toMaster = from.toMaster;
            to.masterLevel = from.masterLevel;
            to.outputChannel = from.outputChannel;
            to.outputMode = from.outputMode;
            ch.mixerTrack = t;
        }
    song.channels.push_back (ch);
    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = (int) song.channels.size() - 1;
    clip.track = below;
    clip.start = plan.start;
    clip.length = std::max<std::int64_t> (1, lengthTicks);
    song.playlist.push_back (clip);
    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    return clip.channel;
}

const char* planConsolidate (const Song& song, const std::vector<int>& selected, ConsolidatePlan& plan)
{
    plan = {};
    if (const auto* why = selectedOnOneTrack (song, selected, plan.clips, plan.track, plan.start, plan.end))
        return why;
    const auto& first = song.playlist[(std::size_t) plan.clips.front()];
    plan.audio = first.kind == ClipKind::audio;
    plan.channel = plan.audio ? first.channel : -1;
    for (const auto i : plan.clips)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if ((c.kind == ClipKind::audio) != plan.audio)
            return TR_NOOP ("Pattern clips and audio clips can't be consolidated together.");
        if (plan.audio && c.channel != plan.channel)
            return TR_NOOP ("Audio clips of one channel only can be consolidated.");
        if (! plan.audio)
        {
            if (c.pattern < 0 || c.pattern >= (int) song.patterns.size())
                return TR_NOOP ("Select the clips first.");
            const auto& p = song.patterns[(std::size_t) c.pattern];
            if (std::any_of (p.events.begin(), p.events.end(), [] (const PatternEvents& lane) { return lane.target.kind != AutomationTargetKind::none; })
                || ! p.channelLoops.empty())
                return TR_NOOP ("A pattern of these clips has event lanes or channel loops: they can't be joined into one pattern.");
        }
    }
    if (plan.audio && (plan.channel < 0 || plan.channel >= (int) song.channels.size()))
        return TR_NOOP ("Select the clips first.");
    if (! plan.audio && song.playTruncatedNotes)
        return TR_NOOP ("With Play truncated notes in clips on, a note cut at a clip's start plays there: these clips can't be joined into one pattern.");
    return nullptr;
}

int consolidatePatternClips (Song& song, const ConsolidatePlan& plan)
{
    if (plan.audio || plan.clips.empty())
        return -1;
    Pattern merged;
    const auto& firstClip = song.playlist[(std::size_t) plan.clips.front()];
    const auto& firstPattern = song.patterns[(std::size_t) firstClip.pattern];
    merged.name = firstPattern.name.empty() ? quickrender::patternName (song, firstClip.pattern) : firstPattern.name;
    merged.colour = firstPattern.colour;
    merged.lengthTicks = plan.end - plan.start;
    for (const auto i : plan.clips)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (c.muted)
            continue;
        const auto& p = song.patterns[(std::size_t) c.pattern];
        const auto length = std::max<std::int64_t> (1, p.effectiveLength (song.meter()));
        const auto passes = clipPasses (c, length);
        for (auto k = passes.first; k <= passes.last; ++k)
            for (const auto& n : p.notes)
            {
                if (passes.loops && n.start >= length)
                    continue;
                const auto at = k * length + n.start; // in the clip's pattern time
                if (at < c.offset || at >= c.offset + c.length)
                    continue;
                auto note = n;
                note.start = c.start + (at - c.offset) - plan.start;
                note.length = std::min (n.length, c.offset + c.length - at); // (cut at the clip's end, as it played)
                merged.notes.push_back (note);
            }
    }
    merged.sortNotes();
    song.patterns.push_back (std::move (merged));
    const auto pattern = (int) song.patterns.size() - 1;
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = pattern;
    clip.track = plan.track;
    clip.start = plan.start;
    clip.length = plan.end - plan.start;
    auto doomed = plan.clips;
    std::sort (doomed.rbegin(), doomed.rend());
    for (const auto i : doomed)
        song.playlist.erase (song.playlist.begin() + i);
    song.playlist.push_back (clip);
    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    return pattern;
}

Song consolidateRenderSong (const Song& song, const ConsolidatePlan& plan)
{
    auto s = onlyClips (thawed (song), song, plan.clips, plan.start, plan.end);
    s = channelTimelineRender (s, plan.channel, false);
    lighten (s, plan.channel);
    s.loopStart = plan.start;
    s.loopEnd = std::max (plan.end, plan.start + 1);
    return s;
}

int applyAudioConsolidation (Song& song, const ConsolidatePlan& plan, const std::string& samplePath, std::int64_t lengthTicks)
{
    if (! plan.audio || plan.channel < 0 || plan.channel >= (int) song.channels.size())
        return -1;
    auto ch = standInChannel (song.channels[(std::size_t) plan.channel], samplePath);
    ch.name = quickrender::uniqueChannelName (song, song.channels[(std::size_t) plan.channel].name);
    ch.uid = 0;
    ch.standIns.clear();
    ch.freeze = {};
    song.channels.push_back (ch);
    const auto channel = (int) song.channels.size() - 1;
    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = channel;
    clip.track = plan.track;
    clip.start = plan.start;
    clip.length = std::max<std::int64_t> (1, lengthTicks);
    clip.gain = stemGain();
    auto doomed = plan.clips;
    std::sort (doomed.rbegin(), doomed.rend());
    for (const auto i : doomed)
        song.playlist.erase (song.playlist.begin() + i);
    song.playlist.push_back (clip);
    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    return channel;
}
} // namespace lynx::freeze

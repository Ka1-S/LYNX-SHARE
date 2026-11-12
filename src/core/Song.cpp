#include "core/Song.h"
#include "core/TempoMap.h"
#include "core/ModelParams.h"
#include "core/FlSynths.h"
#include "core/VoltageController.h"
#include "core/MidiOut.h"
#include "core/PatternBlocks.h"
#include "core/MidiControls.h"
#include "core/SessionIds.h"
#include "core/FlCurve.h"

#include <algorithm>
#include <iterator>
#include <cmath>
#include <functional>
#include <limits>
#include <set>

namespace lynx
{
void Pattern::sortNotes()
{
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
}

std::int64_t Pattern::effectiveLength (int beatsPerBar) const noexcept
{
    if (lengthTicks > 0)
        return lengthTicks;

    const std::int64_t bar = (std::int64_t) ppq * std::max (1, beatsPerBar);
    std::int64_t end = 0;

    for (auto& n : notes)
        end = std::max (end, n.start + std::max<std::int64_t> (n.length, 1));

    return std::max<std::int64_t> (bar, (end + bar - 1) / bar * bar);
}

bool Pattern::hasStep (int channel, int step) const noexcept
{
    const auto tick = (std::int64_t) step * ticksPerStep;
    return std::any_of (notes.begin(), notes.end(),
                        [&] (const Note& n) { return n.channel == channel && n.start == tick && ! n.muted; });
}

void Pattern::toggleStep (int channel, int step, bool muteRemoved)
{
    // FL's SetStep (FLEngine FUN_012d4140, REA round 169f): with "Mute removed steps" on, turning a step off mutes its
    // notes and turning it on unmutes them; with the option off, turning it off deletes them (muted ones too) and
    // turning it on adds a new note (a muted one there stays as it is).
    const auto tick = (std::int64_t) step * ticksPerStep;
    const auto onStep = [&] (const Note& n) { return n.channel == channel && n.start == tick; };

    if (hasStep (channel, step))
    {
        if (muteRemoved)
            for (auto& n : notes)
                n.muted = n.muted || onStep (n);
        else
            notes.erase (std::remove_if (notes.begin(), notes.end(), onStep), notes.end());
        return;
    }

    if (muteRemoved && std::any_of (notes.begin(), notes.end(), onStep))
    {
        for (auto& n : notes)
            if (onStep (n))
                n.muted = false;
        return;
    }

    Note n;
    n.start = tick;
    n.channel = channel;
    notes.push_back (n);
    sortNotes();
}

std::int64_t Song::songLength() const noexcept
{
    const std::int64_t bar = (std::int64_t) ppq * std::max (1, beatsPerBar);
    std::int64_t end = 0;

    for (const auto& c : playlist)
        end = std::max (end, c.end());
    // Pattern Blocks (round 170f) play with the clips
    const auto blocksEnd = patternblocks::end (*this);
    end = std::max (end, blocksEnd);
    // video clips (round 174d): a project with only its picture so far plays to the picture's end, not one bar
    std::int64_t videoEndTicks = 0;
    if (const auto videoSeconds = videotrack::videoEnd (videoTracks); videoSeconds > 0.0)
        videoEndTicks = (std::int64_t) std::ceil (videotrack::ticksForSeconds (*this, videoSeconds) - 1e-6);
    end = std::max (end, videoEndTicks);
    const bool arranged = ! playlist.empty() || blocksEnd > 0 || videoEndTicks > 0;

    // an empty Playlist (e.g. recording the first take in Song mode) loops the current pattern's length, not one bar
    if (! arranged && currentPattern >= 0 && currentPattern < (int) patterns.size())
        end = patterns[(std::size_t) currentPattern].effectiveLength (beatsPerBar);

    // FL: Song mode loops at the exact end of the last clip (a clip ending at 3:05 loops at 3:05), not at the bar end
    if (arranged && end > 0)
        return end;
    return std::max<std::int64_t> (bar, (end + bar - 1) / bar * bar);
}

std::int64_t Song::performanceZoneEnd() const noexcept
{
    if (! performanceMode)
        return 0;
    for (const auto& m : markers())
        if (m.action == Marker::songStart)
            return std::max<std::int64_t> (0, m.tick);
    return 0;
}

void followPatternLengths (const std::vector<std::int64_t>& before, Song& song)
{
    if (before.size() != song.patterns.size())
        return; // patterns added, removed or moved: their clips are handled by that edit

    for (std::size_t i = 0; i < before.size(); ++i)
    {
        const auto now = song.patterns[i].effectiveLength (song.beatsPerBar);
        if (now == before[i] || before[i] <= 0)
            continue;

        song.forEachClipList ([&] (std::vector<Clip>& clips) {
            for (auto& c : clips)
                if (c.kind == ClipKind::pattern && c.pattern == (int) i && c.offset == 0 && c.length == before[i])
                    c.length = now;
        });
    }
}

namespace
{
template <typename Song_, typename Fn>
void forEachHostedPlugin (Song_& song, Fn&& fn)
{
    for (auto& c : song.channels)
        if (c.instrument == InstrumentKind::plugin && ! c.plugin.id.empty())
            fn (c.plugin);
    for (auto& track : song.mixer)
        for (auto& slot : track.effects)
            if (slot.kind == EffectKind::plugin && ! slot.plugin.id.empty())
                fn (slot.plugin);
}
} // namespace

namespace
{
// a plugin an edit may have just added: no saved state yet (not loaded with a project, not a clone) and still docked
bool looksJustAdded (const PluginRef& ref) { return ref.state.empty() && ! ref.detached; }
} // namespace

std::map<std::string, int> freshPluginCounts (const Song& song)
{
    std::map<std::string, int> counts;
    forEachHostedPlugin (song, [&counts] (const PluginRef& ref) {
        if (looksJustAdded (ref))
            ++counts[ref.id];
    });
    return counts;
}

int detachNewPlugins (const std::map<std::string, int>& before, Song& song)
{
    // only fresh plugins count on both sides, so a clone or a loaded plugin (with a state) that raises the number of
    // an id never gets an older fresh plugin of that id (one the user docked again) marked in its place
    auto added = freshPluginCounts (song);
    for (auto& [id, n] : added)
        if (const auto it = before.find (id); it != before.end())
            n -= it->second;

    // the newest of an id are the last ones: skip the fresh plugins that were already there
    std::map<std::string, int> seen;
    const auto now = freshPluginCounts (song);
    int marked = 0;
    forEachHostedPlugin (song, [&] (PluginRef& ref) {
        if (! looksJustAdded (ref))
            return;
        const auto index = seen[ref.id]++;
        const auto left = added[ref.id];
        if (left > 0 && index >= now.at (ref.id) - left)
        {
            ref.detached = true;
            ++marked;
        }
    });
    return marked;
}

int setAllPluginsDetached (Song& song, bool detached)
{
    int changed = 0;
    forEachHostedPlugin (song, [&] (PluginRef& ref) {
        if (ref.detached != detached)
        {
            ref.detached = detached;
            ++changed;
        }
    });
    return changed;
}

std::vector<std::int64_t> patternLengths (const Song& song)
{
    std::vector<std::int64_t> lengths;
    lengths.reserve (song.patterns.size());
    for (const auto& p : song.patterns)
        lengths.push_back (p.effectiveLength (song.beatsPerBar));
    return lengths;
}

void Song::removePattern (int index)
{
    if (index < 0 || index >= (int) patterns.size())
        return;

    patterns.erase (patterns.begin() + index);

    forEachClipList ([index] (std::vector<Clip>& clips) {
        clips.erase (std::remove_if (clips.begin(), clips.end(),
                                     [index] (const Clip& c) { return c.kind == ClipKind::pattern && c.pattern == index; }),
                     clips.end());

        for (auto& c : clips)
            if (c.kind == ClipKind::pattern && c.pattern > index)
                --c.pattern;
    });

    patternblocks::removePattern (*this, index); // (round 170f)

    // Loop Starter channels follow their pattern (round 168b)
    for (auto& c : channels)
        if (c.loopStarterPattern == index)
            c.loopStarterPattern = -1;
        else if (c.loopStarterPattern > index)
            --c.loopStarterPattern;

    if (currentPattern > index)
        --currentPattern;
    keepOnePattern(); // (the last one: an empty "Pattern 1" stays, as in FL)
}

bool Song::keepOnePattern()
{
    bool changed = false;
    if (patterns.empty())
    {
        patterns.emplace_back().name = "Pattern 1";
        changed = true;
    }
    const auto clamped = std::clamp (currentPattern, 0, (int) patterns.size() - 1);
    changed = changed || clamped != currentPattern;
    currentPattern = clamped;
    return changed;
}

const Pattern& Song::activePattern() const noexcept
{
    static const Pattern none;
    if (patterns.empty())
        return none;
    return patterns[(std::size_t) std::clamp (currentPattern, 0, (int) patterns.size() - 1)];
}

Pattern& Song::activePattern()
{
    keepOnePattern();
    return patterns[(std::size_t) currentPattern];
}

std::vector<MixerTrack> Song::defaultMixer (int inserts)
{
    std::vector<MixerTrack> mixer ((std::size_t) std::clamp (inserts + 1, 1, maxMixerTracks));
    mixer[0].name = "Master";

    for (std::size_t i = 1; i < mixer.size(); ++i)
        mixer[i].name = "Insert " + std::to_string (i);

    return mixer;
}

bool Song::insertInUse (int t) const
{
    // free = nothing uses it: no channel plays into it, no insert sends to it, no effects, input or Playlist track
    // link, the default name and routing. Round 157b: an FL project's effect buses (a vocal reverb/delay insert fed only
    // by sends) have no channel, and a sample added to the rack was routed into one (her beat through the vocal
    // H-Delay, reverb and Decapitator: "it sounds terrible").
    if (t <= 0 || t >= (int) mixer.size())
        return t == 0 && ! mixer.empty();
    auto used = [this] (int t) {
        const auto& m = mixer[(std::size_t) t];
        if (! m.effects.empty() || ! m.toMaster || m.inputChannel >= 0 || ! m.sends.empty()
            || (! m.name.empty() && m.name != "Insert " + std::to_string (t)))
            return true;
        if (std::any_of (channels.begin(), channels.end(), [t] (const ChannelDef& c) { return c.mixerTrack == t; }))
            return true;
        if (std::any_of (mixer.begin(), mixer.end(), [t] (const MixerTrack& o) {
                return std::any_of (o.sends.begin(), o.sends.end(), [t] (const MixerSend& send) { return send.target == t; });
            }))
            return true;
        return std::any_of (playlistTracks.begin(), playlistTracks.end(), [t] (const PlaylistTrack& p) { return p.mixerTrack == t; });
    };
    if (used (t))
        return true;
    // round 163 (hiding unused inserts): a fader, pan, mute or colour moved by hand, or automation on it, counts too
    const auto& m = mixer[(std::size_t) t];
    if (m.volume != unityFader || m.pan != 0.0f || m.muted || m.solo || m.colour != 0 || m.stereo != 0.0f || m.eq.hasBoostOrCut()
        || m.outputChannel != outputDefault)
        return true;
    if (std::any_of (mixer.begin(), mixer.end(), [t] (const MixerTrack& o) {
            return std::any_of (o.effects.begin(), o.effects.end(), [t] (const EffectSlot& e) { return e.sidechain == t || e.sidechain2 == t || e.sendTracks[0] == t || e.sendTracks[1] == t; });
        }))
        return true;
    return std::any_of (automations.begin(), automations.end(), [t] (const Automation& a) {
        const auto onTrack = a.target.kind == AutomationTargetKind::mixerVolume || a.target.kind == AutomationTargetKind::mixerPan
                             || a.target.kind == AutomationTargetKind::effectParam || a.target.kind == AutomationTargetKind::effectMix
                             || a.target.kind == AutomationTargetKind::mixerSend;
        return a.source.track == t || (onTrack && a.target.index == t);
    });
}

int Song::nextFreeInsert() const
{
    // Online Sessions (round 168a): a guest's new channels take the inserts reserved for him first, and nobody's new
    // channel takes an insert reserved for another participant
    if (const auto* me = session.self(); me != nullptr && me->insertCount > 0)
        for (int t = me->insertFirst; t < me->insertFirst + me->insertCount && t < (int) mixer.size(); ++t)
            if (t > 0 && ! insertInUse (t))
                return t;
    for (int t = 1; t < (int) mixer.size(); ++t)
        if (! insertInUse (t) && ! session.reservedForOther (true, t))
            return t;

    return 0; // every insert is in use: Master (FL's default for a new channel)
}

int Song::claimFreeInsert()
{
    // a guest's reserved range may lie past the mixer's end: it grows to reach it
    if (const auto* me = session.self(); me != nullptr && me->insertCount > 0 && ! mixer.empty())
        for (int t = std::max (1, me->insertFirst); t < me->insertFirst + me->insertCount && t < maxMixerTracks; ++t)
        {
            while ((int) mixer.size() <= t)
                mixer.emplace_back().name = "Insert " + std::to_string (mixer.size());
            if (! insertInUse (t))
                return t;
        }
    if (const auto t = nextFreeInsert(); t > 0)
        return t;
    if (mixer.empty() || (int) mixer.size() >= maxMixerTracks)
        return 0;
    // a new insert past the end, skipping other participants' reserved ones (they are added as plain inserts)
    while ((int) mixer.size() < maxMixerTracks)
    {
        const auto name = "Insert " + std::to_string (mixer.size());
        mixer.emplace_back().name = name;
        if (! session.reservedForOther (true, (int) mixer.size() - 1))
            return (int) mixer.size() - 1;
    }
    return 0;
}

void Song::removeChannel (int index)
{
    if (index < 0 || index >= (int) channels.size())
        return;

    channels.erase (channels.begin() + index);

    for (auto& p : patterns)
    {
        p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(), [index] (const Note& n) { return n.channel == index; }),
                       p.notes.end());

        for (auto& n : p.notes)
            if (n.channel > index)
                --n.channel;

        // the Channel Rack's loop points (round 165f-rack)
        auto& loops = p.channelLoops;
        loops.erase (std::remove_if (loops.begin(), loops.end(), [index] (const ChannelLoop& l) { return l.channel == index; }), loops.end());
        for (auto& l : loops)
            if (l.channel > index)
                --l.channel;
    }

    forEachClipList ([index] (std::vector<Clip>& clips) {
        clips.erase (std::remove_if (clips.begin(), clips.end(),
                                     [index] (const Clip& c) { return c.kind == ClipKind::audio && c.channel == index; }),
                     clips.end());

        for (auto& c : clips)
            if (c.kind == ClipKind::audio && c.channel > index)
                --c.channel;
    });

    forEachTarget ([index] (AutomationTarget& t) {
        const auto onChannel = t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan
                               || t.kind == AutomationTargetKind::channelParam;

        if (onChannel && t.index == index)
            t.kind = AutomationTargetKind::none;
        else if (onChannel && t.index > index)
            --t.index;
    });

    for (auto& t : playlistTracks) // instrument tracks (REA round 146a)
        if (t.channel == index)
        {
            t.channel = -1;
            if (t.mode == PlaylistTrackMode::instrument)
                t.mode = PlaylistTrackMode::unassigned, t.mixerTrack = -1;
        }
        else if (t.channel > index)
            --t.channel;

    for (auto& automation : automations) // live source on a controller channel
        if (automation.source.channel == index)
            automation.source.channel = -1, automation.target.kind = AutomationTargetKind::none;
        else if (automation.source.channel > index)
            --automation.source.channel;

    for (auto& c : channels) // Layer children (round 164)
    {
        auto& kids = c.layer.children;
        kids.erase (std::remove (kids.begin(), kids.end(), index), kids.end());
        for (auto& k : kids)
            if (k > index)
                --k;
    }

    removeDeadMidiLinks();
}

void Song::removeDeadMidiLinks()
{
    midiLinks.erase (std::remove_if (midiLinks.begin(), midiLinks.end(),
                                     [] (const MidiLink& l) { return l.target.kind == AutomationTargetKind::none; }),
                     midiLinks.end());
    initialValues.erase (std::remove_if (initialValues.begin(), initialValues.end(),
                                         [] (const InitialValue& v) { return v.target.kind == AutomationTargetKind::none; }),
                         initialValues.end());
}

std::vector<MidiLink> linksWithGlobal (const std::vector<MidiLink>& project, const std::vector<MidiLink>& global)
{
    auto out = project;
    for (const auto& g : global)
    {
        const auto overridden = std::any_of (project.begin(), project.end(), [&] (const MidiLink& p) {
            return p.target == g.target || (p.cc == g.cc && p.port == g.port && (p.channel < 0 || g.channel < 0 || p.channel == g.channel));
        });
        if (! overridden && g.target.kind != AutomationTargetKind::none)
            out.push_back (g);
    }
    return out;
}

bool linkOmniAvailable (AutomationTargetKind kind) noexcept
{
    return kind == AutomationTargetKind::channelVolume || kind == AutomationTargetKind::channelPan || kind == AutomationTargetKind::channelParam
        || kind == AutomationTargetKind::mixerVolume || kind == AutomationTargetKind::mixerPan || kind == AutomationTargetKind::effectParam
        || kind == AutomationTargetKind::effectMix;
}

AutomationTarget linkOmniTarget (const MidiLink& link, int selectedChannel, int selectedMixerTrack) noexcept
{
    auto t = link.target;
    if (! link.omni || ! linkOmniAvailable (t.kind))
        return t;
    const auto channelControl = t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan
                                || t.kind == AutomationTargetKind::channelParam;
    const auto index = channelControl ? selectedChannel : selectedMixerTrack;
    if (index >= 0)
        t.index = index;
    return t;
}

int countLinkConflicts (const std::vector<MidiLink>& links, const MidiLink& link)
{
    if (link.cc < 0)
        return 0;
    return (int) std::count_if (links.begin(), links.end(), [&] (const MidiLink& l) { return ! (l.target == link.target) && sameController (l, link); });
}

int setTargetLinks (std::vector<MidiLink>& links, const AutomationTarget& target, std::vector<MidiLink> edited, bool removeConflicts)
{
    edited.erase (std::remove_if (edited.begin(), edited.end(), [] (const MidiLink& l) { return l.cc < 0; }), edited.end());
    for (auto& l : edited)
        l.target = target;
    int removed = 0;
    if (removeConflicts)
    {
        const auto before = links.size();
        links.erase (std::remove_if (links.begin(), links.end(),
                                     [&] (const MidiLink& l) {
                                         return ! (l.target == target)
                                             && std::any_of (edited.begin(), edited.end(), [&] (const MidiLink& e) { return sameController (l, e); });
                                     }),
                     links.end());
        removed = (int) (before - links.size());
    }
    // the control's links keep the place of its first one
    const auto first = std::find_if (links.begin(), links.end(), [&] (const MidiLink& l) { return l.target == target; });
    const auto at = (std::size_t) (first - links.begin());
    std::vector<MidiLink> out;
    for (std::size_t i = 0; i < links.size(); ++i)
    {
        if (i == at)
            out.insert (out.end(), edited.begin(), edited.end());
        if (! (links[i].target == target))
            out.push_back (links[i]);
    }
    if (at >= links.size())
        out.insert (out.end(), edited.begin(), edited.end());
    links = std::move (out);
    return removed;
}

int Song::findInitialValue (const AutomationTarget& target) const noexcept
{
    for (int i = 0; i < (int) initialValues.size(); ++i)
        if (initialValues[(std::size_t) i].target == target)
            return i;
    return -1;
}

void Song::setInitialValue (const AutomationTarget& target, float value)
{
    if (target.kind == AutomationTargetKind::none)
        return;
    value = std::clamp (value, 0.0f, 1.0f);
    if (const auto i = findInitialValue (target); i >= 0)
        initialValues[(std::size_t) i].value = value;
    else
        initialValues.push_back ({ target, value });
}

void Song::removeInitialValue (const AutomationTarget& target)
{
    initialValues.erase (std::remove_if (initialValues.begin(), initialValues.end(),
                                         [&] (const InitialValue& v) { return v.target == target; }),
                         initialValues.end());
}

void Song::applyInitialValues (const std::function<void (const AutomationTarget&, float)>& plugin)
{
    for (const auto& v : initialValues)
        if (! setTargetValue (v.target, v.value) && plugin)
            plugin (v.target, v.value);
}

int fruitySendTarget (const EffectSlot& slot) noexcept
{
    if (slot.kind == EffectKind::patcher) // a Patcher's first extra output (REA_rodada141)
        return ! slot.patcher.outputTracks.empty() && slot.patcher.outputTracks.front() > 0 ? slot.patcher.outputTracks.front() : -1;
    if (slot.kind == EffectKind::flTransporter) // FL's Transporter: its loops' send (parameter 24, round 167f)
    {
        const auto t = (int) std::lround (std::clamp ((double) slot.params[24], 0.0, 1.0) * (maxMixerTracks - 1));
        return t > 0 ? t : -1;
    }
    if (slot.kind != EffectKind::fruitySend)
        return -1;
    const auto track = (int) std::lround (std::clamp ((double) slot.params[3], 0.0, 1.0) * (maxMixerTracks - 1));
    return track > 0 ? track : -1;
}

int slotSendTarget (const EffectSlot& slot, int output) noexcept
{
    if (slot.kind == EffectKind::transmitter || (slot.kind == EffectKind::equo && output == 0)) // (EQUO: its band sends, round 173d3)
        return output >= 0 && output < (int) slot.sendTracks.size() && slot.sendTracks[(std::size_t) output] > 0 ? slot.sendTracks[(std::size_t) output] : -1;
    return output == 0 ? fruitySendTarget (slot) : -1;
}

std::vector<SlotSendTap> fruitySendsOf (const Song& song, int track)
{
    std::vector<SlotSendTap> out;
    const auto n = (int) song.mixer.size();
    if (track <= 0 || track >= n)
        return out;
    const auto& slots = song.mixer[(std::size_t) track].effects;
    for (int s = 0; s < (int) slots.size() && s < maxEffectSlots; ++s)
    {
        const auto& slot = slots[(std::size_t) s];
        if (slot.kind == EffectKind::plugin)
        {
            // a hosted effect's output pairs that its Connections send elsewhere (round 189j), after its processing
            const auto c = slotConnections (song, track, slot);
            for (int o = 0; o < maxPluginConnections && c.routed; ++o)
                if (const auto target = c.outputs[(std::size_t) o]; target >= 0 && target < n && target != track)
                    out.push_back ({ s, target, o, true });
            continue;
        }
        for (int o = 0; o < maxSlotSends; ++o)
            if (const auto target = slotSendTarget (slot, o); target > 0 && target < n && target != track)
                out.push_back ({ s, target, o, slot.kind == EffectKind::transmitter || slot.kind == EffectKind::equo });
    }
    return out;
}

std::vector<int> insertInputList (const Song& song, int track)
{
    std::vector<int> list { track };
    for (int from = 1; from < (int) song.mixer.size(); ++from)
        if (from != track && song.routes (from, track))
            list.push_back (from);
    return list;
}

std::vector<int> insertOutputList (const Song& song, int track)
{
    std::vector<int> list { track };
    if (track <= 0 || track >= (int) song.mixer.size())
        return list;
    for (int to = 0; to < (int) song.mixer.size(); ++to)
        if (to != track && song.routes (track, to))
            list.push_back (to);
    return list;
}

SlotConnections slotConnections (const Song& song, int track, const EffectSlot& slot)
{
    SlotConnections c;
    const auto n = (int) song.mixer.size();
    const auto& ref = slot.plugin;
    if (slot.kind != EffectKind::plugin || track <= 0 || track >= n || (ref.inputs.empty() && ref.outputs.empty()))
        return c;
    c.routed = true;
    // an inactive input / output: silent, unless "Process inactive inputs and outputs" (on by default) [EXEC]
    auto on = [&ref] (const PluginOutputRoute& r) { return r.isActive (true) || ref.processInactive; };
    const auto at = [] (const std::vector<PluginOutputRoute>& list, std::size_t k) { return k < list.size() ? list[k] : PluginOutputRoute {}; };

    // index k = entry k of the inputs list; 0 or past the list = the slot's own input [EXEC e01 e02 e10]
    std::vector<int> sources;
    auto source = [&] (int k) {
        if (k <= 0)
            return -1;
        if (sources.empty())
            sources = insertInputList (song, track);
        return k < (int) sources.size() ? sources[(std::size_t) k] : -1;
    };
    const auto in1 = at (ref.inputs, 0), in2 = at (ref.inputs, 1);
    c.mainSource = on (in1) ? source (in1.offset) : -2;
    // input 2: the slot's side input source (LYNX's SC picker, or its index, which sets it: core/MixerOps.h), else
    // its index; without either the slot's own input [EXEC e00: FL feeds an input at index 0 the insert's audio]
    if (! on (in2))
        c.sideSource = -2;
    else if (slot.sidechain > 0 && slot.sidechain != track && slot.sidechain < n)
        c.sideSource = slot.sidechain;
    else
        c.sideSource = source (in2.offset);

    // outputs: index 0 = into the insert; k = entry k of the outputs list, past its end the last one [EXEC e05 e07 e11
    // e12 e13 e18 e19]; (a negative index, which FL's wheel never gives, went to the Master past the fader, e17 e23)
    std::vector<int> targets;
    for (int o = 0; o < maxPluginConnections; ++o)
    {
        const auto r = ref.outputRoute (o);
        if (! on (r))
            c.outputs[(std::size_t) o] = -2;
        else if (r.offset < 0)
            c.outputs[(std::size_t) o] = 0;
        else if (r.offset > 0)
        {
            if (targets.empty())
                targets = insertOutputList (song, track);
            if (targets.size() > 1)
                c.outputs[(std::size_t) o] = targets[(std::size_t) std::min (r.offset, (int) targets.size() - 1)];
        }
    }
    return c;
}

bool Song::routes (int from, int to) const noexcept
{
    if (from <= 0 || from >= (int) mixer.size())
        return false;

    const auto& t = mixer[(std::size_t) from];

    if (to == 0)
        return t.toMaster;

    return std::any_of (t.sends.begin(), t.sends.end(), [to] (const MixerSend& s) { return s.target == to; });
}

float Song::routeLevel (int from, int to) const noexcept
{
    if (! routes (from, to))
        return 0.0f;

    const auto& t = mixer[(std::size_t) from];

    if (to == 0)
        return t.masterLevel;

    for (const auto& s : t.sends)
        if (s.target == to)
            return s.level;

    return 0.0f;
}

void Song::setRouteLevel (int from, int to, float level)
{
    if (! routes (from, to))
        return;

    const auto v = std::clamp (level, 0.0f, maxFaderGain);
    auto& t = mixer[(std::size_t) from];

    if (to == 0)
        t.masterLevel = v;
    else
        for (auto& s : t.sends)
            if (s.target == to)
                s.level = v;
}

bool Song::sendPre (int from, int to) const noexcept
{
    if (to <= 0 || ! routes (from, to))
        return false;
    for (const auto& s : mixer[(std::size_t) from].sends)
        if (s.target == to)
            return s.pre;
    return false;
}

void Song::setSendPre (int from, int to, bool pre)
{
    if (to <= 0 || ! routes (from, to))
        return;
    for (auto& s : mixer[(std::size_t) from].sends)
        if (s.target == to)
            s.pre = pre;
}

bool Song::canRoute (int from, int to) const
{
    const auto n = (int) mixer.size();

    if (from <= 0 || from >= n || to < 0 || to >= n || from == to)
        return false;

    if (to == 0)
        return true;

    // cycle if `from` is already reachable from `to`
    std::vector<int> stack { to };
    std::vector<bool> seen ((std::size_t) n, false);

    while (! stack.empty())
    {
        const auto t = stack.back();
        stack.pop_back();

        if (t == from)
            return false;

        if (seen[(std::size_t) t])
            continue;

        seen[(std::size_t) t] = true;

        for (const auto& s : mixer[(std::size_t) t].sends)
            if (s.target > 0 && s.target < n)
                stack.push_back (s.target);
        for (const auto& fx : mixer[(std::size_t) t].effects)
            for (int o = 0; o < maxSlotSends; ++o)
                if (const auto target = slotSendTarget (fx, o); target > 0 && target < n)
                    stack.push_back (target);
    }

    return true;
}

void Song::setRoute (int from, int to, bool enabled, float level)
{
    if (from <= 0 || from >= (int) mixer.size() || to < 0 || to >= (int) mixer.size())
        return;

    auto& t = mixer[(std::size_t) from];

    if (to == 0)
    {
        t.toMaster = enabled;
        return;
    }

    auto it = std::find_if (t.sends.begin(), t.sends.end(), [to] (const MixerSend& s) { return s.target == to; });

    if (! enabled)
    {
        if (it != t.sends.end())
            t.sends.erase (it);
        return;
    }

    if (it != t.sends.end())
        it->level = std::clamp (level, 0.0f, maxFaderGain);
    else if (canRoute (from, to))
        t.sends.push_back ({ to, std::clamp (level, 0.0f, maxFaderGain) });
}

std::vector<int> Song::mixerOrder() const
{
    const auto n = (int) mixer.size();
    std::vector<int> incoming ((std::size_t) std::max (0, n), 0);

    // edges: sends and sidechains (the sidechain source must be processed before whoever listens to it)
    std::vector<std::vector<int>> next ((std::size_t) std::max (0, n));
    for (int t = 1; t < n; ++t)
    {
        for (const auto& s : mixer[(std::size_t) t].sends)
            if (s.target > 0 && s.target < n)
                next[(std::size_t) t].push_back (s.target);
        for (const auto& fx : mixer[(std::size_t) t].effects) // Fruity Send / Transmitter slots: the destination comes after
            for (int o = 0; o < maxSlotSends; ++o)
                if (const auto target = slotSendTarget (fx, o); target > 0 && target < n && target != t)
                    next[(std::size_t) t].push_back (target);
        for (const auto& fx : mixer[(std::size_t) t].effects)
            for (const auto source : { fx.sidechain, fx.sidechain2 })
                if (source > 0 && source < n && source != t)
                    next[(std::size_t) source].push_back (t);
    }
    for (int t = 1; t < n; ++t)
        for (const auto target : next[(std::size_t) t])
            ++incoming[(std::size_t) target];

    // Kahn: start with the tracks nothing feeds (in numeric order)
    std::vector<int> order, ready;

    for (int t = n - 1; t >= 1; --t)
        if (incoming[(std::size_t) t] == 0)
            ready.push_back (t);

    while (! ready.empty())
    {
        const auto t = ready.back();
        ready.pop_back();
        order.push_back (t);

        for (const auto target : next[(std::size_t) t])
            if (--incoming[(std::size_t) target] == 0)
            {
                // keep numeric order among those that become ready together
                auto pos = std::upper_bound (ready.begin(), ready.end(), target, std::greater<int>());
                ready.insert (pos, target);
            }
    }

    // cycle (corrupt file): the leftovers go at the end, without the sends that close the cycle
    for (int t = 1; t < n; ++t)
        if (std::find (order.begin(), order.end(), t) == order.end())
            order.push_back (t);

    return order;
}

std::vector<bool> Song::soloAudible() const
{
    const auto n = mixer.size();
    std::vector<bool> audible (n, true);
    std::vector<int> soloed;

    for (std::size_t t = 1; t < n; ++t)
        if (mixer[t].solo)
            soloed.push_back ((int) t);

    if (soloed.empty())
        return audible;

    std::fill (audible.begin(), audible.end(), false);

    if (n > 0)
        audible[0] = true;

    // downstream: the signal path from the soloed tracks to Master
    std::vector<int> stack = soloed;

    while (! stack.empty())
    {
        const auto t = stack.back();
        stack.pop_back();

        if (audible[(std::size_t) t])
            continue;

        audible[(std::size_t) t] = true;

        for (const auto& s : mixer[(std::size_t) t].sends)
            if (s.target > 0 && s.target < (int) n)
                stack.push_back (s.target);
        for (const auto& fx : mixer[(std::size_t) t].effects)
            for (int o = 0; o < maxSlotSends; ++o)
                if (const auto target = slotSendTarget (fx, o); target > 0 && target < (int) n)
                    stack.push_back (target);
    }

    // upstream: whoever feeds the soloed tracks (those soloed with their sources: FL's plain solo leaves them out)
    std::vector<bool> feeds (n, false);
    stack.clear();
    for (auto t : soloed)
        if (mixer[(std::size_t) t].soloSources)
            stack.push_back (t);

    while (! stack.empty())
    {
        const auto t = stack.back();
        stack.pop_back();

        for (std::size_t src = 1; src < n; ++src)
            if (! feeds[src]
                && (routes ((int) src, t)
                    || std::any_of (mixer[src].effects.begin(), mixer[src].effects.end(),
                                    [t] (const EffectSlot& fx) { return slotSendTarget (fx, 0) == t || slotSendTarget (fx, 1) == t; })))
            {
                feeds[src] = true;
                audible[src] = true;
                stack.push_back ((int) src);
            }
    }

    return audible;
}

void Song::uniqueInstances()
{
    std::set<std::uint64_t> seen;
    const auto unique = [&seen] (InstanceTag& tag) {
        if (! seen.insert (tag.id).second)
        {
            tag.renew();
            seen.insert (tag.id);
        }
    };
    for (auto& c : channels)
        unique (c.instance);
    seen.clear();
    for (auto& t : mixer)
        for (auto& slot : t.effects)
            unique (slot.instance);
}

void Song::swapEffectSlots (int track, int a, int b)
{
    if (track < 0 || track >= (int) mixer.size())
        return;

    auto& fx = mixer[(std::size_t) track].effects;

    if (a < 0 || b < 0 || a >= (int) fx.size() || b >= (int) fx.size())
        return;

    std::swap (fx[(std::size_t) a], fx[(std::size_t) b]);

    for (auto& automation : automations)
        if (automation.source.track == track)
            automation.source.slot = automation.source.slot == a ? b : automation.source.slot == b ? a : automation.source.slot;

    forEachTarget ([track, a, b] (AutomationTarget& t) {
        if ((t.kind == AutomationTargetKind::effectParam || t.kind == AutomationTargetKind::effectMix) && t.index == track)
            t.slot = t.slot == a ? b : t.slot == b ? a : t.slot;
    });
}

void Song::removeEffectSlot (int track, int slot)
{
    if (track < 0 || track >= (int) mixer.size() || slot < 0 || slot >= (int) mixer[(std::size_t) track].effects.size())
        return;

    auto& fx = mixer[(std::size_t) track].effects;
    fx.erase (fx.begin() + slot);

    for (auto& automation : automations)
        if (automation.source.track == track)
        {
            if (automation.source.slot == slot)
                automation.source.track = -1, automation.target.kind = AutomationTargetKind::none; // the source is gone
            else if (automation.source.slot > slot)
                --automation.source.slot;
        }

    forEachTarget ([track, slot] (AutomationTarget& t) {
        if ((t.kind == AutomationTargetKind::effectParam || t.kind == AutomationTargetKind::effectMix) && t.index == track)
        {
            if (t.slot == slot)
                t.kind = AutomationTargetKind::none;
            else if (t.slot > slot)
                --t.slot;
        }
    });

    removeDeadMidiLinks();
}

void Song::removeAutomation (int index)
{
    if (index < 0 || index >= (int) automations.size())
        return;

    automations.erase (automations.begin() + index);

    for (auto& track : playlistTracks) // round 177b: lanes follow the renumbering; the removed one's lane goes
    {
        track.lanes.erase (std::remove_if (track.lanes.begin(), track.lanes.end(), [index] (const AutomationLane& l) { return l.automation == index; }),
                           track.lanes.end());
        for (auto& l : track.lanes)
            if (l.automation > index)
                --l.automation;
    }

    forEachClipList ([index] (std::vector<Clip>& clips) {
        clips.erase (std::remove_if (clips.begin(), clips.end(),
                                     [index] (const Clip& c) { return c.kind == ClipKind::automation && c.automation == index; }),
                     clips.end());

        for (auto& c : clips)
            if (c.kind == ClipKind::automation && c.automation > index)
                --c.automation;
    });
}

std::vector<Marker>& Song::markers()
{
    if (arrangements.empty())
        arrangements.push_back ({ "Arrangement 1", {}, 0, 0, {} });

    return arrangements[(std::size_t) std::clamp (currentArrangement, 0, (int) arrangements.size() - 1)].markers;
}

const std::vector<Marker>& Song::markers() const
{
    static const std::vector<Marker> none;
    return arrangements.empty() ? none : arrangements[(std::size_t) std::clamp (currentArrangement, 0, (int) arrangements.size() - 1)].markers;
}

void Song::addMarker (Marker marker)
{
    auto& list = markers();
    const auto at = std::upper_bound (list.begin(), list.end(), marker.tick, [] (std::int64_t t, const Marker& m) { return t < m.tick; });
    list.insert (at, std::move (marker));
}

std::vector<Bar> Song::barsBetween (std::int64_t from, std::int64_t to) const
{
    std::vector<Bar> bars;
    auto barLength = [] (int numerator, int denominator) {
        return std::max<std::int64_t> (1, (std::int64_t) ppq * 4 * std::max (1, numerator) / std::max (1, denominator));
    };

    Bar bar { 0, barLength (beatsPerBar, 4), 1 };
    const auto& list = markers();
    std::size_t next = 0;

    while (bar.tick < to)
    {
        // time signature changes falling at the start of this bar apply to it
        while (next < list.size() && (! list[next].isSignature() || list[next].tick <= bar.tick))
        {
            if (list[next].isSignature())
                bar.length = barLength (list[next].numerator, list[next].denominator);
            ++next;
        }

        // the next change cuts this bar short and starts a new one there
        const auto end = next < list.size() ? std::min (bar.tick + bar.length, list[next].tick) : bar.tick + bar.length;

        if (end > from)
            bars.push_back ({ bar.tick, end - bar.tick, bar.number });

        bar = { end, bar.length, bar.number + 1 };

        // no changes ahead: the rest is arithmetic (avoids walking bar by bar up to `from`)
        if (bar.tick < from && next >= list.size())
        {
            const auto skip = (from - bar.tick) / bar.length;
            bar.tick += skip * bar.length;
            bar.number += (int) skip;
        }
    }

    return bars;
}

Bar Song::barAt (std::int64_t tick) const
{
    const auto bars = barsBetween (std::max<std::int64_t> (0, tick), std::max<std::int64_t> (0, tick) + 1);
    return bars.empty() ? Bar { 0, (std::int64_t) ppq * std::max (1, beatsPerBar), 1 } : bars.front();
}

void Song::selectArrangement (int index)
{
    if (arrangements.empty())
        arrangements.push_back ({ "Arrangement 1", {}, 0, 0, {} });

    currentArrangement = std::clamp (currentArrangement, 0, (int) arrangements.size() - 1);
    index = std::clamp (index, 0, (int) arrangements.size() - 1);

    if (index == currentArrangement)
        return;

    auto& from = arrangements[(std::size_t) currentArrangement];
    from.clips = std::move (playlist);
    from.blocks = std::move (blocks);
    from.loopStart = loopStart;
    from.loopEnd = loopEnd;

    auto& to = arrangements[(std::size_t) index];
    playlist = std::move (to.clips);
    to.clips.clear();
    blocks = std::move (to.blocks);
    to.blocks.clear();
    loopStart = to.loopStart;
    loopEnd = to.loopEnd;
    currentArrangement = index;
}

int Song::addArrangement (const std::string& name, bool copyCurrent)
{
    Arrangement a;
    a.name = name;

    if (copyCurrent)
    {
        a.clips = playlist;
        a.blocks = blocks;
        a.loopStart = loopStart;
        a.loopEnd = loopEnd;
        a.markers = markers();
    }

    arrangements.push_back (std::move (a));
    selectArrangement ((int) arrangements.size() - 1);
    return currentArrangement;
}

void Song::removeArrangement (int index)
{
    if (arrangements.size() <= 1 || index < 0 || index >= (int) arrangements.size())
        return;

    if (index == currentArrangement)
        selectArrangement (index == 0 ? 1 : index - 1);

    arrangements.erase (arrangements.begin() + index);

    if (currentArrangement > index)
        --currentArrangement;
}

namespace
{
// The value field behind output k of a panel (a Dashboard's: its control's value, an XY pad's X then Y)
const float* surfaceOutputValue (const std::vector<SurfaceControl>& controls, int k) noexcept
{
    if (k < 0)
        return nullptr;
    for (const auto& c : controls)
    {
        if (k < c.outputs())
            return k == 0 ? &c.value : &c.valueY;
        k -= c.outputs();
    }
    return nullptr;
}
float* surfaceOutputValue (std::vector<SurfaceControl>& controls, int k) noexcept
{
    return const_cast<float*> (surfaceOutputValue (std::as_const (controls), k));
}
} // namespace

namespace samplerparam
{
namespace
{
#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif
struct Range
{
    const char* name;
    float lo, hi, mid; // mid < 0: linear (else the knob's skew from its mid point, like juce::Slider)
};
constexpr Range ranges[count] = { { TR_NOOP ("Attack"), 0.0f, 2.0f, 0.2f },    { TR_NOOP ("Decay"), 0.0f, 10.0f, 0.5f },
                                  { TR_NOOP ("Sustain"), 0.0f, 1.0f, -1.0f },  { TR_NOOP ("Release"), 0.0f, 4.0f, 0.4f },
                                  { TR_NOOP ("Cutoff"), 20.0f, 20000.0f, 1000.0f }, { TR_NOOP ("Resonance"), 0.0f, 1.0f, -1.0f },
                                  { TR_NOOP ("Start"), 0.0f, 1.0f, -1.0f },    { TR_NOOP ("End"), 0.0f, 1.0f, -1.0f } };

double skewOf (const Range& r) noexcept
{
    return r.mid < 0.0f ? 1.0 : std::log (0.5) / std::log ((double) (r.mid - r.lo) / (double) (r.hi - r.lo));
}
} // namespace

const char* name (int param) noexcept
{
    return param >= 0 && param < count ? ranges[param].name : "";
}

float toValue (int param, float natural) noexcept
{
    if (param < 0 || param >= count)
        return 0.0f;
    const auto& r = ranges[param];
    const auto p = std::clamp ((double) (natural - r.lo) / (double) (r.hi - r.lo), 0.0, 1.0);
    return (float) std::pow (p, skewOf (r));
}

float toNatural (int param, float value) noexcept
{
    if (param < 0 || param >= count)
        return 0.0f;
    const auto& r = ranges[param];
    const auto v = std::clamp ((double) value, 0.0, 1.0);
    const auto p = v <= 0.0 ? 0.0 : std::exp (std::log (v) / skewOf (r));
    return (float) (r.lo + (r.hi - r.lo) * p);
}

float get (const SamplerSettings& s, int param) noexcept
{
    switch (param)
    {
        case attack: return toValue (param, s.attack);
        case decay: return toValue (param, s.decay);
        case sustain: return toValue (param, s.sustain);
        case release: return toValue (param, s.release);
        case cutoff: return toValue (param, s.cutoff);
        case resonance: return toValue (param, s.resonance);
        case start: return toValue (param, s.regionStart);
        case end: return toValue (param, s.regionEnd);
        default: return 0.0f;
    }
}

void set (SamplerSettings& s, int param, float value) noexcept
{
    const auto x = toNatural (param, value);
    switch (param)
    {
        case attack: s.attack = x; break;
        case decay: s.decay = x; break;
        case sustain: s.sustain = x; break;
        case release: s.release = x; break;
        case cutoff: s.cutoff = x; break;
        case resonance: s.resonance = x; break;
        case start: s.regionStart = x; break;
        case end: s.regionEnd = x; break;
        default: return;
    }
    // like editing the knob by hand: the envelope / filter replace FL's imported voice (REA_rodada69 §3, REA_rodada76)
    if (param <= release)
    {
        s.flVolumeEnvelope.clear();
        s.flmPad.clear();
    }
    if (param <= resonance)
        s.flChannel.clear();
}
} // namespace samplerparam

float Song::targetValue (const AutomationTarget& t) const noexcept
{
    const auto validChannel = t.index >= 0 && t.index < (int) channels.size();
    const auto validTrack = t.index >= 0 && t.index < (int) mixer.size();

    switch (t.kind)
    {
        case AutomationTargetKind::channelVolume: return validChannel ? channels[(std::size_t) t.index].volume : -1.0f;
        case AutomationTargetKind::channelPan: return validChannel ? (channels[(std::size_t) t.index].pan + 1.0f) * 0.5f : -1.0f;
        case AutomationTargetKind::mixerVolume: return validTrack ? mixer[(std::size_t) t.index].volume : -1.0f;
        case AutomationTargetKind::mixerPan: return validTrack ? (mixer[(std::size_t) t.index].pan + 1.0f) * 0.5f : -1.0f;
        case AutomationTargetKind::effectParam:
            if (validTrack && t.slot >= 0 && t.slot < (int) mixer[(std::size_t) t.index].effects.size()
                && mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].kind != EffectKind::plugin && t.param >= 0
                && t.param < maxEffectParams)
                return mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].params[(std::size_t) t.param];
            return validTrack ? 0.5f : -1.0f; // plugin parameter: the value lives in the plugin
        case AutomationTargetKind::channelParam:
            if (! validChannel)
                return -1.0f;
            // an Envelope Controller's global Mod X / Y (round 164f): the value lives in its state
            if (channels[(std::size_t) t.index].instrument == InstrumentKind::controller
                && (t.param == envelopeControllerModXParam || t.param == envelopeControllerModYParam))
                if (const auto ec = parseEnvelopeController (channels[(std::size_t) t.index].controllerState))
                    return t.param == envelopeControllerModXParam ? ec->modX : ec->modY;
            // a Dashboard's output k (its control's value), the Voltage Controller's offsets / scales (round 165)
            if (channels[(std::size_t) t.index].instrument == InstrumentKind::dashboard)
                if (const auto* v = surfaceOutputValue (channels[(std::size_t) t.index].patcher.surface, t.param))
                    return *v;
            if (channels[(std::size_t) t.index].instrument == InstrumentKind::voltage && t.param >= 0 && t.param < voltage::paramCount)
                if (const auto vs = voltage::parse (channels[(std::size_t) t.index].pluginState))
                    return voltage::paramValue (*vs, t.param);
            // the Sampler's knobs (issue #12)
            if (channels[(std::size_t) t.index].instrument == InstrumentKind::sampler && t.param >= 0 && t.param < samplerparam::count)
                return samplerparam::get (channels[(std::size_t) t.index].sampler, t.param);
            // a MIDI Out's patch and knobs (round 167i)
            if (channels[(std::size_t) t.index].instrument == InstrumentKind::midiOut && t.param >= 0 && t.param <= midiout::controlCount)
                return midiout::paramValue (midiout::stateOf (channels[(std::size_t) t.index].pluginState), t.param);
            // the knobs of an FL engine kept in the channel's state (round 168n)
            if (const auto v = modelparams::value01 (channels[(std::size_t) t.index], t.param))
                return *v;
            return 0.5f;
        case AutomationTargetKind::mixerSend:
            return validTrack && routes (t.index, t.slot) ? faderPosition (routeLevel (t.index, t.slot)) : -1.0f;
        case AutomationTargetKind::effectMix:
            return validTrack && t.slot >= 0 && t.slot < (int) mixer[(std::size_t) t.index].effects.size()
                       ? mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].mix
                       : -1.0f;
        case AutomationTargetKind::tempo: return valueFromTempo (bpm);
        case AutomationTargetKind::vcaLevel:
            return t.index >= 0 && t.index < (int) vcas.size() ? vcas[(std::size_t) t.index].level : -1.0f;
        case AutomationTargetKind::none: break;
    }

    return -1.0f;
}

bool Song::setTargetValue (const AutomationTarget& t, float v) noexcept
{
    v = std::clamp (v, 0.0f, 1.0f);
    const auto validChannel = t.index >= 0 && t.index < (int) channels.size();
    const auto validTrack = t.index >= 0 && t.index < (int) mixer.size();

    switch (t.kind)
    {
        case AutomationTargetKind::channelVolume: if (! validChannel) return false; channels[(std::size_t) t.index].volume = v; return true;
        case AutomationTargetKind::channelPan: if (! validChannel) return false; channels[(std::size_t) t.index].pan = v * 2.0f - 1.0f; return true;
        case AutomationTargetKind::mixerVolume: if (! validTrack) return false; mixer[(std::size_t) t.index].volume = v; return true;
        case AutomationTargetKind::mixerPan: if (! validTrack) return false; mixer[(std::size_t) t.index].pan = v * 2.0f - 1.0f; return true;
        case AutomationTargetKind::tempo: bpm = tempoFromValue (v); return true;
        case AutomationTargetKind::vcaLevel:
            if (t.index < 0 || t.index >= (int) vcas.size())
                return false;
            vcas[(std::size_t) t.index].level = v;
            return true;
        case AutomationTargetKind::mixerSend:
            if (! validTrack || ! routes (t.index, t.slot))
                return false;
            setRouteLevel (t.index, t.slot, faderGain (v));
            return true;
        case AutomationTargetKind::effectMix:
            if (! validTrack || t.slot < 0 || t.slot >= (int) mixer[(std::size_t) t.index].effects.size())
                return false;
            mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].mix = v;
            return true;
        case AutomationTargetKind::effectParam:
            if (validTrack && t.slot >= 0 && t.slot < (int) mixer[(std::size_t) t.index].effects.size()
                && mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].kind != EffectKind::plugin && t.param >= 0
                && t.param < maxEffectParams)
            {
                mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].params[(std::size_t) t.param] = v;
                return true;
            }
            return false;
        case AutomationTargetKind::channelParam:
            if (validChannel && channels[(std::size_t) t.index].instrument == InstrumentKind::controller
                && (t.param == envelopeControllerModXParam || t.param == envelopeControllerModYParam))
                if (auto ec = parseEnvelopeController (channels[(std::size_t) t.index].controllerState))
                {
                    (t.param == envelopeControllerModXParam ? ec->modX : ec->modY) = v;
                    channels[(std::size_t) t.index].controllerState = writeEnvelopeController (*ec);
                    return true;
                }
            if (validChannel && channels[(std::size_t) t.index].instrument == InstrumentKind::dashboard)
                if (auto* value = surfaceOutputValue (channels[(std::size_t) t.index].patcher.surface, t.param))
                {
                    *value = v;
                    return true;
                }
            if (validChannel && channels[(std::size_t) t.index].instrument == InstrumentKind::voltage && t.param >= 0 && t.param < voltage::paramCount)
                if (auto vs = voltage::parse (channels[(std::size_t) t.index].pluginState))
                {
                    voltage::setParamValue (*vs, t.param, v);
                    channels[(std::size_t) t.index].pluginState = voltage::write (*vs);
                    return true;
                }
            if (validChannel && channels[(std::size_t) t.index].instrument == InstrumentKind::sampler && t.param >= 0
                && t.param < samplerparam::count)
            {
                samplerparam::set (channels[(std::size_t) t.index].sampler, t.param, v);
                return true;
            }
            if (validChannel && channels[(std::size_t) t.index].instrument == InstrumentKind::midiOut && t.param >= 0
                && t.param <= midiout::controlCount)
            {
                auto& ch = channels[(std::size_t) t.index];
                try
                {
                    auto ms = midiout::stateOf (ch.pluginState);
                    midiout::setParamValue (ms, t.param, v);
                    ch.pluginState = midiout::write (ms);
                }
                catch (...)
                {
                    return false;
                }
                return true;
            }
            // the knobs of an FL engine kept in the channel's state (round 168n)
            if (validChannel && modelparams::find (channels[(std::size_t) t.index].instrument, t.param) != nullptr)
                return modelparams::setValue01 (channels[(std::size_t) t.index], t.param, v);
            return false;
        case AutomationTargetKind::none: break;
    }

    return false;
}

float Song::targetValueAt (const AutomationTarget& target, double tick) const
{
    // the same rule as playback (RackRenderer's automationValueAt, issue #97)
    const Clip* playing = nullptr;
    const Clip* ended = nullptr;
    for (const auto& c : playlist)
    {
        if (c.kind != ClipKind::automation || c.automation < 0 || c.automation >= (int) automations.size() || c.length <= 0 || c.muted
            || isTrackMuted (c.track) || ! (automations[(std::size_t) c.automation].target == target) || tick < (double) c.start)
            continue;
        if (tick < (double) c.end())
        {
            if (playing == nullptr || c.start > playing->start || (c.start == playing->start && c.automation < playing->automation))
                playing = &c;
        }
        else if (ended == nullptr || c.end() > ended->end() || (c.end() == ended->end() && c.start > ended->start))
            ended = &c;
    }
    if (playing != nullptr)
        return automations[(std::size_t) playing->automation].playAt (tick - (double) playing->start + (double) playing->offset, tick);
    if (ended != nullptr)
        return automations[(std::size_t) ended->automation].playAt ((double) (ended->length + ended->offset), (double) ended->end());
    return std::numeric_limits<float>::quiet_NaN();
}

double Song::tempoAt (double tick) const
{
    // tempo automation and FL's pattern Tempo events (issue #86, core/TempoMap.h)
    return tempomap::tempoAt (*this, tick);
}

double Song::secondsForTicks (std::int64_t ticks) const
{
    if (! tempomap::active (*this))
        return (double) ticks * 60.0 / (std::max (1.0, bpm) * ppq);

    // integrate in 1/16 steps (playback changes tempo block by block; the difference is minimal)
    double seconds = 0.0;
    constexpr std::int64_t step = ticksPerStep;

    for (std::int64_t t = 0; t < ticks; t += step)
    {
        const auto n = std::min (step, ticks - t);
        seconds += (double) n * 60.0 / (std::max (1.0, tempoAt ((double) t)) * ppq);
    }

    return seconds;
}

float Automation::mapLive (float v) const noexcept
{
    v = std::clamp (v, 0.0f, 1.0f);
    if (curve.size() < 2)
        return v;
    const auto x = v * (float) (curve.size() - 1);
    const auto i = std::min ((std::size_t) x, curve.size() - 2);
    const auto f = x - (float) i;
    return std::clamp (curve[i] + (curve[i + 1] - curve[i]) * f, 0.0f, 1.0f);
}

float PatternEvents::valueAt (double tick) const noexcept
{
    if (events.empty())
        return 0.5f;

    // the last event at or before `tick`
    auto it = std::upper_bound (events.begin(), events.end(), tick, [] (double t, const PatternEvent& e) { return t < (double) e.tick; });
    return it == events.begin() ? events.front().value : std::prev (it)->value;
}

float Automation::valueAt (double tick) const noexcept
{
    return automationCurveAt (points, tick);
}

double automationSegmentShape (const std::vector<AutomationPoint>& points, std::size_t k, double x) noexcept
{
    if (k + 1 >= points.size())
        return x;
    const auto& a = points[k];
    const auto& b = points[k + 1];
    if (a.mode == AutomationPoint::legacyCurve) // LYNX's law before round 190bw: a power curve
        return a.tension != 0.0f ? std::pow (x, std::pow (2.0, (double) a.tension * 3.0)) : x;
    if (a.mode != flcurve::smooth)
        return flcurve::shape (a.mode, -a.tension, x);

    // Smooth: the slopes come from the neighbouring segments that are Smooth too
    flcurve::SmoothSide before, after;
    if (k > 0 && points[k - 1].mode == flcurve::smooth)
    {
        before.smooth = true;
        before.dx = (double) (a.tick - points[k - 1].tick);
        before.y = (double) points[k - 1].value;
    }
    if (k + 2 < points.size() && b.mode == flcurve::smooth)
    {
        after.smooth = true;
        after.dx = (double) (points[k + 2].tick - b.tick);
        after.y = (double) points[k + 2].value;
    }
    return flcurve::evaluate (flcurve::prepareSmooth (-a.tension, (double) (b.tick - a.tick), (double) a.value, (double) b.value, before, after), x);
}

float automationCurveAt (const std::vector<AutomationPoint>& points, double tick) noexcept
{
    if (points.empty())
        return 0.5f;

    if (tick <= (double) points.front().tick)
        return points.front().value;

    for (std::size_t i = 1; i < points.size(); ++i)
    {
        const auto& a = points[i - 1];
        const auto& b = points[i];

        if (tick < (double) b.tick)
        {
            const auto span = (double) std::max<std::int64_t> (1, b.tick - a.tick);
            const auto x = (tick - (double) a.tick) / span;
            if (a.mode == AutomationPoint::legacyCurve || (a.mode == 0 && a.tension == 0.0f))
            {
                // a straight segment, or an older project's bend: the same float arithmetic as before round 190bw
                auto t = (float) x;
                if (a.tension != 0.0f)
                    t = std::pow (t, std::pow (2.0f, a.tension * 3.0f));
                return a.value + (b.value - a.value) * t;
            }
            return (float) ((double) a.value + ((double) b.value - (double) a.value) * automationSegmentShape (points, i - 1, x));
        }
    }

    return points.back().value;
}

int automationPointMode (const std::vector<AutomationPoint>& points, int point) noexcept
{
    if (point < 1 || point >= (int) points.size())
        return -1;
    const auto m = points[(std::size_t) point - 1].mode;
    return m < flcurve::modeCount ? (int) m : -1;
}

void setAutomationPointMode (std::vector<AutomationPoint>& points, int point, int mode) noexcept
{
    if (point < 1 || point >= (int) points.size() || mode < 0 || mode >= flcurve::modeCount)
        return;
    points[(std::size_t) point - 1].mode = (std::uint8_t) mode;
}

int insertAutomationPoint (std::vector<AutomationPoint>& points, AutomationPoint p, int mode, float tension)
{
    const auto at = std::upper_bound (points.begin(), points.end(), p.tick, [] (std::int64_t t, const AutomationPoint& q) { return t < q.tick; });
    const auto index = (int) (at - points.begin());
    const auto m = (std::uint8_t) std::clamp (mode, 0, (int) flcurve::modeCount - 1);
    p.mode = 0;
    p.tension = 0.0f;
    if (index > 0)
    {
        auto& before = points[(std::size_t) index - 1];
        if (index < (int) points.size()) // splitting a segment: its curve stays on the part after the new point
        {
            p.mode = before.mode;
            p.tension = before.tension;
        }
        before.mode = m;
        before.tension = std::clamp (tension, -1.0f, 1.0f);
    }
    points.insert (points.begin() + index, p);
    return index;
}

void eraseAutomationPoint (std::vector<AutomationPoint>& points, int point)
{
    if (point < 0 || point >= (int) points.size())
        return;
    if (point > 0)
    {
        auto& before = points[(std::size_t) point - 1];
        const auto last = point + 1 == (int) points.size();
        before.mode = last ? (std::uint8_t) 0 : points[(std::size_t) point].mode;
        before.tension = last ? 0.0f : points[(std::size_t) point].tension;
    }
    points.erase (points.begin() + point);
}

void setAutomationSegmentTension (std::vector<AutomationPoint>& points, int segment, float tension) noexcept
{
    if (segment < 0 || segment >= (int) points.size())
        return;
    auto& p = points[(std::size_t) segment];
    p.tension = std::clamp (tension, -1.0f, 1.0f);
    if (p.mode == AutomationPoint::legacyCurve)
        p.mode = 0;
}

// --- FL's automation clip LFO (REA round 177b; FUN_00ba1700, FUN_00ba0710, FUN_00ba03d0, FUN_00677660/820) ---------
bool AutomationLfo::isDefault() const noexcept
{
    return *this == AutomationLfo {};
}

double AutomationLfo::cyclesPerBeat (int speed) noexcept
{
    // rate = (10001^(speed/65536) − 1) · 2^32/625 phase units per beat (FL: 45819 = 1 beat, 40898 = 2, 26257 = 16)
    return (std::pow (10001.0, (double) speed / 65536.0) - 1.0) / 625.0;
}

int AutomationLfo::speedForBeats (double beatsPerCycle) noexcept
{
    if (beatsPerCycle <= 0.0)
        return 65536;
    const auto speed = 65536.0 * std::log (625.0 / beatsPerCycle + 1.0) / std::log (10001.0);
    return (int) std::clamp (std::lround (speed), 512L, 65536L);
}

float AutomationLfo::shape (std::uint32_t phase, int tensionKnob, int skewKnob, int pulseWidthKnob, bool unipolar) noexcept
{
    constexpr double full = 4294967296.0; // 2^32: one cycle
    const auto p = (double) pulseWidthKnob / 128.0;
    const auto s = (double) skewKnob / 256.0;
    const auto s7 = p > 0.0 ? std::round ((1.0 - p) * full - 1.0) : 4294967295.0;
    const auto d = std::clamp (((1.0 - p) * 0.5 - s * (1.0 - std::abs (p))) * full, 1.0, s7 - 1.0);
    const auto s5 = std::round (d);
    const auto s6 = p < 0.0 ? std::max (1.0, std::round (d + p * full)) : s5;
    const auto phi = (double) phase;

    double x;
    if (phi > s7)
        return 1.0f; // the pulse's top (p > 0)
    if (phi < s6)
        x = 1.0 - phi * (2.0 / s6); // falling +1 → −1
    else if (phi < s5)
        return -1.0f; // the pulse's bottom (p < 0)
    else
        x = (phi - s5) * (2.0 / (s7 - s5)) - 1.0; // rising −1 → +1

    // tension (FUN_00ba0250): T = sign(t)·((1e18)^((21^(|t|/128) − 1)·0.05) − 1)
    const auto t = (double) tensionKnob;
    const auto T = t == 0.0 ? 0.0 : (t > 0.0 ? 1.0 : -1.0) * (std::pow (9.99999984e17, (std::pow (21.0, std::abs (t) / 128.0) - 1.0) * 0.05) - 1.0);
    auto curve = [T] (double a) {
        if (T == 0.0)
            return a;
        if (T > 0.0)
            return 1.0 - (std::pow (T + 1.0, 1.0 - a) - 1.0) / T;
        return (std::pow (-T + 1.0, a) - 1.0) / -T;
    };
    const auto y = unipolar ? 2.0 * curve ((x + 1.0) * 0.5) - 1.0 : (x < 0.0 ? -curve (-x) : curve (x));
    return (float) std::clamp (y, -1.0, 1.0);
}

float AutomationLfo::output (double clipTick, double songTick) const noexcept
{
    constexpr double full = 4294967296.0;
    // the amplitude points are bipolar: 0.5 = no swing, 1 = full, 0 = full and inverted (FL's renders, round 177b:
    // a single point at 0.5 plays no LFO; points 0 → 1 swing, pass through nothing at the middle and swing back)
    const auto amplitude = 2.0f * automationCurveAt (amp, clipTick) - 1.0f;
    // the start: the middle of the rising ramp (the LFO starts at 0 going up) plus the phase knob (FUN_00ba06a0)
    const auto p = (double) pulseWidth / 128.0, sk = (double) skew / 256.0;
    const auto s7 = p > 0.0 ? std::round ((1.0 - p) * full - 1.0) : 4294967295.0;
    const auto s5 = std::round (std::clamp (((1.0 - p) * 0.5 - sk * (1.0 - std::abs (p))) * full, 1.0, s7 - 1.0));
    const auto start = std::fmod (s5 + (s7 - s5) * 0.5 + 1.0 + (double) phase, full);
    // frozen: no movement inside the clip; global: the phase runs with the song instead of restarting with the clip
    const auto frozen = (flags & 8) != 0, global = (flags & 2) != 0;
    const auto beats = frozen ? 0.0 : std::max (0.0, global ? songTick : clipTick) / (double) ppq;
    auto cycles = beats * cyclesPerBeat (speed);
    cycles -= std::floor (cycles);
    const auto at = (std::uint32_t) (std::uint64_t) std::fmod (start + cycles * full, full);
    return amplitude * shape (at, tension, skew, pulseWidth, (flags & 4) != 0);
}

float AutomationLfo::apply (float value, double clipTick, double songTick) const noexcept
{
    // TACArticulator.Compute (FUN_00ba1700): add mode out += L·amount; multiply mode out *= 1 + 2·L·amount, with L
    // at half the waveform: FL's own renders (round 177b, lfo/meas: default, level 128, add and shaped clips) match
    // only with the LFO's swing at ±0.5 × its amplitude, in both modes
    const auto amount = (float) level / 128.0f * range;
    const auto l = 0.5f * output (clipTick, songTick);
    const auto out = add ? value + l * amount : value * (1.0f + 2.0f * l * amount);
    return std::clamp (out, 0.0f, 1.0f);
}

// --- modulators (round 177b) ------------------------------------------------------------------------------------
namespace
{
float modNoise (std::uint32_t seed, std::int64_t step) noexcept
{
    // a hash of (seed, step) to -1..1: the same output at the same song position, whatever was played before
    auto x = (std::uint64_t) (std::uint32_t) step * 0x9E3779B97F4A7C15ull ^ ((std::uint64_t) seed << 32 | (std::uint32_t) (step >> 32));
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return (float) ((double) (x >> 11) / 9007199254740992.0 * 2.0 - 1.0);
}
} // namespace

float Modulator::output (double cycles) const noexcept
{
    const auto c = cycles + (double) phase;
    const auto whole = std::floor (c);
    const auto f = (float) (c - whole); // 0..1 within the cycle
    float y = 0.0f;
    switch (shape)
    {
        case ModShape::sine: y = std::sin (f * 6.28318530718f); break;
        case ModShape::triangle: y = f < 0.25f ? f * 4.0f : (f < 0.75f ? 2.0f - f * 4.0f : f * 4.0f - 4.0f); break;
        case ModShape::sawUp: y = f * 2.0f - 1.0f; break;
        case ModShape::sawDown: y = 1.0f - f * 2.0f; break;
        case ModShape::square: y = f < 0.5f ? 1.0f : -1.0f; break;
        case ModShape::random: y = modNoise (seed, (std::int64_t) whole); break;
        case ModShape::smoothRandom:
        {
            const auto a = modNoise (seed, (std::int64_t) whole), b = modNoise (seed, (std::int64_t) whole + 1);
            const auto s = f * f * (3.0f - 2.0f * f);
            y = a + (b - a) * s;
            break;
        }
    }
    return unipolar ? (y + 1.0f) * 0.5f : y;
}

bool Modulator::canModulate (const Song& song, const AutomationTarget& t) noexcept
{
    // LYNX's own controls: not the tempo, not a plugin's parameters (their value lives in the plugin)
    if (t.kind == AutomationTargetKind::none || t.kind == AutomationTargetKind::tempo || song.targetValue (t) < 0.0f)
        return false;
    if (t.kind == AutomationTargetKind::effectParam && t.index >= 0 && t.index < (int) song.mixer.size() && t.slot >= 0
        && t.slot < (int) song.mixer[(std::size_t) t.index].effects.size())
    {
        const auto kind = song.mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].kind;
        return kind != EffectKind::plugin && kind != EffectKind::patcher;
    }
    if (t.kind == AutomationTargetKind::channelParam && t.index >= 0 && t.index < (int) song.channels.size())
    {
        const auto kind = song.channels[(std::size_t) t.index].instrument;
        return kind != InstrumentKind::plugin && kind != InstrumentKind::patcher && ! midictl::isMidiParam (t.param);
    }
    return true;
}

std::int64_t Automation::length() const noexcept
{
    return points.empty() ? 1 : std::max<std::int64_t> (1, points.back().tick);
}

Clip splitClip (Clip& clip, std::int64_t tick)
{
    Clip rest = clip;
    const auto head = std::clamp<std::int64_t> (tick - clip.start, 1, std::max<std::int64_t> (1, clip.length - 1));
    clip.length = head;
    rest.start = clip.start + head;
    rest.offset = clip.offset + head;
    rest.length -= head;
    return rest;
}

double TrackEq::peakQ (float w) noexcept
{
    return 0.0999 * (std::pow (1001.0, std::clamp ((double) w, 0.0, 1.0)) - 1.0) + 0.1;
}

double TrackEq::shelfSlope (float w) noexcept
{
    return 1.9 * std::clamp ((double) w, 0.0, 1.0) + 0.1;
}

std::int64_t swingDelay (std::int64_t start, float swing, float channelMix) noexcept
{
    if (swing <= 0.0f || channelMix <= 0.0f)
        return 0;

    constexpr std::int64_t s = ticksPerStep;
    const auto k = (start + s / 2) / s;

    if (k % 2 == 0)
        return 0;

    const auto w = s - 2 * std::abs (start - k * s);
    return (std::int64_t) std::llround ((double) w * std::clamp (swing, 0.0f, 1.0f) * std::clamp (channelMix, 0.0f, 1.0f) * 0.5);
}

bool PatcherDoc::operator== (const PatcherDoc& o) const
{
    return nodes == o.nodes && wires == o.wires && published == o.published && inputX == o.inputX && inputY == o.inputY && outputX == o.outputX && outputY == o.outputY
           && outputTracks == o.outputTracks && surface == o.surface && surfaceX == o.surfaceX && surfaceY == o.surfaceY
           && fromMidiPorts == o.fromMidiPorts && toMidiPorts == o.toMidiPorts && fromNotesHidden == o.fromNotesHidden
           && rack == o.rack && zones == o.zones;
}

bool PatchNode::BusHead::operator== (const BusHead& o) const { return bus == o.bus && head == o.head && gain == o.gain; }

bool PatchNode::operator== (const PatchNode& o) const
{
    return generator == o.generator && label == o.label && x == o.x && y == o.y && busHeads == o.busHeads
           && paramIns == o.paramIns && paramOuts == o.paramOuts
           && (generator ? channel == o.channel : effect == o.effect);
}

bool EffectSlot::operator== (const EffectSlot& o) const
{
    if (kind != o.kind || enabled != o.enabled || params != o.params || mix != o.mix || ! (plugin == o.plugin) || sidechain != o.sidechain
        || sidechain2 != o.sidechain2 || sendTracks != o.sendTracks || state != o.state || samplePath != o.samplePath || ! (substituteFor == o.substituteFor)
        || presetName != o.presetName || abOther != o.abOther || abShowingB != o.abShowingB || branches.size() != o.branches.size()
        || links.size() != o.links.size() || ! (patcher == o.patcher) || row != o.row || look != o.look || goal != o.goal
        || goalAmount != o.goalAmount || ! (flOrigin == o.flOrigin))
        return false;
    for (std::size_t i = 0; i < branches.size(); ++i)
        if (branches[i].gain != o.branches[i].gain || branches[i].chain != o.branches[i].chain)
            return false;
    for (std::size_t i = 0; i < links.size(); ++i)
    {
        const auto& a = links[i];
        const auto& b = o.links[i];
        if (a.branch != b.branch || a.source != b.source || a.target != b.target || a.param != b.param || a.curve != b.curve)
            return false;
    }
    return true;
}

float channelGain (float volume) noexcept
{
    const auto v = std::clamp (volume, 0.0f, 1.0f);
    return (std::pow (11.0f, v) - 1.0f) / 10.0f;
}

float faderGain (float position) noexcept
{
    const auto v = std::clamp (position, 0.0f, 1.0f) * 1.25f;
    return (std::pow (11.0f, v) - 1.0f) / 10.0f;
}

float faderPosition (float gain) noexcept
{
    if (! (gain > 0.0f))
        return 0.0f;
    return std::clamp ((float) (std::log (10.0 * (double) gain + 1.0) / std::log (11.0) / 1.25), 0.0f, 1.0f);
}

Song makeExportSong (const Song& song, bool wholeSong, std::int64_t tailTicks, std::int64_t& musicLength)
{
    Song out = song;
    patternblocks::expandSong (out); // Pattern Blocks (round 170f) become clips

    if (out.patterns.empty())
        out.patterns.emplace_back().name = "Pattern 1";

    if (! wholeSong || out.playlist.empty())
    {
        const auto index = std::clamp (out.currentPattern, 0, (int) out.patterns.size() - 1);
        Clip clip;
        clip.pattern = index;
        clip.length = out.patterns[(std::size_t) index].effectiveLength (out.beatsPerBar);
        out.playlist = { clip };
        musicLength = clip.length;
    }
    else
    {
        musicLength = 0;

        for (const auto& c : out.playlist)
            musicLength = std::max (musicLength, c.end());
    }

    // empty pattern in the tail: extends the song without playing anything. One extra bar of slack
    // because the last rendered block goes slightly past the end and must not wrap to the start.
    if (tailTicks > 0)
    {
        tailTicks += (std::int64_t) ppq * std::max (1, out.beatsPerBar);
        out.patterns.emplace_back().name = "(tail)";
        Clip tail;
        tail.pattern = (int) out.patterns.size() - 1;
        tail.start = musicLength;
        tail.length = tailTicks;
        out.playlist.push_back (tail);
    }

    out.mode = PlayMode::song;
    out.loopStart = out.loopEnd = 0; // export ignores the loop region
    return out;
}

Song makeBounceSong (const Song& song, int track)
{
    Song out = song;

    if (track <= 0 || track >= (int) out.mixer.size())
        return out;

    for (auto& t : out.mixer)
        t.solo = false;

    auto& insert = out.mixer[(std::size_t) track];
    insert.sends.clear();
    insert.toMaster = true;
    insert.masterLevel = 1.0f; // the insert's own output: its route level to Master is the route's (Render to audio keeps the
                               // route on the new insert; rendering it too played a route at 50 % at 25 %, round 186b)
    insert.muted = false;
    insert.solo = true; // it and whatever feeds it are audible

    // the Master neutral: the render plays through the song's Master again (Render to audio), so nothing of it may be in
    // the file — effects, fader, pan, separation, phase / swap, EQ and its automation (round 186b, as StandIns' stems)
    auto& master = out.mixer[0];
    master.effects.clear();
    master.volume = unityFader;
    master.pan = 0.0f;
    master.muted = false;
    master.stereo = 0.0f;
    master.invertPhase = master.swapChannels = false;
    master.eq = {};
    auto onMaster = [] (const AutomationTarget& t) { return refs::targetsInsert (t) && t.index == 0; };
    for (auto& a : out.automations)
        if (onMaster (a.target))
            a.target = {};
    for (auto& p : out.patterns)
        p.events.erase (std::remove_if (p.events.begin(), p.events.end(), [&] (const PatternEvents& lane) { return onMaster (lane.target); }),
                        p.events.end());
    out.initialValues.erase (std::remove_if (out.initialValues.begin(), out.initialValues.end(),
                                             [&] (const Song::InitialValue& v) { return onMaster (v.target); }),
                             out.initialValues.end());
    out.mode = PlayMode::song;
    return out;
}

int applyBounce (Song& song, int track, const std::string& samplePath, std::int64_t length)
{
    if (track <= 0 || track >= (int) song.mixer.size() || (int) song.mixer.size() >= maxMixerTracks)
        return -1;

    auto& original = song.mixer[(std::size_t) track];
    MixerTrack copy;
    copy.name = original.name + " (audio)";
    copy.toMaster = original.toMaster;
    copy.masterLevel = original.masterLevel;
    copy.sends = original.sends;
    original.muted = true;
    song.mixer.push_back (copy);
    const auto newTrack = (int) song.mixer.size() - 1;

    ChannelDef c;
    c.name = copy.name;
    c.instrument = InstrumentKind::audioClip;
    c.sampler.declickMode = audioClipDeclickMode; // FL's AudioTrack.fst
    c.samplePath = samplePath;
    c.mixerTrack = newTrack;
    c.volume = audioClipUnityVolume; // the render plays at the level of what it replaces (1.0 was +5.18 dB, round 186b)
    song.channels.push_back (c);

    int lane = 0;
    for (const auto& clip : song.playlist)
        lane = std::max (lane, clip.track + 1);

    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = (int) song.channels.size() - 1;
    clip.track = lane;
    clip.length = length;
    song.playlist.push_back (clip);
    return newTrack;
}

Song Song::makeDefault()
{
    Song song;
    song.mixer = defaultMixer();
    auto channel = [] (const char* name, InstrumentKind kind, float volume, float pan) {
        ChannelDef c;
        c.name = name;
        c.instrument = kind;
        c.volume = volume;
        c.pan = pan;
        return c;
    };

    song.channels = {
        channel ("Kick", InstrumentKind::kick, 0.9f, 0.0f),
        channel ("Clap", InstrumentKind::clap, 0.8f, 0.0f),
        channel ("Hat", InstrumentKind::hat, 0.7f, 0.1f),
        channel ("Snare", InstrumentKind::snare, 0.8f, 0.0f),
        channel ("Synth", InstrumentKind::synth, 0.78f, 0.0f),
    };

    for (std::size_t i = 0; i < song.channels.size(); ++i)
        song.channels[i].mixerTrack = (int) i + 1;

    // like FL's template: a limiter on Master (transparent up to -0.3 dB)
    EffectSlot limiter;
    limiter.kind = EffectKind::limiter;
    limiter.params = { 11.7f / 12.0f, 0.0f, 0.5f };
    song.mixer[0].effects.push_back (limiter);

    auto& p = song.patterns.emplace_back();
    p.name = "Pattern 1";

    for (int step : { 0, 4, 8, 12 })
        p.toggleStep (0, step);

    for (int step : { 4, 12 })
        p.toggleStep (1, step);

    for (int step : { 2, 6, 10, 14 })
        p.toggleStep (2, step);

    return song;
}
int firstChannelWithNotes (const Song& song, int pattern) noexcept
{
    if (pattern < 0 || pattern >= (int) song.patterns.size())
        return -1;
    int first = -1;
    for (const auto& n : song.patterns[(std::size_t) pattern].notes)
        if (n.channel >= 0 && n.channel < (int) song.channels.size() && (first < 0 || n.channel < first))
            first = n.channel;
    return first;
}

} // namespace lynx

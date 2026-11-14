#include "core/LiveSession.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "core/ClipOps.h"
#include "core/PatternBlocks.h"
#include "core/SessionIds.h"
#include "core/StandIns.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)
#endif

namespace lynx::live
{
namespace
{
// ---------------------------------------------------------------------------------------------------------------------
// The "uid space": a copy of a song whose index references to channels, patterns and automations hold a code of the
// referenced entity's uid instead of its index, with every clip in its arrangement and the local view neutralised. Two
// computers' copies of the same entity are then equal as C++ values, whatever the order of their lists.
// code = tag << 24 | counter (tag 1…126, counter below 2^24): always >= 2^24, so it never looks like an index.

constexpr int codeShift = 24;
constexpr std::uint64_t codeCounterLimit = std::uint64_t (1) << codeShift;

int codeOf (std::uint64_t uid) noexcept
{
    const auto tag = uids::tagOf (uid);
    const auto counter = uid & uids::counterMask;
    if (tag == 0 || tag > 126 || counter >= codeCounterLimit)
        return -1;
    return (int) (tag << codeShift | (std::uint32_t) counter);
}

std::uint64_t uidOfCode (int code) noexcept
{
    return code < (1 << codeShift) ? 0 : uids::make ((std::uint32_t) code >> codeShift, (std::uint64_t) (code & ((1 << codeShift) - 1)));
}

bool isCode (int v) noexcept { return v >= (1 << codeShift); }

bool noteLess (const Note& a, const Note& b)
{
    return std::tie (a.channel, a.start, a.key, a.length, a.velocity, a.pan, a.finePitch, a.muted, a.slide, a.porta, a.release, a.modX, a.modY,
                     a.colour, a.group, a.chance, a.velocityRange, a.chanceGroup, a.chanceOne)
           < std::tie (b.channel, b.start, b.key, b.length, b.velocity, b.pan, b.finePitch, b.muted, b.slide, b.porta, b.release, b.modX, b.modY,
                       b.colour, b.group, b.chance, b.velocityRange, b.chanceGroup, b.chanceOne);
}

struct U
{
    Song s;
    std::unordered_map<std::uint64_t, int> channel, pattern, automation, arrangement;
    std::unordered_map<std::uint64_t, std::pair<int, int>> clip; // arrangement index, clip index

    void index()
    {
        channel.clear(), pattern.clear(), automation.clear(), arrangement.clear(), clip.clear();
        for (int i = 0; i < (int) s.channels.size(); ++i)
            channel.emplace (s.channels[(std::size_t) i].uid, i);
        for (int i = 0; i < (int) s.patterns.size(); ++i)
            pattern.emplace (s.patterns[(std::size_t) i].uid, i);
        for (int i = 0; i < (int) s.automations.size(); ++i)
            automation.emplace (s.automations[(std::size_t) i].uid, i);
        for (int a = 0; a < (int) s.arrangements.size(); ++a)
        {
            arrangement.emplace (s.arrangements[(std::size_t) a].uid, a);
            const auto& clips = s.arrangements[(std::size_t) a].clips;
            for (int c = 0; c < (int) clips.size(); ++c)
                clip.emplace (clips[(std::size_t) c].uid, std::pair { a, c });
        }
    }
    const ChannelDef* findChannel (std::uint64_t uid) const
    {
        auto it = channel.find (uid);
        return it == channel.end() ? nullptr : &s.channels[(std::size_t) it->second];
    }
    const Pattern* findPattern (std::uint64_t uid) const
    {
        auto it = pattern.find (uid);
        return it == pattern.end() ? nullptr : &s.patterns[(std::size_t) it->second];
    }
    const Automation* findAutomation (std::uint64_t uid) const
    {
        auto it = automation.find (uid);
        return it == automation.end() ? nullptr : &s.automations[(std::size_t) it->second];
    }
    const Arrangement* findArrangement (std::uint64_t uid) const
    {
        auto it = arrangement.find (uid);
        return it == arrangement.end() ? nullptr : &s.arrangements[(std::size_t) it->second];
    }
    const Clip* findClip (std::uint64_t uid, std::uint64_t* arrangementUid = nullptr) const
    {
        auto it = clip.find (uid);
        if (it == clip.end())
            return nullptr;
        const auto& a = s.arrangements[(std::size_t) it->second.first];
        if (arrangementUid != nullptr)
            *arrangementUid = a.uid;
        return &a.clips[(std::size_t) it->second.second];
    }
};

U canon (const Song& in)
{
    U u;
    u.s = in;
    auto& s = u.s;
    // references -> codes (before the clips move: forEachClipList follows the current arrangement)
    auto codes = [] (const auto& list) {
        std::vector<int> out;
        out.reserve (list.size());
        for (const auto& e : list)
            out.push_back (codeOf (e.uid));
        return out;
    };
    const auto channelCodes = codes (s.channels), patternCodes = codes (s.patterns), automationCodes = codes (s.automations);
    auto mapper = [] (const std::vector<int>& table) {
        return [&table] (int& i) { i = i >= 0 && i < (int) table.size() ? table[(std::size_t) i] : -1; };
    };
    refs::forEachChannelRef (s, mapper (channelCodes));
    refs::forEachPatternRef (s, mapper (patternCodes));
    refs::forEachAutomationRef (s, mapper (automationCodes));

    // every clip in its arrangement (the current one's live in Song::playlist)
    if (s.arrangements.empty())
        s.arrangements.push_back ({ "Arrangement 1", {}, 0, 0, {} });
    const auto current = std::clamp (s.currentArrangement, 0, (int) s.arrangements.size() - 1);
    s.arrangements[(std::size_t) current].clips = std::move (s.playlist);
    s.playlist.clear();
    s.arrangements[(std::size_t) current].blocks = std::move (s.blocks); // Pattern Blocks (round 170f) likewise
    s.blocks.clear();

    // the local view and derived data
    s.currentPattern = 0;
    s.currentArrangement = -1; // (so Song::forEachClipList visits every arrangement's clips here)
    s.mode = PlayMode::pattern;
    s.loopStart = s.loopEnd = 0;
    s.info.workSeconds = 0.0;
    s.nextUid = 0;
    s.session.me = 0;
    for (auto& a : s.arrangements)
    {
        a.loopStart = a.loopEnd = 0;
        for (auto& c : a.clips)
            c.crossfadeIn = c.crossfadeOut = 0;
    }
    for (auto& t : s.playlistTracks)
        t.height = 0, t.collapsed = false;
    for (auto& t : s.mixer)
        t.flInput = -1;
    for (auto& p : s.patterns)
        std::sort (p.notes.begin(), p.notes.end(), noteLess);
    u.index();
    return u;
}

// Back to index space. Dangling references (to an entity this copy does not have) are dropped the way
// Song::removeChannel & co. drop them.
Song decode (U&& u)
{
    auto& s = u.s;
    std::unordered_map<int, int> channelAt, patternAt, automationAt;
    for (int i = 0; i < (int) s.channels.size(); ++i)
        channelAt.emplace (codeOf (s.channels[(std::size_t) i].uid), i);
    for (int i = 0; i < (int) s.patterns.size(); ++i)
        patternAt.emplace (codeOf (s.patterns[(std::size_t) i].uid), i);
    for (int i = 0; i < (int) s.automations.size(); ++i)
        automationAt.emplace (codeOf (s.automations[(std::size_t) i].uid), i);
    auto has = [] (const std::unordered_map<int, int>& at, int code) { return isCode (code) && at.count (code) > 0; };

    for (auto& p : s.patterns)
    {
        p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(), [&] (const Note& n) { return ! has (channelAt, n.channel); }), p.notes.end());
        p.channelLoops.erase (std::remove_if (p.channelLoops.begin(), p.channelLoops.end(), [&] (const ChannelLoop& l) { return ! has (channelAt, l.channel); }),
                              p.channelLoops.end());
    }
    for (auto& a : s.arrangements)
        a.clips.erase (std::remove_if (a.clips.begin(), a.clips.end(),
                                       [&] (const Clip& c) {
                                           return c.kind == ClipKind::pattern      ? ! has (patternAt, c.pattern)
                                                  : c.kind == ClipKind::audio      ? ! has (channelAt, c.channel)
                                                                                   : ! has (automationAt, c.automation);
                                       }),
                       a.clips.end());
    s.forEachTarget ([&] (AutomationTarget& t) {
        if (refs::targetsChannel (t) && ! has (channelAt, t.index))
            t.kind = AutomationTargetKind::none;
    });
    for (auto& t : s.playlistTracks)
        if (t.channel >= 0 && ! has (channelAt, t.channel))
        {
            t.channel = -1;
            if (t.mode == PlaylistTrackMode::instrument)
                t.mode = PlaylistTrackMode::unassigned, t.mixerTrack = -1;
        }
    for (auto& a : s.automations)
        if (a.source.channel >= 0 && ! has (channelAt, a.source.channel))
            a.source.channel = -1, a.target.kind = AutomationTargetKind::none;
    for (auto& c : s.channels)
    {
        auto& kids = c.layer.children;
        kids.erase (std::remove_if (kids.begin(), kids.end(), [&] (int k) { return ! has (channelAt, k); }), kids.end());
        if (c.loopStarterPattern >= 0 && ! has (patternAt, c.loopStarterPattern))
            c.loopStarterPattern = -1;
    }
    s.removeDeadMidiLinks();

    // the clips of arrangement 0 back into the playlist, then codes -> indices (forEachClipList sees them all again)
    s.currentArrangement = 0;
    s.playlist = std::move (s.arrangements[0].clips);
    s.arrangements[0].clips.clear();
    s.blocks = std::move (s.arrangements[0].blocks);
    s.arrangements[0].blocks.clear();
    auto mapper = [] (const std::unordered_map<int, int>& at) {
        return [&at] (int& i) {
            if (isCode (i))
            {
                auto it = at.find (i);
                i = it == at.end() ? -1 : it->second;
            }
        };
    };
    refs::forEachChannelRef (s, mapper (channelAt));
    refs::forEachPatternRef (s, mapper (patternAt));
    refs::forEachAutomationRef (s, mapper (automationAt));
    s.forEachBlockList ([&s] (std::vector<PatternBlock>& list) { patternblocks::normalize (list, (int) s.patterns.size()); });
    s.currentPattern = 0;
    for (auto& p : s.patterns)
        p.sortNotes();
    clipops::updateCrossfades (s);
    return std::move (s);
}

// --- the entities ----------------------------------------------------------------------------------------------------

bool sameGlobals (const Song& a, const Song& b)
{
    return a.bpm == b.bpm && a.beatsPerBar == b.beatsPerBar && a.beatUnit == b.beatUnit && a.swing == b.swing && a.channelGroups == b.channelGroups && a.masterPitch == b.masterPitch
           && a.info == b.info && a.projectId == b.projectId && a.performanceMode == b.performanceMode && a.substitutesBypassed == b.substitutesBypassed
           && a.cleanMixer == b.cleanMixer && a.autoCrossfades == b.autoCrossfades && a.linkTracksToMixer == b.linkTracksToMixer && a.compCrossfadeMs == b.compCrossfadeMs && a.playTruncatedNotes == b.playTruncatedNotes && a.muteRunsEffects == b.muteRunsEffects && a.patternClipTakeover == b.patternClipTakeover && a.mutedInsertsSkip == b.mutedInsertsSkip
           && a.fastDeclick == b.fastDeclick && a.panTriangular == b.panTriangular && a.flPluginLevels == b.flPluginLevels && a.autoPdc == b.autoPdc
           && a.compensateAutomations == b.compensateAutomations && a.timeSignatureMode == b.timeSignatureMode && a.scaleHighlight == b.scaleHighlight
           && a.dataFolder == b.dataFolder && a.patternBlocksOn == b.patternBlocksOn;
}

void copyGlobals (const Song& from, Song& to)
{
    to.bpm = from.bpm;
    to.beatsPerBar = from.beatsPerBar;
    to.beatUnit = from.beatUnit;
    to.swing = from.swing;
    to.channelGroups = from.channelGroups;
    to.masterPitch = from.masterPitch;
    const auto work = to.info.workSeconds;
    to.info = from.info;
    to.info.workSeconds = work;
    to.projectId = from.projectId;
    to.performanceMode = from.performanceMode;
    to.substitutesBypassed = from.substitutesBypassed;
    to.cleanMixer = from.cleanMixer;
    to.autoCrossfades = from.autoCrossfades;
    to.linkTracksToMixer = from.linkTracksToMixer;
    to.compCrossfadeMs = from.compCrossfadeMs;
    to.playTruncatedNotes = from.playTruncatedNotes;
    to.muteRunsEffects = from.muteRunsEffects;
    to.patternClipTakeover = from.patternClipTakeover;
    to.mutedInsertsSkip = from.mutedInsertsSkip;
    to.fastDeclick = from.fastDeclick;
    to.panTriangular = from.panTriangular;
    to.flPluginLevels = from.flPluginLevels;
    to.autoPdc = from.autoPdc;
    to.compensateAutomations = from.compensateAutomations;
    to.timeSignatureMode = from.timeSignatureMode;
    to.scaleHighlight = from.scaleHighlight;
    to.dataFolder = from.dataFolder;
    to.patternBlocksOn = from.patternBlocksOn; // (round 170f; the blocks travel in their arrangement's header)
}

template <typename T>
std::vector<std::uint64_t> uidsOf (const std::vector<T>& list)
{
    std::vector<std::uint64_t> out;
    out.reserve (list.size());
    for (const auto& e : list)
        out.push_back (e.uid);
    return out;
}

Pattern headerOf (const Pattern& p)
{
    Pattern h = p;
    h.notes.clear();
    return h;
}

Arrangement headerOf (const Arrangement& a)
{
    Arrangement h = a;
    h.clips.clear();
    return h;
}

std::vector<Note> sliceOf (const Pattern& p, int code)
{
    std::vector<Note> out;
    for (const auto& n : p.notes)
        if (n.channel == code)
            out.push_back (n);
    return out;
}

bool sameKey (const U& a, const U& b, const Key& k)
{
    switch (k.kind)
    {
        case Kind::globals: return sameGlobals (a.s, b.s);
        case Kind::session: return a.s.session.participants == b.s.session.participants;
        case Kind::links: return a.s.midiLinks == b.s.midiLinks && a.s.initialValues == b.s.initialValues;
        case Kind::order:
            return uidsOf (a.s.channels) == uidsOf (b.s.channels) && uidsOf (a.s.patterns) == uidsOf (b.s.patterns)
                   && uidsOf (a.s.automations) == uidsOf (b.s.automations) && uidsOf (a.s.arrangements) == uidsOf (b.s.arrangements);
        case Kind::arrangement:
        {
            const auto *x = a.findArrangement (k.a), *y = b.findArrangement (k.a);
            return x == nullptr || y == nullptr ? x == y : headerOf (*x) == headerOf (*y);
        }
        case Kind::channel:
        {
            const auto *x = a.findChannel (k.a), *y = b.findChannel (k.a);
            return x == nullptr || y == nullptr ? x == y : *x == *y;
        }
        case Kind::pattern:
        {
            const auto *x = a.findPattern (k.a), *y = b.findPattern (k.a);
            return x == nullptr || y == nullptr ? x == y : headerOf (*x) == headerOf (*y);
        }
        case Kind::notes:
        {
            const auto *x = a.findPattern (k.a), *y = b.findPattern (k.a);
            const auto code = codeOf (k.b);
            const auto sx = x != nullptr ? sliceOf (*x, code) : std::vector<Note> {};
            const auto sy = y != nullptr ? sliceOf (*y, code) : std::vector<Note> {};
            return sx == sy;
        }
        case Kind::automation:
        {
            const auto *x = a.findAutomation (k.a), *y = b.findAutomation (k.a);
            return x == nullptr || y == nullptr ? x == y : *x == *y;
        }
        case Kind::clip:
        {
            std::uint64_t ax = 0, ay = 0;
            const auto *x = a.findClip (k.a, &ax), *y = b.findClip (k.a, &ay);
            return x == nullptr || y == nullptr ? x == y : ax == ay && *x == *y;
        }
        case Kind::insert:
        {
            const auto i = (std::size_t) k.a;
            const bool hx = i < a.s.mixer.size(), hy = i < b.s.mixer.size();
            return ! hx || ! hy ? hx == hy : a.s.mixer[i] == b.s.mixer[i];
        }
        case Kind::row:
        {
            const auto i = (std::size_t) k.a;
            const bool hx = i < a.s.playlistTracks.size(), hy = i < b.s.playlistTracks.size();
            return ! hx || ! hy ? hx == hy : a.s.playlistTracks[i] == b.s.playlistTracks[i];
        }
    }
    return true;
}

bool present (const U& u, const Key& k)
{
    switch (k.kind)
    {
        case Kind::globals:
        case Kind::session:
        case Kind::links:
        case Kind::order: return true;
        case Kind::arrangement: return u.findArrangement (k.a) != nullptr;
        case Kind::channel: return u.findChannel (k.a) != nullptr;
        case Kind::pattern: return u.findPattern (k.a) != nullptr;
        case Kind::notes:
        {
            const auto* p = u.findPattern (k.a);
            const auto code = codeOf (k.b);
            return p != nullptr && std::any_of (p->notes.begin(), p->notes.end(), [code] (const Note& n) { return n.channel == code; });
        }
        case Kind::automation: return u.findAutomation (k.a) != nullptr;
        case Kind::clip: return u.findClip (k.a) != nullptr;
        case Kind::insert: return k.a < u.s.mixer.size();
        case Kind::row: return k.a < u.s.playlistTracks.size();
    }
    return false;
}

std::set<Key> keysOfU (const U& u)
{
    const auto& s = u.s;
    std::set<Key> keys { { Kind::globals }, { Kind::session }, { Kind::links }, { Kind::order } };
    for (const auto& a : s.arrangements)
    {
        keys.insert ({ Kind::arrangement, a.uid });
        for (const auto& c : a.clips)
            keys.insert ({ Kind::clip, c.uid });
    }
    for (const auto& c : s.channels)
        keys.insert ({ Kind::channel, c.uid });
    for (const auto& p : s.patterns)
    {
        keys.insert ({ Kind::pattern, p.uid });
        int last = -2;
        for (const auto& n : p.notes) // (sorted by channel)
            if (n.channel != last)
            {
                last = n.channel;
                if (isCode (n.channel))
                    keys.insert ({ Kind::notes, p.uid, uidOfCode (n.channel) });
            }
    }
    for (const auto& a : s.automations)
        keys.insert ({ Kind::automation, a.uid });
    for (std::size_t i = 0; i < s.mixer.size(); ++i)
        keys.insert ({ Kind::insert, i });
    for (std::size_t i = 0; i < s.playlistTracks.size(); ++i)
        keys.insert ({ Kind::row, i });
    return keys;
}

// The instance tags (runtime identity: the running plugin / voices) and this computer's plugin ids stay with the
// entity when another computer's copy of it is put in place.
void keepLocalIdentity (ChannelDef& incoming, const ChannelDef& old)
{
    incoming.instance = old.instance;
    if (incoming.plugin.id != old.plugin.id && ! old.plugin.id.empty() && incoming.plugin.name == old.plugin.name)
        incoming.plugin.id = old.plugin.id;
}

void keepLocalIdentity (MixerTrack& incoming, const MixerTrack& old)
{
    for (std::size_t i = 0; i < incoming.effects.size() && i < old.effects.size(); ++i)
    {
        auto& e = incoming.effects[i];
        const auto& o = old.effects[i];
        e.instance = o.instance;
        if (e.plugin.id != o.plugin.id && ! o.plugin.id.empty() && e.plugin.name == o.plugin.name)
            e.plugin.id = o.plugin.id;
    }
}

int kindRank (Kind k)
{
    // creations first (what the others refer to), the order last
    switch (k)
    {
        case Kind::arrangement: return 0;
        case Kind::channel: return 1;
        case Kind::pattern: return 2;
        case Kind::automation: return 3;
        case Kind::order: return 9;
        default: return 5;
    }
}

template <typename T>
void reorderBy (std::vector<T>& list, const std::vector<std::uint64_t>& order)
{
    std::unordered_map<std::uint64_t, std::size_t> rank;
    for (std::size_t i = 0; i < order.size(); ++i)
        rank.emplace (order[i], i);
    std::stable_sort (list.begin(), list.end(), [&] (const T& a, const T& b) {
        auto ra = rank.find (a.uid), rb = rank.find (b.uid);
        const auto x = ra == rank.end() ? order.size() : ra->second;
        const auto y = rb == rank.end() ? order.size() : rb->second;
        return x < y;
    });
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------

std::string toString (const Key& k)
{
    switch (k.kind)
    {
        case Kind::globals: return "g";
        case Kind::session: return "s";
        case Kind::links: return "l";
        case Kind::order: return "o";
        case Kind::arrangement: return "A:" + uids::toHex (k.a);
        case Kind::channel: return "c:" + uids::toHex (k.a);
        case Kind::pattern: return "p:" + uids::toHex (k.a);
        case Kind::notes: return "n:" + uids::toHex (k.a) + ":" + uids::toHex (k.b);
        case Kind::automation: return "a:" + uids::toHex (k.a);
        case Kind::clip: return "k:" + uids::toHex (k.a);
        case Kind::insert: return "m:" + std::to_string (k.a);
        case Kind::row: return "r:" + std::to_string (k.a);
    }
    return "?";
}

std::optional<Key> keyFromString (const std::string& s)
{
    if (s == "g")
        return Key { Kind::globals };
    if (s == "s")
        return Key { Kind::session };
    if (s == "l")
        return Key { Kind::links };
    if (s == "o")
        return Key { Kind::order };
    if (s.size() < 3 || s[1] != ':')
        return std::nullopt;
    const auto rest = s.substr (2);
    auto hex = [] (const std::string& h) -> std::optional<std::uint64_t> {
        const auto v = uids::fromHex (h);
        if (v == 0)
            return std::nullopt;
        return v;
    };
    auto number = [] (const std::string& n) -> std::optional<std::uint64_t> {
        if (n.empty() || n.size() > 6 || ! std::all_of (n.begin(), n.end(), [] (char c) { return c >= '0' && c <= '9'; }))
            return std::nullopt;
        return (std::uint64_t) std::stoul (n);
    };
    std::optional<std::uint64_t> v;
    switch (s[0])
    {
        case 'A': if ((v = hex (rest))) return Key { Kind::arrangement, *v }; break;
        case 'c': if ((v = hex (rest))) return Key { Kind::channel, *v }; break;
        case 'p': if ((v = hex (rest))) return Key { Kind::pattern, *v }; break;
        case 'a': if ((v = hex (rest))) return Key { Kind::automation, *v }; break;
        case 'k': if ((v = hex (rest))) return Key { Kind::clip, *v }; break;
        case 'm': if ((v = number (rest)) && *v < (std::uint64_t) maxMixerTracks) return Key { Kind::insert, *v }; break;
        case 'r': if ((v = number (rest)) && *v < 100000) return Key { Kind::row, *v }; break;
        case 'n':
        {
            const auto colon = rest.find (':');
            if (colon == std::string::npos)
                break;
            auto p = hex (rest.substr (0, colon)), c = hex (rest.substr (colon + 1));
            if (p && c)
                return Key { Kind::notes, *p, *c };
            break;
        }
        default: break;
    }
    return std::nullopt;
}

std::string describe (const Key& k, const Song& song, const Translate& translate)
{
    auto t = [&] (const char* english) { return translate ? translate (english) : std::string (english); };
    auto fill = [] (std::string text, std::initializer_list<std::string> args) {
        int n = 0;
        for (const auto& arg : args)
        {
            const auto mark = "{" + std::to_string (n++) + "}";
            if (const auto at = text.find (mark); at != std::string::npos)
                text.replace (at, mark.size(), arg);
        }
        return text;
    };
    auto patternName = [&] (std::uint64_t uid) -> std::string {
        for (std::size_t i = 0; i < song.patterns.size(); ++i)
            if (song.patterns[i].uid == uid)
                return song.patterns[i].name.empty() ? fill (t (TR_NOOP ("Pattern {0}")), { std::to_string (i + 1) }) : song.patterns[i].name;
        return t (TR_NOOP ("a pattern"));
    };
    auto channelName = [&] (std::uint64_t uid) -> std::string {
        for (std::size_t i = 0; i < song.channels.size(); ++i)
            if (song.channels[i].uid == uid)
                return song.channels[i].name.empty() ? fill (t (TR_NOOP ("Channel {0}")), { std::to_string (i + 1) }) : song.channels[i].name;
        return t (TR_NOOP ("a channel"));
    };
    switch (k.kind)
    {
        case Kind::globals: return t (TR_NOOP ("Tempo & project settings"));
        case Kind::session: return t (TR_NOOP ("Participants"));
        case Kind::links: return t (TR_NOOP ("MIDI links"));
        case Kind::order: return t (TR_NOOP ("Item order"));
        case Kind::arrangement: return t (TR_NOOP ("Arrangement"));
        case Kind::channel: return channelName (k.a);
        case Kind::pattern: return patternName (k.a);
        case Kind::notes: return fill (t (TR_NOOP ("{0} in {1}")), { channelName (k.b), patternName (k.a) });
        case Kind::automation: return t (TR_NOOP ("Automation"));
        case Kind::clip: return t (TR_NOOP ("Playlist clip"));
        case Kind::insert: return k.a == 0 ? t (TR_NOOP ("Master")) : fill (t (TR_NOOP ("Insert {0}")), { std::to_string (k.a) });
        case Kind::row: return fill (t (TR_NOOP ("Track {0}")), { std::to_string (k.a + 1) });
    }
    return {};
}

std::vector<Key> Patch::keys() const
{
    auto out = changed;
    out.insert (out.end(), removed.begin(), removed.end());
    return out;
}

std::set<Key> keysOf (const Song& song)
{
    return keysOfU (canon (song));
}

bool same (const Song& a, const Song& b, const Key& key)
{
    return sameKey (canon (a), canon (b), key);
}

Patch diff (const Song& from, const Song& to, const std::set<Key>* only)
{
    const auto ua = canon (from);
    auto ub = canon (to);
    std::set<Key> keys;
    if (only != nullptr)
        keys = *only;
    else
    {
        keys = keysOfU (ua);
        auto kb = keysOfU (ub);
        keys.insert (kb.begin(), kb.end());
    }
    Patch patch;
    std::set<Key> changed;
    for (const auto& k : keys)
        if (! sameKey (ua, ub, k))
        {
            if (present (ub, k))
            {
                patch.changed.push_back (k);
                changed.insert (k);
            }
            else
                patch.removed.push_back (k);
        }
    if (patch.empty())
        return patch;

    // the carrier: `to` with only the changed entities (and id-only stubs of what they refer to)
    auto& c = ub.s;
    auto isChanged = [&] (Kind kind, std::uint64_t a, std::uint64_t b = 0) { return changed.count ({ kind, a, b }) > 0; };
    const bool orderChanged = isChanged (Kind::order, 0);
    std::unordered_set<std::uint64_t> keepChannel, keepPattern, keepAutomation;
    for (auto& ch : c.channels)
        if (isChanged (Kind::channel, ch.uid))
            keepChannel.insert (ch.uid);
        else
        {
            ChannelDef stub;
            stub.uid = ch.uid;
            ch = std::move (stub);
        }
    for (auto& p : c.patterns)
    {
        const bool header = isChanged (Kind::pattern, p.uid);
        if (header)
            keepPattern.insert (p.uid);
        std::vector<Note> notes;
        for (const auto& n : p.notes)
            if (isChanged (Kind::notes, p.uid, uidOfCode (n.channel)))
                notes.push_back (n);
        if (! notes.empty())
            keepPattern.insert (p.uid);
        if (! header)
        {
            Pattern stub;
            stub.uid = p.uid;
            p = std::move (stub);
        }
        p.notes = std::move (notes);
    }
    for (auto& a : c.automations)
        if (isChanged (Kind::automation, a.uid))
            keepAutomation.insert (a.uid);
        else
        {
            Automation stub;
            stub.uid = a.uid;
            a = std::move (stub);
        }
    for (auto& a : c.arrangements)
    {
        a.clips.erase (std::remove_if (a.clips.begin(), a.clips.end(), [&] (const Clip& k) { return ! isChanged (Kind::clip, k.uid); }), a.clips.end());
        if (! isChanged (Kind::arrangement, a.uid))
        {
            Arrangement stub;
            stub.uid = a.uid;
            stub.clips = std::move (a.clips);
            a = std::move (stub);
        }
    }
    for (std::size_t i = 0; i < c.mixer.size(); ++i)
        if (! isChanged (Kind::insert, i))
            c.mixer[i] = MixerTrack {};
    for (std::size_t i = 0; i < c.playlistTracks.size(); ++i)
        if (! isChanged (Kind::row, i))
            c.playlistTracks[i] = PlaylistTrack {};
    if (! isChanged (Kind::session, 0))
        c.session.participants.clear();
    if (! isChanged (Kind::links, 0))
        c.midiLinks.clear(), c.initialValues.clear();

    // what the kept content refers to keeps its stub; the other stubs go (all of them stay when the order changed)
    if (! orderChanged)
    {
        auto collect = [] (std::unordered_set<std::uint64_t>& into) {
            return [&into] (int& i) {
                if (isCode (i))
                    into.insert (uidOfCode (i));
            };
        };
        refs::forEachChannelRef (c, collect (keepChannel));
        refs::forEachPatternRef (c, collect (keepPattern));
        refs::forEachAutomationRef (c, collect (keepAutomation));
        auto prune = [] (auto& list, const std::unordered_set<std::uint64_t>& keep) {
            list.erase (std::remove_if (list.begin(), list.end(), [&] (const auto& e) { return keep.count (e.uid) == 0; }), list.end());
        };
        prune (c.channels, keepChannel);
        prune (c.patterns, keepPattern);
        prune (c.automations, keepAutomation);
        // arrangements: those with a changed header or changed clips (at least one stays: a song always has one)
        std::vector<Arrangement> kept;
        for (auto& a : c.arrangements)
            if (isChanged (Kind::arrangement, a.uid) || ! a.clips.empty())
                kept.push_back (std::move (a));
        if (kept.empty())
            kept.push_back (std::move (c.arrangements.front()));
        c.arrangements = std::move (kept);
    }
    patch.carrier = decode (std::move (ub));
    return patch;
}

void apply (Song& target, const Patch& patch, const std::function<bool (const Key&)>& filter)
{
    auto wanted = [&] (const Key& k) { return ! filter || filter (k); };
    std::vector<Key> changed, removed;
    for (const auto& k : patch.changed)
        if (wanted (k))
            changed.push_back (k);
    for (const auto& k : patch.removed)
        if (wanted (k))
            removed.push_back (k);
    if (changed.empty() && removed.empty())
        return;

    const Song view = target; // (its local view is put back at the end)
    Song t = target;

    // deletions of channels, patterns, automations, arrangements: the Song functions, so the ripples are a local edit's
    {
        std::vector<int> channels, patterns, automations, arrangements;
        auto indexOf = [] (const auto& list, std::uint64_t uid) {
            for (int i = 0; i < (int) list.size(); ++i)
                if (list[(std::size_t) i].uid == uid)
                    return i;
            return -1;
        };
        for (const auto& k : removed)
        {
            int i = -1;
            switch (k.kind)
            {
                case Kind::channel: if ((i = indexOf (t.channels, k.a)) >= 0) channels.push_back (i); break;
                case Kind::pattern: if ((i = indexOf (t.patterns, k.a)) >= 0) patterns.push_back (i); break;
                case Kind::automation: if ((i = indexOf (t.automations, k.a)) >= 0) automations.push_back (i); break;
                case Kind::arrangement: if ((i = indexOf (t.arrangements, k.a)) >= 0) arrangements.push_back (i); break;
                default: break;
            }
        }
        auto descending = [] (std::vector<int>& v) { std::sort (v.rbegin(), v.rend()); };
        descending (channels), descending (patterns), descending (automations), descending (arrangements);
        for (auto i : channels)
            t.removeChannel (i);
        for (auto i : patterns)
            t.removePattern (i);
        for (auto i : automations)
            t.removeAutomation (i);
        for (auto i : arrangements)
            t.removeArrangement (i);
    }

    auto ut = canon (t);
    const auto up = canon (patch.carrier);
    auto& s = ut.s;
    std::stable_sort (changed.begin(), changed.end(), [] (const Key& a, const Key& b) { return kindRank (a.kind) < kindRank (b.kind); });
    for (const auto& k : changed)
    {
        switch (k.kind)
        {
            case Kind::globals: copyGlobals (up.s, s); break;
            case Kind::session: s.session.participants = up.s.session.participants; break;
            case Kind::links:
                s.midiLinks = up.s.midiLinks;
                s.initialValues = up.s.initialValues;
                break;
            case Kind::order:
                reorderBy (s.channels, uidsOf (up.s.channels));
                reorderBy (s.patterns, uidsOf (up.s.patterns));
                reorderBy (s.automations, uidsOf (up.s.automations));
                reorderBy (s.arrangements, uidsOf (up.s.arrangements));
                break;
            case Kind::arrangement:
            {
                const auto* src = up.findArrangement (k.a);
                if (src == nullptr)
                    break;
                auto it = ut.arrangement.find (k.a);
                if (it != ut.arrangement.end())
                {
                    auto& dst = s.arrangements[(std::size_t) it->second];
                    auto clips = std::move (dst.clips);
                    dst = headerOf (*src);
                    dst.clips = std::move (clips);
                }
                else
                {
                    s.arrangements.push_back (headerOf (*src));
                    ut.arrangement.emplace (k.a, (int) s.arrangements.size() - 1);
                }
                break;
            }
            case Kind::channel:
            {
                const auto* src = up.findChannel (k.a);
                if (src == nullptr)
                    break;
                auto it = ut.channel.find (k.a);
                if (it != ut.channel.end())
                {
                    auto incoming = *src;
                    keepLocalIdentity (incoming, s.channels[(std::size_t) it->second]);
                    s.channels[(std::size_t) it->second] = std::move (incoming);
                }
                else
                {
                    s.channels.push_back (*src);
                    ut.channel.emplace (k.a, (int) s.channels.size() - 1);
                }
                break;
            }
            case Kind::pattern:
            {
                const auto* src = up.findPattern (k.a);
                if (src == nullptr)
                    break;
                auto it = ut.pattern.find (k.a);
                if (it != ut.pattern.end())
                {
                    auto& dst = s.patterns[(std::size_t) it->second];
                    auto notes = std::move (dst.notes);
                    dst = headerOf (*src);
                    dst.notes = std::move (notes);
                }
                else
                {
                    s.patterns.push_back (headerOf (*src));
                    ut.pattern.emplace (k.a, (int) s.patterns.size() - 1);
                }
                break;
            }
            case Kind::automation:
            {
                const auto* src = up.findAutomation (k.a);
                if (src == nullptr)
                    break;
                auto it = ut.automation.find (k.a);
                if (it != ut.automation.end())
                    s.automations[(std::size_t) it->second] = *src;
                else
                {
                    s.automations.push_back (*src);
                    ut.automation.emplace (k.a, (int) s.automations.size() - 1);
                }
                break;
            }
            case Kind::notes:
            {
                const auto* src = up.findPattern (k.a);
                auto it = ut.pattern.find (k.a);
                if (src == nullptr || it == ut.pattern.end())
                    break;
                const auto code = codeOf (k.b);
                auto& notes = s.patterns[(std::size_t) it->second].notes;
                notes.erase (std::remove_if (notes.begin(), notes.end(), [code] (const Note& n) { return n.channel == code; }), notes.end());
                for (const auto& n : src->notes)
                    if (n.channel == code)
                        notes.push_back (n);
                break;
            }
            case Kind::clip: break; // (below, all together)
            case Kind::insert:
            {
                const auto i = (std::size_t) k.a;
                if (i >= up.s.mixer.size())
                    break;
                if (i >= s.mixer.size())
                    s.mixer.resize (i + 1);
                auto incoming = up.s.mixer[i];
                keepLocalIdentity (incoming, s.mixer[i]);
                s.mixer[i] = std::move (incoming);
                break;
            }
            case Kind::row:
            {
                const auto i = (std::size_t) k.a;
                if (i >= up.s.playlistTracks.size())
                    break;
                if (i >= s.playlistTracks.size())
                    s.playlistTracks.resize (i + 1);
                s.playlistTracks[i] = up.s.playlistTracks[i];
                break;
            }
        }
    }
    // the clips: the changed and removed ones leave their arrangement in one pass, the changed ones come back in theirs
    {
        std::unordered_set<std::uint64_t> moving;
        for (const auto& k : changed)
            if (k.kind == Kind::clip)
                moving.insert (k.a);
        for (const auto& k : removed)
            if (k.kind == Kind::clip)
                moving.insert (k.a);
        if (! moving.empty())
        {
            for (auto& a : s.arrangements)
                a.clips.erase (std::remove_if (a.clips.begin(), a.clips.end(), [&] (const Clip& c) { return moving.count (c.uid) > 0; }), a.clips.end());
            for (const auto& k : changed)
                if (k.kind == Kind::clip)
                {
                    std::uint64_t arrangementUid = 0;
                    const auto* src = up.findClip (k.a, &arrangementUid);
                    auto at = ut.arrangement.find (arrangementUid);
                    if (src != nullptr && at != ut.arrangement.end())
                        s.arrangements[(std::size_t) at->second].clips.push_back (*src);
                }
        }
    }
    // the other deletions (slices, positional slots: a trailing one shrinks the list, one in the middle resets)
    std::vector<std::size_t> inserts, rows;
    for (const auto& k : removed)
        switch (k.kind)
        {
            case Kind::notes:
            {
                auto it = ut.pattern.find (k.a);
                if (it == ut.pattern.end())
                    break;
                const auto code = codeOf (k.b);
                auto& notes = s.patterns[(std::size_t) it->second].notes;
                notes.erase (std::remove_if (notes.begin(), notes.end(), [code] (const Note& n) { return n.channel == code; }), notes.end());
                break;
            }
            case Kind::insert: inserts.push_back ((std::size_t) k.a); break;
            case Kind::row: rows.push_back ((std::size_t) k.a); break;
            default: break;
        }
    auto removeSlots = [] (auto& list, std::vector<std::size_t>& gone) {
        std::sort (gone.rbegin(), gone.rend());
        for (auto i : gone)
        {
            if (i + 1 == list.size())
                list.pop_back();
            else if (i < list.size())
                list[i] = {};
        }
    };
    removeSlots (s.mixer, inserts);
    removeSlots (s.playlistTracks, rows);

    auto result = decode (std::move (ut));
    copyLocalView (view, result);
    target = std::move (result);
}

void copyLocalView (const Song& from, Song& to)
{
    // the current arrangement (by uid), its clips into the playlist
    if (! from.arrangements.empty() && ! to.arrangements.empty())
    {
        const auto uid = from.arrangements[(std::size_t) std::clamp (from.currentArrangement, 0, (int) from.arrangements.size() - 1)].uid;
        for (int i = 0; i < (int) to.arrangements.size(); ++i)
            if (to.arrangements[(std::size_t) i].uid == uid)
            {
                to.selectArrangement (i);
                break;
            }
    }
    to.loopStart = from.loopStart;
    to.loopEnd = from.loopEnd;
    for (std::size_t i = 0; i < to.arrangements.size() && i < from.arrangements.size(); ++i)
        if (to.arrangements[i].uid == from.arrangements[i].uid)
            to.arrangements[i].loopStart = from.arrangements[i].loopStart, to.arrangements[i].loopEnd = from.arrangements[i].loopEnd;
    // the current pattern (by uid)
    to.currentPattern = 0;
    if (from.currentPattern >= 0 && from.currentPattern < (int) from.patterns.size())
    {
        const auto uid = from.patterns[(std::size_t) from.currentPattern].uid;
        for (int i = 0; i < (int) to.patterns.size(); ++i)
            if (to.patterns[(std::size_t) i].uid == uid)
                to.currentPattern = i;
    }
    to.mode = from.mode;
    for (std::size_t i = 0; i < to.playlistTracks.size() && i < from.playlistTracks.size(); ++i)
    {
        to.playlistTracks[i].height = from.playlistTracks[i].height;
        to.playlistTracks[i].collapsed = from.playlistTracks[i].collapsed;
    }
    for (std::size_t i = 0; i < to.mixer.size() && i < from.mixer.size(); ++i)
        to.mixer[i].flInput = from.mixer[i].flInput;
    to.info.workSeconds = from.info.workSeconds;
    to.nextUid = std::max (to.nextUid, from.nextUid);
    to.session.me = from.session.me;
}

// --- ownership -------------------------------------------------------------------------------------------------------

bool mayEdit (std::uint32_t tag, const Permissions& perms, const Key& key, const Song& song, const Song& proposed)
{
    (void) proposed;
    if (tag == session::hostTag)
        return true;
    if (perms.listener)
        return false;
    if (perms.editEverything)
        return key.kind != Kind::session;
    if (perms.settings && (key.kind == Kind::globals || key.kind == Kind::arrangement))
        return true;
    const auto* me = song.session.find (tag);
    switch (key.kind)
    {
        case Kind::globals:
        case Kind::session:
        case Kind::links:
        case Kind::order:
        case Kind::arrangement: return false;
        case Kind::channel:
        case Kind::pattern:
        case Kind::automation:
        case Kind::clip: return uids::tagOf (key.a) == tag; // (the uid names who made it)
        case Kind::notes: return uids::tagOf (key.b) == tag; // the notes of his channels, in any pattern
        case Kind::insert: return key.a != 0 && me != nullptr && me->ownsInsert ((int) key.a);
        case Kind::row: return me != nullptr && me->ownsRow ((int) key.a);
    }
    return false;
}

std::uint32_t ownerOf (const Key& key, const Song& song)
{
    switch (key.kind)
    {
        case Kind::channel:
        case Kind::pattern:
        case Kind::automation:
        case Kind::clip: return uids::tagOf (key.a);
        case Kind::notes: return uids::tagOf (key.b);
        case Kind::insert:
            for (const auto& p : song.session.participants)
                if (p.tag != session::hostTag && p.ownsInsert ((int) key.a))
                    return p.tag;
            return session::hostTag;
        case Kind::row:
            for (const auto& p : song.session.participants)
                if (p.tag != session::hostTag && p.ownsRow ((int) key.a))
                    return p.tag;
            return session::hostTag;
        default: return session::hostTag;
    }
}

// --- Authority -------------------------------------------------------------------------------------------------------

Authority::Authority (Song initial, Codec c, std::uint64_t startRev, std::uint64_t e)
    : state (std::move (initial)), rev (startRev), startRevision (startRev), epochId (e), codec (std::move (c))
{
    if (! codec)
        codec = [] (const Patch& p) { return p; };
}

Permissions Authority::permissionsOf (std::uint32_t tag) const
{
    auto it = perms.find (tag);
    return it == perms.end() ? Permissions {} : it->second;
}

Commit Authority::commitPatch (std::uint32_t origin, std::uint32_t seq, Patch patch)
{
    Commit c;
    c.origin = origin;
    c.seq = seq;
    c.rev = rev;
    if (! patch.empty())
    {
        c.patch = codec (patch);
        apply (state, c.patch);
        c.rev = ++rev;
        for (const auto& k : c.patch.keys())
            lastChange[k] = { rev, origin };
        log.push_back (c);
        if (log.size() > replayLimit)
            log.pop_front();
    }
    return c;
}

std::optional<std::vector<Commit>> Authority::since (std::uint64_t from, std::uint64_t fromEpoch) const
{
    if (fromEpoch != epochId)
        return std::nullopt;
    return since (from);
}

std::optional<std::vector<Commit>> Authority::since (std::uint64_t from) const
{
    if (from > rev)
        return std::nullopt; // (a revision we never made: this host restarted behind the peer)
    std::vector<Commit> out;
    if (from == rev)
        return out;
    if (log.empty() || log.front().rev > from + 1)
        return std::nullopt;
    for (const auto& c : log)
        if (c.rev > from)
            out.push_back (c);
    return out;
}

Commit Authority::propose (std::uint32_t origin, const Proposal& proposal)
{
    const auto& p = proposal.patch;
    const auto perm = permissionsOf (origin);
    std::set<Key> allowed, notOwned, conflicts;
    for (const auto& k : p.keys())
    {
        if (k.kind == Kind::order && origin != session::hostTag && ! perm.editEverything)
            continue; // (a guest's new things go at the end; reordering is the host's — not an error)
        if (! mayEdit (origin, perm, k, state, p.carrier))
        {
            notOwned.insert (k);
            continue;
        }
        auto it = lastChange.find (k);
        if (it != lastChange.end() && it->second.rev > proposal.baseRev && it->second.origin != origin)
            conflicts.insert (k);
        else
            allowed.insert (k);
    }
    Song r = state;
    const auto lengths = patternLengths (r);
    apply (r, p, [&] (const Key& k) { return allowed.count (k) > 0; });
    followPatternLengths (lengths, r);
    uids::normalize (r);

    std::vector<Key> rejected (conflicts.begin(), conflicts.end());
    if (! notOwned.empty())
    {
        const auto ur = canon (r), uc = canon (p.carrier);
        for (const auto& k : notOwned)
            if (! sameKey (ur, uc, k)) // (a ripple of an allowed edit that came out the same here is fine)
                rejected.push_back (k);
    }
    auto commit = commitPatch (origin, proposal.seq, diff (state, r));
    commit.rejected = rejected;
    commit.reason = rejected.empty() ? std::string() : ! conflicts.empty() ? "conflict" : "owned";
    return commit;
}

Commit Authority::setParticipants (const std::vector<SessionParticipant>& participants)
{
    Song r = state;
    r.session.participants = participants;
    const std::set<Key> only { { Kind::session } };
    return commitPatch (session::hostTag, 0, diff (state, r, &only));
}

// --- Replica ---------------------------------------------------------------------------------------------------------

Replica::Replica (Song snapshot, std::uint64_t r, std::uint32_t me, Codec c)
    : state (snapshot), applied (snapshot), lastSent (std::move (snapshot)), rev (r), appliedRev (r), tag (me), codec (std::move (c))
{
    if (! codec)
        codec = [] (const Patch& p) { return p; };
    state.session.me = applied.session.me = lastSent.session.me = me;
}

std::optional<Proposal> Replica::poll (const Song& working)
{
    auto d = diff (lastSent, working);
    if (d.empty())
        return std::nullopt;
    Proposal p;
    p.seq = ++seq;
    p.baseRev = resendBase ? std::min (*resendBase, appliedRev) : appliedRev;
    resendBase.reset();
    p.patch = std::move (d);
    inflight[p.seq] = p.patch;
    inflightBase[p.seq] = p.baseRev;
    lastSent = working;
    return p;
}

void Replica::resend()
{
    if (inflight.empty())
        return;
    auto base = appliedRev;
    for (const auto& [s, b] : inflightBase)
        base = std::min (base, b);
    inflight.clear();
    inflightBase.clear();
    // (what the working song has on top of the confirmed state it was last rebased on goes out again)
    lastSent = applied;
    resendBase = base;
}

void Replica::restored (std::uint32_t firstSeq)
{
    seq = std::max (seq, firstSeq);
    inflight.clear();
    inflightBase.clear();
    acks.clear();
    lastSent = applied; // (everything the working song has on top of the base goes out again)
    resendBase = appliedRev;
}

Song Replica::resume (Song snapshot, std::uint64_t r, const Song& working)
{
    return resume (std::move (snapshot), r, working, r < rev);
}

Song Replica::resume (Song snapshot, std::uint64_t r, const Song& working, bool hostBehind)
{
    snapshot.session.me = tag;
    auto base = appliedRev;
    for (const auto& [s, b] : inflightBase)
        base = std::min (base, b);
    std::set<Key> mine;
    for (const auto& k : diff (applied, working).keys())
        mine.insert (k);
    if (hostBehind)
        for (const auto& k : diff (snapshot, applied).keys())
            if (ownerOf (k, applied) == tag && tag != session::hostTag)
                mine.insert (k);
    std::erase_if (mine, [&] (const Key& k) { return k.kind == Kind::session || (k.kind == Kind::order && tag != session::hostTag); });
    const auto own = diff (snapshot, working, &mine);
    Song x = snapshot;
    copyLocalView (working, x);
    apply (x, own);
    state = applied = snapshot;
    lastSent = snapshot;
    copyLocalView (working, lastSent);
    rev = appliedRev = r;
    inflight.clear();
    inflightBase.clear();
    acks.clear();
    resendBase = hostBehind ? r : std::min (base, r);
    return x;
}

Replica::Update Replica::onCommit (const Commit& commit, const Song& working, bool hold)
{
    Update u;
    if (! commit.patch.empty())
    {
        if (commit.rev != rev + 1)
        {
            u.gap = commit.rev > rev + 1;
            return u; // (an old one: already applied)
        }
        apply (state, commit.patch);
        rev = commit.rev;
    }
    if (commit.origin == tag && commit.seq != 0)
        acks.push_back ({ commit.seq, commit });
    if (hold)
        return u;
    return rebase (working);
}

Replica::Update Replica::release (const Song& working)
{
    return rebase (working);
}

Replica::Update Replica::rebase (const Song& working)
{
    Update u;
    // own proposals the host answered: where the working song still holds what was sent, the confirmed value replaces
    // it (the host's rounding, or the old value of a rejected entity)
    std::set<Key> adoptW, adoptSent;
    std::optional<U> uw, us;
    for (const auto& [s, c] : acks)
    {
        auto it = inflight.find (s);
        if (it == inflight.end())
            continue;
        if (! uw)
            uw = canon (working), us = canon (lastSent);
        const auto uc = canon (it->second.carrier);
        for (const auto& k : it->second.keys())
        {
            if (sameKey (*uw, uc, k))
                adoptW.insert (k);
            if (sameKey (*us, uc, k))
                adoptSent.insert (k);
        }
        for (const auto& k : c.rejected)
            if (adoptW.count (k) > 0)
                u.rejected.push_back (k);
        if (! c.rejected.empty())
            u.reason = c.reason;
    }
    auto move = [&] (const Song& w, const std::set<Key>& adopt) {
        Song x = state;
        copyLocalView (w, x);
        // (the order of the lists is the host's: a guest's new things go at the end, after what the host confirmed)
        apply (x, diff (applied, w), [&] (const Key& k) { return adopt.count (k) == 0 && (k.kind != Kind::order || tag == session::hostTag); });
        return x;
    };
    auto newWorking = move (working, adoptW);
    lastSent = move (lastSent, adoptSent);
    for (const auto& [s, c] : acks)
        inflight.erase (s), inflightBase.erase (s);
    acks.clear();
    applied = state;
    appliedRev = rev;
    if (! diff (working, newWorking).empty())
        u.working = std::move (newWorking);
    return u;
}

// --- undo per user ---------------------------------------------------------------------------------------------------

Song selectiveRevert (const Song& current, const Song& before, const Song& after, std::vector<Key>* skipped)
{
    const auto back = diff (after, before);
    std::set<Key> allowed;
    const auto uc = canon (current), ua = canon (after);
    for (const auto& k : back.keys())
    {
        if (sameKey (uc, ua, k))
            allowed.insert (k);
        else if (skipped != nullptr)
            skipped->push_back (k);
    }
    Song x = current;
    apply (x, back, [&] (const Key& k) { return allowed.count (k) > 0; });
    return x;
}
// --- media limits ----------------------------------------------------------------------------------------------------

std::int64_t MediaBudget::limitOf (std::uint32_t tag) const
{
    auto it = raised.find (tag);
    return it == raised.end() ? guestLimit : std::max (guestLimit, it->second);
}

std::int64_t MediaBudget::usedBy (std::uint32_t tag) const
{
    auto it = used.find (tag);
    return it == used.end() ? 0 : it->second;
}

MediaBudget::Verdict MediaBudget::admit (std::uint32_t tag, std::int64_t bytes) const
{
    bytes = std::max<std::int64_t> (0, bytes);
    if (session + bytes > sessionLimit)
        return Verdict::sessionLimit;
    if (tag > session::hostTag && usedBy (tag) + bytes > limitOf (tag))
        return Verdict::guestLimit;
    return Verdict::ok;
}

MediaBudget::Verdict MediaBudget::admitOut (std::int64_t bytes) const
{
    return session + std::max<std::int64_t> (0, bytes) > sessionLimit ? Verdict::sessionLimit : Verdict::ok;
}

void MediaBudget::received (std::uint32_t tag, std::int64_t bytes)
{
    bytes = std::max<std::int64_t> (0, bytes);
    session += bytes;
    if (tag > session::hostTag)
        used[tag] += bytes;
}

void MediaBudget::sent (std::int64_t bytes)
{
    session += std::max<std::int64_t> (0, bytes);
}

void MediaBudget::raise (std::uint32_t tag, std::int64_t bytes)
{
    auto limit = limitOf (tag);
    const auto need = usedBy (tag) + std::max<std::int64_t> (0, bytes);
    while (limit < need && limit < sessionLimit)
        limit *= 2;
    if (limit == limitOf (tag) && limit < sessionLimit)
        limit *= 2;
    raised[tag] = std::min (limit, sessionLimit);
}

std::string mediaSize (std::int64_t bytes)
{
    char text[32];
    const double mb = (double) std::max<std::int64_t> (0, bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0)
    {
        const double gb = mb / 1024.0;
        std::snprintf (text, sizeof text, gb == std::floor (gb) ? "%.0f GB" : "%.1f GB", gb);
    }
    else
        std::snprintf (text, sizeof text, "%.0f MB", std::max (1.0, std::round (mb)));
    return text;
}

// --- chat ------------------------------------------------------------------------------------------------------------

std::string cleanChat (const std::string& text)
{
    std::string out;
    out.reserve (std::min (text.size(), chatMaxBytes));
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const auto c = (unsigned char) text[i];
        if (c == '\r')
            continue;
        if (c < 0x20 && c != '\n')
            out.push_back (' ');
        else if (c == 0x7F)
            continue;
        else
            out.push_back ((char) c);
    }
    // trim spaces and line breaks at both ends
    const auto first = out.find_first_not_of (" \n\t");
    if (first == std::string::npos)
        return {};
    out = out.substr (first, out.find_last_not_of (" \n\t") - first + 1);
    if (out.size() > chatMaxBytes)
    {
        auto cut = chatMaxBytes;
        while (cut > 0 && ((unsigned char) out[cut] & 0xC0) == 0x80)
            --cut; // (not inside a UTF-8 sequence)
        out.resize (cut);
    }
    return out;
}

// --- follow the host's transport -------------------------------------------------------------------------------------

double hostTicksAt (const HostTransport& host, double nowMs) noexcept
{
    if (! host.playing)
        return host.ticks;
    const auto elapsed = std::max (0.0, nowMs - host.atMs);
    return host.ticks + elapsed * host.bpm * (double) ppq / 60000.0;
}

FollowAction follow (const HostTransport& host, double nowMs, bool playing, bool songMode, double ticks, double toleranceTicks)
{
    FollowAction a;
    a.songMode = host.songMode;
    a.ticks = hostTicksAt (host, nowMs);
    if (! host.playing)
    {
        a.type = playing ? FollowAction::Type::stop : FollowAction::Type::none;
        return a;
    }
    if (! playing || songMode != host.songMode)
        a.type = FollowAction::Type::play;
    else if (std::abs (ticks - a.ticks) > toleranceTicks)
        a.type = FollowAction::Type::seek;
    return a;
}

// --- live stand-ins --------------------------------------------------------------------------------------------------

std::set<Key> unplayable (const Song& song, const std::function<bool (const PluginRef&)>& missing)
{
    std::set<Key> out;
    auto anyMissing = [&] (const std::vector<const PluginRef*>& refs) {
        return std::any_of (refs.begin(), refs.end(), [&] (const PluginRef* r) { return missing (*r); });
    };
    for (const auto& c : song.channels)
    {
        std::vector<const PluginRef*> refs;
        standins::pluginsOf (c, refs);
        if (c.uid != 0 && anyMissing (refs))
            out.insert ({ Kind::channel, c.uid, 0 });
    }
    for (int t = 1; t < (int) song.mixer.size(); ++t)
    {
        std::vector<const PluginRef*> refs;
        standins::pluginsOf (song.mixer[(std::size_t) t], refs);
        if (anyMissing (refs))
            out.insert ({ Kind::insert, (std::uint64_t) t, 0 });
    }
    return out;
}

std::vector<Key> toRender (const Song& song, const std::set<Key>& needed, std::uint32_t me, const std::function<bool (const PluginRef&)>& missing)
{
    std::vector<Key> out;
    const auto here = unplayable (song, missing);
    for (const auto& k : needed)
    {
        if (here.count (k) > 0 || ownerOf (k, song) != me)
            continue; // (not all of its plugins are here, or it is someone else's to render)
        if (k.kind == Kind::channel)
        {
            const bool exists = std::any_of (song.channels.begin(), song.channels.end(), [&] (const ChannelDef& c) { return c.uid == k.a; });
            if (exists)
                out.push_back (k);
        }
        else if (k.kind == Kind::insert && k.a > 0 && k.a < song.mixer.size() && standins::insertUsesPlugin (song.mixer[(std::size_t) k.a]))
            out.push_back (k);
    }
    return out;
}
} // namespace lynx::live

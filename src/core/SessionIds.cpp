#include "core/SessionIds.h"

#include <random>
#include <unordered_set>

namespace lynx::uids
{
std::uint64_t next (Song& song)
{
    return make (song.session.me, ++song.nextUid);
}

std::string newProjectId()
{
    std::random_device device;
    std::mt19937_64 random ((std::uint64_t) device() << 32 ^ device() ^ (std::uint64_t) std::random_device {}());
    return toHex (random()) + toHex (random());
}

std::string toHex (std::uint64_t uid)
{
    static const char* digits = "0123456789abcdef";
    std::string out (16, '0');
    for (int i = 15; i >= 0; --i, uid >>= 4)
        out[(std::size_t) i] = digits[uid & 15];
    return out;
}

std::uint64_t fromHex (const std::string& hex) noexcept
{
    if (hex.empty() || hex.size() > 16)
        return 0;
    std::uint64_t v = 0;
    for (auto c : hex)
    {
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0)
            return 0;
        v = v << 4 | (std::uint64_t) d;
    }
    return v;
}

bool normalize (Song& song)
{
    bool changed = false;
    if (song.projectId.empty())
    {
        song.projectId = newProjectId();
        changed = true;
    }

    // nextUid past every counter this participant already used (a project whose counter was lost, or edited by an
    // older LYNX that dropped it, never hands out an existing uid again)
    const auto me = song.session.me;
    auto see = [&] (std::uint64_t uid) {
        if (uid != 0 && tagOf (uid) == me)
            song.nextUid = std::max (song.nextUid, uid & counterMask);
    };
    for (const auto& c : song.channels)
        see (c.uid);
    for (const auto& p : song.patterns)
        see (p.uid);
    for (const auto& a : song.automations)
        see (a.uid);
    for (const auto& a : song.arrangements)
    {
        see (a.uid);
        for (const auto& c : a.clips)
            see (c.uid);
    }
    for (const auto& c : song.playlist)
        see (c.uid);

    // one set per kind: a uid seen before (or 0) gets a new one
    auto fix = [&] (std::unordered_set<std::uint64_t>& seen, std::uint64_t& uid) {
        if (uid == 0 || ! seen.insert (uid).second)
        {
            uid = next (song);
            seen.insert (uid);
            changed = true;
        }
    };
    {
        std::unordered_set<std::uint64_t> seen;
        seen.reserve (song.channels.size() * 2);
        for (auto& c : song.channels)
            fix (seen, c.uid);
    }
    {
        std::unordered_set<std::uint64_t> seen;
        for (auto& p : song.patterns)
            fix (seen, p.uid);
    }
    {
        std::unordered_set<std::uint64_t> seen;
        for (auto& a : song.automations)
            fix (seen, a.uid);
    }
    {
        std::unordered_set<std::uint64_t> seen;
        for (auto& a : song.arrangements)
            fix (seen, a.uid);
    }
    {
        std::unordered_set<std::uint64_t> seen;
        song.forEachClipList ([&] (std::vector<Clip>& list) {
            for (auto& c : list)
                fix (seen, c.uid);
        });
    }
    return changed;
}
} // namespace lynx::uids

namespace lynx::refs
{
bool targetsChannel (const AutomationTarget& t) noexcept
{
    return t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan
           || t.kind == AutomationTargetKind::channelParam || t.kind == AutomationTargetKind::channelAdjust;
}

bool targetsInsert (const AutomationTarget& t) noexcept
{
    return t.kind == AutomationTargetKind::mixerVolume || t.kind == AutomationTargetKind::mixerPan || t.kind == AutomationTargetKind::effectParam
           || t.kind == AutomationTargetKind::effectMix || t.kind == AutomationTargetKind::mixerSend;
}

void forEachChannelRef (Song& song, const IndexFn& fn)
{
    for (auto& p : song.patterns)
    {
        for (auto& n : p.notes)
            fn (n.channel);
        for (auto& l : p.channelLoops)
            fn (l.channel);
    }
    song.forEachClipList ([&] (std::vector<Clip>& list) {
        for (auto& c : list)
            if (c.kind == ClipKind::audio)
                fn (c.channel);
    });
    for (auto& t : song.playlistTracks)
        fn (t.channel);
    for (auto& c : song.channels)
        for (auto& child : c.layer.children)
            fn (child);
    song.forEachTarget ([&] (AutomationTarget& t) {
        if (targetsChannel (t))
            fn (t.index);
    });
    for (auto& a : song.automations)
        fn (a.source.channel);
}

void forEachPatternRef (Song& song, const IndexFn& fn)
{
    song.forEachClipList ([&] (std::vector<Clip>& list) {
        for (auto& c : list)
            if (c.kind == ClipKind::pattern)
                fn (c.pattern);
    });
    song.forEachBlockList ([&] (std::vector<PatternBlock>& list) { // Pattern Blocks' rows (round 170f)
        for (auto& b : list)
            fn (b.pattern);
    });
    for (auto& c : song.channels) // a Loop Starter channel's pattern (round 168b)
        if (c.loopStarterPattern >= 0)
            fn (c.loopStarterPattern);
    fn (song.currentPattern);
}

void forEachAutomationRef (Song& song, const IndexFn& fn)
{
    song.forEachClipList ([&] (std::vector<Clip>& list) {
        for (auto& c : list)
            if (c.kind == ClipKind::automation)
                fn (c.automation);
    });
}

namespace
{
void slotInsertRefs (EffectSlot& slot, const IndexFn& fn)
{
    fn (slot.sidechain);
    fn (slot.sidechain2);
    fn (slot.sendTracks[0]);
    fn (slot.sendTracks[1]);
    for (auto& t : slot.patcher.outputTracks)
        fn (t);
    for (auto& b : slot.branches)
        for (auto& child : b.chain)
            slotInsertRefs (child, fn);
}
} // namespace

void forEachInsertRef (Song& song, const IndexFn& fn)
{
    for (auto& c : song.channels)
    {
        fn (c.mixerTrack);
        for (auto& t : c.patcher.outputTracks)
            fn (t);
    }
    for (auto& m : song.mixer)
    {
        for (auto& s : m.sends)
            fn (s.target);
        for (auto& e : m.effects)
            slotInsertRefs (e, fn);
    }
    for (auto& t : song.playlistTracks)
        fn (t.mixerTrack);
    song.forEachTarget ([&] (AutomationTarget& t) {
        if (targetsInsert (t))
        {
            fn (t.index);
            if (t.kind == AutomationTargetKind::mixerSend)
                fn (t.slot);
        }
    });
    for (auto& a : song.automations)
        fn (a.source.track);
}

void forEachRowRef (Song& song, const IndexFn& fn)
{
    song.forEachClipList ([&] (std::vector<Clip>& list) {
        for (auto& c : list)
            fn (c.track);
    });
}
} // namespace lynx::refs

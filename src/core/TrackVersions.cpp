#include "core/TrackVersions.h"

#include <algorithm>
#include <map>
#include <set>

#include "core/TakeCleanup.h"
#include "core/TrackStates.h"

namespace lynx::trackversions
{
namespace
{
const PlaylistTrack* trackOf (const Song& song, int track) noexcept
{
    return track >= 0 && track < (int) song.playlistTracks.size() ? &song.playlistTracks[(std::size_t) track] : nullptr;
}

PlaylistTrack* trackOf (Song& song, int track) noexcept
{
    return track >= 0 && track < (int) song.playlistTracks.size() ? &song.playlistTracks[(std::size_t) track] : nullptr;
}

// The active content of the track goes into `into`: its clips (of the current arrangement), its lanes and, when `insert`
// is its own insert, that insert's effects.
void takeContent (Song& song, int track, int insert, TrackVersion& into)
{
    into.clips.clear();
    auto& list = song.playlist;
    for (auto it = list.begin(); it != list.end();)
        if (it->track == track)
        {
            into.clips.push_back (*it);
            it = list.erase (it);
        }
        else
            ++it;
    auto& t = song.playlistTracks[(std::size_t) track];
    into.lanes = std::move (t.lanes);
    t.lanes.clear();
    into.hasChain = insert > 0 && insert < (int) song.mixer.size();
    into.chain.clear();
    if (into.hasChain)
        into.chain = song.mixer[(std::size_t) insert].effects;
}

// The stored content of `from` becomes the track's (and `from` keeps only its name)
void putContent (Song& song, int track, int insert, TrackVersion& from)
{
    for (auto c : from.clips)
    {
        c.track = track;
        song.playlist.push_back (c);
    }
    auto& t = song.playlistTracks[(std::size_t) track];
    t.lanes = std::move (from.lanes);
    if (from.hasChain && insert > 0 && insert < (int) song.mixer.size())
        song.mixer[(std::size_t) insert].effects = std::move (from.chain);
    from.clips.clear();
    from.lanes.clear();
    from.chain.clear();
    from.hasChain = false;
}

// A copy of an effect chain that is a chain of its own (new running instances, so the rack loads the copy's state)
std::vector<EffectSlot> freshChain (std::vector<EffectSlot> chain)
{
    for (auto& slot : chain)
        slot.instance.renew();
    return chain;
}

// Every clip anywhere (all lists) that plays pattern `p` / automation `a` and is not on track `track`
bool patternUsedElsewhere (const Song& song, int track, int p)
{
    bool used = false;
    song.forEachClipList ([&] (const std::vector<Clip>& clips) {
        for (const auto& c : clips)
            used = used || (c.kind == ClipKind::pattern && c.pattern == p && c.track != track);
    });
    return used;
}

bool automationUsedElsewhere (const Song& song, int track, int a)
{
    bool used = false;
    song.forEachClipList ([&] (const std::vector<Clip>& clips) {
        for (const auto& c : clips)
            used = used || (c.kind == ClipKind::automation && c.automation == a && c.track != track);
    });
    for (int t = 0; t < (int) song.playlistTracks.size() && ! used; ++t)
    {
        const auto& pt = song.playlistTracks[(std::size_t) t];
        used = std::any_of (pt.lanes.begin(), pt.lanes.end(), [a] (const AutomationLane& l) { return l.automation == a; });
        for (const auto& v : pt.versions)
            used = used || std::any_of (v.lanes.begin(), v.lanes.end(), [a] (const AutomationLane& l) { return l.automation == a; });
    }
    return used;
}

std::string withVersionName (const std::string& base, const std::string& version)
{
    return base.empty() ? std::string() : base + " (" + version + ")";
}
} // namespace

int count (const Song& song, int track) noexcept
{
    const auto* t = trackOf (song, track);
    return t != nullptr ? (int) t->versions.size() : 0;
}

int active (const Song& song, int track) noexcept
{
    const auto* t = trackOf (song, track);
    return t != nullptr && ! t->versions.empty() ? std::clamp (t->activeVersion, 0, (int) t->versions.size() - 1) : 0;
}

std::string nameOf (const Song& song, int track, int version)
{
    const auto* t = trackOf (song, track);
    if (t == nullptr || version < 0 || version >= (int) t->versions.size())
        return {};
    return t->versions[(std::size_t) version].name;
}

bool inCurrentArrangement (const Song& song, int track) noexcept
{
    const auto* t = trackOf (song, track);
    return t != nullptr && ! t->versions.empty() && t->versionArrangement == song.currentArrangement;
}

bool any (const Song& song) noexcept
{
    return std::any_of (song.playlistTracks.begin(), song.playlistTracks.end(), [] (const PlaylistTrack& t) { return ! t.versions.empty(); });
}

int chainInsert (const Song& song, int track)
{
    const auto u = trackstates::unitsOfTrack (song, track);
    return u.inserts.empty() ? -1 : u.inserts.front();
}

std::string freeName (const Song& song, int track, const std::string& base)
{
    const auto* t = trackOf (song, track);
    const auto taken = [t] (const std::string& n) {
        return t != nullptr && std::any_of (t->versions.begin(), t->versions.end(), [&n] (const TrackVersion& v) { return v.name == n; });
    };
    if (! taken (base))
        return base;
    for (int i = 2;; ++i)
        if (const auto n = base + " " + std::to_string (i); ! taken (n))
            return n;
}

int add (Song& song, int track, Start start, const std::string& name, const std::string& firstName)
{
    if (track < 0 || track >= 10000)
        return -1;
    if ((int) song.playlistTracks.size() <= track)
        song.playlistTracks.resize ((std::size_t) track + 1);
    auto* t = trackOf (song, track);
    if (! t->versions.empty() && t->versionArrangement != song.currentArrangement)
        return -1;
    if (t->versions.empty())
    {
        t->versions.push_back ({ firstName.empty() ? std::string ("Version 1") : firstName });
        t->activeVersion = 0;
        t->versionArrangement = song.currentArrangement;
    }
    const auto insert = chainInsert (song, track);
    const auto label = name.empty() ? freeName (song, track, "Version " + std::to_string (t->versions.size() + 1)) : name;

    // the version that starts: a copy of the active content, or nothing with the chain as it is
    TrackVersion made;
    made.name = label;
    if (start == Start::duplicate)
    {
        std::map<int, int> patterns, automations;
        for (const auto& c : song.playlist)
            if (c.track == track)
            {
                auto copy = c;
                copy.uid = 0;
                if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size())
                {
                    const auto& p = song.patterns[(std::size_t) c.pattern];
                    if (p.trackClip || ! patternUsedElsewhere (song, track, c.pattern))
                    {
                        if (! patterns.count (c.pattern))
                        {
                            auto np = p;
                            np.uid = 0;
                            np.name = withVersionName (p.name, label);
                            patterns[c.pattern] = (int) song.patterns.size();
                            song.patterns.push_back (std::move (np));
                        }
                        copy.pattern = patterns[c.pattern];
                    }
                }
                else if (c.kind == ClipKind::automation && c.automation >= 0 && c.automation < (int) song.automations.size()
                         && ! automationUsedElsewhere (song, track, c.automation))
                {
                    if (! automations.count (c.automation))
                    {
                        auto na = song.automations[(std::size_t) c.automation];
                        na.uid = 0;
                        na.name = withVersionName (na.name, label);
                        automations[c.automation] = (int) song.automations.size();
                        song.automations.push_back (std::move (na));
                    }
                    copy.automation = automations[c.automation];
                }
                made.clips.push_back (copy);
            }
        // the lanes' curves are copied: a lane is the track's own
        t = trackOf (song, track);
        for (const auto& l : t->lanes)
            if (l.automation >= 0 && l.automation < (int) song.automations.size())
            {
                auto na = song.automations[(std::size_t) l.automation];
                na.uid = 0;
                song.automations.push_back (std::move (na));
                made.lanes.push_back ({ (int) song.automations.size() - 1, l.height });
            }
    }
    made.hasChain = insert > 0;
    if (made.hasChain)
        made.chain = freshChain (song.mixer[(std::size_t) insert].effects);

    // the active content goes into its version, the new one comes out
    t = trackOf (song, track);
    const auto was = std::clamp (t->activeVersion, 0, (int) t->versions.size() - 1);
    takeContent (song, track, insert, t->versions[(std::size_t) was]);
    t->versions.push_back (std::move (made));
    t->activeVersion = (int) t->versions.size() - 1;
    putContent (song, track, insert, t->versions.back());
    return t->activeVersion;
}

bool switchTo (Song& song, int track, int version)
{
    if (! inCurrentArrangement (song, track))
        return false;
    auto* t = trackOf (song, track);
    if (version < 0 || version >= (int) t->versions.size() || version == t->activeVersion)
        return false;
    const auto insert = chainInsert (song, track);
    takeContent (song, track, insert, t->versions[(std::size_t) std::clamp (t->activeVersion, 0, (int) t->versions.size() - 1)]);
    t->activeVersion = version;
    putContent (song, track, insert, t->versions[(std::size_t) version]);
    return true;
}

bool rename (Song& song, int track, int version, const std::string& name)
{
    auto* t = trackOf (song, track);
    if (t == nullptr || version < 0 || version >= (int) t->versions.size() || name.empty() || t->versions[(std::size_t) version].name == name)
        return false;
    t->versions[(std::size_t) version].name = name;
    return true;
}

bool remove (Song& song, int track, int version)
{
    auto* t = trackOf (song, track);
    if (t == nullptr || version < 0 || version >= (int) t->versions.size())
        return false;
    if (version == t->activeVersion)
    {
        if (! inCurrentArrangement (song, track))
            return false;
        // the neighbour plays from now on; the removed one's content goes with it
        const auto next = version + 1 < (int) t->versions.size() ? version + 1 : version - 1;
        switchTo (song, track, next);
        t = trackOf (song, track);
    }
    // its lanes' automations go too when nothing else plays them
    std::vector<int> curves;
    for (const auto& l : t->versions[(std::size_t) version].lanes)
        curves.push_back (l.automation);
    t->versions.erase (t->versions.begin() + version);
    if (t->activeVersion > version)
        --t->activeVersion;
    if (t->versions.size() == 1)
    {
        t->versions.clear();
        t->activeVersion = 0;
        t->versionArrangement = 0;
    }
    std::sort (curves.rbegin(), curves.rend());
    curves.erase (std::unique (curves.begin(), curves.end()), curves.end());
    for (const auto a : curves)
        if (a >= 0 && a < (int) song.automations.size() && takecleanup::automationUses (song, a) == 0)
            song.removeAutomation (a);
    return true;
}

int promote (Song& song, int track, int version, std::int64_t from, std::int64_t to)
{
    if (! inCurrentArrangement (song, track) || to <= from)
        return -1;
    const auto* t = trackOf (song, track);
    if (version < 0 || version >= (int) t->versions.size() || version == t->activeVersion)
        return -1;

    // the active version's clips: the parts outside [from, to) stay
    std::vector<Clip> kept;
    bool changed = false;
    for (auto it = song.playlist.begin(); it != song.playlist.end();)
    {
        auto& c = *it;
        if (c.track != track || c.start + c.length <= from || c.start >= to)
        {
            ++it;
            continue;
        }
        changed = true;
        auto clip = c;
        it = song.playlist.erase (it);
        if (clip.start < from)
        {
            auto head = clip;
            auto rest = splitClip (head, from);
            kept.push_back (head);
            clip = rest;
        }
        if (clip.start + clip.length > to)
        {
            auto rest = splitClip (clip, to);
            rest.uid = 0;
            kept.push_back (rest);
        }
    }
    for (auto& c : kept)
        song.playlist.push_back (c);

    // the version's clips: their parts inside [from, to)
    int copied = 0;
    for (auto c : song.playlistTracks[(std::size_t) track].versions[(std::size_t) version].clips)
    {
        if (c.start + c.length <= from || c.start >= to)
            continue;
        if (c.start < from)
            c = splitClip (c, from);
        if (c.start + c.length > to)
            splitClip (c, to);
        c.track = track;
        c.uid = 0;
        song.playlist.push_back (c);
        ++copied;
    }
    return changed || copied > 0 ? copied : -1;
}

int keepActiveOnly (Song& song)
{
    int dropped = 0;
    std::vector<int> curves;
    for (auto& t : song.playlistTracks)
    {
        if (t.versions.empty())
            continue;
        for (std::size_t v = 0; v < t.versions.size(); ++v)
            if ((int) v != t.activeVersion)
            {
                ++dropped;
                for (const auto& l : t.versions[v].lanes)
                    curves.push_back (l.automation);
            }
        t.versions.clear();
        t.activeVersion = 0;
        t.versionArrangement = 0;
    }
    std::sort (curves.rbegin(), curves.rend());
    curves.erase (std::unique (curves.begin(), curves.end()), curves.end());
    for (const auto a : curves)
        if (a >= 0 && a < (int) song.automations.size() && takecleanup::automationUses (song, a) == 0)
            song.removeAutomation (a);
    return dropped;
}

int laneUses (const Song& song, int index) noexcept
{
    int uses = 0;
    for (const auto& t : song.playlistTracks)
        for (std::size_t v = 0; v < t.versions.size(); ++v)
            if ((int) v != t.activeVersion)
                uses += (int) std::count_if (t.versions[v].lanes.begin(), t.versions[v].lanes.end(),
                                             [index] (const AutomationLane& l) { return l.automation == index; });
    return uses;
}
} // namespace lynx::trackversions

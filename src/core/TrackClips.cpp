#include "core/TrackClips.h"

#include <algorithm>
#include <map>

#include "core/LayoutStyle.h"

namespace lynx::trackclips
{
namespace
{
const PlaylistTrack* trackOf (const Song& song, int track) noexcept
{
    return track >= 0 && track < (int) song.playlistTracks.size() ? &song.playlistTracks[(std::size_t) track] : nullptr;
}

bool nameTaken (const Song& song, const std::string& name)
{
    return std::any_of (song.patterns.begin(), song.patterns.end(), [&name] (const Pattern& p) { return p.name == name; });
}

// a fresh copy for track `track`: a track clip with a name of the track and no uid (a pattern of its own)
Pattern copyFor (const Song& song, Pattern p, int track)
{
    p.trackClip = true;
    p.uid = 0;
    if (track >= 0)
        p.name = newClipName (song, track);
    return p;
}
} // namespace

bool defaultOn() noexcept
{
    return layoutstyle::current() != layoutstyle::Style::pattern;
}

bool owns (const Song& song, int track) noexcept
{
    const auto* t = trackOf (song, track);
    return t != nullptr && t->ownClips && t->mode == PlaylistTrackMode::instrument;
}

bool isTrackClip (const Song& song, int pattern) noexcept
{
    return pattern >= 0 && pattern < (int) song.patterns.size() && song.patterns[(std::size_t) pattern].trackClip;
}

int count (const Song& song) noexcept
{
    return (int) std::count_if (song.patterns.begin(), song.patterns.end(), [] (const Pattern& p) { return p.trackClip; });
}

bool listedInSelector (const Song& song, int pattern) noexcept
{
    return pattern >= 0 && pattern < (int) song.patterns.size() && (! song.patterns[(std::size_t) pattern].trackClip || shownInSelector());
}

int stepListed (const Song& song, int delta) noexcept
{
    const auto n = (int) song.patterns.size();
    if (n == 0)
        return 0;
    const auto from = std::clamp (song.currentPattern, 0, n - 1);
    if (delta == 0)
        return from;
    // each step goes to the next listed pattern; past the ends it stays on the last listed one reached
    auto at = from;
    const auto dir = delta > 0 ? 1 : -1;
    for (int steps = std::abs (delta); steps > 0; --steps)
    {
        auto next = at + dir;
        while (next >= 0 && next < n && ! listedInSelector (song, next))
            next += dir;
        if (next < 0 || next >= n)
            break;
        at = next;
    }
    return at;
}

std::string trackLabel (const Song& song, int track)
{
    if (const auto* t = trackOf (song, track))
    {
        if (! t->name.empty())
            return t->name;
        if (t->mode == PlaylistTrackMode::instrument && t->channel >= 0 && t->channel < (int) song.channels.size()
            && ! song.channels[(std::size_t) t->channel].name.empty())
            return song.channels[(std::size_t) t->channel].name;
    }
    return "Track " + std::to_string (track + 1);
}

std::string newClipName (const Song& song, int track)
{
    const auto base = trackLabel (song, track) + " \xC2\xB7 clip "; // (U+00B7 middle dot)
    for (int n = 1;; ++n)
        if (auto name = base + std::to_string (n); ! nameTaken (song, name))
            return name;
}

std::uint32_t trackColour (const Song& song, int track)
{
    if (const auto* t = trackOf (song, track))
    {
        if (t->colour != 0)
            return t->colour;
        if (t->channel >= 0 && t->channel < (int) song.channels.size())
            return song.channels[(std::size_t) t->channel].colour;
    }
    return 0;
}

int newClip (Song& song, int track, std::int64_t length)
{
    Pattern p;
    p.name = newClipName (song, track);
    p.colour = trackColour (song, track);
    p.lengthTicks = std::max<std::int64_t> (1, length);
    p.trackClip = true;
    song.patterns.push_back (std::move (p));
    return (int) song.patterns.size() - 1;
}

int clone (Song& song, int pattern, int track)
{
    if (pattern < 0 || pattern >= (int) song.patterns.size())
        return -1;
    auto copy = copyFor (song, song.patterns[(std::size_t) pattern], track);
    song.patterns.push_back (std::move (copy));
    return (int) song.patterns.size() - 1;
}

int makeCopiesUnique (Song& song, const std::vector<int>& clips)
{
    std::map<int, int> made; // source -> copy
    for (const auto i : clips)
    {
        if (i < 0 || i >= (int) song.playlist.size())
            continue;
        const auto& c = song.playlist[(std::size_t) i];
        if (c.kind != ClipKind::pattern || ! isTrackClip (song, c.pattern))
            continue;
        auto it = made.find (c.pattern);
        if (it == made.end())
            it = made.emplace (c.pattern, clone (song, c.pattern, c.track)).first;
        song.playlist[(std::size_t) i].pattern = it->second;
    }
    return (int) made.size();
}

int makeCopiesUnique (Song& song, std::vector<Clip>& clips)
{
    std::map<int, int> made;
    for (auto& c : clips)
    {
        if (c.kind != ClipKind::pattern || ! isTrackClip (song, c.pattern))
            continue;
        auto it = made.find (c.pattern);
        if (it == made.end())
            it = made.emplace (c.pattern, clone (song, c.pattern, c.track)).first;
        c.pattern = it->second;
    }
    return (int) made.size();
}

Carried carry (const Song& song, const std::vector<Clip>& clips)
{
    Carried out;
    for (const auto& c : clips)
        if (c.kind == ClipKind::pattern && isTrackClip (song, c.pattern)
            && std::none_of (out.patterns.begin(), out.patterns.end(), [&c] (const std::pair<int, Pattern>& p) { return p.first == c.pattern; }))
            out.patterns.emplace_back (c.pattern, song.patterns[(std::size_t) c.pattern]);
    return out;
}

void materialize (Song& song, std::vector<Clip>& clips, const Carried& carried)
{
    if (carried.empty())
        return;
    std::map<int, int> made; // carried index -> new pattern
    for (auto& c : clips)
    {
        if (c.kind != ClipKind::pattern)
            continue;
        const auto source = std::find_if (carried.patterns.begin(), carried.patterns.end(),
                                          [&c] (const std::pair<int, Pattern>& p) { return p.first == c.pattern; });
        if (source == carried.patterns.end())
            continue;
        auto it = made.find (c.pattern);
        if (it == made.end())
        {
            song.patterns.push_back (copyFor (song, source->second, c.track));
            it = made.emplace (c.pattern, (int) song.patterns.size() - 1).first;
        }
        c.pattern = it->second;
    }
}

std::vector<int> uses (const Song& song)
{
    std::vector<int> n (song.patterns.size(), 0);
    const auto add = [&n] (int p) {
        if (p >= 0 && p < (int) n.size())
            ++n[(std::size_t) p];
    };
    song.forEachClipList ([&add] (const std::vector<Clip>& clips) {
        for (const auto& c : clips)
            if (c.kind == ClipKind::pattern)
                add (c.pattern);
    });
    song.forEachBlockList ([&add] (const std::vector<PatternBlock>& blocks) {
        for (const auto& b : blocks)
            add (b.pattern);
    });
    for (const auto& c : song.channels)
        add (c.loopStarterPattern);
    return n;
}

int collect (Song& song, std::vector<int>* removed)
{
    if (std::none_of (song.patterns.begin(), song.patterns.end(), [] (const Pattern& p) { return p.trackClip; }))
        return 0; // (every project without track clips: one pass over the patterns, nothing else)
    const auto n = uses (song);
    std::vector<int> doomed;
    for (int p = 0; p < (int) song.patterns.size(); ++p)
        if (song.patterns[(std::size_t) p].trackClip && n[(std::size_t) p] == 0)
            doomed.push_back (p);
    for (auto it = doomed.rbegin(); it != doomed.rend(); ++it) // (from the last: the earlier indices stay valid)
        song.removePattern (*it);
    if (removed != nullptr)
        *removed = doomed;
    return (int) doomed.size();
}

int ownerTrack (const Song& song, int pattern)
{
    int owner = -1;
    song.forEachClipList ([&] (const std::vector<Clip>& clips) {
        if (owner >= 0)
            return;
        for (const auto& c : clips)
            if (c.kind == ClipKind::pattern && c.pattern == pattern)
            {
                owner = c.track;
                return;
            }
    });
    return owner;
}

std::vector<int> ownerTracks (const Song& song)
{
    std::vector<int> owner (song.patterns.size(), -1);
    song.forEachClipList ([&owner] (const std::vector<Clip>& clips) {
        for (const auto& c : clips)
            if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) owner.size() && owner[(std::size_t) c.pattern] < 0)
                owner[(std::size_t) c.pattern] = c.track;
    });
    return owner;
}

int adopt (Song& song, int track)
{
    const auto* t = trackOf (song, track);
    if (t == nullptr)
        return 0;
    const auto channel = t->mode == PlaylistTrackMode::instrument ? t->channel : -1;
    // per pattern: 1 = played on this track only so far, 2 = played elsewhere too (or by a block / Loop Starter channel)
    std::vector<std::uint8_t> where (song.patterns.size(), 0);
    song.forEachClipList ([&] (const std::vector<Clip>& clips) {
        for (const auto& c : clips)
            if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) where.size())
                where[(std::size_t) c.pattern] = c.track == track && where[(std::size_t) c.pattern] != 2 ? 1 : 2;
    });
    const auto elsewhere = [&where] (int p) {
        if (p >= 0 && p < (int) where.size())
            where[(std::size_t) p] = 2;
    };
    song.forEachBlockList ([&elsewhere] (const std::vector<PatternBlock>& blocks) {
        for (const auto& b : blocks)
            elsewhere (b.pattern);
    });
    for (const auto& c : song.channels)
        elsewhere (c.loopStarterPattern);
    int made = 0;
    for (int p = 0; p < (int) song.patterns.size(); ++p)
    {
        auto& pattern = song.patterns[(std::size_t) p];
        if (where[(std::size_t) p] != 1 || pattern.trackClip)
            continue;
        if (channel >= 0 && ! std::all_of (pattern.notes.begin(), pattern.notes.end(), [channel] (const Note& n) { return n.channel == channel; }))
            continue; // (a pattern of several instruments stays an ordinary pattern)
        pattern.trackClip = true;
        ++made;
    }
    return made;
}

int release (Song& song, int track)
{
    int freed = 0;
    std::vector<char> mine (song.patterns.size(), 0);
    song.forEachClipList ([&] (const std::vector<Clip>& clips) {
        for (const auto& c : clips)
            if (c.kind == ClipKind::pattern && c.track == track && c.pattern >= 0 && c.pattern < (int) mine.size())
                mine[(std::size_t) c.pattern] = 1;
    });
    for (int p = 0; p < (int) song.patterns.size(); ++p)
        if (mine[(std::size_t) p] && song.patterns[(std::size_t) p].trackClip)
        {
            song.patterns[(std::size_t) p].trackClip = false;
            ++freed;
        }
    return freed;
}

void setOwnClips (Song& song, int track, bool on)
{
    if (track < 0 || track >= 500)
        return;
    if ((int) song.playlistTracks.size() <= track)
        song.playlistTracks.resize ((std::size_t) track + 1);
    song.playlistTracks[(std::size_t) track].ownClips = on;
    if (on)
        adopt (song, track);
    else
        release (song, track);
}

bool sameContent (const Pattern& a, const Pattern& b) noexcept
{
    return a.notes == b.notes && a.lengthTicks == b.lengthTicks && a.events == b.events && a.loopOn == b.loopOn && a.loopMode == b.loopMode
           && a.channelLoops == b.channelLoops && a.markers == b.markers && a.loopStarterGenre == b.loopStarterGenre
           && a.autoNotes == b.autoNotes; // (round 190ow)
}

int mergeIdentical (Song& song)
{
    const auto n = (int) song.patterns.size();
    std::vector<int> target ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        target[(std::size_t) i] = i;
    std::vector<int> duplicates;
    for (int a = 0; a < n; ++a)
    {
        if (! song.patterns[(std::size_t) a].trackClip || target[(std::size_t) a] != a)
            continue;
        for (int b = a + 1; b < n; ++b)
            if (song.patterns[(std::size_t) b].trackClip && target[(std::size_t) b] == b
                && sameContent (song.patterns[(std::size_t) a], song.patterns[(std::size_t) b]))
            {
                target[(std::size_t) b] = a;
                duplicates.push_back (b);
            }
    }
    if (duplicates.empty())
        return 0;
    song.forEachClipList ([&target, n] (std::vector<Clip>& clips) {
        for (auto& c : clips)
            if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < n)
                c.pattern = target[(std::size_t) c.pattern];
    });
    song.forEachBlockList ([&target, n] (std::vector<PatternBlock>& blocks) {
        for (auto& b : blocks)
            if (b.pattern >= 0 && b.pattern < n)
                b.pattern = target[(std::size_t) b.pattern];
    });
    for (auto& c : song.channels)
        if (c.loopStarterPattern >= 0 && c.loopStarterPattern < n)
            c.loopStarterPattern = target[(std::size_t) c.loopStarterPattern];
    if (song.currentPattern >= 0 && song.currentPattern < n)
        song.currentPattern = target[(std::size_t) song.currentPattern];
    std::sort (duplicates.begin(), duplicates.end());
    for (auto it = duplicates.rbegin(); it != duplicates.rend(); ++it) // (no clip plays them now: only the indices move)
        song.removePattern (*it);
    return (int) duplicates.size();
}

int busSources (const Song& song, int track) noexcept
{
    const auto* t = trackOf (song, track);
    if (t == nullptr || t->mode != PlaylistTrackMode::audio || t->mixerTrack <= 0 || t->mixerTrack >= (int) song.mixer.size())
        return 0;
    int sources = 0;
    for (int m = 1; m < (int) song.mixer.size(); ++m)
        if (m != t->mixerTrack && song.routes (m, t->mixerTrack))
            ++sources;
    return sources;
}

bool isBusTrack (const Song& song, int track) noexcept
{
    return busSources (song, track) > 0;
}
} // namespace lynx::trackclips

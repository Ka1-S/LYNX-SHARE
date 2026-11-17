#include "core/RegionLoop.h"

#include <algorithm>
#include <limits>

namespace lynx::regionloop
{
namespace
{
// a repeat plays what the region plays: the same source from the same place, with the same clip settings
bool sameContent (const Clip& a, const Clip& b) noexcept
{
    return a.kind == b.kind && a.track == b.track && a.pattern == b.pattern && a.channel == b.channel && a.offset == b.offset
           && a.offsetFraction == b.offsetFraction && a.gain == b.gain && a.pan == b.pan && a.pitch == b.pitch && a.reversed == b.reversed
           && a.stretch == b.stretch && a.muted == b.muted && a.groove == b.groove && a.followKey == b.followKey && a.keyShift == b.keyShift
           && a.gridSlot == b.gridSlot;
}

int clipEndingAt (const Song& song, const Clip& next, std::int64_t at, const std::vector<int>& skip)
{
    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (c.end() == at && c.length >= next.length && sameContent (c, next) && std::find (skip.begin(), skip.end(), i) == skip.end())
            return i;
    }
    return -1;
}

int clipStartingAt (const Song& song, const Clip& region, std::int64_t at, const std::vector<int>& skip)
{
    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (c.start == at && c.length <= region.length && sameContent (c, region) && std::find (skip.begin(), skip.end(), i) == skip.end())
            return i;
    }
    return -1;
}
} // namespace

bool loopable (const Song& song, int clip) noexcept
{
    if (clip < 0 || clip >= (int) song.playlist.size())
        return false;
    const auto& c = song.playlist[(std::size_t) clip];
    return (c.kind == ClipKind::pattern || c.kind == ClipKind::audio) && c.length > 0 && c.gridSlot < 0;
}

Run runOf (const Song& song, int clip)
{
    Run run;
    if (! loopable (song, clip))
        return run;
    // back to the region: each earlier pass is a whole pass (as long as the clip after it, or longer for the last)
    std::vector<int> seen { clip };
    auto first = clip;
    for (;;)
    {
        const auto& cur = song.playlist[(std::size_t) first];
        const auto prev = clipEndingAt (song, cur, cur.start, seen);
        if (prev < 0 || (first != clip && song.playlist[(std::size_t) prev].length != cur.length))
            break;
        seen.push_back (prev);
        first = prev;
    }
    const auto region = song.playlist[(std::size_t) first];
    run.clips = { first };
    run.start = region.start;
    run.pass = region.length;
    // forward: whole passes, the last one may be shorter
    auto at = region.end();
    for (;;)
    {
        const auto next = clipStartingAt (song, region, at, run.clips);
        if (next < 0)
            break;
        run.clips.push_back (next);
        const auto& n = song.playlist[(std::size_t) next];
        at = n.end();
        if (n.length < region.length)
            break;
    }
    run.end = at;
    return run;
}

std::int64_t nextStart (const Song& song, const Run& run)
{
    auto next = std::numeric_limits<std::int64_t>::max();
    if (run.empty())
        return next;
    const auto& region = song.playlist[(std::size_t) run.clips.front()];
    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (c.track == region.track && c.gridSlot < 0 && c.start > region.start && std::find (run.clips.begin(), run.clips.end(), i) == run.clips.end())
            next = std::min (next, c.start);
    }
    return next;
}

std::int64_t loopTo (Song& song, int clip, std::int64_t end)
{
    const auto run = runOf (song, clip);
    if (run.empty())
        return -1;
    const auto region = song.playlist[(std::size_t) run.clips.front()];
    const auto pass = region.length;
    const auto firstEnd = region.end();
    const auto cap = nextStart (song, run);
    end = std::max (firstEnd, std::min (end, cap));
    if (end == run.end)
        return -1;

    // the old repeats out (highest index first), the new ones appended
    auto repeats = std::vector<int> (run.clips.begin() + 1, run.clips.end());
    std::sort (repeats.begin(), repeats.end(), std::greater<>());
    for (const auto i : repeats)
        song.playlist.erase (song.playlist.begin() + i);
    for (auto at = firstEnd; at < end; at += pass)
    {
        auto copy = region;
        copy.start = at;
        copy.length = std::min (pass, end - at);
        copy.fadeIn = copy.fadeOut = 0; // (the region's fades stay on the region)
        copy.crossfadeIn = copy.crossfadeOut = 0;
        copy.fadeCurve = 0;
        copy.uid = 0; // (a new clip: uids::normalize gives it its own)
        song.playlist.push_back (copy);
    }
    return end;
}

std::int64_t loopUntilNext (Song& song, int clip, std::int64_t songEnd)
{
    const auto run = runOf (song, clip);
    if (run.empty())
        return -1;
    auto next = nextStart (song, run);
    if (next == std::numeric_limits<std::int64_t>::max())
        next = songEnd;
    if (next <= song.playlist[(std::size_t) run.clips.front()].end())
        return -1;
    return loopTo (song, clip, next);
}

bool unloop (Song& song, int clip)
{
    const auto run = runOf (song, clip);
    if (run.passes() <= 1)
        return false;
    auto repeats = std::vector<int> (run.clips.begin() + 1, run.clips.end());
    std::sort (repeats.begin(), repeats.end(), std::greater<>());
    for (const auto i : repeats)
        song.playlist.erase (song.playlist.begin() + i);
    return true;
}

int regionAt (const Song& song, int track, std::int64_t start, const Clip& like)
{
    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (c.track == track && c.start == start && c.kind == like.kind && c.pattern == like.pattern && c.channel == like.channel && c.gridSlot < 0)
            return i;
    }
    return -1;
}
} // namespace lynx::regionloop

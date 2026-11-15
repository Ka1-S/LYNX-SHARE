#include "core/PatternBlocks.h"
#include "core/PlaylistTrackOps.h"
#include "core/RackOps.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace lynx::patternblocks
{
namespace
{
bool before (const PatternBlock& a, const PatternBlock& b) noexcept
{
    return a.pattern != b.pattern ? a.pattern < b.pattern : a.start < b.start;
}

// [first, last) of row `pattern` in a normalized list
std::pair<std::size_t, std::size_t> rowRange (const std::vector<PatternBlock>& blocks, int pattern) noexcept
{
    const auto lo = std::lower_bound (blocks.begin(), blocks.end(), PatternBlock { pattern, std::numeric_limits<std::int64_t>::min() }, before);
    const auto hi = std::lower_bound (lo, blocks.end(), PatternBlock { pattern + 1, std::numeric_limits<std::int64_t>::min() }, before);
    return { (std::size_t) (lo - blocks.begin()), (std::size_t) (hi - blocks.begin()) };
}

bool validPattern (const Song& song, int pattern) noexcept { return pattern >= 0 && pattern < (int) song.patterns.size(); }

// Removes every block whose played span meets [fromTick, toTick]: of row `pattern`, or of any row when -1.
int eraseSpan (Song& song, int pattern, std::int64_t fromTick, std::int64_t toTick)
{
    if (toTick < fromTick)
        std::swap (fromTick, toTick);
    std::vector<std::size_t> gone;
    if (pattern < 0)
    {
        for (std::size_t i = 0; i < song.blocks.size(); ++i)
            if (song.blocks[i].start <= toTick && fromTick < playedEnd (song, song.blocks, i))
                gone.push_back (i);
    }
    else
    {
        const auto [first, last] = rowRange (song.blocks, pattern);
        for (auto i = first; i < last; ++i)
            if (song.blocks[i].start <= toTick && fromTick < playedEnd (song, song.blocks, i))
                gone.push_back (i);
    }
    for (auto it = gone.rbegin(); it != gone.rend(); ++it)
        song.blocks.erase (song.blocks.begin() + (std::ptrdiff_t) *it);
    return (int) gone.size();
}
} // namespace

std::int64_t lengthOf (const Song& song, int pattern) noexcept
{
    return validPattern (song, pattern) ? song.patterns[(std::size_t) pattern].effectiveLength (song.meter()) : 0;
}

bool active (const Song& song) noexcept { return song.patternBlocksOn && ! song.blocks.empty(); }

void normalize (std::vector<PatternBlock>& blocks, int patternCount)
{
    blocks.erase (std::remove_if (blocks.begin(), blocks.end(),
                                  [patternCount] (const PatternBlock& b) { return b.pattern < 0 || b.pattern >= patternCount || b.start < 0; }),
                  blocks.end());
    std::sort (blocks.begin(), blocks.end(), before);
    // one block per (pattern, start), whatever its mute
    blocks.erase (std::unique (blocks.begin(), blocks.end(),
                               [] (const PatternBlock& a, const PatternBlock& b) { return a.pattern == b.pattern && a.start == b.start; }),
                  blocks.end());
}

std::int64_t playedEnd (const Song& song, const std::vector<PatternBlock>& blocks, std::size_t index) noexcept
{
    if (index >= blocks.size())
        return 0;
    const auto& b = blocks[index];
    auto stop = b.start + lengthOf (song, b.pattern);
    if (index + 1 < blocks.size() && blocks[index + 1].pattern == b.pattern)
        stop = std::min (stop, blocks[index + 1].start);
    return stop;
}

int blockAt (const Song& song, int pattern, std::int64_t tick) noexcept
{
    const auto [first, last] = rowRange (song.blocks, pattern);
    for (auto i = first; i < last; ++i)
    {
        if (song.blocks[i].start > tick)
            break;
        if (tick < playedEnd (song, song.blocks, i))
            return (int) i;
    }
    return -1;
}

std::int64_t snapToBar (const Song& song, std::int64_t tick) { return song.barAt (std::max<std::int64_t> (0, tick)).tick; }

bool fits (const Song& song, int pattern, std::int64_t start) noexcept
{
    const auto length = lengthOf (song, pattern);
    if (length <= 0 || start < 0)
        return false;
    const auto [first, last] = rowRange (song.blocks, pattern);
    for (auto i = first; i < last; ++i)
        if (song.blocks[i].start < start + length && start < playedEnd (song, song.blocks, i))
            return false;
    return true;
}

bool place (Song& song, int pattern, std::int64_t tick)
{
    const auto start = snapToBar (song, tick);
    if (! validPattern (song, pattern) || ! fits (song, pattern, start))
        return false;
    const PatternBlock block { pattern, start };
    song.blocks.insert (std::upper_bound (song.blocks.begin(), song.blocks.end(), block, before), block);
    return true;
}

int paintRange (Song& song, int pattern, std::int64_t fromTick, std::int64_t toTick)
{
    if (! validPattern (song, pattern))
        return 0;
    if (toTick < fromTick)
        std::swap (fromTick, toTick);
    fromTick = std::max<std::int64_t> (0, fromTick);
    toTick = std::max<std::int64_t> (0, toTick);
    int placed = 0;
    // every bar from the one under fromTick to the one under toTick
    for (auto bar = song.barAt (fromTick); bar.tick <= toTick && bar.length > 0;)
    {
        if (place (song, pattern, bar.tick))
            ++placed;
        bar = song.barAt (bar.tick + bar.length);
    }
    return placed;
}

bool erase (Song& song, int pattern, std::int64_t tick)
{
    const auto index = blockAt (song, pattern, tick);
    if (index < 0)
        return false;
    song.blocks.erase (song.blocks.begin() + index);
    return true;
}

std::int64_t move (Song& song, int pattern, std::int64_t fromTick, std::int64_t toTick)
{
    const auto index = blockAt (song, pattern, fromTick);
    if (index < 0)
        return -1;
    const auto start = song.blocks[(std::size_t) index].start;
    const auto to = snapToBar (song, std::max<std::int64_t> (0, toTick));
    if (to == start)
        return start;
    const PatternBlock block = song.blocks[(std::size_t) index];
    song.blocks.erase (song.blocks.begin() + (std::ptrdiff_t) index); // out first, so it never collides with itself
    if (! fits (song, pattern, to))
    {
        song.blocks.insert (std::upper_bound (song.blocks.begin(), song.blocks.end(), block, before), block);
        return start;
    }
    auto moved = block; // (its mute goes with it)
    moved.start = to;
    song.blocks.insert (std::upper_bound (song.blocks.begin(), song.blocks.end(), moved, before), moved);
    return to;
}

int eraseRange (Song& song, int pattern, std::int64_t fromTick, std::int64_t toTick)
{
    if (pattern < 0) // rows are never negative: keep the old contract (no row, nothing erased)
        return 0;
    return eraseSpan (song, pattern, fromTick, toTick);
}

int eraseAnyRow (Song& song, std::int64_t fromTick, std::int64_t toTick)
{
    return eraseSpan (song, -1, fromTick, toTick);
}

bool anyInRange (const Song& song, int pattern, std::int64_t fromTick, std::int64_t toTick) noexcept
{
    if (pattern < 0)
        return false;
    if (toTick < fromTick)
        std::swap (fromTick, toTick);
    const auto [first, last] = rowRange (song.blocks, pattern);
    for (auto i = first; i < last; ++i)
        if (song.blocks[i].start <= toTick && fromTick < playedEnd (song, song.blocks, i))
            return true;
    return false;
}

std::int64_t grabOffset (const Song& song, int pattern, std::int64_t tick)
{
    const auto index = blockAt (song, pattern, tick);
    return index < 0 ? 0 : std::max<std::int64_t> (0, snapToBar (song, tick) - song.blocks[(std::size_t) index].start);
}

std::int64_t dragTarget (const Song& song, std::int64_t tick, std::int64_t grabOffset)
{
    return snapToBar (song, std::max<std::int64_t> (0, snapToBar (song, tick) - std::max<std::int64_t> (0, grabOffset)));
}

std::vector<std::size_t> blocksInRange (const Song& song, int pattern, std::int64_t fromTick, std::int64_t toTick)
{
    if (toTick < fromTick)
        std::swap (fromTick, toTick);
    std::vector<std::size_t> out;
    std::size_t first = 0, last = song.blocks.size();
    if (pattern >= 0)
        std::tie (first, last) = rowRange (song.blocks, pattern);
    for (auto i = first; i < last; ++i)
        if (song.blocks[i].start <= toTick && fromTick < playedEnd (song, song.blocks, i))
            out.push_back (i);
    return out;
}

bool setMuted (Song& song, std::size_t index, bool muted)
{
    if (index >= song.blocks.size() || song.blocks[index].muted == muted)
        return false;
    song.blocks[index].muted = muted;
    return true;
}

std::vector<PatternBlock> inArea (const Song& song, int rowFrom, int rowTo, std::int64_t fromTick, std::int64_t toTick)
{
    if (rowTo < rowFrom)
        std::swap (rowFrom, rowTo);
    std::vector<PatternBlock> out;
    for (auto row = std::max (0, rowFrom); row <= rowTo && row < (int) song.patterns.size(); ++row)
        for (auto i : blocksInRange (song, row, fromTick, toTick))
            out.push_back (song.blocks[i]);
    return out;
}

bool contains (const std::vector<PatternBlock>& list, const PatternBlock& b) noexcept
{
    return std::any_of (list.begin(), list.end(), [&b] (const PatternBlock& x) { return x.pattern == b.pattern && x.start == b.start; });
}

int indexOf (const Song& song, const PatternBlock& b) noexcept
{
    const auto [first, last] = rowRange (song.blocks, b.pattern);
    for (auto i = first; i < last; ++i)
        if (song.blocks[i].start == b.start)
            return (int) i;
    return -1;
}

std::optional<std::vector<PatternBlock>> moveGroup (Song& song, const std::vector<PatternBlock>& group, std::int64_t delta)
{
    if (group.empty())
        return std::nullopt;
    // the blocks themselves, as they are now (their mute included)
    std::vector<PatternBlock> moving;
    for (const auto& g : group)
    {
        const auto i = indexOf (song, g);
        if (i < 0)
            return std::nullopt;
        moving.push_back (song.blocks[(std::size_t) i]);
    }
    std::vector<PatternBlock> moved;
    for (auto b : moving)
    {
        if (b.start + delta < 0)
            return std::nullopt; // (the group keeps its shape: nothing goes before bar 1)
        b.start = snapToBar (song, b.start + delta);
        moved.push_back (b);
    }
    const auto saved = song.blocks;
    song.blocks.erase (std::remove_if (song.blocks.begin(), song.blocks.end(), [&moving] (const PatternBlock& b) { return contains (moving, b); }),
                       song.blocks.end());
    for (const auto& b : moved)
    {
        if (! fits (song, b.pattern, b.start))
        {
            song.blocks = saved;
            return std::nullopt;
        }
        song.blocks.insert (std::upper_bound (song.blocks.begin(), song.blocks.end(), b, before), b);
    }
    return moved;
}

int slice (Song& song, int pattern, std::int64_t cut)
{
    if (! validPattern (song, pattern) || cut <= 0 || cut >= lengthOf (song, pattern))
        return -1;
    const auto current = song.currentPattern;
    const auto added = clonePattern (song, pattern); // (clips and blocks of later patterns move down one)
    if (added != pattern + 1)
        return -1;
    song.currentPattern = current > pattern ? current + 1 : current;

    auto& head = song.patterns[(std::size_t) pattern];
    auto& tail = song.patterns[(std::size_t) added];
    // the pattern keeps what is before the cut: a note across it is shortened (FL 10 doesn't split it in two)
    head.notes.erase (std::remove_if (head.notes.begin(), head.notes.end(), [cut] (const Note& n) { return n.start >= cut; }), head.notes.end());
    for (auto& n : head.notes)
        n.length = std::min (n.length, cut - n.start);
    // the new one what starts at or after it, moved to its start
    tail.notes.erase (std::remove_if (tail.notes.begin(), tail.notes.end(), [cut] (const Note& n) { return n.start < cut; }), tail.notes.end());
    for (auto& n : tail.notes)
        n.start -= cut;
    for (auto& lane : head.events)
        lane.events.erase (std::remove_if (lane.events.begin(), lane.events.end(), [cut] (const PatternEvent& e) { return e.tick >= cut; }),
                           lane.events.end());
    for (auto& lane : tail.events)
    {
        lane.events.erase (std::remove_if (lane.events.begin(), lane.events.end(), [cut] (const PatternEvent& e) { return e.tick < cut; }),
                           lane.events.end());
        for (auto& e : lane.events)
            e.tick -= cut;
    }
    tail.events.erase (std::remove_if (tail.events.begin(), tail.events.end(), [] (const PatternEvents& l) { return l.events.empty(); }), tail.events.end());
    head.events.erase (std::remove_if (head.events.begin(), head.events.end(), [] (const PatternEvents& l) { return l.events.empty(); }), head.events.end());
    head.markers.erase (std::remove_if (head.markers.begin(), head.markers.end(), [cut] (const Marker& m) { return m.tick >= cut; }), head.markers.end());
    tail.markers.erase (std::remove_if (tail.markers.begin(), tail.markers.end(), [cut] (const Marker& m) { return m.tick < cut; }), tail.markers.end());
    for (auto& m : tail.markers)
        m.tick -= cut;
    if (head.lengthTicks > 0) // a set length is shared out; an automatic one stays automatic (whole bars)
    {
        tail.lengthTicks = head.lengthTicks - cut;
        head.lengthTicks = cut;
    }

    // where each block of the pattern now ends, a block of the new one starts (in every arrangement)
    const auto headLength = lengthOf (song, pattern), tailLength = lengthOf (song, added);
    song.forEachBlockList ([&] (std::vector<PatternBlock>& list) {
        std::vector<PatternBlock> follow;
        for (const auto& b : list)
            if (b.pattern == pattern)
                follow.push_back ({ added, b.start + headLength, b.muted });
        for (const auto& b : follow)
        {
            const auto [first, last] = rowRange (list, added);
            auto free = tailLength > 0;
            for (auto i = first; i < last && free; ++i)
                free = ! (list[i].start < b.start + tailLength && b.start < playedEnd (song, list, i));
            if (free)
                list.insert (std::upper_bound (list.begin(), list.end(), b, before), b);
        }
    });
    return added;
}

std::vector<Clip> asClips (const Song& song)
{
    std::vector<Clip> out;
    out.reserve (song.blocks.size());
    for (std::size_t i = 0; i < song.blocks.size(); ++i)
    {
        const auto& b = song.blocks[i];
        if (! validPattern (song, b.pattern))
            continue;
        Clip c;
        c.kind = ClipKind::pattern;
        c.pattern = b.pattern;
        c.track = blockTrack;
        c.start = b.start;
        c.length = playedEnd (song, song.blocks, i) - b.start;
        c.muted = b.muted; // (issue #127: the Mute tool)
        if (c.length > 0)
            out.push_back (c);
    }
    return out;
}

std::int64_t end (const Song& song) noexcept
{
    if (! active (song))
        return 0;
    std::int64_t last = 0;
    for (std::size_t i = 0; i < song.blocks.size(); ++i)
        last = std::max (last, playedEnd (song, song.blocks, i));
    return last;
}

void expandSong (Song& song)
{
    if (active (song))
    {
        auto clips = asClips (song);
        song.playlist.insert (song.playlist.end(), clips.begin(), clips.end());
        std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    }
    song.patternBlocksOn = false;
    song.forEachBlockList ([] (std::vector<PatternBlock>& list) { list.clear(); });
}

int toClips (Song& song)
{
    normalize (song.blocks, (int) song.patterns.size());
    auto clips = asClips (song);
    if (clips.empty())
    {
        song.blocks.clear();
        return 0;
    }

    // the first track under everything in use (clips, named or coloured tracks)
    int next = 0;
    for (const auto& c : song.playlist)
        next = std::max (next, c.track + 1);
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
        if (const auto& pt = song.playlistTracks[(std::size_t) t]; ! pt.name.empty() || pt.colour != 0 || pt.lockedToContent
                                                                       || pt.mode != PlaylistTrackMode::unassigned)
            next = std::max (next, t + 1);

    int row = -1, track = next - 1;
    for (auto& c : clips)
    {
        if (c.pattern != row)
        {
            row = c.pattern;
            ++track;
            if ((int) song.playlistTracks.size() <= track)
                song.playlistTracks.resize ((std::size_t) track + 1);
            song.playlistTracks[(std::size_t) track].lockedToContent = true;
        }
        c.track = track;
        song.playlist.push_back (c);
    }
    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    for (int t = next; t <= track; ++t)
        pltracks::autoName (song, t); // FL's Auto name: the pattern's name and colour
    song.blocks.clear();
    return (int) clips.size();
}

int toClipsInEveryArrangement (Song& song)
{
    const auto current = song.currentArrangement;
    int made = 0;
    for (int i = 0; i < (int) song.arrangements.size(); ++i)
    {
        song.selectArrangement (i);
        made += toClips (song);
    }
    song.selectArrangement (current);
    return made;
}

void removePattern (Song& song, int index)
{
    song.forEachBlockList ([index] (std::vector<PatternBlock>& list) {
        list.erase (std::remove_if (list.begin(), list.end(), [index] (const PatternBlock& b) { return b.pattern == index; }), list.end());
        for (auto& b : list)
            if (b.pattern > index)
                --b.pattern;
    });
}

void shiftPatterns (Song& song, int index, int delta)
{
    song.forEachBlockList ([index, delta] (std::vector<PatternBlock>& list) {
        for (auto& b : list)
            if (b.pattern >= index)
                b.pattern += delta;
    });
}

void swapPatterns (Song& song, int a, int b)
{
    const auto count = (int) song.patterns.size();
    song.forEachBlockList ([a, b, count] (std::vector<PatternBlock>& list) {
        for (auto& x : list)
            x.pattern = x.pattern == a ? b : x.pattern == b ? a : x.pattern;
        normalize (list, count);
    });
}
} // namespace lynx::patternblocks

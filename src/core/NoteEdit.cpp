#include "core/NoteEdit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>

namespace lynx::noteedit
{
namespace
{
bool valid (const Pattern& p, int i)
{
    return i >= 0 && i < (int) p.notes.size();
}
} // namespace

void sortTracking (Pattern& pattern, std::vector<int>& tracked)
{
    sortByStartTracking (pattern.notes, tracked);
}

void remove (Pattern& pattern, std::vector<int> indices)
{
    std::sort (indices.begin(), indices.end());
    indices.erase (std::unique (indices.begin(), indices.end()), indices.end());

    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        if (valid (pattern, *it))
            pattern.notes.erase (pattern.notes.begin() + *it);
}

std::vector<Note> copy (const Pattern& pattern, const std::vector<int>& indices)
{
    std::vector<Note> out;

    for (auto i : indices)
        if (valid (pattern, i))
            out.push_back (pattern.notes[(std::size_t) i]);

    std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    return out;
}

std::vector<int> paste (Pattern& pattern, const std::vector<Note>& notes, std::int64_t offset, int channel)
{
    std::vector<int> added;

    for (auto n : notes)
    {
        n.start = std::max<std::int64_t> (0, n.start + offset);

        if (channel >= 0)
            n.channel = channel;

        added.push_back ((int) pattern.notes.size());
        pattern.notes.push_back (n);
    }

    return added;
}

void span (const std::vector<Note>& notes, std::int64_t& start, std::int64_t& end)
{
    start = end = 0;

    if (notes.empty())
        return;

    start = notes.front().start;
    end = 0;

    for (const auto& n : notes)
    {
        start = std::min (start, n.start);
        end = std::max (end, n.start + n.length);
    }
}

std::vector<int> duplicate (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid)
{
    const auto notes = copy (pattern, indices);

    if (notes.empty())
        return {};

    std::int64_t start = 0, end = 0;
    span (notes, start, end);
    grid = std::max<std::int64_t> (1, grid);
    const auto offset = std::max<std::int64_t> (grid, (end - start + grid - 1) / grid * grid);
    return paste (pattern, notes, offset);
}

std::vector<int> duplicateBars (Pattern& pattern, const std::vector<int>& indices, int beatsPerBar)
{
    return duplicate (pattern, indices, (std::int64_t) ppq * std::max (1, beatsPerBar));
}

void quantize (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid)
{
    grid = std::max<std::int64_t> (1, grid);

    for (auto i : indices)
        if (valid (pattern, i))
        {
            auto& n = pattern.notes[(std::size_t) i];
            n.start = (n.start + grid / 2) / grid * grid;
        }
}

void quantizeFull (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid)
{
    grid = std::max<std::int64_t> (1, grid);

    for (auto i : indices)
        if (valid (pattern, i))
        {
            auto& n = pattern.notes[(std::size_t) i];
            const auto end = (n.start + n.length + grid / 2) / grid * grid;
            n.start = (n.start + grid / 2) / grid * grid;
            n.length = std::max (grid, end - n.start);
        }
}

void move (Pattern& pattern, const std::vector<int>& indices, std::int64_t& deltaTicks, int& deltaKeys)
{
    std::int64_t minStart = INT64_MAX;
    int minKey = 127, maxKey = 0;

    for (auto i : indices)
        if (valid (pattern, i))
        {
            const auto& n = pattern.notes[(std::size_t) i];
            minStart = std::min (minStart, n.start);
            minKey = std::min (minKey, n.key);
            maxKey = std::max (maxKey, n.key);
        }

    if (minStart == INT64_MAX)
    {
        deltaTicks = 0;
        deltaKeys = 0;
        return;
    }

    deltaTicks = std::max (deltaTicks, -minStart);
    deltaKeys = std::clamp (deltaKeys, -minKey, 127 - maxKey);

    for (auto i : indices)
        if (valid (pattern, i))
        {
            auto& n = pattern.notes[(std::size_t) i];
            n.start += deltaTicks;
            n.key += deltaKeys;
        }
}

// --- quick note operations ------------------------------------------------------------

int split (Pattern& pattern, int index, std::int64_t tick)
{
    if (! valid (pattern, index))
        return -1;

    auto& n = pattern.notes[(std::size_t) index];

    if (tick <= n.start || tick >= n.start + n.length)
        return -1;

    auto tail = n;
    tail.start = tick;
    tail.length = n.start + n.length - tick;
    tail.slide = false; // the tail continues the note; it does not glide again
    tail.porta = false;
    n.length = tick - n.start;
    pattern.notes.push_back (tail);
    return (int) pattern.notes.size() - 1;
}

std::vector<int> chop (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid)
{
    grid = std::max<std::int64_t> (1, grid);
    std::vector<int> pieces;

    for (auto i : indices)
    {
        if (! valid (pattern, i))
            continue;

        pieces.push_back (i);
        auto current = i;
        const auto start = pattern.notes[(std::size_t) i].start;
        const auto end = start + pattern.notes[(std::size_t) i].length;

        for (auto t = (start / grid + 1) * grid; t < end; t += grid)
        {
            current = split (pattern, current, t);

            if (current < 0)
                break;

            pieces.push_back (current);
        }
    }

    return pieces;
}

int join (Pattern& pattern, const std::vector<int>& indices)
{
    std::vector<int> order;

    for (auto i : indices)
        if (valid (pattern, i))
            order.push_back (i);

    std::sort (order.begin(), order.end());
    order.erase (std::unique (order.begin(), order.end()), order.end());
    std::sort (order.begin(), order.end(), [&pattern] (int a, int b) {
        const auto& x = pattern.notes[(std::size_t) a];
        const auto& y = pattern.notes[(std::size_t) b];
        return std::tie (x.channel, x.key, x.start) < std::tie (y.channel, y.key, y.start);
    });

    std::vector<int> doomed;
    int keeper = -1;

    for (auto i : order)
    {
        auto& n = pattern.notes[(std::size_t) i];

        if (keeper >= 0)
        {
            auto& k = pattern.notes[(std::size_t) keeper];

            if (k.channel == n.channel && k.key == n.key && n.start <= k.start + k.length)
            {
                k.length = std::max (k.start + k.length, n.start + n.length) - k.start;
                doomed.push_back (i);
                continue;
            }
        }

        keeper = i;
    }

    remove (pattern, doomed);
    return (int) doomed.size();
}

void legato (Pattern& pattern, const std::vector<int>& indices)
{
    std::map<int, std::set<std::int64_t>> starts; // per channel

    for (auto i : indices)
        if (valid (pattern, i))
            starts[pattern.notes[(std::size_t) i].channel].insert (pattern.notes[(std::size_t) i].start);

    for (auto i : indices)
        if (valid (pattern, i))
        {
            auto& n = pattern.notes[(std::size_t) i];
            const auto& s = starts[n.channel];

            if (const auto next = s.upper_bound (n.start); next != s.end())
                n.length = *next - n.start;
        }
}

void shrinkToGrid (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid)
{
    grid = std::max<std::int64_t> (1, grid);

    for (auto i : indices)
        if (valid (pattern, i))
            pattern.notes[(std::size_t) i].length = grid;
}

void rotate (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid, std::int64_t bar, int steps)
{
    grid = std::max<std::int64_t> (1, grid);
    bar = std::max<std::int64_t> (1, bar);
    std::int64_t first = INT64_MAX, last = -1;

    for (auto i : indices)
        if (valid (pattern, i))
        {
            first = std::min (first, pattern.notes[(std::size_t) i].start);
            last = std::max (last, pattern.notes[(std::size_t) i].start);
        }

    if (last < 0)
        return;

    const auto from = first / bar * bar;
    const auto span = (last / bar + 1) * bar - from;

    for (auto i : indices)
        if (valid (pattern, i))
        {
            auto& n = pattern.notes[(std::size_t) i];
            const auto shifted = (n.start - from + (std::int64_t) steps * grid) % span;
            n.start = from + (shifted < 0 ? shifted + span : shifted);
        }
}

// --- grid -------------------------------------------------------------------------------

std::int64_t snapDown (double tick, std::int64_t step)
{
    if (step <= 1)
        return (std::int64_t) std::floor (tick);

    return (std::int64_t) std::floor (tick / (double) step) * step;
}

std::int64_t snapRound (double tick, std::int64_t step)
{
    if (step <= 1)
        return (std::int64_t) std::floor (tick + 0.5);

    return (std::int64_t) std::floor (tick / (double) step + 0.5) * step;
}

bool paintWouldStack (const std::vector<Clip>& playlist, const Clip& c)
{
    for (const auto& other : playlist)
    {
        if (other.track != c.track || other.kind != c.kind || other.start >= c.end() || c.start >= other.end())
            continue;
        const auto same = c.kind == ClipKind::pattern    ? other.pattern == c.pattern
                          : c.kind == ClipKind::audio    ? other.channel == c.channel
                          : c.kind == ClipKind::automation ? other.automation == c.automation
                                                           : true;
        if (same)
            return true;
    }
    return false;
}

std::int64_t snapNearest (double tick, const std::vector<std::int64_t>& candidates, double maxDistance, std::int64_t fallbackStep)
{
    auto best = -1.0;
    std::int64_t bestTick = 0;

    for (auto c : candidates)
    {
        const auto d = std::abs ((double) c - tick);

        if (d <= maxDistance && (best < 0.0 || d < best))
        {
            best = d;
            bestTick = c;
        }
    }

    return best >= 0.0 ? bestTick : snapDown (tick, fallbackStep);
}

std::int64_t screenStep (double pixelsPerTick, std::int64_t bar, double minPixels)
{
    for (const std::int64_t step : { (std::int64_t) ticksPerStep / 4, (std::int64_t) ticksPerStep / 2, (std::int64_t) ticksPerStep, (std::int64_t) ppq })
        if (step * pixelsPerTick >= minPixels && step < bar)
            return step;

    auto step = std::max<std::int64_t> (1, bar);

    while (step * pixelsPerTick < minPixels && step < bar * 1024)
        step *= 2;

    return step;
}

std::int64_t flLineStep (double pixelsPerTick, int numerator, int denominator, double minPixels)
{
    numerator = std::max (1, numerator);
    denominator = std::clamp (denominator, 1, 16);
    const auto step = (std::int64_t) ticksPerStep;
    const auto beat = std::max<std::int64_t> (1, 16 / denominator) * step;
    const auto bar = numerator * beat;
    const auto min = std::floor (std::max (1.0, minPixels)); // (FL compares against a whole number of pixels)
    if (! (pixelsPerTick > 0.0))
        return bar;

    const auto stepPixels = (double) step * pixelsPerTick, beatPixels = (double) beat * pixelsPerTick;
    const auto odd = (numerator & 1) != 0;
    if (stepPixels >= min)
        return step;
    if (min <= stepPixels * 2.0 && ! (denominator == 16 && odd))
        return step * 2;
    if (beatPixels >= min)
        return beat;
    if (min <= beatPixels * 2.0 && ! odd)
        return beat * 2;

    auto bars = bar;
    while ((double) bars * pixelsPerTick < min && bars <= bar * 16)
        bars *= 2;
    return bars;
}

std::string formatTime (std::int64_t ticks, int beatsPerBar, bool isLength)
{
    const auto bar = (std::int64_t) ppq * std::max (1, beatsPerBar);
    ticks = std::max<std::int64_t> (0, ticks);
    const auto base = isLength ? 0 : 1;
    char text[64];
    std::snprintf (text, sizeof (text), "%lld:%lld:%lld", (long long) (ticks / bar + base), (long long) (ticks % bar / ticksPerStep + base),
                   (long long) (ticks % ticksPerStep));
    return text;
}

bool parseTime (const std::string& text, int beatsPerBar, std::int64_t& ticks, bool isLength)
{
    long long parts[3] = { 0, 0, 0 };
    int count = 0;
    std::string field;

    for (std::size_t i = 0; i <= text.size(); ++i)
    {
        const auto c = i < text.size() ? text[i] : ':';

        if (c == ':')
        {
            if (field.empty() || count >= 3)
                return false;

            parts[count++] = std::stoll (field);
            field.clear();
        }
        else if (c >= '0' && c <= '9')
        {
            if (field.size() > 9)
                return false;
            field += c;
        }
        else if (c != ' ')
            return false;
    }

    const auto base = isLength ? 0 : 1;
    const auto stepsPerBar = (long long) std::max (1, beatsPerBar) * stepsPerBeat;

    if (count == 0 || parts[0] < base || (count >= 2 && (parts[1] < base || parts[1] - base >= stepsPerBar))
        || (count == 3 && parts[2] >= ticksPerStep))
        return false;

    const auto bar = (std::int64_t) ppq * std::max (1, beatsPerBar);
    ticks = (parts[0] - base) * bar + (count >= 2 ? (parts[1] - base) * ticksPerStep : 0) + (count == 3 ? parts[2] : 0);
    return true;
}

// --- Playlist clips -----------------------------------------------------------------------

namespace
{
void clampFades (Clip& c)
{
    c.fadeIn = std::clamp<std::int64_t> (c.fadeIn, 0, c.length);
    c.fadeOut = std::clamp<std::int64_t> (c.fadeOut, 0, c.length - c.fadeIn);
}
} // namespace

void trimStart (Clip& clip, std::int64_t newStart, std::int64_t minLength)
{
    minLength = std::max<std::int64_t> (1, std::min (minLength, clip.length));
    auto delta = newStart - clip.start;
    delta = std::max (delta, -clip.offset);
    delta = std::max (delta, -clip.start);
    delta = std::min (delta, clip.length - minLength);
    clip.start += delta;
    clip.offset += delta;
    clip.length -= delta;
    clampFades (clip);
}

void slip (Clip& clip, std::int64_t delta)
{
    clip.offset = std::max<std::int64_t> (0, clip.offset - delta);
}

void insertTime (std::vector<Clip>& clips, std::vector<Marker>& markers, std::int64_t at, std::int64_t length)
{
    if (length <= 0)
        return;

    for (auto& c : clips)
        if (c.start >= at)
            c.start += length;

    for (auto& m : markers)
        if (m.tick >= at)
            m.tick += length;
}

void sliceInsertTime (std::vector<Clip>& clips, std::vector<Marker>& markers, std::int64_t at, std::int64_t length)
{
    if (length <= 0)
        return;

    const auto count = clips.size();
    for (std::size_t i = 0; i < count; ++i)
        if (clips[i].start < at && clips[i].end() > at)
        {
            auto rest = splitClip (clips[i], at);
            clampFades (clips[i]);
            clampFades (rest);
            clips.push_back (rest);
        }

    insertTime (clips, markers, at, length);
}

void trimTime (std::vector<Clip>& clips, std::vector<Marker>& markers, std::int64_t from, std::int64_t to)
{
    if (to <= from)
        return;

    std::int64_t end = to;
    for (const auto& c : clips)
        end = std::max (end, c.end());
    for (const auto& m : markers)
        end = std::max (end, m.tick + 1);

    deleteTime (clips, markers, to, end);
    deleteTime (clips, markers, 0, from);
}

void deleteTime (std::vector<Clip>& clips, std::vector<Marker>& markers, std::int64_t from, std::int64_t to)
{
    const auto length = to - from;

    if (length <= 0)
        return;

    std::vector<Clip> out;
    out.reserve (clips.size() + 4);

    for (auto c : clips)
    {
        const auto end = c.end();

        if (end <= from)
            out.push_back (c);
        else if (c.start >= to)
        {
            c.start -= length;
            out.push_back (c);
        }
        else if (c.start >= from && end <= to)
            continue; // entirely inside: deleted
        else
        {
            if (c.start < from) // head stays
            {
                auto head = c;
                head.length = from - c.start;
                clampFades (head);
                out.push_back (head);
            }

            if (end > to) // tail moves to `from`
            {
                auto tail = c;
                tail.offset = c.offset + (to - c.start);
                tail.length = end - to;
                tail.start = from;
                clampFades (tail);
                out.push_back (tail);
            }
        }
    }

    clips = std::move (out);
    markers.erase (std::remove_if (markers.begin(), markers.end(), [&] (const Marker& m) { return m.tick >= from && m.tick < to; }),
                   markers.end());

    for (auto& m : markers)
        if (m.tick >= to)
            m.tick -= length;
}

std::vector<int> clipsInTime (const std::vector<Clip>& clips, std::int64_t from, std::int64_t to)
{
    std::vector<int> out;

    for (int i = 0; i < (int) clips.size(); ++i)
        if (clips[(std::size_t) i].start < to && clips[(std::size_t) i].end() > from)
            out.push_back (i);

    return out;
}

std::vector<int> repeatTime (std::vector<Clip>& clips, std::int64_t from, std::int64_t to, std::vector<int> sources)
{
    const auto length = to - from;

    if (length <= 0)
        return {};

    if (sources.empty())
        for (int i = 0; i < (int) clips.size(); ++i)
            if (clips[(std::size_t) i].start >= from && clips[(std::size_t) i].start < to)
                sources.push_back (i);

    std::vector<int> added;

    for (auto i : sources)
        if (i >= 0 && i < (int) clips.size())
        {
            auto c = clips[(std::size_t) i];
            c.start += length;
            added.push_back ((int) clips.size());
            clips.push_back (c);
        }

    return added;
}

void rippleDelete (std::vector<Clip>& clips, std::vector<int> indices)
{
    std::sort (indices.begin(), indices.end());
    indices.erase (std::unique (indices.begin(), indices.end()), indices.end());
    indices.erase (std::remove_if (indices.begin(), indices.end(), [&clips] (int i) { return i < 0 || i >= (int) clips.size(); }), indices.end());

    // gaps per track, latest first, so each shift sees the positions the later ones left
    std::vector<Clip> removed;
    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
    {
        removed.push_back (clips[(std::size_t) *it]);
        clips.erase (clips.begin() + *it);
    }

    std::sort (removed.begin(), removed.end(), [] (const Clip& a, const Clip& b) { return a.start > b.start; });

    for (const auto& gap : removed)
        for (auto& c : clips)
            if (c.track == gap.track && c.start >= gap.end())
                c.start -= gap.length;
}

void rippleResize (std::vector<Clip>& clips, int index, std::int64_t newLength)
{
    if (index < 0 || index >= (int) clips.size())
        return;

    auto& clip = clips[(std::size_t) index];
    newLength = std::max<std::int64_t> (1, newLength);
    const auto oldEnd = clip.end();
    const auto delta = newLength - clip.length;
    clip.length = newLength;
    clampFades (clip);

    for (int i = 0; i < (int) clips.size(); ++i)
        if (i != index && clips[(std::size_t) i].track == clips[(std::size_t) index].track && clips[(std::size_t) i].start >= oldEnd)
            clips[(std::size_t) i].start = std::max<std::int64_t> (0, clips[(std::size_t) i].start + delta);
}

std::vector<int> moveTimeRange (std::vector<Clip>& clips, std::int64_t from, std::int64_t to, std::int64_t delta, bool copy)
{
    if (to <= from)
        return {};

    delta = std::max (delta, -from); // nothing before 0

    // cut at the edges so only the inside moves
    for (const auto edge : { from, to })
        for (std::size_t i = 0, n = clips.size(); i < n; ++i)
            if (clips[i].start < edge && clips[i].end() > edge)
            {
                auto rest = splitClip (clips[i], edge);
                clampFades (clips[i]);
                clampFades (rest);
                clips.push_back (rest);
            }

    std::vector<int> moved;

    for (int i = 0, n = (int) clips.size(); i < n; ++i)
    {
        const auto& c = clips[(std::size_t) i];

        if (c.start < from || c.end() > to)
            continue;

        if (copy)
        {
            auto duplicate = c;
            duplicate.start += delta;
            moved.push_back ((int) clips.size());
            clips.push_back (duplicate);
        }
        else
        {
            clips[(std::size_t) i].start += delta;
            moved.push_back (i);
        }
    }

    return moved;
}

std::vector<int> chopClip (std::vector<Clip>& clips, int index, std::int64_t grid)
{
    if (index < 0 || index >= (int) clips.size() || grid <= 0)
        return {};

    std::vector<int> pieces { index };
    auto current = index;

    for (auto cut = (clips[(std::size_t) index].start / grid + 1) * grid; cut < clips[(std::size_t) current].end(); cut += grid)
    {
        auto rest = splitClip (clips[(std::size_t) current], cut);
        clampFades (clips[(std::size_t) current]);
        clampFades (rest);
        clips.push_back (rest);
        current = (int) clips.size() - 1;
        pieces.push_back (current);
    }

    return pieces;
}

namespace
{
std::vector<PlaylistTrack>& headers (Song& song, int atLeast)
{
    if ((int) song.playlistTracks.size() < atLeast)
        song.playlistTracks.resize ((std::size_t) atLeast);
    return song.playlistTracks;
}
} // namespace

void insertTrack (Song& song, int track)
{
    track = std::max (0, track);
    song.forEachClipList ([track] (std::vector<Clip>& clips) {
        for (auto& c : clips)
            if (c.track >= track)
                ++c.track;
    });

    auto& h = headers (song, track);
    h.insert (h.begin() + track, PlaylistTrack {});
}

void removeTrack (Song& song, int track)
{
    if (track < 0)
        return;

    song.forEachClipList ([track] (std::vector<Clip>& clips) {
        clips.erase (std::remove_if (clips.begin(), clips.end(), [track] (const Clip& c) { return c.track == track; }), clips.end());
        for (auto& c : clips)
            if (c.track > track)
                --c.track;
    });

    if (track < (int) song.playlistTracks.size())
        song.playlistTracks.erase (song.playlistTracks.begin() + track);
}

void swapTracks (Song& song, int a, int b)
{
    if (a < 0 || b < 0 || a == b)
        return;

    song.forEachClipList ([a, b] (std::vector<Clip>& clips) {
        for (auto& c : clips)
            c.track = c.track == a ? b : c.track == b ? a : c.track;
    });

    auto& h = headers (song, std::max (a, b) + 1);
    std::swap (h[(std::size_t) a], h[(std::size_t) b]);
}

namespace
{
// FL's channel solo with its memory (round 190bx; FLEngine 2026 FUN_00ffaaa0, docs/analise/REA_rodada190bx.md §3): a
// solo remembers which rows were on (a solo moved to another row keeps the memory of before the first one), and the
// un-solo switches on either every row (General › "Restore previous state after solo" off, FL's default) or only the
// rows that were on before (`restore`). FL reads the option at the un-solo. `muted (row)` reads and
// `setMuted (row, on)` writes a row's mute switch, `touched (row)` says whether the solo may change it. Without a
// memory for this row (none passed, or the solo came with the project) the un-solo unmutes.
template <typename Muted, typename SetMuted, typename Touched>
void soloRows (int rows, int row, flsettings::SoloMemory* memory, bool restore, Muted muted, SetMuted setMuted, Touched touched)
{
    auto alone = ! muted (row);
    for (int i = 0; i < rows && alone; ++i)
        if (i != row && touched (i) && ! muted (i))
            alone = false;

    if (! alone)
    {
        if (memory != nullptr)
        {
            // a solo already on (exactly the remembered row heard) moves: the memory of before the first solo stays
            auto moved = memory->valid (memory->row, (std::size_t) rows) && memory->row != row && ! muted (memory->row);
            for (int i = 0; i < rows && moved; ++i)
                if (i != memory->row && touched (i) && ! muted (i))
                    moved = false;
            if (! moved)
            {
                memory->muted.resize ((std::size_t) rows);
                for (int i = 0; i < rows; ++i)
                    memory->muted[(std::size_t) i] = muted (i);
            }
            memory->row = row;
        }
        for (int i = 0; i < rows; ++i)
            if (i == row || touched (i))
                setMuted (i, i != row);
        return;
    }

    const auto back = restore && memory != nullptr && memory->valid (row, (std::size_t) rows);
    for (int i = 0; i < rows; ++i)
        if (i == row || touched (i))
            setMuted (i, back ? (bool) memory->muted[(std::size_t) i] : false);
    if (memory != nullptr)
        memory->clear();
}
} // namespace

void soloTrack (Song& song, int track, int trackCount, flsettings::SoloMemory* memory, bool restore)
{
    trackCount = std::max (trackCount, track + 1);
    auto& h = headers (song, trackCount);
    soloRows ((int) h.size(), track, memory, restore,
              [&h] (int t) { return h[(std::size_t) t].muted; },
              [&h] (int t, bool on) { h[(std::size_t) t].muted = on; },
              [] (int) { return true; });
}
void soloChannel (Song& song, int channel, flsettings::SoloMemory* memory, bool restore)
{
    auto& c = song.channels;
    if (channel < 0 || channel >= (int) c.size())
        return;
    soloRows ((int) c.size(), channel, memory, restore,
              [&c] (int i) { return c[(std::size_t) i].muted; },
              [&c] (int i, bool on) { c[(std::size_t) i].muted = on; },
              [&c] (int i) { return ! c[(std::size_t) i].soloLock; });
}

// --- track groups ------------------------------------------------------------------------------------

namespace
{
bool groupedFlag (const Song& song, int track)
{
    return track > 0 && track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].grouped;
}
} // namespace

bool isGroupHeader (const Song& song, int track)
{
    return track >= 0 && ! groupedFlag (song, track) && groupedFlag (song, track + 1);
}

int groupHeaderOf (const Song& song, int track)
{
    if (track < 0)
        return -1;

    if (! groupedFlag (song, track))
        return isGroupHeader (song, track) ? track : -1;

    while (track > 0 && groupedFlag (song, track))
        --track;

    return track;
}

std::vector<int> groupMembers (const Song& song, int header)
{
    std::vector<int> out;

    if (header < 0 || groupedFlag (song, header))
        return out;

    for (int t = header + 1; groupedFlag (song, t); ++t)
        out.push_back (t);

    return out;
}

bool isFoldedAway (const Song& song, int track)
{
    if (! groupedFlag (song, track))
        return false;

    const auto header = groupHeaderOf (song, track);
    return header >= 0 && header < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) header].collapsed;
}

void groupWithAbove (Song& song, int track)
{
    if (track <= 0)
        return;

    headers (song, track + 1)[(std::size_t) track].grouped = true;
}

void ungroupTrack (Song& song, int track)
{
    if (groupedFlag (song, track))
        song.playlistTracks[(std::size_t) track].grouped = false;
}

void setGroupCollapsed (Song& song, int track, bool collapsed)
{
    const auto header = groupHeaderOf (song, track);

    if (header < 0)
        return;

    headers (song, header + 1)[(std::size_t) header].collapsed = collapsed;
}

void colourGroup (Song& song, int track)
{
    const auto header = groupHeaderOf (song, track);

    if (header < 0)
        return;

    const auto colour = header < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) header].colour : 0u;

    for (auto t : groupMembers (song, header))
        song.playlistTracks[(std::size_t) t].colour = colour;
}

// --- round 168g: FL's editor Select / Edit items ------------------------------------------------------------------------
std::vector<int> selectRandomMore (const Pattern& pattern, const std::vector<int>& channelNotes, std::vector<int> selection, bool more,
                                   std::uint32_t seed)
{
    std::set<int> chosen (selection.begin(), selection.end());
    std::vector<int> valid;
    for (auto i : channelNotes)
        if (i >= 0 && i < (int) pattern.notes.size())
            valid.push_back (i);
    std::sort (valid.begin(), valid.end());
    valid.erase (std::unique (valid.begin(), valid.end()), valid.end());

    // FUN_00eb2510: the count is the channel's note count / 2 for "more" (one round otherwise); each round picks a
    // random note among the not yet selected ones (none left: the round does nothing)
    const auto rounds = more ? (int) valid.size() / 2 : 1;
    std::uint64_t state = seed == 0 ? 0x9E3779B97F4A7C15ull : seed;
    for (int r = 0; r < rounds; ++r)
    {
        std::vector<int> free;
        for (auto i : valid)
            if (chosen.count (i) == 0)
                free.push_back (i);
        if (free.empty())
            break;
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        chosen.insert (free[(std::size_t) ((state >> 33) % free.size())]);
    }
    return { chosen.begin(), chosen.end() };
}

std::vector<int> selectOddSteps (const Pattern& pattern, const std::vector<int>& channelNotes, std::vector<int> selection, std::int64_t step)
{
    std::set<int> chosen (selection.begin(), selection.end());
    step = std::max<std::int64_t> (1, step);
    for (auto i : channelNotes)
        if (i >= 0 && i < (int) pattern.notes.size())
        {
            // FUN_00eb2b70: ((start + step / 2) / step) is odd (the start rounded to the nearest step)
            const auto s = pattern.notes[(std::size_t) i].start + step / 2;
            const auto q = s >= 0 ? s / step : -((-s + step - 1) / step);
            if ((q & 1) != 0)
                chosen.insert (i);
        }
    return { chosen.begin(), chosen.end() };
}

namespace
{
// cuts the given notes at `tick` (strictly inside); the tails are appended and added to `indices`
void sliceAt (Pattern& pattern, std::vector<int>& indices, std::int64_t tick)
{
    const auto count = indices.size();
    for (std::size_t k = 0; k < count; ++k)
        if (const auto tail = split (pattern, indices[k], tick); tail >= 0)
            indices.push_back (tail);
}
} // namespace

std::vector<int> insertSpace (Pattern& pattern, const std::vector<int>& channelNotes, std::int64_t from, std::int64_t to, bool slice)
{
    std::vector<int> moved;
    if (to <= from)
        return moved;
    auto indices = channelNotes;
    indices.erase (std::remove_if (indices.begin(), indices.end(), [&pattern] (int i) { return i < 0 || i >= (int) pattern.notes.size(); }),
                   indices.end());
    if (slice)
        sliceAt (pattern, indices, from);
    for (auto i : indices)
        if (auto& n = pattern.notes[(std::size_t) i]; n.start >= from)
        {
            n.start += to - from;
            moved.push_back (i);
        }
    for (auto& m : pattern.markers) // (FUN_00e3b2b0: the pattern's markers move with the notes)
        if (m.tick >= from)
            m.tick += to - from;
    std::sort (moved.begin(), moved.end());
    return moved;
}

std::vector<int> deleteSpace (Pattern& pattern, const std::vector<int>& channelNotes, std::int64_t from, std::int64_t to)
{
    std::vector<int> moved;
    if (to <= from)
        return moved;
    auto indices = channelNotes;
    indices.erase (std::remove_if (indices.begin(), indices.end(), [&pattern] (int i) { return i < 0 || i >= (int) pattern.notes.size(); }),
                   indices.end());
    sliceAt (pattern, indices, from);
    sliceAt (pattern, indices, to);

    std::vector<int> removed;
    for (auto i : indices)
    {
        auto& n = pattern.notes[(std::size_t) i];
        if (n.start >= from && n.start < to)
            removed.push_back (i);
        else if (n.start >= to)
        {
            n.start -= to - from;
            moved.push_back (i);
        }
    }

    // the moved indices after the removal
    std::sort (removed.begin(), removed.end());
    std::vector<int> shifted;
    for (auto i : moved)
        shifted.push_back (i - (int) (std::lower_bound (removed.begin(), removed.end(), i) - removed.begin()));
    remove (pattern, removed);
    // the pattern's markers in the space go, the later ones move left (FUN_00e3b2b0)
    auto& markers = pattern.markers;
    markers.erase (std::remove_if (markers.begin(), markers.end(), [&] (const Marker& m) { return m.tick >= from && m.tick < to; }), markers.end());
    for (auto& m : markers)
        if (m.tick >= to)
            m.tick -= to - from;
    std::sort (shifted.begin(), shifted.end());
    return shifted;
}

void trimToSelection (Pattern& pattern, int channel, std::int64_t from, std::int64_t to, std::int64_t end)
{
    auto notesOf = [&pattern, channel] {
        std::vector<int> out;
        for (int i = 0; i < (int) pattern.notes.size(); ++i)
            if (channel < 0 || pattern.notes[(std::size_t) i].channel == channel)
                out.push_back (i);
        return out;
    };
    // (FUN_00eb35f0: delete space [to, end], then [0, from])
    auto last = end;
    for (auto i : notesOf())
        last = std::max (last, pattern.notes[(std::size_t) i].start + 1);
    for (const auto& m : pattern.markers)
        last = std::max (last, m.tick + 1);
    deleteSpace (pattern, notesOf(), to, last);
    deleteSpace (pattern, notesOf(), 0, from);
}

// --- the Piano roll's pattern time markers (round 168g) ----------------------------------------------------------------
namespace
{
void sortMarkers (std::vector<Marker>& markers)
{
    std::stable_sort (markers.begin(), markers.end(), [] (const Marker& x, const Marker& y) { return x.tick < y.tick; });
}
} // namespace

int addPatternMarker (Pattern& pattern, const Marker& marker)
{
    auto m = marker;
    m.tick = std::max<std::int64_t> (0, m.tick);
    pattern.markers.push_back (m);
    sortMarkers (pattern.markers);
    for (int i = (int) pattern.markers.size() - 1; i >= 0; --i)
        if (pattern.markers[(std::size_t) i] == m)
            return i;
    return -1;
}

int addPatternAutoMarker (Pattern& pattern, std::int64_t tick)
{
    tick = std::max<std::int64_t> (0, tick);
    for (int i = 0; i < (int) pattern.markers.size(); ++i)
        if (pattern.markers[(std::size_t) i].tick == tick)
            return i;
    std::string name;
    for (int n = 1;; ++n)
    {
        name = n == 1 ? std::string ("Auto") : "Auto #" + std::to_string (n);
        if (std::none_of (pattern.markers.begin(), pattern.markers.end(), [&name] (const Marker& m) { return m.name == name; }))
            break;
    }
    Marker m;
    m.tick = tick;
    m.name = name;
    return addPatternMarker (pattern, m);
}

std::pair<int, int> patternSignatureAt (const Pattern& pattern, std::int64_t tick, int beatsPerBar)
{
    std::pair<int, int> out { std::max (1, beatsPerBar), 4 };
    for (const auto& m : pattern.markers)
        if (m.isSignature() && m.tick <= tick)
            out = { m.numerator, m.denominator };
    return out;
}

void addPatternSignatureChange (Pattern& pattern, std::int64_t from, std::int64_t to, int numerator, int denominator, int beatsPerBar)
{
    numerator = std::clamp (numerator, 1, 32);
    denominator = std::clamp (denominator, 1, 32);
    from = std::max<std::int64_t> (0, from / ppq * ppq); // (floored to a beat, like the Playlist's)
    const auto before = patternSignatureAt (pattern, from, beatsPerBar);
    auto add = [&pattern] (std::int64_t tick, int n, int d) {
        pattern.markers.erase (std::remove_if (pattern.markers.begin(), pattern.markers.end(),
                                               [tick] (const Marker& m) { return m.isSignature() && m.tick == tick; }),
                               pattern.markers.end());
        Marker m;
        m.tick = tick;
        m.numerator = n;
        m.denominator = d;
        m.name = std::to_string (n) + "/" + std::to_string (d);
        addPatternMarker (pattern, m);
    };
    const auto hadEnd = to > from
                        && std::any_of (pattern.markers.begin(), pattern.markers.end(), [&] (const Marker& m) { return m.isSignature() && m.tick == to / ppq * ppq; });
    add (from, numerator, denominator);
    if (to > from && ! hadEnd)
        add (to / ppq * ppq, before.first, before.second);
}
} // namespace lynx::noteedit

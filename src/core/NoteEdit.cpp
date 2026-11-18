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

std::vector<int> duplicateBars (Pattern& pattern, const std::vector<int>& indices, Meter meter)
{
    return duplicate (pattern, indices, meter.bar());
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
    if (n.curves.has()) // (issue #108: each part keeps its own stretch of the MPE curves)
    {
        tail.curves = notecurves::slice (n.curves, (double) (tick - n.start), (double) n.length);
        n.curves = notecurves::slice (n.curves, 0.0, (double) (tick - n.start));
    }
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

// Issue #108 (MPE stage 2): a joined note keeps both notes' curves, as Live 12 keeps MPE envelopes when it joins notes
// (its What's New lesson, "Join"): from the second note's start on, its own curve (or its own values where it has
// none); before it, the first note's.
static void joinCurves (Note& k, const Note& n)
{
    if (! k.curves.has() && ! n.curves.has())
        return;
    const auto off = (double) (n.start - k.start);
    const auto base = [] (const Note& m, NoteCurveLane l) {
        return l == NoteCurveLane::pitch ? 0.0f : l == NoteCurveLane::timbre ? m.modX : std::clamp ((m.modY - 0.5f) * 2.0f, 0.0f, 1.0f);
    };
    NoteCurves joined = *k.curves;
    for (int i = 0; i < noteCurveLaneCount; ++i)
    {
        const auto l = (NoteCurveLane) i;
        const auto& from = n.curves->lane (l);
        auto& to = joined.lane (l);
        if (from.empty() && to.empty())
            continue;
        if (to.empty())
            to.push_back ({ 0.0f, base (k, l) });
        const auto held = joined.valueAt (l, off, base (k, l));
        to.erase (std::remove_if (to.begin(), to.end(), [off] (const NoteCurvePoint& q) { return (double) q.tick >= off; }), to.end());
        to.push_back ({ (float) std::max (0.0, off - 1.0), held });
        if (from.empty())
            to.push_back ({ (float) off, base (n, l) });
        for (const auto& q : from)
            to.push_back ({ (float) (off + (double) q.tick), q.value });
    }
    k.curves = std::move (joined);
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
                joinCurves (k, n);
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

std::string formatTime (std::int64_t ticks, Meter meter, bool isLength)
{
    const auto bar = meter.bar();
    ticks = std::max<std::int64_t> (0, ticks);
    const auto base = isLength ? 0 : 1;
    char text[64];
    std::snprintf (text, sizeof (text), "%lld:%lld:%lld", (long long) (ticks / bar + base), (long long) (ticks % bar / ticksPerStep + base),
                   (long long) (ticks % ticksPerStep));
    return text;
}

bool parseTime (const std::string& text, Meter meter, std::int64_t& ticks, bool isLength)
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
    const auto stepsPerBar = (long long) (meter.bar() / ticksPerStep);

    if (count == 0 || parts[0] < base || (count >= 2 && (parts[1] < base || parts[1] - base >= stepsPerBar))
        || (count == 3 && parts[2] >= ticksPerStep))
        return false;

    const auto bar = meter.bar();
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
              [&h] (int t) { return ! h[(std::size_t) t].soloLock; }); // (round 190ld: FL's locked state)
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
// Round 190jf (B17): nest-aware. A track's folder depth is `grouped ? 1 + nest : 0`; it belongs to the nearest track above
// it with a smaller depth. With every `nest` 0 (FL's groups, every project before 190jf) each function below does what
// it did before: a header is an ungrouped track followed by a grouped one, its members are the grouped run under it.

namespace
{
bool groupedFlag (const Song& song, int track)
{
    return track > 0 && track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].grouped;
}
} // namespace

int folderDepth (const Song& song, int track)
{
    if (! groupedFlag (song, track))
        return 0;
    return 1 + std::max (0, song.playlistTracks[(std::size_t) track].nest);
}

int folderParent (const Song& song, int track)
{
    const auto d = folderDepth (song, track);
    if (d == 0)
        return -1;
    for (int t = track - 1; t >= 0; --t)
        if (folderDepth (song, t) < d)
            return t;
    return -1;
}

int folderEnd (const Song& song, int header)
{
    if (header < 0)
        return header;
    const auto d = folderDepth (song, header);
    auto last = header;
    while (last + 1 < (int) song.playlistTracks.size() && folderDepth (song, last + 1) > d)
        ++last;
    return last;
}

std::vector<int> folderChildren (const Song& song, int header)
{
    std::vector<int> out;
    const auto end = folderEnd (song, header);
    for (int t = header + 1; t <= end; ++t)
        if (folderParent (song, t) == header)
            out.push_back (t);
    return out;
}

bool isGroupHeader (const Song& song, int track)
{
    return track >= 0 && folderDepth (song, track + 1) > folderDepth (song, track);
}

int groupHeaderOf (const Song& song, int track)
{
    if (track < 0)
        return -1;
    if (isGroupHeader (song, track))
        return track;
    return folderParent (song, track);
}

std::vector<int> groupMembers (const Song& song, int header)
{
    std::vector<int> out;
    if (header < 0 || ! isGroupHeader (song, header))
        return out;
    const auto end = folderEnd (song, header);
    for (int t = header + 1; t <= end; ++t)
        out.push_back (t);
    return out;
}

bool isFoldedAway (const Song& song, int track)
{
    for (auto p = folderParent (song, track); p >= 0; p = folderParent (song, p))
        if (p < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) p].collapsed)
            return true;
    return false;
}

void groupWithAbove (Song& song, int track)
{
    if (track <= 0)
        return;

    auto& t = headers (song, track + 1)[(std::size_t) track];
    t.grouped = true;
    t.nest = 0;
    normalizeFolders (song);
}

void ungroupTrack (Song& song, int track)
{
    if (groupedFlag (song, track))
    {
        song.playlistTracks[(std::size_t) track].grouped = false;
        song.playlistTracks[(std::size_t) track].nest = 0;
        normalizeFolders (song);
    }
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

// --- round 190jf (B17): folders ------------------------------------------------------------------------------------

namespace
{
void setDepth (Song& song, int track, int depth)
{
    if (track <= 0 && depth > 0)
        depth = 0; // (the first track is never grouped)
    auto& t = headers (song, track + 1)[(std::size_t) track];
    t.grouped = depth > 0;
    t.nest = std::max (0, depth - 1);
}
} // namespace

bool normalizeFolders (Song& song)
{
    auto changed = false;
    int previous = -1; // the depth of the track above (−1: none yet, so the first track is 0)
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
    {
        auto& tr = song.playlistTracks[(std::size_t) t];
        if (! tr.grouped && tr.nest != 0)
        {
            tr.nest = 0;
            changed = true;
        }
        const auto d = folderDepth (song, t);
        const auto allowed = previous + 1;
        if (d > allowed)
        {
            setDepth (song, t, allowed);
            changed = true;
        }
        previous = folderDepth (song, t);
    }
    return changed;
}

std::vector<int> groupMuteSet (const Song& song, int track)
{
    const auto header = groupHeaderOf (song, track);
    if (header < 0)
        return {};
    std::vector<int> out { header };
    const auto end = folderEnd (song, header);
    for (int t = header + 1; t <= end; ++t)
        out.push_back (t);
    return out;
}

bool muteGroup (Song& song, int track, const std::vector<int>& insertOf)
{
    const auto set = groupMuteSet (song, track);
    if (set.empty())
        return false;
    // (rollup 0.1.30) a linked header's insert: its mute lights the same LED
    const auto insert = [&] (int t) {
        const auto i = t >= 0 && t < (int) insertOf.size() ? insertOf[(std::size_t) t] : -1;
        return i > 0 && i < (int) song.mixer.size() ? i : -1;
    };
    const auto lit = (track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].muted)
                     || (insert (track) > 0 && song.mixer[(std::size_t) insert (track)].muted);
    const auto muted = ! lit;
    auto& h = headers (song, set.back() + 1);
    for (const auto t : set)
    {
        h[(std::size_t) t].muted = muted;
        if (! muted && insert (t) > 0)
            song.mixer[(std::size_t) insert (t)].muted = false;
    }
    return true;
}

bool soloGroup (Song& song, int track, int trackCount, flsettings::SoloMemory* memory, bool restore)
{
    const auto set = groupMuteSet (song, track);
    if (set.empty())
        return false;
    trackCount = std::max (trackCount, set.back() + 1);
    auto& h = headers (song, trackCount);
    const auto rows = (int) h.size();
    auto inSet = [&set] (int t) { return std::find (set.begin(), set.end(), t) != set.end(); };
    // "alone": every track of the group plays and every other track is muted (then the group solo switches off)
    auto alone = true;
    for (int t = 0; t < rows && alone; ++t)
        alone = h[(std::size_t) t].muted != inSet (t);
    const auto header = set.front();
    if (! alone)
    {
        if (memory != nullptr && ! memory->valid (header, (std::size_t) rows))
        {
            memory->muted.resize ((std::size_t) rows);
            for (int t = 0; t < rows; ++t)
                memory->muted[(std::size_t) t] = h[(std::size_t) t].muted;
            memory->row = header;
        }
        for (int t = 0; t < rows; ++t)
            h[(std::size_t) t].muted = ! inSet (t);
        return true;
    }
    const auto back = restore && memory != nullptr && memory->valid (header, (std::size_t) rows);
    for (int t = 0; t < rows; ++t)
        h[(std::size_t) t].muted = back ? (bool) memory->muted[(std::size_t) t] : false;
    if (memory != nullptr)
        memory->clear();
    return true;
}

namespace
{
// Puts the tracks in the order `order` (order[i] = the old index of the new track i; a permutation of 0…n−1); the clips
// follow their tracks.
void reorderTracks (Song& song, const std::vector<int>& order)
{
    const auto n = (int) order.size();
    std::vector<int> newIndex ((std::size_t) n, -1);
    for (int i = 0; i < n; ++i)
        newIndex[(std::size_t) order[(std::size_t) i]] = i;
    song.forEachClipList ([&newIndex, n] (std::vector<Clip>& clips) {
        for (auto& c : clips)
            if (c.track >= 0 && c.track < n)
                c.track = newIndex[(std::size_t) c.track];
    });
    auto& h = headers (song, n);
    std::vector<PlaylistTrack> moved;
    moved.reserve ((std::size_t) n);
    for (const auto o : order)
        moved.push_back (h[(std::size_t) o]);
    std::copy (moved.begin(), moved.end(), h.begin());
}
} // namespace

int maxFolderDepthAt (const Song& song, int before, int track)
{
    // the rows above the drop place after the folder of `track` is taken out
    for (int t = before - 1; t >= 0; --t)
    {
        if (t >= track && t <= folderEnd (song, track))
            continue;
        return folderDepth (song, t) + 1;
    }
    return 0;
}

int moveFolder (Song& song, int track, int before, int depth)
{
    const auto count = (int) song.playlistTracks.size();
    if (track < 0 || track >= count)
        return -1;
    const auto end = folderEnd (song, track);
    before = std::clamp (before, 0, count);
    const auto n = count;
    if (before > track && before <= end + 1)
        before = track; // (dropped inside itself: the place stays, only the depth changes)
    depth = std::clamp (depth, 0, maxFolderDepthAt (song, before, track));

    std::vector<int> block;
    for (int t = track; t <= end; ++t)
        block.push_back (t);
    std::vector<int> order;
    order.reserve ((std::size_t) n);
    for (int t = 0; t < n; ++t)
    {
        if (t == before)
            order.insert (order.end(), block.begin(), block.end());
        if (t < track || t > end)
            order.push_back (t);
    }
    if (before >= n)
        order.insert (order.end(), block.begin(), block.end());

    const auto shift = depth - folderDepth (song, track);
    std::vector<int> depths;
    for (const auto t : block)
        depths.push_back (folderDepth (song, t) + shift);
    reorderTracks (song, order);
    const auto at = (int) (std::find (order.begin(), order.end(), track) - order.begin());
    for (std::size_t i = 0; i < depths.size(); ++i)
        setDepth (song, at + (int) i, std::max (0, depths[i]));
    normalizeFolders (song);
    return at;
}

bool nestTrack (Song& song, int track)
{
    if (track <= 0 || track >= (int) song.playlistTracks.size())
        return false;
    const auto d = folderDepth (song, track);
    if (d + 1 > folderDepth (song, track - 1) + 1)
        return false; // (already one level under the track above)
    const auto end = folderEnd (song, track);
    for (int t = track; t <= end; ++t)
        setDepth (song, t, folderDepth (song, t) + 1);
    return true;
}

bool unnestTrack (Song& song, int track)
{
    const auto parent = folderParent (song, track);
    if (parent < 0)
        return false;
    // out of its folder: the track (and its own folder) goes below the parent's folder, one level up
    const auto parentEnd = folderEnd (song, parent);
    moveFolder (song, track, parentEnd + 1, folderDepth (song, parent));
    return true;
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

std::pair<int, int> patternSignatureAt (const Pattern& pattern, std::int64_t tick, Meter meter)
{
    std::pair<int, int> out { std::max (1, meter.beats), meterUnit (meter.unit) };
    for (const auto& m : pattern.markers)
        if (m.isSignature() && m.tick <= tick)
            out = { m.numerator, m.denominator };
    return out;
}

void addPatternSignatureChange (Pattern& pattern, std::int64_t from, std::int64_t to, int numerator, int denominator, Meter meter)
{
    numerator = std::clamp (numerator, 1, 32);
    denominator = std::clamp (denominator, 1, 32);
    from = std::max<std::int64_t> (0, from / ppq * ppq); // (floored to a beat, like the Playlist's)
    const auto before = patternSignatureAt (pattern, from, meter);
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

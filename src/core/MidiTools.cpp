#include "core/MidiTools.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <tuple>

#include "core/NoteTools.h"

namespace lynx::miditools
{
namespace
{
// splitmix32: small, seeded, the same on every platform
struct Rng
{
    std::uint32_t state;
    explicit Rng (std::uint32_t seed) : state (seed * 0x9E3779B9u + 0x7F4A7C15u) {}
    std::uint32_t next() noexcept
    {
        auto z = (state += 0x9E3779B9u);
        z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
        z = (z ^ (z >> 13)) * 0xC2B2AE35u;
        return z ^ (z >> 16);
    }
    double unit() noexcept { return (double) (next() >> 8) / 16777216.0; } // 0..1
    int below (int n) noexcept { return n <= 1 ? 0 : (int) (next() % (std::uint32_t) n); }
};

void sortByTime (Notes& notes)
{
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) {
        return std::tie (a.start, a.key) < std::tie (b.start, b.key);
    });
}

float scaledVelocity (float velocity, float factor) noexcept
{
    return std::clamp (velocity * factor, 1.0f / 127.0f, 1.0f);
}

std::int64_t gridShare (double share, std::int64_t grid) noexcept
{
    return (std::int64_t) std::llround (share * (double) std::max<std::int64_t> (1, grid));
}
} // namespace

std::vector<Notes> chords (Notes notes)
{
    std::map<std::tuple<std::int64_t, int>, Notes> groups; // (start, channel)
    for (auto& n : notes)
        groups[{ n.start, n.channel }].push_back (n);
    std::vector<Notes> out;
    for (auto& [key, group] : groups)
    {
        std::stable_sort (group.begin(), group.end(), [] (const Note& a, const Note& b) { return a.key < b.key; });
        out.push_back (std::move (group));
    }
    return out;
}

// --- Span --------------------------------------------------------------------------------------------------------
Notes span (Notes notes, const SpanOptions& o, std::int64_t grid)
{
    sortByTime (notes);
    std::vector<std::int64_t> starts;
    for (const auto& n : notes)
        if (starts.empty() || starts.back() != n.start)
            starts.push_back (n.start);

    std::int64_t smallest = 0;
    for (std::size_t i = 1; i < starts.size(); ++i)
        smallest = smallest == 0 ? starts[i] - starts[i - 1] : std::min (smallest, starts[i] - starts[i - 1]);

    Rng rng (o.seed);
    for (auto& n : notes)
    {
        if (o.mode == SpanOptions::legato)
        {
            if (const auto next = std::upper_bound (starts.begin(), starts.end(), n.start); next != starts.end())
                n.length = *next - n.start;
        }
        else if (o.mode == SpanOptions::staccato && smallest > 0)
            n.length = smallest / 2;

        n.length += gridShare (std::clamp (o.offset, -1.0f, 1.0f), grid);
        if (o.variation != 0.0f)
            n.length = (std::int64_t) std::llround ((double) n.length * (1.0 - std::clamp ((double) o.variation, -1.0, 1.0) * rng.unit()));
        n.length = std::max<std::int64_t> (1, n.length);
    }
    return notes;
}

// --- Strum -------------------------------------------------------------------------------------------------------
double strumCurve (double position, double tension) noexcept
{
    const auto u = std::clamp (position, 0.0, 1.0);
    const auto t = std::clamp (tension, -1.0, 1.0);
    if (t == 0.0 || u <= 0.0 || u >= 1.0)
        return u;
    if (t < 0.0)
        return 1.0 - strumCurve (1.0 - u, -t); // [LIVE] -50 % is +50 % turned round both ways
    // control points: (1/4, 1/4) and (3/4, 3/4) pulled towards (0, 1) by t
    const auto k = 1.0 - t;
    const auto x1 = 0.25 * k, y1 = 1.0 - 0.75 * k, x2 = 0.75 * k, y2 = 1.0 - 0.25 * k;
    auto lo = 0.0, hi = 1.0;
    for (int i = 0; i < 60; ++i) // x(s) rises with s: bisection
    {
        const auto s = 0.5 * (lo + hi);
        const auto x = 3.0 * s * (1.0 - s) * (1.0 - s) * x1 + 3.0 * s * s * (1.0 - s) * x2 + s * s * s;
        (x < u ? lo : hi) = s;
    }
    const auto s = 0.5 * (lo + hi);
    return 3.0 * s * (1.0 - s) * (1.0 - s) * y1 + 3.0 * s * s * (1.0 - s) * y2 + s * s * s;
}

Notes strum (Notes notes, const StrumOptions& o, std::int64_t grid)
{
    const auto low = std::clamp ((double) o.low, -1.0, 1.0), high = std::clamp ((double) o.high, -1.0, 1.0);
    Notes out;
    for (auto& chord : chords (std::move (notes)))
    {
        const auto count = (int) chord.size();
        for (int i = 0; i < count; ++i)
        {
            auto n = chord[(std::size_t) i];
            if (count > 1)
            {
                // position 0 = the chord's highest note … 1 = its lowest
                const auto position = (double) (count - 1 - i) / (double) (count - 1);
                const auto share = high + (low - high) * strumCurve (position, o.tension);
                n.start = std::max<std::int64_t> (0, n.start + gridShare (share, grid));
            }
            out.push_back (n);
        }
    }
    sortByTime (out);
    return out;
}

// --- Ornament ----------------------------------------------------------------------------------------------------
Notes ornament (Notes notes, const OrnamentOptions& o, std::int64_t grid)
{
    Notes out;
    for (auto n : notes)
    {
        const auto original = n;
        std::int64_t taken = 0; // what positive ornaments take from the note's start

        const auto add = [&] (float position, float velocity, int count, int pitch, float chance) {
            const std::int64_t total = std::llabs (gridShare (std::clamp (position, -1.0f, 1.0f), grid));
            count = std::clamp (count, 1, 64);
            if (total < count)
                return;
            const auto before = position < 0.0f;
            if (before && original.start - total < 0)
                return; // Live keeps these at negative times
            if (! before && total >= original.length)
                return;
            const auto from = before ? original.start - total : original.start;
            for (int k = 0; k < count; ++k)
            {
                auto g = original;
                g.start = from + total * k / count;
                g.length = std::max<std::int64_t> (1, from + total * (k + 1) / count - g.start);
                if (pitch != OrnamentOptions::same && k % 2 == 0) // [LIVE] High, 3 notes: above, same, above
                    g.key = o.scale > 0 ? notetools::stepOnScale (original.key, pitch, o.scaleRoot, o.scale)
                                        : std::clamp (original.key + pitch, 0, 127);
                g.velocity = scaledVelocity (original.velocity, velocity);
                g.release = std::clamp (original.release * velocity, 0.0f, 1.0f); // [LIVE] OffVelocity 64 → 32 at 50 %
                g.chance = std::clamp (original.chance * chance, 0.0f, 1.0f);
                g.slide = g.porta = false;
                out.push_back (g);
            }
            if (! before)
                taken = std::max (taken, total);
        };

        if (o.flam)
            add (o.flamPosition, o.flamVelocity, 1, OrnamentOptions::same, 1.0f);
        if (o.grace)
            add (o.gracePosition, o.graceVelocity, o.graceAmount, o.gracePitch, o.graceChance);

        n.start += taken;
        n.length -= taken;
        out.push_back (n);
    }
    sortByTime (out);
    return out;
}

// --- Recombine ---------------------------------------------------------------------------------------------------
Notes recombine (Notes notes, const RecombineOptions& o, std::int64_t grid)
{
    sortByTime (notes);
    if (notes.empty())
        return notes;

    if (o.dimension == RecombineOptions::position && o.rotateOnGrid && ! o.shuffle && ! o.mirror)
    {
        grid = std::max<std::int64_t> (1, grid);
        auto from = notes.front().start, to = notes.front().start;
        for (const auto& n : notes)
            to = std::max (to, n.start + n.length);
        from -= from % grid;
        to += (grid - to % grid) % grid;
        const auto length = std::max (grid, to - from);
        for (auto& n : notes)
            n.start = from + (((n.start - from + o.rotate * grid) % length) + length) % length;
        sortByTime (notes);
        return notes;
    }

    // the units whose values change places: chords for Position, single notes otherwise
    std::vector<std::vector<std::size_t>> units;
    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        if (o.dimension == RecombineOptions::position && ! units.empty() && notes[units.back().front()].start == notes[i].start)
            units.back().push_back (i);
        else
            units.push_back ({ i });
    }
    const auto count = (int) units.size();
    std::vector<int> order ((std::size_t) count);
    std::iota (order.begin(), order.end(), 0);
    if (o.shuffle)
    {
        Rng rng (o.seed);
        for (int i = count - 1; i > 0; --i)
            std::swap (order[(std::size_t) i], order[(std::size_t) rng.below (i + 1)]);
    }
    if (o.mirror)
        std::reverse (order.begin(), order.end());
    if (o.rotate != 0 && count > 0)
        std::rotate (order.begin(), order.begin() + (((o.rotate % count) + count) % count), order.end());

    const auto source = notes;
    for (int i = 0; i < count; ++i)
    {
        const auto& from = source[units[(std::size_t) order[(std::size_t) i]].front()];
        for (auto index : units[(std::size_t) i])
        {
            auto& n = notes[index];
            switch (o.dimension)
            {
                case RecombineOptions::pitch:    n.key = from.key; break;
                case RecombineOptions::duration: n.length = from.length; break;
                case RecombineOptions::velocity: n.velocity = from.velocity; break;
                default:                         n.start = from.start; break;
            }
        }
    }
    sortByTime (notes);
    return notes;
}

// --- Arpeggiate --------------------------------------------------------------------------------------------------
const char* arpeggiateStyleName (int style) noexcept
{
    static const char* const names[] = { "Up", "Down", "UpDown", "DownUp", "Up & Down", "Down & Up", "Converge", "Diverge",
                                         "Con & Diverge", "Pinky Up", "Pinky UpDown", "Thumb Up", "Thumb UpDown", "Play Order",
                                         "Chord Trigger", "Random", "Random Other", "Random Once" };
    return style >= 0 && style < ArpeggiateOptions::styleCount ? names[style] : "";
}

namespace
{
// One pass of the style over the held notes (`held` low key first), as indices into it.
std::vector<int> stylePass (int style, int count, const std::vector<int>& playOrder)
{
    using A = ArpeggiateOptions;
    std::vector<int> up ((std::size_t) count), out;
    std::iota (up.begin(), up.end(), 0);
    auto down = up;
    std::reverse (down.begin(), down.end());
    const auto inner = [] (const std::vector<int>& v) { return v.size() > 2 ? std::vector<int> (v.begin() + 1, v.end() - 1) : std::vector<int>(); };
    const auto join = [] (std::vector<int> a, const std::vector<int>& b) { a.insert (a.end(), b.begin(), b.end()); return a; };
    std::vector<int> converge;
    for (int lo = 0, hi = count - 1; lo <= hi; ++lo, --hi)
    {
        converge.push_back (lo);
        if (hi != lo)
            converge.push_back (hi);
    }
    auto diverge = converge;
    std::reverse (diverge.begin(), diverge.end());
    const auto alternate = [count] (bool pinky) { // every note but the pinky / thumb, each followed by it
        std::vector<int> v;
        for (int i = pinky ? 0 : 1; i < (pinky ? count - 1 : count); ++i)
        {
            if (! pinky)
                v.push_back (0);
            v.push_back (i);
            if (pinky)
                v.push_back (count - 1);
        }
        if (v.empty())
            v.push_back (0);
        return v;
    };
    const auto thereAndBack = [] (std::vector<int> v) { // pairs out, then the pairs back without the two ends
        const auto pairs = (int) v.size() / 2;
        for (int p = pairs - 2; p >= 1; --p)
        {
            v.push_back (v[(std::size_t) (2 * p)]);
            v.push_back (v[(std::size_t) (2 * p + 1)]);
        }
        return v;
    };

    switch (style)
    {
        case A::down:          return down;
        case A::upDown:        return join (up, inner (down));
        case A::downUp:        return join (down, inner (up));
        case A::upAndDown:     return join (up, down);
        case A::downAndUp:     return join (down, up);
        case A::converge:      return converge;
        case A::diverge:       return diverge;
        case A::conAndDiverge: return join (converge, inner (diverge));
        case A::pinkyUp:       return alternate (true);
        case A::pinkyUpDown:   return thereAndBack (alternate (true));
        case A::thumbUp:       return alternate (false);
        case A::thumbUpDown:   return thereAndBack (alternate (false));
        case A::playOrder:     return playOrder;
        default:               return up;
    }
}
} // namespace

Notes arpeggiate (Notes notes, const ArpeggiateOptions& o)
{
    sortByTime (notes);
    const auto rate = std::max<std::int64_t> (1, o.rate);
    const auto length = std::max<std::int64_t> (1, (std::int64_t) std::llround ((double) rate * std::clamp ((double) o.gate, 0.01, 2.0)));
    const auto passes = std::clamp (o.steps, 0, 8) + 1;
    Rng rng (o.seed);
    Notes out;
    std::vector<int> randomOrder, before;
    std::size_t next = 0; // first note not yet started
    std::int64_t time = 0;
    int step = 0;

    while (true)
    {
        std::vector<int> held; // indices into notes, low key first
        for (std::size_t i = 0; i < notes.size() && notes[i].start <= time; ++i)
            if (time < notes[i].start + notes[i].length)
                held.push_back ((int) i);
        while (next < notes.size() && notes[next].start <= time)
            ++next;

        if (held.empty())
        {
            if (next >= notes.size())
                break;
            time = notes[next].start; // nothing sounding: the clock restarts at the next note
            continue;
        }
        // [LIVE] the pattern starts again when none of the notes of the step before is still held (a note that follows
        // another with no gap starts a new run)
        if (std::none_of (held.begin(), held.end(), [&before] (int i) { return std::find (before.begin(), before.end(), i) != before.end(); }))
        {
            step = 0;
            randomOrder.clear();
        }
        before = held;

        auto order = held; // by start, then key: the order "played"
        std::stable_sort (held.begin(), held.end(), [&notes] (int a, int b) { return notes[(std::size_t) a].key < notes[(std::size_t) b].key; });
        const auto count = (int) held.size();
        std::vector<int> playOrder;
        for (auto index : order)
            playOrder.push_back ((int) (std::find (held.begin(), held.end(), index) - held.begin()));

        const auto emit = [&] (int heldIndex, int pass) {
            auto n = notes[(std::size_t) held[(std::size_t) heldIndex]];
            n.key += pass * o.distance;
            if (n.key < 0 || n.key > 127)
                return;
            n.start = time;
            n.length = length;
            n.slide = n.porta = false;
            out.push_back (n);
        };

        if (o.style == ArpeggiateOptions::chordTrigger)
        {
            for (int i = 0; i < count; ++i)
                emit (i, step % passes);
        }
        else if (o.style == ArpeggiateOptions::random)
            emit (rng.below (count), (step / std::max (1, count)) % passes);
        else if (o.style == ArpeggiateOptions::randomOther || o.style == ArpeggiateOptions::randomOnce)
        {
            const auto position = step % count;
            if ((int) randomOrder.size() != count || (position == 0 && step > 0 && o.style == ArpeggiateOptions::randomOther))
            {
                randomOrder.resize ((std::size_t) count);
                std::iota (randomOrder.begin(), randomOrder.end(), 0);
                for (int i = count - 1; i > 0; --i)
                    std::swap (randomOrder[(std::size_t) i], randomOrder[(std::size_t) rng.below (i + 1)]);
            }
            emit (randomOrder[(std::size_t) position], (step / count) % passes);
        }
        else
        {
            const auto pass = stylePass (o.style, count, playOrder);
            const auto size = (int) pass.size();
            emit (pass[(std::size_t) (step % size)], (step / size) % passes);
        }

        ++step;
        time += rate;
    }
    sortByTime (out);
    return out;
}

// --- Connect -----------------------------------------------------------------------------------------------------
Notes connect (Notes notes, const ConnectOptions& o, std::int64_t grid)
{
    const auto rate = std::max<std::int64_t> (1, o.rate > 0 ? o.rate : grid);
    Rng rng (o.seed);
    Notes out;
    const auto groups = chords (notes);
    std::int64_t sounding = 0; // where the notes so far end
    for (std::size_t c = 0; c < groups.size(); ++c)
    {
        for (const auto& n : groups[c])
        {
            out.push_back (n);
            sounding = std::max (sounding, n.start + n.length);
        }
        if (c + 1 >= groups.size())
            break;
        const auto& from = groups[c].front();
        const auto& to = groups[c + 1].front();
        if (sounding >= to.start)
            continue;

        const auto slots = (int) ((to.start - sounding + rate - 1) / rate);
        for (int k = 0; k < slots; ++k)
        {
            if (o.density < 1.0f && rng.unit() >= (double) std::max (0.0f, o.density))
                continue;
            const auto share = (double) (k + 1) / (double) (slots + 1);
            auto n = from;
            n.start = sounding + k * rate;
            n.length = std::min (rate, to.start - n.start);
            n.key = (int) std::floor ((double) from.key + (double) (to.key - from.key) * share + 0.5); // [LIVE] 70.5 → 71
            if (o.spread > 0)
                n.key += rng.below (2 * o.spread + 1) - o.spread;
            n.key = std::clamp (n.key, 0, 127);
            n.velocity = std::clamp (from.velocity + (to.velocity - from.velocity) * (float) share, 1.0f / 127.0f, 1.0f);
            n.slide = n.porta = false;
            const auto tied = o.tie > 0.0f && rng.unit() < (double) o.tie;
            if (tied)
                n.length = to.start - n.start;
            out.push_back (n);
            if (tied)
                break;
        }
    }
    sortByTime (out);
    return out;
}

// --- Chop --------------------------------------------------------------------------------------------------------
Notes chop (Notes notes, const ChopOptions& o)
{
    const auto parts = std::clamp (o.parts, 2, 64);
    Notes out;
    for (const auto& source : notes)
    {
        if (source.length < parts)
        {
            out.push_back (source);
            continue;
        }
        for (int i = 0; i < parts; ++i)
        {
            const auto gap = o.gaps > 0 ? i % (o.gaps + 1) == o.gaps : o.gaps < 0 && i % (1 - o.gaps) != 0;
            if (gap)
                continue;
            auto n = source;
            n.start = source.start + source.length * i / parts;
            n.length = source.start + source.length * (i + 1) / parts - n.start;
            if (i > 0)
                n.slide = n.porta = false;
            out.push_back (n);
        }
    }
    sortByTime (out);
    return out;
}

// --- on a pattern ------------------------------------------------------------------------------------------------
Notes notesAt (const Pattern& pattern, const std::vector<int>& indices)
{
    auto sorted = indices;
    std::sort (sorted.begin(), sorted.end());
    sorted.erase (std::unique (sorted.begin(), sorted.end()), sorted.end());
    Notes out;
    for (auto i : sorted)
        if (i >= 0 && i < (int) pattern.notes.size())
            out.push_back (pattern.notes[(std::size_t) i]);
    return out;
}

std::vector<int> replaceNotes (Pattern& pattern, const std::vector<int>& indices, const Notes& result)
{
    auto doomed = indices;
    std::sort (doomed.begin(), doomed.end());
    doomed.erase (std::unique (doomed.begin(), doomed.end()), doomed.end());
    for (auto it = doomed.rbegin(); it != doomed.rend(); ++it)
        if (*it >= 0 && *it < (int) pattern.notes.size())
            pattern.notes.erase (pattern.notes.begin() + *it);
    for (auto n : result)
    {
        n.start = std::max<std::int64_t> (0, n.start);
        n.length = std::max<std::int64_t> (1, n.length);
        pattern.notes.push_back (n);
    }
    pattern.sortNotes();

    // the new notes' places after the sort: each result note takes the first equal note not yet taken
    std::vector<int> selection;
    std::vector<bool> taken (pattern.notes.size(), false);
    for (auto n : result)
    {
        n.start = std::max<std::int64_t> (0, n.start);
        n.length = std::max<std::int64_t> (1, n.length);
        for (std::size_t i = 0; i < pattern.notes.size(); ++i)
            if (! taken[i] && pattern.notes[i] == n)
            {
                taken[i] = true;
                selection.push_back ((int) i);
                break;
            }
    }
    std::sort (selection.begin(), selection.end());
    return selection;
}
} // namespace lynx::miditools

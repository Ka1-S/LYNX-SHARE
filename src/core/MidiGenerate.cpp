#include "core/MidiGenerate.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <sstream>

#include "core/MidiTools.h"
#include "core/NoteNames.h"
#include "core/NoteTools.h"

#ifndef TR_NOOP
#define TR_NOOP(text) text // marks the names for translation (ui/I18n.h defines the same)
#endif

namespace lynx::midigen
{
namespace
{
// splitmix32 (as core/MidiTools.cpp): small, seeded, the same on every platform
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

constexpr double pi = 3.14159265358979323846;

float velocityOf (int v) noexcept { return (float) std::clamp (v, 1, 127) / 127.0f; }

Note makeNote (std::int64_t start, std::int64_t length, int key, float velocity, int channel)
{
    Note n;
    n.start = start;
    n.length = std::max<std::int64_t> (1, length);
    n.key = std::clamp (key, 0, 127);
    n.velocity = velocity;
    n.channel = channel;
    return n;
}

// a note from `start` lasting `length`, cut at the range's end; false when it starts at / after it
bool emit (Notes& out, const Context& c, std::int64_t start, std::int64_t length, int key, float velocity)
{
    if (start >= c.to || start < c.from)
        return false;
    out.push_back (makeNote (start, std::min (length, c.to - start), key, velocity, c.channel));
    return true;
}

bool validRange (const Context& c) noexcept { return c.to > c.from; }

bool inScale (const ScaleRef& s, int key) noexcept
{
    if (! s.on())
        return true;
    const auto pc = ((key - s.root) % 12 + 12) % 12;
    return std::find (s.intervals.begin(), s.intervals.end(), pc) != s.intervals.end();
}

// the in-scale key nearest to `key` within [lo, hi] (a tie goes down); `key` clamped when none is in the range
int nearestInScale (const ScaleRef& s, int key, int lo, int hi) noexcept
{
    key = std::clamp (key, lo, hi);
    if (! s.on())
        return key;
    for (int d = 0; d <= 127; ++d)
    {
        if (key - d >= lo && inScale (s, key - d))
            return key - d;
        if (key + d <= hi && inScale (s, key + d))
            return key + d;
    }
    return key;
}

std::int64_t binomial (int n, int k) noexcept
{
    if (k < 0 || n < 0 || k > n)
        return 0;
    k = std::min (k, n - k);
    std::int64_t r = 1;
    for (int i = 1; i <= k; ++i)
    {
        const auto num = (std::int64_t) (n - k + i);
        if (r > std::numeric_limits<std::int64_t>::max() / num)
            return std::numeric_limits<std::int64_t>::max();
        r = r * num / i; // (exact: r * num is C(n-k+i, i) * i)
    }
    return r;
}

const std::vector<int> majorSteps { 0, 2, 4, 5, 7, 9, 11 };
} // namespace

// --- note values ---------------------------------------------------------------------------------------------------
const char* noteValueName (int index) noexcept
{
    static const char* names[noteValueCount] = { "1/64T", "1/64", "1/32T", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2T", "1/2", "1/1" };
    return names[std::clamp (index, 0, noteValueCount - 1)];
}

std::int64_t noteValueTicks (int index) noexcept
{
    index = std::clamp (index, 0, noteValueCount - 1);
    if (index == noteValueCount - 1)
        return 4 * ppq;
    const auto straight = (std::int64_t) ppq * 4 / (64 >> (index / 2)); // 1/64, 1/32 ... 1/2
    return index % 2 == 0 ? straight * 2 / 3 : straight;                 // the triplet of a value is two thirds of it
}

const char* toolName (int tool) noexcept
{
    switch (tool)
    {
        case Settings::rhythm:    return TR_NOOP ("Rhythm");
        case Settings::seed:      return TR_NOOP ("Seed");
        case Settings::shape:     return TR_NOOP ("Shape");
        case Settings::stacks:    return TR_NOOP ("Stacks");
        case Settings::euclidean: return TR_NOOP ("Euclidean");
        default:                  return "";
    }
}

// --- Shape presets -------------------------------------------------------------------------------------------------
ShapeOptions::ShapeOptions() : levels (shapePreset (up)) {}

std::array<float, ShapeOptions::levelCount> shapePreset (int preset) noexcept
{
    std::array<float, ShapeOptions::levelCount> l {};
    const auto bounceDown = [] (double x) { return (1.0 - x) * std::abs (std::cos (2.5 * pi * x)); }; // a dropped ball
    for (int i = 0; i < ShapeOptions::levelCount; ++i)
    {
        const auto x = (double) i / (ShapeOptions::levelCount - 1);
        double y = 0.5;
        switch (preset)
        {
            case ShapeOptions::up:         y = x; break;
            case ShapeOptions::down:       y = 1.0 - x; break;
            case ShapeOptions::arcUp:      y = std::sin (pi * x); break;
            case ShapeOptions::arcDown:    y = 1.0 - std::sin (pi * x); break;
            case ShapeOptions::bounceUp:   y = bounceDown (1.0 - x); break;
            case ShapeOptions::bounceDown: y = bounceDown (x); break;
            default: break; // flat, custom
        }
        l[(std::size_t) i] = (float) std::clamp (std::round (y * 1000.0) / 1000.0, 0.0, 1.0);
    }
    return l;
}

const char* shapePresetName (int preset) noexcept
{
    switch (preset)
    {
        case ShapeOptions::custom:     return TR_NOOP ("Custom");
        case ShapeOptions::flat:       return TR_NOOP ("Flat");
        case ShapeOptions::up:         return TR_NOOP ("Up");
        case ShapeOptions::down:       return TR_NOOP ("Down");
        case ShapeOptions::arcUp:      return TR_NOOP ("Arc Up");
        case ShapeOptions::arcDown:    return TR_NOOP ("Arc Down");
        case ShapeOptions::bounceUp:   return TR_NOOP ("Bounce Up");
        case ShapeOptions::bounceDown: return TR_NOOP ("Bounce Down");
        default:                       return "";
    }
}

// --- Rhythm --------------------------------------------------------------------------------------------------------
std::int64_t rhythmCount (int steps, int density) noexcept
{
    steps = std::clamp (steps, 1, 32);
    return binomial (steps, std::clamp (density, 0, steps));
}

std::vector<int> rhythmHits (int steps, int density, std::int64_t pattern)
{
    steps = std::clamp (steps, 1, 32);
    density = std::clamp (density, 0, steps);
    std::vector<int> priority;
    for (int s = 0; s < steps; ++s)
        if (notetools::euclideanHit (steps, density, 0, s))
            priority.push_back (s);
    for (int s = 0; s < steps; ++s)
        if (! notetools::euclideanHit (steps, density, 0, s))
            priority.push_back (s);
    const auto count = std::max<std::int64_t> (1, rhythmCount (steps, density));
    auto rank = ((pattern % count) + count) % count;
    std::vector<int> hits;
    int k = density;
    for (int i = 0; i < steps && k > 0; ++i)
    {
        const auto with = binomial (steps - i - 1, k - 1); // subsets that take position i next
        if (rank < with)
        {
            hits.push_back (priority[(std::size_t) i]);
            --k;
        }
        else
            rank -= with;
    }
    std::sort (hits.begin(), hits.end());
    return hits;
}

Notes rhythm (const RhythmOptions& o, const Context& c)
{
    Notes out;
    if (! validRange (c))
        return out;
    const auto steps = std::clamp (o.steps, 1, 32);
    const auto stepTicks = noteValueTicks (o.stepDuration);
    std::vector<int> hits;
    for (auto s : rhythmHits (steps, o.density, o.pattern))
        hits.push_back (((s + o.shift) % steps + steps) % steps);
    std::sort (hits.begin(), hits.end());
    const auto split = std::clamp ((double) o.split, 0.0, 1.0);
    const auto period = std::max (0, o.period);
    std::int64_t h = 0;
    for (std::int64_t cycle = 0;; ++cycle)
    {
        const auto cycleStart = c.from + cycle * steps * stepTicks;
        if (cycleStart >= c.to || hits.empty())
            break;
        for (auto s : hits)
        {
            const auto t = cycleStart + s * stepTicks;
            if (t >= c.to)
                break;
            const auto accented = period > 0 && (((h - o.offset) % period) + period) % period == 0;
            const auto splitHere = std::floor ((double) (h + 1) * split) > std::floor ((double) h * split);
            const auto parts = std::clamp (o.repetitions, 1, 8) * (splitHere ? 2 : 1);
            for (int p = 0; p < parts; ++p)
            {
                const auto a = t + stepTicks * p / parts, b = t + stepTicks * (p + 1) / parts;
                emit (out, c, a, b - a, o.pitch, velocityOf (accented ? o.accent : o.velocity));
            }
            ++h;
        }
    }
    return out;
}

// --- Seed ----------------------------------------------------------------------------------------------------------
Notes seed (const SeedOptions& o, const Context& c)
{
    Notes out;
    if (! validRange (c))
        return out;
    Rng rng (c.seed);
    const auto lo = std::clamp (std::min (o.minPitch, o.maxPitch), 0, 127), hi = std::clamp (std::max (o.minPitch, o.maxPitch), 0, 127);
    std::vector<int> keys;
    for (int k = lo; k <= hi; ++k)
        if (inScale (c.scale, k))
            keys.push_back (k);
    if (keys.empty())
        return out;
    const auto dLo = std::min (o.minDuration, o.maxDuration), dHi = std::max (o.minDuration, o.maxDuration);
    const auto vLo = std::clamp (std::min (o.minVelocity, o.maxVelocity), 1, 127), vHi = std::clamp (std::max (o.minVelocity, o.maxVelocity), 1, 127);
    const auto voices = std::clamp (o.voices, 1, 8);
    const auto density = std::clamp ((double) o.density, 0.0, 1.0);
    const auto grid = std::max<std::int64_t> (1, c.grid);
    struct Sounding { std::int64_t end; int key; };
    std::vector<Sounding> sounding;
    for (auto t = c.from; t < c.to; t += grid)
    {
        sounding.erase (std::remove_if (sounding.begin(), sounding.end(), [t] (const Sounding& s) { return s.end <= t; }), sounding.end());
        // the k-th note at once (k = notes sounding) starts with chance density^(k + 1): sparse at low density, every
        // voice busy at 100 %
        while ((int) sounding.size() < voices)
        {
            if (rng.unit() >= std::pow (density, (double) sounding.size() + 1.0))
                break;
            std::vector<int> free;
            for (auto k : keys)
                if (std::none_of (sounding.begin(), sounding.end(), [k] (const Sounding& s) { return s.key == k; }))
                    free.push_back (k);
            if (free.empty())
                break;
            const auto key = free[(std::size_t) rng.below ((int) free.size())];
            const auto d = dLo + (dHi - dLo) * rng.unit();
            const auto length = std::max<std::int64_t> (1, std::llround (4.0 * ppq * std::pow (2.0, (double) d)));
            const auto velocity = vLo + rng.below (vHi - vLo + 1);
            emit (out, c, t, length, key, velocityOf (velocity));
            sounding.push_back ({ t + length, key });
        }
    }
    return out;
}

// --- Shape ---------------------------------------------------------------------------------------------------------
double shapeAt (const ShapeOptions& o, double x) noexcept
{
    const auto pos = std::clamp (x, 0.0, 1.0) * (ShapeOptions::levelCount - 1);
    const auto i = std::min ((int) pos, ShapeOptions::levelCount - 2);
    const auto f = pos - i;
    return std::clamp ((double) o.levels[(std::size_t) i] * (1.0 - f) + (double) o.levels[(std::size_t) i + 1] * f, 0.0, 1.0);
}

Notes shape (const ShapeOptions& o, const Context& c)
{
    Notes out;
    if (! validRange (c))
        return out;
    Rng rng (c.seed);
    const auto lo = std::clamp (std::min (o.minPitch, o.maxPitch), 0, 127), hi = std::clamp (std::max (o.minPitch, o.maxPitch), 0, 127);
    const auto step = o.rate <= 0 ? std::max<std::int64_t> (1, c.grid) : noteValueTicks (o.rate - 1);
    const auto span = (double) (c.to - c.from);
    bool lastHere = false; // the previous step made (or continued) a note
    for (auto t = c.from; t < c.to; t += step)
    {
        // three draws every step, so one control's change leaves the others' choices where they were
        const auto tieDraw = rng.unit(), densityDraw = rng.unit(), jitterDraw = rng.unit();
        if (lastHere && ! out.empty() && tieDraw < (double) o.tie)
        {
            auto& n = out.back();
            n.length = std::min (t + step, c.to) - n.start;
            continue;
        }
        lastHere = densityDraw < (double) std::clamp (o.density, 0.0f, 1.0f);
        if (! lastHere)
            continue;
        const auto level = shapeAt (o, (double) (t - c.from) / span);
        const auto jitter = (2.0 * jitterDraw - 1.0) * (double) std::clamp (o.jitter, 0.0f, 1.0f) * (hi - lo) * 0.5;
        const auto key = nearestInScale (c.scale, (int) std::lround (lo + level * (hi - lo) + jitter), lo, hi);
        emit (out, c, t, step, key, velocityOf (o.velocity));
    }
    return out;
}

// --- Stacks --------------------------------------------------------------------------------------------------------
const std::vector<std::vector<int>>* ChordBank::rulesFor (int scaleSize) const noexcept
{
    const RuleSet* best = nullptr;
    for (const auto& s : sets)
        if (! s.rules.empty() && s.scaleSize <= scaleSize && (best == nullptr || s.scaleSize > best->scaleSize))
            best = &s;
    if (best == nullptr)
        for (const auto& s : sets)
            if (! s.rules.empty())
                return &s.rules;
    return best != nullptr ? &best->rules : nullptr;
}

const ChordBank& lynxChordBank()
{
    // LYNX's own rules (round 190fm), scale degrees above the chord's root; a scale of n notes repeats at degree n.
    static const ChordBank bank { {
        { 5, { // pentatonic (major pentatonic C D E G A: degrees 0 1 2 3 4)
                 { 0, 2, 3 },     // the triad inside the scale (C E G)
                 { 0, 3 },        // a fifth
                 { 0, 1, 3 },     // sus2 (C D G)
                 { 0, 2, 3, 4 },  // sixth (C E G A)
                 { 0, 3, 5 },     // power chord with the octave
                 { 0, 2, 4 },     // C E A
                 { 0, 1, 2, 3 },  // cluster
                 { 0, 2, 3, 5 },  // triad and octave
                 { 0, 3, 7 },     // open: fifth and tenth
                 { 0, 1, 3, 4 },  // C D G A
                 { 0, 2, 5 },     // third and octave
                 { 0, 1, 4 },     // C D A
                 { 0, 3, 6 },     // stacked fifths
                 { 0, 2, 3, 6 },  // triad and ninth
                 { -5, 0, 2, 3 }, // the root below, then the triad
                 { 0, 3, 6, 9 } } }, // four stacked fifths
        { 6, { // hexatonic (whole tone, blues, augmented...)
                 { 0, 2, 4 },     // every other note
                 { 0, 2, 4, 5 },
                 { 0, 1, 3 },
                 { 0, 3, 6 },     // root, middle, octave
                 { 0, 3 },
                 { 0, 2, 4, 6 },  // and the octave
                 { 0, 1, 4 },
                 { 0, 2, 3 },
                 { 0, 3, 5 },
                 { 0, 2, 5 },
                 { 0, 3, 8 },     // open
                 { 0, 4, 8 },
                 { 0, 2, 4, 7 },
                 { 0, 1, 3, 5 },
                 { -6, 0, 2, 4 }, // the root below
                 { 0, 3, 6, 9 } } },
        { 7, { // heptatonic (major, minor, the modes): stacked thirds are every other degree
                 { 0, 2, 4 },        // triad
                 { 0, 2, 4, 6 },     // seventh
                 { 0, 1, 4 },        // sus2
                 { 0, 3, 4 },        // sus4
                 { 0, 4, 7 },        // power chord with the octave
                 { 0, 2, 4, 8 },     // add9
                 { 0, 2, 4, 5 },     // sixth
                 { 0, 2, 4, 6, 8 },  // ninth
                 { 0, 2, 6 },        // shell seventh (no fifth)
                 { 0, 4, 9 },        // open: fifth and tenth
                 { 0, 2, 4, 7 },     // triad and octave
                 { 0, 3, 6 },        // quartal, three notes
                 { 0, 3, 6, 9 },     // quartal, four notes
                 { 0, 2, 4, 6, 8, 10 }, // eleventh
                 { -7, 0, 2, 4 },    // the root below, then the triad
                 { 0, 4, 6, 9 } } }, // open seventh: fifth, seventh, tenth
    } };
    return bank;
}

std::optional<ChordBank> readChordBank (const std::string& json, std::string& error)
{
    // A small reader for the one layout: finds "chord_rulesets", then in each of its objects "scale_cardinality" and
    // "rules" (an array of integer arrays). Other members are skipped.
    ChordBank bank;
    const auto key = json.find ("\"chord_rulesets\"");
    if (key == std::string::npos)
    {
        error = "no chord_rulesets";
        return std::nullopt;
    }
    std::size_t i = json.find ('[', key);
    if (i == std::string::npos)
    {
        error = "chord_rulesets is not a list";
        return std::nullopt;
    }
    // the end of the rule set list: its matching ']'
    auto matching = [&json] (std::size_t open) {
        int depth = 0;
        bool inString = false;
        for (auto p = open; p < json.size(); ++p)
        {
            const auto ch = json[p];
            if (inString)
            {
                if (ch == '\\')
                    ++p;
                else if (ch == '"')
                    inString = false;
                continue;
            }
            if (ch == '"')
                inString = true;
            else if (ch == '[' || ch == '{')
                ++depth;
            else if (ch == ']' || ch == '}')
                if (--depth == 0)
                    return p;
        }
        return std::string::npos;
    };
    const auto listEnd = matching (i);
    if (listEnd == std::string::npos)
    {
        error = "chord_rulesets is not closed";
        return std::nullopt;
    }
    for (auto p = i + 1; p < listEnd;)
    {
        const auto open = json.find ('{', p);
        if (open == std::string::npos || open > listEnd)
            break;
        const auto close = matching (open);
        if (close == std::string::npos || close > listEnd)
            break;
        const auto object = json.substr (open, close - open + 1);
        p = close + 1;
        ChordBank::RuleSet set;
        if (const auto c = object.find ("\"scale_cardinality\""); c != std::string::npos)
        {
            const auto colon = object.find (':', c);
            set.scaleSize = colon == std::string::npos ? 0 : (int) std::strtol (object.c_str() + colon + 1, nullptr, 10);
        }
        const auto r = object.find ("\"rules\"");
        if (r == std::string::npos || set.scaleSize < 1 || set.scaleSize > 24)
            continue;
        const auto rulesOpen = object.find ('[', r);
        if (rulesOpen == std::string::npos)
            continue;
        // each inner [...] of integers is one rule
        int depth = 0;
        std::vector<int> rule;
        for (auto q = rulesOpen; q < object.size(); ++q)
        {
            const auto ch = object[q];
            if (ch == '[')
            {
                if (++depth == 2)
                    rule.clear();
            }
            else if (ch == ']')
            {
                if (depth == 2 && ! rule.empty() && set.rules.size() < 16 && rule.size() <= 12)
                    set.rules.push_back (rule);
                if (--depth == 0)
                    break;
            }
            else if (depth == 2 && (ch == '-' || std::isdigit ((unsigned char) ch)))
            {
                char* end = nullptr;
                const auto v = std::strtol (object.c_str() + q, &end, 10);
                if (end != nullptr && end > object.c_str() + q)
                {
                    rule.push_back ((int) std::clamp<long> (v, -48, 48));
                    q = (std::size_t) (end - object.c_str()) - 1;
                }
            }
        }
        if (! set.rules.empty())
            bank.sets.push_back (std::move (set));
    }
    if (bank.sets.empty())
    {
        error = "no rule set with rules";
        return std::nullopt;
    }
    return bank;
}

namespace
{
// the key of scale degree `degree` (any integer) above `base` (the key of degree 0) in `steps`
int degreeKey (int base, const std::vector<int>& steps, int degree) noexcept
{
    const auto n = (int) steps.size();
    const auto octave = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
    const auto d = degree - octave * n;
    return base + octave * 12 + steps[(std::size_t) d] - steps[0];
}
} // namespace

int stackRoot (const StackChord& chord, const ScaleRef& scale) noexcept
{
    if (! scale.on())
        return std::clamp (chord.rootPitch, 0, 127);
    std::vector<int> steps (scale.intervals.begin(), scale.intervals.end());
    std::sort (steps.begin(), steps.end());
    const auto tonic = 12 * (chord.octave + 2) + ((scale.root % 12) + 12) % 12 + steps[0];
    return std::clamp (degreeKey (tonic, steps, chord.rootDegree), 0, 127);
}

std::vector<int> stackKeys (const StackChord& chord, const ScaleRef& scale, const ChordBank& bank)
{
    std::vector<int> steps = scale.on() ? scale.intervals : majorSteps;
    std::sort (steps.begin(), steps.end());
    steps.erase (std::unique (steps.begin(), steps.end()), steps.end());
    const auto* rules = bank.rulesFor ((int) steps.size());
    static const std::vector<int> single { 0 };
    const auto& rule = rules != nullptr && ! rules->empty() ? (*rules)[(std::size_t) (((chord.rule % (int) rules->size()) + (int) rules->size()) % (int) rules->size())] : single;
    int base, degree;
    if (scale.on())
    {
        base = 12 * (chord.octave + 2) + ((scale.root % 12) + 12) % 12 + steps[0];
        degree = chord.rootDegree;
    }
    else
    {
        base = chord.rootPitch; // the major scale on the root
        degree = 0;
    }
    std::vector<int> keys;
    for (auto r : rule)
        keys.push_back (degreeKey (base, steps, degree + r));
    std::sort (keys.begin(), keys.end());
    keys.erase (std::unique (keys.begin(), keys.end()), keys.end());
    for (int i = 0; i < std::abs (chord.inversion) && keys.size() > 1; ++i)
    {
        if (chord.inversion > 0)
            keys.front() += 12;
        else
            keys.back() -= 12;
        std::sort (keys.begin(), keys.end());
    }
    for (auto& k : keys)
        k = std::clamp (k, 0, 127);
    keys.erase (std::unique (keys.begin(), keys.end()), keys.end());
    return keys;
}

std::string stackChordName (const StackChord& chord, const ScaleRef& scale, const ChordBank& bank)
{
    const auto keys = stackKeys (chord, scale, bank);
    if (keys.empty())
        return {};
    const auto root = stackRoot (chord, scale);
    std::set<int> pcs;
    for (auto k : keys)
        pcs.insert (((k - root) % 12 + 12) % 12);
    struct Quality { std::set<int> pcs; const char* suffix; };
    static const Quality qualities[] = {
        { { 0, 4, 7 }, "" },          { { 0, 3, 7 }, "m" },       { { 0, 3, 6 }, "dim" },      { { 0, 4, 8 }, "aug" },
        { { 0, 2, 7 }, "sus2" },      { { 0, 5, 7 }, "sus4" },    { { 0, 7 }, "5" },           { { 0, 4, 7, 10 }, "7" },
        { { 0, 4, 7, 11 }, "maj7" },  { { 0, 3, 7, 10 }, "m7" },  { { 0, 3, 6, 10 }, "m7b5" }, { { 0, 3, 6, 9 }, "dim7" },
        { { 0, 4, 7, 9 }, "6" },      { { 0, 3, 7, 9 }, "m6" },   { { 0, 2, 4, 7 }, "add9" },  { { 0, 2, 3, 7 }, "madd9" },
        { { 0, 2, 4, 7, 11 }, "maj9" }, { { 0, 2, 3, 7, 10 }, "m9" }, { { 0, 2, 4, 7, 10 }, "9" }, { { 0, 4, 11 }, "maj7" },
        { { 0, 3, 10 }, "m7" },       { { 0, 4, 10 }, "7" },
    };
    std::string name = notenames::pitchName (root);
    bool known = false;
    for (const auto& q : qualities)
        if (q.pcs == pcs)
        {
            name += q.suffix;
            known = true;
            break;
        }
    if (! known)
        name += " (" + std::to_string (keys.size()) + ")";
    if (((keys.front() - root) % 12 + 12) % 12 != 0)
        name += "/" + notenames::pitchName (keys.front());
    return name;
}

Notes stacks (const StacksOptions& o, const Context& c, const ChordBank& bank)
{
    Notes out;
    if (! validRange (c) || o.sequence.empty())
        return out;
    const auto bar = (double) ppq * std::max (1, c.beatsPerBar);
    auto t = c.from;
    for (int i = 0; t < c.to && i < 4096; ++i)
    {
        const auto& chord = o.sequence[(std::size_t) i % o.sequence.size()];
        const auto length = std::max<std::int64_t> (ticksPerStep, std::llround (std::clamp ((double) chord.duration, 0.0625, 64.0) * bar));
        const auto end = std::min (t + length, c.to);
        const auto start = std::clamp<std::int64_t> (t + std::llround ((double) chord.offset * ticksPerStep), c.from, end - 1);
        for (auto k : stackKeys (chord, c.scale, bank))
            emit (out, c, start, end - start, k, velocityOf (100));
        t += length;
    }
    return out;
}

// --- Euclidean -----------------------------------------------------------------------------------------------------
Notes euclid (const EuclidOptions& o, const Context& c)
{
    Notes out;
    if (! validRange (c))
        return out;
    const auto steps = std::clamp (o.steps, 1, 32);
    const auto stepTicks = noteValueTicks (o.division);
    std::int64_t i = 0;
    for (auto t = c.from; t < c.to; t += stepTicks, ++i)
        if (notetools::euclideanHit (steps, std::clamp (o.density, 0, steps), o.rotation, (int) (i % steps)))
            emit (out, c, t, stepTicks, o.pitch, velocityOf (o.velocity));
    return out;
}

// --- on a pattern --------------------------------------------------------------------------------------------------
Notes generate (const Settings& s, const Context& c, const ChordBank& bank)
{
    switch (s.tool)
    {
        case Settings::rhythm:    return rhythm (s.rhythmOptions, c);
        case Settings::seed:      return seed (s.seedOptions, c);
        case Settings::shape:     return shape (s.shapeOptions, c);
        case Settings::stacks:    return stacks (s.stacksOptions, c, bank);
        case Settings::euclidean: return euclid (s.euclidOptions, c);
        default:                  return {};
    }
}

std::vector<int> notesInRange (const Pattern& pattern, int channel, std::int64_t from, std::int64_t to)
{
    std::vector<int> indices;
    for (int i = 0; i < (int) pattern.notes.size(); ++i)
    {
        const auto& n = pattern.notes[(std::size_t) i];
        if (n.channel == channel && n.start >= from && n.start < to)
            indices.push_back (i);
    }
    return indices;
}

std::vector<int> writeGenerated (Pattern& pattern, const Context& c, const Notes& generated)
{
    auto notes = generated;
    for (auto& n : notes)
        n.channel = c.channel;
    return miditools::replaceNotes (pattern, notesInRange (pattern, c.channel, c.from, c.to), notes);
}

// --- saved settings ------------------------------------------------------------------------------------------------
namespace
{
std::string num (double v)
{
    char buffer[40];
    std::snprintf (buffer, sizeof (buffer), "%.9g", v);
    return buffer;
}
} // namespace

std::string encode (const Settings& s)
{
    std::ostringstream o;
    const auto line = [&o] (const char* key, const std::string& value) { o << key << '=' << value << '\n'; };
    const auto i = [] (int v) { return std::to_string (v); };
    line ("tool", i (s.tool));
    const auto& r = s.rhythmOptions;
    line ("rhythm.pitch", i (r.pitch));
    line ("rhythm.steps", i (r.steps));
    line ("rhythm.pattern", i (r.pattern));
    line ("rhythm.density", i (r.density));
    line ("rhythm.stepDuration", i (r.stepDuration));
    line ("rhythm.split", num (r.split));
    line ("rhythm.shift", i (r.shift));
    line ("rhythm.velocity", i (r.velocity));
    line ("rhythm.accent", i (r.accent));
    line ("rhythm.period", i (r.period));
    line ("rhythm.offset", i (r.offset));
    line ("rhythm.repetitions", i (r.repetitions));
    const auto& e = s.seedOptions;
    line ("seed.density", num (e.density));
    line ("seed.minPitch", i (e.minPitch));
    line ("seed.maxPitch", i (e.maxPitch));
    line ("seed.minDuration", num (e.minDuration));
    line ("seed.maxDuration", num (e.maxDuration));
    line ("seed.minVelocity", i (e.minVelocity));
    line ("seed.maxVelocity", i (e.maxVelocity));
    line ("seed.voices", i (e.voices));
    const auto& h = s.shapeOptions;
    std::string levels;
    for (auto l : h.levels)
        levels += (levels.empty() ? "" : ",") + num (l);
    line ("shape.levels", levels);
    line ("shape.preset", i (h.preset));
    line ("shape.rate", i (h.rate));
    line ("shape.tie", num (h.tie));
    line ("shape.density", num (h.density));
    line ("shape.minPitch", i (h.minPitch));
    line ("shape.maxPitch", i (h.maxPitch));
    line ("shape.jitter", num (h.jitter));
    line ("shape.velocity", i (h.velocity));
    for (const auto& c : s.stacksOptions.sequence)
        line ("stacks.chord", i (c.rootDegree) + "," + i (c.octave) + "," + i (c.rootPitch) + "," + i (c.inversion) + "," + i (c.rule) + ","
                                  + num (c.duration) + "," + num (c.offset));
    line ("stacks.selected", i (s.stacksOptions.selected));
    if (! s.stacksOptions.bankFile.empty())
        line ("stacks.bank", s.stacksOptions.bankFile);
    const auto& u = s.euclidOptions;
    line ("euclid.steps", i (u.steps));
    line ("euclid.density", i (u.density));
    line ("euclid.rotation", i (u.rotation));
    line ("euclid.division", i (u.division));
    line ("euclid.pitch", i (u.pitch));
    line ("euclid.velocity", i (u.velocity));
    return o.str();
}

Settings decode (const std::string& text)
{
    Settings s;
    std::vector<StackChord> chords;
    std::istringstream in (text);
    std::string row;
    while (std::getline (in, row))
    {
        if (! row.empty() && row.back() == '\r')
            row.pop_back();
        const auto eq = row.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto key = row.substr (0, eq), value = row.substr (eq + 1);
        const auto n = std::strtod (value.c_str(), nullptr);
        const auto clampTo = [n] (double lo, double hi) { return std::clamp (n, lo, hi); };
        const auto whole = [&clampTo] (int lo, int hi) { return (int) std::lround (clampTo (lo, hi)); };
        auto& r = s.rhythmOptions;
        auto& e = s.seedOptions;
        auto& h = s.shapeOptions;
        auto& u = s.euclidOptions;
        if (key == "tool") s.tool = whole (0, Settings::toolCount - 1);
        else if (key == "rhythm.pitch") r.pitch = whole (0, 127);
        else if (key == "rhythm.steps") r.steps = whole (1, 32);
        else if (key == "rhythm.pattern") r.pattern = whole (0, std::numeric_limits<int>::max());
        else if (key == "rhythm.density") r.density = whole (0, 32);
        else if (key == "rhythm.stepDuration") r.stepDuration = whole (0, noteValueCount - 1);
        else if (key == "rhythm.split") r.split = (float) clampTo (0.0, 1.0);
        else if (key == "rhythm.shift") r.shift = whole (-32, 32);
        else if (key == "rhythm.velocity") r.velocity = whole (1, 127);
        else if (key == "rhythm.accent") r.accent = whole (1, 127);
        else if (key == "rhythm.period") r.period = whole (0, 32);
        else if (key == "rhythm.offset") r.offset = whole (-32, 32);
        else if (key == "rhythm.repetitions") r.repetitions = whole (1, 8);
        else if (key == "seed.density") e.density = (float) clampTo (0.0, 1.0);
        else if (key == "seed.minPitch") e.minPitch = whole (0, 127);
        else if (key == "seed.maxPitch") e.maxPitch = whole (0, 127);
        else if (key == "seed.minDuration") e.minDuration = (float) clampTo (-8.0, 2.0);
        else if (key == "seed.maxDuration") e.maxDuration = (float) clampTo (-8.0, 2.0);
        else if (key == "seed.minVelocity") e.minVelocity = whole (1, 127);
        else if (key == "seed.maxVelocity") e.maxVelocity = whole (1, 127);
        else if (key == "seed.voices") e.voices = whole (1, 8);
        else if (key == "shape.levels")
        {
            std::size_t k = 0;
            const char* p = value.c_str();
            while (*p != 0 && k < h.levels.size())
            {
                char* end = nullptr;
                const auto v = std::strtod (p, &end);
                if (end == p)
                    break;
                h.levels[k++] = (float) std::clamp (v, 0.0, 1.0);
                p = *end == ',' ? end + 1 : end;
            }
        }
        else if (key == "shape.preset") h.preset = whole (0, ShapeOptions::presetCount - 1);
        else if (key == "shape.rate") h.rate = whole (0, noteValueCount);
        else if (key == "shape.tie") h.tie = (float) clampTo (0.0, 1.0);
        else if (key == "shape.density") h.density = (float) clampTo (0.0, 1.0);
        else if (key == "shape.minPitch") h.minPitch = whole (0, 127);
        else if (key == "shape.maxPitch") h.maxPitch = whole (0, 127);
        else if (key == "shape.jitter") h.jitter = (float) clampTo (0.0, 1.0);
        else if (key == "shape.velocity") h.velocity = whole (1, 127);
        else if (key == "stacks.chord" && chords.size() < 64)
        {
            double v[7] {};
            int k = 0;
            const char* p = value.c_str();
            while (*p != 0 && k < 7)
            {
                char* end = nullptr;
                const auto value = std::strtod (p, &end);
                if (end == p)
                    break;
                v[k++] = value;
                p = *end == ',' ? end + 1 : end;
            }
            if (k == 7)
            {
                StackChord c;
                c.rootDegree = (int) std::clamp (std::lround (v[0]), -48L, 48L);
                c.octave = (int) std::clamp (std::lround (v[1]), -2L, 8L);
                c.rootPitch = (int) std::clamp (std::lround (v[2]), 0L, 127L);
                c.inversion = (int) std::clamp (std::lround (v[3]), -6L, 6L);
                c.rule = (int) std::clamp (std::lround (v[4]), 0L, 15L);
                c.duration = (float) std::clamp (v[5], 0.0625, 64.0);
                c.offset = (float) std::clamp (v[6], -64.0, 64.0);
                chords.push_back (c);
            }
        }
        else if (key == "stacks.selected") s.stacksOptions.selected = whole (0, 63);
        else if (key == "stacks.bank") s.stacksOptions.bankFile = value;
        else if (key == "euclid.steps") u.steps = whole (1, 32);
        else if (key == "euclid.density") u.density = whole (0, 32);
        else if (key == "euclid.rotation") u.rotation = whole (-32, 32);
        else if (key == "euclid.division") u.division = whole (0, noteValueCount - 1);
        else if (key == "euclid.pitch") u.pitch = whole (0, 127);
        else if (key == "euclid.velocity") u.velocity = whole (1, 127);
    }
    if (! chords.empty())
        s.stacksOptions.sequence = std::move (chords);
    s.stacksOptions.selected = std::clamp (s.stacksOptions.selected, 0, (int) s.stacksOptions.sequence.size() - 1);
    return s;
}
} // namespace lynx::midigen

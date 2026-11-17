#pragma once

// FL Studio's Chord progression tool, its strip of chord blocks (round 190kd, coverage prtools-0055 / 0057 / 0117 and
// the chord-block viewer; REA in docs/analise/REA_rodada190kd.md: FL 2026 screen session, FL's manual
// "pianoroll_chordprogression", the DFM TPRChordToolForm and a static read of the tool's Python byte code).
//
// FL's tool keeps the progression as blocks: each block a chord over a stretch of time, with its own lock, main notes /
// bass note switches, inversion, extra notes, voicing, transposition, fixed bass and note colour. Above them a tab names
// the key ("A Major (Ionian)"). The Generate button runs one of FL's three Generate modes on every block that is not
// locked:
//  - Rework (FL's default, the arrows): new chords in the same key, taking the chords around them as context;
//  - Start over (the dice): a fresh progression in a new random key; the blocks' own changes are reset;
//  - Analyze (the magnifier; only when the Piano roll had notes when the tool opened): the chord that best matches the
//    notes found under each block.
// FL's "Options for generation" (the spanner) choose which parts get notes (Main, Bass) and what happens to the notes
// that were in the Piano roll before ("Non chord notes": Replace all, Replace clashing, Keep (FL's default), Snap).
// "Learn from MIDI input" replaces the selected chord with the chord played on a MIDI controller.
//
// What is FL's and what is LYNX's own: the blocks, their switches, the modes and their rules, the note options, the
// learn and snap rules are FL's (see the REA doc). FL's generator and analyser are trained models whose data stays in
// FL; LYNX generates with its own table of diatonic moves (prtools::generateProgression's) and analyses with its own
// chord templates scored against the notes' sounding time (REA §1 lists FL's analyser law as far as it was read).
// Header only, no JUCE.

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <cmath>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "core/PianoRollTools.h"
#include "core/Song.h"

namespace lynx::chordstrip
{
using prtools::Chord;

enum class GenMode { rework, startOver, analyze };            // FL's ModePageSelect ("Generate mode")
enum class NonChord { replaceAll, replaceClashing, keep, snap }; // FL's spanner menu › Non chord notes (Keep = FL's default)
enum class Voicing { block, open, octave, stacked };            // FL's chord menu › Voicing

inline int pc (int v) noexcept { return ((v % 12) + 12) % 12; }

struct Block
{
    Chord chord;                 // root, intervals above it, slash bass
    std::int64_t length = 4 * ppq; // ticks (the start is the sum of the blocks before it)
    bool locked = false;         // FL's padlock: Generate / Analyze leave it alone
    bool main = true, bass = true; // FL's chord menu › Main notes / Bass note
    int inversion = 0;           // FL's ⇕ number: how many times the lowest main note went up an octave
    int transpose = 0;           // semitones of the main notes (FL's Transpose › semitone / octave)
    int bassTranspose = 0;       // octaves of the bass (FL's Transpose › Bass: one octave up / down)
    int fixedBass = -1;          // FL's Fixed bass: a pitch class kept when the chord is regenerated; -1 = none
    Voicing voicing = Voicing::block;
    int colour = -1;             // FL's Color 1 … 8 (LYNX note colour 0 … 7); -1 = the channel's own

    bool operator== (const Block&) const = default;
};

struct Strip
{
    int tonic = 0;               // pitch class of the key
    bool minor = false;          // FL's "Major (Ionian)" / "Minor Natural (Aeolian)"
    std::vector<Block> blocks;

    std::int64_t startOf (std::size_t i) const noexcept
    {
        std::int64_t t = 0;
        for (std::size_t k = 0; k < i && k < blocks.size(); ++k)
            t += blocks[k].length;
        return t;
    }
    std::int64_t length() const noexcept { return startOf (blocks.size()); }
    bool operator== (const Strip&) const = default;
};

// "A Major (Ionian)" / "A Minor Natural (Aeolian)": the key tab's text (FL's names of the two reference scales)
inline std::string keyName (const Strip& s)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return std::string (names[pc (s.tonic)]) + (s.minor ? " Minor Natural (Aeolian)" : " Major (Ionian)");
}

inline const std::array<int, 7>& scaleSteps (bool minor) noexcept
{
    static constexpr std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 }, natural { 0, 2, 3, 5, 7, 8, 10 };
    return minor ? natural : major;
}

inline bool inKey (const Strip& s, int key) noexcept
{
    const auto& steps = scaleSteps (s.minor);
    return std::find (steps.begin(), steps.end(), pc (key - s.tonic)) != steps.end();
}

// --- names ---------------------------------------------------------------------------------------

// FL's "Names" switch: abs = letter names ("F♯m add9"), rel = Roman numerals in the key ("vi", "IV").
inline std::string blockName (const Strip& s, const Block& b, bool relative)
{
    auto chord = b.chord;
    if (b.fixedBass >= 0)
        chord.bass = b.fixedBass;
    if (relative)
        return prtools::toNumerals ({ chord }, s.tonic);
    return prtools::chordName (chord);
}

// --- the notes of a block ------------------------------------------------------------------------

// The main notes, low to high: the chord at `octave` (C4 = 60 for octave 4), inverted, voiced and transposed as the
// block says. FL's Invert cycles the main notes up (the lowest an octave up) or down (the highest an octave down).
inline std::vector<int> mainKeys (const Block& b, int octave)
{
    const auto baseC = 12 * (std::clamp (octave, 0, 8) + 1);
    std::vector<int> keys;
    for (auto iv : b.chord.intervals)
        keys.push_back (baseC + pc (b.chord.root) + iv);
    std::sort (keys.begin(), keys.end());
    if (keys.empty())
        return keys;

    for (int i = 0; i < b.inversion; ++i)
    {
        keys.front() += 12;
        std::sort (keys.begin(), keys.end());
    }
    for (int i = 0; i > b.inversion; --i)
    {
        keys.back() -= 12;
        std::sort (keys.begin(), keys.end());
    }

    switch (b.voicing)
    {
        case Voicing::block: break;
        case Voicing::open: // no two notes too close: every second note from the bottom goes up an octave
            for (std::size_t i = 1; i < keys.size(); i += 2)
                keys[i] += 12;
            break;
        case Voicing::octave: // the lowest chord note again an octave above
            keys.push_back (keys.front() + 12);
            break;
        case Voicing::stacked: // the chord notes over two octaves
        {
            const auto n = keys.size();
            for (std::size_t i = 0; i < n; ++i)
                keys.push_back (keys[i] + 12);
            break;
        }
    }
    for (auto& k : keys)
        k += b.transpose;
    std::sort (keys.begin(), keys.end());
    keys.erase (std::unique (keys.begin(), keys.end()), keys.end());
    return keys;
}

// The bass note: the fixed bass, else the slash bass, else the root, an octave under the chord's octave.
inline int bassKey (const Block& b, int octave)
{
    const auto baseC = 12 * (std::clamp (octave, 0, 8) + 1);
    const auto pitch = b.fixedBass >= 0 ? b.fixedBass : b.chord.bass >= 0 ? b.chord.bass : b.chord.root;
    return baseC - 12 + pc (pitch) + 12 * b.bassTranspose;
}

// The inversion that keeps the block's main notes closest to `previous` (LYNX's "Smooth voicing": the same search as
// prtools::writeProgression's voice leading; FL shows its own choice as the block's ⇕ number).
inline int smoothInversion (Block b, int octave, const std::vector<int>& previous)
{
    if (previous.empty() || b.chord.intervals.size() < 3)
        return 0;
    const auto centre = [] (const std::vector<int>& k) {
        double s = 0;
        for (auto v : k)
            s += v;
        return k.empty() ? 0.0 : s / (double) k.size();
    };
    const auto target = centre (previous);
    const auto n = (int) b.chord.intervals.size();
    auto best = 0;
    auto bestDistance = 1.0e9;
    for (int inv = -n; inv <= n; ++inv)
    {
        b.inversion = inv;
        const auto d = std::abs (centre (mainKeys (b, octave)) - target);
        if (d < bestDistance - 1.0e-9)
        {
            bestDistance = d;
            best = inv;
        }
    }
    return best;
}

inline void voiceLead (Strip& s, int octave, bool smooth, bool lockedToo = false)
{
    std::vector<int> previous;
    for (auto& b : s.blocks)
    {
        if (! b.locked || lockedToo)
            b.inversion = smooth ? smoothInversion (b, octave, previous) : 0;
        previous = mainKeys (b, octave);
    }
}

// --- the generator (LYNX's table, FL's modes) ----------------------------------------------------

namespace detail
{
// LYNX's own table of moves between scale degrees (prtools::generateProgression's): the usual functional pulls
inline float move (int from, int to) noexcept
{
    static constexpr float moves[7][7] = {
        { 0.2f, 0.6f, 0.4f, 1.0f, 1.0f, 0.9f, 0.1f }, { 0.3f, 0.1f, 0.2f, 0.4f, 1.0f, 0.3f, 0.3f },
        { 0.3f, 0.2f, 0.1f, 0.7f, 0.3f, 1.0f, 0.1f }, { 0.9f, 0.5f, 0.2f, 0.1f, 1.0f, 0.4f, 0.2f },
        { 1.0f, 0.2f, 0.3f, 0.5f, 0.1f, 0.8f, 0.1f }, { 0.4f, 0.7f, 0.4f, 1.0f, 0.7f, 0.1f, 0.1f },
        { 1.0f, 0.1f, 0.5f, 0.1f, 0.2f, 0.3f, 0.0f },
    };
    return from < 0 || to < 0 ? 0.5f : moves[from][to];
}

// the scale degree of a chord root in the key, or -1 when it is not a scale note
inline int degreeOf (const Strip& s, const Chord& c) noexcept
{
    const auto& steps = scaleSteps (s.minor);
    const auto it = std::find (steps.begin(), steps.end(), pc (c.root - s.tonic));
    return it == steps.end() ? -1 : (int) (it - steps.begin());
}

// the diatonic triad (plus a seventh / ninth as the dice say) on a degree
inline Chord triad (const Strip& s, int degree, bool seventh, bool ninth)
{
    const auto& steps = scaleSteps (s.minor);
    Chord c;
    c.root = pc (s.tonic + steps[(std::size_t) degree]);
    const auto third = pc (steps[(std::size_t) ((degree + 2) % 7)] - steps[(std::size_t) degree]);
    const auto fifth = pc (steps[(std::size_t) ((degree + 4) % 7)] - steps[(std::size_t) degree]);
    c.intervals = { 0, third, fifth };
    if (seventh)
    {
        const auto sev = pc (steps[(std::size_t) ((degree + 6) % 7)] - steps[(std::size_t) degree]);
        c.intervals.push_back (sev == 0 ? 11 : sev);
    }
    if (ninth)
        c.intervals.push_back (14);
    std::sort (c.intervals.begin(), c.intervals.end());
    return c;
}

// one degree for a block between two others (either may be -1 = none / not diatonic)
inline int pickDegree (int before, int after, bool last, float adventure, std::mt19937& random, const float* bias = nullptr, int exclude = -1)
{
    std::uniform_real_distribution<float> uniform (0.0f, 1.0f);
    float weights[7], total = 0.0f;
    for (int d = 0; d < 7; ++d)
    {
        if (d == exclude)
        {
            weights[d] = 0.0f;
            continue;
        }
        auto w = before >= 0 ? move (before, d) : (d == 0 ? 1.0f : d == 5 ? 0.7f : 0.4f);
        if (after >= 0)
            w *= 0.4f + move (d, after);
        if (last)
            w *= (d == 4 || d == 3) ? 2.0f : 1.0f;
        w = w * (1.0f - adventure) + adventure * 0.5f;
        if (d == before) // FL's "Chord repeats" at its minimum: two generated neighbours differ
            w *= 0.15f;
        if (bias != nullptr) // (after the Adventurous blend: the notes found weigh as much at any setting)
            w *= bias[d];
        weights[d] = w;
        total += w;
    }
    auto pick = uniform (random) * total;
    for (int d = 0; d < 7; ++d)
    {
        if (weights[d] <= 0.0f)
            continue;
        pick -= weights[d];
        if (pick <= 0.0f)
            return d;
    }
    for (int d = 6; d >= 0; --d)
        if (weights[d] > 0.0f)
            return d;
    return 0;
}

// FL's auto-accompany (Keep / Snap / Replace clashing: the notes found bias the chord choice): each degree's triad
// weighs by how much of the notes sounding under the block it covers
inline std::array<float, 7> accompanyBias (const Strip& s, const std::vector<Note>& notes, std::int64_t from, std::int64_t to)
{
    std::array<float, 7> bias;
    bias.fill (1.0f);
    std::array<double, 12> w {};
    double total = 0.0;
    for (const auto& n : notes)
    {
        const auto a = std::max (from, n.start), b = std::min (to, n.start + std::max<std::int64_t> (1, n.length));
        if (b > a && ! n.muted)
        {
            w[(std::size_t) pc (n.key)] += (double) (b - a);
            total += (double) (b - a);
        }
    }
    if (total <= 0.0)
        return bias;
    for (int d = 0; d < 7; ++d)
    {
        const auto c = triad (s, d, false, false);
        double covered = 0.0;
        for (auto iv : c.intervals)
            covered += w[(std::size_t) pc (c.root + iv)];
        // (FL: no weight for a chord the notes don't fit at its default depth; LYNX keeps a little for every degree)
        const auto f = (float) (covered / total);
        bias[(std::size_t) d] = 0.02f + 30.0f * f * f * f * f * f * f;
    }
    return bias;
}
} // namespace detail

// A new chord for block `i` in the key, taking its neighbours as context (FL's Regenerate: "taking into account any
// neighboring Chords"; FL's single-chord regenerate never gives the chord back, regenerate_bias 0). With `notes`, the
// notes under the block bias the choice (FL's auto-accompany). The fixed bass stays.
inline Chord regenerateChord (const Strip& s, std::size_t i, float adventure, std::uint32_t seed, bool lockedAfterOnly = false,
                              bool excludeCurrent = false, const std::vector<Note>* notes = nullptr)
{
    std::mt19937 random (seed * 2654435761u + (std::uint32_t) i * 40503u + 17u);
    std::uniform_real_distribution<float> uniform (0.0f, 1.0f);
    const auto before = i > 0 ? detail::degreeOf (s, s.blocks[i - 1].chord) : -1;
    const auto after = i + 1 < s.blocks.size() && (! lockedAfterOnly || s.blocks[i + 1].locked) ? detail::degreeOf (s, s.blocks[i + 1].chord) : -1;
    std::array<float, 7> bias;
    bias.fill (1.0f);
    if (notes != nullptr && ! notes->empty())
        bias = detail::accompanyBias (s, *notes, s.startOf (i), s.startOf (i + 1));
    const auto current = excludeCurrent ? detail::degreeOf (s, s.blocks[i].chord) : -1;
    const auto plainFirst = i == 0 && after < 0 && current != 0 && (notes == nullptr || notes->empty());
    const auto degree = plainFirst ? 0 : detail::pickDegree (before, after, i + 1 == s.blocks.size() && s.blocks.size() > 2, adventure, random, bias.data(), current);
    return detail::triad (s, degree, uniform (random) < adventure * 0.6f, uniform (random) < adventure * 0.25f);
}

// FL's chord menu › Show alternatives: up to `count` other chords that fit between the block's neighbours, the most
// likely first (no dice: the order is the table's).
inline std::vector<Chord> alternatives (const Strip& s, std::size_t i, int count = 6)
{
    struct Option { Chord chord; float weight; };
    std::vector<Option> options;
    const auto before = i > 0 ? detail::degreeOf (s, s.blocks[i - 1].chord) : -1;
    const auto after = i + 1 < s.blocks.size() ? detail::degreeOf (s, s.blocks[i + 1].chord) : -1;
    for (int d = 0; d < 7; ++d)
    {
        auto w = (before >= 0 ? detail::move (before, d) : 0.5f) * (after >= 0 ? 0.4f + detail::move (d, after) : 1.0f);
        options.push_back ({ detail::triad (s, d, false, false), w });
        options.push_back ({ detail::triad (s, d, true, false), w * 0.55f });
        auto sus = detail::triad (s, d, false, false);
        if (sus.intervals.size() >= 2 && (sus.intervals[1] == 3 || sus.intervals[1] == 4))
        {
            sus.intervals[1] = 5;
            options.push_back ({ sus, w * 0.35f });
        }
    }
    std::stable_sort (options.begin(), options.end(), [] (const Option& a, const Option& b) { return a.weight > b.weight; });
    std::vector<Chord> out;
    for (const auto& o : options)
    {
        if ((int) out.size() >= count)
            break;
        auto same = o.chord.root == s.blocks[i].chord.root && o.chord.intervals == s.blocks[i].chord.intervals;
        for (const auto& c : out)
            same = same || (c.root == o.chord.root && c.intervals == o.chord.intervals);
        if (! same)
            out.push_back (o.chord);
    }
    return out;
}

// --- FL's chord analyser (REA §1, §2: the tool's ChordAnalyser) -----------------------------------------

namespace analysis
{
// FL's 7 chord types in vocabulary order (maj, min, dim, sus4, sus2, aug, hdim): the intervals in FL's "analysis
// order" (-1 = an empty slot whose weight is skipped), FL's priority, and the LYNX intervals of the type's triad.
struct Type
{
    std::array<int, 10> order;
    double priority;
    std::vector<int> triad;
};
inline const std::array<Type, 7>& types()
{
    static const std::array<Type, 7> list { {
        { { 0, 4, 7, 10, 2, 11, 9, 6, 1, 5 }, 1.00, { 0, 4, 7 } },        // maj
        { { 0, 3, 7, 10, 2, 5, 8, 6, 9, -2 }, 0.99, { 0, 3, 7 } },        // min (9 slots)
        { { 0, 6, 3, 9, 2, 5, 8, -2, -2, -2 }, 0.70, { 0, 3, 6 } },       // dim
        { { 0, 5, 7, -1, 10, 2, 9, -2, -2, -2 }, 0.60, { 0, 5, 7 } },     // sus4
        { { 0, 2, 7, -1, 10, 5, 9, -2, -2, -2 }, 0.59, { 0, 2, 7 } },     // sus2
        { { 0, 4, 8, -1, 10, 2, 5, -2, -2, -2 }, 0.50, { 0, 4, 8 } },     // aug
        { { 0, 6, 3, 10, 1, 5, 8, -2, -2, -2 }, 0.48, { 0, 3, 6, 10 } },  // hdim
    } };
    return list;
}
// FL's DEFAULT_WEIGHTS (the analysis-order slots), at weight_log_power 0
inline constexpr std::array<double, 10> slotWeights { 1.0, 0.75, 0.75, 0.39, 0.30, 0.25, 0.16, 0.13, 0.11, 0.10 };

struct Settings
{
    double bassWeightNote = 1.11, rootMatch = 1.14, slashWeight = 0.06, cumulative = 0.9, typePriorityDepth = 0.2;
    double durationDepth = 0.3;
    int bassSeparation = 7, namingSeparation = 12, maxBass = 59;
};
// learn_chord / one block (FL's analyse_chord)
inline Settings single() { return {}; }
// Analyze over the blocks (FL's analyse_chord_sequence: bass note and root match 1.14, cumulative 0.9 x 0.3, no slash
// weight, separations 12 / 12). FL sets no type priority there because its HMM vocabulary (trained style priorities,
// which stay in FL) already prefers common chords; LYNX, without that data, keeps the single analysis's type priority
// (0.2: major / minor before diminished / half-diminished) as its stand-in [LYNX].
inline Settings sequence()
{
    Settings s;
    s.bassWeightNote = 1.14;
    s.slashWeight = 0.0;
    s.cumulative = 0.3;
    s.typePriorityDepth = 0.2;
    s.bassSeparation = 12;
    return s;
}

struct Result
{
    int type = -1, root = 0;  // -1: no notes (FL's blank chord)
    int bass = -1;            // MIDI key of the named slash bass, -1 = none
    std::vector<int> extras;  // pitch classes above the root outside the triad
    std::array<double, 84> scores {};
    Chord chord() const;
};

// FL's bass_check: the lowest key (≤ maxBass) and the gap to the next one (both start at maxBass + 1).
inline std::pair<int, int> bassCheck (const std::vector<Note>& notes, int maxBass)
{
    auto low = maxBass + 1, second = maxBass + 1;
    for (const auto& n : notes)
    {
        if (n.key <= maxBass && n.key < low)
        {
            second = low;
            low = n.key;
        }
        else if (n.key < second)
            second = n.key;
    }
    return { low, std::min (second, maxBass + 1) - low };
}

// One block: the notes overlapping [from, to) (`to` <= `from`: every note counts fully).
inline Result analyse (const std::vector<Note>& notes, std::int64_t from, std::int64_t to, const Settings& set)
{
    Result r;
    std::vector<Note> in;
    for (const auto& n : notes)
        if (! n.muted && (to <= from || (n.start < to && n.start + std::max<std::int64_t> (1, n.length) > from)))
            in.push_back (n);
    if (in.empty())
        return r;

    const auto [low, gap] = bassCheck (in, set.maxBass);
    const auto bass = gap >= set.bassSeparation && low <= set.maxBass ? low : -1;
    std::array<std::vector<double>, 12> buckets, upper;
    for (const auto& n : in)
    {
        auto w = 1.0;
        if (n.key == bass)
            w *= set.bassWeightNote;
        if (to > from && set.durationDepth != 0.0)
        {
            const auto overlap = (double) (std::min (to, n.start + std::max<std::int64_t> (1, n.length)) - std::max (from, n.start));
            w *= 1.0 - (1.0 - overlap / (double) (to - from)) * set.durationDepth;
        }
        buckets[(std::size_t) pc (n.key)].push_back (w);
        if (bass >= 0 && n.key != bass)
            upper[(std::size_t) pc (n.key)].push_back (w);
    }
    const auto profile = [&set] (std::array<std::vector<double>, 12>& b) {
        std::array<double, 12> g {};
        for (std::size_t i = 0; i < 12; ++i)
        {
            if (b[i].empty())
                continue;
            std::sort (b[i].begin(), b[i].end());
            auto v = b[i].back();
            for (std::size_t k = 0; k + 1 < b[i].size(); ++k)
                v += set.cumulative * b[i][k];
            g[i] = v;
        }
        return g;
    };
    const auto G = profile (buckets);
    const auto E = profile (upper);

    auto best = -1;
    auto bestScore = 0.0, bestPriority = 0.0;
    for (int t = 0; t < 7; ++t)
        for (int root = 0; root < 12; ++root)
        {
            const auto& type = types()[(std::size_t) t];
            std::array<double, 12> tpl {};
            std::array<bool, 12> seen {};
            for (std::size_t slot = 0; slot < type.order.size(); ++slot)
            {
                const auto iv = type.order[slot];
                if (iv < 0)
                    continue;
                const auto p = (std::size_t) pc (root + iv);
                if (! seen[p])
                {
                    seen[p] = true;
                    tpl[p] = slotWeights[slot];
                }
            }
            auto s = 0.0, upperScore = 0.0;
            for (std::size_t i = 0; i < 12; ++i)
            {
                s += G[i] * tpl[i];
                upperScore += E[i] * tpl[i];
            }
            if (bass >= 0)
            {
                if (pc (bass) == root)
                    s *= set.rootMatch;
                else if (set.slashWeight != 0.0)
                    s += set.slashWeight * upperScore;
            }
            s *= 1.0 + (type.priority - 0.5) * set.typePriorityDepth;
            r.scores[(std::size_t) (t * 12 + root)] = s;
            if (best < 0 || s > bestScore || (s == bestScore && type.priority > bestPriority))
            {
                best = t * 12 + root;
                bestScore = s;
                bestPriority = type.priority;
            }
        }
    r.type = best / 12;
    r.root = best % 12;
    r.bass = gap >= set.namingSeparation && low <= set.maxBass ? low : -1;
    const auto& triad = types()[(std::size_t) r.type].triad;
    for (const auto& n : in)
    {
        const auto rel = pc (n.key - r.root);
        const auto inTriad = std::find (triad.begin(), triad.begin() + 3, rel) != triad.begin() + 3;
        if (! inTriad && n.key != r.bass && std::find (r.extras.begin(), r.extras.end(), rel) == r.extras.end())
            r.extras.push_back (rel);
    }
    std::sort (r.extras.begin(), r.extras.end());
    return r;
}

// FL's result as LYNX's chord: the type's triad, the played extensions (a ninth, eleventh or a clashing third / fifth
// an octave up, sixths and sevenths as they are), the slash bass
inline Chord Result::chord() const
{
    Chord c;
    c.root = root;
    c.intervals = types()[(std::size_t) std::max (0, type)].triad;
    const auto has = [&c] (int i) { return std::find (c.intervals.begin(), c.intervals.end(), i) != c.intervals.end(); };
    for (auto e : extras)
    {
        if (has (e))
            continue;
        auto v = e;
        if (e == 1 || e == 2 || e == 5)
            v = e + 12;
        else if ((e == 3 && has (4)) || (e == 4 && has (3)) || (e == 6 && has (7)) || (e == 8 && has (7)) || (e == 7 && (has (6) || has (8))))
            v = e + 12;
        if (! has (v))
            c.intervals.push_back (v);
    }
    std::sort (c.intervals.begin(), c.intervals.end());
    if (bass >= 0 && pc (bass) != root)
        c.bass = pc (bass);
    return c;
}
} // namespace analysis

// FL's Analyze over the strip (REA §2) without FL's trained style data: every unlocked block is analysed on its own
// with FL's sequence settings (FL's "individual" mode), and a block without notes takes the analysis of the block
// before it (FL's fill_blanks; the leading empty blocks the first one with notes). Nothing changes without notes.
inline void analyseStrip (Strip& s, const std::vector<Note>& notes)
{
    std::vector<std::optional<Chord>> found (s.blocks.size());
    std::int64_t t = 0;
    for (std::size_t i = 0; i < s.blocks.size(); ++i)
    {
        const auto r = analysis::analyse (notes, t, t + s.blocks[i].length, analysis::sequence());
        if (r.type >= 0)
            found[i] = r.chord();
        t += s.blocks[i].length;
    }
    std::optional<Chord> first;
    for (const auto& f : found)
        if (f && ! first)
            first = f;
    if (! first)
        return;
    std::optional<Chord> previous = first;
    for (std::size_t i = 0; i < s.blocks.size(); ++i)
    {
        if (found[i])
            previous = found[i];
        if (! s.blocks[i].locked)
        {
            s.blocks[i].chord = *previous;
            s.blocks[i].fixedBass = -1;
        }
    }
}

// --- FL's key guess (REA §3: Scale.estimate on the notes, at the slider's default only Ionian / Aeolian score) ------

struct Key
{
    int tonic = 0;
    bool minor = false;
};

// Every note in the scale scores 1 for the scale and, on the root, 1 for the root; chord roots weigh 2 (the first one
// twice) and chord notes 0.3. The best (score, root score) wins, in FL's order: root C … B, Major before Minor.
// `sampleFrom` > 0 draws among the top share (FL's Start over), weighted by score^(4^-entropy).
inline std::vector<std::pair<Key, std::pair<double, double>>> keyScores (const std::vector<int>& keys, const std::vector<Chord>& chords)
{
    std::vector<std::pair<Key, std::pair<double, double>>> out;
    for (int root = 0; root < 12; ++root)
        for (const auto minor : { false, true })
        {
            const auto& steps = scaleSteps (minor);
            const auto inScale = [&] (int p) { return std::find (steps.begin(), steps.end(), pc (p - root)) != steps.end(); };
            double general = 0.0, rootScore = 0.0;
            const auto add = [&] (int p, double w) {
                if (inScale (p))
                {
                    general += w;
                    if (pc (p) == root)
                        rootScore += w;
                }
            };
            for (auto k : keys)
                add (k, 1.0);
            for (std::size_t c = 0; c < chords.size(); ++c)
            {
                add (chords[c].bass >= 0 ? chords[c].bass : chords[c].root, c == 0 ? 4.0 : 2.0);
                for (auto iv : chords[c].intervals)
                    add (chords[c].root + iv, 0.3);
            }
            out.push_back ({ Key { root, minor }, { general, rootScore } });
        }
    std::stable_sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.second > b.second; });
    return out;
}

inline std::optional<Key> guessKey (const std::vector<Note>& notes, const std::vector<Chord>& chords = {})
{
    std::vector<int> keys;
    for (const auto& n : notes)
        keys.push_back (n.key);
    if (keys.size() < 3 && chords.empty()) // (FL's min_notes 3)
        return std::nullopt;
    return keyScores (keys, chords).front().first;
}

// FL's Start over key: from the opening notes and the locked chords, drawn among the best (more of them as the slider
// goes Adventurous); without either, at random, Major 65 % of the time (FL's random_pick).
inline Key drawKey (const std::vector<Note>& notes, const std::vector<Chord>& locked, float adventure, std::mt19937& random)
{
    std::uniform_real_distribution<double> uniform (0.0, 1.0);
    std::vector<int> keys;
    for (const auto& n : notes)
        keys.push_back (n.key);
    if (keys.size() < 3 && locked.empty())
    {
        Key k;
        k.minor = uniform (random) > 0.65;
        k.tonic = (int) (random() % 12u);
        return k;
    }
    const auto ranked = keyScores (keys, locked);
    // FL: D = (max(macro, 0.1) - 0.1) / 0.9, sample_from = 0.2 D², entropy = D - 1
    const auto d = (std::max (0.1, (double) adventure) - 0.1) / 0.9;
    const auto share = 0.2 * d * d;
    const auto top = std::max<std::size_t> (1, (std::size_t) std::ceil ((double) ranked.size() * share));
    if (top <= 1)
        return ranked.front().first;
    const auto power = std::pow (4.0, -(d - 1.0));
    std::vector<double> w;
    double total = 0.0;
    for (std::size_t i = 0; i < top; ++i)
        total += w.emplace_back (std::pow (std::max (0.0, ranked[i].second.first), power));
    auto pick = uniform (random) * total;
    for (std::size_t i = 0; i < top; ++i)
        if ((pick -= w[i]) <= 0.0)
            return ranked[i].first;
    return ranked.front().first;
}

// --- FL's Generate ---------------------------------------------------------------------------------

struct GenerateOptions
{
    GenMode mode = GenMode::rework;
    float adventure = 0.3f;     // FL's Conventional – Adventurous slider
    std::uint32_t seed = 1;
    int octave = 4;
    bool smoothVoicing = true;
    bool accompany = true;      // FL's auto-accompany: off only in Replace all
};

// Runs Generate on every unlocked block. `existing` = the Piano roll's notes when the tool opened (FL's opening notes:
// Analyze reads them, Rework / Start over follow them unless Replace all is chosen).
inline void generate (Strip& s, const std::vector<Note>& existing, const GenerateOptions& o)
{
    std::mt19937 random (o.seed * 7919u + 3u);
    const auto* accompany = o.accompany ? &existing : nullptr;

    if (o.mode == GenMode::startOver)
    {
        // FL's reset: only the locked chords stay; the key is drawn again from the opening notes and the locked chords
        // (at random without them); the count and the global settings stay
        std::vector<Chord> locked;
        for (const auto& b : s.blocks)
            if (b.locked)
                locked.push_back (b.chord);
        const auto key = drawKey (existing, locked, o.adventure, random);
        s.tonic = key.tonic;
        s.minor = key.minor;
        for (auto& b : s.blocks)
            if (! b.locked)
            {
                const auto length = b.length;
                b = Block {};
                b.length = length;
            }
    }

    if (o.mode == GenMode::analyze)
    {
        if (existing.empty())
            return; // (FL: no notes, no change)
        analyseStrip (s, existing);
    }
    else
    {
        const auto before = s;
        // left to right, each new chord against the chord before it and the next locked one
        for (std::size_t i = 0; i < s.blocks.size(); ++i)
            if (! s.blocks[i].locked)
            {
                // (the next block counts only when it is locked: an unlocked one is about to change)
                auto c = regenerateChord (s, i, o.adventure, o.seed, true, false, accompany);
                if (s.blocks[i].fixedBass >= 0)
                    c.bass = -1;
                s.blocks[i].chord = c;
            }
        // FL's ensure_change: when no unlocked chord changed, one of them (at random) is swapped for another
        if (o.mode == GenMode::rework)
        {
            std::vector<std::size_t> unlocked;
            bool changed = false;
            for (std::size_t i = 0; i < s.blocks.size(); ++i)
                if (! s.blocks[i].locked)
                {
                    unlocked.push_back (i);
                    changed = changed || ! (s.blocks[i].chord == before.blocks[i].chord);
                }
            if (! changed && ! unlocked.empty())
            {
                const auto i = unlocked[random() % unlocked.size()];
                s.blocks[i].chord = regenerateChord (s, i, o.adventure, o.seed + 7u, false, true, accompany);
            }
        }
    }
    voiceLead (s, o.octave, o.smoothVoicing);
}

// FL's "Count": blocks added at the end (generated) or removed from the end.
inline void setCount (Strip& s, int count, std::int64_t length, const GenerateOptions& o)
{
    count = std::clamp (count, 1, 64);
    while ((int) s.blocks.size() > count)
        s.blocks.pop_back();
    while ((int) s.blocks.size() < count)
    {
        Block b;
        b.length = length;
        s.blocks.push_back (b);
        s.blocks.back().chord = regenerateChord (s, s.blocks.size() - 1, o.adventure, o.seed + (std::uint32_t) s.blocks.size());
    }
    voiceLead (s, o.octave, o.smoothVoicing);
}

// FL's "Add passing chord": the block splits in two; the first half gets a new chord that leads on to it.
inline void addPassingChord (Strip& s, std::size_t i, const GenerateOptions& o)
{
    if (i >= s.blocks.size() || s.blocks[i].length < 2)
        return;
    auto passing = s.blocks[i];
    passing.locked = false;
    passing.length = s.blocks[i].length / 2;
    s.blocks[i].length -= passing.length;
    s.blocks.insert (s.blocks.begin() + (std::ptrdiff_t) i, passing);
    // a chord whose root leads to the next one: its dominant (a fifth above) when that fits the key, else any
    Chord leading = regenerateChord (s, i, o.adventure, o.seed + 101u);
    const auto target = s.blocks[i + 1].chord;
    for (int d = 0; d < 7; ++d)
    {
        const auto c = detail::triad (s, d, false, false);
        if (pc (c.root - target.root) == 7)
            leading = c;
    }
    s.blocks[i].chord = leading;
    voiceLead (s, o.octave, o.smoothVoicing);
}

// --- Learn from MIDI input (REA §4) ----------------------------------------------------------------------------

// FL's learn_chord: the played keys, stretched over the block, analysed as one chord (FL's single-chord analyser: one
// key is enough, it gives the major triad on it); the played pitch classes outside the triad become extensions, a key
// an octave or more under the others the slash bass. Returns the block as it becomes; nothing without keys.
inline std::optional<Block> learnBlock (const Block& old, const std::vector<int>& played, int octave)
{
    std::vector<int> keys;
    for (auto k : played)
        if (k >= 0 && k <= 127)
            keys.push_back (k);
    if (keys.empty())
        return std::nullopt;
    std::sort (keys.begin(), keys.end());
    std::vector<Note> notes;
    for (auto k : keys)
    {
        Note n;
        n.key = k;
        n.start = 0;
        n.length = std::max<std::int64_t> (1, old.length);
        notes.push_back (n);
    }
    const auto r = analysis::analyse (notes, 0, std::max<std::int64_t> (1, old.length), analysis::single());
    if (r.type < 0)
        return std::nullopt;
    auto b = old;
    b.chord = r.chord();
    b.fixedBass = -1;
    b.transpose = 0;
    b.bassTranspose = 0;
    b.voicing = Voicing::block;

    // FL's main / bass split: the lowest keys are the bass while each is an octave or more under the next and they
    // share one pitch class; the rest are the main notes, whose lowest one sets the inversion
    std::size_t bassCount = 0;
    while (bassCount + 1 < keys.size() && keys[bassCount + 1] - keys[bassCount] >= 12 && pc (keys[bassCount]) == pc (keys[0]))
        ++bassCount;
    const auto lowestMain = keys[bassCount];
    b.inversion = 0;
    const auto n = (int) b.chord.intervals.size();
    for (int inv = 0; inv < n; ++inv)
    {
        auto t = b;
        t.inversion = inv;
        const auto mk = mainKeys (t, octave);
        if (! mk.empty() && pc (mk.front()) == pc (lowestMain))
        {
            b.inversion = inv;
            break;
        }
    }
    return b;
}

inline std::optional<Chord> learnChord (const std::vector<int>& keys)
{
    if (const auto b = learnBlock (Block {}, keys, 4))
        return b->chord;
    return std::nullopt;
}

// FL's snap (tb.snap, 'nearest'): the pitch classes of the block's chord (with its extensions and slash bass); a key
// outside them, or one an earlier snapped note already sounds on, moves outward a semitone at a time; at an equal
// distance up and down a seeded coin decides (FL's direction_bias 0.5).
inline std::vector<int> chordClasses (const Block& b)
{
    std::vector<int> classes;
    for (auto iv : b.chord.intervals)
        classes.push_back (pc (b.chord.root + iv));
    if (b.fixedBass >= 0)
        classes.push_back (pc (b.fixedBass));
    else if (b.chord.bass >= 0)
        classes.push_back (pc (b.chord.bass));
    return classes;
}

inline int snapKey (int key, const std::vector<int>& classes, const std::vector<int>& taken, std::mt19937& random)
{
    const auto ok = [&] (int k) {
        return k >= 0 && k <= 131 && std::find (classes.begin(), classes.end(), pc (k)) != classes.end()
               && std::find (taken.begin(), taken.end(), k) == taken.end();
    };
    if (ok (key))
        return key;
    for (int d = 1; d <= 131; ++d)
    {
        const auto up = ok (key + d), down = ok (key - d);
        if (up && down)
            return (random() % 2u) == 0u ? key + d : key - d;
        if (up)
            return key + d;
        if (down)
            return key - d;
    }
    return key;
}

// (one note, no clash list, no coin: the key below wins a tie)
inline int snapToChord (int key, const Block& b)
{
    const auto classes = chordClasses (b);
    for (int d = 0; d <= 6; ++d)
    {
        if (std::find (classes.begin(), classes.end(), pc (key - d)) != classes.end())
            return key - d;
        if (std::find (classes.begin(), classes.end(), pc (key + d)) != classes.end())
            return key + d;
    }
    return key;
}

// --- writing ---------------------------------------------------------------------------------------

struct WriteOptions
{
    int channel = 0;
    std::int64_t start = 0;     // where the first block starts
    int octave = 4;
    bool main = true, bass = true; // FL's spanner › Notes (both off: chords shown, no notes)
    NonChord nonChord = NonChord::keep;
    std::string rhythm;         // LYNX's rhythm cells (prtools::ChordWriteOptions::rhythm); empty = held
    float velocity = 100.0f / 128.0f;
    bool group = true;          // FL: the notes of each generated chord are grouped
    std::vector<int> snapBlocks; // FL's chord menu › Snap existing notes: the blocks whose existing notes snap anyway
    std::uint32_t seed = 1;     // the snap's coin (FL's snap_seed)
};

// The block index sounding at tick `t` (relative to the strip's start), or -1.
inline int blockAt (const Strip& s, std::int64_t t)
{
    std::int64_t from = 0;
    for (std::size_t i = 0; i < s.blocks.size(); ++i)
    {
        if (t >= from && t < from + s.blocks[i].length)
            return (int) i;
        from += s.blocks[i].length;
    }
    return -1;
}

// The notes a block renders (main and / or bass, LYNX's rhythm cells), absolute ticks.
inline std::vector<Note> blockNotes (const Strip& s, std::size_t i, const WriteOptions& o)
{
    std::vector<Note> out;
    const auto& b = s.blocks[i];
    const auto chordStart = o.start + s.startOf (i);
    const auto length = std::max<std::int64_t> (1, b.length);
    std::vector<std::pair<std::int64_t, std::int64_t>> hits;
    if (o.rhythm.empty())
        hits.push_back ({ chordStart, length });
    else
    {
        // LYNX's rhythm cells over 4 x the first block's length, as prtools::writeProgression does
        const auto unit = std::max<std::int64_t> (1, s.blocks.front().length);
        const auto cells = (std::int64_t) o.rhythm.size();
        const auto blockLen = 4 * unit;
        const auto cell = std::max<std::int64_t> (1, blockLen / cells);
        for (auto t0 = (chordStart - o.start) / blockLen * blockLen; t0 < chordStart - o.start + length; t0 += blockLen)
            for (std::int64_t k = 0; k < cells; ++k)
            {
                const auto t = o.start + t0 + k * cell;
                if (t < chordStart || t >= chordStart + length)
                    continue;
                if (o.rhythm[(std::size_t) k] == '>' || o.rhythm[(std::size_t) k] == 'p')
                    hits.push_back ({ t, cell });
                else if (o.rhythm[(std::size_t) k] == '-' && ! hits.empty() && hits.back().first + hits.back().second == t)
                    hits.back().second += cell;
            }
    }
    std::vector<int> keys;
    if (o.main && b.main)
        keys = mainKeys (b, o.octave);
    if (o.bass && b.bass)
        keys.insert (keys.begin(), bassKey (b, o.octave));
    for (const auto& [t, len] : hits)
        for (auto k : keys)
        {
            Note n;
            n.start = t;
            n.length = std::min (len, chordStart + length - t);
            n.channel = o.channel;
            n.key = std::clamp (k, 0, 127);
            n.velocity = o.velocity;
            if (b.colour >= 0)
                n.colour = b.colour;
            out.push_back (n);
        }
    return out;
}

// Writes the strip into `pattern`: first FL's treatment of the `existing` notes (indices into pattern.notes, the notes
// the tool found; REA §5b), then each block's notes. Returns how many chord notes were written.
//  - Replace all: the part of each note inside a chord block is cut away (what lies before or after stays);
//  - Replace clashing: only a note on the very key of one of the block's chord notes is cut out of that block;
//  - Keep: every note stays (one that exactly repeats a chord note gives way to it);
//  - Snap: every note stays, the part inside a block moved onto the block's chord (FL's snap_to_chord).
// A note overlapping a block by less than a sixteenth of a beat at its ends counts as outside (FL's ±6-tick margin).
inline int write (Pattern& pattern, const std::vector<int>& existing, const Strip& s, const WriteOptions& o)
{
    const std::int64_t margin = ppq / 16;
    std::vector<std::vector<Note>> rendered (s.blocks.size());
    for (std::size_t i = 0; i < s.blocks.size(); ++i)
        rendered[i] = blockNotes (s, i, o);

    // take the existing notes out, treat them, and put back what stays
    std::vector<int> indices;
    for (auto i : existing)
        if (i >= 0 && i < (int) pattern.notes.size())
            indices.push_back (i);
    std::sort (indices.begin(), indices.end());
    indices.erase (std::unique (indices.begin(), indices.end()), indices.end());
    std::vector<Note> notes;
    for (auto i : indices)
        notes.push_back (pattern.notes[(std::size_t) i]);
    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        pattern.notes.erase (pattern.notes.begin() + *it);

    // cuts the parts of the notes inside block i for which `cut` says so
    const auto cutBlock = [&] (std::size_t i, const std::function<bool (const Note&)>& cut) {
        const auto from = o.start + s.startOf (i), to = from + s.blocks[i].length;
        std::vector<Note> next;
        for (const auto& n : notes)
        {
            const auto end = n.start + std::max<std::int64_t> (1, n.length);
            if (n.start >= to - margin || end <= from + margin || ! cut (n))
            {
                next.push_back (n);
                continue;
            }
            if (n.start < from) // the head before the block
            {
                auto head = n;
                head.length = from - n.start;
                next.push_back (head);
            }
            if (end > to) // the tail after it
            {
                auto tail = n;
                tail.start = to;
                tail.length = end - to;
                next.push_back (tail);
            }
        }
        notes.swap (next);
    };
    const auto snapBlock = [&] (std::size_t i, std::mt19937& random) {
        const auto from = o.start + s.startOf (i), to = from + s.blocks[i].length;
        const auto classes = chordClasses (s.blocks[i]);
        std::vector<Note> next;
        std::vector<Note> snapped;
        for (const auto& n : notes)
        {
            const auto end = n.start + std::max<std::int64_t> (1, n.length);
            if (n.start >= to - margin || end <= from + margin)
            {
                next.push_back (n);
                continue;
            }
            auto inside = n;
            if (n.start < from - margin) // the head stays where it was
            {
                auto head = n;
                head.length = from - n.start;
                next.push_back (head);
                inside.start = from;
            }
            auto insideEnd = end;
            if (end > to + margin)
            {
                auto tail = n;
                tail.start = to;
                tail.length = end - to;
                next.push_back (tail);
                insideEnd = to;
            }
            inside.length = std::max<std::int64_t> (1, insideEnd - inside.start);
            std::vector<int> taken; // the keys of the snapped notes sounding with it
            for (const auto& x : snapped)
                if (x.start < inside.start + inside.length && x.start + x.length > inside.start)
                    taken.push_back (x.key);
            inside.key = std::clamp (snapKey (inside.key, classes, taken, random), 0, 127);
            snapped.push_back (inside);
            next.push_back (inside);
        }
        notes.swap (next);
    };

    std::mt19937 random (o.seed * 1103515245u + 12345u);
    for (std::size_t i = 0; i < s.blocks.size(); ++i)
    {
        switch (o.nonChord)
        {
            case NonChord::replaceAll:
                cutBlock (i, [] (const Note&) { return true; });
                break;
            case NonChord::replaceClashing:
                cutBlock (i, [&] (const Note& n) {
                    return std::any_of (rendered[i].begin(), rendered[i].end(), [&] (const Note& c) { return c.key == n.key; });
                });
                break;
            case NonChord::snap:
                snapBlock (i, random);
                break;
            case NonChord::keep:
                break;
        }
        if (o.nonChord != NonChord::snap && o.nonChord != NonChord::replaceAll
            && std::find (o.snapBlocks.begin(), o.snapBlocks.end(), (int) i) != o.snapBlocks.end())
            snapBlock (i, random);
    }

    // an existing note that exactly repeats a chord note gives way to it (FL's no_exact_copies)
    std::vector<Note> chordNotes;
    for (const auto& r : rendered)
        chordNotes.insert (chordNotes.end(), r.begin(), r.end());
    notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const Note& n) {
                     return std::any_of (chordNotes.begin(), chordNotes.end(), [&] (const Note& c) {
                         return c.key == n.key && c.start == n.start && c.length == n.length;
                     });
                 }),
                 notes.end());
    pattern.notes.insert (pattern.notes.end(), notes.begin(), notes.end());

    auto nextGroup = 0;
    for (const auto& n : pattern.notes)
        nextGroup = std::max (nextGroup, n.group);
    int written = 0;
    for (auto& r : rendered)
    {
        if (r.empty())
            continue;
        const auto group = o.group && nextGroup < 0xFFFF ? ++nextGroup : 0;
        for (auto& n : r)
        {
            n.group = group;
            pattern.notes.push_back (n);
            ++written;
        }
    }
    pattern.sortNotes();
    return written;
}

// --- building a strip ------------------------------------------------------------------------------

inline Strip fromChords (const std::vector<Chord>& chords, int tonic, bool minor, std::int64_t length, int octave, bool smooth)
{
    Strip s;
    s.tonic = pc (tonic);
    s.minor = minor;
    for (const auto& c : chords)
    {
        Block b;
        b.chord = c;
        b.length = std::max<std::int64_t> (1, length);
        s.blocks.push_back (b);
    }
    voiceLead (s, octave, smooth, true);
    return s;
}

// FL's "Type a progression" and "Type in chord name...": chord names ("C, G, Am, F", "F#m add9", "C/E") or Roman
// numerals ("I-V-vi-IV") in the key, separated by commas, dashes, bars or spaces. Nothing when a part can't be read.
inline std::optional<std::vector<Chord>> parseText (const std::string& text, int tonic, bool minor)
{
    // split into tokens; a part that starts with a quality ("add9", "sus4", "7", "/E", "maj7") joins the one before
    std::vector<std::string> tokens;
    std::string cur;
    const auto flush = [&] {
        if (! cur.empty())
            tokens.push_back (cur);
        cur.clear();
    };
    for (std::size_t i = 0; i <= text.size(); ++i)
    {
        const auto c = i < text.size() ? text[i] : ',';
        if (c == ',' || c == '|' || c == '-' || c == ' ')
        {
            flush();
            continue;
        }
        cur += c;
    }
    std::vector<std::string> parts;
    for (const auto& tk : tokens)
    {
        const auto joins = ! parts.empty()
                           && (tk.rfind ("add", 0) == 0 || tk.rfind ("sus", 0) == 0 || tk.rfind ("maj", 0) == 0 || tk[0] == '/'
                               || std::isdigit ((unsigned char) tk[0]) || tk.rfind ("\xE2\x88\x86", 0) == 0);
        if (joins)
            parts.back() += tk;
        else
            parts.push_back (tk);
    }
    if (parts.empty())
        return std::nullopt;

    static const int degreeOf[12] = { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 };
    static const int flatOf[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
    static const char* upperNames[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
    static const int letters[] = { 9, 11, 0, 2, 4, 5, 7 }; // A … G
    // a letter name ("F#", "Bb") at the start: its pitch class and length, or -1
    const auto letter = [] (const std::string& s, std::size_t& pos) -> int {
        if (pos >= s.size() || s[pos] < 'A' || s[pos] > 'G')
            return -1;
        auto p = letters[s[pos] - 'A'];
        ++pos;
        for (;;)
        {
            if (pos < s.size() && s[pos] == '#') { ++p; ++pos; }
            else if (pos < s.size() && s[pos] == 'b') { --p; ++pos; }
            else if (s.compare (pos, 3, "\xE2\x99\xAF") == 0) { ++p; pos += 3; }
            else if (s.compare (pos, 3, "\xE2\x99\xAD") == 0) { --p; pos += 3; }
            else break;
        }
        return pc (p);
    };
    const auto numeral = [&] (int pitch, bool lower) {
        const auto rel = pc (pitch - tonic);
        std::string n = upperNames[degreeOf[rel]];
        if (lower)
            for (auto& ch : n)
                ch = (char) std::tolower ((unsigned char) ch);
        return (flatOf[rel] ? std::string ("b") : std::string()) + n;
    };

    std::vector<Chord> out;
    for (const auto& part : parts)
    {
        std::size_t pos = 0;
        const auto root = letter (part, pos);
        std::string converted = part;
        auto augmented = false;
        if (root >= 0)
        {
            auto rest = part.substr (pos);
            auto lower = false;
            if (rest.rfind ("aug", 0) == 0 || (! rest.empty() && rest[0] == '+'))
            {
                augmented = true;
                rest = rest.substr (rest[0] == '+' ? 1 : 3);
            }
            else if (rest.rfind ("dim", 0) == 0 || rest.rfind ("\xC2\xB0", 0) == 0)
                lower = true;
            else if (! rest.empty() && rest[0] == 'm' && rest.rfind ("maj", 0) != 0)
            {
                lower = true;
                rest = rest.substr (1);
            }
            // a slash bass by letter
            if (const auto slash = rest.find ('/'); slash != std::string::npos)
            {
                std::size_t bp = slash + 1;
                const auto bass = letter (rest, bp);
                if (bass >= 0)
                    rest = rest.substr (0, slash) + "/" + numeral (bass, false) + rest.substr (bp);
            }
            converted = numeral (root, lower) + rest;
        }
        auto parsed = prtools::parseProgression (converted, tonic, minor);
        if (! parsed || parsed->size() != 1)
            return std::nullopt;
        if (augmented)
            parsed->front().intervals = { 0, 4, 8 };
        out.push_back (parsed->front());
    }
    return out;
}

// FL's "Type a progression" / "Get current progression": the blocks' chords as Roman numerals in the key.
inline std::string toText (const Strip& s)
{
    std::vector<Chord> chords;
    for (const auto& b : s.blocks)
        chords.push_back (b.chord);
    return prtools::toNumerals (chords, s.tonic);
}
} // namespace lynx::chordstrip

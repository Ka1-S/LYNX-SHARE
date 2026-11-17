#pragma once

// FL Studio's Piano roll arpeggiator tool, Tools › Arpeggiate (round 190jv, coverage prtools-0003; TPRArpForm, REA of
// FLEngine_x64.dll in docs/analise/REA_rodada190jv.md, "pr" § arpeggiator). FL plays a *pattern* (an .fsc score of FL's
// Data/Patches/Scores/Arpeggiator folder, read from the user's FL install, never shipped) over the chords of the notes:
//
// The pattern (the form's loader FUN_00df2680):
//  - its loop is the end of its last note (TScore length FUN_010620e0: start + max (length, 1)); a note of colour 12
//    instead sets the loop to its own length and is dropped;
//  - every start × Time mul and every length × Time mul (× Gate when the note is shorter than the loop), rounded, at
//    least FL's minimum note length; the loop × Time mul, at least 1. Time mul (wheel 1…7) = 1/8 1/4 1/2 1 2 3 4
//    (table 0x15280f8), Gate = wheel / 48 (wheel 0…48).
// The chords (the apply FUN_00df2bd0): the notes are read in time order; the notes that sound together are "held",
// sorted by key (lowest first). The time is cut into segments wherever a note starts or ends; in each segment the
// pattern plays from its loop position:
//  - a pattern note's key k: k % 12 picks the held note (k % 12 modulo the number held; Pattern mode Flip counts from
//    the top, Alternate flips every other loop pass), k / 12 − 5 the octave (FL's C5 = 0); Range (octaves, 1…6) adds
//    the loop pass modulo Range (Range mode Flip / Alternate as the Pattern mode, Alternate on every other Range
//    cycle) except for a pattern note of colour 8;
//  - Sync Time: the loop runs from the pattern's start; Block: it restarts when a note starts after a silence; Chord: it
//    restarts at every chord change;
//  - a note cut by the segment's end, or as long as the loop, is "open": the next note of the same key, channel and
//    flags with the same pan / velocity / Mod X / Mod Y that starts while it plays (or right at its end) extends it
//    instead of starting again — except at a segment start where the loop restarted;
//  - the note takes the held note's properties; the pattern note's slide flag and porta (toggled) and its levels:
//    velocity × pattern velocity / 100, the others + (pattern − centre), each by its strength wheel (0…128; FL's
//    PAN VEL REL MOD X MOD Y PITCH, FUN_00df2ad0), clamped to FL's ranges;
//  - Group notes: every source note without a group gets a new group, which its arpeggiated notes share.
// The notes that took part are replaced by the result; the others stay. Header only, no JUCE.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "core/PianoRollTools.h"
#include "core/Song.h"

namespace lynx::flarp
{
enum class Mode { normal, flip, alternate };   // FL's Pattern mode / Range mode items
enum class Sync { time, block, chord };        // FL's Sync items

struct Options
{
    Mode patternMode = Mode::normal, rangeMode = Mode::normal;
    Sync sync = Sync::time;
    int range = 1;    // octaves, FL's ArpRangeSelect 1…6
    int timeMul = 4;  // FL's TimeMulWheel 1…7 (4 = ×1)
    int gate = 48;    // FL's GateWheel 0…48 (48 = 100 %)
    bool groupNotes = false;
    // FL's level wheels 0…128 in its order: PAN VEL REL MOD X MOD Y PITCH (all 128 by default)
    std::array<int, 6> strength { 128, 128, 128, 128, 128, 128 };
};

// FL's Time mul table (0x15280f8)
inline float timeMultiplier (int wheel) noexcept
{
    static constexpr std::array<float, 8> table { 0.0f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f };
    return table[(std::size_t) std::clamp (wheel, 1, 7)];
}

// FL's minimum note length (4 ticks at FL's PPQ 96)
constexpr std::int64_t minLength = ppq / 24;

// A pattern note in FL's byte levels (as FL keeps them)
struct Step
{
    std::int64_t start = 0, length = 0;
    int key = 60, colour = 0;
    bool slide = false, porta = false;
    std::array<int, 6> level { 64, 100, 64, 128, 128, 120 }; // pan vel release modX modY finePitch (FL bytes)
};

struct Prepared
{
    std::vector<Step> steps; // in time order
    std::int64_t loop = 1;
};

namespace detail
{
inline std::int64_t round (double v) noexcept { return (std::int64_t) std::nearbyint (v); } // (Delphi Round: half to even)
inline int byteOf (double v, int lo, int hi) noexcept { return std::clamp ((int) std::lround (v), lo, hi); }

// FL's level table (0x15d51a8): the maximum and the centre of pan, velocity, release, Mod X, Mod Y, fine pitch
constexpr std::array<int, 6> levelMax { 128, 128, 128, 255, 255, 240 };
constexpr std::array<int, 6> levelCentre { 64, 100, 64, 128, 128, 120 };

inline std::array<int, 6> bytesOf (const Note& n) noexcept
{
    return { byteOf (64.0 + n.pan * 64.0, 0, 128), byteOf (n.velocity * 128.0, 0, 128), byteOf (n.release * 128.0, 0, 128),
             byteOf (n.modX * 256.0, 0, 255), byteOf (n.modY * 256.0, 0, 255), byteOf (120.0 + n.finePitch, 0, 240) };
}

// FUN_00df2ad0: one level of a new note from its held note's byte `h` and the pattern's byte `p`, by strength `s`
inline int blend (int k, int h, int p, int s) noexcept
{
    const auto target = k == 1 ? (int) ((unsigned) p * (unsigned) h / 100u) : p + (h - levelCentre[(std::size_t) k]);
    const auto r = h + (target - h) * s / 128; // (Delphi: (d * s) div 128, toward zero)
    return std::clamp (r, 0, levelMax[(std::size_t) k]);
}
} // namespace detail

// The loader's law on a parsed .fsc (LYNX ticks).
inline Prepared prepare (const prtools::Score& score, const Options& o)
{
    Prepared out;
    std::int64_t loop = 0;
    for (const auto& n : score.notes)
        loop = std::max (loop, n.start + std::max<std::int64_t> (n.length, 1));
    const auto mul = timeMultiplier (o.timeMul);
    const auto gate = (float) ((double) std::clamp (o.gate, 0, 48) / 48.0);
    for (const auto& n : score.notes)
    {
        if (n.colour == 12) // the loop-length marker
        {
            loop = n.length;
            continue;
        }
        Step s;
        s.start = detail::round ((double) ((float) n.start * mul));
        auto factor = mul;
        if (n.length < loop)
            factor *= gate;
        s.length = std::max (minLength, detail::round ((double) ((float) n.length * factor)));
        s.key = n.key;
        s.colour = n.colour;
        s.slide = n.slide;
        s.porta = n.porta;
        s.level = { detail::byteOf (64.0 + n.pan * 64.0, 0, 128), detail::byteOf (n.velocity * 128.0, 0, 128),
                    detail::byteOf (n.release * 128.0, 0, 128), detail::byteOf (n.modX * 255.0, 0, 255),
                    detail::byteOf (n.modY * 255.0, 0, 255), detail::byteOf (120.0 + n.finePitch, 0, 240) };
        out.steps.push_back (s);
    }
    std::stable_sort (out.steps.begin(), out.steps.end(), [] (const Step& a, const Step& b) { return a.start < b.start; });
    out.loop = std::max<std::int64_t> (1, detail::round ((double) ((float) loop * mul)));
    return out;
}

// Arpeggiates the notes `indices` of `pattern` (FL: the selection, or all of the channel's notes) with `arp`. Returns
// the indices of the new notes (after sorting).
inline std::vector<int> apply (Pattern& pattern, const std::vector<int>& indices, const Prepared& arp, const Options& o)
{
    constexpr auto none = std::numeric_limits<std::int64_t>::max();
    std::vector<int> source;
    for (const auto i : indices)
        if (i >= 0 && i < (int) pattern.notes.size() && pattern.notes[(std::size_t) i].length > 0)
            source.push_back (i);
    std::sort (source.begin(), source.end());
    source.erase (std::unique (source.begin(), source.end()), source.end());
    if (source.empty() || arp.steps.empty())
        return {};
    std::stable_sort (source.begin(), source.end(), [&] (int a, int b) { return pattern.notes[(std::size_t) a].start < pattern.notes[(std::size_t) b].start; });

    std::vector<Note> notes; // the source notes, in time order (a group given here stays with their copies)
    for (const auto i : source)
        notes.push_back (pattern.notes[(std::size_t) i]);
    int nextGroup = 0;
    for (const auto& n : pattern.notes)
        nextGroup = std::max (nextGroup, n.group);

    struct Out
    {
        Note note;
        std::array<int, 6> level {};
        bool open = false;
    };
    std::vector<Out> out; // kept in start order (FL's score adds sorted by time)

    std::vector<int> held; // indices into `notes`, sorted by key
    const auto endOf = [&] (int h) { return notes[(std::size_t) h].start + notes[(std::size_t) h].length; };
    std::int64_t cs = none, ce = none, origin = 0;
    const auto loop = std::max<std::int64_t> (1, arp.loop);
    const auto range = std::clamp (o.range, 1, 6);

    const auto removeEnded = [&] {
        for (int k = (int) held.size() - 1; k >= 0; --k)
            if (endOf (held[(std::size_t) k]) <= cs)
                held.erase (held.begin() + k);
    };

    const auto flush = [&] {
        if (held.empty())
            return;
        const auto count = (int) held.size();
        const auto since = (std::uint64_t) (cs - origin);
        auto cycle = (std::int64_t) (since / (std::uint64_t) loop);
        auto offset = -(std::int64_t) (since % (std::uint64_t) loop);
        for (std::size_t k = 0;;)
        {
            const auto& p = arp.steps[k];
            const auto octave = p.key / 12, rank = p.key % 12;
            auto pick = rank % count;
            if (o.patternMode == Mode::flip || (o.patternMode == Mode::alternate && (cycle & 1) == 1))
                pick = count - pick - 1;
            const auto& h = notes[(std::size_t) held[(std::size_t) pick]];
            auto shift = octave - 5;
            if (range != 1 && p.colour != 8)
            {
                auto r = (int) (cycle % range);
                if (o.rangeMode == Mode::flip || (o.rangeMode == Mode::alternate && ((cycle / range) & 1) == 1))
                    r = range - r - 1;
                shift += r;
            }
            auto t = cs + offset + p.start;
            if (t >= ce)
                break;
            const auto available = ce - t;
            auto length = std::min (p.length, available);
            if (t < cs)
            {
                length -= cs - t;
                t = cs;
            }
            if (length > 0)
            {
                Out n { h, {}, available < p.length || loop <= p.length };
                n.note.start = t;
                n.note.length = length;
                n.note.key = std::clamp (h.key + shift * 12, 0, 127);
                n.note.slide = p.slide;
                n.note.porta = h.porta != p.porta;
                const auto heldLevel = detail::bytesOf (h);
                for (int l = 0; l < 6; ++l)
                    n.level[(std::size_t) l] = detail::blend (l, heldLevel[(std::size_t) l], p.level[(std::size_t) l],
                                                              std::clamp (o.strength[(std::size_t) l], 0, 128));
                // (a level that did not move keeps the held note's exact value)
                if (n.level[0] != heldLevel[0]) n.note.pan = (float) (n.level[0] - 64) / 64.0f;
                if (n.level[1] != heldLevel[1]) n.note.velocity = (float) n.level[1] / 128.0f;
                if (n.level[2] != heldLevel[2]) n.note.release = (float) n.level[2] / 128.0f;
                if (n.level[3] != heldLevel[3]) n.note.modX = (float) n.level[3] / 256.0f;
                if (n.level[4] != heldLevel[4]) n.note.modY = (float) n.level[4] / 256.0f;
                if (n.level[5] != heldLevel[5]) n.note.finePitch = (float) (n.level[5] - 120);

                // FUN_01060620: the first note of the same channel, flags and key that plays over [t − 1, t − 1 + length)
                auto merged = false;
                const auto from = t - 1;
                for (auto& e : out)
                {
                    const auto eLen = std::max<std::int64_t> (1, e.note.length);
                    if (e.note.channel != n.note.channel || e.note.slide != n.note.slide || e.note.key != n.note.key
                        || from + std::max<std::int64_t> (1, length) <= e.note.start || from >= e.note.start + eLen)
                        continue;
                    if (e.open && (cs < from || origin != cs) && e.level[0] == n.level[0] && e.level[1] == n.level[1]
                        && e.level[3] == n.level[3] && e.level[4] == n.level[4])
                    {
                        e.note.length = std::max (e.note.length, t + length - e.note.start);
                        e.open = n.open;
                        e.note.porta = n.note.porta;
                        e.note.colour = n.note.colour;
                        e.note.muted = n.note.muted;
                        merged = true;
                    }
                    break;
                }
                if (! merged)
                {
                    const auto at = std::upper_bound (out.begin(), out.end(), n.note.start, [] (std::int64_t s, const Out& x) { return s < x.note.start; });
                    out.insert (at, n);
                }
            }
            if (++k == arp.steps.size())
            {
                k = 0;
                offset += loop;
                ++cycle;
            }
        }
    };

    for (int i = 0; i < (int) notes.size() || ce != none; ++i)
    {
        auto doFlush = false;
        if (i < (int) notes.size())
        {
            auto& n = notes[(std::size_t) i];
            if (cs != none && cs < n.start)
            {
                ce = std::min (ce, n.start);
                doFlush = true;
                removeEnded();
                --i; // (read again after the flush)
            }
            else
            {
                cs = n.start;
                removeEnded();
                if ((held.empty() && o.sync == Sync::block) || o.sync == Sync::chord)
                    origin = cs;
                ce = std::min (ce, n.start + n.length);
                auto at = held.begin();
                while (at != held.end() && notes[(std::size_t) *at].key < n.key)
                    ++at;
                held.insert (at, i);
                if (o.groupNotes && n.group == 0)
                    n.group = nextGroup = std::min (nextGroup + 1, 0xffff);
            }
        }
        else
        {
            doFlush = ce != none;
            removeEnded();
        }
        if (doFlush)
        {
            flush();
            const auto old = ce;
            cs = ce;
            ce = none;
            for (int k = (int) held.size() - 1; k >= 0; --k)
            {
                const auto end = endOf (held[(std::size_t) k]);
                if (old + 1 <= end && end <= ce)
                {
                    ce = end;
                    if (o.sync == Sync::chord)
                        origin = old;
                }
            }
        }
    }

    // the source notes go, the result comes in
    std::sort (source.begin(), source.end());
    for (auto it = source.rbegin(); it != source.rend(); ++it)
        pattern.notes.erase (pattern.notes.begin() + *it);
    const auto first = pattern.notes.size();
    for (const auto& n : out)
        pattern.notes.push_back (n.note);
    std::vector<Note> added (pattern.notes.begin() + (std::ptrdiff_t) first, pattern.notes.end());
    pattern.sortNotes();
    std::vector<int> result;
    for (int i = 0; i < (int) pattern.notes.size(); ++i)
        for (const auto& a : added)
            if (pattern.notes[(std::size_t) i].start == a.start && pattern.notes[(std::size_t) i].key == a.key
                && pattern.notes[(std::size_t) i].length == a.length && pattern.notes[(std::size_t) i].channel == a.channel)
            {
                result.push_back (i);
                break;
            }
    return result;
}
} // namespace lynx::flarp

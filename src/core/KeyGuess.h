#pragma once

// "Guess from notes" for the project key (round 184a S11, design §3.1): which key the song's notes are in. A
// pitch-class histogram of every pattern note, weighted by length, is scored against each common scale (the Key menu's
// first entries, projectkey::commonFirst) on each of the 12 roots: the share of the notes in the scale, plus a bonus
// for the notes on the root and its fifth (that is what tells C major from A minor, or D Dorian, on the same notes).
// Percussion is left out: a channel that only ever plays one key (FL's step sequencer puts every step on C5) is a drum
// sound, not a pitch. Header only, no JUCE; it reads the song and changes nothing.

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <vector>

#include "core/ProjectKey.h"
#include "core/Song.h"

namespace lynx::keyguess
{
struct Candidate
{
    projectkey::Key key;
    float score = 0.0f;
    int notesInScale = 0; // pitched notes whose key is in the scale ("%d of %d notes in scale")
    int notes = 0;        // pitched notes counted
};

// How many of the common scales are tried (Major, Minor, the pentatonics, Blues, Harmonic minor, Dorian, Mixolydian,
// Phrygian, Lydian: the ten the Key menu lists first).
constexpr int commonScales = 10;

// The channels whose notes count: those with at least two different keys over the whole song (a drum sound plays one).
inline std::vector<bool> pitchedChannels (const Song& song)
{
    std::vector<std::set<int>> keys (song.channels.size());
    for (const auto& p : song.patterns)
        for (const auto& n : p.notes)
            if (n.channel >= 0 && n.channel < (int) keys.size() && keys[(std::size_t) n.channel].size() < 2)
                keys[(std::size_t) n.channel].insert (n.key);
    std::vector<bool> out (keys.size());
    for (std::size_t c = 0; c < keys.size(); ++c)
        out[c] = keys[c].size() >= 2;
    return out;
}

struct Histogram
{
    std::array<double, 12> weight {}; // note length in ticks per pitch class
    std::vector<int> keys;            // every pitched, unmuted note's key
    double total = 0.0;
};

inline Histogram histogram (const Song& song)
{
    Histogram h;
    const auto pitched = pitchedChannels (song);
    for (const auto& p : song.patterns)
        for (const auto& n : p.notes)
        {
            if (n.muted || n.channel < 0 || n.channel >= (int) pitched.size() || ! pitched[(std::size_t) n.channel])
                continue;
            const auto w = (double) std::max<std::int64_t> (1, n.length);
            h.weight[(std::size_t) (((n.key % 12) + 12) % 12)] += w;
            h.total += w;
            h.keys.push_back (n.key);
        }
    return h;
}

// The best `count` keys for the histogram, best first. Empty when there is no pitched note.
inline std::vector<Candidate> guess (const Histogram& h, int count = 3)
{
    std::vector<Candidate> all;
    if (h.total <= 0.0)
        return all;
    const auto order = projectkey::commonFirst();
    const auto scales = std::min<int> (commonScales, (int) order.size());
    for (int s = 0; s < scales; ++s)
    {
        const auto flScale = order[(std::size_t) s];
        for (int root = 0; root < 12; ++root)
        {
            const projectkey::Key key { root, flScale };
            const auto& degrees = projectkey::intervals (key);
            double inside = 0.0;
            for (auto d : degrees)
                inside += h.weight[(std::size_t) ((root + d) % 12)];
            const auto hasFifth = std::find (degrees.begin(), degrees.end(), 7) != degrees.end();
            const auto tonic = h.weight[(std::size_t) root] + (hasFifth ? 0.5 * h.weight[(std::size_t) ((root + 7) % 12)] : 0.0);
            Candidate c;
            c.key = key;
            // in-scale share first; the tonic decides between scales with the same notes; a hair for the menu's order
            // (Major before its pentatonic when the notes fit both)
            c.score = (float) (inside / h.total + 0.3 * tonic / h.total - 0.0001 * s);
            c.notes = (int) h.keys.size();
            for (auto k : h.keys)
                if (projectkey::contains (key, k))
                    ++c.notesInScale;
            all.push_back (c);
        }
    }
    std::stable_sort (all.begin(), all.end(), [] (const Candidate& a, const Candidate& b) { return a.score > b.score; });
    if ((int) all.size() > count)
        all.resize ((std::size_t) std::max (0, count));
    return all;
}

inline std::vector<Candidate> guess (const Song& song, int count = 3) { return guess (histogram (song), count); }
} // namespace lynx::keyguess

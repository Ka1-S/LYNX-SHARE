#pragma once

// Note Chance, Velocity Range and probability groups (round 187c; Live 12's note probability, a LYNX addition: FL has
// none). Live's manual (Editing MIDI, "Chance" and "Velocity Deviation"): a note plays with its probability each time
// it is played; its velocity is "chosen randomly from within the specified range each time a note is played" (+20 on
// 60 = between 60 and 80; negative ranges go down); "Play All" groups play all their notes with the group's
// probability, "Play One" groups play "only one note in the group ... selected at random".
//
// LYNX draws these from a stateless hash instead of a running random generator, so the audio thread keeps no state and
// renders are reproducible: the draw for one occurrence of a note depends only on Song::chanceSeed, where the note is
// (its clip or pattern, channel, key and start) and which pass of the loop this is. The "on" and "off" of an occurrence
// see the same draw, whatever the block size; each new loop pass draws again.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx::chance
{
constexpr std::uint64_t mix (std::uint64_t x) noexcept // splitmix64's finaliser
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// [0, 1) from a hash
constexpr double unit (std::uint64_t h) noexcept { return (double) (h >> 11) * (1.0 / 9007199254740992.0); }

// The place a note plays from: the seed and the clip (or 0 for the pattern itself) and the clip's pass
constexpr std::uint64_t context (std::uint32_t seed, std::uint64_t clip, std::int64_t pass = 0) noexcept
{
    return mix ((std::uint64_t) seed * 0x100000001B3ull ^ mix (clip) ^ mix ((std::uint64_t) pass + 0x51ED27u));
}

// One note in a context: everything a draw needs. `notes` + `index` find the note's Play One group (lazily: only an
// occurrence that is actually emitted scans the pattern).
struct Roll
{
    std::uint64_t place = 0;
    const Note* note = nullptr;
    const std::vector<Note>* notes = nullptr;
    std::size_t index = 0;
};

constexpr std::uint64_t noteIdentity (const Note& n) noexcept
{
    return ((std::uint64_t) (std::uint32_t) n.channel << 40) ^ ((std::uint64_t) (n.key & 0xFF) << 32) ^ (std::uint64_t) n.start;
}

// The member rank of notes[index] in its group and the group's size (members = same chanceGroup, in pattern order)
inline void groupRank (const std::vector<Note>& notes, std::size_t index, int& rank, int& count) noexcept
{
    rank = 0;
    count = 0;
    const auto g = notes[index].chanceGroup;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (notes[i].chanceGroup == g)
        {
            if (i < index)
                ++rank;
            ++count;
        }
}

// Does occurrence `pass` of the note play, and at which velocity (0..1)? Notes without chance always play as they are.
inline bool plays (const Roll& r, std::int64_t pass, float& velocity) noexcept
{
    const auto& n = *r.note;
    velocity = n.velocity;
    if (! n.hasChance())
        return true;
    const auto occurrence = mix (r.place ^ mix ((std::uint64_t) pass * 0xD6E8FEB86659FD93ull));
    if (n.chanceGroup > 0)
    {
        const auto group = mix (occurrence ^ (0xC0FFEEull + (std::uint64_t) n.chanceGroup));
        if (unit (group) >= (double) n.chance)
            return false;
        if (n.chanceOne && r.notes != nullptr && r.index < r.notes->size())
        {
            int rank = 0, count = 1;
            groupRank (*r.notes, r.index, rank, count);
            if (count > 1 && (int) (mix (group ^ 0x0E0E0Eull) % (std::uint64_t) count) != rank)
                return false;
        }
    }
    else if (n.chance < 1.0f && unit (mix (occurrence ^ noteIdentity (n))) >= (double) n.chance)
        return false;
    if (n.velocityRange != 0.0f)
    {
        const auto u = unit (mix (occurrence ^ noteIdentity (n) ^ 0x7E1D3Aull));
        // Live's velocities are 1…127: the range never takes a note below 1
        velocity = (float) std::clamp ((double) n.velocity + (double) n.velocityRange * u, 1.0 / 127.0, 1.0);
    }
    return true;
}

// The notes of a pattern as its first pass plays them (MIDI export of the realised notes): the ones that play, at the
// velocity they play
inline std::vector<Note> realised (const std::vector<Note>& notes, std::uint32_t seed, std::uint64_t clip = 0, std::int64_t pass = 0)
{
    std::vector<Note> out;
    out.reserve (notes.size());
    const auto place = context (seed, clip);
    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        auto v = 0.0f;
        if (! plays ({ place, &notes[i], &notes, i }, pass, v))
            continue;
        auto n = notes[i];
        n.velocity = v;
        n.chance = 1.0f;
        n.velocityRange = 0.0f;
        n.chanceGroup = 0;
        n.chanceOne = false;
        out.push_back (n);
    }
    return out;
}
} // namespace lynx::chance

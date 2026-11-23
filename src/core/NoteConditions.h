#pragma once

// Note Repeats and Conditions (round 190pm, research190oe P19; LYNX additions next to the note Chance of round 187c).
//
// What the others have (checked for this round): FL Studio 2026 has no per-note probability or condition (FLEngine's only
// "Note occurrence probability" is the Piano roll's Randomize tool, forms.prrandomform.noteprobwheel, which makes notes;
// its per-note repeat is the rate of issue #208, core/NoteRepeat.h). Live 12 has Chance and Velocity Deviation per note
// (round 187c) but no ratchets or conditions in a set. Bitwig 5's note operators have Repeats (a count over the note's
// length, with a velocity curve), Occurrence (first / not first / fill / not fill ...) and Recurrence (a cycle of 1-16
// passes with the passes it plays on); Elektron's trig conditions name the same as "A:B" (pass A of every B), FILL, 1ST.
// LYNX keeps one model for both namings: Note::condition (always / first / not first / fill / not fill) and a cycle with
// a mask of its passes, so "2:4" is the cycle 4 with pass 2 set and "2+4:4" plays on passes 2 and 4 of 4.
//
// Header only, no JUCE: the scheduler (audio thread, no allocation), the editors and the exporters share it.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "core/NoteRepeat.h"
#include "core/Song.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx::noteconditions
{
constexpr int maxCycle = 16;

// Does a note with a condition play on pass `pass` (0 = the first) of its loop, with Fill on or off?
constexpr bool plays (const Note& n, std::int64_t pass, bool fill) noexcept
{
    switch (n.condition)
    {
        case notecondition::firstPass: if (pass != 0) return false; break;
        case notecondition::notFirst: if (pass == 0) return false; break;
        case notecondition::fill: if (! fill) return false; break;
        case notecondition::notFill: if (fill) return false; break;
        default: break;
    }
    if (n.cycle > 1)
    {
        const auto length = (std::int64_t) std::min<int> (n.cycle, maxCycle);
        const auto i = (int) (((pass % length) + length) % length);
        if (((n.cycleMask >> i) & 1u) == 0)
            return false;
    }
    return true;
}

// The presets the Piano roll's Condition lane steps through (and the Note inspector's slider): Always, the four
// occurrence conditions, then Elektron's A:B for B = 2 … 8
struct Preset
{
    std::uint8_t condition = notecondition::always, cycle = 0;
    std::uint16_t mask = 0;
};

constexpr int maxPresetCycle = 8;
constexpr int presetCount = 5 + (maxPresetCycle * (maxPresetCycle + 1)) / 2 - 1; // 5 + 2 + 3 + ... + 8 = 40

constexpr Preset preset (int index) noexcept
{
    if (index <= 0)
        return {};
    if (index < 5)
        return { (std::uint8_t) index, 0, 0 };
    auto i = index - 5;
    for (int b = 2; b <= maxPresetCycle; ++b)
    {
        if (i < b)
            return { notecondition::always, (std::uint8_t) b, (std::uint16_t) (1u << i) };
        i -= b;
    }
    return {};
}

// The preset a note's condition is, -1 = none (a cycle with several passes, or a condition with a cycle)
constexpr int presetOf (const Note& n) noexcept
{
    if (! n.hasCondition())
        return 0;
    for (int i = 1; i < presetCount; ++i)
        if (const auto p = preset (i); p.condition == n.condition && (p.cycle > 1 ? p.cycle == n.cycle && p.mask == (n.cycleMask & ((1u << n.cycle) - 1u)) : n.cycle <= 1))
            return i;
    return -1;
}

inline void apply (Note& n, const Preset& p) noexcept
{
    n.condition = p.condition;
    n.cycle = p.cycle;
    n.cycleMask = p.cycle > 1 ? p.mask : 0;
}

// "2:4", "2+4:4" (the passes of the cycle a note plays on); "" without a cycle
inline std::string cycleText (int cycle, std::uint16_t mask)
{
    if (cycle <= 1)
        return {};
    std::string passes;
    for (int i = 0; i < std::min (cycle, maxCycle); ++i)
        if ((mask >> i) & 1u)
            passes += (passes.empty() ? "" : "+") + std::to_string (i + 1);
    return (passes.empty() ? std::string ("-") : passes) + ":" + std::to_string (cycle);
}

// "2:4", "2+4:4", "1,3/4", "2 4 of 4" -> the cycle and its mask; false (nothing changed) when it isn't one. "" = no cycle.
inline bool parseCycle (const std::string& text, std::uint8_t& cycle, std::uint16_t& mask)
{
    std::vector<int> numbers;
    int value = -1;
    for (const char c : text + " ")
    {
        if (c >= '0' && c <= '9')
            value = (value < 0 ? 0 : value * 10) + (c - '0');
        else if (value >= 0)
        {
            numbers.push_back (value);
            value = -1;
        }
        if (value > 999)
            return false;
    }
    if (numbers.empty())
    {
        if (text.find_first_not_of (" \t") != std::string::npos)
            return false;
        cycle = 0;
        mask = 0;
        return true;
    }
    if (numbers.size() < 2)
        return false;
    const auto length = numbers.back();
    if (length < 2 || length > maxCycle)
        return false;
    std::uint16_t bits = 0;
    for (std::size_t i = 0; i + 1 < numbers.size(); ++i)
    {
        if (numbers[i] < 1 || numbers[i] > length)
            return false;
        bits = (std::uint16_t) (bits | (1u << (numbers[i] - 1)));
    }
    cycle = (std::uint8_t) length;
    mask = bits;
    return true;
}

// The English name of a condition (the UI translates the words; the cycle text stays as it is)
inline const char* occurrenceName (int condition) noexcept
{
    switch (condition)
    {
        case notecondition::firstPass: return TR_NOOP ("First pass");
        case notecondition::notFirst: return TR_NOOP ("Not first");
        case notecondition::fill: return TR_NOOP ("Fill");
        case notecondition::notFill: return TR_NOOP ("Not fill");
        default: return TR_NOOP ("Always");
    }
}

// The strikes a note plays as notes of their own (exports to formats without ratchets: .als, .mid, an .flp note FL's
// rates can't hold): its ratchets with their velocities, FL's repeat and every other field kept. A note without ratchets
// comes back as it is.
inline std::vector<Note> ratchetNotes (const Note& n)
{
    std::vector<Note> out;
    const auto count = noterepeat::ratchetCount (n);
    if (count <= 1 || n.length < count)
    {
        out.push_back (n);
        return out;
    }
    noterepeat::forEachStrike (n, n.start, n.length, [&] (std::int64_t at, std::int64_t length, int k) {
        auto s = n;
        s.start = at;
        s.length = length;
        s.velocity = noterepeat::strikeVelocity (n.velocity, noterepeat::strikeGain (n, k, count));
        s.ratchets = 0;
        s.ratchetVelocity = 0.0f;
        out.push_back (s);
    });
    return out;
}

// The notes with every ratchet made notes of their own, sorted by start (a stable sort: the order of notes that start
// together stays); `baked` counts the notes that had ratchets
inline std::vector<Note> withRatchetsAsNotes (const std::vector<Note>& notes, int* baked = nullptr)
{
    std::vector<Note> out;
    out.reserve (notes.size());
    int count = 0;
    for (const auto& n : notes)
    {
        if (noterepeat::ratchetCount (n) > 1 && n.length >= noterepeat::ratchetCount (n))
        {
            ++count;
            for (auto& s : ratchetNotes (n))
                out.push_back (s);
        }
        else
            out.push_back (n);
    }
    if (count > 0)
        std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    if (baked != nullptr)
        *baked = count;
    return out;
}

// FL's repeat rate (1 … 14) that plays a note's ratchets exactly — even strikes of a rate in FL's table that fill the
// note, no velocity bend; 0 = none (the .flp export then writes the strikes as notes)
constexpr int flRateFor (const Note& n) noexcept
{
    const auto count = noterepeat::ratchetCount (n);
    if (count <= 1 || n.ratchetVelocity != 0.0f || n.length % count != 0)
        return 0;
    for (int rate = 1; rate <= noterepeat::rateCount; ++rate)
        if (noterepeat::interval (rate) == n.length / count)
            return rate;
    return 0;
}
} // namespace lynx::noteconditions

#include "core/ChordReadout.h"

#include <algorithm>
#include <cstring>
#include <numeric>

namespace lynx::chordread
{
namespace
{
constexpr int gap = -1; // a degree left out of the family (it keeps its place in the ranking)

struct Family
{
    const char* suffix;      // after a letter name
    const char* romanSuffix; // after a numeral
    bool lowerNumeral;
    double likelihood;
    std::array<int, 10> degrees; // semitones above the root, most telling first; -2 ends the list
};

constexpr int end = -2;
const std::array<Family, 7> families { {
    { "", "", false, 1.0, { 0, 4, 7, 10, 2, 11, 9, 6, 1, 5 } },
    { "m", "", true, 0.99, { 0, 3, 7, 10, 2, 5, 8, 6, 9, end } },
    { "dim", "\xE1\xB5\x92", true, 0.7, { 0, 6, 3, 9, 2, 5, 8, end, end, end } }, // ᵒ
    { "sus4", "sus4", false, 0.6, { 0, 5, 7, gap, 10, 2, 9, end, end, end } },
    { "sus2", "sus2", false, 0.59, { 0, 2, 7, gap, 10, 5, 9, end, end, end } },
    { "aug", "aug", false, 0.5, { 0, 4, 8, gap, 10, 2, 5, end, end, end } },
    { "\xC3\xB8", "\xC3\xB8", true, 0.48, { 0, 6, 3, 10, 1, 5, 8, end, end, end } }, // ø
} };
constexpr int dimFamily = 2, halfDimFamily = 6;

constexpr std::array<double, 10> rankWeight { 1.0, 0.75, 0.75, 0.39, 0.3, 0.25, 0.16, 0.13, 0.11, 0.1 };

// (single-precision literals on purpose: the scores must tie and break the same way as the measured values)
constexpr double lowKeyBoost = 1.11f, extraOctaveShare = 0.9f, rootInBassBoost = 1.14f, withoutBassShare = 0.06f;
constexpr double likelihoodSpread = 0.2f;

const std::array<const char*, 12> sharpNames { "C", "C\xE2\x99\xAF", "D", "D\xE2\x99\xAF", "E", "F", "F\xE2\x99\xAF", "G", "G\xE2\x99\xAF", "A", "A\xE2\x99\xAF", "B" };
const std::array<const char*, 12> flatNames { "C", "D\xE2\x99\xAD", "D", "E\xE2\x99\xAD", "E", "F", "G\xE2\x99\xAD", "G", "A\xE2\x99\xAD", "A", "B\xE2\x99\xAD", "B" };
const std::array<const char*, 12> numerals { "I", "\xE2\x99\xADII", "II", "\xE2\x99\xADIII", "III", "IV", "\xE2\x99\xADV", "V", "\xE2\x99\xADVI", "VI", "\xE2\x99\xADVII", "VII" };
const std::array<const char*, 13> intervals { "", "m 2nd", "M 2nd", "m 3rd", "M 3rd", "4th", "Tritone", "5th", "m 6th", "M 6th", "m 7th", "M 7th", "Octave" };

const char* const delta = "\xE2\x88\x86"; // ∆

struct Tension
{
    const char* text;
    int number;
    int accidental; // sorts after the plain one: none, flat, sharp
};

// semitones above the root, spelled without a seventh in the chord and with one
const std::array<Tension, 12> plainTensions { {
    { "", 0, 0 }, { "\xE2\x99\xAD" "9", 9, 1 }, { "9", 9, 0 }, { "\xE2\x99\xAF" "9", 9, 2 }, { "\xE2\x99\xAD" "4", 4, 1 }, { "4", 4, 0 },
    { "\xE2\x99\xAF" "4", 4, 2 }, { "5", 5, 0 }, { "\xE2\x99\xAD" "6", 6, 1 }, { "6", 6, 0 }, { "", 0, 0 }, { "", 0, 0 },
} };
const std::array<Tension, 12> seventhTensions { {
    { "", 0, 0 }, { "\xE2\x99\xAD" "9", 9, 1 }, { "9", 9, 0 }, { "\xE2\x99\xAF" "9", 9, 2 }, { "\xE2\x99\xAD" "11", 11, 1 }, { "11", 11, 0 },
    { "\xE2\x99\xAF" "11", 11, 2 }, { "5", 5, 0 }, { "\xE2\x99\xAD" "13", 13, 1 }, { "13", 13, 0 }, { "\xE2\x99\xAF" "13", 13, 2 }, { "\xE2\x99\xAF\xE2\x99\xAF" "13", 13, 2 },
} };
const std::array<const char*, 9> noveltyList { "Too many notes", "Free jazz", "Spicy", "Who knows?", "Out there", "Wild", "Hands full", "Anything goes", "Cluster" };

using Strengths = std::array<double, 12>;

// One pitch class's weight: its strongest key counts in full, its other octaves a little less.
Strengths strengthsOf (const std::vector<int>& keys, int lowKey, bool leaveOutLowKey)
{
    std::array<double, 12> strongest {}, others {};
    for (const auto key : keys)
    {
        if (leaveOutLowKey && key == lowKey)
            continue;
        const auto value = key == lowKey ? lowKeyBoost : 1.0;
        auto& top = strongest[(std::size_t) (key % 12)];
        others[(std::size_t) (key % 12)] += std::min (top, value);
        top = std::max (top, value);
    }
    Strengths s {};
    for (std::size_t i = 0; i < 12; ++i)
        s[i] = strongest[i] + others[i] * extraOctaveShare;
    return s;
}

double fit (const Strengths& strengths, const Family& family, int root)
{
    Strengths weights {};
    for (std::size_t rank = 0; rank < family.degrees.size() && family.degrees[rank] != end; ++rank)
        if (family.degrees[rank] != gap)
            weights[(std::size_t) ((family.degrees[rank] + root) % 12)] = rankWeight[rank];
    const auto match = std::inner_product (weights.begin(), weights.end(), strengths.begin(), 0.0);
    return match * ((family.likelihood - 0.5) * likelihoodSpread + 1.0);
}

// The two lowest keys below middle C (key 60 and up count as 60): a wide gap between them makes the lowest a bass.
struct LowKeys
{
    int lowest = 60, gapAbove = 0;
};
LowKeys lowKeysOf (const std::vector<int>& keys)
{
    int first = 60, second = 60;
    for (const auto key : keys)
    {
        if (key < first)
        {
            second = first;
            first = key;
        }
        else if (key < second)
            second = key;
    }
    return { first, second - first };
}

std::string spellTensions (std::vector<int> above, int familyIndex, bool& novelty)
{
    auto take = [&above] (int semitones) {
        const auto it = std::find (above.begin(), above.end(), semitones);
        if (it == above.end())
            return false;
        above.erase (it);
        return true;
    };
    auto has = [&above] (int semitones) { return std::find (above.begin(), above.end(), semitones) != above.end(); };

    std::string lead;
    auto seventh = false;
    if (take (11))
    {
        lead = delta;
        seventh = true;
    }
    else if (familyIndex == dimFamily && has (9))
        seventh = take (9);
    else if (has (10))
        seventh = take (10);
    else if (! above.empty())
        lead = "add";

    const auto& table = seventh ? seventhTensions : plainTensions;
    std::vector<Tension> parts;
    if (seventh)
        parts.push_back ({ "7", 7, 0 });
    for (const auto semitones : above)
        parts.push_back (table[(std::size_t) semitones]);
    std::sort (parts.begin(), parts.end(), [] (const Tension& a, const Tension& b) {
        return a.number != b.number ? a.number < b.number : a.accidental < b.accidental;
    });

    // a stack of thirds says only its top: 7 9 11 is "11"
    if (seventh)
        for (const auto top : { 13, 11, 9 })
        {
            static constexpr int stack[] { 7, 9, 11, 13 };
            const auto length = top == 13 ? 4u : top == 11 ? 3u : 2u;
            auto whole = parts.size() >= length;
            for (std::size_t i = 0; whole && i < length; ++i)
                whole = parts[i].number == stack[i] && parts[i].accidental == 0;
            if (whole)
            {
                parts.erase (parts.begin(), parts.begin() + (std::ptrdiff_t) length - 1);
                break;
            }
        }

    if (parts.size() >= 5)
    {
        novelty = true;
        return {};
    }
    if (parts.size() == 4)
    {
        // room for three: a plain number goes first
        const auto plain = std::find_if (parts.begin(), parts.end(), [] (const Tension& t) { return t.accidental == 0; });
        parts.erase (plain != parts.end() ? plain : parts.begin());
    }

    auto text = lead;
    auto endsInDigit = false;
    for (const auto& part : parts)
    {
        if (endsInDigit && part.accidental == 0)
            text += ',';
        text += part.text;
        endsInDigit = true; // (every spelling ends in its number)
    }
    return text;
}

void replaceFirst (std::string& s, const std::string& from, const std::string& to)
{
    if (const auto at = s.find (from); at != std::string::npos)
        s.replace (at, from.size(), to);
}

bool endsWith (const std::string& s, const std::string& tail)
{
    return s.size() >= tail.size() && s.compare (s.size() - tail.size(), tail.size(), tail) == 0;
}
} // namespace

const std::array<const char*, 9>& noveltyNames()
{
    return noveltyList;
}

KeysName nameKeys (const std::vector<int>& keys, const NameOptions& options)
{
    if (keys.size() < 2)
        return {};

    if (keys.size() == 2)
    {
        const auto span = std::abs (keys[0] - keys[1]);
        if (span == 0)
            return {};
        const auto within = span % 12 == 0 ? 12 : span % 12;
        return { std::string (intervals[(std::size_t) within]) + (span > 12 ? " +" : ""), false };
    }

    const auto low = lowKeysOf (keys);
    const auto hasBass = low.gapAbove > 6;
    const auto bassClass = hasBass ? low.lowest % 12 : -1;
    const auto all = strengthsOf (keys, hasBass ? low.lowest : -1, false);
    const auto rest = strengthsOf (keys, hasBass ? low.lowest : -1, true);

    int bestRoot = 0, bestFamily = 0;
    auto bestScore = -1.0;
    for (int root = 0; root < 12; ++root)
        for (int f = 0; f < (int) families.size(); ++f)
        {
            auto score = fit (all, families[(std::size_t) f], root);
            if (hasBass)
                score = root == bassClass ? score * rootInBassBoost
                                          : score + fit (rest, families[(std::size_t) f], root) * withoutBassShare;
            if (score > bestScore)
            {
                bestScore = score;
                bestRoot = root;
                bestFamily = f;
            }
        }

    const auto& family = families[(std::size_t) bestFamily];
    auto noteName = [&options] (int pitchClass) -> std::string {
        if (options.notes != nullptr)
            return (*options.notes)[(std::size_t) pitchClass];
        return (options.sharps ? sharpNames : flatNames)[(std::size_t) pitchClass];
    };

    std::string name;
    if (options.roman && options.tonic >= 0)
    {
        name = numerals[(std::size_t) ((bestRoot - options.tonic + 12) % 12)];
        if (family.lowerNumeral)
            for (auto& c : name)
                if (c >= 'A' && c <= 'Z')
                    c = (char) (c - 'A' + 'a');
        name += family.romanSuffix;
    }
    else
        name = noteName (bestRoot) + family.suffix;

    // what the keys add to the family's first three notes (the bass stands apart)
    std::vector<int> above;
    for (const auto key : keys)
    {
        if (hasBass && key == low.lowest)
            continue;
        const auto semitones = (key % 12 - bestRoot + 12) % 12;
        const auto inTriad = std::any_of (family.degrees.begin(), family.degrees.begin() + 3, [semitones] (int d) { return d == semitones; });
        if (! inTriad && std::find (above.begin(), above.end(), semitones) == above.end())
            above.push_back (semitones);
    }

    auto novelty = false;
    const auto tensions = spellTensions (std::move (above), bestFamily, novelty);
    if (novelty)
    {
        // (any of them will do; the same keys keep the same one)
        const auto seed = std::accumulate (keys.begin(), keys.end(), 0);
        return { noveltyList[(std::size_t) seed % noveltyList.size()], true };
    }

    if (! tensions.empty())
        name += " " + tensions;

    // the usual shorthands
    if (endsWith (name, std::string (delta) + "7"))
        name.pop_back();
    else if (endsWith (name, std::string (families[halfDimFamily].suffix) + " 7"))
        name.resize (name.size() - 2);
    replaceFirst (name, "9,13", "13");
    if (const auto sus = name.find ("sus"); sus != std::string::npos && sus + 4 < name.size())
    {
        const auto word = name.substr (sus, 4);
        name.erase (sus, 4);
        name += word;
    }

    if (hasBass && bestRoot != bassClass)
        name += "/" + noteName (bassClass);
    return { name, false };
}

KeysName Readout::describe (const std::vector<int>& k) const
{
    if ((int) k.size() < minKeys || (int) k.size() > maxKeys)
        return { noChord, false };
    return nameKeys (k, options);
}

void Readout::setDelayMs (std::uint32_t ms)
{
    delayMs = ms;
    holdMs = 0;
}

void Readout::note (int channel, int key, bool on, std::uint32_t nowMs)
{
    if (channel < 0 || channel >= channels)
        return;
    auto& held = keys[(std::size_t) channel];
    const auto at = std::find (held.begin(), held.end(), key);
    if (on && at == held.end())
        held.push_back (key);
    else if (! on && at != held.end())
        held.erase (at);

    auto name = describe (held);
    holdMs = std::max<std::uint32_t> (100, delayMs);
    if (on)
    {
        shown = std::move (name.text);
        shownNovelty = name.novelty;
        waiting = false;
    }
    else
    {
        next = std::move (name.text);
        waitingNovelty = name.novelty;
        waiting = true;
        sinceMs = nowMs;
    }
}

void Readout::selection (std::vector<int> selected, std::uint32_t nowMs)
{
    std::sort (selected.begin(), selected.end());
    selected.erase (std::unique (selected.begin(), selected.end()), selected.end());
    auto name = (int) selected.size() >= minKeys && (int) selected.size() <= maxKeys ? nameKeys (selected, options) : KeysName {};
    shown = std::move (name.text);
    shownNovelty = name.novelty;
    next = noChord;
    waitingNovelty = false;
    waiting = true;
    sinceMs = nowMs;
    holdMs = std::max<std::uint32_t> (2000, delayMs);
}

bool Readout::tick (std::uint32_t nowMs)
{
    if (! waiting || nowMs - sinceMs < holdMs)
        return false;
    waiting = false;
    const auto changed = shown != next;
    shown = next;
    shownNovelty = waitingNovelty;
    return changed;
}
} // namespace lynx::chordread

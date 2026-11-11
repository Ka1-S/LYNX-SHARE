#pragma once

// Ableton Live 12's scale list (round 190o, GAPMAP #9 Scale Mode), for the .als round trip of the project key.
// A Live 12 set stores the scale as an index into the list its Scale choosers show: LiveSet/ScaleInformation { Root
// 0 = C … 11, Name = index } + LiveSet/InKey (Scale Mode on), and the same pair per clip (MidiClip/ScaleInformation +
// IsInKey). Live 11 stores the name as text and has clip scales only.
// [LIVE 12.4 save] /mnt/e/lynx-work/ableton190o/live/mt-saved Project: a clip set to C Bhairav is saved as Root 0,
// Name 22, IsInKey true, and the set's own pair follows it (the set remembers the scale last chosen). So the index is
// the chooser's order (… Super Locrian 20, 8-Tone Spanish 21, Bhairav 22, Hungarian Minor 23 …) — not the order of the
// Live API's help text [BIN], which lists Bhairav and Hungarian Minor before 8-Tone Spanish. The chooser's tail was
// read off a screenshot (Lydian Dominant … Messiaen 7); the head is the help text's order, which agrees with it.
// The degrees are the scales' textbook definitions (Bhairav's checked against Live's key highlight: C E F G in, D A
// out). Header only, no JUCE.

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <vector>

#include "core/ProjectKey.h"

namespace lynx::livescales
{
struct Scale
{
    const char* name;
    std::vector<int> intervals;
};

inline const std::vector<Scale>& scales()
{
    static const std::vector<Scale> list {
        { "Major", { 0, 2, 4, 5, 7, 9, 11 } },
        { "Minor", { 0, 2, 3, 5, 7, 8, 10 } },
        { "Dorian", { 0, 2, 3, 5, 7, 9, 10 } },
        { "Mixolydian", { 0, 2, 4, 5, 7, 9, 10 } },
        { "Lydian", { 0, 2, 4, 6, 7, 9, 11 } },
        { "Phrygian", { 0, 1, 3, 5, 7, 8, 10 } },
        { "Locrian", { 0, 1, 3, 5, 6, 8, 10 } },
        { "Whole Tone", { 0, 2, 4, 6, 8, 10 } },
        { "Half-whole Dim.", { 0, 1, 3, 4, 6, 7, 9, 10 } },
        { "Whole-half Dim.", { 0, 2, 3, 5, 6, 8, 9, 11 } },
        { "Minor Blues", { 0, 3, 5, 6, 7, 10 } },
        { "Minor Pentatonic", { 0, 3, 5, 7, 10 } },
        { "Major Pentatonic", { 0, 2, 4, 7, 9 } },
        { "Harmonic Minor", { 0, 2, 3, 5, 7, 8, 11 } },
        { "Harmonic Major", { 0, 2, 4, 5, 7, 8, 11 } },
        { "Dorian #4", { 0, 2, 3, 6, 7, 9, 10 } },
        { "Phrygian Dominant", { 0, 1, 4, 5, 7, 8, 10 } },
        { "Melodic Minor", { 0, 2, 3, 5, 7, 9, 11 } },
        { "Lydian Augmented", { 0, 2, 4, 6, 8, 9, 11 } },
        { "Lydian Dominant", { 0, 2, 4, 6, 7, 9, 10 } },
        { "Super Locrian", { 0, 1, 3, 4, 6, 8, 10 } },
        { "8-Tone Spanish", { 0, 1, 3, 4, 5, 6, 8, 10 } },
        { "Bhairav", { 0, 1, 4, 5, 7, 8, 11 } },
        { "Hungarian Minor", { 0, 2, 3, 6, 7, 8, 11 } },
        { "Hirajoshi", { 0, 2, 3, 7, 8 } },
        { "In-Sen", { 0, 1, 5, 7, 10 } },
        { "Iwato", { 0, 1, 5, 6, 10 } },
        { "Kumoi", { 0, 2, 3, 7, 9 } },
        { "Pelog Selisir", { 0, 1, 3, 7, 8 } },
        { "Pelog Tembung", { 0, 1, 5, 7, 8 } },
        { "Messiaen 3", { 0, 2, 3, 4, 6, 7, 8, 10, 11 } },
        { "Messiaen 4", { 0, 1, 2, 5, 6, 7, 8, 11 } },
        { "Messiaen 5", { 0, 1, 5, 6, 7, 11 } },
        { "Messiaen 6", { 0, 2, 4, 5, 6, 8, 10, 11 } },
        { "Messiaen 7", { 0, 1, 2, 3, 5, 6, 7, 8, 9, 11 } },
    };
    return list;
}

// Live's index of a scale name (Live 11 sets store the name); -1 = not one of Live's.
inline int indexOfName (std::string_view name) noexcept
{
    const auto& list = scales();
    for (int i = 0; i < (int) list.size(); ++i)
        if (name == list[(std::size_t) i].name)
            return i;
    return -1;
}

// The project key with the same degrees as Live's scale `index` (FL's reference list holds the key: the first FL scale
// with exactly these degrees); nullopt when Live's index is unknown or FL's list has no such scale.
inline std::optional<projectkey::Key> keyFor (int root, int index)
{
    const auto& list = scales();
    if (index < 0 || index >= (int) list.size())
        return std::nullopt;
    const auto& fl = notetools::flReferenceScales();
    for (int i = 0; i < (int) fl.size(); ++i)
        if (fl[(std::size_t) i].intervals == list[(std::size_t) index].intervals)
            return projectkey::Key { ((root % 12) + 12) % 12, i };
    return std::nullopt;
}

// Live's scale index for a project key (the same degrees); -1 when Live has no such scale.
inline int indexFor (const projectkey::Key& key)
{
    const auto& d = projectkey::intervals (key);
    if (d.empty())
        return -1;
    const auto& list = scales();
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(std::size_t) i].intervals == d)
            return i;
    return -1;
}
} // namespace lynx::livescales

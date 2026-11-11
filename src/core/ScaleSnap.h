#pragma once

// Snap to scale (round 190an; LYNX's own option, off by default: FL's Piano roll only highlights the scale and its
// free drawing stays the default). One place for what the option means, for every note editor front-end (the FL-style
// Piano roll and the Clip View's note editor share it):
//  - a drawn note lands on the in-scale key nearest to the mouse (a tie goes down);
//  - moved notes (a drag, a clone dropped by a drag, a freshly stamped chord still held) move by scale steps: the
//    grabbed note follows the mouse onto the scale, and every other note moves the same number of scale steps, so a
//    triad stays a triad of the scale (Bitwig 6's Snap to Key and Live 12's scale-degree transpose);
//  - the arrow keys' "one up / one down" is one scale step;
//  - notes that are already off the scale are left alone until they are moved in pitch: a drag along the time axis,
//    a resize, a paste or switching the option on never changes them (Bitwig keeps "Quantize to Key" a separate
//    command; LYNX's is Piano roll menu › Move the notes onto the scale). An off-scale note that is moved in pitch
//    reaches the scale with its first step.
// Header only, no JUCE. Tests: tests/ScaleSnap190anTests.inc.

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "core/NoteTools.h"
#include "core/Song.h"

namespace lynx::scalesnap
{
struct Scale
{
    int root = 0;    // 0 = C … 11 = B
    int scale = 0;   // index into notetools::scales(); 0 = none
    bool on = false; // the Snap to scale option
    bool active() const noexcept { return on && scale > 0 && scale < (int) notetools::scales().size(); }
};

// The key of a note drawn at `key`.
inline int drawKey (const Scale& s, int key) noexcept
{
    return s.active() ? notetools::snapToScale (key, s.root, s.scale) : key;
}

// The scale steps from `from` up (+) or down (−) to `to`: the in-scale keys passed or reached on the way. `from`
// itself never counts, so an off-scale key is one step away from each of its in-scale neighbours.
inline int stepsBetween (const Scale& s, int from, int to) noexcept
{
    if (! s.active())
        return to - from;
    int steps = 0;
    for (int k = std::min (from, to) + 1; k <= std::max (from, to); ++k)
    {
        const auto passed = to > from ? k : k - 1; // going down: the keys from − 1 … to
        if (passed >= 0 && passed <= 127 && notetools::inScale (passed, s.root, s.scale))
            ++steps;
    }
    return to > from ? steps : -steps;
}

// `key` moved by `steps` scale steps; stops at the last in-scale key of the keyboard. `done` (if given) gets the
// steps really made.
inline int stepKey (const Scale& s, int key, int steps, int* done = nullptr) noexcept
{
    int made = 0;
    const auto direction = steps > 0 ? 1 : -1;
    for (int i = 0; i < std::abs (steps); ++i)
    {
        const auto next = notetools::stepOnScale (key, direction, s.root, s.active() ? s.scale : 0);
        if (next == key)
            break;
        key = next;
        made += direction;
    }
    if (done != nullptr)
        *done = made;
    return key;
}

// A drag: the scale steps for the whole group when the note grabbed at `grabbedKey` is dragged to the row `mouseKey`.
// 0 while the mouse is on the note's own row (a move along the time axis leaves the pitch alone, also off the scale)
// and while the nearest in-scale key is the grabbed note's own.
inline int dragSteps (const Scale& s, int grabbedKey, int mouseKey) noexcept
{
    if (mouseKey == grabbedKey)
        return 0;
    if (! s.active())
        return mouseKey - grabbedKey;
    const auto target = notetools::snapToScale (mouseKey, s.root, s.scale);
    return target == grabbedKey ? 0 : stepsBetween (s, grabbedKey, target);
}

// Moves the notes `indices` by `steps` scale steps (semitones without an active scale). When a note would leave the
// keyboard the whole group stops one step earlier, so it keeps its shape. Returns the steps made.
inline int moveBySteps (Pattern& pattern, const std::vector<int>& indices, int steps, const Scale& s)
{
    if (steps == 0)
        return 0;
    auto possible = steps;
    for (const auto i : indices)
        if (i >= 0 && i < (int) pattern.notes.size())
        {
            int done = 0;
            stepKey (s, pattern.notes[(std::size_t) i].key, steps, &done);
            if (std::abs (done) < std::abs (possible))
                possible = done;
        }
    if (possible != 0)
        for (const auto i : indices)
            if (i >= 0 && i < (int) pattern.notes.size())
                pattern.notes[(std::size_t) i].key = stepKey (s, pattern.notes[(std::size_t) i].key, possible);
    return possible;
}
} // namespace lynx::scalesnap

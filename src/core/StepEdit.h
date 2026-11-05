#pragma once

// Round 170e3: FL's "Step edit" (Options › Step edit, Ctrl+E, toolbar StepEditBtn [DFM]; the action hands its
// checked state to the editor [DECOMP 0xf1e740], REA_rodada169k #4). With the transport stopped, notes played on a MIDI
// keyboard or the typing keyboard are written into the current pattern at the position, one snap step long; when every
// key is released the position moves on by the snap step, so a chord lands on one step and the next note on the next.
// No JUCE: the UI feeds it the played notes and moves the song position to `position` afterwards.

#include "core/Song.h"

#include <algorithm>
#include <cstdint>

namespace lynx::stepedit
{
struct Played
{
    int channel = 0;
    int key = 60;
    float velocity = 0.78f;
    bool on = true;
};

class Writer
{
public:
    // A new position (seek, or step edit turned on): forgets the keys held so far.
    void reset (std::int64_t at) noexcept
    {
        position = std::max<std::int64_t> (0, at);
        held = 0;
    }

    std::int64_t getPosition() const noexcept { return position; }
    int heldKeys() const noexcept { return held; }

    // Applies one played note to `pattern`; returns true when the position moved on (every key released).
    bool play (const Played& p, Pattern& pattern, std::int64_t step)
    {
        step = std::max<std::int64_t> (1, step);
        if (p.on)
        {
            Note n;
            n.start = position;
            n.length = step;
            n.channel = p.channel;
            n.key = std::clamp (p.key, 0, 127);
            n.velocity = std::clamp (p.velocity, 0.0f, 1.0f);
            // the same key again on this step replaces the note instead of stacking a second one
            auto& notes = pattern.notes;
            notes.erase (std::remove_if (notes.begin(), notes.end(),
                                         [&n] (const Note& o) { return o.channel == n.channel && o.key == n.key && o.start == n.start; }),
                         notes.end());
            const auto at = std::upper_bound (notes.begin(), notes.end(), n.start, [] (std::int64_t t, const Note& o) { return t < o.start; });
            notes.insert (at, n);
            ++held;
            return false;
        }
        if (held <= 0)
            return false; // a key that was held before step edit started
        if (--held > 0)
            return false;
        position += step;
        return true;
    }

private:
    std::int64_t position = 0;
    int held = 0;
};
} // namespace lynx::stepedit

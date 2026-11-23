#pragma once

// Round gap190og: three Clip View commands of the reference DAW on a MIDI clip of the clip grid (Clip-launch layout).
//
// Duplicate Loop (coverage live1:detail-019) [DOC Live 12 manual 8.9 "Looping Clips": "You can quickly duplicate the
// loop's length and its contents via the Edit menu's Duplicate Loop command. In MIDI clips, any notes to the right of the
// loop will be moved, so that they maintain their position relative to the end of the loop."; SHOT
// /mnt/e/lynx-work/ableton187c/shots/live-01-velocity-lane.png: the Clip panel's "Duplicate" button under Start / End;
// STR live-exe-strings-u16.txt 11787-11799: mpDuplicateLoopBang, DuplicateLoop in the ClipRegionPanel].
//
// Crop Clip (coverage live1:detail-020) [DOC Live 12 manual 8.11: "Crop Clip is used when cropping to the region between
// start and end marker (in unlooped clips) or the start/loop start marker and loop end marker (in looped clips)"; 10.7.1:
// "MIDI data that is outside of the loop brace can be deleted using the Crop Clip command"; SHOT
// /mnt/e/lynx-work/details2_190ld/shots/ref/live-clip-menu.png: "Crop Clip  Ctrl+Shift+J" in the clip's menu].
//
// Randomize (coverage live1:detail-047) [DOC Live 12 manual 10.5.12: "Velocity values for selected notes (or notes with
// selected markers) can be randomized by clicking on the Randomize button. If no notes or markers are selected, values
// for all notes will be randomized." "Velocities will be randomly increased or decreased by a value between zero and the
// number shown in the slider."; SHOT ableton187c/shots/live-01-velocity-lane.png: the Velocity lane's footer
// "Velocity  Randomize [100]"].
//
// LYNX's grid clip: with Loop on, Clip::offset / length are the loop brace and ClipGridSlot::startMarker sits inside it
// (core/ClipBrace.h); with Loop off they are the Start / End markers. The clip's notes are its pattern's (ticks from the
// pattern's start). JUCE-free, so the tests check the model.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "core/Song.h"

namespace lynx::cliploop
{
// The span a MIDI clip plays: [from, to) in pattern ticks (the loop brace with Loop on, Start..End with it off). The
// Start marker never lies before the loop start in LYNX (ClipGridSlot::startMarker >= 0), so the reference's "start / loop
// start marker, whichever comes first" is the loop start.
struct Span
{
    std::int64_t from = 0, to = 0;
    std::int64_t length() const noexcept { return to - from; }
};
inline Span spanOf (const Clip& c) noexcept
{
    const auto from = std::max<std::int64_t> (0, c.offset);
    return { from, from + std::max<std::int64_t> (1, c.length) };
}

inline bool canEdit (const Clip& c, const Song& song) noexcept
{
    return c.kind == ClipKind::pattern && c.length > 0 && c.pattern >= 0 && c.pattern < (int) song.patterns.size();
}

namespace detail
{
inline void sortEvents (Pattern& p)
{
    for (auto& lane : p.events)
    {
        std::stable_sort (lane.events.begin(), lane.events.end(), [] (const PatternEvent& a, const PatternEvent& b) { return a.tick < b.tick; });
        // one event per tick: the one written last wins
        std::vector<PatternEvent> kept;
        for (const auto& e : lane.events)
        {
            if (! kept.empty() && kept.back().tick == e.tick)
                kept.back() = e;
            else
                kept.push_back (e);
        }
        lane.events = std::move (kept);
    }
}
inline void sortNotes (Pattern& p)
{
    std::stable_sort (p.notes.begin(), p.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
}
} // namespace detail

// Duplicate Loop: the loop's notes (those that start in it) and pattern events are copied right after it, everything to
// the right of the loop moves on by the loop's length, and the loop (or Start..End) becomes twice as long. The Start
// marker stays where it is. Returns false (nothing changed) for a clip that is not a MIDI clip.
inline bool duplicateLoop (Song& song, Clip& c)
{
    if (! canEdit (c, song))
        return false;
    auto& p = song.patterns[(std::size_t) c.pattern];
    const auto span = spanOf (c);
    const auto shift = span.length();
    std::vector<Note> copies;
    for (auto& n : p.notes)
    {
        if (n.start >= span.to)
            n.start += shift;
        else if (n.start >= span.from)
        {
            copies.push_back (n);
            copies.back().start += shift;
        }
    }
    p.notes.insert (p.notes.end(), copies.begin(), copies.end());
    detail::sortNotes (p);
    for (auto& lane : p.events)
    {
        std::vector<PatternEvent> eventCopies;
        for (auto& e : lane.events)
        {
            if (e.tick >= span.to)
                e.tick += shift;
            else if (e.tick >= span.from)
                eventCopies.push_back ({ e.tick + shift, e.value });
        }
        lane.events.insert (lane.events.end(), eventCopies.begin(), eventCopies.end());
    }
    detail::sortEvents (p);
    if (p.lengthTicks > 0)
        p.lengthTicks = std::max (p.lengthTicks, span.to) + shift;
    c.offset = span.from;
    c.length = 2 * shift;
    return true;
}

// Crop Clip: the MIDI data outside the clip's span goes — notes that start before it or at / after its end, and the
// pattern events outside it; a note that runs past the end is cut there. The span then starts the pattern (the clip's
// region keeps its length and the Start marker its place in it), as a cropped audio clip's new sample does ("Cropping
// MIDI clips works in the same way as cropping audio clips"). Returns false for a clip that is not a MIDI clip.
inline bool cropClip (Song& song, Clip& c)
{
    if (! canEdit (c, song))
        return false;
    auto& p = song.patterns[(std::size_t) c.pattern];
    const auto span = spanOf (c);
    std::vector<Note> kept;
    kept.reserve (p.notes.size());
    for (auto n : p.notes)
    {
        if (n.start < span.from || n.start >= span.to)
            continue;
        n.length = std::min (n.length, span.to - n.start);
        n.start -= span.from;
        kept.push_back (n);
    }
    p.notes = std::move (kept);
    detail::sortNotes (p);
    for (auto& lane : p.events)
    {
        std::vector<PatternEvent> keptEvents;
        for (const auto& e : lane.events)
            if (e.tick >= span.from && e.tick < span.to)
                keptEvents.push_back ({ e.tick - span.from, e.value });
        lane.events = std::move (keptEvents);
    }
    p.lengthTicks = span.length();
    c.offset = 0;
    c.length = span.length();
    return true;
}

// The Velocity lane's Randomize: every note of `indices` (the selection, or all the notes edited when nothing is
// selected) moves up or down by a whole number of velocity steps between 0 and `amount` (0…127, the reference's 1…127
// scale), staying within 1…127.
inline void randomizeVelocity (Pattern& p, const std::vector<int>& indices, int amount, std::uint32_t seed)
{
    amount = std::clamp (amount, 0, 127);
    if (amount == 0)
        return;
    std::mt19937 random (seed);
    std::uniform_int_distribution<int> by (-amount, amount);
    for (const auto i : indices)
    {
        if (i < 0 || i >= (int) p.notes.size())
            continue;
        auto& n = p.notes[(std::size_t) i];
        const auto steps = (int) std::lround (std::clamp (n.velocity, 0.0f, 1.0f) * 127.0f);
        n.velocity = (float) std::clamp (steps + by (random), 1, 127) / 127.0f;
    }
}
} // namespace lynx::cliploop

#pragma once

// Small read-only measurements of a Song for the toolbar's meters (no JUCE).

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/Song.h"

namespace lynx::songstats
{
// Notes of `pattern` that sound at `localTick` (a step note lasts at least one step).
inline int notesAt (const Song& song, const Pattern& pattern, double localTick)
{
    int count = 0;

    for (const auto& n : pattern.notes)
    {
        if ((double) n.start > localTick)
            break; // sorted by start

        const auto end = (double) (n.start + std::max<std::int64_t> (n.length, ticksPerStep));

        if (! n.muted && localTick < end && n.channel >= 0 && n.channel < (int) song.channels.size()
            && ! song.channels[(std::size_t) n.channel].muted)
            ++count;
    }

    return count;
}

// How many notes the song plays at `tick` (the toolbar's polyphony meter): in pattern mode the current
// pattern (looped), in song mode every unmuted pattern clip under the playhead plus one per audio clip.
// Voices that are still releasing are not counted.
inline int soundingNotes (const Song& song, double tick)
{
    if (song.patterns.empty() || tick < 0.0)
        return 0;

    if (song.mode == PlayMode::pattern)
    {
        const auto current = (std::size_t) std::clamp (song.currentPattern, 0, (int) song.patterns.size() - 1);
        const auto& pattern = song.patterns[current];
        const auto length = (double) std::max<std::int64_t> (1, pattern.effectiveLength (song.meter()));
        return notesAt (song, pattern, std::fmod (tick, length));
    }

    int count = 0;

    for (const auto& clip : song.playlist)
    {
        if (clip.muted || song.isTrackMuted (clip.track) || tick < (double) clip.start || tick >= (double) clip.end())
            continue;

        if (clip.kind == ClipKind::audio)
            ++count;
        else if (clip.kind == ClipKind::pattern && clip.pattern >= 0 && clip.pattern < (int) song.patterns.size())
            count += notesAt (song, song.patterns[(std::size_t) clip.pattern], tick - (double) clip.start + (double) clip.offset);
    }

    return count;
}
} // namespace lynx::songstats

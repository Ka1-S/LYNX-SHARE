#pragma once

// Round 190rw (0.1.32 stage Q1): an editor's drag keeps indices into the song (clips, notes, channels, points). Anything
// else that replaces the song while the button is held — undo / redo (AudioEngine::isEditGestureHeld refuses them now),
// an Online Session guest's edit, a Remote Control project.edit, a plugin state load, an import finishing — could leave
// those indices on other items or past the end: in 0.1.31.2 an undo under a Playlist drag wrote past the playlist's end
// ("double free or corruption"). The editor arms the watch when its press is done and marks it after each of its own
// moves; a song change it didn't make is "foreign": the drag is then checked by IDs (or by value) and, if what it holds
// is gone or moved, it ends there with no write. Message thread only.

#include <cstdint>

#include "engine/AudioEngine.h"

namespace lynx::ui
{
struct DragSongWatch
{
    std::uint64_t stamp = 0;
    bool armed = false;

    void arm (const AudioEngine& e) noexcept
    {
        stamp = e.getSongStamp();
        armed = true;
    }
    void mark (const AudioEngine& e) noexcept
    {
        if (armed)
            stamp = e.getSongStamp();
    }
    void disarm() noexcept { armed = false; }
    // the song changed since the editor's own last move
    bool foreign (const AudioEngine& e) const noexcept { return armed && e.getSongStamp() != stamp; }

    // marks the watch when the handler returns (its own writes are not foreign), whatever path it returns by
    struct MarkOnExit
    {
        DragSongWatch& watch;
        const AudioEngine& engine;
        bool armFirst = false; // mouseDown: arm (a new press)
        ~MarkOnExit()
        {
            if (armFirst)
                watch.arm (engine);
            else
                watch.mark (engine);
        }
    };
};
} // namespace lynx::ui

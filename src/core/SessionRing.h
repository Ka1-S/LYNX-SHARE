#pragma once

// Round 190kc: the session ring's model (Live's control surfaces: the coloured box around the part of the Session grid a
// controller's pad matrix shows; a surface moves it a track / scene at a time or a whole page). Only the model: the
// controller item (tracker/QUEUE-0.1.30.md #12) drives it and draws it. Pure, header only.

#include <algorithm>

namespace lynx::sessionring
{
struct Ring
{
    int track = 0, scene = 0;   // the top-left slot
    int width = 8, height = 5;  // the surface's pads (APC40: 8 x 5, Push: 8 x 8, Launchpad: 8 x 8)
    bool operator== (const Ring&) const = default;
};

// The ring kept inside a grid of `tracks` x `scenes` (a grid smaller than the ring keeps it at 0).
inline Ring clamped (Ring r, int tracks, int scenes) noexcept
{
    r.width = std::max (1, r.width);
    r.height = std::max (1, r.height);
    r.track = std::clamp (r.track, 0, std::max (0, tracks - r.width));
    r.scene = std::clamp (r.scene, 0, std::max (0, scenes - r.height));
    return r;
}

// Moved by `dTracks` / `dScenes` slots (a surface's arrows), kept inside the grid.
inline Ring moved (Ring r, int dTracks, int dScenes, int tracks, int scenes) noexcept
{
    r.track += dTracks;
    r.scene += dScenes;
    return clamped (r, tracks, scenes);
}

// A page further (Shift + arrow on most surfaces): the ring's own width / height.
inline Ring paged (Ring r, int pagesRight, int pagesDown, int tracks, int scenes) noexcept
{
    return moved (r, pagesRight * r.width, pagesDown * r.height, tracks, scenes);
}

inline bool contains (const Ring& r, int track, int scene) noexcept
{
    return track >= r.track && track < r.track + r.width && scene >= r.scene && scene < r.scene + r.height;
}

// The grid slot under pad (column, row) of the surface; false when the pad is outside the ring.
inline bool slotOf (const Ring& r, int column, int row, int& track, int& scene) noexcept
{
    if (column < 0 || row < 0 || column >= r.width || row >= r.height)
        return false;
    track = r.track + column;
    scene = r.scene + row;
    return true;
}

// The ring moved so that slot (track, scene) is inside it (the highlighted scene / selected track followed), with the
// least movement.
inline Ring reveal (Ring r, int track, int scene, int tracks, int scenes) noexcept
{
    if (track < r.track)
        r.track = track;
    else if (track >= r.track + r.width)
        r.track = track - r.width + 1;
    if (scene < r.scene)
        r.scene = scene;
    else if (scene >= r.scene + r.height)
        r.scene = scene - r.height + 1;
    return clamped (r, tracks, scenes);
}
} // namespace lynx::sessionring

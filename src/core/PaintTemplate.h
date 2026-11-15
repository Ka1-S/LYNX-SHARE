#pragma once

#include "core/Song.h"

#include <optional>

// Round 190he (issue #175, "pick existing playlist elements"): in FL's Playlist a click with the Pencil or the Brush on a
// clip already in the Playlist makes THAT clip what the tools paint next, as it is (its length, where its content starts,
// its stretch, pitch, fades, gain...), even while the Picker panel is shown; choosing an item in the Picker panel paints
// the item's plain version again. A clip painted from such a template is a copy of it at the new place.
namespace lynx::painttemplate
{
// What a clicked clip leaves as the template: the clip itself, without what belongs to that one placed clip (its place,
// its mute switch, its identity, its clip-launch slot, the crossfades the Playlist works out from its neighbours).
inline Clip fromClicked (const Clip& clicked)
{
    auto t = clicked;
    t.track = 0;
    t.start = 0;
    t.muted = false;
    t.uid = 0;
    t.gridSlot = -1;
    t.crossfadeIn = t.crossfadeOut = 0;
    return t;
}

// Whether the template still stands for what the tools paint: the same kind of clip with the same source (a pattern
// clip: the current pattern, which a click on a pattern clip also selects; an audio clip: its channel; an automation
// clip: its automation).
inline bool matches (const Clip& t, ClipKind kind, int index, int currentPattern) noexcept
{
    if (t.kind != kind)
        return false;
    switch (kind)
    {
        case ClipKind::pattern: return t.pattern == currentPattern;
        case ClipKind::audio: return t.channel == index;
        case ClipKind::automation: return t.automation == index;
    }
    return false;
}

// The clip painted at `track` / `start` from the template.
inline Clip paint (const Clip& t, int track, std::int64_t start)
{
    auto c = t;
    c.track = track;
    c.start = start < 0 ? 0 : start;
    return c;
}
} // namespace lynx::painttemplate

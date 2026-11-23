#pragma once

// FL's Theme › Options › "Audio and automation clips use note colors" (round 190od, issue #239; FL 2026's
// ClipsOverrideBox, .flstheme OverrideClips, hint "Use the note color list to find random colors for new clips"; FL
// 2026's what's new 14100: "the reserved color palette can now be used for new playlist clips"): with the option on, a
// new audio clip channel or automation clip that has no colour takes a random one of the theme's 16 note colours.
// ui/ThemeSettings sets the switch and the colours; AudioEngine::editSong colours what an edit added. No JUCE.

#include <array>
#include <atomic>
#include <cstdint>
#include <random>

#include "core/Song.h"

namespace lynx::newclipcolours
{
constexpr int count = 16;

struct State
{
    bool on = false;
    std::array<std::uint32_t, count> colours {};
    std::minstd_rand random { 190 };
};
inline State& state()
{
    static State s; // (the message thread: the theme page and editSong)
    return s;
}

inline void set (bool on, const std::array<std::uint32_t, count>& colours)
{
    auto& s = state();
    s.on = on;
    s.colours = colours;
}

inline std::uint32_t pick()
{
    auto& s = state();
    return s.colours[(std::size_t) (s.random() % (unsigned) count)] | 0xff000000u;
}

// After an edit: the audio clip channels at or past `channelsBefore` and the automation clips at or past
// `automationsBefore` that have no colour take a note colour. Returns how many were coloured.
inline int colourNew (Song& song, std::size_t channelsBefore, std::size_t automationsBefore)
{
    if (! state().on)
        return 0;
    int coloured = 0;
    for (auto c = channelsBefore; c < song.channels.size(); ++c)
        if (auto& ch = song.channels[c]; ch.instrument == InstrumentKind::audioClip && ch.colour == 0)
            ch.colour = pick(), ++coloured;
    for (auto a = automationsBefore; a < song.automations.size(); ++a)
        if (auto& au = song.automations[a]; au.colour == 0)
            au.colour = pick(), ++coloured;
    return coloured;
}
} // namespace lynx::newclipcolours

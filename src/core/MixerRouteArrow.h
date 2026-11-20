#pragma once

// FL's route arrows at the foot of every Mixer strip (round 190io, design190ia FM-32; FL manual, Mixer › Routing
// Iconography): the selected track shows a downward arrow (its audio goes out); every other track an up-arrow that
// is the Send switch from the selected track (hollow: available, lit: the selected track sends here); a track that
// feeds the selected one shows a greyed switch (routing back would make a loop); a small up-arrow above the switch
// says another track sends into this one ("Send in use"). LYNX keeps its "→ n" pill in the routing lane as well.
// No JUCE: the state only (ui/MixerComponent.cpp draws it).

#include "core/Song.h"

namespace lynx::routearrow
{
enum class State
{
    none,      // nothing drawn (no such track)
    out,       // the selected track: the downward arrow
    available, // a hollow up-arrow: a click makes the selected track send here
    routed,    // a lit up-arrow: the selected track sends here
    blocked    // greyed: this track feeds the selected one, or routing there would make a loop
};

inline State of (const Song& song, int selected, int track) noexcept
{
    const auto n = (int) song.mixer.size();
    if (track < 0 || track >= n)
        return State::none;
    if (track == selected)
        return State::out;
    if (selected <= 0 || selected >= n) // the Master selected (or nothing): it sends nowhere inside the Mixer
        return State::blocked;
    if (song.routes (selected, track))
        return State::routed;
    return song.canRoute (selected, track) ? State::available : State::blocked;
}

// FL's "Send in use": another insert sends into `track` (the Master counts the inserts routed to it).
inline bool receives (const Song& song, int track) noexcept
{
    for (int from = 1; from < (int) song.mixer.size(); ++from)
        if (from != track && song.routes (from, track))
            return true;
    return false;
}
} // namespace lynx::routearrow

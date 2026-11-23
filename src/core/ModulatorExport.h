#pragma once

// Round 190ox (modulators stage 2): LYNX's modulators in an .flp. FL Studio has no modulators; what it has that plays
// the same is an automation clip, with FL's own clip LFO (TACArticulator, core/Song.h AutomationLfo) for a tempo-synced
// LFO. So each route whose modulator follows the song position alone becomes one automation clip over the song on a
// free Playlist track:
//   - an LFO (synced): a flat clip at the base with the clip LFO on (FL's LFO is a bent triangle: a triangle is exact,
//     saw and square are FL's skew / tension extremes, a sine the closest tension; random shapes are baked as points);
//   - steps and random (synced): baked as points, FL's Hold mode between them (exact without glide / smoothing);
//   - a macro: a flat clip at base + depth × its value (exact).
// Everything else is listed as lost: followers, envelopes, keytrack / velocity, free-running rates, a target that has
// its own automation clips or lanes, a target FL has no parameter for (the exporter reports that one).

#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::modexport
{
struct Result
{
    int clips = 0;                  // automation clips written
    std::vector<std::string> notes; // what was approximated
    std::vector<std::string> lost;  // what FL cannot play
};

// Turns the song's modulators into automation clips (as above) and removes them; `song` is the exporter's copy.
Result bakeForFl (Song& song);

// The clip LFO's tension (−128..128) whose bent triangle is closest to a sine (the largest gap over a cycle).
int sineTension() noexcept;
} // namespace lynx::modexport

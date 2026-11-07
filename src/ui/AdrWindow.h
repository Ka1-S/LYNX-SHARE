#pragma once

// The ADR window (round 174d, core/Adr.h, docs/analise/REA_rodada174d.md §5): Tools › ADR. The cue list (in / out
// timecode, character, line, notes, status; CSV import / export), the record cycle (Rehearse, Record, Play takes with
// pre-roll and post-roll; one take per pass in Cycle mode), the cue signals (three beeps to the headphones, the
// streamer and the punch over the video windows), the guide (the original dialogue's insert) heard on the headphones
// only, the takes of each cue (named, rated, circled, the one that plays), the comp (drag on a take's lane: that take
// plays there) and Align (the take time-aligned to the guide, offline, as a new take).

#include <memory>

namespace juce
{
class Component;
}

namespace lynx
{
class AudioEngine;

namespace adrwindow
{
void show (AudioEngine& engine);
// The window's content on its own (the --videotrackshot screenshots)
std::unique_ptr<juce::Component> makePanel (AudioEngine& engine);
}
} // namespace lynx

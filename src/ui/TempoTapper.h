#pragma once

// The tempo tapper (View menu, the tempo display's right-click menu): a tool window with a big Tap pad. The tempo
// follows the taps from the second one on (core/ValueMath.h TapTempo); "Apply tempo" off only measures, "Sync"
// puts the playhead on the nearest beat at each tap during playback, and the nudge buttons run the song a little
// slower or faster while held, to drift the beat into place. The workflow follows FL Studio's tempo tapper
// (REA_rodada132: its form has Tap, Apply tempo, Sync and the two nudge buttons; round 190my added FL's Mute and "now",
// REA 190my R3); the window is LYNX's own.

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
void openTempoTapper (AudioEngine& engine);

// --snapshot: the tapper's content, after `taps` taps at `bpm`.
std::unique_ptr<juce::Component> makeTempoTapperForSnapshot (AudioEngine& engine, int taps, double bpm);
} // namespace lynx

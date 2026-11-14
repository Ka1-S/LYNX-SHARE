#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;

// Plugin performance monitor (FL's, interpreted; REA_rodada84): how much processing time each instrument and effect
// slot takes, as a share of real time (now, average, peak), with a search, Freeze, Reset and "Reset on play". The
// rack measures only while the window is open.
void showPluginMonitor (AudioEngine& engine);

// The content for --snapshot mode, filled with sample measurements.
std::unique_ptr<juce::Component> makePluginMonitorPanel (AudioEngine& engine);

// Round 190gg (B5): the dropouts in a sentence or two, for the CPU meter's hint and this window (empty: none yet):
// how many since the start, when the last one was, and why (the processing, naming the slowest call of that block, or
// the driver / system while the load was low, with what to try).
juce::String describeDropouts (AudioEngine& engine);
} // namespace lynx

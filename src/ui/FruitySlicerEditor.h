#pragma once

// Fruity Slicer's editor (round 180c, core/FruitySlicer.h): FL's layout (its form is 377 × 330; LYNX draws it at twice
// that size with its own look) — the sample name, the Open / Sample / Slicing / Dump buttons, AUTO-DUMP, AUTO-FIT,
// DECLICK, the selected slice's REV, name and key, the PS / TS / ATT / DEC sliders, the sample scope with its slices,
// ANIMATE, PLAY TO END and the auto-slicing LOW / HIGH wheels.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;
struct ChannelDef;
struct Song;

// `channel`: the Channel Rack channel (−1 in a Patcher: `locate` finds the node)
std::unique_ptr<juce::Component> makeFruitySlicerEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel);
// Round 190gk ("Open in new Fruity Slicer channel" of the Sampler's waveform menu): the next editor made for Channel
// Rack channel `channel` loads `path` as its own Load does (auto slices, FL's beats); `channel` −1 forgets it.
void loadSampleWhenOpened (int channel, const juce::String& path);
} // namespace lynx

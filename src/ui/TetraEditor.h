#pragma once

// The editor of Tetra, LYNX's own 4-operator FM synth (round 190hs, core/FmSynth.h): LYNX's own design. Algorithm
// strip (click a routing), the four operators side by side (wave, ratio / fixed frequency, level, feedback, scaling,
// switches), one envelope editor with a drawn curve for the operator, filter, pitch or LFO envelope picked on its
// tabs, and the filter, LFO, pitch envelope and global sections. Every knob is automatable (its engine parameter).

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;
struct ChannelDef;
struct Song;

std::unique_ptr<juce::Component> makeTetraEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel);
} // namespace lynx

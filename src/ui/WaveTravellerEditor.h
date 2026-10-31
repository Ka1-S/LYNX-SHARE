#pragma once

// Wave Traveller's editor (round 167f, core/WaveTraveller.h): LYNX's own layout of FL's controls — the key whose patch
// is edited (each key holds its own curves), Speed (tempo based or absolute), Attack, Release, Start, End, the
// position / volume curves with their points and segment types, the mute switches along the curve, and FL's presets
// (.spln files of the user's FL installation).

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;
struct ChannelDef;
struct Song;

// `channel`: the Channel Rack channel (−1 in a Patcher: `locate` finds the node)
std::unique_ptr<juce::Component> makeWaveTravellerEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel);
std::unique_ptr<juce::Component> makeWaveTravellerEditor (AudioEngine& engine, int channel);
} // namespace lynx

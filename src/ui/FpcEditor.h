#pragma once

// FPC's editor (round 190q, issue #135; core/Fpc.h, core/FpcInstrument.h): FL's layout (its form is 790 × 367, drawn
// 1.25 × larger; positions from the plugin's form resource, docs/analise/REA_rodada9.md §1.8) — the 4 × 4 pad grid with
// banks A / B, the selected pad's name, volume, pan, tuning, mute, solo, note, cut / cut by groups and output, its
// layers (volume, pan, tuning, velocity range) and the selected layer's sample. The kit is the channel's FL state: every
// edit writes it back through the codec, so the kit goes to FL as edited.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;
struct ChannelDef;
struct Song;

// `channel`: the Channel Rack channel (−1 in a Patcher: `locate` finds the node)
std::unique_ptr<juce::Component> makeFpcEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel);
} // namespace lynx

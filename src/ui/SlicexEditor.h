#pragma once

// Slicex's editor (round 190r, core/Slicex.h; FL's form is 714 × 625, REA_rodada190r.md): the masters and the Mod X / Y
// pad, the layering row, the region settings, the filter and the articulator editor (envelope, LFO and the four maps
// of pan, volume, cutoff, resonance, speed and sample start for each of the 8 groups), the deck tool row, the deck
// wave editors with their regions, and the keyboard with the key map. LYNX draws it with its own look.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;
struct ChannelDef;
struct Song;

// `channel`: the Channel Rack channel (−1 in a Patcher: `locate` finds the node)
std::unique_ptr<juce::Component> makeSlicexEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel);
// Round 190gk ("Open in new Slicex channel" of the Sampler's waveform menu): the next editor made for Channel Rack
// channel `channel` loads `path` on deck A as the deck's Load sample does; `channel` −1 forgets it.
void loadSlicexSampleWhenOpened (int channel, const juce::String& path);

// The editor test (LYNX --slicexedittest, round 190aj): one command for the Slicex editor inside `root` (the channel's
// settings panel), on its current deck; the result as text. The commands are listed at SlicexEditor::test.
juce::String slicexEditorTest (juce::Component& root, const juce::String& command);
} // namespace lynx

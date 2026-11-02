#pragma once

// Loop Starter (round 168b; core/LoopStarter.h, REA_rodada168b-loopstarter.md): the Channel Rack's dice button opens
// it. Pick a genre and "New loop" builds the kit channels with steps in a new "Loop Starter #N" pattern; the dice
// re-roll the samples (all unlocked channels, or the selected ones) or the steps; "Send to Playlist" places the
// pattern in the song and the channels leave Loop Starter. FL's per-channel menu (Replace sample, Generate steps,
// Locked, One-shot category) is in each row and in the channel's right-click menu (Loop Starter ›).

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/LoopStarter.h"
#include "engine/AudioEngine.h"

namespace lynx::loopstarterui
{
// The configuration, presets and sample library (FL's when installed, else LYNX's own), read once.
const loopstarter::Sources& sources();

void open (AudioEngine& engine);

// the per-channel actions (also used by the Channel Rack's menu)
void replaceSamples (AudioEngine& engine, const std::vector<int>& channels); // locked ones stay when more than one
void generateSteps (AudioEngine& engine, const std::vector<int>& channels);
void setCategory (AudioEngine& engine, int channel, const std::string& category);
void setLocked (AudioEngine& engine, int channel, bool locked);
// "Loop Starter ›" for a Loop Starter channel's right-click menu (nothing for other channels)
void addChannelMenu (juce::PopupMenu& menu, AudioEngine& engine, int channel);

// --snapshot: the window's content (after a "New loop" when `generate`)
std::unique_ptr<juce::Component> makeForSnapshot (AudioEngine& engine, bool generate);
} // namespace lynx::loopstarterui

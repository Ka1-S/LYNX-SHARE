#pragma once

// FL's "Envelope / instrument settings" page for a generator that runs Image-Line's plugin voice processor
// (core/FlVoiceSettings.h; round 190f, issue #129): the five targets (Pan, Volume, Mod X, Mod Y, Pitch), each with its
// envelope (delay, attack, hold, decay, sustain, release, amount, the three tensions) and its LFO (delay, attack,
// amount, speed, shape, tempo, global), and the filter (Mod X, Mod Y, type). It edits FL's own values, so what is set
// here is what FL reads from an exported project.

#include <functional>
#include <memory>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/FlVoiceSettings.h"

namespace lynx
{
class AudioEngine;

// `read`: the generator's voice settings (nullopt = no such channel any more); `write`: stores edited ones
std::unique_ptr<juce::Component> makeFlVoicePanel (AudioEngine& engine, std::function<std::optional<flvoice::Settings>()> read,
                                                   std::function<void (const flvoice::Settings&)> write);
} // namespace lynx

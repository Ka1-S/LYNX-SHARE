#pragma once

// FL's "Envelope / instrument settings" page for a generator that runs Image-Line's plugin voice processor
// (core/FlVoiceSettings.h; rounds 190f / 190m, issue #129), laid out as FL's: the target tabs (Panning, Volume, Mod X,
// Mod Y, Pitch) over three boxes — Envelope (switch, graph, delay, attack, hold, decay, sustain, release, amount, the
// three tensions, Tempo), LFO (shape buttons, graph, delay, attack, amount, speed, Tempo, Global) and Filter (Mod X,
// Mod Y, type). It edits FL's own values, so what is set here is what FL reads from an exported project.

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
// Shows a target's tab (flvoice::Target) of the page inside `root`, if it holds one (for the --snapshot pictures)
void showFlVoiceTarget (juce::Component& root, int target);
} // namespace lynx

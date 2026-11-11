#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// "Open without third-party plugins" (round 190ap, core/PluginHold.h): the project's hosted plugins, each off or on,
// with "Switch on" for the chosen one and "Switch all on". Switching a plugin on creates its slots from the settings
// kept in the project; a plugin that then fails says so here.
std::unique_ptr<juce::Component> makePluginsOffPanel (AudioEngine& engine);

// --selftest: a project with hosted plugins (channels, insert slots, inside a Patcher, a substitute's original) opened
// with the plugins off creates none of them, a save made in that state reads back with every plugin's settings
// identical, switching one on asks only for that one, and the next project ends the mode. "OK" or what failed.
juce::String pluginsOffSelfTest();
} // namespace lynx

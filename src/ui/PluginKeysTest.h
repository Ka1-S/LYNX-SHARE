#pragma once
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>

namespace lynx
{
// One step of issue #96's run-time check, per timer tick. false: finished (see PluginKeysTest.cpp).
bool pluginKeysTestTick (juce::AudioProcessorEditor& editor, const std::function<void (bool)>& setOption, const std::function<void (const juce::String&)>& step);
} // namespace lynx

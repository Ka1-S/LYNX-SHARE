#pragma once

#include <juce_core/juce_core.h>
#include <functional>

namespace lynx
{
class AudioEngine;

// Called only on the message thread, after RemoteControl authenticated the caller and
// checked the project/revision guard for writes. cancelled() == true prevents commit.
bool isRemotePluginOperation (const juce::String& operation);
juce::var dispatchRemotePlugin (AudioEngine&, const juce::String& operation, const juce::var& args,
                              const std::function<bool()>& cancelled = {});
}

#pragma once

#include <juce_core/juce_core.h>
#include <functional>

namespace lynx
{
class AudioEngine;
bool isRemotePatternOperation (const juce::String&);
// Message thread only; the outer dispatcher authenticates and guards writes.
juce::var dispatchRemotePattern (AudioEngine&, const juce::String&, const juce::var&,
                              const std::function<bool()>& cancelled = {});
}

#pragma once

#include <juce_core/juce_core.h>
#include <functional>

namespace lynx
{
class AudioEngine;
bool isRemoteInstrumentOperation (const juce::String&);
// Message thread only; the outer control dispatcher authenticates and guards writes.
juce::var dispatchRemoteInstrument (AudioEngine&, const juce::String&, const juce::var&,
                                 const std::function<bool()>& cancelled = {});
}

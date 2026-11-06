#pragma once

#include <functional>
#include <juce_core/juce_core.h>

namespace lynx::remoteui
{
// Message-thread settings and status for the application-owned local API.
bool enabled();
bool active();
juce::String status();
void setEnabled (bool);
void setRuntimeStatus (bool active, const juce::String& detail = {});
void install (std::function<void (bool)> enable);
void shutdown();
}

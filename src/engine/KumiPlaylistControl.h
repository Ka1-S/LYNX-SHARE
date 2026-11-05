#pragma once
#include <juce_core/juce_core.h>
namespace lynx
{
class AudioEngine;
bool isKumiPlaylistOperation (const juce::String& op);
juce::var dispatchKumiPlaylist (AudioEngine&, const juce::String& op, const juce::var& args);
} // namespace lynx

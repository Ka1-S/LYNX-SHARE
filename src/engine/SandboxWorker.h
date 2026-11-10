#pragma once

#include <memory>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

// What the sandbox's child side (engine/SandboxWorker.cpp) needs from the program it runs in (round 176a): LYNX.exe
// implements these with its PluginHost (engine/PluginSandbox.cpp); the 32-bit bridge LYNX-bridge32.exe with its own
// small set of plugin formats (bridge/Bridge32Main.cpp).
namespace lynx::sandboxhost
{
// Creates the plugin (not prepared yet). nullptr + `error` on failure.
std::unique_ptr<juce::AudioPluginInstance> open (const juce::PluginDescription& description, double sampleRate, int blockSize,
                                                 juce::String& error);
// Stereo layout when the plugin takes it, rate and block size, prepareToPlay (PluginHost's preparePlugin).
void prepare (juce::AudioPluginInstance& plugin, double sampleRate, int blockSize);
// The plugin's state as base64 (PluginHost's capturePluginState).
std::string captureState (juce::AudioPluginInstance& plugin);
// The plugin window's background colour and icon.
juce::Colour windowColour();
juce::Image windowIcon();
// 32-bit x86 only (the bridge): the x87 control word set on the plugin's threads before each call, as FL's ilbridge.exe
// does (REA_rodada68 §2.1: 0x033F, 64-bit precision, round to nearest); 0 = leave it alone.
unsigned x87ControlWord();
} // namespace lynx::sandboxhost

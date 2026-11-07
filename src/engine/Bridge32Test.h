#pragma once

#include <juce_core/juce_core.h>

namespace lynx
{
// LYNX.exe --bridge32test <32-bit test plugin> <report> [--compare <its 64-bit build>] (round 176a): scan, open, play,
// automate, transport, state, sample rate / block sizes, editor and crash isolation of the 32-bit bridge, plus its cost
// per block. Returns the process exit code (0 = OK). Windows x64 only (elsewhere: SKIPPED).
int runBridge32Test (const juce::String& pluginPath, const juce::String& reportPath, const juce::String& comparePath);
} // namespace lynx

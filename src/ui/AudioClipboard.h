#pragma once

#include <cstdint>
#include <vector>

#include <juce_core/juce_core.h>

#include "core/AudioEdit.h"

namespace lynx::audioclipboard
{
// The audio editor's clipboard: one for every editor window (copy in one, paste in another).
audioedit::Audio& editor();

// What Tools › Clipboard › "Save audio clipboard to browser" saves (round 181b). FL reads the Windows clipboard's
// CF_WAVE (FUN_0124d070 with format 12); LYNX does too on Windows, and falls back to its audio editor's clipboard.
// Empty when there is nothing.
std::vector<std::uint8_t> wav();
bool hasAudio();

// FL's Browser folder "Clipboard files" (REA round 181b: folder id 0x3f2, the user's Presets\Clipboard files).
juce::File folder();
// Writes `data` as `baseName` + `extension` into folder(), with " (2)", " (3)"... when the name is taken.
// Returns the file written, or an empty File (folder not created / write failed).
juce::File save (const juce::String& baseName, const juce::String& extension, const void* data, std::size_t size);
} // namespace lynx::audioclipboard

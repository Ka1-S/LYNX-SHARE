#pragma once

#include <functional>

#include "engine/AudioEngine.h"

namespace lynx
{
// The audio clip menu's "Extract stems from sample" (issue #27): asks before the first model download, then downloads,
// separates and writes the stems on a background thread with a progress window and Cancel. The result is one undo
// step. `done` runs on the message thread after the stems were added.
void extractStemsFromClip (AudioEngine& engine, int clipIndex, std::function<void()> done);

// --stemsplit: separates `input` into `folder` with the model already installed. Empty, or what went wrong.
juce::String extractStemsToFolder (AudioEngine& engine, const juce::File& input, const juce::File& folder);
} // namespace lynx

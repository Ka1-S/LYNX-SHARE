#pragma once

#include <functional>

#include "core/StemSplit.h"
#include "engine/AudioEngine.h"

namespace lynx
{
// The audio clip menu's "Extract stems from sample" (issue #27): asks before the first model download, then downloads,
// separates and writes the stems on a background thread with a progress window and Cancel. The result is one undo
// step. `done` runs on the message thread after the stems were added.
void extractStemsFromClip (AudioEngine& engine, int clipIndex, std::function<void()> done);

// App shutdown: a job still running stops at its next check and is waited for (before the engine goes).
void shutdownStemExtraction();

// --stemsplit: separates `input` into `folder` with the models already installed (round 190jr: the options' stems and
// model; the defaults = every stem of the standard model). Empty, or what went wrong.
juce::String extractStemsToFolder (AudioEngine& engine, const juce::File& input, const juce::File& folder, const stemsplit::Options& options = {});

// Round 190jr (BUILD-LIST 14): the "Extract stems from sample" form (FL's TDEMUCSFORM + the model rows), for the dialog and
// for snapshots; `extract` gets the options when Extract is pressed.
std::unique_ptr<juce::Component> makeStemOptionsPanel (const stemsplit::Options& start, std::function<void (stemsplit::Options)> extract);
} // namespace lynx

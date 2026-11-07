#pragma once

// Event editor (round 83, FL's event editor): edits the current pattern's events of one target (channel volume,
// pan, a plugin or mixer parameter) as a bar graph. Tools: draw, line, select; on the selection (or the whole
// pattern): scale/offset, randomise, FL's LFO, clear. Each gesture is one undo step. While the pattern plays, its
// events drive the target (core/RackRenderer).

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// Opens (or brings to front) the event editor of the current pattern, showing `target`.
void openEventEditor (AudioEngine& engine, const AutomationTarget& target);
namespace EventEditorHooks
{
// File › Browse automations: opens the Browser at the automation scores (set by the main window)
extern std::function<void()> onBrowseAutomations;
}

// Just the editor (for --snapshot mode).
std::unique_ptr<juce::Component> makeEventEditor (AudioEngine& engine, const AutomationTarget& target);
} // namespace lynx

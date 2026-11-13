#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;

// The Groove Pool of the Clip-launch workflow (round 190bv, core/Groove.h): the project's grooves as rows (name, Base,
// Quantize, Timing, Random, Velocity) with the pool's Amount in the title row. Own art; what the columns are follows
// the manual of the studied program (docs/analise/REA_rodada190bv.md). Made only when the clip grid shows it.
std::unique_ptr<juce::Component> makeGroovePool (AudioEngine& engine);
// The height the pool wants for its rows (title and column heads included), at most `maxRows` rows.
int groovePoolHeight (const AudioEngine& engine, int maxRows = 5);

// The "add a groove" menu, shared with the slot menu's groove chooser: LYNX's own grooves by folder, and a groove file
// of the user's. `added`: the id of the groove now in the pool.
void addGrooveMenuItems (juce::PopupMenu& menu, AudioEngine& engine, juce::Component* owner, std::function<void (std::uint32_t)> added);
} // namespace lynx

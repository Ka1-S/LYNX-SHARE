#pragma once

#include <functional>
#include <utility>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;

// The clip grid of the Clip-launch workflow (round 181a, core/ClipLauncher.h): Playlist tracks as columns, scenes as rows.
// Own design and art. Made only the first time the grid is shown; never in Pattern workflow. It repaints from a timer that
// runs only while it is on screen.
std::unique_ptr<juce::Component> makeClipGrid (AudioEngine& engine);

// What the grid tells the Detail View (round 183a): a slot selected (click or arrow keys; a click on a column's title
// selects that track), a filled slot double-clicked (show the clip), a column's title double-clicked (show its devices).
enum class GridPick { select, openClip, openTrack };
void setClipGridPickListener (juce::Component& grid, std::function<void (int track, int scene, GridPick pick)> listener);
// The grid's selected track and scene ({ 0, 0 } before any click).
std::pair<int, int> clipGridSelection (juce::Component& grid);
} // namespace lynx

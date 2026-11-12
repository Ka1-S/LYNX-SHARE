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
// Round 190bv: shows or hides the Groove Pool under the grid (ui/GroovePoolComponent.h).
void setClipGridGroovePoolShown (juce::Component& grid, bool shown);
// The menu of a slot (the right-click menu; --gridshot draws it).
juce::PopupMenu clipGridSlotMenu (juce::Component& grid, int track, int scene);
// A clip dragged out of a slot (the Playlist takes it as a clip of its own): true and its slot when `description` is one.
bool clipGridDrag (const juce::var& description, int& track, int& scene);
// The grid's selected track and scene ({ 0, 0 } before any click).
std::pair<int, int> clipGridSelection (juce::Component& grid);
} // namespace lynx

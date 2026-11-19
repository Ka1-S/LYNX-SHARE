#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// FL Studio's Project picker (View › Project picker, Ctrl+F8; round 190my, DF-143; captures 190mv A11 and 190my A4):
// the Plugin picker's full-screen see-through form (TPlugListForm, ui/PickerLook.h) showing the project instead of the
// plugins — "Patterns" on the left (each pattern as its clip with its notes, then a tile that adds a pattern), "Channels"
// on the right (each sample channel as its clip with its waveform, each generator as its plugin's picture under a name
// tag), packed in rows as the Plugin picker packs them, the tabs All / Patterns / Channels along the bottom. Pointing at
// a tab shows it (the wheel steps through them); a click picks a tile and closes the picker (a pattern becomes the current
// one; a channel is selected and its window opens); a right-click on a pattern shows it in the Browser's Current project;
// dragging a pattern hides the picker and drops its clip on the Playlist. Typing searches (an orange magnifier and the
// text at the top; the others dim). Escape, Ctrl+F8, a right click outside the tiles or another window closes it.
struct ProjectPickerTargets
{
    std::function<void (int)> pickPattern;          // a pattern tile (−1 = the add tile: a new pattern)
    std::function<void (int)> pickChannel;          // a channel tile (FL: selects it and opens its window)
    std::function<void (int)> revealPattern;        // a right-click on a pattern tile (FL: shown in the Browser)
    std::function<juce::Component*()> dragOwner;    // inside the main window's drag container (the picker is its own window)
    std::function<void()> close;                    // the owner removes the view
};

std::unique_ptr<juce::Component> makeProjectPickerView (AudioEngine& engine, ProjectPickerTargets targets);

// The screen under the picker, taken before it shows (`screenArea` in logical screen coordinates).
void takeProjectPickerBackdrop (juce::Component& view, juce::Rectangle<int> screenArea);

// (shots / tests) the tile under a point: {kind, index}, kind 0 none, 1 pattern, 2 the add tile, 3 channel; and where a
// pattern / channel tile is drawn
std::pair<int, int> projectPickerTileAt (juce::Component& view, juce::Point<int> p);
juce::Rectangle<int> projectPickerTileBounds (juce::Component& view, int kind, int index);
void projectPickerHoverForSnapshot (juce::Component& view, juce::Point<int> p);
void projectPickerShowTabForSnapshot (juce::Component& view, int tab); // 0 All, 1 Patterns, 2 Channels
void projectPickerBackdropForSnapshot (juce::Component& view, const juce::Image& screen); // (no screen to take off Windows)
void projectPickerTypeForSnapshot (juce::Component& view, const juce::String& text);
} // namespace lynx

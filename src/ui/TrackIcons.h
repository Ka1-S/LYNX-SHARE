#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/TrackIcons.h"

// LYNX's own icons for channels, mixer inserts and Playlist tracks (round 168h): original line drawings on the 24x24
// grid of ui/Icons.h (one family with the toolbar icons), never FL's artwork. The model keeps FL's icon index
// (core/TrackIcons.h maps it to these).
namespace lynx::ui
{
// Draws `icon` fitted (centred, square) into `area`.
void drawTrackIcon (juce::Graphics& g, trackicons::Icon icon, juce::Rectangle<float> area, juce::Colour colour);

// The icon picker (FL's "Change icon..." of mixer tracks, channels and Playlist tracks; FL's rename dialog's F5 icon
// menu): a grid of LYNX's icons in a pop-up at `screenArea`, the current one (`currentFlIndex`, FL's index) marked.
// `onPick` gets the chosen icon's FL index (0 = none).
void showTrackIconPicker (juce::Rectangle<int> screenArea, int currentFlIndex, std::function<void (int)> onPick);

// The grid itself, for --snapshot mode and tests.
std::unique_ptr<juce::Component> makeTrackIconGrid (int currentFlIndex, std::function<void (int)> onPick);
} // namespace lynx::ui

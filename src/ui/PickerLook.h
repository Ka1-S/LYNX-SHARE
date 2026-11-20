#pragma once

// The look shared by FL Studio's full-screen pickers (TPlugListForm: the Plugin picker, F8, and the Project picker,
// Ctrl+F8; rounds 169k / 175b / 190my): the see-through veil over the screen and the highlight behind the tile or tab
// pointed at.

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx::pickerlook
{
// What the screen showed under a picker when it opened (`physical` pixels). Windows: the screen's pixels; elsewhere:
// nothing (the veil alone).
juce::Image captureScreen (juce::Rectangle<int> physical);

// FL's veil over `backdrop` (the screen taken before the picker showed), composed into `backdropShown` once per size.
void paintVeil (juce::Graphics& g, const juce::Component& owner, const juce::Image& backdrop, juce::Image& backdropShown, double pixelScale);

// FL's highlight box behind the tile (or tab) `b` in a row of `rowHeight`, and its painting (`pressed`: the warm colour).
juce::Rectangle<float> highlight (juce::Rectangle<int> b, int rowHeight, bool isTab);
void paintHighlight (juce::Graphics& g, juce::Rectangle<float> box, bool pressed);
} // namespace lynx::pickerlook

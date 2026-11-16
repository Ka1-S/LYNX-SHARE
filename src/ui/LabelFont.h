#pragma once

#include <juce_graphics/juce_graphics.h>

namespace lynx::theme
{
// Settings › General › "Condensed labels (FL style)" (round 190je, design190ia FM-5 / FM-S8). FL's landmark text is a
// condensed face; with the option on, the FL-landmark roles (menu bar, menus, Browser tree, Playlist track names, Mixer
// strip names) draw with Open Sans Condensed (SemiBold / Bold, SIL OFL, resources/fonts) a step larger, in about the
// same width. Off (the default): labelFont is font() itself, pixel for pixel. Saved in the profile
// (ui-condensed-labels.txt), applied at start-up like the interface font; --condensedlabels turns it on for one run.
juce::Font labelFont (float size, bool bold = false);
void setCondensedLabels (bool on);
bool condensedLabels();
bool savedCondensedLabels();
void saveCondensedLabels (bool on);
} // namespace lynx::theme

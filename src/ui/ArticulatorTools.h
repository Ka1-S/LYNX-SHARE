#pragma once

// FL's automation clip menu › "Articulator tools" (round 190mk, details DE-115; capture 190mk A5 fl-pl-autoclip-menu.png):
// Copy state, Paste state | Flip vertically, Scale levels..., Normalize levels, Decimate points..., Filter..., Smooth
// up..., Smooth up abrupt changes, Turn all points smooth, Create sequence..., Analyze audio file... — the same envelope
// tools as Gross Beat's mapping menu, on the automation clip's points. The forms are LYNX's Edison envelope tools
// (ui/AudioEditorTools, as round 190mf's Gross Beat host): the points over the clip's length are an envelope 0…1; a form's
// moves are shown at once (no undo step), its Accept is one undo step, its Cancel puts the points back. FL's Options ›
// "Focus on click" is not offered (its effect could not be captured, 190mk A1.6).

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx::articulator
{
// the submenu for automation `automation` (index into Song::automations) shown by a clip `length` ticks long
juce::PopupMenu menu (AudioEngine& engine, int automation, std::int64_t length, juce::Component& owner);

// the pure parts (tests): the points as an envelope over 0…`length` and back (modes kept where a point stays; a new
// point is a Single curve), Flip, Normalize, all-smooth
std::vector<AutomationPoint> flipped (std::vector<AutomationPoint> points);
std::vector<AutomationPoint> normalized (std::vector<AutomationPoint> points);
std::vector<AutomationPoint> allSmooth (std::vector<AutomationPoint> points);
// FL's "Analyze audio file": the file's peak levels (equal slices) over the clip, loud = 1
std::vector<AutomationPoint> fromLevels (const std::vector<float>& peaks, std::int64_t length);
} // namespace lynx::articulator

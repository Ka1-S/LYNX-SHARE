#pragma once

// Issue #254 (round 190ou): a list that only scrolls up and down takes a notch of the horizontal wheel (a tilt wheel, a
// second wheel) as a notch of the vertical one, as FL 2026's Browser does: a tilt to the right scrolls up, as the
// wheel turned up. JUCE's Viewport ignores the horizontal wheel when it has no horizontal scroll bar. A trackpad's
// swipe and Shift+wheel are left as they are (core/EditorGestures.h, horizontalWheelNotch).

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/EditorGestures.h"
#include "ui/FlMods.h"

namespace lynx::ui
{
class TiltViewport : public juce::Viewport
{
public:
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const auto w = flmods::normalisedWheel (wheel);
        if (! getHorizontalScrollBar().isVisible() && gesture::horizontalWheelNotch (w.deltaX, w.deltaY, w.isSmooth, e.mods.isShiftDown()) != 0)
        {
            auto vertical = wheel;
            vertical.deltaY = -wheel.deltaX;
            vertical.deltaX = 0.0f;
            juce::Viewport::mouseWheelMove (e, vertical);
            return;
        }
        juce::Viewport::mouseWheelMove (e, wheel);
    }
};
} // namespace lynx::ui

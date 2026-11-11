#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/Theme.h"

namespace lynx::ui
{
// "Open without third-party plugins" (round 190ap): the tag of a channel or insert whose plugin is switched off: a
// grey plate with the power sign (the size of the stand-in render's amber "R"; no letters, so nothing to translate).
inline void drawPluginOffTag (juce::Graphics& g, juce::Rectangle<float> tag)
{
    theme::fillRoundRect (g, tag, 3.0f, theme::colour::textSecondary.withAlpha (0.9f));
    g.setColour (theme::colour::bg1);
    const auto centre = tag.getCentre().translated (0.0f, 0.6f);
    const auto r = juce::jmin (tag.getWidth(), tag.getHeight()) * 0.27f;
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, r, r, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
    g.strokePath (arc, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine (centre.x, centre.y - r - 1.2f, centre.x, centre.y - 0.2f, 1.3f);
}
} // namespace lynx::ui

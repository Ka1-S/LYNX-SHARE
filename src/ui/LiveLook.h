#pragma once

// Round 190jn (design190ia slices 13, 17, 19, 25): the small parts of Ableton Live 12.4's look that the Clip-launch
// layout's views share — the open-ring knob, the power disc and the round title-bar wells. Measured in Live 12.4 (Dark
// theme, 100 %) on design190ia/shots/ref/live-session.png (Utility card 1075..1213 × 807..995), round 190ei's
// mn_drumrack-choke-menu.png (909 Core Kit) and round 183a's d02-utility-off.png. Drawn with LYNX vectors; only the
// Clip-launch layout in Live's look (theme::livelook::active()) calls these.

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/Theme.h"

namespace lynx::theme::livelook
{
inline const juce::Colour knobValue { 0xff03c3d5 };  // a knob's value arc (909 Core Kit Low Gain / Tune / Decay)
inline const juce::Colour needle { 0xff181818 };     // a knob's needle
inline const juce::Colour wellRim { 0xff121212 };    // the 1 px rim of a power disc or a title-bar well
inline const juce::Colour wellDisabled { 0xff757575 }; // a rack column toggle that can't be used now
inline const juce::Colour padName { 0xffb5b5b5 };    // a Drum Rack pad's name area

// Live's knob: an open ring (#111111, ~2 px, open 70° at the bottom), a needle (#181818) from the centre through the
// ring, the value arc (#03C3D5) from the default (the centre of a bipolar knob, else the start) to the value, and a small
// ▼ over the ring where a bipolar knob's default is. Measured at 25 px (Utility's Gain: ring x 1167..1191).
inline void drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float value01, bool bipolar)
{
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 2.0f;
    if (size <= 6.0f)
        return;
    const auto centre = bounds.getCentre().translated (0.0f, 1.0f); // (room for the ▼ above)
    const auto thickness = juce::jlimit (1.6f, 3.0f, size * 0.09f);
    const auto radius = size * 0.5f - thickness * 0.5f - 1.0f;
    constexpr auto pi = juce::MathConstants<float>::pi;
    constexpr auto start = -pi * (145.0f / 180.0f), range = pi * (290.0f / 180.0f);
    const auto value = juce::jlimit (0.0f, 1.0f, value01);
    juce::Path ring;
    ring.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, start + range, true);
    g.setColour (knobRing);
    g.strokePath (ring, juce::PathStrokeType (thickness));
    const auto from = bipolar ? start + range * 0.5f : start, to = start + range * value;
    if (std::abs (to - from) > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
        g.setColour (knobValue);
        g.strokePath (arc, juce::PathStrokeType (thickness));
    }
    const auto tip = radius + thickness * 0.5f;
    g.setColour (needle);
    g.drawLine (centre.x, centre.y, centre.x + std::sin (to) * tip, centre.y - std::cos (to) * tip, 1.5f);
    if (bipolar)
    {
        const auto top = centre.y - radius - thickness * 0.5f - 1.0f;
        juce::Path t;
        t.addTriangle (centre.x - 3.5f, top - 3.5f, centre.x + 3.5f, top - 3.5f, centre.x, top);
        g.setColour (knobRing);
        g.fillPath (t);
    }
}

// The power disc of a device's title bar: 13 px with a 1 px #121212 rim, filled #FFAD56 while on, #1E1E1E while off.
inline void drawPower (juce::Graphics& g, juce::Rectangle<float> r, bool on)
{
    g.setColour (wellRim);
    g.fillEllipse (r);
    g.setColour (on ? theme::livelook::on : well);
    g.fillEllipse (r.reduced (1.0f));
}

// A round 13 px well of a title bar or a rack's column (hot-swap, save, menu; R / M; the column toggles): a 1 px
// #121212 rim around #1E1E1E; lit = #FFAD56; disabled = #757575. Returns the colour for its glyph.
inline juce::Colour drawWell (juce::Graphics& g, juce::Rectangle<float> r, bool lit, bool enabled = true)
{
    g.setColour (wellRim);
    g.fillEllipse (r);
    g.setColour (! enabled ? wellDisabled : lit ? theme::livelook::on : well);
    g.fillEllipse (r.reduced (1.0f));
    return ! enabled ? surface : lit ? ink : text;
}
} // namespace lynx::theme::livelook

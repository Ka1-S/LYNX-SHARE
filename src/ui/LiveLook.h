#pragma once

// Round 190jn (design190ia slices 13, 17, 19, 25): the small parts of Ableton Live 12.4's look that the Clip-launch
// layout's views share — the open-ring knob, the power disc and the round title-bar wells. Measured in Live 12.4 (Dark
// theme, 100 %) on design190ia/shots/ref/live-session.png (Utility card 1075..1213 × 807..995), round 190ei's
// mn_drumrack-choke-menu.png (909 Core Kit) and round 183a's d02-utility-off.png. Drawn with LYNX vectors; only the
// Clip-launch layout in Live's look (theme::livelook::active()) calls these.

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/Theme.h"

namespace lynx::theme
{
// Live's knobs drawn from cached tiles (Theme.cpp, as theme::drawKnob: on Windows each arc drawn straight is a new
// Direct2D path geometry): `direct` paints one (bounds, value, default) into the tile; `style` keeps the kinds apart.
void drawLiveKnobTile (juce::Graphics& g, juce::Rectangle<float> bounds, float value01, float default01, int style,
                       void (*direct) (juce::Graphics&, juce::Rectangle<float>, float, float));
} // namespace lynx::theme

namespace lynx::theme::livelook
{
inline const juce::Colour knobValue { 0xff03c3d5 };  // a knob's value arc (909 Core Kit Low Gain / Tune / Decay)
inline const juce::Colour needle { 0xff181818 };     // a knob's needle
inline const juce::Colour wellRim { 0xff121212 };    // the 1 px rim of a power disc or a title-bar well
inline const juce::Colour wellDisabled { 0xff757575 }; // a rack column toggle that can't be used now
inline const juce::Colour padName { 0xffb5b5b5 };    // a Drum Rack pad's name area

// Live's knob: an open ring (#111111, ~2 px, open 70° at the bottom), a needle (#181818) from the centre through the
// ring, the value arc (#03C3D5) from the default to the value (Utility's Gain at its 0 dB default: no arc; a macro: from
// the start), and a small ▼ over the ring at the default when that is not the start (Utility's Gain, Mid/Side, Balance).
// `default01`: the default's place on the knob, 0..1. Measured at 25 px (Utility's Gain: ring x 1167..1191).
inline void drawKnobDirect (juce::Graphics& g, juce::Rectangle<float> bounds, float value01, float default01)
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
    const auto home = juce::jlimit (0.0f, 1.0f, default01);
    const auto from = start + range * home, to = start + range * value;
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
    if (home > 0.001f)
    {
        const auto r = radius + thickness * 0.5f + 1.0f; // (its tip just off the ring, pointing at the centre)
        const auto a = from;
        const auto tip = juce::Point<float> (centre.x + std::sin (a) * r, centre.y - std::cos (a) * r);
        const auto base = juce::Point<float> (centre.x + std::sin (a) * (r + 3.5f), centre.y - std::cos (a) * (r + 3.5f));
        const auto side = juce::Point<float> (std::cos (a) * 3.5f, std::sin (a) * 3.5f);
        juce::Path t;
        t.addTriangle (base.x - side.x, base.y - side.y, base.x + side.x, base.y + side.y, tip.x, tip.y);
        g.setColour (knobRing);
        g.fillPath (t);
    }
}

// Live's knob from a tile (the way to draw it; drawKnobDirect paints the tile).
inline void drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float value01, float default01)
{
    drawLiveKnobTile (g, bounds, value01, default01, 0, &drawKnobDirect);
}

// The same knob with a bipolar knob's default at its centre, any other's at its start.
inline void drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float value01, bool bipolar)
{
    drawKnob (g, bounds, value01, bipolar ? 0.5f : 0.0f);
}

// The Session mixer's knob (round 190jn slice 13, live-session.png x 403..429 y 581..605): an open ring 27 px across,
// 3 px thick in #111111, open at the bottom (from -135° to +135°), a 3 px #181818 needle from the centre to the rim
// (`default01` unused: the mixer's knobs show no value arc).
inline void drawMixerKnobDirect (juce::Graphics& g, juce::Rectangle<float> box, float value01, float)
{
    const auto centre = box.getCentre();
    const auto radius = box.getWidth() * 0.5f - 1.5f;
    constexpr auto from = -juce::MathConstants<float>::pi * 0.75f, to = juce::MathConstants<float>::pi * 0.75f;
    juce::Path ring;
    ring.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, from, to, true);
    g.setColour (knobRing);
    g.strokePath (ring, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    const auto angle = from + juce::jlimit (0.0f, 1.0f, value01) * (to - from);
    const auto tip = centre.getPointOnCircumference (radius + 1.0f, angle);
    g.setColour (needle);
    g.drawLine ({ centre, tip }, 3.0f);
}

// Round 190kc: a switched-off send knob of a return track (live-session.png x 1397..1424 y 581..608: Live draws its ring
// and needle #2A2A2A on the strip's #363636 instead of #111111 / #181818)
inline void drawMixerKnobOffDirect (juce::Graphics& g, juce::Rectangle<float> box, float value01, float)
{
    const auto centre = box.getCentre();
    const auto radius = box.getWidth() * 0.5f - 1.5f;
    constexpr auto from = -juce::MathConstants<float>::pi * 0.75f, to = juce::MathConstants<float>::pi * 0.75f;
    const juce::Colour off { 0xff2a2a2a };
    juce::Path ring;
    ring.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, from, to, true);
    g.setColour (off);
    g.strokePath (ring, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    const auto angle = from + juce::jlimit (0.0f, 1.0f, value01) * (to - from);
    g.drawLine ({ centre, centre.getPointOnCircumference (radius + 1.0f, angle) }, 3.0f);
}

// A small ▼ (`width` px across its top, odd) as rows of whole pixels from `top`: no path to build (Direct2D) and crisp.
inline void fillDownMark (juce::Graphics& g, float centreX, float top, int width, juce::Colour colour)
{
    g.setColour (colour);
    const auto left = std::round (centreX - (float) width * 0.5f);
    for (int row = 0; width - 2 * row > 0; ++row)
        g.fillRect (left + (float) row, std::round (top) + (float) row, (float) (width - 2 * row), 1.0f);
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

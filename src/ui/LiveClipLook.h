#pragma once

// Round 190jn (design190ia slice 17, CL-20a / CL-20b): the Clip View's widgets in Ableton Live 12.4's look, for the
// Clip-launch layout's Live look only (theme::livelook::active()). Measured on design190ia/shots/ref/live-clipview-audio.png
// (the Clip / Launch / Audio column 249..418 × 645..997) and ableton190o/shots/live-01-clipview-notes.png:
//   fields 13 px: a 1 px #111111 rim around #1E1E1E, text #B5B5B5; pills (Set, ÷2, ×2, Reverse, Edit) 13 px #242424
//   with a #111111 rim; toggles (Warp, Loop) #FFAD56 with #070707 text while on; labels #B5B5B5; section headers 17 px
//   #464646; the editor's tab bar 17 px #363636 with the shown tab #464646.
// style() swaps a widget's colours (and, for toggles and sliders, its look) to Live's and back: the colours a widget had
// before are kept in its properties, so LYNX's look comes back exactly as it was.

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/LiveLook.h"
#include "ui/Theme.h"

namespace lynx::liveclip
{
namespace ll = theme::livelook;
inline const juce::Colour pill { 0xff242424 };   // a small button's fill (Set, ÷2, ×2, Reverse, Edit)
inline const juce::Colour rim { 0xff111111 };    // a field's / pill's 1 px rim
inline const juce::Colour header { 0xff464646 }; // a section header; the editor's shown tab
constexpr int row = 13, headerHeight = 17, columnWidth = 170;

// Live's toggles (Loop, Legato: an orange pill while on), sliders (the Gain scale with its ▶ handle, the Pitch knob, the
// st / ct number fields) and their value texts
struct Laf final : juce::LookAndFeel_V4
{
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const auto on = b.getToggleState();
        g.setColour (rim);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (on ? ll::on : highlighted ? ll::raised : pill);
        g.fillRoundedRectangle (r.reduced (1.0f), 1.5f);
        g.setColour ((on ? ll::ink : ll::text).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
        g.setFont (theme::font (theme::type::small));
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, true);
    }
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical)
        {
            const auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
            g.setColour (rim);
            g.fillRect (r);
            g.setColour (ll::well);
            g.fillRect (r.reduced (1.0f));
            return;
        }
        // Live's Gain: a 3 px #111111 rail, ticks at its left, the ▶ handle left of the rail at the value
        const auto rail = juce::Rectangle<float> ((float) x + (float) w * 0.5f - 1.5f, (float) y, 3.0f, (float) h);
        g.setColour (rim);
        g.fillRect (rail);
        g.setColour (ll::text.withAlpha (0.6f));
        for (int k = 0; k <= 8; ++k)
            g.fillRect (rail.getX() - 5.0f, (float) y + (float) h * (float) k / 8.0f, 3.0f, 1.0f);
        juce::Path tri;
        tri.addTriangle (rail.getX() - 11.0f, pos - 5.0f, rail.getX() - 11.0f, pos + 5.0f, rail.getX() - 2.0f, pos);
        g.setColour (s.isMouseOverOrDragging() ? ll::text.brighter (0.3f) : ll::text);
        g.fillPath (tri);
    }
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& s) override
    {
        const auto size = (float) juce::jmin (27, juce::jmin (w, h));
        ll::drawKnob (g, juce::Rectangle<float> (size, size).withCentre (juce::Rectangle<int> (x, y, w, h).toFloat().getCentre()), pos, s.getMinimum() < 0.0);
    }
    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
        l->setColour (juce::Label::textColourId, ll::text);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setFont (theme::font (theme::type::small));
        return l;
    }
    juce::Font getLabelFont (juce::Label&) override { return theme::font (theme::type::small); }
    juce::Slider::SliderLayout getSliderLayout (juce::Slider& s) override
    {
        auto layout = juce::LookAndFeel_V4::getSliderLayout (s);
        if (s.getSliderStyle() == juce::Slider::LinearVertical) // (the rail leaves room for the ▶ at its left)
            layout.sliderBounds = layout.sliderBounds.reduced (0, 2);
        return layout;
    }
};

inline Laf& laf()
{
    static Laf l;
    return l;
}

// One colour of a widget set for Live's look (the earlier one kept) or given back.
inline void swapColour (juce::Component& c, int id, juce::Colour live, bool on)
{
    const auto key = juce::Identifier ("lynxLiveKept" + juce::String (id));
    auto& props = c.getProperties();
    if (on)
    {
        if (! props.contains (key))
            props.set (key, c.isColourSpecified (id) ? juce::var ((juce::int64) c.findColour (id).getARGB()) : juce::var ("unset"));
        c.setColour (id, live);
        return;
    }
    if (! props.contains (key))
        return;
    const auto kept = props[key];
    props.remove (key);
    if (kept.isString())
        c.removeColour (id);
    else
        c.setColour (id, juce::Colour ((juce::uint32) (juce::int64) kept));
}

// Live's colours on a widget (on) or LYNX's back (off). TextButtons that toggle (Warp, Reverse, Fade, Follow Action,
// Linked) light orange; plain ones are pills.
inline void style (juce::Component& c, bool on)
{
    using TB = juce::TextButton;
    if (auto* t = dynamic_cast<juce::ToggleButton*> (&c))
        t->setLookAndFeel (on ? &laf() : nullptr);
    else if (auto* b = dynamic_cast<TB*> (&c))
    {
        swapColour (c, TB::buttonColourId, pill, on);
        swapColour (c, TB::buttonOnColourId, ll::on, on);
        swapColour (c, TB::textColourOffId, ll::text, on);
        swapColour (c, TB::textColourOnId, ll::ink, on);
        (void) b;
    }
    else if (dynamic_cast<juce::ComboBox*> (&c) != nullptr)
    {
        swapColour (c, juce::ComboBox::backgroundColourId, ll::well, on);
        swapColour (c, juce::ComboBox::textColourId, ll::text, on);
        swapColour (c, juce::ComboBox::outlineColourId, rim, on);
    }
    else if (auto* e = dynamic_cast<juce::TextEditor*> (&c))
    {
        swapColour (c, juce::TextEditor::backgroundColourId, ll::well, on);
        swapColour (c, juce::TextEditor::textColourId, ll::text, on);
        swapColour (c, juce::TextEditor::outlineColourId, rim, on);
        e->applyColourToAllText (e->findColour (juce::TextEditor::textColourId), true);
    }
    else if (dynamic_cast<juce::Label*> (&c) != nullptr)
        swapColour (c, juce::Label::textColourId, ll::text, on);
    else if (auto* s = dynamic_cast<juce::Slider*> (&c))
    {
        s->setLookAndFeel (on ? &laf() : nullptr);
        swapColour (c, juce::Slider::textBoxTextColourId, ll::text, on);
        swapColour (c, juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack, on);
        swapColour (c, juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack, on);
    }
}

// A section header of Live's column (17 px #464646): its icon (a small LYNX glyph: 0 clip, 1 launch, 2 audio, 3 pitch
// & time), its title and the fold triangle (▼ open, ◀ folded) 12 px from the right (live-clipview-audio.png 771..805).
inline void paintHeader (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, int icon, bool open)
{
    g.setColour (header);
    g.fillRect (r);
    g.setColour (ll::separator);
    g.fillRect (r.getX(), r.getBottom(), r.getWidth(), 1);
    g.setColour (ll::text);
    const auto ix = (float) r.getX() + 6.0f, cy = (float) r.getCentreY();
    switch (icon)
    {
        case 0: g.drawRect (juce::Rectangle<float> (ix, cy - 3.5f, 7.0f, 7.0f), 1.0f); break;
        case 1:
            g.fillRect (ix, cy - 3.5f, 1.0f, 7.0f);
            g.fillRect (ix + 2.0f, cy - 1.0f, 3.0f, 2.0f);
            {
                juce::Path p;
                p.addTriangle (ix + 5.0f, cy - 3.0f, ix + 5.0f, cy + 3.0f, ix + 8.0f, cy);
                g.fillPath (p);
            }
            break;
        case 2:
            for (int k = 0; k < 5; ++k)
            {
                const auto hh = k == 2 ? 8.0f : k % 2 == 0 ? 3.0f : 6.0f;
                g.fillRect (ix + (float) k * 1.8f, cy - hh * 0.5f, 1.0f, hh);
            }
            break;
        default:
            g.fillRect (ix, cy - 3.0f, 7.0f, 1.0f);
            g.fillRect (ix, cy, 5.0f, 1.0f);
            g.fillRect (ix, cy + 3.0f, 7.0f, 1.0f);
            break;
    }
    g.setFont (theme::font (theme::type::small));
    g.drawText (title, r.withTrimmedLeft (19).withTrimmedRight (20), juce::Justification::centredLeft, true);
    juce::Path t;
    const auto tx = (float) r.getRight() - 15.0f;
    if (open)
        t.addTriangle (tx, cy - 2.5f, tx + 6.0f, cy - 2.5f, tx + 3.0f, cy + 2.5f);
    else
        t.addTriangle (tx + 5.0f, cy - 3.0f, tx + 5.0f, cy + 3.0f, tx, cy);
    g.fillPath (t);
}
} // namespace lynx::liveclip

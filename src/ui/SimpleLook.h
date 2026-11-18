#pragma once

// The Simple workflow's look options (round 190jc, design190ia slices 5, 14, 24 and owner decision 9).
//
// "Large controls" (View › Large controls, Simple layout only; off by default): the Simple track headers, the track dock
// and the sampler pads grow to touch size (44–48 px targets: BandLab / FL Studio Mobile on a touch screen). The other
// layouts never read it. Message thread only.

#include <juce_graphics/juce_graphics.h>

namespace lynx::ui::simplelook
{
inline bool& largeControls()
{
    static bool on = false;
    return on;
}

// `normal` px, or `large` px with Large controls on
inline int px (int normal, int large) { return largeControls() ? large : normal; }
inline float px (float normal, float large) { return largeControls() ? large : normal; }

// WCAG relative luminance and contrast ratio (sRGB)
inline float luminance (juce::Colour c)
{
    const auto lin = [] (float v) { return v <= 0.04045f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin (c.getFloatRed()) + 0.7152f * lin (c.getFloatGreen()) + 0.0722f * lin (c.getFloatBlue());
}
inline float contrast (juce::Colour a, juce::Colour b)
{
    const auto la = luminance (a), lb = luminance (b);
    return (juce::jmax (la, lb) + 0.05f) / (juce::jmin (la, lb) + 0.05f);
}

// `ink` lightened (on a dark `ground`) or darkened (on a light one) in small steps until it reads at `ratio`:1 (a coloured
// type icon on a plate tinted with the same colour)
inline juce::Colour readableOn (juce::Colour ink, juce::Colour ground, float ratio)
{
    const auto lighten = luminance (ground) < 0.18f;
    for (int i = 0; i < 12 && contrast (ink, ground) < ratio; ++i)
        ink = lighten ? ink.interpolatedWith (juce::Colours::white, 0.18f) : ink.interpolatedWith (juce::Colours::black, 0.18f);
    return ink;
}

// The label colour for text on a coloured fill (a pad, a tile): black on a light fill, white on a dark one, whichever
// reads better (design190ia SG-11: white "Hat" on orange was 2.71:1). The better of the two is at least 4.58:1 on any fill.
inline juce::Colour inkOn (juce::Colour fill)
{
    const juce::Colour dark (0xff000000);
    return contrast (juce::Colours::white, fill) >= contrast (dark, fill) ? juce::Colours::white : dark;
}

// --- the Simple workflow's small vector glyphs (round 190jc, design190ia SG-7..SG-10, SG-12: no more 2-5 px text glyphs
// for ‹ › × − +). LYNX's own shapes, stroked with round caps; `r` is the glyph's box, drawn centred in it.
enum class Glyph { chevronLeft, chevronRight, cross, plus, minus, piano, keyboard, notes, fx, pads, dials, headphones };

inline void drawGlyph (juce::Graphics& g, Glyph glyph, juce::Rectangle<float> r, juce::Colour colour, float stroke = 1.5f)
{
    g.setColour (colour);
    const juce::PathStrokeType line (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const auto c = r.getCentre();
    juce::Path p;
    switch (glyph)
    {
        case Glyph::chevronLeft:
        case Glyph::chevronRight:
        {
            // a chevron r.getWidth() wide and r.getHeight() tall
            const auto w = r.getWidth() * 0.5f, h = r.getHeight() * 0.5f, d = glyph == Glyph::chevronLeft ? 1.0f : -1.0f;
            p.startNewSubPath (c.x + d * w, c.y - h);
            p.lineTo (c.x - d * w, c.y);
            p.lineTo (c.x + d * w, c.y + h);
            g.strokePath (p, line);
            return;
        }
        case Glyph::cross:
        {
            const auto a = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
            p.startNewSubPath (c.x - a, c.y - a);
            p.lineTo (c.x + a, c.y + a);
            p.startNewSubPath (c.x - a, c.y + a);
            p.lineTo (c.x + a, c.y - a);
            g.strokePath (p, line);
            return;
        }
        case Glyph::plus:
        case Glyph::minus:
        {
            const auto a = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
            p.startNewSubPath (c.x - a, c.y);
            p.lineTo (c.x + a, c.y);
            if (glyph == Glyph::plus)
            {
                p.startNewSubPath (c.x, c.y - a);
                p.lineTo (c.x, c.y + a);
            }
            g.strokePath (p, line);
            return;
        }
        case Glyph::piano: // a grand piano's lid seen from above: the curved body and its keys
        {
            const auto b = r.reduced (r.getWidth() * 0.08f);
            p.startNewSubPath (b.getX(), b.getBottom());
            p.lineTo (b.getX(), b.getY() + b.getHeight() * 0.1f);
            p.quadraticTo (b.getX(), b.getY(), b.getX() + b.getWidth() * 0.3f, b.getY());
            p.quadraticTo (b.getX() + b.getWidth() * 0.62f, b.getY(), b.getX() + b.getWidth() * 0.62f, b.getY() + b.getHeight() * 0.36f);
            p.quadraticTo (b.getX() + b.getWidth() * 0.66f, b.getY() + b.getHeight() * 0.56f, b.getRight(), b.getY() + b.getHeight() * 0.6f);
            p.lineTo (b.getRight(), b.getBottom());
            p.closeSubPath();
            g.strokePath (p, line);
            const auto keys = b.withTop (b.getBottom() - b.getHeight() * 0.28f);
            g.drawLine (keys.getX(), keys.getY(), keys.getRight(), keys.getY(), stroke);
            for (int k = 1; k < 4; ++k)
            {
                const auto x = keys.getX() + keys.getWidth() * (float) k / 4.0f;
                g.drawLine (x, keys.getY(), x, keys.getBottom(), stroke * 0.8f);
            }
            return;
        }
        case Glyph::headphones: // round 190ld (DS-9, BandLab B22's monitor switch): a headband over two ear cups
        {
            const auto b = r.reduced (r.getWidth() * 0.08f);
            juce::Path band;
            band.addCentredArc (b.getCentreX(), b.getY() + b.getHeight() * 0.55f, b.getWidth() * 0.42f, b.getHeight() * 0.42f, 0.0f,
                                -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath (band, line);
            const auto cupW = b.getWidth() * 0.22f, cupH = b.getHeight() * 0.36f, cupY = b.getY() + b.getHeight() * 0.55f;
            for (const auto x : { b.getCentreX() - b.getWidth() * 0.42f - cupW * 0.5f, b.getCentreX() + b.getWidth() * 0.42f - cupW * 0.5f })
            {
                juce::Path cup;
                cup.addRoundedRectangle (x, cupY, cupW, cupH, cupW * 0.4f);
                g.strokePath (cup, line);
            }
            return;
        }
        case Glyph::keyboard: // a keyboard: a frame, white-key lines and three black keys
        {
            const auto b = r.withSizeKeepingCentre (r.getWidth(), r.getHeight() * 0.72f).reduced (stroke * 0.5f);
            p.addRoundedRectangle (b, 1.5f);
            g.strokePath (p, line);
            for (int k = 1; k < 5; ++k)
            {
                const auto x = b.getX() + b.getWidth() * (float) k / 5.0f;
                g.drawLine (x, b.getCentreY(), x, b.getBottom(), stroke * 0.8f);
            }
            for (int k : { 1, 2, 4 })
            {
                const auto x = b.getX() + b.getWidth() * (float) k / 5.0f;
                g.fillRect (juce::Rectangle<float> (b.getWidth() * 0.11f, b.getHeight() * 0.5f).withCentre ({ x, b.getY() + b.getHeight() * 0.25f }));
            }
            return;
        }
        case Glyph::notes: // the Editor: three note bars on a roll, stepped
        {
            const auto h = juce::jmax (2.0f, r.getHeight() * 0.16f);
            const float xs[] { 0.0f, 0.36f, 0.62f }, ys[] { 0.7f, 0.42f, 0.14f }, ws[] { 0.42f, 0.32f, 0.38f };
            for (int i = 0; i < 3; ++i)
                g.fillRoundedRectangle (r.getX() + r.getWidth() * xs[i], r.getY() + r.getHeight() * ys[i], r.getWidth() * ws[i], h, h * 0.4f);
            g.fillRect (juce::Rectangle<float> (r.getX(), r.getBottom() - stroke * 0.7f, r.getWidth(), stroke * 0.7f));
            return;
        }
        case Glyph::fx: // the Effects: a knob with its pointer and a short arc (LYNX's own "effect" sign)
        {
            const auto d = juce::jmin (r.getWidth(), r.getHeight()) - stroke;
            const auto knob = juce::Rectangle<float> (d, d).withCentre (c);
            p.addCentredArc (c.x, c.y, d * 0.5f, d * 0.5f, 0.0f, -2.4f, 2.4f, true);
            g.strokePath (p, line);
            g.fillEllipse (knob.reduced (d * 0.3f));
            g.drawLine (c.x, c.y, c.x + d * 0.36f * std::sin (0.8f), c.y - d * 0.36f * std::cos (0.8f), stroke);
            return;
        }
        case Glyph::pads: // the Pads: a 2 x 2 grid of rounded squares
        {
            const auto b = r.reduced (r.getWidth() * 0.06f);
            const auto gap = b.getWidth() * 0.16f, cell = (b.getWidth() - gap) * 0.5f;
            for (int i = 0; i < 4; ++i)
                g.fillRoundedRectangle (b.getX() + (float) (i % 2) * (cell + gap), b.getY() + (float) (i / 2) * (cell + gap), cell, cell, cell * 0.25f);
            return;
        }
        case Glyph::dials: // round 190ke, the Controls tab (GarageBand's Smart Controls dial): two knobs with their pointers
        {
            const auto d = juce::jmin (r.getWidth() * 0.5f, r.getHeight()) - stroke;
            for (int i = 0; i < 2; ++i)
            {
                const auto kc = juce::Point<float> (r.getX() + r.getWidth() * (i == 0 ? 0.25f : 0.75f), c.y + (i == 0 ? d * 0.12f : -d * 0.12f));
                g.drawEllipse (juce::Rectangle<float> (d, d).withCentre (kc), stroke);
                const auto a = i == 0 ? -0.9f : 1.1f;
                g.drawLine (kc.x, kc.y, kc.x + d * 0.42f * std::sin (a), kc.y - d * 0.42f * std::cos (a), stroke);
            }
            return;
        }
    }
}
} // namespace lynx::ui::simplelook

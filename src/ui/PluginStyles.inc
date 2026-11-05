// The alternative plugin-editor styles of the kit (REA round 126, "Plugin style" in Settings > General):
//   Neon      — near-black with saturated light of the family colour: full-panel washes, LED segment rings and neon
//               rings on knobs, neon-outlined glass, coloured section headers, a big glowing title;
//   Hardware  — premium studio gear in LYNX's own design: a brushed, anodized faceplate tinted by the family, screws,
//               engraved labels, big machined aluminium knobs with a printed scale and a lamp ring, backlit displays;
//   Vivid     — bright, flat and bold: a saturated colour block per family, dark cards, oversized friendly knobs,
//               chunky pills, heavy type and a springy opening.
// Included by PluginSkin.cpp (it shares that file's helpers). Only drawing: no editor's layout changes.

namespace styles
{
using juce::Colour;
namespace Colours = juce::Colours;
using juce::ColourGradient;
using juce::Rectangle;
using juce::Point;

inline Colour neonOf (Colour c) { return c.withSaturation (juce::jmin (1.0f, c.getSaturation() * 1.15f + 0.05f)).withBrightness (juce::jmin (1.0f, c.getBrightness() * 1.08f)); }
// the anodized faceplate's colour for a family (dark, a little of the accent)
inline Colour plateOf (Colour c) { return Colour (0xff2b2d33).interpolatedWith (c.withSaturation (c.getSaturation() * 0.55f).withBrightness (0.36f), 0.42f); }
// the vivid colour block of a family
inline Colour blockOf (Colour c) { return c.withSaturation (juce::jmin (1.0f, c.getSaturation() * 0.95f + 0.05f)).withBrightness (0.62f); }
constexpr float knobScaleHardware = 1.04f, knobScaleVivid = 1.1f;

inline void fillLinear (juce::Graphics& g, Rectangle<float> a, Colour top, Colour bottom)
{
    g.setGradientFill (ColourGradient (top, 0.0f, a.getY(), bottom, 0.0f, a.getBottom(), false));
    g.fillRect (a);
}

// a brushed-metal streak layer (rows of random brightness), drawn into cached layers only
inline void brushed (juce::Graphics& g, Rectangle<float> a, float strength)
{
    juce::Random rnd (4242);
    for (auto y = a.getY(); y < a.getBottom(); y += 1.0f)
    {
        const auto v = rnd.nextFloat();
        if (v < 0.35f) continue;
        g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (strength * (v - 0.35f)));
        const auto x0 = a.getX() + rnd.nextFloat() * a.getWidth() * 0.3f;
        g.fillRect (Rectangle<float> (x0, y, a.getWidth() * (0.4f + 0.6f * rnd.nextFloat()), 1.0f));
    }
}

inline void screw (juce::Graphics& g, Point<float> c, float d)
{
    const auto r = Rectangle<float> (d, d).withCentre (c);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillEllipse (r.expanded (1.2f).translated (0.0f, 0.8f));
    ColourGradient head (Colour (0xffeef0f3), c.x - d * 0.2f, c.y - d * 0.3f, Colour (0xff6b7079), c.x + d * 0.5f, c.y + d * 0.5f, true);
    g.setGradientFill (head);
    g.fillEllipse (r);
    juce::Path slot;
    slot.addRectangle (-d * 0.36f, -d * 0.07f, d * 0.72f, d * 0.14f);
    slot.applyTransform (juce::AffineTransform::rotation (0.6f).translated (c.x, c.y));
    g.setColour (Colour (0xff2a2c30));
    g.fillPath (slot);
}

// --- canvases (cached by the caller) ---------------------------------------------------------------
inline void canvas (juce::Graphics& g, Rectangle<float> a, Colour accent, bool display, Style s)
{
    const auto big = juce::jmax (a.getWidth(), a.getHeight());
    if (s == Style::neon)
    {
        const auto n = neonOf (accent);
        g.setColour (Colour (0xff050509));
        g.fillRect (a);
        // full-panel washes: the family colour from the upper left, a shifted hue from the lower right
        ColourGradient wash (n.withAlpha (display ? 0.34f : 0.40f), a.getX(), a.getY(), n.withAlpha (0.0f), a.getX() + a.getWidth() * 0.7f, a.getY() + a.getHeight() * 0.9f, false);
        g.setGradientFill (wash);
        g.fillRect (a);
        const auto second = n.withRotatedHue (accent == colour::crimson ? 0.08f : -0.12f);
        ColourGradient wash2 (second.withAlpha (0.0f), a.getX() + a.getWidth() * 0.35f, a.getY(), second.withAlpha (display ? 0.22f : 0.30f), a.getRight(), a.getBottom(), false);
        g.setGradientFill (wash2);
        g.fillRect (a);
        // a faint neon grid
        g.setColour (n.withAlpha (0.045f));
        for (auto x = a.getX() + 24.0f; x < a.getRight(); x += 24.0f) g.fillRect (Rectangle<float> (x, a.getY(), 1.0f, a.getHeight()));
        for (auto y = a.getY() + 24.0f; y < a.getBottom(); y += 24.0f) g.fillRect (Rectangle<float> (a.getX(), y, a.getWidth(), 1.0f));
    }
    else if (s == Style::hardware)
    {
        if (display) // a backlit glass display
        {
            const auto glass = accent.withSaturation (accent.getSaturation() * 0.6f).withBrightness (0.16f);
            fillLinear (g, a, glass.brighter (0.15f), Colour (0xff07080a));
            ColourGradient back (accent.withAlpha (0.2f), a.getCentreX(), a.getCentreY(), accent.withAlpha (0.0f), a.getCentreX() + big * 0.6f, a.getCentreY(), true);
            g.setGradientFill (back);
            g.fillRect (a);
            juce::Path sheen; // a diagonal reflection on the glass
            sheen.addQuadrilateral (a.getX(), a.getY(), a.getX() + a.getWidth() * 0.45f, a.getY(), a.getX() + a.getWidth() * 0.25f, a.getBottom(), a.getX(), a.getBottom());
            g.setColour (Colours::white.withAlpha (0.025f));
            g.fillPath (sheen);
        }
        else
        {
            const auto plate = plateOf (accent);
            fillLinear (g, a, plate.brighter (0.25f), plate.darker (0.35f));
            brushed (g, a, 0.07f);
            // a soft light across the top of the plate
            ColourGradient light (Colours::white.withAlpha (0.10f), a.getCentreX(), a.getY() - big * 0.1f, Colours::white.withAlpha (0.0f), a.getCentreX() + big * 0.5f, a.getY(), true);
            g.setGradientFill (light);
            g.fillRect (a);
            // bevelled edges and four screws
            g.setColour (Colours::white.withAlpha (0.18f));
            g.fillRect (a.withHeight (1.0f));
            g.fillRect (a.withWidth (1.0f));
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillRect (a.withTop (a.getBottom() - 1.0f));
            g.fillRect (a.withLeft (a.getRight() - 1.0f));
            if (a.getWidth() > 200.0f && a.getHeight() > 120.0f)
                for (const auto& p : { Point<float> { a.getX() + 7.0f, a.getBottom() - 7.0f }, { a.getRight() - 7.0f, a.getBottom() - 7.0f } })
                    screw (g, p, 7.0f);
        }
    }
    else // vivid
    {
        if (display)
        {
            const auto b = blockOf (accent);
            fillLinear (g, a, b.darker (0.9f), Colour (0xff0b0c12));
            ColourGradient pool (b.withAlpha (0.35f), a.getCentreX(), a.getBottom(), b.withAlpha (0.0f), a.getCentreX() + big * 0.55f, a.getBottom(), true);
            g.setGradientFill (pool);
            g.fillRect (a);
        }
        else
        {
            const auto b = blockOf (accent);
            fillLinear (g, a, b, b.darker (0.75f));
            // big playful circles of lighter colour
            g.setColour (Colours::white.withAlpha (0.07f));
            g.fillEllipse (Rectangle<float> (big * 0.7f, big * 0.7f).withCentre ({ a.getRight() - big * 0.08f, a.getY() + big * 0.05f }));
            g.setColour (Colours::black.withAlpha (0.08f));
            g.fillEllipse (Rectangle<float> (big * 0.5f, big * 0.5f).withCentre ({ a.getX() + big * 0.05f, a.getBottom() }));
        }
    }
}

// --- header ------------------------------------------------------------------------------------
inline void headerBar (juce::Graphics& g, Rectangle<float> a, Colour accent, Style s)
{
    if (s == Style::neon)
    {
        const auto n = neonOf (accent);
        g.setColour (Colour (0xff040406));
        g.fillRect (a);
        g.setGradientFill (ColourGradient (n.withAlpha (0.55f), a.getX(), 0.0f, n.withAlpha (0.06f), a.getRight(), 0.0f, false));
        g.fillRect (a);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.08f), 0.0f, a.getY(), Colours::white.withAlpha (0.0f), 0.0f, a.getCentreY(), false));
        g.fillRect (a.withHeight (a.getHeight() * 0.5f));
        if (! reduced)
        {
            g.setGradientFill (ColourGradient (n.withAlpha (0.45f), 0.0f, a.getBottom(), n.withAlpha (0.0f), 0.0f, a.getBottom() + 7.0f, false));
            g.fillRect (Rectangle<float> (a.getX(), a.getBottom(), a.getWidth(), 7.0f));
        }
        g.setColour (n.brighter (0.4f));
        g.fillRect (Rectangle<float> (a.getX(), a.getBottom() - 2.0f, a.getWidth(), 2.0f));
    }
    else if (s == Style::hardware)
    {
        fillLinear (g, a, Colour (0xff4a4d55), Colour (0xff23252a));
        brushed (g, a, 0.06f);
        g.setColour (Colours::white.withAlpha (0.22f));
        g.fillRect (a.withHeight (1.0f));
        g.setColour (Colours::black.withAlpha (0.8f));
        g.fillRect (a.withTop (a.getBottom() - 2.0f));
        g.setColour (accent.withAlpha (0.9f)); // a coloured pin stripe
        g.fillRect (Rectangle<float> (a.getX(), a.getBottom() - 4.0f, a.getWidth(), 2.0f));
    }
    else
    {
        g.setColour (Colour (0xff10121c));
        g.fillRect (a);
        g.setColour (blockOf (accent).brighter (0.2f));
        g.fillRect (Rectangle<float> (a.getX(), a.getBottom() - 4.0f, a.getWidth(), 4.0f));
    }
}

// round 170k2: `size` comes from skin::titleText, which fits the name in the box first (see titleLook)
inline float titleText (juce::Graphics& g, Rectangle<float> a, const juce::String& name, Colour accent, bool underline, Style s, float size)
{
    if (s == Style::neon)
    {
        const auto n = neonOf (accent);
        g.setFont (juce::Font (theme::font (size, true)).withExtraKerningFactor (0.06f));
        const auto w = juce::jmin (a.getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 4.0f);
        const auto box = a.withWidth (w);
        if (! reduced)
        {
            g.setColour (n.withAlpha (0.16f));
            for (const auto& o : { Point<float> { -2.0f, 0.0f }, { 2.0f, 0.0f }, { 0.0f, -2.0f }, { 0.0f, 2.0f }, { -1.5f, -1.5f }, { 1.5f, 1.5f } })
                g.drawText (name, box.translated (o.x, o.y), juce::Justification::centredLeft, true);
            g.setColour (n.withAlpha (0.45f));
            for (const auto& o : { Point<float> { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } })
                g.drawText (name, box.translated (o.x, o.y), juce::Justification::centredLeft, true);
        }
        g.setColour (n.interpolatedWith (Colours::white, 0.75f));
        g.drawText (name, box, juce::Justification::centredLeft, true);
        return w;
    }
    if (s == Style::hardware) // an engraved silver badge
    {
        g.setFont (juce::Font (theme::font (size, true)).withExtraKerningFactor (0.08f));
        const auto text = name.toUpperCase();
        const auto tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
        const auto w = juce::jmin (a.getWidth(), tw + 16.0f);
        const auto plate = Rectangle<float> (a.getX(), a.getCentreY() - size * 0.8f, w, size * 1.6f);
        g.setColour (Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (plate.translated (0.0f, 1.0f), 2.5f);
        g.setGradientFill (ColourGradient (Colour (0xffe4e6ea), 0.0f, plate.getY(), Colour (0xff9a9ea6), 0.0f, plate.getBottom(), false));
        g.fillRoundedRectangle (plate, 2.5f);
        brushed (g, plate.reduced (1.0f), 0.05f);
        g.setColour (Colours::white.withAlpha (0.7f));
        g.drawText (text, plate.translated (0.0f, 1.0f), juce::Justification::centred, true);
        g.setColour (Colour (0xff1c1e22));
        g.drawText (text, plate, juce::Justification::centred, true);
        return w;
    }
    // vivid: heavy, big, white
    g.setFont (juce::Font (theme::font (size, true)).withExtraKerningFactor (0.03f));
    const auto text = name.toUpperCase();
    const auto w = juce::jmin (a.getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 2.0f);
    g.setColour (blockOf (accent).darker (0.6f));
    g.drawText (text, a.withWidth (w).translated (1.5f, 1.5f), juce::Justification::centredLeft, true);
    g.setColour (Colours::white);
    g.drawText (text, a.withWidth (w), juce::Justification::centredLeft, true);
    juce::ignoreUnused (underline);
    return w;
}

// --- groups ------------------------------------------------------------------------------------
inline void section (juce::Graphics& g, Rectangle<float> a, float corner, bool raised, Colour tint, Style s)
{
    const auto accent = tint.getAlpha() > 0 ? tint : colour::crimson;
    if (s == Style::neon)
    {
        const auto n = neonOf (accent);
        g.setColour (Colour (0xc0050508));
        g.fillRoundedRectangle (a, corner);
        g.setGradientFill (ColourGradient (n.withAlpha (raised ? 0.12f : 0.08f), 0.0f, a.getY(), n.withAlpha (0.0f), 0.0f, a.getY() + juce::jmin (90.0f, a.getHeight()), false));
        g.fillRoundedRectangle (a, corner);
        if (! reduced && tint.getAlpha() > 0)
        {
            g.setColour (n.withAlpha (0.12f));
            g.drawRoundedRectangle (a.expanded (1.5f), corner + 1.5f, 3.0f);
        }
        g.setColour (tint.getAlpha() > 0 ? n.withAlpha (0.8f) : Colours::white.withAlpha (0.18f));
        g.drawRoundedRectangle (a.reduced (0.5f), corner, 1.2f);
        return;
    }
    if (s == Style::hardware) // a recessed panel milled into the plate
    {
        const auto plate = plateOf (accent).darker (raised ? 0.15f : 0.4f);
        g.setGradientFill (ColourGradient (plate.darker (0.2f), 0.0f, a.getY(), plate.brighter (0.05f), 0.0f, a.getBottom(), false));
        g.fillRoundedRectangle (a, corner * 0.6f);
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.55f), 0.0f, a.getY(), Colours::transparentBlack, 0.0f, a.getY() + 7.0f, false));
        g.fillRoundedRectangle (a.withHeight (juce::jmin (7.0f, a.getHeight())), corner * 0.6f);
        g.setColour (Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (a.reduced (0.5f), corner * 0.6f, 1.0f);
        g.setColour (Colours::white.withAlpha (0.14f));
        g.drawHorizontalLine ((int) a.getBottom(), a.getX() + corner, a.getRight() - corner);
        g.drawVerticalLine ((int) a.getRight(), a.getY() + corner, a.getBottom() - corner);
        return;
    }
    // vivid: a dark card
    const auto r = corner * 1.6f + 2.0f;
    g.setColour (Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (a.translated (0.0f, 3.0f), r);
    g.setColour (raised ? Colour (0xf21c1f2e) : Colour (0xf0151724));
    g.fillRoundedRectangle (a, r);
}

inline void sectionTitle (juce::Graphics& g, Rectangle<float> strip, const juce::String& text, Colour accent, float fontSize, Style s)
{
    g.setFont (juce::Font (theme::font (fontSize, true)).withExtraKerningFactor (0.12f));
    const auto w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
    const auto pill = Rectangle<float> (w + fontSize * 1.8f, juce::jmin (strip.getHeight(), fontSize + 6.0f)).withCentre (strip.getCentre());
    if (s == Style::neon) // a coloured header pill
    {
        const auto n = neonOf (accent);
        if (! reduced)
        {
            g.setColour (n.withAlpha (0.25f));
            g.fillRoundedRectangle (pill.expanded (3.0f), pill.getHeight() * 0.5f + 3.0f);
        }
        g.setGradientFill (ColourGradient (n.brighter (0.3f), pill.getX(), 0.0f, n.darker (0.1f), pill.getRight(), 0.0f, false));
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        g.setColour (Colour (0xff07070b));
        g.drawText (text, pill, juce::Justification::centred, false);
        g.setGradientFill (ColourGradient (n.withAlpha (0.0f), strip.getX() + 6.0f, 0.0f, n.withAlpha (0.7f), pill.getX() - 4.0f, 0.0f, false));
        g.fillRect (Rectangle<float> (strip.getX() + 6.0f, strip.getCentreY() - 0.5f, juce::jmax (0.0f, pill.getX() - 10.0f - strip.getX()), 1.0f));
        g.setGradientFill (ColourGradient (n.withAlpha (0.7f), pill.getRight() + 4.0f, 0.0f, n.withAlpha (0.0f), strip.getRight() - 6.0f, 0.0f, false));
        g.fillRect (Rectangle<float> (pill.getRight() + 4.0f, strip.getCentreY() - 0.5f, juce::jmax (0.0f, strip.getRight() - 10.0f - pill.getRight()), 1.0f));
        return;
    }
    if (s == Style::hardware) // silk-screened caps between engraved lines
    {
        g.setColour (Colours::black.withAlpha (0.6f));
        g.drawText (text, strip.translated (0.0f, 1.0f), juce::Justification::centred, false);
        g.setColour (Colour (0xffeceef2));
        g.drawText (text, strip, juce::Justification::centred, false);
        const auto y = (int) strip.getCentreY();
        for (const auto& seg : { std::pair<float, float> { strip.getX() + 8.0f, pill.getX() }, { pill.getRight(), strip.getRight() - 8.0f } })
            if (seg.second > seg.first)
            {
                g.setColour (Colours::black.withAlpha (0.5f));
                g.drawHorizontalLine (y, seg.first, seg.second);
                g.setColour (Colours::white.withAlpha (0.12f));
                g.drawHorizontalLine (y + 1, seg.first, seg.second);
            }
        return;
    }
    // vivid: chunky caps on a solid pill
    g.setColour (blockOf (accent));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (Colours::white);
    g.drawText (text, pill, juce::Justification::centred, false);
}

inline void well (juce::Graphics& g, Rectangle<float> a, float corner, Colour glow, Style s)
{
    const auto accent = glow.getAlpha() > 0 ? glow : Colour (0xff8c919c);
    if (s == Style::neon)
    {
        const auto n = neonOf (accent);
        g.setColour (Colour (0xff020204));
        g.fillRoundedRectangle (a, corner);
        if (! reduced && glow.getAlpha() > 0)
        {
            g.saveState();
            g.reduceClipRegion (a.toNearestInt());
            ColourGradient rise (n.withAlpha (0.28f), a.getCentreX(), a.getBottom(), n.withAlpha (0.0f), a.getCentreX() + a.getWidth() * 0.6f, a.getBottom(), true);
            g.setGradientFill (rise);
            g.fillRect (a);
            g.restoreState();
            g.setColour (n.withAlpha (0.14f));
            g.drawRoundedRectangle (a.expanded (1.5f), corner + 1.5f, 3.0f);
        }
        g.setColour (n.withAlpha (glow.getAlpha() > 0 ? 0.75f : 0.3f));
        g.drawRoundedRectangle (a.reduced (0.5f), corner, 1.2f);
        return;
    }
    if (s == Style::hardware) // a backlit display behind glass, in a dark bezel
    {
        g.setColour (Colour (0xff141518));
        g.fillRoundedRectangle (a.expanded (3.0f), corner + 2.0f);
        g.setColour (Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (a.expanded (3.0f).reduced (0.5f), corner + 2.0f, 1.0f);
        const auto glass = accent.withSaturation (accent.getSaturation() * 0.6f).withBrightness (0.13f);
        g.setGradientFill (ColourGradient (glass.brighter (0.2f), 0.0f, a.getY(), Colour (0xff060708), 0.0f, a.getBottom(), false));
        g.fillRoundedRectangle (a, corner);
        if (! reduced)
        {
            g.saveState();
            g.reduceClipRegion (a.toNearestInt());
            ColourGradient back (accent.withAlpha (0.16f), a.getCentreX(), a.getCentreY(), accent.withAlpha (0.0f), a.getCentreX() + juce::jmax (a.getWidth(), a.getHeight()) * 0.6f,
                                 a.getCentreY(), true);
            g.setGradientFill (back);
            g.fillRect (a);
            g.restoreState();
        }
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.6f), 0.0f, a.getY(), Colours::transparentBlack, 0.0f, a.getY() + 8.0f, false));
        g.fillRoundedRectangle (a.withHeight (juce::jmin (8.0f, a.getHeight())), corner);
        return;
    }
    g.setColour (Colour (0xff0b0c13));
    g.fillRoundedRectangle (a, corner * 1.5f + 2.0f);
    if (glow.getAlpha() > 0)
    {
        g.saveState();
        g.reduceClipRegion (a.toNearestInt());
        ColourGradient pool (blockOf (accent).withAlpha (0.25f), a.getCentreX(), a.getBottom(), blockOf (accent).withAlpha (0.0f), a.getCentreX() + a.getWidth() * 0.55f, a.getBottom(), true);
        g.setGradientFill (pool);
        g.fillRect (a);
        g.restoreState();
    }
}

inline void floatingPanel (juce::Graphics& g, Rectangle<float> r, Colour edge, float corner, Style s)
{
    juce::DropShadow (Colours::black.withAlpha (0.7f), 22, { 0, 8 }).drawForRectangle (g, r.toNearestInt());
    if (s == Style::neon)
    {
        const auto n = neonOf (edge);
        g.setColour (Colour (0xf0060609));
        g.fillRoundedRectangle (r, corner);
        g.setGradientFill (ColourGradient (n.withAlpha (0.14f), 0.0f, r.getY(), n.withAlpha (0.0f), 0.0f, r.getY() + r.getHeight() * 0.6f, false));
        g.fillRoundedRectangle (r, corner);
        if (! reduced)
        {
            g.setColour (n.withAlpha (0.18f));
            g.drawRoundedRectangle (r.expanded (2.0f), corner + 2.0f, 4.0f);
        }
        g.setColour (n);
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.5f);
        return;
    }
    if (s == Style::hardware)
    {
        const auto plate = Colour (0xff2c2e34);
        g.setGradientFill (ColourGradient (plate.brighter (0.25f), 0.0f, r.getY(), plate.darker (0.3f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 4.0f);
        brushed (g, r.reduced (2.0f), 0.04f);
        g.setColour (Colours::white.withAlpha (0.2f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 4.0f, r.getRight() - 4.0f);
        g.setColour (edge);
        g.fillRect (Rectangle<float> (r.getX() + 4.0f, r.getY(), r.getWidth() - 8.0f, 2.0f));
        g.setColour (Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        return;
    }
    g.setColour (Colour (0xfa161825));
    g.fillRoundedRectangle (r, corner + 4.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (r.reduced (1.0f), corner + 4.0f, 2.5f);
}

inline void bottomBar (juce::Graphics& g, Rectangle<float> a, Style s)
{
    if (s == Style::neon) { g.setColour (Colour (0xff040406)); g.fillRect (a); g.setColour (Colours::white.withAlpha (0.1f)); g.drawHorizontalLine ((int) a.getY(), a.getX(), a.getRight()); return; }
    if (s == Style::hardware)
    {
        fillLinear (g, a, Colour (0xff33363c), Colour (0xff1c1d21));
        brushed (g, a, 0.05f);
        g.setColour (Colours::black.withAlpha (0.8f));
        g.drawHorizontalLine ((int) a.getY(), a.getX(), a.getRight());
        g.setColour (Colours::white.withAlpha (0.15f));
        g.drawHorizontalLine ((int) a.getY() + 1, a.getX(), a.getRight());
        return;
    }
    g.setColour (Colour (0xff10121c));
    g.fillRect (a);
}

// --- knobs -------------------------------------------------------------------------------------
inline void knobNeon (juce::Graphics& g, Rectangle<float> area, const KnobLook& k)
{
    const auto size = area.getWidth();
    const auto c = area.getCentre();
    const auto small = size < 26.0f;
    const auto fx = ! small && ! reduced;
    const auto n = k.enabled ? neonOf (k.active ? k.accent.brighter (0.25f) : k.accent) : theme::colour::textDisabled;
    const auto value = juce::jlimit (0.0f, 1.0f, k.value);
    const auto hover = juce::jlimit (0.0f, 1.0f, k.active ? 1.0f : k.hover);
    const auto ringT = juce::jlimit (2.0f, 8.0f, size * 0.10f);
    const auto ringR = size * 0.5f - ringT * 0.6f;
    const auto from = k.bipolar ? arcStart + arcRange * 0.5f : arcStart;
    const auto to = arcStart + arcRange * value;
    // the LED segment ring: lit up to the value
    const int segs = small ? 0 : juce::jlimit (15, 31, (int) (size / 3.0f) | 1);
    for (int i = 0; i < segs; ++i)
    {
        const auto t = ((float) i + 0.5f) / (float) segs;
        const auto a = arcStart + arcRange * t;
        const auto lit = k.bipolar ? ((a >= juce::jmin (from, to) && a <= juce::jmax (from, to))) : t <= value + 0.0001f;
        juce::Path seg;
        const auto half = arcRange / (float) segs * 0.32f;
        seg.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, a - half, a + half, true);
        if (lit && k.enabled && fx)
        {
            g.setColour (n.withAlpha (0.22f + 0.15f * hover));
            g.strokePath (seg, roundStroke (ringT * 2.2f));
        }
        g.setColour (lit ? (k.enabled ? n.interpolatedWith (Colours::white, 0.25f) : n) : n.withAlpha (0.13f));
        g.strokePath (seg, juce::PathStrokeType (ringT, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }
    // the neon value ring inside it
    const auto innerR = segs > 0 ? ringR - ringT * 1.5f : size * 0.5f - ringT;
    const auto innerT = juce::jmax (1.5f, ringT * 0.55f);
    g.setColour (n.withAlpha (0.12f));
    g.strokePath (arcPath (c, innerR, arcStart, arcStart + arcRange), roundStroke (innerT));
    if (std::abs (to - from) > 0.002f)
    {
        const auto arc = arcPath (c, innerR, from, to);
        if (k.enabled && fx)
        {
            g.setColour (n.withAlpha (0.2f));
            g.strokePath (arc, roundStroke (innerT * 4.0f));
            g.setColour (n.withAlpha (0.45f));
            g.strokePath (arc, roundStroke (innerT * 2.0f));
        }
        g.setColour (n);
        g.strokePath (arc, roundStroke (innerT));
        if (fx)
        {
            g.setColour (Colours::white.withAlpha (0.85f));
            g.strokePath (arc, roundStroke (innerT * 0.35f));
        }
    }
    // a glossy black cap with a neon rim and pointer
    const auto capR = juce::jmax (3.0f, innerR - innerT * 2.2f);
    const auto cap = Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
    g.setGradientFill (ColourGradient (Colour (0xff22232b).brighter (hover * 0.15f), c.x, cap.getY(), Colour (0xff050507), c.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    if (fx)
    {
        ColourGradient spec (Colours::white.withAlpha (0.16f), c.x - capR * 0.3f, c.y - capR * 0.55f, Colours::white.withAlpha (0.0f), c.x + capR * 0.3f, c.y - capR * 0.55f, true);
        g.setGradientFill (spec);
        g.fillEllipse (cap.reduced (capR * 0.1f));
    }
    g.setColour (n.withAlpha (k.enabled ? 0.55f + 0.3f * hover : 0.3f));
    g.drawEllipse (cap.reduced (0.5f), 1.2f);
    const Point<float> dir { std::sin (to), -std::cos (to) };
    juce::Path pointer;
    pointer.startNewSubPath (c + dir * capR * 0.25f);
    pointer.lineTo (c + dir * capR * 0.85f);
    const auto pw = juce::jlimit (1.5f, 4.0f, size * 0.055f);
    if (k.enabled && fx)
    {
        g.setColour (n.withAlpha (0.4f));
        g.strokePath (pointer, roundStroke (pw * 3.2f));
    }
    g.setColour (k.enabled ? n.interpolatedWith (Colours::white, 0.6f) : theme::colour::textSecondary);
    g.strokePath (pointer, roundStroke (pw));
}

inline void knobHardware (juce::Graphics& g, Rectangle<float> area, const KnobLook& k)
{
    const auto size = area.getWidth();
    const auto c = area.getCentre();
    const auto small = size < 26.0f;
    const auto value = juce::jlimit (0.0f, 1.0f, k.value);
    const auto hover = juce::jlimit (0.0f, 1.0f, k.active ? 1.0f : k.hover);
    const auto lamp = k.enabled ? k.accent.brighter (k.active ? 0.3f : 0.05f) : theme::colour::textDisabled;
    const auto outerR = size * 0.5f;
    // the printed scale: ticks on the faceplate
    const auto tickR0 = outerR - juce::jmax (2.0f, size * 0.07f), tickR1 = outerR;
    if (! small)
        for (int i = 0; i <= 10; ++i)
        {
            const auto a = arcStart + arcRange * (float) i / 10.0f;
            const Point<float> d { std::sin (a), -std::cos (a) };
            g.setColour (Colours::white.withAlpha (i % 5 == 0 ? 0.75f : 0.4f));
            g.drawLine (juce::Line<float> (c + d * (i % 5 == 0 ? tickR0 - 1.5f : tickR0), c + d * tickR1), i % 5 == 0 ? 1.4f : 1.0f);
        }
    // the lamp ring: a thin lit arc in the family colour
    const auto lampR = tickR0 - juce::jmax (2.0f, size * 0.05f);
    const auto lampT = juce::jlimit (1.5f, 3.5f, size * 0.04f);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.strokePath (arcPath (c, lampR, arcStart, arcStart + arcRange), roundStroke (lampT + 1.5f));
    const auto from = k.bipolar ? arcStart + arcRange * 0.5f : arcStart;
    const auto to = arcStart + arcRange * value;
    if (std::abs (to - from) > 0.002f)
    {
        const auto arc = arcPath (c, lampR, from, to);
        if (k.enabled && ! reduced && ! small)
        {
            g.setColour (lamp.withAlpha (0.3f));
            g.strokePath (arc, roundStroke (lampT * 3.0f));
        }
        g.setColour (lamp.brighter (0.2f));
        g.strokePath (arc, roundStroke (lampT));
    }
    // the knob: a shadow, a dark knurled skirt, a brushed aluminium top
    const auto skirtR = lampR - lampT * 1.6f;
    const auto skirt = Rectangle<float> (skirtR * 2.0f, skirtR * 2.0f).withCentre (c);
    if (! small)
    {
        ColourGradient shadow (Colours::black.withAlpha (0.7f), c.x + skirtR * 0.12f, c.y + skirtR * 0.25f, Colours::transparentBlack, c.x + skirtR * 1.35f, c.y + skirtR * 0.25f, true);
        g.setGradientFill (shadow);
        g.fillEllipse (skirt.expanded (skirtR * 0.32f).translated (skirtR * 0.08f, skirtR * 0.18f));
    }
    g.setGradientFill (ColourGradient (Colour (0xff3d4047), c.x, skirt.getY(), Colour (0xff0e0f12), c.x, skirt.getBottom(), false));
    g.fillEllipse (skirt);
    if (! small) // knurling
    {
        g.setColour (Colours::black.withAlpha (0.45f));
        const int teeth = juce::jlimit (24, 60, (int) (skirtR * 1.8f));
        for (int i = 0; i < teeth; ++i)
        {
            const auto a = juce::MathConstants<float>::twoPi * (float) i / (float) teeth + to;
            const Point<float> d { std::sin (a), -std::cos (a) };
            g.drawLine (juce::Line<float> (c + d * skirtR * 0.8f, c + d * skirtR), 1.0f);
        }
    }
    const auto topR = skirtR * (small ? 0.86f : 0.76f);
    const auto top = Rectangle<float> (topR * 2.0f, topR * 2.0f).withCentre (c);
    ColourGradient metal (Colour (0xfff2f3f5).darker (0.05f - hover * 0.05f), c.x - topR * 0.45f, c.y - topR * 0.55f, Colour (0xff7c818b), c.x + topR * 0.9f, c.y + topR * 0.9f, true);
    metal.addColour (0.55, Colour (0xffb8bcc3));
    g.setGradientFill (metal);
    g.fillEllipse (top);
    if (! small && ! reduced) // turned finish
    {
        g.setColour (Colours::black.withAlpha (0.05f));
        for (auto rr = topR * 0.15f; rr < topR; rr += juce::jmax (1.3f, topR * 0.06f))
            g.drawEllipse (Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 0.6f);
    }
    g.setColour (Colours::white.withAlpha (0.6f));
    g.drawEllipse (top.reduced (0.5f), 0.8f);
    // the engraved pointer and its coloured dot
    const Point<float> dir { std::sin (to), -std::cos (to) };
    const auto pw = juce::jlimit (1.4f, 3.2f, size * 0.04f);
    g.setColour (Colour (0xff15161a));
    g.drawLine (juce::Line<float> (c + dir * topR * 0.15f, c + dir * topR * 0.92f), pw);
    if (! small)
    {
        const auto dot = c + dir * (skirtR * 0.9f);
        g.setColour (k.enabled ? lamp.brighter (0.4f) : theme::colour::textSecondary);
        g.fillEllipse (Rectangle<float> (pw * 1.6f, pw * 1.6f).withCentre (dot));
    }
}

inline void knobVivid (juce::Graphics& g, Rectangle<float> area, const KnobLook& k)
{
    const auto size = area.getWidth();
    const auto c = area.getCentre();
    const auto small = size < 26.0f;
    const auto value = juce::jlimit (0.0f, 1.0f, k.value);
    const auto hover = juce::jlimit (0.0f, 1.0f, k.active ? 1.0f : k.hover);
    const auto b = k.enabled ? blockOf (k.accent).brighter (0.25f + 0.15f * hover) : theme::colour::textDisabled;
    const auto t = juce::jlimit (2.5f, 11.0f, size * 0.13f);
    const auto r = size * 0.5f - t * 0.5f;
    g.setColour (Colours::white.withAlpha (0.10f + 0.05f * hover));
    g.strokePath (arcPath (c, r, arcStart, arcStart + arcRange), roundStroke (t));
    const auto from = k.bipolar ? arcStart + arcRange * 0.5f : arcStart;
    const auto to = arcStart + arcRange * value;
    if (std::abs (to - from) > 0.002f)
    {
        g.setColour (k.enabled ? Colours::white : theme::colour::textSecondary);
        g.strokePath (arcPath (c, r, from, to), roundStroke (t));
    }
    // a big flat disc in the family colour, its upper half lighter, a chunky white pointer
    const auto capR = r - t * (small ? 0.9f : 0.95f);
    const auto cap = Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillEllipse (cap.translated (0.0f, juce::jmax (1.5f, size * 0.035f)));
    g.setColour (b);
    g.fillEllipse (cap);
    {
        juce::Path half;
        half.addPieSegment (cap, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, 0.0f);
        half.applyTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, c.x, c.y));
        g.setColour (Colours::white.withAlpha (0.12f));
        g.fillPath (half);
    }
    const Point<float> dir { std::sin (to), -std::cos (to) };
    const auto pw = juce::jlimit (2.0f, 7.0f, size * 0.085f);
    juce::Path pointer;
    pointer.startNewSubPath (c + dir * capR * 0.3f);
    pointer.lineTo (c + dir * capR * 0.82f);
    g.setColour (k.enabled ? Colours::white : theme::colour::textSecondary);
    g.strokePath (pointer, roundStroke (pw));
    if (k.active)
    {
        g.setColour (Colours::white.withAlpha (0.35f));
        g.drawEllipse (area.reduced (0.5f), 2.0f);
    }
}

// --- controls ----------------------------------------------------------------------------------
inline void button (juce::Graphics& g, Rectangle<float> r, bool lit, bool hover, Colour accent, float corner, Style s)
{
    if (s == Style::neon)
    {
        const auto n = neonOf (accent);
        g.setColour (lit ? n.withAlpha (0.32f) : Colour (0xff07070a));
        g.fillRoundedRectangle (r, corner);
        if (lit && ! reduced)
        {
            g.setColour (n.withAlpha (hover ? 0.3f : 0.2f));
            g.drawRoundedRectangle (r.expanded (2.0f), corner + 2.0f, 4.0f);
        }
        g.setColour (lit ? n.brighter (0.3f) : n.withAlpha (hover ? 0.7f : 0.3f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, lit ? 1.5f : 1.0f);
        return;
    }
    if (s == Style::hardware)
    {
        g.setColour (Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 3.0f);
        if (lit)
        {
            if (! reduced) { g.setColour (accent.withAlpha (0.25f)); g.fillRoundedRectangle (r.expanded (3.0f), 5.0f); }
            g.setGradientFill (ColourGradient (accent.brighter (0.45f), 0.0f, r.getY(), accent.darker (0.2f), 0.0f, r.getBottom(), false));
        }
        else
            g.setGradientFill (ColourGradient (Colour (hover ? 0xff52565f : 0xff464950), 0.0f, r.getY(), Colour (0xff1f2024), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (Colours::white.withAlpha (lit ? 0.45f : 0.2f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 3.0f, r.getRight() - 3.0f);
        g.setColour (Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        return;
    }
    const auto pill = juce::jmin (r.getHeight() * 0.5f, corner + 6.0f);
    g.setColour (lit ? blockOf (accent).brighter (0.2f) : Colour (hover ? 0xff30344a : 0xff252839));
    g.fillRoundedRectangle (r, pill);
    if (lit)
    {
        g.setColour (Colours::white.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (1.0f), pill, 2.0f);
    }
}

inline void switchPill (juce::Graphics& g, Rectangle<float> box, bool on, Colour accent, bool enabled, Style s)
{
    const auto rr = box.getHeight() * 0.5f;
    const auto c = enabled ? accent : theme::colour::textSecondary;
    const auto d = box.getHeight() - 2.0f;
    const auto knobR = Rectangle<float> (d, d).withCentre ({ on ? box.getRight() - d * 0.5f - 1.0f : box.getX() + d * 0.5f + 1.0f, box.getCentreY() });
    if (s == Style::neon)
    {
        const auto n = neonOf (c);
        g.setColour (on ? n.withAlpha (0.35f) : Colour (0xff050507));
        g.fillRoundedRectangle (box, rr);
        if (on && ! reduced) { g.setColour (n.withAlpha (0.25f)); g.drawRoundedRectangle (box.expanded (1.5f), rr + 1.5f, 3.0f); }
        g.setColour (on ? n : n.withAlpha (0.35f));
        g.drawRoundedRectangle (box.reduced (0.5f), rr, 1.0f);
        g.setColour (on ? n.interpolatedWith (Colours::white, 0.6f) : n.withAlpha (0.4f));
        g.fillEllipse (knobR.reduced (1.0f));
        return;
    }
    if (s == Style::hardware)
    {
        g.setColour (Colour (0xff0b0c0e));
        g.fillRoundedRectangle (box, rr);
        if (on) { g.setColour (c.withAlpha (0.8f)); g.fillRoundedRectangle (box.reduced (1.5f), rr - 1.5f); }
        g.setColour (Colours::black.withAlpha (0.5f));
        g.fillEllipse (knobR.translated (0.0f, 1.0f));
        g.setGradientFill (ColourGradient (Colour (0xffeef0f2), knobR.getCentreX(), knobR.getY(), Colour (0xff80858d), knobR.getCentreX(), knobR.getBottom(), false));
        g.fillEllipse (knobR);
        return;
    }
    g.setColour (on ? blockOf (c).brighter (0.25f) : Colour (0xff2a2e40));
    g.fillRoundedRectangle (box.expanded (1.0f), rr + 1.0f);
    g.setColour (Colours::white);
    g.fillEllipse (knobR);
}

inline void selector (juce::Graphics& g, Rectangle<float> r, bool hover, float corner, Style s)
{
    if (s == Style::neon)
    {
        g.setColour (Colour (0xff07070a));
        g.fillRoundedRectangle (r, corner);
        g.setColour (Colours::white.withAlpha (hover ? 0.45f : 0.22f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
        return;
    }
    if (s == Style::hardware) { button (g, r, false, hover, colour::crimson, corner, s); return; }
    g.setColour (Colour (hover ? 0xff30344a : 0xff252839));
    g.fillRoundedRectangle (r, juce::jmin (r.getHeight() * 0.5f, corner + 6.0f));
}

inline void slider (juce::Graphics& g, Rectangle<float> t, float v, bool vertical, bool bipolar, Colour accent, bool hot, bool enabled, float thumbLength,
                    float thumbThickness, Style s)
{
    const auto thick = vertical ? t.getWidth() : t.getHeight();
    Rectangle<float> fill;
    Point<float> thumb;
    if (vertical)
    {
        const auto y = t.getBottom() - t.getHeight() * v;
        const auto from = bipolar ? t.getCentreY() : t.getBottom();
        fill = { t.getX(), juce::jmin (y, from), t.getWidth(), std::abs (from - y) };
        thumb = { t.getCentreX(), y };
    }
    else
    {
        const auto x = t.getX() + t.getWidth() * v;
        const auto from = bipolar ? t.getCentreX() : t.getX();
        fill = { juce::jmin (x, from), t.getY(), std::abs (from - x), t.getHeight() };
        thumb = { x, t.getCentreY() };
    }
    const auto c = enabled ? accent : theme::colour::textDisabled;
    const auto tr = vertical ? Rectangle<float> (thumbLength, thumbThickness).withCentre (thumb) : Rectangle<float> (thumbThickness, thumbLength).withCentre (thumb);
    if (s == Style::neon)
    {
        const auto n = neonOf (c);
        g.setColour (n.withAlpha (0.12f));
        g.fillRoundedRectangle (t, thick * 0.5f);
        if (enabled && ! reduced && ! fill.isEmpty()) { g.setColour (n.withAlpha (0.25f)); g.fillRoundedRectangle (fill.expanded (thick), thick * 1.5f); }
        g.setColour (n);
        g.fillRoundedRectangle (fill, thick * 0.5f);
        g.setColour (Colour (0xff050507));
        g.fillRoundedRectangle (tr, 3.0f);
        g.setColour (n.interpolatedWith (Colours::white, hot ? 0.6f : 0.3f));
        g.drawRoundedRectangle (tr.reduced (0.5f), 3.0f, 1.5f);
        return;
    }
    if (s == Style::hardware)
    {
        g.setColour (Colour (0xff050506));
        g.fillRoundedRectangle (t.expanded (1.5f), thick * 0.5f + 1.5f);
        g.setColour (c.withAlpha (0.85f));
        g.fillRoundedRectangle (fill, thick * 0.5f);
        g.setColour (Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (tr.translated (0.0f, 2.0f), 2.0f);
        g.setGradientFill (ColourGradient (Colour (0xffeef0f3), 0.0f, tr.getY(), Colour (0xff7d828b), 0.0f, tr.getBottom(), false));
        g.fillRoundedRectangle (tr, 2.0f);
        g.setColour (Colour (0xff15161a));
        if (vertical) g.fillRect (Rectangle<float> (tr.getX() + 2.0f, thumb.y - 0.75f, tr.getWidth() - 4.0f, 1.5f));
        else g.fillRect (Rectangle<float> (thumb.x - 0.75f, tr.getY() + 2.0f, 1.5f, tr.getHeight() - 4.0f));
        return;
    }
    const auto big = t.expanded (vertical ? thick * 0.4f : 0.0f, vertical ? 0.0f : thick * 0.4f);
    g.setColour (Colours::white.withAlpha (0.12f));
    g.fillRoundedRectangle (big, thick);
    g.setColour (enabled ? Colours::white : theme::colour::textSecondary);
    g.fillRoundedRectangle (vertical ? fill.expanded (thick * 0.4f, 0.0f) : fill.expanded (0.0f, thick * 0.4f), thick);
    const auto d = juce::jmax (thumbThickness, thick * 3.2f);
    const auto knobR = Rectangle<float> (d, d).withCentre (thumb);
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillEllipse (knobR.translated (0.0f, 2.0f));
    g.setColour (blockOf (c).brighter (hot ? 0.4f : 0.25f));
    g.fillEllipse (knobR);
    g.setColour (Colours::white);
    g.drawEllipse (knobR.reduced (1.0f), 2.0f);
}

inline void meter (juce::Graphics& g, Rectangle<float> r, float lv, float pk, bool vertical, Style s)
{
    auto gradient = [&] (float alpha) {
        auto gr = vertical ? ColourGradient (theme::colour::meterLow.withAlpha (alpha), 0.0f, r.getBottom(), theme::colour::meterClip.withAlpha (alpha), 0.0f, r.getY(), false)
                           : ColourGradient (theme::colour::meterLow.withAlpha (alpha), r.getX(), 0.0f, theme::colour::meterClip.withAlpha (alpha), r.getRight(), 0.0f, false);
        gr.addColour (0.72, theme::colour::meterMid.withAlpha (alpha));
        return gr;
    };
    const auto lit = vertical ? r.withTop (r.getBottom() - r.getHeight() * lv) : r.withWidth (r.getWidth() * lv);
    if (s == Style::vivid) // flat, chunky, rounded
    {
        g.setColour (Colour (0xff0b0c13));
        g.fillRoundedRectangle (r.expanded (1.5f), juce::jmin (r.getWidth(), r.getHeight()) * 0.5f);
        if (lv > 0.0f) { g.setGradientFill (gradient (1.0f)); g.fillRoundedRectangle (lit, juce::jmin (lit.getWidth(), lit.getHeight()) * 0.5f); }
        if (pk > 0.01f)
        {
            g.setColour (Colours::white);
            if (vertical) g.fillRoundedRectangle (Rectangle<float> (r.getX(), r.getBottom() - r.getHeight() * pk - 1.5f, r.getWidth(), 3.0f), 1.5f);
            else g.fillRoundedRectangle (Rectangle<float> (r.getX() + r.getWidth() * pk - 1.5f, r.getY(), 3.0f, r.getHeight()), 1.5f);
        }
        return;
    }
    // neon and hardware: LED blocks (hardware: chunkier, in a bezel)
    const auto step = s == Style::hardware ? 4.0f : 3.0f;
    g.setColour (s == Style::hardware ? Colour (0xff141518) : Colour (0xff020203));
    g.fillRoundedRectangle (r.expanded (s == Style::hardware ? 2.5f : 1.0f), 2.5f);
    g.setGradientFill (gradient (s == Style::neon ? 0.14f : 0.12f));
    g.fillRect (r);
    if (lv > 0.0f)
    {
        if (! reduced) { g.setGradientFill (gradient (s == Style::neon ? 0.35f : 0.2f)); g.fillRect (vertical ? lit.expanded (2.5f, 0.0f) : lit.expanded (0.0f, 2.5f)); }
        g.setGradientFill (gradient (1.0f));
        g.fillRect (lit);
    }
    g.setColour (Colour (0xff020203));
    if (vertical) for (auto y = r.getBottom() - step; y > r.getY(); y -= step) g.fillRect (Rectangle<float> (r.getX(), y, r.getWidth(), s == Style::hardware ? 1.5f : 1.0f));
    else for (auto x = r.getX() + step; x < r.getRight(); x += step) g.fillRect (Rectangle<float> (x, r.getY(), s == Style::hardware ? 1.5f : 1.0f, r.getHeight()));
    if (pk > 0.01f)
    {
        g.setColour (pk > 0.97f ? theme::colour::meterClip : Colours::white);
        if (vertical) g.fillRect (Rectangle<float> (r.getX(), r.getBottom() - r.getHeight() * pk - 1.0f, r.getWidth(), 2.0f));
        else g.fillRect (Rectangle<float> (r.getX() + r.getWidth() * pk - 1.0f, r.getY(), 2.0f, r.getHeight()));
    }
}

inline void meterSolid (juce::Graphics& g, Rectangle<float> r, float lv, Colour c, bool fromTop, Style s)
{
    const auto lit = fromTop ? r.withHeight (r.getHeight() * lv) : r.withTop (r.getBottom() - r.getHeight() * lv);
    if (s == Style::vivid)
    {
        g.setColour (Colour (0xff0b0c13));
        g.fillRoundedRectangle (r.expanded (1.5f), r.getWidth() * 0.5f);
        if (lv > 0.0f) { g.setColour (c); g.fillRoundedRectangle (lit, r.getWidth() * 0.5f); }
        return;
    }
    const auto col = s == Style::neon ? neonOf (c) : c;
    g.setColour (s == Style::hardware ? Colour (0xff141518) : Colour (0xff020203));
    g.fillRoundedRectangle (r.expanded (s == Style::hardware ? 2.5f : 1.0f), 2.5f);
    g.setColour (col.withAlpha (0.12f));
    g.fillRect (r);
    if (lv > 0.0f)
    {
        if (! reduced) { g.setColour (col.withAlpha (s == Style::neon ? 0.35f : 0.2f)); g.fillRect (lit.expanded (2.5f, 0.0f)); }
        g.setColour (col);
        g.fillRect (lit);
    }
    g.setColour (Colour (0xff020203));
    const auto step = s == Style::hardware ? 4.0f : 3.0f;
    for (auto y = r.getBottom() - step; y > r.getY(); y -= step) g.fillRect (Rectangle<float> (r.getX(), y, r.getWidth(), 1.0f));
}

inline void glowStroke (juce::Graphics& g, const juce::Path& p, Colour c, float thickness, float glow, Style s)
{
    if (s == Style::neon)
    {
        const auto n = neonOf (c);
        if (glow > 0.0f && ! reduced)
        {
            g.setColour (n.withAlpha (0.08f * glow));
            g.strokePath (p, roundStroke (thickness * 8.0f));
            g.setColour (n.withAlpha (0.22f * glow));
            g.strokePath (p, roundStroke (thickness * 3.6f));
        }
        g.setColour (n);
        g.strokePath (p, roundStroke (thickness * 1.4f));
        if (glow > 0.0f) { g.setColour (Colours::white.withAlpha (0.85f * glow)); g.strokePath (p, roundStroke (thickness * 0.5f)); }
        return;
    }
    if (s == Style::hardware) // a phosphor trace
    {
        if (glow > 0.0f && ! reduced) { g.setColour (c.withAlpha (0.18f * glow)); g.strokePath (p, roundStroke (thickness * 3.5f)); }
        g.setColour (c.brighter (0.25f));
        g.strokePath (p, roundStroke (thickness));
        return;
    }
    g.setColour (Colours::black.withAlpha (0.25f));
    g.strokePath (p, roundStroke (thickness * 1.6f), juce::AffineTransform::translation (0.0f, 2.0f));
    g.setColour (c.brighter (0.15f));
    g.strokePath (p, roundStroke (thickness * 1.6f));
}

inline void valuePlate (juce::Graphics& g, Rectangle<float> r, float corner, Style s)
{
    if (s == Style::neon)
    {
        g.setColour (Colour (0xff020203));
        g.fillRoundedRectangle (r, corner);
        g.setColour (Colours::white.withAlpha (0.16f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
        return;
    }
    if (s == Style::hardware) // an LCD window in a bezel
    {
        g.setColour (Colour (0xff141518));
        g.fillRoundedRectangle (r.expanded (1.5f), corner + 1.0f);
        g.setGradientFill (ColourGradient (Colour (0xff1d2420), 0.0f, r.getY(), Colour (0xff0a0d0b), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, corner);
        g.setColour (Colours::white.withAlpha (0.1f));
        g.drawHorizontalLine ((int) r.getBottom() + 1, r.getX() + corner, r.getRight() - corner);
        return;
    }
    g.setColour (Colour (0xff0c0d15));
    g.fillRoundedRectangle (r, juce::jmin (r.getHeight() * 0.5f, corner + 5.0f));
}

inline void powerLed (juce::Graphics& g, Point<float> c, float d, bool on, Style s)
{
    if (s == Style::vivid)
    {
        g.setColour (on ? theme::colour::ledOn : Colour (0xff3a3e52));
        g.fillEllipse (Rectangle<float> (d + 2.0f, d + 2.0f).withCentre (c));
        return;
    }
    const auto col = on ? theme::colour::ledOn : theme::colour::ledOff;
    if (on && ! reduced) bloom (g, c, d * (s == Style::neon ? 2.6f : 1.8f), col, 0.6f);
    g.setColour (Colours::black.withAlpha (0.7f));
    g.fillEllipse (Rectangle<float> (d + 2.5f, d + 2.5f).withCentre (c));
    g.setColour (on ? col.brighter (0.3f) : col);
    g.fillEllipse (Rectangle<float> (d, d).withCentre (c));
    g.setColour (Colours::white.withAlpha (on ? 0.7f : 0.15f));
    g.fillEllipse (Rectangle<float> (d * 0.4f, d * 0.4f).withCentre (c.translated (-d * 0.12f, -d * 0.15f)));
}
} // namespace styles

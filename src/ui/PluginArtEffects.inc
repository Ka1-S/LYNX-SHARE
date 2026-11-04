// The effects' themes of the "Themed" plugin style (see PluginArt.inc for the framework and the rules; the art
// directions and the style guide are in the tracker's plugin-art-0.1.24.md). One block per theme: its drawing
// hooks, then its Theme record; then the registry and artForEffect. Ids: PluginArtEffectIds.inc (128..255).
// LYNX's own procedural drawings only — never a copied, traced or extracted bitmap, logo or skin.

namespace artkit
{
// === Facet EQ: studio glass — clean and precise, thin lines, no motion =============================================

inline void canvasFacet (juce::Graphics& g, Rectangle<float> a, bool)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.bgTop, 0.0f, a.getY(), p.bgBottom, 0.0f, a.getBottom(), false));
    g.fillRect (a);
    if (! reduced)
    {
        // a cool studio light from above and a warm one low on the right: glass, not fireworks
        bloom (g, { a.getCentreX(), a.getY() - a.getHeight() * 0.25f }, a.getWidth() * 0.6f, p.accent2, 0.10f);
        bloom (g, { a.getRight() - a.getWidth() * 0.12f, a.getBottom() }, a.getWidth() * 0.35f, p.accent, 0.05f);
    }
    // a fine sheen line at the top and a soft vignette
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.05f), 0.0f, a.getY(), Colours::white.withAlpha (0.0f), 0.0f, a.getY() + 40.0f, false));
    g.fillRect (a.withHeight (40.0f));
    ColourGradient vig (Colours::transparentBlack, a.getCentreX(), a.getCentreY(), Colours::black.withAlpha (0.35f), a.getX(), a.getY(), true);
    g.setGradientFill (vig);
    g.fillRect (a);
}

inline const Theme& facetTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Facet EQ";
        x.base = Style::glow;
        x.pal = { Colour (0xff161e2b), Colour (0xff090d14), Colour (0xffffc95a), Colour (0xff66d6ff), Colour (0xd8172030), Colour (0xe80e131c),
                  Colour (0xff7189ab), Colour (0xffe7eef8), Colour (0xff070a10), Colour (0xff2a3446), Colour (0xff141a25), Colour (0xff2a3446),
                  Colour (0xfff4f7fb), Colour (0xff151c28) };
        x.canvas = canvasFacet;
        x.thin = true;
        x.kerning = 0.06f;
        x.cap = Cap::flat;
        x.arcGlow = 0.0f;
        return x;
    }();
    return t;
}

// === Nebula: deep space — a magenta/cyan nebula, planet knobs, twinkling stars ======================================

inline void canvasNebula (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.bgTop, 0.0f, a.getY(), p.bgBottom, 0.0f, a.getBottom(), false));
    g.fillRect (a);
    if (! reduced)
    {
        // gas clouds: stacked soft blooms of magenta, violet and teal along a diagonal band
        juce::Random rnd (2026);
        for (int i = 0; i < 26; ++i)
        {
            const auto t = rnd.nextFloat();
            const Point<float> c { a.getX() + a.getWidth() * (0.05f + 0.9f * t), a.getY() + a.getHeight() * (0.75f - 0.5f * t + (rnd.nextFloat() - 0.5f) * 0.35f) };
            const auto col = t < 0.4f ? p.accent : t < 0.7f ? Colour (0xff8a5cff) : p.accent2;
            bloom (g, c, a.getHeight() * (0.12f + 0.25f * rnd.nextFloat()), col, 0.05f + 0.07f * rnd.nextFloat());
        }
        // a bright core
        bloom (g, { a.getX() + a.getWidth() * 0.55f, a.getY() + a.getHeight() * 0.45f }, a.getHeight() * 0.22f, Colour (0xffffe6fb), 0.10f);
    }
    // a spiral galaxy far away, top right
    if (! display)
    {
        const auto gc = Point<float> (a.getRight() - a.getWidth() * 0.12f, a.getY() + a.getHeight() * 0.2f);
        const auto gr = juce::jmin (a.getWidth(), a.getHeight()) * 0.09f;
        for (int arm = 0; arm < 2; ++arm)
        {
            juce::Path s;
            for (int i = 0; i <= 60; ++i)
            {
                const auto t = (float) i / 60.0f;
                const auto ang = (float) arm * juce::MathConstants<float>::pi + t * 4.2f;
                const Point<float> q { gc.x + std::cos (ang) * gr * t, gc.y + std::sin (ang) * gr * t * 0.45f };
                if (i == 0) s.startNewSubPath (q);
                else s.lineTo (q);
            }
            g.setColour (Colour (0xffe8dcff).withAlpha (0.18f));
            g.strokePath (s, stroke (2.2f));
            g.setColour (Colour (0xffe8dcff).withAlpha (0.35f));
            g.strokePath (s, stroke (0.8f));
        }
        if (! reduced) bloom (g, gc, gr * 0.7f, Colour (0xfffff0ff), 0.35f);
    }
    stars (g, a, display ? 60 : 160, 5, 0.9f);
}

inline void nebulaHeader (juce::Graphics& g, Rectangle<float> a) { stars (g, a, (int) (a.getWidth() / 9.0f), 3, 0.6f); }

inline void nebulaMotion (juce::Graphics& g, const Motion& m)
{
    for (const auto& s : m.motes) // twinkling stars
    {
        const auto tw = 0.5f + 0.5f * std::sin (m.t * (1.2f + s.spin) + s.phase);
        if (! reduced) bloom (g, { s.x, s.y }, s.size * 4.0f, m.p.accent2.interpolatedWith (Colours::white, 0.5f), 0.25f * tw);
        g.setColour (Colours::white.withAlpha (0.25f + 0.7f * tw));
        g.fillEllipse (Rectangle<float> (s.size, s.size).withCentre ({ s.x, s.y }));
    }
}

inline const Theme& nebulaTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Nebula";
        x.base = Style::neon;
        x.pal = { Colour (0xff0c0b22), Colour (0xff020208), Colour (0xffff5cd3), Colour (0xff3fe2ff), Colour (0xb4141133), Colour (0xd8070617),
                  Colour (0xffa184ff), Colour (0xfff1eaff), Colour (0xff03030b), Colour (0xff3a2a6e), Colour (0xff0b0920), Colour (0xff221c44),
                  Colour (0xffffffff), Colour (0xff120f2a) };
        x.canvas = canvasNebula;
        x.headerDeco = nebulaHeader;
        x.twoTone = true;
        x.cap = Cap::planet;
        x.pointer = Pointer::electron;
        x.motes = 22;
        x.reach = 4.0f;
        x.paintMotion = nebulaMotion;
        return x;
    }();
    return t;
}

// === the registry =================================================================================================

const Theme* effectTheme (Art a)
{
    switch (a)
    {
        case Art::facet: return &facetTheme();
        case Art::nebula: return &nebulaTheme();
        default: return nullptr;
    }
}
} // namespace artkit

Art artForEffect (EffectKind kind)
{
    switch (kind)
    {
        case EffectKind::facetEq: return Art::facet;
        case EffectKind::nebula: return Art::nebula;
        default: return Art::none;
    }
}

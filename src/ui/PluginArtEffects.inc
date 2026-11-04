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

// === round 170k: shared helpers of the effects' themes ============================================================

inline void vfill (juce::Graphics& g, Rectangle<float> a, Colour top, Colour bottom)
{
    g.setGradientFill (ColourGradient (top, 0.0f, a.getY(), bottom, 0.0f, a.getBottom(), false));
    g.fillRect (a);
}

// fine horizontal hairlines (brushed metal); deterministic
inline void brushedLines (juce::Graphics& g, Rectangle<float> a, juce::int64 seed, float alpha)
{
    if (reduced) return;
    juce::Random rnd (seed);
    for (float y = a.getY(); y < a.getBottom(); y += 1.5f + rnd.nextFloat() * 1.5f)
    {
        g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (alpha * (0.3f + rnd.nextFloat() * 0.7f)));
        const auto x0 = a.getX() + rnd.nextFloat() * a.getWidth() * 0.3f;
        g.fillRect (Rectangle<float> (x0, y, a.getWidth() * (0.4f + rnd.nextFloat() * 0.6f), 0.6f).getIntersection (a));
    }
}

// a soft round light (a radial gradient, cheaper and wider than bloom)
inline void softLight (juce::Graphics& g, Point<float> c, float r, Colour col, float alpha)
{
    g.setGradientFill (ColourGradient (col.withAlpha (alpha), c.x, c.y, col.withAlpha (0.0f), c.x + r, c.y, true));
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
}

inline void vignette (juce::Graphics& g, Rectangle<float> a, float alpha)
{
    g.setGradientFill (ColourGradient (Colours::transparentBlack, a.getCentreX(), a.getCentreY(), Colours::black.withAlpha (alpha), a.getX(), a.getY(), true));
    g.fillRect (a);
}

inline void cornerScrews (juce::Graphics& g, Rectangle<float> a, float inset, float d)
{
    if (a.getWidth() < 60.0f || a.getHeight() < 40.0f) return;
    for (const auto& c : { a.getTopLeft().translated (inset, inset), a.getTopRight().translated (-inset, inset), a.getBottomLeft().translated (inset, -inset),
                           a.getBottomRight().translated (-inset, -inset) })
        styles::screw (g, c, d);
}

// knob caps of the effects' materials (Theme::drawCap)
inline void capConsole (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool)
{
    const auto& p = pal();
    g.setColour (Colour (0xff101113));
    g.fillEllipse (cap.expanded (capR * 0.06f));
    g.setGradientFill (ColourGradient (p.capTop.brighter (hover * 0.2f), c.x, cap.getY(), p.capBottom, c.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    // the coloured insert on top of the cap
    g.setGradientFill (ColourGradient (p.accent.brighter (0.2f), c.x, c.y - capR * 0.5f, p.accent.darker (0.5f), c.x, c.y + capR * 0.5f, false));
    g.fillEllipse (cap.reduced (capR * 0.45f));
}

inline void capBrushed (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.capTop.brighter (hover * 0.1f), c.x - capR, c.y - capR, p.capBottom, c.x + capR, c.y + capR, false));
    g.fillEllipse (cap);
    if (fx)
        for (float r = capR * 0.25f; r < capR; r += 1.6f)
        {
            g.setColour (Colours::white.withAlpha (0.06f));
            g.drawEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 0.6f);
        }
    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawEllipse (cap, 1.0f);
}

inline void capIron (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.capTop.brighter (hover * 0.15f), c.x, cap.getY(), p.capBottom, c.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    if (! fx) return;
    for (int i = 0; i < 6; ++i) // rivets
    {
        const auto ang = juce::MathConstants<float>::twoPi * (float) i / 6.0f;
        const auto rv = c + Point<float> (std::sin (ang), -std::cos (ang)) * capR * 0.78f;
        g.setGradientFill (ColourGradient (p.capTop.brighter (0.7f), rv.x - 1.0f, rv.y - 1.0f, Colours::black, rv.x + 1.5f, rv.y + 1.5f, true));
        g.fillEllipse (Rectangle<float> (capR * 0.16f, capR * 0.16f).withCentre (rv));
    }
}

inline void capVelvet (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.capTop.brighter (hover * 0.12f), c.x - capR * 0.3f, c.y - capR * 0.6f, p.capBottom, c.x + capR * 0.4f, c.y + capR, true));
    g.fillEllipse (cap);
    if (fx)
    {
        g.setColour (p.accent.withAlpha (0.18f));
        g.drawEllipse (cap.reduced (0.6f), 1.2f);
    }
}

// === Ridge Strip (channel-strip role): a console at night =========================================================

inline void canvasRidge (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    brushedLines (g, a, 17, 0.025f);
    // the channel rails: thin dark grooves with a lit edge, one strip every 64 px
    for (float x = a.getX() + 32.0f; x < a.getRight(); x += 64.0f)
    {
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRect (Rectangle<float> (x, a.getY(), 1.5f, a.getHeight()));
        g.setColour (Colours::white.withAlpha (0.05f));
        g.fillRect (Rectangle<float> (x + 1.5f, a.getY(), 1.0f, a.getHeight()));
    }
    // fader caps out of focus along the bottom, each in its slot
    juce::Random rnd (65);
    for (float x = a.getX() + 4.0f; x < a.getRight() - 24.0f; x += 64.0f)
    {
        const auto slot = Rectangle<float> (x + 26.0f, a.getBottom() - a.getHeight() * 0.22f, 4.0f, a.getHeight() * 0.2f);
        g.setColour (Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (slot, 2.0f);
        const auto cap = Rectangle<float> (x + 14.0f, slot.getY() + rnd.nextFloat() * slot.getHeight() * 0.7f, 28.0f, 12.0f);
        g.setGradientFill (ColourGradient (Colour (0xffd9d9d4).withAlpha (0.16f), 0.0f, cap.getY(), Colour (0xff6d6d6a).withAlpha (0.12f), 0.0f, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (Colours::white.withAlpha (0.12f));
        g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 6.0f, 1.2f));
    }
    // a warm meter-bridge light from above
    softLight (g, { a.getCentreX(), a.getY() - a.getHeight() * 0.1f }, a.getWidth() * 0.7f, Colour (0xffffd8a0), 0.06f);
    vignette (g, a, 0.35f);
}

inline void ridgeEmblem (juce::Graphics& g, Point<float> c, float r) // a fader cap in its slot
{
    const auto& p = pal();
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRect (Rectangle<float> (r * 0.25f, r * 2.4f).withCentre (c));
    const auto cap = Rectangle<float> (r * 1.7f, r * 0.9f).withCentre (c.translated (0.0f, -r * 0.2f));
    g.setGradientFill (ColourGradient (Colour (0xffe8e6df), 0.0f, cap.getY(), Colour (0xff8d8c86), 0.0f, cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 1.0f);
    g.setColour (p.accent);
    g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 2.0f, 1.0f));
}

inline void ridgeScrews (juce::Graphics& g, Rectangle<float> a, float) { cornerScrews (g, a, 5.0f, 4.0f); }

inline const Theme& ridgeTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Ridge Strip";
        x.base = Style::hardware;
        x.pal = { Colour (0xff2c3139), Colour (0xff14171c), Colour (0xffe8463c), Colour (0xffffd9a0), Colour (0xd8262a31), Colour (0xe8181b20),
                  Colour (0xff6b7480), Colour (0xffeceae2), Colour (0xff0b0d10), Colour (0xff4a4e55), Colour (0xff1b1d21), Colour (0xff0e1013),
                  Colour (0xfff4f1e8), Colour (0xff22262c) };
        x.canvas = canvasRidge;
        x.sectionDeco = ridgeScrews;
        x.titleEmblem = ridgeEmblem;
        x.drawCap = capConsole;
        x.pointerFrom = 0.55f;
        x.buttTrack = true;
        x.arcGlow = 0.0f;
        x.thumbPointer = true;
        return x;
    }();
    return t;
}

// === Crest Compressor (Pro-C role): precision amber — instrument glass, knee curves printed in it ==================

inline void canvasCrest (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // the transfer grid, stronger every fourth line
    for (int i = 0; (float) i * 18.0f < juce::jmax (a.getWidth(), a.getHeight()); ++i)
    {
        g.setColour (p.accent.withAlpha (i % 4 == 0 ? 0.045f : 0.018f));
        g.fillRect (Rectangle<float> (a.getX() + (float) i * 18.0f, a.getY(), 1.0f, a.getHeight()));
        g.fillRect (Rectangle<float> (a.getX(), a.getY() + (float) i * 18.0f, a.getWidth(), 1.0f));
    }
    // a fan of knee curves: unity up to the threshold, then bending to rising ratios
    const auto s = juce::jmin (a.getWidth(), a.getHeight()) * 0.95f;
    const auto o = Point<float> (a.getRight() - s * 1.05f, a.getBottom() - s * 0.02f);
    for (int k = 0; k < 6; ++k)
    {
        juce::Path curve;
        const auto th = 0.45f, ratio = 1.0f + (float) k * 1.4f, knee = 0.08f;
        curve.startNewSubPath (o);
        for (int i = 1; i <= 40; ++i)
        {
            const auto x = (float) i / 40.0f;
            float y;
            if (x < th - knee) y = x;
            else if (x > th + knee) y = th + (x - th) / ratio;
            else { const auto d = x - th + knee; y = x + (1.0f / ratio - 1.0f) * d * d / (4.0f * knee); }
            curve.lineTo (o.x + x * s, o.y - y * s);
        }
        g.setColour ((k == 0 ? p.accent2 : p.accent).withAlpha (0.08f));
        g.strokePath (curve, juce::PathStrokeType (1.4f));
    }
    softLight (g, { a.getX() + a.getWidth() * 0.25f, a.getY() }, a.getWidth() * 0.5f, p.accent, 0.10f);
    vignette (g, a, 0.4f);
}

inline void crestEmblem (juce::Graphics& g, Point<float> c, float r) // a knee
{
    juce::Path k;
    k.startNewSubPath (c.x - r, c.y + r);
    k.lineTo (c.x - r * 0.1f, c.y - r * 0.05f);
    k.quadraticTo (c.x + r * 0.2f, c.y - r * 0.35f, c.x + r, c.y - r * 0.55f);
    g.setColour (pal().accent);
    g.strokePath (k, stroke (1.6f));
}

inline const Theme& crestTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Crest Compressor";
        x.base = Style::glow;
        x.pal = { Colour (0xff1c1e24), Colour (0xff0a0b0e), Colour (0xffffb340), Colour (0xff7fe3ff), Colour (0xd01a1c22), Colour (0xe40c0d10),
                  Colour (0xff6e6450), Colour (0xfff2e6cf), Colour (0xff07080a), Colour (0xff2c2e33), Colour (0xff111215), Colour (0xff22242a),
                  Colour (0xfffff3d9), Colour (0xff191b20) };
        x.canvas = canvasCrest;
        x.titleEmblem = crestEmblem;
        x.cap = Cap::flat;
        x.pointer = Pointer::glowLine;
        x.arcGlow = 0.25f;
        return x;
    }();
    return t;
}

// === Apex Limiter (Pro-L role): the ceiling — a line of light the peaks press against ===============================

inline void canvasApex (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto ceil = a.getY() + a.getHeight() * 0.3f;
    // a waveform's silhouette pressing up, flat-topped at the ceiling
    juce::Path wave;
    juce::Random rnd (909);
    wave.startNewSubPath (a.getX(), a.getBottom());
    for (float x = a.getX(); x <= a.getRight(); x += 3.0f)
    {
        const auto u = x - a.getX();
        const auto env = 0.55f + 0.45f * std::sin (u * 0.013f) * std::sin (u * 0.0041f + 1.0f);
        const auto y = a.getBottom() - (a.getBottom() - ceil) * juce::jlimit (0.05f, 1.2f, env * (0.6f + rnd.nextFloat() * 0.6f));
        wave.lineTo (x, juce::jmax (y, ceil + 1.0f));
    }
    wave.lineTo (a.getRight(), a.getBottom());
    wave.closeSubPath();
    g.setGradientFill (ColourGradient (p.accent2.withAlpha (0.13f), 0.0f, ceil, p.accent2.withAlpha (0.01f), 0.0f, a.getBottom(), false));
    g.fillPath (wave);
    // the ceiling: a line of light, a glow under it and a hint of red where the peaks touch
    if (! reduced)
    {
        g.setGradientFill (ColourGradient (p.accent.withAlpha (0.0f), 0.0f, ceil - 14.0f, p.accent.withAlpha (0.18f), 0.0f, ceil, false));
        g.fillRect (Rectangle<float> (a.getX(), ceil - 14.0f, a.getWidth(), 14.0f));
    }
    ColourGradient line (p.accent.withAlpha (0.0f), a.getX(), ceil, p.accent.withAlpha (0.0f), a.getRight(), ceil, false);
    line.addColour (0.2, p.accent.withAlpha (0.55f));
    line.addColour (0.5, Colour (0xffff5a5a).withAlpha (0.5f));
    line.addColour (0.8, p.accent.withAlpha (0.55f));
    g.setGradientFill (line);
    g.fillRect (Rectangle<float> (a.getX(), ceil - 0.75f, a.getWidth(), 1.5f));
    vignette (g, a, 0.4f);
}

inline void apexEmblem (juce::Graphics& g, Point<float> c, float r) // bars under a line
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.fillRect (Rectangle<float> (r * 2.4f, 1.4f).withCentre (c.translated (0.0f, -r * 0.75f)));
    g.setColour (p.accent2);
    for (int i = 0; i < 4; ++i)
    {
        const auto h = r * (0.7f + 0.45f * (float) ((i * 2) % 3) / 2.0f);
        g.fillRect (Rectangle<float> (c.x - r * 1.1f + (float) i * r * 0.6f, c.y + r * 0.8f - h, r * 0.38f, h));
    }
}

inline const Theme& apexTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Apex Limiter";
        x.base = Style::glow;
        x.pal = { Colour (0xff131c27), Colour (0xff05080c), Colour (0xff4fb3ff), Colour (0xffd9ecff), Colour (0xd0131c27), Colour (0xe4070b10),
                  Colour (0xff4f6f92), Colour (0xffe6f1ff), Colour (0xff04070b), Colour (0xffc9d2dc), Colour (0xff59626d), Colour (0xff17212c),
                  Colour (0xff0f1720), Colour (0xff17202b) };
        x.canvas = canvasApex;
        x.titleEmblem = apexEmblem;
        x.drawCap = capBrushed;
        x.arcGlow = 0.22f;
        return x;
    }();
    return t;
}

// === Prism Multiband (Pro-MB role): a glass prism splits a white beam into four bands ==============================

inline void canvasPrism (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto pc = Point<float> (a.getX() + a.getWidth() * 0.28f, a.getY() + a.getHeight() * 0.42f);
    const auto pr = juce::jmin (a.getWidth(), a.getHeight()) * 0.16f;
    // the white beam coming in
    juce::Path beam;
    beam.startNewSubPath (a.getX(), pc.y + pr * 0.35f);
    beam.lineTo (pc.x - pr * 0.4f, pc.y + pr * 0.05f);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.0f), a.getX(), 0.0f, Colours::white.withAlpha (0.35f), pc.x, 0.0f, false));
    g.strokePath (beam, juce::PathStrokeType (3.0f));
    // four bands fanning out to the right, low (warm) to high (cool)
    const Colour bands[] { Colour (0xffff5a4e), Colour (0xffffc04a), Colour (0xff4fe08a), Colour (0xff4fa8ff) };
    for (int i = 0; i < 4; ++i)
    {
        juce::Path fan;
        const auto y0 = pc.y - pr * 0.1f + (float) i * pr * 0.08f;
        const auto spread = a.getHeight() * 0.95f;
        const auto ya = a.getY() + a.getHeight() * 0.06f + spread * (float) i / 4.0f, yb = ya + spread / 4.0f;
        fan.startNewSubPath (pc.x + pr * 0.45f, y0);
        fan.lineTo (a.getRight(), ya);
        fan.lineTo (a.getRight(), yb);
        fan.lineTo (pc.x + pr * 0.45f, y0 + pr * 0.08f);
        fan.closeSubPath();
        g.setGradientFill (ColourGradient (bands[i].withAlpha (0.30f), pc.x, 0.0f, bands[i].withAlpha (0.03f), a.getRight(), 0.0f, false));
        g.fillPath (fan);
    }
    // the prism: a glass triangle with a bright edge
    juce::Path tri;
    tri.addTriangle (pc.x, pc.y - pr, pc.x - pr * 0.9f, pc.y + pr * 0.6f, pc.x + pr * 0.9f, pc.y + pr * 0.6f);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.16f), pc.x, pc.y - pr, Colour (0xff8ab4ff).withAlpha (0.05f), pc.x, pc.y + pr, false));
    g.fillPath (tri);
    g.setColour (Colours::white.withAlpha (0.35f));
    g.strokePath (tri, juce::PathStrokeType (1.2f));
    vignette (g, a, 0.35f);
}

inline void prismEmblem (juce::Graphics& g, Point<float> c, float r)
{
    juce::Path tri;
    tri.addTriangle (c.x, c.y - r, c.x - r, c.y + r * 0.7f, c.x + r, c.y + r * 0.7f);
    ColourGradient sp (Colour (0xffff5a4e), c.x - r, c.y, Colour (0xff4fa8ff), c.x + r, c.y, false);
    sp.addColour (0.33, Colour (0xffffc04a));
    sp.addColour (0.66, Colour (0xff4fe08a));
    g.setGradientFill (sp);
    g.fillPath (tri);
    g.setColour (Colours::white.withAlpha (0.7f));
    g.strokePath (tri, juce::PathStrokeType (0.8f));
}

inline const Theme& prismTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Prism Multiband";
        x.base = Style::glow;
        x.pal = { Colour (0xff17141f), Colour (0xff07060b), Colour (0xffb98cff), Colour (0xff6fe3ff), Colour (0xc4191624), Colour (0xdc0a0910),
                  Colour (0xff8e86b0), Colour (0xfff0ecff), Colour (0xff050409), Colour (0xff3b3552), Colour (0xff100e18), Colour (0xff221f2e),
                  Colour (0xffffffff), Colour (0xff1a1724) };
        x.canvas = canvasPrism;
        x.titleEmblem = prismEmblem;
        x.twoTone = true;
        x.cap = Cap::orb;
        return x;
    }();
    return t;
}

// === Sentry Gate (Pro-G role): a gatehouse at night — iron bars, a lantern, cold stone =============================

inline void canvasSentry (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // stone courses
    for (float y = a.getY(); y < a.getBottom(); y += 26.0f)
    {
        const auto shift = ((int) ((y - a.getY()) / 26.0f)) % 2 == 0 ? 0.0f : 34.0f;
        g.setColour (Colours::black.withAlpha (0.18f));
        g.fillRect (Rectangle<float> (a.getX(), y, a.getWidth(), 1.0f));
        for (float x = a.getX() - shift; x < a.getRight(); x += 68.0f)
            g.fillRect (Rectangle<float> (x, y, 1.0f, 26.0f));
    }
    // the lantern's light, upper left
    softLight (g, { a.getX() + a.getWidth() * 0.1f, a.getY() + a.getHeight() * 0.18f }, a.getWidth() * 0.45f, p.accent, 0.16f);
    // the gate: iron bars with spear tips and two rails
    const auto gate = Rectangle<float> (a.getX() + a.getWidth() * 0.35f, a.getY() + a.getHeight() * 0.08f, a.getWidth() * 0.6f, a.getHeight() * 0.92f);
    g.setColour (Colour (0xff05080a).withAlpha (0.55f));
    for (float x = gate.getX(); x < gate.getRight(); x += 22.0f)
    {
        g.fillRect (Rectangle<float> (x, gate.getY() + 10.0f, 4.0f, gate.getHeight() - 10.0f));
        juce::Path tip;
        tip.addTriangle (x - 3.0f, gate.getY() + 11.0f, x + 7.0f, gate.getY() + 11.0f, x + 2.0f, gate.getY());
        g.fillPath (tip);
    }
    for (const auto f : { 0.18f, 0.82f })
        g.fillRect (Rectangle<float> (gate.getX() - 4.0f, gate.getY() + gate.getHeight() * f, gate.getWidth(), 5.0f));
    g.setColour (p.accent.withAlpha (0.10f)); // the lantern's glint on the bars
    for (float x = gate.getX(); x < gate.getRight(); x += 22.0f)
        g.fillRect (Rectangle<float> (x, gate.getY() + 10.0f, 1.0f, gate.getHeight() - 10.0f));
    vignette (g, a, 0.45f);
}

inline void sentryEmblem (juce::Graphics& g, Point<float> c, float r) // a keyhole
{
    juce::Path k;
    k.addEllipse (Rectangle<float> (r * 1.1f, r * 1.1f).withCentre (c.translated (0.0f, -r * 0.35f)));
    k.addTriangle (c.x, c.y - r * 0.2f, c.x - r * 0.5f, c.y + r * 0.95f, c.x + r * 0.5f, c.y + r * 0.95f);
    g.setColour (pal().accent);
    g.fillPath (k);
}

inline void sentryRivets (juce::Graphics& g, Rectangle<float> a, float)
{
    if (a.getWidth() < 60.0f || a.getHeight() < 40.0f) return;
    const auto& p = pal();
    for (const auto& c : { a.getTopLeft().translated (6.0f, 6.0f), a.getTopRight().translated (-6.0f, 6.0f), a.getBottomLeft().translated (6.0f, -6.0f),
                           a.getBottomRight().translated (-6.0f, -6.0f) })
    {
        g.setGradientFill (ColourGradient (p.capTop.brighter (0.5f), c.x - 1.5f, c.y - 1.5f, Colours::black, c.x + 2.0f, c.y + 2.0f, true));
        g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (c));
    }
}

inline const Theme& sentryTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Sentry Gate";
        x.base = Style::hardware;
        x.pal = { Colour (0xff1a252c), Colour (0xff070b0e), Colour (0xffffc864), Colour (0xff6fc6c0), Colour (0xd0182228), Colour (0xe40a0f12),
                  Colour (0xff5b6a70), Colour (0xffeee4cc), Colour (0xff050709), Colour (0xff3a3f42), Colour (0xff111315), Colour (0xff1c2428),
                  Colour (0xffffe2a8), Colour (0xff192127) };
        x.canvas = canvasSentry;
        x.sectionDeco = sentryRivets;
        x.titleEmblem = sentryEmblem;
        x.drawCap = capIron;
        x.pointer = Pointer::glowLine;
        x.arcGlow = 0.2f;
        return x;
    }();
    return t;
}

// === Glow Leveler (opto-leveler role): a hammertone rack panel lit by its electro-luminescent cell ==================

inline void canvasGlowLeveler (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    if (! reduced) // hammertone: a seeded mottle of soft light and dark dabs
    {
        juce::Random rnd (2);
        for (int i = 0; i < (int) (a.getWidth() * a.getHeight() / 260.0f); ++i)
        {
            const Point<float> c { a.getX() + rnd.nextFloat() * a.getWidth(), a.getY() + rnd.nextFloat() * a.getHeight() };
            const auto r = 2.0f + rnd.nextFloat() * 5.0f;
            g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (0.03f));
            g.fillEllipse (Rectangle<float> (r * 2.0f, r * 1.6f).withCentre (c));
        }
    }
    softLight (g, { a.getCentreX(), a.getY() + a.getHeight() * 0.3f }, a.getWidth() * 0.45f, p.accent2, 0.13f); // the cell's glow
    for (const auto x : { a.getX(), a.getRight() - 16.0f }) // rack ears
    {
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillRect (Rectangle<float> (x, a.getY(), 16.0f, a.getHeight()));
        for (const auto f : { 0.12f, 0.88f })
            styles::screw (g, { x + 8.0f, a.getY() + a.getHeight() * f }, 7.0f);
    }
    vignette (g, a, 0.3f);
}

inline void glowLevelerEmblem (juce::Graphics& g, Point<float> c, float r) // the glowing cell
{
    const auto& p = pal();
    if (! reduced) bloom (g, c, r * 3.0f, p.accent2, 0.45f);
    g.setColour (p.accent2);
    g.fillEllipse (Rectangle<float> (r * 1.3f, r * 1.3f).withCentre (c));
    g.setColour (Colours::white.withAlpha (0.8f));
    g.fillEllipse (Rectangle<float> (r * 0.5f, r * 0.5f).withCentre (c));
}

inline const Theme& glowLevelerTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Glow Leveler";
        x.base = Style::hardware;
        x.pal = { Colour (0xff4a5560), Colour (0xff262c33), Colour (0xfff0e2c0), Colour (0xff8fe6d8), Colour (0xd83e4852), Colour (0xe82a3038),
                  Colour (0xff8a96a2), Colour (0xfff4ead2), Colour (0xff12171c), Colour (0xff2d2d2d), Colour (0xff0c0c0c), Colour (0xff1f252b),
                  Colour (0xfff4ead2), Colour (0xff353d46) };
        x.canvas = canvasGlowLeveler;
        x.sectionDeco = ridgeScrews;
        x.titleEmblem = glowLevelerEmblem;
        x.cap = Cap::bakelite;
        x.pointerFrom = 0.15f;
        x.ticks = 11;
        x.buttTrack = true;
        x.arcGlow = 0.0f;
        x.knobScale = 1.04f;
        x.thumbPointer = true;
        return x;
    }();
    return t;
}

// === Hush De-esser (de-esser role): hush — sibilance hairlines thinning into the dark, an S ribbon in the mist =====

inline void canvasHush (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    juce::Random rnd (5);
    for (float x = a.getX() + a.getWidth() * 0.35f; x < a.getRight(); x += 3.0f)
    {
        const auto u = (x - a.getX()) / a.getWidth();
        const auto h = a.getHeight() * (0.15f + 0.5f * rnd.nextFloat() * (1.0f - u));
        g.setColour (p.accent2.withAlpha (0.05f * (1.2f - u)));
        g.fillRect (Rectangle<float> (x, a.getCentreY() - h * 0.5f, 1.0f, h));
    }
    juce::Path s; // the S ribbon
    const auto c = Point<float> (a.getX() + a.getWidth() * 0.22f, a.getCentreY());
    const auto r = a.getHeight() * 0.28f;
    s.startNewSubPath (c.x + r * 0.8f, c.y - r * 1.1f);
    s.cubicTo (c.x - r * 0.4f, c.y - r * 1.6f, c.x - r * 1.4f, c.y - r * 0.2f, c.x, c.y);
    s.cubicTo (c.x + r * 1.4f, c.y + r * 0.2f, c.x + r * 0.4f, c.y + r * 1.6f, c.x - r * 0.8f, c.y + r * 1.1f);
    if (! reduced)
    {
        g.setColour (p.accent.withAlpha (0.06f));
        g.strokePath (s, stroke (r * 0.5f));
    }
    g.setGradientFill (ColourGradient (p.accent.withAlpha (0.25f), c.x, c.y - r, p.accent2.withAlpha (0.05f), c.x, c.y + r * 1.2f, false));
    g.strokePath (s, stroke (r * 0.12f));
    vignette (g, a, 0.4f);
}

inline void hushEmblem (juce::Graphics& g, Point<float> c, float r)
{
    juce::Path s;
    s.startNewSubPath (c.x + r * 0.7f, c.y - r * 0.8f);
    s.cubicTo (c.x - r * 0.3f, c.y - r * 1.2f, c.x - r * 1.0f, c.y - r * 0.2f, c.x, c.y);
    s.cubicTo (c.x + r * 1.0f, c.y + r * 0.2f, c.x + r * 0.3f, c.y + r * 1.2f, c.x - r * 0.7f, c.y + r * 0.8f);
    g.setColour (pal().accent);
    g.strokePath (s, stroke (1.5f));
}

inline const Theme& hushTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Hush De-esser";
        x.base = Style::glow;
        x.pal = { Colour (0xff261b35), Colour (0xff0b0910), Colour (0xffc792ff), Colour (0xffffb3e0), Colour (0xc8221830), Colour (0xe00d0a13),
                  Colour (0xff86709e), Colour (0xfff1e8ff), Colour (0xff07050b), Colour (0xff3a2e4c), Colour (0xff17121f), Colour (0xff221a2e),
                  Colour (0xfff6eaff), Colour (0xff1e1729) };
        x.canvas = canvasHush;
        x.titleEmblem = hushEmblem;
        x.drawCap = capVelvet;
        x.twoTone = true;
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
        case Art::ridge: return &ridgeTheme();
        case Art::crest: return &crestTheme();
        case Art::apex: return &apexTheme();
        case Art::prism: return &prismTheme();
        case Art::sentry: return &sentryTheme();
        case Art::glowLeveler: return &glowLevelerTheme();
        case Art::hush: return &hushTheme();
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
        case EffectKind::ridgeStrip: return Art::ridge;
        case EffectKind::crestCompressor: return Art::crest;
        case EffectKind::apexLimiter: return Art::apex;
        case EffectKind::prismMultiband: return Art::prism;
        case EffectKind::sentryGate: return Art::sentry;
        case EffectKind::glowLeveler: return Art::glowLeveler;
        case EffectKind::hushDeEsser: return Art::hush;
        default: return Art::none;
    }
}

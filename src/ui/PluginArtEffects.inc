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

// === Epoch Verb (vintage-digital reverb role): a 1980s rack — black brushed unit, a red display glow, memory chips ==

inline void canvasEpoch (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    brushedLines (g, a, 80, 0.03f);
    // a grid of memory chips printed faintly on the board behind the panel (rows of small dark packages with legs)
    juce::Random rnd (1980);
    for (float y = a.getY() + 18.0f; y < a.getBottom() - 20.0f; y += 46.0f)
        for (float x = a.getX() + 14.0f + (rnd.nextFloat() * 20.0f); x < a.getRight() - 40.0f; x += 58.0f + rnd.nextFloat() * 20.0f)
        {
            const auto chip = Rectangle<float> (x, y, 34.0f, 14.0f);
            g.setColour (Colours::black.withAlpha (0.28f));
            g.fillRoundedRectangle (chip, 1.5f);
            g.setColour (Colours::white.withAlpha (0.035f));
            for (float lx = chip.getX() + 3.0f; lx < chip.getRight() - 2.0f; lx += 4.0f)
            {
                g.fillRect (Rectangle<float> (lx, chip.getY() - 3.0f, 1.5f, 3.0f));
                g.fillRect (Rectangle<float> (lx, chip.getBottom(), 1.5f, 3.0f));
            }
            g.fillEllipse (Rectangle<float> (2.5f, 2.5f).withCentre (chip.getTopLeft().translated (4.0f, 4.0f)));
        }
    // the readout's red glow spilling from the top and warm sodium light low
    softLight (g, { a.getX() + a.getWidth() * 0.7f, a.getY() }, a.getWidth() * 0.45f, p.accent, 0.12f);
    softLight (g, { a.getX() + a.getWidth() * 0.15f, a.getBottom() }, a.getWidth() * 0.4f, p.accent2, 0.06f);
    vignette (g, a, 0.4f);
}

inline void epochEmblem (juce::Graphics& g, Point<float> c, float r) // a lit display segment
{
    const auto& p = pal();
    if (! reduced) bloom (g, c, r * 2.6f, p.accent, 0.35f);
    g.setColour (p.accent);
    g.fillRoundedRectangle (Rectangle<float> (r * 1.8f, r * 1.1f).withCentre (c), 1.0f);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRect (Rectangle<float> (r * 1.8f, 0.8f).withCentre (c));
}

inline const Theme& epochTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Epoch Verb";
        x.base = Style::hardware;
        x.pal = { Colour (0xff1c1c1e), Colour (0xff0a0a0b), Colour (0xffff5a36), Colour (0xffffb35a), Colour (0xd81a1a1c), Colour (0xe80c0c0d),
                  Colour (0xff57534f), Colour (0xfff2ede6), Colour (0xff070606), Colour (0xff2a2a2b), Colour (0xff0e0e0f), Colour (0xff1e1e20),
                  Colour (0xfff7f2ea), Colour (0xff1b1b1d) };
        x.canvas = canvasEpoch;
        x.sectionDeco = ridgeScrews;
        x.titleEmblem = epochEmblem;
        x.cap = Cap::bakelite;
        x.pointerFrom = 0.2f;
        x.buttTrack = true;
        x.arcGlow = 0.18f;
        x.thumbPointer = true;
        return x;
    }();
    return t;
}

// === Atrium (room role): a stone atrium in afternoon light — arches, pillars, light shafts, dust ===================

inline void canvasAtrium (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // a colonnade of arches receding to a vanishing point (thin sandstone lines)
    const auto vp = Point<float> (a.getX() + a.getWidth() * 0.62f, a.getY() + a.getHeight() * 0.45f);
    for (int i = 0; i < 6; ++i)
    {
        const auto s = std::pow (0.72f, (float) i); // each arch smaller (further)
        const auto w = a.getWidth() * 0.9f * s, h = a.getHeight() * 1.1f * s;
        const auto box = Rectangle<float> (w, h).withCentre (vp.translated (0.0f, h * 0.1f));
        juce::Path arch;
        arch.startNewSubPath (box.getX(), box.getBottom());
        arch.lineTo (box.getX(), box.getY() + w * 0.5f);
        arch.addCentredArc (box.getCentreX(), box.getY() + w * 0.5f, w * 0.5f, w * 0.5f, 0.0f, -juce::MathConstants<float>::halfPi,
                            juce::MathConstants<float>::halfPi, false);
        arch.lineTo (box.getRight(), box.getBottom());
        g.setColour (p.accent2.withAlpha (0.10f * (1.0f - 0.12f * (float) i)));
        g.strokePath (arch, juce::PathStrokeType (juce::jmax (0.8f, 3.0f * s)));
    }
    // light shafts slanting down from the upper left
    if (! reduced)
        for (int i = 0; i < 3; ++i)
        {
            juce::Path shaft;
            const auto x0 = a.getX() + a.getWidth() * (0.08f + 0.14f * (float) i);
            shaft.startNewSubPath (x0, a.getY());
            shaft.lineTo (x0 + a.getWidth() * 0.06f, a.getY());
            shaft.lineTo (x0 + a.getWidth() * 0.36f, a.getBottom());
            shaft.lineTo (x0 + a.getWidth() * 0.24f, a.getBottom());
            shaft.closeSubPath();
            g.setGradientFill (ColourGradient (p.accent.withAlpha (0.09f), x0, a.getY(), p.accent.withAlpha (0.0f), x0 + a.getWidth() * 0.3f, a.getBottom(), false));
            g.fillPath (shaft);
        }
    // dust motes in the light (static)
    juce::Random rnd (9);
    for (int i = 0; i < 40; ++i)
    {
        g.setColour (p.accent.withAlpha (0.08f + rnd.nextFloat() * 0.12f));
        g.fillEllipse (Rectangle<float> (1.6f, 1.6f).withCentre ({ a.getX() + rnd.nextFloat() * a.getWidth() * 0.6f, a.getY() + rnd.nextFloat() * a.getHeight() }));
    }
    vignette (g, a, 0.45f);
}

inline void atriumEmblem (juce::Graphics& g, Point<float> c, float r) // an arch
{
    juce::Path arch;
    arch.startNewSubPath (c.x - r, c.y + r);
    arch.lineTo (c.x - r, c.y);
    arch.addCentredArc (c.x, c.y, r, r, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, false);
    arch.lineTo (c.x + r, c.y + r);
    g.setColour (pal().accent);
    g.strokePath (arch, stroke (1.6f));
}

inline void capStone (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.capTop.brighter (hover * 0.12f), c.x - capR * 0.3f, c.y - capR * 0.6f, p.capBottom, c.x + capR * 0.4f, c.y + capR, true));
    g.fillEllipse (cap);
    if (! fx) return;
    juce::Random rnd ((juce::int64) (capR * 10.0f));
    g.setColour (Colours::black.withAlpha (0.12f));
    for (int i = 0; i < 18; ++i) // the stone's grain
        g.fillEllipse (Rectangle<float> (1.2f, 1.2f).withCentre (c + Point<float> (rnd.nextFloat() - 0.5f, rnd.nextFloat() - 0.5f) * capR * 1.4f));
}

inline const Theme& atriumTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Atrium";
        x.base = Style::glow;
        x.pal = { Colour (0xff2a221b), Colour (0xff0f0c09), Colour (0xfff2c58a), Colour (0xffd9b48a), Colour (0xcc2a221b), Colour (0xe0130f0c),
                  Colour (0xff8a7356), Colour (0xfff6e8d2), Colour (0xff0a0806), Colour (0xffcbb89b), Colour (0xff7d6b54), Colour (0xff2a2119),
                  Colour (0xff2a1d10), Colour (0xff221b15) };
        x.canvas = canvasAtrium;
        x.titleEmblem = atriumEmblem;
        x.drawCap = capStone;
        x.arcGlow = 0.18f;
        return x;
    }();
    return t;
}

// === Foil Plate (plate role): a suspended steel plate on springs, brushed foil sheen, copper transducers ==========

inline void canvasFoil (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // the plate: a big tilted sheet in the background, hung from the frame by springs
    const auto plate = Rectangle<float> (a.getWidth() * 0.86f, a.getHeight() * 0.62f).withCentre (a.getCentre().translated (0.0f, a.getHeight() * 0.06f));
    g.setGradientFill (ColourGradient (p.accent2.withAlpha (0.10f), plate.getX(), plate.getY(), p.accent2.withAlpha (0.03f), plate.getRight(), plate.getBottom(), false));
    g.fillRect (plate);
    if (! reduced) // the diagonal sheen of the foil
    {
        juce::Path sheen;
        sheen.startNewSubPath (plate.getX() + plate.getWidth() * 0.35f, plate.getY());
        sheen.lineTo (plate.getX() + plate.getWidth() * 0.5f, plate.getY());
        sheen.lineTo (plate.getX() + plate.getWidth() * 0.3f, plate.getBottom());
        sheen.lineTo (plate.getX() + plate.getWidth() * 0.15f, plate.getBottom());
        sheen.closeSubPath();
        g.setColour (Colours::white.withAlpha (0.05f));
        g.fillPath (sheen);
    }
    brushedLines (g, plate, 33, 0.03f);
    g.setColour (p.accent2.withAlpha (0.22f));
    g.drawRect (plate, 1.0f);
    // springs from the frame's corners to the plate's
    auto spring = [&] (Point<float> from, Point<float> to)
    {
        juce::Path s;
        const auto n = 9;
        const auto d = to - from;
        const Point<float> perp { -d.y, d.x };
        const auto len = juce::jmax (1.0f, d.getDistanceFromOrigin());
        s.startNewSubPath (from);
        for (int i = 1; i < n; ++i)
            s.lineTo (from + d * ((float) i / (float) n) + perp * ((i % 2 == 0 ? 1.0f : -1.0f) * 4.0f / len));
        s.lineTo (to);
        g.setColour (p.accent2.withAlpha (0.28f));
        g.strokePath (s, juce::PathStrokeType (1.0f));
    };
    spring (a.getTopLeft().translated (6.0f, 6.0f), plate.getTopLeft());
    spring (a.getTopRight().translated (-6.0f, 6.0f), plate.getTopRight());
    spring (a.getBottomLeft().translated (6.0f, -6.0f), plate.getBottomLeft());
    spring (a.getBottomRight().translated (-6.0f, -6.0f), plate.getBottomRight());
    // copper transducers on the plate
    for (const auto f : { 0.3f, 0.72f })
    {
        const auto c = Point<float> (plate.getX() + plate.getWidth() * f, plate.getY() + plate.getHeight() * (f < 0.5f ? 0.35f : 0.62f));
        g.setGradientFill (ColourGradient (p.accent.withAlpha (0.35f), c.x - 6.0f, c.y - 6.0f, p.accent.darker (0.6f).withAlpha (0.3f), c.x + 6.0f, c.y + 6.0f, true));
        g.fillEllipse (Rectangle<float> (16.0f, 16.0f).withCentre (c));
    }
    vignette (g, a, 0.4f);
}

inline void foilEmblem (juce::Graphics& g, Point<float> c, float r) // a plate on two springs
{
    const auto& p = pal();
    g.setColour (p.accent2);
    g.drawRect (Rectangle<float> (r * 2.2f, r * 1.0f).withCentre (c.translated (0.0f, r * 0.3f)), 1.0f);
    g.setColour (p.accent);
    for (const auto dx : { -0.8f, 0.8f })
        g.drawLine (c.x + dx * r, c.y - r, c.x + dx * r, c.y - r * 0.2f, 1.0f);
}

inline void capChrome (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool)
{
    ColourGradient ch (Colours::white.withAlpha (0.95f), c.x, cap.getY(), Colour (0xff2a2d31), c.x, cap.getBottom(), false);
    ch.addColour (0.45, Colour (0xff9aa1a8).brighter (hover * 0.2f));
    ch.addColour (0.55, Colour (0xff3d4248));
    g.setGradientFill (ch);
    g.fillEllipse (cap);
    g.setColour (Colours::black.withAlpha (0.4f));
    g.drawEllipse (cap, 0.8f);
    juce::ignoreUnused (capR);
}

inline const Theme& foilTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Foil Plate";
        x.base = Style::hardware;
        x.pal = { Colour (0xff22262b), Colour (0xff0d0f12), Colour (0xffd98a52), Colour (0xffc9d1d8), Colour (0xd01f2327), Colour (0xe40d0f12),
                  Colour (0xff7d8791), Colour (0xffeef2f5), Colour (0xff08090b), Colour (0xffd9dee3), Colour (0xff4b5157), Colour (0xff1a1d21),
                  Colour (0xff15181b), Colour (0xff1f2327) };
        x.canvas = canvasFoil;
        x.titleEmblem = foilEmblem;
        x.drawCap = capChrome;
        x.arcGlow = 0.18f;
        return x;
    }();
    return t;
}

// === Halo (shimmer role): a luminous ring of light, sparkles rising an octave at a time ===========================

inline void canvasHalo (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto c = Point<float> (a.getCentreX(), a.getY() + a.getHeight() * 0.32f);
    const auto r = juce::jmin (a.getWidth(), a.getHeight()) * 0.3f;
    if (! reduced)
    {
        softLight (g, c, r * 2.2f, p.accent, 0.10f);
        // the corona: a few soft rings
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (p.accent.withAlpha (0.05f));
            g.drawEllipse (Rectangle<float> ((r + (float) i * 5.0f) * 2.0f, (r + (float) i * 5.0f) * 0.7f).withCentre (c), 6.0f);
        }
    }
    g.setGradientFill (ColourGradient (p.accent.withAlpha (0.55f), c.x - r, c.y, p.accent2.withAlpha (0.4f), c.x + r, c.y, false));
    g.drawEllipse (Rectangle<float> (r * 2.0f, r * 0.7f).withCentre (c), 2.0f);
    // sparkles rising in octaves: tiers of smaller and smaller four-point stars
    juce::Random rnd (12);
    for (int tier = 0; tier < 3; ++tier)
        for (int i = 0; i < 9; ++i)
        {
            const Point<float> s { a.getX() + rnd.nextFloat() * a.getWidth(), a.getBottom() - a.getHeight() * (0.15f + 0.28f * (float) tier + rnd.nextFloat() * 0.2f) };
            const auto sz = 5.0f / (float) (tier + 1);
            juce::Path star;
            star.startNewSubPath (s.x, s.y - sz);
            star.quadraticTo (s.x, s.y, s.x + sz, s.y);
            star.quadraticTo (s.x, s.y, s.x, s.y + sz);
            star.quadraticTo (s.x, s.y, s.x - sz, s.y);
            star.quadraticTo (s.x, s.y, s.x, s.y - sz);
            g.setColour (p.accent.interpolatedWith (Colours::white, 0.5f).withAlpha (0.18f + 0.1f * (float) tier));
            g.fillPath (star);
        }
    vignette (g, a, 0.35f);
}

inline void haloEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    if (! reduced) bloom (g, c, r * 2.4f, p.accent, 0.3f);
    g.setColour (p.accent);
    g.drawEllipse (Rectangle<float> (r * 2.2f, r * 0.9f).withCentre (c), 1.4f);
}

inline void haloTip (juce::Graphics& g, Point<float> tip, float t, float) // a sparkle rides the arc's tip
{
    const auto& p = pal();
    const auto sz = t * 1.6f;
    juce::Path star;
    star.startNewSubPath (tip.x, tip.y - sz);
    star.quadraticTo (tip.x, tip.y, tip.x + sz, tip.y);
    star.quadraticTo (tip.x, tip.y, tip.x, tip.y + sz);
    star.quadraticTo (tip.x, tip.y, tip.x - sz, tip.y);
    star.quadraticTo (tip.x, tip.y, tip.x, tip.y - sz);
    g.setColour (p.accent.interpolatedWith (Colours::white, 0.6f));
    g.fillPath (star);
}

inline void haloMotion (juce::Graphics& g, const Motion& m)
{
    for (const auto& s : m.motes) // sparkles rising and fading
    {
        const auto fade = juce::jlimit (0.0f, 1.0f, (s.y - m.stage.getY()) / juce::jmax (1.0f, m.stage.getHeight()));
        const auto sz = s.size * (0.8f + 0.4f * std::sin (m.t * 3.0f + s.phase));
        juce::Path star;
        star.startNewSubPath (s.x, s.y - sz);
        star.quadraticTo (s.x, s.y, s.x + sz, s.y);
        star.quadraticTo (s.x, s.y, s.x, s.y + sz);
        star.quadraticTo (s.x, s.y, s.x - sz, s.y);
        star.quadraticTo (s.x, s.y, s.x, s.y - sz);
        g.setColour (m.p.accent.interpolatedWith (Colours::white, 0.5f).withAlpha (0.2f + 0.6f * fade));
        g.fillPath (star);
    }
}

inline const Theme& haloTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Halo";
        x.base = Style::neon;
        x.pal = { Colour (0xff141c30), Colour (0xff05070e), Colour (0xffffe08a), Colour (0xff8fe8ff), Colour (0xb8141c30), Colour (0xd806080f),
                  Colour (0xff9fb0d0), Colour (0xfffff6dc), Colour (0xff04060c), Colour (0xfff4f0e6), Colour (0xffb6b0a4), Colour (0xff1d2438),
                  Colour (0xff3a2f14), Colour (0xff161d2e) };
        x.canvas = canvasHalo;
        x.titleEmblem = haloEmblem;
        x.twoTone = true;
        x.cap = Cap::porcelain;
        x.tipDeco = haloTip;
        x.motes = 10;
        x.drift = Drift::rise;
        x.sizeMin = 1.5f;
        x.sizeRange = 2.0f;
        x.vyMin = -10.0f;
        x.vyRange = -8.0f;
        x.sway = 3.0f;
        x.reach = 2.0f;
        x.paintMotion = haloMotion;
        return x;
    }();
    return t;
}

// === Horizon (echo + reverb role): dawn on a flat horizon, the sun repeated as fading echoes =======================

inline void canvasHorizon (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    ColourGradient sky (Colour (0xff0b0915), 0.0f, a.getY(), Colour (0xff2a1b46), 0.0f, a.getY() + a.getHeight() * 0.62f, false);
    sky.addColour (0.85, Colour (0xff6a2c4a));
    sky.addColour (1.0, Colour (0xffb04a3a));
    g.setGradientFill (sky);
    g.fillRect (a.withHeight (a.getHeight() * 0.62f));
    vfill (g, a.withTrimmedTop (a.getHeight() * 0.62f), Colour (0xff1a0f18), p.bgBottom);
    if (display) return;
    const auto hy = a.getY() + a.getHeight() * 0.62f;
    // the sun and its echoes to the right, each fainter and smaller
    for (int i = 4; i >= 0; --i)
    {
        const auto r = a.getHeight() * 0.13f * std::pow (0.82f, (float) i);
        const auto c = Point<float> (a.getX() + a.getWidth() * (0.22f + 0.16f * (float) i), hy);
        g.saveState();
        g.reduceClipRegion (Rectangle<float> (a.getX(), a.getY(), a.getWidth(), hy - a.getY()).toNearestInt());
        if (i == 0 && ! reduced) softLight (g, c, r * 3.0f, p.accent, 0.25f);
        g.setGradientFill (ColourGradient (Colour (0xffffd28a).withAlpha (0.9f * std::pow (0.5f, (float) i)), c.x, c.y - r, p.accent.withAlpha (0.7f * std::pow (0.5f, (float) i)), c.x, c.y, false));
        g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        g.restoreState();
    }
    // the horizon line and the sun's path on the water
    g.setColour (p.accent.withAlpha (0.5f));
    g.fillRect (Rectangle<float> (a.getX(), hy, a.getWidth(), 1.0f));
    juce::Random rnd (4);
    for (int i = 0; i < 14; ++i)
    {
        const auto y = hy + 4.0f + (float) i * 4.0f;
        const auto w = a.getWidth() * (0.12f - 0.006f * (float) i) * (0.6f + rnd.nextFloat() * 0.6f);
        g.setColour (p.accent.withAlpha (0.18f - 0.011f * (float) i));
        g.fillRect (Rectangle<float> (a.getX() + a.getWidth() * 0.22f - w * 0.5f, y, w, 1.2f));
    }
    vignette (g, a, 0.35f);
}

inline void horizonEmblem (juce::Graphics& g, Point<float> c, float r) // a half sun on a line
{
    const auto& p = pal();
    juce::Path sun;
    sun.addCentredArc (c.x, c.y + r * 0.4f, r, r, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    sun.closeSubPath();
    g.setColour (p.accent);
    g.fillPath (sun);
    g.fillRect (Rectangle<float> (r * 2.8f, 1.0f).withCentre (c.translated (0.0f, r * 0.5f)));
}

inline const Theme& horizonTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Horizon";
        x.base = Style::vivid;
        x.pal = { Colour (0xff2a1b46), Colour (0xff0b0915), Colour (0xffff8a3d), Colour (0xffc58aff), Colour (0xc01d1430), Colour (0xdc0c0915),
                  Colour (0xff8a6a9a), Colour (0xfffff0e0), Colour (0xff07050c), Colour (0xff342a44), Colour (0xff130f1c), Colour (0xff261c36),
                  Colour (0xffffe4cc), Colour (0xff1e1630) };
        x.canvas = canvasHorizon;
        x.titleEmblem = horizonEmblem;
        x.twoTone = true;
        x.cap = Cap::orb;
        return x;
    }();
    return t;
}

// === Fruity Reeverb 2: a concert hall — velvet, brass, stage lights =================================================

inline void canvasReeverb2 (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // the curtain's folds along the top and the sides
    for (float x = a.getX(); x < a.getRight(); x += 14.0f)
    {
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.035f), x, 0.0f, Colours::black.withAlpha (0.12f), x + 14.0f, 0.0f, false));
        g.fillRect (Rectangle<float> (x, a.getY(), 14.0f, a.getHeight()));
    }
    // the swag across the top
    juce::Path swag;
    swag.startNewSubPath (a.getX(), a.getY());
    for (int i = 0; i < 4; ++i)
    {
        const auto x0 = a.getX() + a.getWidth() * (float) i / 4.0f, x1 = a.getX() + a.getWidth() * (float) (i + 1) / 4.0f;
        swag.quadraticTo ((x0 + x1) * 0.5f, a.getY() + a.getHeight() * 0.12f, x1, a.getY());
    }
    swag.closeSubPath();
    g.setColour (p.bgTop.brighter (0.15f).withAlpha (0.8f));
    g.fillPath (swag);
    g.setColour (p.accent.withAlpha (0.45f)); // a brass trim
    g.strokePath (swag, juce::PathStrokeType (1.2f));
    // two stage-light cones
    if (! reduced)
        for (const auto f : { 0.3f, 0.7f })
        {
            juce::Path cone;
            const auto x = a.getX() + a.getWidth() * f;
            cone.addTriangle (x, a.getY(), x - a.getWidth() * 0.18f, a.getBottom(), x + a.getWidth() * 0.18f, a.getBottom());
            g.setGradientFill (ColourGradient (p.accent2.withAlpha (0.08f), x, a.getY(), p.accent2.withAlpha (0.0f), x, a.getBottom(), false));
            g.fillPath (cone);
        }
    vignette (g, a, 0.45f);
}

inline void reeverb2Emblem (juce::Graphics& g, Point<float> c, float r) // a curtain swag
{
    juce::Path s;
    s.startNewSubPath (c.x - r * 1.2f, c.y - r * 0.6f);
    s.quadraticTo (c.x - r * 0.6f, c.y + r * 0.6f, c.x, c.y - r * 0.6f);
    s.quadraticTo (c.x + r * 0.6f, c.y + r * 0.6f, c.x + r * 1.2f, c.y - r * 0.6f);
    g.setColour (pal().accent);
    g.strokePath (s, stroke (1.5f));
}

inline void capBrass (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    ColourGradient b (p.accent.brighter (0.4f + hover * 0.2f), c.x - capR * 0.4f, c.y - capR * 0.6f, p.accent.darker (0.7f), c.x + capR * 0.5f, c.y + capR, true);
    g.setGradientFill (b);
    g.fillEllipse (cap);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (cap.reduced (capR * 0.3f));
    if (fx)
    {
        g.setGradientFill (ColourGradient (p.capTop, c.x, c.y - capR * 0.7f, p.capBottom, c.x, c.y + capR * 0.7f, false));
        g.fillEllipse (cap.reduced (capR * 0.34f));
    }
}

inline const Theme& reeverb2Theme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Reeverb 2";
        x.base = Style::hardware;
        x.pal = { Colour (0xff3a0f17), Colour (0xff12060a), Colour (0xffd8a24a), Colour (0xffffe0b0), Colour (0xd02a0c12), Colour (0xe4120609),
                  Colour (0xff8a5a3a), Colour (0xfff6e3c4), Colour (0xff0a0305), Colour (0xff3a1a1e), Colour (0xff12070a), Colour (0xff2a0d13),
                  Colour (0xfffff0d6), Colour (0xff2a0e14) };
        x.canvas = canvasReeverb2;
        x.titleEmblem = reeverb2Emblem;
        x.drawCap = capBrass;
        x.arcGlow = 0.15f;
        x.thumbPointer = true;
        return x;
    }();
    return t;
}

// === LuxeVerb: art-deco luxury — black lacquer, gold sunbursts and fans ============================================

inline void canvasLuxe (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // a sunburst from the bottom centre
    const auto c = Point<float> (a.getCentreX(), a.getBottom() + a.getHeight() * 0.1f);
    for (int i = 0; i < 25; ++i)
    {
        const auto ang = juce::MathConstants<float>::pi * (0.08f + 0.84f * (float) i / 24.0f);
        const Point<float> d { -std::cos (ang), -std::sin (ang) };
        g.setColour (p.accent.withAlpha (i % 2 == 0 ? 0.07f : 0.035f));
        g.drawLine ({ c + d * a.getHeight() * 0.3f, c + d * a.getWidth() }, 1.0f);
    }
    // deco fans in the top corners
    for (const auto right : { false, true })
    {
        const auto o = right ? a.getTopRight() : a.getTopLeft();
        for (int k = 1; k <= 4; ++k)
        {
            juce::Path fan;
            const auto rr = 18.0f * (float) k;
            fan.addCentredArc (o.x, o.y, rr, rr, 0.0f, right ? juce::MathConstants<float>::pi : juce::MathConstants<float>::halfPi,
                               right ? juce::MathConstants<float>::pi * 1.5f : juce::MathConstants<float>::pi, true);
            g.setColour (p.accent.withAlpha (0.16f - 0.025f * (float) k));
            g.strokePath (fan, juce::PathStrokeType (1.2f));
        }
    }
    // a double gold rule along the bottom
    g.setColour (p.accent.withAlpha (0.25f));
    g.fillRect (Rectangle<float> (a.getX() + 12.0f, a.getBottom() - 8.0f, a.getWidth() - 24.0f, 1.0f));
    g.fillRect (Rectangle<float> (a.getX() + 12.0f, a.getBottom() - 5.0f, a.getWidth() - 24.0f, 1.0f));
    vignette (g, a, 0.4f);
}

inline void luxeEmblem (juce::Graphics& g, Point<float> c, float r) // a deco fan
{
    const auto& p = pal();
    g.setColour (p.accent);
    for (int i = 0; i < 5; ++i)
    {
        const auto ang = juce::MathConstants<float>::pi * (0.15f + 0.7f * (float) i / 4.0f);
        g.drawLine ({ c.translated (0.0f, r * 0.7f), c.translated (-std::cos (ang) * r * 1.2f, r * 0.7f - std::sin (ang) * r * 1.2f) }, 1.0f);
    }
}

inline void capLacquer (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (Colour (0xff3a3a40).brighter (hover * 0.2f), c.x - capR * 0.4f, c.y - capR * 0.6f, Colour (0xff060607), c.x + capR * 0.5f, c.y + capR, true));
    g.fillEllipse (cap);
    g.setColour (p.accent.withAlpha (0.85f)); // the gold ring
    g.drawEllipse (cap.reduced (capR * 0.08f), juce::jmax (1.0f, capR * 0.08f));
    if (fx)
    {
        g.setColour (Colours::white.withAlpha (0.18f));
        g.fillEllipse (Rectangle<float> (capR * 0.6f, capR * 0.3f).withCentre ({ c.x - capR * 0.25f, c.y - capR * 0.5f }));
    }
}

inline const Theme& luxeTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "LuxeVerb";
        x.base = Style::glow;
        x.pal = { Colour (0xff17161a), Colour (0xff070708), Colour (0xffe8c77a), Colour (0xfff6ecd2), Colour (0xd0141317), Colour (0xe8070708),
                  Colour (0xff8a7646), Colour (0xfff6ecd2), Colour (0xff050505), Colour (0xff2a2a2e), Colour (0xff0a0a0b), Colour (0xff1e1d22),
                  Colour (0xfff6ecd2), Colour (0xff17161a) };
        x.canvas = canvasLuxe;
        x.titleEmblem = luxeEmblem;
        x.drawCap = capLacquer;
        x.thin = true;
        x.kerning = 0.2f;
        x.arcGlow = 0.15f;
        return x;
    }();
    return t;
}

// === Fruity Reeverb (v1): a little tiled echo chamber in perspective ==============================================

inline void canvasReeverb1 (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // the chamber: a back wall and the floor's tiles converging to it
    const auto back = Rectangle<float> (a.getWidth() * 0.4f, a.getHeight() * 0.4f).withCentre (a.getCentre().translated (a.getWidth() * 0.15f, -a.getHeight() * 0.08f));
    g.setColour (p.accent2.withAlpha (0.10f));
    for (int i = 0; i <= 8; ++i)
    {
        const auto f = (float) i / 8.0f;
        g.drawLine ({ { back.getX() + back.getWidth() * f, back.getBottom() }, { a.getX() + a.getWidth() * f, a.getBottom() } }, 1.0f);
        g.drawLine ({ { back.getX() + back.getWidth() * f, back.getY() }, { a.getX() + a.getWidth() * f, a.getY() } }, 1.0f);
        g.drawLine ({ { back.getX(), back.getY() + back.getHeight() * f }, { a.getX(), a.getY() + a.getHeight() * f } }, 1.0f);
        g.drawLine ({ { back.getRight(), back.getY() + back.getHeight() * f }, { a.getRight(), a.getY() + a.getHeight() * f } }, 1.0f);
    }
    for (int i = 1; i < 5; ++i) // depth lines
    {
        const auto f = std::pow ((float) i / 5.0f, 1.6f);
        const auto r = Rectangle<float> (back.getX() + (a.getX() - back.getX()) * f, back.getY() + (a.getY() - back.getY()) * f,
                                         back.getWidth() + (a.getWidth() - back.getWidth()) * f, back.getHeight() + (a.getHeight() - back.getHeight()) * f);
        g.drawRect (r, 1.0f);
    }
    g.setColour (p.accent2.withAlpha (0.16f));
    g.drawRect (back, 1.0f);
    vignette (g, a, 0.4f);
}

inline void reeverb1Emblem (juce::Graphics& g, Point<float> c, float r) // a tile
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.drawRect (Rectangle<float> (r * 1.8f, r * 1.8f).withCentre (c), 1.2f);
    g.fillRect (Rectangle<float> (r * 0.7f, r * 0.7f).withCentre (c));
}

inline const Theme& reeverb1Theme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Reeverb";
        x.base = Style::glow;
        x.pal = { Colour (0xff2a3532), Colour (0xff0e1312), Colour (0xff7fe0c4), Colour (0xffd8efe8), Colour (0xcc243030), Colour (0xe00d1212),
                  Colour (0xff6a8a82), Colour (0xffe8f6f2), Colour (0xff070a09), Colour (0xffeef3f1), Colour (0xffa9b5b1), Colour (0xff1f2a28),
                  Colour (0xff1c2b27), Colour (0xff1f2a28) };
        x.canvas = canvasReeverb1;
        x.titleEmblem = reeverb1Emblem;
        x.cap = Cap::porcelain;
        x.arcGlow = 0.15f;
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
        case Art::epoch: return &epochTheme();
        case Art::atrium: return &atriumTheme();
        case Art::foil: return &foilTheme();
        case Art::halo: return &haloTheme();
        case Art::horizon: return &horizonTheme();
        case Art::reeverb2: return &reeverb2Theme();
        case Art::luxe: return &luxeTheme();
        case Art::reeverb1: return &reeverb1Theme();
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
        case EffectKind::epochVerb: return Art::epoch;
        case EffectKind::atrium: return Art::atrium;
        case EffectKind::foilPlate: return Art::foil;
        case EffectKind::halo: return Art::halo;
        case EffectKind::horizon: return Art::horizon;
        case EffectKind::reeverb2: return Art::reeverb2;
        case EffectKind::luxeVerb: return Art::luxe;
        case EffectKind::reeverb1: return Art::reeverb1;
        default: return Art::none;
    }
}

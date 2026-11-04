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

// a display-first editor (the Pro editors) shows its illustration behind its display, dimmed under the readouts
inline void displayDim (juce::Graphics& g, Rectangle<float> a, bool display)
{
    if (! display) return;
    g.setColour (Colours::black.withAlpha (0.42f));
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
    displayDim (g, a, display);
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
        x.pal = { Colour (0xff1c1e24), Colour (0xff0a0b0e), Colour (0xffffb340), Colour (0xff7fe3ff), Colour (0x9c1a1c22), Colour (0xb80c0d10),
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
    displayDim (g, a, display);
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
        x.pal = { Colour (0xff131c27), Colour (0xff05080c), Colour (0xff4fb3ff), Colour (0xffd9ecff), Colour (0x9c131c27), Colour (0xb8070b10),
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
    displayDim (g, a, display);
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
        x.pal = { Colour (0xff17141f), Colour (0xff07060b), Colour (0xffb98cff), Colour (0xff6fe3ff), Colour (0x9c191624), Colour (0xb80a0910),
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
    displayDim (g, a, display);
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
        x.pal = { Colour (0xff1a252c), Colour (0xff070b0e), Colour (0xffffc864), Colour (0xff6fc6c0), Colour (0x9c182228), Colour (0xb80a0f12),
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
    displayDim (g, a, display);
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
        x.pal = { Colour (0xff261b35), Colour (0xff0b0910), Colour (0xffc792ff), Colour (0xffffb3e0), Colour (0x9c221830), Colour (0xb80d0a13),
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
        x.pal = { Colour (0xff2a221b), Colour (0xff0f0c09), Colour (0xfff2c58a), Colour (0xffd9b48a), Colour (0x9c2a221b), Colour (0xb8130f0c),
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
        x.pal = { Colour (0xff22262b), Colour (0xff0d0f12), Colour (0xffd98a52), Colour (0xffc9d1d8), Colour (0x9c1f2327), Colour (0xb80d0f12),
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
        x.pal = { Colour (0xff141c30), Colour (0xff05070e), Colour (0xffffe08a), Colour (0xff8fe8ff), Colour (0x9c141c30), Colour (0xb806080f),
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
        x.pal = { Colour (0xff2a1b46), Colour (0xff0b0915), Colour (0xffff8a3d), Colour (0xffc58aff), Colour (0x9c1d1430), Colour (0xb80c0915),
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
        x.pal = { Colour (0xff3a0f17), Colour (0xff12060a), Colour (0xffd8a24a), Colour (0xffffe0b0), Colour (0x9c2a0c12), Colour (0xb8120609),
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
        x.pal = { Colour (0xff17161a), Colour (0xff070708), Colour (0xffe8c77a), Colour (0xfff6ecd2), Colour (0x9c141317), Colour (0xb8070708),
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
        x.pal = { Colour (0xff2a3532), Colour (0xff0e1312), Colour (0xff7fe0c4), Colour (0xffd8efe8), Colour (0x9c243030), Colour (0xb80d1212),
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

// === Echoform (tape-echo role): a reel-to-reel tape path — reels, capstan, a loop of tape, cream and oxblood ========

inline void reel (juce::Graphics& g, Point<float> c, float r, float angle, Colour metal, float alpha)
{
    g.setColour (metal.withAlpha (0.14f * alpha));
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setColour (Colour (0xff3a2a1e).withAlpha (0.55f * alpha)); // the wound tape
    g.fillEllipse (Rectangle<float> (r * 1.45f, r * 1.45f).withCentre (c));
    juce::Path holes; // three cut-outs of the flange
    for (int i = 0; i < 3; ++i)
    {
        const auto a = angle + juce::MathConstants<float>::twoPi * (float) i / 3.0f;
        holes.addCentredArc (c.x, c.y, r * 0.86f, r * 0.86f, 0.0f, a - 0.45f, a + 0.45f, true);
        holes.addCentredArc (c.x, c.y, r * 0.42f, r * 0.42f, 0.0f, a + 0.45f, a - 0.45f, false);
        holes.closeSubPath();
    }
    g.setColour (metal.withAlpha (0.22f * alpha));
    g.fillPath (holes);
    g.setColour (metal.withAlpha (0.45f * alpha));
    g.drawEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.2f);
    g.fillEllipse (Rectangle<float> (r * 0.22f, r * 0.22f).withCentre (c));
}

inline void canvasEchoform (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // tolex: a fine cross-hatch
    if (! reduced)
    {
        g.setColour (Colours::black.withAlpha (0.08f));
        for (float x = a.getX() - a.getHeight(); x < a.getRight(); x += 5.0f)
            g.drawLine (x, a.getY(), x + a.getHeight(), a.getBottom(), 0.6f);
    }
    // two reels high in the background and the tape running between them past the heads
    const auto r = juce::jmin (a.getHeight() * 0.3f, a.getWidth() * 0.16f);
    const auto c1 = Point<float> (a.getX() + a.getWidth() * 0.2f, a.getY() + a.getHeight() * 0.3f);
    const auto c2 = Point<float> (a.getRight() - a.getWidth() * 0.2f, c1.y);
    reel (g, c1, r, 0.3f, p.accent2, 1.0f);
    reel (g, c2, r, 1.1f, p.accent2, 1.0f);
    juce::Path tape;
    tape.startNewSubPath (c1.x, c1.y + r * 0.72f);
    tape.lineTo (a.getCentreX() - r * 0.6f, a.getY() + a.getHeight() * 0.72f);
    tape.lineTo (a.getCentreX() + r * 0.6f, a.getY() + a.getHeight() * 0.72f);
    tape.lineTo (c2.x, c2.y + r * 0.72f);
    g.setColour (Colour (0xff4a3020).withAlpha (0.6f));
    g.strokePath (tape, juce::PathStrokeType (2.2f));
    for (int i = 0; i < 3; ++i) // the heads (erase, record, play) under the tape
    {
        const auto h = Rectangle<float> (12.0f, 9.0f).withCentre ({ a.getCentreX() + ((float) i - 1.0f) * 22.0f, a.getY() + a.getHeight() * 0.72f + 6.0f });
        g.setColour (p.accent2.withAlpha (0.18f));
        g.fillRoundedRectangle (h, 2.0f);
    }
    softLight (g, { a.getCentreX(), a.getY() }, a.getWidth() * 0.5f, p.accent2, 0.06f);
    vignette (g, a, 0.4f);
}

inline void echoformEmblem (juce::Graphics& g, Point<float> c, float r) { reel (g, c, r * 1.1f, 0.0f, pal().accent2, 4.0f); }

inline void capChickenHead (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setColour (Colour (0xff15100c));
    g.fillEllipse (cap.expanded (capR * 0.04f));
    g.setGradientFill (ColourGradient (p.capTop.brighter (0.1f + hover * 0.15f), c.x, cap.getY(), p.capBottom, c.x, cap.getBottom(), false));
    g.fillEllipse (cap.reduced (capR * 0.1f));
    if (fx)
    {
        g.setColour (Colours::white.withAlpha (0.25f));
        g.fillEllipse (Rectangle<float> (capR * 0.7f, capR * 0.3f).withCentre ({ c.x - capR * 0.15f, c.y - capR * 0.5f }));
    }
}

inline void echoformMotion (juce::Graphics& g, const Motion& m) // two small reels turning in the stage strip
{
    const auto s = m.stage;
    const auto r = juce::jmin (s.getHeight() * 0.42f, 14.0f);
    if (r < 4.0f) return;
    const auto y = s.getCentreY();
    for (int i = 0; i < 2; ++i)
        reel (g, { s.getRight() - r - 4.0f - (float) i * (r * 2.0f + 8.0f), y }, r, m.t * (i == 0 ? 1.4f : 1.9f), m.p.accent2, 3.5f);
}

inline const Theme& echoformTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Echoform";
        x.base = Style::hardware;
        x.pal = { Colour (0xff2a1f18), Colour (0xff100b08), Colour (0xffc4473a), Colour (0xffe9dfc8), Colour (0x9c281c15), Colour (0xb8100b08),
                  Colour (0xff8a6a4e), Colour (0xfff2e8d2), Colour (0xff0a0705), Colour (0xffeee4cc), Colour (0xffb5a688), Colour (0xff22180f),
                  Colour (0xff3a1612), Colour (0xff2a1f18) };
        x.canvas = canvasEchoform;
        x.sectionDeco = ridgeScrews;
        x.titleEmblem = echoformEmblem;
        x.drawCap = capChickenHead;
        x.pointerFrom = 0.1f;
        x.buttTrack = true;
        x.arcGlow = 0.0f;
        x.thumbPointer = true;
        x.stageRepaint = true;
        x.paintMotion = echoformMotion;
        return x;
    }();
    return t;
}

// === Spiral Echo (frequency-shifting echo role): a spiral of repeats, each turn shifted in hue ======================

inline void spiral (juce::Graphics& g, Point<float> c, float r, float turns, float phase, float alpha, float width)
{
    juce::Path s;
    const auto n = (int) (turns * 64.0f);
    for (int i = 0; i <= n; ++i)
    {
        const auto u = (float) i / (float) n;
        const auto ang = phase + u * turns * juce::MathConstants<float>::twoPi;
        const auto rr = r * (0.06f + 0.94f * u);
        const Point<float> q { c.x + std::cos (ang) * rr, c.y + std::sin (ang) * rr };
        if (i == 0) s.startNewSubPath (q);
        else s.lineTo (q);
    }
    ColourGradient hue (Colour (0xff3fe0c5).withAlpha (alpha), c.x - r, c.y, Colour (0xffff7a6a).withAlpha (alpha), c.x + r, c.y, false);
    hue.addColour (0.5, Colour (0xffffd66a).withAlpha (alpha));
    g.setGradientFill (hue);
    g.strokePath (s, stroke (width));
}

inline void canvasSpiral (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto c = Point<float> (a.getX() + a.getWidth() * 0.72f, a.getY() + a.getHeight() * 0.45f);
    const auto r = juce::jmin (a.getWidth(), a.getHeight()) * 0.55f;
    if (! reduced) softLight (g, c, r, p.accent, 0.07f);
    spiral (g, c, r, 3.2f, 0.0f, 0.16f, 2.0f);
    // the repeats: dots along the spiral, shrinking inwards
    for (int i = 0; i < 12; ++i)
    {
        const auto u = 1.0f - (float) i / 12.0f;
        const auto ang = u * 3.2f * juce::MathConstants<float>::twoPi;
        const auto rr = r * (0.06f + 0.94f * u);
        g.setColour (p.accent2.withAlpha (0.25f * u));
        g.fillEllipse (Rectangle<float> (6.0f * u + 2.0f, 6.0f * u + 2.0f).withCentre ({ c.x + std::cos (ang) * rr, c.y + std::sin (ang) * rr }));
    }
    vignette (g, a, 0.4f);
}

inline void spiralEmblem (juce::Graphics& g, Point<float> c, float r) { spiral (g, c, r * 1.1f, 2.2f, 0.0f, 1.0f, 1.3f); }

inline const Theme& spiralTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Spiral Echo";
        x.base = Style::neon;
        x.pal = { Colour (0xff0d2026), Colour (0xff04090b), Colour (0xff3fe0c5), Colour (0xffff7a6a), Colour (0x9c0d1f25), Colour (0xb805090b),
                  Colour (0xff4f8a8a), Colour (0xffe6fbf6), Colour (0xff030708), Colour (0xffe9f2ef), Colour (0xff8aa39c), Colour (0xff14282d),
                  Colour (0xff10302c), Colour (0xff0f2227) };
        x.canvas = canvasSpiral;
        x.titleEmblem = spiralEmblem;
        x.twoTone = true;
        x.cap = Cap::porcelain;
        return x;
    }();
    return t;
}

// === Orbit (modulation-machine role): a planet with satellites on inclined orbits, interference rings ==============

inline void canvasOrbit (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    stars (g, a, 60, 101, 0.5f);
    const auto c = Point<float> (a.getX() + a.getWidth() * 0.78f, a.getY() + a.getHeight() * 0.32f);
    const auto r = juce::jmin (a.getWidth(), a.getHeight()) * 0.12f;
    // interference rings (a comb filter seen as ripples)
    for (int i = 1; i < 9; ++i)
    {
        g.setColour (p.accent.withAlpha (0.05f - 0.004f * (float) i));
        g.drawEllipse (Rectangle<float> (r * 2.0f + (float) i * 22.0f, r * 2.0f + (float) i * 22.0f).withCentre (c), 1.0f);
    }
    // the planet
    g.setGradientFill (ColourGradient (Colour (0xff6a8ab8), c.x - r * 0.4f, c.y - r * 0.5f, Colour (0xff0e1626), c.x + r * 0.8f, c.y + r, true));
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    // inclined orbits with a satellite each
    for (int i = 0; i < 3; ++i)
    {
        juce::Path o;
        o.addEllipse (Rectangle<float> (r * (3.0f + (float) i * 1.4f), r * (0.9f + (float) i * 0.35f)).withCentre (c));
        o.applyTransform (juce::AffineTransform::rotation (-0.5f + (float) i * 0.45f, c.x, c.y));
        g.setColour (p.accent2.withAlpha (0.22f));
        g.strokePath (o, juce::PathStrokeType (0.9f));
        const auto pt = o.getPointAlongPath (o.getLength() * (0.15f + 0.3f * (float) i));
        g.setColour (p.accent);
        g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre (pt));
    }
    vignette (g, a, 0.35f);
}

inline void orbitEmblem (juce::Graphics& g, Point<float> c, float r) // a satellite
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.fillRect (Rectangle<float> (r * 0.7f, r * 0.7f).withCentre (c));
    g.setColour (p.accent2);
    g.fillRect (Rectangle<float> (r * 0.8f, r * 0.45f).withCentre (c.translated (-r * 0.85f, 0.0f)));
    g.fillRect (Rectangle<float> (r * 0.8f, r * 0.45f).withCentre (c.translated (r * 0.85f, 0.0f)));
}

inline void capDish (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.capTop.brighter (hover * 0.15f), c.x - capR * 0.5f, c.y - capR * 0.5f, p.capBottom, c.x + capR, c.y + capR, true));
    g.fillEllipse (cap);
    if (! fx) return;
    for (int i = 1; i < 4; ++i) // concentric rings of a dish
    {
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawEllipse (Rectangle<float> (capR * 2.0f * (float) i / 4.0f, capR * 2.0f * (float) i / 4.0f).withCentre (c), 0.7f);
    }
}

inline void orbitMotion (juce::Graphics& g, const Motion& m) // a satellite crossing the stage strip on its orbit
{
    const auto s = m.stage;
    if (s.getWidth() < 30.0f) return;
    const auto c = s.getCentre();
    juce::Path o;
    o.addEllipse (s.reduced (6.0f, juce::jmax (2.0f, s.getHeight() * 0.25f)));
    g.setColour (m.p.accent2.withAlpha (0.2f));
    g.strokePath (o, juce::PathStrokeType (0.8f));
    const auto pt = o.getPointAlongPath (o.getLength() * std::fmod (m.t * 0.08f, 1.0f));
    if (! reduced) bloom (g, pt, 7.0f, m.p.accent, 0.5f);
    g.setColour (m.p.accent);
    g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre (pt));
    juce::ignoreUnused (c);
}

inline const Theme& orbitTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Orbit";
        x.base = Style::neon;
        x.pal = { Colour (0xff16233a), Colour (0xff060a12), Colour (0xffb6ff5c), Colour (0xff7fb4ff), Colour (0x9c142035), Colour (0xb8060a12),
                  Colour (0xff4f6a8a), Colour (0xffe8f2ff), Colour (0xff04070c), Colour (0xff4a5a74), Colour (0xff111827), Colour (0xff1c2a40),
                  Colour (0xffeaffd6), Colour (0xff17233a) };
        x.canvas = canvasOrbit;
        x.titleEmblem = orbitEmblem;
        x.drawCap = capDish;
        x.pointer = Pointer::electron;
        x.stageRepaint = true;
        x.paintMotion = orbitMotion;
        return x;
    }();
    return t;
}

// === Lattice Mod (multitap-modulation role): an isometric lattice of glowing nodes (the taps) ======================

inline void canvasLattice (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto step = 34.0f;
    const auto h = step * 0.866f;
    juce::Random rnd (16);
    for (int row = 0; (float) row * h < a.getHeight() + h; ++row)
        for (int col = 0; (float) col * step < a.getWidth() + step; ++col)
        {
            const Point<float> q { a.getX() + (float) col * step + (row % 2 == 0 ? 0.0f : step * 0.5f), a.getY() + (float) row * h };
            const Point<float> right { q.x + step, q.y }, down1 { q.x + step * 0.5f, q.y + h }, down2 { q.x - step * 0.5f, q.y + h };
            g.setColour (p.accent.withAlpha (0.05f));
            g.drawLine ({ q, right }, 1.0f);
            g.drawLine ({ q, down1 }, 1.0f);
            g.drawLine ({ q, down2 }, 1.0f);
            const auto lit = rnd.nextFloat();
            if (lit > 0.82f)
            {
                if (! reduced) bloom (g, q, 9.0f, lit > 0.93f ? p.accent2 : p.accent, 0.25f);
                g.setColour ((lit > 0.93f ? p.accent2 : p.accent).withAlpha (0.55f));
                g.fillEllipse (Rectangle<float> (3.5f, 3.5f).withCentre (q));
            }
            else
            {
                g.setColour (p.accent.withAlpha (0.12f));
                g.fillEllipse (Rectangle<float> (2.0f, 2.0f).withCentre (q));
            }
        }
    vignette (g, a, 0.45f);
}

inline void latticeEmblem (juce::Graphics& g, Point<float> c, float r) // a hexagon
{
    juce::Path hex;
    for (int i = 0; i < 6; ++i)
    {
        const auto ang = juce::MathConstants<float>::twoPi * (float) i / 6.0f;
        const Point<float> q { c.x + std::cos (ang) * r, c.y + std::sin (ang) * r };
        if (i == 0) hex.startNewSubPath (q);
        else hex.lineTo (q);
    }
    hex.closeSubPath();
    g.setColour (pal().accent);
    g.strokePath (hex, juce::PathStrokeType (1.4f));
}

inline void capHex (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    juce::Path hex;
    for (int i = 0; i < 6; ++i)
    {
        const auto ang = juce::MathConstants<float>::twoPi * (float) i / 6.0f + juce::MathConstants<float>::pi / 6.0f;
        const Point<float> q { c.x + std::cos (ang) * capR, c.y + std::sin (ang) * capR };
        if (i == 0) hex.startNewSubPath (q);
        else hex.lineTo (q);
    }
    hex.closeSubPath();
    g.setGradientFill (ColourGradient (p.capTop.brighter (hover * 0.15f), c.x, cap.getY(), p.capBottom, c.x, cap.getBottom(), false));
    g.fillPath (hex);
    if (fx)
    {
        g.setColour (p.accent.withAlpha (0.3f));
        g.strokePath (hex, juce::PathStrokeType (0.8f));
    }
}

inline const Theme& latticeTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Lattice Mod";
        x.base = Style::neon;
        x.pal = { Colour (0xff1a0f1c), Colour (0xff07040a), Colour (0xffff4fa3), Colour (0xffffb84f), Colour (0x9c1a0f1c), Colour (0xb808040a),
                  Colour (0xff8a4f78), Colour (0xfffde8f4), Colour (0xff050307), Colour (0xff3a2238), Colour (0xff120a14), Colour (0xff261626),
                  Colour (0xffffffff), Colour (0xff1e1220) };
        x.canvas = canvasLattice;
        x.titleEmblem = latticeEmblem;
        x.drawCap = capHex;
        x.twoTone = true;
        x.pointer = Pointer::glowLine;
        return x;
    }();
    return t;
}

// === Gemini Doubler: the twins — the Gemini constellation over a sky split in two hues ============================

inline void canvasGemini (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // the two halves: cyan light from the left, gold from the right, meeting in the middle
    softLight (g, { a.getX(), a.getCentreY() }, a.getWidth() * 0.6f, p.accent, 0.10f);
    softLight (g, { a.getRight(), a.getCentreY() }, a.getWidth() * 0.6f, p.accent2, 0.10f);
    stars (g, a, 70, 2, 0.45f);
    // the constellation: two stick figures of stars side by side, linked at the head (public sky, drawn freely)
    const auto u = juce::jmin (a.getWidth(), a.getHeight()) * 0.08f;
    const auto o = Point<float> (a.getCentreX() - u * 1.5f, a.getY() + a.getHeight() * 0.18f);
    const Point<float> left[] { { 0.0f, 0.0f }, { 0.2f, 1.6f }, { 0.1f, 3.2f }, { -0.6f, 4.6f }, { 0.6f, 4.8f }, { -0.9f, 1.4f } };
    const Point<float> right[] { { 3.0f, 0.3f }, { 2.9f, 1.8f }, { 3.1f, 3.4f }, { 2.5f, 4.9f }, { 3.7f, 4.7f }, { 3.9f, 1.6f } };
    auto figure = [&] (const Point<float>* pts, Colour col)
    {
        juce::Path s;
        s.startNewSubPath (o + pts[0] * u);
        s.lineTo (o + pts[1] * u);
        s.lineTo (o + pts[2] * u);
        s.lineTo (o + pts[3] * u);
        s.startNewSubPath (o + pts[2] * u);
        s.lineTo (o + pts[4] * u);
        s.startNewSubPath (o + pts[1] * u);
        s.lineTo (o + pts[5] * u);
        g.setColour (col.withAlpha (0.22f));
        g.strokePath (s, juce::PathStrokeType (0.9f));
        for (int i = 0; i < 6; ++i)
        {
            const auto q = o + pts[i] * u;
            if (! reduced) bloom (g, q, i == 0 ? 8.0f : 5.0f, col, 0.35f);
            g.setColour (Colours::white.withAlpha (0.7f));
            g.fillEllipse (Rectangle<float> (i == 0 ? 3.0f : 2.0f, i == 0 ? 3.0f : 2.0f).withCentre (q));
        }
    };
    figure (left, p.accent);
    figure (right, p.accent2);
    g.setColour (Colours::white.withAlpha (0.15f));
    g.drawLine ({ o + left[0] * u, o + right[0] * u }, 0.8f);
    vignette (g, a, 0.35f);
}

inline void geminiEmblem (juce::Graphics& g, Point<float> c, float r) // II
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.fillRect (Rectangle<float> (1.6f, r * 1.6f).withCentre (c.translated (-r * 0.4f, 0.0f)));
    g.setColour (p.accent2);
    g.fillRect (Rectangle<float> (1.6f, r * 1.6f).withCentre (c.translated (r * 0.4f, 0.0f)));
    g.setColour (Colours::white.withAlpha (0.8f));
    g.fillRect (Rectangle<float> (r * 1.6f, 1.2f).withCentre (c.translated (0.0f, -r * 0.8f)));
    g.fillRect (Rectangle<float> (r * 1.6f, 1.2f).withCentre (c.translated (0.0f, r * 0.8f)));
}

inline void capTwin (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.capTop.brighter (0.2f + hover * 0.2f), c.x - capR * 0.45f, c.y - capR * 0.5f, p.capBottom, c.x + capR * 0.7f, c.y + capR * 0.8f, true));
    g.fillEllipse (cap);
    if (! fx) return;
    g.setColour (p.accent.withAlpha (0.45f)); // two rings, one per twin
    g.drawEllipse (cap.reduced (capR * 0.12f).translated (-capR * 0.05f, 0.0f), 0.8f);
    g.setColour (p.accent2.withAlpha (0.45f));
    g.drawEllipse (cap.reduced (capR * 0.24f).translated (capR * 0.05f, 0.0f), 0.8f);
}

inline const Theme& geminiTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Gemini Doubler";
        x.base = Style::vivid;
        x.pal = { Colour (0xff151a2c), Colour (0xff06070e), Colour (0xff4fd8ff), Colour (0xffffc95a), Colour (0x9c141a2c), Colour (0xb806070e),
                  Colour (0xff6a7a9a), Colour (0xfff0f4ff), Colour (0xff04050a), Colour (0xff3a4462), Colour (0xff10131f), Colour (0xff1e2438),
                  Colour (0xffffffff), Colour (0xff171c2e) };
        x.canvas = canvasGemini;
        x.titleEmblem = geminiEmblem;
        x.drawCap = capTwin;
        x.twoTone = true;
        x.pointer = Pointer::electron;
        return x;
    }();
    return t;
}

// === Fruity Delay 2: a bouncing ball, each bounce lower and shorter — the repeats ================================

inline void canvasDelay2 (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto floor = a.getBottom() - a.getHeight() * 0.1f;
    g.setColour (p.accent.withAlpha (0.12f));
    g.fillRect (Rectangle<float> (a.getX(), floor, a.getWidth(), 1.0f));
    auto x = a.getX() + a.getWidth() * 0.04f, h = a.getHeight() * 0.75f, w = a.getWidth() * 0.22f;
    for (int i = 0; i < 8 && x < a.getRight(); ++i)
    {
        juce::Path hop; // one parabola from floor to floor, its peak at h
        hop.startNewSubPath (x, floor);
        hop.quadraticTo (x + w * 0.5f, floor - h * 2.0f, x + w, floor);
        g.setColour (p.accent2.withAlpha (0.14f * std::pow (0.85f, (float) i)));
        g.strokePath (hop, juce::PathStrokeType (1.2f));
        const auto r = 8.0f * std::pow (0.82f, (float) i);
        if (! reduced) bloom (g, { x + w * 0.5f, floor - h }, r * 2.5f, p.accent, 0.2f * std::pow (0.8f, (float) i));
        g.setColour (p.accent.withAlpha (0.4f * std::pow (0.8f, (float) i)));
        g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre ({ x + w * 0.5f, floor - h }));
        x += w;
        w *= 0.8f;
        h *= 0.62f;
    }
    vignette (g, a, 0.35f);
}

inline void delay2Emblem (juce::Graphics& g, Point<float> c, float r) // a ball and its two smaller echoes
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.fillEllipse (Rectangle<float> (r * 1.1f, r * 1.1f).withCentre (c.translated (-r * 0.6f, 0.0f)));
    g.setColour (p.accent.withAlpha (0.6f));
    g.fillEllipse (Rectangle<float> (r * 0.75f, r * 0.75f).withCentre (c.translated (r * 0.4f, r * 0.15f)));
    g.setColour (p.accent.withAlpha (0.35f));
    g.fillEllipse (Rectangle<float> (r * 0.5f, r * 0.5f).withCentre (c.translated (r * 1.1f, r * 0.27f)));
}

inline const Theme& delay2Theme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Delay 2";
        x.base = Style::glow;
        x.pal = { Colour (0xff162126), Colour (0xff070b0d), Colour (0xff58e0b8), Colour (0xffb8f4ff), Colour (0x9c152026), Colour (0xb8070b0d),
                  Colour (0xff4f7a78), Colour (0xffe6faf4), Colour (0xff04080a), Colour (0xff2c3a3e), Colour (0xff0e1416), Colour (0xff1c2a2e),
                  Colour (0xffeafff8), Colour (0xff17232a) };
        x.canvas = canvasDelay2;
        x.titleEmblem = delay2Emblem;
        x.cap = Cap::rubber;
        return x;
    }();
    return t;
}

// === Fruity Delay 3: an analog delay stomp box — powder-coated enclosure, a jewel lamp, a bucket brigade =========

inline void canvasDelay3 (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    if (! reduced) // powder coat: a fine speckle
    {
        juce::Random rnd (3);
        for (int i = 0; i < (int) (a.getWidth() * a.getHeight() / 90.0f); ++i)
        {
            g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (0.05f));
            g.fillRect (Rectangle<float> (a.getX() + rnd.nextFloat() * a.getWidth(), a.getY() + rnd.nextFloat() * a.getHeight(), 1.0f, 1.0f));
        }
    }
    // the bucket brigade: a chain of buckets passing the signal along, printed along the bottom
    const auto y = a.getBottom() - a.getHeight() * 0.1f;
    for (float x = a.getX() + 18.0f; x < a.getRight() - 18.0f; x += 26.0f)
    {
        juce::Path b;
        b.startNewSubPath (x - 7.0f, y - 8.0f);
        b.lineTo (x - 5.0f, y + 2.0f);
        b.lineTo (x + 5.0f, y + 2.0f);
        b.lineTo (x + 7.0f, y - 8.0f);
        g.setColour (p.accent2.withAlpha (0.16f));
        g.strokePath (b, juce::PathStrokeType (1.2f));
        g.drawLine (x + 8.0f, y - 4.0f, x + 17.0f, y - 4.0f, 0.8f);
    }
    // the jewel lamp's glow, top right
    softLight (g, { a.getRight() - a.getWidth() * 0.12f, a.getY() + a.getHeight() * 0.12f }, a.getWidth() * 0.3f, p.accent, 0.12f);
    vignette (g, a, 0.35f);
}

inline void delay3Emblem (juce::Graphics& g, Point<float> c, float r) // a jewel lamp
{
    const auto& p = pal();
    if (! reduced) bloom (g, c, r * 2.6f, p.accent, 0.4f);
    g.setGradientFill (ColourGradient (p.accent.brighter (0.6f), c.x - r * 0.3f, c.y - r * 0.3f, p.accent.darker (0.4f), c.x + r * 0.6f, c.y + r * 0.6f, true));
    g.fillEllipse (Rectangle<float> (r * 1.5f, r * 1.5f).withCentre (c));
    g.setColour (Colour (0xffc9ccd1));
    g.drawEllipse (Rectangle<float> (r * 1.8f, r * 1.8f).withCentre (c), 1.0f);
}

inline void capPedal (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setColour (Colour (0xff050505));
    g.fillEllipse (cap.expanded (capR * 0.05f));
    g.setGradientFill (ColourGradient (Colour (0xff2c2c2c).brighter (hover * 0.2f), c.x, cap.getY(), Colour (0xff0b0b0b), c.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    if (fx)
    {
        g.setColour (Colours::black.withAlpha (0.7f)); // knurling
        for (int i = 0; i < 30; ++i)
        {
            const auto ang = juce::MathConstants<float>::twoPi * (float) i / 30.0f;
            const Point<float> d { std::sin (ang), -std::cos (ang) };
            g.drawLine ({ c + d * capR * 0.84f, c + d * capR * 0.99f }, 0.8f);
        }
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.14f), c.x, c.y - capR * 0.6f, Colours::transparentWhite, c.x, c.y, false));
        g.fillEllipse (cap.reduced (capR * 0.2f));
    }
    juce::ignoreUnused (p);
}

inline void delay3Motion (juce::Graphics& g, const Motion& m) // the jewel lamp pulses slowly
{
    const auto s = m.stage;
    if (s.getWidth() < 16.0f) return;
    const auto c = Point<float> (s.getRight() - 12.0f, s.getCentreY());
    const auto pulse = 0.5f + 0.5f * std::sin (m.t * 2.2f);
    if (! reduced) bloom (g, c, 12.0f, m.p.accent, 0.2f + 0.4f * pulse);
    g.setColour (m.p.accent.interpolatedWith (Colours::white, 0.3f * pulse));
    g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre (c));
    g.setColour (Colour (0xffc9ccd1));
    g.drawEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (c), 1.0f);
}

inline const Theme& delay3Theme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Delay 3";
        x.base = Style::vivid;
        x.pal = { Colour (0xff1f4e63), Colour (0xff0c2430), Colour (0xffff5a4a), Colour (0xffeaf2f5), Colour (0xc81a4254), Colour (0xe00b2029),
                  Colour (0xff6a9aac), Colour (0xfff2f8fa), Colour (0xff061117), Colour (0xff2c2c2c), Colour (0xff0b0b0b), Colour (0xff0e2a36),
                  Colour (0xfff4f4f4), Colour (0xff163a4a) };
        x.canvas = canvasDelay3;
        x.sectionDeco = ridgeScrews;
        x.titleEmblem = delay3Emblem;
        x.drawCap = capPedal;
        x.pointerFrom = 0.3f;
        x.arcGlow = 0.2f;
        x.stageRepaint = true;
        x.paintMotion = delay3Motion;
        return x;
    }();
    return t;
}

// === Fruity Delay Bank: a sorting rack of pigeonholes in warm wood, each a delay line with a brass label ==========

inline void canvasDelayBank (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    if (! reduced) // wood grain: long wavy lines
    {
        juce::Random rnd (8);
        for (float y = a.getY(); y < a.getBottom(); y += 5.0f + rnd.nextFloat() * 4.0f)
        {
            juce::Path grain;
            grain.startNewSubPath (a.getX(), y);
            for (float x = a.getX(); x <= a.getRight(); x += 20.0f)
                grain.lineTo (x, y + 1.5f * std::sin (x * 0.02f + y));
            g.setColour (Colours::black.withAlpha (0.07f + rnd.nextFloat() * 0.05f));
            g.strokePath (grain, juce::PathStrokeType (0.8f));
        }
    }
    // the pigeonholes: a grid of dark cells with brass labels
    const auto cw = 70.0f, ch = 48.0f;
    for (float y = a.getY() + 10.0f; y + ch < a.getBottom(); y += ch + 8.0f)
        for (float x = a.getX() + 10.0f; x + cw < a.getRight(); x += cw + 8.0f)
        {
            const auto cell = Rectangle<float> (x, y, cw, ch);
            g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.28f), 0.0f, cell.getY(), Colours::black.withAlpha (0.1f), 0.0f, cell.getBottom(), false));
            g.fillRect (cell);
            g.setColour (p.accent.withAlpha (0.16f));
            g.fillRoundedRectangle (Rectangle<float> (22.0f, 6.0f).withCentre ({ cell.getCentreX(), cell.getBottom() - 6.0f }), 1.0f);
        }
    vignette (g, a, 0.4f);
}

inline void delayBankEmblem (juce::Graphics& g, Point<float> c, float r) // four pigeonholes
{
    g.setColour (pal().accent);
    for (int i = 0; i < 4; ++i)
        g.drawRect (Rectangle<float> (r * 0.9f, r * 0.9f).withCentre (c.translated ((i % 2 == 0 ? -0.5f : 0.5f) * r, (i < 2 ? -0.5f : 0.5f) * r)), 1.0f);
}

inline const Theme& delayBankTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Delay Bank";
        x.base = Style::hardware;
        x.pal = { Colour (0xff3a2617), Colour (0xff150d07), Colour (0xffe0b25a), Colour (0xfff4dcb0), Colour (0xd0301f13), Colour (0xe4140c06),
                  Colour (0xff8a6440), Colour (0xfff6e6c8), Colour (0xff0c0704), Colour (0xffe0b25a), Colour (0xff6a4a1e), Colour (0xff24170c),
                  Colour (0xff1c1206), Colour (0xff2e1e12) };
        x.canvas = canvasDelayBank;
        x.titleEmblem = delayBankEmblem;
        x.drawCap = capBrass;
        x.arcGlow = 0.12f;
        x.thumbPointer = true;
        return x;
    }();
    return t;
}

// === Ember Saturator (saturation role): a forge — coals glowing at the bottom, heat haze, blackened iron ============

inline void canvasEmber (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    // the heat rising from the bed of coals
    g.setGradientFill (ColourGradient (p.accent.withAlpha (0.28f), 0.0f, a.getBottom(), p.accent.withAlpha (0.0f), 0.0f, a.getBottom() - a.getHeight() * 0.55f, false));
    g.fillRect (a);
    // the coals: lumpy dark shapes with glowing cracks along the bottom edge
    juce::Random rnd (451);
    for (float x = a.getX() - 10.0f; x < a.getRight(); x += 16.0f + rnd.nextFloat() * 14.0f)
    {
        const auto w = 20.0f + rnd.nextFloat() * 18.0f, h = 10.0f + rnd.nextFloat() * 10.0f;
        const auto coal = Rectangle<float> (x, a.getBottom() - h * 0.8f, w, h);
        g.setColour (Colour (0xff120806));
        g.fillEllipse (coal);
        const auto hot = rnd.nextFloat();
        g.setGradientFill (ColourGradient (p.accent2.withAlpha (0.5f + 0.4f * hot), coal.getCentreX(), coal.getY() + 2.0f, p.accent.withAlpha (0.0f), coal.getCentreX(), coal.getBottom(), false));
        g.fillEllipse (coal.reduced (w * 0.2f, h * 0.25f).translated (0.0f, -h * 0.1f));
    }
    // a few sparks frozen mid-air
    for (int i = 0; i < 24; ++i)
    {
        const Point<float> s { a.getX() + rnd.nextFloat() * a.getWidth(), a.getBottom() - a.getHeight() * (0.1f + 0.6f * rnd.nextFloat() * rnd.nextFloat()) };
        g.setColour (p.accent2.withAlpha (0.3f + 0.4f * rnd.nextFloat()));
        g.fillEllipse (Rectangle<float> (1.8f, 1.8f).withCentre (s));
    }
    vignette (g, a, 0.4f);
    displayDim (g, a, display);
}

inline void emberEmblem (juce::Graphics& g, Point<float> c, float r) // a flame
{
    const auto& p = pal();
    juce::Path f;
    f.startNewSubPath (c.x, c.y - r * 1.2f);
    f.cubicTo (c.x + r * 0.9f, c.y - r * 0.2f, c.x + r * 0.8f, c.y + r, c.x, c.y + r);
    f.cubicTo (c.x - r * 0.8f, c.y + r, c.x - r * 0.9f, c.y - r * 0.1f, c.x - r * 0.2f, c.y - r * 0.4f);
    f.quadraticTo (c.x - r * 0.1f, c.y - r * 0.1f, c.x, c.y - r * 1.2f);
    g.setGradientFill (ColourGradient (p.accent2, c.x, c.y + r * 0.6f, p.accent, c.x, c.y - r, false));
    g.fillPath (f);
}

inline void capBlackSteel (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.capTop.brighter (hover * 0.2f), c.x - capR * 0.5f, c.y - capR * 0.7f, p.capBottom, c.x + capR * 0.5f, c.y + capR, true));
    g.fillEllipse (cap);
    if (fx) // the fire's reflection along the bottom of the cap
    {
        g.setGradientFill (ColourGradient (p.accent.withAlpha (0.35f), c.x, cap.getBottom(), p.accent.withAlpha (0.0f), c.x, c.y, false));
        g.fillEllipse (cap.reduced (capR * 0.06f));
    }
}

inline void emberMotion (juce::Graphics& g, const Motion& m) // embers rising off the coals
{
    for (const auto& s : m.motes)
    {
        const auto life = juce::jlimit (0.0f, 1.0f, (s.y - m.stage.getY()) / juce::jmax (1.0f, m.stage.getHeight()));
        if (! reduced) bloom (g, { s.x, s.y }, s.size * 3.0f, m.p.accent, 0.3f * life);
        g.setColour (m.p.accent2.interpolatedWith (m.p.accent, 1.0f - life).withAlpha (0.25f + 0.65f * life));
        g.fillEllipse (Rectangle<float> (s.size, s.size).withCentre ({ s.x, s.y }));
    }
}

inline const Theme& emberTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Ember Saturator";
        x.base = Style::vivid;
        x.pal = { Colour (0xff1e1412), Colour (0xff080504), Colour (0xffff6a1a), Colour (0xffffd27a), Colour (0x9c1c1210), Colour (0xb8080504),
                  Colour (0xff8a4a2a), Colour (0xfffff0dc), Colour (0xff060303), Colour (0xff3a3434), Colour (0xff0a0808), Colour (0xff261a16),
                  Colour (0xffffe0b8), Colour (0xff221714) };
        x.canvas = canvasEmber;
        x.titleEmblem = emberEmblem;
        x.drawCap = capBlackSteel;
        x.twoTone = true;
        x.pointer = Pointer::glowLine;
        x.arcGlow = 0.3f;
        x.motes = 10;
        x.drift = Drift::rise;
        x.sizeMin = 1.4f;
        x.sizeRange = 1.6f;
        x.vyMin = -14.0f;
        x.vyRange = -12.0f;
        x.sway = 5.0f;
        x.reach = 3.0f;
        x.paintMotion = emberMotion;
        return x;
    }();
    return t;
}

// === Aether (air-EQ role): fresh air — a pale sky with high cirrus streaks and a breeze ============================

inline void canvasAether (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    softLight (g, { a.getCentreX(), a.getY() - a.getHeight() * 0.2f }, a.getWidth() * 0.8f, p.accent, 0.10f);
    // cirrus: long feathery streaks, slanted
    juce::Random rnd (7);
    for (int i = 0; i < 9; ++i)
    {
        const auto y = a.getY() + a.getHeight() * (0.08f + 0.5f * rnd.nextFloat());
        const auto x = a.getX() + a.getWidth() * (rnd.nextFloat() * 0.8f - 0.1f);
        const auto len = a.getWidth() * (0.2f + 0.3f * rnd.nextFloat());
        juce::Path streak;
        streak.startNewSubPath (x, y);
        streak.quadraticTo (x + len * 0.5f, y - 8.0f - rnd.nextFloat() * 10.0f, x + len, y - 3.0f);
        g.setColour (Colours::white.withAlpha (0.05f + 0.04f * rnd.nextFloat()));
        g.strokePath (streak, stroke (3.0f + rnd.nextFloat() * 5.0f));
        for (int k = 1; k < 6; ++k) // the feathering
        {
            const auto q = streak.getPointAlongPath (streak.getLength() * (float) k / 6.0f);
            g.drawLine ({ q, q.translated (-6.0f, 5.0f) }, 0.8f);
        }
    }
    // the breeze: fine curved lines low on the panel
    for (int i = 0; i < 5; ++i)
    {
        juce::Path w;
        const auto y = a.getBottom() - a.getHeight() * (0.1f + 0.07f * (float) i);
        w.startNewSubPath (a.getX(), y);
        for (float x = a.getX(); x <= a.getRight(); x += 10.0f)
            w.lineTo (x, y + 4.0f * std::sin (x * 0.015f + (float) i));
        g.setColour (p.accent.withAlpha (0.06f));
        g.strokePath (w, juce::PathStrokeType (1.0f));
    }
    vignette (g, a, 0.3f);
    displayDim (g, a, display);
}

inline void aetherEmblem (juce::Graphics& g, Point<float> c, float r) // a wind swirl
{
    juce::Path w;
    w.startNewSubPath (c.x - r * 1.2f, c.y);
    w.lineTo (c.x + r * 0.4f, c.y);
    w.addCentredArc (c.x + r * 0.4f, c.y - r * 0.45f, r * 0.45f, r * 0.45f, 0.0f, juce::MathConstants<float>::pi, juce::MathConstants<float>::pi * 2.6f, false);
    w.startNewSubPath (c.x - r * 0.9f, c.y + r * 0.55f);
    w.lineTo (c.x + r * 0.7f, c.y + r * 0.55f);
    g.setColour (pal().accent);
    g.strokePath (w, stroke (1.3f));
}

inline void capFrost (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.55f + hover * 0.2f), c.x - capR * 0.4f, c.y - capR * 0.6f, p.capBottom, c.x + capR * 0.6f, c.y + capR, true));
    g.fillEllipse (cap);
    if (fx)
    {
        g.setColour (Colours::white.withAlpha (0.35f));
        g.drawEllipse (cap.reduced (0.5f), 1.0f);
    }
}

inline const Theme& aetherTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Aether";
        x.base = Style::glow;
        x.pal = { Colour (0xff1e3550), Colour (0xff0b1424), Colour (0xff7fd0ff), Colour (0xffffffff), Colour (0x9c1c3048), Colour (0xb80b1424),
                  Colour (0xff7a9ac0), Colour (0xfff0f8ff), Colour (0xff07101c), Colour (0xffcfe6f5), Colour (0xff5a7a96), Colour (0xff1c2e44),
                  Colour (0xff0f2a44), Colour (0xff1a2d44) };
        x.canvas = canvasAether;
        x.titleEmblem = aetherEmblem;
        x.drawCap = capFrost;
        x.twoTone = true;
        return x;
    }();
    return t;
}

// === Fruity Blood Overdrive: a blood moon over a black field, glossy crimson ========================================

inline void canvasBlood (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto c = Point<float> (a.getRight() - a.getWidth() * 0.22f, a.getY() + a.getHeight() * 0.3f);
    const auto r = juce::jmin (a.getWidth(), a.getHeight()) * 0.2f;
    if (! reduced) softLight (g, c, r * 3.0f, p.accent, 0.18f);
    g.setGradientFill (ColourGradient (Colour (0xffff6a5a).withAlpha (0.7f), c.x - r * 0.4f, c.y - r * 0.5f, Colour (0xff5a0812).withAlpha (0.7f), c.x + r, c.y + r, true));
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    juce::Random rnd (66); // craters
    for (int i = 0; i < 7; ++i)
    {
        const auto q = c + Point<float> (rnd.nextFloat() - 0.5f, rnd.nextFloat() - 0.5f) * r * 1.3f;
        g.setColour (Colours::black.withAlpha (0.12f));
        g.fillEllipse (Rectangle<float> (r * 0.2f, r * 0.2f).withCentre (q));
    }
    // a black field's horizon of grass blades along the bottom
    juce::Path field;
    field.startNewSubPath (a.getX(), a.getBottom());
    for (float x = a.getX(); x <= a.getRight(); x += 4.0f)
        field.lineTo (x, a.getBottom() - a.getHeight() * 0.08f - rnd.nextFloat() * 8.0f);
    field.lineTo (a.getRight(), a.getBottom());
    field.closeSubPath();
    g.setColour (Colour (0xff050102));
    g.fillPath (field);
    vignette (g, a, 0.45f);
}

inline void bloodEmblem (juce::Graphics& g, Point<float> c, float r) // a drop
{
    juce::Path d;
    d.startNewSubPath (c.x, c.y - r * 1.1f);
    d.cubicTo (c.x + r * 0.2f, c.y - r * 0.5f, c.x + r * 0.85f, c.y, c.x + r * 0.7f, c.y + r * 0.4f);
    d.cubicTo (c.x + r * 0.5f, c.y + r * 1.0f, c.x - r * 0.5f, c.y + r * 1.0f, c.x - r * 0.7f, c.y + r * 0.4f);
    d.cubicTo (c.x - r * 0.85f, c.y, c.x - r * 0.2f, c.y - r * 0.5f, c.x, c.y - r * 1.1f);
    g.setGradientFill (ColourGradient (Colour (0xffff5a5a), c.x - r * 0.3f, c.y - r * 0.2f, Colour (0xff7a0a16), c.x + r * 0.5f, c.y + r, true));
    g.fillPath (d);
}

inline void capGlossRed (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.accent.brighter (0.4f + hover * 0.2f), c.x - capR * 0.4f, c.y - capR * 0.5f, p.accent.darker (0.85f), c.x + capR * 0.6f, c.y + capR, true));
    g.fillEllipse (cap);
    if (fx)
    {
        g.setColour (Colours::white.withAlpha (0.4f));
        g.fillEllipse (Rectangle<float> (capR * 0.6f, capR * 0.3f).withCentre ({ c.x - capR * 0.25f, c.y - capR * 0.52f }));
    }
}

inline const Theme& bloodTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Blood Overdrive";
        x.base = Style::hardware;
        x.pal = { Colour (0xff1a0508), Colour (0xff070102), Colour (0xffc4122f), Colour (0xffff8a8a), Colour (0x9c180508), Colour (0xb8070102),
                  Colour (0xff6a1a24), Colour (0xffffe6e6), Colour (0xff050102), Colour (0xff3a0a12), Colour (0xff120306), Colour (0xff22080c),
                  Colour (0xffffffff), Colour (0xff1e0609) };
        x.canvas = canvasBlood;
        x.titleEmblem = bloodEmblem;
        x.drawCap = capGlossRed;
        x.arcGlow = 0.25f;
        return x;
    }();
    return t;
}

// === Fruity Fast Dist: speed — motion-blur streaks and a sharp chevron, safety orange on asphalt ==================

inline void canvasFastDist (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    if (! reduced) // asphalt grain
    {
        juce::Random rnd (12);
        for (int i = 0; i < (int) (a.getWidth() * a.getHeight() / 70.0f); ++i)
        {
            g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (0.04f));
            g.fillRect (Rectangle<float> (a.getX() + rnd.nextFloat() * a.getWidth(), a.getY() + rnd.nextFloat() * a.getHeight(), 1.2f, 1.2f));
        }
    }
    juce::Random rnd (21); // speed streaks
    for (int i = 0; i < 16; ++i)
    {
        const auto y = a.getY() + rnd.nextFloat() * a.getHeight();
        const auto x = a.getX() + rnd.nextFloat() * a.getWidth() * 0.6f;
        const auto len = a.getWidth() * (0.15f + 0.4f * rnd.nextFloat());
        g.setGradientFill (ColourGradient (p.accent.withAlpha (0.0f), x, y, p.accent.withAlpha (0.12f), x + len, y, false));
        g.fillRect (Rectangle<float> (x, y, len, 1.0f + rnd.nextFloat() * 2.0f));
    }
    // a big chevron, right
    juce::Path chev;
    const auto c = Point<float> (a.getRight() - a.getWidth() * 0.16f, a.getCentreY());
    const auto r = a.getHeight() * 0.35f;
    chev.startNewSubPath (c.x - r * 0.6f, c.y - r);
    chev.lineTo (c.x + r * 0.2f, c.y);
    chev.lineTo (c.x - r * 0.6f, c.y + r);
    g.setColour (p.accent.withAlpha (0.10f));
    g.strokePath (chev, juce::PathStrokeType (r * 0.3f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    vignette (g, a, 0.35f);
}

inline void fastDistEmblem (juce::Graphics& g, Point<float> c, float r)
{
    juce::Path chev;
    chev.startNewSubPath (c.x - r * 0.6f, c.y - r * 0.8f);
    chev.lineTo (c.x + r * 0.3f, c.y);
    chev.lineTo (c.x - r * 0.6f, c.y + r * 0.8f);
    g.setColour (pal().accent);
    g.strokePath (chev, juce::PathStrokeType (r * 0.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
}

inline const Theme& fastDistTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Fast Dist";
        x.base = Style::vivid;
        x.pal = { Colour (0xff1c1c1c), Colour (0xff0a0a0a), Colour (0xffff7a00), Colour (0xffffd400), Colour (0x9c1a1a1a), Colour (0xb80a0a0a),
                  Colour (0xff6a5a4a), Colour (0xfffff2e0), Colour (0xff060606), Colour (0xff2a2a2a), Colour (0xff0e0e0e), Colour (0xff222222),
                  Colour (0xffff9a2a), Colour (0xff1c1c1c) };
        x.canvas = canvasFastDist;
        x.titleEmblem = fastDistEmblem;
        x.cap = Cap::rubber;
        x.pointer = Pointer::glowLine;
        x.arcGlow = 0.3f;
        return x;
    }();
    return t;
}

// === Fruity WaveShaper: an oscilloscope lab — a green phosphor graticule, transfer curves ghosted ================

inline void canvasWaveShaper (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // the graticule: major divisions with minor ticks on the centre lines
    const auto div = 40.0f;
    for (float x = a.getX(); x < a.getRight(); x += div)
    {
        g.setColour (p.accent.withAlpha (0.06f));
        g.fillRect (Rectangle<float> (x, a.getY(), 1.0f, a.getHeight()));
    }
    for (float y = a.getY(); y < a.getBottom(); y += div)
        g.fillRect (Rectangle<float> (a.getX(), y, a.getWidth(), 1.0f));
    for (float x = a.getX(); x < a.getRight(); x += div / 5.0f)
        g.fillRect (Rectangle<float> (x, a.getCentreY() - 2.0f, 1.0f, 4.0f));
    // ghosted transfer curves (tanh-like, folded)
    const auto s = juce::jmin (a.getWidth(), a.getHeight()) * 0.8f;
    const auto c = a.getCentre();
    for (int k = 0; k < 4; ++k)
    {
        juce::Path curve;
        for (int i = 0; i <= 60; ++i)
        {
            const auto x = -1.0f + 2.0f * (float) i / 60.0f;
            const auto drive = 1.0f + (float) k * 1.8f;
            const auto y = std::tanh (x * drive) / std::tanh (drive);
            const Point<float> q { c.x + x * s * 0.5f, c.y - y * s * 0.5f };
            if (i == 0) curve.startNewSubPath (q);
            else curve.lineTo (q);
        }
        if (! reduced)
        {
            g.setColour (p.accent.withAlpha (0.04f));
            g.strokePath (curve, juce::PathStrokeType (5.0f));
        }
        g.setColour (p.accent.withAlpha (0.12f));
        g.strokePath (curve, juce::PathStrokeType (1.2f));
    }
    // scanlines
    g.setColour (Colours::black.withAlpha (0.12f));
    for (float y = a.getY(); y < a.getBottom(); y += 3.0f)
        g.fillRect (Rectangle<float> (a.getX(), y, a.getWidth(), 1.0f));
    vignette (g, a, 0.5f);
}

inline void waveShaperEmblem (juce::Graphics& g, Point<float> c, float r) // an S curve
{
    juce::Path s;
    for (int i = 0; i <= 20; ++i)
    {
        const auto x = -1.0f + 2.0f * (float) i / 20.0f;
        const Point<float> q { c.x + x * r, c.y - std::tanh (x * 2.5f) * r * 0.8f };
        if (i == 0) s.startNewSubPath (q);
        else s.lineTo (q);
    }
    g.setColour (pal().accent);
    g.strokePath (s, stroke (1.4f));
}

inline const Theme& waveShaperTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity WaveShaper";
        x.base = Style::neon;
        x.pal = { Colour (0xff0b1a10), Colour (0xff030805), Colour (0xff5cff8a), Colour (0xffc8ffd6), Colour (0x9c0a1a0f), Colour (0xb8030805),
                  Colour (0xff3a8a52), Colour (0xffd6ffe0), Colour (0xff020503), Colour (0xff2a3a2e), Colour (0xff0a120c), Colour (0xff132a19),
                  Colour (0xffd6ffe0), Colour (0xff0f2014) };
        x.canvas = canvasWaveShaper;
        x.titleEmblem = waveShaperEmblem;
        x.cap = Cap::flat;
        x.pointer = Pointer::glowLine;
        x.ticks = 11;
        x.boldTicks = true;
        x.arcGlow = 0.3f;
        return x;
    }();
    return t;
}

// === Fruity Soft Clipper: velvet clip — a sine whose tops round off against two soft bars ==========================

inline void canvasSoftClipper (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto c = a.getCentreY();
    const auto amp = a.getHeight() * 0.32f;
    for (const auto sign : { -1.0f, 1.0f }) // the two velvet bars
    {
        const auto y = c + sign * amp;
        g.setGradientFill (ColourGradient (p.accent.withAlpha (0.10f), 0.0f, y, p.accent.withAlpha (0.0f), 0.0f, y + sign * 26.0f, false));
        g.fillRect (Rectangle<float> (a.getX(), juce::jmin (y, y + sign * 26.0f), a.getWidth(), 26.0f));
    }
    juce::Path wave; // a sine driven into a soft clip
    for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
    {
        const auto v = std::tanh (2.2f * std::sin ((x - a.getX()) * 0.025f)) / std::tanh (2.2f);
        const Point<float> q { x, c - v * amp };
        if (x == a.getX()) wave.startNewSubPath (q);
        else wave.lineTo (q);
    }
    if (! reduced)
    {
        g.setColour (p.accent.withAlpha (0.06f));
        g.strokePath (wave, stroke (8.0f));
    }
    g.setColour (p.accent.withAlpha (0.20f));
    g.strokePath (wave, stroke (1.6f));
    vignette (g, a, 0.4f);
}

inline void softClipperEmblem (juce::Graphics& g, Point<float> c, float r) // a rounded square wave
{
    juce::Path w;
    w.startNewSubPath (c.x - r * 1.2f, c.y + r * 0.6f);
    w.quadraticTo (c.x - r * 1.0f, c.y - r * 0.6f, c.x - r * 0.6f, c.y - r * 0.6f);
    w.lineTo (c.x - r * 0.2f, c.y - r * 0.6f);
    w.quadraticTo (c.x + r * 0.1f, c.y - r * 0.6f, c.x + r * 0.2f, c.y + r * 0.6f);
    w.lineTo (c.x + r * 0.7f, c.y + r * 0.6f);
    w.quadraticTo (c.x + r * 1.0f, c.y + r * 0.6f, c.x + r * 1.2f, c.y - r * 0.4f);
    g.setColour (pal().accent);
    g.strokePath (w, stroke (1.4f));
}

inline const Theme& softClipperTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Soft Clipper";
        x.base = Style::glow;
        x.pal = { Colour (0xff261a16), Colour (0xff0d0807), Colour (0xffffb08a), Colour (0xffffe0c8), Colour (0x9c241815), Colour (0xb80d0807),
                  Colour (0xff8a6a5a), Colour (0xfffff0e6), Colour (0xff080504), Colour (0xff4a3630), Colour (0xff1a120f), Colour (0xff2a1d19),
                  Colour (0xfffff0e6), Colour (0xff241815) };
        x.canvas = canvasSoftClipper;
        x.titleEmblem = softClipperEmblem;
        x.drawCap = capVelvet;
        x.twoTone = true;
        return x;
    }();
    return t;
}

// === Hardcore (guitar-rig role): a pedalboard on grille cloth — stomp boxes, patch cables ==========================

inline void canvasHardcore (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    if (! reduced) // grille cloth: a fine weave
    {
        g.setColour (Colours::white.withAlpha (0.025f));
        for (float x = a.getX(); x < a.getRight(); x += 3.0f)
            g.fillRect (Rectangle<float> (x, a.getY(), 1.0f, a.getHeight()));
        g.setColour (Colours::black.withAlpha (0.15f));
        for (float y = a.getY(); y < a.getBottom(); y += 3.0f)
            g.fillRect (Rectangle<float> (a.getX(), y, a.getWidth(), 1.0f));
    }
    // stomp-box silhouettes in a row along the bottom, each its own colour, joined by cables
    const Colour shells[] { Colour (0xffd4561e), Colour (0xff2a7ad4), Colour (0xff3ab06a), Colour (0xffe0c03a), Colour (0xffb03a8a), Colour (0xffd8d8d8) };
    const auto w = 46.0f, h = 62.0f;
    const auto y = a.getBottom() - h * 0.55f;
    int i = 0;
    for (float x = a.getX() + 14.0f; x + w < a.getRight(); x += w + 20.0f, ++i)
    {
        const auto box = Rectangle<float> (x, y, w, h);
        g.setColour (shells[i % 6].withAlpha (0.16f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (Colours::white.withAlpha (0.08f));
        for (int k = 0; k < 2; ++k)
            g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre ({ box.getX() + 13.0f + (float) k * 20.0f, box.getY() + 12.0f }));
        g.fillRoundedRectangle (Rectangle<float> (22.0f, 10.0f).withCentre ({ box.getCentreX(), box.getY() + 38.0f }), 2.0f);
        if (x + w * 2.0f + 20.0f < a.getRight()) // the cable to the next box
        {
            juce::Path cable;
            cable.startNewSubPath (box.getRight(), box.getY() + 22.0f);
            cable.quadraticTo (box.getRight() + 10.0f, box.getY() + 2.0f, box.getRight() + 20.0f, box.getY() + 22.0f);
            g.setColour (Colours::black.withAlpha (0.5f));
            g.strokePath (cable, juce::PathStrokeType (3.0f));
        }
    }
    softLight (g, { a.getCentreX(), a.getY() }, a.getWidth() * 0.5f, p.accent, 0.06f);
    vignette (g, a, 0.45f);
}

inline void hardcoreEmblem (juce::Graphics& g, Point<float> c, float r) // a jack plug
{
    const auto& p = pal();
    g.setColour (p.accent2);
    g.fillRoundedRectangle (Rectangle<float> (r * 0.9f, r * 1.2f).withCentre (c.translated (0.0f, r * 0.4f)), 1.0f);
    g.setColour (p.accent);
    g.fillRect (Rectangle<float> (r * 0.35f, r * 1.0f).withCentre (c.translated (0.0f, -r * 0.6f)));
    g.fillEllipse (Rectangle<float> (r * 0.45f, r * 0.45f).withCentre (c.translated (0.0f, -r * 1.1f)));
}

inline const Theme& hardcoreTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Hardcore";
        x.base = Style::hardware;
        x.pal = { Colour (0xff1c1a18), Colour (0xff090807), Colour (0xffff6a3a), Colour (0xffe0e0e0), Colour (0x9c1a1816), Colour (0xb8090807),
                  Colour (0xff6a5a4e), Colour (0xfff2ece6), Colour (0xff060505), Colour (0xff2c2c2c), Colour (0xff0b0b0b), Colour (0xff221f1c),
                  Colour (0xfff4f4f4), Colour (0xff1e1c1a) };
        x.canvas = canvasHardcore;
        x.titleEmblem = hardcoreEmblem;
        x.drawCap = capPedal;
        x.pointerFrom = 0.3f;
        x.arcGlow = 0.2f;
        return x;
    }();
    return t;
}

// === Distructor: a wrecking yard — cracked concrete, rebar, warning red and rust ===================================

inline void canvasDistructor (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    if (! reduced) // concrete: blotches
    {
        juce::Random rnd (13);
        for (int i = 0; i < (int) (a.getWidth() * a.getHeight() / 500.0f); ++i)
        {
            const auto r = 3.0f + rnd.nextFloat() * 9.0f;
            g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (0.025f));
            g.fillEllipse (Rectangle<float> (r * 2.0f, r * 1.4f).withCentre ({ a.getX() + rnd.nextFloat() * a.getWidth(), a.getY() + rnd.nextFloat() * a.getHeight() }));
        }
    }
    // cracks branching from a point of impact
    juce::Random rnd (99);
    const auto o = Point<float> (a.getX() + a.getWidth() * 0.7f, a.getY() + a.getHeight() * 0.35f);
    for (int k = 0; k < 7; ++k)
    {
        juce::Path crack;
        auto q = o;
        auto ang = juce::MathConstants<float>::twoPi * (float) k / 7.0f + rnd.nextFloat() * 0.5f;
        crack.startNewSubPath (q);
        for (int s = 0; s < 9; ++s)
        {
            ang += (rnd.nextFloat() - 0.5f) * 0.9f;
            q += Point<float> (std::cos (ang), std::sin (ang)) * (10.0f + rnd.nextFloat() * 16.0f);
            crack.lineTo (q);
        }
        g.setColour (Colours::black.withAlpha (0.45f));
        g.strokePath (crack, juce::PathStrokeType (1.6f));
        g.setColour (p.accent.withAlpha (0.08f));
        g.strokePath (crack, juce::PathStrokeType (4.0f));
    }
    // rebar poking out along the bottom, rusty
    for (float x = a.getX() + 20.0f; x < a.getRight(); x += 52.0f)
    {
        g.setColour (p.accent2.withAlpha (0.22f));
        g.drawLine (x, a.getBottom(), x + 14.0f, a.getBottom() - 30.0f - (float) ((int) x % 23), 3.0f);
    }
    vignette (g, a, 0.45f);
}

inline void distructorEmblem (juce::Graphics& g, Point<float> c, float r) // a crack
{
    juce::Path k;
    k.startNewSubPath (c.x - r * 0.2f, c.y - r * 1.1f);
    k.lineTo (c.x + r * 0.3f, c.y - r * 0.3f);
    k.lineTo (c.x - r * 0.3f, c.y + r * 0.1f);
    k.lineTo (c.x + r * 0.2f, c.y + r * 1.1f);
    g.setColour (pal().accent);
    g.strokePath (k, juce::PathStrokeType (1.8f));
}

inline const Theme& distructorTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Distructor";
        x.base = Style::vivid;
        x.pal = { Colour (0xff2a2a2a), Colour (0xff0e0e0e), Colour (0xffff2e2e), Colour (0xffc46a3a), Colour (0x9c262626), Colour (0xb80e0e0e),
                  Colour (0xff6a5a52), Colour (0xfff2eeee), Colour (0xff080808), Colour (0xff4a4a4a), Colour (0xff161616), Colour (0xff222222),
                  Colour (0xffffe6e6), Colour (0xff262626) };
        x.canvas = canvasDistructor;
        x.sectionDeco = sentryRivets;
        x.titleEmblem = distructorEmblem;
        x.drawCap = capIron;
        x.pointer = Pointer::glowLine;
        x.arcGlow = 0.25f;
        return x;
    }();
    return t;
}

// === Fruity Chorus: a choir of waves — several sines slightly out of phase, woven together ========================

inline void wavesWoven (juce::Graphics& g, Rectangle<float> a, int count, float amp, float freq, float spread, Colour c1, Colour c2, float alpha)
{
    for (int k = 0; k < count; ++k)
    {
        juce::Path w;
        const auto ph = (float) k * spread;
        for (float x = a.getX(); x <= a.getRight(); x += 3.0f)
        {
            const Point<float> q { x, a.getCentreY() + amp * std::sin ((x - a.getX()) * freq + ph) * (0.8f + 0.2f * std::sin ((x - a.getX()) * 0.004f + ph)) };
            if (x == a.getX()) w.startNewSubPath (q);
            else w.lineTo (q);
        }
        g.setColour (c1.interpolatedWith (c2, (float) k / (float) juce::jmax (1, count - 1)).withAlpha (alpha));
        g.strokePath (w, juce::PathStrokeType (1.3f));
    }
}

inline void canvasChorus (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    wavesWoven (g, a.withTrimmedTop (a.getHeight() * 0.2f), 5, a.getHeight() * 0.22f, 0.018f, 0.35f, p.accent, p.accent2, 0.14f);
    softLight (g, { a.getCentreX(), a.getCentreY() }, a.getWidth() * 0.5f, p.accent, 0.05f);
    vignette (g, a, 0.4f);
}

inline void chorusEmblem (juce::Graphics& g, Point<float> c, float r) // three waves
{
    const auto& p = pal();
    for (int k = 0; k < 3; ++k)
    {
        juce::Path w;
        for (int i = 0; i <= 16; ++i)
        {
            const auto x = -1.0f + 2.0f * (float) i / 16.0f;
            const Point<float> q { c.x + x * r * 1.2f, c.y + std::sin (x * 3.14f + (float) k * 0.6f) * r * 0.6f };
            if (i == 0) w.startNewSubPath (q);
            else w.lineTo (q);
        }
        g.setColour (p.accent.interpolatedWith (p.accent2, (float) k / 2.0f));
        g.strokePath (w, juce::PathStrokeType (1.0f));
    }
}

inline void capPebble (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.capTop.brighter (0.3f + hover * 0.2f), c.x - capR * 0.45f, c.y - capR * 0.55f, p.capBottom, c.x + capR * 0.6f, c.y + capR * 0.9f, true));
    g.fillEllipse (cap.withSizeKeepingCentre (cap.getWidth(), cap.getHeight() * 0.94f));
    if (fx)
    {
        g.setColour (Colours::white.withAlpha (0.35f));
        g.fillEllipse (Rectangle<float> (capR * 0.5f, capR * 0.28f).withCentre ({ c.x - capR * 0.32f, c.y - capR * 0.48f }));
    }
}

inline const Theme& chorusTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Chorus";
        x.base = Style::glow;
        x.pal = { Colour (0xff111a2c), Colour (0xff05080f), Colour (0xff4fd8e8), Colour (0xffb59cff), Colour (0xc0101a2a), Colour (0xdc05080f),
                  Colour (0xff5a7aa0), Colour (0xffe8f6ff), Colour (0xff04060b), Colour (0xff3a5a7a), Colour (0xff0e1a2a), Colour (0xff1a2638),
                  Colour (0xffffffff), Colour (0xff141e30) };
        x.canvas = canvasChorus;
        x.titleEmblem = chorusEmblem;
        x.drawCap = capPebble;
        x.twoTone = true;
        return x;
    }();
    return t;
}

// === Hyper Chorus: synthwave — a neon sun over a perspective grid floor ============================================

inline void canvasHyperChorus (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto hz = a.getY() + a.getHeight() * 0.55f;
    // the sun: horizontal slices, pink to orange
    const auto c = Point<float> (a.getCentreX(), hz);
    const auto r = a.getHeight() * 0.32f;
    g.saveState();
    g.reduceClipRegion (Rectangle<float> (a.getX(), a.getY(), a.getWidth(), hz - a.getY()).toNearestInt());
    if (! reduced) softLight (g, c, r * 2.2f, p.accent, 0.15f);
    g.setGradientFill (ColourGradient (Colour (0xffffd25a).withAlpha (0.5f), c.x, c.y - r, p.accent.withAlpha (0.5f), c.x, c.y, false));
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setColour (p.bgTop);
    for (int i = 0; i < 6; ++i)
        g.fillRect (Rectangle<float> (c.x - r, c.y - r * 0.5f + (float) i * r * 0.1f, r * 2.0f, 1.0f + (float) i * 0.8f));
    g.restoreState();
    // the grid floor
    g.setColour (p.accent2.withAlpha (0.14f));
    g.fillRect (Rectangle<float> (a.getX(), hz, a.getWidth(), 1.2f));
    for (int i = -12; i <= 12; ++i)
        g.drawLine ({ { c.x + (float) i * 14.0f, hz }, { c.x + (float) i * a.getWidth() * 0.12f, a.getBottom() } }, 0.9f);
    for (int k = 1; k < 8; ++k)
    {
        const auto y = hz + (a.getBottom() - hz) * std::pow ((float) k / 8.0f, 1.8f);
        g.setColour (p.accent2.withAlpha (0.06f + 0.015f * (float) k));
        g.fillRect (Rectangle<float> (a.getX(), y, a.getWidth(), 1.0f));
    }
    vignette (g, a, 0.35f);
}

inline void hyperChorusEmblem (juce::Graphics& g, Point<float> c, float r) // a sliced sun
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (Colour (0xffffd25a), c.x, c.y - r, p.accent, c.x, c.y + r, false));
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setColour (p.bgTop);
    for (int i = 0; i < 3; ++i)
        g.fillRect (Rectangle<float> (c.x - r, c.y + (float) i * r * 0.35f, r * 2.0f, 1.0f));
}

inline void capNeonRing (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setColour (Colour (0xff0b0614));
    g.fillEllipse (cap);
    if (fx && ! reduced) bloom (g, c, capR * 1.3f, p.accent, 0.2f + hover * 0.2f);
    g.setColour (p.accent.withAlpha (0.85f));
    g.drawEllipse (cap.reduced (capR * 0.16f), 1.4f);
    g.setColour (p.accent2.withAlpha (0.6f));
    g.drawEllipse (cap.reduced (capR * 0.42f), 1.0f);
}

inline void hyperChorusMotion (juce::Graphics& g, const Motion& m) // a little grid floor scrolling towards the viewer
{
    const auto s = m.stage;
    if (s.getWidth() < 40.0f || s.getHeight() < 10.0f) return;
    g.saveState();
    g.reduceClipRegion (s.toNearestInt());
    const auto off = std::fmod (m.t * 0.6f, 1.0f);
    for (int k = 0; k < 6; ++k)
    {
        const auto f = std::pow (((float) k + off) / 6.0f, 1.8f);
        g.setColour (m.p.accent2.withAlpha (0.15f + 0.4f * f));
        g.fillRect (Rectangle<float> (s.getX(), s.getY() + s.getHeight() * f, s.getWidth(), 1.0f));
    }
    for (int i = -6; i <= 6; ++i)
    {
        g.setColour (m.p.accent2.withAlpha (0.35f));
        g.drawLine ({ { s.getCentreX() + (float) i * 6.0f, s.getY() }, { s.getCentreX() + (float) i * s.getWidth() * 0.09f, s.getBottom() } }, 0.8f);
    }
    g.restoreState();
}

inline const Theme& hyperChorusTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Hyper Chorus";
        x.base = Style::neon;
        x.pal = { Colour (0xff1e0c33), Colour (0xff07030f), Colour (0xffff3fa4), Colour (0xff3fe8ff), Colour (0xc01a0a2c), Colour (0xdc07030f),
                  Colour (0xff8a4aa0), Colour (0xffffe6f6), Colour (0xff05020a), Colour (0xff2a1440), Colour (0xff0b0614), Colour (0xff22103a),
                  Colour (0xffffffff), Colour (0xff1c0b2e) };
        x.canvas = canvasHyperChorus;
        x.titleEmblem = hyperChorusEmblem;
        x.drawCap = capNeonRing;
        x.twoTone = true;
        x.pointer = Pointer::glowLine;
        x.arcGlow = 0.35f;
        x.stageRepaint = true;
        x.paintMotion = hyperChorusMotion;
        return x;
    }();
    return t;
}

// === Vintage Chorus: a 1980s rack chorus — brushed silver, two big push buttons and red lamps printed as decoration =

inline void canvasVintageChorus (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    brushedLines (g, a, 1984, 0.05f);
    // a black stripe across the top like the unit's legend band
    g.setColour (Colours::black.withAlpha (0.22f));
    g.fillRect (Rectangle<float> (a.getX(), a.getY() + a.getHeight() * 0.06f, a.getWidth(), 10.0f));
    // two big push buttons with red lamps, low right, as decoration
    for (int i = 0; i < 2; ++i)
    {
        const auto b = Rectangle<float> (36.0f, 22.0f).withCentre ({ a.getRight() - 70.0f + (float) i * 44.0f, a.getBottom() - 26.0f });
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.12f), 0.0f, b.getY(), Colours::black.withAlpha (0.12f), 0.0f, b.getBottom(), false));
        g.fillRoundedRectangle (b, 2.0f);
        g.setColour (p.accent.withAlpha (0.35f));
        g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (b.getCentre().translated (0.0f, -18.0f)));
    }
    cornerScrews (g, a, 8.0f, 7.0f);
    vignette (g, a, 0.3f);
}

inline void vintageChorusEmblem (juce::Graphics& g, Point<float> c, float r) // two lamps
{
    const auto& p = pal();
    for (const auto dx : { -0.6f, 0.6f })
    {
        if (! reduced) bloom (g, c.translated (dx * r, 0.0f), r * 1.4f, p.accent, 0.3f);
        g.setColour (p.accent);
        g.fillEllipse (Rectangle<float> (r * 0.8f, r * 0.8f).withCentre (c.translated (dx * r, 0.0f)));
    }
}

inline const Theme& vintageChorusTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Vintage Chorus";
        x.base = Style::hardware;
        x.pal = { Colour (0xff8a8e94), Colour (0xff4a4e54), Colour (0xffff3a2e), Colour (0xff2a2a2e), Colour (0xd8555a60), Colour (0xe83a3e44),
                  Colour (0xffb5bac0), Colour (0xff111214), Colour (0xff141517), Colour (0xffe2e5e9), Colour (0xff7d838a), Colour (0xff2e3136),
                  Colour (0xff111214), Colour (0xff6a6e74) };
        x.canvas = canvasVintageChorus;
        x.sectionDeco = ridgeScrews;
        x.titleEmblem = vintageChorusEmblem;
        x.drawCap = capChrome;
        x.buttTrack = true;
        x.arcGlow = 0.15f;
        x.thumbPointer = true;
        return x;
    }();
    return t;
}

// === Fruity Flanger: a jet — a contrail sweeping through a comb of stripes =========================================

inline void canvasFlanger (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // the comb: vertical stripes whose spacing sweeps across the panel
    auto x = a.getX();
    for (int i = 0; x < a.getRight(); ++i)
    {
        const auto u = (x - a.getX()) / a.getWidth();
        const auto gap = 6.0f + 18.0f * (0.5f + 0.5f * std::sin (u * 6.0f));
        g.setColour (p.accent2.withAlpha (0.05f));
        g.fillRect (Rectangle<float> (x, a.getY(), 1.5f, a.getHeight()));
        x += gap;
    }
    // the contrail: a long swoosh from low left to high right, widening
    juce::Path trail;
    trail.startNewSubPath (a.getX(), a.getBottom() - a.getHeight() * 0.15f);
    trail.cubicTo (a.getX() + a.getWidth() * 0.4f, a.getBottom() - a.getHeight() * 0.1f, a.getX() + a.getWidth() * 0.6f, a.getY() + a.getHeight() * 0.5f,
                   a.getRight(), a.getY() + a.getHeight() * 0.12f);
    if (! reduced)
    {
        g.setColour (Colours::white.withAlpha (0.04f));
        g.strokePath (trail, stroke (22.0f));
    }
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.02f), a.getX(), a.getBottom(), Colours::white.withAlpha (0.28f), a.getRight(), a.getY(), false));
    g.strokePath (trail, stroke (2.5f));
    vignette (g, a, 0.35f);
}

inline void flangerEmblem (juce::Graphics& g, Point<float> c, float r) // a swoosh
{
    juce::Path s;
    s.startNewSubPath (c.x - r * 1.2f, c.y + r * 0.7f);
    s.quadraticTo (c.x, c.y + r * 0.6f, c.x + r * 1.2f, c.y - r * 0.8f);
    g.setColour (pal().accent);
    g.strokePath (s, stroke (2.0f));
}

inline const Theme& flangerTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Flanger";
        x.base = Style::vivid;
        x.pal = { Colour (0xff1b2735), Colour (0xff070b11), Colour (0xff6ac8ff), Colour (0xffeaf6ff), Colour (0xc01a2534), Colour (0xdc070b11),
                  Colour (0xff6a8aa8), Colour (0xffeef6ff), Colour (0xff050810), Colour (0xffc8d0d8), Colour (0xff5a646e), Colour (0xff1e2a38),
                  Colour (0xff0f1a26), Colour (0xff1a2534) };
        x.canvas = canvasFlanger;
        x.titleEmblem = flangerEmblem;
        x.drawCap = capBrushed;
        x.arcGlow = 0.25f;
        return x;
    }();
    return t;
}

// === Fruity Flangus: liquid metal — a mercury surface with ripples ================================================

inline void canvasFlangus (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // ripples from two drops, interfering
    for (const auto& c : { Point<float> (a.getX() + a.getWidth() * 0.3f, a.getY() + a.getHeight() * 0.6f), Point<float> (a.getX() + a.getWidth() * 0.75f, a.getY() + a.getHeight() * 0.35f) })
        for (int i = 1; i < 14; ++i)
        {
            const auto r = (float) i * 16.0f;
            g.setColour ((i % 2 == 0 ? p.accent2 : p.accent).withAlpha (0.07f - 0.004f * (float) i));
            g.drawEllipse (Rectangle<float> (r * 2.0f, r * 0.8f).withCentre (c), 1.6f);
        }
    // a sheen band across
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.0f), 0.0f, a.getY() + a.getHeight() * 0.3f, Colours::white.withAlpha (0.05f), 0.0f, a.getY() + a.getHeight() * 0.45f, false));
    g.fillRect (a.withTrimmedTop (a.getHeight() * 0.3f).withHeight (a.getHeight() * 0.15f));
    vignette (g, a, 0.4f);
}

inline void flangusEmblem (juce::Graphics& g, Point<float> c, float r) // a ripple
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.drawEllipse (Rectangle<float> (r * 2.2f, r * 0.9f).withCentre (c), 1.2f);
    g.setColour (p.accent2);
    g.drawEllipse (Rectangle<float> (r * 1.1f, r * 0.45f).withCentre (c), 1.0f);
}

inline void capMercury (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    ColourGradient m (Colours::white.withAlpha (0.95f), c.x - capR * 0.4f, c.y - capR * 0.6f, Colour (0xff3a3e48), c.x + capR * 0.6f, c.y + capR, true);
    m.addColour (0.5, Colour (0xffa8afba).brighter (hover * 0.15f));
    g.setGradientFill (m);
    g.fillEllipse (cap);
    if (fx)
    {
        g.setColour (pal().accent.withAlpha (0.25f));
        g.drawEllipse (cap.reduced (capR * 0.3f).translated (capR * 0.1f, capR * 0.15f), 0.8f);
    }
}

inline const Theme& flangusTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Flangus";
        x.base = Style::neon;
        x.pal = { Colour (0xff23222e), Colour (0xff09090f), Colour (0xffb28aff), Colour (0xffd8dde3), Colour (0xc021202c), Colour (0xdc09090f),
                  Colour (0xff7a7890), Colour (0xfff2f0ff), Colour (0xff06060a), Colour (0xffd8dde3), Colour (0xff4a4e58), Colour (0xff22212e),
                  Colour (0xff1a1830), Colour (0xff201f2c) };
        x.canvas = canvasFlangus;
        x.titleEmblem = flangusEmblem;
        x.drawCap = capMercury;
        x.twoTone = true;
        return x;
    }();
    return t;
}

// === Fruity Phaser: phase rings — concentric rings sliding out of phase (a moiré) ==================================

inline void canvasPhaser (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto c1 = Point<float> (a.getX() + a.getWidth() * 0.62f, a.getCentreY());
    const auto c2 = c1.translated (14.0f, 4.0f);
    for (int i = 1; i < 28; ++i)
    {
        const auto r = (float) i * 9.0f;
        g.setColour (p.accent.withAlpha (0.06f));
        g.drawEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c1), 1.4f);
        g.setColour (p.accent2.withAlpha (0.05f));
        g.drawEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c2), 1.4f);
    }
    vignette (g, a, 0.45f);
}

inline void phaserEmblem (juce::Graphics& g, Point<float> c, float r) // two offset rings
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.drawEllipse (Rectangle<float> (r * 1.6f, r * 1.6f).withCentre (c.translated (-r * 0.3f, 0.0f)), 1.3f);
    g.setColour (p.accent2);
    g.drawEllipse (Rectangle<float> (r * 1.6f, r * 1.6f).withCentre (c.translated (r * 0.3f, 0.0f)), 1.3f);
}

inline const Theme& phaserTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Phaser";
        x.base = Style::neon;
        x.pal = { Colour (0xff0f1f22), Colour (0xff04090a), Colour (0xffff9a3c), Colour (0xff3ce0d0), Colour (0xc00e1d20), Colour (0xdc04090a),
                  Colour (0xff4f7a7a), Colour (0xfffff0e0), Colour (0xff030707), Colour (0xff2a3a3c), Colour (0xff0b1213), Colour (0xff17292c),
                  Colour (0xffffe6cc), Colour (0xff122326) };
        x.canvas = canvasPhaser;
        x.titleEmblem = phaserEmblem;
        x.cap = Cap::rubber;
        x.twoTone = true;
        x.arcGlow = 0.28f;
        return x;
    }();
    return t;
}

// === Vintage Phaser: a small phaser stomp box in burnt-orange enamel with a jewel lamp ==============================

inline void canvasVintagePhaser (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    if (! reduced) // hammertone enamel
    {
        juce::Random rnd (1974);
        for (int i = 0; i < (int) (a.getWidth() * a.getHeight() / 220.0f); ++i)
        {
            const auto r = 2.0f + rnd.nextFloat() * 4.0f;
            g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (0.035f));
            g.fillEllipse (Rectangle<float> (r * 2.0f, r * 1.7f).withCentre ({ a.getX() + rnd.nextFloat() * a.getWidth(), a.getY() + rnd.nextFloat() * a.getHeight() }));
        }
    }
    softLight (g, { a.getX() + a.getWidth() * 0.3f, a.getY() }, a.getWidth() * 0.6f, Colours::white, 0.06f);
    cornerScrews (g, a, 9.0f, 8.0f);
    vignette (g, a, 0.4f);
}

inline void vintagePhaserMotion (juce::Graphics& g, const Motion& m) // the jewel lamp blinks slowly (an LFO's pace)
{
    const auto s = m.stage;
    if (s.getWidth() < 16.0f) return;
    const auto c = Point<float> (s.getRight() - 12.0f, s.getCentreY());
    const auto pulse = 0.5f + 0.5f * std::sin (m.t * 3.0f);
    if (! reduced) bloom (g, c, 12.0f, m.p.accent2, 0.15f + 0.45f * pulse);
    g.setColour (m.p.accent2.interpolatedWith (Colours::white, 0.35f * pulse));
    g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre (c));
    g.setColour (Colour (0xffc9ccd1));
    g.drawEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (c), 1.0f);
}

inline void vintagePhaserEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    if (! reduced) bloom (g, c, r * 2.4f, p.accent2, 0.4f);
    g.setColour (p.accent2);
    g.fillEllipse (Rectangle<float> (r * 1.4f, r * 1.4f).withCentre (c));
    g.setColour (Colour (0xffc9ccd1));
    g.drawEllipse (Rectangle<float> (r * 1.8f, r * 1.8f).withCentre (c), 1.0f);
}

inline const Theme& vintagePhaserTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Vintage Phaser";
        x.base = Style::hardware;
        x.pal = { Colour (0xffd4561e), Colour (0xff8a3410), Colour (0xfffff0dc), Colour (0xffff3a2e), Colour (0xd0b2471a), Colour (0xe0802e0e),
                  Colour (0xffffb08a), Colour (0xfffff6ea), Colour (0xff2a1006), Colour (0xff2c2c2c), Colour (0xff0b0b0b), Colour (0xff5a2208),
                  Colour (0xfff4f4f4), Colour (0xff9a3c14) };
        x.canvas = canvasVintagePhaser;
        x.titleEmblem = vintagePhaserEmblem;
        x.drawCap = capPedal;
        x.pointerFrom = 0.3f;
        x.arcGlow = 0.0f;
        x.stageRepaint = true;
        x.paintMotion = vintagePhaserMotion;
        return x;
    }();
    return t;
}

// === Frequency Shifter: a radio dial — a tuning strip with a needle and station marks, bakelite and cream glow =====

inline void canvasFreqShifter (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // the dial window along the top: a warm glowing strip with a scale and a red needle
    const auto dial = Rectangle<float> (a.getX() + a.getWidth() * 0.08f, a.getY() + a.getHeight() * 0.06f, a.getWidth() * 0.84f, 26.0f);
    if (! reduced) softLight (g, dial.getCentre(), dial.getWidth() * 0.55f, p.accent, 0.08f);
    g.setGradientFill (ColourGradient (p.accent.withAlpha (0.10f), 0.0f, dial.getY(), p.accent.withAlpha (0.04f), 0.0f, dial.getBottom(), false));
    g.fillRoundedRectangle (dial, 4.0f);
    g.setColour (p.accent.withAlpha (0.25f));
    for (int i = 0; i <= 40; ++i)
    {
        const auto x = dial.getX() + 8.0f + (dial.getWidth() - 16.0f) * (float) i / 40.0f;
        g.fillRect (Rectangle<float> (x, dial.getBottom() - (i % 5 == 0 ? 10.0f : 5.0f), 1.0f, i % 5 == 0 ? 8.0f : 3.0f));
    }
    g.setColour (p.accent2.withAlpha (0.6f));
    g.fillRect (Rectangle<float> (dial.getX() + dial.getWidth() * 0.62f, dial.getY() + 3.0f, 1.6f, dial.getHeight() - 6.0f));
    // a speaker cloth weave low on the panel
    if (! reduced)
    {
        g.setColour (Colours::black.withAlpha (0.12f));
        for (float x = a.getX(); x < a.getRight(); x += 4.0f)
            g.drawLine (x, a.getBottom() - a.getHeight() * 0.25f, x + 8.0f, a.getBottom(), 0.7f);
    }
    vignette (g, a, 0.45f);
}

inline void freqShifterEmblem (juce::Graphics& g, Point<float> c, float r) // a needle on a scale
{
    const auto& p = pal();
    g.setColour (p.accent);
    for (int i = 0; i < 5; ++i)
        g.fillRect (Rectangle<float> (1.0f, i % 2 == 0 ? r * 0.8f : r * 0.4f).withCentre (c.translated (-r + (float) i * r * 0.5f, r * 0.3f)));
    g.setColour (p.accent2);
    g.fillRect (Rectangle<float> (1.6f, r * 2.0f).withCentre (c.translated (r * 0.2f, 0.0f)));
}

inline const Theme& freqShifterTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Frequency Shifter";
        x.base = Style::hardware;
        x.pal = { Colour (0xff2a1a12), Colour (0xff0d0806), Colour (0xffffe7b0), Colour (0xffff4a3a), Colour (0xd0261810), Colour (0xe40d0806),
                  Colour (0xff8a6a4a), Colour (0xfffff2d8), Colour (0xff080504), Colour (0xff4a2e1e), Colour (0xff1a100a), Colour (0xff24170f),
                  Colour (0xfffff2d8), Colour (0xff261810) };
        x.canvas = canvasFreqShifter;
        x.titleEmblem = freqShifterEmblem;
        x.cap = Cap::bakelite;
        x.pointerFrom = 0.15f;
        x.ticks = 11;
        x.buttTrack = true;
        x.arcGlow = 0.0f;
        x.thumbPointer = true;
        return x;
    }();
    return t;
}

// === Fruity Parametric EQ 2: engineering graph paper under the curve, ink blue ======================================

inline void canvasParamEq (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // graph paper: a fine grid, every 5th line stronger
    for (int i = 0; (float) i * 8.0f < juce::jmax (a.getWidth(), a.getHeight()); ++i)
    {
        g.setColour (p.accent.withAlpha (i % 5 == 0 ? 0.07f : 0.025f));
        if ((float) i * 8.0f < a.getWidth()) g.fillRect (Rectangle<float> (a.getX() + (float) i * 8.0f, a.getY(), 1.0f, a.getHeight()));
        if ((float) i * 8.0f < a.getHeight()) g.fillRect (Rectangle<float> (a.getX(), a.getY() + (float) i * 8.0f, a.getWidth(), 1.0f));
    }
    // a drafting curve: a few bells summed, drawn faintly like a pencil line
    juce::Path curve;
    for (float x = a.getX(); x <= a.getRight(); x += 3.0f)
    {
        const auto u = (x - a.getX()) / a.getWidth();
        const auto y = 0.35f * std::exp (-std::pow ((u - 0.2f) / 0.08f, 2.0f)) - 0.25f * std::exp (-std::pow ((u - 0.55f) / 0.12f, 2.0f))
                       + 0.2f * std::exp (-std::pow ((u - 0.85f) / 0.06f, 2.0f));
        const Point<float> q { x, a.getCentreY() - y * a.getHeight() * 0.6f };
        if (x == a.getX()) curve.startNewSubPath (q);
        else curve.lineTo (q);
    }
    g.setColour (p.accent2.withAlpha (0.14f));
    g.strokePath (curve, stroke (1.6f));
    vignette (g, a, 0.35f);
}

inline void paramEqEmblem (juce::Graphics& g, Point<float> c, float r) // a bell
{
    juce::Path b;
    for (int i = 0; i <= 20; ++i)
    {
        const auto x = -1.0f + 2.0f * (float) i / 20.0f;
        const Point<float> q { c.x + x * r * 1.2f, c.y + r * 0.6f - std::exp (-x * x * 5.0f) * r * 1.3f };
        if (i == 0) b.startNewSubPath (q);
        else b.lineTo (q);
    }
    g.setColour (pal().accent);
    g.strokePath (b, stroke (1.4f));
}

inline const Theme& paramEqTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Parametric EQ 2";
        x.base = Style::glow;
        x.pal = { Colour (0xff0f1b2e), Colour (0xff050a12), Colour (0xff46c8ff), Colour (0xffe8f4ff), Colour (0xc00e1a2c), Colour (0xdc050a12),
                  Colour (0xff4a6a90), Colour (0xffe8f4ff), Colour (0xff040810), Colour (0xff2a3a52), Colour (0xff0e1624), Colour (0xff1a2840),
                  Colour (0xffe8f4ff), Colour (0xff12203a) };
        x.canvas = canvasParamEq;
        x.titleEmblem = paramEqEmblem;
        x.thin = true;
        x.kerning = 0.08f;
        x.cap = Cap::flat;
        x.arcGlow = 0.0f;
        return x;
    }();
    return t;
}

// === Fruity Limiter: a brick wall — a level line pressing up against a wall of bricks ==============================

inline void canvasLimiter (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // the wall across the top
    const auto wall = a.withHeight (a.getHeight() * 0.22f);
    const auto bw = 34.0f, bh = 13.0f;
    for (float y = wall.getY(); y < wall.getBottom(); y += bh)
    {
        const auto row = (int) ((y - wall.getY()) / bh);
        for (float x = wall.getX() - (row % 2 == 0 ? 0.0f : bw * 0.5f); x < wall.getRight(); x += bw)
        {
            const auto brick = Rectangle<float> (x + 1.0f, y + 1.0f, bw - 2.0f, bh - 2.0f);
            g.setGradientFill (ColourGradient (p.accent.withAlpha (0.28f), 0.0f, brick.getY(), p.accent.darker (0.6f).withAlpha (0.22f), 0.0f, brick.getBottom(), false));
            g.fillRect (brick);
        }
    }
    g.setGradientFill (ColourGradient (Colours::transparentBlack, 0.0f, wall.getY(), p.bgBottom.withAlpha (0.4f), 0.0f, wall.getBottom(), false));
    g.fillRect (wall);
    // peaks pushing up to the wall and flattening there
    juce::Path wave;
    juce::Random rnd (2);
    wave.startNewSubPath (a.getX(), a.getBottom());
    for (float x = a.getX(); x <= a.getRight(); x += 4.0f)
        wave.lineTo (x, juce::jmax (wall.getBottom() + 2.0f, a.getBottom() - a.getHeight() * (0.3f + 0.6f * rnd.nextFloat())));
    wave.lineTo (a.getRight(), a.getBottom());
    wave.closeSubPath();
    g.setColour (p.accent2.withAlpha (0.06f));
    g.fillPath (wave);
    vignette (g, a, 0.4f);
}

inline void limiterEmblem (juce::Graphics& g, Point<float> c, float r) // a brick
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.fillRect (Rectangle<float> (r * 2.0f, r * 0.8f).withCentre (c.translated (0.0f, -r * 0.45f)));
    g.fillRect (Rectangle<float> (r * 0.9f, r * 0.8f).withCentre (c.translated (-r * 0.55f, r * 0.5f)));
    g.fillRect (Rectangle<float> (r * 0.9f, r * 0.8f).withCentre (c.translated (r * 0.55f, r * 0.5f)));
}

inline const Theme& limiterTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Limiter";
        x.base = Style::hardware;
        x.pal = { Colour (0xff221c1b), Colour (0xff0b0909), Colour (0xffc0483a), Colour (0xffd8d0c8), Colour (0xc8201a19), Colour (0xe00b0909),
                  Colour (0xff7a5a52), Colour (0xfff4ece6), Colour (0xff070606), Colour (0xff2c2c2c), Colour (0xff0e0e0e), Colour (0xff241e1d),
                  Colour (0xfff4ece6), Colour (0xff221c1b) };
        x.canvas = canvasLimiter;
        x.titleEmblem = limiterEmblem;
        x.cap = Cap::rubber;
        x.arcGlow = 0.22f;
        return x;
    }();
    return t;
}

// === Fruity Compressor: a spring press — a coil being squeezed, steel and amber ===================================

inline void coil (juce::Graphics& g, Point<float> top, float w, float h, int turns, Colour col, float width)
{
    juce::Path s;
    s.startNewSubPath (top);
    for (int i = 0; i <= turns * 16; ++i)
    {
        const auto u = (float) i / (float) (turns * 16);
        s.lineTo (top.x + w * 0.5f * std::sin (u * (float) turns * juce::MathConstants<float>::twoPi), top.y + u * h);
    }
    g.setColour (col);
    g.strokePath (s, stroke (width));
}

inline void canvasCompressor (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // three springs, more and more compressed, along the right
    for (int i = 0; i < 3; ++i)
    {
        const auto h = a.getHeight() * (0.7f - 0.2f * (float) i);
        const auto x = a.getRight() - a.getWidth() * (0.32f - 0.11f * (float) i);
        const auto top = Point<float> (x, a.getBottom() - h - 10.0f);
        g.setColour (p.accent2.withAlpha (0.12f)); // the press plate
        g.fillRect (Rectangle<float> (40.0f, 4.0f).withCentre (top.translated (0.0f, -3.0f)));
        coil (g, top, 30.0f, h, 9, p.accent2.withAlpha (0.14f), 1.5f);
    }
    softLight (g, { a.getX() + a.getWidth() * 0.2f, a.getY() }, a.getWidth() * 0.5f, p.accent, 0.08f);
    vignette (g, a, 0.4f);
}

inline void compressorEmblem (juce::Graphics& g, Point<float> c, float r) { coil (g, c.translated (0.0f, -r), r * 1.6f, r * 2.0f, 4, pal().accent, 1.2f); }

inline const Theme& compressorTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Compressor";
        x.base = Style::hardware;
        x.pal = { Colour (0xff1e2226), Colour (0xff0a0c0e), Colour (0xffffb84d), Colour (0xffc8d0d8), Colour (0xc81c2024), Colour (0xe00a0c0e),
                  Colour (0xff6a7480), Colour (0xfff2f0ea), Colour (0xff060708), Colour (0xffc9d2dc), Colour (0xff59626d), Colour (0xff1e2328),
                  Colour (0xff15181b), Colour (0xff1c2024) };
        x.canvas = canvasCompressor;
        x.titleEmblem = compressorEmblem;
        x.drawCap = capBrushed;
        x.arcGlow = 0.2f;
        return x;
    }();
    return t;
}

// === Fruity Multiband Compressor: three strata, like a geological cross-section ===================================

inline void canvasMultiband (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const Colour bands[] { Colour (0xff4fa8ff), Colour (0xff4fe08a), Colour (0xffff6a5a) }; // high on top, low at the bottom
    juce::Random rnd (3);
    for (int b = 0; b < 3; ++b)
    {
        juce::Path layer;
        const auto y0 = a.getY() + a.getHeight() * (0.15f + 0.28f * (float) b);
        layer.startNewSubPath (a.getX(), a.getBottom());
        for (float x = a.getX(); x <= a.getRight(); x += 12.0f)
            layer.lineTo (x, y0 + 10.0f * std::sin ((x - a.getX()) * 0.012f + (float) b * 1.7f) + rnd.nextFloat() * 3.0f);
        layer.lineTo (a.getRight(), a.getBottom());
        layer.closeSubPath();
        g.setGradientFill (ColourGradient (bands[b].withAlpha (0.11f), 0.0f, y0, bands[b].withAlpha (0.03f), 0.0f, a.getBottom(), false));
        g.fillPath (layer);
    }
    vignette (g, a, 0.4f);
}

inline void multibandEmblem (juce::Graphics& g, Point<float> c, float r) // three bars
{
    const Colour bands[] { Colour (0xff4fa8ff), Colour (0xff4fe08a), Colour (0xffff6a5a) };
    for (int b = 0; b < 3; ++b)
    {
        g.setColour (bands[b]);
        g.fillRect (Rectangle<float> (r * 2.0f, r * 0.42f).withCentre (c.translated (0.0f, ((float) b - 1.0f) * r * 0.62f)));
    }
}

inline const Theme& multibandTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Multiband Compressor";
        x.base = Style::glow;
        x.pal = { Colour (0xff181c22), Colour (0xff07090c), Colour (0xff4fe08a), Colour (0xff4fa8ff), Colour (0xc0161a20), Colour (0xdc07090c),
                  Colour (0xff5a6a7a), Colour (0xffeef4f8), Colour (0xff05070a), Colour (0xff2a3038), Colour (0xff0e1116), Colour (0xff1c2128),
                  Colour (0xffffffff), Colour (0xff181d24) };
        x.canvas = canvasMultiband;
        x.titleEmblem = multibandEmblem;
        x.twoTone = true;
        x.cap = Cap::flat;
        return x;
    }();
    return t;
}

// === Maximus: maximum — pressure gauges, chrome and hot yellow ====================================================

inline void gauge (juce::Graphics& g, Point<float> c, float r, float value, Colour col, float alpha)
{
    g.setColour (col.withAlpha (0.08f * alpha));
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setColour (col.withAlpha (0.3f * alpha));
    g.drawEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.2f);
    for (int i = 0; i <= 10; ++i)
    {
        const auto ang = arcStart + arcRange * (float) i / 10.0f;
        const Point<float> d { std::sin (ang), -std::cos (ang) };
        g.setColour ((i >= 8 ? Colour (0xffff3a2e) : col).withAlpha (0.35f * alpha));
        g.drawLine ({ c + d * r * 0.78f, c + d * r * 0.92f }, 1.2f);
    }
    const auto ang = arcStart + arcRange * value;
    g.setColour (col.withAlpha (0.5f * alpha));
    g.drawLine ({ c, c + Point<float> (std::sin (ang), -std::cos (ang)) * r * 0.75f }, 1.6f);
}

inline void canvasMaximus (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto r = juce::jmin (a.getHeight() * 0.28f, a.getWidth() * 0.12f);
    gauge (g, { a.getX() + a.getWidth() * 0.18f, a.getY() + a.getHeight() * 0.4f }, r, 0.92f, p.accent, 1.0f);
    gauge (g, { a.getX() + a.getWidth() * 0.5f, a.getY() + a.getHeight() * 0.65f }, r * 0.8f, 0.7f, p.accent, 0.8f);
    gauge (g, { a.getX() + a.getWidth() * 0.82f, a.getY() + a.getHeight() * 0.38f }, r * 0.9f, 0.85f, p.accent, 0.9f);
    softLight (g, { a.getCentreX(), a.getY() }, a.getWidth() * 0.5f, p.accent, 0.06f);
    vignette (g, a, 0.4f);
}

inline void maximusEmblem (juce::Graphics& g, Point<float> c, float r) { gauge (g, c, r * 1.1f, 0.9f, pal().accent, 3.0f); }

inline const Theme& maximusTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Maximus";
        x.base = Style::hardware;
        x.pal = { Colour (0xff1a1a1c), Colour (0xff070708), Colour (0xffffd43b), Colour (0xffe0e4e8), Colour (0xc818181a), Colour (0xe0070708),
                  Colour (0xff7a7458), Colour (0xfffff8dc), Colour (0xff050505), Colour (0xffe0e4e8), Colour (0xff3a3e44), Colour (0xff222224),
                  Colour (0xff111111), Colour (0xff1c1c1e) };
        x.canvas = canvasMaximus;
        x.titleEmblem = maximusEmblem;
        x.drawCap = capChrome;
        x.pointer = Pointer::glowLine;
        x.arcGlow = 0.3f;
        return x;
    }();
    return t;
}

// === Transient Processor: a strike — the flash of an attack and a long soft tail ==================================

inline void canvasTransient (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto hit = Point<float> (a.getX() + a.getWidth() * 0.18f, a.getCentreY());
    if (! reduced) softLight (g, hit, a.getHeight() * 0.7f, p.accent, 0.2f);
    // the envelope: a sharp spike and an exponential tail
    juce::Path env;
    env.startNewSubPath (a.getX(), a.getBottom() - 6.0f);
    env.lineTo (hit.x - 3.0f, a.getBottom() - 6.0f);
    env.lineTo (hit.x, a.getY() + a.getHeight() * 0.1f);
    for (float x = hit.x; x <= a.getRight(); x += 4.0f)
        env.lineTo (x, a.getBottom() - 6.0f - (a.getHeight() * 0.85f) * std::exp (-(x - hit.x) / (a.getWidth() * 0.18f)));
    g.setColour (p.accent2.withAlpha (0.2f));
    g.strokePath (env, stroke (1.6f));
    // flash rays at the hit
    for (int i = 0; i < 10; ++i)
    {
        const auto ang = juce::MathConstants<float>::twoPi * (float) i / 10.0f;
        g.setColour (p.accent.withAlpha (0.12f));
        g.drawLine ({ hit + Point<float> (std::cos (ang), std::sin (ang)) * 8.0f, hit + Point<float> (std::cos (ang), std::sin (ang)) * (20.0f + (float) (i % 3) * 10.0f) }, 1.2f);
    }
    vignette (g, a, 0.4f);
}

inline void transientEmblem (juce::Graphics& g, Point<float> c, float r) // a spike and a tail
{
    juce::Path e;
    e.startNewSubPath (c.x - r * 1.2f, c.y + r * 0.8f);
    e.lineTo (c.x - r * 0.7f, c.y - r);
    e.quadraticTo (c.x - r * 0.4f, c.y + r * 0.6f, c.x + r * 1.2f, c.y + r * 0.8f);
    g.setColour (pal().accent);
    g.strokePath (e, stroke (1.4f));
}

inline void capDrumHead (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setColour (p.accent2.darker (0.6f));
    g.fillEllipse (cap);
    g.setGradientFill (ColourGradient (Colour (0xfff4f2ec).brighter (hover * 0.1f), c.x - capR * 0.3f, c.y - capR * 0.5f, Colour (0xffb8b4a8), c.x + capR * 0.5f, c.y + capR, true));
    g.fillEllipse (cap.reduced (capR * 0.12f));
    if (fx)
    {
        g.setColour (Colours::black.withAlpha (0.15f));
        g.drawEllipse (cap.reduced (capR * 0.5f), 0.8f);
    }
}

inline const Theme& transientTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Transient Processor";
        x.base = Style::vivid;
        x.pal = { Colour (0xff1a1630), Colour (0xff07060f), Colour (0xffffffff), Colour (0xff8a7aff), Colour (0xc018142c), Colour (0xdc07060f),
                  Colour (0xff6a5a9a), Colour (0xfff4f0ff), Colour (0xff05040a), Colour (0xfff4f2ec), Colour (0xffb8b4a8), Colour (0xff221c3a),
                  Colour (0xff1a1630), Colour (0xff1c1834) };
        x.canvas = canvasTransient;
        x.titleEmblem = transientEmblem;
        x.drawCap = capDrumHead;
        x.twoTone = true;
        x.arcGlow = 0.3f;
        return x;
    }();
    return t;
}

// === Fruity Stereo Shaper: the stereo field — a goniometer diamond with a Lissajous haze ==========================

inline void canvasStereoShaper (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto c = Point<float> (a.getX() + a.getWidth() * 0.72f, a.getCentreY());
    const auto r = juce::jmin (a.getWidth(), a.getHeight()) * 0.42f;
    juce::Path d;
    d.addQuadrilateral (c.x, c.y - r, c.x + r, c.y, c.x, c.y + r, c.x - r, c.y);
    g.setColour (p.accent.withAlpha (0.14f));
    g.strokePath (d, juce::PathStrokeType (1.2f));
    g.drawLine (c.x, c.y - r, c.x, c.y + r, 0.8f);
    g.drawLine (c.x - r, c.y, c.x + r, c.y, 0.8f);
    // the haze: a few Lissajous loops of a slightly decorrelated stereo signal
    juce::Path l;
    lissajous (l, c, r * 0.35f, r * 0.75f, 3.0f, 2.0f, 0.7f, 240);
    l.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi * 0.25f, c.x, c.y));
    g.setColour (p.accent2.withAlpha (0.16f));
    g.strokePath (l, juce::PathStrokeType (1.0f));
    vignette (g, a, 0.4f);
}

inline void stereoShaperEmblem (juce::Graphics& g, Point<float> c, float r) // a diamond
{
    juce::Path d;
    d.addQuadrilateral (c.x, c.y - r, c.x + r, c.y, c.x, c.y + r, c.x - r, c.y);
    g.setColour (pal().accent);
    g.strokePath (d, juce::PathStrokeType (1.4f));
}

inline const Theme& stereoShaperTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Stereo Shaper";
        x.base = Style::neon;
        x.pal = { Colour (0xff0f2216), Colour (0xff040a06), Colour (0xffb6ff5c), Colour (0xff5cffd0), Colour (0xc00e2014), Colour (0xdc040a06),
                  Colour (0xff4a8a5a), Colour (0xffeeffe0), Colour (0xff030704), Colour (0xff2a3a2e), Colour (0xff0a120c), Colour (0xff152a1b),
                  Colour (0xffeeffe0), Colour (0xff10241a) };
        x.canvas = canvasStereoShaper;
        x.titleEmblem = stereoShaperEmblem;
        x.twoTone = true;
        x.cap = Cap::orb;
        return x;
    }();
    return t;
}

// === Fruity Stereo Enhancer: wide — two speakers pushing a wide arc of sound outwards ==============================

inline void speaker (juce::Graphics& g, Point<float> c, float r, Colour col, float alpha)
{
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (col.withAlpha (alpha * (0.12f + 0.05f * (float) i)));
        g.drawEllipse (Rectangle<float> (r * 2.0f * (1.0f - 0.22f * (float) i), r * 2.0f * (1.0f - 0.22f * (float) i)).withCentre (c), 1.2f);
    }
    g.setColour (col.withAlpha (alpha * 0.3f));
    g.fillEllipse (Rectangle<float> (r * 0.3f, r * 0.3f).withCentre (c));
}

inline void canvasStereoEnhancer (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto r = juce::jmin (a.getHeight() * 0.18f, 40.0f);
    const auto l = Point<float> (a.getX() + a.getWidth() * 0.12f, a.getCentreY()), rr = Point<float> (a.getRight() - a.getWidth() * 0.12f, a.getCentreY());
    speaker (g, l, r, p.accent, 1.0f);
    speaker (g, rr, r, p.accent, 1.0f);
    // wavefronts spreading outwards from each speaker
    for (int i = 1; i < 7; ++i)
    {
        const auto w = r + (float) i * 22.0f;
        juce::Path left, right;
        left.addCentredArc (l.x, l.y, w, w * 1.2f, 0.0f, juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 1.8f, true);
        right.addCentredArc (rr.x, rr.y, w, w * 1.2f, 0.0f, juce::MathConstants<float>::pi * 0.2f, juce::MathConstants<float>::pi * 0.8f, true);
        g.setColour (p.accent2.withAlpha (0.09f - 0.01f * (float) i));
        g.strokePath (left, juce::PathStrokeType (1.4f));
        g.strokePath (right, juce::PathStrokeType (1.4f));
    }
    vignette (g, a, 0.4f);
}

inline void stereoEnhancerEmblem (juce::Graphics& g, Point<float> c, float r) { speaker (g, c, r * 1.1f, pal().accent, 4.0f); }

inline const Theme& stereoEnhancerTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Stereo Enhancer";
        x.base = Style::neon;
        x.pal = { Colour (0xff102036), Colour (0xff050a12), Colour (0xff6ac8ff), Colour (0xff9affe0), Colour (0xc00f1e34), Colour (0xdc050a12),
                  Colour (0xff4a6a90), Colour (0xffeaf6ff), Colour (0xff040810), Colour (0xff2a3a52), Colour (0xff0e1624), Colour (0xff18283e),
                  Colour (0xffffffff), Colour (0xff122238) };
        x.canvas = canvasStereoEnhancer;
        x.titleEmblem = stereoEnhancerEmblem;
        x.drawCap = capDish;
        x.twoTone = true;
        return x;
    }();
    return t;
}

// === Spreader: a peacock fan of rays spreading from the bottom centre ==============================================

inline void canvasSpreader (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto o = Point<float> (a.getCentreX(), a.getBottom() + 10.0f);
    for (int i = 0; i < 17; ++i)
    {
        const auto ang = juce::MathConstants<float>::pi * (0.1f + 0.8f * (float) i / 16.0f);
        const Point<float> d { -std::cos (ang), -std::sin (ang) };
        const auto len = a.getHeight() * 0.95f;
        g.setColour (p.accent.interpolatedWith (p.accent2, (float) i / 16.0f).withAlpha (0.08f));
        g.drawLine ({ o, o + d * len }, 2.0f);
        // the "eye" at each ray's end
        const auto e = o + d * len * 0.9f;
        g.setColour (p.accent2.withAlpha (0.12f));
        g.fillEllipse (Rectangle<float> (10.0f, 14.0f).withCentre (e));
        g.setColour (p.accent.withAlpha (0.2f));
        g.fillEllipse (Rectangle<float> (4.0f, 6.0f).withCentre (e));
    }
    vignette (g, a, 0.4f);
}

inline void spreaderEmblem (juce::Graphics& g, Point<float> c, float r) // a fan
{
    const auto& p = pal();
    for (int i = 0; i < 5; ++i)
    {
        const auto ang = juce::MathConstants<float>::pi * (0.15f + 0.7f * (float) i / 4.0f);
        g.setColour (p.accent.interpolatedWith (p.accent2, (float) i / 4.0f));
        g.drawLine ({ c.translated (0.0f, r * 0.8f), c.translated (-std::cos (ang) * r * 1.3f, r * 0.8f - std::sin (ang) * r * 1.3f) }, 1.4f);
    }
}

inline const Theme& spreaderTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Spreader";
        x.base = Style::neon;
        x.pal = { Colour (0xff0b2420), Colour (0xff030a09), Colour (0xff2ee0a0), Colour (0xff3ab0ff), Colour (0xc00a221e), Colour (0xdc030a09),
                  Colour (0xff3a8a7a), Colour (0xffe6fff6), Colour (0xff020706), Colour (0xff1e3a34), Colour (0xff081412), Colour (0xff12302a),
                  Colour (0xffffffff), Colour (0xff0d2622) };
        x.canvas = canvasSpreader;
        x.titleEmblem = spreaderEmblem;
        x.twoTone = true;
        x.cap = Cap::orb;
        return x;
    }();
    return t;
}

// === Gross Beat: a turntable — a vinyl record and a tonearm, time-warp swirls, magenta and cyan ==================

inline void vinyl (juce::Graphics& g, Point<float> c, float r, float angle, Colour label, Colour shine, float alpha)
{
    g.setColour (Colour (0xff050507).withAlpha (alpha));
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    for (float k = r * 0.38f; k < r * 0.98f; k += 2.4f) // grooves
    {
        g.setColour (Colours::white.withAlpha (0.03f * alpha));
        g.drawEllipse (Rectangle<float> (k * 2.0f, k * 2.0f).withCentre (c), 0.6f);
    }
    // two sheen wedges that turn with the record
    for (int i = 0; i < 2; ++i)
    {
        juce::Path w;
        const auto a0 = angle + (float) i * juce::MathConstants<float>::pi;
        w.addPieSegment (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), a0, a0 + 0.35f, 0.38f);
        g.setColour (shine.withAlpha (0.10f * alpha));
        g.fillPath (w);
    }
    g.setColour (label.withAlpha (0.8f * alpha)); // our own label: a plain disc with a ring
    g.fillEllipse (Rectangle<float> (r * 0.7f, r * 0.7f).withCentre (c));
    g.setColour (Colours::white.withAlpha (0.4f * alpha));
    g.drawEllipse (Rectangle<float> (r * 0.5f, r * 0.5f).withCentre (c), 0.8f);
    g.setColour (Colours::black.withAlpha (alpha));
    g.fillEllipse (Rectangle<float> (r * 0.06f + 1.0f, r * 0.06f + 1.0f).withCentre (c));
}

inline void canvasGrossBeat (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto r = juce::jmin (a.getHeight() * 0.42f, a.getWidth() * 0.22f);
    const auto c = Point<float> (a.getRight() - r * 1.25f, a.getY() + r * 1.1f);
    if (! reduced) softLight (g, c, r * 1.6f, p.accent, 0.12f);
    vinyl (g, c, r, 0.4f, p.accent, Colours::white, 0.75f);
    // the tonearm
    const auto pivot = Point<float> (c.x + r * 1.05f, c.y - r * 0.85f);
    g.setColour (p.accent2.withAlpha (0.35f));
    g.drawLine ({ pivot, c.translated (r * 0.35f, r * 0.45f) }, 3.0f);
    g.fillEllipse (Rectangle<float> (12.0f, 12.0f).withCentre (pivot));
    // time-warp swirls on the left: stretched arcs
    for (int i = 0; i < 6; ++i)
    {
        juce::Path s;
        const auto rr = r * (0.6f + 0.25f * (float) i);
        s.addCentredArc (a.getX() + a.getWidth() * 0.15f, a.getCentreY(), rr * 1.6f, rr * 0.6f, -0.3f, 0.5f, 2.4f, true);
        g.setColour (p.accent2.withAlpha (0.07f));
        g.strokePath (s, juce::PathStrokeType (1.6f));
    }
    vignette (g, a, 0.4f);
}

inline void grossBeatEmblem (juce::Graphics& g, Point<float> c, float r) { vinyl (g, c, r * 1.2f, 0.4f, pal().accent, Colours::white, 1.0f); }

inline void grossBeatMotion (juce::Graphics& g, const Motion& m) // a small record turning in the stage strip
{
    const auto s = m.stage;
    const auto r = juce::jmin (s.getHeight() * 0.48f, 18.0f);
    if (r < 5.0f || s.getWidth() < r * 2.5f) return;
    vinyl (g, { s.getRight() - r - 4.0f, s.getCentreY() }, r, m.t * 3.5f, m.p.accent, Colours::white, 1.0f);
}

inline const Theme& grossBeatTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Gross Beat";
        x.base = Style::vivid;
        x.pal = { Colour (0xff1c0f22), Colour (0xff07040a), Colour (0xffff2f92), Colour (0xff2fe0ff), Colour (0x9c1a0e20), Colour (0xb807040a),
                  Colour (0xff8a3a7a), Colour (0xffffe6f4), Colour (0xff050307), Colour (0xff2a1a2e), Colour (0xff0b070e), Colour (0xff241628),
                  Colour (0xffffffff), Colour (0xff1e1024) };
        x.canvas = canvasGrossBeat;
        x.titleEmblem = grossBeatEmblem;
        x.cap = Cap::rubber;
        x.twoTone = true;
        x.pointer = Pointer::glowLine;
        x.arcGlow = 0.3f;
        x.stageRepaint = true;
        x.paintMotion = grossBeatMotion;
        return x;
    }();
    return t;
}

// === Vocodex / Fruity Vocoder: voice bands — a bank of band-pass bars like a talking spectrum, robot-panel steel ===

inline void canvasVoiceBands (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto n = 28;
    const auto bw = a.getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const auto u = (float) i / (float) n;
        // a formant-shaped envelope: two humps
        const auto h = 0.15f + 0.55f * std::exp (-std::pow ((u - 0.25f) / 0.1f, 2.0f)) + 0.4f * std::exp (-std::pow ((u - 0.6f) / 0.12f, 2.0f));
        const auto bar = Rectangle<float> (a.getX() + (float) i * bw + 2.0f, a.getBottom() - a.getHeight() * h * 0.8f, bw - 4.0f, a.getHeight() * h * 0.8f);
        g.setGradientFill (ColourGradient (p.accent.withAlpha (0.16f), 0.0f, bar.getY(), p.accent.withAlpha (0.02f), 0.0f, bar.getBottom(), false));
        g.fillRect (bar);
        for (float y = bar.getBottom() - 4.0f; y > bar.getY(); y -= 5.0f) // LED segments
        {
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillRect (Rectangle<float> (bar.getX(), y, bar.getWidth(), 1.0f));
        }
    }
    vignette (g, a, 0.4f);
}

inline void voiceBandsEmblem (juce::Graphics& g, Point<float> c, float r) // a grille: rows of dots
{
    g.setColour (pal().accent);
    for (int y = -1; y <= 1; ++y)
        for (int x = -2; x <= 2; ++x)
            g.fillEllipse (Rectangle<float> (r * 0.3f, r * 0.3f).withCentre (c.translated ((float) x * r * 0.45f, (float) y * r * 0.5f)));
}

inline const Theme& vocodexTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Vocodex";
        x.base = Style::neon;
        x.pal = { Colour (0xff141a16), Colour (0xff050806), Colour (0xffa8ff3e), Colour (0xffc8d0d8), Colour (0x9c121a14), Colour (0xb8050806),
                  Colour (0xff5a7a5a), Colour (0xffeeffe0), Colour (0xff030504), Colour (0xffc9d2dc), Colour (0xff59626d), Colour (0xff1a241c),
                  Colour (0xff111611), Colour (0xff161e18) };
        x.canvas = canvasVoiceBands;
        x.titleEmblem = voiceBandsEmblem;
        x.drawCap = capBrushed;
        x.pointer = Pointer::glowLine;
        return x;
    }();
    return t;
}

inline const Theme& vocoderTheme()
{
    static const Theme t = []
    {
        Theme x = vocodexTheme();
        x.name = "Fruity Vocoder";
        x.base = Style::hardware;
        x.pal.accent = Colour (0xff3ee0ff);
        x.pal.accent2 = Colour (0xffd0d8e0);
        x.pal.rim = Colour (0xff4a6a7a);
        return x;
    }();
    return t;
}

// === Pitcher (pitch correction): a tuning fork with resonance rings, gold on midnight =============================

inline void fork (juce::Graphics& g, Point<float> c, float h, Colour col, float width)
{
    juce::Path f;
    const auto w = h * 0.18f;
    f.startNewSubPath (c.x - w, c.y - h * 0.5f);
    f.lineTo (c.x - w, c.y);
    f.quadraticTo (c.x - w, c.y + w * 1.4f, c.x, c.y + w * 1.4f);
    f.quadraticTo (c.x + w, c.y + w * 1.4f, c.x + w, c.y);
    f.lineTo (c.x + w, c.y - h * 0.5f);
    f.startNewSubPath (c.x, c.y + w * 1.4f);
    f.lineTo (c.x, c.y + h * 0.5f);
    g.setColour (col);
    g.strokePath (f, stroke (width));
}

inline void canvasPitcher (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto c = Point<float> (a.getRight() - a.getWidth() * 0.18f, a.getCentreY());
    const auto h = a.getHeight() * 0.7f;
    for (int i = 1; i < 9; ++i) // resonance rings around the tines
    {
        g.setColour (p.accent.withAlpha (0.07f - 0.006f * (float) i));
        g.drawEllipse (Rectangle<float> ((float) i * 26.0f, (float) i * 34.0f).withCentre (c.translated (0.0f, -h * 0.25f)), 1.2f);
    }
    fork (g, c, h, p.accent.withAlpha (0.3f), 4.0f);
    vignette (g, a, 0.4f);
}

inline void pitcherEmblem (juce::Graphics& g, Point<float> c, float r) { fork (g, c, r * 2.2f, pal().accent, 1.3f); }

inline const Theme& pitcherTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Pitcher";
        x.base = Style::vivid;
        x.pal = { Colour (0xff141a30), Colour (0xff05070f), Colour (0xffffcf5a), Colour (0xff8ab4ff), Colour (0x9c121a2e), Colour (0xb805070f),
                  Colour (0xff6a6a8a), Colour (0xfffff6dc), Colour (0xff04060c), Colour (0xff2a3048), Colour (0xff0e1220), Colour (0xff1c2238),
                  Colour (0xffffffff), Colour (0xff161c32) };
        x.canvas = canvasPitcher;
        x.titleEmblem = pitcherEmblem;
        x.drawCap = capBrass;
        x.arcGlow = 0.25f;
        return x;
    }();
    return t;
}

// === LYNX Tune (vocal tuning role): a pitch lattice with a curve snapping to its lines, warm rose ================

inline void canvasLynxTune (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto rowH = 16.0f;
    for (float y = a.getY(); y < a.getBottom(); y += rowH) // the semitone rows, black keys darker
    {
        const auto k = ((int) ((y - a.getY()) / rowH)) % 12;
        const auto black = k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
        g.setColour ((black ? Colours::black : Colours::white).withAlpha (black ? 0.12f : 0.015f));
        g.fillRect (Rectangle<float> (a.getX(), y, a.getWidth(), rowH));
        g.setColour (p.accent.withAlpha (0.04f));
        g.fillRect (Rectangle<float> (a.getX(), y, a.getWidth(), 1.0f));
    }
    // the raw pitch (wobbly, faint) and the corrected one (steps with smooth glides)
    juce::Path raw, tuned;
    const float notes[] { 0.55f, 0.45f, 0.62f, 0.38f, 0.5f };
    for (float x = a.getX(); x <= a.getRight(); x += 3.0f)
    {
        const auto u = (x - a.getX()) / a.getWidth();
        const auto idx = juce::jmin (4, (int) (u * 5.0f));
        const auto target = notes[idx];
        const auto wob = 0.025f * std::sin (u * 60.0f) + 0.012f * std::sin (u * 23.0f);
        const auto frac = u * 5.0f - (float) idx;
        const auto prev = idx > 0 ? notes[idx - 1] : target;
        const auto glide = frac < 0.12f ? prev + (target - prev) * (frac / 0.12f) : target;
        const Point<float> qr { x, a.getY() + a.getHeight() * (target + wob) }, qt { x, a.getY() + a.getHeight() * glide };
        if (x == a.getX()) { raw.startNewSubPath (qr); tuned.startNewSubPath (qt); }
        else { raw.lineTo (qr); tuned.lineTo (qt); }
    }
    g.setColour (p.accent2.withAlpha (0.12f));
    g.strokePath (raw, juce::PathStrokeType (1.0f));
    g.setColour (p.accent.withAlpha (0.22f));
    g.strokePath (tuned, stroke (2.0f));
    vignette (g, a, 0.4f);
}

inline void lynxTuneEmblem (juce::Graphics& g, Point<float> c, float r) // a quantised step
{
    juce::Path s;
    s.startNewSubPath (c.x - r * 1.2f, c.y + r * 0.6f);
    s.lineTo (c.x - r * 0.3f, c.y + r * 0.6f);
    s.lineTo (c.x - r * 0.1f, c.y - r * 0.6f);
    s.lineTo (c.x + r * 1.2f, c.y - r * 0.6f);
    g.setColour (pal().accent);
    g.strokePath (s, stroke (1.5f));
}

inline const Theme& lynxTuneTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "LYNX Tune";
        x.base = Style::vivid;
        x.pal = { Colour (0xff221a20), Colour (0xff0a0709), Colour (0xffff5f87), Colour (0xffffc0d0), Colour (0x9c201820), Colour (0xb80a0709),
                  Colour (0xff8a5a6a), Colour (0xfffff0f4), Colour (0xff070506), Colour (0xff3a2c34), Colour (0xff141014), Colour (0xff2a2026),
                  Colour (0xffffffff), Colour (0xff221a20) };
        x.canvas = canvasLynxTune;
        x.titleEmblem = lynxTuneEmblem;
        x.drawCap = capPebble;
        x.twoTone = true;
        x.arcGlow = 0.3f;
        return x;
    }();
    return t;
}

// === Voice Shift: a morphing formant shape, violet to coral ========================================================

inline void canvasVoiceShift (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    for (int k = 0; k < 5; ++k) // formant envelopes sliding from low to high
    {
        juce::Path f;
        const auto shift = 0.08f * (float) k;
        for (float x = a.getX(); x <= a.getRight(); x += 3.0f)
        {
            const auto u = (x - a.getX()) / a.getWidth();
            const auto y = std::exp (-std::pow ((u - 0.2f - shift) / 0.07f, 2.0f)) * 0.7f + std::exp (-std::pow ((u - 0.5f - shift) / 0.1f, 2.0f)) * 0.45f
                           + std::exp (-std::pow ((u - 0.75f - shift) / 0.08f, 2.0f)) * 0.3f;
            const Point<float> q { x, a.getBottom() - 10.0f - y * a.getHeight() * 0.7f };
            if (x == a.getX()) f.startNewSubPath (q);
            else f.lineTo (q);
        }
        g.setColour (p.accent.interpolatedWith (p.accent2, (float) k / 4.0f).withAlpha (0.13f));
        g.strokePath (f, stroke (1.6f));
    }
    vignette (g, a, 0.4f);
}

inline void voiceShiftEmblem (juce::Graphics& g, Point<float> c, float r) // a formant hump
{
    juce::Path f;
    f.startNewSubPath (c.x - r * 1.2f, c.y + r * 0.7f);
    f.cubicTo (c.x - r * 0.6f, c.y + r * 0.7f, c.x - r * 0.6f, c.y - r, c.x, c.y - r);
    f.cubicTo (c.x + r * 0.5f, c.y - r, c.x + r * 0.3f, c.y + r * 0.2f, c.x + r * 1.2f, c.y + r * 0.7f);
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.accent, c.x - r, c.y, p.accent2, c.x + r, c.y, false));
    g.strokePath (f, stroke (1.5f));
}

inline const Theme& voiceShiftTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Voice Shift";
        x.base = Style::vivid;
        x.pal = { Colour (0xff1e1430), Colour (0xff09060f), Colour (0xffb070ff), Colour (0xffff8a6a), Colour (0x9c1c122c), Colour (0xb809060f),
                  Colour (0xff7a5a9a), Colour (0xfff6eaff), Colour (0xff06040a), Colour (0xff3a2c4c), Colour (0xff17121f), Colour (0xff261a38),
                  Colour (0xfff6eaff), Colour (0xff1e1430) };
        x.canvas = canvasVoiceShift;
        x.titleEmblem = voiceShiftEmblem;
        x.drawCap = capVelvet;
        x.twoTone = true;
        return x;
    }();
    return t;
}

// === Quell Denoiser: calm water — a still lake with a single ring, grain fading away ===============================

inline void canvasQuell (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    if (! reduced) // the noise: fine grain on the left, fading out to the right
    {
        juce::Random rnd (17);
        for (int i = 0; i < (int) (a.getWidth() * a.getHeight() / 60.0f); ++i)
        {
            const auto u = rnd.nextFloat();
            if (rnd.nextFloat() > (1.0f - u) * (1.0f - u)) continue;
            g.setColour (Colours::white.withAlpha (0.05f));
            g.fillRect (Rectangle<float> (a.getX() + u * a.getWidth(), a.getY() + rnd.nextFloat() * a.getHeight(), 1.0f, 1.0f));
        }
    }
    const auto c = Point<float> (a.getX() + a.getWidth() * 0.68f, a.getY() + a.getHeight() * 0.62f);
    for (int i = 1; i < 6; ++i) // one ring spreading on still water
    {
        g.setColour (p.accent.withAlpha (0.12f - 0.018f * (float) i));
        g.drawEllipse (Rectangle<float> ((float) i * 40.0f, (float) i * 12.0f).withCentre (c), 1.2f);
    }
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.04f), 0.0f, c.y - 2.0f, Colours::transparentWhite, 0.0f, a.getBottom(), false));
    g.fillRect (a.withTop (c.y - 2.0f)); // the water's sheen
    vignette (g, a, 0.4f);
}

inline void quellEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.drawEllipse (Rectangle<float> (r * 2.2f, r * 0.8f).withCentre (c), 1.2f);
    g.fillEllipse (Rectangle<float> (r * 0.4f, r * 0.4f).withCentre (c));
}

inline const Theme& quellTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Quell Denoiser";
        x.base = Style::glow;
        x.pal = { Colour (0xff0e2228), Colour (0xff04090b), Colour (0xff5ad8d0), Colour (0xffcaf4f0), Colour (0x9c0e2026), Colour (0xb804090b),
                  Colour (0xff4a7a7a), Colour (0xffe6fbf8), Colour (0xff03080a), Colour (0xffbcc8c4), Colour (0xff6a7874), Colour (0xff16292d),
                  Colour (0xff10302c), Colour (0xff10242a) };
        x.canvas = canvasQuell;
        x.titleEmblem = quellEmblem;
        x.drawCap = capStone;
        x.arcGlow = 0.15f;
        return x;
    }();
    return t;
}

// === Pitch Shifter (FL's): stairs — an ascending staircase of semitone steps, cyan on indigo =====================

inline void canvasPitchShifter (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto n = 12;
    const auto sw = a.getWidth() * 0.6f / (float) n, sh = a.getHeight() * 0.7f / (float) n;
    juce::Path stairs;
    stairs.startNewSubPath (a.getX() + a.getWidth() * 0.35f, a.getBottom());
    for (int i = 0; i < n; ++i)
    {
        const auto x = a.getX() + a.getWidth() * 0.35f + (float) i * sw;
        const auto y = a.getBottom() - (float) (i + 1) * sh;
        stairs.lineTo (x, y);
        stairs.lineTo (x + sw, y);
    }
    stairs.lineTo (a.getRight(), a.getBottom());
    stairs.closeSubPath();
    g.setGradientFill (ColourGradient (p.accent.withAlpha (0.10f), a.getRight(), a.getY(), p.accent.withAlpha (0.02f), a.getX(), a.getBottom(), false));
    g.fillPath (stairs);
    g.setColour (p.accent.withAlpha (0.2f));
    g.strokePath (stairs, juce::PathStrokeType (1.0f));
    vignette (g, a, 0.4f);
}

inline void pitchShifterEmblem (juce::Graphics& g, Point<float> c, float r)
{
    juce::Path s;
    s.startNewSubPath (c.x - r * 1.2f, c.y + r);
    for (int i = 0; i < 3; ++i)
    {
        s.lineTo (c.x - r * 1.2f + (float) i * r * 0.8f, c.y + r - (float) (i + 1) * r * 0.65f);
        s.lineTo (c.x - r * 0.4f + (float) i * r * 0.8f, c.y + r - (float) (i + 1) * r * 0.65f);
    }
    g.setColour (pal().accent);
    g.strokePath (s, juce::PathStrokeType (1.4f));
}

inline const Theme& pitchShifterTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Pitch Shifter";
        x.base = Style::vivid;
        x.pal = { Colour (0xff181a3a), Colour (0xff06070f), Colour (0xff3ae0ff), Colour (0xffb08aff), Colour (0x9c161838), Colour (0xb806070f),
                  Colour (0xff5a6aa0), Colour (0xffeaf6ff), Colour (0xff05060c), Colour (0xff2a2e52), Colour (0xff0e1024), Colour (0xff1e2244),
                  Colour (0xffffffff), Colour (0xff181a3a) };
        x.canvas = canvasPitchShifter;
        x.titleEmblem = pitchShifterEmblem;
        x.cap = Cap::orb;
        x.twoTone = true;
        return x;
    }();
    return t;
}

// === Fruity Love Philter: a love letter — parchment pink, hand-drawn heart curves, wax-seal knobs ================

inline juce::Path heart (Point<float> c, float r)
{
    juce::Path h;
    h.startNewSubPath (c.x, c.y + r);
    h.cubicTo (c.x - r * 1.4f, c.y, c.x - r * 0.9f, c.y - r * 1.1f, c.x, c.y - r * 0.35f);
    h.cubicTo (c.x + r * 0.9f, c.y - r * 1.1f, c.x + r * 1.4f, c.y, c.x, c.y + r);
    h.closeSubPath();
    return h;
}

inline void canvasLovePhilter (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // an envelope curve drawn by hand across the panel, with hearts at its points
    juce::Path env;
    juce::Random rnd (14);
    env.startNewSubPath (a.getX(), a.getBottom() - a.getHeight() * 0.2f);
    std::vector<Point<float>> pts;
    for (int i = 1; i <= 6; ++i)
    {
        const Point<float> q { a.getX() + a.getWidth() * (float) i / 6.0f, a.getY() + a.getHeight() * (0.2f + 0.6f * rnd.nextFloat()) };
        const auto prev = env.getCurrentPosition();
        env.cubicTo (prev.x + (q.x - prev.x) * 0.5f, prev.y, prev.x + (q.x - prev.x) * 0.5f, q.y, q.x, q.y);
        pts.push_back (q);
    }
    g.setColour (p.accent.withAlpha (0.18f));
    g.strokePath (env, stroke (1.6f));
    for (const auto& q : pts)
    {
        g.setColour (p.accent.withAlpha (0.25f));
        g.fillPath (heart (q, 6.0f));
    }
    // a few big faint hearts floating
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (p.accent2.withAlpha (0.05f));
        g.fillPath (heart ({ a.getX() + rnd.nextFloat() * a.getWidth(), a.getY() + rnd.nextFloat() * a.getHeight() }, 20.0f + rnd.nextFloat() * 30.0f));
    }
    vignette (g, a, 0.4f);
}

inline void lovePhilterEmblem (juce::Graphics& g, Point<float> c, float r)
{
    g.setColour (pal().accent);
    g.fillPath (heart (c, r * 0.9f));
}

inline void capWaxSeal (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    juce::Path seal; // a slightly irregular disc
    for (int i = 0; i <= 24; ++i)
    {
        const auto ang = juce::MathConstants<float>::twoPi * (float) i / 24.0f;
        const auto rr = capR * (0.94f + 0.06f * std::sin ((float) i * 2.7f));
        const Point<float> q { c.x + std::cos (ang) * rr, c.y + std::sin (ang) * rr };
        if (i == 0) seal.startNewSubPath (q);
        else seal.lineTo (q);
    }
    seal.closeSubPath();
    g.setGradientFill (ColourGradient (p.accent.brighter (0.2f + hover * 0.2f), c.x - capR * 0.4f, c.y - capR * 0.5f, p.accent.darker (0.7f), c.x + capR * 0.6f, c.y + capR, true));
    g.fillPath (seal);
    if (fx)
    {
        g.setColour (Colours::black.withAlpha (0.2f));
        g.drawEllipse (cap.reduced (capR * 0.3f), 1.0f);
    }
}

inline const Theme& lovePhilterTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruity Love Philter";
        x.base = Style::vivid;
        x.pal = { Colour (0xff2e1620), Colour (0xff10070b), Colour (0xffff5a8a), Colour (0xffffc8d8), Colour (0x9c2a141e), Colour (0xb810070b),
                  Colour (0xff9a5a72), Colour (0xfffff0f4), Colour (0xff0a0507), Colour (0xff4a2232), Colour (0xff1a0a12), Colour (0xff301822),
                  Colour (0xfffff0f4), Colour (0xff2a1420) };
        x.canvas = canvasLovePhilter;
        x.titleEmblem = lovePhilterEmblem;
        x.drawCap = capWaxSeal;
        x.twoTone = true;
        return x;
    }();
    return t;
}

// === Effector: a multi-tool fanned out — blades for its twelve effects, red and steel =============================

inline void canvasEffector (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto o = Point<float> (a.getRight() - a.getWidth() * 0.2f, a.getBottom() - a.getHeight() * 0.15f);
    for (int i = 0; i < 12; ++i) // twelve blades fanned out of the handle
    {
        const auto ang = juce::MathConstants<float>::pi * (0.95f + 0.55f * (float) i / 11.0f);
        juce::Path blade;
        const auto len = a.getHeight() * (0.55f + 0.1f * (float) (i % 3));
        blade.addRoundedRectangle (Rectangle<float> (0.0f, -4.0f, len, 8.0f), 4.0f);
        blade.applyTransform (juce::AffineTransform::rotation (ang).translated (o));
        g.setColour (Colour (0xffc8ccd2).withAlpha (0.08f));
        g.fillPath (blade);
        g.setColour (Colour (0xffc8ccd2).withAlpha (0.14f));
        g.strokePath (blade, juce::PathStrokeType (0.8f));
    }
    // the handle: a red rounded shape with a white cross
    const auto handle = Rectangle<float> (a.getWidth() * 0.22f, 26.0f).withCentre (o);
    g.setColour (p.accent.withAlpha (0.3f));
    g.fillRoundedRectangle (handle, 13.0f);
    g.setColour (Colours::white.withAlpha (0.35f));
    g.fillRect (Rectangle<float> (10.0f, 3.0f).withCentre (handle.getCentre()));
    g.fillRect (Rectangle<float> (3.0f, 10.0f).withCentre (handle.getCentre()));
    vignette (g, a, 0.4f);
}

inline void effectorEmblem (juce::Graphics& g, Point<float> c, float r) // a cross on a red badge
{
    g.setColour (pal().accent);
    g.fillRoundedRectangle (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), r * 0.4f);
    g.setColour (Colours::white);
    g.fillRect (Rectangle<float> (r * 1.2f, r * 0.36f).withCentre (c));
    g.fillRect (Rectangle<float> (r * 0.36f, r * 1.2f).withCentre (c));
}

inline const Theme& effectorTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Effector";
        x.base = Style::vivid;
        x.pal = { Colour (0xff1e1e22), Colour (0xff0a0a0c), Colour (0xffe8303a), Colour (0xffc8ccd2), Colour (0x9c1c1c20), Colour (0xb80a0a0c),
                  Colour (0xff6a6e74), Colour (0xfff2f2f4), Colour (0xff060607), Colour (0xffc9d2dc), Colour (0xff59626d), Colour (0xff222226),
                  Colour (0xff15181b), Colour (0xff1c1c20) };
        x.canvas = canvasEffector;
        x.titleEmblem = effectorEmblem;
        x.drawCap = capBrushed;
        x.arcGlow = 0.25f;
        return x;
    }();
    return t;
}

// === Squeeze / Lo-fi: 8-bit — a pixel grid, CRT scanlines, handheld greens ========================================

inline void canvasEightBit (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // a staircase waveform drawn in big pixels
    const auto px = 8.0f;
    for (float x = a.getX(); x < a.getRight(); x += px)
    {
        const auto u = (x - a.getX()) / a.getWidth();
        const auto v = std::round (std::sin (u * 9.0f) * 4.0f) / 4.0f;
        const auto y = a.getCentreY() - v * a.getHeight() * 0.3f;
        g.setColour (p.accent.withAlpha (0.18f));
        g.fillRect (Rectangle<float> (x + 1.0f, std::round (y / px) * px + 1.0f, px - 2.0f, px - 2.0f));
    }
    g.setColour (Colours::black.withAlpha (0.12f)); // the pixel grid
    for (float x = a.getX(); x < a.getRight(); x += px) g.fillRect (Rectangle<float> (x, a.getY(), 1.0f, a.getHeight()));
    for (float y = a.getY(); y < a.getBottom(); y += px) g.fillRect (Rectangle<float> (a.getX(), y, a.getWidth(), 1.0f));
    vignette (g, a, 0.45f);
}

inline void eightBitEmblem (juce::Graphics& g, Point<float> c, float r) // a step drawn in blocks
{
    g.setColour (pal().accent);
    const auto s = r * 0.55f;
    for (const auto& q : { Point<float> (-1.5f, 1.0f), Point<float> (-0.5f, 1.0f), Point<float> (-0.5f, 0.0f), Point<float> (0.5f, 0.0f), Point<float> (0.5f, -1.0f), Point<float> (1.5f, -1.0f) })
        g.fillRect (Rectangle<float> (s, s).withCentre (c + q * s));
}

inline void capPixel (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    const auto sq = cap.reduced (capR * 0.18f);
    g.setColour (p.capBottom);
    g.fillRect (sq);
    g.setColour (p.capTop.brighter (hover * 0.2f));
    g.fillRect (sq.reduced (2.0f).withTrimmedBottom (sq.getHeight() * 0.3f));
    if (fx)
    {
        g.setColour (Colours::black.withAlpha (0.5f));
        g.drawRect (sq, 1.0f);
    }
    juce::ignoreUnused (c);
}

inline const Theme& eightBitTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Squeeze";
        x.base = Style::vivid;
        x.pal = { Colour (0xff1a2a12), Colour (0xff070d05), Colour (0xff9bd23a), Colour (0xffd8f0a0), Colour (0x9c182812), Colour (0xb8070d05),
                  Colour (0xff5a7a3a), Colour (0xffe8ffd0), Colour (0xff050a04), Colour (0xff5a7a3a), Colour (0xff1e2e14), Colour (0xff1c2c14),
                  Colour (0xffe8ffd0), Colour (0xff1a2a12) };
        x.canvas = canvasEightBit;
        x.titleEmblem = eightBitEmblem;
        x.drawCap = capPixel;
        x.buttTrack = true;
        x.arcGlow = 0.0f;
        return x;
    }();
    return t;
}

inline const Theme& lofiTheme()
{
    static const Theme t = []
    {
        Theme x = eightBitTheme();
        x.name = "Lo-fi";
        x.pal.accent = Colour (0xffffb03a);
        x.pal.accent2 = Colour (0xfffff0c8);
        x.pal.bgTop = Colour (0xff2a1e12);
        x.pal.bgBottom = Colour (0xff0d0905);
        return x;
    }();
    return t;
}

// === batch 8: LYNX's basic effects and FL's smaller tools — lighter themes, each with its own motif ================
// The motifs below are shared; each theme picks one with its own palette and placement.

inline void motifRings (juce::Graphics& g, Rectangle<float> a, Point<float> c, int n, float step, float squash, Colour col, float alpha)
{
    for (int i = 1; i <= n; ++i)
    {
        g.setColour (col.withAlpha (alpha * (1.0f - (float) i / (float) (n + 1))));
        g.drawEllipse (Rectangle<float> ((float) i * step * 2.0f, (float) i * step * 2.0f * squash).withCentre (c), 1.2f);
    }
    juce::ignoreUnused (a);
}

inline void motifCurve (juce::Graphics& g, Rectangle<float> a, float (*fn) (float), float amp, Colour col, float alpha, float width)
{
    juce::Path c;
    for (float x = a.getX(); x <= a.getRight(); x += 3.0f)
    {
        const Point<float> q { x, a.getCentreY() - fn ((x - a.getX()) / a.getWidth()) * amp };
        if (x == a.getX()) c.startNewSubPath (q);
        else c.lineTo (q);
    }
    if (! reduced)
    {
        g.setColour (col.withAlpha (alpha * 0.3f));
        g.strokePath (c, stroke (width * 4.0f));
    }
    g.setColour (col.withAlpha (alpha));
    g.strokePath (c, stroke (width));
}

inline void motifBars (juce::Graphics& g, Rectangle<float> a, int n, const float* heights, Colour col, float alpha)
{
    const auto bw = a.getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const auto h = a.getHeight() * heights[i];
        const auto bar = Rectangle<float> (a.getX() + (float) i * bw + bw * 0.2f, a.getBottom() - h, bw * 0.6f, h);
        g.setGradientFill (ColourGradient (col.withAlpha (alpha), 0.0f, bar.getY(), col.withAlpha (alpha * 0.15f), 0.0f, bar.getBottom(), false));
        g.fillRoundedRectangle (bar, bw * 0.15f);
    }
}

inline void emblemDot (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    if (! reduced) bloom (g, c, r * 2.4f, p.accent, 0.35f);
    g.setColour (p.accent);
    g.fillEllipse (Rectangle<float> (r * 1.1f, r * 1.1f).withCentre (c));
    g.setColour (p.accent2);
    g.drawEllipse (Rectangle<float> (r * 1.9f, r * 1.9f).withCentre (c), 1.0f);
}

inline float fnBell (float u) { return std::exp (-std::pow ((u - 0.45f) / 0.12f, 2.0f)) - 0.4f * std::exp (-std::pow ((u - 0.8f) / 0.07f, 2.0f)); }
inline float fnLowpass (float u) { return 0.5f + (u < 0.55f ? 0.0f : -1.6f * (u - 0.55f)) + 0.35f * std::exp (-std::pow ((u - 0.55f) / 0.04f, 2.0f)); }
inline float fnSine (float u) { return std::sin (u * 18.0f) * (0.6f + 0.4f * std::sin (u * 3.0f)); }
inline float fnPan (float u) { return 2.0f * std::abs (std::fmod (u * 3.0f, 1.0f) - 0.5f) * 2.0f - 1.0f; }
inline float fnDecay (float u) { return std::exp (-u * 4.0f) * std::sin (u * 60.0f); }
inline float fnClip (float u) { return juce::jlimit (-0.6f, 0.6f, 1.5f * std::sin (u * 14.0f)); }
inline float fnSteps (float u) { return std::fmod (std::floor (u * 16.0f), 2.0f) < 1.0f ? 0.5f : -0.2f; }
inline float fnTilt (float u) { return 0.6f - 1.2f * u + 0.15f * std::sin (u * 9.0f); }
inline float fnBass (float u) { return 0.8f * std::exp (-std::pow (u / 0.18f, 2.0f)) - 0.1f; }
inline float fnPresence (float u) { return 0.7f * std::exp (-std::pow ((u - 0.7f) / 0.15f, 2.0f)); }

// --- LYNX's EQ: a clean bell over a fine grid (studio steel) -----------------------------------------------------------
inline void canvasBasicEq (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    for (float x = a.getX(); x < a.getRight(); x += 24.0f)
    {
        g.setColour (p.accent2.withAlpha (0.03f));
        g.fillRect (Rectangle<float> (x, a.getY(), 1.0f, a.getHeight()));
    }
    motifCurve (g, a, fnBell, a.getHeight() * 0.3f, p.accent, 0.16f, 1.6f);
    vignette (g, a, 0.4f);
}

// --- LYNX's Compressor (RComp role): a renaissance gold — an arch of a squeezed waveform ---------------------------------
inline void canvasBasicComp (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    motifCurve (g, a, fnClip, a.getHeight() * 0.35f, p.accent, 0.12f, 1.4f);
    softLight (g, { a.getCentreX(), a.getY() }, a.getWidth() * 0.6f, p.accent, 0.08f);
    vignette (g, a, 0.4f);
}

// --- LYNX's Delay: echoes — a pulse and its repeats fading to the right --------------------------------------------------
inline void canvasBasicDelay (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    for (int i = 0; i < 8; ++i)
    {
        const auto x = a.getX() + a.getWidth() * (0.08f + 0.12f * (float) i);
        const auto h = a.getHeight() * 0.6f * std::pow (0.72f, (float) i);
        g.setColour (p.accent.withAlpha (0.22f * std::pow (0.8f, (float) i)));
        g.fillRoundedRectangle (Rectangle<float> (4.0f, h).withCentre ({ x, a.getCentreY() }), 2.0f);
    }
    vignette (g, a, 0.4f);
}

// --- LYNX's Reverb: a room's reflections — rays bouncing off walls -------------------------------------------------------
inline void canvasBasicReverb (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    auto q = Point<float> (a.getX() + a.getWidth() * 0.1f, a.getY() + a.getHeight() * 0.7f);
    Point<float> d { 0.83f, -0.55f };
    juce::Path ray;
    ray.startNewSubPath (q);
    for (int i = 0; i < 24; ++i)
    {
        auto tx = d.x > 0 ? (a.getRight() - q.x) / d.x : (a.getX() - q.x) / d.x;
        auto ty = d.y > 0 ? (a.getBottom() - q.y) / d.y : (a.getY() - q.y) / d.y;
        const auto t = juce::jmin (tx, ty);
        q += d * t;
        ray.lineTo (q);
        if (tx < ty) d.x = -d.x; else d.y = -d.y;
    }
    g.setColour (p.accent.withAlpha (0.08f));
    g.strokePath (ray, juce::PathStrokeType (1.0f));
    vignette (g, a, 0.4f);
}

// --- LYNX's Limiter / Distortion / Filter / Chorus ------------------------------------------------------------------------
inline void canvasBasicLimiter (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    g.setColour (p.accent.withAlpha (0.35f));
    g.fillRect (Rectangle<float> (a.getX(), a.getY() + a.getHeight() * 0.25f, a.getWidth(), 1.2f));
    motifCurve (g, a.withTrimmedTop (a.getHeight() * 0.25f), fnClip, a.getHeight() * 0.3f, p.accent2, 0.1f, 1.2f);
    vignette (g, a, 0.4f);
}

inline void canvasBasicDistortion (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    motifCurve (g, a, fnClip, a.getHeight() * 0.32f, p.accent, 0.18f, 1.6f);
    g.setGradientFill (ColourGradient (p.accent.withAlpha (0.14f), 0.0f, a.getBottom(), p.accent.withAlpha (0.0f), 0.0f, a.getCentreY(), false));
    g.fillRect (a);
    vignette (g, a, 0.4f);
}

inline void canvasBasicFilter (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    motifCurve (g, a, fnLowpass, a.getHeight() * 0.45f, p.accent, 0.18f, 1.6f);
    vignette (g, a, 0.4f);
}

inline void canvasBasicChorus (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    wavesWoven (g, a, 3, a.getHeight() * 0.2f, 0.022f, 0.5f, p.accent, p.accent2, 0.15f);
    vignette (g, a, 0.4f);
}

// --- panning / stereo / utilities -----------------------------------------------------------------------------------------
inline void canvasPan (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    motifCurve (g, a, fnPan, a.getHeight() * 0.28f, p.accent, 0.16f, 1.4f);
    for (const auto f : { 0.06f, 0.94f }) // left and right speakers
        speaker (g, { a.getX() + a.getWidth() * f, a.getCentreY() }, 16.0f, p.accent2, 0.8f);
    vignette (g, a, 0.4f);
}

inline void canvasBalance (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // a balance scale: a beam on a post and two pans
    const auto c = Point<float> (a.getRight() - a.getWidth() * 0.22f, a.getY() + a.getHeight() * 0.35f);
    const auto w = a.getWidth() * 0.18f;
    g.setColour (p.accent.withAlpha (0.2f));
    g.drawLine (c.x, c.y, c.x, a.getBottom() - 8.0f, 2.0f);
    g.drawLine (c.x - w, c.y + 4.0f, c.x + w, c.y - 4.0f, 2.0f);
    for (const auto s : { -1.0f, 1.0f })
    {
        const auto pc = Point<float> (c.x + s * w, c.y - s * 4.0f + 30.0f);
        g.drawLine (c.x + s * w, c.y - s * 4.0f, pc.x - 14.0f, pc.y, 0.8f);
        g.drawLine (c.x + s * w, c.y - s * 4.0f, pc.x + 14.0f, pc.y, 0.8f);
        g.drawLine (pc.x - 16.0f, pc.y, pc.x + 16.0f, pc.y, 2.0f);
    }
    vignette (g, a, 0.4f);
}

inline void canvasUtility (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // a calm graphite plate with a fine dot grid
    for (float y = a.getY() + 6.0f; y < a.getBottom(); y += 12.0f)
        for (float x = a.getX() + 6.0f; x < a.getRight(); x += 12.0f)
        {
            g.setColour (p.accent2.withAlpha (0.05f));
            g.fillEllipse (Rectangle<float> (1.6f, 1.6f).withCentre ({ x, y }));
        }
    vignette (g, a, 0.35f);
}

// --- FL's filters and EQs ---------------------------------------------------------------------------------------------------
inline void canvasSweep (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    for (int k = 0; k < 4; ++k) // a filter sweeping: the same curve at four cutoffs
    {
        juce::Path c;
        for (float x = a.getX(); x <= a.getRight(); x += 3.0f)
        {
            const auto u = (x - a.getX()) / a.getWidth() - 0.15f * (float) k;
            const Point<float> q { x, a.getCentreY() - fnLowpass (u + 0.3f) * a.getHeight() * 0.4f };
            if (x == a.getX()) c.startNewSubPath (q);
            else c.lineTo (q);
        }
        g.setColour (p.accent.interpolatedWith (p.accent2, (float) k / 3.0f).withAlpha (0.12f));
        g.strokePath (c, stroke (1.4f));
    }
    vignette (g, a, 0.4f);
}

inline void canvasGraphicEq (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    static const float h[] { 0.5f, 0.62f, 0.45f, 0.38f, 0.52f, 0.66f, 0.58f };
    motifBars (g, a.reduced (a.getWidth() * 0.1f, a.getHeight() * 0.1f), 7, h, p.accent, 0.16f);
    vignette (g, a, 0.4f);
}

inline void canvasBassBoost (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    motifCurve (g, a, fnBass, a.getHeight() * 0.45f, p.accent, 0.2f, 2.0f);
    speaker (g, { a.getRight() - a.getWidth() * 0.18f, a.getCentreY() }, a.getHeight() * 0.3f, p.accent, 1.0f);
    vignette (g, a, 0.4f);
}

inline void canvasEmphasis (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    motifCurve (g, a, fnPresence, a.getHeight() * 0.4f, p.accent, 0.18f, 1.6f);
    vignette (g, a, 0.4f);
}

inline void canvasTilt (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    motifCurve (g, a, fnTilt, a.getHeight() * 0.3f, p.accent, 0.16f, 1.6f);
    vignette (g, a, 0.4f);
}

// --- splitters, multiband delay, convolver --------------------------------------------------------------------------------
inline void canvasDelta (juce::Graphics& g, Rectangle<float> a, bool display) // a river splitting into a delta
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto o = Point<float> (a.getX(), a.getCentreY());
    for (int i = 0; i < 5; ++i)
    {
        juce::Path r;
        const auto ty = a.getY() + a.getHeight() * (0.1f + 0.2f * (float) i);
        r.startNewSubPath (o);
        r.cubicTo (a.getX() + a.getWidth() * 0.35f, o.y, a.getX() + a.getWidth() * 0.5f, ty, a.getRight(), ty);
        g.setColour (p.accent.interpolatedWith (p.accent2, (float) i / 4.0f).withAlpha (0.14f));
        g.strokePath (r, stroke (3.0f - 0.4f * (float) i));
    }
    vignette (g, a, 0.4f);
}

inline void canvasSkyline (juce::Graphics& g, Rectangle<float> a, bool display) // a skyline of band-coloured buildings
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const Colour bands[] { Colour (0xffff6a5a), Colour (0xffffc04a), Colour (0xff4fe08a), Colour (0xff4fa8ff), Colour (0xffb98cff) };
    juce::Random rnd (21);
    for (float x = a.getX(); x < a.getRight(); x += 22.0f)
    {
        const auto h = a.getHeight() * (0.15f + 0.45f * rnd.nextFloat());
        const auto b = Rectangle<float> (x + 2.0f, a.getBottom() - h, 18.0f, h);
        g.setColour (bands[rnd.nextInt (5)].withAlpha (0.10f));
        g.fillRect (b);
        g.setColour (Colours::white.withAlpha (0.05f));
        for (float wy = b.getY() + 4.0f; wy < b.getBottom() - 4.0f; wy += 7.0f) // windows
            g.fillRect (Rectangle<float> (b.getX() + 4.0f, wy, 3.0f, 3.0f));
    }
    vignette (g, a, 0.4f);
}

inline void canvasCathedral (juce::Graphics& g, Rectangle<float> a, bool display) // an impulse becoming a cathedral
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // the impulse response: a spike and a decaying comb of reflections
    for (float x = a.getX() + 20.0f; x < a.getRight(); x += 5.0f)
    {
        const auto u = (x - a.getX() - 20.0f) / a.getWidth();
        const auto h = a.getHeight() * 0.7f * std::exp (-u * 3.5f) * (x == a.getX() + 20.0f ? 1.0f : 0.5f + 0.5f * std::abs (std::sin (x * 1.7f)));
        g.setColour (p.accent.withAlpha (0.13f));
        g.fillRect (Rectangle<float> (x, a.getBottom() - h, 1.5f, h));
    }
    // a gothic window silhouette on the right
    const auto c = Point<float> (a.getRight() - a.getWidth() * 0.16f, a.getY() + a.getHeight() * 0.25f);
    const auto w = a.getWidth() * 0.08f;
    juce::Path win;
    win.startNewSubPath (c.x - w, a.getBottom());
    win.lineTo (c.x - w, c.y + w);
    win.quadraticTo (c.x - w, c.y - w * 0.4f, c.x, c.y - w * 1.2f);
    win.quadraticTo (c.x + w, c.y - w * 0.4f, c.x + w, c.y + w);
    win.lineTo (c.x + w, a.getBottom());
    g.setColour (p.accent2.withAlpha (0.15f));
    g.strokePath (win, juce::PathStrokeType (1.6f));
    vignette (g, a, 0.4f);
}

inline void canvasGate (juce::Graphics& g, Rectangle<float> a, bool display) // a step sequence of open / closed gates
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    motifCurve (g, a, fnSteps, a.getHeight() * 0.3f, p.accent, 0.15f, 1.4f);
    vignette (g, a, 0.4f);
}

inline void canvasShift (juce::Graphics& g, Rectangle<float> a, bool display) // a spectrum line moved sideways, twice
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    for (int k = 0; k < 3; ++k)
    {
        juce::Path c;
        for (float x = a.getX(); x <= a.getRight(); x += 3.0f)
        {
            const auto u = (x - a.getX()) / a.getWidth() - 0.08f * (float) k;
            const Point<float> q { x, a.getCentreY() - fnPresence (u + 0.1f) * a.getHeight() * 0.4f };
            if (x == a.getX()) c.startNewSubPath (q);
            else c.lineTo (q);
        }
        g.setColour (p.accent.interpolatedWith (p.accent2, (float) k / 2.0f).withAlpha (0.15f));
        g.strokePath (c, stroke (1.4f));
    }
    vignette (g, a, 0.4f);
}

inline void canvasDecay (juce::Graphics& g, Rectangle<float> a, bool display) // a ringing decay
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    motifCurve (g, a, fnDecay, a.getHeight() * 0.38f, p.accent, 0.15f, 1.2f);
    vignette (g, a, 0.4f);
}

inline void canvasSine (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    motifCurve (g, a, fnSine, a.getHeight() * 0.25f, p.accent, 0.14f, 1.3f);
    vignette (g, a, 0.4f);
}

// one theme from a motif, a palette and a knob family (batch 8 builds its themes with it)
inline Theme liteTheme (const char* name, Style base, Colour bgTop, Colour bgBottom, Colour accent, Colour accent2, void (*canvas) (juce::Graphics&, Rectangle<float>, bool),
                        Cap cap, void (*drawCap) (juce::Graphics&, Rectangle<float>, Point<float>, float, float, bool) = nullptr)
{
    Theme x;
    x.name = name;
    x.base = base;
    x.pal = { bgTop, bgBottom, accent, accent2, bgTop.brighter (0.06f).withAlpha ((juce::uint8) 0x9c), bgBottom.withAlpha ((juce::uint8) 0xb8),
              bgTop.interpolatedWith (accent, 0.35f).brighter (0.2f), accent2.interpolatedWith (Colours::white, 0.7f), bgBottom.darker (0.4f),
              bgTop.brighter (0.25f), bgBottom.brighter (0.05f), bgTop.brighter (0.08f), Colours::white, bgTop.brighter (0.04f) };
    x.canvas = canvas;
    x.titleEmblem = emblemDot;
    x.cap = cap;
    x.drawCap = drawCap;
    return x;
}

#define LYNX_LITE_THEME(fn, ...) inline const Theme& fn() { static const Theme t = liteTheme (__VA_ARGS__); return t; }
LYNX_LITE_THEME (basicEqTheme, "EQ", Style::glow, Colour (0xff1a2026), Colour (0xff080a0d), Colour (0xffff4a5a), Colour (0xff9ad0ff), canvasBasicEq, Cap::flat)
LYNX_LITE_THEME (basicCompTheme, "Compressor", Style::hardware, Colour (0xff2a2418), Colour (0xff0d0b07), Colour (0xffe8b54a), Colour (0xfffff0c8), canvasBasicComp, Cap::flat, capBrass)
LYNX_LITE_THEME (basicDelayTheme, "Delay", Style::glow, Colour (0xff14202c), Colour (0xff05090d), Colour (0xff4ab8ff), Colour (0xffc8ecff), canvasBasicDelay, Cap::rubber)
LYNX_LITE_THEME (basicReverbTheme, "Reverb", Style::neon, Colour (0xff1c1630), Colour (0xff07050f), Colour (0xffa67cff), Colour (0xffe0d4ff), canvasBasicReverb, Cap::orb)
LYNX_LITE_THEME (basicLimiterTheme, "Limiter", Style::glow, Colour (0xff221a1a), Colour (0xff0b0808), Colour (0xffff5a4a), Colour (0xffffd0c8), canvasBasicLimiter, Cap::flat)
LYNX_LITE_THEME (basicDistortionTheme, "Distortion", Style::vivid, Colour (0xff261410), Colour (0xff0b0504), Colour (0xffff6a2a), Colour (0xffffd08a), canvasBasicDistortion, Cap::rubber)
LYNX_LITE_THEME (basicFilterTheme, "Filter", Style::neon, Colour (0xff10222a), Colour (0xff04090c), Colour (0xff3ae0d0), Colour (0xffb8fff4), canvasBasicFilter, Cap::orb)
LYNX_LITE_THEME (basicChorusTheme, "Chorus", Style::glow, Colour (0xff121c2a), Colour (0xff05080d), Colour (0xff5ad8ff), Colour (0xffd0a8ff), canvasBasicChorus, Cap::porcelain)
LYNX_LITE_THEME (autoPanTheme, "Auto-pan", Style::glow, Colour (0xff142226), Colour (0xff05090b), Colour (0xff4fe0c0), Colour (0xffc8fff0), canvasPan, Cap::flat)
LYNX_LITE_THEME (panOMaticTheme, "Fruity PanOMatic", Style::vivid, Colour (0xff1e1430), Colour (0xff09060f), Colour (0xffff7ad0), Colour (0xff9ae8ff), canvasPan, Cap::rubber)
LYNX_LITE_THEME (balanceTheme, "Fruity Balance", Style::glow, Colour (0xff1c1e22), Colour (0xff09090b), Colour (0xffd8c08a), Colour (0xfff0e8d8), canvasBalance, Cap::flat, capBrass)
LYNX_LITE_THEME (utilityTheme, "Utility", Style::glow, Colour (0xff1e2024), Colour (0xff0a0a0c), Colour (0xffb6ff5c), Colour (0xffe0e4e8), canvasUtility, Cap::flat)
LYNX_LITE_THEME (sendTheme, "Fruity Send", Style::glow, Colour (0xff1a1e26), Colour (0xff08090c), Colour (0xff7ab8ff), Colour (0xffe0ecff), canvasUtility, Cap::flat)
LYNX_LITE_THEME (splitterTheme, "Frequency Splitter", Style::glow, Colour (0xff10202a), Colour (0xff04080c), Colour (0xff4fa8ff), Colour (0xff4fe08a), canvasDelta, Cap::flat)
LYNX_LITE_THEME (crossoverTheme, "Crossover", Style::glow, Colour (0xff141e26), Colour (0xff05080b), Colour (0xff5ab4ff), Colour (0xffffc04a), canvasDelta, Cap::flat)
LYNX_LITE_THEME (multibandDelayTheme, "Multiband Delay", Style::vivid, Colour (0xff141428), Colour (0xff05050e), Colour (0xffffc04a), Colour (0xff4fa8ff), canvasSkyline, Cap::orb)
LYNX_LITE_THEME (equoTheme, "EQUO", Style::neon, Colour (0xff142030), Colour (0xff05080f), Colour (0xff46d8ff), Colour (0xffff7ad0), canvasGraphicEq, Cap::flat)
LYNX_LITE_THEME (emphasizerTheme, "Emphasizer", Style::glow, Colour (0xff221c14), Colour (0xff0b0906), Colour (0xffffc86a), Colour (0xfffff0d0), canvasEmphasis, Cap::porcelain)
LYNX_LITE_THEME (lowLifterTheme, "Low Lifter", Style::hardware, Colour (0xff1a1c26), Colour (0xff07080c), Colour (0xffff7a3a), Colour (0xffffd8b0), canvasBassBoost, Cap::bakelite)
LYNX_LITE_THEME (fastLpTheme, "Fruity Fast LP", Style::neon, Colour (0xff10261e), Colour (0xff040b08), Colour (0xff3aff9a), Colour (0xffc8ffe0), canvasSweep, Cap::rubber)
LYNX_LITE_THEME (fruityFilterTheme, "Fruity Filter", Style::neon, Colour (0xff1a1430), Colour (0xff07050f), Colour (0xffc070ff), Colour (0xff70e0ff), canvasSweep, Cap::orb)
LYNX_LITE_THEME (freeFilterTheme, "Fruity Free Filter", Style::neon, Colour (0xff0e2230), Colour (0xff04090f), Colour (0xff3ac8ff), Colour (0xffff9a6a), canvasSweep, Cap::orb)
LYNX_LITE_THEME (sevenBandTheme, "Fruity 7 Band EQ", Style::hardware, Colour (0xff22262c), Colour (0xff0a0b0d), Colour (0xff6ae06a), Colour (0xffe8ffe0), canvasGraphicEq, Cap::bakelite)
LYNX_LITE_THEME (bassBoostTheme, "Fruity Bass Boost", Style::vivid, Colour (0xff200f1c), Colour (0xff0a0409), Colour (0xffff3a8a), Colour (0xffffc0e0), canvasBassBoost, Cap::rubber)
LYNX_LITE_THEME (convolverTheme, "Fruity Convolver", Style::glow, Colour (0xff1c1a24), Colour (0xff08070c), Colour (0xffd8c08a), Colour (0xffb8a8ff), canvasCathedral, Cap::flat, capStone)
LYNX_LITE_THEME (volumeGateTheme, "Volume gate", Style::vivid, Colour (0xff1a1430), Colour (0xff07050f), Colour (0xffff4fa3), Colour (0xff4fe0ff), canvasGate, Cap::rubber)
LYNX_LITE_THEME (rhythmGainTheme, "Rhythmic envelope", Style::vivid, Colour (0xff201628), Colour (0xff0a060d), Colour (0xffff7a5a), Colour (0xffffd0a0), canvasGate, Cap::rubber)
LYNX_LITE_THEME (freqShiftTheme, "Frequency shifter", Style::neon, Colour (0xff102224), Colour (0xff04090a), Colour (0xff3ae0c0), Colour (0xffff8a6a), canvasShift, Cap::orb)
LYNX_LITE_THEME (pitchShiftTheme, "Pitch shift", Style::neon, Colour (0xff181a36), Colour (0xff06070f), Colour (0xff6ab8ff), Colour (0xffd0a8ff), canvasShift, Cap::orb)
LYNX_LITE_THEME (phaseInverterTheme, "Fruity Phase Inverter", Style::glow, Colour (0xff1e1e24), Colour (0xff09090c), Colour (0xff9ad0ff), Colour (0xffff9a9a), canvasSine, Cap::flat)
LYNX_LITE_THEME (centerTheme, "Fruity Center", Style::glow, Colour (0xff1e2024), Colour (0xff09090b), Colour (0xffd0d4da), Colour (0xff9ad0ff), canvasUtility, Cap::flat)
LYNX_LITE_THEME (delay1Theme, "Fruity Delay", Style::glow, Colour (0xff162226), Colour (0xff060a0b), Colour (0xff6ad8c0), Colour (0xffd0fff4), canvasBasicDelay, Cap::rubber)
LYNX_LITE_THEME (mute2Theme, "Fruity Mute 2", Style::glow, Colour (0xff221a1c), Colour (0xff0b0809), Colour (0xffff5a5a), Colour (0xffe0e0e0), canvasUtility, Cap::flat)
#undef LYNX_LITE_THEME

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
        case Art::echoform: return &echoformTheme();
        case Art::spiralEcho: return &spiralTheme();
        case Art::orbit: return &orbitTheme();
        case Art::lattice: return &latticeTheme();
        case Art::gemini: return &geminiTheme();
        case Art::delay2: return &delay2Theme();
        case Art::delay3: return &delay3Theme();
        case Art::delayBank: return &delayBankTheme();
        case Art::ember: return &emberTheme();
        case Art::aether: return &aetherTheme();
        case Art::blood: return &bloodTheme();
        case Art::fastDist: return &fastDistTheme();
        case Art::waveShaper: return &waveShaperTheme();
        case Art::softClipper: return &softClipperTheme();
        case Art::hardcore: return &hardcoreTheme();
        case Art::distructor: return &distructorTheme();
        case Art::chorus: return &chorusTheme();
        case Art::hyperChorus: return &hyperChorusTheme();
        case Art::vintageChorus: return &vintageChorusTheme();
        case Art::flanger: return &flangerTheme();
        case Art::flangus: return &flangusTheme();
        case Art::phaser: return &phaserTheme();
        case Art::vintagePhaser: return &vintagePhaserTheme();
        case Art::freqShifter: return &freqShifterTheme();
        case Art::paramEq: return &paramEqTheme();
        case Art::limiter: return &limiterTheme();
        case Art::compressor: return &compressorTheme();
        case Art::multiband: return &multibandTheme();
        case Art::maximus: return &maximusTheme();
        case Art::transient: return &transientTheme();
        case Art::stereoShaper: return &stereoShaperTheme();
        case Art::stereoEnhancer: return &stereoEnhancerTheme();
        case Art::spreader: return &spreaderTheme();
        case Art::grossBeat: return &grossBeatTheme();
        case Art::vocodex: return &vocodexTheme();
        case Art::vocoder: return &vocoderTheme();
        case Art::pitcher: return &pitcherTheme();
        case Art::lynxTune: return &lynxTuneTheme();
        case Art::voiceShift: return &voiceShiftTheme();
        case Art::quell: return &quellTheme();
        case Art::pitchShifter: return &pitchShifterTheme();
        case Art::lovePhilter: return &lovePhilterTheme();
        case Art::effector: return &effectorTheme();
        case Art::squeeze: return &eightBitTheme();
        case Art::lofi: return &lofiTheme();
        case Art::basicEq: return &basicEqTheme();
        case Art::basicComp: return &basicCompTheme();
        case Art::basicDelay: return &basicDelayTheme();
        case Art::basicReverb: return &basicReverbTheme();
        case Art::basicLimiter: return &basicLimiterTheme();
        case Art::basicDistortion: return &basicDistortionTheme();
        case Art::basicFilter: return &basicFilterTheme();
        case Art::basicChorus: return &basicChorusTheme();
        case Art::autoPan: return &autoPanTheme();
        case Art::panOMatic: return &panOMaticTheme();
        case Art::balance: return &balanceTheme();
        case Art::utility: return &utilityTheme();
        case Art::send: return &sendTheme();
        case Art::splitter: return &splitterTheme();
        case Art::crossover: return &crossoverTheme();
        case Art::multibandDelay: return &multibandDelayTheme();
        case Art::equo: return &equoTheme();
        case Art::emphasizer: return &emphasizerTheme();
        case Art::lowLifter: return &lowLifterTheme();
        case Art::fastLp: return &fastLpTheme();
        case Art::fruityFilter: return &fruityFilterTheme();
        case Art::freeFilter: return &freeFilterTheme();
        case Art::sevenBand: return &sevenBandTheme();
        case Art::bassBoost: return &bassBoostTheme();
        case Art::convolver: return &convolverTheme();
        case Art::volumeGate: return &volumeGateTheme();
        case Art::rhythmGain: return &rhythmGainTheme();
        case Art::freqShift: return &freqShiftTheme();
        case Art::pitchShift: return &pitchShiftTheme();
        case Art::phaseInverter: return &phaseInverterTheme();
        case Art::center: return &centerTheme();
        case Art::delay1: return &delay1Theme();
        case Art::mute2: return &mute2Theme();
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
        case EffectKind::echoform: return Art::echoform;
        case EffectKind::spiralEcho: return Art::spiralEcho;
        case EffectKind::orbit: return Art::orbit;
        case EffectKind::latticeMod: return Art::lattice;
        case EffectKind::geminiDoubler: return Art::gemini;
        case EffectKind::fruityDelay2: return Art::delay2;
        case EffectKind::fruityDelay3: return Art::delay3;
        case EffectKind::fruityDelayBank: return Art::delayBank;
        case EffectKind::ember: return Art::ember;
        case EffectKind::aether: return Art::aether;
        case EffectKind::fruityBloodOverdrive: return Art::blood;
        case EffectKind::fruityFastDist: return Art::fastDist;
        case EffectKind::fruityWaveShaper: return Art::waveShaper;
        case EffectKind::fruitySoftClipper: return Art::softClipper;
        case EffectKind::hardcore: return Art::hardcore;
        case EffectKind::distructor: return Art::distructor;
        case EffectKind::fruityChorus: return Art::chorus;
        case EffectKind::hyperChorus: return Art::hyperChorus;
        case EffectKind::vintageChorus: return Art::vintageChorus;
        case EffectKind::fruityFlanger: return Art::flanger;
        case EffectKind::fruityFlangus: return Art::flangus;
        case EffectKind::fruityPhaser: return Art::phaser;
        case EffectKind::vintagePhaser: return Art::vintagePhaser;
        case EffectKind::flFrequencyShifter: return Art::freqShifter;
        case EffectKind::parametricEq: return Art::paramEq;
        case EffectKind::fruityLimiter: return Art::limiter;
        case EffectKind::fruityCompressor: return Art::compressor;
        case EffectKind::fruityMultiband: return Art::multiband;
        case EffectKind::maximus: return Art::maximus;
        case EffectKind::transientProcessor: return Art::transient;
        case EffectKind::stereoShaper: return Art::stereoShaper;
        case EffectKind::stereoEnhancer: return Art::stereoEnhancer;
        case EffectKind::spreader: return Art::spreader;
        case EffectKind::grossBeat: return Art::grossBeat;
        case EffectKind::vocodex: return Art::vocodex;
        case EffectKind::vocoder: return Art::vocoder;
        case EffectKind::pitchCorrect: return Art::pitcher;
        case EffectKind::lynxTune: return Art::lynxTune;
        case EffectKind::voiceShift: return Art::voiceShift;
        case EffectKind::quellDenoiser: return Art::quell;
        case EffectKind::flPitchShifter: return Art::pitchShifter;
        case EffectKind::lovePhilter: return Art::lovePhilter;
        case EffectKind::effector: return Art::effector;
        case EffectKind::squeeze: return Art::squeeze;
        case EffectKind::lofi: return Art::lofi;
        case EffectKind::eq: return Art::basicEq;
        case EffectKind::compressor: return Art::basicComp;
        case EffectKind::delay: return Art::basicDelay;
        case EffectKind::reverb: return Art::basicReverb;
        case EffectKind::limiter: return Art::basicLimiter;
        case EffectKind::distortion: return Art::basicDistortion;
        case EffectKind::filter: return Art::basicFilter;
        case EffectKind::chorus: return Art::basicChorus;
        case EffectKind::autoPan: return Art::autoPan;
        case EffectKind::fruityPanOMatic: return Art::panOMatic;
        case EffectKind::fruityBalance: return Art::balance;
        case EffectKind::utility: return Art::utility;
        case EffectKind::fruitySend: return Art::send;
        case EffectKind::frequencySplitter: return Art::splitter;
        case EffectKind::crossover: return Art::crossover;
        case EffectKind::multibandDelay: return Art::multibandDelay;
        case EffectKind::equo: return Art::equo;
        case EffectKind::emphasizer: return Art::emphasizer;
        case EffectKind::flLowLifter: return Art::lowLifter;
        case EffectKind::fruityFastLP: return Art::fastLp;
        case EffectKind::fruityFilter: return Art::fruityFilter;
        case EffectKind::fruityFreeFilter: return Art::freeFilter;
        case EffectKind::fl7BandEq: return Art::sevenBand;
        case EffectKind::flBassBoost: return Art::bassBoost;
        case EffectKind::convolver: return Art::convolver;
        case EffectKind::volumeGate: return Art::volumeGate;
        case EffectKind::rhythmGain: return Art::rhythmGain;
        case EffectKind::freqShift: return Art::freqShift;
        case EffectKind::pitchShift: return Art::pitchShift;
        case EffectKind::flPhaseInverter: return Art::phaseInverter;
        case EffectKind::flCenter: return Art::center;
        case EffectKind::flDelay1: return Art::delay1;
        case EffectKind::fruityMute2: return Art::mute2;
        default: return Art::none;
    }
}

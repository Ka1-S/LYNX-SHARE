// The generators' themes of the "Themed" plugin style (see PluginArt.inc for the framework and the rules; the art
// directions and the style guide are in the tracker's plugin-art-0.1.24.md). One block per theme: its drawing
// hooks, then its Theme record; then the registry and artForInstrument. Ids: PluginArtGeneratorIds.inc (1..127).
// LYNX's own procedural drawings only — never a copied, traced or extracted bitmap, logo or skin.

namespace artkit
{
// === Sakura: cherry blossoms at dusk — plum night, porcelain knobs, petals drifting through the name band ===========

inline void sakuraBranch (juce::Graphics& g, Rectangle<float> a)
{
    // the branch enters from the top edge, bows down across the free band under the header and splits in twigs
    const auto u = juce::jmin (a.getWidth() / 1085.0f, a.getHeight() / 770.0f) * 1.0f;
    const auto bark = Colour (0xff3e2426), barkLit = Colour (0xff6b4440);
    juce::Random rnd (1704);
    std::vector<Point<float>> tips;
    // one tapered limb from p0 to p2 bowing through p1
    auto limb = [&] (Point<float> p0, Point<float> p1, Point<float> p2, float w0, float w1) {
        juce::Path l, lit;
        constexpr int n = 18;
        std::vector<Point<float>> left, right;
        for (int i = 0; i <= n; ++i)
        {
            const auto t = (float) i / (float) n;
            const auto q = p0 * ((1 - t) * (1 - t)) + p1 * (2 * (1 - t) * t) + p2 * (t * t);
            const auto d = (p1 - p0) * (2 * (1 - t)) + (p2 - p1) * (2 * t);
            const auto len = juce::jmax (0.001f, d.getDistanceFromOrigin());
            const Point<float> nrm { -d.y / len, d.x / len };
            // gnarls: the width wobbles a little
            const auto w = (w0 + (w1 - w0) * t) * (1.0f + 0.12f * std::sin (t * 23.0f + p0.x));
            left.push_back (q + nrm * w * 0.5f);
            right.push_back (q - nrm * w * 0.5f);
        }
        l.startNewSubPath (left[0]);
        for (auto& p : left) l.lineTo (p);
        for (auto it = right.rbegin(); it != right.rend(); ++it) l.lineTo (*it);
        l.closeSubPath();
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillPath (l, juce::AffineTransform::translation (0.0f, 2.5f * u));
        g.setColour (bark);
        g.fillPath (l);
        // the lit upper edge of the bark
        lit.startNewSubPath (left[0]);
        for (auto& p : left) lit.lineTo (p);
        g.setColour (barkLit.withAlpha (0.8f));
        g.strokePath (lit, juce::PathStrokeType (juce::jmax (0.8f, 1.1f * u)));
        tips.push_back (p2);
    };
    auto P = [&] (float x, float y) { return Point<float> { a.getX() + x * a.getWidth(), a.getY() + y * a.getHeight() }; };
    limb (P (0.205f, -0.02f), P (0.27f, 0.13f), P (0.44f, 0.165f), 15.0f * u, 7.0f * u);
    limb (P (0.44f, 0.165f), P (0.53f, 0.17f), P (0.625f, 0.13f), 7.0f * u, 2.2f * u);
    limb (P (0.31f, 0.135f), P (0.33f, 0.215f), P (0.39f, 0.245f), 6.0f * u, 1.8f * u);
    limb (P (0.44f, 0.165f), P (0.47f, 0.225f), P (0.535f, 0.24f), 5.0f * u, 1.6f * u);
    limb (P (0.53f, 0.165f), P (0.56f, 0.10f), P (0.6f, 0.075f), 3.5f * u, 1.3f * u);
    limb (P (0.25f, 0.07f), P (0.20f, 0.10f), P (0.165f, 0.125f), 5.0f * u, 1.6f * u);
    limb (P (0.36f, 0.15f), P (0.39f, 0.10f), P (0.43f, 0.085f), 3.5f * u, 1.2f * u);
    // blossom clusters at the tips and along the limbs, buds between
    const auto pink = pal().accent2, deep = Colour (0xffff9dbb);
    std::vector<Point<float>> spots = tips;
    for (const auto& s : { P (0.30f, 0.12f), P (0.37f, 0.16f), P (0.48f, 0.18f), P (0.56f, 0.155f), P (0.42f, 0.2f), P (0.26f, 0.09f), P (0.6f, 0.11f),
                           P (0.34f, 0.2f), P (0.51f, 0.215f) })
        spots.push_back (s);
    for (const auto& s : spots)
    {
        const int n = 2 + rnd.nextInt (3);
        for (int i = 0; i < n; ++i)
        {
            const auto off = Point<float> ((rnd.nextFloat() - 0.5f) * 30.0f * u, (rnd.nextFloat() - 0.5f) * 22.0f * u);
            const auto r = (6.5f + rnd.nextFloat() * 5.5f) * u;
            if (! reduced) bloom (g, s + off, r * 3.2f, deep, 0.10f);
            blossom (g, s + off, r, rnd.nextFloat() * 1.3f, rnd.nextBool() ? pink : deep.interpolatedWith (pink, 0.6f), Colour (0xfffff0b8), 0.92f);
        }
        for (int i = 0; i < 2; ++i) // buds
        {
            const auto b = s + Point<float> ((rnd.nextFloat() - 0.5f) * 40.0f * u, (rnd.nextFloat() - 0.5f) * 26.0f * u);
            g.setColour (Colour (0xffe8577f).withAlpha (0.9f));
            g.fillEllipse (Rectangle<float> (4.0f * u, 5.0f * u).withCentre (b));
        }
    }
}

inline void canvasSakura (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.bgTop, 0.0f, a.getY(), p.bgBottom, 0.0f, a.getBottom(), false));
    g.fillRect (a);
    if (display) return;
    // dusk: a warm glow low on the horizon and a pale moon behind the branch
    g.setGradientFill (ColourGradient (Colour (0x30ff7a8a), a.getCentreX(), a.getBottom(), Colour (0x00ff7a8a), a.getCentreX(), a.getBottom() - a.getHeight() * 0.55f, false));
    g.fillRect (a);
    const auto moonC = Point<float> (a.getX() + a.getWidth() * 0.47f, a.getY() + a.getHeight() * 0.115f);
    const auto moonR = juce::jmin (a.getWidth(), a.getHeight()) * 0.07f;
    if (! reduced) bloom (g, moonC, moonR * 4.0f, Colour (0xffffe3ea), 0.15f);
    g.setGradientFill (ColourGradient (Colour (0xfffff4f2), moonC.x - moonR * 0.4f, moonC.y - moonR * 0.4f, Colour (0xffe9c9d2), moonC.x + moonR, moonC.y + moonR, true));
    g.setOpacity (0.78f);
    g.fillEllipse (Rectangle<float> (moonR * 2.0f, moonR * 2.0f).withCentre (moonC));
    g.setOpacity (1.0f);
    // far hills in mist along the bottom
    for (int layer = 0; layer < 2; ++layer)
    {
        juce::Path hill;
        const auto base = a.getBottom() - a.getHeight() * (0.06f + 0.05f * (float) layer);
        hill.startNewSubPath (a.getX(), a.getBottom());
        for (int i = 0; i <= 24; ++i)
        {
            const auto x = a.getX() + a.getWidth() * (float) i / 24.0f;
            const auto y = base - a.getHeight() * 0.035f * (std::sin ((float) i * 0.7f + (float) layer * 2.0f) * 0.6f + std::sin ((float) i * 0.23f) + 1.0f);
            hill.lineTo (x, y);
        }
        hill.lineTo (a.getRight(), a.getBottom());
        hill.closeSubPath();
        g.setColour (Colour (0xff3a1f3a).withAlpha (layer == 0 ? 0.45f : 0.6f));
        g.fillPath (hill);
    }
    sakuraBranch (g, a);
    // petals already on the ground
    juce::Random rnd (77);
    for (int i = 0; i < 26; ++i)
        petal (g, { a.getX() + rnd.nextFloat() * a.getWidth(), a.getBottom() - rnd.nextFloat() * a.getHeight() * 0.05f }, 5.0f + rnd.nextFloat() * 3.0f,
               rnd.nextFloat() * 6.28f, 0.35f + rnd.nextFloat() * 0.4f, p.accent2, 0.35f);
}

inline void sakuraWashi (juce::Graphics& g, Rectangle<float> a, float)
{
    juce::Random rnd ((juce::int64) (a.getWidth() * 31.0f + a.getHeight()));
    g.setColour (Colour (0xffffd9e4).withAlpha (0.035f));
    for (int i = 0; i < (int) (a.getWidth() * a.getHeight() / 900.0f); ++i)
    {
        const Point<float> s { a.getX() + rnd.nextFloat() * a.getWidth(), a.getY() + rnd.nextFloat() * a.getHeight() };
        const auto ang = rnd.nextFloat() * 6.28f, len = 4.0f + rnd.nextFloat() * 10.0f;
        g.drawLine ({ s, s + Point<float> (std::cos (ang), std::sin (ang)) * len }, 0.7f);
    }
}

inline void sakuraEmblem (juce::Graphics& g, Point<float> c, float r) { blossom (g, c, r, 0.3f, pal().accent2, Colour (0xfffff0b8), 1.0f); }
inline void sakuraTip (juce::Graphics& g, Point<float> tip, float t, float angle) { blossom (g, tip, t * 1.25f, angle, pal().accent2, Colour (0xfffff0b8), 1.0f); }

inline void sakuraMotion (juce::Graphics& g, const Motion& m)
{
    for (const auto& p : m.motes) // (they fade out as they reach the bottom of the band)
        petal (g, { p.x, p.y }, p.size, p.rot, 0.35f + 0.65f * std::abs (std::sin (m.t * 1.7f + p.phase)), m.p.accent2,
               0.75f * juce::jlimit (0.0f, 1.0f, (m.stage.getBottom() - p.y) / 30.0f));
}

inline const Theme& sakuraTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Sakura";
        x.base = Style::glow;
        x.pal = { Colour (0xff2b1731), Colour (0xff110a15), Colour (0xffff7aa5), Colour (0xffffd3e1), Colour (0xc4281b2c), Colour (0xdc170f1b),
                  Colour (0xffffa9c4), Colour (0xffffe6ee), Colour (0xff100810), Colour (0xfff8eef0), Colour (0xffd8bfc8), Colour (0xff3d2736),
                  Colour (0xffd8496f), Colour (0xff2a1c2a) };
        x.canvas = canvasSakura;
        x.sectionDeco = sakuraWashi;
        x.titleEmblem = sakuraEmblem;
        x.cap = Cap::porcelain;
        x.tipDeco = sakuraTip;
        x.thumbPointer = true;
        x.motes = 12;
        x.drift = Drift::fall;
        x.sizeMin = 6.0f; x.sizeRange = 5.0f;
        x.vxMin = 6.0f; x.vxRange = 10.0f;
        x.vyMin = 10.0f; x.vyRange = 12.0f;
        x.sway = 9.0f;
        x.reach = 0.75f;
        x.paintMotion = sakuraMotion;
        return x;
    }();
    return t;
}

// === Toxic Biohazard: a containment lab — hazard tape, rubber knobs with notches, bubbling green ==================

inline void canvasToxic (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.bgTop, 0.0f, a.getY(), p.bgBottom, 0.0f, a.getBottom(), false));
    g.fillRect (a);
    // a hex mesh, as on a containment floor
    const auto hs = 18.0f;
    juce::Path hex;
    for (auto row = 0; a.getY() + (float) row * hs * 0.866f < a.getBottom() + hs; ++row)
        for (auto col = 0; a.getX() + (float) col * hs * 1.5f < a.getRight() + hs; ++col)
        {
            const auto cx = a.getX() + (float) col * hs * 1.5f;
            const auto cy = a.getY() + (float) row * hs * 0.866f * 2.0f + ((col & 1) ? hs * 0.866f : 0.0f);
            for (int k = 0; k < 6; ++k)
            {
                const auto a0 = juce::MathConstants<float>::pi / 3.0f * (float) k, a1 = a0 + juce::MathConstants<float>::pi / 3.0f;
                hex.startNewSubPath (cx + hs * 0.5f * std::cos (a0), cy + hs * 0.5f * std::sin (a0));
                hex.lineTo (cx + hs * 0.5f * std::cos (a1), cy + hs * 0.5f * std::sin (a1));
            }
        }
    g.setColour (p.accent.withAlpha (0.045f));
    g.strokePath (hex, juce::PathStrokeType (1.0f));
    if (display) return;
    // the glow of something radioactive under the floor
    if (! reduced)
    {
        bloom (g, { a.getCentreX(), a.getBottom() + a.getHeight() * 0.1f }, a.getWidth() * 0.55f, p.accent, 0.20f);
        bloom (g, { a.getCentreX(), a.getCentreY() }, a.getHeight() * 0.5f, p.accent, 0.07f);
    }
    // the big trefoil watermark in the middle
    drawTrefoil (g, a.getCentre().translated (0.0f, a.getHeight() * 0.06f), juce::jmin (a.getWidth(), a.getHeight()) * 0.36f, p.accent, 0.05f);
    // slime oozing from the top edge
    juce::Random rnd (31);
    for (int i = 0; i < 14; ++i)
    {
        const auto x = a.getX() + a.getWidth() * (0.42f + 0.56f * rnd.nextFloat());
        const auto w = 5.0f + rnd.nextFloat() * 10.0f;
        const auto len = 6.0f + rnd.nextFloat() * rnd.nextFloat() * 34.0f;
        juce::Path d;
        d.startNewSubPath (x - w, a.getY());
        d.cubicTo (x - w * 0.4f, a.getY() + 2.0f, x - w * 0.35f, a.getY() + len * 0.7f, x - w * 0.3f, a.getY() + len);
        d.addCentredArc (x, a.getY() + len, w * 0.3f, w * 0.34f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, false);
        d.cubicTo (x + w * 0.35f, a.getY() + len * 0.7f, x + w * 0.4f, a.getY() + 2.0f, x + w, a.getY());
        d.closeSubPath();
        g.setGradientFill (ColourGradient (p.accent.withAlpha (0.55f), x, a.getY(), p.accent.darker (0.4f).withAlpha (0.5f), x, a.getY() + len, false));
        g.fillPath (d);
        g.setColour (Colours::white.withAlpha (0.25f));
        g.fillEllipse (Rectangle<float> (w * 0.18f, w * 0.22f).withCentre ({ x - w * 0.08f, a.getY() + len - w * 0.05f }));
    }
    g.setColour (p.accent.withAlpha (0.5f));
    g.fillRect (a.withHeight (2.0f));
    // hazard tape along the bottom edge
    hazardStripes (g, a.withTop (a.getBottom() - 7.0f), 0.85f);
}

inline void toxicHeader (juce::Graphics& g, Rectangle<float> a) { hazardStripes (g, a.withTop (a.getBottom() - 4.0f), 0.9f); }

inline void toxicNotch (juce::Graphics& g, Rectangle<float> a, float corner) // a hazard notch in the top left corner
{
    if (a.getWidth() <= 50.0f || a.getHeight() <= 30.0f) return;
    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle (a, corner);
    g.reduceClipRegion (clip);
    juce::Path tri;
    tri.addTriangle (a.getX(), a.getY(), a.getX() + 18.0f, a.getY(), a.getX(), a.getY() + 18.0f);
    g.reduceClipRegion (tri);
    hazardStripes (g, a.withSize (18.0f, 18.0f), 0.55f);
    g.restoreState();
}

inline void toxicEmblem (juce::Graphics& g, Point<float> c, float r) { drawTrefoil (g, c, r * 1.15f, pal().accent, 0.95f); }

inline void toxicMotion (juce::Graphics& g, const Motion& m)
{
    // the trefoil next to the name, breathing
    const auto& s = m.stage;
    const auto pulse = 0.5f + 0.5f * std::sin (m.t * 2.4f);
    const auto c = Point<float> (s.getX() + s.getHeight() * 0.5f, s.getCentreY());
    const auto r = s.getHeight() * 0.36f;
    if (! reduced) bloom (g, c, r * (2.2f + 0.5f * pulse), m.p.accent, 0.25f + 0.25f * pulse);
    drawTrefoil (g, c, r, m.p.accent.interpolatedWith (Colours::white, 0.15f * pulse), 0.85f);
    for (const auto& b : m.motes) // bubbles
    {
        g.setColour (m.p.accent.withAlpha (0.55f));
        g.drawEllipse (Rectangle<float> (b.size, b.size).withCentre ({ b.x, b.y }), 1.0f);
        g.setColour (Colours::white.withAlpha (0.4f));
        g.fillEllipse (Rectangle<float> (b.size * 0.3f, b.size * 0.3f).withCentre ({ b.x - b.size * 0.15f, b.y - b.size * 0.15f }));
    }
}

inline const Theme& toxicTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Toxic Biohazard";
        x.base = Style::neon;
        x.pal = { Colour (0xff101a10), Colour (0xff040704), Colour (0xff9dff3a), Colour (0xffffd60a), Colour (0xcc141d13), Colour (0xe409100a),
                  Colour (0xff7cff45), Colour (0xffdaffc4), Colour (0xff030603), Colour (0xff343a33), Colour (0xff0c0f0c), Colour (0xff1c2a19),
                  Colour (0xffc6ff6e), Colour (0xff121a11) };
        x.canvas = canvasToxic;
        x.headerDeco = toxicHeader;
        x.sectionDeco = toxicNotch;
        x.titleEmblem = toxicEmblem;
        x.cap = Cap::rubber;
        x.pointer = Pointer::glowLine;
        x.ticks = 9;
        x.boldTicks = true;
        x.arcGlow = 0.35f;
        x.arcGlowWidth = 3.2f;
        x.motes = 9;
        x.drift = Drift::rise;
        x.sizeMin = 2.0f; x.sizeRange = 4.0f;
        x.vyMin = -24.0f; x.vyRange = 14.0f;
        x.sway = 3.0f;
        x.stageRepaint = true;
        x.paintMotion = toxicMotion;
        return x;
    }();
    return t;
}

// === Sytrus: FM orbits — indigo space, glass orbs, electrons riding the arcs, a turning Lissajous ================

inline void canvasSytrus (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.bgTop, 0.0f, a.getY(), p.bgBottom, 0.0f, a.getBottom(), false));
    g.fillRect (a);
    if (! reduced)
    {
        bloom (g, { a.getX() + a.getWidth() * 0.78f, a.getY() + a.getHeight() * 0.18f }, a.getWidth() * 0.42f, p.accent, 0.16f);
        bloom (g, { a.getX() + a.getWidth() * 0.15f, a.getBottom() - a.getHeight() * 0.1f }, a.getWidth() * 0.38f, p.accent2, 0.09f);
    }
    if (display) return;
    // operators: six orbits around a nucleus, like an FM matrix seen as an atom
    const auto c = Point<float> (a.getX() + a.getWidth() * 0.78f, a.getY() + a.getHeight() * 0.18f);
    const auto R = juce::jmin (a.getWidth(), a.getHeight()) * 0.30f;
    for (int i = 0; i < 6; ++i)
    {
        juce::Path orbit;
        orbit.addEllipse (Rectangle<float> (R * 2.0f, R * 0.62f).withCentre ({ 0.0f, 0.0f }));
        orbit.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi * (float) i / 6.0f).translated (c.x, c.y));
        g.setColour ((i % 2 == 0 ? p.accent : p.accent2).withAlpha (0.22f));
        g.strokePath (orbit, juce::PathStrokeType (1.3f));
        // an operator riding each orbit
        const auto ang = 0.9f + 1.7f * (float) i;
        const Point<float> op = c + Point<float> (std::cos (ang) * R, std::sin (ang) * R * 0.31f).rotatedAboutOrigin (juce::MathConstants<float>::pi * (float) i / 6.0f);
        if (! reduced) bloom (g, op, 9.0f, i % 2 == 0 ? p.accent : p.accent2, 0.5f);
        g.setColour (Colours::white.withAlpha (0.8f));
        g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre (op));
    }
    // FM figures: a few lissajous curves fading across the panel
    for (int i = 0; i < 3; ++i)
    {
        juce::Path l;
        lissajous (l, { a.getX() + a.getWidth() * (0.2f + 0.3f * (float) i), a.getY() + a.getHeight() * 0.55f }, a.getWidth() * 0.16f, a.getHeight() * 0.3f,
                   (float) (2 + i), (float) (3 + i), 0.6f * (float) i);
        g.setColour ((i == 1 ? p.accent2 : p.accent).withAlpha (0.07f));
        g.strokePath (l, juce::PathStrokeType (1.0f));
    }
    stars (g, a, 70, 9, 0.35f);
}

inline void sytrusMotion (juce::Graphics& g, const Motion& m)
{
    if (m.stage == m.area) return;
    juce::Path l;
    lissajous (l, m.stage.getCentre(), m.stage.getWidth() * 0.42f, m.stage.getHeight() * 0.38f, 3.0f, 2.0f, m.t * 0.5f, 240);
    g.setColour (m.p.accent2.withAlpha (0.18f));
    g.strokePath (l, juce::PathStrokeType (2.6f));
    g.setColour (m.p.accent.withAlpha (0.55f));
    g.strokePath (l, juce::PathStrokeType (1.0f));
}

inline const Theme& sytrusTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Sytrus";
        x.base = Style::neon;
        x.pal = { Colour (0xff1a1642), Colour (0xff08071a), Colour (0xffa67cff), Colour (0xff5ce1ff), Colour (0xc41d1a44), Colour (0xdc0f0e27),
                  Colour (0xffb9a0ff), Colour (0xffece4ff), Colour (0xff06051a), Colour (0xff4a4390), Colour (0xff100e2c), Colour (0xff27234e),
                  Colour (0xffeaf9ff), Colour (0xff191636) };
        x.canvas = canvasSytrus;
        x.twoTone = true;
        x.cap = Cap::orb;
        x.pointer = Pointer::electron;
        x.stageRepaint = true;
        x.paintMotion = sytrusMotion;
        return x;
    }();
    return t;
}

// === 3x Osc: a vintage lab bench — brushed plates with screws, bakelite knobs with a 0-10 scale, an amber scope ===

inline void canvasThreeOsc (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.bgTop, 0.0f, a.getY(), p.bgBottom, 0.0f, a.getBottom(), false));
    g.fillRect (a);
    styles::brushed (g, a, 0.035f);
    if (display) return;
    // a lamp lighting the faceplate from the top left
    if (! reduced) bloom (g, { a.getX() + a.getWidth() * 0.2f, a.getY() }, a.getWidth() * 0.6f, Colour (0xfffff1d6), 0.06f);
    // three oscillator traces printed on the plate, one per oscillator row, fading to the right (sine, saw, square)
    for (int row = 0; row < 3; ++row)
    {
        juce::Path w;
        const auto y = a.getY() + a.getHeight() * (0.2f + 0.24f * (float) row);
        const auto amp = a.getHeight() * 0.045f;
        const auto x0 = a.getX() + a.getWidth() * 0.03f, x1 = a.getRight() - a.getWidth() * 0.03f;
        const int n = 220;
        for (int i = 0; i <= n; ++i)
        {
            const auto t = (float) i / (float) n;
            const auto ph = std::fmod (t * 7.0f, 1.0f);
            const auto v = row == 0 ? std::sin (ph * juce::MathConstants<float>::twoPi) : row == 1 ? 1.0f - 2.0f * ph : (ph < 0.5f ? 1.0f : -1.0f);
            const Point<float> q { x0 + (x1 - x0) * t, y - amp * v };
            if (i == 0) w.startNewSubPath (q);
            else w.lineTo (q);
        }
        g.setColour (p.accent.withAlpha (0.07f));
        g.strokePath (w, juce::PathStrokeType (1.4f));
    }
    // vent slots along the bottom
    g.setColour (Colours::black.withAlpha (0.35f));
    for (auto x = a.getX() + 18.0f; x < a.getRight() - 18.0f; x += 9.0f)
        g.fillRoundedRectangle (Rectangle<float> (x, a.getBottom() - 6.0f, 4.0f, 3.0f), 1.5f);
}

inline void threeOscScrews (juce::Graphics& g, Rectangle<float> a, float) // a screwed-on plate
{
    if (a.getWidth() > 60.0f && a.getHeight() > 40.0f)
        for (const auto& c : { a.getTopLeft().translated (6.0f, 6.0f), a.getTopRight().translated (-6.0f, 6.0f), a.getBottomLeft().translated (6.0f, -6.0f),
                               a.getBottomRight().translated (-6.0f, -6.0f) })
            styles::screw (g, c, 5.0f);
}

inline void threeOscLamp (juce::Graphics& g, Point<float> c, float r)
{
    g.setColour (pal().accent);
    g.fillEllipse (Rectangle<float> (r * 1.2f, r * 1.2f).withCentre (c));
    g.setColour (Colours::white.withAlpha (0.6f));
    g.fillEllipse (Rectangle<float> (r * 0.45f, r * 0.45f).withCentre (c.translated (-r * 0.15f, -r * 0.15f)));
}

inline void threeOscMotion (juce::Graphics& g, const Motion& m)
{
    // a little amber phosphor scope next to the name: the three oscillators' sum scrolling by
    if (m.stage == m.area) return;
    const auto scr = m.stage.reduced (2.0f);
    g.setColour (Colour (0xff050604));
    g.fillRoundedRectangle (scr, 6.0f);
    g.setColour (m.p.accent.withAlpha (0.10f));
    for (int i = 1; i < 6; ++i)
        g.drawVerticalLine ((int) (scr.getX() + scr.getWidth() * (float) i / 6.0f), scr.getY() + 3.0f, scr.getBottom() - 3.0f);
    g.drawHorizontalLine ((int) scr.getCentreY(), scr.getX() + 3.0f, scr.getRight() - 3.0f);
    juce::Path tr;
    const int n = 120;
    for (int i = 0; i <= n; ++i)
    {
        const auto u = (float) i / (float) n;
        const auto ph = u * 2.0f + m.t * 0.35f;
        const auto sine = std::sin (ph * juce::MathConstants<float>::twoPi);
        const auto saw = 1.0f - 2.0f * (ph * 2.0f - std::floor (ph * 2.0f));
        const auto sq = std::fmod (ph * 0.5f, 1.0f) < 0.5f ? 1.0f : -1.0f;
        const auto v = (sine * 0.5f + saw * 0.25f + sq * 0.2f) * 0.8f;
        const Point<float> q { scr.getX() + 4.0f + (scr.getWidth() - 8.0f) * u, scr.getCentreY() - v * (scr.getHeight() * 0.42f) };
        if (i == 0) tr.startNewSubPath (q);
        else tr.lineTo (q);
    }
    g.saveState();
    g.reduceClipRegion (scr.toNearestInt());
    g.setColour (m.p.accent.withAlpha (0.25f));
    g.strokePath (tr, juce::PathStrokeType (4.0f));
    g.setColour (m.p.accent2);
    g.strokePath (tr, juce::PathStrokeType (1.3f));
    g.restoreState();
    g.setColour (Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (scr, 6.0f, 1.0f);
}

inline const Theme& threeOscTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "3x Osc";
        x.base = Style::hardware;
        x.pal = { Colour (0xff3a3c40), Colour (0xff1d1e21), Colour (0xffffa53d), Colour (0xffffd28a), Colour (0xd42d2f33), Colour (0xe61f2023),
                  Colour (0xff9a9da3), Colour (0xfff1e7cf), Colour (0xff0b0c0a), Colour (0xff2e2e2e), Colour (0xff0d0d0d), Colour (0xff45474c),
                  Colour (0xfff1e7cf), Colour (0xff232427) };
        x.canvas = canvasThreeOsc;
        x.sectionDeco = threeOscScrews;
        x.titleEmblem = threeOscLamp;
        x.cap = Cap::bakelite;
        x.pointerFrom = 0.15f;
        x.ticks = 11;
        x.buttTrack = true;
        x.arcGlow = 0.0f;
        x.knobScale = 1.04f;
        x.thumbPointer = true;
        x.stageRepaint = true;
        x.paintMotion = threeOscMotion;
        return x;
    }();
    return t;
}

// === the registry =================================================================================================

const Theme* generatorTheme (Art a)
{
    switch (a)
    {
        case Art::sakura: return &sakuraTheme();
        case Art::toxic: return &toxicTheme();
        case Art::sytrus: return &sytrusTheme();
        case Art::threeOsc: return &threeOscTheme();
        default: return nullptr;
    }
}
} // namespace artkit

Art artForInstrument (InstrumentKind kind)
{
    switch (kind)
    {
        case InstrumentKind::sakura: return Art::sakura;
        case InstrumentKind::toxic: return Art::toxic;
        case InstrumentKind::sytrus: return Art::sytrus;
        case InstrumentKind::threeosc: return Art::threeOsc;
        default: return Art::none;
    }
}

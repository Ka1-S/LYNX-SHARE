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

// --- shared by the generators' themes -------------------------------------------------------------------------------

inline void fillBackdrop (juce::Graphics& g, Rectangle<float> a)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.bgTop, 0.0f, a.getY(), p.bgBottom, 0.0f, a.getBottom(), false));
    g.fillRect (a);
}

// a path through n+1 points of f (u in 0..1) → (x, y)
template <typename F>
inline juce::Path curve (int n, F&& f)
{
    juce::Path path;
    for (int i = 0; i <= n; ++i)
    {
        const auto q = f ((float) i / (float) n);
        if (i == 0) path.startNewSubPath (q);
        else path.lineTo (q);
    }
    return path;
}

// a cap turned on a lathe: concentric marks, a bright bevel (Harmor, Kepler Exo)
inline void machinedCap (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.capTop.brighter (0.1f + hover * 0.15f), c.x, cap.getY(), p.capBottom, c.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    if (fx)
    {
        for (int i = 1; i < 5; ++i)
        {
            g.setColour (Colours::white.withAlpha (i % 2 == 0 ? 0.05f : 0.025f));
            g.drawEllipse (cap.reduced (capR * 0.18f * (float) i), 0.8f);
        }
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.35f), c.x, cap.getY(), Colours::white.withAlpha (0.0f), c.x, c.y, false));
        g.drawEllipse (cap.reduced (0.6f), 1.1f);
    }
}

// === Harmor: spectral resynthesis — graphite, a heat-coloured spectrogram, machined knobs ===========================

inline Colour heat (float v) // 0..1 → ember red, amber, pale yellow
{
    const auto& p = pal();
    return v < 0.5f ? Colour (0xffb3261e).interpolatedWith (p.accent, v * 2.0f) : p.accent.interpolatedWith (Colour (0xfffff1b8), (v - 0.5f) * 2.0f);
}

inline void canvasHarmor (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (! reduced) bloom (g, { a.getRight() - a.getWidth() * 0.2f, a.getY() + a.getHeight() * 0.12f }, a.getWidth() * 0.4f, p.accent, 0.10f);
    if (display) return;
    // a spectrogram: time runs right, partials stack up, each column a frame of a resynthesised sound
    const auto sp = Rectangle<float> (a.getX() + a.getWidth() * 0.02f, a.getY() + 4.0f, a.getWidth() * 0.96f, a.getHeight() * 0.55f);
    juce::Random rnd (34);
    const int cols = 96, rows = 36;
    const auto cw = sp.getWidth() / (float) cols, rh = sp.getHeight() / (float) rows;
    for (int x = 0; x < cols; ++x)
    {
        const auto u = (float) x / (float) cols;
        const auto env = std::exp (-u * 2.2f) * (0.75f + 0.25f * std::sin (u * 19.0f));
        const auto fade = juce::jmin (1.0f, u * 6.0f) * (1.0f - u * 0.6f);
        for (int h = 1; h <= rows; ++h)
        {
            const auto v = juce::jmin (1.0f, 1.3f * env * std::pow ((float) h, -0.45f) * (0.7f + 0.3f * rnd.nextFloat()));
            if (v < 0.08f) continue;
            g.setColour (heat (v).withAlpha ((0.18f + 0.6f * v) * fade));
            g.fillRect (Rectangle<float> (sp.getX() + (float) x * cw, sp.getBottom() - (float) h * rh, cw - 0.6f, rh * 0.55f));
        }
    }
    // the harmonic ladder on the left: a sawtooth's partials, 1/n
    for (int h = 1; h <= 24; ++h)
    {
        const auto len = a.getWidth() * 0.22f / (float) h;
        g.setColour (p.accent2.withAlpha (0.10f));
        g.fillRect (Rectangle<float> (a.getX() + 10.0f, a.getBottom() - 10.0f - (float) h * 5.0f, len, 1.2f));
    }
}

inline void harmorEmblem (juce::Graphics& g, Point<float> c, float r)
{
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (heat (1.0f - 0.3f * (float) i));
        g.fillRoundedRectangle (Rectangle<float> (r * (2.0f - 0.55f * (float) i), r * 0.38f).withCentre ({ c.x - r * 0.275f * (float) i, c.y + r * 0.6f - r * 0.6f * (float) i }), 1.0f);
    }
}

inline void harmorMotion (juce::Graphics& g, const Motion& m)
{
    // a live spectrum next to the name: partials breathing
    if (m.stage == m.area) return;
    const auto s = m.stage.reduced (4.0f);
    const int n = 22;
    const auto bw = s.getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const auto h = (float) (i + 1);
        const auto v = juce::jlimit (0.05f, 1.0f, std::pow (h, -0.55f) * (0.65f + 0.35f * std::sin (m.t * (1.1f + 0.13f * h) + h)));
        const auto bar = Rectangle<float> (s.getX() + (float) i * bw + 1.0f, s.getBottom() - v * s.getHeight(), bw - 2.0f, v * s.getHeight());
        g.setColour (heat (v).withAlpha (0.55f));
        g.fillRect (bar);
    }
}

inline const Theme& harmorTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Harmor";
        x.base = Style::neon;
        x.pal = { Colour (0xff1d1f25), Colour (0xff0a0b0e), Colour (0xffffa62b), Colour (0xff4fd2ff), Colour (0x941e2027), Colour (0xb40e0f13),
                  Colour (0xff6d7380), Colour (0xffeef0f4), Colour (0x9c060709), Colour (0xff4a4f5a), Colour (0xff17191e), Colour (0xff2a2d35),
                  Colour (0xfffff4e0), Colour (0xff1c1e24) }; // (translucent wells: Harmor's big display areas show the spectrogram)
        x.canvas = canvasHarmor;
        x.titleEmblem = harmorEmblem;
        x.drawCap = machinedCap;
        x.arcGlow = 0.28f;
        x.stageRepaint = true;
        x.paintMotion = harmorMotion;
        x.hz = 12;
        return x;
    }();
    return t;
}

// === Harmless: a harmonic comb in clear water — ice blue and lime, glass knobs ======================================

inline void canvasHarmless (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (! reduced)
    {
        bloom (g, { a.getX() + a.getWidth() * 0.12f, a.getY() + a.getHeight() * 0.1f }, a.getWidth() * 0.35f, p.accent, 0.10f);
        bloom (g, { a.getRight() - a.getWidth() * 0.1f, a.getBottom() }, a.getWidth() * 0.35f, p.accent2, 0.06f);
    }
    if (display) return;
    // the comb: evenly spaced harmonics under a smooth filter curve (the additive recipe in one picture)
    const auto base = a.getBottom() - a.getHeight() * 0.06f;
    const auto top = a.getY() + a.getHeight() * 0.3f;
    const int n = 48;
    const auto x0 = a.getX() + a.getWidth() * 0.04f, x1 = a.getRight() - a.getWidth() * 0.04f;
    auto filter = [&] (float u) { return std::exp (-std::pow ((u - 0.22f) / 0.18f, 2.0f)) * 0.9f + 0.25f * std::exp (-u * 3.0f) + 0.12f * std::exp (-std::pow ((u - 0.62f) / 0.07f, 2.0f)); };
    for (int i = 0; i < n; ++i)
    {
        const auto u = (float) i / (float) (n - 1);
        const auto x = x0 + (x1 - x0) * u;
        const auto v = filter (u);
        g.setGradientFill (ColourGradient (p.accent.withAlpha (0.20f * v + 0.03f), x, base - v * (base - top), p.accent.withAlpha (0.0f), x, base, false));
        g.fillRect (Rectangle<float> (x - 1.0f, base - v * (base - top), 2.0f, v * (base - top)));
    }
    const auto env = curve (160, [&] (float u) { return Point<float> (x0 + (x1 - x0) * u, base - filter (u) * (base - top)); });
    g.setColour (p.accent2.withAlpha (0.22f));
    g.strokePath (env, stroke (1.4f));
    // ripples on the surface
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (p.accent.withAlpha (0.05f));
        g.drawEllipse (Rectangle<float> (a.getWidth() * (0.2f + 0.12f * (float) i), a.getHeight() * (0.05f + 0.02f * (float) i))
                           .withCentre ({ a.getX() + a.getWidth() * 0.22f, a.getY() + a.getHeight() * 0.1f }), 1.0f);
    }
}

inline void harmlessEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    for (int i = 0; i < 4; ++i)
    {
        const auto h = r * 2.0f / (float) (i + 1) * 0.9f;
        g.setColour (i == 0 ? p.accent2 : p.accent);
        g.fillRect (Rectangle<float> (c.x - r + (float) i * r * 0.6f, c.y + r * 0.9f - h, r * 0.32f, h));
    }
}

inline const Theme& harmlessTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Harmless";
        x.base = Style::glow;
        x.pal = { Colour (0xff132230), Colour (0xff070e15), Colour (0xff7fe3ff), Colour (0xffc4f27a), Colour (0xc8152434), Colour (0xe00b141d),
                  Colour (0xff6fa8c6), Colour (0xffeaf8ff), Colour (0xff050a10), Colour (0xffbfe9fb), Colour (0xff3b6d86), Colour (0xff22384a),
                  Colour (0xff0d2230), Colour (0xff172a3a) };
        x.canvas = canvasHarmless;
        x.titleEmblem = harmlessEmblem;
        x.cap = Cap::orb;
        x.thumbPointer = true;
        return x;
    }();
    return t;
}

// === GMS: a groove machine — warm black, orange step pads, teal display, a running step light =======================

inline void canvasGms (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    styles::brushed (g, a, 0.02f);
    if (! reduced) bloom (g, { a.getRight() - a.getWidth() * 0.18f, a.getY() + a.getHeight() * 0.05f }, a.getWidth() * 0.35f, p.accent, 0.08f);
    if (display) return;
    // a row of sixteen step pads printed along the top, a groove lit in them
    static const bool groove[16] { true, false, false, true, false, false, true, false, true, false, true, false, false, true, false, true };
    const auto row = Rectangle<float> (a.getX() + a.getWidth() * 0.42f, a.getY() + 10.0f, a.getWidth() * 0.55f, juce::jmin (26.0f, a.getHeight() * 0.06f));
    const auto pw = row.getWidth() / 16.0f;
    for (int i = 0; i < 16; ++i)
    {
        const auto pad = Rectangle<float> (row.getX() + (float) i * pw + 2.0f, row.getY(), pw - 4.0f, row.getHeight()).reduced (0.0f, 2.0f);
        g.setColour ((groove[i] ? p.accent : p.plate).withAlpha (groove[i] ? 0.22f : 0.35f));
        g.fillRoundedRectangle (pad, 2.0f);
        if (i % 4 == 0)
        {
            g.setColour (p.accent2.withAlpha (0.25f));
            g.fillRect (pad.withY (pad.getBottom() + 3.0f).withHeight (1.5f));
        }
    }
    // a groove's waveform under it, teal on the dark
    const auto wave = curve (300, [&] (float u) {
        const auto beat = std::fmod (u * 8.0f, 1.0f);
        return Point<float> (a.getX() + a.getWidth() * u, a.getBottom() - a.getHeight() * 0.08f - std::sin (u * 180.0f) * std::exp (-beat * 6.0f) * a.getHeight() * 0.05f);
    });
    g.setColour (p.accent2.withAlpha (0.10f));
    g.strokePath (wave, juce::PathStrokeType (1.0f));
}

inline void consoleCap (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx) // a mixer-console cap with a coloured index ring
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.capTop.brighter (0.15f + hover * 0.1f), c.x, cap.getY(), p.capBottom, c.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setColour (p.accent.withAlpha (0.55f));
    g.drawEllipse (cap.reduced (capR * 0.22f), juce::jmax (1.0f, capR * 0.08f));
    if (fx)
    {
        g.setColour (Colours::white.withAlpha (0.08f));
        g.fillEllipse (cap.reduced (capR * 0.35f).withTrimmedBottom (capR * 0.5f));
    }
}

inline void gmsEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (i == 1 ? p.accent2 : (i == 2 ? p.plate.brighter (0.4f) : p.accent));
        g.fillRoundedRectangle (Rectangle<float> (r * 0.85f, r * 0.85f).withCentre ({ c.x + (i % 2 == 0 ? -0.5f : 0.5f) * r, c.y + (i < 2 ? -0.5f : 0.5f) * r }), 1.0f);
    }
}

inline void gmsMotion (juce::Graphics& g, const Motion& m)
{
    // sixteen steps in the stage with the playing step running through them
    if (m.stage == m.area) return;
    const auto s = m.stage.reduced (4.0f, juce::jmax (4.0f, m.stage.getHeight() * 0.3f));
    const auto pw = s.getWidth() / 16.0f;
    const auto at = (int) std::fmod (m.t * 4.0f, 16.0f); // 120 bpm sixteenths at half speed
    for (int i = 0; i < 16; ++i)
    {
        const auto pad = Rectangle<float> (s.getX() + (float) i * pw + 1.5f, s.getY(), pw - 3.0f, s.getHeight());
        g.setColour (i == at ? m.p.accent : (i % 4 == 0 ? m.p.plate.brighter (0.5f) : m.p.plate.brighter (0.2f)));
        g.fillRoundedRectangle (pad, 2.0f);
    }
}

inline const Theme& gmsTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "GMS";
        x.base = Style::hardware;
        x.pal = { Colour (0xff221d19), Colour (0xff0d0b0a), Colour (0xffff7a1a), Colour (0xff2fd6c3), Colour (0xd2241f1b), Colour (0xe6120f0d),
                  Colour (0xff7a6a5c), Colour (0xfff6ece2), Colour (0xff070605), Colour (0xff3c3631), Colour (0xff171412), Colour (0xff2f2925),
                  Colour (0xfffff0e0), Colour (0xff2a2420) };
        x.canvas = canvasGms;
        x.titleEmblem = gmsEmblem;
        x.drawCap = consoleCap;
        x.arcGlow = 0.18f;
        x.stageRepaint = true;
        x.paintMotion = gmsMotion;
        x.hz = 8;
        return x;
    }();
    return t;
}

// === Morphine: shapes melting into each other — violet and aqua, a waveform morphing ================================

inline float morphShape (float u, float k) // k = 0: a round sine, 1: a sharp spiky pulse train
{
    const auto ph = u * juce::MathConstants<float>::twoPi * 3.0f;
    const auto soft = std::sin (ph);
    const auto sharp = std::pow (std::abs (std::sin (ph * 0.5f)), 9.0f) * 2.0f - 0.6f;
    return soft * (1.0f - k) + sharp * k;
}

inline void canvasMorphine (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (! reduced)
    {
        bloom (g, { a.getX() + a.getWidth() * 0.25f, a.getY() + a.getHeight() * 0.25f }, a.getWidth() * 0.4f, p.accent, 0.10f);
        bloom (g, { a.getRight() - a.getWidth() * 0.2f, a.getBottom() - a.getHeight() * 0.2f }, a.getWidth() * 0.35f, p.accent2, 0.07f);
    }
    if (display) return;
    // a fan of in-between shapes: the morph from a sine to a pulse train, across the panel
    const auto midY = a.getY() + a.getHeight() * 0.18f;
    const auto amp = a.getHeight() * 0.07f;
    for (int i = 0; i <= 8; ++i)
    {
        const auto k = (float) i / 8.0f;
        const auto path = curve (220, [&] (float u) { return Point<float> (a.getX() + a.getWidth() * u, midY + (float) i * a.getHeight() * 0.012f - amp * morphShape (u, k)); });
        g.setColour (p.accent.interpolatedWith (p.accent2, k).withAlpha (0.05f + 0.06f * (i == 0 || i == 8 ? 1.0f : 0.0f)));
        g.strokePath (path, juce::PathStrokeType (i == 0 || i == 8 ? 1.4f : 1.0f));
    }
}

inline void morphineEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    const auto path = curve (60, [&] (float u) { return Point<float> (c.x - r + 2.0f * r * u, c.y - r * 0.7f * morphShape (u * 0.34f, u)); });
    g.setGradientFill (ColourGradient (p.accent, c.x - r, c.y, p.accent2, c.x + r, c.y, false));
    g.strokePath (path, stroke (1.6f));
}

inline void morphineMotion (juce::Graphics& g, const Motion& m)
{
    if (m.stage == m.area) return;
    const auto s = m.stage.reduced (4.0f);
    const auto k = 0.5f + 0.5f * std::sin (m.t * 0.6f);
    const auto path = curve (120, [&] (float u) { return Point<float> (s.getX() + s.getWidth() * u, s.getCentreY() - s.getHeight() * 0.36f * morphShape (u * 0.67f, k)); });
    g.setColour (m.p.accent.interpolatedWith (m.p.accent2, k).withAlpha (0.25f));
    g.strokePath (path, juce::PathStrokeType (4.0f));
    g.setColour (m.p.accent.interpolatedWith (m.p.accent2, k));
    g.strokePath (path, juce::PathStrokeType (1.4f));
}

inline const Theme& morphineTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Morphine";
        x.base = Style::neon;
        x.pal = { Colour (0xff1c1236), Colour (0xff080512), Colour (0xffc86bff), Colour (0xff39e6c8), Colour (0xc41e1438), Colour (0xdc0e0920),
                  Colour (0xffa184d8), Colour (0xfff0e8ff), Colour (0xff05030c), Colour (0xff5b4296), Colour (0xff160f2c), Colour (0xff2a2048),
                  Colour (0xfff6f0ff), Colour (0xff1a1232) };
        x.canvas = canvasMorphine;
        x.titleEmblem = morphineEmblem;
        x.twoTone = true;
        x.cap = Cap::orb;
        x.pointer = Pointer::glowLine;
        x.stageRepaint = true;
        x.paintMotion = morphineMotion;
        x.hz = 15;
        return x;
    }();
    return t;
}

// === Ogun: forged iron — hammered dark metal, a glowing seam, sparks rising =========================================

inline void canvasOgun (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    // hammer marks: soft dents, lit from the top
    juce::Random rnd (37);
    const auto dents = (int) juce::jmin (420.0f, a.getWidth() * a.getHeight() / 1400.0f);
    for (int i = 0; i < dents; ++i)
    {
        const Point<float> c { a.getX() + rnd.nextFloat() * a.getWidth(), a.getY() + rnd.nextFloat() * a.getHeight() };
        const auto r = 6.0f + rnd.nextFloat() * 14.0f;
        const auto d = Rectangle<float> (r * 2.0f, r * 1.6f).withCentre (c);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.018f), c.x, d.getY(), Colours::black.withAlpha (0.05f), c.x, d.getBottom(), false));
        g.fillEllipse (d);
    }
    if (display) return;
    // a hot seam: a crack glowing from inside the metal, across the lower panel
    const auto seam = curve (90, [&] (float u) {
        return Point<float> (a.getX() + a.getWidth() * u, a.getBottom() - a.getHeight() * 0.1f + a.getHeight() * 0.03f * std::sin (u * 23.0f) + a.getHeight() * 0.015f * std::sin (u * 71.0f));
    });
    if (! reduced)
    {
        g.setColour (p.accent.withAlpha (0.10f));
        g.strokePath (seam, stroke (9.0f));
        bloom (g, { a.getCentreX(), a.getBottom() }, a.getWidth() * 0.45f, p.accent, 0.10f);
    }
    g.setColour (p.accent2.withAlpha (0.35f));
    g.strokePath (seam, stroke (1.2f));
}

inline void forgedCap (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx) // an octagonal forged nut, hot at the rim
{
    const auto& p = pal();
    juce::Path oct;
    for (int i = 0; i < 8; ++i)
    {
        const auto ang = juce::MathConstants<float>::twoPi * ((float) i + 0.5f) / 8.0f;
        const Point<float> q { c.x + std::sin (ang) * capR, c.y - std::cos (ang) * capR };
        if (i == 0) oct.startNewSubPath (q);
        else oct.lineTo (q);
    }
    oct.closeSubPath();
    if (fx && ! reduced) bloom (g, c, capR * 1.5f, p.accent, 0.12f + 0.15f * hover);
    g.setGradientFill (ColourGradient (p.capTop.brighter (0.2f + hover * 0.15f), c.x - capR * 0.5f, cap.getY(), p.capBottom, c.x + capR * 0.5f, cap.getBottom(), false));
    g.fillPath (oct);
    g.setColour (p.accent.withAlpha (0.35f + 0.25f * hover));
    g.strokePath (oct, juce::PathStrokeType (1.0f));
    if (fx)
    {
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillEllipse (cap.reduced (capR * 0.55f));
    }
}

inline void ogunEmblem (juce::Graphics& g, Point<float> c, float r) // an anvil
{
    const auto& p = pal();
    juce::Path anvil;
    anvil.startNewSubPath (c.x - r * 1.2f, c.y - r * 0.55f);
    anvil.lineTo (c.x + r * 0.9f, c.y - r * 0.55f);
    anvil.quadraticTo (c.x + r * 1.3f, c.y - r * 0.5f, c.x + r * 1.35f, c.y - r * 0.25f);
    anvil.lineTo (c.x + r * 0.5f, c.y - r * 0.1f);
    anvil.lineTo (c.x + r * 0.35f, c.y + r * 0.35f);
    anvil.lineTo (c.x + r * 0.8f, c.y + r * 0.8f);
    anvil.lineTo (c.x - r * 0.8f, c.y + r * 0.8f);
    anvil.lineTo (c.x - r * 0.35f, c.y + r * 0.35f);
    anvil.lineTo (c.x - r * 0.55f, c.y - r * 0.1f);
    anvil.lineTo (c.x - r * 1.2f, c.y - r * 0.3f);
    anvil.closeSubPath();
    g.setColour (p.accent);
    g.fillPath (anvil);
}

inline void ogunMotion (juce::Graphics& g, const Motion& m)
{
    for (const auto& s : m.motes) // sparks: hot dots that cool as they rise
    {
        const auto life = juce::jlimit (0.0f, 1.0f, (s.y - m.stage.getY()) / juce::jmax (1.0f, m.stage.getHeight()));
        const auto col = m.p.accent.interpolatedWith (m.p.accent2, life);
        if (! reduced) bloom (g, { s.x, s.y }, s.size * 2.5f, col, 0.35f * life);
        g.setColour (col.withAlpha (0.4f + 0.6f * life));
        g.fillEllipse (Rectangle<float> (s.size, s.size).withCentre ({ s.x, s.y }));
    }
}

inline const Theme& ogunTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Ogun";
        x.base = Style::hardware;
        x.pal = { Colour (0xff2a211c), Colour (0xff0e0a08), Colour (0xffff6a1f), Colour (0xffffc24a), Colour (0xcc2a221d), Colour (0xe2120e0b),
                  Colour (0xff8a6a55), Colour (0xfff8e6d6), Colour (0xff080605), Colour (0xff4a423d), Colour (0xff1a1614), Colour (0xff332a25),
                  Colour (0xffffe2c4), Colour (0xff2a221e) };
        x.canvas = canvasOgun;
        x.titleEmblem = ogunEmblem;
        x.drawCap = forgedCap;
        x.ticks = 9;
        x.boldTicks = true;
        x.arcGlow = 0.3f;
        x.arcGlowWidth = 2.8f;
        x.motes = 10;
        x.drift = Drift::rise;
        x.sizeMin = 1.5f; x.sizeRange = 1.8f;
        x.vyMin = -30.0f; x.vyRange = 16.0f;
        x.sway = 6.0f;
        x.reach = 2.5f;
        x.paintMotion = ogunMotion;
        x.hz = 15;
        return x;
    }();
    return t;
}

// === PoiZone: venom — a glossy violet drop, drips from the top edge, acid highlights ================================

inline juce::Path dropShape (Point<float> c, float r) // a drop, point up
{
    juce::Path d;
    d.startNewSubPath (c.x, c.y - r * 1.6f);
    d.cubicTo (c.x + r * 0.35f, c.y - r * 0.9f, c.x + r, c.y - r * 0.35f, c.x + r, c.y + r * 0.15f);
    d.cubicTo (c.x + r, c.y + r * 0.75f, c.x + r * 0.5f, c.y + r, c.x, c.y + r);
    d.cubicTo (c.x - r * 0.5f, c.y + r, c.x - r, c.y + r * 0.75f, c.x - r, c.y + r * 0.15f);
    d.cubicTo (c.x - r, c.y - r * 0.35f, c.x - r * 0.35f, c.y - r * 0.9f, c.x, c.y - r * 1.6f);
    d.closeSubPath();
    return d;
}

inline void canvasPoizone (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (! reduced) bloom (g, { a.getRight() - a.getWidth() * 0.12f, a.getY() + a.getHeight() * 0.2f }, a.getWidth() * 0.3f, p.accent, 0.12f);
    if (display) return;
    // drips running down from the top edge
    juce::Random rnd (44);
    for (int i = 0; i < 18; ++i)
    {
        const auto x = a.getX() + rnd.nextFloat() * a.getWidth();
        const auto len = a.getHeight() * (0.03f + 0.12f * rnd.nextFloat() * rnd.nextFloat());
        const auto w = 2.0f + rnd.nextFloat() * 4.0f;
        g.setGradientFill (ColourGradient (p.accent.withAlpha (0.16f), x, a.getY(), p.accent.withAlpha (0.08f), x, a.getY() + len, false));
        g.fillRoundedRectangle (Rectangle<float> (x - w * 0.5f, a.getY() - 4.0f, w, len + 4.0f), w * 0.5f);
        g.fillEllipse (Rectangle<float> (w * 1.5f, w * 1.6f).withCentre ({ x, a.getY() + len }));
    }
    // one big glossy drop, with rings where it will land
    const auto c = Point<float> (a.getRight() - a.getWidth() * 0.12f, a.getY() + a.getHeight() * 0.18f);
    const auto r = juce::jmin (a.getWidth(), a.getHeight()) * 0.07f;
    const auto drop = dropShape (c, r);
    g.setGradientFill (ColourGradient (p.accent.brighter (0.2f).withAlpha (0.35f), c.x - r * 0.5f, c.y - r, p.accent.darker (0.6f).withAlpha (0.3f), c.x + r, c.y + r, true));
    g.fillPath (drop);
    g.setColour (p.accent2.withAlpha (0.25f));
    g.strokePath (drop, juce::PathStrokeType (1.0f));
    g.setColour (Colours::white.withAlpha (0.2f));
    g.fillEllipse (Rectangle<float> (r * 0.35f, r * 0.6f).withCentre ({ c.x - r * 0.45f, c.y - r * 0.1f }));
    for (int i = 1; i <= 3; ++i)
    {
        g.setColour (p.accent2.withAlpha (0.07f));
        g.drawEllipse (Rectangle<float> (r * 2.2f * (float) i, r * 0.5f * (float) i).withCentre ({ c.x, c.y + r * 3.2f }), 1.0f);
    }
}

inline void poizoneEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    const auto d = dropShape (c.translated (0.0f, r * 0.25f), r * 0.75f);
    g.setColour (p.accent);
    g.fillPath (d);
    g.setColour (p.accent2);
    g.fillEllipse (Rectangle<float> (r * 0.3f, r * 0.3f).withCentre (c.translated (-r * 0.25f, r * 0.2f)));
}

inline const Theme& poizoneTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "PoiZone";
        x.base = Style::neon;
        x.pal = { Colour (0xff1e0f26), Colour (0xff09050d), Colour (0xffe04bff), Colour (0xff9bff5a), Colour (0xc8211028), Colour (0xe00f0714),
                  Colour (0xffa45bc0), Colour (0xfffbeaff), Colour (0xff07030a), Colour (0xff5a2f6e), Colour (0xff1a0d22), Colour (0xff2e1a38),
                  Colour (0xfffff0ff), Colour (0xff22122a) };
        x.canvas = canvasPoizone;
        x.titleEmblem = poizoneEmblem;
        x.cap = Cap::orb;
        x.pointer = Pointer::glowLine;
        x.arcGlow = 0.3f;
        return x;
    }();
    return t;
}

// === Kepler: orbital mechanics — a gold sun, teal orbits, Kepler's equal areas swept ================================

inline void canvasKepler (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    stars (g, a, display ? 30 : 90, 46, 0.3f);
    if (display) return;
    const auto sun = Point<float> (a.getRight() - a.getWidth() * 0.16f, a.getY() + a.getHeight() * 0.14f);
    if (! reduced) bloom (g, sun, a.getWidth() * 0.12f, p.accent2, 0.35f);
    g.setColour (p.accent2.withAlpha (0.8f));
    g.fillEllipse (Rectangle<float> (10.0f, 10.0f).withCentre (sun));
    // orbits: ellipses with the sun at a focus, a planet on each, one with its swept equal-area wedges
    for (int i = 0; i < 4; ++i)
    {
        const auto ra = a.getWidth() * (0.10f + 0.07f * (float) i);
        const auto e = 0.35f + 0.08f * (float) i;
        const auto rb = ra * std::sqrt (1.0f - e * e);
        const auto centre = sun.translated (-ra * e, 0.0f);
        juce::Path orbit;
        orbit.addEllipse (Rectangle<float> (ra * 2.0f, rb * 2.0f).withCentre (centre));
        orbit.applyTransform (juce::AffineTransform::rotation (0.25f, sun.x, sun.y));
        g.setColour (p.accent.withAlpha (0.16f - 0.025f * (float) i));
        g.strokePath (orbit, juce::PathStrokeType (1.0f));
        auto at = [&] (float ang) {
            return sun + (Point<float> (centre.x + std::cos (ang) * ra, centre.y + std::sin (ang) * rb) - sun).rotatedAboutOrigin (0.25f);
        };
        if (i == 2)
            for (int w = 0; w < 2; ++w) // two wedges of equal area: a short wide one near the sun, a long thin one far away
            {
                juce::Path wedge;
                wedge.startNewSubPath (sun);
                const auto from = w == 0 ? -0.25f : 2.9f, to = w == 0 ? 0.45f : 3.25f;
                for (int k = 0; k <= 12; ++k) wedge.lineTo (at (from + (to - from) * (float) k / 12.0f));
                wedge.closeSubPath();
                g.setColour (p.accent.withAlpha (0.07f));
                g.fillPath (wedge);
            }
        const auto planet = at (1.3f + 1.4f * (float) i);
        if (! reduced) bloom (g, planet, 8.0f, p.accent, 0.4f);
        g.setColour (p.accent.brighter (0.4f));
        g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre (planet));
    }
}

inline void keplerMotion (juce::Graphics& g, const Motion& m)
{
    // a small planet on an elliptical orbit around a sun at the stage's left focus; it speeds up near the sun
    if (m.stage == m.area) return;
    const auto s = m.stage.reduced (4.0f);
    const auto ra = s.getWidth() * 0.42f, rb = s.getHeight() * 0.36f;
    const auto e = std::sqrt (juce::jmax (0.0f, 1.0f - (rb * rb) / (ra * ra)));
    const auto c = s.getCentre();
    const auto sun = c.translated (-ra * e, 0.0f);
    juce::Path orbit;
    orbit.addEllipse (Rectangle<float> (ra * 2.0f, rb * 2.0f).withCentre (c));
    g.setColour (m.p.accent.withAlpha (0.22f));
    g.strokePath (orbit, juce::PathStrokeType (1.0f));
    if (! reduced) bloom (g, sun, 10.0f, m.p.accent2, 0.5f);
    g.setColour (m.p.accent2);
    g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (sun));
    // mean anomaly → eccentric anomaly (a few Newton steps): Kepler's equation
    const auto M = std::fmod (m.t * 0.7f, juce::MathConstants<float>::twoPi);
    auto E = M;
    for (int i = 0; i < 4; ++i) E -= (E - e * std::sin (E) - M) / (1.0f - e * std::cos (E));
    const Point<float> q { c.x - std::cos (E) * ra, c.y + std::sin (E) * rb };
    if (! reduced) bloom (g, q, 8.0f, m.p.accent, 0.5f);
    g.setColour (m.p.accent.brighter (0.5f));
    g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre (q));
}

inline const Theme& keplerTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Kepler";
        x.base = Style::neon;
        x.pal = { Colour (0xff0b1c26), Colour (0xff03080c), Colour (0xff2ee6c5), Colour (0xffffcc66), Colour (0xc40d1e29), Colour (0xdc061118),
                  Colour (0xff4f9aa3), Colour (0xffe6fbf7), Colour (0xff020609), Colour (0xff2c6a72), Colour (0xff0a1d22), Colour (0xff17323a),
                  Colour (0xfff2fffd), Colour (0xff102630) };
        x.canvas = canvasKepler;
        x.cap = Cap::orb;
        x.pointer = Pointer::electron;
        x.stageRepaint = true;
        x.paintMotion = keplerMotion;
        x.hz = 15;
        return x;
    }();
    return t;
}

// === Kepler Exo: an exoplanet transit — a huge amber star, a dark world crossing it, the light curve dipping ========

inline float transitCurve (float u) // the star's light while a planet crosses (1 = full)
{
    const auto d = std::abs (u - 0.5f);
    return d > 0.16f ? 1.0f : d < 0.11f ? 0.55f : 0.55f + 0.45f * (d - 0.11f) / 0.05f;
}

inline void canvasKeplerExo (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    stars (g, a, display ? 25 : 70, 167, 0.3f);
    if (display) return;
    // the star's limb filling the top right corner, a dark planet in front of it
    const auto R = juce::jmax (a.getWidth(), a.getHeight()) * 0.32f;
    const auto star = Point<float> (a.getRight() + R * 0.35f, a.getY() - R * 0.45f);
    if (! reduced) bloom (g, star, R * 1.5f, p.accent, 0.22f);
    g.setGradientFill (ColourGradient (Colour (0xfffff2d0).withAlpha (0.7f), star.x, star.y, p.accent.withAlpha (0.55f), star.x - R, star.y + R * 0.4f, true));
    g.fillEllipse (Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (star));
    const auto planet = star + Point<float> (-R * 0.8f, R * 0.5f);
    const auto pr = R * 0.045f;
    g.setColour (p.bgBottom.withAlpha (0.95f));
    g.fillEllipse (Rectangle<float> (pr * 2.0f, pr * 2.0f).withCentre (planet));
    g.setColour (p.accent2.withAlpha (0.8f)); // its atmosphere lit from behind
    g.drawEllipse (Rectangle<float> (pr * 2.0f, pr * 2.0f).withCentre (planet), 1.5f);
    // the light curve along the bottom: flat, a dip while the world transits, flat again
    const auto lc = curve (200, [&] (float u) {
        return Point<float> (a.getX() + a.getWidth() * u, a.getBottom() - a.getHeight() * 0.12f + (1.0f - transitCurve (u)) * a.getHeight() * 0.1f);
    });
    g.setColour (p.accent2.withAlpha (0.18f));
    g.strokePath (lc, stroke (1.3f));
}

inline void exoEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    if (! reduced) bloom (g, c, r * 2.6f, p.accent, 0.4f);
    g.setColour (p.accent);
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setColour (p.bgBottom);
    g.fillEllipse (Rectangle<float> (r * 0.8f, r * 0.8f).withCentre (c.translated (r * 0.3f, r * 0.1f)));
}

inline void exoMotion (juce::Graphics& g, const Motion& m)
{
    // the light curve scrolling through the stage, the dip coming round again
    if (m.stage == m.area) return;
    const auto s = m.stage.reduced (4.0f);
    const auto lc = curve (100, [&] (float u) {
        const auto ph = std::fmod (u + m.t * 0.08f, 1.0f);
        return Point<float> (s.getX() + s.getWidth() * u, s.getY() + s.getHeight() * 0.3f + (1.0f - transitCurve (ph)) * s.getHeight() * 1.0f);
    });
    g.setColour (m.p.accent.withAlpha (0.25f));
    g.strokePath (lc, juce::PathStrokeType (3.5f));
    g.setColour (m.p.accent.brighter (0.3f));
    g.strokePath (lc, juce::PathStrokeType (1.2f));
}

inline const Theme& keplerExoTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Kepler Exo";
        x.base = Style::neon;
        x.pal = { Colour (0xff180f2a), Colour (0xff05030b), Colour (0xffff9b3d), Colour (0xff8a7bff), Colour (0xc41a1230), Colour (0xdc0b0718),
                  Colour (0xff8a6ab8), Colour (0xfff6ecff), Colour (0xff04020a), Colour (0xff4a3d6e), Colour (0xff120c22), Colour (0xff261d40),
                  Colour (0xfffff3e6), Colour (0xff1b1430) };
        x.canvas = canvasKeplerExo;
        x.titleEmblem = exoEmblem;
        x.drawCap = machinedCap;
        x.stageRepaint = true;
        x.paintMotion = exoMotion;
        x.hz = 10;
        return x;
    }();
    return t;
}

// === Transistor Bass: an acid bass box at night — gunmetal, an acid-yellow squelch, a step line with slides ========

inline float squelch (float u, float cutoff, float q) // a resonant low-pass response, u = log frequency 0..1
{
    const auto d = (u - cutoff) / 0.06f;
    const auto peak = q * std::exp (-d * d);
    const auto roll = u > cutoff ? std::exp (-(u - cutoff) * 9.0f) : 1.0f;
    return 0.45f * roll + peak;
}

inline void canvasTransistorBass (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    styles::brushed (g, a, 0.04f);
    if (display) return;
    // a sixteen-step acid line: pitches as dots, slides as arcs, accents brighter
    static const int notes[16] { 0, 0, 12, 3, 0, 7, 5, 12, 0, 3, 3, 10, 0, 7, 15, 12 };
    static const bool slide[16] { false, true, false, false, true, false, false, true, false, false, true, false, false, true, false, false };
    const auto row = Rectangle<float> (a.getX() + a.getWidth() * 0.4f, a.getY() + 8.0f, a.getWidth() * 0.56f, a.getHeight() * 0.16f);
    const auto sw = row.getWidth() / 16.0f;
    auto at = [&] (int i) { return Point<float> (row.getX() + sw * ((float) i + 0.5f), row.getBottom() - row.getHeight() * (float) notes[i] / 15.0f); };
    for (int i = 0; i < 16; ++i)
    {
        if (slide[i] && i < 15)
        {
            juce::Path s;
            s.startNewSubPath (at (i));
            s.quadraticTo ((at (i).x + at (i + 1).x) * 0.5f, juce::jmin (at (i).y, at (i + 1).y) - 8.0f, at (i + 1).x, at (i + 1).y);
            g.setColour (p.accent2.withAlpha (0.22f));
            g.strokePath (s, juce::PathStrokeType (1.2f));
        }
        const auto accent = notes[i] >= 12;
        g.setColour (p.accent.withAlpha (accent ? 0.45f : 0.22f));
        g.fillEllipse (Rectangle<float> (accent ? 6.0f : 4.0f, accent ? 6.0f : 4.0f).withCentre (at (i)));
    }
    // the squelch: the filter's response with a screaming resonance, printed across the lower panel
    const auto base = a.getBottom() - a.getHeight() * 0.06f;
    const auto resp = curve (200, [&] (float u) { return Point<float> (a.getX() + a.getWidth() * u, base - squelch (u, 0.58f, 0.9f) * a.getHeight() * 0.18f); });
    if (! reduced)
    {
        g.setColour (p.accent.withAlpha (0.06f));
        g.strokePath (resp, stroke (7.0f));
    }
    g.setColour (p.accent.withAlpha (0.18f));
    g.strokePath (resp, stroke (1.4f));
}

inline void transistorEmblem (juce::Graphics& g, Point<float> c, float r) // a transistor: a can and three legs
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.fillRoundedRectangle (Rectangle<float> (r * 1.5f, r * 1.0f).withCentre (c.translated (0.0f, -r * 0.35f)), r * 0.35f);
    for (int i = -1; i <= 1; ++i)
        g.fillRect (Rectangle<float> (r * 0.16f, r * 0.75f).withCentre ({ c.x + (float) i * r * 0.45f, c.y + r * 0.5f }));
}

inline void transistorMotion (juce::Graphics& g, const Motion& m)
{
    // the cutoff sweeping up and down with its resonance peak
    if (m.stage == m.area) return;
    const auto s = m.stage.reduced (4.0f);
    const auto cut = 0.35f + 0.3f * (0.5f + 0.5f * std::sin (m.t * 1.2f));
    const auto resp = curve (90, [&] (float u) { return Point<float> (s.getX() + s.getWidth() * u, s.getBottom() - squelch (u, cut, 0.9f) * s.getHeight() * 0.7f); });
    g.setColour (m.p.accent.withAlpha (0.25f));
    g.strokePath (resp, juce::PathStrokeType (4.0f));
    g.setColour (m.p.accent);
    g.strokePath (resp, juce::PathStrokeType (1.3f));
}

inline const Theme& transistorBassTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Transistor Bass";
        x.base = Style::hardware;
        x.pal = { Colour (0xff2c2f34), Colour (0xff131417), Colour (0xfff4e542), Colour (0xffff8a3d), Colour (0xd02e3136), Colour (0xe418191c),
                  Colour (0xff8b9099), Colour (0xfff2f3f5), Colour (0xff09090a), Colour (0xff2b2b2b), Colour (0xff0c0c0c), Colour (0xff3e4148),
                  Colour (0xfff2f3f5), Colour (0xff26282c) };
        x.canvas = canvasTransistorBass;
        x.titleEmblem = transistorEmblem;
        x.cap = Cap::bakelite;
        x.pointerFrom = 0.15f;
        x.ticks = 11;
        x.buttTrack = true;
        x.arcGlow = 0.2f;
        x.knobScale = 1.04f;
        x.thumbPointer = true;
        x.stageRepaint = true;
        x.paintMotion = transistorMotion;
        x.hz = 15;
        return x;
    }();
    return t;
}

// === Sawer: a luthier's workshop — dark walnut, brass, a saw's teeth along the bottom ===============================

inline void canvasSawer (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    // walnut grain: long wavy fibres
    juce::Random rnd (65);
    for (int i = 0; i < (display ? 18 : 60); ++i)
    {
        const auto y0 = a.getY() + rnd.nextFloat() * a.getHeight();
        const auto amp = 2.0f + rnd.nextFloat() * 6.0f, f = 2.0f + rnd.nextFloat() * 4.0f, ph = rnd.nextFloat() * 6.0f;
        const auto grain = curve (80, [&] (float u) { return Point<float> (a.getX() + a.getWidth() * u, y0 + amp * std::sin (u * f + ph)); });
        g.setColour ((rnd.nextBool() ? Colours::black : p.accent2).withAlpha (0.035f + 0.03f * rnd.nextFloat()));
        g.strokePath (grain, juce::PathStrokeType (0.8f + rnd.nextFloat() * 1.6f));
    }
    if (display) return;
    if (! reduced) bloom (g, { a.getX() + a.getWidth() * 0.8f, a.getY() }, a.getWidth() * 0.45f, p.accent, 0.08f);
    // a blade's teeth along the bottom: a sawtooth in brass
    juce::Path teeth;
    const auto y = a.getBottom() - 3.0f, h = juce::jmin (10.0f, a.getHeight() * 0.025f), tw = 12.0f;
    teeth.startNewSubPath (a.getX(), y);
    for (auto x = a.getX(); x < a.getRight(); x += tw)
    {
        teeth.lineTo (x + tw, y - h);
        teeth.lineTo (x + tw, y);
    }
    g.setColour (p.accent.withAlpha (0.2f));
    g.strokePath (teeth, juce::PathStrokeType (1.2f));
    // three saw waves, detuned, drifting apart across the top band
    for (int k = 0; k < 3; ++k)
    {
        const auto f = 6.0f * (1.0f + 0.04f * (float) k);
        const auto w = curve (300, [&] (float u) {
            const auto ph = std::fmod (u * f + 0.13f * (float) k, 1.0f);
            return Point<float> (a.getX() + a.getWidth() * u, a.getY() + a.getHeight() * (0.1f + 0.02f * (float) k) + (ph - 0.5f) * a.getHeight() * 0.06f);
        });
        g.setColour (p.accent.withAlpha (0.08f));
        g.strokePath (w, juce::PathStrokeType (1.0f));
    }
}

inline void brassCap (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx) // dark wood knob with a brass ring
{
    const auto& p = pal();
    g.setColour (p.accent.darker (0.3f));
    g.fillEllipse (cap.expanded (capR * 0.06f));
    g.setGradientFill (ColourGradient (p.capTop.brighter (0.15f + 0.1f * hover), c.x, cap.getY(), p.capBottom, c.x, cap.getBottom(), false));
    g.fillEllipse (cap.reduced (capR * 0.12f));
    if (fx)
    {
        g.setGradientFill (ColourGradient (p.accent.brighter (0.4f), c.x - capR, cap.getY(), p.accent.darker (0.5f), c.x + capR, cap.getBottom(), false));
        g.drawEllipse (cap.reduced (capR * 0.06f), juce::jmax (1.0f, capR * 0.1f));
    }
}

inline void sawerEmblem (juce::Graphics& g, Point<float> c, float r)
{
    juce::Path s;
    s.startNewSubPath (c.x - r * 1.2f, c.y + r * 0.6f);
    for (int i = 0; i < 3; ++i)
    {
        s.lineTo (c.x - r * 1.2f + r * 0.8f * (float) (i + 1), c.y - r * 0.6f);
        s.lineTo (c.x - r * 1.2f + r * 0.8f * (float) (i + 1), c.y + r * 0.6f);
    }
    g.setColour (pal().accent);
    g.strokePath (s, stroke (1.5f));
}

inline const Theme& sawerTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Sawer";
        x.base = Style::hardware;
        x.pal = { Colour (0xff2c1d14), Colour (0xff120b07), Colour (0xffe0a84a), Colour (0xfff3e2c0), Colour (0xd02e2017), Colour (0xe414100b),
                  Colour (0xff9a7650), Colour (0xfff8ecd6), Colour (0xff0a0705), Colour (0xff4a3020), Colour (0xff1e130c), Colour (0xff3a2a1e),
                  Colour (0xfff8ecd6), Colour (0xff2a1d14) };
        x.canvas = canvasSawer;
        x.titleEmblem = sawerEmblem;
        x.drawCap = brassCap;
        x.ticks = 11;
        x.arcGlow = 0.15f;
        x.thumbPointer = true;
        return x;
    }();
    return t;
}

// === BeepMap: a picture played as sound — mint and pink pixels melting into spectral lines, a scan line ============

inline void canvasBeepMap (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (display) return;
    // a pixel picture (a seeded landscape of blocks) on the right, its rows becoming partial lines towards the left
    const auto img = Rectangle<float> (a.getRight() - a.getWidth() * 0.34f, a.getY() + 8.0f, a.getWidth() * 0.32f, a.getHeight() * 0.32f);
    const int cols = 32, rows = 18;
    const auto cw = img.getWidth() / (float) cols, rh = img.getHeight() / (float) rows;
    juce::Random rnd (65);
    for (int y = 0; y < rows; ++y)
    {
        const auto horizon = 0.45f;
        for (int x = 0; x < cols; ++x)
        {
            const auto hill = 0.55f + 0.18f * std::sin ((float) x * 0.35f) + 0.08f * std::sin ((float) x * 1.1f);
            const auto v = (float) y / (float) rows;
            const auto lit = v > hill ? 0.8f : (v < horizon * 0.5f && rnd.nextFloat() < 0.08f ? 1.0f : 0.15f * rnd.nextFloat());
            if (lit < 0.05f) continue;
            g.setColour ((v > hill ? p.accent : p.accent2).withAlpha (0.18f * lit));
            g.fillRect (Rectangle<float> (img.getX() + (float) x * cw, img.getY() + (float) y * rh, cw - 1.0f, rh - 1.0f));
        }
        // the row as a partial, trailing left
        g.setGradientFill (ColourGradient (p.accent.withAlpha (0.0f), a.getX() + a.getWidth() * 0.2f, 0.0f, p.accent.withAlpha (0.07f), img.getX(), 0.0f, false));
        g.fillRect (Rectangle<float> (a.getX() + a.getWidth() * 0.2f, img.getY() + (float) y * rh + rh * 0.4f, img.getX() - a.getX() - a.getWidth() * 0.2f, 1.0f));
    }
}

inline void beepMapEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    const auto s = r * 0.62f;
    for (int i = 0; i < 9; ++i)
    {
        if (i == 1 || i == 3 || i == 8) continue;
        g.setColour (i % 2 == 0 ? p.accent : p.accent2);
        g.fillRect (Rectangle<float> (s * 0.85f, s * 0.85f).withCentre ({ c.x + (float) (i % 3 - 1) * s, c.y + (float) (i / 3 - 1) * s }));
    }
}

inline void beepMapMotion (juce::Graphics& g, const Motion& m)
{
    // a tiny picture in the stage, a scan line playing it column by column
    if (m.stage == m.area) return;
    const auto s = m.stage.reduced (4.0f);
    const int cols = 24, rows = 6;
    const auto cw = s.getWidth() / (float) cols, rh = s.getHeight() / (float) rows;
    const auto scan = std::fmod (m.t * 0.25f, 1.0f) * (float) cols;
    for (int x = 0; x < cols; ++x)
        for (int y = 0; y < rows; ++y)
        {
            const auto on = (float) y / (float) rows > 0.45f + 0.3f * std::sin ((float) x * 0.5f);
            if (! on) continue;
            const auto near = juce::jmax (0.0f, 1.0f - std::abs ((float) x - scan) / 3.0f);
            g.setColour (m.p.accent.withAlpha (0.25f + 0.6f * near));
            g.fillRect (Rectangle<float> (s.getX() + (float) x * cw, s.getY() + (float) y * rh, cw - 1.0f, rh - 1.0f));
        }
    g.setColour (m.p.accent2);
    g.fillRect (Rectangle<float> (s.getX() + scan * cw, s.getY(), 1.5f, s.getHeight()));
}

inline const Theme& beepMapTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "BeepMap";
        x.base = Style::neon;
        x.pal = { Colour (0xff10191a), Colour (0xff050909), Colour (0xff4cf2b0), Colour (0xffff6ad5), Colour (0xc8121c1d), Colour (0xe0080e0f),
                  Colour (0xff4a8f80), Colour (0xffe8fff6), Colour (0xff030606), Colour (0xff2c4a44), Colour (0xff0e1716), Colour (0xff1c2e2c),
                  Colour (0xfff2fffa), Colour (0xff142220) };
        x.canvas = canvasBeepMap;
        x.titleEmblem = beepMapEmblem;
        x.cap = Cap::flat;
        x.arcGlow = 0.25f;
        x.stageRepaint = true;
        x.paintMotion = beepMapMotion;
        x.hz = 12;
        return x;
    }();
    return t;
}

// === FL Keys: a grand piano — black lacquer with a long reflection, ivory and gold, keys along the bottom ===========

inline void canvasFlKeys (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    // the lacquer's reflection: a long soft diagonal sheen
    if (! reduced)
    {
        ColourGradient sheen (Colours::white.withAlpha (0.0f), a.getX(), a.getY(), Colours::white.withAlpha (0.0f), a.getRight(), a.getBottom(), false);
        sheen.addColour (0.42, Colours::white.withAlpha (0.0f));
        sheen.addColour (0.5, Colours::white.withAlpha (0.045f));
        sheen.addColour (0.58, Colours::white.withAlpha (0.0f));
        g.setGradientFill (sheen);
        g.fillRect (a);
    }
    if (display) return;
    // a keyboard along the bottom, the white keys ivory, the black ones lacquer
    const auto kb = Rectangle<float> (a.getX(), a.getBottom() - juce::jmin (34.0f, a.getHeight() * 0.07f), a.getWidth(), juce::jmin (34.0f, a.getHeight() * 0.07f));
    const auto kw = 14.0f;
    const int n = (int) (kb.getWidth() / kw) + 1;
    for (int i = 0; i < n; ++i)
    {
        const auto k = Rectangle<float> (kb.getX() + (float) i * kw, kb.getY(), kw - 1.0f, kb.getHeight());
        g.setGradientFill (ColourGradient (p.accent2.withAlpha (0.10f), 0.0f, k.getY(), p.accent2.withAlpha (0.16f), 0.0f, k.getBottom(), false));
        g.fillRect (k);
    }
    for (int i = 0; i < n; ++i)
    {
        const auto inOctave = i % 7;
        if (inOctave == 2 || inOctave == 6) continue;
        g.setColour (Colours::black.withAlpha (0.55f));
        g.fillRect (Rectangle<float> (kb.getX() + (float) i * kw + kw * 0.65f, kb.getY(), kw * 0.6f, kb.getHeight() * 0.6f));
    }
    g.setColour (p.accent.withAlpha (0.35f)); // the gold rail above the keys
    g.fillRect (kb.withHeight (1.5f).translated (0.0f, -2.0f));
    // a curve of the piano's lid on the right
    juce::Path lid;
    lid.startNewSubPath (a.getRight() - a.getWidth() * 0.4f, a.getY() + a.getHeight() * 0.04f);
    lid.cubicTo (a.getRight() - a.getWidth() * 0.12f, a.getY() + a.getHeight() * 0.02f, a.getRight() - a.getWidth() * 0.02f, a.getY() + a.getHeight() * 0.12f,
                 a.getRight() - a.getWidth() * 0.02f, a.getY() + a.getHeight() * 0.3f);
    g.setColour (p.accent.withAlpha (0.14f));
    g.strokePath (lid, stroke (1.5f));
}

inline void flKeysEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (p.accent2);
        g.fillRect (Rectangle<float> (r * 0.6f, r * 1.8f).withPosition (c.x - r + (float) i * r * 0.7f, c.y - r * 0.9f));
    }
    g.setColour (Colours::black);
    g.fillRect (Rectangle<float> (r * 0.36f, r * 1.0f).withPosition (c.x - r + r * 0.48f, c.y - r * 0.9f));
    g.fillRect (Rectangle<float> (r * 0.36f, r * 1.0f).withPosition (c.x - r + r * 1.18f, c.y - r * 0.9f));
}

inline const Theme& flKeysTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "FL Keys";
        x.base = Style::glow;
        x.pal = { Colour (0xff17161a), Colour (0xff060607), Colour (0xffd9a441), Colour (0xfff3e6c8), Colour (0xcc19181c), Colour (0xe40b0b0d),
                  Colour (0xff6e6658), Colour (0xfff6efe0), Colour (0xff050506), Colour (0xfffaf3e3), Colour (0xffcfc4ab), Colour (0xff2c2a2a),
                  Colour (0xff8a5a1a), Colour (0xff1f1e22) };
        x.canvas = canvasFlKeys;
        x.titleEmblem = flKeysEmblem;
        x.cap = Cap::porcelain;
        x.arcGlow = 0.15f;
        x.thumbPointer = true;
        return x;
    }();
    return t;
}

// === Fruity Granulizer: a cloud of grains — sand and amber specks drifting into a cloud, teal light =================

inline void canvasGranulizer (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (! reduced) bloom (g, { a.getRight() - a.getWidth() * 0.25f, a.getY() + a.getHeight() * 0.2f }, a.getWidth() * 0.35f, p.accent2, 0.08f);
    if (display) return;
    // grains: tiny windowed bursts (a dot with a soft envelope), denser towards a cloud on the right
    juce::Random rnd (67);
    const auto centre = Point<float> (a.getRight() - a.getWidth() * 0.2f, a.getY() + a.getHeight() * 0.08f);
    for (int i = 0; i < 520; ++i)
    {
        const auto ang = rnd.nextFloat() * 6.283f;
        const auto rad = std::pow (rnd.nextFloat(), 0.7f) * a.getWidth() * 0.45f;
        const Point<float> q { centre.x + std::cos (ang) * rad * 1.3f, centre.y + std::sin (ang) * rad * 0.45f };
        if (! a.contains (q)) continue;
        const auto d = 1.0f + rnd.nextFloat() * 2.4f;
        g.setColour ((rnd.nextFloat() < 0.2f ? p.accent2 : p.accent).withAlpha (0.10f + 0.3f * (1.0f - rad / (a.getWidth() * 0.45f))));
        g.fillEllipse (Rectangle<float> (d, d).withCentre (q));
    }
    // a few grains drawn large: a sine burst in a bell window
    for (int k = 0; k < 4; ++k)
    {
        const auto x0 = a.getX() + a.getWidth() * (0.08f + 0.12f * (float) k), y0 = a.getY() + a.getHeight() * (0.1f + 0.05f * (float) (k % 2));
        const auto w = a.getWidth() * 0.08f;
        const auto burst = curve (80, [&] (float u) {
            const auto win = std::exp (-std::pow ((u - 0.5f) / 0.2f, 2.0f));
            return Point<float> (x0 + w * u, y0 - win * std::sin (u * 40.0f) * a.getHeight() * 0.03f);
        });
        g.setColour (p.accent.withAlpha (0.12f));
        g.strokePath (burst, juce::PathStrokeType (1.0f));
    }
}

inline void granulizerEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    juce::Random rnd (7);
    for (int i = 0; i < 9; ++i)
    {
        g.setColour (i % 3 == 0 ? p.accent2 : p.accent);
        const auto d = r * (0.3f + 0.3f * rnd.nextFloat());
        g.fillEllipse (Rectangle<float> (d, d).withCentre ({ c.x + (rnd.nextFloat() - 0.5f) * r * 2.0f, c.y + (rnd.nextFloat() - 0.5f) * r * 1.6f }));
    }
}

inline void granulizerMotion (juce::Graphics& g, const Motion& m)
{
    for (const auto& s : m.motes) // grains flaring and fading
    {
        const auto env = std::pow (std::max (0.0f, std::sin (m.t * (0.9f + s.spin) + s.phase)), 3.0f);
        if (env < 0.02f) continue;
        if (! reduced) bloom (g, { s.x, s.y }, s.size * 3.0f, m.p.accent, 0.3f * env);
        g.setColour (m.p.accent.brighter (0.3f).withAlpha (env));
        g.fillEllipse (Rectangle<float> (s.size, s.size).withCentre ({ s.x, s.y }));
    }
}

inline const Theme& granulizerTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Granulizer";
        x.base = Style::glow;
        x.pal = { Colour (0xff1e1a14), Colour (0xff0a0806), Colour (0xffffc46b), Colour (0xff4fd8d0), Colour (0xc8201b15), Colour (0xe00e0b08),
                  Colour (0xff8c7656), Colour (0xfff8eedd), Colour (0xff070504), Colour (0xffe9d2a8), Colour (0xff8a7250), Colour (0xff332b20),
                  Colour (0xff3a2a10), Colour (0xff261f17) };
        x.canvas = canvasGranulizer;
        x.titleEmblem = granulizerEmblem;
        x.cap = Cap::porcelain;
        x.motes = 16;
        x.sizeMin = 1.5f; x.sizeRange = 2.0f;
        x.reach = 3.0f;
        x.paintMotion = granulizerMotion;
        x.hz = 12;
        return x;
    }();
    return t;
}

// === Drumaxx: membrane modes — a slate drum head with its nodal circles and diameters, brass tension rods ==========

inline void drumHead (juce::Graphics& g, Point<float> c, float R, float alpha)
{
    const auto& p = pal();
    g.setColour (p.accent.withAlpha (0.10f * alpha));
    g.drawEllipse (Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c), 2.0f);
    // nodal lines of a few membrane modes: circles and diameters
    for (int i = 1; i <= 3; ++i)
    {
        g.setColour (p.accent.withAlpha (0.06f * alpha));
        g.drawEllipse (Rectangle<float> (R * 2.0f * (float) i / 4.0f, R * 2.0f * (float) i / 4.0f).withCentre (c), 1.0f);
    }
    for (int i = 0; i < 3; ++i)
    {
        const auto ang = juce::MathConstants<float>::pi * (float) i / 3.0f + 0.3f;
        g.setColour (p.accent2.withAlpha (0.06f * alpha));
        g.drawLine ({ c + Point<float> (std::cos (ang), std::sin (ang)) * R, c - Point<float> (std::cos (ang), std::sin (ang)) * R }, 1.0f);
    }
    // the rods round the rim
    for (int i = 0; i < 10; ++i)
    {
        const auto ang = juce::MathConstants<float>::twoPi * (float) i / 10.0f;
        const Point<float> d { std::cos (ang), std::sin (ang) };
        g.setColour (p.accent2.withAlpha (0.22f * alpha));
        g.drawLine ({ c + d * R, c + d * (R + 9.0f) }, 2.5f);
    }
}

inline void canvasDrumaxx (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (! reduced) bloom (g, { a.getRight() - a.getWidth() * 0.15f, a.getY() + a.getHeight() * 0.15f }, a.getWidth() * 0.3f, p.accent, 0.08f);
    if (display) return;
    const auto R = juce::jmin (a.getWidth(), a.getHeight()) * 0.3f;
    drumHead (g, { a.getRight() - R * 0.55f, a.getY() + R * 0.5f }, R, 1.0f);
    drumHead (g, { a.getX() + R * 0.3f, a.getBottom() - R * 0.25f }, R * 0.7f, 0.6f);
}

inline void drumaxxEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.drawEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.4f);
    g.drawEllipse (Rectangle<float> (r * 1.0f, r * 1.0f).withCentre (c), 1.0f);
    g.setColour (p.accent2);
    g.drawLine ({ c.translated (-r, 0.0f), c.translated (r, 0.0f) }, 1.0f);
}

inline void drumRipples (juce::Graphics& g, const Motion& m, Point<float> c, float maxR, float rate) // rings spreading from a hit, then a new hit
{
    const auto ph = std::fmod (m.t * rate, 1.0f);
    for (int i = 0; i < 3; ++i)
    {
        const auto u = std::fmod (ph + (float) i * 0.33f, 1.0f);
        g.setColour (m.p.accent.withAlpha (0.5f * (1.0f - u)));
        g.drawEllipse (Rectangle<float> (maxR * 2.0f * u, maxR * 0.7f * u).withCentre (c), 1.2f);
    }
}

inline void drumaxxMotion (juce::Graphics& g, const Motion& m)
{
    if (m.stage == m.area) return;
    drumRipples (g, m, m.stage.getCentre(), m.stage.getWidth() * 0.45f, 0.6f);
}

inline const Theme& drumaxxTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Drumaxx";
        x.base = Style::neon;
        x.pal = { Colour (0xff17202a), Colour (0xff070a0e), Colour (0xff3ad0ff), Colour (0xffe8b04a), Colour (0xc819232e), Colour (0xe00b1016),
                  Colour (0xff5f7f99), Colour (0xffe9f5ff), Colour (0xff04070a), Colour (0xff3a4a5a), Colour (0xff121a22), Colour (0xff22303d),
                  Colour (0xfff0f8ff), Colour (0xff18222c) };
        x.canvas = canvasDrumaxx;
        x.titleEmblem = drumaxxEmblem;
        x.drawCap = machinedCap;
        x.arcGlow = 0.25f;
        x.stageRepaint = true;
        x.paintMotion = drumaxxMotion;
        x.hz = 15;
        return x;
    }();
    return t;
}

// === Drumpad: a practice pad — gum rubber, two sticks crossed, amber hit marks ======================================

inline void canvasDrumpad (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    // rubber grain
    juce::Random rnd (28);
    g.setColour (Colours::white.withAlpha (0.018f));
    for (int i = 0; i < (int) (a.getWidth() * a.getHeight() / 220.0f); ++i)
        g.fillRect (Rectangle<float> (a.getX() + rnd.nextFloat() * a.getWidth(), a.getY() + rnd.nextFloat() * a.getHeight(), 1.0f, 1.0f));
    if (display) return;
    // the pad: a big round, with the scuffs of a thousand hits near its centre
    const auto R = juce::jmin (a.getWidth(), a.getHeight()) * 0.26f;
    const auto c = Point<float> (a.getRight() - R * 0.7f, a.getY() + R * 0.6f);
    g.setColour (Colours::black.withAlpha (0.25f));
    g.fillEllipse (Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c));
    g.setColour (p.accent.withAlpha (0.10f));
    g.drawEllipse (Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c), 2.0f);
    for (int i = 0; i < 40; ++i)
    {
        const auto ang = rnd.nextFloat() * 6.283f, rad = std::pow (rnd.nextFloat(), 2.0f) * R * 0.4f;
        g.setColour (p.accent.withAlpha (0.05f + 0.06f * rnd.nextFloat()));
        g.fillEllipse (Rectangle<float> (4.0f, 3.0f).withCentre (c + Point<float> (std::cos (ang), std::sin (ang)) * rad));
    }
    // two sticks crossed over the pad
    for (int s = 0; s < 2; ++s)
    {
        const auto ang = s == 0 ? 2.5f : 0.64f;
        const Point<float> d { std::cos (ang), std::sin (ang) };
        const auto from = c - d * R * 1.4f, to = c + d * R * 0.6f;
        g.setColour (p.accent2.withAlpha (0.12f));
        g.drawLine ({ from, to }, 5.0f);
        g.fillEllipse (Rectangle<float> (8.0f, 8.0f).withCentre (to));
    }
}

inline void drumpadEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.fillEllipse (Rectangle<float> (r * 1.6f, r * 1.6f).withCentre (c));
    g.setColour (p.accent2);
    g.drawLine ({ c.translated (-r * 1.2f, -r * 1.1f), c.translated (r * 0.2f, r * 0.1f) }, 1.4f);
}

inline void drumpadMotion (juce::Graphics& g, const Motion& m)
{
    if (m.stage == m.area) return;
    drumRipples (g, m, m.stage.getCentre(), m.stage.getWidth() * 0.4f, 1.0f);
}

inline const Theme& drumpadTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Drumpad";
        x.base = Style::hardware;
        x.pal = { Colour (0xff252526), Colour (0xff0e0e0f), Colour (0xffffb347), Colour (0xffd9cdb8), Colour (0xd0262628), Colour (0xe4121213),
                  Colour (0xff77706a), Colour (0xfff4efe8), Colour (0xff080808), Colour (0xff3a3a3c), Colour (0xff141415), Colour (0xff302f2f),
                  Colour (0xfffff4e4), Colour (0xff262628) };
        x.canvas = canvasDrumpad;
        x.titleEmblem = drumpadEmblem;
        x.cap = Cap::rubber;
        x.arcGlow = 0.2f;
        x.stageRepaint = true;
        x.paintMotion = drumpadMotion;
        x.hz = 15;
        return x;
    }();
    return t;
}

// === BassDrum: the sub thump — a woofer's cone, the kick's pitch drop, deep plum and hot orange ====================

inline void woofer (juce::Graphics& g, Point<float> c, float R, float alpha, float push)
{
    const auto& p = pal();
    for (int i = 0; i < 6; ++i)
    {
        const auto r = R * (1.0f - (float) i * 0.14f) * (1.0f + push * 0.03f * (float) i);
        g.setColour ((i == 0 ? p.accent2 : p.accent).withAlpha ((i == 0 ? 0.3f : 0.14f) * alpha));
        g.drawEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), i == 0 ? 3.0f : 1.0f);
    }
    g.setGradientFill (ColourGradient (p.accent.withAlpha (0.18f * alpha), c.x - R * 0.1f, c.y - R * 0.12f, Colours::black.withAlpha (0.2f * alpha), c.x + R * 0.2f, c.y + R * 0.2f, true));
    g.fillEllipse (Rectangle<float> (R * 0.5f, R * 0.5f).withCentre (c));
}

inline void canvasBassDrum (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (! reduced) bloom (g, { a.getX() + a.getWidth() * 0.15f, a.getBottom() }, a.getWidth() * 0.45f, p.accent, 0.10f);
    if (display) return;
    const auto R = juce::jmin (a.getWidth(), a.getHeight()) * 0.28f;
    woofer (g, { a.getRight() - R * 0.75f, a.getY() + R * 0.7f }, R, 1.0f, 0.0f);
    // the kick: a sine whose pitch drops fast, decaying — printed along the bottom
    const auto kick = curve (400, [&] (float u) {
        const auto ph = 40.0f * u + 6.0f * (1.0f - std::exp (-u * 18.0f));
        return Point<float> (a.getX() + a.getWidth() * u, a.getBottom() - a.getHeight() * 0.12f - std::sin (ph * 2.0f) * std::exp (-u * 3.5f) * a.getHeight() * 0.08f);
    });
    g.setColour (p.accent.withAlpha (0.14f));
    g.strokePath (kick, juce::PathStrokeType (1.3f));
}

inline void bassDrumEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    g.setColour (p.accent2);
    g.drawEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.4f);
    g.setColour (p.accent);
    g.fillEllipse (Rectangle<float> (r * 0.9f, r * 0.9f).withCentre (c));
}

inline void bassDrumMotion (juce::Graphics& g, const Motion& m)
{
    // a small woofer in the stage, thumping at 120 bpm
    if (m.stage == m.area) return;
    const auto beat = std::fmod (m.t * 2.0f, 1.0f);
    const auto push = std::exp (-beat * 7.0f);
    const auto c = m.stage.getCentre();
    const auto R = m.stage.getHeight() * 0.42f;
    if (! reduced) bloom (g, c, R * 1.6f, m.p.accent, 0.3f * push);
    for (int i = 0; i < 3; ++i)
    {
        const auto r = R * (1.0f - 0.3f * (float) i) * (1.0f + 0.08f * push);
        g.setColour (m.p.accent.withAlpha (0.35f + 0.4f * push));
        g.drawEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.2f);
    }
}

inline const Theme& bassDrumTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "BassDrum";
        x.base = Style::neon;
        x.pal = { Colour (0xff1f1022), Colour (0xff09040b), Colour (0xffff5a2e), Colour (0xffffd34d), Colour (0xc8221125), Colour (0xe00f0711),
                  Colour (0xff8a5a70), Colour (0xfffbeee8), Colour (0xff060307), Colour (0xff3a2433), Colour (0xff140a12), Colour (0xff2e1a2a),
                  Colour (0xfffff0e6), Colour (0xff221326) };
        x.canvas = canvasBassDrum;
        x.titleEmblem = bassDrumEmblem;
        x.cap = Cap::rubber;
        x.pointer = Pointer::glowLine;
        x.arcGlow = 0.3f;
        x.stageRepaint = true;
        x.paintMotion = bassDrumMotion;
        x.hz = 15;
        return x;
    }();
    return t;
}

// --- more knob materials (G3) ----------------------------------------------------------------------------------------

// a chicken-head knob's base (guitar amps): a dark round; the pointer from its very centre is the bar (pointerFrom = 0)
inline void chickenBase (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.capTop.brighter (0.1f + 0.1f * hover), c.x, cap.getY(), p.capBottom, c.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    if (fx)
    {
        g.setColour (Colours::white.withAlpha (0.08f));
        g.drawEllipse (cap.reduced (0.6f), 1.0f);
    }
}

// a chrome dome: a bright mirror with a dark horizon band
inline void chromeCap (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    ColourGradient chrome (Colour (0xfff4f6f8), c.x, cap.getY(), Colour (0xff3a3d42), c.x, cap.getBottom(), false);
    chrome.addColour (0.45, Colour (0xffb8bec6));
    chrome.addColour (0.52, Colour (0xff4a4e55));
    chrome.addColour (0.7, Colour (0xff8d939b));
    g.setGradientFill (chrome);
    g.fillEllipse (cap);
    if (fx)
    {
        g.setColour (Colours::white.withAlpha (0.25f + 0.2f * hover));
        g.fillEllipse (Rectangle<float> (capR * 0.7f, capR * 0.35f).withCentre ({ c.x - capR * 0.2f, c.y - capR * 0.5f }));
        g.setColour (Colours::black.withAlpha (0.35f));
        g.drawEllipse (cap.reduced (0.5f), 1.0f);
    }
}

// === BooBass: a bass guitar — a sunburst body, four strings, a chrome pickup; one string ringing ==================

inline void canvasBooBass (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    // sunburst: amber in the middle, burning to black at the edges
    g.setGradientFill (ColourGradient (p.bgTop, a.getX() + a.getWidth() * 0.6f, a.getY() + a.getHeight() * 0.35f, p.bgBottom, a.getX(), a.getBottom(), true));
    g.fillRect (a);
    if (display) return;
    // four strings running along the panel, a pickup under them
    const auto y0 = a.getY() + a.getHeight() * 0.06f;
    for (int s = 0; s < 4; ++s)
    {
        const auto y = y0 + (float) s * 7.0f;
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRect (Rectangle<float> (a.getX(), y + 1.0f, a.getWidth(), 1.0f + (float) s * 0.4f));
        g.setColour (p.accent2.withAlpha (0.18f));
        g.fillRect (Rectangle<float> (a.getX(), y, a.getWidth(), 0.8f + (float) s * 0.4f));
    }
    const auto pickup = Rectangle<float> (a.getX() + a.getWidth() * 0.62f, y0 - 6.0f, 46.0f, 33.0f);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (pickup, 4.0f);
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (p.accent2.withAlpha (0.3f));
        g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre ({ pickup.getX() + 9.0f + (float) i * 9.5f, y0 + 1.0f + (float) i * 7.0f }));
    }
    // the body's binding: a cream line following a curve near the bottom
    juce::Path bind;
    bind.startNewSubPath (a.getX(), a.getBottom() - a.getHeight() * 0.05f);
    bind.cubicTo (a.getX() + a.getWidth() * 0.3f, a.getBottom() - a.getHeight() * 0.12f, a.getX() + a.getWidth() * 0.7f, a.getBottom(), a.getRight(), a.getBottom() - a.getHeight() * 0.08f);
    g.setColour (p.accent2.withAlpha (0.12f));
    g.strokePath (bind, stroke (2.0f));
}

inline void booBassEmblem (juce::Graphics& g, Point<float> c, float r) // a headstock with tuners
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.fillRoundedRectangle (Rectangle<float> (r * 0.9f, r * 2.0f).withCentre (c), r * 0.3f);
    g.setColour (p.accent2);
    for (int i = 0; i < 2; ++i)
    {
        g.fillEllipse (Rectangle<float> (r * 0.45f, r * 0.45f).withCentre ({ c.x - r * 0.7f, c.y - r * 0.5f + (float) i * r * 0.9f }));
        g.fillEllipse (Rectangle<float> (r * 0.45f, r * 0.45f).withCentre ({ c.x + r * 0.7f, c.y - r * 0.5f + (float) i * r * 0.9f }));
    }
}

inline void ringingString (juce::Graphics& g, const Motion& m, float rate) // a string vibrating across the stage, its swing decaying and re-plucked
{
    if (m.stage == m.area) return;
    const auto s = m.stage.reduced (4.0f);
    const auto since = std::fmod (m.t * rate, 1.0f);
    const auto amp = std::exp (-since * 4.0f) * s.getHeight() * 0.38f;
    const auto path = curve (60, [&] (float u) {
        return Point<float> (s.getX() + s.getWidth() * u, s.getCentreY() + std::sin (u * juce::MathConstants<float>::pi) * amp * std::sin (m.t * 38.0f));
    });
    g.setColour (m.p.accent2.withAlpha (0.25f));
    g.strokePath (path, juce::PathStrokeType (3.0f));
    g.setColour (m.p.accent2);
    g.strokePath (path, juce::PathStrokeType (1.1f));
}

inline void booBassMotion (juce::Graphics& g, const Motion& m) { ringingString (g, m, 0.5f); }

inline const Theme& booBassTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "BooBass";
        x.base = Style::hardware;
        x.pal = { Colour (0xff4a2a10), Colour (0xff0b0603), Colour (0xffffb04a), Colour (0xfff5e6c8), Colour (0xc82a1a0e), Colour (0xe0120b06),
                  Colour (0xff9a6a3a), Colour (0xfffaf0dc), Colour (0xff080503), Colour (0xffd8dde3), Colour (0xff4a4e55), Colour (0xff3a2818),
                  Colour (0xff1a1a1a), Colour (0xff2a1c10) };
        x.canvas = canvasBooBass;
        x.titleEmblem = booBassEmblem;
        x.drawCap = chromeCap;
        x.arcGlow = 0.18f;
        x.stageRepaint = true;
        x.paintMotion = booBassMotion;
        return x;
    }();
    return t;
}

// === Fruity DrumSynth Live: a live drum machine — four rows of coloured pads, a rubber feel =========================

inline void canvasDrumSynth (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (display) return;
    static const juce::uint32 rowColours[4] { 0xffff5a3c, 0xffffa23c, 0xffffd23f, 0xff6ad86a };
    const auto grid = Rectangle<float> (a.getRight() - a.getWidth() * 0.3f, a.getY() + 8.0f, a.getWidth() * 0.27f, a.getHeight() * 0.3f);
    const auto pw = grid.getWidth() / 8.0f, ph = grid.getHeight() / 4.0f;
    juce::Random rnd (30);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 8; ++c)
        {
            const auto lit = rnd.nextFloat() < 0.35f;
            g.setColour (Colour (rowColours[r]).withAlpha (lit ? 0.28f : 0.07f));
            g.fillRoundedRectangle (Rectangle<float> (grid.getX() + (float) c * pw, grid.getY() + (float) r * ph, pw - 3.0f, ph - 3.0f), 3.0f);
        }
    if (! reduced) bloom (g, grid.getCentre(), grid.getWidth() * 0.7f, p.accent, 0.06f);
}

inline void drumSynthEmblem (juce::Graphics& g, Point<float> c, float r)
{
    static const juce::uint32 cols[4] { 0xffff5a3c, 0xffffa23c, 0xffffd23f, 0xff6ad86a };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (Colour (cols[i]));
        g.fillRoundedRectangle (Rectangle<float> (r * 0.85f, r * 0.85f).withCentre ({ c.x + (i % 2 == 0 ? -0.5f : 0.5f) * r, c.y + (i < 2 ? -0.5f : 0.5f) * r }), 1.0f);
    }
}

inline const Theme& drumSynthTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "DrumSynth Live";
        x.base = Style::vivid;
        x.pal = { Colour (0xff222124), Colour (0xff0c0c0d), Colour (0xffff5a3c), Colour (0xffffd23f), Colour (0xd0242326), Colour (0xe4111113),
                  Colour (0xff6e6a70), Colour (0xfff6f2ee), Colour (0xff070707), Colour (0xff3a383c), Colour (0xff151416), Colour (0xff302e33),
                  Colour (0xfffff4ea), Colour (0xff262528) };
        x.canvas = canvasDrumSynth;
        x.titleEmblem = drumSynthEmblem;
        x.cap = Cap::rubber;
        x.arcGlow = 0.22f;
        return x;
    }();
    return t;
}

// === Fruity DX10: an eighties FM keyboard — dark umber, a stripe of coloured membrane bands, an algorithm diagram ===

inline void canvasDx10 (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (display) return;
    // the membrane stripe band along the top
    static const juce::uint32 stripes[5] { 0xff57d18b, 0xffe05a6a, 0xffe7c35a, 0xff5aa7e0, 0xffb07ad8 };
    const auto band = Rectangle<float> (a.getX(), a.getY() + 2.0f, a.getWidth(), 5.0f);
    for (int i = 0; i < 5; ++i)
    {
        g.setColour (Colour (stripes[i]).withAlpha (0.35f));
        g.fillRect (band.withX (a.getX() + a.getWidth() * (float) i / 5.0f).withWidth (a.getWidth() / 5.0f - 2.0f));
    }
    // an algorithm: operators as numbered boxes, modulators stacked over carriers, a feedback loop
    const auto box = 22.0f;
    const auto o = Point<float> (a.getRight() - a.getWidth() * 0.2f, a.getY() + a.getHeight() * 0.06f);
    const Point<float> ops[4] { o, o.translated (0.0f, box * 1.6f), o.translated (box * 1.6f, box * 1.6f), o.translated (0.8f * box, box * 3.2f) };
    g.setColour (p.accent.withAlpha (0.18f));
    g.drawLine ({ ops[0].translated (box * 0.5f, box), ops[1].translated (box * 0.5f, 0.0f) }, 1.2f);
    g.drawLine ({ ops[1].translated (box * 0.5f, box), ops[3].translated (box * 0.5f, 0.0f) }, 1.2f);
    g.drawLine ({ ops[2].translated (box * 0.5f, box), ops[3].translated (box * 0.5f, 0.0f) }, 1.2f);
    juce::Path fb; // feedback loop on the top operator
    fb.addArc (ops[0].x + box * 0.6f, ops[0].y - box * 0.4f, box * 0.8f, box * 0.8f, 0.0f, juce::MathConstants<float>::pi * 1.4f, true);
    g.strokePath (fb, juce::PathStrokeType (1.2f));
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (Colours::black.withAlpha (0.3f));
        g.fillRect (Rectangle<float> (box, box).withPosition (ops[i]));
        g.setColour ((i == 3 ? p.accent2 : p.accent).withAlpha (0.3f));
        g.drawRect (Rectangle<float> (box, box).withPosition (ops[i]), 1.2f);
        g.setFont (theme::font (11.0f, true));
        g.drawText (juce::String (i + 1), Rectangle<float> (box, box).withPosition (ops[i]), juce::Justification::centred, false);
    }
}

inline void dx10Emblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.drawRect (Rectangle<float> (r * 0.9f, r * 0.9f).withCentre (c.translated (0.0f, -r * 0.55f)), 1.2f);
    g.setColour (p.accent2);
    g.fillRect (Rectangle<float> (r * 0.9f, r * 0.9f).withCentre (c.translated (0.0f, r * 0.55f)));
}

inline const Theme& dx10Theme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "DX10";
        x.base = Style::glow;
        x.pal = { Colour (0xff2b2522), Colour (0xff100d0c), Colour (0xff57d18b), Colour (0xffe05a6a), Colour (0xd02c2623), Colour (0xe4141110),
                  Colour (0xff7a6c62), Colour (0xfff2ece6), Colour (0xff070605), Colour (0xff3a3330), Colour (0xff171412), Colour (0xff342c28),
                  Colour (0xfff2ece6), Colour (0xff2a2421) };
        x.canvas = canvasDx10;
        x.titleEmblem = dx10Emblem;
        x.cap = Cap::flat;
        x.thin = true;
        x.kerning = 0.08f;
        x.arcGlow = 0.0f;
        return x;
    }();
    return t;
}

// === MiniSynth: a patch-cord monosynth — navy panel, cream legends, orange cords looping between jacks ==============

inline void canvasMiniSynth (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (display) return;
    // patch cords: sagging curves between jacks along the top
    juce::Random rnd (41);
    static const juce::uint32 cords[3] { 0xffff8a2a, 0xfff5e6c8, 0xff3fc7d8 };
    for (int i = 0; i < 5; ++i)
    {
        const auto x0 = a.getX() + a.getWidth() * (0.35f + 0.6f * rnd.nextFloat()), x1 = a.getX() + a.getWidth() * (0.35f + 0.6f * rnd.nextFloat());
        const auto y = a.getY() + 10.0f;
        const auto sag = a.getHeight() * (0.06f + 0.1f * rnd.nextFloat());
        juce::Path cord;
        cord.startNewSubPath (x0, y);
        cord.cubicTo (x0, y + sag, x1, y + sag, x1, y);
        const auto col = Colour (cords[i % 3]);
        g.setColour (Colours::black.withAlpha (0.25f));
        g.strokePath (cord, stroke (4.0f), juce::AffineTransform::translation (0.0f, 2.0f));
        g.setColour (col.withAlpha (0.28f));
        g.strokePath (cord, stroke (3.0f));
        for (const auto x : { x0, x1 })
        {
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillEllipse (Rectangle<float> (8.0f, 8.0f).withCentre ({ x, y }));
            g.setColour (p.accent2.withAlpha (0.35f));
            g.drawEllipse (Rectangle<float> (8.0f, 8.0f).withCentre ({ x, y }), 1.0f);
        }
    }
}

inline void miniSynthEmblem (juce::Graphics& g, Point<float> c, float r) // a jack with a cord
{
    const auto& p = pal();
    g.setColour (p.accent);
    juce::Path cord;
    cord.startNewSubPath (c.translated (-r * 1.2f, -r * 0.6f));
    cord.cubicTo (c.x - r * 1.2f, c.y + r, c.x + r * 0.2f, c.y + r, c.x + r * 0.2f, c.y);
    g.strokePath (cord, stroke (1.5f));
    g.setColour (p.accent2);
    g.drawEllipse (Rectangle<float> (r * 1.0f, r * 1.0f).withCentre (c.translated (r * 0.4f, -r * 0.1f)), 1.2f);
}

inline const Theme& miniSynthTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "MiniSynth";
        x.base = Style::hardware;
        x.pal = { Colour (0xff17203a), Colour (0xff070a14), Colour (0xffff8a2a), Colour (0xfff5e6c8), Colour (0xcc19223c), Colour (0xe20b1020),
                  Colour (0xff5a6890), Colour (0xfff5e6c8), Colour (0xff04060c), Colour (0xff2b2f3a), Colour (0xff0d0f14), Colour (0xff27304a),
                  Colour (0xfff5e6c8), Colour (0xff1c2440) };
        x.canvas = canvasMiniSynth;
        x.titleEmblem = miniSynthEmblem;
        x.cap = Cap::bakelite;
        x.pointerFrom = 0.15f;
        x.ticks = 11;
        x.arcGlow = 0.2f;
        x.thumbPointer = true;
        return x;
    }();
    return t;
}

// === Plucked!: a harp's strings — deep teal, honey-gold strings fanning, one plucked ===============================

inline void canvasPlucked (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (! reduced) bloom (g, { a.getRight(), a.getY() }, a.getWidth() * 0.5f, p.accent, 0.08f);
    if (display) return;
    // a harp's strings fanning from the top right, shorter to longer
    const auto top = Point<float> (a.getRight() - a.getWidth() * 0.05f, a.getY());
    for (int i = 0; i < 14; ++i)
    {
        const auto x = a.getRight() - a.getWidth() * (0.08f + 0.035f * (float) i);
        const auto len = a.getHeight() * (0.12f + 0.022f * (float) i);
        g.setColour (p.accent.withAlpha (0.10f + (i == 6 ? 0.12f : 0.0f)));
        if (i == 6)
        {
            const auto plucked = curve (40, [&] (float u) { return Point<float> (x + std::sin (u * juce::MathConstants<float>::pi) * 4.0f, top.y + len * u); });
            g.strokePath (plucked, juce::PathStrokeType (1.2f));
        }
        else
            g.drawLine ({ x, top.y, x, top.y + len }, 1.0f);
    }
    // the neck curve over them
    juce::Path neck;
    neck.startNewSubPath (a.getRight() - a.getWidth() * 0.6f, a.getY() + a.getHeight() * 0.1f);
    neck.quadraticTo (a.getRight() - a.getWidth() * 0.3f, a.getY() - a.getHeight() * 0.02f, a.getRight(), a.getY() + a.getHeight() * 0.02f);
    g.setColour (p.accent.withAlpha (0.2f));
    g.strokePath (neck, stroke (3.0f));
}

inline void pluckedEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    g.setColour (p.accent);
    for (int i = 0; i < 3; ++i)
        g.drawLine ({ c.x - r * 0.8f + (float) i * r * 0.8f, c.y - r, c.x - r * 0.8f + (float) i * r * 0.8f, c.y + r * (0.3f + 0.35f * (float) i) }, 1.2f);
}

inline void pluckedMotion (juce::Graphics& g, const Motion& m) { ringingString (g, m, 0.7f); }

inline const Theme& pluckedTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Plucked!";
        x.base = Style::glow;
        x.pal = { Colour (0xff0f2628), Colour (0xff040c0d), Colour (0xffe8b85a), Colour (0xfffff0c8), Colour (0xc8112a2c), Colour (0xe0071315),
                  Colour (0xff4f8a86), Colour (0xfff4fbf8), Colour (0xff030909), Colour (0xfff2e2b8), Colour (0xffa0844c), Colour (0xff1c3a3a),
                  Colour (0xff3a2a10), Colour (0xff153032) };
        x.canvas = canvasPlucked;
        x.titleEmblem = pluckedEmblem;
        x.cap = Cap::porcelain;
        x.arcGlow = 0.18f;
        x.stageRepaint = true;
        x.paintMotion = pluckedMotion;
        return x;
    }();
    return t;
}

// === SimSynth: an early-90s desktop synth — slate bevels, a sine/saw/square triad, a sky-blue and butter palette ===

inline void canvasSimSynth (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    // a bevelled tile grid, like an old desktop's raised panels
    const auto tile = 48.0f;
    for (auto y = a.getY(); y < a.getBottom(); y += tile)
        for (auto x = a.getX(); x < a.getRight(); x += tile)
        {
            g.setColour (Colours::white.withAlpha (0.018f));
            g.fillRect (Rectangle<float> (x, y, tile - 1.0f, 1.0f));
            g.fillRect (Rectangle<float> (x, y, 1.0f, tile - 1.0f));
            g.setColour (Colours::black.withAlpha (0.12f));
            g.fillRect (Rectangle<float> (x, y + tile - 1.0f, tile, 1.0f));
            g.fillRect (Rectangle<float> (x + tile - 1.0f, y, 1.0f, tile));
        }
    if (display) return;
    // three oscillators' shapes, stacked, sky blue / butter / white
    const Colour cols[3] { p.accent, p.accent2, Colour (0xffeef2ff) };
    for (int k = 0; k < 3; ++k)
    {
        const auto y = a.getY() + a.getHeight() * (0.08f + 0.05f * (float) k);
        const auto w = curve (200, [&] (float u) {
            const auto ph = std::fmod (u * 5.0f, 1.0f);
            const auto v = k == 0 ? std::sin (ph * juce::MathConstants<float>::twoPi) : k == 1 ? 1.0f - 2.0f * ph : (ph < 0.5f ? 1.0f : -1.0f);
            return Point<float> (a.getX() + a.getWidth() * (0.45f + 0.52f * u), y - v * a.getHeight() * 0.018f);
        });
        g.setColour (cols[k].withAlpha (0.16f));
        g.strokePath (w, juce::PathStrokeType (1.2f));
    }
}

inline void simSynthEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    g.setColour (p.plate.brighter (0.6f));
    g.fillRect (Rectangle<float> (r * 1.8f, r * 1.8f).withCentre (c));
    g.setColour (Colours::white.withAlpha (0.6f));
    g.fillRect (Rectangle<float> (r * 1.8f, 1.0f).withPosition (c.x - r * 0.9f, c.y - r * 0.9f));
    g.setColour (p.accent);
    g.fillRect (Rectangle<float> (r * 0.9f, r * 0.9f).withCentre (c));
}

inline const Theme& simSynthTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "SimSynth";
        x.base = Style::glow;
        x.pal = { Colour (0xff252836), Colour (0xff0e1016), Colour (0xff5ec8ff), Colour (0xffffd166), Colour (0xd0272a38), Colour (0xe4121420),
                  Colour (0xff6a7090), Colour (0xffeef2ff), Colour (0xff07080c), Colour (0xff4a5068), Colour (0xff1a1d28), Colour (0xff30344a),
                  Colour (0xffeef2ff), Colour (0xff2a2e3e) };
        x.canvas = canvasSimSynth;
        x.titleEmblem = simSynthEmblem;
        x.cap = Cap::flat;
        x.arcGlow = 0.15f;
        return x;
    }();
    return t;
}

// === Autogun: presets fired at random — gunmetal, a cyan reticle, a scatter of hits ================================

inline void canvasAutogun (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    styles::brushed (g, a, 0.025f);
    if (display) return;
    const auto c = Point<float> (a.getRight() - a.getWidth() * 0.16f, a.getY() + a.getHeight() * 0.18f);
    const auto R = juce::jmin (a.getWidth(), a.getHeight()) * 0.16f;
    g.setColour (p.accent.withAlpha (0.16f));
    for (int i = 1; i <= 3; ++i)
        g.drawEllipse (Rectangle<float> (R * 2.0f * (float) i / 3.0f, R * 2.0f * (float) i / 3.0f).withCentre (c), 1.0f);
    g.drawLine ({ c.x - R * 1.3f, c.y, c.x - R * 0.15f, c.y }, 1.0f);
    g.drawLine ({ c.x + R * 0.15f, c.y, c.x + R * 1.3f, c.y }, 1.0f);
    g.drawLine ({ c.x, c.y - R * 1.3f, c.x, c.y - R * 0.15f }, 1.0f);
    g.drawLine ({ c.x, c.y + R * 0.15f, c.x, c.y + R * 1.3f }, 1.0f);
    // the hits: random presets, each a tiny waveform glyph
    juce::Random rnd (57);
    for (int i = 0; i < 26; ++i)
    {
        const auto ang = rnd.nextFloat() * 6.283f, rad = R * 1.8f * std::sqrt (rnd.nextFloat());
        const auto q = c + Point<float> (std::cos (ang), std::sin (ang)) * rad;
        const auto f = 1.0f + (float) rnd.nextInt (4);
        const auto glyph = curve (16, [&] (float u) { return Point<float> (q.x - 6.0f + 12.0f * u, q.y - std::sin (u * f * 6.283f) * 3.0f); });
        g.setColour ((i % 4 == 0 ? p.accent2 : p.accent).withAlpha (0.18f));
        g.strokePath (glyph, juce::PathStrokeType (1.0f));
    }
}

inline void autogunEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.drawEllipse (Rectangle<float> (r * 1.8f, r * 1.8f).withCentre (c), 1.2f);
    g.drawLine ({ c.x - r * 1.2f, c.y, c.x + r * 1.2f, c.y }, 1.0f);
    g.drawLine ({ c.x, c.y - r * 1.2f, c.x, c.y + r * 1.2f }, 1.0f);
    g.setColour (p.accent2);
    g.fillEllipse (Rectangle<float> (r * 0.45f, r * 0.45f).withCentre (c.translated (r * 0.35f, -r * 0.3f)));
}

inline const Theme& autogunTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Autogun";
        x.base = Style::neon;
        x.pal = { Colour (0xff23272c), Colour (0xff0b0d0f), Colour (0xff39f3ff), Colour (0xffffe14a), Colour (0xcc252a30), Colour (0xe2111316),
                  Colour (0xff5f7a85), Colour (0xffeefcff), Colour (0xff050607), Colour (0xff3a3f46), Colour (0xff15181b), Colour (0xff2c3238),
                  Colour (0xfff0feff), Colour (0xff24292e) };
        x.canvas = canvasAutogun;
        x.titleEmblem = autogunEmblem;
        x.drawCap = machinedCap;
        x.pointer = Pointer::glowLine;
        x.ticks = 9;
        x.boldTicks = true;
        x.arcGlow = 0.3f;
        return x;
    }();
    return t;
}

// === FL Slayer: a guitar amp stack — black tolex, a woven grille, gold piping, chicken-head knobs, a pilot light ===

inline void canvasSlayer (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    // tolex: a fine pebbled grain
    juce::Random rnd (68);
    for (int i = 0; i < (int) (a.getWidth() * a.getHeight() / 160.0f); ++i)
    {
        g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (0.025f));
        g.fillEllipse (Rectangle<float> (1.6f, 1.6f).withCentre ({ a.getX() + rnd.nextFloat() * a.getWidth(), a.getY() + rnd.nextFloat() * a.getHeight() }));
    }
    if (display) return;
    // the grille: a diagonal weave in the top right, framed by gold piping
    const auto grille = Rectangle<float> (a.getRight() - a.getWidth() * 0.36f, a.getY() + 6.0f, a.getWidth() * 0.34f, a.getHeight() * 0.28f);
    g.saveState();
    g.reduceClipRegion (grille.toNearestInt());
    for (auto x = grille.getX() - grille.getHeight(); x < grille.getRight(); x += 5.0f)
    {
        g.setColour (p.accent.withAlpha (0.06f));
        g.drawLine ({ x, grille.getBottom(), x + grille.getHeight(), grille.getY() }, 1.0f);
        g.setColour (Colours::black.withAlpha (0.18f));
        g.drawLine ({ x, grille.getY(), x + grille.getHeight(), grille.getBottom() }, 1.0f);
    }
    g.restoreState();
    g.setColour (p.accent.withAlpha (0.35f));
    g.drawRoundedRectangle (grille, 6.0f, 1.5f);
    // the pilot light
    const auto pilot = Point<float> (grille.getX() - 16.0f, grille.getY() + 14.0f);
    if (! reduced) bloom (g, pilot, 18.0f, p.accent2, 0.5f);
    g.setColour (p.accent2);
    g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre (pilot));
}

inline void slayerEmblem (juce::Graphics& g, Point<float> c, float r) // a pilot jewel
{
    const auto& p = pal();
    if (! reduced) bloom (g, c, r * 2.2f, p.accent2, 0.5f);
    g.setColour (p.accent2);
    g.fillEllipse (Rectangle<float> (r * 1.3f, r * 1.3f).withCentre (c));
    g.setColour (p.accent);
    g.drawEllipse (Rectangle<float> (r * 1.8f, r * 1.8f).withCentre (c), 1.2f);
}

inline const Theme& slayerTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "FL Slayer";
        x.base = Style::hardware;
        x.pal = { Colour (0xff1c1b1b), Colour (0xff090909), Colour (0xffffb238), Colour (0xffe8433a), Colour (0xd01d1c1c), Colour (0xe40c0c0c),
                  Colour (0xff8a7048), Colour (0xfff6ead2), Colour (0xff060606), Colour (0xfff2ead8), Colour (0xffb8ab90), Colour (0xff2e2b28),
                  Colour (0xff1a1a1a), Colour (0xff222120) };
        x.canvas = canvasSlayer;
        x.titleEmblem = slayerEmblem;
        x.drawCap = chickenBase;
        x.pointerFrom = 0.0f;
        x.ticks = 11;
        x.arcGlow = 0.2f;
        x.thumbPointer = true;
        return x;
    }();
    return t;
}

// === Fruit Kick: a juicy kick — an orange slice whose segments are the kick's envelope, lime zest, a beat =========

inline void orangeSlice (juce::Graphics& g, Point<float> c, float R, float alpha)
{
    const auto& p = pal();
    g.setColour (p.accent.withAlpha (0.22f * alpha));
    g.drawEllipse (Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c), 3.0f);
    for (int i = 0; i < 10; ++i)
    {
        const auto a0 = juce::MathConstants<float>::twoPi * (float) i / 10.0f + 0.05f, a1 = a0 + juce::MathConstants<float>::twoPi / 10.0f - 0.1f;
        juce::Path seg;
        seg.startNewSubPath (c + Point<float> (std::sin (a0), -std::cos (a0)) * R * 0.12f);
        seg.addCentredArc (c.x, c.y, R * 0.86f, R * 0.86f, 0.0f, a0, a1);
        seg.closeSubPath();
        g.setColour (p.accent.withAlpha ((0.06f + 0.05f * (float) (i % 2)) * alpha));
        g.fillPath (seg);
    }
}

inline void canvasFruitKick (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (! reduced) bloom (g, { a.getRight() - a.getWidth() * 0.15f, a.getY() + a.getHeight() * 0.2f }, a.getWidth() * 0.35f, p.accent, 0.10f);
    if (display) return;
    const auto R = juce::jmin (a.getWidth(), a.getHeight()) * 0.22f;
    orangeSlice (g, { a.getRight() - R * 0.9f, a.getY() + R * 0.85f }, R, 1.0f);
    // lime zest specks
    juce::Random rnd (43);
    for (int i = 0; i < 40; ++i)
    {
        g.setColour (p.accent2.withAlpha (0.10f + 0.1f * rnd.nextFloat()));
        g.fillEllipse (Rectangle<float> (2.0f, 2.0f).withCentre ({ a.getX() + rnd.nextFloat() * a.getWidth(), a.getY() + rnd.nextFloat() * a.getHeight() * 0.35f }));
    }
}

inline void fruitKickEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.fillEllipse (Rectangle<float> (r * 1.9f, r * 1.9f).withCentre (c));
    g.setColour (p.accent2);
    g.fillEllipse (Rectangle<float> (r * 0.6f, r * 0.35f).withCentre (c.translated (r * 0.3f, -r * 1.0f)));
}

inline void fruitKickMotion (juce::Graphics& g, const Motion& m)
{
    // the slice thumping in the stage at 120 bpm
    if (m.stage == m.area) return;
    const auto beat = std::fmod (m.t * 2.0f, 1.0f);
    const auto push = std::exp (-beat * 8.0f);
    const auto c = m.stage.getCentre();
    const auto R = m.stage.getHeight() * (0.36f + 0.06f * push);
    if (! reduced) bloom (g, c, R * 1.8f, m.p.accent, 0.35f * push);
    g.setColour (m.p.accent.withAlpha (0.6f + 0.4f * push));
    g.drawEllipse (Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c), 1.6f);
    for (int i = 0; i < 8; ++i)
    {
        const auto ang = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
        g.drawLine ({ c + Point<float> (std::sin (ang), -std::cos (ang)) * R * 0.2f, c + Point<float> (std::sin (ang), -std::cos (ang)) * R * 0.85f }, 1.0f);
    }
}

inline const Theme& fruitKickTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Fruit Kick";
        x.base = Style::vivid;
        x.pal = { Colour (0xff24121a), Colour (0xff0b0508), Colour (0xffff8c1a), Colour (0xffa6e22e), Colour (0xc826141c), Colour (0xe0100810),
                  Colour (0xff9a5a4a), Colour (0xfffff0e0), Colour (0xff070305), Colour (0xffffd9b0), Colour (0xffc07a3a), Colour (0xff3a2028),
                  Colour (0xff4a1a00), Colour (0xff281420) };
        x.canvas = canvasFruitKick;
        x.titleEmblem = fruitKickEmblem;
        x.cap = Cap::porcelain;
        x.arcGlow = 0.3f;
        x.stageRepaint = true;
        x.paintMotion = fruitKickMotion;
        x.hz = 15;
        return x;
    }();
    return t;
}

// === Wasp: a hive — black and yellow, a honeycomb lattice, a striped band ==========================================

inline void canvasWasp (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    // the honeycomb, brighter in the top right
    const auto hs = 16.0f;
    for (auto row = 0; a.getY() + (float) row * hs * 0.866f < a.getBottom() + hs; ++row)
        for (auto col = 0; a.getX() + (float) col * hs * 1.5f < a.getRight() + hs; ++col)
        {
            const Point<float> c { a.getX() + (float) col * hs * 1.5f, a.getY() + (float) row * hs * 0.866f * 2.0f + (col % 2 == 0 ? 0.0f : hs * 0.866f) };
            const auto near = juce::jmax (0.0f, 1.0f - c.getDistanceFrom ({ a.getRight(), a.getY() }) / (a.getWidth() * 0.6f));
            if (near <= 0.0f) continue;
            juce::Path hex;
            for (int i = 0; i < 6; ++i)
            {
                const auto ang = juce::MathConstants<float>::twoPi * (float) i / 6.0f;
                const Point<float> q { c.x + std::cos (ang) * hs * 0.95f, c.y + std::sin (ang) * hs * 0.95f };
                if (i == 0) hex.startNewSubPath (q);
                else hex.lineTo (q);
            }
            hex.closeSubPath();
            g.setColour (p.accent.withAlpha (0.12f * near));
            g.strokePath (hex, juce::PathStrokeType (1.0f));
        }
    if (display) return;
    // a striped band along the bottom
    g.saveState();
    const auto band = a.withTop (a.getBottom() - 6.0f);
    g.reduceClipRegion (band.toNearestInt());
    for (auto x = band.getX() - band.getHeight(); x < band.getRight(); x += 14.0f)
    {
        juce::Path s;
        s.addQuadrilateral (x, band.getBottom(), x + 7.0f, band.getBottom(), x + 7.0f + band.getHeight(), band.getY(), x + band.getHeight(), band.getY());
        g.setColour (p.accent.withAlpha (0.5f));
        g.fillPath (s);
    }
    g.restoreState();
}

inline void waspEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.fillEllipse (Rectangle<float> (r * 1.2f, r * 2.0f).withCentre (c));
    g.setColour (Colours::black);
    for (int i = -1; i <= 1; ++i) g.fillRect (Rectangle<float> (r * 1.3f, r * 0.22f).withCentre (c.translated (0.0f, (float) i * r * 0.5f)));
}

inline const Theme& waspTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Wasp";
        x.base = Style::neon;
        x.pal = { Colour (0xff1c1a12), Colour (0xff090805), Colour (0xffffcc00), Colour (0xfffff3c0), Colour (0xcc1e1c14), Colour (0xe20d0c08),
                  Colour (0xff8a7a3a), Colour (0xfffff8e0), Colour (0xff060504), Colour (0xff2a2a2a), Colour (0xff0e0e0e), Colour (0xff302c1c),
                  Colour (0xffffe680), Colour (0xff221f16) };
        x.canvas = canvasWasp;
        x.titleEmblem = waspEmblem;
        x.cap = Cap::rubber;
        x.pointer = Pointer::glowLine;
        x.arcGlow = 0.28f;
        return x;
    }();
    return t;
}

// === LYNX Soundfont: a sound library — deep navy, shelves of preset cartridges, azure and mint ======================

inline void canvasSoundFont (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (! reduced) bloom (g, { a.getRight() - a.getWidth() * 0.15f, a.getY() }, a.getWidth() * 0.4f, p.accent, 0.10f);
    if (display) return;
    // shelves of cartridges, each with its own little waveform, fading out to the left
    juce::Random rnd (158);
    const auto cw = 34.0f, ch = 22.0f;
    for (int shelf = 0; shelf < 3; ++shelf)
    {
        const auto y = a.getY() + 10.0f + (float) shelf * (ch + 12.0f);
        g.setColour (p.accent.withAlpha (0.10f));
        g.fillRect (Rectangle<float> (a.getX() + a.getWidth() * 0.35f, y + ch + 3.0f, a.getWidth() * 0.63f, 1.5f));
        for (auto x = a.getRight() - cw - 12.0f; x > a.getX() + a.getWidth() * 0.35f; x -= cw + 6.0f)
        {
            const auto fade = juce::jlimit (0.0f, 1.0f, (x - a.getX() - a.getWidth() * 0.35f) / (a.getWidth() * 0.3f));
            const auto card = Rectangle<float> (x, y, cw, ch);
            const auto col = rnd.nextFloat() < 0.25f ? p.accent2 : p.accent;
            g.setColour (col.withAlpha (0.08f * fade));
            g.fillRoundedRectangle (card, 3.0f);
            g.setColour (col.withAlpha (0.22f * fade));
            g.drawRoundedRectangle (card, 3.0f, 1.0f);
            const auto f = 1.0f + (float) rnd.nextInt (4);
            const auto wave = curve (24, [&] (float u) { return Point<float> (card.getX() + 4.0f + (cw - 8.0f) * u, card.getCentreY() - std::sin (u * f * 6.283f) * std::exp (-u * 2.0f) * ch * 0.3f); });
            g.setColour (col.withAlpha (0.35f * fade));
            g.strokePath (wave, juce::PathStrokeType (1.0f));
        }
    }
}

inline void soundFontEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    g.setColour (p.accent);
    g.drawRoundedRectangle (Rectangle<float> (r * 2.0f, r * 1.4f).withCentre (c), 2.0f, 1.3f);
    const auto wave = curve (16, [&] (float u) { return Point<float> (c.x - r * 0.7f + r * 1.4f * u, c.y - std::sin (u * 12.566f) * r * 0.35f); });
    g.setColour (p.accent2);
    g.strokePath (wave, juce::PathStrokeType (1.1f));
}

inline const Theme& soundFontTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "LYNX Soundfont";
        x.base = Style::glow;
        x.pal = { Colour (0xff10192c), Colour (0xff050912), Colour (0xff3fa9ff), Colour (0xff5ff2c6), Colour (0xc8121c30), Colour (0xe0080e1a),
                  Colour (0xff4a6f9a), Colour (0xffe8f3ff), Colour (0xff04070e), Colour (0xffd2e6fb), Colour (0xff5a7a9c), Colour (0xff1c2a40),
                  Colour (0xff0e2440), Colour (0xff16223a) };
        x.canvas = canvasSoundFont;
        x.titleEmblem = soundFontEmblem;
        x.cap = Cap::orb;
        x.arcGlow = 0.2f;
        return x;
    }();
    return t;
}

// === LYNX Synth: the LYNX lab — graphite, LYNX crimson, three oscillator rings and a filter sweep ===================

inline void canvasLynxSynth (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (! reduced)
    {
        bloom (g, { a.getRight() - a.getWidth() * 0.1f, a.getY() + a.getHeight() * 0.05f }, a.getWidth() * 0.45f, p.accent, 0.10f);
        bloom (g, { a.getX(), a.getBottom() }, a.getWidth() * 0.35f, p.accent2, 0.05f);
    }
    if (display) return;
    // three oscillators as interlocking rings, each carrying its wave around it
    const auto R = juce::jmin (a.getWidth(), a.getHeight()) * 0.11f;
    const auto c0 = Point<float> (a.getRight() - R * 3.2f, a.getY() + R * 1.4f);
    for (int k = 0; k < 3; ++k)
    {
        const auto c = c0.translated ((float) k * R * 1.1f, (k == 1 ? R * 0.5f : 0.0f));
        const auto ring = curve (180, [&] (float u) {
            const auto ang = u * juce::MathConstants<float>::twoPi;
            const auto ph = std::fmod (u * 8.0f, 1.0f);
            const auto v = k == 0 ? std::sin (ph * 6.283f) : k == 1 ? 1.0f - 2.0f * ph : (ph < 0.5f ? 1.0f : -1.0f);
            const auto rr = R * (1.0f + 0.08f * v);
            return Point<float> (c.x + std::cos (ang) * rr, c.y + std::sin (ang) * rr);
        });
        g.setColour ((k == 1 ? p.accent2 : p.accent).withAlpha (0.20f));
        g.strokePath (ring, juce::PathStrokeType (1.3f));
    }
    // a filter sweep along the bottom
    const auto sweep = curve (160, [&] (float u) { return Point<float> (a.getX() + a.getWidth() * u, a.getBottom() - a.getHeight() * 0.06f - squelch (u, 0.7f, 0.5f) * a.getHeight() * 0.1f); });
    g.setColour (p.accent.withAlpha (0.14f));
    g.strokePath (sweep, stroke (1.3f));
}

inline void lynxSynthEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    for (int k = 0; k < 3; ++k)
    {
        g.setColour (k == 1 ? p.accent2 : p.accent);
        g.drawEllipse (Rectangle<float> (r * 1.1f, r * 1.1f).withCentre (c.translated (((float) k - 1.0f) * r * 0.6f, 0.0f)), 1.2f);
    }
}

inline const Theme& lynxSynthTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "LYNX Synth";
        x.base = Style::glow;
        x.pal = { Colour (0xff1e1f25), Colour (0xff0a0a0d), Colour (0xffe5383b), Colour (0xffff9f80), Colour (0xd0202128), Colour (0xe40e0e12),
                  Colour (0xff6a5a60), Colour (0xfff3eeee), Colour (0xff070709), Colour (0xff3a3b42), Colour (0xff17181c), Colour (0xff2e2f36),
                  Colour (0xfffff2ee), Colour (0xff24252c) };
        x.canvas = canvasLynxSynth;
        x.titleEmblem = lynxSynthEmblem;
        x.drawCap = machinedCap;
        x.arcGlow = 0.25f;
        return x;
    }();
    return t;
}

// === Wave Traveller: travelling through a recording — a ribbon of waveform spiralling into a vanishing point ========

inline void canvasWaveTraveller (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    fillBackdrop (g, a);
    if (! reduced) bloom (g, { a.getRight() - a.getWidth() * 0.2f, a.getY() + a.getHeight() * 0.2f }, a.getWidth() * 0.35f, p.accent2, 0.12f);
    if (display) return;
    // a waveform ribbon coiling towards a vanishing point: the playhead travelling back and forth through time
    const auto vp = Point<float> (a.getRight() - a.getWidth() * 0.2f, a.getY() + a.getHeight() * 0.2f);
    const auto ribbon = curve (700, [&] (float u) {
        const auto ang = u * 18.0f;
        const auto rad = a.getWidth() * 0.5f * std::pow (1.0f - u, 1.6f);
        const auto wob = std::sin (u * 380.0f) * 6.0f * (1.0f - u);
        return Point<float> (vp.x - std::cos (ang) * (rad + wob), vp.y + std::sin (ang) * (rad + wob) * 0.45f);
    });
    g.setColour (p.accent.withAlpha (0.10f));
    g.strokePath (ribbon, juce::PathStrokeType (1.0f));
    g.setColour (p.accent2.withAlpha (0.5f));
    g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (vp));
}

inline void waveTravellerEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto& p = pal();
    const auto sp = curve (40, [&] (float u) { const auto ang = u * 9.0f; const auto rr = r * (1.0f - u); return Point<float> (c.x + std::cos (ang) * rr, c.y + std::sin (ang) * rr * 0.7f); });
    g.setColour (p.accent);
    g.strokePath (sp, juce::PathStrokeType (1.2f));
    g.setColour (p.accent2);
    g.fillEllipse (Rectangle<float> (r * 0.4f, r * 0.4f).withCentre (c));
}

inline const Theme& waveTravellerTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Wave Traveller";
        x.base = Style::neon;
        x.pal = { Colour (0xff111a26), Colour (0xff05080d), Colour (0xff35e0d0), Colour (0xffff5fb0), Colour (0xc8131d2a), Colour (0xe0080d14),
                  Colour (0xff4a7a8a), Colour (0xffe6fbff), Colour (0xa804070c), Colour (0xff2a4450), Colour (0xff0e1820), Colour (0xff1c2c38),
                  Colour (0xfff0ffff), Colour (0xff16222e) };
        x.canvas = canvasWaveTraveller;
        x.titleEmblem = waveTravellerEmblem;
        x.cap = Cap::orb;
        x.pointer = Pointer::glowLine;
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
        case Art::harmor: return &harmorTheme();
        case Art::harmless: return &harmlessTheme();
        case Art::gms: return &gmsTheme();
        case Art::morphine: return &morphineTheme();
        case Art::ogun: return &ogunTheme();
        case Art::poizone: return &poizoneTheme();
        case Art::kepler: return &keplerTheme();
        case Art::keplerExo: return &keplerExoTheme();
        case Art::transistorBass: return &transistorBassTheme();
        case Art::sawer: return &sawerTheme();
        case Art::beepMap: return &beepMapTheme();
        case Art::flKeys: return &flKeysTheme();
        case Art::granulizer: return &granulizerTheme();
        case Art::drumaxx: return &drumaxxTheme();
        case Art::drumpad: return &drumpadTheme();
        case Art::bassDrum: return &bassDrumTheme();
        case Art::booBass: return &booBassTheme();
        case Art::drumSynth: return &drumSynthTheme();
        case Art::dx10: return &dx10Theme();
        case Art::miniSynth: return &miniSynthTheme();
        case Art::plucked: return &pluckedTheme();
        case Art::simSynth: return &simSynthTheme();
        case Art::autogun: return &autogunTheme();
        case Art::slayer: return &slayerTheme();
        case Art::fruitKick: return &fruitKickTheme();
        case Art::wasp: return &waspTheme();
        case Art::soundFont: return &soundFontTheme();
        case Art::lynxSynth: return &lynxSynthTheme();
        case Art::waveTraveller: return &waveTravellerTheme();
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
        case InstrumentKind::harmor: return Art::harmor;
        case InstrumentKind::harmless: return Art::harmless;
        case InstrumentKind::gms: return Art::gms;
        case InstrumentKind::morphine: return Art::morphine;
        case InstrumentKind::ogun: return Art::ogun;
        case InstrumentKind::poizone: return Art::poizone;
        case InstrumentKind::kepler: return Art::kepler;
        case InstrumentKind::keplerExo: return Art::keplerExo;
        case InstrumentKind::transistorbass: return Art::transistorBass;
        case InstrumentKind::sawer: return Art::sawer;
        case InstrumentKind::beepmap: return Art::beepMap;
        case InstrumentKind::flkeys: return Art::flKeys;
        case InstrumentKind::granulizer: return Art::granulizer;
        case InstrumentKind::drumaxx: return Art::drumaxx;
        case InstrumentKind::drumpad: return Art::drumpad;
        case InstrumentKind::bassdrum: return Art::bassDrum;
        case InstrumentKind::boobass: return Art::booBass;
        case InstrumentKind::drumsynth: return Art::drumSynth;
        case InstrumentKind::dx10: return Art::dx10;
        case InstrumentKind::minisynth: return Art::miniSynth;
        case InstrumentKind::plucked: return Art::plucked;
        case InstrumentKind::simsynth: return Art::simSynth;
        case InstrumentKind::autogun: return Art::autogun;
        case InstrumentKind::slayer: return Art::slayer;
        case InstrumentKind::fruitkick: return Art::fruitKick;
        case InstrumentKind::wasp: return Art::wasp;
        case InstrumentKind::soundfont: return Art::soundFont;
        case InstrumentKind::synth: return Art::lynxSynth;
        case InstrumentKind::wavetraveller: return Art::waveTraveller;
        default: return Art::none;
    }
}

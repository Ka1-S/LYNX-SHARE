// The "Themed" plugin style (round 170j): a plugin with ART of its own is drawn in a theme that evokes the mood of the
// plugin it is inspired by — its palette, its materials and an illustration — instead of one of the four shared
// styles. Pilot set: Sakura (cherry blossoms at dusk), Toxic Biohazard (a containment lab), Sytrus (FM orbits),
// 3x Osc (a vintage lab bench with phosphor traces), Facet EQ (studio glass, clean and precise) and Nebula (deep
// space). Art directions: the tracker's plugin-art-0.1.24.md.
//
// Everything here is LYNX's own procedural vector drawing (juce::Path, gradients, seeded random): no bitmap, logo,
// skin or artwork of the original plugins is copied, traced or extracted. Layouts and gestures are the editors'
// own and never change; only the drawing does. Static parts are drawn into the kit's cached layers (canvas, knobs);
// the moving parts live in skin::ArtLayer (below), which repaints only small areas at ≤ 30 Hz while it is shown.
// Included by PluginSkin.cpp (it shares that file's helpers).

namespace artkit
{
using juce::Colour;
using juce::ColourGradient;
using juce::Point;
using juce::Rectangle;
namespace Colours = juce::Colours;

struct Palette
{
    Colour bgTop, bgBottom;          // the canvas
    Colour accent, accent2;          // main light, second light
    Colour panelTop, panelBottom;    // a section's glass (translucent)
    Colour rim;                      // a section's edge
    Colour title;                    // section titles
    Colour well;                     // display wells
    Colour capTop, capBottom;        // knob caps, fader thumbs
    Colour track;                    // knob / fader tracks
    Colour pointer;                  // knob pointers
    Colour plate;                    // value plates, selectors, unlit buttons
};

inline const Palette& palette (Art a)
{
    static const Palette sakura { Colour (0xff2b1731), Colour (0xff110a15), Colour (0xffff7aa5), Colour (0xffffd3e1), Colour (0xc4281b2c), Colour (0xdc170f1b),
                                  Colour (0xffffa9c4), Colour (0xffffe6ee), Colour (0xff100810), Colour (0xfff8eef0), Colour (0xffd8bfc8), Colour (0xff3d2736),
                                  Colour (0xffd8496f), Colour (0xff2a1c2a) };
    static const Palette toxic { Colour (0xff101a10), Colour (0xff040704), Colour (0xff9dff3a), Colour (0xffffd60a), Colour (0xcc141d13), Colour (0xe409100a),
                                 Colour (0xff7cff45), Colour (0xffdaffc4), Colour (0xff030603), Colour (0xff343a33), Colour (0xff0c0f0c), Colour (0xff1c2a19),
                                 Colour (0xffc6ff6e), Colour (0xff121a11) };
    static const Palette sytrus { Colour (0xff1a1642), Colour (0xff08071a), Colour (0xffa67cff), Colour (0xff5ce1ff), Colour (0xc41d1a44), Colour (0xdc0f0e27),
                                  Colour (0xffb9a0ff), Colour (0xffece4ff), Colour (0xff06051a), Colour (0xff4a4390), Colour (0xff100e2c), Colour (0xff27234e),
                                  Colour (0xffeaf9ff), Colour (0xff191636) };
    static const Palette threeOsc { Colour (0xff3a3c40), Colour (0xff1d1e21), Colour (0xffffa53d), Colour (0xffffd28a), Colour (0xd42d2f33), Colour (0xe61f2023),
                                    Colour (0xff9a9da3), Colour (0xfff1e7cf), Colour (0xff0b0c0a), Colour (0xff2e2e2e), Colour (0xff0d0d0d), Colour (0xff45474c),
                                    Colour (0xfff1e7cf), Colour (0xff232427) };
    static const Palette facet { Colour (0xff161e2b), Colour (0xff090d14), Colour (0xffffc95a), Colour (0xff66d6ff), Colour (0xd8172030), Colour (0xe80e131c),
                                 Colour (0xff7189ab), Colour (0xffe7eef8), Colour (0xff070a10), Colour (0xff2a3446), Colour (0xff141a25), Colour (0xff2a3446),
                                 Colour (0xfff4f7fb), Colour (0xff151c28) };
    static const Palette nebula { Colour (0xff0c0b22), Colour (0xff020208), Colour (0xffff5cd3), Colour (0xff3fe2ff), Colour (0xb4141133), Colour (0xd8070617),
                                  Colour (0xffa184ff), Colour (0xfff1eaff), Colour (0xff03030b), Colour (0xff3a2a6e), Colour (0xff0b0920), Colour (0xff221c44),
                                  Colour (0xffffffff), Colour (0xff120f2a) };
    switch (a)
    {
        case Art::sakura: return sakura;
        case Art::toxic: return toxic;
        case Art::sytrus: return sytrus;
        case Art::threeOsc: return threeOsc;
        case Art::facet: return facet;
        case Art::nebula: return nebula;
        default: return facet;
    }
}

inline const Palette& pal() { return palette (currentArt); }

inline juce::PathStrokeType stroke (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); }

// --- shared drawing --------------------------------------------------------------------------------

// a five-petal blossom (each petal notched at its tip), `r` = petal length; rotation in radians
inline void blossom (juce::Graphics& g, Point<float> c, float r, float rotation, Colour petal, Colour centre, float alpha)
{
    juce::Path p;
    for (int i = 0; i < 5; ++i)
    {
        juce::Path one;
        const auto w = r * 0.62f;
        one.startNewSubPath (0.0f, 0.0f);
        one.cubicTo (-w * 0.9f, -r * 0.25f, -w, -r * 0.85f, -w * 0.28f, -r);
        one.lineTo (0.0f, -r * 0.86f); // the notch
        one.lineTo (w * 0.28f, -r);
        one.cubicTo (w, -r * 0.85f, w * 0.9f, -r * 0.25f, 0.0f, 0.0f);
        one.closeSubPath();
        p.addPath (one, juce::AffineTransform::rotation (rotation + juce::MathConstants<float>::twoPi * (float) i / 5.0f));
    }
    p.applyTransform (juce::AffineTransform::translation (c.x, c.y));
    g.setGradientFill (ColourGradient (petal.brighter (0.25f).withAlpha (alpha), c.x, c.y - r, petal.darker (0.15f).withAlpha (alpha), c.x, c.y + r, false));
    g.fillPath (p);
    // a blush at the heart and fine veins
    g.setGradientFill (ColourGradient (Colour (0xffe0457a).withAlpha (0.55f * alpha), c.x, c.y, Colour (0xffe0457a).withAlpha (0.0f), c.x + r * 0.7f, c.y, true));
    g.fillPath (p);
    g.setColour (centre.withAlpha (alpha));
    for (int i = 0; i < 7; ++i)
    {
        const auto a = rotation * 1.3f + juce::MathConstants<float>::twoPi * (float) i / 7.0f;
        const Point<float> d { std::sin (a), -std::cos (a) };
        g.drawLine ({ c, c + d * r * 0.42f }, juce::jmax (0.5f, r * 0.05f));
        g.fillEllipse (Rectangle<float> (r * 0.12f, r * 0.12f).withCentre (c + d * r * 0.45f));
    }
}

// one falling petal (a single notched petal), centred at c
inline void petal (juce::Graphics& g, Point<float> c, float r, float rotation, float squash, Colour col, float alpha)
{
    juce::Path one;
    const auto w = r * 0.62f;
    one.startNewSubPath (0.0f, r * 0.5f);
    one.cubicTo (-w * 0.9f, r * 0.25f, -w, -r * 0.35f, -w * 0.28f, -r * 0.5f);
    one.lineTo (0.0f, -r * 0.36f);
    one.lineTo (w * 0.28f, -r * 0.5f);
    one.cubicTo (w, -r * 0.35f, w * 0.9f, r * 0.25f, 0.0f, r * 0.5f);
    one.closeSubPath();
    one.applyTransform (juce::AffineTransform::scale (juce::jmax (0.15f, squash), 1.0f).rotated (rotation).translated (c.x, c.y));
    g.setGradientFill (ColourGradient (col.brighter (0.3f).withAlpha (alpha), c.x, c.y - r * 0.5f, col.darker (0.1f).withAlpha (alpha), c.x, c.y + r * 0.5f, false));
    g.fillPath (one);
}

// the biohazard trefoil (the public hazard symbol, built from circles): three crescents crossed by a ring, holes
// cut out — computed per pixel into a cached alpha mask (JUCE paths have no boolean operations)
inline bool trefoilInside (float u, float v)
{
    const auto rr = std::sqrt (u * u + v * v);
    if (rr < 0.15f || rr > 1.0f) return false;
    bool crescent = false, hole = false;
    for (int i = 0; i < 3; ++i)
    {
        const auto a = juce::MathConstants<float>::twoPi * (float) i / 3.0f;
        const auto dx = std::sin (a), dy = -std::cos (a);
        const auto lx = u - dx * 0.42f, ly = v - dy * 0.42f;
        const auto hx = u - dx * 0.56f, hy = v - dy * 0.56f;
        const auto inLobe = lx * lx + ly * ly < 0.58f * 0.58f;
        const auto inHole = hx * hx + hy * hy < 0.42f * 0.42f;
        hole = hole || inHole;
        crescent = crescent || (inLobe && ! inHole);
    }
    const auto ring = rr > 0.49f && rr < 0.60f;
    return (crescent || ring) && ! hole;
}

inline const juce::Image& trefoilMask (int px)
{
    static std::map<int, juce::Image> cache;
    if (cache.size() > 24) cache.clear();
    auto& img = cache[px];
    if (img.isNull())
    {
        img = juce::Image (juce::Image::ARGB, px, px, true);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < px; ++y)
            for (int x = 0; x < px; ++x)
            {
                int n = 0;
                for (int sy = 0; sy < 3; ++sy)
                    for (int sx = 0; sx < 3; ++sx)
                        n += trefoilInside (((float) x + ((float) sx + 0.5f) / 3.0f) / (float) px * 2.0f - 1.0f,
                                            ((float) y + ((float) sy + 0.5f) / 3.0f) / (float) px * 2.0f - 1.0f) ? 1 : 0;
                bd.setPixelColour (x, y, Colours::white.withAlpha ((float) n / 9.0f));
            }
    }
    return img;
}

inline void drawTrefoil (juce::Graphics& g, Point<float> c, float r, Colour col, float alpha)
{
    if (r < 1.5f) return;
    const auto px = juce::jlimit (8, 512, ((int) std::ceil (r * 2.0f * pixelScale (g)) + 7) / 8 * 8);
    g.setColour (col.withAlpha (alpha));
    g.drawImage (trefoilMask (px), Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), juce::RectanglePlacement::stretchToFit, true);
}

// diagonal hazard stripes in `a` (yellow and black)
inline void hazardStripes (juce::Graphics& g, Rectangle<float> a, float alpha)
{
    g.saveState();
    g.reduceClipRegion (a.toNearestInt());
    g.setColour (Colour (0xff15140c).withAlpha (alpha));
    g.fillRect (a);
    const auto step = juce::jmax (8.0f, a.getHeight() * 1.6f);
    g.setColour (pal().accent2.withAlpha (alpha * 0.9f));
    for (auto x = a.getX() - a.getHeight(); x < a.getRight() + a.getHeight(); x += step)
    {
        juce::Path s;
        s.startNewSubPath (x, a.getBottom());
        s.lineTo (x + a.getHeight(), a.getY());
        s.lineTo (x + a.getHeight() + step * 0.5f, a.getY());
        s.lineTo (x + step * 0.5f, a.getBottom());
        s.closeSubPath();
        g.fillPath (s);
    }
    g.restoreState();
}

// a deterministic starfield in `a`
inline void stars (juce::Graphics& g, Rectangle<float> a, int count, juce::int64 seed, float alpha)
{
    juce::Random rnd (seed);
    for (int i = 0; i < count; ++i)
    {
        const Point<float> p { a.getX() + rnd.nextFloat() * a.getWidth(), a.getY() + rnd.nextFloat() * a.getHeight() };
        const auto big = rnd.nextFloat();
        const auto d = 0.6f + big * big * big * 2.2f;
        const auto tint = rnd.nextFloat() < 0.15f ? pal().accent2 : rnd.nextFloat() < 0.1f ? pal().accent : Colour (0xfff4f1ff);
        const auto al = alpha * (0.25f + 0.75f * rnd.nextFloat());
        if (d > 1.9f && ! reduced) bloom (g, p, d * 3.5f, tint, al * 0.35f);
        g.setColour (tint.withAlpha (al));
        g.fillEllipse (Rectangle<float> (d, d).withCentre (p));
    }
}

// --- canvases ---------------------------------------------------------------------------------------

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

inline void lissajous (juce::Path& path, Point<float> c, float rx, float ry, float fa, float fb, float phase, int n = 360)
{
    for (int i = 0; i <= n; ++i)
    {
        const auto t = juce::MathConstants<float>::twoPi * (float) i / (float) n;
        const Point<float> q { c.x + rx * std::sin (fa * t + phase), c.y + ry * std::sin (fb * t) };
        if (i == 0) path.startNewSubPath (q);
        else path.lineTo (q);
    }
}

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

inline void canvas (juce::Graphics& g, Rectangle<float> a, Colour, bool display)
{
    switch (currentArt)
    {
        case Art::sakura: canvasSakura (g, a, display); break;
        case Art::toxic: canvasToxic (g, a, display); break;
        case Art::sytrus: canvasSytrus (g, a, display); break;
        case Art::threeOsc: canvasThreeOsc (g, a, display); break;
        case Art::nebula: canvasNebula (g, a, display); break;
        default: canvasFacet (g, a, display); break;
    }
}

// --- surfaces --------------------------------------------------------------------------------------

inline bool headerBar (juce::Graphics& g, Rectangle<float> a, Colour)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.bgTop.brighter (0.25f), 0.0f, a.getY(), p.bgBottom.brighter (0.1f), 0.0f, a.getBottom(), false));
    g.fillRect (a);
    if (currentArt == Art::toxic)
        hazardStripes (g, a.withTop (a.getBottom() - 4.0f), 0.9f);
    if (currentArt == Art::nebula) stars (g, a, (int) (a.getWidth() / 9.0f), 3, 0.6f);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.07f), 0.0f, a.getY(), Colours::white.withAlpha (0.0f), 0.0f, a.getCentreY(), false));
    g.fillRect (a.withHeight (a.getHeight() * 0.5f));
    const auto lineW = juce::jmin (a.getWidth(), 720.0f);
    ColourGradient line (p.accent, a.getX(), 0.0f, p.accent2.withAlpha (0.0f), a.getX() + lineW, 0.0f, false);
    line.addColour (0.5, p.accent2.withAlpha (0.7f));
    g.setGradientFill (line);
    g.fillRect (Rectangle<float> (a.getX(), a.getBottom() - 1.5f, lineW, 1.5f));
    return true;
}

inline bool section (juce::Graphics& g, Rectangle<float> a, float corner, bool raised, Colour)
{
    const auto& p = pal();
    if (! reduced)
    {
        g.setColour (Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (a.translated (0.0f, 3.0f).expanded (1.0f), corner + 1.0f);
    }
    g.setGradientFill (ColourGradient (raised ? p.panelTop.brighter (0.08f) : p.panelTop, 0.0f, a.getY(), p.panelBottom, 0.0f, a.getBottom(), false));
    g.fillRoundedRectangle (a, corner);
    switch (currentArt)
    {
        case Art::threeOsc: // a screwed-on plate with a brushed face
        {
            if (a.getWidth() > 60.0f && a.getHeight() > 40.0f)
                for (const auto& c : { a.getTopLeft().translated (6.0f, 6.0f), a.getTopRight().translated (-6.0f, 6.0f), a.getBottomLeft().translated (6.0f, -6.0f),
                                       a.getBottomRight().translated (-6.0f, -6.0f) })
                    styles::screw (g, c, 5.0f);
            break;
        }
        case Art::toxic: // a hazard notch in the top left corner
            if (a.getWidth() > 50.0f && a.getHeight() > 30.0f)
            {
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
            break;
        case Art::sakura: // washi fibres
        {
            juce::Random rnd ((juce::int64) (a.getWidth() * 31.0f + a.getHeight()));
            g.setColour (Colour (0xffffd9e4).withAlpha (0.035f));
            for (int i = 0; i < (int) (a.getWidth() * a.getHeight() / 900.0f); ++i)
            {
                const Point<float> s { a.getX() + rnd.nextFloat() * a.getWidth(), a.getY() + rnd.nextFloat() * a.getHeight() };
                const auto ang = rnd.nextFloat() * 6.28f, len = 4.0f + rnd.nextFloat() * 10.0f;
                g.drawLine ({ s, s + Point<float> (std::cos (ang), std::sin (ang)) * len }, 0.7f);
            }
            break;
        }
        default: break;
    }
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.06f), 0.0f, a.getY(), Colours::white.withAlpha (0.0f), 0.0f, a.getY() + juce::jmin (40.0f, a.getHeight() * 0.5f), false));
    g.fillRoundedRectangle (a, corner);
    // the rim: the art's light along the top, fading down the sides
    ColourGradient rim (p.rim.withAlpha (currentArt == Art::facet ? 0.35f : 0.55f), 0.0f, a.getY(), p.rim.withAlpha (0.10f), 0.0f, a.getBottom(), false);
    if (currentArt == Art::sytrus || currentArt == Art::nebula)
        rim = ColourGradient (p.accent.withAlpha (0.55f), a.getX(), a.getY(), p.accent2.withAlpha (0.35f), a.getRight(), a.getBottom(), false);
    g.setGradientFill (rim);
    g.drawRoundedRectangle (a.reduced (0.5f), corner, currentArt == Art::facet ? 1.0f : 1.2f);
    return true;
}

inline bool sectionTitle (juce::Graphics& g, Rectangle<float> strip, const juce::String& text, Colour, float fontSize)
{
    const auto& p = pal();
    g.setFont (juce::Font (theme::font (fontSize, true)).withExtraKerningFactor (currentArt == Art::facet ? 0.06f : 0.12f));
    const auto w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
    const auto box = Rectangle<float> (w + 26.0f, strip.getHeight()).withCentre (strip.getCentre());
    const auto y = strip.getCentreY();
    // hairlines to the edges
    g.setGradientFill (ColourGradient (p.rim.withAlpha (0.0f), strip.getX() + 8.0f, 0.0f, p.rim.withAlpha (0.4f), box.getX(), 0.0f, false));
    g.fillRect (Rectangle<float> (strip.getX() + 8.0f, y, juce::jmax (0.0f, box.getX() - strip.getX() - 10.0f), 1.0f));
    g.setGradientFill (ColourGradient (p.rim.withAlpha (0.4f), box.getRight(), 0.0f, p.rim.withAlpha (0.0f), strip.getRight() - 8.0f, 0.0f, false));
    g.fillRect (Rectangle<float> (box.getRight() + 2.0f, y, juce::jmax (0.0f, strip.getRight() - 8.0f - box.getRight()), 1.0f));
    // the emblem left of the text
    const auto ec = Point<float> (box.getX() + 8.0f, y);
    const auto er = juce::jmax (3.0f, fontSize * 0.42f);
    switch (currentArt)
    {
        case Art::sakura: blossom (g, ec, er, 0.3f, p.accent2, Colour (0xfffff0b8), 1.0f); break;
        case Art::toxic: drawTrefoil (g, ec, er * 1.15f, p.accent, 0.95f); break;
        case Art::threeOsc:
            g.setColour (p.accent);
            g.fillEllipse (Rectangle<float> (er * 1.2f, er * 1.2f).withCentre (ec));
            g.setColour (Colours::white.withAlpha (0.6f));
            g.fillEllipse (Rectangle<float> (er * 0.45f, er * 0.45f).withCentre (ec.translated (-er * 0.15f, -er * 0.15f)));
            break;
        default:
        {
            if (! reduced) bloom (g, ec, er * 3.0f, p.accent, 0.4f);
            juce::Path orbit;
            orbit.addEllipse (Rectangle<float> (er * 2.2f, er * 1.0f).withCentre (ec));
            g.setColour (p.accent2.withAlpha (0.8f));
            if (currentArt != Art::facet) g.strokePath (orbit, juce::PathStrokeType (0.8f));
            g.setColour (p.accent);
            g.fillEllipse (Rectangle<float> (er * 0.9f, er * 0.9f).withCentre (ec));
            break;
        }
    }
    g.setColour (p.title);
    g.drawText (text, box.withTrimmedLeft (18.0f), juce::Justification::centredLeft, false);
    return true;
}

inline bool well (juce::Graphics& g, Rectangle<float> a, float corner, Colour glow)
{
    const auto& p = pal();
    g.setColour (p.well);
    g.fillRoundedRectangle (a, corner);
    if (! reduced && glow.getAlpha() > 0)
    {
        g.saveState();
        g.reduceClipRegion (a.toNearestInt());
        bloom (g, { a.getCentreX(), a.getBottom() }, a.getWidth() * 0.55f, p.accent, 0.12f);
        g.restoreState();
    }
    const auto sh = juce::jmin (8.0f, a.getHeight() * 0.3f);
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.55f), 0.0f, a.getY(), Colours::transparentBlack, 0.0f, a.getY() + sh, false));
    g.fillRoundedRectangle (a.withHeight (sh), corner);
    g.setColour (p.rim.withAlpha (0.22f));
    g.drawRoundedRectangle (a.reduced (0.5f), corner, 1.0f);
    return true;
}

inline bool floatingPanel (juce::Graphics& g, Rectangle<float> r, Colour, float corner)
{
    const auto& p = pal();
    juce::DropShadow (Colours::black.withAlpha (0.6f), 20, { 0, 6 }).drawForRectangle (g, r.toNearestInt());
    g.setGradientFill (ColourGradient (p.panelTop.withAlpha (0.97f), 0.0f, r.getY(), p.panelBottom.withAlpha (0.98f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setGradientFill (ColourGradient (p.accent.withAlpha (0.6f), r.getX(), r.getY(), p.accent2.withAlpha (0.4f), r.getRight(), r.getBottom(), false));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.2f);
    return true;
}

// --- controls --------------------------------------------------------------------------------------

inline float knobScale() { return currentArt == Art::threeOsc ? 1.04f : 1.0f; }

inline juce::Path arc (Point<float> c, float r, float from, float to)
{
    juce::Path p;
    p.addCentredArc (c.x, c.y, r, r, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
    return p;
}

inline void knob (juce::Graphics& g, Rectangle<float> area, const KnobLook& k)
{
    const auto& p = pal();
    const auto size = area.getWidth();
    const auto c = area.getCentre();
    const auto small = size < 26.0f;
    const auto fx = ! small && ! reduced;
    const auto value = juce::jlimit (0.0f, 1.0f, k.value);
    const auto hover = juce::jlimit (0.0f, 1.0f, k.active ? 1.0f : k.hover);
    const auto from = k.bipolar ? arcStart + arcRange * 0.5f : arcStart;
    const auto to = arcStart + arcRange * value;
    const auto lit = k.enabled ? (k.active ? p.accent.brighter (0.25f) : p.accent) : theme::colour::textDisabled;
    const auto lit2 = k.enabled ? p.accent2 : theme::colour::textDisabled;
    const Point<float> dir { std::sin (to), -std::cos (to) };

    const auto thin = currentArt == Art::facet;
    const auto t = juce::jlimit (thin ? 1.6f : 2.0f, thin ? 3.5f : 7.0f, size * (thin ? 0.05f : 0.085f));
    const auto ringR = size * 0.5f - t * 0.7f;

    // scale ticks (3x Osc: a printed 0-10 scale; Toxic: hazard notches)
    if ((currentArt == Art::threeOsc || currentArt == Art::toxic) && size >= 30.0f)
    {
        const int n = currentArt == Art::threeOsc ? 11 : 9;
        for (int i = 0; i < n; ++i)
        {
            const auto ang = arcStart + arcRange * (float) i / (float) (n - 1);
            const Point<float> d { std::sin (ang), -std::cos (ang) };
            g.setColour (currentArt == Art::threeOsc ? p.title.withAlpha (0.65f) : p.accent2.withAlpha (0.7f));
            g.drawLine ({ c + d * (size * 0.5f - 0.5f), c + d * (size * 0.5f - (i % 5 == 0 ? 4.5f : 2.5f)) }, currentArt == Art::threeOsc ? 1.0f : 1.6f);
        }
    }
    const auto r = (currentArt == Art::threeOsc || currentArt == Art::toxic) && size >= 30.0f ? ringR - 4.0f : ringR;

    // the track and the lit value arc
    g.setColour (p.track.withAlpha (k.enabled ? 1.0f : 0.6f));
    g.strokePath (arc (c, r, arcStart, arcStart + arcRange), juce::PathStrokeType (t, juce::PathStrokeType::curved, currentArt == Art::threeOsc ? juce::PathStrokeType::butt : juce::PathStrokeType::rounded));
    if (std::abs (to - from) > 0.002f)
    {
        const auto v = arc (c, r, from, to);
        if (fx && k.enabled && currentArt != Art::facet && currentArt != Art::threeOsc)
        {
            g.setColour (lit.withAlpha (currentArt == Art::toxic ? 0.35f : 0.22f));
            g.strokePath (v, stroke (t * (currentArt == Art::toxic ? 3.2f : 2.4f)));
        }
        if (currentArt == Art::sytrus || currentArt == Art::nebula)
            g.setGradientFill (ColourGradient (lit2, c.x - r, c.y + r, lit, c.x + r, c.y - r, false));
        else
            g.setColour (lit);
        g.strokePath (v, stroke (t));
    }

    // the cap
    const auto capR = juce::jmax (3.0f, r - t * (thin ? 2.4f : 1.6f));
    const auto cap = Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
    if (fx)
    {
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillEllipse (cap.translated (0.0f, capR * 0.12f).expanded (1.0f));
    }
    switch (currentArt)
    {
        case Art::sakura: // porcelain
            g.setGradientFill (ColourGradient (p.capTop.brighter (hover * 0.05f), c.x - capR * 0.4f, c.y - capR * 0.6f, p.capBottom, c.x + capR * 0.5f, c.y + capR, true));
            g.fillEllipse (cap);
            g.setColour (p.accent.withAlpha (0.35f));
            g.drawEllipse (cap.reduced (capR * 0.18f), 0.8f);
            break;
        case Art::sytrus: // a glass orb
        case Art::nebula: // a small planet
        {
            ColourGradient orb (p.capTop.brighter (0.3f + hover * 0.2f), c.x - capR * 0.45f, c.y - capR * 0.5f, p.capBottom, c.x + capR * 0.7f, c.y + capR * 0.8f, true);
            g.setGradientFill (orb);
            g.fillEllipse (cap);
            if (fx && currentArt == Art::nebula) // a thin ring around the planet
            {
                juce::Path ringP;
                ringP.addEllipse (Rectangle<float> (capR * 2.7f, capR * 0.7f).withCentre (c));
                ringP.applyTransform (juce::AffineTransform::rotation (-0.35f, c.x, c.y));
                g.setColour (p.accent2.withAlpha (0.35f));
                g.strokePath (ringP, juce::PathStrokeType (0.9f));
            }
            if (fx)
            {
                g.setColour (Colours::white.withAlpha (0.28f));
                g.fillEllipse (Rectangle<float> (capR * 0.6f, capR * 0.35f).withCentre ({ c.x - capR * 0.3f, c.y - capR * 0.5f }));
            }
            break;
        }
        case Art::toxic: // black rubber with knurling
        {
            g.setGradientFill (ColourGradient (p.capTop.brighter (hover * 0.15f), c.x, cap.getY(), p.capBottom, c.x, cap.getBottom(), false));
            g.fillEllipse (cap);
            if (fx)
            {
                g.setColour (Colours::black.withAlpha (0.55f));
                for (int i = 0; i < 24; ++i)
                {
                    const auto ang = juce::MathConstants<float>::twoPi * (float) i / 24.0f;
                    const Point<float> d { std::sin (ang), -std::cos (ang) };
                    g.drawLine ({ c + d * capR * 0.82f, c + d * capR * 0.98f }, 1.0f);
                }
            }
            break;
        }
        case Art::threeOsc: // bakelite with a skirt
        {
            g.setColour (Colour (0xff1a1a1a));
            g.fillEllipse (cap.expanded (capR * 0.08f));
            g.setGradientFill (ColourGradient (p.capTop.brighter (0.2f + hover * 0.1f), c.x, cap.getY(), p.capBottom, c.x, cap.getBottom(), false));
            g.fillEllipse (cap.reduced (capR * 0.14f));
            if (fx)
            {
                g.setColour (Colours::white.withAlpha (0.10f));
                g.drawEllipse (cap.reduced (capR * 0.14f), 1.0f);
            }
            break;
        }
        default: // facet: flat, precise
            g.setGradientFill (ColourGradient (p.capTop.brighter (hover * 0.12f), c.x, cap.getY(), p.capBottom, c.x, cap.getBottom(), false));
            g.fillEllipse (cap);
            g.setColour (Colours::white.withAlpha (0.10f + 0.1f * hover));
            g.drawEllipse (cap.reduced (0.5f), 1.0f);
            break;
    }

    // the pointer
    const auto pw = juce::jlimit (1.2f, 3.5f, size * (thin ? 0.035f : 0.05f));
    const auto pointerCol = k.enabled ? p.pointer : theme::colour::textSecondary;
    if (currentArt == Art::sytrus || currentArt == Art::nebula)
    {
        // a bright body on its orbit (the arc's tip) and a faint spoke
        g.setColour (pointerCol.withAlpha (0.35f));
        g.drawLine ({ c, c + dir * capR * 0.9f }, pw * 0.8f);
        const auto tip = c + dir * r;
        if (fx && k.enabled) bloom (g, tip, t * 3.0f, lit, 0.6f);
        g.setColour (pointerCol);
        g.fillEllipse (Rectangle<float> (t * 1.3f, t * 1.3f).withCentre (tip));
    }
    else
    {
        juce::Path ptr;
        ptr.startNewSubPath (c + dir * capR * (currentArt == Art::threeOsc ? 0.15f : 0.35f));
        ptr.lineTo (c + dir * capR * 0.86f);
        if (currentArt == Art::toxic && fx && k.enabled)
        {
            g.setColour (lit.withAlpha (0.4f));
            g.strokePath (ptr, stroke (pw * 3.0f));
        }
        g.setColour (currentArt == Art::toxic && k.enabled ? lit.interpolatedWith (Colours::white, 0.35f) : pointerCol);
        g.strokePath (ptr, stroke (pw));
        if (currentArt == Art::sakura && size >= 34.0f && k.enabled) // a blossom rides the arc's tip
            blossom (g, c + dir * r, t * 1.25f, to, p.accent2, Colour (0xfffff0b8), 1.0f);
    }
}

inline bool slider (juce::Graphics& g, Rectangle<float> tr, float v, bool vertical, bool bipolar, Colour, bool hot, bool enabled, float thumbLength, float thumbThickness)
{
    const auto& p = pal();
    v = juce::jlimit (0.0f, 1.0f, v);
    const auto track = vertical ? tr.withSizeKeepingCentre (juce::jmin (4.0f, tr.getWidth()), tr.getHeight()) : tr.withSizeKeepingCentre (tr.getWidth(), juce::jmin (4.0f, tr.getHeight()));
    g.setColour (p.track);
    g.fillRoundedRectangle (track, 2.0f);
    const auto lit = enabled ? p.accent : theme::colour::textDisabled;
    auto fill = track;
    const auto pos = vertical ? track.getBottom() - v * track.getHeight() : track.getX() + v * track.getWidth();
    if (vertical)
    {
        const auto zero = bipolar ? track.getCentreY() : track.getBottom();
        fill = Rectangle<float> (track.getX(), juce::jmin (zero, pos), track.getWidth(), std::abs (zero - pos));
    }
    else
    {
        const auto zero = bipolar ? track.getCentreX() : track.getX();
        fill = Rectangle<float> (juce::jmin (zero, pos), track.getY(), std::abs (zero - pos), track.getHeight());
    }
    if (! reduced && enabled && currentArt != Art::facet)
    {
        g.setColour (lit.withAlpha (0.25f));
        g.fillRoundedRectangle (fill.expanded (2.0f), 3.0f);
    }
    if (currentArt == Art::sytrus || currentArt == Art::nebula)
        g.setGradientFill (ColourGradient (enabled ? p.accent2 : lit, fill.getX(), fill.getBottom(), lit, fill.getRight(), fill.getY(), false));
    else
        g.setColour (lit);
    g.fillRoundedRectangle (fill, 2.0f);
    // the thumb
    const auto thumb = vertical ? Rectangle<float> (thumbLength, thumbThickness).withCentre ({ track.getCentreX(), pos })
                                : Rectangle<float> (thumbThickness, thumbLength).withCentre ({ pos, track.getCentreY() });
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (thumb.translated (0.0f, 1.5f), 3.0f);
    g.setGradientFill (ColourGradient (p.capTop.brighter (hot ? 0.25f : 0.0f), 0.0f, thumb.getY(), p.capBottom, 0.0f, thumb.getBottom(), false));
    g.fillRoundedRectangle (thumb, 3.0f);
    g.setColour (enabled ? (currentArt == Art::sakura || currentArt == Art::threeOsc ? p.pointer : lit) : theme::colour::textDisabled);
    if (vertical) g.fillRect (thumb.withSizeKeepingCentre (thumb.getWidth() - 6.0f, 1.6f));
    else g.fillRect (thumb.withSizeKeepingCentre (1.6f, thumb.getHeight() - 6.0f));
    return true;
}

inline bool button (juce::Graphics& g, Rectangle<float> r, bool lit, bool hover, Colour, float corner)
{
    const auto& p = pal();
    if (lit)
    {
        if (! reduced) bloom (g, r.getCentre(), juce::jmax (r.getWidth(), r.getHeight()) * 0.75f, p.accent, 0.22f);
        if (currentArt == Art::sytrus || currentArt == Art::nebula)
            g.setGradientFill (ColourGradient (p.accent, r.getX(), r.getY(), p.accent2.interpolatedWith (p.accent, 0.4f), r.getRight(), r.getBottom(), false));
        else
            g.setGradientFill (ColourGradient (p.accent.brighter (0.15f), 0.0f, r.getY(), p.accent.darker (0.25f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, corner);
        g.setColour (Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
        return true;
    }
    g.setGradientFill (ColourGradient (p.plate.brighter (hover ? 0.25f : 0.1f), 0.0f, r.getY(), p.plate.darker (0.2f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (p.rim.withAlpha (hover ? 0.55f : 0.28f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    return true;
}

inline bool switchPill (juce::Graphics& g, Rectangle<float> box, bool on, Colour, bool enabled)
{
    const auto& p = pal();
    const auto r = box.getHeight() * 0.5f;
    g.setColour (on && enabled ? p.accent.withAlpha (0.85f) : p.track);
    g.fillRoundedRectangle (box, r);
    const auto knobD = box.getHeight() - 4.0f;
    const auto kx = on ? box.getRight() - 2.0f - knobD : box.getX() + 2.0f;
    g.setGradientFill (ColourGradient (p.capTop.brighter (0.3f), 0.0f, box.getY(), p.capBottom, 0.0f, box.getBottom(), false));
    g.fillEllipse (kx, box.getY() + 2.0f, knobD, knobD);
    return true;
}

inline bool selector (juce::Graphics& g, Rectangle<float> r, bool hover, float corner)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.plate.brighter (hover ? 0.2f : 0.05f), 0.0f, r.getY(), p.plate.darker (0.3f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (p.rim.withAlpha (hover ? 0.6f : 0.3f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    return true;
}

inline bool valuePlate (juce::Graphics& g, Rectangle<float> r, float corner)
{
    const auto& p = pal();
    g.setColour (p.well);
    g.fillRoundedRectangle (r, corner);
    g.setColour (p.rim.withAlpha (0.3f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    return true;
}
} // namespace artkit

namespace
{
// the style an art builds on for what it does not draw itself (title text, LEDs, meters, curves)
struct BaseScope
{
    BaseScope() : saved (currentStyle), on (currentStyle == Style::themed)
    {
        if (on) currentStyle = baseStyleOf (currentArt);
    }
    ~BaseScope()
    {
        if (on) currentStyle = saved;
    }
    BaseScope (const BaseScope&) = delete;
    BaseScope& operator= (const BaseScope&) = delete;
    Style saved;
    bool on;
};
} // namespace

Art artForEffect (EffectKind kind)
{
    switch (kind)
    {
        case EffectKind::facetEq: return Art::facet;
        case EffectKind::nebula: return Art::nebula;
        default: return Art::none;
    }
}

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

Style baseStyleOf (Art a)
{
    switch (a)
    {
        case Art::toxic: case Art::nebula: case Art::sytrus: return Style::neon;
        case Art::threeOsc: return Style::hardware;
        default: return Style::glow;
    }
}

juce::Colour artAccent (Art a) { return a == Art::none ? colour::crimson : artkit::palette (a).accent; }
juce::Colour artAccent2 (Art a) { return a == Art::none ? colour::crimson : artkit::palette (a).accent2; }

const char* artName (Art a)
{
    switch (a)
    {
        case Art::sakura: return "Sakura";
        case Art::toxic: return "Toxic Biohazard";
        case Art::sytrus: return "Sytrus";
        case Art::threeOsc: return "3x Osc";
        case Art::facet: return "Facet EQ";
        case Art::nebula: return "Nebula";
        default: return "";
    }
}

bool osReducedMotion()
{
   #if JUCE_WINDOWS
    int animate = 1; // "Show animations in Windows" (Settings › Accessibility › Visual effects)
    if (lynxSystemParametersInfoW (0x1042 /* SPI_GETCLIENTAREAANIMATION */, 0, &animate, 0) != 0)
        return animate == 0;
   #endif
    return false;
}

// --- the moving part of the art ---------------------------------------------------------------------

namespace
{
std::vector<ArtLayer*>& artLayers()
{
    static std::vector<ArtLayer*> all; // (message thread only)
    return all;
}
} // namespace

void kickArtLayers()
{
    for (auto* l : artLayers())
    {
        l->kick();
        l->repaint();
    }
}

struct ArtLayer::Impl
{
    struct Mote // a petal, a bubble, a star...
    {
        float x = 0, y = 0, vx = 0, vy = 0, size = 0, rot = 0, spin = 0, phase = 0, life = 0;
    };
    std::vector<Mote> motes;
    juce::Random rnd { 170 };
    float w = 0, h = 0;

    void reset (Art a, juce::Rectangle<float> area, juce::Rectangle<float> st)
    {
        w = area.getWidth();
        h = area.getHeight();
        motes.clear();
        rnd.setSeed (170);
        const auto s = st.isEmpty() ? area : st;
        int n = 0;
        switch (a)
        {
            case Art::sakura: n = 14; break;
            case Art::toxic: n = 9; break;
            case Art::nebula: n = 22; break;
            case Art::sytrus: n = 6; break;
            default: n = 0; break;
        }
        for (int i = 0; i < n; ++i)
        {
            Mote m;
            m.x = s.getX() + rnd.nextFloat() * s.getWidth();
            m.y = s.getY() + rnd.nextFloat() * s.getHeight();
            m.size = a == Art::sakura ? 6.0f + rnd.nextFloat() * 5.0f : a == Art::toxic ? 2.0f + rnd.nextFloat() * 4.0f : 1.0f + rnd.nextFloat() * 1.8f;
            m.vx = a == Art::sakura ? 6.0f + rnd.nextFloat() * 10.0f : 0.0f;
            m.vy = a == Art::sakura ? 14.0f + rnd.nextFloat() * 16.0f : a == Art::toxic ? -(10.0f + rnd.nextFloat() * 14.0f) : 0.0f;
            m.rot = rnd.nextFloat() * 6.28f;
            m.spin = (rnd.nextFloat() - 0.5f) * 2.4f;
            m.phase = rnd.nextFloat() * 6.28f;
            motes.push_back (m);
        }
    }

    juce::Rectangle<float> boundsOf (Art a, const Mote& m) const
    {
        const auto r = a == Art::sakura ? m.size * 0.75f + 2.0f : a == Art::nebula ? m.size * 4.0f + 2.0f : m.size + 2.0f;
        return { m.x - r, m.y - r, r * 2.0f, r * 2.0f };
    }
};

ArtLayer::ArtLayer() : impl (std::make_unique<Impl>())
{
    setInterceptsMouseClicks (false, false);
    setOpaque (false);
    artLayers().push_back (this);
}

ArtLayer::~ArtLayer()
{
    auto& all = artLayers();
    all.erase (std::remove (all.begin(), all.end(), this), all.end());
}

void ArtLayer::setArt (Art a, std::function<bool()> active)
{
    artId = a;
    isActive = std::move (active);
    impl->reset (artId, getLocalBounds().toFloat(), stage);
    kick();
}

void ArtLayer::setStage (juce::Rectangle<float> s)
{
    if (s == stage) return;
    stage = s;
    impl->reset (artId, getLocalBounds().toFloat(), stage);
    repaint();
}

void ArtLayer::resized()
{
    impl->reset (artId, getLocalBounds().toFloat(), stage);
    kick();
}

bool ArtLayer::showing() const
{
    return artId != Art::none && (isActive == nullptr || isActive()) && isShowing() && getWidth() > 0;
}

void ArtLayer::kick()
{
    const auto run = showing() && artMotion() && ! reduced && ! osReducedMotion() && artId != Art::facet;
    if (run && ! isTimerRunning())
    {
        lastMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (artId == Art::sakura ? 30 : 20);
    }
    else if (! run && isTimerRunning())
        stopTimer();
}

void ArtLayer::setPhase (double seconds)
{
    stopTimer();
    impl->reset (artId, getLocalBounds().toFloat(), stage);
    // advance in steps so the motes travel as they would have
    const auto steps = (int) (seconds * 30.0);
    for (int i = 0; i < steps; ++i)
    {
        t += 1.0 / 30.0;
        for (auto& m : impl->motes)
        {
            const auto dt = 1.0f / 30.0f;
            m.x += (m.vx + 8.0f * std::sin ((float) t * 1.3f + m.phase)) * dt;
            m.y += m.vy * dt;
            m.rot += m.spin * dt;
        }
    }
    repaint();
}

void ArtLayer::timerCallback()
{
    if (! showing() || ! artMotion() || reduced)
    {
        stopTimer();
        repaint();
        return;
    }
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt = (float) juce::jlimit (0.0, 0.1, (now - lastMs) / 1000.0);
    lastMs = now;
    t += dt;
    const auto area = getLocalBounds().toFloat();
    const auto s = stage.isEmpty() ? area : stage;
    juce::RectangleList<int> dirty;
    for (auto& m : impl->motes)
    {
        dirty.addWithoutMerging (impl->boundsOf (artId, m).getSmallestIntegerContainer());
        switch (artId)
        {
            case Art::sakura: // drifting down with a sway, tumbling
                m.x += (m.vx + 9.0f * std::sin ((float) t * 1.3f + m.phase)) * dt;
                m.y += m.vy * dt;
                m.rot += m.spin * dt;
                if (m.y > area.getBottom() + 10.0f || m.x > area.getRight() + 10.0f)
                {
                    m.y = area.getY() - 8.0f;
                    m.x = area.getX() + impl->rnd.nextFloat() * area.getWidth() * 0.9f;
                }
                break;
            case Art::toxic: // bubbles rising in the stage, popping at its top
                m.y += m.vy * dt;
                m.x += 3.0f * std::sin ((float) t * 3.0f + m.phase) * dt;
                if (m.y < s.getY())
                {
                    m.y = s.getBottom();
                    m.x = s.getX() + impl->rnd.nextFloat() * s.getWidth();
                }
                break;
            case Art::nebula: // twinkling stars, still
            case Art::sytrus:
            default:
                break;
        }
        dirty.addWithoutMerging (impl->boundsOf (artId, m).getSmallestIntegerContainer());
    }
    if (artId == Art::toxic || artId == Art::sytrus || artId == Art::threeOsc) // the stage glows, turns or scrolls: repaint it
        dirty.add (s.getSmallestIntegerContainer());
    dirty.consolidate();
    for (const auto& r : dirty)
        repaint (r);
}

void ArtLayer::paint (juce::Graphics& g)
{
    if (artId == Art::none || (isActive != nullptr && ! isActive())) return;
    const StyleScope scope (Style::themed, artId);
    const auto& p = artkit::palette (artId);
    const auto area = getLocalBounds().toFloat();
    const auto s = stage.isEmpty() ? area : stage;
    const auto tt = (float) t;
    switch (artId)
    {
        case Art::sakura:
            for (const auto& m : impl->motes)
                artkit::petal (g, { m.x, m.y }, m.size, m.rot, 0.35f + 0.65f * std::abs (std::sin (tt * 1.7f + m.phase)), p.accent2, 0.72f);
            break;
        case Art::toxic:
        {
            // the trefoil next to the name, breathing
            const auto pulse = 0.5f + 0.5f * std::sin (tt * 2.4f);
            const auto c = juce::Point<float> (s.getX() + s.getHeight() * 0.5f, s.getCentreY());
            const auto r = s.getHeight() * 0.36f;
            if (! reduced) bloom (g, c, r * (2.2f + 0.5f * pulse), p.accent, 0.25f + 0.25f * pulse);
            artkit::drawTrefoil (g, c, r, p.accent.interpolatedWith (juce::Colours::white, 0.15f * pulse), 0.85f);
            for (const auto& m : impl->motes)
            {
                g.setColour (p.accent.withAlpha (0.55f));
                g.drawEllipse (juce::Rectangle<float> (m.size, m.size).withCentre ({ m.x, m.y }), 1.0f);
                g.setColour (juce::Colours::white.withAlpha (0.4f));
                g.fillEllipse (juce::Rectangle<float> (m.size * 0.3f, m.size * 0.3f).withCentre ({ m.x - m.size * 0.15f, m.y - m.size * 0.15f }));
            }
            break;
        }
        case Art::nebula:
            for (const auto& m : impl->motes)
            {
                const auto tw = 0.5f + 0.5f * std::sin (tt * (1.2f + m.spin) + m.phase);
                if (! reduced) bloom (g, { m.x, m.y }, m.size * 4.0f, p.accent2.interpolatedWith (juce::Colours::white, 0.5f), 0.25f * tw);
                g.setColour (juce::Colours::white.withAlpha (0.25f + 0.7f * tw));
                g.fillEllipse (juce::Rectangle<float> (m.size, m.size).withCentre ({ m.x, m.y }));
            }
            break;
        case Art::sytrus:
        {
            if (s == area) break;
            juce::Path l;
            artkit::lissajous (l, s.getCentre(), s.getWidth() * 0.42f, s.getHeight() * 0.38f, 3.0f, 2.0f, tt * 0.5f, 240);
            g.setColour (p.accent2.withAlpha (0.18f));
            g.strokePath (l, juce::PathStrokeType (2.6f));
            g.setColour (p.accent.withAlpha (0.55f));
            g.strokePath (l, juce::PathStrokeType (1.0f));
            break;
        }
        case Art::threeOsc:
        {
            // a little amber phosphor scope next to the name: the three oscillators' sum scrolling by
            if (s == area) break;
            const auto scr = s.reduced (2.0f);
            g.setColour (juce::Colour (0xff050604));
            g.fillRoundedRectangle (scr, 6.0f);
            g.setColour (p.accent.withAlpha (0.10f));
            for (int i = 1; i < 6; ++i)
                g.drawVerticalLine ((int) (scr.getX() + scr.getWidth() * (float) i / 6.0f), scr.getY() + 3.0f, scr.getBottom() - 3.0f);
            g.drawHorizontalLine ((int) scr.getCentreY(), scr.getX() + 3.0f, scr.getRight() - 3.0f);
            juce::Path tr;
            const int n = 120;
            for (int i = 0; i <= n; ++i)
            {
                const auto u = (float) i / (float) n;
                const auto ph = u * 2.0f + tt * 0.35f;
                const auto sine = std::sin (ph * juce::MathConstants<float>::twoPi);
                const auto saw = 1.0f - 2.0f * (ph * 2.0f - std::floor (ph * 2.0f));
                const auto sq = std::fmod (ph * 0.5f, 1.0f) < 0.5f ? 1.0f : -1.0f;
                const auto v = (sine * 0.5f + saw * 0.25f + sq * 0.2f) * 0.8f;
                const juce::Point<float> q { scr.getX() + 4.0f + (scr.getWidth() - 8.0f) * u, scr.getCentreY() - v * (scr.getHeight() * 0.42f) };
                if (i == 0) tr.startNewSubPath (q);
                else tr.lineTo (q);
            }
            g.saveState();
            g.reduceClipRegion (scr.toNearestInt());
            g.setColour (p.accent.withAlpha (0.25f));
            g.strokePath (tr, juce::PathStrokeType (4.0f));
            g.setColour (p.accent2);
            g.strokePath (tr, juce::PathStrokeType (1.3f));
            g.restoreState();
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawRoundedRectangle (scr, 6.0f, 1.0f);
            break;
        }
        default: break;
    }
}

juce::String artSelfTest()
{
    juce::StringArray errors;
    auto expect = [&] (bool ok, const char* what) { if (! ok) errors.add (what); };
    const auto keptForced = forcedStyle();
    const auto keptMotion = artMotion();
    expect (styleFromName ("Themed") == Style::themed && juce::String (styleName (Style::themed)) == "Themed", "style name");
    expect (unpackStyle (packStyle (Style::themed, Art::toxic)) == Style::themed && unpackArt (packStyle (Style::themed, Art::toxic)) == Art::toxic, "pack");
    setForcedStyle (-1);
    expect (styleFor (EffectKind::facetEq) == Style::themed && styleFor (EffectKind::nebula) == Style::themed, "pilots are Themed");
    expect (styleFor (EffectKind::epochVerb) == Style::hardware && styleFor (EffectKind::crestCompressor) == Style::glow, "others keep their style");
    expect (resolve (Style::themed, Art::sakura) == Style::themed && resolve (Style::neon, Art::none) == Style::neon, "automatic");
    setForcedStyle ((int) Style::themed); // Themed everywhere: only plugins with art
    expect (styleFor (EffectKind::facetEq) == Style::themed && styleFor (EffectKind::epochVerb) == Style::hardware, "forced Themed");
    expect (resolve (Style::neon, Art::none) == Style::neon && resolve (Style::themed, Art::none) == Style::glow, "forced Themed without art");
    setForcedStyle ((int) Style::vivid);
    expect (styleFor (EffectKind::facetEq) == Style::vivid && resolve (Style::themed, Art::toxic) == Style::vivid, "forced Vivid");
    setForcedStyle (keptForced);
    expect (artForInstrument (InstrumentKind::sakura) == Art::sakura && artForInstrument (InstrumentKind::threeosc) == Art::threeOsc
                && artForInstrument (InstrumentKind::kick) == Art::none,
            "instrument art");
    // every art draws its canvas, a section, a knob, a fader, a button and its moving layer
    for (int a = 1; a <= (int) Art::nebula; ++a)
    {
        juce::Image img (juce::Image::ARGB, 320, 200, true);
        {
            juce::Graphics g (img);
            const StyleScope scope (Style::themed, (Art) a);
            canvas (g, { 0.0f, 0.0f, 320.0f, 200.0f }, colour::crimson);
            section (g, { 10.0f, 10.0f, 140.0f, 120.0f }, 6.0f, true, colour::crimson);
            sectionTitle (g, { 10.0f, 10.0f, 140.0f, 16.0f }, "OSC 1", colour::crimson, 10.0f);
            KnobLook k;
            k.value = 0.7f;
            knob (g, { 30.0f, 40.0f, 60.0f, 60.0f }, k);
            slider (g, { 200.0f, 20.0f, 4.0f, 120.0f }, 0.4f, true, false, colour::crimson, false, true, 24.0f, 12.0f);
            button (g, { 220.0f, 160.0f, 60.0f, 20.0f }, true, false, colour::crimson, 4.0f);
            expect (style() == Style::themed && art() == (Art) a, "scope");
        }
        expect (style() != Style::themed, "scope restored");
        const auto px = img.getPixelAt (160, 100);
        expect (px.getAlpha() == 255, artName ((Art) a));
        ArtLayer layer;
        layer.setSize (320, 200);
        layer.setArt ((Art) a, [] { return true; });
        layer.setStage ({ 200.0f, 10.0f, 100.0f, 40.0f });
        layer.setPhase (1.5);
        juce::Image moving (juce::Image::ARGB, 320, 200, true);
        {
            juce::Graphics g (moving);
            layer.paint (g);
        }
        expect (! layer.animating(), "a layer off screen never animates");
    }
    setArtMotion (keptMotion);
    return errors.isEmpty() ? juce::String ("OK") : "ERROR " + errors.joinIntoString (", ");
}

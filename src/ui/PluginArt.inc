// The "Themed" plugin style (round 170j): a plugin with ART of its own is drawn in a theme that evokes the mood of the
// plugin it is inspired by — its palette, its materials and an illustration — instead of one of the four shared
// styles. Art directions and the style guide: the tracker's plugin-art-0.1.24.md.
//
// Everything here is LYNX's own procedural vector drawing (juce::Path, gradients, seeded random): no bitmap, logo,
// skin or artwork of the original plugins is copied, traced or extracted. Layouts and gestures are the editors'
// own and never change; only the drawing does. Static parts are drawn into the kit's cached layers (canvas, knobs);
// the moving parts live in skin::ArtLayer (below), which repaints only small areas at 20 Hz while it is shown.
//
// The files:
//   PluginArt.inc            this framework: the Theme record, shared motifs, the themed surfaces and controls
//                            (driven by the Theme's fields), the moving layer and the self-test
//   PluginArtGenerators.inc  the generators' themes (ids in PluginArtGeneratorIds.inc, 1..127) + artForInstrument
//   PluginArtEffects.inc     the effects' themes (ids in PluginArtEffectIds.inc, 128..255) + artForEffect
// A theme is DATA (palette, knob family, flags) plus a few optional drawing hooks; adding one never touches this file.
// Included by PluginSkin.cpp (it shares that file's helpers: bloom, reduced, styles::screw, KnobLook...).

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

// knob families (the cap's material); a theme may also draw its own cap (Theme::drawCap)
enum class Cap { flat, porcelain, orb, planet, rubber, bakelite };
// pointer families: a line from the cap's centre, the same with a glow, or a bright body riding the arc's tip
enum class Pointer { line, glowLine, electron };
// how the motes of the moving layer travel
enum class Drift { still, fall, rise };

struct Mote // a petal, a bubble, a star...
{
    float x = 0, y = 0, vx = 0, vy = 0, size = 0, rot = 0, spin = 0, phase = 0, life = 0;
};

// what a theme's moving painter gets each frame
struct Motion
{
    const Palette& p;
    Rectangle<float> area, stage; // the layer, and the strip it plays in (== area when the editor gave none)
    float t;                      // seconds
    const std::vector<Mote>& motes;
};

struct Theme
{
    const char* name = "";                   // "Sakura"... (tests, snapshots)
    Style base = Style::glow;                // the shared style it builds on for what it does not draw (title text, LEDs, meters)
    Palette pal;

    // the canvas (required) and optional decorations of the shared surfaces
    void (*canvas) (juce::Graphics&, Rectangle<float> area, bool display) = nullptr;
    void (*headerDeco) (juce::Graphics&, Rectangle<float> bar) = nullptr;                     // over the header bar's fill
    void (*sectionDeco) (juce::Graphics&, Rectangle<float> panel, float corner) = nullptr;    // over a section's glass
    void (*titleEmblem) (juce::Graphics&, Point<float> c, float r) = nullptr;                 // left of a section title (default: an orbiting dot)

    // the line work
    bool thin = false;      // finer tracks, rims and pointers, no glow on faders (precise / clinical themes)
    bool twoTone = false;   // arcs, fills, rims and lit buttons run accent2 → accent
    float kerning = 0.12f;  // section titles

    // knobs
    Cap cap = Cap::flat;
    void (*drawCap) (juce::Graphics&, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx) = nullptr; // replaces `cap`
    // round tp172c: a colour per parameter for the caps (a console's colour-coded knobs); drawCap reads it as knobTint
    Colour (*capTint) (const char* param) = nullptr;
    Pointer pointer = Pointer::line;
    float pointerFrom = 0.35f;      // where a line pointer starts (fraction of the cap's radius)
    int ticks = 0;                  // a printed scale around the knob (0 = none; every 5th is long)
    bool boldTicks = false;         // accent2 notches instead of fine title-coloured marks
    bool buttTrack = false;         // square track ends (hardware)
    float arcGlow = 0.22f;          // the halo under the lit arc (0 = none)
    float arcGlowWidth = 2.4f;      // ... its width in track widths
    float knobScale = 1.0f;         // knobs drawn a little larger than their slot (cap skirts)
    void (*tipDeco) (juce::Graphics&, Point<float> tip, float t, float angle) = nullptr; // rides the arc's tip (knobs >= 34 px)
    bool thumbPointer = false;      // fader thumbs carry the pointer colour (else the accent)
    // round tp172c: the label of an OFF toggle (transparent = the global disabled text; a light panel needs its own)
    Colour offLabelCol { 0 };
    bool noValueArc = false;        // round tp172c: knobs show their value by the pointer on a printed scale only (consoles)
    // round tp172b4: the "grip" knob of the gate / de-esser references, in our own drawing — a solid domed cap with soft
    // dimples round its edge, a light thumb riding the edge at the value (with a short mark), on a dark ticked ring; no
    // lit arc. A knob's capTint (KnobLook) recolours the cap (the small grey knobs beside the big coloured ones)
    bool gripKnob = false;
    // round tp172b4: the compressor reference's big caps — the cap fills the knob, a fine rim round it, the lit arc
    // runs just inside the rim with a small square mark at its tip
    bool arcInCap = false;

    // round tp172d: the flat "hardware graphic" look of the third-party originals (Valhalla, Soundtoys...) — solid
    // colour fields, bold outlines, big plain type; every field defaults to the look above
    bool flatSections = false;      // sections: a solid panelTop fill and a rimWidth outline in `rim`; titles plain and centred
    float rimWidth = 2.0f;
    bool discKnob = false;          // knobs: a flat disc (capTop) with a thick pointer to its edge; a thin value ring outside
    bool discRing = true;           // ... with that ring (accent) — false = the disc alone
    bool barSlider = false;         // faders: a wide flat bar (track) filled from the bottom in the accent, no thumb
    Colour captionCol { 0 };        // knob / fader captions (transparent = the shared grey)
    Colour valueCol { 0 };          // their value read-outs (transparent = the shared light grey)
    float captionScale = 1.0f;      // caption and value type size (big, plain labels)
    Colour barCol { 0 };            // barSlider's fill (transparent = the accent)
    Colour headerCol { 0 };         // a solid header bar (transparent = the canvas' gradient); for light themes
    float cornerScale = 1.0f;       // flat sections' corner radius × this (the originals' rounded "pods": 3 … 5)
    Colour sectionFills[4] {};      // flat sections: the n-th top-level section's own fill (transparent = panelTop)
    bool hwKeys = false;            // option tabs and switches as square hardware keys (grey caps, lit in the accent)
    float cheek = 0.0f;             // the hardware canvases' wooden cheeks, px (0 = their own default)
    float bigThumb = 0.0f;          // round tp172d: vertical faders get a large grooved metal cap (× the thumb thickness) on a dark slot, no lit fill
    bool lowerCaptions = false;     // round tp172d: captions and plain menus in lower case (the originals' minimal type)
    float bow = 0.0f;               // round tp172d: flat sections' sides bulge out by this × their size (the bowed pods)
    bool numerals = false;          // round tp172d: big knobs (≥ 70 px) print 0 … 10 around their scale (a hardware Drive)
    int numeralStep = 1;            // round tp172d: the printed numbers count in this step (10: 0 … 100, an opto leveler)
    bool ledReadouts = false;       // round tp172d: read-outs, stepped menus and the tap pad's tempo as red LED displays (EchoBoy)
    bool dotTicks = false;          // round tp172d: the knobs' printed scale as a ring of dots (Decapitator)
    bool titleRules = false;        // round tp172d: the plugin's name set between two rules (a hardware title bar)
    bool vuBezel = false;           // round tp172d: the VU meter in a chrome bezel
    bool vuBezelDark = false;       // round tp172d (slice 8): ... a dark gunmetal one
    bool goldKeys = false;          // round tp172d (slice 8): keys on a dark section fill are gold (lit: pale cream)
    bool numeralLit = false;        // round tp172d (slice 8): the printed number nearest the pointer in the accent
    Colour pointerKnobTint { 0 };   // round tp172d (slice 8): a knob whose capTint is this colour is a chicken-head pointer knob

    // the moving layer (ArtLayer): motes and/or a painter for the stage strip; all null/0 = a still theme
    int motes = 0;
    Drift drift = Drift::still;
    float sizeMin = 1.0f, sizeRange = 1.8f;  // mote size
    float vxMin = 0.0f, vxRange = 0.0f;      // px/s
    float vyMin = 0.0f, vyRange = 0.0f;      // px/s (negative = up)
    float sway = 0.0f;                       // px/s of side-to-side drift
    float reach = 1.0f;                      // a mote's repaint radius, in sizes
    bool stageRepaint = false;               // the painter animates the whole stage strip (scopes, pulses)
    void (*paintMotion) (juce::Graphics&, const Motion&) = nullptr;
    int hz = 20;                             // frames per second of the moving layer (slow motion needs fewer: CPU)
    // round 170k: the part of the stage the painter animates (a lamp, a record), repainted instead of the whole stage
    // strip when stageRepaint is set: the editor under the strip then redraws only that corner
    juce::Rectangle<float> (*motionArea) (juce::Rectangle<float> stage) = nullptr;
};

// the registries (PluginArtGenerators.inc / PluginArtEffects.inc)
const Theme* generatorTheme (Art);
const Theme* effectTheme (Art);

inline const Theme& fallbackTheme()
{
    static const Theme t = []
    {
        Theme f;
        f.name = "";
        f.pal = { Colour (0xff1a1b20), Colour (0xff0b0b0e), colour::crimson, Colour (0xffff8a8a), Colour (0xd01a1b20), Colour (0xe00e0f12),
                  Colour (0xff5a5d66), Colour (0xffe8e8ec), Colour (0xff08080a), Colour (0xff3a3c44), Colour (0xff15161a), Colour (0xff2c2e34),
                  Colour (0xfff2f2f4), Colour (0xff1c1d22) };
        return f;
    }();
    return t;
}

inline const Theme& themeOf (Art a)
{
    if (const auto* t = generatorTheme (a)) return *t;
    if (const auto* t = effectTheme (a)) return *t;
    return fallbackTheme();
}

inline const Theme& th() { return themeOf (currentArt); }

// round tp172c: the cap colour of the knob being drawn (KnobLook::capTint; transparent = the theme's own)
inline Colour knobTint;
inline Colour capTintFor (const char* param) { return th().capTint != nullptr ? th().capTint (param) : Colour(); }
inline Colour offLabelColour() { return th().offLabelCol; } // round tp172c
inline const Palette& palette (Art a) { return themeOf (a).pal; }
inline const Palette& pal() { return th().pal; }

inline juce::PathStrokeType stroke (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); }

inline juce::Path arc (Point<float> c, float r, float from, float to)
{
    juce::Path p;
    p.addCentredArc (c.x, c.y, r, r, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
    return p;
}

// the plain canvas (a theme without its own)
inline void canvasPlain (juce::Graphics& g, Rectangle<float> a, bool)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.bgTop, 0.0f, a.getY(), p.bgBottom, 0.0f, a.getBottom(), false));
    g.fillRect (a);
}


// --- shared motifs (any theme may use them) ----------------------------------------------------------

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
    static const bool registered = lynx::ui::registerImageCache ([] { cache.clear(); }); (void) registered;
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

} // namespace artkit

#include "ui/PluginArtGenerators.inc"
#include "ui/PluginArtEffects.inc"

namespace artkit
{
inline void canvas (juce::Graphics& g, Rectangle<float> a, Colour, bool display)
{
    if (const auto fn = th().canvas) fn (g, a, display);
    else canvasPlain (g, a, display);
}

// --- surfaces --------------------------------------------------------------------------------------

inline bool headerBar (juce::Graphics& g, Rectangle<float> a, Colour)
{
    const auto& p = pal();
    if (th().flatSections) // round tp172d: a flat bar
    {
        g.setColour (th().headerCol.getAlpha() > 0 ? th().headerCol : p.bgTop.brighter (0.12f));
        g.fillRect (a);
        g.setColour (p.accent.withAlpha (0.8f));
        g.fillRect (a.withTop (a.getBottom() - 2.0f));
        return true;
    }
    g.setGradientFill (ColourGradient (p.bgTop.brighter (0.25f), 0.0f, a.getY(), p.bgBottom.brighter (0.1f), 0.0f, a.getBottom(), false));
    g.fillRect (a);
    if (const auto fn = th().headerDeco) fn (g, a);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.07f), 0.0f, a.getY(), Colours::white.withAlpha (0.0f), 0.0f, a.getCentreY(), false));
    g.fillRect (a.withHeight (a.getHeight() * 0.5f));
    const auto lineW = juce::jmin (a.getWidth(), 720.0f);
    ColourGradient line (p.accent, a.getX(), 0.0f, p.accent2.withAlpha (0.0f), a.getX() + lineW, 0.0f, false);
    line.addColour (0.5, p.accent2.withAlpha (0.7f));
    g.setGradientFill (line);
    g.fillRect (Rectangle<float> (a.getX(), a.getBottom() - 1.5f, lineW, 1.5f));
    return true;
}

inline int sectionOrdinal = -1; // round tp172d: the section being drawn, in layout order (sectionFills)
inline bool onDarkFill = false; // round tp172d: the item being drawn sits on a dark section fill (light legends)

// round tp172d: a square hardware key — a bevelled grey cap, lit in the accent (the originals' style and switch keys)
inline void hwKey (juce::Graphics& g, Rectangle<float> r, bool lit, bool hover, Colour litColour = {})
{
    const auto& p = pal();
    const auto lamp = litColour.getAlpha() > 0 ? litColour : p.accent;
    const auto corner = juce::jmin (3.0f, r.getHeight() * 0.12f);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.5f).expanded (1.0f), corner + 1.0f);
    const auto top = lit ? lamp.brighter (0.25f) : Colour (0xff9b9c9e).brighter (hover ? 0.12f : 0.0f);
    const auto bottom = lit ? lamp.darker (0.35f) : Colour (0xff4f5053);
    g.setGradientFill (ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    const auto face = r.reduced (juce::jmax (2.0f, r.getHeight() * 0.12f));
    g.setGradientFill (ColourGradient (bottom.interpolatedWith (top, 0.75f), 0.0f, face.getY(), bottom.interpolatedWith (top, 0.35f), 0.0f, face.getBottom(), false));
    g.fillRoundedRectangle (face, corner * 0.6f);
    g.setColour (Colours::white.withAlpha (lit ? 0.45f : 0.3f));
    g.drawLine (r.getX() + corner, r.getY() + 0.8f, r.getRight() - corner, r.getY() + 0.8f, 1.0f);
    g.setColour (Colour (0xff141414));
    g.drawRoundedRectangle (r, corner, 1.0f);
    if (lit && ! reduced && p.bgTop.getBrightness() < 0.6f) bloom (g, r.getCentre(), r.getWidth() * 0.9f, lamp, 0.16f); // (no lamp glow on a light faceplate)
}

// round tp172d (slice 8): an opto leveler's strip keys — gold caps, the lit one pale cream
inline void goldKey (juce::Graphics& g, Rectangle<float> r, bool lit, bool hover)
{
    const auto corner = juce::jmin (2.5f, r.getHeight() * 0.12f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.2f).expanded (1.0f), corner + 1.0f);
    const auto top = lit ? Colour (0xfffbf5d2) : Colour (0xffc9c196).brighter (hover ? 0.1f : 0.0f);
    const auto bottom = lit ? Colour (0xffe2d699) : Colour (0xff8f8760);
    g.setGradientFill (ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    if (lit && ! reduced)
        bloom (g, r.getCentre(), r.getWidth() * 0.8f, Colour (0xfffff2b0), 0.18f);
    g.setColour (Colours::white.withAlpha (lit ? 0.6f : 0.35f));
    g.drawLine (r.getX() + corner, r.getY() + 0.8f, r.getRight() - corner, r.getY() + 0.8f, 1.0f);
    g.setColour (Colour (0xff1a1810));
    g.drawRoundedRectangle (r, corner, 1.0f);
}

// round tp172d (slice 8): a metal bat toggle — a hex nut on a washer, the chrome lever leaning up or down from it
inline void batToggle (juce::Graphics& g, Rectangle<float> r, bool up, bool hover, bool enabled)
{
    const auto c = r.getCentre();
    const auto n = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f; // the nut's radius
    // the washer
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (Rectangle<float> (n * 2.5f, n * 2.5f).withCentre (c.translated (0.0f, n * 0.12f)));
    g.setGradientFill (ColourGradient (Colour (0xffd4d6d8), c.x, c.y - n * 1.2f, Colour (0xff5c5f63), c.x, c.y + n * 1.2f, false));
    g.fillEllipse (Rectangle<float> (n * 2.3f, n * 2.3f).withCentre (c));
    // the hex nut
    juce::Path hex;
    for (int i = 0; i < 6; ++i)
    {
        const auto a = juce::MathConstants<float>::pi / 3.0f * (float) i + juce::MathConstants<float>::pi / 6.0f;
        const Point<float> q { c.x + std::cos (a) * n * 0.95f, c.y + std::sin (a) * n * 0.95f };
        if (i == 0) hex.startNewSubPath (q); else hex.lineTo (q);
    }
    hex.closeSubPath();
    g.setGradientFill (ColourGradient (Colour (0xffeeeff0), c.x - n, c.y - n, Colour (0xff6d7075), c.x + n, c.y + n, false));
    g.fillPath (hex);
    g.setColour (Colour (0xff2a2b2d));
    g.strokePath (hex, juce::PathStrokeType (1.0f));
    // the lever: a tapered chrome bat from the nut's centre, its rounded tip `len` away (foreshortened: it leans out)
    const auto d = up ? -1.0f : 1.0f;
    const auto len = n * 1.55f;
    const Point<float> tip { c.x + n * 0.18f, c.y + d * len };
    const auto wBase = n * 0.42f, wTip = n * 0.62f;
    juce::Path bat;
    bat.startNewSubPath (c.x - wBase * 0.5f, c.y);
    bat.lineTo (tip.x - wTip * 0.5f, tip.y);
    bat.lineTo (tip.x + wTip * 0.5f, tip.y);
    bat.lineTo (c.x + wBase * 0.5f, c.y);
    bat.closeSubPath();
    g.setColour (Colours::black.withAlpha (0.4f)); // its shadow on the plate
    g.fillPath (bat, juce::AffineTransform::translation (n * 0.35f, n * 0.3f));
    const auto hi = enabled ? Colour (0xfff7f8f9).brighter (hover ? 0.1f : 0.0f) : Colour (0xffb8babc);
    g.setGradientFill (ColourGradient (Colour (0xff6a6d72), tip.x - wTip * 0.5f, 0.0f, hi, tip.x + wTip * 0.05f, 0.0f, false));
    g.fillPath (bat);
    g.setGradientFill (ColourGradient (hi, tip.x, tip.y - wTip * 0.4f, Colour (0xff7d8086), tip.x, tip.y + wTip * 0.4f, false));
    g.fillEllipse (Rectangle<float> (wTip, wTip * 0.85f).withCentre (tip));
    g.setColour (Colour (0xff2a2b2d).withAlpha (0.8f));
    g.drawEllipse (Rectangle<float> (wTip, wTip * 0.85f).withCentre (tip), 0.8f);
    g.setColour (Colour (0xff3a3c40));
    g.fillEllipse (Rectangle<float> (wBase, wBase).withCentre (c));
}

// round tp172d (slice 8): a selector — a glossy black pointer knob with a white line, turned to `angle`
inline void selectorKnob (juce::Graphics& g, Rectangle<float> r, float angle, bool hover, bool enabled)
{
    const auto c = r.getCentre();
    const auto rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillEllipse (Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c.translated (rad * 0.12f, rad * 0.22f)));
    g.setGradientFill (ColourGradient (Colour (0xff3c3d40).brighter (hover ? 0.15f : 0.0f), c.x - rad * 0.4f, c.y - rad * 0.6f,
                                       Colour (0xff050506), c.x + rad * 0.5f, c.y + rad, true));
    g.fillEllipse (Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c));
    g.setColour (Colours::white.withAlpha (0.22f)); // the gloss
    g.fillEllipse (Rectangle<float> (rad * 0.9f, rad * 0.5f).withCentre (c.translated (-rad * 0.25f, -rad * 0.45f)));
    const Point<float> dir { std::sin (angle), -std::cos (angle) };
    g.setColour (enabled ? Colours::white : Colour (0xff9a9c9e));
    g.drawLine ({ c + dir * rad * 0.15f, c + dir * rad * 0.92f }, juce::jmax (1.6f, rad * 0.12f));
}

// round tp172d: a pod whose four sides bow outwards (the hardware originals' rounded cut-outs), inside `a`
inline juce::Path bowedRect (Rectangle<float> a, float corner, float bow)
{
    const auto bx = a.getWidth() * bow, by = a.getHeight() * bow * 0.5f;
    const auto r = a.reduced (bx * 0.5f, by * 0.5f);
    const auto c = juce::jmin (corner, r.getWidth() * 0.25f, r.getHeight() * 0.25f);
    juce::Path p;
    p.startNewSubPath (r.getX() + c, r.getY());
    p.quadraticTo (r.getCentreX(), r.getY() - by, r.getRight() - c, r.getY());
    p.quadraticTo (r.getRight(), r.getY(), r.getRight(), r.getY() + c);
    p.quadraticTo (r.getRight() + bx, r.getCentreY(), r.getRight(), r.getBottom() - c);
    p.quadraticTo (r.getRight(), r.getBottom(), r.getRight() - c, r.getBottom());
    p.quadraticTo (r.getCentreX(), r.getBottom() + by, r.getX() + c, r.getBottom());
    p.quadraticTo (r.getX(), r.getBottom(), r.getX(), r.getBottom() - c);
    p.quadraticTo (r.getX() - bx, r.getCentreY(), r.getX(), r.getY() + c);
    p.quadraticTo (r.getX(), r.getY(), r.getX() + c, r.getY());
    p.closeSubPath();
    return p;
}

inline bool section (juce::Graphics& g, Rectangle<float> a, float corner, bool raised, Colour)
{
    const auto& t = th();
    const auto& p = t.pal;
    if (t.flatSections) // round tp172d: a solid field with a bold outline
    {
        corner *= t.cornerScale;
        const auto own = sectionOrdinal >= 0 && sectionOrdinal < 4 && t.sectionFills[sectionOrdinal].getAlpha() > 0;
        const auto fill = own ? t.sectionFills[sectionOrdinal] : (raised ? p.panelTop.brighter (0.06f) : p.panelTop);
        if (own) // a printed colour field: a soft top-down sheen
            g.setGradientFill (ColourGradient (fill.brighter (0.08f), 0.0f, a.getY(), fill.darker (0.12f), 0.0f, a.getBottom(), false));
        else
            g.setColour (fill);
        if (t.bow > 0.0f && ! raised) // round tp172d: the bowed pods
        {
            const auto shape = bowedRect (a.reduced (t.rimWidth * 0.5f), corner, t.bow);
            g.fillPath (shape);
            if (t.sectionDeco != nullptr) t.sectionDeco (g, a, corner);
            if (t.rimWidth > 0.0f)
            {
                g.setColour (p.rim);
                g.strokePath (shape, juce::PathStrokeType (t.rimWidth));
            }
            return true;
        }
        g.fillRoundedRectangle (a, corner);
        if (t.sectionDeco != nullptr) t.sectionDeco (g, a, corner);
        if (t.rimWidth > 0.0f)
        {
            g.setColour (p.rim);
            g.drawRoundedRectangle (a.reduced (t.rimWidth * 0.5f), corner, t.rimWidth);
        }
        return true;
    }
    if (! reduced)
    {
        g.setColour (Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (a.translated (0.0f, 3.0f).expanded (1.0f), corner + 1.0f);
    }
    g.setGradientFill (ColourGradient (raised ? p.panelTop.brighter (0.08f) : p.panelTop, 0.0f, a.getY(), p.panelBottom, 0.0f, a.getBottom(), false));
    g.fillRoundedRectangle (a, corner);
    if (t.sectionDeco != nullptr) t.sectionDeco (g, a, corner);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.06f), 0.0f, a.getY(), Colours::white.withAlpha (0.0f), 0.0f, a.getY() + juce::jmin (40.0f, a.getHeight() * 0.5f), false));
    g.fillRoundedRectangle (a, corner);
    // the rim: the art's light along the top, fading down the sides
    ColourGradient rim (p.rim.withAlpha (t.thin ? 0.35f : 0.55f), 0.0f, a.getY(), p.rim.withAlpha (0.10f), 0.0f, a.getBottom(), false);
    if (t.twoTone)
        rim = ColourGradient (p.accent.withAlpha (0.55f), a.getX(), a.getY(), p.accent2.withAlpha (0.35f), a.getRight(), a.getBottom(), false);
    g.setGradientFill (rim);
    g.drawRoundedRectangle (a.reduced (0.5f), corner, t.thin ? 1.0f : 1.2f);
    return true;
}

inline bool sectionTitle (juce::Graphics& g, Rectangle<float> strip, const juce::String& text, Colour, float fontSize)
{
    const auto& t = th();
    const auto& p = t.pal;
    if (t.flatSections) // round tp172d: a plain, big, centred title
    {
        g.setFont (juce::Font (theme::font (fontSize * t.captionScale * 1.15f, true)).withExtraKerningFactor (t.kerning));
        g.setColour (p.title);
        g.drawText (text, strip.translated (0.0f, 2.0f), juce::Justification::centred, false);
        return true;
    }
    g.setFont (juce::Font (theme::font (fontSize, true)).withExtraKerningFactor (t.kerning));
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
    if (t.titleEmblem != nullptr)
        t.titleEmblem (g, ec, er);
    else
    {
        // a lit body on a thin orbit
        if (! reduced) bloom (g, ec, er * 3.0f, p.accent, 0.4f);
        juce::Path orbit;
        orbit.addEllipse (Rectangle<float> (er * 2.2f, er * 1.0f).withCentre (ec));
        g.setColour (p.accent2.withAlpha (0.8f));
        if (! t.thin) g.strokePath (orbit, juce::PathStrokeType (0.8f));
        g.setColour (p.accent);
        g.fillEllipse (Rectangle<float> (er * 0.9f, er * 0.9f).withCentre (ec));
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

inline float knobScale() { return th().knobScale; }

// the knob families' caps
inline void drawCapFamily (juce::Graphics& g, Cap family, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    switch (family)
    {
        case Cap::porcelain:
            g.setGradientFill (ColourGradient (p.capTop.brighter (hover * 0.05f), c.x - capR * 0.4f, c.y - capR * 0.6f, p.capBottom, c.x + capR * 0.5f, c.y + capR, true));
            g.fillEllipse (cap);
            g.setColour (p.accent.withAlpha (0.35f));
            g.drawEllipse (cap.reduced (capR * 0.18f), 0.8f);
            break;
        case Cap::orb:    // a glass orb
        case Cap::planet: // a small planet with a thin ring
        {
            ColourGradient orb (p.capTop.brighter (0.3f + hover * 0.2f), c.x - capR * 0.45f, c.y - capR * 0.5f, p.capBottom, c.x + capR * 0.7f, c.y + capR * 0.8f, true);
            g.setGradientFill (orb);
            g.fillEllipse (cap);
            if (fx && family == Cap::planet)
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
        case Cap::rubber: // black rubber with knurling
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
        case Cap::bakelite: // bakelite with a skirt
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
        case Cap::flat: // flat, precise
        default:
            g.setGradientFill (ColourGradient (p.capTop.brighter (hover * 0.12f), c.x, cap.getY(), p.capBottom, c.x, cap.getBottom(), false));
            g.fillEllipse (cap);
            g.setColour (Colours::white.withAlpha (0.10f + 0.1f * hover));
            g.drawEllipse (cap.reduced (0.5f), 1.0f);
            break;
    }
}

inline void knob (juce::Graphics& g, Rectangle<float> area, const KnobLook& k)
{
    knobTint = k.capTint; // round tp172c: for the theme's drawCap (set first: every branch below may draw a cap)
    const auto& th0 = th();
    const auto& p = th0.pal;
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

    if (th0.discKnob) // round tp172d: a flat disc with a thick pointer (the originals' graphic knobs)
    {
        const auto ringT = juce::jlimit (1.2f, 3.0f, size * 0.04f);
        const auto discR = size * 0.5f - (th0.discRing ? ringT * 2.6f : 1.0f);
        if (th0.discRing)
        {
            const auto rr = size * 0.5f - ringT * 0.8f;
            g.setColour (p.track.withAlpha (k.enabled ? 0.9f : 0.5f));
            g.strokePath (arc (c, rr, arcStart, arcStart + arcRange), juce::PathStrokeType (ringT));
            if (std::abs (to - from) > 0.002f)
            {
                g.setColour (lit);
                g.strokePath (arc (c, rr, from, to), juce::PathStrokeType (ringT));
            }
        }
        const auto disc = Rectangle<float> (discR * 2.0f, discR * 2.0f).withCentre (c);
        const auto capBase = knobTint.isTransparent() ? p.capTop : knobTint; // round tp172d: a shade per knob (Halo's row)
        const auto capCol = k.enabled ? capBase.brighter (hover * 0.12f) : capBase.withMultipliedSaturation (0.3f);
        g.setColour (capCol);
        g.fillEllipse (disc);
        if (p.capBottom != p.capTop)
        {
            g.setColour (p.capBottom);
            g.drawEllipse (disc.reduced (0.5f), juce::jmax (1.0f, size * 0.02f));
        }
        const auto pw = juce::jlimit (1.6f, 6.0f, size * 0.075f);
        g.setColour (k.enabled ? p.pointer : p.pointer.withAlpha (0.5f));
        g.drawLine ({ c + dir * discR * th0.pointerFrom, c + dir * (discR - pw * 0.2f) }, pw);
        return;
    }

    if (th0.gripKnob)
    {
        // the ring: a dark band with fine marks along the travel
        const auto ringW = juce::jlimit (2.0f, 7.0f, size * 0.075f);
        const auto outerR = size * 0.5f - 0.5f;
        g.setColour (p.track.withAlpha (k.enabled ? 1.0f : 0.6f));
        g.drawEllipse (Rectangle<float> (2.0f * (outerR - ringW * 0.5f), 2.0f * (outerR - ringW * 0.5f)).withCentre (c), ringW);
        if (size >= 30.0f)
        {
            g.setColour (p.title.withAlpha (0.45f));
            for (int i = 0; i < 11; ++i)
            {
                const auto ang = arcStart + arcRange * (float) i / 10.0f;
                const Point<float> d { std::sin (ang), -std::cos (ang) };
                g.drawLine ({ c + d * (outerR - 0.5f), c + d * (outerR - ringW * (i % 5 == 0 ? 0.95f : 0.6f)) }, 1.0f);
            }
        }
        // the cap
        const auto capR = juce::jmax (3.0f, outerR - ringW - juce::jmax (1.0f, size * 0.015f));
        const auto cap = Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
        auto top = k.capTint.getAlpha() > 0 ? k.capTint : p.capTop;
        auto bottom = k.capTint.getAlpha() > 0 ? k.capTint.darker (0.45f) : p.capBottom;
        if (! k.enabled) { top = top.withMultipliedSaturation (0.3f); bottom = bottom.withMultipliedSaturation (0.3f); }
        if (fx)
        {
            g.setColour (Colours::black.withAlpha (0.4f));
            g.fillEllipse (cap.translated (0.0f, capR * 0.08f).expanded (1.0f));
        }
        g.setGradientFill (ColourGradient (top.brighter (hover * 0.10f), c.x, cap.getY(), bottom, c.x, cap.getBottom(), false));
        g.fillEllipse (cap);
        if (fx) // soft dimples round the edge
        {
            g.saveState();
            g.reduceClipRegion (juce::Path ([&] { juce::Path e; e.addEllipse (cap); return e; }()));
            const int n = 10;
            for (int i = 0; i < n; ++i)
            {
                const auto ang = juce::MathConstants<float>::twoPi * ((float) i + 0.5f) / (float) n;
                const Point<float> d { std::sin (ang), -std::cos (ang) };
                const auto dc = c + d * capR * 1.02f;
                g.setColour (Colours::black.withAlpha (0.16f));
                g.fillEllipse (Rectangle<float> (capR * 0.42f, capR * 0.42f).withCentre (dc));
                g.setColour (Colours::white.withAlpha (0.07f));
                g.drawEllipse (Rectangle<float> (capR * 0.42f, capR * 0.42f).withCentre (dc), 0.8f);
            }
            g.restoreState();
        }
        g.setColour (Colours::black.withAlpha (0.35f));
        g.drawEllipse (cap.reduced (0.5f), 1.0f);
        // the thumb: a light disc on the cap's edge at the value, clipped to the cap, with a short dark mark
        {
            g.saveState();
            g.reduceClipRegion (juce::Path ([&] { juce::Path e; e.addEllipse (cap.reduced (0.6f)); return e; }()));
            const auto tc = c + dir * capR * 0.72f;
            const auto tr = capR * 0.46f;
            const auto light = k.enabled ? p.pointer : p.pointer.withMultipliedSaturation (0.2f).darker (0.4f);
            g.setColour (Colours::black.withAlpha (0.22f));
            g.fillEllipse (Rectangle<float> (tr * 2.1f, tr * 2.1f).withCentre (tc.translated (0.0f, tr * 0.08f)));
            g.setGradientFill (ColourGradient (light.brighter (0.15f + hover * 0.1f), tc.x, tc.y - tr, light.darker (0.18f), tc.x, tc.y + tr, false));
            g.fillEllipse (Rectangle<float> (tr * 2.0f, tr * 2.0f).withCentre (tc));
            g.restoreState();
            g.setColour (p.capBottom.darker (0.6f).withAlpha (0.85f));
            g.drawLine ({ tc - dir * tr * 0.35f, tc + dir * tr * 0.25f }, juce::jmax (1.0f, size * 0.014f));
        }
        if (k.active)
        {
            g.setColour (lit.withAlpha (0.55f));
            g.drawEllipse (cap.expanded (1.0f), 1.2f);
        }
        return;
    }

    if (th0.arcInCap)
    {
        const auto capR = size * 0.5f - 1.0f;
        const auto cap = Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
        if (fx)
        {
            g.setColour (Colours::black.withAlpha (0.4f));
            g.fillEllipse (cap.translated (0.0f, capR * 0.06f).expanded (1.0f));
        }
        g.setGradientFill (ColourGradient (p.capTop.brighter (hover * 0.08f), c.x, cap.getY(), p.capBottom, c.x, cap.getBottom(), false));
        g.fillEllipse (cap);
        g.setColour (p.rim.withAlpha (k.enabled ? 0.9f : 0.5f));
        g.drawEllipse (cap.reduced (0.5f), juce::jmax (1.0f, size * 0.012f));
        const auto t = juce::jlimit (1.6f, 3.2f, size * 0.035f);
        const auto r = capR - t * 1.6f;
        if (std::abs (to - from) > 0.002f)
        {
            g.setColour (lit);
            g.strokePath (arc (c, r, from, to), juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }
        // the mark at the value, just inside the arc
        const auto tip = c + dir * (r - t * 1.9f);
        g.setColour (k.enabled ? lit : theme::colour::textDisabled);
        g.fillRect (Rectangle<float> (t * 1.1f, t * 1.1f).withCentre (tip));
        return;
    }

    const auto thin = th0.thin;
    const auto t = juce::jlimit (thin ? 1.6f : 2.0f, thin ? 3.5f : 7.0f, size * (thin ? 0.05f : 0.085f));
    const auto ringR = size * 0.5f - t * 0.7f;

    // a printed scale (fine marks) or notches (bold)
    const auto scale = th0.ticks > 1 && size >= 30.0f;
    if (scale)
    {
        const int n = th0.ticks;
        g.setColour (th0.boldTicks ? p.accent2.withAlpha (0.7f) : p.title.withAlpha (0.65f));
        for (int i = 0; i < n; ++i)
        {
            const auto ang = arcStart + arcRange * (float) i / (float) (n - 1);
            const Point<float> d { std::sin (ang), -std::cos (ang) };
            if (th0.dotTicks) // round tp172d: a dot per mark
            {
                const auto dot = juce::jmax (2.0f, size * 0.045f);
                g.fillEllipse (Rectangle<float> (dot, dot).withCentre (c + d * (size * 0.5f - dot * 0.5f)));
            }
            else
                g.drawLine ({ c + d * (size * 0.5f - 0.5f), c + d * (size * 0.5f - (i % 5 == 0 ? 4.5f : 2.5f)) }, th0.boldTicks ? 1.6f : 1.0f);
        }
        if (th0.numerals && size >= 70.0f) // round tp172d (look slice): the printed 0 … 10
        {
            g.setFont (juce::FontOptions (juce::jmax (8.0f, size * 0.11f), juce::Font::bold));
            const auto litNumeral = th0.numeralLit && k.enabled ? juce::roundToInt (value * 10.0f) : -1; // (slice 8: the nearest one in red)
            for (int i = 0; i <= 10; ++i)
            {
                g.setColour (i == litNumeral ? p.accent : (th0.boldTicks ? p.accent2.withAlpha (0.7f) : p.title.withAlpha (0.65f)));
                const auto ang = arcStart + arcRange * (float) i / 10.0f;
                const auto q = c + Point<float> { std::sin (ang), -std::cos (ang) } * (size * 0.5f + size * 0.09f);
                g.drawText (juce::String (i * th0.numeralStep), Rectangle<float> (size * (th0.numeralStep > 1 ? 0.26f : 0.16f), size * 0.12f).withCentre (q), juce::Justification::centred, false);
            }
        }
    }
    const auto r = scale ? ringR - 4.0f : ringR;

    // the track and the lit value arc
    if (! th0.noValueArc)
    {
        g.setColour (p.track.withAlpha (k.enabled ? 1.0f : 0.6f));
        g.strokePath (arc (c, r, arcStart, arcStart + arcRange), juce::PathStrokeType (t, juce::PathStrokeType::curved, th0.buttTrack ? juce::PathStrokeType::butt : juce::PathStrokeType::rounded));
        if (std::abs (to - from) > 0.002f)
        {
            const auto v = arc (c, r, from, to);
            if (fx && k.enabled && th0.arcGlow > 0.0f)
            {
                g.setColour (lit.withAlpha (th0.arcGlow));
                g.strokePath (v, stroke (t * th0.arcGlowWidth));
            }
            if (th0.twoTone)
                g.setGradientFill (ColourGradient (lit2, c.x - r, c.y + r, lit, c.x + r, c.y - r, false));
            else
                g.setColour (lit);
            g.strokePath (v, stroke (t));
        }
    }

    // round tp172d (slice 8): a chicken-head pointer knob (an opto leveler's HI FREQ): a round base, a tapered bar across it
    if (th0.pointerKnobTint.getAlpha() > 0 && knobTint == th0.pointerKnobTint)
    {
        const auto base = juce::jmax (3.0f, r);
        const auto cream = k.enabled ? th0.pointerKnobTint.brighter (hover * 0.08f) : th0.pointerKnobTint.withMultipliedSaturation (0.3f);
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillEllipse (Rectangle<float> (base * 1.3f, base * 1.3f).withCentre (c.translated (base * 0.08f, base * 0.14f)));
        g.setGradientFill (ColourGradient (cream.brighter (0.15f), c.x, c.y - base * 0.6f, cream.darker (0.35f), c.x, c.y + base * 0.6f, false));
        g.fillEllipse (Rectangle<float> (base * 1.2f, base * 1.2f).withCentre (c));
        juce::Path bar; // the head: long and pointed at the value, short and round behind
        const auto w = base * 0.42f;
        bar.startNewSubPath (0.0f, -base * 0.98f);
        bar.lineTo (w * 0.5f, -base * 0.55f);
        bar.lineTo (w * 0.55f, base * 0.55f);
        bar.quadraticTo (0.0f, base * 0.8f, -w * 0.55f, base * 0.55f);
        bar.lineTo (-w * 0.5f, -base * 0.55f);
        bar.closeSubPath();
        bar.applyTransform (juce::AffineTransform::rotation (to).translated (c));
        g.setColour (Colours::black.withAlpha (0.4f));
        g.fillPath (bar, juce::AffineTransform::translation (base * 0.06f, base * 0.12f));
        g.setGradientFill (ColourGradient (cream.brighter (0.25f), c.x - base * 0.5f, c.y - base * 0.5f, cream.darker (0.25f), c.x + base * 0.5f, c.y + base * 0.5f, false));
        g.fillPath (bar);
        g.setColour (Colour (0xff3a3328).withAlpha (0.7f));
        g.strokePath (bar, juce::PathStrokeType (0.9f));
        g.setColour (Colour (0xff2a2520));
        g.drawLine ({ c + dir * base * 0.2f, c + dir * base * 0.88f }, juce::jmax (1.2f, base * 0.07f)); // its index line
        return;
    }

    // the cap (round tp172c: without the arc it fills the scale, a console knob)
    const auto capR = juce::jmax (3.0f, th0.noValueArc ? r + t * 0.2f : r - t * (thin ? 2.4f : 1.6f));
    const auto cap = Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
    if (fx)
    {
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillEllipse (cap.translated (0.0f, capR * 0.12f).expanded (1.0f));
    }
    if (th0.drawCap != nullptr) th0.drawCap (g, cap, c, capR, hover, fx);
    else drawCapFamily (g, th0.cap, cap, c, capR, hover, fx);

    // the pointer
    const auto pw = juce::jlimit (1.2f, 3.5f, size * (thin ? 0.035f : 0.05f));
    const auto pointerCol = k.enabled ? p.pointer : theme::colour::textSecondary;
    if (th0.pointer == Pointer::electron)
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
        ptr.startNewSubPath (c + dir * capR * th0.pointerFrom);
        ptr.lineTo (c + dir * capR * 0.86f);
        const auto glowing = th0.pointer == Pointer::glowLine && k.enabled;
        if (glowing && fx)
        {
            g.setColour (lit.withAlpha (0.4f));
            g.strokePath (ptr, stroke (pw * 3.0f));
        }
        g.setColour (glowing ? lit.interpolatedWith (Colours::white, 0.35f) : pointerCol);
        g.strokePath (ptr, stroke (pw));
    }
    if (th0.tipDeco != nullptr && size >= 34.0f && k.enabled)
        th0.tipDeco (g, c + dir * r, t, to);
}

inline bool slider (juce::Graphics& g, Rectangle<float> tr, float v, bool vertical, bool bipolar, Colour, bool hot, bool enabled, float thumbLength, float thumbThickness)
{
    const auto& th0 = th();
    const auto& p = th0.pal;
    v = juce::jlimit (0.0f, 1.0f, v);
    if (th0.barSlider) // round tp172d: a wide flat bar filled in the accent (Valhalla's faders)
    {
        const auto wide = vertical ? tr.withSizeKeepingCentre (juce::jmax (tr.getWidth(), thumbLength * 0.9f), tr.getHeight())
                                   : tr.withSizeKeepingCentre (tr.getWidth(), juce::jmax (tr.getHeight(), thumbLength * 0.6f));
        g.setColour (p.track);
        g.fillRect (wide);
        const auto barBase = th0.barCol.getAlpha() > 0 ? th0.barCol : p.accent;
        const auto lit = enabled ? barBase.brighter (hot ? 0.15f : 0.0f) : theme::colour::textDisabled;
        auto fill = wide;
        if (vertical)
        {
            const auto pos = wide.getBottom() - v * wide.getHeight();
            const auto zero = bipolar ? wide.getCentreY() : wide.getBottom();
            fill = Rectangle<float> (wide.getX(), juce::jmin (zero, pos), wide.getWidth(), std::abs (zero - pos));
            g.setColour (lit.withAlpha (0.8f));
            g.fillRect (fill);
            g.setColour (lit);
            g.fillRect (Rectangle<float> (wide.getX(), pos - 2.0f, wide.getWidth(), 4.0f)); // the level edge
        }
        else
        {
            const auto pos = wide.getX() + v * wide.getWidth();
            const auto zero = bipolar ? wide.getCentreX() : wide.getX();
            fill = Rectangle<float> (juce::jmin (zero, pos), wide.getY(), std::abs (zero - pos), wide.getHeight());
            g.setColour (lit.withAlpha (0.8f));
            g.fillRect (fill);
            g.setColour (lit);
            g.fillRect (Rectangle<float> (pos - 2.0f, wide.getY(), 4.0f, wide.getHeight()));
        }
        return true;
    }
    if (th0.bigThumb > 0.0f && vertical) // round tp172d: the noise suppressor's big metal fader cap on a recessed slot
    {
        const auto slot = tr.withSizeKeepingCentre (thumbLength * 0.45f, tr.getHeight());
        g.setGradientFill (ColourGradient (Colour (0xff050607), slot.getX(), 0.0f, Colour (0xff2a2d31), slot.getRight(), 0.0f, false));
        g.fillRoundedRectangle (slot, 3.0f);
        g.setColour (Colours::white.withAlpha (0.18f));
        g.drawRoundedRectangle (slot.expanded (1.5f), 4.0f, 1.0f);
        const auto capH = juce::jmin (tr.getHeight() * 0.18f, thumbThickness * th0.bigThumb);
        const auto travel = slot.reduced (0.0f, capH * 0.5f);
        const auto y = travel.getBottom() - v * travel.getHeight();
        const auto cap = Rectangle<float> (thumbLength * 2.4f, capH).withCentre ({ slot.getCentreX(), y });
        g.setColour (Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (cap.translated (0.0f, 3.0f), 3.0f);
        g.setGradientFill (ColourGradient (p.capTop.brighter (hot ? 0.2f : 0.0f), 0.0f, cap.getY(), p.capBottom, 0.0f, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 3.0f);
        for (int k = 1; k < 7; ++k) // the grooves
        {
            const auto gy = cap.getY() + cap.getHeight() * (float) k / 7.0f;
            g.setColour (Colours::black.withAlpha (0.28f));
            g.drawHorizontalLine ((int) gy, cap.getX() + 3.0f, cap.getRight() - 3.0f);
            g.setColour (Colours::white.withAlpha (0.25f));
            g.drawHorizontalLine ((int) gy + 1, cap.getX() + 3.0f, cap.getRight() - 3.0f);
        }
        g.setColour (enabled ? p.accent : theme::colour::textDisabled); // the index line
        g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 4.0f, 1.6f));
        return true;
    }
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
    if (! reduced && enabled && ! th0.thin)
    {
        g.setColour (lit.withAlpha (0.25f));
        g.fillRoundedRectangle (fill.expanded (2.0f), 3.0f);
    }
    if (th0.twoTone)
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
    g.setColour (enabled ? (th0.thumbPointer ? p.pointer : lit) : theme::colour::textDisabled);
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
        if (th().twoTone)
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

// every art id, in order (generators, then effects)
const std::vector<Art>& allArts()
{
    static const std::vector<Art> all {
       #define LYNX_ART(id, value) Art::id,
       #include "ui/PluginArtGeneratorIds.inc"
       #include "ui/PluginArtEffectIds.inc"
       #undef LYNX_ART
    };
    return all;
}
} // namespace

juce::Colour captionColour()
{
    if (currentStyle == Style::themed && artkit::onDarkFill) return juce::Colour (0xffe4e6e8);
    if (currentStyle == Style::themed && artkit::th().captionCol.getAlpha() > 0) return artkit::th().captionCol;
    return juce::Colour (0xff8c919c);
}
juce::Colour valueColour (bool hot)
{
    if (currentStyle == Style::themed && artkit::onDarkFill) return hot ? juce::Colours::white : juce::Colour (0xffc9ccd0);
    if (currentStyle == Style::themed && artkit::th().valueCol.getAlpha() > 0)
        return hot ? artkit::th().valueCol.brighter (0.3f) : artkit::th().valueCol;
    return hot ? theme::colour::textPrimary : juce::Colour (0xffd3d6dd);
}
float captionScale() { return currentStyle == Style::themed ? artkit::th().captionScale : 1.0f; }
bool lowerCaptions() { return currentStyle == Style::themed && artkit::th().lowerCaptions; }
bool ledReadouts() { return currentStyle == Style::themed && artkit::th().ledReadouts; }
bool titleRules() { return currentStyle == Style::themed && artkit::th().titleRules; }
bool vuBezel() { return currentStyle == Style::themed && (artkit::th().vuBezel || artkit::th().vuBezelDark); }
bool vuBezelDark() { return currentStyle == Style::themed && artkit::th().vuBezelDark; }
bool goldKeys() { return currentStyle == Style::themed && artkit::th().goldKeys && artkit::onDarkFill; }
void goldKey (juce::Graphics& g, juce::Rectangle<float> r, bool lit, bool hover) { artkit::goldKey (g, r, lit, hover); }
void batToggle (juce::Graphics& g, juce::Rectangle<float> r, bool up, bool hover, bool enabled) { artkit::batToggle (g, r, up, hover, enabled); }
void selectorKnob (juce::Graphics& g, juce::Rectangle<float> r, float angle, bool hover, bool enabled) { artkit::selectorKnob (g, r, angle, hover, enabled); }
bool hwKeys() { return currentStyle == Style::themed && artkit::th().hwKeys; }
void hwKey (juce::Graphics& g, juce::Rectangle<float> r, bool lit, bool hover, juce::Colour litColour) { artkit::hwKey (g, r, lit, hover, litColour); }
void setSectionOrdinal (int ordinal) { artkit::sectionOrdinal = ordinal; }
void setOnDarkFill (bool dark) { artkit::onDarkFill = dark; }
bool sectionFillIsDark (int ordinal)
{
    if (currentStyle != Style::themed || ordinal < 0 || ordinal >= 4) return false;
    const auto c = artkit::th().sectionFills[ordinal];
    return c.getAlpha() > 0 && c.getBrightness() < 0.35f;
}

Style baseStyleOf (Art a) { return a == Art::none ? Style::glow : artkit::themeOf (a).base; }
juce::Colour artAccent (Art a) { return a == Art::none ? colour::crimson : artkit::palette (a).accent; }
juce::Colour artAccent2 (Art a) { return a == Art::none ? colour::crimson : artkit::palette (a).accent2; }
const char* artName (Art a) { return a == Art::none ? "" : artkit::themeOf (a).name; }

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
    using Mote = artkit::Mote;
    std::vector<Mote> motes;
    juce::Random rnd { 170 };

    void reset (Art a, juce::Rectangle<float> area, juce::Rectangle<float> st)
    {
        motes.clear();
        rnd.setSeed (170);
        if (a == Art::none) return;
        const auto& th = artkit::themeOf (a);
        const auto s = st.isEmpty() ? area : st;
        for (int i = 0; i < th.motes; ++i)
        {
            Mote m;
            m.x = s.getX() + rnd.nextFloat() * s.getWidth();
            m.y = s.getY() + rnd.nextFloat() * s.getHeight();
            m.size = th.sizeMin + rnd.nextFloat() * th.sizeRange;
            m.vx = th.vxMin + rnd.nextFloat() * th.vxRange;
            m.vy = th.vyMin + rnd.nextFloat() * th.vyRange;
            m.rot = rnd.nextFloat() * 6.28f;
            m.spin = (rnd.nextFloat() - 0.5f) * 2.4f;
            m.phase = rnd.nextFloat() * 6.28f;
            motes.push_back (m);
        }
    }

    static juce::Rectangle<float> boundsOf (const artkit::Theme& th, const Mote& m)
    {
        const auto r = m.size * th.reach + 2.0f;
        return { m.x - r, m.y - r, r * 2.0f, r * 2.0f };
    }

    // one step of a mote's travel; it re-enters at the other side of the stage once it has left it
    void step (const artkit::Theme& th, Mote& m, float dt, float t, juce::Rectangle<float> s)
    {
        switch (th.drift)
        {
            case artkit::Drift::fall: // drifting down with a sway, tumbling
                m.x += (m.vx + th.sway * std::sin (t * 1.3f + m.phase)) * dt;
                m.y += m.vy * dt;
                m.rot += m.spin * dt;
                if (m.y > s.getBottom() + 6.0f || m.x > s.getRight() + 10.0f)
                {
                    m.y = s.getY() - 6.0f;
                    m.x = s.getX() + rnd.nextFloat() * s.getWidth() * 0.9f;
                }
                break;
            case artkit::Drift::rise: // rising with a wobble, popping at the top
                m.y += m.vy * dt;
                m.x += th.sway * std::sin (t * 3.0f + m.phase) * dt;
                m.rot += m.spin * dt;
                if (m.y < s.getY())
                {
                    m.y = s.getBottom();
                    m.x = s.getX() + rnd.nextFloat() * s.getWidth();
                }
                break;
            case artkit::Drift::still:
            default:
                break;
        }
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
    const auto moves = artId != Art::none && artkit::themeOf (artId).paintMotion != nullptr;
    const auto run = moves && showing() && artMotion() && ! reduced && ! osReducedMotion();
    if (run && ! isTimerRunning())
    {
        lastMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (juce::jlimit (5, 30, artkit::themeOf (artId).hz));
    }
    else if (! run && isTimerRunning())
        stopTimer();
}

void ArtLayer::setPhase (double seconds)
{
    stopTimer();
    impl->reset (artId, getLocalBounds().toFloat(), stage);
    t = 0.0;
    if (artId == Art::none) return;
    // advance in steps so the motes travel as they would have
    const auto& th = artkit::themeOf (artId);
    const auto area = getLocalBounds().toFloat();
    const auto s = stage.isEmpty() ? area : stage;
    const auto steps = (int) (seconds * 30.0);
    for (int i = 0; i < steps; ++i)
    {
        t += 1.0 / 30.0;
        for (auto& m : impl->motes)
            impl->step (th, m, 1.0f / 30.0f, (float) t, s);
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
    const auto& th = artkit::themeOf (artId);
    const auto area = getLocalBounds().toFloat();
    const auto s = stage.isEmpty() ? area : stage;
    juce::RectangleList<int> dirty;
    for (auto& m : impl->motes)
    {
        dirty.addWithoutMerging (Impl::boundsOf (th, m).getSmallestIntegerContainer());
        impl->step (th, m, dt, (float) t, s);
        dirty.addWithoutMerging (Impl::boundsOf (th, m).getSmallestIntegerContainer());
    }
    if (th.stageRepaint) // the stage glows, turns or scrolls: repaint it (or only the part that moves)
        dirty.add ((th.motionArea != nullptr ? th.motionArea (s).getIntersection (s) : s).getSmallestIntegerContainer());
    dirty.consolidate();
    for (const auto& r : dirty)
        repaint (r);
}

void ArtLayer::paint (juce::Graphics& g)
{
    if (artId == Art::none || (isActive != nullptr && ! isActive())) return;
    const auto& th = artkit::themeOf (artId);
    if (th.paintMotion == nullptr) return;
    const StyleScope scope (Style::themed, artId);
    const auto area = getLocalBounds().toFloat();
    th.paintMotion (g, { th.pal, area, stage.isEmpty() ? area : stage, (float) t, impl->motes });
}

juce::String artSelfTest()
{
    juce::StringArray errors;
    auto expect = [&] (bool ok, const juce::String& what) { if (! ok) errors.add (what); };
    const auto keptForced = forcedStyle();
    const auto keptMotion = artMotion();
    expect (styleFromName ("Themed") == Style::themed && juce::String (styleName (Style::themed)) == "Themed", "style name");
    expect (unpackStyle (packStyle (Style::themed, Art::toxic)) == Style::themed && unpackArt (packStyle (Style::themed, Art::toxic)) == Art::toxic, "pack");
    setForcedStyle (-1);
    expect (styleFor (EffectKind::facetEq) == Style::themed && styleFor (EffectKind::nebula) == Style::themed, "pilots are Themed");
    expect (styleFor (EffectKind::peakController) == Style::glow, "others keep their style");
    for (const auto k : { EffectKind::ridgeStrip, EffectKind::crestCompressor, EffectKind::apexLimiter, EffectKind::prismMultiband, EffectKind::sentryGate,
                          EffectKind::glowLeveler, EffectKind::hushDeEsser, EffectKind::epochVerb, EffectKind::atrium, EffectKind::foilPlate,
                          EffectKind::halo, EffectKind::horizon, EffectKind::reeverb2, EffectKind::luxeVerb, EffectKind::reeverb1,
                          EffectKind::echoform, EffectKind::spiralEcho, EffectKind::orbit, EffectKind::latticeMod, EffectKind::geminiDoubler,
                          EffectKind::fruityDelay2, EffectKind::fruityDelay3, EffectKind::fruityDelayBank, EffectKind::ember, EffectKind::kilnChannel, EffectKind::fretChannel, EffectKind::shellChannel, EffectKind::strikeShaper, EffectKind::anvilBus, EffectKind::aether,
                          EffectKind::fruityBloodOverdrive, EffectKind::fruityFastDist, EffectKind::fruityWaveShaper, EffectKind::fruitySoftClipper,
                          EffectKind::hardcore, EffectKind::distructor, EffectKind::fruityChorus, EffectKind::hyperChorus, EffectKind::vintageChorus,
                          EffectKind::fruityFlanger, EffectKind::fruityFlangus, EffectKind::fruityPhaser, EffectKind::vintagePhaser,
                          EffectKind::flFrequencyShifter, EffectKind::parametricEq, EffectKind::fruityLimiter, EffectKind::fruityCompressor,
                          EffectKind::fruityMultiband, EffectKind::maximus, EffectKind::transientProcessor, EffectKind::stereoShaper,
                          EffectKind::stereoEnhancer, EffectKind::spreader, EffectKind::grossBeat, EffectKind::vocodex, EffectKind::vocoder, EffectKind::pitchCorrect, EffectKind::lynxTune, EffectKind::voiceShift, EffectKind::quellDenoiser, EffectKind::flPitchShifter, EffectKind::lovePhilter, EffectKind::effector, EffectKind::squeeze, EffectKind::lofi, EffectKind::eq, EffectKind::compressor, EffectKind::delay, EffectKind::reverb, EffectKind::limiter, EffectKind::distortion, EffectKind::filter, EffectKind::chorus, EffectKind::autoPan, EffectKind::fruityPanOMatic, EffectKind::fruityBalance, EffectKind::utility, EffectKind::fruitySend, EffectKind::frequencySplitter, EffectKind::crossover, EffectKind::multibandDelay, EffectKind::equo, EffectKind::emphasizer, EffectKind::flLowLifter, EffectKind::fruityFastLP, EffectKind::fruityFilter, EffectKind::fruityFreeFilter, EffectKind::fl7BandEq, EffectKind::flBassBoost, EffectKind::convolver, EffectKind::volumeGate, EffectKind::rhythmGain, EffectKind::freqShift, EffectKind::pitchShift, EffectKind::flPhaseInverter, EffectKind::flCenter, EffectKind::flDelay1, EffectKind::fruityMute2 }) // round 170k: the effects' themes
        expect (artForEffect (k) != Art::none && styleFor (k) == Style::themed, "effect themes (170k)");
    for (int a = 128; a < 256; ++a) // round 170k: a theme that names its moving corner keeps it small and inside the stage
        if (const auto* th = artkit::effectTheme ((Art) a); th != nullptr && th->motionArea != nullptr)
        {
            const juce::Rectangle<float> stage (200.0f, 4.0f, 420.0f, 30.0f);
            const auto area = th->motionArea (stage).getIntersection (stage);
            expect (th->stageRepaint && th->paintMotion != nullptr && ! area.isEmpty() && area.getWidth() * area.getHeight() < stage.getWidth() * stage.getHeight() * 0.5f,
                    "motion area (170k)");
        }
    expect (resolve (Style::themed, Art::sakura) == Style::themed && resolve (Style::neon, Art::none) == Style::neon, "automatic");
    setForcedStyle ((int) Style::themed); // Themed everywhere: only plugins with art
    expect (styleFor (EffectKind::facetEq) == Style::themed && styleFor (EffectKind::peakController) == Style::glow, "forced Themed");
    expect (resolve (Style::neon, Art::none) == Style::neon && resolve (Style::themed, Art::none) == Style::glow, "forced Themed without art");
    setForcedStyle ((int) Style::vivid);
    expect (styleFor (EffectKind::facetEq) == Style::vivid && resolve (Style::themed, Art::toxic) == Style::vivid, "forced Vivid");
    setForcedStyle (keptForced);
    expect (artForInstrument (InstrumentKind::sakura) == Art::sakura && artForInstrument (InstrumentKind::threeosc) == Art::threeOsc
                && artForInstrument (InstrumentKind::kick) == Art::none,
            "instrument art");
    { // every generator with a plugin page of its own has a theme (round 170j's rollout)
        using I = InstrumentKind;
        for (const auto k : { I::sakura, I::drumpad, I::boobass, I::drumsynth, I::harmless, I::sytrus, I::harmor, I::gms, I::morphine, I::ogun,
                              I::minisynth, I::dx10, I::kepler, I::poizone, I::fruitkick, I::bassdrum, I::toxic, I::threeosc, I::autogun,
                              I::transistorbass, I::simsynth, I::wasp, I::plucked, I::beepmap, I::sawer, I::flkeys, I::granulizer, I::drumaxx,
                              I::slayer, I::soundfont, I::keplerExo, I::wavetraveller, I::synth })
            expect (artForInstrument (k) != Art::none, "a generator without art");
        expect (artForInstrument (I::sampler) == Art::none && artForInstrument (I::layer) == Art::none, "channel chrome keeps the shared style");
    }
    expect (artForEffect (EffectKind::facetEq) == Art::facet && artForEffect (EffectKind::peakController) == Art::none, "effect art");
    for (const auto a : allArts()) // every id has exactly one theme, with a name and a canvas
    {
        const auto* gt = artkit::generatorTheme (a);
        const auto* et = artkit::effectTheme (a);
        expect ((gt != nullptr) != (et != nullptr), "one theme per art");
        const auto& t = artkit::themeOf (a);
        expect (juce::String (t.name).isNotEmpty() && t.canvas != nullptr, "theme name and canvas");
        expect (((int) a < 128) == (gt != nullptr), "generator ids 1..127, effect ids 128..255");
    }
    // every art draws its canvas, a section, a knob, a fader, a button and its moving layer
    for (const auto a : allArts())
    {
        juce::Image img (juce::Image::ARGB, 320, 200, true);
        juce::Image softwareCanvas (juce::Image::ARGB, 320, 200, true, juce::SoftwareImageType());
        {
            juce::Graphics g (img);
            const StyleScope scope (Style::themed, a);
            if (const auto drawCanvas = artkit::themeOf (a).canvas; drawCanvas != nullptr)
            {
                juce::Graphics software (softwareCanvas);
                drawCanvas (software, { 0.0f, 0.0f, 320.0f, 200.0f }, false);
            }
            canvas (g, { 0.0f, 0.0f, 320.0f, 200.0f }, colour::crimson);
            section (g, { 10.0f, 10.0f, 140.0f, 120.0f }, 6.0f, true, colour::crimson);
            sectionTitle (g, { 10.0f, 10.0f, 140.0f, 16.0f }, "OSC 1", colour::crimson, 10.0f);
            KnobLook k;
            k.value = 0.7f;
            knob (g, { 30.0f, 40.0f, 60.0f, 60.0f }, k);
            slider (g, { 200.0f, 20.0f, 4.0f, 120.0f }, 0.4f, true, false, colour::crimson, false, true, 24.0f, 12.0f);
            button (g, { 220.0f, 160.0f, 60.0f, 20.0f }, true, false, colour::crimson, 4.0f);
            expect (style() == Style::themed && art() == a, "scope");
        }
        expect (style() != Style::themed, "scope restored");
        // Native 8-bit compositing can round an opaque canvas to alpha 254 after translucent overlays.
        // Check exact opacity with the software renderer, then allow only that one-unit native rounding error.
        const auto softwareAlpha = softwareCanvas.getPixelAt (160, 100).getAlpha();
        expect (softwareAlpha == 255, juce::String (artName (a)) + " (software canvas alpha " + juce::String (softwareAlpha) + ")");
        const auto px = img.getPixelAt (160, 100);
        expect (px.getAlpha() >= 254, juce::String (artName (a)) + " (rendered alpha " + juce::String (px.getAlpha()) + ")");
        ArtLayer layer;
        layer.setSize (320, 200);
        layer.setArt (a, [] { return true; });
        layer.setStage ({ 200.0f, 10.0f, 100.0f, 40.0f });
        layer.setPhase (1.5);
        juce::Image moving (juce::Image::ARGB, 320, 200, true);
        {
            juce::Graphics g (moving);
            layer.paint (g);
        }
        expect (! layer.animating(), "a layer off screen never animates");
    }
    { // round 170k2: a plugin's name always fits its header box: every effect's name, every style, in the narrowest box
      // titleMinWidth promises (what the effect windows' minimum width is built from) draws whole, no ellipsis
        juce::Image img (juce::Image::ARGB, 600, 26, true);
        juce::Graphics g (img);
        auto clipped = 0, twoLines = 0;
        for (const auto st : { Style::glow, Style::neon, Style::hardware, Style::vivid })
        {
            const StyleScope scope (st);
            for (int k = 0; k <= (int) lastEffectKind; ++k)
            {
                const auto* raw = effectInfo ((EffectKind) k).name;
                const auto name = raw != nullptr ? juce::String::fromUTF8 (raw) : juce::String();
                if (name.isEmpty()) continue;
                titleText (g, { 66.0f, 0.0f, titleMinWidth (name, 26.0f), 26.0f }, name, juce::Colours::red);
                clipped += lastTitleFit().clipped ? 1 : 0;
                twoLines += lastTitleFit().lines == 2 ? 1 : 0;
            }
        }
        expect (clipped == 0, "a plugin name clipped in its narrowest header box");
        expect (twoLines > 0, "the Fruity prefix line");
        for (const auto st : { Style::glow, Style::hardware })
        {
            const StyleScope scope (st);
            titleText (g, { 66.0f, 0.0f, 400.0f, 26.0f }, "Fruity Blood Overdrive", juce::Colours::red);
            expect (lastTitleFit().lines == 1 && ! lastTitleFit().clipped && lastTitleFit().size >= 12.0f, "a wide box keeps the one-line name");
            // the narrowest box the name may get (titleMinWidth): too narrow for one line at the smallest size in any style
            titleText (g, { 66.0f, 0.0f, titleMinWidth ("Fruity Blood Overdrive", 26.0f), 26.0f }, "Fruity Blood Overdrive", juce::Colours::red);
            const auto f = lastTitleFit();
            if (f.lines != 2 || f.clipped)
                errors.add ("a narrow box puts Fruity above the name (" + juce::String (styleName (st)) + ": lines " + juce::String (f.lines) + ", size "
                            + juce::String (f.size, 1) + ", need " + juce::String (f.need, 1) + ")");
        }
    }
    setArtMotion (keptMotion);
    return errors.isEmpty() ? juce::String ("OK") : "ERROR " + errors.joinIntoString (", ");
}

// The effects' themes of the "Themed" plugin style (see PluginArt.inc for the framework and the rules; the art
// directions and the style guide are in the tracker's plugin-art-0.1.24.md). One block per theme: its drawing
// hooks, then its Theme record; then the registry and artForEffect. Ids: PluginArtEffectIds.inc (128..255).
// LYNX's own procedural drawings only — never a copied, traced or extracted bitmap, logo or skin.

namespace artkit
{
// round tp172d: the flat canvas of the originals' graphic looks (Valhalla...) — one solid field, a faint fall-off
inline void canvasFlat (juce::Graphics& g, Rectangle<float> a, bool)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.bgTop, 0.0f, a.getY(), p.bgBottom, 0.0f, a.getBottom(), false));
    g.fillRect (a);
}

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
        x.pal = { Colour (0xff1f1d1c), Colour (0xff0d0c0c), Colour (0xffffc84a), Colour (0xff7fd0ff), Colour (0xd820201f), Colour (0xe80f0f0e),
                  Colour (0xff6a6560), Colour (0xfff2eee6), Colour (0xff090808), Colour (0xff35322f), Colour (0xff1a1817), Colour (0xff2c2a28),
                  Colour (0xfff7f3ea), Colour (0xff1c1a19) };
        x.canvas = canvasFacet;
        x.thin = true;
        x.kerning = 0.06f;
        x.cap = Cap::flat;
        x.arcGlow = 0.0f;
        return x;
    }();
    return t;
}

// === Nebula (Valhalla Supermassive role): the original's flat graphic panel — indigo night, violet fields, big black knobs ===

inline const Theme& nebulaTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Nebula";
        x.base = Style::glow;
        x.pal = { Colour (0xff131133), Colour (0xff0c0b24), Colour (0xffbdb8ff), Colour (0xff7d78ff), Colour (0xff4a44c6), Colour (0xff4a44c6),
                  Colour (0xff6c66f0), Colour (0xffffffff), Colour (0xff1b1842), Colour (0xff060608), Colour (0xff060608), Colour (0xff2b2775),
                  Colour (0xffffffff), Colour (0xff3a35a0) };
        x.canvas = canvasFlat;
        x.flatSections = true;
        x.discKnob = true;
        x.pointerFrom = 0.0f;
        x.arcGlow = 0.0f;
        x.kerning = 0.02f;
        x.captionScale = 1.3f;
        x.captionCol = Colours::white;
        x.valueCol = Colour (0xffc9c5ff);
        x.rimWidth = 0.0f;
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

// round tp172b4: the references' fields — light under the header, darkest a little past the middle, lifting again
// toward the bottom bar
inline void valley (juce::Graphics& g, Rectangle<float> a, Colour top, Colour mid, Colour bottom, float at)
{
    ColourGradient grad (top, 0.0f, a.getY(), bottom, 0.0f, a.getBottom(), false);
    grad.addColour (juce::jlimit (0.05, 0.95, (double) at), mid);
    g.setGradientFill (grad);
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


// round tp172d: the Soundtoys-role hardware look — a black anodised face between two walnut cheeks, corner screws
inline void woodCheek (juce::Graphics& g, Rectangle<float> a, juce::int64 seed)
{
    vfill (g, a, Colour (0xff6a3a1c), Colour (0xff3e200d));
    if (reduced) return;
    juce::Random rnd (seed);
    for (int i = 0; i < 14; ++i) // the grain: long wavy darker lines
    {
        juce::Path gr;
        const auto x0 = a.getX() + rnd.nextFloat() * a.getWidth();
        const auto amp = 0.6f + rnd.nextFloat() * 1.4f, ph = rnd.nextFloat() * 6.0f;
        for (float y = a.getY(); y <= a.getBottom(); y += 6.0f)
        {
            const Point<float> q { x0 + amp * std::sin (ph + y * 0.035f), y };
            if (y == a.getY()) gr.startNewSubPath (q);
            else gr.lineTo (q);
        }
        g.setColour (Colour (0xff1e0e04).withAlpha (0.25f + 0.3f * rnd.nextFloat()));
        g.strokePath (gr, juce::PathStrokeType (0.6f + rnd.nextFloat() * 1.2f));
    }
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.10f), a.getX(), 0.0f, Colours::transparentBlack, a.getRight(), 0.0f, false));
    g.fillRect (a);
}

inline void canvasSoundtoys (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto cheek = th().cheek > 0.0f ? th().cheek : juce::jlimit (8.0f, 16.0f, a.getWidth() * 0.016f);
    woodCheek (g, a.withWidth (cheek), 7);
    woodCheek (g, a.withTrimmedLeft (a.getWidth() - cheek), 11);
    const auto face = a.reduced (cheek, 0.0f);
    brushedLines (g, face, 72, 0.025f);
    g.setColour (Colours::black.withAlpha (0.6f)); // the face's edge against the wood
    g.drawRect (face, 1.5f);
    cornerScrews (g, face.reduced (2.0f), 5.0f, 4.0f);
}

inline Theme soundtoysTheme (const char* name, Colour accent, Colour title, Colour panel, Colour rim, float rimWidth)
{
    Theme x;
    x.name = name;
    x.base = Style::hardware;
    x.pal = { Colour (0xff1b1b1c), Colour (0xff0c0c0d), accent, accent.brighter (0.4f), panel, panel, rim, title, Colour (0xff070707),
              Colour (0xff2a2a2c), Colour (0xff060606), Colour (0x00000000), Colours::white, Colour (0xff232325) };
    x.canvas = canvasSoundtoys;
    x.flatSections = true;
    x.rimWidth = rimWidth;
    x.cap = Cap::rubber;
    x.ticks = 11;           // the printed dot scale around each knob
    x.pointerFrom = 0.15f;
    x.arcGlow = 0.0f;
    x.thin = true;
    x.kerning = 0.28f;      // wide-tracked caps
    x.captionCol = title;
    x.valueCol = title.withMultipliedBrightness (0.85f);
    x.captionScale = 1.1f;
    return x;
}

// === Ridge Strip (channel-strip role, round tp172c): a large-format console's channel — warm grey metal panels, ===
// colour-coded knobs (red HF, green HMF, blue LMF, black LF, silver filters, grey dynamics), printed scales, orange
// lit switches. The mood of the reference channel strips (a console module), drawn here from scratch.

inline void canvasRidge (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    // round tp172c (slice 2): the console faceplate — mid-grey brushed metal, the brushing running down the module,
    // a dark recessed channel down the middle where the meter bridge sits
    if (! reduced)
    {
        juce::Random rnd (172);
        for (int i = 0; i < 420; ++i)
        {
            const auto x = a.getX() + rnd.nextFloat() * a.getWidth();
            const auto y = a.getY() + rnd.nextFloat() * a.getHeight();
            const auto len = a.getHeight() * (0.3f + 0.7f * rnd.nextFloat());
            g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (0.010f + 0.016f * rnd.nextFloat()));
            g.fillRect (Rectangle<float> (x, y - len * 0.5f, 0.8f + rnd.nextFloat(), len));
        }
    }
    const auto mid = Rectangle<float> (a.getX() + a.getWidth() * 0.355f, a.getY(), a.getWidth() * 0.29f, a.getHeight());
    g.setGradientFill (ColourGradient (Colour (0xff2a2b2e), mid.getX(), 0.0f, Colour (0xff3b3d40), mid.getCentreX(), 0.0f, true));
    g.fillRect (mid);
    for (const auto x : { mid.getX(), mid.getRight() })
    {
        g.setColour (Colours::black.withAlpha (0.55f));
        g.fillRect (Rectangle<float> (x - 1.0f, a.getY(), 2.0f, a.getHeight()));
        g.setColour (Colours::white.withAlpha (0.35f));
        g.fillRect (Rectangle<float> (x + (x == mid.getX() ? -2.0f : 1.0f), a.getY(), 1.0f, a.getHeight()));
    }
    // a soft studio light from above
    softLight (g, { a.getCentreX(), a.getY() - a.getHeight() * 0.15f }, a.getWidth() * 0.8f, Colour (0xffffffff), 0.10f);
    vignette (g, a, 0.12f);
}

// the header: the console's dark legend strip (the faceplate around stays light)
inline void ridgeHeader (juce::Graphics& g, Rectangle<float> bar)
{
    g.setGradientFill (ColourGradient (Colour (0xff36383c), 0.0f, bar.getY(), Colour (0xff1d1e21), 0.0f, bar.getBottom(), false));
    g.fillRect (bar);
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


// the console's colour code, by parameter (a Ridge parameter name -> the cap colour)
inline Colour ridgeCapTint (const char* param)
{
    const juce::String n (param);
    // slice 2: the Vocal strip voicing's colour code (the reference vocal strip's): green compander timing and ratio,
    // grey levels, blue notch, red high band, black filter and level trims
    if (n == "Compander attack" || n == "Compander release" || n == "Compander ratio") return Colour (0xff2f8a4a);
    if (n.startsWith ("Notch ")) return Colour (0xff2c6fc9);
    if (n.startsWith ("High ")) return Colour (0xffc8202a);
    if (n == "Vocal HPF freq." || n == "Make-up" || n.startsWith ("Vocal ")) return Colour (0xff26272a);
    if (n.startsWith ("Compander") || n.startsWith ("Expander") || n.startsWith ("De-")) return Colour (0xffcfd1d3);
    if (n.startsWith ("HF ")) return Colour (0xffc8202a);                         // red
    if (n.startsWith ("HMF ")) return Colour (0xff2f9a45);                        // green
    if (n.startsWith ("LMF ")) return Colour (0xff2c6fc9);                        // blue
    if (n.startsWith ("LF ")) return Colour (0xff26272a);                         // black
    if (n == "HPF" || n == "LPF") return Colour (0xffd9dadb);                     // silver
    if (n.startsWith ("Gate ")) return Colour (0xff2e3033);                       // dark grey
    if (n.startsWith ("Comp") || n == "Ratio" || n == "Make-up offset") return Colour (0xffb9bbbd); // light grey
    if (n == "Input" || n == "Mic gain") return Colour (0xff26272a);
    // tp172c6: the DRIVE section — a red drive (the amp strips' drive cap), black level trims, a grey mix
    if (n == "Drive amount") return Colour (0xffc8202a);
    if (n == "Drive input" || n == "Drive trim") return Colour (0xff26272a);
    if (n == "Drive mix") return Colour (0xffb9bbbd);
    return {};
}

// a console knob: a moulded coloured body on a dark skirt, a light rim on top and a white pointer (drawn by the kit)
inline void capRidge (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto body = knobTint.isTransparent() ? Colour (0xff3a3c40) : knobTint;
    // the skirt
    g.setGradientFill (ColourGradient (Colour (0xff4a4c50), c.x, cap.getY(), Colour (0xff141517), c.x, cap.getBottom(), false));
    g.fillEllipse (cap.expanded (capR * 0.04f));
    // the body
    const auto top = cap.reduced (capR * 0.16f);
    g.setGradientFill (ColourGradient (body.brighter (0.35f + hover * 0.15f), c.x - capR * 0.35f, c.y - capR * 0.55f,
                                       body.darker (0.45f), c.x + capR * 0.45f, c.y + capR * 0.7f, true));
    g.fillEllipse (top);
    if (fx)
    {
        // the moulded top's highlight and its rim
        g.setColour (Colours::white.withAlpha (body.getPerceivedBrightness() > 0.6f ? 0.35f : 0.22f));
        g.drawEllipse (top.reduced (0.6f), 1.0f);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.28f), c.x, top.getY(), Colours::transparentWhite, c.x, c.y, false));
        g.fillEllipse (top.reduced (capR * 0.12f).withHeight (top.getHeight() * 0.45f).translated (0.0f, capR * 0.04f));
    }
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawEllipse (cap.expanded (capR * 0.04f), 1.0f);
}

inline const Theme& ridgeTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Ridge Strip";
        x.base = Style::hardware;
        // round tp172c (slice 2): the reference's mid-grey faceplate, light legends, dark recessed meters
        //          canvas (brushed metal)                   accent (orange lamps)      second light
        x.pal = { Colour (0xff6e7073), Colour (0xff5a5c5f), Colour (0xfff08a24), Colour (0xffffc46a),
                  // panels: barely there — one continuous faceplate split by thin dividers   rim (dividers)   titles (light legends)
                  Colour (0x0affffff), Colour (0x0affffff), Colour (0x9a2a2c2f), Colour (0xfff2f2ee),
                  // wells (the meter bridge)  caps (fader)                                   track                      pointer
                  Colour (0xff141517), Colour (0xffe6e6e2), Colour (0xff8e8e8a), Colour (0xff2e3033), Colour (0xfffafafa),
                  Colour (0xff3f4145) };
        x.canvas = canvasRidge;
        x.headerDeco = ridgeHeader;
        x.sectionDeco = ridgeScrews;
        x.titleEmblem = ridgeEmblem;
        x.drawCap = capRidge;
        x.capTint = ridgeCapTint;
        x.pointerFrom = 0.25f;
        x.ticks = 11;
        x.buttTrack = true;
        x.arcGlow = 0.0f;
        x.thumbPointer = true;
        x.offLabelCol = Colour (0xffc4c7cc); // round tp172c: light legends on the grey faceplate
        x.captionCol = Colour (0xffeeeff1);
        x.valueCol = Colour (0xffc9ccd1);
        x.flatSections = true;               // no cards: the modules are marked by thin dividers on the one faceplate
        x.rimWidth = 1.0f;
        x.headerCol = Colour (0xff26272a);
        x.noValueArc = true;                 // the value reads off the pointer and the printed scale, as on a console
        return x;
    }();
    return t;
}

// round tp172c (slice 2): the 4K voicing's own look — the E-series console the role is measured on is a dark
// charcoal module (the light faceplate above is the newer console's): dark metal, light legends, the same colour
// code with the LF knob brown and the gate's green
inline void canvasRidge4k (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    brushedLines (g, a, 17, 0.035f);
    const auto mid = Rectangle<float> (a.getX() + a.getWidth() * 0.355f, a.getY(), a.getWidth() * 0.29f, a.getHeight());
    g.setGradientFill (ColourGradient (Colour (0xff1c1d20).withAlpha (0.7f), mid.getX(), 0.0f, Colour (0xff2a2c30).withAlpha (0.7f), mid.getCentreX(), 0.0f, true));
    g.fillRect (mid);
    for (const auto x : { mid.getX(), mid.getRight() })
    {
        g.setColour (Colours::black.withAlpha (0.5f));
        g.fillRect (Rectangle<float> (x - 1.0f, a.getY(), 1.5f, a.getHeight()));
        g.setColour (Colours::white.withAlpha (0.08f));
        g.fillRect (Rectangle<float> (x + 0.5f, a.getY(), 1.0f, a.getHeight()));
    }
    softLight (g, { a.getCentreX(), a.getY() - a.getHeight() * 0.15f }, a.getWidth() * 0.8f, Colour (0xfffff2dc), 0.06f);
    vignette (g, a, 0.25f);
}

inline Colour ridge4kCapTint (const char* param)
{
    const juce::String n (param);
    if (n.startsWith ("LF ")) return Colour (0xff6e4a3c);                         // brown
    if (n.startsWith ("Gate ")) return Colour (0xff2c5a36);                       // green
    if (n == "Mic gain") return Colour (0xffa8282c);                              // red
    return ridgeCapTint (param);
}

inline const Theme& ridge4kTheme()
{
    static const Theme t = []
    {
        Theme x = ridgeTheme();
        x.name = "Ridge Strip 4K";
        x.pal = { Colour (0xff3b3d40), Colour (0xff2c2e31), Colour (0xfff08a24), Colour (0xffffc46a),
                  Colour (0xf0434548), Colour (0xf036383b), Colour (0xff1d1e21), Colour (0xffeeeeea),
                  Colour (0xff111214), Colour (0xffd8d8d4), Colour (0xff8a8a86), Colour (0xff55585c), Colour (0xfffafafa),
                  Colour (0xff4a4c50) };
        x.canvas = canvasRidge4k;
        x.headerDeco = nullptr;
        x.flatSections = false;              // its own sheet-metal modules
        x.capTint = ridge4kCapTint;
        x.offLabelCol = Colour (0xffb4b8bf);
        x.captionCol = Colour (0xffd9dadc);
        x.valueCol = Colour (0xffb9bcc1);
        return x;
    }();
    return t;
}

// === Kiln Channel (console-emulation channel role, round tp172c7): a compact single-channel console — near-black ===
// side columns for the main level and meters, a dark gunmetal centre module with fine brushing and corner screws, an
// amber-lit VU, a kiln-mouth emblem (LYNX's own mark for the plugin) and amber key lamps

inline void kilnEmblem (juce::Graphics& g, Point<float> c, float r)
{
    // a kiln's arched mouth with a flame inside, in a rounded badge
    const auto badge = Rectangle<float> (r * 2.0f, r * 2.2f).withCentre (c);
    g.setColour (Colour (0xff0d0d0e));
    g.fillRoundedRectangle (badge, r * 0.35f);
    g.setColour (Colour (0xffb9bcc1).withAlpha (0.8f));
    g.drawRoundedRectangle (badge, r * 0.35f, juce::jmax (1.0f, r * 0.09f));
    juce::Path mouth;
    const auto m = badge.reduced (r * 0.42f, r * 0.45f);
    mouth.startNewSubPath (m.getX(), m.getBottom());
    mouth.lineTo (m.getX(), m.getY() + m.getHeight() * 0.45f);
    mouth.quadraticTo (m.getCentreX(), m.getY() - m.getHeight() * 0.15f, m.getRight(), m.getY() + m.getHeight() * 0.45f);
    mouth.lineTo (m.getRight(), m.getBottom());
    g.strokePath (mouth, juce::PathStrokeType (juce::jmax (1.0f, r * 0.12f)));
    juce::Path flame;
    const auto fx = m.getCentreX(), fb = m.getBottom() - r * 0.08f, fh = m.getHeight() * 0.62f, fw = m.getWidth() * 0.26f;
    flame.startNewSubPath (fx, fb - fh);
    flame.cubicTo (fx + fw * 1.4f, fb - fh * 0.45f, fx + fw, fb, fx, fb);
    flame.cubicTo (fx - fw, fb, fx - fw * 1.4f, fb - fh * 0.45f, fx, fb - fh);
    g.setGradientFill (ColourGradient (Colour (0xffffd27a), fx, fb, Colour (0xffd2461e), fx, fb - fh, false));
    g.fillPath (flame);
}

inline void canvasKiln (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    const auto sx = a.getWidth() / 460.0f, sy = a.getHeight() / 640.0f;
    const auto mod = Rectangle<float> (a.getX() + 104.0f * sx, a.getY() + 6.0f * sy, 252.0f * sx, 628.0f * sy);
    for (const auto col : { Rectangle<float> (a.getX() + 6.0f * sx, mod.getY(), 92.0f * sx, mod.getHeight()),
                            Rectangle<float> (a.getX() + 362.0f * sx, mod.getY(), 92.0f * sx, mod.getHeight()) })
    {
        g.setColour (Colour (0xff0b0b0c));
        g.fillRoundedRectangle (col, 6.0f);
        g.setColour (Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (col.reduced (0.5f), 6.0f, 1.0f);
    }
    // the centre module: dark gunmetal, vertical brushing, a soft top light, worn edges
    vfill (g, mod, Colour (0xff2b2c2f), Colour (0xff17181a));
    brushedLines (g, mod, 224, 0.045f);
    softLight (g, { mod.getCentreX(), mod.getY() + mod.getHeight() * 0.25f }, mod.getWidth() * 0.9f, Colour (0xfffff0d8), 0.07f);
    g.setColour (Colours::black.withAlpha (0.7f));
    g.drawRect (mod, 1.5f);
    g.setColour (Colours::white.withAlpha (0.07f));
    g.drawRect (mod.reduced (2.0f), 1.0f);
    cornerScrews (g, mod.reduced (2.0f), 9.0f * sx, 7.0f * sx);
    kilnEmblem (g, { mod.getCentreX(), mod.getY() + 17.0f * sy }, 11.0f * sx);
    // the VU's bezel
    const auto bezel = Rectangle<float> (a.getX() + 120.0f * sx, a.getY() + 36.0f * sy, 220.0f * sx, 124.0f * sy);
    g.setColour (Colour (0xff060606));
    g.fillRoundedRectangle (bezel, 6.0f);
    g.setColour (Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (bezel.reduced (1.0f), 6.0f, 1.0f);
    vignette (g, mod, 0.3f);
}

// === Fret Channel (guitar / bass channel strip role, round tp172c8): a mid-grey brushed panel between near-black input /
// output columns, darker recessed sections (phase correction, EQ, comp, drive), teal read-out screens, colour-coded caps
// (blue mid band, red high gain and drive, silver trims)
inline void canvasFret (juce::Graphics& g, Rectangle<float> a, bool display)
{
    vfill (g, a, Colour (0xff6a6c70), Colour (0xff505256));
    if (display) return;
    const auto sx = a.getWidth() / 976.0f, sy = a.getHeight() / 540.0f;
    brushedLines (g, a, 225, 0.03f);
    for (const auto col : { Rectangle<float> (a.getX(), a.getY(), 100.0f * sx, a.getHeight()),
                            Rectangle<float> (a.getRight() - 100.0f * sx, a.getY(), 100.0f * sx, a.getHeight()) })
    {
        vfill (g, col, Colour (0xff35363a), Colour (0xff26272a));
        g.setColour (Colour (0xff0f1012));
        g.fillRoundedRectangle (Rectangle<float> (col.getX() + 20.0f * sx, col.getY() + 50.0f * sy, 60.0f * sx, 310.0f * sy), 3.0f);
    }
    for (const auto r : { Rectangle<float> (118.0f, 160.0f, 168.0f, 190.0f), Rectangle<float> (300.0f, 160.0f, 170.0f, 190.0f),
                          Rectangle<float> (560.0f, 110.0f, 150.0f, 140.0f), Rectangle<float> (204.0f, 368.0f, 92.0f, 166.0f),
                          Rectangle<float> (556.0f, 368.0f, 232.0f, 166.0f) })
    {
        const auto rr = Rectangle<float> (a.getX() + r.getX() * sx, a.getY() + r.getY() * sy, r.getWidth() * sx, r.getHeight() * sy);
        g.setColour (Colour (0xff47494d));
        g.fillRoundedRectangle (rr, 4.0f);
    }
    g.setColour (Colour (0xff0d2622));
    g.fillRoundedRectangle (Rectangle<float> (a.getX() + 504.0f * sx, a.getY() + 270.0f * sy, 356.0f * sx, 84.0f * sy), 3.0f);
    vignette (g, a, 0.25f);
}

inline Colour fretCapTint (const char* param)
{
    const juce::String n (param);
    if (n == "Mid freq." || n == "Mid gain") return Colour (0xff2f6fb3);
    if (n == "High gain" || n == "Guitar drive" || n == "Bass drive") return Colour (0xffb3262b);
    if (n == "Low gain" || n == "Boost" || n == "Punch" || n == "Edge" || n == "Bass punch" || n == "Bass shape" || n == "Threshold" || n == "Delay"
        || n == "Input" || n == "Output")
        return Colour (0xff1c1d20);
    return Colour (0xffc9cbcf);
}

inline const Theme& fretTheme()
{
    static const Theme t = []
    {
        Theme x = ridgeTheme();
        x.name = "Fret Channel";
        x.pal = { Colour (0xff626468), Colour (0xff4c4e52), Colour (0xfff08a3a), Colour (0xffffb46a),
                  Colour (0x00000000), Colour (0x00000000), Colour (0x00000000), Colour (0xfff1f1ef),
                  Colour (0xff2a2b2e), Colour (0xff47494d), Colour (0xff3a3c40), Colour (0xff8a8c90), Colour (0xfff4f4f2),
                  Colour (0xff3a3c40) };
        x.canvas = canvasFret;
        x.headerDeco = nullptr;
        x.flatSections = true;
        x.rimWidth = 0.0f;
        x.capTint = fretCapTint;
        x.hwKeys = true;
        x.offLabelCol = Colour (0xffd0d2d6);
        x.captionCol = Colour (0xffeeeeec);
        x.valueCol = Colour (0xffd0d2d6);
        x.headerCol = Colour (0xff3a3b3f);
        return x;
    }();
    return t;
}

// === Shell Channel (drum channel strip role, round tp172c8): charcoal modules on a near-black frame, a lighter header
// band, green-capped gate / speed / comp knobs, a blue low and a red high frequency cap, silver amounts, and the five
// chevrons of the process order along the bottom
inline void canvasShell (juce::Graphics& g, Rectangle<float> a, bool display)
{
    vfill (g, a, Colour (0xff2c2d30), Colour (0xff1e1f21));
    if (display) return;
    const auto sx = a.getWidth() / 1000.0f, sy = a.getHeight() / 500.0f;
    for (const auto col : { Rectangle<float> (a.getX(), a.getY(), 110.0f * sx, a.getHeight()),
                            Rectangle<float> (a.getRight() - 110.0f * sx, a.getY(), 110.0f * sx, a.getHeight()) })
    {
        vfill (g, col, Colour (0xff232427), Colour (0xff18191b));
        g.setColour (Colour (0xff0a0b0c));
        g.fillRoundedRectangle (Rectangle<float> (col.getX() + 30.0f * sx, col.getY() + 24.0f * sy, 50.0f * sx, 270.0f * sy), 2.0f);
    }
    for (const auto r : { Rectangle<float> (112.0f, 20.0f, 484.0f, 212.0f), Rectangle<float> (598.0f, 20.0f, 290.0f, 212.0f),
                          Rectangle<float> (112.0f, 236.0f, 240.0f, 164.0f), Rectangle<float> (356.0f, 236.0f, 240.0f, 164.0f),
                          Rectangle<float> (600.0f, 236.0f, 288.0f, 164.0f) })
    {
        const auto rr = Rectangle<float> (a.getX() + r.getX() * sx, a.getY() + r.getY() * sy, r.getWidth() * sx, r.getHeight() * sy);
        vfill (g, rr, Colour (0xff3a3b3e), Colour (0xff2f3033));
        g.setColour (Colours::black.withAlpha (0.5f));
        g.drawRect (rr, 1.0f);
    }
    // the process order's chevrons
    for (int i = 0; i < 5; ++i)
    {
        const auto r = Rectangle<float> (a.getX() + (186.0f + 130.0f * i) * sx, a.getY() + 440.0f * sy, 124.0f * sx, 22.0f * sy);
        juce::Path p;
        p.startNewSubPath (r.getX(), r.getY());
        p.lineTo (r.getRight() - 10.0f * sx, r.getY());
        p.lineTo (r.getRight(), r.getCentreY());
        p.lineTo (r.getRight() - 10.0f * sx, r.getBottom());
        p.lineTo (r.getX(), r.getBottom());
        p.lineTo (r.getX() + 10.0f * sx, r.getCentreY());
        p.closeSubPath();
        g.setColour (Colour (0xff3d3e42));
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }
    vignette (g, a, 0.3f);
}

inline Colour shellCapTint (const char* param)
{
    const juce::String n (param);
    if (n == "Gate hold" || n == "Gate attack" || n == "Gate release" || n == "Shaper speed" || n == "LMC comp") return Colour (0xff1f7a4c);
    if (n == "Low freq.") return Colour (0xff2f6fb3);
    if (n == "High freq.") return Colour (0xffb3262b);
    return Colour (0xffc9cbcf);
}

inline const Theme& shellTheme()
{
    static const Theme t = []
    {
        Theme x = ridgeTheme();
        x.name = "Shell Channel";
        x.pal = { Colour (0xff2c2d30), Colour (0xff1e1f21), Colour (0xfff08a3a), Colour (0xffffb46a),
                  Colour (0x00000000), Colour (0x00000000), Colour (0x00000000), Colour (0xffe8e8e6),
                  Colour (0xff0a0b0c), Colour (0xff3a3b3e), Colour (0xff2f3033), Colour (0xff6a6c70), Colour (0xfff4f4f2),
                  Colour (0xff2f3033) };
        x.canvas = canvasShell;
        x.headerDeco = nullptr;
        x.flatSections = true;
        x.rimWidth = 0.0f;
        x.capTint = shellCapTint;
        x.hwKeys = true;
        x.offLabelCol = Colour (0xffb4b8bf);
        x.captionCol = Colour (0xffe2e2e0);
        x.valueCol = Colour (0xffb9bcc1);
        x.headerCol = Colour (0xff8c8d90);
        return x;
    }();
    return t;
}

// === Strike Shaper (the two-knob transient designer role, round trans189d): brushed silver modules round black
// capsules, silver knobs, lit square keys
inline void canvasStrike (juce::Graphics& g, Rectangle<float> a, bool display)
{
    vfill (g, a, Colour (0xff2a2c30), Colour (0xff141518));
    if (display) return;
    const auto sx = a.getWidth() / 540.0f, sy = a.getHeight() / 300.0f;
    const auto frame = Rectangle<float> (a.getX() + 6.0f * sx, a.getY() + 6.0f * sy, 528.0f * sx, 288.0f * sy);
    vfill (g, frame, Colour (0xffe9ebee), Colour (0xffa4a7ac));
    juce::Random rnd (228);
    for (int i = 0; i < 400; ++i) // vertical brushing
    {
        const auto x = frame.getX() + rnd.nextFloat() * frame.getWidth();
        const auto y = frame.getY() + rnd.nextFloat() * frame.getHeight();
        g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (0.04f));
        g.drawVerticalLine ((int) x, y, juce::jmin (frame.getBottom(), y + 10.0f + 30.0f * rnd.nextFloat()));
    }
    const auto cap = frame.reduced (16.0f * sx, 16.0f * sy);
    vfill (g, cap, Colour (0xff24262b), Colour (0xff0b0c0e));
    g.setColour (Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (cap, juce::jmin (cap.getHeight() * 0.5f, 60.0f * sy), 1.4f);
    vignette (g, a, 0.25f);
}

inline Colour strikeCapTint (const char*) { return Colour (0xffd6d9de); }

inline const Theme& strikeTheme()
{
    static const Theme t = []
    {
        Theme x = ridgeTheme();
        x.name = "Strike Shaper";
        x.pal = { Colour (0xff24262b), Colour (0xff0b0c0e), Colour (0xffff8a2a), Colour (0xffffb46a),
                  Colour (0x00000000), Colour (0x00000000), Colour (0x00000000), Colour (0xffe9eaec),
                  Colour (0xff0a0b0c), Colour (0xff3a3c41), Colour (0xff1a1b1e), Colour (0xff9a9ea6), Colour (0xfff4f4f2),
                  Colour (0xff2a2c30) };
        x.canvas = canvasStrike;
        x.headerDeco = nullptr;
        x.flatSections = true;
        x.rimWidth = 0.0f;
        x.capTint = strikeCapTint;
        x.hwKeys = true;
        x.offLabelCol = Colour (0xffb4b8bf);
        x.captionCol = Colour (0xffe9eaec);
        x.valueCol = Colour (0xffb9bcc1);
        x.headerCol = Colour (0xff9a9ea6);
        return x;
    }();
    return t;
}

// === Anvil Bus (round 190fk, LYNX's drum-bus processor): forged gunmetal hammered by hand, a forge's ember glow
// along the bottom edge, steel knobs with ember pointers; a small anvil beside each section title. LYNX's own drawing.
inline void canvasAnvil (juce::Graphics& g, Rectangle<float> a, bool display)
{
    valley (g, a, Colour (0xff2b2d32), Colour (0xff17181b), Colour (0xff1d1e22), 0.62f);
    if (display) return;
    if (! reduced)
    {
        // hammer dimples: soft light-over-dark ovals, deterministic
        juce::Random rnd (190);
        const auto count = (int) (a.getWidth() * a.getHeight() / 900.0f);
        for (int i = 0; i < count; ++i)
        {
            const auto r = 3.0f + rnd.nextFloat() * 9.0f;
            const auto c = Point<float> (a.getX() + rnd.nextFloat() * a.getWidth(), a.getY() + rnd.nextFloat() * a.getHeight());
            const auto e = Rectangle<float> (r * 2.0f, r * (1.4f + rnd.nextFloat() * 0.5f)).withCentre (c);
            g.setColour (Colours::black.withAlpha (0.10f + 0.08f * rnd.nextFloat()));
            g.fillEllipse (e.translated (0.0f, r * 0.18f));
            g.setColour (Colours::white.withAlpha (0.025f + 0.025f * rnd.nextFloat()));
            g.fillEllipse (e.reduced (r * 0.25f).translated (-r * 0.12f, -r * 0.15f));
        }
        brushedLines (g, a, 1909, 0.035f);
    }
    // the forge's glow rising from the bottom edge
    const auto glow = a.withTop (a.getBottom() - a.getHeight() * 0.22f);
    g.setGradientFill (ColourGradient (Colour (0xffff5a2e).withAlpha (0.0f), 0.0f, glow.getY(), Colour (0xffff5a2e).withAlpha (0.20f), 0.0f, glow.getBottom(), false));
    g.fillRect (glow);
    softLight (g, Point<float> (a.getCentreX(), a.getBottom() + a.getHeight() * 0.05f), a.getWidth() * 0.45f, Colour (0xffffa84a), 0.10f);
    vignette (g, a, 0.32f);
}

// a small anvil (horn to the left, flat face, waist, foot)
inline void anvilEmblem (juce::Graphics& g, Point<float> c, float r)
{
    const auto w = r * 2.6f, h = r * 1.7f;
    const auto x0 = c.x - w * 0.5f, y0 = c.y - h * 0.5f;
    juce::Path p;
    p.startNewSubPath (x0, y0 + h * 0.10f);                   // the horn's tip
    p.quadraticTo (x0 + w * 0.22f, y0 + h * 0.02f, x0 + w * 0.34f, y0);
    p.lineTo (x0 + w, y0);                                    // the face
    p.lineTo (x0 + w, y0 + h * 0.32f);
    p.lineTo (x0 + w * 0.80f, y0 + h * 0.38f);
    p.lineTo (x0 + w * 0.70f, y0 + h * 0.66f);                // the waist
    p.lineTo (x0 + w * 0.90f, y0 + h);                        // the foot
    p.lineTo (x0 + w * 0.24f, y0 + h);
    p.lineTo (x0 + w * 0.44f, y0 + h * 0.66f);
    p.lineTo (x0 + w * 0.36f, y0 + h * 0.38f);
    p.quadraticTo (x0 + w * 0.12f, y0 + h * 0.30f, x0, y0 + h * 0.10f);
    p.closeSubPath();
    g.setColour (Colour (0xffc9c6bf));
    g.fillPath (p);
    g.setColour (Colour (0xffff7a3a).withAlpha (0.9f));
    g.fillRect (Rectangle<float> (x0 + w * 0.36f, y0, w * 0.64f, juce::jmax (1.0f, h * 0.10f)));
}

inline Colour anvilCapTint (const char* param)
{
    const juce::String n (param);
    if (n == "Drive" || n == "Transients" || n == "Boom") return Colour (0xff3b3e44);
    return Colour (0xff2c2e33);
}

inline const Theme& anvilTheme()
{
    static const Theme t = []
    {
        Theme x = ridgeTheme();
        x.name = "Anvil Bus";
        x.pal = { Colour (0xff2b2d32), Colour (0xff17181b), Colour (0xffff5a2e), Colour (0xffffa84a),
                  Colour (0xcc2a2c31), Colour (0xcc1e1f23), Colour (0xff0b0c0e), Colour (0xffd9d4ca),
                  Colour (0xff09090a), Colour (0xff50535a), Colour (0xff1b1c1f), Colour (0xff121315), Colour (0xffff7a3a),
                  Colour (0xff26282c) };
        x.canvas = canvasAnvil;
        x.headerDeco = nullptr;
        x.titleEmblem = anvilEmblem;
        x.flatSections = true;
        x.rimWidth = 1.2f;
        x.capTint = anvilCapTint;
        x.hwKeys = true;
        x.offLabelCol = Colour (0xffa9a49b);
        x.captionCol = Colour (0xffe6e1d8);
        x.valueCol = Colour (0xffbdb7ad);
        x.headerCol = Colour (0xff141518);
        return x;
    }();
    return t;
}

inline Colour kilnCapTint (const char* param)
{
    const juce::String n (param);
    if (n == "Console") return Colour (0xff141416);
    return Colour (0xff1d1e21);
}

inline const Theme& kilnTheme()
{
    static const Theme t = []
    {
        Theme x = ridgeTheme();
        x.name = "Kiln Channel";
        x.pal = { Colour (0xff141415), Colour (0xff0a0a0b), Colour (0xfff2a33a), Colour (0xffffc46a),
                  Colour (0x00000000), Colour (0x00000000), Colour (0x00000000), Colour (0xffe8e6e0),
                  Colour (0xff070707), Colour (0xff2e2f33), Colour (0xff111214), Colour (0xff55585c), Colour (0xfff4f4f2),
                  Colour (0xff2a2b2e) };
        x.canvas = canvasKiln;
        x.headerDeco = nullptr;
        x.flatSections = true;
        x.rimWidth = 0.0f;
        x.capTint = kilnCapTint;
        x.hwKeys = true;                     // Noise reduction / Group bypass: square keys, lit amber
        x.offLabelCol = Colour (0xffb4b8bf);
        x.captionCol = Colour (0xffd9dadc);
        x.valueCol = Colour (0xffb9bcc1);
        x.headerCol = Colour (0xff0e0e0f);
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
        g.setColour ((k == 0 ? p.accent2 : p.accent).withAlpha (0.03f)); // (tp172b4: fainter, the field reads plain)
        g.strokePath (curve, juce::PathStrokeType (1.4f));
    }
    softLight (g, { a.getX() + a.getWidth() * 0.25f, a.getY() }, a.getWidth() * 0.5f, p.accent, 0.04f);
    // round tp172b4: no dimming — the reference's display is its graphite field itself (sampled #242225 … #323134)
    juce::ignoreUnused (display);
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
        // round tp172b4: the reference's warm graphite field, dark caps and a thin yellow arc (sampled: arc #f8c838,
        // caps #29252a, field #262429)
        x.pal = { Colour (0xff2b292e), Colour (0xff1c1a1e), Colour (0xfff8c838), Colour (0xff7fe3ff), Colour (0x9c2a282d), Colour (0xb81c1a1e),
                  Colour (0xff4e4a50), Colour (0xffd6d0d6), Colour (0xff1f1d21), Colour (0xff353136), Colour (0xff1f1c20), Colour (0xff151316),
                  Colour (0xfff8c838), Colour (0xff2e2b30) };
        x.canvas = canvasCrest;
        x.titleEmblem = crestEmblem;
        x.cap = Cap::flat;
        x.pointer = Pointer::glowLine;
        x.arcGlow = 0.06f;
        x.thin = true; // round tp172b: the reference's look
        x.arcInCap = true; // round tp172b4: caps as big as the reference's, the arc inside the rim
        return x;
    }();
    return t;
}

// === Apex Limiter (Pro-L role): the ceiling — a line of light the peaks press against ===============================

inline void canvasApex (juce::Graphics& g, Rectangle<float> a, bool display)
{
    // round tp172b4: the reference's plum-graphite field, plain (sampled: #262228 under the header, #19171a in the
    // valley, #2a262c over the bottom bar); our ceiling motif stays as a faint silhouette
    const auto& p = pal();
    valley (g, a, p.bgTop, Colour (0xff19171a), p.bgBottom, 0.3f);
    const auto ceil = a.getY() + a.getHeight() * 0.3f;
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
    g.setColour (p.accent2.withAlpha (display ? 0.018f : 0.03f));
    g.fillPath (wave);
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
        // round tp172b4: the reference's field (sampled #262228 / #19171a / #2a262c), its dim red ceiling (#a93d49) as
        // the accent's darker side, graphite caps
        x.pal = { Colour (0xff272329), Colour (0xff2a262c), Colour (0xffd2404e), Colour (0xffd8d8de), Colour (0xe0262228), Colour (0xf01c1a1e),
                  Colour (0xff4a4250), Colour (0xffe6e0e8), Colour (0xff1d1a1e), Colour (0xff3a363c), Colour (0xff1f1c21), Colour (0xff141215),
                  Colour (0xfff2f0f2), Colour (0xff2b272d) };
        x.canvas = canvasApex;
        x.titleEmblem = apexEmblem;
        x.arcGlow = 0.08f;
        x.thin = true; // round tp172b: the reference's look
        return x;
    }();
    return t;
}

// === Prism Multiband (Pro-MB role): a glass prism splits a white beam into four bands ==============================

inline void canvasPrism (juce::Graphics& g, Rectangle<float> a, bool display)
{
    // round tp172b4: the reference's neutral graphite field (sampled #3c3c3c under the header, #10100f in the valley,
    // #2a2a2a over the bottom bar); our prism and its fan stay as a faint watermark
    const auto& p = pal();
    valley (g, a, p.bgTop, Colour (0xff111110), p.bgBottom, 0.68f);
    const auto pc = Point<float> (a.getX() + a.getWidth() * 0.28f, a.getY() + a.getHeight() * 0.42f);
    const auto pr = juce::jmin (a.getWidth(), a.getHeight()) * 0.16f;
    const auto k = display ? 0.12f : 0.2f;
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
        g.setGradientFill (ColourGradient (bands[i].withAlpha (0.30f * k), pc.x, 0.0f, bands[i].withAlpha (0.0f), a.getRight(), 0.0f, false));
        g.fillPath (fan);
    }
    juce::Path tri;
    tri.addTriangle (pc.x, pc.y - pr, pc.x - pr * 0.9f, pc.y + pr * 0.6f, pc.x + pr * 0.9f, pc.y + pr * 0.6f);
    g.setColour (Colours::white.withAlpha (0.25f * k));
    g.strokePath (tri, juce::PathStrokeType (1.0f));
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
        // round tp172b4: the reference's neutral graphite (sampled #3c3c3c / #10100f / #2a2a2a), its orange 0 dB line
        // (#f3af44) as the accent
        x.pal = { Colour (0xff3d3d3d), Colour (0xff2a2a2a), Colour (0xfff3af44), Colour (0xff8fd0ff), Colour (0xe02e2e2e), Colour (0xf01b1b1b),
                  Colour (0xff5a5a5a), Colour (0xffe8e8e8), Colour (0xff121212), Colour (0xff3a3a3a), Colour (0xff1e1e1e), Colour (0xff161616),
                  Colour (0xfff3af44), Colour (0xff2a2a2a) };
        x.canvas = canvasPrism;
        x.titleEmblem = prismEmblem;
        x.twoTone = false;
        x.cap = Cap::flat;
        x.thin = true; // round tp172b: the reference's look
        x.arcGlow = 0.08f; // round tp172b: the reference's look
        return x;
    }();
    return t;
}

// === Sentry Gate (Pro-G role): a gatehouse at night — iron bars, a lantern, cold stone =============================

inline void canvasSentry (juce::Graphics& g, Rectangle<float> a, bool display)
{
    // round tp172b4: the reference's slate-blue field, plain (sampled #2f2d42 under the header to #1c1f2f over the
    // bottom bar); our gate stays as a faint row of bars behind the display
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    const auto gate = Rectangle<float> (a.getX() + a.getWidth() * 0.35f, a.getY() + a.getHeight() * 0.08f, a.getWidth() * 0.6f, a.getHeight() * 0.92f);
    g.setColour (Colour (0xff05080a).withAlpha (display ? 0.03f : 0.05f));
    for (float x = gate.getX(); x < gate.getRight(); x += 22.0f)
    {
        g.fillRect (Rectangle<float> (x, gate.getY() + 10.0f, 3.0f, gate.getHeight() - 10.0f));
        juce::Path tip;
        tip.addTriangle (x - 3.0f, gate.getY() + 11.0f, x + 6.0f, gate.getY() + 11.0f, x + 1.5f, gate.getY());
        g.fillPath (tip);
    }
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
        x.base = Style::glow;
        // round tp172b4: the reference's slate field (#2f2d42 → #1c1f2f), blue-grey grip caps (#7b839f) with a light
        // thumb, dark ticked rings (#34344a)
        x.pal = { Colour (0xff2f2d42), Colour (0xff1c1f2f), Colour (0xff9fb0e0), Colour (0xffe8ecf6), Colour (0xe0363650), Colour (0xf0222336),
                  Colour (0xff5d6380), Colour (0xffd8dbe6), Colour (0xff1e2030), Colour (0xff8e96b2), Colour (0xff5f667e), Colour (0xff2e2e42),
                  Colour (0xffe6e6ec), Colour (0xff2b2a3e) };
        x.canvas = canvasSentry;
        x.titleEmblem = sentryEmblem;
        x.pointer = Pointer::line;
        x.arcGlow = 0.0f;
        x.cap = Cap::porcelain;
        x.gripKnob = true;
        return x;
    }();
    return t;
}

// === Glow Leveler (opto-leveler role): round tp172d — the reference's silver faceplate: light brushed aluminium,
// black knobs on printed scales, the cream VU in a dark bezel, a dark strip along the bottom for the trims ==========

inline void canvasGlowLeveler (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    brushedLines (g, a, 41, 0.05f);
    if (! reduced) bloom (g, { a.getX() + a.getWidth() * 0.3f, a.getY() }, a.getWidth() * 0.7f, Colours::white, 0.10f); // the top light
    for (const auto& c : { a.getTopLeft().translated (11.0f, 11.0f), a.getTopRight().translated (-11.0f, 11.0f),
                           a.getBottomLeft().translated (11.0f, -11.0f), a.getBottomRight().translated (-11.0f, -11.0f) })
        styles::screw (g, c, 6.0f);
    g.setColour (Colours::black.withAlpha (0.22f)); // the faceplate's edge
    g.drawRect (a.reduced (1.0f), 1.0f);
}

inline void glowLevelerEmblem (juce::Graphics& g, Point<float> c, float r) // the cell, a small red lamp on the plate
{
    g.setColour (Colour (0xff5a0d0a));
    g.fillEllipse (Rectangle<float> (r * 1.3f, r * 1.3f).withCentre (c));
    g.setColour (pal().accent);
    g.fillEllipse (Rectangle<float> (r * 0.9f, r * 0.9f).withCentre (c));
    g.setColour (Colours::white.withAlpha (0.7f));
    g.fillEllipse (Rectangle<float> (r * 0.3f, r * 0.3f).withCentre (c.translated (-r * 0.15f, -r * 0.15f)));
}

// round tp172d (slice 8): the opto leveler's knob — a black knurled skirt round a polished chrome dome
inline void glowLevelerCap (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    g.setGradientFill (ColourGradient (Colour (0xff3a3b3e).brighter (hover * 0.1f), c.x, cap.getY(), Colour (0xff0b0b0c), c.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    if (fx) // the knurling round the skirt: lit ridges, so the black skirt reads on the charcoal strip too
    {
        const int n = capR > 30.0f ? 48 : 28;
        for (int i = 0; i < n; ++i)
        {
            const auto a = juce::MathConstants<float>::twoPi * (float) i / (float) n;
            const Point<float> d { std::sin (a), -std::cos (a) };
            g.setColour (Colours::white.withAlpha (0.08f + 0.14f * juce::jmax (0.0f, -d.y))); // brighter on the top half
            g.drawLine ({ c + d * capR * 0.8f, c + d * capR * 0.98f }, 1.0f);
        }
    }
    g.setColour (Colour (0xff76797e)); // the skirt's turned edge
    g.drawEllipse (cap.reduced (0.6f), 1.2f);
    // the chrome dome: a dark rim, then rings of light and shade (a spun metal cap) — half the skirt, as on the original
    const auto dome = capR * 0.5f;
    g.setColour (Colour (0xff17181a));
    g.fillEllipse (Rectangle<float> (dome * 2.16f, dome * 2.16f).withCentre (c));
    g.setGradientFill (ColourGradient (Colour (0xfff4f5f6), c.x - dome * 0.5f, c.y - dome * 0.7f, Colour (0xff6c7075), c.x + dome * 0.6f, c.y + dome * 0.9f, false));
    g.fillEllipse (Rectangle<float> (dome * 2.0f, dome * 2.0f).withCentre (c));
    if (fx)
    {
        g.setGradientFill (ColourGradient (Colour (0xffa9adb2), c.x - dome * 0.5f, c.y - dome * 0.5f, Colour (0xffeceef0), c.x + dome * 0.5f, c.y + dome * 0.5f, false));
        g.fillEllipse (Rectangle<float> (dome * 1.3f, dome * 1.3f).withCentre (c));
        g.setColour (Colours::white.withAlpha (0.55f));
        g.fillEllipse (Rectangle<float> (dome * 0.7f, dome * 0.4f).withCentre (c.translated (-dome * 0.32f, -dome * 0.5f)));
    }
}

inline Colour glowLevelerCapTint (const char* param)
{
    return param != nullptr && juce::String (param) == "Emphasis" ? Colour (0xffe9dfc4) : Colour();
}

inline const Theme& glowLevelerTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Glow Leveler";
        x.base = Style::hardware;
        x.pal = { Colour (0xffd0d3d5), Colour (0xffa9adb1), Colour (0xffc8231b), Colour (0xffe8463c), Colour (0x00000000), Colour (0x00000000),
                  Colour (0xff6e7277), Colour (0xff1c1d1f), Colour (0xff1a1c1e), Colour (0xff3c3d3f), Colour (0xff050505), Colour (0xff55595d),
                  Colour (0xfff4f4f4), Colour (0xff45484c) };
        x.canvas = canvasGlowLeveler;
        x.titleEmblem = glowLevelerEmblem;
        x.drawCap = glowLevelerCap;        // round tp172d (slice 8): chrome domes in knurled black skirts
        x.pointerFrom = 0.56f;             // ... the white line on the skirt
        x.capTint = glowLevelerCapTint;    // ... HI FREQ is a cream chicken-head pointer knob
        x.pointerKnobTint = Colour (0xffe9dfc4);
        x.ticks = 11;
        x.boldTicks = false;
        x.buttTrack = true;
        x.arcGlow = 0.0f;
        x.knobScale = 1.04f;
        x.thumbPointer = true;
        x.noValueArc = true;               // the value reads off the pointer and the printed scale
        x.numerals = true;                 // round tp172d (look slice): the reference's printed 0 … 100
        x.numeralStep = 10;
        x.flatSections = true;             // one faceplate: no cards
        x.rimWidth = 0.0f;
        x.captionCol = Colour (0xff1c1d1f);
        x.valueCol = Colour (0xff2e3033);
        x.offLabelCol = Colour (0xff2e3033);
        x.hwKeys = true;
        x.headerCol = Colour (0xff2b2d30);
        x.sectionFills[1] = Colour (0xff2d2e30); // the charcoal lower strip (Analog, HF emphasis, Mix, Trim)
        x.goldKeys = true;                 // round tp172d (slice 8): its gold 50Hz / 60Hz / OFF keys
        x.numeralLit = true;               // ... the number at the pointer printed red
        x.vuBezelDark = true;              // ... the meter in a gunmetal frame
        return x;
    }();
    return t;
}

// === Hush De-esser (de-esser role): hush — sibilance hairlines thinning into the dark, an S ribbon in the mist =====

inline void canvasHush (juce::Graphics& g, Rectangle<float> a, bool display)
{
    // round tp172b4: the reference's charcoal field (sampled #3b3a3a under the header, #141414 at the display's foot,
    // #2f2f2f under the knobs); our S ribbon stays as a faint watermark
    const auto& p = pal();
    valley (g, a, p.bgTop, Colour (0xff151515), p.bgBottom, 0.45f);
    juce::Path s;
    const auto c = Point<float> (a.getX() + a.getWidth() * 0.22f, a.getCentreY());
    const auto r = a.getHeight() * 0.28f;
    s.startNewSubPath (c.x + r * 0.8f, c.y - r * 1.1f);
    s.cubicTo (c.x - r * 0.4f, c.y - r * 1.6f, c.x - r * 1.4f, c.y - r * 0.2f, c.x, c.y);
    s.cubicTo (c.x + r * 1.4f, c.y + r * 0.2f, c.x + r * 0.4f, c.y + r * 1.6f, c.x - r * 0.8f, c.y + r * 1.1f);
    g.setColour (p.accent.withAlpha (display ? 0.025f : 0.04f));
    g.strokePath (s, stroke (r * 0.12f));
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
        // round tp172b4: the reference's charcoal field (#3b3a3a / #141414 / #2f2f2f), big amber grip caps (#e2aa12)
        // with a white thumb on dark ticked rings (#262425); the small knobs grey (their own cap tint)
        x.pal = { Colour (0xff3b3a3a), Colour (0xff2f2f2f), Colour (0xffe2aa12), Colour (0xff9fd36a), Colour (0xe0333232), Colour (0xf0222222),
                  Colour (0xff5a5856), Colour (0xffdedcd8), Colour (0xff1c1c1c), Colour (0xfff2bc1c), Colour (0xffc68c06), Colour (0xff232122),
                  Colour (0xffe9e9e9), Colour (0xff302f2f) };
        x.canvas = canvasHush;
        x.titleEmblem = hushEmblem;
        x.twoTone = false;
        x.cap = Cap::orb;
        x.arcGlow = 0.0f;
        x.gripKnob = true;
        return x;
    }();
    return t;
}

// === Epoch Verb (ValhallaVintageVerb role): the original's 1970s-80s flat graphic — oxblood ground, orange outlines, orange disc knobs ===

inline const Theme& epochTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Epoch Verb";
        x.base = Style::glow;
        x.pal = { Colour (0xff3c0c08), Colour (0xff2c0705), Colour (0xffffb02e), Colour (0xffff6a1f), Colour (0xff3c0c08), Colour (0xff3c0c08),
                  Colour (0xffff9a1f), Colour (0xffffffff), Colour (0xff210503), Colour (0xffff6a1f), Colour (0xffff6a1f), Colour (0xff5a1a10),
                  Colour (0xff120302), Colour (0xff5a1a10) };
        x.canvas = canvasFlat;
        x.flatSections = true;
        x.discKnob = true;
        x.pointerFrom = 0.0f;
        x.arcGlow = 0.0f;
        x.kerning = 0.02f;
        x.captionScale = 1.3f;
        x.captionCol = Colour (0xffffa94a);
        x.valueCol = Colours::white;
        x.rimWidth = 2.5f;
        return x;
    }();
    return t;
}

// === Atrium (ValhallaRoom role): the original's "Electric Blue" mode — graphite fields, cyan bars and cyan disc knobs ===

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
        x.pal = { Colour (0xff4b4c4f), Colour (0xff444548), Colour (0xff72f0f8), Colour (0xff3fc8d4), Colour (0xff38393c), Colour (0xff38393c),
                  Colour (0xff38393c), Colour (0xffffffff), Colour (0xff2a2b2d), Colour (0xff72f0f8), Colour (0xff72f0f8), Colour (0xff2b2c2f),
                  Colour (0xff1c1d1f), Colour (0xff2f3033) };
        x.canvas = canvasFlat;
        x.flatSections = true;
        x.discKnob = true;
        x.pointerFrom = 0.0f;
        x.arcGlow = 0.0f;
        x.kerning = 0.02f;
        x.captionScale = 1.3f;
        x.captionCol = Colours::white;
        x.valueCol = Colours::white;
        x.rimWidth = 0.0f;
        x.barSlider = true;
        x.discRing = false;
        return x;
    }();
    return t;
}

// === Foil Plate (ValhallaPlate role): the original's graphic look — black ground, steel-blue fields, black disc knobs ===

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
        x.base = Style::glow;
        x.pal = { Colour (0xff050506), Colour (0xff000000), Colour (0xffa9cdfa), Colour (0xff5d95d8), Colour (0xff21599a), Colour (0xff21599a),
                  Colour (0xff21599a), Colour (0xffffffff), Colour (0xff0d2747), Colour (0xff040405), Colour (0xff040405), Colour (0xff163d6a),
                  Colour (0xffffffff), Colour (0xff1a4a82) };
        x.canvas = canvasFlat;
        x.flatSections = true;
        x.discKnob = true;
        x.pointerFrom = 0.0f;
        x.arcGlow = 0.0f;
        x.kerning = 0.02f;
        x.captionScale = 1.3f;
        x.captionCol = Colours::white;
        x.valueCol = Colour (0xffbdd8fb);
        x.rimWidth = 0.0f;
        return x;
    }();
    return t;
}

// === Halo (ValhallaShimmer role): the original's light minimal look — pale grey ground, graphite disc knobs, dark type ===

// round tp172d (look slice): the original's row of flat discs runs from black to a light grey in signal order
inline Colour haloCapTint (const char* param)
{
    static const char* const order[] = { "Mix", "Shift", "Feedback", "Diffusion", "Size", "Low cut", "High cut", "Mod rate", "Mod depth" };
    if (param != nullptr)
        for (int i = 0; i < 9; ++i)
            if (std::strcmp (param, order[i]) == 0)
                return Colour::greyLevel (0.02f + 0.48f * (float) i / 8.0f);
    return {};
}

inline const Theme& haloTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Halo";
        x.base = Style::glow;
        x.pal = { Colour (0xffeeeef0), Colour (0xffb4b4b9), Colour (0xff4a4a50), Colour (0xff8a8a90), Colour (0x00ffffff), Colour (0x00ffffff),
                  Colour (0x00ffffff), Colour (0xff2e2e33), Colour (0xffd4d4d8), Colour (0xff26262a), Colour (0xff26262a), Colour (0xffc4c4c8),
                  Colour (0xffeeeeee), Colour (0xff55555b) };
        x.canvas = canvasFlat;
        x.flatSections = true;
        x.discKnob = true;
        x.pointerFrom = 0.0f;
        x.arcGlow = 0.0f;
        x.kerning = 0.02f;
        x.captionScale = 1.3f;
        x.captionCol = Colour (0xff2e2e33);
        x.valueCol = Colour (0xff2e2e33);
        x.rimWidth = 0.0f;
        x.headerCol = Colour (0xff3a3a3f);
        x.pal.accent2 = Colour (0xff3a3a40);
        // round tp172d (look slice): big discs shading across the row, no value ring, a short light tick at the edge,
        // lower-case captions and modes
        x.capTint = haloCapTint;
        x.discRing = false;
        x.pointerFrom = 0.62f;
        x.pal.pointer = Colour (0xffc4c4c8);
        x.pal.capBottom = x.pal.capTop;
        x.lowerCaptions = true;
        x.captionScale = 1.7f; // the original's legends: captions and values about a quarter of the knob's radius tall
        return x;
    }();
    return t;
}

// === Horizon (ValhallaFutureVerb role): the original's look — black ground, mint outlines, outlined black knobs ===

inline const Theme& horizonTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Horizon";
        x.base = Style::glow;
        x.pal = { Colour (0xff060709), Colour (0xff030405), Colour (0xff55f2cc), Colour (0xff38c9e8), Colour (0xff060709), Colour (0xff060709),
                  Colour (0xff55f2cc), Colour (0xffe9fff9), Colour (0xff0b1513), Colour (0xff060709), Colour (0xff55f2cc), Colour (0xff163a33),
                  Colour (0xff55f2cc), Colour (0xff10241f) };
        x.canvas = canvasFlat;
        x.flatSections = true;
        x.discKnob = true;
        x.pointerFrom = 0.0f;
        x.arcGlow = 0.0f;
        x.kerning = 0.02f;
        x.captionScale = 1.3f;
        x.captionCol = Colours::white;
        x.valueCol = Colour (0xffc4fbec);
        x.rimWidth = 2.5f;
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

// === LuxeVerb, Space page (round tp172b4, the Pro-R role): a night-navy strip of knobs across the top whose lower
// edge swells under the big SPACE dial, lit from below by a thin blue line; the field under it near black ============

inline void canvasLuxeSpace (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, Colour (0xff040305), p.bgBottom); // the field (sampled #030203 at the top of the display, #121013 low)
    if (display) return;
    // the strip: its edge 150 px into the 456 px page (under a 26 px header), swelling to 180 under the centre
    const auto edgeY = [&] (float u) {
        const auto d = (u - 0.497f) / 0.2f;
        return a.getY() + a.getHeight() * (176.0f + 30.0f * std::exp (-d * d * 2.2f)) / 482.0f;
    };
    juce::Path strip, edge;
    strip.startNewSubPath (a.getX(), a.getY());
    strip.lineTo (a.getRight(), a.getY());
    for (int i = 64; i >= 0; --i)
    {
        const auto u = (float) i / 64.0f;
        const juce::Point<float> pt { a.getX() + a.getWidth() * u, edgeY (u) };
        strip.lineTo (pt);
    }
    strip.closeSubPath();
    for (int i = 0; i <= 64; ++i)
    {
        const auto u = (float) i / 64.0f;
        const juce::Point<float> pt { a.getX() + a.getWidth() * u, edgeY (u) };
        if (i == 0) edge.startNewSubPath (pt); else edge.lineTo (pt);
    }
    const auto bottom = edgeY (0.497f);
    ColourGradient sg (p.bgTop, 0.0f, a.getY(), Colour (0xff0d1622), 0.0f, bottom, false);
    sg.addColour (0.55, p.bgTop);
    g.setGradientFill (sg);
    g.fillPath (strip);
    if (! reduced)
    {
        g.setColour (p.accent2.withAlpha (0.10f)); // the glow along the edge
        g.strokePath (edge, juce::PathStrokeType (6.0f));
    }
    g.setColour (p.accent2.withAlpha (0.55f));
    g.strokePath (edge, juce::PathStrokeType (1.0f));
}

inline void capNavy (juce::Graphics& g, Rectangle<float> cap, Point<float> c, float capR, float hover, bool fx)
{
    const auto& p = pal();
    g.setGradientFill (ColourGradient (p.capTop.brighter (hover * 0.15f), c.x, cap.getY(), p.capBottom, c.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    if (fx)
    {
        g.setColour (p.accent2.withAlpha (0.18f)); // a cold glint along the rim
        g.drawEllipse (cap.reduced (0.5f), 1.0f);
    }
}

inline const Theme& luxeSpaceTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "LuxeVerb Space";
        x.base = Style::glow;
        // sampled from the reference: strip #130f16, caps #171d29, rings #165a85 (the blue glow round them), arcs #dfad34,
        // the decay curve's blue #2d7ca7
        x.pal = { Colour (0xff130f16), Colour (0xff121013), Colour (0xffdfad34), Colour (0xff2d8cc0), Colour (0xe0141a26), Colour (0xf00c1018),
                  Colour (0xff1d4a6e), Colour (0xffd8dce4), Colour (0xff070608), Colour (0xff1b2130), Colour (0xff141924), Colour (0xff165a85),
                  Colour (0xffe8e4d8), Colour (0xff161b26) };
        x.canvas = canvasLuxeSpace;
        x.titleEmblem = luxeEmblem;
        x.drawCap = capNavy;
        x.thin = true;
        x.arcGlow = 0.0f;
        x.pointerFrom = 0.55f;
        x.captionCol = Colour (0xffd2d6de);
        x.valueCol = Colour (0xff8e97a8);
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

// === Echoform (EchoBoy role, round tp172d): the original's hardware look — see soundtoysTheme =====================

inline const Theme& echoformTheme()
{
    // round tp172d: EchoBoy's hardware look (its role) — black face, walnut cheeks, bold white rounded outlines, red LEDs
    static const Theme t = []
    {
        auto x = soundtoysTheme ("Echoform", Colour (0xffe8402c), Colour (0xffeeeeea), Colour (0xff0b0b0c), Colour (0xfff2f2ef), 3.0f); // (slice 8 review: thicker, whiter pod outlines)
        x.cornerScale = 4.0f; // the original's three rounded pods
        x.bow = 0.05f;        // round tp172d (look slice): with bowed sides
        x.hwKeys = true;
        x.cheek = 30.0f; // (the layout leaves room for them)
        x.ledReadouts = true; // round tp172d (look slice): the time, tap tempo and style on red LED read-outs
        return x;
    }();
    return t;
}

// === Spiral Echo (ValhallaFreqEcho role): the original's look — a periwinkle field, black disc knobs, white type ===

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

inline const Theme& spiralTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Spiral Echo";
        x.base = Style::glow;
        x.pal = { Colour (0xff4b63b9), Colour (0xff4359ab), Colour (0xffffffff), Colour (0xffd8e0ff), Colour (0x00000000), Colour (0x00000000),
                  Colour (0x00000000), Colour (0xffffffff), Colour (0xff34478a), Colour (0xff050508), Colour (0xff050508), Colour (0xff3a4f99),
                  Colour (0xffffffff), Colour (0xff3a4f99) };
        x.canvas = canvasFlat;
        x.flatSections = true;
        x.discKnob = true;
        x.pointerFrom = 0.0f;
        x.arcGlow = 0.0f;
        x.kerning = 0.02f;
        x.captionScale = 1.3f;
        x.captionCol = Colours::white;
        x.valueCol = Colours::white;
        x.rimWidth = 0.0f;
        x.discRing = false;
        x.headerCol = Colour (0xff34478a);
        x.pal.accent = Colour (0xff15161c);
        return x;
    }();
    return t;
}

// === Orbit (ValhallaSpaceModulator role): the original's look — a dark maroon field, bright coloured disc knobs, big white type ===

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

inline const Theme& orbitTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Orbit";
        x.base = Style::glow;
        x.pal = { Colour (0xff2c0a0b), Colour (0xff1d0405), Colour (0xffff2f97), Colour (0xff2f86ff), Colour (0x00000000), Colour (0x00000000),
                  Colour (0x00000000), Colour (0xffffffff), Colour (0xff3d1213), Colour (0xffff2f97), Colour (0xffff2f97), Colour (0xff4a1a1c),
                  Colour (0xff141414), Colour (0xff4a1a1c) };
        x.canvas = canvasFlat;
        x.flatSections = true;
        x.discKnob = true;
        x.pointerFrom = 0.0f;
        x.arcGlow = 0.0f;
        x.kerning = 0.02f;
        x.captionScale = 1.3f;
        x.captionCol = Colours::white;
        x.valueCol = Colours::white;
        x.rimWidth = 0.0f;
        x.captionScale = 1.5f;
        x.discRing = false;
        return x;
    }();
    return t;
}

// === Lattice Mod (ValhallaUberMod role): the original's look — a sky-blue field, white level bars, a graphite tab panel ===

inline const Theme& latticeTheme()
{
    static const Theme t = []
    {
        Theme x;
        x.name = "Lattice Mod";
        x.base = Style::glow;
        x.pal = { Colour (0xff5b8fc6), Colour (0xff5084bc), Colour (0xffffffff), Colour (0xffdbe9f8), Colour (0xff3a3a3c), Colour (0xff3a3a3c),
                  Colour (0xff3a3a3c), Colour (0xffffffff), Colour (0xff2c2c2e), Colour (0xff4f86c0), Colour (0xff4f86c0), Colour (0xff303032),
                  Colour (0xffffffff), Colour (0xff3a3a3c) };
        x.canvas = canvasFlat;
        x.flatSections = true;
        x.discKnob = true;
        x.pointerFrom = 0.0f;
        x.arcGlow = 0.0f;
        x.kerning = 0.02f;
        x.captionScale = 1.3f;
        x.captionCol = Colours::white;
        x.valueCol = Colours::white;
        x.rimWidth = 0.0f;
        x.barSlider = true;
        x.discRing = false;
        x.barCol = Colours::white;
        x.headerCol = Colour (0xff2f5f92);
        x.pal.accent = Colour (0xff1c1c1e);
        x.pal.accent2 = Colour (0xff3a3a3c);
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

// round 170k: the small corners the stage painters animate (Theme::motionArea), so only they are repainted
inline Rectangle<float> lampArea (Rectangle<float> s) { return Rectangle<float> (30.0f, 30.0f).withCentre ({ s.getRight() - 12.0f, s.getCentreY() }); }
inline Rectangle<float> recordArea (Rectangle<float> s)
{
    const auto r = juce::jmin (s.getHeight() * 0.48f, 18.0f);
    return Rectangle<float> (r * 2.0f + 8.0f, r * 2.0f + 8.0f).withCentre ({ s.getRight() - r - 4.0f, s.getCentreY() });
}
inline Rectangle<float> gridArea (Rectangle<float> s) { return s.withTrimmedLeft (juce::jmax (0.0f, s.getWidth() - 120.0f)); }

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
        x.hz = 10; // round 170k: slow motion, fewer frames
        x.motionArea = lampArea;
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

// === Ember Saturator (Decapitator role, round tp172d): the original's hardware look — see soundtoysTheme ==========

inline const Theme& emberTheme()
{
    // round tp172d: Decapitator's hardware look (its role) — black face, walnut cheeks, cream type, an orange style lamp
    static const Theme t = []
    {
        auto x = soundtoysTheme ("Ember Saturator", Colour (0xffff7a45), Colour (0xffe9e2d0), Colour (0xff121213), Colour (0xff3c3c3e), 1.0f);
        x.hwKeys = true;  // Punish / Thump / Steep and the five style keys
        x.cheek = 30.0f;
        x.numerals = true; // round tp172d (look slice): the big Drive's printed 0 … 10
        // round tp172d (look slice): the original's chunky black caps on a ring of dots, no lit arc; bigger spaced
        // legends; the name between two rules; the VU in a chrome bezel
        x.cap = Cap::bakelite;
        x.noValueArc = true;
        x.dotTicks = true;
        x.captionScale = 1.45f;
        x.titleRules = true;
        x.vuBezel = true;
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
    const auto stage = m.stage;
    if (stage.getWidth() < 40.0f || stage.getHeight() < 10.0f) return;
    const auto s = gridArea (stage); // only the strip's right end moves (CPU)
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
        x.hz = 12; // round 170k: slow motion, fewer frames
        x.motionArea = gridArea;
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
        x.hz = 12; // round 170k: slow motion, fewer frames
        x.motionArea = lampArea;
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
        x.hz = 12; // round 170k: slow motion, fewer frames
        x.motionArea = recordArea;
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

// === Voice Shift (Little AlterBoy role, round tp172d): the original's look — mustard panels, red LEDs ============

inline const Theme& voiceShiftTheme()
{
    // round tp172d: Little AlterBoy's look (its role) — black face, walnut cheeks, mustard panels, red LED read-outs
    static const Theme t = []
    {
        auto x = soundtoysTheme ("Voice Shift", Colour (0xffe8322a), Colour (0xfff7e9b0), Colour (0xffc39a1a), Colour (0xffc39a1a), 0.0f);
        x.valueCol = Colour (0xffff4a3a);
        x.captionCol = Colours::white; // the original's white legends on the mustard panels
        x.pal.well = Colour (0xff1a0806);
        // the original's three printed fields: mustard (pitch / formant), teal (mode), red (drive / mix)
        // round tp172d (look slice): the black face that frames them comes first
        x.sectionFills[0] = Colour (0xff131416);
        x.sectionFills[1] = Colour (0xffc39a1a);
        x.sectionFills[2] = Colour (0xff1d5a6c);
        x.sectionFills[3] = Colour (0xffb51d1d);
        x.hwKeys = true;
        x.cheek = 30.0f;
        x.cornerScale = 0.5f;
        x.captionScale = 1.45f; // round tp172d (look slice): the original's big legends
        return x;
    }();
    return t;
}

// === Quell Denoiser (noise-suppressor role): round tp172d — the reference's look: graphite metal shading darker to
// the bottom, a black meter well, a chrome fader cap on a black slot, orange read-outs ==============================

inline void canvasQuell (juce::Graphics& g, Rectangle<float> a, bool display)
{
    const auto& p = pal();
    vfill (g, a, p.bgTop, p.bgBottom);
    if (display) return;
    brushedLines (g, a, 17, 0.035f);
    if (! reduced) bloom (g, { a.getX() + a.getWidth() * 0.6f, a.getY() }, a.getWidth() * 0.8f, Colours::white, 0.10f); // the top light
    vignette (g, a, 0.35f);
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
        x.base = Style::hardware;
        x.pal = { Colour (0xff8c9299), Colour (0xff43484e), Colour (0xffeb9a2e), Colour (0xffffc46b), Colour (0x00000000), Colour (0x00000000),
                  Colour (0xff2a2d31), Colour (0xffeef0f2), Colour (0xff0a0b0c), Colour (0xffe2e5e8), Colour (0xff5f6368), Colour (0xff0d0e10),
                  Colour (0xff1a1a1a), Colour (0xff34383d) };
        x.canvas = canvasQuell;
        x.titleEmblem = quellEmblem;
        x.cap = Cap::bakelite;
        x.arcGlow = 0.0f;
        x.buttTrack = true;
        x.flatSections = true;
        x.rimWidth = 0.0f;
        x.captionCol = Colour (0xffeef0f2);
        x.valueCol = Colour (0xffffa63d);
        x.offLabelCol = Colour (0xffd8dbde);
        x.bigThumb = 5.0f; // the reference's large grooved metal fader cap
        x.hwKeys = true;
        x.headerCol = Colour (0xff2b2d30);
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
// round tp172d: PanMan's hardware look (Auto-pan's role) — black face, walnut cheeks, white outlines, a red LED meter
inline const Theme& autoPanTheme()
{
    // round tp172d: PanMan's look (its role) — black face, walnut cheeks, rounded pods with white outlines, square keys
    static const Theme t = []
    {
        auto x = soundtoysTheme ("Auto-pan", Colour (0xffe8402c), Colour (0xffeeeeea), Colour (0xff0b0b0c), Colour (0xff6a6a6c), 2.0f);
        x.cornerScale = 4.0f;
        x.bow = 0.05f; // round tp172d (look slice): PanMan's bowed pods, a grey bevel instead of the white outline
        x.hwKeys = true;
        x.cheek = 30.0f;
        return x;
    }();
    return t;
}
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
LYNX_LITE_THEME (freqShiftTheme, "Freq Shift", Style::neon, Colour (0xff102224), Colour (0xff04090a), Colour (0xff3ae0c0), Colour (0xffff8a6a), canvasShift, Cap::orb)
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
        case Art::ridge4k: return &ridge4kTheme();
        case Art::kiln: return &kilnTheme();
        case Art::fret: return &fretTheme();
        case Art::shell: return &shellTheme();
        case Art::strike: return &strikeTheme();
        case Art::anvil: return &anvilTheme();
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
        case Art::luxeSpace: return &luxeSpaceTheme();
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
        case EffectKind::kilnChannel: return Art::kiln;
        case EffectKind::fretChannel: return Art::fret;
        case EffectKind::shellChannel: return Art::shell;
        case EffectKind::strikeShaper: return Art::strike;
        case EffectKind::anvilBus: return Art::anvil;
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

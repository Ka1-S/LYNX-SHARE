// Round tp172c8: the panels of Fret Channel (guitar / bass channel strip) and Shell Channel (drum channel strip), drawn
// here from scratch in the layout of their roles' originals (SSL Guitarstrip, SSL Native Drumstrip: the same sections,
// controls and read-outs in the same places), with LYNX's own names, marks and drawing — no bitmap, logo or type of
// theirs. Included by ProEditors.cpp inside its anonymous namespace.

// the shared hardware-panel kit: knobs with a printed dot scale and end labels, lit square power keys, text keys, menus,
// teal read-out screens, stereo meters with a printed scale
class StripPanel : public ProEditor
{
public:
    StripPanel (AudioEngine& e, int t, int s, EffectKind k, bool d, int w, int h) : ProEditor (e, t, s, k, d, w, h) {}

protected:
    static constexpr int idParam = 100, idPower = 400, idExtra = 500;
    static constexpr float y0 = headerH;
    static constexpr float typeScale = 1.4f; // the panels' type, measured against the originals' at the same panel height
    enum class Cap { silver, black, blue, red, green };
    struct Knob
    {
        int p = -1;
        float x = 0, y = 0, r = 18;
        Cap cap = Cap::silver;
        const char* caption = nullptr; // above (Fret) or below (Shell) the knob, see captionBelow
        const char* lo = nullptr;
        const char* hi = nullptr;
        const char* unit = nullptr;    // under the knob
        bool visible = true;
    };
    enum class Key { power, text, lit, menu, screen };
    struct Button
    {
        int p = -1;
        juce::Rectangle<float> r;
        Key key = Key::text;
        const char* label = nullptr;
        const char* options = nullptr; // a menu's options, '|' separated (index = value x (n - 1))
        bool visible = true;
    };
    std::vector<Knob> knobs;
    std::vector<Button> buttons;
    bool captionBelow = false;
    juce::Colour panelText { 0xffeeeeec }, dimText { 0xffb9bbbf };

    static juce::Point<float> at (float x, float y) { return { x, y0 + y }; }
    static juce::Rectangle<float> rect (float x, float y, float w, float h) { return { x, y0 + y, w, h }; }
    static juce::Point<float> polar (juce::Point<float> c, float a, float r) { return c + juce::Point<float> { std::sin (a), -std::cos (a) } * r; }
    static float angleOf (float v) { return juce::MathConstants<float>::pi * (-0.75f + 1.5f * juce::jlimit (0.0f, 1.0f, v)); }
    static void text (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, float size, bool bold, juce::Colour c,
                      juce::Justification j = juce::Justification::centred, float kern = 0.0f)
    {
        g.setColour (c);
        g.setFont (juce::Font (theme::font (size * typeScale, bold)).withExtraKerningFactor (kern));
        if (t.containsChar ('\n'))
            g.drawFittedText (t, r.toNearestInt(), j, 2, 1.0f);
        else
            g.drawText (t, r, j, false);
    }

    // which knob slot shows which parameter right now (subclasses swap the amp's controls); -1 = none
    virtual int stepsOf (int) const { return 0; }
    juce::String hintFor (int p) const
    {
        const auto& info = effectInfo (kind);
        if (p < 0 || p >= (int) info.params.size()) return {};
        const auto name = tr (info.params[(std::size_t) p].name);
        const auto what = pluginheader::describe (kind, info.params[(std::size_t) p].name);
        return what.isNotEmpty() ? name + juce::String::fromUTF8 (" \xe2\x80\x94 ") + what : name;
    }

    void layoutControls (std::vector<Ctl>& out) const override
    {
        for (const auto& k : knobs)
            if (k.visible && k.p >= 0)
                out.push_back (custom (juce::Rectangle<float> ((k.r + 7.0f) * 2.0f, (k.r + 7.0f) * 2.0f).withCentre (at (k.x, k.y)), idParam + k.p, -1, hintFor (k.p)));
        for (const auto& b : buttons)
            if (b.visible)
                out.push_back (custom (b.r.translated (0.0f, y0).expanded (2.0f), b.p >= 0 ? idParam + b.p : idPower, -1,
                                       b.p >= 0 ? hintFor (b.p) : tr ("Power: switch the effect on or off")));
        extraControls (out);
    }
    virtual void extraControls (std::vector<Ctl>&) const {}
    virtual void extraDown (const Ctl&, const juce::MouseEvent&) {}
    virtual void extraDrag (const Ctl&, const juce::MouseEvent&) {}

    const Button* buttonFor (int p) const
    {
        for (const auto& b : buttons)
            if (b.visible && b.p == p) return &b;
        return nullptr;
    }
    void edit (int p, float x)
    {
        if (const auto steps = stepsOf (p); steps > 0)
            x = (float) std::lround (juce::jlimit (0.0f, 1.0f, x) * (float) steps) / (float) steps;
        set ({ { p, x } });
    }

    int dragParam = -1;
    bool dragging (int p) const { return dragParam == p && isMouseButtonDown(); }
    void customDown (const Ctl& c, const juce::MouseEvent& e) override
    {
        dragParam = -1;
        if (c.id >= idExtra)
        {
            extraDown (c, e);
            return;
        }
        if (c.id == idPower)
        {
            toggleEnabled();
            return;
        }
        const auto p = c.id - idParam;
        if (e.mods.isPopupMenu())
        {
            ui::showValueMenu (*this, &engine, specFor (p, stepsOf (p)), v (p), [this, p] (double x) { edit (p, (float) x); });
            return;
        }
        if (const auto* b = buttonFor (p))
        {
            if (b->key == Key::menu)
            {
                juce::PopupMenu menu;
                const auto options = juce::StringArray::fromTokens (b->options, "|", "");
                const auto n = juce::jmax (2, options.size());
                const auto now = (int) std::lround (v (p) * (float) (n - 1));
                for (int i = 0; i < options.size(); ++i)
                    menu.addItem (tr (options[i]), true, i == now, [this, p, i, n] { edit (p, (float) i / (float) (n - 1)); });
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (b->r.translated (0.0f, y0).toNearestInt())));
                return;
            }
            if (b->key != Key::screen)
            {
                edit (p, v (p) >= 0.5f ? 0.0f : 1.0f);
                return;
            }
        }
        if (ui::isResetClick (e))
        {
            edit (p, (float) specFor (p).defaultValue);
            return;
        }
        dragParam = p;
        valueDrag.begin (e, v (p));
        dragStarted (p);
    }
    virtual void dragStarted (int) {}
    virtual void dragEnded (int) {}
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragParam >= 0) dragEnded (dragParam);
        dragParam = -1;
        ProEditor::mouseUp (e);
    }
    void customDrag (const Ctl& c, const juce::MouseEvent& e) override
    {
        if (c.id >= idExtra)
        {
            extraDrag (c, e);
            return;
        }
        if (dragParam < 0) return;
        const auto spec = specFor (dragParam, stepsOf (dragParam));
        const auto x = (float) valueDrag.update (e, spec);
        edit (dragParam, x);
        ui::showValueTip (*this, c.r.toNearestInt(), spec.text (v (dragParam)));
    }
    bool bodyWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        Ctl c;
        if (! controlAt (e.position, c) || c.id < idParam || c.id >= idPower) return false;
        const auto p = c.id - idParam;
        if (const auto* b = buttonFor (p); b != nullptr && b->key != Key::screen) return false;
        const auto spec = specFor (p, stepsOf (p));
        if (const auto x = ui::wheelValue (v (p), e, wheel, spec))
        {
            edit (p, (float) *x);
            ui::flashValueTip (*this, c.r.toNearestInt(), spec.text (v (p)));
        }
        return true;
    }

    // --- metering: peak bars with a fall, for input L / R (4, 5) and output L / R (6, 7) -------------------------
    std::array<float, 4> barDb { -120.0f, -120.0f, -120.0f, -120.0f };
    float meterDb (int k) const
    {
        const auto lin = liveMeter (k);
        return lin > 1e-6f ? 20.0f * std::log10 (lin) : -120.0f;
    }
    bool tickBars()
    {
        const auto* fx = current();
        const auto on = fx != nullptr && fx->enabled;
        bool moving = false;
        for (int i = 0; i < 4; ++i)
        {
            const auto now = on ? meterDb (4 + i) : -120.0f;
            const auto next = now > barDb[(std::size_t) i] ? now : juce::jmax (-120.0f, barDb[(std::size_t) i] - 1.5f);
            moving = moving || std::abs (next - barDb[(std::size_t) i]) > 0.01f;
            barDb[(std::size_t) i] = next;
        }
        return moving;
    }

    // --- drawing kit -------------------------------------------------------------------------------------------
    static juce::Colour capColour (Cap c)
    {
        switch (c)
        {
            case Cap::black: return juce::Colour (0xff2a2b2e);
            case Cap::blue: return juce::Colour (0xff2f74c0);
            case Cap::red: return juce::Colour (0xffc0262c);
            case Cap::green: return juce::Colour (0xff1d8a57);
            default: return juce::Colour (0xffd9dadc);
        }
    }
    void drawKnob (juce::Graphics& g, const Knob& k) const
    {
        if (! k.visible) return;
        const auto c = at (k.x, k.y);
        const auto r = k.r;
        // the printed scale: eleven dots round the throw
        for (int i = 0; i <= 10; ++i)
        {
            g.setColour (panelText.withAlpha (0.75f));
            g.fillEllipse (juce::Rectangle<float> (2.2f, 2.2f).withCentre (polar (c, angleOf ((float) i / 10.0f), r + 6.0f)));
        }
        // the skirt's shadow, the ridged skirt, the cap
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.16f));
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c).translated (0.0f, (float) i * 1.3f).expanded ((float) i * 0.8f));
        }
        const auto skirt = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4c50), c.x, c.y - r, juce::Colour (0xff121315), c.x, c.y + r, false));
        g.fillEllipse (skirt);
        g.setColour (juce::Colour (0xff0a0a0b));
        for (int i = 0; i < 28; ++i)
        {
            const auto a = juce::MathConstants<float>::twoPi * (float) i / 28.0f;
            g.drawLine ({ polar (c, a, r * 0.86f), polar (c, a, r) }, 0.9f);
        }
        const auto capR = r * 0.84f;
        const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
        const auto base = capColour (k.cap);
        const bool hot = dragging (k.p) || juce::Rectangle<float> ((r + 6.0f) * 2.0f, (r + 6.0f) * 2.0f).withCentre (c).contains (mouse);
        g.setGradientFill (juce::ColourGradient (base.brighter (hot ? 0.55f : 0.4f), c.x - capR * 0.5f, c.y - capR * 0.7f,
                                                 base.darker (0.55f), c.x + capR * 0.5f, c.y + capR * 0.8f, false));
        g.fillEllipse (cap);
        g.setColour (juce::Colours::white.withAlpha (k.cap == Cap::silver ? 0.5f : 0.18f));
        g.drawEllipse (cap.reduced (0.6f), 0.8f);
        // the pointer: dark on the silver cap, white on the coloured ones
        const auto a = angleOf (v (k.p));
        g.setColour (k.cap == Cap::silver ? juce::Colour (0xff17181a) : juce::Colours::white);
        g.drawLine ({ polar (c, a, capR * 0.2f), polar (c, a, r * 0.96f) }, juce::jmax (2.0f, r * 0.11f));
        // the end labels, the unit and the caption
        const auto lp = polar (c, angleOf (0.0f), r + 12.0f), hp = polar (c, angleOf (1.0f), r + 12.0f);
        if (k.lo != nullptr) text (g, juce::Rectangle<float> (40.0f, 14.0f).withCentre (lp.translated (captionBelow ? 0.0f : -6.0f, 4.0f)), tr (k.lo), captionBelow ? 7.0f : 8.0f, false, panelText);
        if (k.hi != nullptr) text (g, juce::Rectangle<float> (40.0f, 14.0f).withCentre (hp.translated (captionBelow ? 0.0f : 6.0f, 4.0f)), tr (k.hi), captionBelow ? 7.0f : 8.0f, false, panelText);
        if (k.unit != nullptr) text (g, juce::Rectangle<float> (44.0f, 14.0f).withCentre (c.translated (0.0f, r + 13.0f)), tr (k.unit), 8.5f, false, dimText);
        if (k.caption != nullptr)
        {
            const auto cy = captionBelow ? c.y + r + 28.0f : c.y - r - 17.0f;
            const juce::String cap (k.caption);
            const auto unit = cap == "Hz" || cap == "kHz"; // units keep their case
            text (g, { c.x - 50.0f, cy - 7.0f, 100.0f, 14.0f }, unit ? cap : caps (tr (k.caption)), 9.0f, captionBelow, panelText, juce::Justification::centred, 0.03f);
        }
        if (hot && ! dragging (k.p))
        {
            const auto t = ProEditor::text (k.p);
            const auto box = juce::Rectangle<float> (juce::jmax (52.0f, (float) t.length() * 6.4f + 10.0f), 16.0f).withCentre (c.translated (0.0f, r + 26.0f));
            g.setColour (juce::Colour (0xe0101012));
            g.fillRoundedRectangle (box, 3.0f);
            text (g, box, t, 7.5f, false, juce::Colours::white);
        }
    }

    // a raised square key; lit = warm orange glass (the power keys and the lit keys)
    void drawKey (juce::Graphics& g, juce::Rectangle<float> r, bool lit, bool hot) const
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.5f).expanded (1.0f), 4.0f);
        if (lit)
        {
            g.setColour (juce::Colour (0xfff08a3a).withAlpha (0.28f));
            g.fillRoundedRectangle (r.expanded (3.0f), 6.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffc58a), r.getCentreX(), r.getCentreY(), juce::Colour (0xffe0742a), r.getX(), r.getY(), true));
        }
        else
            g.setGradientFill (juce::ColourGradient (juce::Colour (hot ? 0xffd0d2d5 : 0xffbfc1c4), 0.0f, r.getY(), juce::Colour (0xff8d8f93), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.5f);
        g.setColour (juce::Colours::white.withAlpha (lit ? 0.45f : 0.6f));
        g.drawRoundedRectangle (r.reduced (1.5f), 2.5f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.drawRoundedRectangle (r, 3.5f, 1.0f);
    }
    static void powerGlyph (juce::Graphics& g, juce::Point<float> c, float rr, juce::Colour col)
    {
        juce::Path glyph;
        glyph.addCentredArc (c.x, c.y, rr, rr, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
        glyph.startNewSubPath (c.x, c.y - rr * 1.25f);
        glyph.lineTo (c.x, c.y - rr * 0.2f);
        g.setColour (col);
        g.strokePath (glyph, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void drawButton (juce::Graphics& g, const Button& b) const
    {
        if (! b.visible) return;
        const auto r = b.r.translated (0.0f, y0);
        const bool hot = r.contains (mouse);
        switch (b.key)
        {
            case Key::power:
            {
                const auto* fx = current();
                const bool lit = b.p >= 0 ? v (b.p) >= 0.5f : (fx != nullptr && fx->enabled);
                drawKey (g, r, lit, hot);
                powerGlyph (g, r.getCentre(), r.getWidth() * 0.2f, lit ? juce::Colour (0xff3a1a06) : juce::Colour (0xff2a2b2e));
                break;
            }
            case Key::text: case Key::lit:
            {
                const bool lit = b.key == Key::lit && v (b.p) >= 0.5f;
                const bool pressed = b.key == Key::text && v (b.p) >= 0.5f;
                drawKey (g, r, lit, hot);
                if (pressed) // a latched grey key reads darker, with a lit line
                {
                    g.setColour (juce::Colours::black.withAlpha (0.25f));
                    g.fillRoundedRectangle (r.reduced (2.0f), 2.5f);
                    g.setColour (juce::Colour (0xfff08a3a));
                    g.fillRect (r.getCentreX() - 8.0f, r.getBottom() - 4.0f, 16.0f, 1.6f);
                }
                const auto lines = juce::StringArray::fromTokens (caps (tr (b.label)), "\n", "");
                const auto lh = 11.5f;
                auto ty = r.getCentreY() - lh * (float) lines.size() * 0.5f;
                for (const auto& line : lines)
                {
                    text (g, { r.getX() - 4.0f, ty, r.getWidth() + 8.0f, lh }, line, 7.0f, true, lit ? juce::Colour (0xff2b1404) : juce::Colour (0xff1d1e21));
                    ty += lh;
                }
                break;
            }
            case Key::menu:
            {
                g.setColour (juce::Colour (0xff0c2b27));
                g.fillRoundedRectangle (r, 2.5f);
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.drawRoundedRectangle (r, 2.5f, 1.0f);
                const auto options = juce::StringArray::fromTokens (b.options, "|", "");
                const auto n = juce::jmax (2, options.size());
                const auto i = juce::jlimit (0, options.size() - 1, (int) std::lround (v (b.p) * (float) (n - 1)));
                text (g, r.withTrimmedRight (14.0f), caps (tr (options[i])), 9.0f, true, juce::Colour (0xff7fe3cf));
                juce::Path tri;
                const auto tc = juce::Point<float> (r.getRight() - 9.0f, r.getCentreY());
                tri.addTriangle (tc.x - 3.5f, tc.y - 2.0f, tc.x + 3.5f, tc.y - 2.0f, tc.x, tc.y + 2.5f);
                g.setColour (juce::Colour (0xff7fe3cf));
                g.fillPath (tri);
                break;
            }
            case Key::screen:
            {
                screenBox (g, r);
                text (g, r, ProEditor::text (b.p), 8.5f, true, juce::Colour (0xff7fe3cf));
                break;
            }
        }
    }
    static void screenBox (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (juce::Colour (0xff0b2622));
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.drawRoundedRectangle (r, 2.5f, 1.2f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine ((int) r.getBottom(), r.getX() + 2.0f, r.getRight() - 2.0f);
    }
    // a recessed panel inside the face
    static void recess (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, float corner = 5.0f)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r, corner);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine ((int) r.getBottom() - 1, r.getX() + corner, r.getRight() - corner);
    }
    // a stereo meter well: two green bars, dB marks (printed between or beside them), clip lamps on top
    void stereoMeter (juce::Graphics& g, juce::Rectangle<float> well, int first, float rangeDb, std::initializer_list<int> marks, bool scaleBetween) const
    {
        g.setColour (juce::Colour (0xff101113));
        g.fillRoundedRectangle (well, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (well, 3.0f, 1.0f);
        const auto top = well.getY() + 14.0f, bottom = well.getBottom() - 10.0f, h = bottom - top;
        const float bw = 7.0f;
        const float xs[2] { scaleBetween ? well.getX() + 6.0f : well.getX() + 6.0f, scaleBetween ? well.getRight() - 6.0f - bw : well.getX() + 8.0f + bw };
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto slot = juce::Rectangle<float> (xs[ch], top, bw, h);
            g.setColour (juce::Colour (0xff1c2a24));
            g.fillRect (slot);
            const auto db = barDb[(std::size_t) (first + ch)];
            g.setColour (db > -0.1f ? juce::Colour (0xffe5383b) : juce::Colour (0xff3b1c1c));
            g.fillRect (juce::Rectangle<float> (xs[ch], well.getY() + 4.0f, bw, 5.0f));
            if (db > -rangeDb)
            {
                const auto y = top + juce::jlimit (0.0f, rangeDb, -db) / rangeDb * h;
                const auto lit = juce::Rectangle<float> (slot.getX() + 1.0f, y, bw - 2.0f, bottom - y);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2fa77a), 0.0f, bottom, juce::Colour (0xff8be6c2), 0.0f, top, false));
                g.fillRect (lit);
            }
            g.setColour (juce::Colour (0xff2fa77a));
            g.fillRect (juce::Rectangle<float> (xs[ch], bottom + 2.0f, bw, 3.0f));
        }
        for (const auto m : marks)
        {
            const auto y = top + (float) m / rangeDb * h;
            const auto tx = scaleBetween ? well.getCentreX() - 12.0f : well.getRight() + 1.0f;
            text (g, { tx, y - 5.5f, 24.0f, 11.0f }, juce::String (m), 6.5f, false, panelText.withAlpha (0.85f));
        }
    }
    // the plugin's top band: the power key and the name on the left, LYNX's mark on the right
    void topBand (juce::Graphics& g, juce::Rectangle<float> band, const juce::String& name, juce::Colour fill, juce::Colour ink, juce::Colour markInk) const
    {
        g.setGradientFill (juce::ColourGradient (fill.brighter (0.08f), 0.0f, band.getY(), fill.darker (0.12f), 0.0f, band.getBottom(), false));
        g.fillRect (band);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawHorizontalLine ((int) band.getBottom() - 1, band.getX(), band.getRight());
        const auto nameR = juce::Rectangle<float> (band.getX() + 54.0f, band.getY(), 300.0f, band.getHeight());
        g.setColour (ink);
        g.setFont (juce::Font (theme::font (21.0f, true)).withExtraKerningFactor (0.01f));
        g.drawText (name, nameR, juce::Justification::centredLeft, false);
        // LYNX's mark: the word, small caps, tracked out
        text (g, { band.getRight() - 300.0f, band.getY(), 280.0f, band.getHeight() }, juce::String (juce::CharPointer_UTF8 ("L Y N X  \xc2\xb7  ")) + caps (tr ("Channel strips")), 10.5f, false,
              markInk.withAlpha (0.9f), juce::Justification::centredRight, 0.06f);
    }
};

// ===================================================================================================== Fret Channel
// The guitar / bass channel strip: the input column (meter, high-pass screen, input), PHASE CORRECTION (align to, the
// correlation, DELAY with its time dial and nudge arcs, polarity, ALL-PASS with its phase screen), the EQUALISER (LF,
// the blue MF pair, the red HF, the low-pass) over its curve screen, COMP (mode, reduction meter, auto gain, threshold,
// mix), DRIVE (amp model, pre-amp, red drive, the voicings, mix) and the output column (meter, mix screen, output)
class FretEditor final : public StripPanel
{
public:
    FretEditor (AudioEngine& e, int t, int s, bool d) : StripPanel (e, t, s, EffectKind::fretChannel, d, 976, (int) (headerH + 542.0f + barH))
    {
        namespace f = mixsuite::fret;
        knobs = {
            { f::input, 50, 478, 22, Cap::black, "Input", "-24", "+24", "dB" },
            { f::output, 926, 478, 22, Cap::black, "Output", "-24", "+24", "dB" },
            { f::delay, 202, 268, 22, Cap::black, "Time", "-20", "+20", "ms" },
            { f::lowGain, 537, 208, 22, Cap::black, "Gain", "-12", "+12", "dB" },
            { f::midFreq, 607, 158, 19.5, Cap::blue, "Freq", "0.5", "7", "kHz" },
            { f::midGain, 668, 208, 22, Cap::blue, "Gain", "-12", "+12", "dB" },
            { f::highGain, 751, 208, 22, Cap::red, "Gain", "-12", "+12", "dB" },
            { f::lpf, 818, 158, 20.5, Cap::silver, "Freq", "3", "20", "kHz" },
            { f::compThreshold, 150, 478, 22, Cap::black, "Threshold", "-36", "0", "dB" },
            { f::compMix, 342, 478, 22, Cap::silver, "Mix", "0", "100", "%" },
            { f::preAmp, 440, 478, 22, Cap::silver, "Pre-amp", "-6", "30", "dB" },
            { f::guitarDrive, 516, 478, 22, Cap::red, "Drive", "0", "11", nullptr },
            { f::guitarBoost, 599, 478, 22, Cap::black, "Boost", "-", "+", nullptr },
            { f::guitarPunch, 672, 478, 22, Cap::black, "Punch", "-", "+", nullptr },
            { f::guitarEdge, 745, 478, 22, Cap::black, "Edge", "0", "10", nullptr },
            { f::bassDrive, 516, 478, 22, Cap::red, "Drive", "0", "11", nullptr },
            { f::bassPunch, 599, 478, 22, Cap::black, "Punch", "-", "+", nullptr },
            { f::bassShape, 672, 478, 22, Cap::black, "Shape", "0", "10", nullptr },
            { f::driveMix, 830, 478, 22, Cap::silver, "Mix", "0", "100", "%" },
        };
        buttons = {
            { -1, { 120, 14, 28, 28 }, Key::power, nullptr },
            { f::phaseCorrection, { 121, 76, 24, 24 }, Key::power, nullptr },
            { f::scListen, { 368, 72, 44, 30 }, Key::text, TR_NOOP ("S/C\nlisten") },
            { f::monoListen, { 424, 72, 46, 30 }, Key::text, TR_NOOP ("Mono\nlisten") },
            { f::alignMode, { 118, 129, 97, 25 }, Key::menu, nullptr, TR_NOOP ("Side chain|Left|Right") },
            { f::delayIn, { 134, 196, 48, 26 }, Key::lit, TR_NOOP ("In") },
            { f::phaseInvert, { 222, 193, 46, 28 }, Key::text, nullptr },
            { f::allPassIn, { 320, 194, 46, 27 }, Key::lit, TR_NOOP ("In") },
            { f::allPassOrder, { 406, 194, 48, 28 }, Key::lit, TR_NOOP ("2nd\norder") },
            { f::allPassFreq, { 313, 318, 58, 25 }, Key::screen, nullptr },
            { f::allPassQ, { 403, 318, 58, 25 }, Key::screen, nullptr },
            { f::hpf, { 22, 394, 56, 26 }, Key::screen, nullptr },
            { f::outputMix, { 898, 394, 56, 26 }, Key::screen, nullptr },
            { f::eqIn, { 506, 76, 24, 24 }, Key::power, nullptr },
            { f::autoListen, { 754, 72, 46, 30 }, Key::text, TR_NOOP ("Auto\nlisten") },
            { f::eqPrePost, { 812, 72, 46, 30 }, Key::text, TR_NOOP ("Pre\npost") },
            { f::compIn, { 121, 395, 24, 24 }, Key::power, nullptr },
            { f::compMode, { 210, 395, 80, 25 }, Key::menu, nullptr, TR_NOOP ("Fastest|Fast|Medium|Slow|Slowest") },
            { f::compAutoGain, { 320, 392, 46, 28 }, Key::lit, TR_NOOP ("Auto\ngain") },
            { f::driveIn, { 412, 395, 24, 24 }, Key::power, nullptr },
            { f::driveModel, { 622, 395, 102, 25 }, Key::menu, nullptr, TR_NOOP ("Guitar amp|Bass amp") },
        };
        syncVisibility();
        shownBass = bass();
    }

private:
    static constexpr int idNudge = idExtra; // + 0..3: the delay's -10 / -1 / +1 / +10 sample arcs

    juce::String title() const override { return "Fret Channel"; }
    int stepsOf (int p) const override
    {
        namespace f = mixsuite::fret;
        return p == f::alignMode ? 2 : p == f::compMode ? 4 : 0;
    }
    bool bass() const { return v (mixsuite::fret::driveModel) >= 0.5f; }
    // EQ Auto listen: while an EQ band's knob is held, that band is heard alone (a transient, unsaved setting)
    void dragStarted (int p) override
    {
        namespace f = mixsuite::fret;
        const auto band = p == f::lowGain ? 1 : (p == f::midFreq || p == f::midGain) ? 2 : p == f::highGain ? 3 : 0;
        if (band > 0 && v (f::autoListen) >= 0.5f)
            set ({ { f::listenBand, (float) band / 3.0f } }, false);
    }
    void dragEnded (int) override
    {
        if (v (mixsuite::fret::listenBand) > 0.0f)
            set ({ { mixsuite::fret::listenBand, 0.0f } }, false);
    }
    // the amp's controls follow the model: the guitar's pre-amp, drive, Boost / Punch / Edge or the bass's drive, Punch / Shape
    void syncVisibility()
    {
        namespace f = mixsuite::fret;
        const auto b = bass();
        for (auto& k : knobs)
        {
            if (k.p == f::preAmp || k.p == f::guitarDrive || k.p == f::guitarBoost || k.p == f::guitarPunch || k.p == f::guitarEdge) k.visible = ! b;
            if (k.p == f::bassDrive || k.p == f::bassPunch || k.p == f::bassShape) k.visible = b;
        }
    }
    bool shownBass = false;

    juce::Rectangle<float> nudgeRect (int i) const
    {
        // the arcs either side of the time dial: outer -10 / inner -1 on the left, inner +1 / outer +10 on the right
        const auto c = at (202.0f, 268.0f);
        const float dx[4] { -48.0f, -34.0f, 34.0f, 48.0f };
        return juce::Rectangle<float> (12.0f, 34.0f).withCentre (c.translated (dx[i], 0.0f));
    }
    void extraControls (std::vector<Ctl>& out) const override
    {
        static const char* hints[4] { TR_NOOP ("Delay: 10 samples earlier"), TR_NOOP ("Delay: 1 sample earlier"), TR_NOOP ("Delay: 1 sample later"),
                                      TR_NOOP ("Delay: 10 samples later") };
        for (int i = 0; i < 4; ++i)
            out.push_back (custom (nudgeRect (i), idNudge + i, -1, tr (hints[i])));
    }
    void extraDown (const Ctl& c, const juce::MouseEvent&) override
    {
        namespace f = mixsuite::fret;
        static const int steps[4] { -10, -1, 1, 10 };
        const auto i = c.id - idNudge;
        if (i < 0 || i > 3) return;
        // one sample at 48 kHz over the ±20 ms throw
        edit (f::delay, v (f::delay) + (float) steps[i] / (48.0f * 40.0f));
    }

    void tick() override
    {
        bool moving = tickBars();
        const auto gr = liveMeter (0) * 30.0f, corr = liveMeter (1);
        moving = moving || std::abs (gr - grShown) > 0.05f || std::abs (corr - corrShown) > 0.002f;
        grShown = grShown + (gr - grShown) * 0.4f;
        corrShown = corrShown + (corr - corrShown) * 0.3f;
        if (bass() != shownBass)
        {
            shownBass = bass();
            syncVisibility();
            moving = true;
        }
        if (moving) repaint();
    }
    float grShown = 0.0f, corrShown = 0.5f;

    void phaseScreen (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        namespace f = mixsuite::fret;
        screenBox (g, r);
        g.setColour (juce::Colour (0xff7fe3cf).withAlpha (0.15f));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX() + 3.0f, r.getRight() - 3.0f);
        // the all-pass's phase (-180 .. +180 degrees) over 20 Hz .. 20 kHz
        const bool in = v (f::allPassIn) >= 0.5f;
        const auto f0 = f::allPassHz (v (f::allPassFreq)), q = f::allPassQOf (v (f::allPassQ));
        const bool second = v (f::allPassOrder) >= 0.5f;
        juce::Path curve;
        for (int i = 0; i <= 120; ++i)
        {
            const auto hz = 20.0 * std::pow (1000.0, i / 120.0);
            double ph = 0.0;
            if (in)
            {
                const auto x = hz / f0;
                ph = second ? -2.0 * std::atan2 (x / q, 1.0 - x * x) : 2.0 * std::atan (1.0 / x) - 3.14159265358979; // 1st: 180 low -> 0 high
                if (! second) ph = 3.14159265358979 - 2.0 * std::atan (x);
                while (ph > 3.14159265358979) ph -= 2.0 * 3.14159265358979;
                while (ph < -3.14159265358979) ph += 2.0 * 3.14159265358979;
            }
            const auto px = r.getX() + 4.0f + (r.getWidth() - 8.0f) * (float) i / 120.0f;
            const auto py = r.getCentreY() - (float) (ph / 3.14159265358979) * (r.getHeight() * 0.5f - 5.0f);
            if (i == 0) curve.startNewSubPath (px, py);
            else curve.lineTo (px, py);
        }
        g.setColour (juce::Colour (0xff7fe3cf).withAlpha (in ? 0.95f : 0.35f));
        g.strokePath (curve, juce::PathStrokeType (1.4f));
    }

    void eqScreen (juce::Graphics& g, juce::Rectangle<float> r, const EffectSlot& fx) const
    {
        namespace f = mixsuite::fret;
        screenBox (g, r);
        g.setColour (juce::Colour (0xff7fe3cf).withAlpha (0.12f));
        for (const auto hz : { 100.0, 1000.0, 10000.0 })
        {
            const auto x = r.getX() + r.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0));
            g.drawVerticalLine ((int) x, r.getY() + 3.0f, r.getBottom() - 3.0f);
        }
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX() + 3.0f, r.getRight() - 3.0f);
        const auto rp = f::ridgeParams (fx.params);
        juce::Path curve;
        for (int i = 0; i <= 160; ++i)
        {
            const auto hz = 20.0 * std::pow (1000.0, i / 160.0);
            const auto db = juce::jlimit (-18.0, 18.0, mixsuite::ridge::responseDb (rp, 48000.0, hz));
            const auto px = r.getX() + 3.0f + (r.getWidth() - 6.0f) * (float) i / 160.0f;
            const auto py = r.getCentreY() - (float) (db / 18.0) * (r.getHeight() * 0.5f - 6.0f);
            if (i == 0) curve.startNewSubPath (px, py);
            else curve.lineTo (px, py);
        }
        g.setColour (juce::Colour (0xff7fe3cf).withAlpha (v (f::eqIn) >= 0.5f ? 0.95f : 0.35f));
        g.strokePath (curve, juce::PathStrokeType (1.6f));
    }

    void paintBody (juce::Graphics& g, const EffectSlot& fx) override
    {
        namespace f = mixsuite::fret;
        const auto body = bodyArea();
        // the face: mid-grey, a fine vertical grain
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6b6d71), 0.0f, body.getY(), juce::Colour (0xff4f5155), 0.0f, body.getBottom(), false));
        g.fillRect (body);
        juce::Random rnd (225);
        for (int i = 0; i < 1400; ++i)
        {
            const auto x = body.getX() + rnd.nextFloat() * body.getWidth();
            const auto y = body.getY() + rnd.nextFloat() * body.getHeight();
            g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.01f + 0.015f * rnd.nextFloat()));
            g.drawVerticalLine ((int) x, y, juce::jmin (body.getBottom(), y + 4.0f + rnd.nextFloat() * 18.0f));
        }
        // the side columns
        for (const auto x : { 0.0f, 876.0f })
        {
            const auto col = rect (x, 0.0f, 100.0f, 542.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3b3f), 0.0f, col.getY(), juce::Colour (0xff2a2b2e), 0.0f, col.getBottom(), false));
            g.fillRect (col);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawVerticalLine ((int) (x == 0.0f ? col.getRight() - 1.0f : col.getX()), col.getY(), col.getBottom());
        }
        topBand (g, rect (100.0f, 0.0f, 776.0f, 56.0f), "Fret Channel", juce::Colour (0xff97989c), juce::Colour (0xff1f2023), juce::Colour (0xfff4f4f2));
        // section separators
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawVerticalLine (486, y0 + 56.0f, y0 + 362.0f);
        g.drawHorizontalLine ((int) (y0 + 362.0f), 100.0f, 876.0f);
        g.drawHorizontalLine ((int) (y0 + 262.0f), 486.0f, 876.0f);
        g.drawVerticalLine (391, y0 + 362.0f, y0 + 542.0f);

        // the columns' contents
        for (const auto [x, name, first] : { std::tuple<float, const char*, int> { 0.0f, TR_NOOP ("Input"), 0 }, { 876.0f, TR_NOOP ("Output"), 2 } })
        {
            text (g, rect (x, 16.0f, 100.0f, 20.0f), caps (tr (name)), 9.5f, true, panelText, juce::Justification::centred, 0.05f);
            stereoMeter (g, rect (x + 22.0f, 56.0f, 58.0f, 310.0f), first, 60.0f, { 0, 6, 12, 18, 24, 30, 36, 42, 48, 54, 60 }, true);
        }
        text (g, rect (0.0f, 374.0f, 100.0f, 16.0f), caps (tr ("High pass")), 8.5f, false, panelText, juce::Justification::centred, 0.03f);
        text (g, rect (876.0f, 374.0f, 100.0f, 16.0f), caps (tr ("Mix")), 8.5f, false, panelText, juce::Justification::centred, 0.03f);

        // PHASE CORRECTION
        text (g, rect (152.0f, 80.0f, 240.0f, 16.0f), caps (tr ("Phase correction")), 11.0f, false, panelText, juce::Justification::centredLeft, 0.03f);
        text (g, rect (118.0f, 111.0f, 97.0f, 14.0f), caps (tr ("Align to")), 7.0f, false, dimText);
        text (g, rect (232.0f, 111.0f, 239.0f, 14.0f), caps (tr ("Correlation")), 7.0f, false, dimText);
        {
            const auto bar = rect (232.0f, 129.0f, 239.0f, 25.0f);
            screenBox (g, bar);
            text (g, bar.withWidth (16.0f).translated (3.0f, 0.0f), "-1", 6.0f, false, juce::Colour (0xff7fe3cf).withAlpha (0.6f));
            text (g, bar.withTrimmedLeft (bar.getWidth() - 19.0f), "+1", 6.0f, false, juce::Colour (0xff7fe3cf).withAlpha (0.6f));
            const auto x = bar.getX() + 18.0f + (bar.getWidth() - 36.0f) * juce::jlimit (0.0f, 1.0f, fx.enabled ? corrShown : 0.5f);
            g.setColour (juce::Colour (0xff7fe3cf));
            g.fillRect (x - 1.5f, bar.getY() + 4.0f, 3.0f, bar.getHeight() - 8.0f);
        }
        recess (g, rect (118.0f, 165.0f, 168.0f, 189.0f), juce::Colour (0xff3c3c3e));
        recess (g, rect (302.0f, 165.0f, 168.0f, 189.0f), juce::Colour (0xff3c3c3e));
        text (g, rect (118.0f, 172.0f, 168.0f, 16.0f), caps (tr ("Delay")), 9.5f, false, panelText, juce::Justification::centred, 0.04f);
        text (g, rect (302.0f, 172.0f, 168.0f, 16.0f), caps (tr ("All-pass")), 9.5f, false, panelText, juce::Justification::centred, 0.04f);
        // the delay's nudge arcs: grey segments either side of the time dial, a minus / plus on each
        {
            const auto c = at (202.0f, 268.0f);
            for (int i = 0; i < 4; ++i)
            {
                const bool left = i < 2;
                const auto outer = i == 0 || i == 3;
                const auto rr = outer ? 46.0f : 33.0f;
                juce::Path arc;
                const auto a0 = left ? -juce::MathConstants<float>::halfPi - 0.42f : juce::MathConstants<float>::halfPi - 0.42f;
                arc.addCentredArc (c.x, c.y, rr, rr, 0.0f, a0, a0 + 0.84f, true);
                const bool hot = nudgeRect (i).contains (mouse);
                g.setColour (juce::Colour (hot ? 0xffd9dadc : 0xffbfc1c4));
                g.strokePath (arc, juce::PathStrokeType (outer ? 10.0f : 7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
                text (g, nudgeRect (i), left ? "-" : "+", outer ? 9.5f : 7.5f, true, juce::Colour (0xff1d1e21));
            }
        }
        {
            // the delay in samples (at 48 kHz, as the read-out of the original)
            const auto box = rect (168.0f, 319.0f, 67.0f, 24.0f);
            screenBox (g, box);
            const auto smp = (int) std::lround (f::delayMs (v (f::delay)) * 48.0);
            text (g, box, juce::String (smp) + " " + tr ("smp"), 8.5f, true, juce::Colour (0xff7fe3cf));
        }
        phaseScreen (g, rect (313.0f, 231.0f, 148.0f, 63.0f));
        text (g, rect (313.0f, 300.0f, 58.0f, 14.0f), caps (tr ("Freq")), 8.0f, false, panelText);
        text (g, rect (403.0f, 300.0f, 58.0f, 14.0f), "Q", 8.0f, false, panelText);

        // EQUALISER
        text (g, rect (540.0f, 80.0f, 160.0f, 16.0f), caps (tr ("Equaliser")), 11.0f, false, panelText, juce::Justification::centredLeft, 0.03f);
        {
            // the listen lamp: lit while the EQ is post (it hears the compressor first)
            const auto c = at (739.0f, 88.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (c));
            g.setColour (v (f::eqPrePost) >= 0.5f ? juce::Colour (0xff4fd6a8) : juce::Colour (0xff1f3a33));
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (c));
        }
        recess (g, rect (561.0f, 115.0f, 152.0f, 137.0f), juce::Colour (0xff595a5e));
        recess (g, rect (795.0f, 115.0f, 64.0f, 137.0f), juce::Colour (0xff595a5e));
        text (g, rect (517.0f, 134.0f, 40.0f, 14.0f), "LF", 8.5f, false, dimText);
        text (g, rect (647.0f, 134.0f, 40.0f, 14.0f), "MF", 8.5f, false, dimText);
        text (g, rect (723.0f, 134.0f, 40.0f, 14.0f), "HF", 8.5f, false, dimText);
        text (g, rect (795.0f, 210.0f, 64.0f, 30.0f), caps (tr ("Low\npass")), 8.5f, false, dimText);
        eqScreen (g, rect (504.0f, 272.0f, 356.0f, 82.0f), fx);

        // COMP
        text (g, rect (152.0f, 399.0f, 60.0f, 16.0f), caps (tr ("Comp")), 12.0f, false, panelText, juce::Justification::centredLeft, 0.03f);
        recess (g, rect (205.0f, 373.0f, 90.0f, 162.0f), juce::Colour (0xff313235));
        text (g, rect (210.0f, 377.0f, 80.0f, 14.0f), caps (tr ("Mode")), 8.0f, false, dimText);
        text (g, rect (205.0f, 427.0f, 90.0f, 14.0f), caps (tr ("Reduction")), 8.0f, false, dimText);
        {
            const auto m = rect (222.0f, 443.0f, 55.0f, 84.0f);
            screenBox (g, m);
            for (const auto db : { 5, 10, 15 })
            {
                const auto y = m.getY() + 6.0f + (float) db / 20.0f * (m.getHeight() - 12.0f);
                text (g, { m.getRight() - 22.0f, y - 5.0f, 20.0f, 10.0f }, "-" + juce::String (db), 6.0f, false, juce::Colour (0xff7fe3cf).withAlpha (0.6f));
            }
            text (g, { m.getX() + 3.0f, m.getBottom() - 12.0f, 16.0f, 10.0f }, "dB", 5.5f, false, juce::Colour (0xff7fe3cf).withAlpha (0.5f));
            const auto h = juce::jlimit (0.0f, 1.0f, (fx.enabled ? grShown : 0.0f) / 20.0f) * (m.getHeight() - 12.0f);
            g.setColour (juce::Colour (0xff7fe3cf));
            g.fillRect (m.getX() + 8.0f, m.getY() + 6.0f, 14.0f, h);
        }

        // DRIVE
        text (g, rect (443.0f, 399.0f, 60.0f, 16.0f), caps (tr ("Drive")), 12.0f, false, panelText, juce::Justification::centredLeft, 0.03f);
        recess (g, rect (557.0f, 373.0f, 231.0f, 162.0f), juce::Colour (0xff313235));
        text (g, rect (622.0f, 377.0f, 102.0f, 14.0f), caps (tr ("Model")), 8.0f, false, dimText);

        for (const auto& b : buttons)
            drawButton (g, b);
        // the polarity key's mark
        {
            const auto r = rect (222.0f, 193.0f, 46.0f, 28.0f);
            const auto c = r.getCentre();
            g.setColour (juce::Colour (0xff1d1e21));
            g.drawEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (c), 1.6f);
            g.drawLine (c.x - 6.0f, c.y + 8.0f, c.x + 6.0f, c.y - 8.0f, 1.6f);
        }
        for (const auto& k : knobs)
            drawKnob (g, k);
    }
};

// ===================================================================================================== Shell Channel
// The drum channel strip: the input column (meter, gain), the gate meter (open / close marks on the input, the
// reduction, the range), GATE (open, close, range, hold, attack, release), the TRANSIENT SHAPER (invert, listen, speed,
// gain, amount, its meter), the LOW FREQ and HIGH FREQ ENHANCERS, the LISTEN MIC COMPRESSOR, the process order (drag a
// module along the row) and the output column (meter, mix, gain)
class ShellEditor final : public StripPanel
{
public:
    ShellEditor (AudioEngine& e, int t, int s, bool d) : StripPanel (e, t, s, EffectKind::shellChannel, d, 1000, (int) (headerH + 500.0f + barH))
    {
        namespace s_ = mixsuite::shell;
        captionBelow = true;
        knobs = {
            { s_::input, 55, 412, 25, Cap::black, "Gain", "-20", "+20", nullptr },
            { s_::outputMix, 945, 330, 19.5, Cap::silver, "Mix", "Dry", "Wet", nullptr },
            { s_::output, 945, 420, 19.5, Cap::silver, "Gain", "-20", "+20", nullptr },
            { s_::gateOpen, 318, 98, 21, Cap::silver, "Open", "-30", "0", nullptr },
            { s_::gateClose, 318, 186, 21, Cap::silver, "Close", "-30", "0", nullptr },
            { s_::gateRange, 392, 130, 21, Cap::silver, "Range", "0", "80", nullptr },
            { s_::gateHold, 467, 130, 21, Cap::green, "Hold", "0", "4", nullptr },
            { s_::gateAttack, 543, 98, 21, Cap::green, "Attack", "0", "0.1", nullptr },
            { s_::gateRelease, 543, 186, 21, Cap::green, "Release", "0.01", "1", nullptr },
            { s_::tsSpeed, 725, 130, 21, Cap::green, "Speed", "0", "1", nullptr },
            { s_::tsGain, 798, 98, 21, Cap::black, "Gain", "-20", "+20", nullptr },
            { s_::tsAmount, 798, 186, 21, Cap::silver, "Amount", "0", "100", nullptr },
            { s_::lfAmount, 163, 308, 21, Cap::silver, "Amount", "0", "100", nullptr },
            { s_::lfFreq, 233, 354, 21, Cap::blue, "Hz", "20", "250", nullptr },
            { s_::lfDrive, 303, 354, 21, Cap::silver, "Drive", "0", "10", nullptr },
            { s_::hfDrive, 405, 354, 21, Cap::silver, "Drive", "0", "10", nullptr },
            { s_::hfFreq, 474, 354, 21, Cap::red, "kHz", "2", "20", nullptr },
            { s_::hfAmount, 543, 308, 21, Cap::silver, "Amount", "0", "100", nullptr },
            { s_::lmcComp, 700, 354, 21, Cap::green, "Comp", "0", "100", nullptr },
            { s_::lmcMakeup, 770, 354, 21, Cap::silver, "Makeup gain", "-20", "+20", nullptr },
            { s_::lmcMix, 838, 308, 21, Cap::silver, "Mix", "Dry", "Wet", nullptr },
        };
        buttons = {
            { -1, { 123, 13, 28, 28 }, Key::power, nullptr },
            { s_::gateOn, { 442, 217, 20, 20 }, Key::power, nullptr },
            { s_::tsInvert, { 626, 103, 46, 27 }, Key::text, TR_NOOP ("Invert") },
            { s_::tsListen, { 626, 152, 46, 27 }, Key::text, TR_NOOP ("Listen") },
            { s_::tsOn, { 628, 217, 20, 20 }, Key::power, nullptr },
            { s_::lfOn, { 303, 288, 20, 20 }, Key::power, nullptr },
            { s_::hfOn, { 383, 288, 20, 20 }, Key::power, nullptr },
            { s_::lmcOn, { 628, 288, 20, 20 }, Key::power, nullptr },
            { s_::lmcNarrow, { 628, 350, 20, 20 }, Key::text, nullptr },
        };
    }

private:
    static constexpr int idChevron = idExtra; // + 0..4: the process order's slots
    juce::String title() const override { return "Shell Channel"; }
    int stepsOf (int p) const override { return p == mixsuite::shell::processOrder ? 119 : 0; }

    juce::Rectangle<float> chevronRect (int i) const { return rect (182.0f + 130.0f * (float) i, 455.0f, 133.0f, 25.0f); }
    void extraControls (std::vector<Ctl>& out) const override
    {
        for (int i = 0; i < 5; ++i)
            out.push_back (custom (chevronRect (i), idChevron + i, -1, tr ("Process order: drag a module left or right to move it")));
    }
    int dragFrom = -1, dragTo = -1;
    void extraDown (const Ctl& c, const juce::MouseEvent&) override
    {
        dragFrom = c.id - idChevron;
        dragTo = dragFrom;
    }
    void extraDrag (const Ctl&, const juce::MouseEvent& e) override
    {
        if (dragFrom < 0) return;
        int to = dragFrom;
        for (int i = 0; i < 5; ++i)
            if (e.position.x >= chevronRect (i).getX() && e.position.x < chevronRect (i).getRight()) to = i;
        if (e.position.x < chevronRect (0).getX()) to = 0;
        if (e.position.x >= chevronRect (4).getRight()) to = 4;
        dragTo = to;
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        namespace s_ = mixsuite::shell;
        if (dragFrom >= 0 && dragTo >= 0 && dragTo != dragFrom)
        {
            auto order = s_::orderOf (s_::orderIndex (v (s_::processOrder)));
            const auto moved = order[(std::size_t) dragFrom];
            std::vector<int> rest;
            for (int i = 0; i < 5; ++i)
                if (i != dragFrom) rest.push_back (order[(std::size_t) i]);
            rest.insert (rest.begin() + dragTo, moved);
            for (int i = 0; i < 5; ++i)
                order[(std::size_t) i] = rest[(std::size_t) i];
            edit (s_::processOrder, s_::orderValue (s_::indexOf (order)));
        }
        dragFrom = dragTo = -1;
        StripPanel::mouseUp (e);
    }

    float grShown = 0.0f, liftShown = 0.0f, gateIn = -120.0f;
    void tick() override
    {
        bool moving = tickBars();
        const auto* fx = current();
        const auto on = fx != nullptr && fx->enabled;
        const auto gr = on ? liveMeter (1) * 80.0f : 0.0f, lift = on ? liveMeter (0) * 20.0f : 0.0f;
        const auto in = on ? meterDb (2) : -120.0f;
        moving = moving || std::abs (gr - grShown) > 0.05f || std::abs (lift - liftShown) > 0.05f || std::abs (in - gateIn) > 0.1f;
        grShown += (gr - grShown) * 0.4f;
        liftShown += (lift - liftShown) * 0.4f;
        gateIn = in > gateIn ? in : juce::jmax (-120.0f, gateIn - 1.5f);
        if (moving) repaint();
    }

    void verticalCaption (juce::Graphics& g, juce::Point<float> c, const juce::String& t, juce::Colour col) const
    {
        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, c.x, c.y));
        text (g, juce::Rectangle<float> (180.0f, 10.0f).withCentre (c), t, 6.5f, true, col, juce::Justification::centred, 0.06f);
        g.restoreState();
    }

    void paintBody (juce::Graphics& g, const EffectSlot&) override
    {
        namespace s_ = mixsuite::shell;
        const auto body = bodyArea();
        g.setColour (juce::Colour (0xff1c1d1f));
        g.fillRect (body);
        // the side columns
        for (const auto x : { 0.0f, 890.0f })
        {
            const auto col = rect (x, 0.0f, 110.0f, 498.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2b2e), 0.0f, col.getY(), juce::Colour (0xff1e1f21), 0.0f, col.getBottom(), false));
            g.fillRect (col);
        }
        topBand (g, rect (110.0f, 0.0f, 780.0f, 52.0f), "Shell Channel", juce::Colour (0xffb9babd), juce::Colour (0xff222326), juce::Colour (0xff222326));
        // the modules
        const auto face = [&g] (juce::Rectangle<float> r) {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b3c3f), 0.0f, r.getY(), juce::Colour (0xff303134), 0.0f, r.getBottom(), false));
            g.fillRect (r);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawRect (r, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 1.0f, r.getRight() - 1.0f);
        };
        face (rect (265.0f, 52.0f, 332.0f, 213.0f));
        face (rect (597.0f, 52.0f, 255.0f, 213.0f));
        face (rect (110.0f, 265.0f, 243.0f, 168.0f));
        face (rect (353.0f, 265.0f, 244.0f, 168.0f));
        face (rect (597.0f, 265.0f, 293.0f, 168.0f));
        g.setColour (juce::Colour (0xff141517));
        g.fillRect (rect (110.0f, 52.0f, 155.0f, 213.0f));
        g.fillRect (rect (852.0f, 52.0f, 38.0f, 213.0f));
        g.fillRect (rect (110.0f, 433.0f, 780.0f, 65.0f));

        // the columns
        text (g, rect (0.0f, 16.0f, 110.0f, 20.0f), caps (tr ("Input")), 9.5f, true, panelText, juce::Justification::centred, 0.05f);
        text (g, rect (890.0f, 16.0f, 110.0f, 20.0f), caps (tr ("Output")), 9.5f, true, panelText, juce::Justification::centred, 0.05f);
        stereoMeter (g, rect (30.0f, 46.0f, 40.0f, 216.0f), 0, 30.0f, { 0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 30 }, false);
        stereoMeter (g, rect (920.0f, 46.0f, 40.0f, 216.0f), 2, 30.0f, { 0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 30 }, false);
        text (g, rect (35.0f, 352.0f, 40.0f, 12.0f), "0", 7.0f, false, panelText);

        // the gate meter: the input with the open (green) and close (orange) marks, the reduction, the range
        {
            const auto top = y0 + 66.0f, h = 180.0f;
            auto yOf = [top, h] (double db) { return top + (float) juce::jlimit (0.0, 1.0, -db / 60.0) * h; };
            const auto inBar = rect (157.0f, 66.0f, 20.0f, h);
            g.setColour (juce::Colour (0xff0d2622));
            g.fillRect (inBar);
            if (gateIn > -60.0f)
            {
                const auto y = yOf (gateIn);
                g.setColour (juce::Colour (0xff3fae86));
                g.fillRect (juce::Rectangle<float> (inBar.getX() + 2.0f, y, inBar.getWidth() - 4.0f, inBar.getBottom() - y));
            }
            const auto openY = yOf (s_::thresholdDb (v (s_::gateOpen))), closeY = yOf (s_::thresholdDb (v (s_::gateClose)));
            g.setColour (juce::Colour (0xff3fd08f));
            g.fillRect (138.0f, openY - 1.0f, 40.0f, 2.5f);
            g.setColour (juce::Colour (0xffe0742a));
            g.fillRect (138.0f, closeY - 1.0f, 40.0f, 2.5f);
            verticalCaption (g, at (130.0f, 156.0f), caps (tr ("Open / close")), dimText);
            verticalCaption (g, at (190.0f, 156.0f), caps (tr ("Gate input")), dimText);
            const auto grBar = rect (208.0f, 66.0f, 13.0f, h);
            g.setColour (juce::Colour (0xff2a1d14));
            g.fillRect (grBar);
            g.setColour (juce::Colour (0xffb8642a));
            g.fillRect (grBar.withHeight (juce::jlimit (0.0f, h, grShown / 80.0f * h)));
            verticalCaption (g, at (232.0f, 156.0f), caps (tr ("Gain reduction")), dimText);
            const auto rangeBar = rect (240.0f, 66.0f, 10.0f, h);
            g.setColour (juce::Colour (0xff2b2c2f));
            g.fillRect (rangeBar);
            g.setColour (juce::Colour (0xff7d7f83));
            g.fillRect (rangeBar.withHeight ((float) (s_::rangeDb (v (s_::gateRange)) / 80.0) * h));
            verticalCaption (g, at (258.0f, 156.0f), caps (tr ("Range")), dimText);
        }
        text (g, rect (380.0f, 219.0f, 56.0f, 16.0f), caps (tr ("Gate")), 11.0f, true, panelText, juce::Justification::centredRight, 0.04f);
        text (g, rect (656.0f, 213.0f, 140.0f, 28.0f), caps (tr ("Transient\nshaper")), 10.5f, true, panelText, juce::Justification::centredLeft, 0.03f);
        // the shaper's meter: its lift
        {
            const auto m = rect (866.0f, 66.0f, 9.0f, 184.0f);
            g.setColour (juce::Colour (0xff0d2622));
            g.fillRect (m);
            const auto h = juce::jlimit (0.0f, 1.0f, liftShown / 20.0f) * m.getHeight();
            g.setColour (juce::Colour (0xff3fae86));
            g.fillRect (m.withTop (m.getBottom() - h));
        }
        text (g, rect (196.0f, 286.0f, 104.0f, 28.0f), caps (tr ("Low freq\nenhancer")), 10.5f, true, panelText, juce::Justification::centredRight, 0.03f);
        text (g, rect (410.0f, 286.0f, 110.0f, 28.0f), caps (tr ("High freq\nenhancer")), 10.5f, true, panelText, juce::Justification::centredLeft, 0.03f);
        text (g, rect (656.0f, 286.0f, 140.0f, 28.0f), caps (tr ("Listen mic\ncompressor")), 10.5f, true, panelText, juce::Justification::centredLeft, 0.03f);
        text (g, rect (603.0f, 380.0f, 70.0f, 24.0f), caps (tr ("Narrow\nband")), 7.0f, true, panelText);

        // the process order: five chevrons, the dragged one follows the mouse's slot
        {
            auto order = s_::orderOf (s_::orderIndex (v (s_::processOrder)));
            std::array<int, 5> shown = order;
            if (dragFrom >= 0 && dragTo >= 0 && dragTo != dragFrom)
            {
                std::vector<int> rest;
                for (int i = 0; i < 5; ++i)
                    if (i != dragFrom) rest.push_back (order[(std::size_t) i]);
                rest.insert (rest.begin() + dragTo, order[(std::size_t) dragFrom]);
                for (int i = 0; i < 5; ++i) shown[(std::size_t) i] = rest[(std::size_t) i];
            }
            static const char* names[5] { TR_NOOP ("Gate"), TR_NOOP ("Transient\nshaper"), TR_NOOP ("Low freq\nenhancer"), TR_NOOP ("High freq\nenhancer"),
                                          TR_NOOP ("Listen mic\ncompressor") };
            const bool ons[5] { v (s_::gateOn) >= 0.5f, v (s_::tsOn) >= 0.5f, v (s_::lfOn) >= 0.5f, v (s_::hfOn) >= 0.5f, v (s_::lmcOn) >= 0.5f };
            for (int i = 0; i < 5; ++i)
            {
                const auto r = chevronRect (i);
                juce::Path p;
                p.startNewSubPath (r.getX(), r.getY());
                p.lineTo (r.getRight() - 12.0f, r.getY());
                p.lineTo (r.getRight(), r.getCentreY());
                p.lineTo (r.getRight() - 12.0f, r.getBottom());
                p.lineTo (r.getX(), r.getBottom());
                p.lineTo (r.getX() + 12.0f, r.getCentreY());
                p.closeSubPath();
                const auto m = shown[(std::size_t) i];
                const bool hot = r.contains (mouse) || (dragTo == i && dragFrom >= 0);
                g.setColour (hot ? juce::Colour (0xff3a3b3f) : juce::Colour (0xff242528));
                g.fillPath (p);
                g.setColour (juce::Colour (ons[(std::size_t) m] ? 0xff8d8f93 : 0xff4a4c50));
                g.strokePath (p, juce::PathStrokeType (1.2f));
                const auto lines = juce::StringArray::fromTokens (caps (tr (names[(std::size_t) m])), "\n", "");
                auto ty = r.getCentreY() - 4.5f * (float) lines.size();
                for (const auto& line : lines)
                {
                    text (g, { r.getX() + 12.0f, ty, r.getWidth() - 24.0f, 9.0f }, line, 7.5f, true,
                          ons[(std::size_t) m] ? juce::Colour (0xffd9dadc) : juce::Colour (0xff6f7175), juce::Justification::centred, 0.04f);
                    ty += 9.0f;
                }
                // a small play mark at the right end, as a step in the chain
                juce::Path tri;
                const auto tc = juce::Point<float> (r.getRight() - 17.0f, r.getCentreY());
                tri.addTriangle (tc.x - 2.5f, tc.y - 3.5f, tc.x - 2.5f, tc.y + 3.5f, tc.x + 3.0f, tc.y);
                g.setColour (juce::Colour (0xff6f7175));
                g.fillPath (tri);
            }
        }

        for (const auto& b : buttons)
            drawButton (g, b);
        {
            // the narrow band key's mark: a band-pass hump
            const auto r = rect (628.0f, 350.0f, 20.0f, 20.0f);
            juce::Path hump;
            hump.startNewSubPath (r.getX() + 4.0f, r.getBottom() - 5.0f);
            hump.lineTo (r.getX() + 6.0f, r.getY() + 6.0f);
            hump.lineTo (r.getRight() - 6.0f, r.getY() + 6.0f);
            hump.lineTo (r.getRight() - 4.0f, r.getBottom() - 5.0f);
            g.setColour (v (s_::lmcNarrow) >= 0.5f ? juce::Colour (0xffe0742a) : juce::Colour (0xff1d1e21));
            g.strokePath (hump, juce::PathStrokeType (1.5f));
        }
        for (const auto& k : knobs)
            drawKnob (g, k);
    }
};

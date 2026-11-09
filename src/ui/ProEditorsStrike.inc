// Round trans189d (issue #94): Strike Shaper's editor — the two-knob transient designer's hardware mood, drawn here
// from scratch (no bitmaps, logos or fonts of any original): two narrow rack modules side by side, each a brushed
// silver frame with slotted screws round a black capsule. The left module is the unit itself: SIG / OVL lamps,
// ATTACK and SUSTAIN on their printed dB scales, ON and LINK lit keys, OUTPUT GAIN. The right module carries LYNX's
// name plate with its own strike mark, the two activity rows (how far the attack and sustain gains move, live), the
// detector's high-pass, GUARD and the parallel MIX, and the power lamp. Included by ui/ProEditors.cpp.

class StrikeEditor final : public ProEditor
{
public:
    StrikeEditor (AudioEngine& e, int t, int s, bool d) : ProEditor (e, t, s, EffectKind::strikeShaper, d, 580, 736) {}

private:
    static constexpr int idParam = 100, idOn = 200, idPower = 201;
    static constexpr float y0 = headerH;

    juce::String title() const override { return "Strike Shaper"; }

    static juce::Point<float> at (float x, float y) { return { x, y0 + y }; }
    static juce::Point<float> knobCentre (int p)
    {
        namespace s = strike;
        switch (p)
        {
            case s::attack: return at (149.0f, 172.0f);
            case s::sustain: return at (149.0f, 322.0f);
            case s::output: return at (149.0f, 530.0f);
            case s::detectHp: return at (431.0f, 342.0f);
            case s::mix: return at (431.0f, 520.0f);
            default: return {};
        }
    }
    static float knobRadius (int p)
    {
        namespace s = strike;
        return p == s::output ? 44.0f : p == s::mix ? 36.0f : p == s::detectHp ? 26.0f : 38.0f;
    }
    static juce::Rectangle<float> keyRect (int id) // ON, LINK, GUARD
    {
        if (id == idOn) return juce::Rectangle<float> (26.0f, 26.0f).withCentre (at (106.0f, 420.0f));
        if (id == idParam + strike::link) return juce::Rectangle<float> (26.0f, 26.0f).withCentre (at (192.0f, 420.0f));
        return juce::Rectangle<float> (26.0f, 26.0f).withCentre (at (431.0f, 432.0f)); // GUARD
    }
    static juce::Point<float> powerCentre() { return at (431.0f, 606.0f); }
    static float angleOf (float value) { return juce::MathConstants<float>::pi * (-0.75f + 1.5f * value); }
    static juce::Point<float> polar (juce::Point<float> c, float a, float r) { return c + juce::Point<float> { std::sin (a), -std::cos (a) } * r; }

    void layoutControls (std::vector<Ctl>& out) const override
    {
        namespace s = strike;
        for (const auto p : { s::attack, s::sustain, s::output, s::detectHp, s::mix })
        {
            const auto r = knobRadius (p) + 10.0f;
            out.push_back (custom (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (knobCentre (p)), idParam + p, -1, hintFor (p)));
        }
        out.push_back (custom (keyRect (idOn).expanded (30.0f, 6.0f).withTrimmedRight (30.0f), idOn, -1, tr ("Power: switch the effect on or off")));
        out.push_back (custom (keyRect (idParam + s::link).expanded (30.0f, 6.0f).withTrimmedLeft (30.0f), idParam + s::link, -1, hintFor (s::link)));
        out.push_back (custom (keyRect (idParam + s::guard).expanded (6.0f, 20.0f).withTrimmedBottom (14.0f), idParam + s::guard, -1, hintFor (s::guard)));
        out.push_back (custom (juce::Rectangle<float> (30.0f, 40.0f).withCentre (powerCentre().translated (0.0f, -6.0f)), idPower, -1,
                               tr ("Power: switch the effect on or off")));
    }

    juce::String hintFor (int p) const
    {
        const auto& info = effectInfo (kind);
        const auto name = tr (info.params[(std::size_t) p].name);
        const auto what = pluginheader::describe (kind, info.params[(std::size_t) p].name);
        return what.isNotEmpty() ? name + juce::String::fromUTF8 (" \xe2\x80\x94 ") + what : name;
    }

    // --- edits
    int dragParam = -1;
    bool dragging (int p) const { return dragParam == p && isMouseButtonDown(); }
    void customDown (const Ctl& c, const juce::MouseEvent& e) override
    {
        namespace s = strike;
        dragParam = -1;
        if (c.id == idOn || c.id == idPower)
        {
            toggleEnabled();
            return;
        }
        if (c.id < idParam) return;
        const auto p = c.id - idParam;
        if (e.mods.isPopupMenu())
        {
            ui::showValueMenu (*this, &engine, specFor (p), v (p), [this, p] (double x) { set ({ { p, (float) x } }); });
            return;
        }
        if (p == s::link || p == s::guard)
        {
            set ({ { p, v (p) >= 0.5f ? 0.0f : 1.0f } });
            return;
        }
        if (ui::isResetClick (e))
        {
            set ({ { p, (float) specFor (p).defaultValue } });
            return;
        }
        dragParam = p;
        valueDrag.begin (e, v (p));
    }
    void customDrag (const Ctl& c, const juce::MouseEvent& e) override
    {
        if (dragParam < 0) return;
        const auto spec = specFor (dragParam);
        set ({ { dragParam, (float) valueDrag.update (e, spec) } });
        ui::showValueTip (*this, c.r.toNearestInt(), spec.text (v (dragParam)));
    }
    bool bodyWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        Ctl c;
        if (! controlAt (e.position, c) || c.id < idParam) return false;
        const auto p = c.id - idParam;
        if (knobRadius (p) <= 0.0f || knobCentre (p).y <= 0.0f || p == strike::link || p == strike::guard) return false;
        const auto spec = specFor (p);
        if (const auto x = ui::wheelValue (v (p), e, wheel, spec))
        {
            set ({ { p, (float) *x } });
            ui::flashValueTip (*this, c.r.toNearestInt(), spec.text (v (p)));
        }
        return true;
    }

    // --- live state: the attack / sustain moves (dB, signed, held a moment), the SIG / OVL lamps
    float attackShown = 0.0f, sustainShown = 0.0f, sigLevel = 0.0f, ovlHold = 0.0f;
    void tick() override
    {
        const auto* fx = current();
        const auto on = fx != nullptr && fx->enabled;
        auto fallToward = [] (float shown, float now) { return std::abs (now) >= std::abs (shown) ? now : shown * 0.82f; };
        const auto a = on ? (liveMeter (0) - 0.5f) * 48.0f : 0.0f, s = on ? (liveMeter (1) - 0.5f) * 48.0f : 0.0f;
        const auto na = fallToward (attackShown, a), ns = fallToward (sustainShown, s);
        const auto sig = on ? liveMeter (2) : 0.0f, out = on ? liveMeter (3) : 0.0f;
        const auto novl = out >= 0.989f ? 1.0f : ovlHold * 0.9f;
        const bool moved = std::abs (na - attackShown) > 0.05f || std::abs (ns - sustainShown) > 0.05f
                           || (sig > 0.01f) != (sigLevel > 0.01f) || std::abs (novl - ovlHold) > 0.02f;
        attackShown = na;
        sustainShown = ns;
        sigLevel = sig;
        ovlHold = novl;
        if (moved) repaint();
    }

    // --- drawing --------------------------------------------------------------------------------------------
    static inline const juce::Colour kInk { 0xffe9eaec }, kDim { 0xff9a9ea6 }, kCapsule { 0xff141518 };
    static void text (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, float size, bool bold, juce::Colour c,
                      juce::Justification j = juce::Justification::centred, float kern = 0.0f)
    {
        g.setColour (c);
        g.setFont (juce::Font (theme::font (size, bold)).withExtraKerningFactor (kern));
        g.drawText (t, r, j, false);
    }

    static void screw (juce::Graphics& g, juce::Point<float> c, float r)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f + 4.0f, r * 2.0f + 4.0f).withCentre (c.translated (0.0f, 1.0f)));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f3f4), c.x - r, c.y - r, juce::Colour (0xff6e7177), c.x + r, c.y + r, false));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        g.setColour (juce::Colour (0xff3a3c40));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.0f);
        g.setColour (juce::Colour (0xff2a2b2e)); // a hex socket
        juce::Path hex;
        for (int i = 0; i < 6; ++i)
        {
            const auto p = polar (c, juce::MathConstants<float>::pi / 3.0f * (float) i + 0.3f, r * 0.48f);
            if (i == 0) hex.startNewSubPath (p);
            else hex.lineTo (p);
        }
        hex.closeSubPath();
        g.fillPath (hex);
    }

    // a module: the brushed silver frame (lit from the top), screws top and bottom, the black capsule inside
    static juce::Rectangle<float> capsuleOf (juce::Rectangle<float> frame) { return frame.reduced (20.0f, 50.0f); }
    void module (juce::Graphics& g, juce::Rectangle<float> frame, int seed) const
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (frame.translated (0.0f, 3.0f).expanded (1.0f), 16.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f5f7), frame.getX(), frame.getY(), juce::Colour (0xffa6a9ae), frame.getX(), frame.getBottom(), false));
        g.fillRoundedRectangle (frame, 15.0f);
        juce::Random rnd (seed);
        for (int i = 0; i < 900; ++i) // vertical brushing
        {
            const auto x = frame.getX() + 4.0f + rnd.nextFloat() * (frame.getWidth() - 8.0f);
            const auto y = frame.getY() + 8.0f + rnd.nextFloat() * (frame.getHeight() - 16.0f);
            g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.035f + 0.04f * rnd.nextFloat()));
            g.drawVerticalLine ((int) x, y, juce::jmin (frame.getBottom() - 8.0f, y + 12.0f + 50.0f * rnd.nextFloat()));
        }
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawRoundedRectangle (frame.reduced (1.0f), 14.0f, 1.2f);
        g.setColour (juce::Colour (0xff55585e));
        g.drawRoundedRectangle (frame, 15.0f, 1.0f);
        screw (g, { frame.getCentreX(), frame.getY() + 24.0f }, 10.0f);
        screw (g, { frame.getCentreX(), frame.getBottom() - 24.0f }, 10.0f);
        // the capsule: a bevel (dark above, light below), the black face with a soft top sheen
        const auto cap = capsuleOf (frame);
        const auto rr = cap.getWidth() * 0.5f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5d6066), cap.getX(), cap.getY(), juce::Colour (0xfff7f8fa), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap.expanded (5.0f), rr + 5.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff24262b), cap.getX(), cap.getY(), juce::Colour (0xff0b0c0e), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, rr);
        juce::Random grain (seed + 7);
        for (int i = 0; i < 500; ++i) // a fine horizontal texture on the black
        {
            const auto x = cap.getX() + 14.0f + grain.nextFloat() * (cap.getWidth() - 28.0f);
            const auto y = cap.getY() + 30.0f + grain.nextFloat() * (cap.getHeight() - 60.0f);
            g.setColour (juce::Colours::white.withAlpha (0.018f));
            g.drawHorizontalLine ((int) y, x, juce::jmin (cap.getRight() - 14.0f, x + 20.0f + 40.0f * grain.nextFloat()));
        }
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (cap, rr, 1.5f);
    }

    // a machined silver knob with a black pointer line
    void silverKnob (juce::Graphics& g, juce::Point<float> c, float r, float value, bool hot) const
    {
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.16f));
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c.translated (0.0f, (float) i * 1.4f)).expanded ((float) i));
        }
        const auto body = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8a8d93), c.x, c.y - r, juce::Colour (0xff3c3e43), c.x, c.y + r, false));
        g.fillEllipse (body);
        const auto face = body.reduced (r * 0.08f);
        juce::ColourGradient sheen (juce::Colour (hot ? 0xffffffff : 0xfffafbfc), c.x - r * 0.35f, c.y - r * 0.45f, juce::Colour (0xff9fa3aa), c.x + r * 0.5f, c.y + r * 0.7f, true);
        sheen.addColour (0.55, juce::Colour (0xffd4d7dc));
        g.setGradientFill (sheen);
        g.fillEllipse (face);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawEllipse (face.reduced (1.0f), 1.0f);
        const auto a = angleOf (value);
        g.setColour (juce::Colour (0xff111214));
        g.drawLine ({ polar (c, a, r * 0.18f), polar (c, a, r * 0.98f) }, juce::jmax (2.0f, r * 0.07f));
    }

    // the printed dB scale: a dotted ring, the stops' numbers round it, "dB" under the knob
    void scale (juce::Graphics& g, juce::Point<float> c, float r, const std::vector<std::pair<float, juce::String>>& stops, int dots) const
    {
        for (int i = 0; i <= dots; ++i)
            g.setColour (kDim), g.fillEllipse (juce::Rectangle<float> (2.2f, 2.2f).withCentre (polar (c, angleOf ((float) i / (float) dots), r + 9.0f)));
        for (const auto& [pos, label] : stops)
        {
            const auto a = angleOf (pos);
            g.setColour (kInk);
            g.fillEllipse (juce::Rectangle<float> (3.4f, 3.4f).withCentre (polar (c, a, r + 9.0f)));
            text (g, juce::Rectangle<float> (26.0f, 12.0f).withCentre (polar (c, a, r + 22.0f)), label, 9.5f, true, kInk);
        }
        text (g, juce::Rectangle<float> (30.0f, 12.0f).withCentre (c.translated (0.0f, r + 18.0f)), "dB", 9.0f, true, kDim);
    }
    static std::vector<std::pair<float, juce::String>> stopsOf (const double* below, const double* above, bool skipHalf = false)
    {
        std::vector<std::pair<float, juce::String>> v;
        for (int i = 5; i >= 1; --i)
            v.push_back ({ 0.5f - 0.1f * (float) i, (i == 5 ? "-" : "") + juce::String (below[i], 0).replace (".0", "") });
        v.push_back ({ 0.5f, "0" });
        for (int i = 1; i <= 5; ++i)
            if (! (skipHalf && i == 4))
                v.push_back ({ 0.5f + 0.1f * (float) i, (i == 5 ? "+" : "") + juce::String (above[i], 0).replace (".0", "") });
        return v;
    }

    void litKey (juce::Graphics& g, juce::Rectangle<float> r, bool on, juce::Colour lit) const
    {
        g.setColour (juce::Colour (0xff050506));
        g.fillRoundedRectangle (r.expanded (4.0f), 4.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3c41), r.getX(), r.getY(), juce::Colour (0xff1a1b1e), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        const auto lens = r.reduced (6.0f);
        if (on)
        {
            g.setColour (lit.withAlpha (0.28f));
            g.fillRoundedRectangle (lens.expanded (7.0f), 6.0f);
            g.setGradientFill (juce::ColourGradient (lit.brighter (0.9f), lens.getCentreX(), lens.getCentreY(), lit, lens.getX(), lens.getY(), true));
        }
        else
            g.setGradientFill (juce::ColourGradient (lit.darker (2.4f), lens.getCentreX(), lens.getCentreY(), lit.darker (3.5f), lens.getX(), lens.getY(), true));
        g.fillRoundedRectangle (lens, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (on ? 0.6f : 0.12f));
        g.drawRoundedRectangle (lens, 2.0f, 0.8f);
    }

    void led (juce::Graphics& g, juce::Point<float> c, float r, bool on, juce::Colour lit) const
    {
        if (on)
        {
            g.setColour (lit.withAlpha (0.3f));
            g.fillEllipse (juce::Rectangle<float> (r * 4.0f, r * 4.0f).withCentre (c));
        }
        g.setColour (juce::Colour (0xff050506));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f + 3.0f, r * 2.0f + 3.0f).withCentre (c));
        g.setGradientFill (juce::ColourGradient (on ? lit.brighter (0.9f) : lit.darker (2.6f), c.x - r * 0.3f, c.y - r * 0.3f,
                                                 on ? lit : lit.darker (3.6f), c.x + r, c.y + r, true));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    }

    // LYNX's strike mark: a sharp attack spike over a decaying tail, inside a ring
    static void strikeMark (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col)
    {
        g.setColour (col);
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.6f);
        juce::Path p;
        p.startNewSubPath (c.x - r * 0.72f, c.y + r * 0.42f);
        p.lineTo (c.x - r * 0.38f, c.y + r * 0.42f);
        p.lineTo (c.x - r * 0.22f, c.y - r * 0.66f);
        p.quadraticTo (c.x + r * 0.02f, c.y + r * 0.30f, c.x + r * 0.72f, c.y + r * 0.40f);
        g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // an activity row: 5 LEDs each side of a centre one; lit up to the gain's |dB| (cut left, boost right)
    void activityRow (juce::Graphics& g, juce::Point<float> c, const juce::String& name, float db) const
    {
        static const float steps[5] { 1.0f, 3.0f, 6.0f, 10.0f, 15.0f };
        text (g, { c.x - 90.0f, c.y - 22.0f, 180.0f, 12.0f }, name, 9.5f, true, kInk, juce::Justification::centred, 0.08f);
        const juce::Colour cut (0xff3d9bff), boost (0xffff4a2e), mid (0xff42d77a);
        led (g, c, 4.0f, true, mid);
        for (int i = 0; i < 5; ++i)
        {
            const auto dx = 15.0f * (float) (i + 1);
            led (g, c.translated (-dx, 0.0f), 4.0f, db <= -steps[i], cut);
            led (g, c.translated (dx, 0.0f), 4.0f, db >= steps[i], boost);
        }
        text (g, { c.x - 92.0f, c.y + 7.0f, 24.0f, 10.0f }, "-15", 7.5f, false, kDim);
        text (g, { c.x + 68.0f, c.y + 7.0f, 24.0f, 10.0f }, "+15", 7.5f, false, kDim);
    }

    void hoverValue (juce::Graphics& g, int p) const
    {
        const auto c = knobCentre (p);
        const auto r = knobRadius (p);
        if (! juce::Rectangle<float> ((r + 10.0f) * 2.0f, (r + 10.0f) * 2.0f).withCentre (c).contains (mouse) || dragging (p)) return;
        const auto box = juce::Rectangle<float> (70.0f, 16.0f).withCentre (c.translated (0.0f, r * 0.55f));
        g.setColour (juce::Colour (0xe0101012));
        g.fillRoundedRectangle (box, 3.0f);
        text (g, box, ProEditor::text (p), 10.0f, false, kInk);
    }

    // the shared bottom bar carries nothing here: the rack's dark backdrop runs down to the description line
    void paintOverlay (juce::Graphics& g, const EffectSlot&) override
    {
        g.setColour (juce::Colour (0xff1b1c1f));
        g.fillRect (barArea());
    }

    void paintBody (juce::Graphics& g, const EffectSlot& fx) override
    {
        namespace s = strike;
        const auto body = bodyArea();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2c30), 0.0f, body.getY(), juce::Colour (0xff141518), 0.0f, body.getBottom(), false));
        g.fillRect (body);
        const auto left = juce::Rectangle<float> (12.0f, y0 + 8.0f, 274.0f, 648.0f);
        const auto right = juce::Rectangle<float> (294.0f, y0 + 8.0f, 274.0f, 648.0f);
        module (g, left, 189);
        module (g, right, 190);
        const auto enabled = fx.enabled;
        const auto demoLive = demo; // --plugui pictures: show the lamps as they look on a drum loop

        // left: the unit
        led (g, at (82.0f, 104.0f), 5.0f, enabled && (sigLevel > 0.01f || demoLive), juce::Colour (0xffffb21e));
        text (g, { 52.0f, y0 + 112.0f, 40.0f, 12.0f }, "SIG", 8.5f, true, kInk);
        led (g, at (216.0f, 104.0f), 5.0f, enabled && ovlHold > 0.5f, juce::Colour (0xffff2a1e));
        text (g, { 206.0f, y0 + 112.0f, 40.0f, 12.0f }, "OVL", 8.5f, true, kInk);
        for (const auto p : { s::attack, s::sustain, s::output })
        {
            const auto c = knobCentre (p);
            const auto r = knobRadius (p);
            const auto name = caps (tr (p == s::output ? "Output gain" : effectInfo (kind).params[(std::size_t) p].name));
            text (g, { c.x - 90.0f, c.y - r - 46.0f, 180.0f, 16.0f }, name, 13.0f, true, kInk, juce::Justification::centred, 0.06f);
            if (p == s::attack) scale (g, c, r, stopsOf (s::law::attackStops, s::law::attackStops), 30);
            else if (p == s::sustain) scale (g, c, r, stopsOf (s::law::sustainStops, s::law::sustainStops), 30);
            else scale (g, c, r, stopsOf (s::law::outputLow, s::law::outputHigh, true), 30);
            silverKnob (g, c, r, v (p), dragging (p));
        }
        litKey (g, keyRect (idOn), enabled, juce::Colour (0xff3fa6ff));
        text (g, { 50.0f, keyRect (idOn).getY(), 38.0f, 26.0f }, "ON", 11.0f, true, kInk, juce::Justification::centredRight);
        litKey (g, keyRect (idParam + s::link), v (s::link) >= 0.5f, juce::Colour (0xffffa323));
        text (g, { keyRect (idParam + s::link).getRight() + 8.0f, keyRect (idParam + s::link).getY(), 50.0f, 26.0f }, caps (tr ("Link")), 11.0f, true, kInk,
              juce::Justification::centredLeft);
        text (g, { 40.0f, y0 + 600.0f, 218.0f, 14.0f }, "STRIKE SHAPER", 10.5f, true, kInk, juce::Justification::centred, 0.18f);

        // right: the name plate, the activity rows, the detector, GUARD, MIX, power
        const auto plate = juce::Rectangle<float> (366.0f, y0 + 84.0f, 130.0f, 58.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (plate.translated (0.0f, 2.0f), 6.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e3035), plate.getX(), plate.getY(), juce::Colour (0xff17181b), plate.getX(), plate.getBottom(), false));
        g.fillRoundedRectangle (plate, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (plate.reduced (0.5f), 6.0f, 1.0f);
        strikeMark (g, { plate.getX() + 30.0f, plate.getCentreY() }, 17.0f, juce::Colour (0xffe8463a));
        text (g, { plate.getX() + 54.0f, plate.getY() + 8.0f, 72.0f, 22.0f }, "LYNX", 19.0f, true, kInk, juce::Justification::centredLeft, 0.12f);
        text (g, { plate.getX() + 55.0f, plate.getY() + 32.0f, 72.0f, 14.0f }, "AUDIO", 8.5f, false, kDim, juce::Justification::centredLeft, 0.4f);
        text (g, { 330.0f, y0 + 154.0f, 202.0f, 26.0f }, "Strike", 24.0f, true, kInk);
        text (g, { 330.0f, y0 + 180.0f, 202.0f, 16.0f }, "SHAPER", 11.0f, true, kDim, juce::Justification::centred, 0.5f);
        activityRow (g, at (431.0f, 228.0f), caps (tr ("Attack")), demoLive ? 7.0f : attackShown);
        activityRow (g, at (431.0f, 268.0f), caps (tr ("Sustain")), demoLive ? -3.5f : sustainShown);

        {
            const auto c = knobCentre (s::detectHp);
            const auto r = knobRadius (s::detectHp);
            text (g, { c.x - 90.0f, c.y - r - 30.0f, 180.0f, 14.0f }, caps (tr ("Detect high-pass")), 10.5f, true, kInk, juce::Justification::centred, 0.05f);
            for (int i = 0; i <= 20; ++i)
                g.setColour (kDim), g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre (polar (c, angleOf ((float) i / 20.0f), r + 8.0f)));
            text (g, juce::Rectangle<float> (30.0f, 12.0f).withCentre (polar (c, angleOf (0.0f), r + 20.0f)), caps (tr ("Off")), 8.5f, true, kInk);
            text (g, juce::Rectangle<float> (34.0f, 12.0f).withCentre (polar (c, angleOf (1.0f), r + 20.0f)), "1k", 8.5f, true, kInk);
            text (g, juce::Rectangle<float> (34.0f, 12.0f).withCentre (polar (c, angleOf (s::detectHpValueOf (140.0)), r + 20.0f)), "140", 8.5f, true, kInk);
            silverKnob (g, c, r, v (s::detectHp), dragging (s::detectHp));
        }
        litKey (g, keyRect (idParam + s::guard), v (s::guard) >= 0.5f, juce::Colour (0xffff3b2a));
        text (g, { 381.0f, keyRect (idParam + s::guard).getY() - 18.0f, 100.0f, 12.0f }, caps (tr ("Guard")), 10.5f, true, kInk, juce::Justification::centred, 0.05f);
        {
            const auto c = knobCentre (s::mix);
            const auto r = knobRadius (s::mix);
            text (g, { c.x - 90.0f, c.y - r - 34.0f, 180.0f, 14.0f }, caps (tr ("Mix")), 11.5f, true, kInk, juce::Justification::centred, 0.06f);
            for (int i = 0; i <= 20; ++i)
                g.setColour (kDim), g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre (polar (c, angleOf ((float) i / 20.0f), r + 8.0f)));
            text (g, juce::Rectangle<float> (34.0f, 12.0f).withCentre (polar (c, angleOf (0.0f), r + 22.0f)), caps (tr ("Dry")), 8.5f, true, kInk);
            text (g, juce::Rectangle<float> (34.0f, 12.0f).withCentre (polar (c, angleOf (1.0f), r + 22.0f)), caps (tr ("Wet")), 8.5f, true, kInk);
            silverKnob (g, c, r, v (s::mix), dragging (s::mix));
        }
        led (g, powerCentre(), 7.0f, enabled, juce::Colour (0xff4aa8ff));
        text (g, { 381.0f, powerCentre().y - 28.0f, 100.0f, 12.0f }, caps (tr ("Power")), 9.0f, true, kInk, juce::Justification::centred, 0.08f);

        for (const auto p : { s::attack, s::sustain, s::output, s::detectHp, s::mix })
            hoverValue (g, p);
    }
};

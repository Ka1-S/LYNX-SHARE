// The Pocket effects' editors (round 188i): FL Studio Mobile's own module panels for LYNX's Pocket engines
// (core/PocketFx.h), in that app's layout and mood: one flat slate card, orange ring knobs with the value inside, slim
// sliders with a round handle, option lists and step tabs, the selected entry in white. Every pixel is drawn here by
// LYNX code; no bitmap, font or logo of the original is used. Positions are given in the reference panel's units
// (a 482-unit-wide card) and drawn at `kScale`.

#ifndef TR_NOOP
#define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py)
#endif

class PocketPanel : public ProEditor
{
public:
    PocketPanel (AudioEngine& e, int t, int s, EffectKind k, bool d, float cardH)
        : ProEditor (e, t, s, k, d, (int) (kCardW * kScale), (int) (headerH + (cardH + 2.0f * kPad) * kScale + barH))
    {
    }

protected:
    static constexpr float kCardW = 482.0f, kPad = 10.0f, kScale = 1.4f;
    static constexpr int idParam = 100, idOption = 1000, idPad = 2000;
    static juce::Colour cBack() { return juce::Colour (0xff21272b); }
    static juce::Colour cCard() { return juce::Colour (0xff394046); }
    static juce::Colour cTrack() { return juce::Colour (0xff31373b); }
    static juce::Colour cLine() { return juce::Colour (0xff434c51); }
    static juce::Colour cOrange() { return juce::Colour (0xfff6764c); }
    static juce::Colour cText() { return juce::Colour (0xffc7ceda); }
    static juce::Colour cDeep() { return juce::Colour (0xff13191d); }

    enum class T { ring, slider, list, tabs, pad, stepper };
    static constexpr int kPage = 1000; // an El's p: the editor's page (tabs), not a parameter
    int page = 0;
    virtual void pageChanged() {}
    struct El
    {
        T t = T::ring;
        int p = -1;
        juce::Rectangle<float> r;      // reference units, the card's top-left = 0, 0
        const char* caption = nullptr; // over the control
        const char* options = nullptr; // list / tabs: '|' separated
        std::vector<float> values;     // list / tabs: the value each option stores (empty: i / (n − 1))
        bool bipolar = false;          // sliders: the fill starts at the centre
        bool valueBelow = false;       // sliders: the value printed under the slider
        int py = -1;                   // pad: the Y axis parameter
        bool visible = true;
    };
    std::vector<El> els;
    static El ring (int p, juce::Rectangle<float> r, const char* caption)
    {
        El e; e.t = T::ring; e.p = p; e.r = r; e.caption = caption; return e;
    }
    static El slider (int p, juce::Rectangle<float> r, const char* caption, bool bipolar = false, bool valueBelow = false)
    {
        El e; e.t = T::slider; e.p = p; e.r = r; e.caption = caption; e.bipolar = bipolar; e.valueBelow = valueBelow; return e;
    }
    static El options (T t, int p, juce::Rectangle<float> r, const char* names, std::vector<float> values)
    {
        El e; e.t = t; e.p = p; e.r = r; e.options = names; e.values = std::move (values); return e;
    }
    static El stepper (int p, juce::Rectangle<float> r, const char* caption, std::vector<float> values)
    {
        El e; e.t = T::stepper; e.p = p; e.r = r; e.caption = caption; e.values = std::move (values); return e;
    }
    static El pad (int px, int py, juce::Rectangle<float> r)
    {
        El e; e.t = T::pad; e.p = px; e.py = py; e.r = r; return e;
    }

    // reference units -> editor pixels
    juce::Rectangle<float> px (juce::Rectangle<float> r) const
    {
        return { (r.getX() + kPad) * kScale, headerH + (r.getY() + kPad) * kScale, r.getWidth() * kScale, r.getHeight() * kScale };
    }
    juce::Rectangle<float> cardArea() const
    {
        return { kPad * 0.5f * kScale, headerH + kPad * 0.5f * kScale, (float) getWidth() - kPad * kScale, bodyArea().getHeight() - kPad * kScale };
    }
    static float steps (const El& e) { return (float) juce::jmax (2, juce::StringArray::fromTokens (e.options, "|", "").size()); }
    static float optionValue (const El& e, int i)
    {
        if (i < (int) e.values.size()) return e.values[(std::size_t) i];
        return (float) i / (steps (e) - 1.0f);
    }
    // the option a value shows: the nearest stored value
    static int optionOf (const El& e, float x)
    {
        const auto n = (int) steps (e);
        int best = 0;
        for (int i = 1; i < n; ++i)
            if (std::abs (optionValue (e, i) - x) < std::abs (optionValue (e, best) - x)) best = i;
        return best;
    }
    // a subclass may map a value to its option itself (an engine's own steps: ⌊7v⌋ ...)
    virtual int shownOption (const El& e) const { return e.p == kPage ? page : optionOf (e, v (e.p)); }
    int stepperIndex (const El& e) const
    {
        int best = 0;
        for (int i = 1; i < (int) e.values.size(); ++i)
            if (e.values[(std::size_t) i] <= v (e.p) + 1e-6f) best = i;
        return best;
    }

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
        for (int i = 0; i < (int) els.size(); ++i)
        {
            const auto& e = els[(std::size_t) i];
            if (! e.visible || (e.p < 0 && e.p != kPage)) continue;
            if (e.t == T::stepper)
            {
                const auto r = px (e.r);
                out.push_back (custom (r.withHeight (r.getHeight() * 0.5f), idOption + i * 16 + 0, -1, hintFor (e.p)));
                out.push_back (custom (r.withTrimmedTop (r.getHeight() * 0.5f), idOption + i * 16 + 1, -1, hintFor (e.p)));
                continue;
            }
            if (e.t == T::list || e.t == T::tabs)
            {
                const auto n = (int) steps (e);
                for (int k = 0; k < n; ++k)
                    out.push_back (custom (optionRect (e, k), idOption + i * 16 + k, -1, hintFor (e.p)));
            }
            else
                out.push_back (custom (px (e.r), (e.t == T::pad ? idPad : idParam) + i, -1, hintFor (e.p)));
        }
    }
    juce::Rectangle<float> optionRect (const El& e, int k) const
    {
        const auto r = px (e.r);
        const auto n = steps (e);
        if (e.t == T::tabs)
            return { r.getX() + r.getWidth() * (float) k / n, r.getY(), r.getWidth() / n, r.getHeight() };
        return { r.getX(), r.getY() + r.getHeight() * (float) k / n, r.getWidth(), r.getHeight() / n };
    }

    int dragEl = -1;
    juce::Point<float> padStart;
    void customDown (const Ctl& c, const juce::MouseEvent& e) override
    {
        dragEl = -1;
        if (c.id >= idOption && c.id < idPad)
        {
            const auto i = (c.id - idOption) / 16, k = (c.id - idOption) % 16;
            const auto& el = els[(std::size_t) i];
            if (el.t == T::stepper)
            {
                const auto n = (int) el.values.size();
                const auto at = juce::jlimit (0, n - 1, stepperIndex (el) + (k == 0 ? 1 : -1));
                set ({ { el.p, el.values[(std::size_t) at] } });
                return;
            }
            if (el.p == kPage)
            {
                page = k;
                pageChanged();
                repaint();
                return;
            }
            set ({ { el.p, optionValue (el, k) } });
            return;
        }
        const auto i = c.id >= idPad ? c.id - idPad : c.id - idParam;
        const auto& el = els[(std::size_t) i];
        if (el.t == T::pad)
        {
            dragEl = i;
            padTo (el, e.position);
            return;
        }
        if (e.mods.isPopupMenu())
        {
            ui::showValueMenu (*this, &engine, specFor (el.p), v (el.p), [this, p = el.p] (double x) { set ({ { p, (float) x } }); });
            return;
        }
        if (ui::isResetClick (e))
        {
            set ({ { el.p, (float) specFor (el.p).defaultValue } });
            return;
        }
        dragEl = i;
        valueDrag.begin (e, v (el.p));
    }
    void customDrag (const Ctl& c, const juce::MouseEvent& e) override
    {
        if (dragEl < 0) return;
        const auto& el = els[(std::size_t) dragEl];
        if (el.t == T::pad)
        {
            padTo (el, e.position);
            return;
        }
        const auto spec = specFor (el.p);
        set ({ { el.p, (float) valueDrag.update (e, spec) } });
        ui::showValueTip (*this, c.r.toNearestInt(), spec.text (v (el.p)));
    }
    virtual void padTo (const El& el, juce::Point<float> at)
    {
        const auto r = px (el.r).reduced (8.0f);
        const auto x = juce::jlimit (0.0f, 1.0f, (at.x - r.getX()) / r.getWidth());
        const auto y = juce::jlimit (0.0f, 1.0f, 1.0f - (at.y - r.getY()) / r.getHeight());
        set ({ { el.p, x }, { el.py, y } });
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        dragEl = -1;
        ProEditor::mouseUp (e);
    }
    bool bodyWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        Ctl c;
        if (! controlAt (e.position, c) || c.id < idParam || c.id >= idOption) return false;
        const auto& el = els[(std::size_t) (c.id >= idPad ? c.id - idPad : c.id - idParam)];
        if (el.t == T::pad) return false;
        const auto spec = specFor (el.p);
        if (const auto x = ui::wheelValue (v (el.p), e, wheel, spec))
        {
            set ({ { el.p, (float) *x } });
            ui::flashValueTip (*this, c.r.toNearestInt(), spec.text (v (el.p)));
        }
        return true;
    }

    // --- drawing ------------------------------------------------------------------------------------------------
    static void label (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, float size, juce::Colour c,
                       juce::Justification j = juce::Justification::centred, bool bold = false)
    {
        g.setColour (c);
        g.setFont (theme::font (size * kScale, bold));
        g.drawText (t, r, j, false);
    }
    // a value's text as the reference prints it (the parameter's own text; subclasses may shorten it)
    virtual juce::String valueText (const El& e) const { return text (e.p); }

    void paintBody (juce::Graphics& g, const EffectSlot& fx) override
    {
        g.setColour (cBack());
        g.fillRect (bodyArea());
        const auto card = cardArea();
        g.setColour (cCard());
        g.fillRect (card);
        g.setColour (fx.enabled ? cOrange() : cLine()); // the card's top edge: the module's on state
        g.fillRect (card.withHeight (1.5f));
        paintCard (g, fx);
        for (int i = 0; i < (int) els.size(); ++i)
            if (els[(std::size_t) i].visible) paintEl (g, els[(std::size_t) i], i, fx.enabled);
    }
    virtual void paintCard (juce::Graphics&, const EffectSlot&) {} // a display, the tab strip ... (under the controls)

    void paintEl (juce::Graphics& g, const El& e, int index, bool on)
    {
        const auto r = px (e.r);
        const auto orange = on ? cOrange() : cOrange().withSaturation (0.15f).withMultipliedBrightness (0.7f);
        const auto active = dragEl == index && isMouseButtonDown();
        switch (e.t)
        {
            case T::ring:
            {
                const auto capH = e.caption != nullptr ? 14.0f * kScale : 0.0f;
                if (e.caption != nullptr) label (g, r.withHeight (capH), tr (e.caption), 7.5f, orange);
                const auto area = r.withTrimmedTop (capH);
                const auto rad = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 6.0f * kScale;
                const auto c = area.getCentre();
                const auto a0 = juce::MathConstants<float>::pi * -0.8333f, a1 = juce::MathConstants<float>::pi * 0.8333f;
                juce::Path ring;
                ring.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a1, true);
                g.setColour (orange);
                g.strokePath (ring, juce::PathStrokeType (3.2f * kScale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                const auto a = a0 + (a1 - a0) * juce::jlimit (0.0f, 1.0f, v (e.p));
                const auto dot = c + juce::Point<float> (std::sin (a), -std::cos (a)) * rad;
                g.fillEllipse (juce::Rectangle<float> (13.0f * kScale, 13.0f * kScale).withCentre (dot));
                if (active)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.35f));
                    g.drawEllipse (juce::Rectangle<float> (17.0f * kScale, 17.0f * kScale).withCentre (dot), 1.2f);
                }
                label (g, juce::Rectangle<float> (rad * 1.8f, 16.0f * kScale).withCentre (c), valueText (e), 9.0f, cText());
                break;
            }
            case T::slider:
            {
                const auto capH = e.caption != nullptr ? 14.0f * kScale : 0.0f;
                const auto valH = e.valueBelow ? 14.0f * kScale : 0.0f;
                if (e.caption != nullptr) label (g, r.withHeight (capH), tr (e.caption), 7.5f, orange);
                const auto track = r.withTrimmedTop (capH + 6.0f * kScale).withTrimmedBottom (valH + 4.0f * kScale);
                const auto w = 5.0f * kScale, hr = 7.0f * kScale;
                const auto lane = juce::Rectangle<float> (w, track.getHeight() - 2.0f * hr).withCentre (track.getCentre());
                g.setColour (cTrack());
                g.fillRoundedRectangle (lane.expanded (0.0f, hr * 0.5f), w * 0.5f);
                const auto x = juce::jlimit (0.0f, 1.0f, v (e.p));
                const auto y = lane.getBottom() - lane.getHeight() * x;
                const auto from = e.bipolar ? lane.getCentreY() : lane.getBottom() + hr * 0.5f;
                g.setColour (orange);
                g.fillRoundedRectangle (lane.withY (juce::jmin (y, from)).withHeight (std::abs (from - y)), w * 0.5f);
                g.fillEllipse (juce::Rectangle<float> (2.0f * hr, 2.0f * hr).withCentre ({ lane.getCentreX(), y }));
                if (active)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.35f));
                    g.drawEllipse (juce::Rectangle<float> (2.0f * hr + 5.0f, 2.0f * hr + 5.0f).withCentre ({ lane.getCentreX(), y }), 1.2f);
                }
                if (e.valueBelow) label (g, r.withTrimmedTop (r.getHeight() - valH), valueText (e), 7.5f, orange);
                break;
            }
            case T::list: case T::tabs:
            {
                const auto n = (int) steps (e);
                const auto opts = juce::StringArray::fromTokens (e.options, "|", "");
                const auto sel = shownOption (e);
                for (int k = 0; k < n; ++k)
                {
                    const auto cell = optionRect (e, k);
                    label (g, cell, k < opts.size() ? tr (opts[k]) : juce::String(), 8.0f, k == sel ? cText() : orange);
                }
                g.setColour (cText());
                const auto cell = optionRect (e, sel);
                if (e.t == T::tabs)
                    g.fillRect (juce::Rectangle<float> (cell.getWidth() * 0.82f, 2.5f * kScale).withCentre ({ cell.getCentreX(), cell.getBottom() - 1.5f * kScale }));
                else
                    g.fillRect (juce::Rectangle<float> (2.5f * kScale, cell.getHeight() * 0.8f).withCentre ({ r.getRight() + 3.0f * kScale, cell.getCentreY() }));
                break;
            }
            case T::pad: paintPad (g, e, r, on); break;
            case T::stepper:
            {
                // the caption, an up chevron, the value, a down chevron (the reference's LFO Rate / Shape)
                const auto rowH = r.getHeight() / 4.0f;
                if (e.caption != nullptr) label (g, r.withHeight (rowH), tr (e.caption), 7.0f, orange);
                const auto chevron = [&] (juce::Rectangle<float> c, bool up)
                {
                    const auto w = 9.0f * kScale, h = 4.5f * kScale;
                    const auto m = c.getCentre();
                    juce::Path p;
                    p.startNewSubPath (m.x - w, m.y + (up ? h : -h));
                    p.lineTo (m.x, m.y + (up ? -h : h));
                    p.lineTo (m.x + w, m.y + (up ? h : -h));
                    g.setColour (orange);
                    g.strokePath (p, juce::PathStrokeType (2.2f * kScale, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
                };
                chevron (r.withTrimmedTop (rowH).withHeight (rowH), true);
                label (g, r.withTrimmedTop (2.0f * rowH).withHeight (rowH), tr (text (e.p)), 8.0f, orange);
                chevron (r.withTrimmedTop (3.0f * rowH), false);
                break;
            }
        }
    }
    virtual void paintPad (juce::Graphics& g, const El& e, juce::Rectangle<float> r, bool on)
    {
        g.setColour (cDeep());
        g.fillRect (r);
        g.setColour (cLine());
        for (int k = 1; k < 4; ++k)
        {
            g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) k / 4.0f), r.getX(), r.getRight());
            g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) k / 4.0f), r.getY(), r.getBottom());
        }
        const auto in = r.reduced (8.0f);
        const auto at = juce::Point<float> (in.getX() + in.getWidth() * v (e.p), in.getBottom() - in.getHeight() * v (e.py));
        g.setColour ((on ? cOrange() : cLine()).withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (34.0f * kScale, 34.0f * kScale).withCentre (at));
        g.setColour (on ? cOrange() : cLine());
        g.fillEllipse (juce::Rectangle<float> (16.0f * kScale, 16.0f * kScale).withCentre (at));
    }
};

// --- Distortion (FLM module 6): the curve list, Bias, Drive, Stereo, BitCrush, Gain -----------------------------
class PocketDistortionEditor final : public PocketPanel
{
public:
    PocketDistortionEditor (AudioEngine& e, int t, int s, bool d) : PocketPanel (e, t, s, EffectKind::pocketDistortion, d, 92.0f)
    {
        els.push_back (options (T::list, 4, { 14.0f, 4.0f, 56.0f, 82.0f }, "Hard|Tube|Soft|Clip", { 0.0f, 0.34f, 0.67f, 1.0f }));
        els.push_back (slider (5, { 98.0f, 4.0f, 46.0f, 86.0f }, TR_NOOP ("Bias"), true));
        els.push_back (ring (0, { 165.0f, 4.0f, 72.0f, 86.0f }, TR_NOOP ("Drive")));
        els.push_back (ring (3, { 244.0f, 4.0f, 72.0f, 86.0f }, TR_NOOP ("Stereo")));
        els.push_back (slider (1, { 332.0f, 4.0f, 46.0f, 86.0f }, TR_NOOP ("BitCrush")));
        els.push_back (ring (2, { 404.0f, 4.0f, 72.0f, 86.0f }, TR_NOOP ("Gain")));
        fillDemo();
    }
    juce::String title() const override { return "Pocket Distortion"; }

private:
    int shownOption (const El& e) const override { return e.p == 4 ? pocket::distortionKind (v (4)) : PocketPanel::shownOption (e); }
    juce::String valueText (const El& e) const override
    {
        if (e.p == 0) return juce::String ((int) (v (0) * 100.0f)); // the reference prints Drive and Stereo bare
        if (e.p == 3) return juce::String ((int) ((v (3) - 0.5f) * 200.0f));
        return PocketPanel::valueText (e);
    }
};

// --- Limiter (legacy) (FLM module 5): three rings -----------------------------------------------------------------
class PocketLimiterClassicEditor final : public PocketPanel
{
public:
    PocketLimiterClassicEditor (AudioEngine& e, int t, int s, bool d) : PocketPanel (e, t, s, EffectKind::pocketLimiterClassic, d, 100.0f)
    {
        els.push_back (ring (0, { 46.0f, 8.0f, 78.0f, 92.0f }, TR_NOOP ("Threshold")));
        els.push_back (ring (1, { 201.0f, 8.0f, 78.0f, 92.0f }, TR_NOOP ("Release Time")));
        els.push_back (ring (2, { 353.0f, 8.0f, 78.0f, 92.0f }, TR_NOOP ("Output Gain")));
        fillDemo();
    }
    juce::String title() const override { return "Pocket Limiter Classic"; }

private:
    juce::String valueText (const El& e) const override
    {
        if (e.p == 1) return juce::String ((int) std::lround (v (1) * 100.0f)) + "%"; // the reference's Release Time
        return PocketPanel::valueText (e);
    }
};

// --- Limiter (FLM module 28): In Gain and Threshold, the gain-reduction trace, Release and Out Gain --------------
class PocketLimiterEditor final : public PocketPanel
{
public:
    PocketLimiterEditor (AudioEngine& e, int t, int s, bool d) : PocketPanel (e, t, s, EffectKind::pocketLimiter, d, 100.0f)
    {
        els.push_back (slider (0, { 6.0f, 6.0f, 46.0f, 94.0f }, TR_NOOP ("In Gain"), false, true));
        els.push_back (slider (3, { 58.0f, 6.0f, 46.0f, 94.0f }, TR_NOOP ("Thr."), false, true));
        els.push_back (slider (2, { 372.0f, 6.0f, 46.0f, 94.0f }, TR_NOOP ("Release"), false, true));
        els.push_back (slider (1, { 424.0f, 6.0f, 50.0f, 94.0f }, TR_NOOP ("Out Gain"), false, true));
        fillDemo();
    }
    juce::String title() const override { return "Pocket Limiter"; }
    bool usesHistory() const override { return true; }

private:
    Column demoColumn (int i) const override
    {
        const auto beat = std::fmod ((float) i * 0.01f, 0.5f);
        const auto in = -14.0f + 16.0f * std::exp (-beat * 9.0f);
        return { in, juce::jmin (in, -2.0f), juce::jmax (0.0f, in + 2.0f) };
    }
    void paintCard (juce::Graphics& g, const EffectSlot&) override
    {
        // the gain-reduction trace: 0 dB on the top line, the reduction falling from it (newest on the right)
        const auto r = px ({ 108.0f, 8.0f, 258.0f, 74.0f });
        g.setColour (cLine());
        for (int k = 1; k <= 4; ++k)
            g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) k / 4.5f), r.getX(), r.getRight());
        juce::Path p;
        const auto n = 240;
        const auto gains = (v (0) * 48.0f - 24.0f) + (v (1) * 48.0f - 24.0f); // the reduction = in + In Gain − (out − Out Gain)
        for (int k = 0; k < n; ++k)
        {
            const auto& c = columnBack (n - 1 - k);
            const auto gr = juce::jlimit (0.0f, 24.0f, juce::jmax (c.gr, c.out > -119.0f ? c.in + gains - c.out : 0.0f));
            const auto x = r.getX() + r.getWidth() * (float) k / (float) (n - 1);
            const auto y = r.getY() + r.getHeight() * gr / 24.0f;
            if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (cOrange());
        g.strokePath (p, juce::PathStrokeType (1.6f * kScale));
    }
};

// --- Trance Delay (FLM module 16): the time steps, the mode list, Input Pan (Ping-Pong only), Feedback, Filter, Mix
class PocketTranceDelayEditor final : public PocketPanel
{
public:
    PocketTranceDelayEditor (AudioEngine& e, int t, int s, bool d) : PocketPanel (e, t, s, EffectKind::pocketTranceDelay, d, 132.0f)
    {
        els.push_back (options (T::tabs, 0, { 0.0f, 6.0f, 474.0f, 26.0f }, "1/4|3/8|1/2|3/4|1/1", { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }));
        els.push_back (options (T::list, 4, { 6.0f, 42.0f, 92.0f, 80.0f }, "Stereo|Ping-Pong|Mirror", { 0.0f, 0.5f, 1.0f }));
        els.push_back (ring (5, { 128.0f, 40.0f, 78.0f, 90.0f }, TR_NOOP ("Input Pan")));
        els.push_back (slider (1, { 231.0f, 40.0f, 46.0f, 86.0f }, TR_NOOP ("Feedback")));
        els.push_back (slider (3, { 318.0f, 40.0f, 46.0f, 86.0f }, TR_NOOP ("Filter"), true));
        els.push_back (slider (2, { 405.0f, 40.0f, 46.0f, 86.0f }, TR_NOOP ("Mix")));
        fillDemo();
    }
    juce::String title() const override { return "Pocket Trance Delay"; }

private:
    int shownOption (const El& e) const override
    {
        if (e.p == 0) return juce::jlimit (0, 4, (int) (v (0) * 4.0f));
        if (e.p == 4) return juce::jlimit (0, 2, (int) (v (4) * 2.0f + 0.5f));
        return PocketPanel::shownOption (e);
    }
    void tick() override
    {
        const auto pingPong = (int) (v (4) * 2.0f + 0.5f) == 1; // the reference shows Input Pan in Ping-Pong only
        if (els[2].visible != pingPong) { els[2].visible = pingPong; repaint(); }
    }
    juce::String valueText (const El& e) const override
    {
        if (e.p == 5) return juce::String ((int) std::lround (v (5) * 100.0f));
        return PocketPanel::valueText (e);
    }
    void paintCard (juce::Graphics& g, const EffectSlot&) override
    {
        const auto strip = px ({ 0.0f, 0.0f, 474.0f, 36.0f });
        g.setColour (cLine());
        g.fillRect (strip.withY (strip.getBottom()).withHeight (1.5f));
        const auto split = px ({ 112.0f, 40.0f, 1.0f, 84.0f });
        g.setColour (cBack().withAlpha (0.7f));
        g.fillRect (split.withWidth (2.0f));
    }
};

// --- Leveller (FLM module 20): the dB scale over the level, a long Gain bar, Pan -----------------------------------
class PocketLevellerEditor final : public PocketPanel
{
public:
    PocketLevellerEditor (AudioEngine& e, int t, int s, bool d) : PocketPanel (e, t, s, EffectKind::pocketLeveller, d, 84.0f)
    {
        els.push_back (ring (1, { 390.0f, 6.0f, 72.0f, 76.0f }, TR_NOOP ("Pan")));
        fillDemo();
    }
    juce::String title() const override { return "Pocket Leveller"; }
    bool usesHistory() const override { return true; }

private:
    static constexpr float x0 = 14.0f, x1 = 360.0f;
    // the scale's dB at a fraction of the bar: Gain's own law (v^0.6 → −60 … +12 dB)
    static float dbAt (float x) { return 72.0f * std::pow (juce::jlimit (0.0f, 1.0f, x), 0.6f) - 60.0f; }
    static float xAt (float db) { return std::pow (juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 72.0f), 1.0f / 0.6f); }
    juce::Rectangle<float> bar() const { return px ({ x0, 52.0f, x1 - x0, 20.0f }); }
    void layoutControls (std::vector<Ctl>& out) const override
    {
        PocketPanel::layoutControls (out);
        out.push_back (custom (bar().expanded (0.0f, 6.0f), idPad + 99, -1, hintFor (0)));
    }
    void customDown (const Ctl& c, const juce::MouseEvent& e) override
    {
        if (c.id != idPad + 99) { PocketPanel::customDown (c, e); return; }
        if (ui::isResetClick (e)) { set ({ { 0, (float) specFor (0).defaultValue } }); return; }
        if (e.mods.isPopupMenu())
        {
            ui::showValueMenu (*this, &engine, specFor (0), v (0), [this] (double x) { set ({ { 0, (float) x } }); });
            return;
        }
        gainTo (e.position.x);
    }
    void customDrag (const Ctl& c, const juce::MouseEvent& e) override
    {
        if (c.id != idPad + 99) { PocketPanel::customDrag (c, e); return; }
        gainTo (e.position.x);
        ui::showValueTip (*this, c.r.toNearestInt(), text (0));
    }
    void gainTo (float x)
    {
        const auto b = bar();
        set ({ { 0, juce::jlimit (0.0f, 1.0f, (x - b.getX()) / b.getWidth()) } });
    }
    juce::String valueText (const El& e) const override
    {
        if (e.p == 1)
        {
            const auto i = (int) (v (1) * 2000.0f - 1000.0f);
            return i == 0 ? tr ("mid") : juce::String ((double) std::abs (i) * 0.1, 1) + (i < 0 ? "% L" : "% R");
        }
        return PocketPanel::valueText (e);
    }
    void paintCard (juce::Graphics& g, const EffectSlot& fx) override
    {
        const auto on = fx.enabled;
        const auto orange = on ? cOrange() : cLine();
        // the scale: −inf, −50 … +12 dB over the level meter
        const auto scale = px ({ x0, 4.0f, x1 - x0, 12.0f });
        const auto b = bar();
        for (const auto db : { -60.0f, -50.0f, -40.0f, -30.0f, -20.0f, -10.0f, -5.0f, 0.0f, 3.0f, 6.0f, 9.0f, 12.0f })
        {
            const auto x = b.getX() + b.getWidth() * xAt (db);
            label (g, juce::Rectangle<float> (30.0f * kScale, scale.getHeight()).withCentre ({ x, scale.getCentreY() }),
                   db <= -60.0f ? juce::String ("-inf.") : juce::String ((int) db), 6.5f, cText().withAlpha (0.8f));
        }
        const auto meter = px ({ x0, 18.0f, x1 - x0, 5.0f });
        g.setColour (cDeep());
        g.fillRect (meter);
        const auto level = columnBack (0).out;
        if (level > -60.0f)
        {
            g.setColour (orange.withAlpha (0.85f));
            g.fillRect (meter.withWidth (meter.getWidth() * xAt (level)));
        }
        // Gain: its value over the bar, the bar filled to the handle
        label (g, px ({ x0, 30.0f, x1 - x0, 16.0f }), text (0), 9.0f, orange);
        const auto lane = b.withSizeKeepingCentre (b.getWidth(), 5.0f * kScale);
        g.setColour (cTrack());
        g.fillRoundedRectangle (lane, 2.5f * kScale);
        const auto hx = b.getX() + b.getWidth() * v (0);
        g.setColour (orange);
        g.fillRoundedRectangle (lane.withRight (hx), 2.5f * kScale);
        g.fillEllipse (juce::Rectangle<float> (16.0f * kScale, 16.0f * kScale).withCentre ({ hx, lane.getCentreY() }));
    }
};

// --- Gate (FLM module 27): LEVEL and ENVELOPE pages; Asymmetry, Threshold, the input meters, Mix --------------------
class PocketGateEditor final : public PocketPanel
{
public:
    PocketGateEditor (AudioEngine& e, int t, int s, bool d) : PocketPanel (e, t, s, EffectKind::pocketGate, d, 150.0f)
    {
        els.push_back (options (T::tabs, kPage, { 0.0f, 6.0f, 474.0f, 26.0f }, "LEVEL|ENVELOPE", {}));
        // LEVEL
        els.push_back (ring (1, { 20.0f, 40.0f, 106.0f, 108.0f }, TR_NOOP ("Asymmetry")));
        els.push_back (slider (0, { 136.0f, 40.0f, 46.0f, 106.0f }, TR_NOOP ("Threshold")));
        els.push_back (ring (6, { 352.0f, 40.0f, 106.0f, 108.0f }, TR_NOOP ("Mix")));
        // ENVELOPE (the second page: the module's timing controls in the same card)
        els.push_back (ring (2, { 14.0f, 44.0f, 84.0f, 98.0f }, TR_NOOP ("Attack")));
        els.push_back (slider (3, { 104.0f, 40.0f, 46.0f, 106.0f }, TR_NOOP ("A. Curve"), true));
        els.push_back (ring (7, { 160.0f, 44.0f, 84.0f, 98.0f }, TR_NOOP ("Hold")));
        els.push_back (ring (4, { 284.0f, 44.0f, 84.0f, 98.0f }, TR_NOOP ("Release")));
        els.push_back (slider (5, { 376.0f, 40.0f, 46.0f, 106.0f }, TR_NOOP ("R. Curve"), true));
        pageChanged();
        fillDemo();
    }
    juce::String title() const override { return "Pocket Gate"; }
    bool usesHistory() const override { return true; }

private:
    void pageChanged() override
    {
        for (int i = 1; i < (int) els.size(); ++i)
            els[(std::size_t) i].visible = (i <= 3) == (page == 0);
    }
    Column demoColumn (int i) const override
    {
        const auto beat = std::fmod ((float) i * 0.01f, 0.5f);
        const auto in = -40.0f + 34.0f * std::exp (-beat * 7.0f);
        return { in, in > -30.0f ? in : -120.0f, 0.0f };
    }
    juce::String valueText (const El& e) const override
    {
        if (e.p == 1) return juce::String (juce::roundToInt (v (1) * v (1) * 66.0f)) + " dB";
        if (e.p == 6) return juce::String (juce::roundToInt (v (6) * 200.0f - 100.0f)) + "%";
        return PocketPanel::valueText (e);
    }
    void paintCard (juce::Graphics& g, const EffectSlot& fx) override
    {
        const auto strip = px ({ 0.0f, 0.0f, 474.0f, 36.0f });
        g.setColour (cLine());
        g.fillRect (strip.withY (strip.getBottom()).withHeight (1.5f));
        if (page != 0) return;
        // the input meters (Left, Right) with the threshold's mark: 66v − 60 dB on a −60 … +6 dB scale
        const auto orange = fx.enabled ? cOrange() : cLine();
        const auto level = columnBack (0).in;
        const auto threshold = 66.0f * v (0) - 60.0f;
        for (int side = 0; side < 2; ++side)
        {
            const auto col = px ({ side == 0 ? 196.0f : 262.0f, 40.0f, 46.0f, 106.0f });
            label (g, col.withHeight (14.0f * kScale), tr (side == 0 ? TR_NOOP ("Left") : TR_NOOP ("Right")), 7.5f, orange);
            const auto lane = juce::Rectangle<float> (9.0f * kScale, col.getHeight() - 26.0f * kScale).withCentre ({ col.getCentreX(), col.getCentreY() + 6.0f * kScale });
            g.setColour (cTrack());
            g.fillRect (lane);
            const auto frac = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };
            if (level > -60.0f)
            {
                g.setColour (orange.withAlpha (level >= threshold ? 0.95f : 0.45f));
                g.fillRect (lane.withTop (lane.getBottom() - lane.getHeight() * frac (level)));
            }
            g.setColour (orange);
            g.fillRect (juce::Rectangle<float> (lane.getWidth() + 6.0f * kScale, 2.0f * kScale).withCentre ({ lane.getCentreX(), lane.getBottom() - lane.getHeight() * frac (threshold) }));
        }
        label (g, px ({ 222.0f, 128.0f, 60.0f, 14.0f }), juce::String (juce::roundToInt (threshold)) + " dB", 7.0f, cText());
    }
};

// --- Tape Delay (FLM module 18): four rings ---------------------------------------------------------------------
class PocketTapeDelayEditor final : public PocketPanel
{
public:
    PocketTapeDelayEditor (AudioEngine& e, int t, int s, bool d) : PocketPanel (e, t, s, EffectKind::pocketTapeDelay, d, 96.0f)
    {
        els.push_back (ring (0, { 30.0f, 6.0f, 72.0f, 86.0f }, TR_NOOP ("Time")));
        els.push_back (ring (1, { 145.0f, 6.0f, 72.0f, 86.0f }, TR_NOOP ("Fdbk")));
        els.push_back (ring (2, { 262.0f, 6.0f, 72.0f, 86.0f }, TR_NOOP ("Async.")));
        els.push_back (ring (3, { 378.0f, 6.0f, 72.0f, 86.0f }, TR_NOOP ("Mix")));
        fillDemo();
    }
    juce::String title() const override { return "Pocket Tape Delay"; }

private:
    juce::String valueText (const El& e) const override
    {
        const auto x = v (e.p);
        switch (e.p)
        {
            case 0: return juce::String ((int) (x * x * 1000.0f)) + " ms";
            case 2: return juce::String ((int) (x * 200.0f - 100.0f)) + "%";
            default: return juce::String ((int) (x * 100.0f)) + "%";
        }
    }
};

// --- MultiFX (FLM module 4): the eight kinds, LFO Rate and Shape, the X/Y pad, LFO Amount -------------------------
class PocketMultiFxEditor final : public PocketPanel
{
public:
    PocketMultiFxEditor (AudioEngine& e, int t, int s, bool d) : PocketPanel (e, t, s, EffectKind::pocketMultiFx, d, 178.0f)
    {
        std::vector<float> kinds;
        for (int i = 0; i < 8; ++i) kinds.push_back (i == 7 ? 1.0f : ((float) i + 0.5f) / 7.0f);
        els.push_back (options (T::tabs, 0, { 0.0f, 0.0f, 474.0f, 30.0f }, "Delay|Reverb|LP Filter|HP Filter|Flanger|Phaser|Panner|Vox", kinds));
        std::vector<float> rates, shapes { 0.0f, 0.5f, 1.0f };
        for (int i = 0; i < 10; ++i) rates.push_back (((float) i + 0.01f) / 9.0f); // the engine's step: ⌊9v⌋
        els.push_back (stepper (3, { 4.0f, 40.0f, 52.0f, 68.0f }, TR_NOOP ("LFO Rate"), rates));
        els.push_back (stepper (5, { 4.0f, 110.0f, 52.0f, 68.0f }, TR_NOOP ("LFO Shape"), shapes));
        els.push_back (pad (1, 2, { 62.0f, 44.0f, 352.0f, 128.0f }));
        els.push_back (slider (4, { 420.0f, 40.0f, 50.0f, 134.0f }, TR_NOOP ("LFO Amnt")));
        fillDemo();
    }
    juce::String title() const override { return "Pocket MultiFX"; }

private:
    int shownOption (const El& e) const override { return e.p == 0 ? pocket::multiFxKind (v (0)) : PocketPanel::shownOption (e); }
    void paintCard (juce::Graphics& g, const EffectSlot&) override
    {
        const auto strip = px ({ 0.0f, 0.0f, 474.0f, 30.0f });
        g.setColour (cLine());
        g.fillRect (strip.withY (strip.getBottom()).withHeight (1.5f));
    }
    // the reference's pad: a dark grid, the point as two full-length lines with the axes' names on them
    void paintPad (juce::Graphics& g, const El& e, juce::Rectangle<float> r, bool on) override
    {
        g.setColour (cDeep().interpolatedWith (cCard(), 0.35f));
        g.fillRect (r);
        g.setColour (cCard().darker (0.25f));
        for (int k = 1; k < 10; ++k) g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) k / 10.0f), r.getY(), r.getBottom());
        for (int k = 1; k < 8; ++k) g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) k / 8.0f), r.getX(), r.getRight());
        const auto x = r.getX() + r.getWidth() * v (e.p);
        const auto y = r.getBottom() - r.getHeight() * v (e.py);
        const auto orange = on ? cOrange() : cLine();
        g.setColour (orange);
        g.fillRect (juce::Rectangle<float> (2.0f, r.getHeight()).withCentre ({ x, r.getCentreY() }));
        g.fillRect (juce::Rectangle<float> (r.getWidth(), 2.0f).withCentre ({ r.getCentreX(), y }));
        g.fillEllipse (juce::Rectangle<float> (8.0f * kScale, 8.0f * kScale).withCentre ({ x, y }));
        const auto kindNow = pocket::multiFxKind (v (0));
        label (g, { x + 4.0f, r.getY() + 2.0f, 120.0f * kScale, 12.0f * kScale }, tr (pocket::multiFxAxisName (kindNow, false)), 7.0f, orange,
               juce::Justification::centredLeft);
        label (g, { r.getX() + 3.0f, y - 13.0f * kScale, 120.0f * kScale, 12.0f * kScale }, tr (pocket::multiFxAxisName (kindNow, true)), 7.0f, orange,
               juce::Justification::centredLeft);
    }
    void padTo (const El& el, juce::Point<float> at) override
    {
        const auto r = px (el.r);
        set ({ { el.p, juce::jlimit (0.0f, 1.0f, (at.x - r.getX()) / r.getWidth()) }, { el.py, juce::jlimit (0.0f, 1.0f, (r.getBottom() - at.y) / r.getHeight()) } });
    }
};

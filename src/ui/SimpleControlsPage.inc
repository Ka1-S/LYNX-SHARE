// Round 190ke (owner decision D4 of coverage190jw): the Simple dock's Controls tab, GarageBand's Smart Controls as a
// LYNX-own screen (design kind (d)). Included by ui/SimpleDock.cpp after its helpers (DockKnob, styleCaption, utf8).
//
// GarageBand's landmarks kept (Apple's guide, "Intro to Smart Controls"; ref/img/gb-smartcontrols-pane.png): the
// inspector column on the left (what the controls drive, Compare-like actions), the screen controls on the right as
// big labelled knobs with the label ABOVE each knob, knobs gathered in framed sections with their title above the frame
// (GarageBand's "TWEED", "DISTORTION", "ECHO", "REVERB"), a 0…10 scale around each knob. LYNX's own: dark panel and red
// accent, the value under the knob, one knob = one existing control (core/SmartControls.h), "Edit mapping" (a ▾ under
// each label picks what the knob turns; GarageBand's Mac app has no user mapping) and "Reset to default".
// Performance: static paint, no timer; a turned knob repaints itself only; the page follows the song on refresh().

namespace
{
// A big knob of the Controls tab: LYNX vector (a dark body with a lit pointer, the arc from 0 to the value in the
// accent, eleven scale marks with 0 / 5 / 10 numbers around it). Drag up / down (Shift: fine), wheel, arrow keys,
// double-click = the control's default.
class BigKnob final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    std::function<void (float)> onChange;
    std::function<juce::String (float)> valueText;
    float resetValue = 0.5f;
    bool empty = false; // a knob with nothing mapped: drawn as an empty socket

    BigKnob()
    {
        setWantsKeyboardFocus (true);
        getProperties().set ("lynxClickKeepsKeys", true);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }
    float value() const noexcept { return shown; }
    void setValue (float v)
    {
        v = juce::jlimit (0.0f, 1.0f, v);
        if (dragging || std::abs (v - shown) < 1.0e-6f)
            return;
        shown = v;
        repaint();
    }
    void turnTo (float v)
    {
        if (empty)
            return;
        shown = juce::jlimit (0.0f, 1.0f, v);
        repaint();
        if (onChange)
            onChange (shown);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto valueRow = r.removeFromBottom (18.0f);
        const auto d = juce::jmin (r.getWidth() - 20.0f, r.getHeight() - 14.0f);
        const auto centre = r.getCentre().translated (0.0f, 4.0f);
        const auto body = juce::Rectangle<float> (d, d).withCentre (centre).reduced (d * 0.17f);
        const bool hot = ! empty && (dragging || isMouseOver() || hasKeyboardFocus (false));
        constexpr float start = -2.4f, end = 2.4f; // the travel, radians from straight up
        const auto ring = d * 0.5f - 3.0f;
        // the scale: eleven marks, numbers at 0, 5 and 10 (GarageBand's 0…10 scale)
        g.setFont (font (type::caption * 0.9f));
        for (int k = 0; k <= 10; ++k)
        {
            const auto a = start + (end - start) * (float) k / 10.0f;
            const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
            g.setColour (empty ? colour::border : colour::textDisabled);
            g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre (centre + dir * (ring + 4.0f)));
            if (k % 5 == 0 && d >= 52.0f)
            {
                g.setColour (empty ? colour::border : colour::textSecondary);
                g.drawText (juce::String (k), juce::Rectangle<float> (18.0f, 11.0f).withCentre (centre + dir * (ring + 13.0f)), juce::Justification::centred, false);
            }
        }
        // the track and the lit arc
        juce::Path track, lit;
        track.addCentredArc (centre.x, centre.y, ring - 4.0f, ring - 4.0f, 0.0f, start, end, true);
        g.setColour (colour::bg4);
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const auto at = start + (end - start) * shown;
        if (! empty && shown > 0.001f)
        {
            lit.addCentredArc (centre.x, centre.y, ring - 4.0f, ring - 4.0f, 0.0f, start, at, true);
            g.setColour (dragging ? colour::accentHover : colour::accent);
            g.strokePath (lit, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // the body: a dark disc with a soft top light and a rim; the pointer from its centre to its edge
        g.setGradientFill (juce::ColourGradient (colour::bg5, body.getCentreX(), body.getY(), colour::bg0, body.getCentreX(), body.getBottom(), false));
        g.fillEllipse (body);
        g.setColour (hot ? colour::borderStrong : colour::border);
        g.drawEllipse (body.reduced (0.5f), 1.0f);
        if (! empty)
        {
            const auto dir = juce::Point<float> (std::sin (at), -std::cos (at));
            g.setColour (colour::textPrimary);
            g.drawLine ({ body.getCentre() + dir * (body.getWidth() * 0.12f), body.getCentre() + dir * (body.getWidth() * 0.44f) }, 2.5f);
        }
        g.setColour (empty ? colour::textDisabled : hot ? colour::textPrimary : colour::textSecondary);
        g.setFont (font (type::small, hot));
        g.drawText (empty ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) : valueText ? valueText (shown) : percent (shown), valueRow,
                    juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || empty)
            return;
        dragging = true;
        startValue = shown;
        grabKeyboardFocus();
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging)
            turnTo (startValue - (float) e.getDistanceFromDragStartY() / (e.mods.isShiftDown() ? 640.0f : 180.0f));
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = false;
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { turnTo (resetValue); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (std::abs (wheel.deltaY) > 0.0f)
            turnTo (shown + (wheel.deltaY > 0.0f ? 0.02f : -0.02f));
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto step = key.getModifiers().isShiftDown() ? 0.01f : 0.05f;
        if (key.isKeyCode (juce::KeyPress::upKey) || key.isKeyCode (juce::KeyPress::rightKey))
            return turnTo (shown + step), true;
        if (key.isKeyCode (juce::KeyPress::downKey) || key.isKeyCode (juce::KeyPress::leftKey))
            return turnTo (shown - step), true;
        return false;
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    float shown = 0.0f, startValue = 0.0f;
    bool dragging = false;
};

juce::String groupTitle (smartcontrols::Group g)
{
    using G = smartcontrols::Group;
    return g == G::sound ? tr ("Sound") : g == G::effects ? tr ("Effects") : tr ("Track");
}
} // namespace

class SimpleDock::ControlsPage final : public juce::Component
{
public:
    explicit ControlsPage (AudioEngine& e) : engine (e)
    {
        styleCaption (headCaption, tr ("Controls"));
        headCaption.setFont (font (type::body, true));
        headCaption.setColour (juce::Label::textColourId, colour::textPrimary);
        addAndMakeVisible (headCaption);
        styleCaption (sourceCaption, {});
        sourceCaption.setFont (font (type::small));
        sourceCaption.setMinimumHorizontalScale (0.8f);
        addAndMakeVisible (sourceCaption);
        styleCaption (hint, tr ("Eight knobs for the track's main settings: its sound, its effects and its level. Each one moves a control the track already has."));
        hint.setJustificationType (juce::Justification::topLeft);
        hint.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (hint);
        editButton.setButtonText (tr ("Edit mapping"));
        editButton.setTooltip (tr ("Choose what each knob turns: a ▾ appears under each name"));
        editButton.setClickingTogglesState (true);
        editButton.setColour (juce::TextButton::buttonOnColourId, colour::accent);
        editButton.setComponentID ("simple.dock.controls.edit");
        editButton.onClick = [this] {
            editing = editButton.getToggleState();
            resized();
            repaint();
        };
        addAndMakeVisible (editButton);
        resetButton.setButtonText (tr ("Reset to default"));
        resetButton.setTooltip (tr ("Every knob back to LYNX's choice for this instrument and its effects"));
        resetButton.onClick = [this] {
            const auto o = owner;
            engine.editSong ([o] (Song& s) { smartcontrols::reset (s, o); });
            refresh();
        };
        addAndMakeVisible (resetButton);
        for (int k = 0; k < smartcontrols::knobCount; ++k)
        {
            auto& knob = knobs[(std::size_t) k];
            knob.setComponentID ("simple.dock.controls.knob" + juce::String (k));
            knob.onChange = [this, k] (float v) { turn (k, v); };
            addAndMakeVisible (knob);
        }
    }

    void show (int newTrack, int newChannel, int newInsert)
    {
        track = newTrack;
        owner = { newChannel, newInsert };
        refresh();
    }

    void refresh()
    {
        const auto& song = engine.getSong();
        const auto has = owner.insert > 0 && owner.insert < (int) song.mixer.size();
        mapping = has ? smartcontrols::mappingOf (song, owner) : smartcontrols::Mapping {};
        const auto user = has ? smartcontrols::decode (song.mixer[(std::size_t) owner.insert].smartControls) : std::array<std::optional<smartcontrols::Control>, smartcontrols::knobCount> {};
        for (int k = 0; k < smartcontrols::knobCount; ++k)
        {
            const auto& c = mapping[(std::size_t) k];
            auto& knob = knobs[(std::size_t) k];
            labels[(std::size_t) k] = smartcontrols::labelOf (song, owner, c);
            remapped[(std::size_t) k] = user[(std::size_t) k].has_value();
            knob.empty = c.source == smartcontrols::Source::none;
            knob.resetValue = smartcontrols::defaultValueOf (song, owner, c);
            knob.setValue (smartcontrols::valueOf (song, owner, c));
            knob.valueText = [this, k] (float v) { return valueText (k, v); };
            knob.setTooltip (knob.empty ? tr ("Nothing on this knob: Edit mapping gives it a control") : nameOf (k));
            knob.repaint();
        }
        juce::String source;
        if (owner.channel >= 0 && owner.channel < (int) song.channels.size())
            source = utf8 (song.channels[(std::size_t) owner.channel].name);
        if (has && ! song.mixer[(std::size_t) owner.insert].chainPreset.empty())
            source << (source.isEmpty() ? "" : juce::String::fromUTF8 ("  \xc2\xb7  ")) << utf8 (song.mixer[(std::size_t) owner.insert].chainPreset);
        sourceCaption.setText (! has ? tr ("Select a track to see its controls.") : source.isEmpty() ? trackTitle (song, track) : source, juce::dontSendNotification);
        editButton.setEnabled (has);
        resetButton.setEnabled (has && ! song.mixer[(std::size_t) owner.insert].smartControls.empty());
        repaint();
    }

    juce::PopupMenu remapMenu (int knob)
    {
        juce::PopupMenu menu;
        const auto& song = engine.getSong();
        const auto current = mapping[(std::size_t) knob];
        menu.addItem (tr ("Default"), true, ! remapped[(std::size_t) knob], [this, knob] { remap (knob, std::nullopt); });
        menu.addItem (tr ("Nothing"), true, remapped[(std::size_t) knob] && current.source == smartcontrols::Source::none,
                      [this, knob] { remap (knob, smartcontrols::Control {}); });
        juce::String section;
        juce::PopupMenu sub;
        juce::String subName;
        auto flush = [&] {
            if (sub.getNumItems() > 0)
                menu.addSubMenu (subName, sub);
            sub = {};
        };
        for (const auto& c : smartcontrols::candidates (song, owner))
        {
            const auto l = smartcontrols::labelOf (song, owner, c);
            const auto group = l.group == smartcontrols::Group::effects ? utf8 (l.effect == "Mix" ? l.name : l.effect) : groupTitle (l.group);
            if (group != subName)
            {
                flush();
                subName = group;
            }
            auto text = l.group == smartcontrols::Group::effects && l.effect == "Mix" ? tr ("Mix")
                        : c.source == smartcontrols::Source::goal ? tr ("Knob") + juce::String::fromUTF8 (" \xe2\x80\x94 ") + tr (utf8 (l.name))
                                                                   : tr (utf8 (l.name));
            sub.addItem (text, true, c == current, [this, knob, c] { remap (knob, c); });
        }
        flush();
        return menu;
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colour::border);
        g.fillRect (sideWidth, pad, 1, getHeight() - 2 * pad);
        // the screen controls' panel and its sections: each run of knobs of one group in a frame, its title above
        const auto panel = panelArea();
        fillRoundRect (g, panel.toFloat(), radius::lg, colour::bg0);
        for (const auto& s : sections())
        {
            const auto frame = s.frame.toFloat();
            g.setColour (colour::borderStrong);
            g.drawRoundedRectangle (frame.reduced (0.5f), radius::lg, 1.0f);
            g.setColour (colour::textSecondary);
            g.setFont (font (type::small, true).withExtraKerningFactor (0.08f));
            g.drawText (s.title.toUpperCase(), frame.withY (frame.getY() - 20.0f).withHeight (18.0f).toNearestInt(), juce::Justification::centred, false);
        }
        // each knob's name above it (GarageBand), ▾ while editing
        for (int k = 0; k < smartcontrols::knobCount; ++k)
        {
            const auto area = nameArea (k);
            const auto empty = knobs[(std::size_t) k].empty;
            g.setColour (empty ? colour::textDisabled : colour::textPrimary);
            g.setFont (font (type::small, true));
            auto text = empty ? tr ("Empty") : nameOf (k).toUpperCase();
            if (editing)
                text << juce::String::fromUTF8 (" \xe2\x96\xbe");
            g.drawFittedText (text, area, juce::Justification::centred, 1, 0.75f);
            if (editing)
            {
                g.setColour (remapped[(std::size_t) k] ? colour::accent : colour::border);
                g.drawRoundedRectangle (area.toFloat().reduced (1.0f), radius::md, 1.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int k = 0; editing && k < smartcontrols::knobCount; ++k)
            if (nameArea (k).contains (e.getPosition()))
            {
                remapMenu (k).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (nameArea (k))));
                return;
            }
    }

    void resized() override
    {
        auto side = getLocalBounds().reduced (pad).removeFromLeft (sideWidth - pad);
        headCaption.setBounds (side.removeFromTop (20));
        sourceCaption.setBounds (side.removeFromTop (18));
        side.removeFromTop (8);
        editButton.setBounds (side.removeFromTop (26));
        side.removeFromTop (6);
        resetButton.setBounds (side.removeFromTop (26));
        side.removeFromTop (8);
        hint.setBounds (side);
        hint.setVisible (side.getHeight() >= 40);
        for (int k = 0; k < smartcontrols::knobCount; ++k)
            knobs[(std::size_t) k].setBounds (knobArea (k));
    }

    // tests and shots
    void turnForCheck (int knob, float v) { knobs[(std::size_t) knob].turnTo (v); }
    void setEditingForSnapshot (bool on)
    {
        editButton.setToggleState (on, juce::dontSendNotification);
        editing = on;
        resized();
        repaint();
    }
    juce::String nameForCheck (int knob) const { return nameOf (knob); }
    float valueForCheck (int knob) const { return knobs[(std::size_t) knob].value(); }
    void remapForCheck (int knob, std::optional<smartcontrols::Control> c) { remap (knob, c); }

private:
    struct Section
    {
        juce::Rectangle<int> frame;
        juce::String title;
    };

    juce::Rectangle<int> panelArea() const { return getLocalBounds().reduced (pad).withTrimmedLeft (sideWidth + pad); }

    // the knobs in one row of eight (two rows of four when the dock is narrow), each column: name 18, knob, value 18
    int columns() const { return panelArea().getWidth() >= 8 * 96 + 40 ? 8 : 4; }
    juce::Rectangle<int> cell (int k) const
    {
        const auto cols = columns(), rows = smartcontrols::knobCount / cols;
        auto inner = panelArea().reduced (12, 0).withTrimmedTop (30).withTrimmedBottom (10);
        const auto gapsBefore = groupGapsBefore (k);
        const auto totalGaps = groupGapsBefore (smartcontrols::knobCount) * (cols == 8 ? 1 : 0);
        const auto width = (inner.getWidth() - totalGaps * groupGap) / cols;
        const auto height = inner.getHeight() / rows;
        const auto col = k % cols, row = k / cols;
        const auto x = inner.getX() + col * width + (cols == 8 ? gapsBefore * groupGap : 0);
        return { x, inner.getY() + row * height, width, height };
    }
    juce::Rectangle<int> nameArea (int k) const { return cell (k).removeFromTop (20).reduced (4, 0); }
    juce::Rectangle<int> knobArea (int k) const
    {
        auto c = cell (k).withTrimmedTop (20);
        const auto size = juce::jmin (c.getWidth(), c.getHeight(), 112);
        return c.withSizeKeepingCentre (juce::jmax (40, size), c.getHeight());
    }
    // how many group changes come before knob k (a wider gap between sections)
    int groupGapsBefore (int k) const
    {
        int n = 0;
        for (int i = 1; i <= k && i < smartcontrols::knobCount; ++i)
            n += labels[(std::size_t) i].group != labels[(std::size_t) i - 1].group ? 1 : 0;
        return n;
    }
    std::vector<Section> sections() const
    {
        std::vector<Section> out;
        const auto cols = columns();
        for (int k = 0; k < smartcontrols::knobCount;)
        {
            auto end = k + 1;
            while (end < smartcontrols::knobCount && end % cols != 0 && labels[(std::size_t) end].group == labels[(std::size_t) k].group)
                ++end;
            auto frame = cell (k).getUnion (cell (end - 1)).withTrimmedTop (-4).expanded (2, 0);
            out.push_back ({ frame.withTrimmedTop (0), groupTitle (labels[(std::size_t) k].group) });
            k = end;
        }
        return out;
    }

    juce::String nameOf (int k) const
    {
        const auto& l = labels[(std::size_t) k];
        if (l.name.empty())
            return {};
        if (mapping[(std::size_t) k].source == smartcontrols::Source::target && mapping[(std::size_t) k].target.kind == AutomationTargetKind::effectMix)
            return tr (utf8 (l.name)) + " " + tr ("Mix");
        return tr (utf8 (l.name));
    }

    juce::String valueText (int k, float v) const
    {
        const auto& c = mapping[(std::size_t) k];
        const auto& song = engine.getSong();
        if (c.source == smartcontrols::Source::target)
        {
            if (c.target.kind == AutomationTargetKind::mixerVolume)
            {
                const auto db = juce::Decibels::gainToDecibels (v / unityFader, -60.0f);
                return db <= -60.0f ? juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e dB")) : juce::String (db, 1) + " dB";
            }
            if (c.target.kind == AutomationTargetKind::mixerPan)
            {
                const auto p = juce::roundToInt ((v - 0.5f) * 200.0f);
                return p == 0 ? tr ("Centre") : juce::String (std::abs (p)) + (p < 0 ? " L" : " R");
            }
            if (c.target.kind == AutomationTargetKind::effectParam && owner.insert > 0 && owner.insert < (int) song.mixer.size()
                && c.target.slot >= 0 && c.target.slot < (int) song.mixer[(std::size_t) owner.insert].effects.size())
            {
                const auto& fx = song.mixer[(std::size_t) owner.insert].effects[(std::size_t) c.target.slot];
                return utf8 (formatEffectParam (fx.kind, c.target.param, v, song.bpm));
            }
        }
        return juce::String (v * 10.0f, 1); // (GarageBand's 0…10 scale)
    }

    void turn (int k, float v)
    {
        const auto o = owner;
        const auto c = mapping[(std::size_t) k];
        engine.editSong ([o, c, v] (Song& s) { smartcontrols::setValue (s, o, c, v); });
    }

    void remap (int k, std::optional<smartcontrols::Control> c)
    {
        const auto o = owner;
        engine.editSong ([o, k, c] (Song& s) { smartcontrols::remap (s, o, k, c); });
        refresh();
    }

    static constexpr int groupGap = 14;
    AudioEngine& engine;
    int track = -1;
    smartcontrols::Owner owner;
    smartcontrols::Mapping mapping {};
    std::array<smartcontrols::Label, smartcontrols::knobCount> labels {};
    std::array<bool, smartcontrols::knobCount> remapped {};
    std::array<BigKnob, smartcontrols::knobCount> knobs;
    juce::Label headCaption, sourceCaption, hint;
    juce::TextButton editButton, resetButton;
    bool editing = false;
};

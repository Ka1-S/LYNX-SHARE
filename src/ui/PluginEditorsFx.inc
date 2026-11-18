// Displays of the round-105 arrangements (core/PluginLayoutFx.inc): included inside NativeEffectEditor's class body.
// Band tables (EQUO, Multiband Delay) are drawn as bars and edited by dragging across them.

    // --- band tables ------------------------------------------------------------------------------------

    struct Bars
    {
        std::vector<int> params;           // the editor parameter of each bar (state controls)
        std::vector<juce::String> labels;  // under each bar
        bool bipolar = false;              // pan: drawn from the centre
        bool plain = false;                // round 137: plain effect parameters (not state controls)
    };

    Bars barsFor (const juce::String& id) const
    {
        Bars b;
        const auto* fx = current();
        if (fx == nullptr)
            return b;
        const auto plain = (int) effectInfo (kind).params.size();
        if (id == "equobars")
        {
            // page digits: target ×1 (vol, pan, send), bank ×3 (M = the master bank 8, then 1…8)
            const auto target = page % 3, option = (page / 3) % 9;
            const auto bank = option == 0 ? equofx::numBanks - 1 : option - 1;
            for (int band = 0; band < equofx::numBands; ++band)
            {
                b.params.push_back (plain + (bank * equofx::numBands + band) * equofx::numTargets + target);
                const auto hz = 20.0 * std::pow (2.0, band / 3.0);
                b.labels.push_back (band % 3 != 0 ? juce::String() : hz >= 1000.0 ? juce::String (hz / 1000.0, 2) + "k" : juce::String (juce::roundToInt (hz)));
            }
            b.bipolar = target == 1;
        }
        else if (id == "rhythmbars") // round 137: Echoform's rhythm taps (plain parameters), one bar per heard tap
        {
            namespace ef = echosuite::echoform;
            const auto bpm = engine.getSong().bpm > 1.0 ? engine.getSong().bpm : 120.0;
            const auto taps = ef::rhythmTaps (fx->params, bpm);
            for (int k = 0; k < (int) taps.size(); ++k)
            {
                b.params.push_back (ef::rhythmTap1 + k);
                b.labels.push_back (juce::String (k + 1));
            }
            b.plain = true;
        }
        else if (id == "mbdbars")
        {
            const auto& table = stateTable();
            const auto cells = mbdelay::numBanks * 48;
            const auto target = juce::jlimit (0, 2, table.get (fx->state, cells).value_or (0));
            const auto bank = juce::jlimit (0, mbdelay::numBanks - 1, table.get (fx->state, cells + 1).value_or (mbdelay::numBanks - 1));
            static const char* const freqs[16] { "31", "47", "71", "112", "172", "264", "413", "624", "958", "1.5k", "2.3k", "3.6k",
                                                 "5.5k", "8.4k", "12.7k", "19.0k" };
            for (int band = 0; band < mbdelay::numBands; ++band)
            {
                b.params.push_back (plain + bank * 48 + target * 16 + band);
                b.labels.push_back (freqs[band]);
            }
            b.bipolar = target == 2;
        }
        return b;
    }

    static bool isBars (const LayoutItem& item)
    {
        return item.type == Item::display && item.display != nullptr
               && (std::strcmp (item.display, "equobars") == 0 || std::strcmp (item.display, "mbdbars") == 0
                   || std::strcmp (item.display, "rhythmbars") == 0);
    }

    juce::Rectangle<float> barsArea (const LayoutItem& item) const
    {
        return rectOf (item).reduced (4.0f * scale()).withTrimmedBottom (14.0f * scale());
    }

    void paintBars (juce::Graphics& g, const LayoutItem& item, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        const auto b = barsFor (item.display);
        const auto area = barsArea (item);
        const auto n = (int) b.params.size();
        if (n == 0)
            return;
        const auto w = area.getWidth() / (float) n;
        const auto c = fx.enabled ? colour::accent : colour::textDisabled;
        for (int i = 0; i < n; ++i)
        {
            const auto cell = juce::Rectangle<float> (area.getX() + w * (float) i, area.getY(), w, area.getHeight()).reduced (1.0f, 0.0f);
            g.setColour (colour::bg2);
            g.fillRect (cell);
            const auto v = valueOf (b.params[(std::size_t) i]);
            const auto y = cell.getBottom() - cell.getHeight() * v;
            const auto base = b.bipolar ? cell.getCentreY() : cell.getBottom();
            g.setColour (c.withAlpha (0.25f));
            g.fillRect (cell.withTop (juce::jmin (y, base)).withBottom (juce::jmax (y, base)));
            g.setColour (c);
            g.fillRect (cell.withTop (y - 1.5f * s).withHeight (3.0f * s));
            g.setColour (colour::textSecondary);
            g.setFont (font (juce::jmax (7.0f, 8.5f * s)));
            g.drawFittedText (b.labels[(std::size_t) i], juce::Rectangle<float> (cell.getX() - 6.0f * s, area.getBottom() + 1.0f, cell.getWidth() + 12.0f * s, 12.0f * s).toNearestInt(),
                              juce::Justification::centred, 1, 0.6f);
        }
        if (b.bipolar)
        {
            g.setColour (colour::gridBar);
            g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
        }
    }

    // drag across the bars: every bar the mouse passes takes the mouse's height (one edit per mouse event)
    void barsMouse (const juce::MouseEvent& e, const LayoutItem& item, bool start)
    {
        const auto* fx = current();
        const auto b = barsFor (item.display);
        const auto area = barsArea (item);
        if (fx == nullptr || b.params.empty())
            return;
        const auto n = (int) b.params.size();
        const auto barAt = [&] (float x) { return juce::jlimit (0, n - 1, (int) ((x - area.getX()) / area.getWidth() * (float) n)); };
        const auto v = juce::jlimit (0.0f, 1.0f, (area.getBottom() - e.position.y) / juce::jmax (1.0f, area.getHeight()));
        const auto to = barAt (e.position.x);
        const auto from = start ? to : barAt (barsLastX);
        barsLastX = e.position.x;
        if (b.plain)
        {
            std::vector<std::pair<int, float>> changes;
            for (int i = juce::jmin (from, to); i <= juce::jmax (from, to); ++i)
                changes.push_back ({ b.params[(std::size_t) i], v });
            setParams (changes);
            ui::showValueTip (*this, rectOf (item).toNearestInt(), valueText (b.params[(std::size_t) to], v));
            return;
        }
        auto bytes = fx->state;
        const auto& table = stateTable();
        for (int i = juce::jmin (from, to); i <= juce::jmax (from, to); ++i)
        {
            const auto p = b.params[(std::size_t) i];
            bytes = table.with (bytes, plugui::stateIndex (kind, p), plugui::stateRaw (kind, p, v));
        }
        if (bytes != fx->state)
            engine.editSong ([t = track, sl = slot, bytes] (Song& song) { song.mixer[(std::size_t) t].effects[(std::size_t) sl].state = bytes; });
        ui::showValueTip (*this, rectOf (item).toNearestInt(), valueText (b.params[(std::size_t) to], v));
    }

    float barsLastX = 0.0f;
    bool barsDragging = false;

    // --- round 144: Echoform's rhythm editor (EchoBoy's role) -----------------------------------------------
    // The heard taps on the pattern's time axis (slot 0 = the input, at 0): drag a tap up / down for its level and
    // sideways for its time (Free / Grid; a sideways drag in Even turns the even pattern into Free taps first);
    // double-click empty space = a new tap there, double-click a tap = remove it (Free / Grid) or its level back to
    // 100 % (Even); the Patterns button loads one of LYNX's own tap patterns.

    static bool isRhythmEdit (const LayoutItem& item)
    {
        return item.type == Item::display && item.display != nullptr && std::strcmp (item.display, "rhythmedit") == 0;
    }
    static bool isRhythmPatterns (const LayoutItem& item)
    {
        return item.type == Item::display && item.display != nullptr && std::strcmp (item.display, "rhythmpatterns") == 0;
    }
    double rhythmBpm() const { return engine.getSong().bpm > 1.0 ? engine.getSong().bpm : 120.0; }
    juce::Rectangle<float> rhythmArea (const LayoutItem& item) const
    {
        return rectOf (item).reduced (8.0f * scale(), 6.0f * scale()).withTrimmedBottom (12.0f * scale());
    }
    // the heard taps: x on screen, the tap's own level (0 … 1, before the pattern's normalisation)
    struct RhythmHandle { int k; float x; float level; double ms; };
    std::vector<RhythmHandle> rhythmHandles (const LayoutItem& item) const
    {
        namespace ef = echosuite::echoform;
        std::vector<RhythmHandle> out;
        const auto* fx = current();
        if (fx == nullptr)
            return out;
        const auto area = rhythmArea (item);
        const auto length = ef::rhythmLengthMs (fx->params, rhythmBpm());
        const auto taps = ef::rhythmTaps (fx->params, rhythmBpm());
        for (int k = 0; k < (int) taps.size(); ++k)
            out.push_back ({ k, area.getX() + area.getWidth() * (float) (taps[(std::size_t) k].ms / juce::jmax (1.0, length)),
                             fx->params[(std::size_t) (ef::rhythmTap1 + k)], taps[(std::size_t) k].ms });
        return out;
    }
    int rhythmNearest (const LayoutItem& item, juce::Point<float> p) const
    {
        int best = -1;
        auto bestD = 7.0f * scale();
        for (const auto& h : rhythmHandles (item))
            if (const auto d = std::abs (h.x - p.x); d < bestD)
                best = h.k, bestD = d;
        return best;
    }

    void paintRhythmEdit (juce::Graphics& g, const LayoutItem& item, const EffectSlot& fx)
    {
        namespace ef = echosuite::echoform;
        using namespace theme;
        const auto s = scale();
        const auto area = rhythmArea (item);
        const auto bpm = rhythmBpm();
        const auto beats = ef::rhythmBeatsOf (fx.params[ef::rhythmLength]);
        const auto edit = ef::rhythmEditOf (fx.params[ef::rhythmEdit]);
        const auto c = fx.enabled ? colour::accent : colour::textDisabled;
        // grid: the beats, and the Grid edit's note value (1/16 lines otherwise)
        const auto gridBeats = edit == ef::RhythmEdit::grid ? ef::rhythmGridBeats (ef::rhythmGridOf (fx.params[ef::rhythmGrid])) : 0.25;
        const auto lines = (int) std::round ((double) beats / gridBeats);
        if (lines <= 256)
            for (int i = 1; i < lines; ++i)
            {
                const auto x = area.getX() + area.getWidth() * (float) i / (float) lines;
                g.setColour (std::fmod ((double) i * gridBeats, 1.0) < 1e-6 ? colour::gridBar : colour::gridBeat);
                g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            }
        g.setFont (font (juce::jmax (7.0f, 8.5f * s)));
        g.setColour (colour::textDisabled);
        for (int b = 0; b < beats; ++b)
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (area.getX() + area.getWidth() * (float) b / (float) beats + 2.0f * s, area.getBottom() + 1.0f, 20.0f * s, 11.0f * s),
                        juce::Justification::centredLeft);
        // slot 0: the input itself (heard through the feedback when the pattern starts again)
        g.setColour (c.withAlpha (0.5f));
        g.drawRect (juce::Rectangle<float> (area.getX() - 1.5f * s, area.getY(), 3.0f * s, area.getHeight()), 1.0f);
        const auto hot = rhythmDrag >= 0 ? rhythmDrag : (hoverItem >= 0 && &activeLayout().items[(std::size_t) hoverItem] == &item ? rhythmNearest (item, mouseAt) : -1);
        for (const auto& h : rhythmHandles (item))
        {
            const auto w = juce::jmax (4.0f, 6.0f * s);
            const auto bar = juce::Rectangle<float> (h.x - w * 0.5f, area.getBottom() - area.getHeight() * h.level, w, area.getHeight() * h.level);
            g.setColour (c.withAlpha (h.k == hot ? 0.55f : 0.3f));
            g.fillRect (bar);
            g.setColour (c);
            g.fillRect (bar.withHeight (3.0f * s).withY (bar.getY() - 1.5f * s));
            g.setColour (colour::textSecondary);
            g.drawText (juce::String (h.k + 1), juce::Rectangle<float> (h.x - 10.0f * s, area.getY() - 1.0f, 20.0f * s, 10.0f * s), juce::Justification::centred);
        }
        // the read-out: the edit, the length, the tempo
        const auto editName = edit == ef::RhythmEdit::even ? tr ("Even") : edit == ef::RhythmEdit::free ? tr ("Free") : tr ("Grid") + " " + valueText (ef::rhythmGrid, fx.params[ef::rhythmGrid]);
        g.setColour (colour::textSecondary);
        g.drawText (editName + "  " + valueText (ef::rhythmLength, fx.params[ef::rhythmLength]) + "  " + juce::String (ef::tempoFor (fx.params, bpm), 1) + " BPM",
                    rectOf (item).reduced (6.0f * s, 2.0f * s), juce::Justification::bottomRight);
    }

    void paintRhythmPatterns (juce::Graphics& g, const LayoutItem& item)
    {
        using namespace theme;
        const auto s = scale();
        const auto r = rectOf (item);
        g.setColour (colour::bg2);
        g.fillRoundedRectangle (r, radius::sm * s);
        g.setColour (colour::textPrimary);
        g.setFont (font (juce::jmax (8.0f, 10.0f * s)));
        g.drawText (tr ("Patterns") + juce::String::fromUTF8 (" \u25be"), r, juce::Justification::centred);
    }

    void rhythmPatternsMenu (const LayoutItem& item)
    {
        namespace ef = echosuite::echoform;
        juce::PopupMenu menu;
        for (int i = 0; i < ef::rhythmPresetCount(); ++i)
            menu.addItem (tr (ef::rhythmPresetName (i)), [this, i] { bar.beginGesture(); setParams (ef::rhythmPresetEdits (i)); repaint(); });
        menu.addSeparator();
        menu.addItem (tr ("Even spacing"), [this] { setParams ({ { ef::rhythmEdit, 0.0f } }); repaint(); });
        menu.addItem (tr ("All taps at 100 %"), [this] {
            std::vector<std::pair<int, float>> e;
            for (int k = 0; k < 15; ++k)
                e.push_back ({ ef::rhythmTap1 + k, 1.0f });
            setParams (e);
            repaint();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (rectOf (item).toNearestInt())));
    }

    void rhythmMouse (const juce::MouseEvent& e, const LayoutItem& item, bool start)
    {
        namespace ef = echosuite::echoform;
        const auto* fx = current();
        if (fx == nullptr)
            return;
        const auto area = rhythmArea (item);
        if (start)
        {
            rhythmDrag = rhythmNearest (item, e.position);
            rhythmStartX = e.position.x;
            if (rhythmDrag >= 0)
                bar.beginGesture();
            return;
        }
        if (rhythmDrag < 0)
            return;
        const auto k = rhythmDrag;
        const auto level = juce::jlimit (0.0f, 1.0f, (area.getBottom() - e.position.y) / juce::jmax (1.0f, area.getHeight()));
        std::vector<std::pair<int, float>> changes { { ef::rhythmTap1 + k, level } };
        auto params = fx->params;
        if (ef::rhythmEditOf (params[ef::rhythmEdit]) == ef::RhythmEdit::even && std::abs (e.position.x - rhythmStartX) > 4.0f * scale())
            for (const auto& c : ef::rhythmFreeFromEven (params, rhythmBpm())) // moving a tap in time: Free taps from here on
            {
                changes.push_back (c);
                params[(std::size_t) c.first] = c.second;
            }
        juce::String tip = valueText (ef::rhythmTap1 + k, level);
        if (ef::rhythmEditOf (params[ef::rhythmEdit]) != ef::RhythmEdit::even)
        {
            const auto length = ef::rhythmLengthMs (params, rhythmBpm());
            const auto ms = (double) juce::jlimit (0.0f, 1.0f, (e.position.x - area.getX()) / juce::jmax (1.0f, area.getWidth())) * length;
            const auto t = ef::rhythmTimeValue (params, ms, rhythmBpm());
            changes.push_back ({ ef::rhythmTime1 + k, t });
            params[(std::size_t) (ef::rhythmTime1 + k)] = t;
            tip = juce::String (juce::roundToInt (ef::rhythmTapMs (params, k, rhythmBpm()))) + " ms  " + tip;
        }
        setParams (changes);
        ui::showValueTip (*this, rectOf (item).toNearestInt(), tr ("Tap") + " " + juce::String (k + 1) + ": " + tip);
    }

    // double-click: a new tap at the mouse (Free / Grid), or the tap under it removed / back to 100 %
    void rhythmDoubleClick (const juce::MouseEvent& e, const LayoutItem& item)
    {
        namespace ef = echosuite::echoform;
        const auto* fx = current();
        if (fx == nullptr)
            return;
        auto params = fx->params;
        const auto area = rhythmArea (item);
        const auto k = rhythmNearest (item, e.position);
        const auto even = ef::rhythmEditOf (params[ef::rhythmEdit]) == ef::RhythmEdit::even;
        const auto heard = (int) ef::rhythmTaps (params, rhythmBpm()).size();
        std::vector<std::pair<int, float>> changes;
        if (k >= 0 && even)
            changes.push_back ({ ef::rhythmTap1 + k, 1.0f });
        else if (k >= 0) // remove tap k: the later ones move down a place
        {
            for (int j = k; j + 1 < heard; ++j)
            {
                changes.push_back ({ ef::rhythmTime1 + j, params[(std::size_t) (ef::rhythmTime1 + j + 1)] });
                changes.push_back ({ ef::rhythmTap1 + j, params[(std::size_t) (ef::rhythmTap1 + j + 1)] });
            }
            changes.push_back ({ ef::rhythmRepeats, (float) juce::jmax (0, heard - 1) / 15.0f });
        }
        else if (heard < 15)
        {
            if (even)
                for (const auto& c : ef::rhythmFreeFromEven (params, rhythmBpm()))
                {
                    changes.push_back (c);
                    params[(std::size_t) c.first] = c.second;
                }
            const auto ms = (double) juce::jlimit (0.0f, 1.0f, (e.position.x - area.getX()) / juce::jmax (1.0f, area.getWidth())) * ef::rhythmLengthMs (params, rhythmBpm());
            changes.push_back ({ ef::rhythmTime1 + heard, ef::rhythmTimeValue (params, ms, rhythmBpm()) });
            changes.push_back ({ ef::rhythmTap1 + heard, juce::jlimit (0.05f, 1.0f, (area.getBottom() - e.position.y) / juce::jmax (1.0f, area.getHeight())) });
            changes.push_back ({ ef::rhythmRepeats, (float) (heard + 1) / 15.0f });
        }
        if (! changes.empty())
        {
            bar.beginGesture();
            setParams (changes);
            repaint();
        }
    }

    int rhythmDrag = -1;
    float rhythmStartX = 0.0f;

    // --- round 190ld (DP-32): Fruity Vocoder's live band levels ------------------------------------------------------
    // The effect publishes up to 32 columns, 6 bits each (0 = −60 dB or less … 63 = 0 dB), four per meter output 0…7
    // (core/Effects.cpp Vocoder::controlOutput). The timer repaints the band display only when a column changed (one
    // last frame when the signal stops); a held HOLD freezes the envelopes, so nothing is redrawn.
    std::array<std::uint32_t, 8> vocBandsSeen {};
    int vocHoldParam = -1; // the HOLD button held down (released on mouse up)

    float vocoderBandLevel (int column) const
    {
        if (column < 0 || column >= 32)
            return 0.0f;
        return (float) ((vocBandsSeen[(std::size_t) (column / 4)] >> (6 * (column % 4))) & 63u) / 63.0f;
    }

    void vocoderBandsTick()
    {
        std::array<std::uint32_t, 8> now {};
        for (int k = 0; k < 8; ++k)
            now[(std::size_t) k] = (std::uint32_t) juce::jlimit (0.0f, 16777215.0f, liveMeter (k));
        if (now == vocBandsSeen)
            return;
        vocBandsSeen = now;
        for (const auto& item : activeLayout().items)
            if (item.type == Item::display && item.display != nullptr && std::strcmp (item.display, "vocbands") == 0 && visible (item))
                repaint (rectOf (item).getSmallestIntegerContainer().expanded (1));
    }

    // --- the other displays -----------------------------------------------------------------------------

    // true = drawn (id is one of round 105's displays)
    bool paintFxDisplay (juce::Graphics& g, const juce::String& id, const LayoutItem& item, juce::Rectangle<float> inner, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        const auto curveColour = fx.enabled ? colour::accent : colour::textDisabled;

        if (id == "equobars" || id == "mbdbars" || id == "rhythmbars")
        {
            paintBars (g, item, fx);
            return true;
        }
        if (id == "rhythmedit") // round 144
        {
            paintRhythmEdit (g, item, fx);
            return true;
        }
        if (id == "rhythmpatterns")
        {
            paintRhythmPatterns (g, item);
            return true;
        }
        if (id == "vocbands") // Fruity Vocoder: one column per band, each with its live level (round 190ld, DP-32)
        {
            const auto bands = 4 << juce::jlimit (0, 5, juce::roundToInt (param01 ("Bands") * 5.0f));
            const auto shown = juce::jmin (bands, 32);
            const auto w = inner.getWidth() / (float) shown;
            for (int i = 0; i < shown; ++i)
            {
                const auto cell = juce::Rectangle<float> (inner.getX() + w * (float) i, inner.getY(), w, inner.getHeight()).reduced (1.0f, 0.0f);
                g.setColour (colour::bg2);
                g.fillRect (cell);
                // FL 2026 (capture 190ld B4, fl-vocoder-playing-*): a light fill per band rising with the band's level
                // while the signal plays (LYNX: its curve colour, translucent); frozen while HOLD is held
                if (const auto level = vocoderBandLevel (i); level > 0.0f)
                {
                    g.setColour (curveColour.withAlpha (0.42f));
                    g.fillRect (cell.withTop (cell.getBottom() - cell.getHeight() * level));
                }
                g.setColour (curveColour.withAlpha (0.8f));
                g.fillRect (cell.withTop (cell.getY() + cell.getHeight() * 0.32f).withHeight (2.0f * s));
            }
            if (bands > shown)
            {
                g.setColour (colour::textSecondary);
                g.setFont (font (juce::jmax (8.0f, 9.0f * s)));
                g.drawText (juce::String (bands), inner.reduced (4.0f * s), juce::Justification::bottomRight);
            }
            return true;
        }
        if (id == "fsplit") // Frequency Splitter: the low / mid / high bands over 20 Hz…20 kHz at their levels
        {
            const auto fOf = [] (float v) { return 30.0 * std::pow (2.0, 9.0589 * v); };
            static const int slopes[8] { 6, 12, 18, 24, 36, 48, 72, 96 };
            const auto slopeOf = [] (float v) { return slopes[juce::jlimit (0, 7, (int) (v / 0.125f))]; };
            const auto levelOf = [] (float v) { return v < 0.5f ? 2.0f * v : 1.0f + 13.8866f * (v - 0.5f); };
            const auto xOf = [&] (double hz) { return (float) (std::log (juce::jlimit (20.0, 20000.0, hz) / 20.0) / std::log (1000.0)); };
            const auto three = param01 ("3 bands") >= 0.5f;
            auto f1 = fOf (param01 ("Low/mid freq.")), f2 = fOf (param01 ("Mid/high freq."));
            const auto s1 = slopeOf (param01 ("Low/mid slope")), s2 = slopeOf (param01 ("Mid/high slope"));
            g.setFont (font (juce::jmax (7.0f, 8.5f * s)));
            for (const double hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            {
                const auto x = inner.getX() + inner.getWidth() * xOf (hz);
                g.setColour (colour::gridBeat);
                g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
                g.setColour (colour::textDisabled);
                g.drawText (hz >= 1000.0 ? juce::String (juce::roundToInt (hz / 1000.0)) + "k" : juce::String (juce::roundToInt (hz)),
                            juce::Rectangle<float> (x + 2.0f * s, inner.getBottom() - 12.0f * s, 30.0f * s, 11.0f * s), juce::Justification::centredLeft);
            }
            g.setColour (colour::gridBar);
            g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());
            const auto dbAt = [] (double hz, double fc, int slope, bool low) {
                const auto oct = std::log2 (hz / fc) * (low ? 1.0 : -1.0);
                return oct <= 0.0 ? -3.0 * std::pow (2.0, oct * 2.0) : -3.0 - slope * oct; // a smooth knee at the crossover
            };
            const char* const levels[3] { "Low level", "Mid level", "High level" };
            const juce::Colour colours[3] { juce::Colour (0xffe5383b), juce::Colour (0xffe8b631), juce::Colour (0xff5fd35a) };
            for (int band = 0; band < (three ? 3 : 2); ++band)
            {
                const auto gainDb = 20.0 * std::log10 (juce::jmax (1.0e-4f, levelOf (param01 (levels[three ? band : band * 2]))));
                strokeBand (g, inner, colours[three ? band : band * 2].withAlpha (fx.enabled ? 1.0f : 0.4f), [&] (double hz) {
                    double db = 0.0;
                    if (band == 0)
                        db = dbAt (hz, f1, s1, true);
                    else if (band == 1 && three)
                        db = dbAt (hz, f1, s1, false) + dbAt (hz, f2, s2, true);
                    else
                        db = dbAt (hz, three ? f2 : f1, three ? s2 : s1, false);
                    return (float) ((db + gainDb + 48.0) / 96.0); // ±48 dB
                });
            }
            return true;
        }
        if (id == "psgraph") // Pitch Shifter (music page): the pitch as a line over the grid
        {
            grid8 (g, inner);
            const auto y = inner.getBottom() - inner.getHeight() * param01 ("Pitch");
            g.setColour (curveColour);
            g.drawHorizontalLine ((int) y, inner.getX(), inner.getRight());
            return true;
        }
        if (id == "vcxmap" || id == "vcxcarrier") // Vocodex: the band gain map (read-only) and the carrier
        {
            const auto st = vocodex::parseState (vocodex::splitInstallData (fx.state).first);
            if (id == "vcxcarrier")
            {
                g.setColour (colour::textSecondary);
                g.setFont (font (juce::jmax (8.0f, 10.0f * s)));
                juce::String text = tr ("Carrier: own input");
                if (st && st->internalCarrier())
                    text = tr ("Carrier synthesizer") + ": " + (st->tone.empty() ? juce::String ("-") : juce::String (st->tone));
                else if (st && st->carIn > 0)
                    text = tr ("Carrier: side input {0}", { juce::String (st->carIn) });
                g.drawText (text, inner, juce::Justification::centredLeft);
                return true;
            }
            grid8 (g, inner);
            g.setColour (colour::textDisabled);
            g.setFont (font (juce::jmax (8.0f, 9.0f * s), true));
            g.drawText (caps (tr ("Band gain multiplier")), inner.reduced (4.0f * s), juce::Justification::topLeft);
            if (st && ! st->maps[0].x.empty())
            {
                const auto& m = st->maps[0];
                const auto last = juce::jmax (1e-9, m.x.back());
                juce::Path path;
                for (std::size_t i = 0; i < m.x.size(); ++i)
                {
                    const juce::Point<float> pt { inner.getX() + inner.getWidth() * (float) (m.x[i] / last),
                                                  inner.getBottom() - inner.getHeight() * (float) juce::jlimit (0.0, 1.0, m.y[i]) };
                    if (i == 0)
                        path.startNewSubPath (pt);
                    else
                        path.lineTo (pt);
                    g.setColour (curveColour);
                    g.drawEllipse (juce::Rectangle<float> (6.0f * s, 6.0f * s).withCentre (pt), 1.0f);
                }
                g.strokePath (path, juce::PathStrokeType (juce::jmax (1.5f, 2.0f * s)));
            }
            return true;
        }
        if (id == "irinfo" || id == "irwave") // Fruity Convolver: the impulse response
        {
            const auto ir = convolverAudio (fx);
            if (id == "irinfo")
            {
                g.setColour (colour::textSecondary);
                g.setFont (font (juce::jmax (8.0f, 10.0f * s)));
                if (ir.l.empty())
                    g.drawText (tr ("No impulse response"), inner, juce::Justification::centredLeft);
                else
                    g.drawText (juce::String (juce::roundToInt (ir.rate)) + " Hz    " + (ir.r.empty() ? tr ("Mono") : tr ("Stereo")) + "    "
                                    + juce::String ((double) ir.l.size() / ir.rate, 3) + " s",
                                inner, juce::Justification::centredLeft);
                return true;
            }
            g.setColour (colour::gridBeat);
            g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());
            if (ir.l.empty())
                return true;
            const auto columns = juce::jmax (1, (int) inner.getWidth());
            float peak = 1.0e-9f;
            for (auto x : ir.l)
                peak = juce::jmax (peak, std::abs (x));
            g.setColour (curveColour.withAlpha (0.9f));
            for (int c = 0; c < columns; ++c)
            {
                const auto a = ir.l.size() * (std::size_t) c / (std::size_t) columns, z = ir.l.size() * (std::size_t) (c + 1) / (std::size_t) columns;
                float m = 0.0f;
                for (auto i = a; i < z && i < ir.l.size(); ++i)
                    m = juce::jmax (m, std::abs (ir.l[i]));
                const auto h = inner.getHeight() * 0.5f * m / peak;
                g.drawVerticalLine ((int) inner.getX() + c, inner.getCentreY() - h, inner.getCentreY() + h + 1.0f);
            }
            return true;
        }
        return false;
    }

    void grid8 (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setColour (theme::colour::gridBeat);
        for (int i = 1; i < 8; ++i)
        {
            g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / 8.0f), area.getY(), area.getBottom());
            g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * (float) i / 8.0f), area.getX(), area.getRight());
        }
    }

    template <typename F>
    void strokeBand (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour c, F yOfHz)
    {
        juce::Path p;
        const auto steps = juce::jmax (32, (int) area.getWidth() / 2);
        for (int i = 0; i <= steps; ++i)
        {
            const auto x = (float) i / (float) steps;
            const auto hz = 20.0 * std::pow (1000.0, (double) x);
            const auto y = juce::jlimit (0.0f, 1.0f, yOfHz (hz));
            const juce::Point<float> pt { area.getX() + area.getWidth() * x, area.getBottom() - area.getHeight() * y };
            if (i == 0)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);
        }
        auto fill = p;
        fill.lineTo (area.getRight(), area.getBottom());
        fill.lineTo (area.getX(), area.getBottom());
        fill.closeSubPath();
        g.setColour (c.withAlpha (0.16f));
        g.fillPath (fill);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (juce::jmax (1.5f, 2.0f * scale())));
    }

    // the impulse response of a Convolver slot, decoded once per state
    const convolver::Audio& convolverAudio (const EffectSlot& fx)
    {
        if (fx.state != irState)
        {
            irState = fx.state;
            irAudio = {};
            std::vector<std::uint8_t> fl, file;
            if (convolver::unpackBlob (fx.state, fl, file))
                if (const auto st = convolver::parseState (fl))
                    if (auto a = convolver::decodeAudio (st->fromFile ? file : st->wav))
                        irAudio = std::move (*a);
        }
        return irAudio;
    }

    std::vector<std::uint8_t> irState;
    convolver::Audio irAudio;

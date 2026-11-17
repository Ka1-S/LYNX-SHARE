// Round gaps190kf (item 3; included by ui/BrowserComponent.cpp inside namespace lynx, after BrowserTagsLive.inc): the
// Loop Browser of the Sample browser window — GarageBand's Loop Browser [GUIDE "Find loops" picture
// gaps190kf/loops/ev/gb-loop-browser.png]: the Sound Packs line (LYNX: Places), the category buttons Instrument / Genre
// / Descriptors between the reset button and the Favorites button, the grid of keyword buttons (4 columns; picked = lit,
// several = all of them; dimmed = no loop matches), then Scale (Any / Minor / Major / Neither / Good for Both), LYNX's
// Tempo menu and the results header with the Loop Types button (the colour bars: All / Audio / MIDI / Pattern loops).
// Everything is drawn by this one component (no child components, no timer): it paints only when something changed.
// Logic in core/LoopKeywords.h. LYNX vectors; nothing of Apple's artwork.

namespace loopicons
{
// GarageBand's Loop Types button: three stacked colour bars (audio / MIDI / pattern), LYNX's colours
inline void drawTypeBars (juce::Graphics& g, juce::Rectangle<float> r, int types)
{
    const juce::Colour colours[] { juce::Colour (0xff3d7fd9), juce::Colour (0xff45b05a), juce::Colour (0xffd9a62e) };
    const auto h = r.getHeight() / 3.0f;
    for (int k = 0; k < 3; ++k)
    {
        const auto on = (types >> k & 1) != 0;
        g.setColour (on ? colours[k] : colours[k].withAlpha (0.25f));
        g.fillRect (r.getX(), r.getY() + h * (float) k, r.getWidth(), h - 0.5f);
    }
}
// the reset button's circled cross
inline void drawReset (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const auto d = juce::jmin (r.getWidth(), r.getHeight());
    const auto circle = r.withSizeKeepingCentre (d, d);
    g.setColour (c);
    g.fillEllipse (circle);
    const auto x = circle.reduced (d * 0.3f);
    g.setColour (c.contrasting (0.9f));
    g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.4f);
    g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.4f);
}
// the Favorites button's heart
inline void drawHeart (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const auto s = juce::jmin (r.getWidth(), r.getHeight());
    const auto b = r.withSizeKeepingCentre (s, s * 0.9f);
    juce::Path p;
    const auto cx = b.getCentreX(), top = b.getY() + b.getHeight() * 0.28f;
    p.startNewSubPath (cx, b.getBottom());
    p.cubicTo (b.getX() - s * 0.05f, b.getY() + b.getHeight() * 0.55f, b.getX() + s * 0.05f, b.getY() - s * 0.05f, cx, top);
    p.cubicTo (b.getRight() - s * 0.05f, b.getY() - s * 0.05f, b.getRight() + s * 0.05f, b.getY() + b.getHeight() * 0.55f, cx, b.getBottom());
    p.closeSubPath();
    g.setColour (c);
    g.fillPath (p);
}
} // namespace loopicons

// --- the search row's LOOPS button (the Sample browser window only) --------------------------------------------------
class BrowserComponent::LoopsButton final : public juce::Component, public ui::HintSource
{
public:
    explicit LoopsButton (BrowserComponent& b) : browser (b)
    {
        setRepaintsOnMouseActivity (true);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }
    static constexpr int width = 46;

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto on = browser.isLoopsShown();
        const auto r = getLocalBounds().toFloat().reduced (0.5f, 2.5f);
        g.setColour (on ? colour::accent : isMouseOver() ? colour::bg5 : colour::bg3);
        g.fillRoundedRectangle (r, radius::sm);
        g.setColour (on ? colour::onAccent : colour::textSecondary);
        g.setFont (font (type::caption, true));
        g.drawText (tr ("LOOPS"), getLocalBounds(), juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent&) override { browser.setLoopsShown (! browser.isLoopsShown()); }

    ui::Hint hintAt (juce::Point<int>) override
    {
        ui::Hint h;
        h.text = tr ("Loop Browser: keyword buttons, scale, tempo and loop types filter the loops of your sample folders");
        return h;
    }

private:
    BrowserComponent& browser;
};

// --- the Loop Browser panel -------------------------------------------------------------------------------------------
class BrowserComponent::LoopPanel final : public juce::Component, public ui::HintSource
{
public:
    explicit LoopPanel (BrowserComponent& b) : browser (b) { setRepaintsOnMouseActivity (true); }

    // GarageBand's proportions at LYNX's sizes: rows of 20 px, the category buttons 24 px, keyword buttons 15 px on a
    // 17 px pitch, 4 columns
    static constexpr int pad = 4, rowH = 20, catH = 24, keyH = 15, keyPitch = 17, columns = 4, headerH = 18;

    // the height at `width` for at most `maxHeight` (the grid's rows scroll with the wheel past that)
    int heightFor (int width, int maxHeight)
    {
        juce::ignoreUnused (width);
        const auto rows = gridRows();
        const auto fixed = pad + rowH + 3 + catH + 4 + 4 + rowH + 3 + headerH;
        const auto fit = juce::jmax (2, (maxHeight - fixed) / keyPitch);
        shownRows = juce::jmin (rows, fit);
        scroll = juce::jlimit (0, juce::jmax (0, rows - shownRows), scroll);
        return fixed + shownRows * keyPitch;
    }

    void refresh()
    {
        layout();
        repaint();
    }

    void resized() override { layout(); }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg2);
        const auto mouse = isMouseOver() ? getMouseXYRelative() : juce::Point<int> (-1, -1);
        const auto& filter = browser.loopFilter();

        // Places: Show All ▾ (GarageBand's "Sound Packs: Show All")
        g.setFont (font (type::caption));
        g.setColour (colour::textSecondary);
        const auto placeName = browser.loopPlaceName();
        const auto labelW = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), tr ("Places:")) + 6;
        g.setFont (font (type::caption, true));
        const auto nameW = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), placeName) + 14;
        auto line = placesBox.withSizeKeepingCentre (juce::jmin (placesBox.getWidth(), labelW + nameW), placesBox.getHeight());
        g.setFont (font (type::caption));
        g.drawText (tr ("Places:"), line.removeFromLeft (labelW), juce::Justification::centredLeft, false);
        g.setFont (font (type::caption, true));
        g.setColour (placesBox.contains (mouse) ? colour::textPrimary : colour::textPrimary.withAlpha (0.86f));
        const auto arrow = line.removeFromRight (10);
        g.drawText (placeName, line, juce::Justification::centredLeft, true);
        drawUpDown (g, arrow.toFloat(), colour::textSecondary);

        // ⊗ | Instrument | Genre | Descriptors | ♥
        {
            const auto hover = resetBox.contains (mouse);
            g.setColour (hover ? colour::bg5 : colour::bg4);
            g.fillRoundedRectangle (resetBox.toFloat(), radius::sm);
            loopicons::drawReset (g, resetBox.toFloat().withSizeKeepingCentre (12.0f, 12.0f),
                                  filter.keywords.empty() ? colour::textDisabled : colour::textSecondary);
        }
        static const char* catNames[] { TR_NOOP ("Instrument"), TR_NOOP ("Genre"), TR_NOOP ("Descriptors") };
        for (int c = 0; c < loopkeys::categoryCount; ++c)
        {
            const auto on = (browser.loopCategories() >> c & 1) != 0;
            const auto r = catBoxes[(std::size_t) c];
            g.setColour (on ? colour::accent : r.contains (mouse) ? colour::bg5 : colour::bg4);
            g.fillRoundedRectangle (r.toFloat(), radius::sm);
            g.setColour (on ? colour::onAccent : colour::textPrimary);
            g.setFont (font (type::body, true));
            g.drawText (tr (catNames[c]), r, juce::Justification::centred, true);
        }
        {
            const auto on = filter.favouritesOnly;
            g.setColour (on ? colour::accent : favBox.contains (mouse) ? colour::bg5 : colour::bg4);
            g.fillRoundedRectangle (favBox.toFloat(), radius::sm);
            loopicons::drawHeart (g, favBox.toFloat().withSizeKeepingCentre (12.0f, 11.0f), on ? colour::onAccent : colour::textPrimary);
        }

        // the keyword buttons
        g.setFont (font (type::caption, true));
        const auto& counts = browser.loopKeywordCounts();
        for (const auto& cell : cells)
        {
            if (cell.box.isEmpty())
                continue;
            const auto picked = std::find (filter.keywords.begin(), filter.keywords.end(), cell.id) != filter.keywords.end();
            const auto it = counts.find (cell.id);
            const auto dim = ! picked && it != counts.end() && it->second == 0 && browser.loopsReady();
            const auto hover = cell.box.contains (mouse) && ! dim;
            g.setColour (picked ? colour::accent : hover ? colour::bg5 : colour::bg3);
            g.fillRoundedRectangle (cell.box.toFloat(), 2.5f);
            if (dragFrom >= 0 && &cell == &cells[(std::size_t) dragFrom])
            {
                g.setColour (colour::accent);
                g.drawRoundedRectangle (cell.box.toFloat().reduced (0.5f), 2.5f, 1.0f);
            }
            g.setColour (picked ? colour::onAccent : dim ? colour::textDisabled : colour::textPrimary);
            g.drawText (cell.text, cell.box.reduced (2, 0), juce::Justification::centred, true);
        }
        if (gridRows() > shownRows) // more rows: a thin scroll mark at the grid's right edge
        {
            const auto total = (float) gridRows();
            const auto track = gridArea.toFloat().removeFromRight (2.0f);
            g.setColour (colour::bg5);
            g.fillRect (track.withY (track.getY() + track.getHeight() * (float) scroll / total).withHeight (track.getHeight() * (float) shownRows / total));
        }

        // Scale: [Any ⇅]   Tempo: [Any ⇅]
        drawChooser (g, scaleBox, tr ("Scale:"), scaleName (filter.scale), mouse);
        drawChooser (g, tempoBox, tr ("Tempo:"), tr (loopkeys::tempoRanges()[(std::size_t) juce::jlimit (0, (int) loopkeys::tempoRanges().size() - 1, browser.loopTempoChoice())].label), mouse);

        // the results header: Loop Types | Name ... Tempo · Key | star
        g.setColour (colour::bg1);
        g.fillRect (headerBox);
        g.setColour (colour::border);
        g.fillRect (headerBox.getX(), headerBox.getBottom() - 1, headerBox.getWidth(), 1);
        g.setColour (typesBox.contains (mouse) ? colour::bg5 : colour::bg1);
        g.fillRect (typesBox);
        loopicons::drawTypeBars (g, typesBox.toFloat().withSizeKeepingCentre (12.0f, 11.0f), filter.types);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        g.drawText (tr ("Name"), headerBox.withTrimmedLeft (typesBox.getWidth() + 8), juce::Justification::centredLeft, false);
        g.drawText (tr ("Tempo") + juce::String::fromUTF8 ("  \xc2\xb7  ") + tr ("Key"), headerBox.withTrimmedRight (24), juce::Justification::centredRight, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        dragFrom = -1;
        if (placesBox.contains (p))
            return browser.showLoopPlacesMenu();
        if (resetBox.contains (p))
        {
            auto f = browser.loopFilter();
            f.keywords.clear();
            return browser.setLoopFilter (std::move (f));
        }
        if (favBox.contains (p))
        {
            auto f = browser.loopFilter();
            f.favouritesOnly = ! f.favouritesOnly;
            return browser.setLoopFilter (std::move (f));
        }
        for (int c = 0; c < loopkeys::categoryCount; ++c)
            if (catBoxes[(std::size_t) c].contains (p))
            {
                // GarageBand: a click shows that category's buttons; Shift-click adds more categories
                auto mask = e.mods.isShiftDown() ? browser.loopCategories() ^ (1 << c) : 1 << c;
                return browser.setLoopCategories (mask == 0 ? 1 << c : mask);
            }
        if (scaleBox.contains (p))
            return browser.showLoopScaleMenu();
        if (tempoBox.contains (p))
            return browser.showLoopTempoMenu();
        if (typesBox.contains (p))
            return browser.showLoopTypesMenu();
        if (const auto i = cellAt (p); i >= 0)
        {
            if (e.mods.isPopupMenu())
                return keywordMenu (i);
            dragFrom = i;
            repaint (cells[(std::size_t) i].box);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto from = std::exchange (dragFrom, -1);
        if (from < 0)
            return;
        const auto to = cellAt (e.getPosition());
        const auto& a = cells[(std::size_t) from];
        if (to >= 0 && to != from && e.getDistanceFromDragStart() > 4)
        {
            // GarageBand: "Drag a button over another button to switch the keywords on the two buttons"
            const auto& b = cells[(std::size_t) to];
            auto layout = browser.loopLayout();
            if (a.category == b.category && layout.swap (a.category, a.place, b.place))
                browser.setLoopLayout (std::move (layout));
            else
                repaint();
            return;
        }
        if (to == from)
        {
            auto f = browser.loopFilter();
            if (const auto it = std::find (f.keywords.begin(), f.keywords.end(), a.id); it != f.keywords.end())
                f.keywords.erase (it);
            else
            {
                const auto dimmed = browser.loopsReady() && browser.loopKeywordCounts().count (a.id) > 0 && browser.loopKeywordCounts().at (a.id) == 0;
                if (dimmed)
                {
                    repaint();
                    return; // (GarageBand: a dimmed keyword has no loops; it can't be picked)
                }
                f.keywords.push_back (a.id);
            }
            browser.setLoopFilter (std::move (f));
        }
        else
            repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (! gridArea.contains (e.getPosition()) || gridRows() <= shownRows)
            return;
        const auto before = scroll;
        scroll = juce::jlimit (0, gridRows() - shownRows, scroll + (w.deltaY < 0.0f ? 1 : -1));
        if (scroll != before)
        {
            layout();
            repaint (gridArea);
        }
    }

    ui::Hint hintAt (juce::Point<int> p) override
    {
        ui::Hint h;
        if (placesBox.contains (p))
            h.text = tr ("Places: show the loops of every sample folder, or of one");
        else if (resetBox.contains (p))
            h.text = tr ("Reset: unpick every keyword");
        else if (favBox.contains (p))
            h.text = tr ("Favorites: show only your starred loops (the star at a row's end)");
        else if (std::any_of (catBoxes.begin(), catBoxes.end(), [p] (const juce::Rectangle<int>& r) { return r.contains (p); }))
            h.text = tr ("Keyword category: [LMB] show its keyword buttons  [Shift+LMB] show several categories");
        else if (scaleBox.contains (p))
            h.text = tr ("Scale: loops in a minor key, a major key, with no scale (drums, effects) or good for both");
        else if (tempoBox.contains (p))
            h.text = tr ("Tempo: loops whose tempo (from their name) is in this range");
        else if (typesBox.contains (p))
            h.text = tr ("Loop types: audio loops, MIDI loops and pattern loops");
        else if (const auto i = cellAt (p); i >= 0)
        {
            const auto& cell = cells[(std::size_t) i];
            h.text = tr ("Keyword \"{0}\": [LMB] pick / unpick (several: loops with all of them)  [Drag] onto another button to swap  [RMB] another keyword here",
                         { cell.text });
            if (const auto it = browser.loopKeywordCounts().find (cell.id); it != browser.loopKeywordCounts().end() && browser.loopsReady())
                h.value = juce::String (it->second);
            h.hasMenu = true;
        }
        return h;
    }

    // --loopshot190kf: the keyword buttons' places (a keyword's centre, in this component)
    juce::Point<int> cellCentre (const std::string& id) const
    {
        for (const auto& c : cells)
            if (c.id == id && ! c.box.isEmpty())
                return c.box.getCentre();
        return { -1, -1 };
    }

private:
    struct Cell
    {
        std::string id;
        juce::String text;
        loopkeys::Category category = loopkeys::Category::instrument;
        int place = 0; // its place in the category's layout
        juce::Rectangle<int> box;
    };

    static juce::String scaleName (loopkeys::Scale s)
    {
        using S = loopkeys::Scale;
        switch (s)
        {
            case S::any: return tr ("Any");
            case S::minor: return tr ("Minor");
            case S::major: return tr ("Major");
            case S::neither: return tr ("Neither");
            case S::both: return tr ("Good for Both");
        }
        return {};
    }

    void drawUpDown (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) const
    {
        juce::Path p;
        const auto cx = r.getCentreX(), cy = r.getCentreY();
        p.addTriangle (cx - 3.0f, cy - 1.5f, cx + 3.0f, cy - 1.5f, cx, cy - 5.0f);
        p.addTriangle (cx - 3.0f, cy + 1.5f, cx + 3.0f, cy + 1.5f, cx, cy + 5.0f);
        g.setColour (c);
        g.fillPath (p);
    }

    void drawChooser (juce::Graphics& g, juce::Rectangle<int> box, const juce::String& label, const juce::String& value, juce::Point<int> mouse) const
    {
        using namespace theme;
        g.setFont (font (type::caption));
        g.setColour (colour::textSecondary);
        const auto lw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), label) + 5;
        auto b = box;
        g.drawText (label, b.removeFromLeft (lw), juce::Justification::centredLeft, false);
        const auto field = b.reduced (0, 1);
        g.setColour (field.contains (mouse) ? colour::bg5 : colour::bg4);
        g.fillRoundedRectangle (field.toFloat(), radius::sm);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::caption, true));
        g.drawText (value, field.reduced (6, 0).withTrimmedRight (10), juce::Justification::centredLeft, true);
        drawUpDown (g, field.toFloat().removeFromRight (14.0f), colour::textSecondary);
    }

    std::vector<Cell> gridCells() const
    {
        std::vector<Cell> out;
        const auto& layout = browser.loopLayout();
        for (int c = 0; c < loopkeys::categoryCount; ++c)
            if ((browser.loopCategories() >> c & 1) != 0)
            {
                const auto& order = layout.order[(std::size_t) c];
                for (int i = 0; i < (int) order.size(); ++i)
                    if (const auto* k = loopkeys::find (order[(std::size_t) i]))
                        out.push_back ({ k->id, tr (k->label), (loopkeys::Category) c, i, {} });
            }
        return out;
    }
    int gridRows() const { return ((int) gridCells().size() + columns - 1) / columns; }

    void layout()
    {
        auto area = getLocalBounds().reduced (pad, 0);
        area.removeFromTop (pad);
        placesBox = area.removeFromTop (rowH);
        area.removeFromTop (3);
        {
            auto row = area.removeFromTop (catH);
            resetBox = row.removeFromLeft (catH + 4);
            favBox = row.removeFromRight (catH + 4);
            row.reduce (3, 0);
            const auto w = row.getWidth() / loopkeys::categoryCount;
            for (int c = 0; c < loopkeys::categoryCount; ++c)
                catBoxes[(std::size_t) c] = (c + 1 == loopkeys::categoryCount ? row : row.removeFromLeft (w)).reduced (c == 0 ? 0 : 2, 0).withTrimmedRight (c + 1 == loopkeys::categoryCount ? 0 : 1);
        }
        area.removeFromTop (4);
        cells = gridCells();
        const auto rows = ((int) cells.size() + columns - 1) / columns;
        if (shownRows <= 0)
            shownRows = rows;
        scroll = juce::jlimit (0, juce::jmax (0, rows - shownRows), scroll);
        gridArea = area.removeFromTop (shownRows * keyPitch);
        const auto colW = (float) gridArea.getWidth() / (float) columns;
        for (int i = 0; i < (int) cells.size(); ++i)
        {
            const auto row = i / columns - scroll, col = i % columns;
            if (row < 0 || row >= shownRows)
                continue;
            const auto x0 = gridArea.getX() + juce::roundToInt (colW * (float) col), x1 = gridArea.getX() + juce::roundToInt (colW * (float) (col + 1));
            cells[(std::size_t) i].box = { x0 + 1, gridArea.getY() + row * keyPitch + 1, x1 - x0 - 3, keyH };
        }
        area.removeFromTop (4);
        {
            auto row = area.removeFromTop (rowH);
            scaleBox = row.removeFromLeft (row.getWidth() / 2 - 3);
            row.removeFromLeft (6);
            tempoBox = row;
        }
        area.removeFromTop (3);
        headerBox = area.removeFromTop (headerH).withX (0).withWidth (getWidth());
        typesBox = headerBox.withWidth (24);
    }

    int cellAt (juce::Point<int> p) const
    {
        for (int i = 0; i < (int) cells.size(); ++i)
            if (cells[(std::size_t) i].box.contains (p))
                return i;
        return -1;
    }

    void keywordMenu (int index)
    {
        const auto cell = cells[(std::size_t) index];
        juce::PopupMenu menu;
        ui::addItemHeader (menu, tr ("Keyword button"), cell.text, theme::colour::accent);
        static const char* catNames[] { TR_NOOP ("Instrument"), TR_NOOP ("Genre"), TR_NOOP ("Descriptors") };
        auto safe = juce::Component::SafePointer<BrowserComponent> (&browser);
        // GarageBand: "Control-click the button, choose a keyword category from the shortcut menu, then choose a new
        // keyword from the submenu"
        for (int c = 0; c < loopkeys::categoryCount; ++c)
        {
            juce::PopupMenu sub;
            for (const auto* k : loopkeys::ofCategory ((loopkeys::Category) c))
                sub.addItem (tr (k->label), true, k->id == cell.id, [safe, cell, id = std::string (k->id)] {
                    if (safe == nullptr)
                        return;
                    auto layout = safe->loopLayout();
                    if (layout.place (cell.category, cell.place, id))
                        safe->setLoopLayout (std::move (layout));
                });
            menu.addSubMenu (tr (catNames[c]), sub);
        }
        menu.addSeparator();
        menu.addItem (tr ("Reset the keyword layout"), browser.loopLayout() != loopkeys::Layout::defaults(), false, [safe] {
            if (safe != nullptr)
                safe->setLoopLayout (loopkeys::Layout::defaults());
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }

    BrowserComponent& browser;
    std::vector<Cell> cells;
    juce::Rectangle<int> placesBox, resetBox, favBox, gridArea, scaleBox, tempoBox, headerBox, typesBox;
    std::array<juce::Rectangle<int>, loopkeys::categoryCount> catBoxes;
    int shownRows = 0, scroll = 0, dragFrom = -1;
};


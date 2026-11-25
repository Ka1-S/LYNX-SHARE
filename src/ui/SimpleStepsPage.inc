// The Simple dock's Drum Machine tab (round 190lr, DS-39; included by ui/SimpleDock.cpp). BandLab's drum machine dock
// (reference /mnt/e/lynx-work/bandlab190p/shots/09-drum-machine-dock.png): a top row with Length, Swing, the page's own
// ▶ and the patterns A–H with "Add A"; a ruler of beats ("1.1 1.2 …", a bar's first beat in the track colour); the step
// grid, one row per kit sound with its icon and the typing key that plays it; a step at the default velocity in the
// track colour, a softer one darker. The model is core/SimpleSteps.h (a view of the Drums track: nothing new saved).
// Motion: only while the song (or the page's ▶) plays and the page shows, on the main window's 30 Hz tick, and only the
// two columns whose state changed repaint.

class SimpleDock::StepsPage final : public juce::Component,
                                    public juce::SettableTooltipClient,
                                    public demo::CellSource
{
public:
    explicit StepsPage (AudioEngine& e) : engine (e)
    {
        setComponentID ("simple.dock.drumMachine"); // (round 190np: --demo finds it, and its cells, by this ID)
        setWantsKeyboardFocus (false);
        setRepaintsOnMouseActivity (false);
    }

    void show (int newTrack)
    {
        if (newTrack != track)
        {
            letter = 0;
            stopPreview();
        }
        track = newTrack;
        hideMessage();
        update();
    }

    void update()
    {
        const auto& song = engine.getSong();
        rows = simplesteps::kitRows (song, track);
        pattern = simplesteps::letterPattern (song, track, letter);
        if (pattern < 0 && letter != 0)
        {
            letter = 0;
            pattern = simplesteps::letterPattern (song, track, 0);
        }
        steps = pattern >= 0 ? simplesteps::stepCount (song, pattern) : 16;
        resized();
        repaint();
    }

    // Round 190rb (crash report 00d9f624, 0.1.31.1 Linux: a step's tooltip read song.channels[rows[...]]): the song can
    // change under the page (a project opened, a sound deleted, an undo) a moment before update() runs, and `rows` /
    // `pattern` then name channels and patterns the song no longer has. Each event and paint reads them again first in
    // that case (the layout follows on the next message); true when they were still good.
    bool revalidate()
    {
        const auto& song = engine.getSong();
        const auto good = pattern < (int) song.patterns.size()
                          && std::all_of (rows.begin(), rows.end(), [&song] (int ch) { return ch >= 0 && ch < (int) song.channels.size(); });
        if (good)
            return true;
        rows = simplesteps::kitRows (song, track);
        pattern = simplesteps::letterPattern (song, track, letter);
        if (pattern < 0 && letter != 0)
            pattern = simplesteps::letterPattern (song, track, letter = 0);
        steps = pattern >= 0 ? simplesteps::stepCount (song, pattern) : 16;
        paintRow = velocityRow = velocityStep = -1;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<StepsPage> (this)] {
            if (safe != nullptr)
                safe->update();
        });
        return false;
    }
    void hoverForCheck (juce::Point<int> p) { mouseMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p.toFloat(), {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, this, this, juce::Time::getCurrentTime(), p.toFloat(), juce::Time::getCurrentTime(), 0, false)); }
    juce::Rectangle<int> cellForCheck (int row, int step) const { return cellBounds (row, step); }

    // the main window's 30 Hz tick, only while the page shows (SimpleDock::tickInputMeter)
    void watchPlayback()
    {
        const auto& transport = engine.getTransport();
        const auto playing = transport.playing.load (std::memory_order_relaxed);
        if (previewing && ! playing && juce::Time::getMillisecondCounter() - previewStarted > 500) // (the transport may start a block later)
            stopPreview();
        const auto pos = transport.positionTicks.load (std::memory_order_relaxed);
        // BandLab (capture C b): while the song plays, the dock shows the pattern under the playhead and its ▶, letters and
        // Add are greyed out
        const auto song = playing && ! previewing;
        if (song != songPlaying)
        {
            songPlaying = song;
            repaint (topArea);
        }
        if (song && isShowing())
        {
            const auto& s = engine.getSong();
            for (const auto& c : s.playlist)
                if (c.kind == ClipKind::pattern && c.track == track && ! c.muted && pos >= (double) c.start && pos < (double) c.end())
                {
                    if (const auto l = simplesteps::letterOf (s, track, c.pattern); l >= 0 && l != letter)
                    {
                        letter = l;
                        update();
                    }
                    break;
                }
        }
        const auto step = isShowing() ? simplesteps::playingStep (engine.getSong(), track, pattern, pos, playing) : -1;
        setPlayingStep (step);
    }

    // tests and the shot route
    int rowCountForCheck() const noexcept { return (int) rows.size(); }
    int stepCountForCheck() const noexcept { return steps; }
    int letterForCheck() const noexcept { return letter; }
    int patternForCheck() const noexcept { return pattern; }
    int playingStepForCheck() const noexcept { return shownStep; }
    bool previewingForCheck() const noexcept { return previewing; }
    void clickStepForCheck (int row, int step) { toggle (row, step); }
    void pickLetterForCheck (int l) { pickLetter (l); }
    void addClipForCheck() { addClip(); }
    int messagesShownForCheck() const // visible bubbles on the page
    {
        int n = 0;
        for (auto* c : getChildren())
            n += dynamic_cast<juce::BubbleMessageComponent*> (c) != nullptr && c->isVisible() ? 1 : 0;
        return n;
    }
    void setLengthForCheck (int bars) { setLength (bars); }
    void setSwingForCheck (float v) { setSwing (v); }
    void previewForCheck() { togglePreview(); }
    void playingStepForSnapshot (int step) { setPlayingStep (step); }
    void velocityForCheck (int row, int step, float v) { setVelocity (row, step, v); }
    void velocityToolForSnapshot (bool on) { velocityTool = on; repaint(); }
    juce::PopupMenu lengthMenuForSnapshot() const { return lengthMenu(); }
    juce::Rectangle<int> gridBoundsForSnapshot() const { return gridArea; }
    // round 190np (--demo): a step's cell (row 0 = the bottom row, as drawn), for ui.find / ui.click with "cell"
    juce::Rectangle<int> demoCellBounds (int row, int column) const override
    {
        return row >= 0 && row < (int) rows.size() && column >= 0 && column < steps ? cellBounds (row, column) : juce::Rectangle<int>();
    }
    // a typing key (BandLab's row keys): plays the row and lights its header; −1 when the key is not one of them
    int channelForKey (int keyCode)
    {
        const auto row = simplesteps::rowForKey (keyCode);
        return row >= 0 && row < (int) rows.size() ? rows[(std::size_t) row] : -1;
    }

    void paint (juce::Graphics& g) override
    {
        revalidate();
        const auto& song = engine.getSong();
        const auto colourOfTrack = trackColour();
        // ---- the top row
        if (g.clipRegionIntersects (topArea))
        {
            g.setFont (font (type::body, true));
            g.setColour (colour::textPrimary);
            g.drawText (tr ("Length"), lengthLabel, juce::Justification::centredRight, false);
            // BandLab's Edit: the pointer (click steps on / off) | V (drag a step up / down: its velocity)
            g.drawText (tr ("Edit"), editLabel, juce::Justification::centredRight, false);
            fillRoundRect (g, editBox.toFloat(), editBox.getHeight() * 0.5f, colour::bg4);
            for (int k = 0; k < 2; ++k)
            {
                const auto seg = editBox.withWidth (editBox.getWidth() / 2).translated (k * editBox.getWidth() / 2, 0).reduced (2);
                if ((k == 1) == velocityTool)
                    fillRoundRect (g, seg.toFloat(), seg.getHeight() * 0.5f, colour::bg6);
                g.setColour ((k == 1) == velocityTool ? colour::textPrimary : colour::textSecondary);
                if (k == 0)
                {
                    juce::Path arrow; // (a pointer)
                    const auto c = seg.getCentre().toFloat();
                    arrow.startNewSubPath (c.x - 4.0f, c.y - 6.0f);
                    arrow.lineTo (c.x + 5.0f, c.y + 1.0f);
                    arrow.lineTo (c.x + 0.5f, c.y + 1.5f);
                    arrow.lineTo (c.x - 1.5f, c.y + 6.0f);
                    arrow.closeSubPath();
                    g.strokePath (arrow, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                else
                    g.drawText ("V", seg, juce::Justification::centred, false);
            }
            g.drawText (tr ("Swing"), swingLabel, juce::Justification::centredRight, false);
            g.drawText (tr ("Patterns"), patternsLabel, juce::Justification::centredRight, false);
            // Length: "2 bars ▾"
            fillRoundRect (g, lengthBox.toFloat(), radius::md, hover == Part::length ? colour::bg5 : colour::bg4);
            g.setColour (colour::textPrimary);
            const auto bars = pattern >= 0 ? simplesteps::lengthBars (song, pattern) : 1;
            g.drawText (barsText (bars), lengthBox.reduced (10, 0).withTrimmedRight (14), juce::Justification::centredLeft, false);
            {
                juce::Path down; // (BandLab's ▾)
                const auto cx = (float) lengthBox.getRight() - 14.0f, cy = (float) lengthBox.getCentreY();
                down.startNewSubPath (cx - 4.0f, cy - 2.0f);
                down.lineTo (cx, cy + 2.0f);
                down.lineTo (cx + 4.0f, cy - 2.0f);
                g.setColour (colour::textSecondary);
                g.strokePath (down, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            // Swing: a slim bar in the track colour with a round thumb, and "60%"
            const auto swing = juce::jlimit (0.0f, 1.0f, song.swing);
            const auto bar = swingBar.toFloat();
            fillRoundRect (g, bar.withSizeKeepingCentre (bar.getWidth(), 3.0f), 1.5f, colour::bg5);
            fillRoundRect (g, bar.withSizeKeepingCentre (bar.getWidth(), 3.0f).withWidth (bar.getWidth() * swing), 1.5f, colourOfTrack);
            g.setColour (colour::textPrimary);
            g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ bar.getX() + bar.getWidth() * swing, bar.getCentreY() }));
            g.setFont (font (type::body, true));
            g.drawText (juce::String (juce::roundToInt (swing * 100.0f)) + "%", swingValue, juce::Justification::centredLeft, false);
            // the page's own ▶ (■ while it plays the pattern)
            const auto play = playButton.toFloat();
            const auto ink = songPlaying ? colour::textDisabled : colour::textPrimary; // (greyed while the song plays)
            g.setColour (hover == Part::play && ! songPlaying ? colour::bg5 : colour::bg4);
            g.fillEllipse (play);
            if (previewing)
                g.setColour (ink), g.fillRect (juce::Rectangle<float> (12.0f, 12.0f).withCentre (play.getCentre())); // (BandLab's ■ 12 x 12)
            else
            {
                juce::Path tri; // (BandLab's ▶ 10 x 12)
                const auto c = play.getCentre();
                tri.addTriangle (c.x - 4.0f, c.y - 6.0f, c.x - 4.0f, c.y + 6.0f, c.x + 6.0f, c.y);
                g.setColour (ink);
                g.fillPath (tri);
            }
            // A … H: the shown one white with dark ink (BandLab), the others on the control colour
            const auto with = simplesteps::lettersWithPattern (song, track);
            for (int l = 0; l < simplesteps::letterCount; ++l)
            {
                const auto r = letterBox (l).toFloat();
                const auto shown = l == letter;
                const auto over = hover == Part::letter && hoverIndex == l && ! songPlaying;
                fillRoundRect (g, r, radius::md, shown ? (songPlaying ? colour::textDisabled : over ? colour::textSecondary.brighter (0.4f) : colour::textPrimary)
                                                       : over ? colour::bg5 : colour::bg4);
                g.setColour (shown ? colour::bg0 : songPlaying ? colour::textDisabled : (with & (1u << l)) != 0 ? colour::textPrimary : colour::textSecondary);
                g.setFont (font (type::small, true));
                g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + l)), r.toNearestInt(), juce::Justification::centred, false);
            }
            fillRoundRect (g, addButton.toFloat(), radius::md, hover == Part::add && ! songPlaying ? colour::bg5 : colour::bg4);
            g.setColour (songPlaying ? colour::textDisabled : colour::textPrimary);
            g.setFont (font (type::small, true));
            g.drawText (tr ("Add {0}", { juce::String::charToString ((juce::juce_wchar) ('A' + letter)) }), addButton, juce::Justification::centred, false);
        }
        // ---- the ruler: "1.1 1.2 …" at each beat, a bar's first beat in the track colour
        const auto stepsPerBeat = (int) (song.meter().beat() / ticksPerStep);
        const auto beatsPerBar = juce::jmax (1, (int) (song.barTicks() / juce::jmax<std::int64_t> (1, song.meter().beat())));
        if (g.clipRegionIntersects (rulerArea))
        {
            g.setFont (font (type::caption, true));
            for (int s = 0; s < steps; s += juce::jmax (1, stepsPerBeat))
            {
                const auto beat = s / juce::jmax (1, stepsPerBeat);
                const auto x = cellX (s);
                const auto first = beat % beatsPerBar == 0;
                g.setColour (first ? ui::simplelook::readableOn (colourOfTrack, colour::bg1, 4.5f) : colour::textSecondary);
                g.drawText (juce::String (beat / beatsPerBar + 1) + "." + juce::String (beat % beatsPerBar + 1),
                            juce::Rectangle<int> (x + 2, rulerArea.getY(), 60, rulerArea.getHeight()), juce::Justification::centredLeft, false);
            }
        }
        // ---- the rows: the icon and key, then the steps
        const auto& pat = pattern >= 0 ? song.patterns[(std::size_t) pattern] : emptyPattern;
        for (int row = 0; row < (int) rows.size(); ++row)
        {
            const auto y = rowY (row);
            const auto head = juce::Rectangle<int> (gridArea.getX(), y, headWidth, rowHeight);
            const auto ch = rows[(std::size_t) row];
            if (g.clipRegionIntersects (head))
            {
                const auto lit = row == litRow;
                const auto ink = lit ? colourOfTrack : colour::textSecondary;
                ui::drawTrackIcon (g, iconFor (song.channels[(std::size_t) ch]), juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ (float) head.getX() + 12.0f, (float) head.getCentreY() }), ink);
                // round 190np (coordinator review): the sound's name (elided when long; the tooltip has it whole), and
                // BandLab's typing key as a small badge at the right of the column (it was the key alone: "J E I F O A")
                auto label = head.withTrimmedLeft (26).withTrimmedRight (4);
                if (const auto key = simplesteps::rowKey (row); key != 0)
                {
                    const auto badge = label.removeFromRight (keyBadgeWidth).withSizeKeepingCentre (keyBadgeWidth, juce::jmin (16, rowHeight));
                    g.setColour (colour::bg4);
                    g.fillRoundedRectangle (badge.toFloat(), radius::sm);
                    g.setColour (lit ? colourOfTrack : colour::textDisabled);
                    g.setFont (font (type::caption, true));
                    g.drawText (juce::String::charToString ((juce::juce_wchar) key), badge, juce::Justification::centred, false);
                    label.removeFromRight (4);
                }
                g.setColour (lit ? colourOfTrack : colour::textSecondary);
                g.setFont (font (type::small, true));
                g.drawText (rowName (ch), label, juce::Justification::centredLeft, true);
            }
            for (int s = 0; s < steps; ++s)
            {
                const auto cell = cellBounds (row, s);
                if (! g.clipRegionIntersects (cell))
                    continue;
                const auto beat = s / juce::jmax (1, stepsPerBeat);
                const auto ground = beat % 2 == 0 ? colour::bg3 : colour::bg2; // (BandLab: every other beat a shade darker)
                const auto velocity = stepVelocity (pat, ch, s);
                const auto hovered = hover == Part::cell && hoverIndex == row * maxSteps + s;
                // BandLab (capture bl-drum-preview-playing.png): the steps the playhead is on turn white; an empty cell under
                // the pointer takes a tint of the track colour (bl-drum-hover-cell-off.png)
                if (velocity >= 0.0f && s == shownStep)
                {
                    g.setColour (juce::Colours::white);
                    g.fillRect (cell);
                    continue;
                }
                auto fill = velocity < 0.0f ? ground : ground.interpolatedWith (colourOfTrack, simplesteps::shade (velocity));
                if (velocity < 0.0f && hovered)
                    fill = ground.interpolatedWith (colourOfTrack, 0.18f);
                g.setColour (fill);
                g.fillRect (cell);
                if (velocity >= 0.0f)
                {
                    g.setColour (colourOfTrack.brighter (hovered ? 0.6f : 0.3f));
                    g.drawRect (cell, 1);
                }
            }
        }
        if (rows.empty() && g.clipRegionIntersects (gridArea))
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (type::small));
            g.drawFittedText (tr ("This drum track has no sounds. Add sounds to its group in the Channel Rack."), gridArea, juce::Justification::centred, 2);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 6);
        topArea = r.removeFromTop (30);
        auto top = topArea;
        // left: Length and Swing (BandLab: Edit, Length, Swing in the middle of the row; the kit picker LYNX has no
        // equivalent of sits left of them there)
        editLabel = top.removeFromLeft (34);
        top.removeFromLeft (8);
        editBox = top.removeFromLeft (76).withSizeKeepingCentre (76, 28);
        top.removeFromLeft (20);
        lengthLabel = top.removeFromLeft (52);
        top.removeFromLeft (8);
        lengthBox = top.removeFromLeft (92).withSizeKeepingCentre (92, 28);
        top.removeFromLeft (20);
        swingLabel = top.removeFromLeft (46);
        top.removeFromLeft (10);
        swingBar = top.removeFromLeft (110).withSizeKeepingCentre (110, 14);
        top.removeFromLeft (8);
        swingValue = top.removeFromLeft (44);
        // right: ▶, Patterns, A … H, Add A
        addButton = top.removeFromRight (58).withSizeKeepingCentre (58, 28);
        top.removeFromRight (6);
        lettersArea = top.removeFromRight (simplesteps::letterCount * 32 + 4).withSizeKeepingCentre (simplesteps::letterCount * 32 + 4, 28);
        top.removeFromRight (6);
        patternsLabel = top.removeFromRight (64);
        top.removeFromRight (8);
        playButton = top.removeFromRight (28).withSizeKeepingCentre (28, 28);
        r.removeFromTop (8);
        // round 190np: the row-name column fits the longest sound name (icon, name, key badge), up to a sixth of the width
        {
            const auto nameFont = font (type::small, true);
            int widest = 0;
            for (const auto ch : rows)
                widest = juce::jmax (widest, juce::roundToInt (juce::GlyphArrangement::getStringWidth (nameFont, rowName (ch))));
            headWidth = juce::jlimit (minHeadWidth, juce::jmax (minHeadWidth, r.getWidth() / 6), 26 + widest + 6 + keyBadgeWidth + 8);
        }
        rulerArea = r.removeFromTop (16).withTrimmedLeft (headWidth);
        r.removeFromTop (2);
        gridArea = r;
        const auto count = juce::jmax (1, (int) rows.size());
        rowHeight = juce::jlimit (10, 24, (gridArea.getHeight() + gap) / count - gap);
        rulerArea = rulerArea.withRight (gridArea.getRight());
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        revalidate();
        setHover (e.getPosition());
        // round 190np: the row-name column's tooltip: the whole name and its typing key
        const auto p = e.getPosition();
        if (const auto row = rowAt (p.y); row >= 0 && p.x >= gridArea.getX() && p.x < gridArea.getX() + headWidth)
        {
            const auto key = simplesteps::rowKey (row);
            setTooltip (rowName (rows[(std::size_t) row]) + (key != 0 ? " (" + tr ("key {0}", { juce::String::charToString ((juce::juce_wchar) key) }) + ")" : juce::String()));
        }
    }
    void mouseExit (const juce::MouseEvent&) override { setHover ({ -1, -1 }); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        revalidate();
        const auto p = e.getPosition();
        paintRow = -1;
        if (lengthBox.contains (p))
        {
            lengthMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (lengthBox)),
                                        [safe = juce::Component::SafePointer<StepsPage> (this)] (int id) {
                                            if (safe != nullptr && id > 0)
                                                safe->setLength (id);
                                        });
            return;
        }
        if (swingBar.expanded (6, 8).contains (p))
        {
            draggingSwing = true;
            engine.beginUndoGesture();
            setSwing (swingAt (p.x));
            return;
        }
        if (editBox.contains (p))
        {
            velocityTool = p.x >= editBox.getCentreX();
            repaint (topArea);
            return;
        }
        if (playButton.contains (p))
            return songPlaying ? void() : togglePreview();
        for (int l = 0; l < simplesteps::letterCount; ++l)
            if (letterBox (l).contains (p))
                return songPlaying ? void() : pickLetter (l);
        if (addButton.contains (p))
            return songPlaying ? void() : addClip();
        if (const auto row = rowAt (p.y); row >= 0 && p.x < gridArea.getX() + headWidth && p.x >= gridArea.getX())
            return audition (row);
        const auto [row, step] = cellAt (p);
        if (row < 0 || pattern < 0)
            return;
        // FL's and BandLab's grid: a click turns the step over; a drag paints that state along the row; right-click clears
        const auto& pat = engine.getSong().patterns[(std::size_t) pattern];
        if (velocityTool && ! e.mods.isPopupMenu()) // (BandLab's V: a vertical drag on a step sets its velocity)
        {
            if (stepVelocity (pat, rows[(std::size_t) row], step) >= 0.0f)
            {
                velocityRow = row, velocityStep = step;
                velocityStart = stepVelocity (pat, rows[(std::size_t) row], step);
                engine.beginUndoGesture();
            }
            return;
        }
        paintOn = ! e.mods.isPopupMenu() && stepVelocity (pat, rows[(std::size_t) row], step) < 0.0f;
        paintRow = row;
        lastPainted = -1;
        engine.beginUndoGesture();
        paintStep (step);
        if (paintOn)
            audition (row);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        revalidate();
        if (draggingSwing)
            return setSwing (swingAt (e.x));
        if (velocityRow >= 0)
            return setVelocity (velocityRow, velocityStep, juce::jlimit (0.05f, 1.0f, velocityStart - (float) e.getDistanceFromDragStartY() / 100.0f));
        if (paintRow < 0)
            return;
        const auto [row, step] = cellAt ({ e.x, rowY (paintRow) + rowHeight / 2 });
        juce::ignoreUnused (row);
        if (step >= 0)
            paintStep (step);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingSwing || paintRow >= 0 || velocityRow >= 0)
            engine.endUndoGesture();
        velocityRow = velocityStep = -1;
        draggingSwing = false;
        paintRow = -1;
    }

    // a row's sound was played (its key, a click on its header): the header lights for a moment
    void lightRow (int row)
    {
        litRow = row;
        litAt = juce::Time::getMillisecondCounter();
        repaint (gridArea.withWidth (headWidth));
    }

private:
    enum class Part { none, length, swing, play, letter, add, cell };
    static constexpr int minHeadWidth = 96, keyBadgeWidth = 16, gap = 2, maxSteps = 1024;
    int headWidth = minHeadWidth; // (round 190np: follows the names, resized())
    juce::String rowName (int channel) const
    {
        const auto& song = engine.getSong();
        return channel >= 0 && channel < (int) song.channels.size() ? juce::String::fromUTF8 (song.channels[(std::size_t) channel].name.c_str()) : juce::String();
    }

    AudioEngine& engine;
    int track = -1, letter = 0, pattern = -1, steps = 16, rowHeight = 20;
    std::vector<int> rows;
    juce::Rectangle<int> topArea, editLabel, editBox, lengthLabel, lengthBox, swingLabel, swingBar, swingValue, playButton, patternsLabel, lettersArea, addButton,
        rulerArea, gridArea;
    Part hover = Part::none;
    int hoverIndex = -1;
    int shownStep = -1;
    bool previewing = false, draggingSwing = false, paintOn = true, songPlaying = false, velocityTool = false;
    int velocityRow = -1, velocityStep = -1;
    float velocityStart = 0.78f;
public:
    juce::String lastMessage; // (the last bubble's text, for the checks)
private:
    std::unique_ptr<juce::BubbleMessageComponent> bubble; // "Make a beat first..." (round 190nr: one, owned)
    int paintRow = -1, lastPainted = -1, litRow = -1;
    juce::uint32 litAt = 0, previewStarted = 0;
    const Pattern emptyPattern {};

    juce::Colour trackColour() const
    {
        const auto& song = engine.getSong();
        const auto own = track >= 0 && track < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) track].colour : 0u;
        return juce::Colour (own != 0 ? own : simpletracks::defaultColour (simpletracks::Kind::drums)).withAlpha (1.0f);
    }

    static juce::String barsText (int bars) { return bars == 1 ? tr ("1 bar") : tr ("{0} bars", { juce::String (bars) }); }

    static trackicons::Icon iconFor (const ChannelDef& c)
    {
        using I = trackicons::Icon;
        if (c.icon > 0)
            if (const auto own = trackicons::iconFor (c.icon); own != I::none && own != I::tag)
                return own;
        const auto name = juce::String::fromUTF8 (c.name.c_str()).toLowerCase();
        switch (c.instrument)
        {
            case InstrumentKind::kick: return I::kick;
            case InstrumentKind::snare: return I::snare;
            case InstrumentKind::hat: return I::hiHat;
            case InstrumentKind::clap: return I::clap;
            default: break;
        }
        return name.contains ("kick") || name.contains ("808") ? I::kick
               : name.contains ("snare") || name.contains ("rim") ? I::snare
               : name.contains ("hat") ? I::hiHat
               : name.contains ("clap") || name.contains ("snap") ? I::clap
               : name.contains ("crash") || name.contains ("ride") || name.contains ("cymbal") ? I::cymbal
               : name.contains ("shaker") ? I::shaker
               : name.contains ("tamb") ? I::tambourine
               : name.contains ("cowbell") ? I::cowbell
                                           : I::percussion;
    }

    // the step's velocity (the loudest note starting there), −1 for an empty step
    static float stepVelocity (const Pattern& p, int channel, int step)
    {
        const auto tick = (std::int64_t) step * ticksPerStep;
        auto v = -1.0f;
        for (const auto& n : p.notes)
        {
            if (n.start > tick)
                break;
            if (n.start == tick && n.channel == channel && ! n.muted)
                v = juce::jmax (v, n.velocity);
        }
        return v;
    }

    int rowY (int row) const { return gridArea.getY() + ((int) rows.size() - 1 - row) * (rowHeight + gap); } // (row 0 at the bottom)
    int rowAt (int y) const
    {
        for (int row = 0; row < (int) rows.size(); ++row)
            if (y >= rowY (row) && y < rowY (row) + rowHeight)
                return row;
        return -1;
    }
    int cellX (int step) const
    {
        const auto left = gridArea.getX() + headWidth;
        return left + (int) ((std::int64_t) step * (gridArea.getRight() - left + gap) / juce::jmax (1, steps));
    }
    juce::Rectangle<int> cellBounds (int row, int step) const
    {
        const auto x = cellX (step);
        return { x, rowY (row), juce::jmax (1, cellX (step + 1) - x - gap), rowHeight };
    }
    std::pair<int, int> cellAt (juce::Point<int> p) const
    {
        const auto row = rowAt (p.y);
        if (row < 0 || p.x < gridArea.getX() + headWidth || p.x >= gridArea.getRight())
            return { -1, -1 };
        for (int s = 0; s < steps; ++s)
            if (p.x >= cellX (s) && p.x < cellX (s + 1))
                return { row, s };
        return { -1, -1 };
    }
    juce::Rectangle<int> letterBox (int l) const { return { lettersArea.getX() + l * 32 + 2, lettersArea.getY(), 28, lettersArea.getHeight() }; }
    float swingAt (int x) const { return juce::jlimit (0.0f, 1.0f, (float) (x - swingBar.getX()) / (float) juce::jmax (1, swingBar.getWidth())); }

    juce::PopupMenu lengthMenu() const
    {
        juce::PopupMenu m;
        const auto bars = pattern >= 0 ? simplesteps::lengthBars (engine.getSong(), pattern) : 1;
        for (auto b : simplesteps::lengthChoices)
            m.addItem (b, barsText (b), pattern >= 0, b == bars);
        return m;
    }

    void setHover (juce::Point<int> p)
    {
        auto part = Part::none;
        int index = -1;
        if (lengthBox.contains (p))
            part = Part::length;
        else if (playButton.contains (p))
            part = Part::play;
        else if (addButton.contains (p))
            part = Part::add;
        else
        {
            for (int l = 0; l < simplesteps::letterCount && part == Part::none; ++l)
                if (letterBox (l).contains (p))
                    part = Part::letter, index = l;
            if (part == Part::none)
                if (const auto [row, step] = cellAt (p); row >= 0)
                    part = Part::cell, index = row * maxSteps + step;
        }
        if (part == hover && index == hoverIndex)
            return;
        const auto area = [this] (Part pt, int i) -> juce::Rectangle<int> {
            switch (pt)
            {
                case Part::length: return lengthBox;
                case Part::play: return playButton;
                case Part::add: return addButton;
                case Part::letter: return letterBox (i);
                case Part::cell: return cellBounds (i / maxSteps, i % maxSteps).expanded (1);
                default: return {};
            }
        };
        repaint (area (hover, hoverIndex));
        hover = part;
        hoverIndex = index;
        repaint (area (hover, hoverIndex));
        if (part == Part::play)
            setTooltip (previewing ? tr ("Stop the pattern") : tr ("Play this pattern on its own"));
        else if (part == Part::add)
            setTooltip (tr ("Put this pattern on the track at the song position"));
        else if (part == Part::cell && index / maxSteps < (int) rows.size())
            setTooltip (rowName (rows[(std::size_t) (index / maxSteps)]));
        else
            setTooltip ({});
    }

    void setPlayingStep (int step)
    {
        if (litRow >= 0 && juce::Time::getMillisecondCounter() - litAt > 150)
        {
            litRow = -1;
            repaint (gridArea.withWidth (headWidth));
        }
        if (step == shownStep)
            return;
        const auto column = [this] (int s) { return s < 0 ? juce::Rectangle<int>() : juce::Rectangle<int> (cellX (s), gridArea.getY(), cellX (s + 1) - cellX (s), gridArea.getHeight()); };
        repaint (column (shownStep));
        shownStep = step;
        repaint (column (shownStep));
    }

    void hideMessage()
    {
        if (bubble != nullptr)
            bubble->setVisible (false);
    }

    void toggle (int row, int step)
    {
        if (row < 0 || row >= (int) rows.size() || pattern < 0 || step < 0 || step >= steps)
            return;
        hideMessage();
        engine.editSong ([p = pattern, ch = rows[(std::size_t) row], step] (Song& s) {
            if (p < (int) s.patterns.size())
                s.patterns[(std::size_t) p].toggleStep (ch, step);
        });
        repaint (cellBounds (row, step).expanded (1));
    }

    void paintStep (int step)
    {
        if (step == lastPainted || paintRow < 0 || pattern < 0)
            return;
        hideMessage();
        lastPainted = step;
        const auto ch = rows[(std::size_t) paintRow];
        const auto& pat = engine.getSong().patterns[(std::size_t) pattern];
        if ((stepVelocity (pat, ch, step) >= 0.0f) != paintOn)
            toggle (paintRow, step);
    }

    void setVelocity (int row, int step, float v)
    {
        if (pattern < 0 || row < 0 || row >= (int) rows.size())
            return;
        engine.editSong ([p = pattern, ch = rows[(std::size_t) row], tick = (std::int64_t) step * ticksPerStep, v] (Song& s) {
            if (p < (int) s.patterns.size())
                for (auto& n : s.patterns[(std::size_t) p].notes)
                    if (n.channel == ch && n.start == tick)
                        n.velocity = v;
        });
        repaint (cellBounds (row, step).expanded (1));
    }

    void audition (int row)
    {
        if (row < 0 || row >= (int) rows.size())
            return;
        const auto ch = rows[(std::size_t) row];
        engine.sendLiveNote (ch, 60, 0.78f, true, false);
        engine.sendLiveNote (ch, 60, 0.0f, false, false);
        lightRow (row);
    }

    void pickLetter (int l)
    {
        if (l < 0 || l >= simplesteps::letterCount)
            return;
        if (simplesteps::letterPattern (engine.getSong(), track, l) < 0)
            engine.editSong ([t = track, l] (Song& s) { simplesteps::ensureLetter (s, t, l); });
        letter = l;
        hideMessage();
        const auto wasPreviewing = previewing;
        update();
        if (wasPreviewing && pattern >= 0)
            engine.editSong ([p = pattern] (Song& s) { s.currentPattern = p; }, false); // (the preview goes on with the new letter)
    }

    void addClip()
    {
        // BandLab (capture C e): an empty pattern is not put on the track: "Make a beat first before exporting it to a track."
        if (pattern < 0 || engine.getSong().patterns[(std::size_t) pattern].notes.empty())
        {
            // round 190nr: one bubble, kept (a new click shows it again instead of stacking another), and gone as soon as
            // the pattern gets a step (the 0.1.30 shot route showed it over the grid after the beat was made)
            if (bubble == nullptr)
            {
                bubble = std::make_unique<juce::BubbleMessageComponent> (300);
                addChildComponent (*bubble);
            }
            bubble->showAt (addButton, juce::AttributedString (tr ("Make a beat first before exporting it to a track.")), 2500, true, false);
            lastMessage = tr ("Make a beat first before exporting it to a track.");
            return;
        }
        const auto at = (std::int64_t) juce::jmax (0.0, engine.getTransport().positionTicks.load (std::memory_order_relaxed));
        engine.editSong ([t = track, l = letter, at] (Song& s) {
            simplesteps::ensureLetter (s, t, l);
            simplesteps::addLetterClip (s, t, l, at);
        });
    }

    void setLength (int bars)
    {
        if (pattern < 0)
            return;
        engine.editSong ([p = pattern, bars] (Song& s) { simplesteps::setLengthBars (s, p, bars); });
        update();
    }

    void setSwing (float v)
    {
        if (std::abs (engine.getSong().swing - v) < 1.0e-4f)
            return;
        engine.editSong ([v] (Song& s) { s.swing = v; });
        repaint (topArea);
    }

    void togglePreview()
    {
        if (previewing)
            return stopPreview (true);
        if (pattern < 0)
            return;
        if (engine.getTransport().playing.load (std::memory_order_relaxed))
            engine.stop();
        engine.editSong ([p = pattern] (Song& s) {
            s.mode = PlayMode::pattern;
            s.currentPattern = p;
        }, false);
        previewing = true;
        previewStarted = juce::Time::getMillisecondCounter();
        engine.play();
        repaint (playButton);
    }

    void stopPreview (bool stopTransport = false)
    {
        if (! previewing)
            return;
        previewing = false;
        if (stopTransport)
            engine.stop();
        engine.editSong ([] (Song& s) { s.mode = PlayMode::song; }, false); // (the Simple workflow plays the song)
        repaint (playButton);
    }
};

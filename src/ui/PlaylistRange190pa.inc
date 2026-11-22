// Round 190pa (research190oe P14, LYNX extra): a range on chosen tracks (REAPER's razor edit, Live's time selection over
// some tracks). Included by PlaylistComponent.cpp. core/RangeEdit.h does the work on the song; Song::trackRange holds the
// range, so it is saved with the project and undo brings it back with the edit.
//
// Ctrl+Alt+drag in the grid draws it (FL 2026's Playlist has no Ctrl+Alt+drag of its own: Ctrl+drag selects, Alt turns
// snap off), over the rows it crosses: the tracks' clips and automation lanes, or one lane when the drag stays on it. Its
// edges sit on the nearest grid lines. A drag inside it moves what it holds (the clips are cut at its edges), a right-
// click opens its menu, Delete / Ctrl+X / C / V / B (or D) / E act on it, Esc or any other press clears it.

bool PlaylistComponent::rangeEditsOn() const
{
    if (simpleHeaders)
        return false;
    const auto style = layoutstyle::current();
    return style == layoutstyle::Style::clipLaunch || (style == layoutstyle::Style::pattern && rangeedit::patternLayoutOn());
}

rangeedit::RangeClip& PlaylistComponent::rangeClipboard()
{
    static rangeedit::RangeClip clip;
    return clip;
}

bool& PlaylistComponent::rangeClipboardNewest()
{
    static bool newest = false;
    return newest;
}

int& PlaylistComponent::rangeClipboardTop()
{
    static int top = 0;
    return top;
}

TrackRange PlaylistComponent::rangeAt (juce::Point<int> a, juce::Point<int> b) const
{
    const auto laneA = laneAt (a), laneB = laneAt (b);
    const auto trackA = laneA.track >= 0 ? laneA.track : yToTrack (a.y);
    const auto trackB = laneB.track >= 0 ? laneB.track : yToTrack (b.y);
    const auto oneLane = laneA.track >= 0 && laneB.track == laneA.track && laneB.lane == laneA.lane ? laneAutomation (laneA.track, laneA.lane) : -1;
    const auto grid = gridArea();
    auto tickOf = [&] (int x) { return juce::jmax<std::int64_t> (0, snapLine (xToTick (juce::jlimit (grid.getX(), grid.getRight(), x)))); };
    return rangeedit::fromCorners (tickOf (a.x), tickOf (b.x), trackA, trackB, oneLane);
}

std::vector<juce::Rectangle<int>> PlaylistComponent::rangeRects (const TrackRange& r) const
{
    std::vector<juce::Rectangle<int>> out;
    if (r.empty())
        return out;
    const auto& song = engine.getSong();
    const auto grid = gridArea();
    const auto x0 = (int) std::floor (tickToX ((double) r.from)), x1 = (int) std::ceil (tickToX ((double) r.to));
    if (x1 <= grid.getX() || x0 >= grid.getRight())
        return out;
    for (const auto t : r.tracks)
    {
        if (t < 0 || t >= numTracks())
            continue;
        juce::Rectangle<int> row;
        if (r.laneAutomation >= 0)
        {
            if (t >= (int) song.playlistTracks.size())
                continue;
            const auto& lanes = song.playlistTracks[(std::size_t) t].lanes;
            for (int i = 0; i < (int) lanes.size(); ++i)
                if (lanes[(std::size_t) i].automation == r.laneAutomation)
                    row = laneRow (t, i);
            if (row.isEmpty()) // (lanes folded or off: the track's row)
                row = { 0, trackToY (t), getWidth(), heightOf (t) };
        }
        else
            row = { 0, trackToY (t), getWidth(), heightOf (t) + laneBlockHeight (t) };
        row = juce::Rectangle<int>::leftTopRightBottom (x0, row.getY(), x1, row.getBottom()).getIntersection (grid);
        if (! row.isEmpty())
            out.push_back (row);
    }
    return out;
}

bool PlaylistComponent::rangeHit (juce::Point<int> p) const
{
    for (const auto& r : rangeRects (engine.getSong().trackRange))
        if (r.contains (p))
            return true;
    return false;
}

void PlaylistComponent::paintTrackRange (juce::Graphics& g)
{
    // a translucent band per track in the accent (REAPER's razor areas), outlined; a move shows where it lands. Live's
    // look (the Clip-launch Arrangement): Live's time selection colour, #b0ddeb (live190cw shot 03a), over the rows,
    // no outline
    const juce::Graphics::ScopedSaveState keep (g);
    g.reduceClipRegion (gridArea());
    const auto live = liveArrangeLook();
    const auto colour = live ? juce::Colour (0xffb0ddeb) : theme::colour::accent;
    auto draw = [&g, live, colour] (const std::vector<juce::Rectangle<int>>& rects, float fill, float edge) {
        for (const auto& r : rects)
        {
            if (fill > 0.0f)
            {
                g.setColour (colour.withAlpha (live ? juce::jmin (1.0f, fill * 2.6f) : fill));
                g.fillRect (r);
            }
            if (! live || fill <= 0.0f)
            {
                g.setColour (colour.withAlpha (edge));
                g.drawRect (r, 1);
            }
        }
    };
    if (drag == Drag::range)
        draw (rangeRects (rangeDraft), 0.22f, 0.9f);
    else if (drag == Drag::rangeMove)
    {
        draw (rangeRects (engine.getSong().trackRange), 0.0f, 0.45f);
        draw (rangeRects (rangeDraft), 0.22f, 0.9f);
    }
    else
        draw (rangeRects (engine.getSong().trackRange), 0.22f, 0.9f);
}

void PlaylistComponent::setTrackRange (const TrackRange& r)
{
    if (engine.getSong().trackRange == r)
        return;
    engine.editSong ([r] (Song& s) { s.trackRange = r; }, false); // (a selection: no undo step of its own)
    repaint();
}

void PlaylistComponent::setTrackRangeForSnapshot (const TrackRange& r)
{
    setTrackRange (r);
}

void PlaylistComponent::drawRangeForSnapshot (juce::Point<int> a, juce::Point<int> b)
{
    setTrackRange (rangeAt (a, b));
}

bool PlaylistComponent::rangeMouseDown (const juce::MouseEvent& e)
{
    if (! rangeEditsOn() || e.x < headerWidth || ! gridArea().contains (e.getPosition()))
        return false;
    const auto left = e.mods.isLeftButtonDown() && ! e.mods.isRightButtonDown() && ! e.mods.isMiddleButtonDown();
    if (left && flmods::ctrl (e.mods) && e.mods.isAltDown() && ! e.mods.isShiftDown())
    {
        drag = Drag::range;
        rangeDownPos = e.getPosition();
        rangeDraft = {};
        repaint();
        return true;
    }
    const auto& song = engine.getSong();
    if (song.trackRange.empty() || e.mods.isMiddleButtonDown())
        return false;
    if (rangeHit (e.getPosition()))
    {
        if (flmods::popup (e.mods))
        {
            makeRangeMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            return true;
        }
        if (left && ! e.mods.isAnyModifierKeyDown())
        {
            drag = Drag::rangeMove;
            rangeDownPos = e.getPosition();
            rangeDraft = song.trackRange;
            return true;
        }
    }
    setTrackRange ({}); // a press elsewhere clears it (and goes on as usual)
    return false;
}

void PlaylistComponent::rangeMouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::range)
    {
        rangeDraft = rangeAt (rangeDownPos, e.getPosition());
        repaint();
        return;
    }
    const auto& r = engine.getSong().trackRange;
    const auto dt = juce::jmax (-r.from, snapLine (xToTick (e.x)) - snapLine (xToTick (rangeDownPos.x)));
    const auto dtr = juce::jmax (-r.top(), yToTrack (e.y) - yToTrack (rangeDownPos.y));
    rangeDraft = r;
    rangeDraft.from += dt;
    rangeDraft.to += dt;
    for (auto& t : rangeDraft.tracks)
        t += dtr;
    if (dtr != 0)
        rangeDraft.laneAutomation = -1;
    repaint();
}

void PlaylistComponent::rangeMouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::range)
    {
        const auto r = rangeAt (rangeDownPos, e.getPosition());
        setTrackRange (r);
        if (! r.empty())
            ui::postHint ({ r.laneAutomation >= 0 ? tr ("Range on one automation lane: Delete, Ctrl+X / C / V, Ctrl+B duplicates; right-click for more; Esc clears it")
                                                  : tr ("Range on {0} track(s): Delete, Ctrl+X / C / V, Ctrl+B duplicates, Ctrl+E splits; right-click for more; Esc clears it",
                                                        { juce::String ((int) r.tracks.size()) }),
                            {}, false, false },
                          5000);
        rangeDraft = {};
        repaint();
        return;
    }
    const auto r = engine.getSong().trackRange;
    const auto dt = juce::jmax (-r.from, snapLine (xToTick (e.x)) - snapLine (xToTick (rangeDownPos.x)));
    const auto dtr = juce::jmax (-r.top(), yToTrack (e.y) - yToTrack (rangeDownPos.y));
    rangeDraft = {};
    if (dt != 0 || dtr != 0)
        engine.editSong ([dt, dtr] (Song& s) { s.trackRange = rangeedit::moveRange (s, s.trackRange, dt, dtr); });
    repaint();
}

bool PlaylistComponent::rangeKeyPressed (const juce::KeyPress& key)
{
    if (! rangeEditsOn())
        return false;
    const auto mods = key.getModifiers();
    const auto cmd = mods.isCommandDown() && ! mods.isShiftDown() && ! mods.isAltDown();
    const auto code = key.getKeyCode();
    const auto letter = keys::letterOfKeyCode (code);
    if (engine.getSong().trackRange.empty())
    {
        // a range copied last: Ctrl+V lays it at the song position (Live's insert marker in the Clip-launch layout)
        if (cmd && letter == 'V' && rangeClipboardNewest() && ! rangeClipboard().empty())
        {
            rangeCommand (RangeCommand::paste);
            return true;
        }
        return false;
    }
    auto run = [this] (RangeCommand c) {
        rangeCommand (c);
        return true;
    };
    if (code == juce::KeyPress::deleteKey && ! mods.isAnyModifierKeyDown())
        return run (RangeCommand::clear);
    if (code == juce::KeyPress::escapeKey && ! mods.isAnyModifierKeyDown())
        return run (RangeCommand::deselect);
    if (! cmd)
        return false;
    switch (letter)
    {
        case 'C': return run (RangeCommand::copy);
        case 'X': return run (RangeCommand::cut);
        case 'V': return run (RangeCommand::paste);
        case 'B':
        case 'D': return run (RangeCommand::duplicate);
        case 'E': return run (RangeCommand::split);
        default: break;
    }
    return false;
}

void PlaylistComponent::rangeCommand (RangeCommand which)
{
    const auto r = engine.getSong().trackRange;
    switch (which)
    {
        case RangeCommand::copy:
        case RangeCommand::cut:
            if (r.empty())
                return;
            rangeClipboard() = rangeedit::copyRange (engine.getSong(), r);
            rangeClipboardNewest() = true;
            rangeClipboardTop() = r.top();
            if (which == RangeCommand::cut)
                engine.editSong ([] (Song& s) { rangeedit::clearRange (s, s.trackRange); });
            break;
        case RangeCommand::clear:
            if (! r.empty())
                engine.editSong ([] (Song& s) { rangeedit::clearRange (s, s.trackRange); });
            break;
        case RangeCommand::paste:
        {
            const auto clip = rangeClipboard();
            if (clip.empty())
                return;
            std::int64_t at = 0;
            int top = 0;
            if (! r.empty())
            {
                at = r.from;
                top = r.top();
            }
            else if (layoutstyle::current() == layoutstyle::Style::clipLaunch && insertMarkTrack >= 0 && insertMarkTick >= 0)
            {
                at = insertMarkTick;
                top = insertMarkTrack;
            }
            else
            {
                at = juce::jmax<std::int64_t> (0, snapLine ((double) songPositionTick()));
                top = rangeClipboardTop();
            }
            int skipped = 0;
            engine.editSong ([&] (Song& s) { s.trackRange = rangeedit::pasteRange (s, at, top, clip, &skipped); });
            if (skipped > 0)
                ui::postHint ({ tr ("{0} automation lane(s) stay out: a lane belongs to its own track", { juce::String (skipped) }), {}, false, false }, 4000);
            break;
        }
        case RangeCommand::duplicate:
            if (! r.empty())
                engine.editSong ([] (Song& s) { s.trackRange = rangeedit::duplicateRange (s, s.trackRange); });
            break;
        case RangeCommand::split:
            if (! r.empty())
                engine.editSong ([] (Song& s) { rangeedit::splitAtRange (s, s.trackRange); });
            break;
        case RangeCommand::render:
            if (! r.empty())
                consolidatePlaylistRange (engine, rangeedit::renderClips (engine.getSong(), r), r.top(), r.from, r.to);
            break;
        case RangeCommand::deselect:
            setTrackRange ({});
            break;
    }
    repaint();
}

juce::PopupMenu PlaylistComponent::makeRangeMenu()
{
    const auto& r = engine.getSong().trackRange;
    const auto has = ! r.empty();
    const auto lane = r.laneAutomation >= 0;
    juce::PopupMenu menu;
    menu.addSectionHeader (lane ? tr ("Range on one automation lane") : tr ("Range on {0} track(s)", { juce::String ((int) r.tracks.size()) }));
    auto add = [&] (const juce::String& text, int keyChar, bool enabled, RangeCommand c) {
        juce::PopupMenu::Item item (text);
        if (keyChar != 0)
            item.shortcutKeyDescription = keyChar == juce::KeyPress::deleteKey ? juce::KeyPress (juce::KeyPress::deleteKey).getTextDescription()
                                                                               : juce::KeyPress (keyChar, juce::ModifierKeys (juce::ModifierKeys::commandModifier), 0).getTextDescription();
        item.isEnabled = enabled;
        item.action = [this, c] { rangeCommand (c); };
        menu.addItem (std::move (item));
    };
    add (tr ("Cut range"), 'X', has, RangeCommand::cut);
    add (tr ("Copy range"), 'C', has, RangeCommand::copy);
    add (tr ("Paste range here"), 'V', ! rangeClipboard().empty(), RangeCommand::paste);
    add (tr ("Delete what the range holds"), juce::KeyPress::deleteKey, has, RangeCommand::clear);
    add (tr ("Duplicate range"), 'B', has, RangeCommand::duplicate);
    add (tr ("Split clips at the range's edges"), 'E', has && ! lane, RangeCommand::split);
    menu.addSeparator();
    add (tr ("Render range to an audio clip..."), 0, has && ! lane, RangeCommand::render);
    menu.addSeparator();
    add (tr ("Clear the range selection"), 0, has, RangeCommand::deselect);
    return menu;
}

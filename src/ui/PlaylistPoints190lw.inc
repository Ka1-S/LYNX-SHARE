// Issue #207 (round 190lw, LYNX extras): several points of one curve edited together, and the freehand pen. Included by
// PlaylistComponent.cpp. The curve is an automation clip's points, an audio clip's envelope lane (round 190gi) or an
// automation lane (round 177b); core/AutoPointEdit.h does the point work.

PlaylistComponent::CurveSpot PlaylistComponent::curveSpotAt (juce::Point<int> p) const
{
    if (const auto lane = laneAt (p); lane.track >= 0)
    {
        if (p.x >= headerWidth && laneAutomation (lane.track, lane.lane) >= 0 && laneBody (lane.track, lane.lane).expanded (0.0f, 4.0f).contains (p.toFloat()))
            return { -1, lane.track, lane.lane };
        return {};
    }
    if (! gridArea().contains (p))
        return {};
    bool right = false, left = false;
    const auto clip = clipAt (p.x, p.y, right, left);
    if (clip < 0 || ! automationEditable (clip))
        return {};
    const auto& c = engine.getSong().playlist[(std::size_t) clip];
    if (c.kind != ClipKind::automation && c.kind != ClipKind::audio)
        return {};
    // an audio clip's curve only while its envelope lane shows (curveOf gives none otherwise)
    return automationBody (c).expanded (0.0f, 4.0f).contains (p.toFloat()) ? CurveSpot { clip, -1, -1 } : CurveSpot {};
}

const std::vector<AutomationPoint>* PlaylistComponent::spotPoints (const CurveSpot& s) const
{
    const auto& song = engine.getSong();
    if (s.clip >= 0)
        return s.clip < (int) song.playlist.size() ? curveOf (s.clip).points : nullptr;
    const auto a = laneAutomation (s.track, s.lane);
    return a >= 0 ? &song.automations[(std::size_t) a].points : nullptr;
}

juce::Rectangle<float> PlaylistComponent::spotBody (const CurveSpot& s) const
{
    if (s.clip >= 0)
        return s.clip < (int) engine.getSong().playlist.size() ? automationBody (engine.getSong().playlist[(std::size_t) s.clip]) : juce::Rectangle<float> {};
    return laneBody (s.track, s.lane);
}

void PlaylistComponent::editSpot (const CurveSpot& s, const std::function<void (std::vector<AutomationPoint>&)>& fn)
{
    if (s.clip >= 0)
    {
        editCurve (s.clip, fn);
        return;
    }
    if (const auto a = laneAutomation (s.track, s.lane); a >= 0)
        engine.editSong ([a, &fn] (Song& song) { fn (song.automations[(std::size_t) a].points); });
}

std::int64_t PlaylistComponent::spotTickAt (const CurveSpot& s, float x) const
{
    const auto songTick = juce::jmax<std::int64_t> (0, (std::int64_t) std::floor (xToTick ((int) std::round (x))));
    if (s.clip < 0)
        return songTick;
    const auto& c = engine.getSong().playlist[(std::size_t) s.clip];
    return juce::jlimit (c.offset, c.offset + c.length, songTick - c.start + c.offset); // (inside the clip)
}

std::int64_t PlaylistComponent::spotSongTick (const CurveSpot& s, std::int64_t tick) const
{
    if (s.clip < 0)
        return tick;
    const auto& c = engine.getSong().playlist[(std::size_t) s.clip];
    return c.start + tick - c.offset;
}

float PlaylistComponent::spotValueAt (const CurveSpot& s, float y) const
{
    const auto body = spotBody (s);
    const auto shown = juce::jlimit (0.0f, 1.0f, (body.getBottom() - y) / juce::jmax (1.0f, body.getHeight()));
    return s.clip >= 0 ? shown : laneValueOf (laneAutomation (s.track, s.lane), shown);
}

int PlaylistComponent::spotPointAt (const CurveSpot& s, juce::Point<int> p) const
{
    return s.clip >= 0 ? automationPointAt (s.clip, p) : lanePointAt (s.track, s.lane, p);
}

bool PlaylistComponent::isPointSelected (const CurveSpot& s, int point) const
{
    return s == pointSpot && std::binary_search (selectedPoints.begin(), selectedPoints.end(), point);
}

void PlaylistComponent::clearPointSelection()
{
    if (selectedPoints.empty() && pointBand.isEmpty())
        return;
    selectedPoints.clear();
    pointBand = {};
    pointSpot = {};
    repaint();
}

// The pen's modifier: on an automation clip (or an audio clip's envelope) Alt, which FL 2026 leaves free there (its
// Ctrl+drag starts the clips' rubber band, also on an automation clip; Alt+drag and Alt+click on the body do nothing,
// capture 190lw c5 / c9); in an automation lane Ctrl (as in REAPER; the lanes' Alt bends a segment, as Live's).
bool PlaylistComponent::penModifier (bool lane, juce::ModifierKeys mods)
{
    return ! mods.isShiftDown() && (lane ? flmods::ctrl (mods) && ! mods.isAltDown() : mods.isAltDown() && ! flmods::ctrl (mods));
}

bool PlaylistComponent::pointMouseDown (const juce::MouseEvent& e, int)
{
    const auto p = e.getPosition();
    const auto spot = e.mods.isLeftButtonDown() ? curveSpotAt (p) : CurveSpot {};
    const auto tool = tools.getTool();
    const auto drawingTool = tool == EditTool::pencil || tool == EditTool::brush || tool == EditTool::lasso;
    const auto lane = spot.track >= 0;
    const auto onPoint = spot.valid() && spotPointAt (spot, p) >= 0;

    // the pen (Alt+click on a point keeps deleting it; Draw Mode draws in the lanes already)
    if (spot.valid() && drawingTool && penModifier (lane, e.mods) && ! onPoint && ! (lane && drawmode::isOn())
        && (lane || automationHandleAt (spot.clip, p) < 0))
    {
        clearPointSelection();
        pointSpot = spot;
        pointPress = p;
        penStarted = false;
        drag = Drag::pen;
        return true;
    }

    // a press on a selected point moves the selection
    if (spot.valid() && spot == pointSpot && selectedPoints.size() > 0 && ! flmods::ctrl (e.mods) && ! e.mods.isAltDown())
        if (const auto point = spotPointAt (spot, p); point >= 0 && isPointSelected (spot, point))
        {
            if (const auto* points = spotPoints (spot))
            {
                pointsBefore = *points;
                pointPress = p;
                autoPoint = point;
                drag = Drag::pointsMove;
                return true;
            }
        }

    // the rubber band: the Select tool on a curve; and in the Clip-launch layout a drag in an automation lane's
    // background, as Live's ("Clicking and dragging in the envelope's background defines a selection": the points in
    // that time range, the whole height; a press on the line still adds a point, Draw Mode still draws)
    const auto liveBackground = lane && layoutstyle::current() == layoutstyle::Style::clipLaunch && ! drawmode::isOn() && ! onPoint
                                && ! flmods::ctrl (e.mods) && ! e.mods.isAltDown() && ! nearLaneLine (spot, p);
    if (spot.valid() && ! flmods::ctrl (e.mods) && ! e.mods.isAltDown() && (tool == EditTool::lasso || liveBackground))
    {
        bandAdds = e.mods.isShiftDown() && spot == pointSpot;
        bandWholeHeight = liveBackground && tool != EditTool::lasso;
        if (! bandAdds)
            selectedPoints.clear();
        pointSpot = spot;
        pointPress = p;
        pointBand = {};
        drag = Drag::pointBand;
        repaint();
        return true;
    }

    if (! selectedPoints.empty() && ! e.mods.isMiddleButtonDown())
        clearPointSelection();
    return false;
}

bool PlaylistComponent::nearLaneLine (const CurveSpot& s, juce::Point<int> p) const
{
    const auto a = laneAutomation (s.track, s.lane);
    if (a < 0)
        return false;
    const auto body = laneBody (s.track, s.lane);
    const auto value = engine.getSong().automations[(std::size_t) a].valueAt (juce::jmax (0.0, xToTick (p.x)));
    return std::abs (body.getBottom() - laneShown (a, value) * body.getHeight() - (float) p.y) <= 5.0f;
}

void PlaylistComponent::pointMouseDrag (const juce::MouseEvent& e)
{
    const auto* points = spotPoints (pointSpot);
    if (points == nullptr)
        return;
    const auto p = e.getPosition();

    if (drag == Drag::pen)
    {
        if (! penStarted)
        {
            if (p.getDistanceFrom (pointPress) < 3)
                return;
            penStarted = true;
            pointsBefore = *points;
            penTick = penLo = penHi = spotTickAt (pointSpot, (float) pointPress.x);
            penValue = spotValueAt (pointSpot, (float) pointPress.y);
        }
        const auto tick = spotTickAt (pointSpot, (float) p.x);
        const auto value = spotValueAt (pointSpot, (float) p.y);
        editSpot (pointSpot, [from = penTick, v0 = penValue, tick, value] (std::vector<AutomationPoint>& list) {
            drawmode::drawLine (list, from, v0, tick, value);
        });
        penLo = juce::jmin (penLo, tick);
        penHi = juce::jmax (penHi, tick);
        penTick = tick;
        penValue = value;
        return;
    }

    if (drag == Drag::pointBand)
    {
        pointBand = juce::Rectangle<int> (pointPress, p).getIntersection (getLocalBounds());
        const auto body = spotBody (pointSpot);
        if (bandWholeHeight) // (Live: a time range of the lane)
            pointBand = juce::Rectangle<int>::leftTopRightBottom (pointBand.getX(), (int) body.getY(), pointBand.getRight(), (int) body.getBottom());
        const auto t0 = spotTickAt (pointSpot, (float) pointBand.getX()), t1 = spotTickAt (pointSpot, (float) pointBand.getRight());
        const auto v0 = spotValueAt (pointSpot, (float) juce::jmin ((float) pointBand.getBottom(), body.getBottom()));
        const auto v1 = spotValueAt (pointSpot, (float) juce::jmax ((float) pointBand.getY(), body.getY()));
        auto inside = autopoints::inBox (*points, t0, t1, v0, v1);
        if (bandAdds)
        {
            inside.insert (inside.end(), selectedPoints.begin(), selectedPoints.end());
            std::sort (inside.begin(), inside.end());
            inside.erase (std::unique (inside.begin(), inside.end()), inside.end());
        }
        if (! bandAdds || inside.size() >= selectedPoints.size())
            selectedPoints = std::move (inside);
        repaint();
        return;
    }

    if (drag == Drag::pointsMove)
    {
        if (autoPoint < 0 || autoPoint >= (int) pointsBefore.size())
            return;
        const auto snapOn = ! e.mods.isAltDown();
        const auto& grabbed = pointsBefore[(std::size_t) autoPoint];
        const auto songTick = snap (xToTick (p.x), snapOn);
        autopoints::Delta want;
        want.ticks = (pointSpot.clip >= 0 ? songTick - spotSongTick (pointSpot, 0) : songTick) - grabbed.tick;
        want.value = spotValueAt (pointSpot, (float) p.y) - grabbed.value;
        // as one point's drag: Shift pressed after the click keeps the time, Ctrl the value
        if (e.mods.isShiftDown() && ! downMods.isShiftDown())
            want.ticks = 0;
        if (flmods::ctrl (e.mods) && ! flmods::ctrl (downMods))
            want.value = 0.0f;
        editSpot (pointSpot, [this, want] (std::vector<AutomationPoint>& list) { list = autopoints::moved (pointsBefore, selectedPoints, want); });
    }
}

void PlaylistComponent::pointMouseUp (const juce::MouseEvent& e)
{
    const auto was = drag;
    drag = Drag::none;
    if (was == Drag::pen)
    {
        if (! penStarted)
        {
            // FL's Ctrl+click on a clip: into / out of the selection
            if (pointSpot.clip >= 0)
            {
                auto s = selection;
                if (isSelected (pointSpot.clip))
                    s.erase (std::find (s.begin(), s.end(), pointSpot.clip));
                else
                    s.push_back (pointSpot.clip);
                setSelection (s);
            }
            pointSpot = {};
            return;
        }
        // the stroke thinned: within 1.5 px of the line it drew (at least 0.4 %, at most 2 % of the range)
        const auto tolerance = juce::jlimit (0.004f, 0.02f, 1.5f / juce::jmax (1.0f, spotBody (pointSpot).getHeight()));
        editSpot (pointSpot, [lo = penLo, hi = penHi, tolerance] (std::vector<AutomationPoint>& list) { autopoints::thin (list, lo, hi, tolerance); });
        pointSpot = {};
        penStarted = false;
        return;
    }
    if (was == Drag::pointBand)
    {
        const auto clicked = pointBand.getWidth() < 3 && pointBand.getHeight() < 3;
        pointBand = {};
        if (clicked)
        {
            // a click: the point under it alone, else (no point) the Select tool's click on the clip selects the clip
            if (const auto point = spotPointAt (pointSpot, pointPress); point >= 0 && ! bandWholeHeight)
                selectedPoints = { point };
            else
            {
                selectedPoints.clear();
                if (pointSpot.clip >= 0)
                    setSelection (e.mods.isShiftDown() ? [this] { auto s = selection; s.push_back (pointSpot.clip); return s; }()
                                                       : std::vector<int> { pointSpot.clip });
            }
        }
        if (selectedPoints.empty())
            pointSpot = {};
        else
            setSelection ({}); // (points, not clips: Delete / Ctrl+C work on the points)
        repaint();
        return;
    }
    if (was == Drag::pointsMove)
        repaint();
}

bool PlaylistComponent::pointKeyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    const auto letter = keys::letterOfKeyCode (code);
    const auto* points = spotPoints (pointSpot);
    const auto haveSelection = points != nullptr && ! selectedPoints.empty();

    if (haveSelection && ! mods.isAnyModifierKeyDown() && (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey))
    {
        editSpot (pointSpot, [sel = selectedPoints] (std::vector<AutomationPoint>& list) { autopoints::erase (list, sel); });
        clearPointSelection();
        return true;
    }
    if (haveSelection && code == juce::KeyPress::escapeKey)
    {
        clearPointSelection();
        return true;
    }
    if (haveSelection && mods.isCommandDown() && ! mods.isShiftDown() && ! mods.isAltDown() && (letter == 'C' || letter == 'X'))
    {
        pointClipboard = autopoints::copy (*points, selectedPoints);
        if (windowclip::enabled()) // (issue #232, round 190nz: and to another LYNX SHARE window)
        {
            crossclipboard::Payload copy;
            copy.kind = crossclipboard::Kind::points;
            copy.points = pointClipboard;
            windowclip::publish (std::move (copy));
        }
        if (letter == 'X')
        {
            editSpot (pointSpot, [sel = selectedPoints] (std::vector<AutomationPoint>& list) { autopoints::erase (list, sel); });
            clearPointSelection();
        }
        return true;
    }
    // Ctrl+V with copied points and the mouse on a curve: pasted at the mouse (snapped), selected (issue #232, round 190nz:
    // points copied in another LYNX SHARE window too)
    if (mods.isCommandDown() && ! mods.isShiftDown() && ! mods.isAltDown() && letter == 'V')
    {
        const auto at = lastMouse; // (the last mouse position over the Playlist)
        const auto spot = curveSpotAt (at);
        if (! spot.valid())
            return false;
        if (auto incoming = windowclip::incoming (crossclipboard::Kind::points); incoming && ! incoming->points.empty())
            pointClipboard = std::move (incoming->points);
        if (pointClipboard.empty())
            return false;
        const auto songTick = snap (xToTick (at.x), true);
        const auto tick = juce::jmax<std::int64_t> (0, spot.clip >= 0 ? songTick - spotSongTick (spot, 0) : songTick);
        std::vector<int> pasted;
        editSpot (spot, [&pasted, tick] (std::vector<AutomationPoint>& list) { pasted = autopoints::paste (list, pointClipboard, tick); });
        setSelection ({});
        pointSpot = spot;
        selectedPoints = pasted;
        repaint();
        return true;
    }
    return false;
}

void PlaylistComponent::paintPointSelection (juce::Graphics& g)
{
    if (const auto* points = spotPoints (pointSpot); points != nullptr && ! selectedPoints.empty())
    {
        const auto body = spotBody (pointSpot);
        const auto a = pointSpot.clip < 0 ? laneAutomation (pointSpot.track, pointSpot.lane) : -1;
        for (const auto i : selectedPoints)
        {
            if (i < 0 || i >= (int) points->size())
                continue;
            const auto& pt = (*points)[(std::size_t) i];
            const auto x = (float) tickToX ((double) spotSongTick (pointSpot, pt.tick));
            const auto shown = pointSpot.clip >= 0 ? pt.value : laneShown (a, pt.value);
            const auto y = body.getBottom() - shown * body.getHeight();
            if (x < body.getX() - 4.0f || x > body.getRight() + 4.0f)
                continue;
            // a selected point: filled in the selection colour inside a light ring (the rest keep their dot)
            g.setColour (theme::colour::bg0);
            g.fillEllipse (x - 4.5f, y - 4.5f, 9.0f, 9.0f);
            g.setColour (theme::colour::textPrimary);
            g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
            g.setColour (theme::colour::accent);
            g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
        }
    }
    if (drag == Drag::pointBand && ! pointBand.isEmpty())
    {
        g.setColour (theme::colour::selectionFill.withMultipliedAlpha (0.5f));
        g.fillRect (pointBand);
        g.setColour (theme::colour::textPrimary.withAlpha (0.7f));
        g.drawRect (pointBand, 1);
    }
}

bool PlaylistComponent::makePointDragHint (ui::Hint& hint)
{
    switch (drag)
    {
        case Drag::pen:
            if (! penStarted)
                return false;
            hint.text = tr ("Pen: drawing the curve freehand (thinned when you let go)");
            hint.value = juce::String (juce::roundToInt (penValue * 100.0f)) + "%";
            return true;
        case Drag::pointBand:
            hint.text = tr ("Selecting points: {0}  [Shift] adds", { juce::String ((int) selectedPoints.size()) });
            return true;
        case Drag::pointsMove:
            hint.text = tr ("Moving {0} point(s): then [Shift] keeps time  [Ctrl] keeps value  [Alt] no snap", { juce::String ((int) selectedPoints.size()) });
            return true;
        default:
            return false;
    }
}

juce::Point<int> PlaylistComponent::curvePointForTests (int clip, int point) const
{
    const auto* points = spotPoints ({ clip, -1, -1 });
    if (points == nullptr || point < 0 || point >= (int) points->size())
        return {};
    return curveSpotXYForTests (clip, (*points)[(std::size_t) point].tick, (*points)[(std::size_t) point].value);
}

juce::Point<int> PlaylistComponent::curveSpotXYForTests (int clip, std::int64_t tick, float value) const
{
    const CurveSpot s { clip, -1, -1 };
    const auto body = spotBody (s);
    return { (int) std::lround (tickToX ((double) spotSongTick (s, tick))), (int) std::lround (body.getBottom() - value * body.getHeight()) };
}

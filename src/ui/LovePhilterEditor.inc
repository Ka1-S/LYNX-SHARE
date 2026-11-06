// Love Philter's articulator editor (round 173d3, gap173d-2; REA_rodada66 + the TSynthEditorForm DFM): included inside
// NativeEffectEditor's class body.
//
// FL's form shows one articulator part at a time under the unit page: the editor target (PAN … WS, the eight
// articulators of the selected unit, plus WS: the unit's shaper curve) × the part (the pattern envelope of the pattern
// shown, the LFO's amplitude envelope, the Mod X / Mod Y / envelope-follower maps). The page encodes the choice: unit
// (×1), target (×8, nine: PAN VOL CUT RES LOW BAND HIGH WS WMIX), part (×72).
// The graph is FL's shared envelope editor (the gestures of Maximus' and Gross Beat's, REA_rodada168m §2 / 168x §1):
//   right-click on empty space   add a point there (dragged while the button is held; it takes the last tension)
//   right-click a point           the point's menu          Alt+right-click a point   delete it
//   right-click a tension handle  straight segment           left-drag a point / handle   move / bend
// The first point stays at time 0. Every edit is written back into FL's own state (philter::readDoc / writeDoc), so a
// project or preset saved after the edit opens in FL with the same curve.

    struct LpPoint
    {
        double x = 0; // the point's time (seconds, beats with the tempo flag) or map input 0…1
        float y = 0, tension = 0;
        std::uint8_t mode = 0;
    };

    int lpDragPoint = -1, lpDragTension = -1, lpHoverPoint = -1, lpHoverHandle = -1;
    bool lpDragging = false;
    std::vector<LpPoint> lpDragPoints;
    double lpDragSpan = 1.0;
    float lpTensionStart = 0.0f, lpLastTension = 0.0f;
    static inline float lpCopiedValue = -1.0f;

    static bool isPhilterCurve (const LayoutItem& item) { return item.type == Item::display && item.display != nullptr && juce::String (item.display) == "lpcurve"; }

    int lpUnit() const { return page % 8; }
    static constexpr int lpShaper = 7; // the WS target: the unit's shaper curve
    int lpTarget() const { return (page / 8) % 9; }
    int lpPart() const { return (page / 72) % 5; } // 0 pattern envelope, 1 LFO, 2 Mod X, 3 Mod Y, 4 follower
    bool lpMap() const { return lpTarget() == lpShaper || lpPart() >= 2; }
    int lpPattern() const // the "Pattern" knob (FL 1…10) picks the pattern envelope shown
    {
        const auto* fx = current();
        return fx != nullptr && fx->params.size() > 5 ? juce::jlimit (0, 9, juce::roundToInt (fx->params[5] * 9.0f)) : 0;
    }

    static philter::DocEnv& lpEnvIn (philter::Doc& d, int unit, int target, int part, int pattern)
    {
        if (target == lpShaper)
            return d.u[(std::size_t) unit].ws;
        auto& a = d.u[(std::size_t) unit].art[(std::size_t) (target < lpShaper ? target : 7)];
        return part == 0 ? a.pat[(std::size_t) pattern] : part == 1 ? a.lfo : a.maps[(std::size_t) part - 2];
    }

    std::optional<philter::DocEnv> lpEnv() const
    {
        const auto* fx = current();
        if (fx == nullptr)
            return std::nullopt;
        auto d = philter::readDoc (fx->state);
        if (! d)
            return std::nullopt;
        return lpEnvIn (*d, lpUnit(), lpTarget(), lpPart(), lpPattern());
    }

    static std::vector<LpPoint> lpPointsOf (const philter::DocEnv& e)
    {
        std::vector<LpPoint> out;
        double x = 0;
        float prev = 0.0f;
        for (std::size_t i = 0; i < e.pts.size(); ++i)
        {
            const auto& q = e.pts[i];
            x += i == 0 ? 0.0 : std::max (0.0, q.dt);
            const auto y = std::isnan (q.y) ? prev : (float) q.y;
            prev = y;
            out.push_back ({ x, juce::jlimit (0.0f, 1.0f, y), q.tension, q.mode });
        }
        return out;
    }

    std::vector<LpPoint> lpPoints() const
    {
        if (lpDragging)
            return lpDragPoints;
        const auto e = lpEnv();
        return e ? lpPointsOf (*e) : std::vector<LpPoint> {};
    }

    // the time shown across the graph: a map's 0…1, an envelope's length and a little room after its last point
    double lpSpan() const
    {
        if (lpDragging)
            return lpDragSpan;
        if (lpMap())
            return 1.0;
        const auto pts = lpPoints();
        return std::max (1.0, (pts.empty() ? 0.0 : pts.back().x) * 1.25);
    }

    // the envelope rebuilt from absolute points: the dt between points, FL's sustain / loop / decay marks following
    // the points they were set on (a mark on a deleted point goes away)
    static philter::DocEnv lpWithPoints (philter::DocEnv e, const std::vector<LpPoint>& pts, int inserted, int removed)
    {
        const auto shift = [&] (int& mark) {
            if (mark < 0)
                return;
            if (removed >= 0)
                mark = mark == removed ? -1 : mark > removed ? mark - 1 : mark;
            if (inserted >= 0 && mark >= inserted)
                ++mark;
        };
        shift (e.sus), shift (e.loop), shift (e.decay);
        std::vector<philter::DocPoint> out;
        for (std::size_t i = 0; i < pts.size(); ++i)
        {
            philter::DocPoint q;
            const auto source = (int) i == inserted ? -1 : removed >= 0 && (int) i >= removed ? (int) i + 1 : inserted >= 0 && (int) i > inserted ? (int) i - 1 : (int) i;
            if (source >= 0 && source < (int) e.pts.size())
                q = e.pts[(std::size_t) source]; // (keeps FL's padding bytes)
            q.dt = i == 0 ? 0.0 : std::max (0.0, pts[i].x - pts[i - 1].x);
            q.y = pts[i].y;
            q.tension = pts[i].tension;
            q.mode = pts[i].mode;
            out.push_back (q);
        }
        e.pts = std::move (out);
        return e;
    }

    void lpWrite (const std::vector<LpPoint>& points, int inserted = -1, int removed = -1)
    {
        const auto* fx = current();
        if (fx == nullptr || points.empty())
            return;
        auto d = philter::readDoc (fx->state);
        if (! d)
            return;
        auto& e = lpEnvIn (*d, lpUnit(), lpTarget(), lpPart(), lpPattern());
        e = lpWithPoints (e, points, inserted, removed);
        if (usesBar())
            bar.willEdit();
        editSlot ([bytes = philter::writeDoc (*d)] (EffectSlot& target) { target.state = bytes; });
        repaint();
    }

    juce::Rectangle<float> lpArea (const LayoutItem& item) const { return rectOf (item).reduced (6.0f * scale()); }

    juce::Point<float> lpPos (juce::Rectangle<float> a, double x, float y, double span) const
    {
        return { a.getX() + a.getWidth() * (float) (x / span), a.getBottom() - a.getHeight() * juce::jlimit (0.0f, 1.0f, y) };
    }

    std::pair<double, float> lpMouseValue (const LayoutItem& item, juce::Point<float> p, double span) const
    {
        const auto a = lpArea (item);
        return { span * juce::jlimit (0.0, 1.0, (double) ((p.x - a.getX()) / juce::jmax (1.0f, a.getWidth()))),
                 juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / juce::jmax (1.0f, a.getHeight())) };
    }

    // the curve as the plugin steps it at x (the engine's own curve maths), for the handles and "stick to the curve"
    float lpLevelAt (const std::vector<LpPoint>& pts, double x, double span) const
    {
        const auto e = lpEnv();
        if (! e || pts.empty())
            return pts.empty() ? 0.0f : pts.front().y;
        constexpr int n = 256;
        const auto shape = philter::curveShape (lpWithPoints (*e, pts, -1, -1), lpMap(), n + 1, span);
        const auto f = juce::jlimit (0.0, 1.0, x / span) * n;
        const auto i = juce::jmin (n - 1, (int) f);
        return shape[(std::size_t) i] + (shape[(std::size_t) i + 1] - shape[(std::size_t) i]) * (float) (f - i);
    }

    int lpPointAt (const LayoutItem& item, juce::Point<float> p) const
    {
        const auto pts = lpPoints();
        const auto a = lpArea (item);
        const auto span = lpSpan();
        int best = -1;
        auto bestD = 7.0f * scale();
        for (std::size_t i = 0; i < pts.size(); ++i)
            if (const auto d = lpPos (a, pts[i].x, pts[i].y, span).getDistanceFrom (p); d <= bestD)
                best = (int) i, bestD = d;
        return best;
    }

    juce::Point<float> lpHandlePosIn (juce::Rectangle<float> a, const std::vector<LpPoint>& pts, int i, double span) const
    {
        if (i < 1 || i >= (int) pts.size())
            return { -9999.0f, -9999.0f };
        const auto x0 = pts[(std::size_t) i - 1].x, x1 = pts[(std::size_t) i].x;
        if ((x1 - x0) / span * a.getWidth() < 14.0f * scale())
            return { -9999.0f, -9999.0f };
        const auto xm = 0.5 * (x0 + x1);
        return lpPos (a, xm, lpLevelAt (pts, xm, span), span);
    }

    int lpHandleAt (const LayoutItem& item, juce::Point<float> p) const
    {
        const auto pts = lpPoints();
        const auto span = lpSpan();
        for (int i = 1; i < (int) pts.size(); ++i)
            if (lpHandlePosIn (lpArea (item), pts, i, span).getDistanceFrom (p) <= 6.0f * scale())
                return i;
        return -1;
    }

    juce::String lpXText (double x) const
    {
        if (lpMap())
            return juce::String (juce::roundToInt (x * 100.0)) + "%";
        const auto e = lpEnv();
        return e && (e->flags & 1) != 0 ? juce::String (x, 2) + " " + tr ("beats") : juce::String (x, 2) + " s";
    }

    void lpShowTip (const LayoutItem& item, const LpPoint& p)
    {
        ui::showValueTip (*this, rectOf (item).toNearestInt(), lpXText (p.x) + "  " + juce::String (juce::roundToInt (p.y * 100.0f)) + "%");
    }

    // clamps a moved point between its neighbours (the first stays at 0, a map's points within 0…1)
    void lpMovePoint (std::vector<LpPoint>& pts, int i, double x, float y) const
    {
        if (i < 0 || i >= (int) pts.size())
            return;
        auto& p = pts[(std::size_t) i];
        const auto lo = i == 0 ? 0.0 : pts[(std::size_t) i - 1].x;
        const auto hi = i == 0 ? 0.0 : i + 1 < (int) pts.size() ? pts[(std::size_t) i + 1].x : lpMap() ? 1.0 : 1.0e6;
        p.x = juce::jlimit (lo, hi, x);
        p.y = y;
    }

    bool lpMouseDown (const juce::MouseEvent& e, const LayoutItem& item)
    {
        auto points = lpPoints();
        if (points.empty())
            return false;
        const auto span = lpSpan();
        const auto point = lpPointAt (item, e.position);
        const auto handle = point < 0 ? lpHandleAt (item, e.position) : -1;
        if (e.mods.isPopupMenu())
        {
            if (point > 0 && e.mods.isAltDown())
            {
                points.erase (points.begin() + point);
                lpWrite (points, -1, point);
                return false;
            }
            if (point >= 0)
            {
                lpPointMenu (point);
                return false;
            }
            if (handle >= 0)
            {
                points[(std::size_t) handle].tension = 0.0f;
                lpLastTension = 0.0f;
                lpWrite (points);
                return false;
            }
            auto [x, y] = lpMouseValue (item, e.position, span);
            const auto onCurve = lpLevelAt (points, x, span);
            if (std::abs (lpPos (lpArea (item), x, onCurve, span).y - e.position.y) < 8.0f * scale() || e.mods.isShiftDown())
                y = onCurve;
            int at = (int) points.size();
            for (int i = 1; i < (int) points.size(); ++i)
                if (x < points[(std::size_t) i].x)
                {
                    at = i;
                    break;
                }
            if (lpMap() && at == (int) points.size() && x > 1.0)
                return false;
            points.insert (points.begin() + at, LpPoint { x, y, lpLastTension, 0 });
            lpDragPoint = at;
            lpDragPoints = points;
            lpDragSpan = span;
            lpDragging = true;
            lpWrite (points, at, -1);
            lpShowTip (item, points[(std::size_t) at]);
            return true;
        }
        if (handle >= 0 && e.mods.isAltDown())
        {
            points[(std::size_t) handle].tension = 0.0f;
            lpLastTension = 0.0f;
            lpWrite (points);
            return false;
        }
        if (point >= 0)
        {
            lpDragPoint = point;
            lpDragPoints = points;
            lpDragSpan = span;
            lpDragging = true;
            lpShowTip (item, points[(std::size_t) point]);
            return true;
        }
        if (handle >= 0)
        {
            lpDragTension = handle;
            lpTensionStart = points[(std::size_t) handle].tension;
            lpDragPoints = points;
            lpDragSpan = span;
            lpDragging = true;
            return true;
        }
        return false;
    }

    void lpMouseDrag (const juce::MouseEvent& e, const LayoutItem& item)
    {
        if (! lpDragging)
            return;
        auto points = lpDragPoints;
        if (lpDragTension >= 0 && lpDragTension < (int) points.size())
        {
            // up raises the segment's middle: the handle follows the mouse
            const auto& a = points[(std::size_t) lpDragTension - 1];
            const auto& b = points[(std::size_t) lpDragTension];
            const auto sign = b.y >= a.y ? -1.0f : 1.0f;
            const auto dy = -(float) e.getDistanceFromDragStartY() / ((e.mods.isCtrlDown() ? 600.0f : 150.0f) * scale());
            auto& t = points[(std::size_t) lpDragTension].tension;
            t = juce::jlimit (-1.0f, 1.0f, lpTensionStart + sign * dy);
            lpLastTension = t;
            ui::showValueTip (*this, rectOf (item).toNearestInt(), tr ("Tension") + " " + juce::String (juce::roundToInt (t * 100.0f)) + "%");
        }
        else if (lpDragPoint >= 0 && lpDragPoint < (int) points.size())
        {
            const auto [x, y] = lpMouseValue (item, e.position, lpDragSpan);
            lpMovePoint (points, lpDragPoint, x, y);
            lpShowTip (item, points[(std::size_t) lpDragPoint]);
        }
        else
            return;
        lpDragPoints = points;
        lpWrite (points);
    }

    void lpMouseUp()
    {
        lpDragging = false;
        lpDragPoint = lpDragTension = -1;
        ui::hideValueTip();
        repaint();
    }

    bool lpMouseMove (const LayoutItem& item, juce::Point<float> p)
    {
        if (! isPhilterCurve (item))
        {
            if (lpHoverPoint >= 0 || lpHoverHandle >= 0)
            {
                lpHoverPoint = lpHoverHandle = -1;
                repaint();
            }
            return false;
        }
        const auto point = lpPointAt (item, p), handle = point < 0 ? lpHandleAt (item, p) : -1;
        if (point != lpHoverPoint || handle != lpHoverHandle)
        {
            lpHoverPoint = point;
            lpHoverHandle = handle;
            repaint();
        }
        setMouseCursor (point >= 0 ? juce::MouseCursor::DraggingHandCursor
                                   : handle >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::CrosshairCursor);
        return true;
    }

    void lpPointMenu (int point)
    {
        const auto pts = lpPoints();
        if (point < 0 || point >= (int) pts.size())
            return;
        const auto& p = pts[(std::size_t) point];
        juce::PopupMenu menu;
        menu.addSectionHeader (tr ("Point {0}/{1}", { juce::String (point + 1), juce::String ((int) pts.size()) }) + ": " + lpXText (p.x) + "  "
                               + juce::String (juce::roundToInt (p.y * 100.0f)) + "%");
        menu.addItem (tr ("Delete"), point > 0, false, [this, point] {
            auto now = lpPoints();
            if (point > 0 && point < (int) now.size())
            {
                now.erase (now.begin() + point);
                lpWrite (now, -1, point);
            }
        });
        if (point > 0) // the shape of the segment that ends here (FL's Mode list)
        {
            menu.addSectionHeader (tr ("Mode"));
            for (const auto& m : flshape::waveShaperModes())
                menu.addItem (tr (m.name), true, p.mode == m.mode, [this, point, mode = m.mode] {
                    auto now = lpPoints();
                    if (point < (int) now.size())
                    {
                        now[(std::size_t) point].mode = mode;
                        lpWrite (now);
                    }
                });
        }
        if (! lpMap()) // FL's ADSR section: the loop start and the sustain / loop end marks
            if (const auto e = lpEnv())
            {
                menu.addSectionHeader (tr ("ADSR section"));
                menu.addItem (tr ("Loop start"), true, e->loop == point, [this, point] { lpSetMark (true, point); });
                menu.addItem (tr ("Sustain / loop end"), true, e->sus == point, [this, point] { lpSetMark (false, point); });
            }
        menu.addSeparator();
        menu.addItem (tr ("Copy value"), [y = p.y] { lpCopiedValue = y; });
        menu.addItem (tr ("Paste value"), lpCopiedValue >= 0.0f, false, [this, point] {
            auto now = lpPoints();
            if (point < (int) now.size())
            {
                now[(std::size_t) point].y = lpCopiedValue;
                lpWrite (now);
            }
        });
        menu.addItem (tr ("Type in value..."), [this, point] { lpTypeValue (point); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    // a mark set on the point again clears it (FL's ticked menu entries)
    void lpSetMark (bool loop, int point)
    {
        const auto* fx = current();
        if (fx == nullptr)
            return;
        auto d = philter::readDoc (fx->state);
        if (! d)
            return;
        auto& e = lpEnvIn (*d, lpUnit(), lpTarget(), lpPart(), lpPattern());
        auto& mark = loop ? e.loop : e.sus;
        mark = mark == point ? -1 : point;
        if (usesBar())
            bar.willEdit();
        editSlot ([bytes = philter::writeDoc (*d)] (EffectSlot& target) { target.state = bytes; });
        repaint();
    }

    void lpTypeValue (int point)
    {
        const auto pts = lpPoints();
        if (point < 0 || point >= (int) pts.size())
            return;
        auto* window = new juce::AlertWindow (tr ("Point value"), tr ("Percent (0 to 100)"), juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("value", juce::String (pts[(std::size_t) point].y * 100.0f, 1));
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<NativeEffectEditor> self (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([self, window, point] (int result) {
                                     if (result != 1 || self == nullptr)
                                         return;
                                     auto now = self->lpPoints();
                                     if (point < (int) now.size())
                                     {
                                         now[(std::size_t) point].y = juce::jlimit (0.0f, 1.0f, window->getTextEditorContents ("value").getFloatValue() / 100.0f);
                                         self->lpWrite (now);
                                     }
                                 }),
                                 true);
    }

    juce::String lpHint (const LayoutItem& item, juce::Point<float> p) const
    {
        if (lpPointAt (item, p) >= 0)
            return tr ("Point: [LMB] drag moves  [RMB] shape, value, delete  [Alt][RMB] delete");
        if (lpHandleAt (item, p) >= 0)
            return tr ("Tension: [LMB] drag up / down bends the segment ([Ctrl] fine)  [RMB] straight");
        return tr ("Articulator: [RMB] add a point (hold and drag to place it)  [LMB] drag a point");
    }

    void paintPhilterCurve (juce::Graphics& g, juce::Rectangle<float> inner, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        const auto c = fx.enabled ? colour::accent : colour::textDisabled;
        const auto span = lpSpan();
        g.setColour (colour::gridBeat);
        const auto lines = lpMap() ? 4 : juce::jmax (1, (int) std::ceil (span));
        for (int i = 1; i < lines; ++i)
            g.drawVerticalLine ((int) (inner.getX() + inner.getWidth() * (float) i / (float) lines), inner.getY(), inner.getBottom());
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine ((int) (inner.getY() + inner.getHeight() * (float) i / 4.0f), inner.getX(), inner.getRight());
        const auto e = lpEnv();
        const auto pts = lpPoints();
        if (e && ! pts.empty())
        {
            const auto steps = juce::jmax (32, (int) inner.getWidth() / 2);
            const auto shape = philter::curveShape (lpWithPoints (*e, pts, -1, -1), lpMap(), steps + 1, span);
            juce::Path p;
            for (int i = 0; i <= steps; ++i)
            {
                const auto at = lpPos (inner, span * (double) i / (double) steps, shape[(std::size_t) i], span);
                if (i == 0)
                    p.startNewSubPath (at);
                else
                    p.lineTo (at);
            }
            auto fill = p;
            fill.lineTo (inner.getRight(), inner.getBottom());
            fill.lineTo (inner.getX(), inner.getBottom());
            fill.closeSubPath();
            g.setColour (c.withAlpha (0.14f));
            g.fillPath (fill);
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (juce::jmax (1.5f, 2.0f * s)));
            if (! lpMap() && e->sus >= 0 && e->sus < (int) pts.size()) // the sustain point's time
            {
                g.setColour (colour::textSecondary.withAlpha (0.6f));
                g.drawVerticalLine ((int) lpPos (inner, pts[(std::size_t) e->sus].x, 0.0f, span).x, inner.getY(), inner.getBottom());
            }
            for (int i = 1; i < (int) pts.size(); ++i)
                if (const auto h = lpHandlePosIn (inner, pts, i, span); h.x > -1000.0f)
                {
                    const auto hot = i == lpHoverHandle || i == lpDragTension;
                    g.setColour (hot ? colour::textPrimary : c.withAlpha (0.85f));
                    g.drawEllipse (juce::Rectangle<float> (8.0f * s, 8.0f * s).withCentre (h), juce::jmax (1.0f, 1.3f * s));
                }
            for (int i = 0; i < (int) pts.size(); ++i)
            {
                const auto at = lpPos (inner, pts[(std::size_t) i].x, pts[(std::size_t) i].y, span);
                const auto hot = i == lpHoverPoint || i == lpDragPoint;
                g.setColour (colour::bg0);
                g.fillEllipse (juce::Rectangle<float> (11.0f * s, 11.0f * s).withCentre (at));
                g.setColour (hot ? colour::textPrimary : c);
                g.fillEllipse (juce::Rectangle<float> (7.0f * s, 7.0f * s).withCentre (at));
            }
        }
    }

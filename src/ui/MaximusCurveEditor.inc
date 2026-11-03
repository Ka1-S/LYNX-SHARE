// Maximus' In/Out compression curve editor (round 168x, issue #40; REA_rodada168x §1): included inside
// NativeEffectEditor's class body.
//
// Before this round the "maxcurve" display only drew the selected band's curve: the graph never received the mouse
// (itemAt did not hit-test it) and had no editing code, on every OS. FL's Maximus graph is its shared envelope editor
// (Maximus_x64.dll MouseDown FUN_0081e410, the same code as Gross Beat's FUN_00821760 in REA_rodada168m §2):
//   right-click on empty space   "add point" at the mouse (sticks to the curve within 8 px; Shift = on the curve),
//                                 dragged while the button is held; the new point gets the last used tension
//   right-click a point           the point's menu        Alt+right-click a point   "delete point"
//   right-click a tension handle  "default tension"       Alt+left on a handle      "default tension"
//   left-drag a point             "move point"            left-drag a handle        "move tension"
// "The first point of the envelope can't be removed."; LYNX also keeps the last one (it holds the curve's end at x = 1).
// Every edit is written back into the band's envelope in FL's own state layout (core/MaximusFx withCurve).

    int mxDragPoint = -1, mxDragTension = -1, mxHoverPoint = -1, mxHoverHandle = -1;
    bool mxDragging = false;
    std::vector<maximusfx::CurveEditPoint> mxDragPoints;
    float mxTensionStart = 0.0f, mxTensionSign = 1.0f;
    float mxLastTension = 0.0f; // FL: a new point takes the tension used last (editor field 0x15dc)
    static inline float mxCopiedValue = -1.0f;

    static bool isMaximusCurve (const LayoutItem& item) { return item.type == Item::display && item.display != nullptr && juce::String (item.display) == "maxcurve"; }

    int mxBand() const { return juce::jlimit (0, 3, page); }

    std::vector<maximusfx::CurveEditPoint> mxPoints() const
    {
        if (mxDragging)
            return mxDragPoints; // shown at once while the edit travels to the engine
        const auto* fx = current();
        return fx != nullptr ? maximusfx::curveForEditing (fx->state, mxBand()) : std::vector<maximusfx::CurveEditPoint> {};
    }

    void mxWrite (const std::vector<maximusfx::CurveEditPoint>& points)
    {
        const auto* fx = current();
        if (fx == nullptr || points.size() < 2)
            return;
        if (usesBar())
            bar.willEdit();
        editSlot ([bytes = maximusfx::withCurve (fx->state, mxBand(), points)] (EffectSlot& target) { target.state = bytes; });
        repaint();
    }

    // the graph's inner area: the same as paintDisplay's (the well reduced by 6 units)
    juce::Rectangle<float> mxArea (const LayoutItem& item) const { return rectOf (item).reduced (6.0f * scale()); }

    static juce::Point<float> mxPos (juce::Rectangle<float> a, double x, float y)
    {
        return { a.getX() + a.getWidth() * (float) x, a.getBottom() - a.getHeight() * juce::jlimit (0.0f, 1.0f, y) };
    }

    std::pair<double, float> mxMouseValue (const LayoutItem& item, juce::Point<float> p) const
    {
        const auto a = mxArea (item);
        return { juce::jlimit (0.0, 1.0, (double) ((p.x - a.getX()) / juce::jmax (1.0f, a.getWidth()))),
                 juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / juce::jmax (1.0f, a.getHeight())) };
    }

    // the curve's level at x as the engine plays it (straight lines between the points when a segment type is not played)
    static float mxLevelAt (const std::vector<maximusfx::CurveEditPoint>& pts, double x)
    {
        constexpr int n = 512;
        const auto shape = maximusfx::curveShape (pts, n);
        if (! shape.empty())
        {
            const auto f = juce::jlimit (0.0, 1.0, x) * n;
            const auto i = juce::jmin (n - 1, (int) f);
            const auto t = (float) (f - i);
            return shape[(std::size_t) i] + (shape[(std::size_t) i + 1] - shape[(std::size_t) i]) * t;
        }
        for (std::size_t i = 1; i < pts.size(); ++i)
            if (x <= pts[i].x)
            {
                const auto w = pts[i].x - pts[i - 1].x;
                return w <= 0.0 ? pts[i].y : pts[i - 1].y + (pts[i].y - pts[i - 1].y) * (float) ((x - pts[i - 1].x) / w);
            }
        return pts.empty() ? 0.0f : pts.back().y;
    }

    int mxPointAt (const LayoutItem& item, juce::Point<float> p) const
    {
        const auto pts = mxPoints();
        const auto a = mxArea (item);
        int best = -1;
        auto bestD = 7.0f * scale();
        for (std::size_t i = 0; i < pts.size(); ++i)
            if (const auto d = mxPos (a, pts[i].x, pts[i].y).getDistanceFrom (p); d <= bestD)
                best = (int) i, bestD = d;
        return best;
    }

    // the tension handle of the segment that ends at point i (none on a very short segment)
    juce::Point<float> mxHandlePos (const LayoutItem& item, const std::vector<maximusfx::CurveEditPoint>& pts, int i) const
    {
        return mxHandlePosIn (mxArea (item), pts, i);
    }

    int mxHandleAt (const LayoutItem& item, juce::Point<float> p) const
    {
        const auto pts = mxPoints();
        for (int i = 1; i < (int) pts.size(); ++i)
            if (mxHandlePos (item, pts, i).getDistanceFrom (p) <= 6.0f * scale())
                return i;
        return -1;
    }

    void mxShowTip (const LayoutItem& item, double x, float y)
    {
        // FL's hint "In: … out: …"
        ui::showValueTip (*this, rectOf (item).toNearestInt(),
                          tr ("In") + " " + juce::String (maximusfx::curveLevelText (x)) + "  " + tr ("Out") + " " + juce::String (maximusfx::curveLevelText (y)));
    }

    // mouse down on the graph; true = a drag follows
    bool mxMouseDown (const juce::MouseEvent& e, const LayoutItem& item)
    {
        auto points = mxPoints();
        if (points.size() < 2)
            return false;
        const auto point = mxPointAt (item, e.position);
        const auto handle = point < 0 ? mxHandleAt (item, e.position) : -1;
        if (e.mods.isPopupMenu())
        {
            if (point >= 0 && e.mods.isAltDown()) // FL: Alt+right-click deletes the point
            {
                if (maximusfx::removeCurvePoint (points, point))
                    mxWrite (points);
                return false;
            }
            if (point >= 0)
            {
                mxPointMenu (point);
                return false;
            }
            if (handle >= 0) // FL: "default tension"
            {
                points[(std::size_t) handle].tension = 0.0f;
                mxLastTension = 0.0f;
                mxWrite (points);
                return false;
            }
            // FL: "add point" at the mouse, dragged while the button is held
            auto [x, y] = mxMouseValue (item, e.position);
            const auto onCurve = mxLevelAt (points, x);
            if (std::abs (mxPos (mxArea (item), x, onCurve).y - e.position.y) < 8.0f * scale() || e.mods.isShiftDown())
                y = onCurve; // sticks to the curve within 8 pixels; Shift: on the curve
            mxDragPoint = maximusfx::addCurvePoint (points, x, y, mxLastTension, 0);
            if (mxDragPoint < 0)
                return false;
            mxDragPoints = points;
            mxDragging = true;
            mxWrite (points);
            mxShowTip (item, points[(std::size_t) mxDragPoint].x, points[(std::size_t) mxDragPoint].y);
            return true;
        }
        if (handle >= 0 && e.mods.isAltDown()) // FL: Alt+click on a handle = default tension
        {
            points[(std::size_t) handle].tension = 0.0f;
            mxLastTension = 0.0f;
            mxWrite (points);
            return false;
        }
        if (point >= 0)
        {
            mxDragPoint = point;
            mxDragPoints = points;
            mxDragging = true;
            mxShowTip (item, points[(std::size_t) point].x, points[(std::size_t) point].y);
            return true;
        }
        if (handle >= 0)
        {
            mxDragTension = handle;
            mxTensionStart = points[(std::size_t) handle].tension;
            // which way a positive change bends the curve at the handle: the handle follows the mouse up and down
            auto bent = points;
            bent[(std::size_t) handle].tension = juce::jlimit (-1.0f, 1.0f, mxTensionStart + (mxTensionStart > 0.8f ? -0.1f : 0.1f));
            const auto xm = 0.5 * (points[(std::size_t) handle - 1].x + points[(std::size_t) handle].x);
            const auto up = (mxLevelAt (bent, xm) - mxLevelAt (points, xm)) * (bent[(std::size_t) handle].tension - mxTensionStart);
            mxTensionSign = up < 0.0f ? -1.0f : 1.0f;
            mxDragPoints = points;
            mxDragging = true;
            return true;
        }
        return false;
    }

    void mxMouseDrag (const juce::MouseEvent& e, const LayoutItem& item)
    {
        if (! mxDragging)
            return;
        auto points = mxDragPoints;
        if (mxDragTension >= 0 && mxDragTension < (int) points.size())
        {
            const auto dy = -(float) e.getDistanceFromDragStartY() / ((e.mods.isCtrlDown() ? 600.0f : 150.0f) * scale());
            auto& t = points[(std::size_t) mxDragTension].tension;
            t = juce::jlimit (-1.0f, 1.0f, mxTensionStart + mxTensionSign * dy);
            mxLastTension = t;
            ui::showValueTip (*this, rectOf (item).toNearestInt(), tr ("Tension") + " " + juce::String (juce::roundToInt (t * 100.0f)) + "%");
        }
        else if (mxDragPoint >= 0 && mxDragPoint < (int) points.size())
        {
            const auto [x, y] = mxMouseValue (item, e.position);
            maximusfx::moveCurvePoint (points, mxDragPoint, x, y);
            mxShowTip (item, points[(std::size_t) mxDragPoint].x, points[(std::size_t) mxDragPoint].y);
        }
        else
            return;
        mxDragPoints = points;
        mxWrite (points);
    }

    void mxMouseUp()
    {
        mxDragging = false;
        mxDragPoint = mxDragTension = -1;
        ui::hideValueTip();
        repaint();
    }

    bool mxMouseMove (const LayoutItem& item, juce::Point<float> p)
    {
        if (! isMaximusCurve (item))
        {
            if (mxHoverPoint >= 0 || mxHoverHandle >= 0)
            {
                mxHoverPoint = mxHoverHandle = -1;
                repaint();
            }
            return false;
        }
        const auto point = mxPointAt (item, p), handle = point < 0 ? mxHandleAt (item, p) : -1;
        if (point != mxHoverPoint || handle != mxHoverHandle)
        {
            mxHoverPoint = point;
            mxHoverHandle = handle;
            repaint();
        }
        setMouseCursor (point >= 0 ? juce::MouseCursor::DraggingHandCursor
                                   : handle >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::CrosshairCursor);
        return true;
    }

    void mxPointMenu (int point)
    {
        const auto pts = mxPoints();
        if (point < 0 || point >= (int) pts.size())
            return;
        const auto& p = pts[(std::size_t) point];
        juce::PopupMenu menu;
        menu.addSectionHeader (tr ("Point {0}/{1}", { juce::String (point + 1), juce::String ((int) pts.size()) }) + ": "
                               + tr ("In") + " " + juce::String (maximusfx::curveLevelText (p.x)) + "  " + tr ("Out") + " "
                               + juce::String (maximusfx::curveLevelText (p.y)));
        menu.addItem (tr ("Delete"), point > 0 && point + 1 < (int) pts.size(), false, [this, point] {
            auto now = mxPoints();
            if (maximusfx::removeCurvePoint (now, point))
                mxWrite (now);
        });
        if (point > 0) // the shape of the segment that ends here: the FL modes LYNX's Maximus engine plays
        {
            menu.addSectionHeader (tr ("Mode"));
            for (const auto& m : flshape::waveShaperModes())
                if (hlfx::maximusCurveTypeSupported (m.mode))
                    menu.addItem (tr (m.name), true, p.type == m.mode, [this, point, mode = m.mode] {
                        auto now = mxPoints();
                        if (point < (int) now.size())
                        {
                            now[(std::size_t) point].type = mode;
                            mxWrite (now);
                        }
                    });
        }
        menu.addSeparator();
        menu.addItem (tr ("Copy value"), [y = p.y] { mxCopiedValue = y; });
        menu.addItem (tr ("Paste value"), mxCopiedValue >= 0.0f, false, [this, point] {
            auto now = mxPoints();
            if (point < (int) now.size())
            {
                now[(std::size_t) point].y = mxCopiedValue;
                mxWrite (now);
            }
        });
        menu.addItem (tr ("Type in value..."), [this, point] { mxTypeValue (point); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void mxTypeValue (int point)
    {
        const auto pts = mxPoints();
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
                                     auto now = self->mxPoints();
                                     if (point < (int) now.size())
                                     {
                                         now[(std::size_t) point].y = juce::jlimit (0.0f, 1.0f, window->getTextEditorContents ("value").getFloatValue() / 100.0f);
                                         self->mxWrite (now);
                                     }
                                 }),
                                 true);
    }

    juce::String mxHint (const LayoutItem& item, juce::Point<float> p) const
    {
        if (mxPointAt (item, p) >= 0)
            return tr ("Point: [LMB] drag moves  [RMB] shape, value, delete  [Alt][RMB] delete");
        if (mxHandleAt (item, p) >= 0)
            return tr ("Tension: [LMB] drag up / down bends the segment ([Ctrl] fine)  [RMB] straight");
        return tr ("Compression curve (input across, output up): [RMB] add a point (hold and drag to place it)  [LMB] drag a point");
    }

    void paintMaximusCurve (juce::Graphics& g, juce::Rectangle<float> inner, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        const auto band = mxBand();
        const auto c = fx.enabled ? peqview::bandColour (band) : colour::textDisabled;
        g.setColour (colour::gridBeat);
        for (int i = 1; i < 4; ++i)
        {
            g.drawVerticalLine ((int) (inner.getX() + inner.getWidth() * (float) i / 4.0f), inner.getY(), inner.getBottom());
            g.drawHorizontalLine ((int) (inner.getY() + inner.getHeight() * (float) i / 4.0f), inner.getX(), inner.getRight());
        }
        g.setColour (colour::gridBar);
        g.drawLine ({ inner.getBottomLeft(), inner.getTopRight() }, 1.0f);
        g.drawVerticalLine ((int) inner.getCentreX(), inner.getY(), inner.getBottom());
        g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());
        const auto pts = mxDragging ? mxDragPoints : maximusfx::curveForEditing (fx.state, band);
        if (pts.size() >= 2)
        {
            juce::Path p;
            const auto steps = juce::jmax (32, (int) inner.getWidth() / 2);
            const auto shape = maximusfx::curveShape (pts, steps);
            for (int i = 0; i <= steps; ++i)
            {
                const auto x = (double) i / (double) steps;
                const auto y = shape.empty() ? mxLevelAt (pts, x) : shape[(std::size_t) i];
                const auto at = mxPos (inner, x, y);
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
            // tension handles (hollow, between the points), then the points
            for (int i = 1; i < (int) pts.size(); ++i)
                if (const auto h = mxHandlePosIn (inner, pts, i); h.x > -1000.0f)
                {
                    const auto hot = i == mxHoverHandle || i == mxDragTension;
                    g.setColour (hot ? colour::textPrimary : c.withAlpha (0.85f));
                    g.drawEllipse (juce::Rectangle<float> (8.0f * s, 8.0f * s).withCentre (h), juce::jmax (1.0f, 1.3f * s));
                }
            for (int i = 0; i < (int) pts.size(); ++i)
            {
                const auto at = mxPos (inner, pts[(std::size_t) i].x, pts[(std::size_t) i].y);
                const auto hot = i == mxHoverPoint || i == mxDragPoint;
                g.setColour (colour::bg0);
                g.fillEllipse (juce::Rectangle<float> (11.0f * s, 11.0f * s).withCentre (at));
                g.setColour (hot ? colour::textPrimary : c);
                g.fillEllipse (juce::Rectangle<float> (7.0f * s, 7.0f * s).withCentre (at));
            }
        }
        g.setColour (colour::textSecondary);
        g.setFont (font (juce::jmax (8.0f, 9.0f * s)));
        g.drawText (tr ("Out"), inner.withHeight (12.0f * s).toNearestInt(), juce::Justification::topLeft, false);
        g.drawText (tr ("In"), inner.withTop (inner.getBottom() - 12.0f * s).toNearestInt(), juce::Justification::bottomRight, false);
    }

    juce::Point<float> mxHandlePosIn (juce::Rectangle<float> a, const std::vector<maximusfx::CurveEditPoint>& pts, int i) const
    {
        if (i < 1 || i >= (int) pts.size())
            return { -9999.0f, -9999.0f };
        const auto x0 = pts[(std::size_t) i - 1].x, x1 = pts[(std::size_t) i].x;
        if ((x1 - x0) * a.getWidth() < 14.0f * scale())
            return { -9999.0f, -9999.0f };
        const auto xm = 0.5 * (x0 + x1);
        return mxPos (a, xm, mxLevelAt (pts, xm));
    }

// Gross Beat's editor (round 168m, REA_rodada168m): included inside NativeEffectEditor's class body.
//
// FL's Gross Beat window (DFM TSynthEditorForm of the plugin DLL) beside the slot grids: a column of tools (Options menu,
// the Snap selector "off / 1/16 … 1/3 beat", the Envelope selector "time travel / volume"), the envelope editor of the
// selected slot (FL's shared MEnvEditor: right-click adds a hold point and drags it, left-drag moves a point or a
// tension handle, right-click a point = its menu, Alt+right-click deletes, right-click a handle = straight again) and,
// under it, the slot's name and settings (link, key-held, one-shot, HOLD Off / Last / Next, TRIG SYNC, POS SYNC).
// Everything is written back into the slot's LXGB container (FL's own state layout).

    // the envelope shown and edited: 0 time travel, 1 volume (FL: the grid clicked last, or the Envelope selector)
    int gbEditKind = 0;
    // FL's Snap selector: at creation the editor's snap flag × 3 = "1/8 beat" (FUN_0083fcf0 → FUN_0084b8f0)
    int gbSnap = 3;
    int gbDragPoint = -1, gbDragTension = -1, gbHoverPoint = -1, gbHoverHandle = -1;
    int gbHoverCell = -1; // the slot under the mouse in a grid (kind × 36 + slot)
    bool gbDragging = false;
    std::vector<grossbeat::EditPoint> gbDragPoints;
    float gbTensionStart = 0.0f, gbTensionSign = 1.0f;
    float gbLastTension = 0.0f; // FL gives a new point the tension used last (editor field 0x15dc)
    juce::ModifierKeys gbDownMods;
    mutable std::vector<std::uint8_t> gbCacheBytes;
    mutable grossbeat::State gbCache;
    mutable bool gbCacheValid = false;
    static inline std::optional<grossbeat::Slot> gbClipboard;

    static bool isGbItem (const LayoutItem& item, const char* id) { return item.type == Item::display && juce::String (item.display) == id; }

    const grossbeat::State& gbState() const
    {
        static const std::vector<std::uint8_t> none;
        const auto* fx = current();
        const auto& bytes = fx != nullptr ? fx->state : none;
        if (! gbCacheValid || bytes != gbCacheBytes)
        {
            gbCacheBytes = bytes;
            gbCache = grossbeat::editableState (bytes);
            gbCacheValid = true;
        }
        return gbCache;
    }

    int gbSlotOf (int kindIndex) const { return grossbeat::rawFromParam (kindIndex, param01 (kindIndex == 0 ? "Time slot" : "Volume slot")); }
    int gbSlotIndex() const { return gbEditKind * grossbeat::slotsPerKind + gbSlotOf (gbEditKind); }

    grossbeat::Slot gbSlot() const
    {
        auto slot = gbState().slots[(std::size_t) gbSlotIndex()];
        if (gbDragging)
            grossbeat::setPoints (slot, gbDragPoints); // shown at once while the edit travels to the engine
        return slot;
    }

    void gbWriteState (const grossbeat::State& state)
    {
        const auto* fx = current();
        if (fx == nullptr)
            return;
        if (usesBar())
            bar.willEdit();
        editSlot ([bytes = grossbeat::withState (fx->state, state)] (EffectSlot& target) { target.state = bytes; });
        repaint();
    }

    void gbWriteSlot (const grossbeat::Slot& slot)
    {
        auto state = gbState();
        state.slots[(std::size_t) gbSlotIndex()] = slot;
        gbWriteState (state);
    }

    void gbWritePoints (const std::vector<grossbeat::EditPoint>& points)
    {
        auto slot = gbState().slots[(std::size_t) gbSlotIndex()];
        grossbeat::setPoints (slot, points);
        gbWriteSlot (slot);
    }

    // --- geometry ----------------------------------------------------------------------------------------

    juce::Rectangle<float> gbArea (const LayoutItem& item) const { return rectOf (item).reduced (8.0f * scale()); }

    static juce::Point<float> gbPos (juce::Rectangle<float> a, double x, float y)
    {
        return { a.getX() + a.getWidth() * (float) (x / grossbeat::barBeats), a.getBottom() - a.getHeight() * juce::jlimit (0.0f, 1.0f, y) };
    }

    // beats and level under the mouse
    std::pair<double, float> gbMouseValue (const LayoutItem& item, juce::Point<float> p) const
    {
        const auto a = gbArea (item);
        return { juce::jlimit (0.0, grossbeat::barBeats, (double) ((p.x - a.getX()) / juce::jmax (1.0f, a.getWidth())) * grossbeat::barBeats),
                 juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / juce::jmax (1.0f, a.getHeight())) };
    }

    // FL's snap (FUN_00816a60): on the selected grid, Alt inverts the snap
    double gbSnapX (double x, const juce::ModifierKeys& mods) const
    {
        const auto step = grossbeat::snapStep (gbSnap);
        const auto on = (step > 0.0) != mods.isAltDown();
        const auto use = step > 0.0 ? step : grossbeat::snapStep (3);
        return on ? juce::jlimit (0.0, grossbeat::barBeats, std::round (x / use) * use) : x;
    }

    int gbPointAt (const LayoutItem& item, juce::Point<float> p) const
    {
        const auto pts = grossbeat::editPoints (gbSlot());
        const auto a = gbArea (item);
        int best = -1;
        auto bestD = 7.0f * scale();
        for (std::size_t i = 0; i < pts.size(); ++i)
            if (const auto d = gbPos (a, pts[i].x, pts[i].y).getDistanceFrom (p); d <= bestD)
                best = (int) i, bestD = d;
        return best;
    }

    // the tension handle of the segment that ends at point i: none on a hold segment or a very short one
    juce::Point<float> gbHandlePos (const LayoutItem& item, const grossbeat::Slot& slot, const std::vector<grossbeat::EditPoint>& pts, int i) const
    {
        if (i < 1 || i >= (int) pts.size() || pts[(std::size_t) i].type == 2)
            return { -9999.0f, -9999.0f };
        const auto a = gbArea (item);
        const auto x0 = pts[(std::size_t) i - 1].x, x1 = pts[(std::size_t) i].x;
        if ((x1 - x0) / grossbeat::barBeats * a.getWidth() < 14.0f * scale())
            return { -9999.0f, -9999.0f };
        const auto xm = 0.5 * (x0 + x1);
        return gbPos (a, xm, grossbeat::levelAt (slot, xm));
    }

    int gbHandleAt (const LayoutItem& item, juce::Point<float> p) const
    {
        const auto slot = gbSlot();
        const auto pts = grossbeat::editPoints (slot);
        for (int i = 1; i < (int) pts.size(); ++i)
            if (gbHandlePos (item, slot, pts, i).getDistanceFrom (p) <= 6.0f * scale())
                return i;
        return -1;
    }

    juce::String gbLevelText (float y) const
    {
        if (gbEditKind == 1)
            return juce::String (juce::roundToInt (y * 100.0f)) + "%";
        // time travel: how far back the buffer is read (level 1 = now, 0 = two bars back)
        const auto beats = (1.0 - (double) y) * 8.0;
        return tr ("{0} beats back", { juce::String (beats, 2) });
    }

    void gbShowTip (const LayoutItem& item, double x, float y)
    {
        ui::showValueTip (*this, rectOf (item).toNearestInt(), tr ("Beat {0}", { juce::String (x + 1.0, 2) }) + "  " + gbLevelText (y));
    }

    // --- mouse on the envelope ----------------------------------------------------------------------------

    bool gbEnvelopeDown (const juce::MouseEvent& e, const LayoutItem& item)
    {
        auto points = grossbeat::editPoints (gbState().slots[(std::size_t) gbSlotIndex()]);
        const auto point = gbPointAt (item, e.position);
        const auto handle = point < 0 ? gbHandleAt (item, e.position) : -1;
        gbDownMods = e.mods;
        if (e.mods.isPopupMenu())
        {
            if (point >= 0 && e.mods.isAltDown()) // FL: Alt+right-click deletes the point
            {
                gbDeletePoint (point);
                return false;
            }
            if (point >= 0)
            {
                gbPointMenu (point);
                return false;
            }
            if (handle >= 0) // FL: "default tension"
            {
                points[(std::size_t) handle].tension = 0.0f;
                gbLastTension = 0.0f;
                gbWritePoints (points);
                return false;
            }
            // FL: "add point" — a hold point (Gross Beat sets the editor's point type to 2) at the mouse, dragged while held
            auto [x, y] = gbMouseValue (item, e.position);
            const auto slot = gbState().slots[(std::size_t) gbSlotIndex()];
            const auto snapped = gbSnapX (x, e.mods);
            const auto snapping = snapped != x || ((grossbeat::snapStep (gbSnap) > 0.0) != e.mods.isAltDown());
            if (! snapping) // without snap the new point sticks to the curve within 8 pixels
            {
                const auto onCurve = grossbeat::levelAt (slot, x);
                if (std::abs (gbPos (gbArea (item), x, onCurve).y - e.position.y) < 8.0f * scale())
                    y = onCurve;
            }
            x = snapped;
            if (e.mods.isShiftDown()) // Shift: on the curve
                y = grossbeat::levelAt (slot, x);
            gbDragPoint = grossbeat::addPoint (points, x, y, 2, gbLastTension);
            gbDragPoints = points;
            gbDragging = true;
            gbWritePoints (points);
            gbShowTip (item, x, y);
            return true;
        }
        if (handle >= 0 && e.mods.isAltDown()) // FL: Alt+click on a handle = default tension
        {
            points[(std::size_t) handle].tension = 0.0f;
            gbLastTension = 0.0f;
            gbWritePoints (points);
            return false;
        }
        if (point >= 0)
        {
            gbDragPoint = point;
            gbDragPoints = points;
            gbDragging = true;
            gbShowTip (item, points[(std::size_t) point].x, points[(std::size_t) point].y);
            return true;
        }
        if (handle >= 0)
        {
            gbDragTension = handle;
            gbTensionStart = points[(std::size_t) handle].tension;
            // which way a positive change bends the curve at the handle: up or down
            auto bent = points;
            bent[(std::size_t) handle].tension = juce::jlimit (-1.0f, 1.0f, gbTensionStart + (gbTensionStart > 0.8f ? -0.1f : 0.1f));
            grossbeat::Slot a = gbState().slots[(std::size_t) gbSlotIndex()], b = a;
            grossbeat::setPoints (b, bent);
            const auto xm = 0.5 * (points[(std::size_t) handle - 1].x + points[(std::size_t) handle].x);
            const auto up = (grossbeat::levelAt (b, xm) - grossbeat::levelAt (a, xm)) * (bent[(std::size_t) handle].tension - gbTensionStart);
            gbTensionSign = up < 0.0f ? -1.0f : 1.0f;
            gbDragPoints = points;
            gbDragging = true;
            return true;
        }
        return false;
    }

    void gbEnvelopeDrag (const juce::MouseEvent& e, const LayoutItem& item)
    {
        if (! gbDragging)
            return;
        auto points = gbDragPoints;
        if (gbDragTension >= 0 && gbDragTension < (int) points.size())
        {
            const auto dy = -(float) e.getDistanceFromDragStartY() / ((e.mods.isCtrlDown() ? 600.0f : 150.0f) * scale());
            auto& t = points[(std::size_t) gbDragTension].tension;
            t = juce::jlimit (-1.0f, 1.0f, gbTensionStart + gbTensionSign * dy);
            gbLastTension = t;
            ui::showValueTip (*this, rectOf (item).toNearestInt(), tr ("Tension") + " " + juce::String (juce::roundToInt (t * 100.0f)) + "%");
        }
        else if (gbDragPoint >= 0 && gbDragPoint < (int) points.size())
        {
            auto [x, y] = gbMouseValue (item, e.position);
            x = gbSnapX (x, e.mods);
            grossbeat::movePoint (points, gbDragPoint, x, y);
            gbShowTip (item, points[(std::size_t) gbDragPoint].x, points[(std::size_t) gbDragPoint].y);
        }
        else
            return;
        gbDragPoints = points;
        gbWritePoints (points);
    }

    void gbEnvelopeUp()
    {
        gbDragging = false;
        gbDragPoint = gbDragTension = -1;
        ui::hideValueTip();
        repaint();
    }

    void gbDeletePoint (int point)
    {
        auto points = grossbeat::editPoints (gbState().slots[(std::size_t) gbSlotIndex()]);
        if (point == 0)
        {
            ui::postHint ({ tr ("The first point of the envelope can't be removed."), {} }, 2500);
            return;
        }
        if (grossbeat::removePoint (points, point))
            gbWritePoints (points);
        else
            ui::postHint ({ tr ("The envelope must keep its first and last point."), {} }, 2500);
    }

    void gbPointMenu (int point)
    {
        const auto points = grossbeat::editPoints (gbState().slots[(std::size_t) gbSlotIndex()]);
        if (point < 0 || point >= (int) points.size())
            return;
        const auto& p = points[(std::size_t) point];
        juce::PopupMenu menu;
        menu.addSectionHeader (tr ("Point {0}/{1}", { juce::String (point + 1), juce::String ((int) points.size()) }) + ": " + gbLevelText (p.y));
        menu.addItem (tr ("Delete"), point > 0 && point + 1 < (int) points.size(), false, [this, point] { gbDeletePoint (point); });
        if (point > 0) // the shape of the segment that ends here (FL's point menu, Gross Beat's DFM order; Smooth: round 190ee)
        {
            menu.addSectionHeader (tr ("Mode"));
            // Gross Beat's own PointMenu (DFM of TSynthEditorForm's MEnvEditor): Hold first, then Smooth
            static const int order[] { 2, 10, 0, 7, 11, 1, 8, 12, 9, 3, 4, 5, 6 };
            for (const auto mode : order)
                menu.addItem (tr (flcurve::modeName (mode)), true, p.type == mode, [this, point, mode] {
                        auto pts = grossbeat::editPoints (gbState().slots[(std::size_t) gbSlotIndex()]);
                        if (point < (int) pts.size())
                        {
                            pts[(std::size_t) point].type = (std::uint8_t) mode;
                            gbWritePoints (pts);
                        }
                    });
        }
        menu.addSeparator();
        menu.addItem (tr ("Type in value..."), [this, point] { gbTypeValue (point); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void gbTypeValue (int point)
    {
        const auto points = grossbeat::editPoints (gbState().slots[(std::size_t) gbSlotIndex()]);
        if (point < 0 || point >= (int) points.size())
            return;
        auto* window = new juce::AlertWindow (tr ("Point value"), tr ("Percent (0 to 100)"), juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("value", juce::String (points[(std::size_t) point].y * 100.0f, 1));
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<NativeEffectEditor> self (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([self, window, point] (int result) {
                                     if (result != 1 || self == nullptr)
                                         return;
                                     auto pts = grossbeat::editPoints (self->gbState().slots[(std::size_t) self->gbSlotIndex()]);
                                     if (point < (int) pts.size())
                                     {
                                         pts[(std::size_t) point].y = juce::jlimit (0.0f, 1.0f, window->getTextEditorContents ("value").getFloatValue() / 100.0f);
                                         self->gbWritePoints (pts);
                                     }
                                 }),
                                 true);
    }

    // --- the tool column: Options, Snap, Envelope (time travel / volume) -------------------------------------

    // parts of the column in design units from its top: 0 = Options, 1…7 = snap cells, 8 / 9 = time / volume
    juce::Rectangle<float> gbToolCell (const LayoutItem& item, int part) const
    {
        const auto r = rectOf (item);
        const auto s = scale();
        if (part == 0)
            return { r.getX(), r.getY(), r.getWidth(), 27.0f * s };
        if (part <= 7)
            return { r.getX(), r.getY() + (36.0f + 27.0f * (float) (part - 1)) * s, r.getWidth(), 26.0f * s };
        return { r.getX(), r.getBottom() - (part == 8 ? 54.0f : 27.0f) * s, r.getWidth(), 26.0f * s };
    }

    int gbToolAt (const LayoutItem& item, juce::Point<float> p) const
    {
        for (int part = 0; part <= 9; ++part)
            if (gbToolCell (item, part).contains (p))
                return part;
        return -1;
    }

    static const char* gbSnapName (int i)
    {
        static const char* const names[7] { TR_NOOP ("off"), "1/16 beat", "1/12 beat", "1/8 beat", "1/6 beat", "1/4 beat", "1/3 beat" };
        return names[juce::jlimit (0, 6, i)];
    }

    void gbToolDown (const LayoutItem& item, juce::Point<float> p)
    {
        const auto part = gbToolAt (item, p);
        if (part == 0)
            gbOptionsMenu (gbToolCell (item, 0));
        else if (part >= 1 && part <= 7)
            gbSnap = part - 1;
        else if (part == 8 || part == 9)
            gbEditKind = part - 8;
        repaint();
    }

    void gbOptionsMenu (juce::Rectangle<float> at)
    {
        juce::PopupMenu menu;
        const auto slotName = juce::String::fromUTF8 (gbState().slots[(std::size_t) gbSlotIndex()].name.c_str());
        menu.addSectionHeader ((gbEditKind == 0 ? tr ("Time slot {0}", { juce::String (gbSlotOf (0) + 1) }) : tr ("Volume slot {0}", { juce::String (gbSlotOf (1) + 1) }))
                               + (slotName.isEmpty() ? juce::String() : " - " + slotName));
        menu.addItem (tr ("Copy"), [this] { gbClipboard = gbState().slots[(std::size_t) gbSlotIndex()]; });
        menu.addItem (tr ("Paste"), gbClipboard.has_value(), false, [this] {
            if (gbClipboard)
                gbWriteSlot (*gbClipboard);
        });
        menu.addItem (tr ("Flip vertically"), [this] {
            auto pts = grossbeat::editPoints (gbState().slots[(std::size_t) gbSlotIndex()]);
            for (auto& p : pts)
                p.y = 1.0f - p.y, p.tension = -p.tension;
            gbWritePoints (pts);
        });
        menu.addItem (tr ("Reset"), [this] { // an empty slot: a flat bar at full level, unnamed
            auto slot = gbState().slots[(std::size_t) gbSlotIndex()];
            slot.name.clear();
            grossbeat::setPoints (slot, { { 0.0, 1.0f, 0.0f, 2 }, { grossbeat::barBeats, 1.0f, 0.0f, 2 } });
            gbWriteSlot (slot);
        });
        menu.addSeparator();
        menu.addItem (tr ("Rename..."), [this] { gbRename(); });
        menu.addSubMenu (tr ("Factory slots"), gbFactorySlots());
        menu.addItem (tr ("Load LYNX SHARE's factory slots (keeps the selected slots)"), [this] { gbWriteState (gbWithFactory()); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (at.toNearestInt())));
    }

    grossbeat::State gbWithFactory() const
    {
        auto state = grossbeat::factoryState();
        const auto& now = gbState();
        state.params = now.params;
        state.options = now.options;
        return state;
    }

    void gbRename()
    {
        auto* window = new juce::AlertWindow (tr ("Rename"), tr ("Name of the slot:"), juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", juce::String::fromUTF8 (gbState().slots[(std::size_t) gbSlotIndex()].name.c_str()));
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<NativeEffectEditor> self (this);
        const auto index = gbSlotIndex();
        window->enterModalState (true, juce::ModalCallbackFunction::create ([self, window, index] (int result) {
                                     if (result != 1 || self == nullptr)
                                         return;
                                     auto state = self->gbState();
                                     state.slots[(std::size_t) index].name = window->getTextEditorContents ("name").trim().substring (0, 60).toStdString();
                                     self->gbWriteState (state);
                                 }),
                                 true);
    }

    // --- the slot bar under the envelope (FL's NameLabel, Link / Key-held / One-shot, HOLD, TRIG SYNC, POS SYNC) ---

    // FL's x positions relative to the editor (298…824): name 0–157, link 159, key-held 182, one-shot 205, hold 232–308,
    // trigger sync 312–416, position sync 420–526
    juce::Rectangle<float> gbBarCell (const LayoutItem& item, int part) const
    {
        static constexpr float x0[7] { 0, 159, 182, 205, 232, 312, 420 }, x1[7] { 157, 181, 204, 227, 308, 416, 526 };
        const auto r = rectOf (item);
        const auto k = r.getWidth() / 526.0f;
        return { r.getX() + x0[part] * k, r.getY(), (x1[part] - x0[part]) * k, r.getHeight() };
    }

    int gbBarAt (const LayoutItem& item, juce::Point<float> p) const
    {
        for (int part = 0; part < 7; ++part)
            if (gbBarCell (item, part).contains (p))
                return part;
        return -1;
    }

    static juce::String gbSyncText (int v)
    {
        static const char* const names[6] { TR_NOOP ("Off"), TR_NOOP ("1/4 beat"), TR_NOOP ("1/2 beat"), TR_NOOP ("1 beat"), TR_NOOP ("2 beats"), TR_NOOP ("4 beats") };
        return tr (names[juce::jlimit (0, 5, v)]);
    }

    static juce::String gbHoldText (int v)
    {
        static const char* const names[3] { TR_NOOP ("Off"), TR_NOOP ("Last"), TR_NOOP ("Next") };
        return tr (names[juce::jlimit (0, 2, v)]);
    }

    void gbBarDown (const LayoutItem& item, juce::Point<float> p)
    {
        const auto part = gbBarAt (item, p);
        auto slot = gbState().slots[(std::size_t) gbSlotIndex()];
        switch (part)
        {
            case 0: gbRename(); return;
            case 1: slot.link = ! slot.link; break;
            case 2: slot.keyHeld = ! slot.keyHeld; break;
            case 3: slot.oneShot = ! slot.oneShot; break;
            case 4:
            case 5:
            case 6:
            {
                juce::PopupMenu menu;
                const auto count = part == 4 ? 3 : 6;
                const auto now = part == 4 ? slot.hold : part == 5 ? slot.triggerSync : slot.positionSync;
                for (int v = 0; v < count; ++v)
                    menu.addItem (part == 4 ? gbHoldText (v) : gbSyncText (v), true, v == now, [this, part, v] {
                        auto s = gbState().slots[(std::size_t) gbSlotIndex()];
                        (part == 4 ? s.hold : part == 5 ? s.triggerSync : s.positionSync) = v;
                        gbWriteSlot (s);
                    });
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (gbBarCell (item, part).toNearestInt())));
                return;
            }
            default: return;
        }
        gbWriteSlot (slot);
    }

    // --- the slot grids (FL's two 4 × 9 grids) and the tab bar over the envelope (round 168m) --------------------

    static bool isGbAny (const LayoutItem& item)
    {
        return item.type == Item::display && item.display != nullptr && ((std::strncmp (item.display, "gb", 2) == 0 && std::strcmp (item.display, "gbjog") != 0) || std::strcmp (item.display, "grossbeat") == 0);
    }

    static int gbGridKind (const LayoutItem& item) { return isGbItem (item, "gbtimegrid") ? 0 : isGbItem (item, "gbvolumegrid") ? 1 : -1; }

    juce::Rectangle<float> gbGridCell (const LayoutItem& item, int slot) const
    {
        const auto r = rectOf (item);
        const auto w = r.getWidth() / 4.0f, h = r.getHeight() / 9.0f;
        return { r.getX() + w * (float) (slot % 4), r.getY() + h * (float) (slot / 4), w, h };
    }

    int gbGridAt (const LayoutItem& item, juce::Point<float> p) const
    {
        const auto r = rectOf (item);
        if (! r.contains (p))
            return -1;
        const auto col = juce::jlimit (0, 3, (int) ((p.x - r.getX()) / (r.getWidth() / 4.0f)));
        const auto row = juce::jlimit (0, 8, (int) ((p.y - r.getY()) / (r.getHeight() / 9.0f)));
        return row * 4 + col;
    }

    // FL: clicking a slot selects it for its kind and shows its envelope
    void gbSelect (int kindIndex, int slot)
    {
        gbEditKind = kindIndex;
        const auto param = plugui::paramIndex (kind, kindIndex == 0 ? "Time slot" : "Volume slot");
        if (param >= 0 && gbSlotOf (kindIndex) != slot)
            setParams ({ { param, grossbeat::paramFromRaw (kindIndex, slot) } });
        repaint();
    }

    juce::String gbSlotTitle (int kindIndex, int slot) const
    {
        const auto& name = gbState().slots[(std::size_t) (kindIndex * grossbeat::slotsPerKind + slot)].name;
        return name.empty() ? tr ("Slot {0}", { juce::String (slot + 1) }) : juce::String::fromUTF8 (name.c_str());
    }

    // the factory slot browser: LYNX SHARE's slots of the shown kind; the one picked replaces the selected slot
    juce::PopupMenu gbFactorySlots()
    {
        static const auto factory = grossbeat::factoryState();
        juce::PopupMenu menu;
        for (int i = 0; i < grossbeat::slotsPerKind; ++i)
            if (const auto& slot = factory.slots[(std::size_t) (gbEditKind * grossbeat::slotsPerKind + i)]; ! slot.name.empty())
                menu.addItem (juce::String::fromUTF8 (slot.name.c_str()), [this, slot] {
                    auto now = gbState().slots[(std::size_t) gbSlotIndex()];
                    now.name = slot.name;
                    now.points = slot.points;
                    gbWriteSlot (now);
                });
        return menu;
    }

    void gbFactoryMenu (juce::Rectangle<float> at)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (gbEditKind == 0 ? tr ("Time slot {0}", { juce::String (gbSlotOf (0) + 1) }) : tr ("Volume slot {0}", { juce::String (gbSlotOf (1) + 1) }));
        const auto slots = gbFactorySlots();
        for (juce::PopupMenu::MenuItemIterator it (slots); it.next();)
            menu.addItem (it.getItem());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (at.toNearestInt())));
    }

    // tab bar parts: 0 Time tab, 1 Volume tab, 2 the factory slot browser
    juce::Rectangle<float> gbTabCell (const LayoutItem& item, int part) const
    {
        const auto r = rectOf (item);
        const auto s = scale();
        if (part == 2)
            return r.withLeft (r.getRight() - 150.0f * s);
        return { r.getX() + (float) part * 92.0f * s, r.getY(), 90.0f * s, r.getHeight() };
    }

    int gbTabAt (const LayoutItem& item, juce::Point<float> p) const
    {
        for (int part = 0; part < 3; ++part)
            if (gbTabCell (item, part).contains (p))
                return part;
        return -1;
    }

    // --- the editor's mouse entry points (called by mouseDown / mouseDrag / mouseUp / mouseMove) -----------------

    // true = handled (a drag may follow when gbDragging is set)
    bool gbMouseDown (const juce::MouseEvent& e, const LayoutItem& item, int index)
    {
        if (item.type == Item::radio && item.param != nullptr) // FL: a click in a slot grid shows that slot's envelope
        {
            if (std::strcmp (item.param, "Time slot") == 0)
                gbEditKind = 0;
            else if (std::strcmp (item.param, "Volume slot") == 0)
                gbEditKind = 1;
            return false;
        }
        if (isGbItem (item, "grossbeat"))
        {
            if (gbEnvelopeDown (e, item))
                dragItem = index;
            return true;
        }
        if (isGbItem (item, "gbtools"))
        {
            gbToolDown (item, e.position);
            return true;
        }
        if (isGbItem (item, "gbopts"))
        {
            gbPluginOptionsMenu (rectOf (item));
            return true;
        }
        if (isGbItem (item, "gbslot"))
        {
            gbBarDown (item, e.position);
            return true;
        }
        if (const auto k = gbGridKind (item); k >= 0)
        {
            if (const auto slot = gbGridAt (item, e.position); slot >= 0)
            {
                gbSelect (k, slot);
                if (e.mods.isPopupMenu()) // right-click: the slot's menu (copy, paste, factory slots…)
                    gbOptionsMenu (gbGridCell (item, slot));
            }
            return true;
        }
        if (isGbItem (item, "gbtabs"))
        {
            const auto part = gbTabAt (item, e.position);
            if (part == 0 || part == 1)
                gbEditKind = part, repaint();
            else if (part == 2)
                gbFactoryMenu (gbTabCell (item, 2));
            return true;
        }
        return false;
    }

    bool gbMouseMove (const LayoutItem& item, juce::Point<float> p)
    {
        const auto k = gbGridKind (item);
        const auto hoverCell = k >= 0 ? (gbGridAt (item, p) >= 0 ? k * grossbeat::slotsPerKind + gbGridAt (item, p) : -1) : -1;
        if (hoverCell != gbHoverCell)
        {
            gbHoverCell = hoverCell;
            repaint();
        }
        if (k >= 0 || isGbItem (item, "gbtabs"))
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            return true;
        }
        if (! isGbItem (item, "grossbeat"))
        {
            if (gbHoverPoint >= 0 || gbHoverHandle >= 0)
            {
                gbHoverPoint = gbHoverHandle = -1;
                repaint();
            }
            return false;
        }
        const auto point = gbPointAt (item, p), handle = point < 0 ? gbHandleAt (item, p) : -1;
        if (point != gbHoverPoint || handle != gbHoverHandle)
        {
            gbHoverPoint = point;
            gbHoverHandle = handle;
            repaint();
        }
        setMouseCursor (point >= 0 ? juce::MouseCursor::DraggingHandCursor
                                   : handle >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::CrosshairCursor);
        return true;
    }

    // FL's Options button over the slot grids (DFM OptionsBtn, SynthResModule.OptionsMenu): round 190ee builds its MIDI
    // item, "Trigger slots from MIDI notes" (FL's first item), and the wrapper's MIDI input port beside it (FL keeps
    // the port in the plugin wrapper's settings). FL's other items (click reduction, attack compensation, DC offset,
    // HQ resampling, scratching clock, About) are not in this menu yet.
    void gbPluginOptionsMenu (juce::Rectangle<float> at)
    {
        const auto* fx = current();
        if (fx == nullptr)
            return;
        const auto on = gbState().midiTrigger;
        juce::PopupMenu menu;
        menu.addItem (tr ("Trigger slots from MIDI notes"), true, on, [this, on] {
            auto st = gbState();
            st.midiTrigger = ! on;
            gbWriteState (st);
        });
        menu.addSubMenu (tr ("MIDI input port"), midiPortMenu());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (at.toNearestInt())));
    }

    juce::String gbHint (const LayoutItem& item, juce::Point<float> p) const
    {
        if (isGbItem (item, "gbopts"))
            return tr ("Options: trigger slots from MIDI notes (time slots: keys C5 to B7, volume slots: C2 to B4), MIDI input port");
        if (isGbItem (item, "gbtools"))
        {
            const auto part = gbToolAt (item, p);
            if (part == 0)
                return tr ("Options: copy, paste, flip, reset or rename the slot; LYNX SHARE's factory slots");
            if (part >= 1 && part <= 7)
                return tr ("Snap: {0} (Alt while editing: the other way)", { tr (gbSnapName (part - 1)) });
            if (part == 8 || part == 9)
                return tr ("Envelope: {0}", { part == 8 ? tr ("time travel") : tr ("volume") });
            return {};
        }
        if (isGbItem (item, "gbslot"))
        {
            switch (gbBarAt (item, p))
            {
                case 0: return tr ("Slot name: click to rename");
                case 1: return tr ("Link volume slot");
                case 2: return tr ("Key-held (back to previous slot on key release)");
                case 3: return tr ("One-shot (back to first slot once done)");
                case 4: return tr ("Force hold delay (bar repeat)");
                case 5: return tr ("Slot trigger synchronization");
                case 6: return tr ("Slot position synchronization");
                default: return {};
            }
        }
        if (const auto k = gbGridKind (item); k >= 0)
        {
            const auto slot = gbGridAt (item, p);
            return slot < 0 ? juce::String() : (k == 0 ? tr ("Time slot {0}", { juce::String (slot + 1) }) : tr ("Volume slot {0}", { juce::String (slot + 1) }))
                                                   + ": " + gbSlotTitle (k, slot) + "  " + tr ("[LMB] select  [RMB] copy, paste, factory slots, rename");
        }
        if (isGbItem (item, "gbtabs"))
        {
            const auto part = gbTabAt (item, p);
            return part == 2 ? tr ("Factory slots: put one of LYNX SHARE's slots in the selected slot")
                             : tr ("Envelope: {0}", { gbEditKind == 0 ? tr ("time travel") : tr ("volume") });
        }
        if (gbPointAt (item, p) >= 0)
            return tr ("Point: [LMB] drag moves it ([Alt] no snap)  [RMB] shape, value, delete  [Alt][RMB] delete");
        if (gbHandleAt (item, p) >= 0)
            return tr ("Tension: [LMB] drag up / down bends the segment ([Ctrl] fine)  [RMB] straight");
        return gbEditKind == 0 ? tr ("Time envelope: [RMB] add a point (hold and drag to place it)  [LMB] drag a point; top = now, bottom = two bars back")
                               : tr ("Volume envelope: [RMB] add a point (hold and drag to place it)  [LMB] drag a point; top = full level");
    }

    // --- painting -----------------------------------------------------------------------------------------

    void paintGrossBeat (juce::Graphics& g, const juce::String& id, juce::Rectangle<float> r, const EffectSlot& fx, const LayoutItem& item)
    {
        using namespace theme;
        const auto s = scale();
        const auto timeColour = fx.enabled ? accent : colour::textDisabled;
        const auto volumeColour = colour::info.withAlpha (fx.enabled ? 1.0f : 0.4f);
        const auto kindColour = gbEditKind == 0 ? timeColour : volumeColour;

        if (id == "gbopts") // FL's small Options button (a menu glyph)
        {
            const auto b = r.reduced (1.0f * s);
            g.setColour (colour::bg3);
            g.fillRoundedRectangle (b, 3.0f * s);
            g.setColour (fx.enabled ? colour::text : colour::textDisabled);
            for (int i = 0; i < 3; ++i)
                g.fillRect (juce::Rectangle<float> (b.getWidth() * 0.5f, 1.6f * s).withCentre ({ b.getCentreX(), b.getCentreY() + (float) (i - 1) * 4.0f * s }));
            return;
        }
        if (id == "gbjog") // a dial with the beat positions (FL's scratch indicator)
        {
            const auto c = r.getCentre();
            const auto radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.4f;
            g.setColour (colour::bg3);
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c));
            for (int i = 0; i < 12; ++i)
            {
                const auto a = juce::MathConstants<float>::twoPi * (float) i / 12.0f;
                g.setColour (i == 0 ? (fx.enabled ? accent : colour::textSecondary) : colour::textDisabled);
                g.fillEllipse (juce::Rectangle<float> (4.0f * s, 4.0f * s).withCentre ({ c.x + std::sin (a) * radius * 0.85f, c.y - std::cos (a) * radius * 0.85f }));
            }
            return;
        }

        if (id == "gbtools")
        {
            const auto cell = [&] (int part, bool lit, const juce::String& text) {
                const auto c = gbToolCell (item, part).reduced (1.0f * s);
                g.setColour (lit ? kindColour.withAlpha (0.35f) : colour::bg2);
                g.fillRoundedRectangle (c, radius::sm * s);
                g.setColour (lit ? colour::textPrimary : colour::textSecondary);
                g.setFont (font (juce::jmax (7.5f, 9.0f * s), lit));
                g.drawFittedText (text, c.toNearestInt(), juce::Justification::centred, 1, 0.6f);
            };
            cell (0, false, juce::String::fromUTF8 ("\xe2\x89\xa1")); // ≡
            static const char* const snapShort[7] { "off", "16", "12", "8", "6", "4", "3" };
            for (int i = 0; i < 7; ++i)
                cell (i + 1, gbSnap == i, i == 0 ? tr ("off") : juce::String (snapShort[i]));
            cell (8, gbEditKind == 0, "T");
            cell (9, gbEditKind == 1, "V");
            return;
        }

        if (const auto k = gbGridKind (item); k >= 0) // the slot grids: each slot's name over its envelope
        {
            const auto colour = k == 0 ? timeColour : volumeColour;
            const auto selected = gbSlotOf (k);
            g.setColour (colour::bg0);
            g.fillRoundedRectangle (r, radius::sm * s);
            for (int i = 0; i < grossbeat::slotsPerKind; ++i)
            {
                const auto cell = gbGridCell (item, i).reduced (1.0f * s);
                const auto& slot = gbState().slots[(std::size_t) (k * grossbeat::slotsPerKind + i)];
                const auto isSel = i == selected, hot = gbHoverCell == k * grossbeat::slotsPerKind + i;
                g.setColour (isSel ? colour.withAlpha (0.22f) : hot ? colour::bg3 : (i / 4 + i) % 2 == 0 ? colour::bg1 : colour::bg2);
                g.fillRoundedRectangle (cell, radius::sm * s);
                const auto levels = grossbeat::renderSlot (slot, juce::jmax (8, (int) cell.getWidth()));
                const auto flat = std::all_of (levels.begin(), levels.end(), [&] (float v) { return std::abs (v - levels.front()) < 1.0e-4f; });
                if (! flat) // a thumbnail of the envelope, filled
                {
                    const auto a = cell.reduced (2.0f * s, 2.0f * s);
                    juce::Path line, area;
                    for (std::size_t j = 0; j < levels.size(); ++j)
                    {
                        const juce::Point<float> pt { a.getX() + a.getWidth() * (float) j / (float) (levels.size() - 1), a.getBottom() - a.getHeight() * levels[j] };
                        j == 0 ? line.startNewSubPath (pt) : line.lineTo (pt);
                    }
                    area = line;
                    area.lineTo (a.getBottomRight());
                    area.lineTo (a.getBottomLeft());
                    area.closeSubPath();
                    g.setColour (colour.withAlpha (isSel ? 0.22f : 0.10f));
                    g.fillPath (area);
                    g.setColour (colour.withAlpha (isSel ? 0.65f : 0.30f));
                    g.strokePath (line, juce::PathStrokeType (1.0f * s));
                }
                const auto named = ! slot.name.empty();
                g.setFont (font (juce::jmax (7.0f, 8.5f * s), isSel));
                if (named && ! flat) // a dark backing keeps the name readable over the thumbnail
                {
                    const auto tw = juce::jmin (cell.getWidth() - 4.0f * s, (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), juce::String::fromUTF8 (slot.name.c_str())) + 6.0f * s);
                    g.setColour (colour::bg0.withAlpha (0.62f));
                    g.fillRoundedRectangle (juce::Rectangle<float> (tw, 11.0f * s).withCentre (cell.getCentre()), 2.0f * s);
                }
                g.setColour (isSel ? colour::textPrimary : named ? colour::textPrimary.withAlpha (0.85f) : colour::textDisabled);
                g.drawFittedText (named ? juce::String::fromUTF8 (slot.name.c_str()) : juce::String (i + 1), cell.reduced (3.0f * s, 0.0f).toNearestInt(),
                                  juce::Justification::centred, 1, 0.65f);
                if (isSel)
                {
                    g.setColour (colour);
                    g.drawRoundedRectangle (cell, radius::sm * s, 1.4f * s);
                }
            }
            return;
        }

        if (id == "gbtabs") // Time / Volume tabs, the selected slot's name, the factory slot browser
        {
            g.setColour (colour::bg1);
            g.fillRoundedRectangle (r, radius::sm * s);
            for (int part = 0; part < 2; ++part)
            {
                const auto c = gbTabCell (item, part).reduced (2.0f * s, 3.0f * s);
                const auto on = gbEditKind == part;
                const auto colour = part == 0 ? timeColour : volumeColour;
                g.setColour (on ? colour.withAlpha (0.25f) : colour::bg2);
                g.fillRoundedRectangle (c, radius::sm * s);
                if (on)
                {
                    g.setColour (colour);
                    g.fillRect (c.withTop (c.getBottom() - 2.0f * s));
                }
                g.setColour (on ? colour::textPrimary : colour::textSecondary);
                g.setFont (font (juce::jmax (8.0f, 10.0f * s), true));
                g.drawText (part == 0 ? tr ("TIME") : tr ("VOLUME"), c, juce::Justification::centred, false);
            }
            const auto browser = gbTabCell (item, 2).reduced (2.0f * s, 3.0f * s);
            const auto title = r.withLeft (gbTabCell (item, 1).getRight() + 8.0f * s).withRight (browser.getX() - 6.0f * s);
            g.setColour (kindColour);
            g.setFont (font (juce::jmax (8.0f, 11.0f * s), true));
            const auto shownSlot = gbSlotOf (gbEditKind);
            const auto& shownName = gbState().slots[(std::size_t) (gbEditKind * grossbeat::slotsPerKind + shownSlot)].name;
            g.drawFittedText (shownName.empty() ? gbSlotTitle (gbEditKind, shownSlot) : juce::String (shownSlot + 1) + "  " + gbSlotTitle (gbEditKind, shownSlot), title.toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.7f);
            g.setColour (colour::bg3);
            g.fillRoundedRectangle (browser, radius::sm * s);
            g.setColour (colour::textPrimary);
            g.setFont (font (juce::jmax (8.0f, 10.0f * s)));
            g.drawText (tr ("Factory slots") + juce::String::fromUTF8 (" \xe2\x96\xbe"), browser, juce::Justification::centred, false);
            return;
        }

        if (id == "gbslot")
        {
            g.setColour (colour::bg1);
            g.fillRoundedRectangle (r, radius::sm * s);
            g.setFont (font (juce::jmax (8.0f, 10.0f * s)));
            const auto shown = gbSlot();
            const auto name = shown.name.empty() ? tr ("Slot {0}", { juce::String (gbSlotOf (gbEditKind) + 1) }) : juce::String::fromUTF8 (shown.name.c_str());
            g.setColour (kindColour);
            g.drawFittedText ((gbEditKind == 0 ? tr ("Time") : tr ("Volume")) + ": " + name, gbBarCell (item, 0).reduced (5.0f * s, 0.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.7f);
            const auto toggle = [&] (int part, bool on, const char* letter) {
                const auto c = gbBarCell (item, part).reduced (1.5f * s, 3.0f * s);
                g.setColour (on ? kindColour.withAlpha (0.45f) : colour::bg3);
                g.fillRoundedRectangle (c, radius::sm * s);
                g.setColour (on ? colour::textPrimary : colour::textSecondary);
                g.drawText (letter, c, juce::Justification::centred, false);
            };
            toggle (1, shown.link, "L");
            toggle (2, shown.keyHeld, "K");
            toggle (3, shown.oneShot, "1");
            const auto combo = [&] (int part, const juce::String& caption, const juce::String& value) {
                const auto c = gbBarCell (item, part).reduced (2.0f * s, 3.0f * s);
                g.setColour (colour::bg2);
                g.fillRoundedRectangle (c, radius::sm * s);
                g.setColour (colour::textSecondary);
                g.drawFittedText (caption + " " + value, c.reduced (4.0f * s, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
            };
            combo (4, tr ("HOLD:"), gbHoldText (shown.hold));
            combo (5, tr ("TRIG SYNC:"), gbSyncText (shown.triggerSync));
            combo (6, tr ("POS SYNC:"), gbSyncText (shown.positionSync));
            return;
        }
        if (id != "grossbeat")
            return;

        // the editor: grid (bars of the snap grid, beats stronger), the curve filled, points and tension handles
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, radius::md * s);
        const auto inner = gbArea (item);
        const auto step = grossbeat::snapStep (gbSnap > 0 ? gbSnap : 3);
        g.setColour (colour::gridBeat.withAlpha (0.45f));
        for (double x = step; x < grossbeat::barBeats - 1.0e-9; x += step)
            g.drawVerticalLine ((int) gbPos (inner, x, 0.0f).x, inner.getY(), inner.getBottom());
        g.setColour (colour::gridBar);
        for (int b = 1; b < 4; ++b)
            g.drawVerticalLine ((int) gbPos (inner, b, 0.0f).x, inner.getY(), inner.getBottom());
        g.setColour (colour::gridBeat);
        for (int i = 1; i < 8; ++i) // time: one line per beat of delay; volume: eighths of the level
            g.drawHorizontalLine ((int) (inner.getY() + inner.getHeight() * (float) i / 8.0f), inner.getX(), inner.getRight());
        g.setColour (colour::gridBar);
        g.drawRect (inner, 1.0f);

        const auto shown = gbSlot();
        const auto n = juce::jmax (16, (int) inner.getWidth());
        const auto levels = grossbeat::renderSlot (shown, n);
        juce::Path curve, fill;
        for (int i = 0; i < n; ++i)
        {
            const auto pt = gbPos (inner, grossbeat::barBeats * (double) i / (double) n, levels[(std::size_t) i]);
            if (i == 0)
                curve.startNewSubPath (pt);
            else
                curve.lineTo (pt);
        }
        curve.lineTo (gbPos (inner, grossbeat::barBeats, levels.back()));
        fill = curve;
        fill.lineTo (inner.getBottomRight());
        fill.lineTo (inner.getBottomLeft());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (kindColour.withAlpha (0.26f), inner.getX(), inner.getY(), kindColour.withAlpha (0.0f), inner.getX(), inner.getBottom(), false));
        g.fillPath (fill);
        g.setColour (kindColour.withAlpha (0.25f)); // a soft glow under the line
        g.strokePath (curve, juce::PathStrokeType (juce::jmax (3.0f, 5.0f * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (kindColour);
        g.strokePath (curve, juce::PathStrokeType (juce::jmax (1.5f, 2.0f * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto pts = grossbeat::editPoints (shown);
        for (int i = 1; i < (int) pts.size(); ++i)
            if (const auto h = gbHandlePos (item, shown, pts, i); h.x > -1000.0f)
            {
                const auto hot = i == gbHoverHandle || i == gbDragTension;
                g.setColour (kindColour.withAlpha (hot ? 1.0f : 0.7f));
                g.drawEllipse (juce::Rectangle<float> ((hot ? 9.0f : 7.0f) * s, (hot ? 9.0f : 7.0f) * s).withCentre (h), 1.2f * s);
            }
        for (int i = 0; i < (int) pts.size(); ++i)
        {
            const auto hot = i == gbHoverPoint || i == gbDragPoint;
            const auto pt = gbPos (inner, pts[(std::size_t) i].x, pts[(std::size_t) i].y);
            g.setColour (hot ? colour::textPrimary : kindColour);
            g.fillEllipse (juce::Rectangle<float> ((hot ? 8.0f : 6.0f) * s, (hot ? 8.0f : 6.0f) * s).withCentre (pt));
        }
        // the scale: beat numbers along the top, the level on the left (time: how far back; volume: percent)
        g.setFont (font (juce::jmax (7.5f, 9.0f * s)));
        g.setColour (colour::textSecondary.withAlpha (0.8f));
        for (int b = 0; b < 4; ++b)
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (gbPos (inner, b, 1.0f).x + 4.0f * s, inner.getY() + 2.0f * s, 20.0f * s, 12.0f * s),
                        juce::Justification::centredLeft, false);
        for (int i = 0; i <= 2; ++i)
        {
            const auto y = 1.0f - 0.5f * (float) i;
            const auto text = gbEditKind == 0 ? (i == 0 ? tr ("now") : i == 1 ? tr ("1 bar back") : tr ("2 bars back"))
                                              : juce::String (juce::roundToInt (y * 100.0f)) + "%";
            const auto at = gbPos (inner, 0.0, y);
            g.drawText (text, juce::Rectangle<float> (at.x + 4.0f * s, juce::jlimit (inner.getY() + 14.0f * s, inner.getBottom() - 13.0f * s, at.y - 6.0f * s), 90.0f * s, 12.0f * s),
                        juce::Justification::centredLeft, false);
        }
        g.setColour (kindColour.withAlpha (0.8f));
        g.setFont (font (juce::jmax (8.0f, 10.0f * s), true));
        g.drawText (gbEditKind == 0 ? tr ("time travel") : tr ("volume"), inner.reduced (6.0f * s).toNearestInt(), juce::Justification::topRight, false);
    }

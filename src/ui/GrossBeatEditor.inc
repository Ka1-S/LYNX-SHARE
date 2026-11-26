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
    // Round 190mf (DP-6, capture 190lx GB4 fl-gb-tools.png): FL's three envelope tool switches under the snap cells —
    // "Step editing (hold shift for pulse mode)", "Slide remaining points", "Freeze editing" (FL's MEnvEditor). All three
    // off by default (capture 190mf A1: Gross Beat's are, unlike Slicex's Slide)
    bool gbStepEdit = false, gbSlide = false, gbFrozen = false;
    // FL's point selection (capture 190mf A1: Ctrl+A / Ctrl+D / Ctrl+I, the dragged point; a selected point = a 9 px ring
    // with a dot); indexes into the slot's points
    std::set<int> gbSelected;
    // round 190mk (DP-7): the points clipboard (Cut / Copy / Paste / Duplicate), the options' switches and the file chooser
    static inline std::optional<grossbeat::PointClip> gbPointClip;
    static inline bool gbFocusOnClick = false, gbBackgroundGradient = false, gbInvertGrid = true; // (FL 2026's defaults)
    bool gbBoxing = false; // FL's Ctrl+left-drag box (the only way to select several points: capture 190mk A1.4)
    juce::Point<float> gbBoxFrom, gbBoxTo;
    double gbLastMouseBeat = 0.0; // where the pointer was over the envelope (a paste with nothing selected goes there)
    std::unique_ptr<juce::FileChooser> gbChooser;
    double gbStepLastX = -1.0; // (step editing: where the drag was last)
    int gbDragPoint = -1, gbDragTension = -1, gbHoverPoint = -1, gbHoverHandle = -1;
    int gbHoverCell = -1; // the slot under the mouse in a grid (kind × 36 + slot)
    bool gbDragging = false;
    // round 190oa (DP-8; FL manual "click on the clock-dial and move your mouse up/down", REA 190oa R5: TScratchWheel,
    // parameter 8, released = back to no offset): the scratch dial's drag
    bool gbScratching = false;
    float gbScratchFromY = 0.0f, gbScratchFrom = 0.0f;
    // capture 190oa C4: 22 px down = "-000:00:033", 42 px = "-000:00:063" (≈ 1.5 ticks of 96 a beat per px): the whole
    // offset (two bars = 768 ticks) in ≈ 512 px
    static constexpr float gbScratchPixels = 512.0f;
    // FL's hint value while held: the offset back in bars:beats:ticks (96 a beat), "-000:00:033"
    juce::String gbScratchText() const
    {
        const auto ticks = juce::roundToInt (gbScratchValue() * 768.0f);
        return "-" + juce::String (ticks / 384).paddedLeft ('0', 3) + ":" + juce::String ((ticks % 384) / 96).paddedLeft ('0', 2) + ":"
               + juce::String (ticks % 96).paddedLeft ('0', 3);
    }
    float gbScratchValue() const { return current() != nullptr ? current()->params[(std::size_t) grossbeat::scratchParam] : 0.0f; }
    void gbScratchDrag (const juce::MouseEvent& e)
    {
        // down = further back in time (FL's raw falls from 65536, LYNX's value rises from 0)
        setValue (grossbeat::scratchParam, juce::jlimit (0.0f, 1.0f, gbScratchFrom + (e.position.y - gbScratchFromY) / (gbScratchPixels * scale())));
        repaintGbDial();
    }
    void gbScratchUp()
    {
        gbScratching = false;
        if (gbScratchValue() != 0.0f) // FL's mouse-up: the dial springs back (the audio glides back through the smoothers)
            setValue (grossbeat::scratchParam, 0.0f);
        repaintGbDial();
    }
    void gbScratchWheel (const juce::MouseWheelDetails& wheel) // FL: ±0x800 of 0x10000 a notch
    {
        const auto up = (wheel.deltaY > 0.0f) != wheel.isReversed;
        setValue (grossbeat::scratchParam, juce::jlimit (0.0f, 1.0f, gbScratchValue() + (up ? -1.0f : 1.0f) / 32.0f));
        repaintGbDial();
    }
    void repaintGbDial()
    {
        for (const auto& item : activeLayout().items)
            if (isGbItem (item, "gbjog"))
                repaint (rectOf (item).getSmallestIntegerContainer().expanded (2));
    }
    std::vector<grossbeat::EditPoint> gbDragPoints;
    float gbTensionStart = 0.0f, gbTensionSign = 1.0f;
    float gbLastTension = 0.0f; // FL gives a new point the tension used last (editor field 0x15dc)
    juce::ModifierKeys gbDownMods;
    mutable std::vector<std::uint8_t> gbCacheBytes;
    mutable grossbeat::State gbCache;
    mutable bool gbCacheValid = false;
    static inline std::optional<grossbeat::Slot> gbClipboard;

    static bool isGbItem (const LayoutItem& item, const char* id) { return item.type == Item::display && juce::String (item.display) == id; }

    // --- live play state (round 190ih, PLAN R2: DP-1, DP-2, DP-3, DC-1, DC-2) ---------------------------------------
    // FL's manual (Envelope Mapping Panel): "the Playback locator - a vertical ... scrolling line, visible only when the
    // host is playing"; (Time Slots and Volume Slots) "Green rectangle = Queued for playback. Green filled = Playing".
    // Captured in FL 2026 (details190if raw/c22, c24, c25): a 1 px line with a short fading trail behind it, and the
    // playing slot of each grid filling from the left as its envelope plays; stopped, neither shows. The colour is
    // LYNX's playhead (owner decision PL1). The engine publishes the state (GrossBeatEffect::controlOutput); the
    // editor's 30 Hz timer reads it and repaints only the locator's old and new columns and the playing cells.
    struct GbLive
    {
        bool playing = false;
        std::array<float, 2> position { 0.0f, 0.0f }; // 0 … 1 of the 4-beat envelope: [0] time, [1] volume
        std::array<int, 2> slot { -1, -1 }, queued { -1, -1 };
        bool operator== (const GbLive& o) const { return playing == o.playing && position == o.position && slot == o.slot && queued == o.queued; }
    };
    GbLive gbLive;

    GbLive gbReadLive() const
    {
        GbLive v;
        if (! engine.getTransport().playing.load (std::memory_order_relaxed) || liveMeter (0) < 0.5f)
            return v; // stopped: nothing drawn (FL: "visible only when the host is playing")
        v.playing = true;
        for (int k = 0; k < 2; ++k)
        {
            v.position[(std::size_t) k] = juce::jlimit (0.0f, 1.0f, liveMeter (1 + k));
            v.slot[(std::size_t) k] = juce::roundToInt (liveMeter (3 + k)) - 1;
            v.queued[(std::size_t) k] = juce::roundToInt (liveMeter (5 + k)) - 1;
        }
        return v;
    }

    // the locator's x in the envelope editor for a state, and the columns it covers with its trail
    float gbLocatorX (const LayoutItem& item, const GbLive& v) const
    {
        return gbPos (gbArea (item), grossbeat::barBeats * (double) v.position[(std::size_t) gbEditKind], 0.0f).x;
    }
    juce::Rectangle<int> gbLocatorBounds (const LayoutItem& item, const GbLive& v) const
    {
        const auto inner = gbArea (item);
        const auto x = gbLocatorX (item, v), trail = gbTrailWidth();
        return juce::Rectangle<float> (x - trail - 2.0f, inner.getY(), trail + 5.0f, inner.getHeight()).getSmallestIntegerContainer();
    }
    float gbTrailWidth() const // a fade: Settings › Animations (FL's colours: up to 3 px, round 190jd)
    {
        return ui::motion::enabled() ? (gblocator::flStyle() ? 3.0f : 6.0f) * scale() : 0.0f;
    }
    // round 190jd: the beat phase the FL-colours locator is drawn at (set before each paint / overlay step)
    double gbPaintPhase = 0.0;
    double gbLastOverlayMs = 0.0;
    double gbPhaseOf (const GbLive& v) const { return gblocator::beatPhase ((double) v.position[(std::size_t) gbEditKind], (int) grossbeat::barBeats); }
    double gbLastFrameMs = 0.0;

    // Rollup 0.1.30 (round 190iy): the locator on round 190ip's GPU overlay (ui/MotionOverlay.h) while the song plays. It
    // then moves at every tick of the editor's 30 Hz timer and the window presents no frame for it (round 190ih measured a
    // fixed cost per frame the window presents, whatever is drawn). The slot fills stay on the frame budget below: they are
    // drawn under each slot's thumbnail and name (a picture over them would cover both) and grow from the cell's left edge
    // (not a picture that moves). Fallback (Linux / macOS, LYNX_OVERLAY=0, Draw with the GPU off, a window over the mark):
    // the locator moves with the fills, as before. LYNX_GB_DIAG bit 2: never on the overlay (A/B measurements).
    std::unique_ptr<ui::MotionOverlay> gbOverlay;
    bool gbLocatorOnGpu() const noexcept { return gbOverlay != nullptr && gbOverlay->isShowing(); }
    const LayoutItem* gbEditorItem() const
    {
        for (const auto& item : activeLayout().items)
            if (isGbItem (item, "grossbeat") && visible (item))
                return &item;
        return nullptr;
    }
    // the locator and its trail at x (paint() and the overlay's picture: the same code)
    void gbPaintLocator (juce::Graphics& g, juce::Rectangle<float> inner, float x) const
    {
        using namespace theme;
        const auto lineW = juce::jmax (1.0f, std::floor (scale()));
        if (gblocator::flStyle()) // round 190jd: FL's colours, cycling once per beat (core/GrossBeatLocator.h)
        {
            // (drawn at the phase's colour step, so the overlay's pictures stay a bounded set)
            const auto phase = (gblocator::stepOf (gbPaintPhase) + 0.5) / gblocator::stepsPerBeat;
            const auto line = juce::Colour (gblocator::lineColour (phase));
            if (ui::motion::enabled())
                for (int px = 1; px <= 3; ++px)
                    if (const auto a = gblocator::trailAlpha (px, phase); a > 0.0f)
                    {
                        g.setColour (line.withAlpha (a));
                        g.fillRect (juce::Rectangle<float> (x - (float) px * lineW, inner.getY(), lineW, inner.getHeight()).getIntersection (inner));
                    }
            g.setColour (line);
            g.fillRect (juce::Rectangle<float> (x, inner.getY(), lineW, inner.getHeight()).getIntersection (inner));
            return;
        }
        if (const auto trail = gbTrailWidth(); trail > 0.0f)
        {
            const auto t = juce::Rectangle<float> (x - trail, inner.getY(), trail, inner.getHeight()).getIntersection (inner);
            g.setGradientFill (juce::ColourGradient (colour::playhead.withAlpha (0.0f), x - trail, 0.0f, colour::playhead.withAlpha (0.45f), x, 0.0f, false));
            g.fillRect (t);
        }
        g.setColour (colour::playhead);
        g.fillRect (juce::Rectangle<float> (x, inner.getY(), lineW, inner.getHeight()).getIntersection (inner));
    }
    // moves the overlay to `now` (every tick); the locator's painted columns are repainted the tick it goes on or off it
    void gbStepOverlay (const GbLive& now)
    {
        const auto* item = gbEditorItem();
        const auto wanted = item != nullptr && now.playing && (gbDiag() & 2) == 0;
        if (gbOverlay == nullptr)
        {
            if (! wanted)
                return;
            gbOverlay = std::make_unique<ui::MotionOverlay> (*this, [this] (juce::Graphics& g, float x) {
                if (const auto* it = gbEditorItem())
                    gbPaintLocator (g, gbArea (*it), x);
            });
            // the Themed style's art layer spans the whole editor in front of it (Gross Beat's layout has no menu, so the
            // art's stage is the whole editor): the overlay took it for a cover and fell back at every tick (Windows
            // --uiperf: 25-30 fallbacks a second, no overlay frame). It is ignored now: a mote of the art that drifts
            // across the envelope editor shows under the locator instead of over it (rollup 0.1.30, recommended)
            if (artLayer != nullptr)
                artLayer->getProperties().set ("lynxOverlayIgnores", true);
            gbOverlay->onHidden = [this] { // (it hid itself: the locator is painted again, where the fills' frame has it)
                if (const auto* it = gbEditorItem(); it != nullptr && gbLive.playing)
                    repaint (gbLocatorBounds (*it, gbLive));
            };
        }
        // FL's colours (round 190jd): the locator jumps about 21 times a second, as FL's does
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();
        if (gblocator::flStyle() && wanted && gbOverlay->isShowing() && nowMs - gbLastOverlayMs < 1000.0 / gblocator::flRateHz - 4.0)
            return;
        gbLastOverlayMs = nowMs;
        gbPaintPhase = gbPhaseOf (now);
        ui::MotionOverlay::Mark mark;
        if (item != nullptr)
        {
            const auto inner = gbArea (*item);
            const auto trail = gbTrailWidth();
            const auto lineW = juce::jmax (1.0f, std::floor (scale()));
            mark.x = gbLocatorX (*item, now);
            mark.extent = juce::Rectangle<float> (-trail - 1.0f, inner.getY(), trail + lineW + 2.0f, inner.getHeight()).getSmallestIntegerContainer();
            mark.clip = inner.getSmallestIntegerContainer();
            mark.look = ((std::uint64_t) theme::colour::playhead.getARGB() << 32) ^ ((std::uint64_t) juce::roundToInt (trail * 64.0f) << 16)
                        ^ ((std::uint64_t) juce::roundToInt (lineW) << 12) ^ (std::uint64_t) mark.clip.getHeight()
                        ^ (gblocator::flStyle() ? ((std::uint64_t) (gblocator::stepOf (gbPaintPhase) + 1) << 56) : 0); // (round 190jd)
        }
        const auto gpu = ui::stepMark (*gbOverlay, wanted, mark, false);
        if (gpu.flipped && item != nullptr && gbLive.playing) // (the painted locator goes, or comes back)
            repaint (gbLocatorBounds (*item, gbLive));
    }
    // 15 a second by default: measured in one process against the static window (round 190ih, Thief, interleaved
    // 2.5 s steps), 30 a second cost +5 … +13 points of process CPU, 15 or fewer stayed inside the measurement's own
    // noise; LYNX_GB_DIAG's high byte overrides it for measurements
    // (round 190jd: FL's colours move at FL's ~21 Hz)
    static double gbMaxHz() { const auto hz = gbDiag() >> 8; return hz > 0 ? (double) hz : gblocator::flStyle() ? gblocator::flRateHz : 15.0; }

    // the drawn width of the playing cell's fill, in px
    int gbFillPx (const LayoutItem& item, int kind, const GbLive& v) const
    {
        const auto cell = v.slot[(std::size_t) kind];
        if (! v.playing || cell < 0 || cell >= grossbeat::slotsPerKind)
            return -1;
        const auto r = gbGridCell (item, cell).reduced (1.0f * scale());
        return (int) std::floor (r.getX() + r.getWidth() * v.position[(std::size_t) kind]);
    }

    // round 190ih: what changed since the last tick is repainted, nothing else: the locator's old and new columns when its
    // pixel moved, the fill's new strip when its pixel moved, whole cells only when the playing / queued slot changed
    void gbLiveTick()
    {
       #if JUCE_WINDOWS
        if ((gbDiag() & 64) != 0) // (diagnostics: the window on JUCE's software renderer, as hosted plugin windows are)
            if (auto* peer = getPeer(); peer != nullptr && peer->getAvailableRenderingEngines().size() > 1 && peer->getCurrentRenderingEngine() != 0)
                peer->setCurrentRenderingEngine (0);
       #endif
        if ((gbDiag() & 1) != 0)
            return;
        if (auto* peer = getPeer(); peer != nullptr && peer->isMinimised()) // (nothing to see: no frames)
            return;
        // the frame budget: the editor's window pays a fixed cost for every frame it shows (measured, round 190ih:
        // ≈ 2.5 ms of the message thread per frame plus the GPU driver's work, whatever the size of the repaint; the
        // trail, an opaque editor or the software renderer changed nothing), so the locator and the fills move
        // together, at most gbMaxHz() times a second, and only when one of them moved a pixel
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();
        const auto now = gbReadLive();
        gbStepOverlay (now); // round 190iy: the locator on the GPU, at every tick
        if (gbLive.playing && now.playing && nowMs - gbLastFrameMs < 1000.0 / gbMaxHz() - 4.0) // (a stop shows at once)
            return;
        if (now == gbLive)
            return;
        const auto before = gbLive;
        gbLive = now;
        gbLastFrameMs = nowMs;
        for (const auto& item : activeLayout().items)
        {
            if (! visible (item))
                continue;
            if (isGbItem (item, "gbjog")) // round 190lx (DP-9): the dial's read-position ring, only when it moved a degree
            {
                if (std::lround (gbDialAngle (before)) != std::lround (gbDialAngle (now)) || before.playing != now.playing)
                    repaint (rectOf (item).getSmallestIntegerContainer());
                continue;
            }
            if (isGbItem (item, "grossbeat"))
            {
                if (gbLocatorOnGpu()) // (round 190iy: the overlay moves it; nothing under it is painted)
                    continue;
                const auto xBefore = before.playing ? (int) std::floor (gbLocatorX (item, before)) : -1;
                const auto xNow = now.playing ? (int) std::floor (gbLocatorX (item, now)) : -1;
                if (xBefore == xNow)
                    continue; // (the same pixel: nothing to draw)
                if (before.playing)
                    repaint (gbLocatorBounds (item, before));
                if (now.playing)
                    repaint (gbLocatorBounds (item, now));
            }
            else if (const auto k = gbGridKind (item); k >= 0)
            {
                const auto kk = (std::size_t) k;
                if (before.playing != now.playing || before.slot[kk] != now.slot[kk] || before.queued[kk] != now.queued[kk])
                {
                    for (const auto cell : { before.slot[kk], now.slot[kk], before.queued[kk], now.queued[kk] })
                        if (cell >= 0 && cell < grossbeat::slotsPerKind)
                            repaint (gbGridCell (item, cell).getSmallestIntegerContainer().expanded (1));
                    continue;
                }
                const auto a = gbFillPx (item, k, before), b = gbFillPx (item, k, now);
                if (a == b || a < 0 || b < 0)
                    continue;
                const auto cell = gbGridCell (item, now.slot[kk]).getSmallestIntegerContainer();
                if (b > a) // (the fill grew: its new strip)
                    repaint (juce::Rectangle<int> (a - 1, cell.getY(), b - a + 3, cell.getHeight()));
                else // (it wrapped round: the cell)
                    repaint (cell.expanded (1));
            }
        }
    }

    // Round 190lx (DP-9, capture 190lx films/gbdial): FL's scratch dial does not turn; while playing, a small ring shows
    // the read position: on the 12 o'clock dot with no delay, moving counter-clockwise 45° per beat of delay (one turn =
    // the time envelope's 8 beats). Degrees counter-clockwise from 12 o'clock; -1 when stopped.
    float gbDialAngle (const GbLive& live) const
    {
        if (! live.playing)
            return -1.0f;
        const auto index = live.slot[0] >= 0 ? live.slot[0] : gbSlotOf (0);
        if (index < 0 || index >= grossbeat::slotsPerKind)
            return -1.0f;
        const auto level = grossbeat::levelAt (gbState().slots[(std::size_t) index], (double) live.position[0] * grossbeat::barBeats);
        return juce::jlimit (0.0f, 360.0f, (1.0f - level) * 8.0f * 45.0f);
    }

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
        if (gbFrozen) // (Freeze editing: no points to take)
            return -1;
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
        if (gbFrozen)
            return -1;
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
        gbLastMouseBeat = gbMouseValue (item, e.position).first;
        if (gbFrozen) // (FL's Freeze editing: the envelope can't be edited, its points are hidden)
            return false;
        if (getWantsKeyboardFocus())
            grabKeyboardFocus(); // (round 190mf: the points' Ctrl+A / Ctrl+D / Ctrl+I / Del)
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
            gbSelected.clear();
            gbDragPoint = grossbeat::addPoint (points, x, y, 2, gbLastTension);
            gbDragPoints = points;
            gbDragging = true;
            gbWritePoints (points);
            gbShowTip (item, x, y);
            return true;
        }
        if (point < 0 && handle < 0 && e.mods.isCommandDown()) // round 190mk (DP-7): FL's Ctrl+left-drag box selects
        {
            gbBoxing = gbDragging = true;
            gbBoxFrom = gbBoxTo = e.position;
            gbSelected.clear();
            repaint();
            return true;
        }
        if (handle >= 0 && e.mods.isAltDown()) // FL: Alt+click on a handle = default tension
        {
            points[(std::size_t) handle].tension = 0.0f;
            gbLastTension = 0.0f;
            gbWritePoints (points);
            return false;
        }
        if (gbStepEdit) // FL's Step editing: a left drag draws hold steps (capture 190mf A1)
        {
            gbSelected.clear();
            gbDragPoints = points;
            gbDragging = true;
            gbStepLastX = -1.0;
            gbStepDraw (item, e, true);
            return true;
        }
        if (point >= 0)
        {
            if (gbSelected.count (point) == 0) // (FL: the dragged point is the selection)
                gbSelected = { point };
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
        if (gbBoxing) // (round 190mk: the points inside the box are the selection)
        {
            gbBoxTo = e.position;
            const auto box = juce::Rectangle<float> (gbBoxFrom, gbBoxTo);
            const auto pts = grossbeat::editPoints (gbState().slots[(std::size_t) gbSlotIndex()]);
            const auto inner = gbArea (item);
            gbSelected.clear();
            for (int i = 0; i < (int) pts.size(); ++i)
                if (box.contains (gbPos (inner, pts[(std::size_t) i].x, pts[(std::size_t) i].y)))
                    gbSelected.insert (i);
            repaint();
            return;
        }
        if (gbStepEdit && gbDragPoint < 0 && gbDragTension < 0)
        {
            gbStepDraw (item, e, false);
            return;
        }
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
            if (gbSlide) // FL's Slide remaining points: the points after it move with it (inside the bar)
                grossbeat::slidePoint (points, gbDragPoint, x, y);
            else
                grossbeat::movePoint (points, gbDragPoint, x, y);
            gbShowTip (item, points[(std::size_t) gbDragPoint].x, points[(std::size_t) gbDragPoint].y);
        }
        else
            return;
        gbDragPoints = points;
        gbWritePoints (points);
    }

    // FL's Step editing (capture 190mf A1, fl-gb-step*.png): with the snap off a drag draws one flat hold from where it
    // started to where it is, at the height under the mouse (the points inside are replaced); with a snap, every snap
    // cell the mouse passes gets a hold point at its left edge at the mouse's height there (a staircase). Shift (FL's
    // "pulse mode") drew the same in the capture.
    void gbStepDraw (const LayoutItem& item, const juce::MouseEvent& e, bool first)
    {
        auto [x, y] = gbMouseValue (item, e.position);
        auto points = gbDragPoints;
        const auto step = grossbeat::snapStep (gbSnap);
        const auto snapOn = (step > 0.0) != e.mods.isAltDown();
        if (! snapOn)
        {
            const auto [x0, unusedY] = gbMouseValue (item, e.mouseDownPosition);
            juce::ignoreUnused (unusedY);
            grossbeat::stepDraw (points, juce::jmin (x0, x), juce::jmax (x0, x), y);
            gbWritePoints (points);
            gbShowTip (item, x, y);
            return;
        }
        const auto use = step > 0.0 ? step : grossbeat::snapStep (3);
        const auto cell = std::floor (x / use) * use;
        const auto from = first || gbStepLastX < 0.0 ? cell : std::floor (gbStepLastX / use) * use;
        for (auto c = juce::jmin (from, cell); c <= juce::jmax (from, cell) + 1.0e-9; c += use)
            grossbeat::stepDraw (points, c, juce::jmin (grossbeat::barBeats, c + use), y);
        gbStepLastX = x;
        gbDragPoints = points;
        gbWritePoints (points);
        gbShowTip (item, cell, y);
    }

    void gbSelectAll (int how) // 0 select all, 1 deselect all, 2 invert
    {
        const auto n = (int) grossbeat::editPoints (gbState().slots[(std::size_t) gbSlotIndex()]).size();
        std::set<int> next;
        for (int i = 0; i < n; ++i)
            if (how == 0 || (how == 2 && gbSelected.count (i) == 0))
                next.insert (i);
        gbSelected = std::move (next);
        repaint();
    }

    void gbDeleteSelected()
    {
        auto points = grossbeat::editPoints (gbState().slots[(std::size_t) gbSlotIndex()]);
        for (auto it = gbSelected.rbegin(); it != gbSelected.rend(); ++it)
            if (*it > 0 && *it + 1 < (int) points.size()) // (the first and the last point stay)
                points.erase (points.begin() + *it);
        gbSelected.clear();
        gbWritePoints (points);
    }

    // Ctrl+A / Ctrl+D / Ctrl+I / Del on the envelope (FL's Points selection shortcuts)
    bool gbKey (const juce::KeyPress& k)
    {
        const auto cmd = k.getModifiers().isCommandDown();
        if (cmd && (k.getKeyCode() == 'A' || k.getKeyCode() == 'D' || k.getKeyCode() == 'I'))
        {
            gbSelectAll (k.getKeyCode() == 'A' ? 0 : k.getKeyCode() == 'D' ? 1 : 2);
            return true;
        }
        if (! cmd && (k.getKeyCode() == juce::KeyPress::deleteKey) && ! gbSelected.empty())
        {
            gbDeleteSelected();
            return true;
        }
        // round 190mk (DP-7): FL's Cut Ctrl+X, Copy Ctrl+C, Paste (insert) Ctrl+V, Paste (replace) Ctrl+R, Duplicate Ctrl+B
        if (cmd && ! k.getModifiers().isShiftDown() && ! k.getModifiers().isAltDown())
            switch (k.getKeyCode())
            {
                case 'X': gbClipboardCommand (0); return true;
                case 'C': gbClipboardCommand (1); return true;
                case 'V': gbClipboardCommand (2); return true;
                case 'R': gbClipboardCommand (3); return true;
                case 'B': gbClipboardCommand (4); return true;
                default: break;
            }
        return false;
    }

    std::vector<int> gbSelectionVector() const { return { gbSelected.begin(), gbSelected.end() }; }

    // 0 Cut, 1 Copy, 2 Paste (insert), 3 Paste (replace), 4 Duplicate (capture 190mk A1.4; core/GrossBeat.h)
    void gbClipboardCommand (int which)
    {
        if (gbFrozen)
            return;
        const auto points = grossbeat::editPoints (gbState().slots[(std::size_t) gbSlotIndex()]);
        const auto sel = gbSelectionVector();
        const auto select = [this] (const std::vector<int>& indices) { gbSelected = std::set<int> (indices.begin(), indices.end()); };
        switch (which)
        {
            case 0:
            case 1:
                if (sel.empty())
                    return;
                gbPointClip = grossbeat::copyPoints (points, sel);
                if (which == 0)
                {
                    gbSelected.clear();
                    gbWritePoints (grossbeat::cutPoints (points, sel));
                }
                return;
            case 2:
            case 3:
            {
                if (! gbPointClip || gbPointClip->points.empty() || (which == 3 && sel.empty()))
                    return;
                const auto r = which == 2 ? grossbeat::pasteInsert (points, sel, *gbPointClip, gbLastMouseBeat)
                                          : grossbeat::pasteReplace (points, sel, *gbPointClip);
                select (r.second);
                gbWritePoints (r.first);
                return;
            }
            case 4:
            {
                if (sel.empty())
                    return;
                const auto r = grossbeat::duplicatePoints (points, sel);
                select (r.second);
                gbWritePoints (r.first);
                return;
            }
            default:
                return;
        }
    }

    // State › Presets: FL's envelope maps from its installation (Data\System\Articulator\Maps; read, never shipped),
    // after "Default" (a flat bar at full level, as Reset)
    static juce::Array<juce::File> gbPresetFiles()
    {
        static const juce::Array<juce::File> files = [] {
            juce::Array<juce::File> out;
            const auto fl = findFlStudio();
            if (fl.installDir.isDirectory())
                out = fl.installDir.getChildFile ("Data").getChildFile ("System").getChildFile ("Articulator").getChildFile ("Maps")
                          .findChildFiles (juce::File::findFiles, false, "*.fnv");
            std::sort (out.begin(), out.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
            return out;
        }();
        return files;
    }
    static juce::File gbStateFolder() { return platform::documentsFolder().getChildFile ("LYNX/Envelope states"); }

    bool gbLoadEnvelopeFile (const juce::File& file)
    {
        juce::MemoryBlock data;
        if (file.getSize() > 4 * 1024 * 1024 || ! file.loadFileAsData (data))
            return false;
        const auto points = grossbeat::readEnvelopeFile ((const std::uint8_t*) data.getData(), data.getSize());
        if (! points)
        {
            ui::postHint ({ tr ("Not an envelope state file: {0}", { file.getFileName() }), {} }, 3000);
            return false;
        }
        gbSelected.clear();
        gbWritePoints (*points);
        return true;
    }

    void gbOpenStateFile()
    {
        const auto folder = gbStateFolder();
        folder.createDirectory();
        gbChooser = ui::makeFileChooser (tr ("Open state file"), folder, "*.fnv");
        ui::launchChooser (gbChooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe = juce::Component::SafePointer<NativeEffectEditor> (this)] (const juce::FileChooser& c) {
                               if (safe != nullptr && c.getResult().existsAsFile())
                                   safe->gbLoadEnvelopeFile (c.getResult());
                           });
    }

    void gbSaveStateFile()
    {
        const auto folder = gbStateFolder();
        folder.createDirectory();
        gbChooser = ui::makeFileChooser (tr ("Save state file"), folder, "*.fnv");
        ui::launchChooser (gbChooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                          | juce::FileBrowserComponent::warnAboutOverwriting,
                           [safe = juce::Component::SafePointer<NativeEffectEditor> (this)] (const juce::FileChooser& c) {
                               if (safe == nullptr || c.getResult() == juce::File())
                                   return;
                               const auto file = c.getResult().withFileExtension ("fnv");
                               const auto bytes = grossbeat::writeEnvelopeFile (safe->gbState().slots[(std::size_t) safe->gbSlotIndex()]);
                               if (! file.replaceWithData (bytes.data(), bytes.size()))
                                   ui::postHint ({ tr ("Could not write {0}", { file.getFullPathName() }), {} }, 3000);
                           });
    }

    // Tools › Analyze audio file... (FL's manual: "Opens a file-import window and analyzes the envelope volume of the
    // selected audio, converting it to an mapping envelope"; capture 190mk A1.5: no options dialog, the whole file over
    // the bar, ~100 single-curve points, loud at the top)
    void gbAnalyzeAudioFile()
    {
        gbChooser = ui::makeFileChooser (tr ("Analyze audio file"), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        ui::launchChooser (gbChooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe = juce::Component::SafePointer<NativeEffectEditor> (this)] (const juce::FileChooser& c) {
                               if (safe != nullptr && c.getResult().existsAsFile())
                                   safe->gbAnalyze (c.getResult());
                           });
    }
    bool gbAnalyze (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0)
        {
            ui::postHint ({ tr ("Could not read {0}", { file.getFileName() }), {} }, 3000);
            return false;
        }
        constexpr int slices = 1024;
        const auto total = std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 1200.0)); // (20 minutes at most)
        std::vector<float> peaks (slices, 0.0f);
        const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
        juce::AudioBuffer<float> block (channels, 8192);
        for (juce::int64 pos = 0; pos < total; pos += block.getNumSamples())
        {
            const auto n = (int) std::min<juce::int64> (block.getNumSamples(), total - pos);
            reader->read (&block, 0, n, pos, true, channels > 1);
            for (int i = 0; i < n; ++i)
            {
                auto v = 0.0f;
                for (int ch = 0; ch < channels; ++ch)
                    v = juce::jmax (v, std::abs (block.getSample (ch, i)));
                auto& peak = peaks[(std::size_t) std::min<juce::int64> (slices - 1, (pos + i) * slices / total)];
                peak = juce::jmax (peak, v);
            }
        }
        gbSelected.clear();
        gbWritePoints (grossbeat::envelopeFromLevels (peaks));
        return true;
    }

    void gbEnvelopeUp()
    {
        gbBoxing = false;
        gbDragging = false;
        gbDragPoint = gbDragTension = -1;
        ui::hideValueTip();
        repaint();
    }

    void gbDeletePoint (int point)
    {
        gbSelected.clear();
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

    // parts of the column in design units from its top: 0 = Options, 1…7 = snap cells, 8 / 9 = time / volume,
    // 10 / 11 / 12 = Step editing / Slide remaining points / Freeze editing (round 190mf: FL's places, 272 / 298 / 325
    // from the column's top in capture 190lx GB4)
    juce::Rectangle<float> gbToolCell (const LayoutItem& item, int part) const
    {
        const auto r = rectOf (item);
        const auto s = scale();
        if (part == 0)
            return { r.getX(), r.getY(), r.getWidth(), 27.0f * s };
        if (part <= 7)
            return { r.getX(), r.getY() + (36.0f + 27.0f * (float) (part - 1)) * s, r.getWidth(), 26.0f * s };
        if (part >= 10)
            return { r.getX(), r.getY() + (271.0f + 27.0f * (float) (part - 10)) * s, r.getWidth(), 26.0f * s };
        return { r.getX(), r.getBottom() - (part == 8 ? 54.0f : 27.0f) * s, r.getWidth(), 26.0f * s };
    }

    int gbToolAt (const LayoutItem& item, juce::Point<float> p) const
    {
        for (int part = 0; part <= 12; ++part)
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
            gbEditKind = part - 8, gbSelected.clear();
        else if (part == 10)
            gbStepEdit = ! gbStepEdit;
        else if (part == 11)
            gbSlide = ! gbSlide;
        else if (part == 12)
            gbFrozen = ! gbFrozen;
        repaint();
    }

    // Round 190lx (DP-4, capture 190lx GB2 fl-gb-slot-menu.png): FL's right-click menu of a time / volume slot — Reset,
    // Edit slot name..., Move slot left, Move slot right, then the slot selector's parameter menu (Automation: Edit events,
    // Init song with this position, Create automation clip...; Remote control: Link to controller...; Value: Copy value,
    // Paste value, Type in value...). LYNX's own slot items (copy / paste the slot, flip, factory slots) follow under a line.
    void gbSlotMenu (juce::Rectangle<float> at)
    {
        juce::PopupMenu menu;
        const auto k = gbEditKind, slot = gbSlotOf (gbEditKind);
        menu.addItem (tr ("Reset"), [this] { gbResetSlot(); });
        menu.addItem (tr ("Edit slot name..."), [this] { gbRename(); });
        menu.addItem (tr ("Move slot left"), slot > 0, false, [this, k, slot] { gbMoveSlot (k, slot, -1); });
        menu.addItem (tr ("Move slot right"), slot + 1 < grossbeat::slotsPerKind, false, [this, k, slot] { gbMoveSlot (k, slot, 1); });
        LayoutItem selector;
        selector.type = Item::knob;
        selector.param = k == 0 ? "Time slot" : "Volume slot";
        selector.steps = grossbeat::slotsPerKind - 1;
        if (const auto param = paramOf (selector); param >= 0)
        {
            // (LYNX's parameter menu starts with its own "Reset (value)": FL's slot menu has the slot's Reset above instead)
            juce::PopupMenu items;
            ui::addValueItems (items, *this, node.locate ? nullptr : &engine, specFor (selector), param01 (selector.param),
                               [this, param] (double v) { setParams ({ { param, (float) v } }); });
            bool first = true;
            for (juce::PopupMenu::MenuItemIterator it (items); it.next();)
                if (! std::exchange (first, false))
                    menu.addItem (it.getItem());
        }
        menu.addSeparator();
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
        menu.addSubMenu (tr ("Factory slots"), gbFactorySlots());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (at.toNearestInt())));
    }

    void gbResetSlot() // an empty slot: a flat bar at full level, unnamed
    {
        auto slot = gbState().slots[(std::size_t) gbSlotIndex()];
        slot.name.clear();
        grossbeat::setPoints (slot, { { 0.0, 1.0f, 0.0f, 2 }, { grossbeat::barBeats, 1.0f, 0.0f, 2 } });
        gbWriteSlot (slot);
    }

    // Round 190mf (DP-7, capture 190lx GB3 fl-gb-mapping-menu.png): FL's mapping options menu (the ▾ at the top of the
    // tool column), two columns as FL's: [State] Reset, Copy state, Paste state; [Tools] Insert point at locator, Flip
    // vertically, Scale levels..., Normalize levels, Decimate points..., Filter..., Smooth up..., Turn all points
    // smooth, Create sequence... (the forms are LYNX's Edison envelope tools, core/AudioEditTools.h: FL opens the same
    // TMEnvEditor tool forms). Round 190mk (capture 190mk A1) added the rest: State › Presets / Open / Save state file /
    // Prepare for smooth editing, the point clipboard items, Analyze audio file..., Options › Focus on click, View ›
    // Background gradient / Invert grid. LYNX's own items (rename, factory slots, the locator's colours) follow under a line.
    void gbOptionsMenu (juce::Rectangle<float> at)
    {
        gbBuildOptionsMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (at.toNearestInt())));
    }

    juce::PopupMenu gbBuildOptionsMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (tr ("State"));
        { // round 190mk (DP-7, capture 190mk A1): FL's Presets (Default, then FL's maps), Open / Save state file (*.fnv)
            juce::PopupMenu presets;
            presets.addItem (tr ("Default"), [this] { gbResetSlot(); });
            for (const auto& f : gbPresetFiles())
                presets.addItem (f.getFileNameWithoutExtension(), [this, f] { gbLoadEnvelopeFile (f); });
            menu.addSubMenu (tr ("Presets"), presets);
        }
        menu.addItem (tr ("Open state file..."), [this] { gbOpenStateFile(); });
        menu.addItem (tr ("Save state file..."), [this] { gbSaveStateFile(); });
        menu.addSeparator();
        menu.addItem (tr ("Reset"), [this] { gbResetSlot(); });
        menu.addItem (tr ("Prepare for smooth editing"), [this] {
            gbSelected.clear();
            gbWritePoints (grossbeat::prepareForSmoothEditing (gbState().slots[(std::size_t) gbSlotIndex()]));
        });
        menu.addSeparator(); // (FL: a line between Reset and Copy state)
        menu.addItem (tr ("Copy state"), [this] { gbClipboard = gbState().slots[(std::size_t) gbSlotIndex()]; });
        menu.addItem (tr ("Paste state"), gbClipboard.has_value(), false, [this] {
            if (gbClipboard)
                gbWriteSlot (*gbClipboard);
        });
        menu.addSectionHeader (tr ("Points selection"));
        { auto it = juce::PopupMenu::Item (tr ("Select all")).setAction ([this] { gbSelectAll (0); }); it.shortcutKeyDescription = "Ctrl+A"; menu.addItem (std::move (it)); }
        { auto it = juce::PopupMenu::Item (tr ("Deselect all")).setAction ([this] { gbSelectAll (1); }); it.shortcutKeyDescription = "Ctrl+D"; menu.addItem (std::move (it)); }
        { auto it = juce::PopupMenu::Item (tr ("Invert")).setAction ([this] { gbSelectAll (2); }); it.shortcutKeyDescription = "Ctrl+I"; menu.addItem (std::move (it)); }
        // round 190mk (DP-7): FL's point clipboard items
        const auto clipItem = [this, &menu] (const juce::String& text, const char* keys, bool enabled, int which) {
            auto it = juce::PopupMenu::Item (text).setEnabled (enabled).setAction ([this, which] { gbClipboardCommand (which); });
            it.shortcutKeyDescription = keys;
            menu.addItem (std::move (it));
        };
        const auto hasClip = gbPointClip.has_value() && ! gbPointClip->points.empty();
        clipItem (tr ("Cut"), "Ctrl+X", ! gbSelected.empty(), 0);
        clipItem (tr ("Copy"), "Ctrl+C", ! gbSelected.empty(), 1);
        clipItem (tr ("Paste (insert)"), "Ctrl+V", hasClip, 2);
        clipItem (tr ("Paste (replace)"), "Ctrl+R", hasClip && ! gbSelected.empty(), 3);
        clipItem (tr ("Duplicate"), "Ctrl+B", ! gbSelected.empty(), 4);
        { auto it = juce::PopupMenu::Item (tr ("Delete")).setEnabled (! gbSelected.empty()).setAction ([this] { gbDeleteSelected(); }); it.shortcutKeyDescription = "Del"; menu.addItem (std::move (it)); }
        menu.addColumnBreak();
        menu.addSectionHeader (tr ("Tools"));
        const auto locator = gbLive.playing; // (FL greys it while the locator is not shown)
        menu.addItem (tr ("Insert point at locator"), locator, false, [this] { gbInsertAtLocator(); });
        menu.addItem (tr ("Flip vertically"), [this] {
            auto pts = grossbeat::editPoints (gbState().slots[(std::size_t) gbSlotIndex()]);
            for (auto& p : pts)
                p.y = 1.0f - p.y, p.tension = -p.tension;
            gbWritePoints (pts);
        });
        menu.addItem (tr ("Scale levels..."), [this] { gbOpenTool (audiotools::Tool::scaleLevels); });
        menu.addItem (tr ("Normalize levels"), [this] {
            auto pts = grossbeat::editPoints (gbState().slots[(std::size_t) gbSlotIndex()]);
            if (pts.empty())
                return;
            const auto [lo, hi] = std::minmax_element (pts.begin(), pts.end(), [] (const auto& a, const auto& b) { return a.y < b.y; });
            const auto low = lo->y, range = hi->y - lo->y;
            if (range <= 1.0e-6f)
                return;
            for (auto& p : pts)
                p.y = (p.y - low) / range;
            gbWritePoints (pts);
        });
        menu.addItem (tr ("Decimate points..."), [this] {
            // FL refuses Gross Beat's hold, single-curve and smooth points (capture 190mf A1, its message box)
            const auto pts = grossbeat::editPoints (gbState().slots[(std::size_t) gbSlotIndex()]);
            if (std::any_of (pts.begin() + (pts.empty() ? 0 : 1), pts.end(), [] (const auto& p) { return p.type == 0 || p.type == 2 || p.type == 10; }))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Can't decimate"),
                                                        tr ("The envelope contains points with curve modes that aren't compatible with decimation."));
                return;
            }
            gbOpenTool (audiotools::Tool::decimate);
        });
        menu.addItem (tr ("Filter..."), [this] { gbOpenTool (audiotools::Tool::envFilter); });
        menu.addItem (tr ("Smooth up..."), [this] { gbOpenTool (audiotools::Tool::smoothUp); });
        menu.addItem (tr ("Turn all points smooth"), [this] {
            auto pts = grossbeat::editPoints (gbState().slots[(std::size_t) gbSlotIndex()]);
            for (std::size_t i = 1; i < pts.size(); ++i)
                pts[i].type = 10; // (FL's "Smooth" point mode)
            gbWritePoints (pts);
        });
        menu.addItem (tr ("Create sequence..."), [this] { gbOpenTool (audiotools::Tool::createSequence); });
        menu.addItem (tr ("Analyze audio file..."), [this] { gbAnalyzeAudioFile(); }); // round 190mk
        menu.addSectionHeader (tr ("Options"));
        menu.addItem (tr ("Focus on click"), true, gbFocusOnClick, [] { gbFocusOnClick = ! gbFocusOnClick; });
        menu.addSectionHeader (tr ("View"));
        menu.addItem (tr ("Background gradient"), true, gbBackgroundGradient, [this] { gbBackgroundGradient = ! gbBackgroundGradient; repaint(); });
        menu.addItem (tr ("Invert grid"), true, gbInvertGrid, [this] { gbInvertGrid = ! gbInvertGrid; repaint(); });
        menu.addSeparator();
        menu.addItem (tr ("Rename..."), [this] { gbRename(); });
        menu.addSubMenu (tr ("Factory slots"), gbFactorySlots());
        menu.addItem (tr ("Load LYNX SHARE's factory slots (keeps the selected slots)"), [this] { gbWriteState (gbWithFactory()); });
        // round 190jd: the playback locator in FL's colours (cream on the beat, fading to red) instead of LYNX's playhead
        menu.addItem (tr ("Playback locator in FL's colours"), true, gblocator::flStyle(), [this] {
            gblocator::setFlStyle (! gblocator::flStyle()); // (kept in ui-options "grossBeatFlLocator")
            repaint();
        });
        return menu;
    }

    // FL's Tools › Insert point at locator: a point on the curve where the playback locator is
    void gbInsertAtLocator()
    {
        if (! gbLive.playing)
            return;
        const auto slot = gbState().slots[(std::size_t) gbSlotIndex()];
        auto points = grossbeat::editPoints (slot);
        const auto x = grossbeat::barBeats * (double) gbLive.position[(std::size_t) gbEditKind];
        if (std::any_of (points.begin(), points.end(), [x] (const auto& p) { return std::abs (p.x - x) < 1.0e-6; }))
            return;
        grossbeat::addPoint (points, x, grossbeat::levelAt (slot, x), 2, gbLastTension);
        gbWritePoints (points);
    }

    // The envelope tool forms on the selected slot (round 190mf): LYNX's Edison tool windows (ui/AudioEditorTools) see the
    // slot's points as an envelope over 0…1 = the 4 beats; every change is written back to the slot (the window's Cancel
    // puts the slot back). Points the tool kept keep their point mode; new ones are FL's "Single curve".
    struct GbToolHost final : audiotools::Host
    {
        explicit GbToolHost (NativeEffectEditor& e) : editor (e)
        {
            doc.audio.sampleRate = 1.0; // (4 frames at 1 Hz = 4 s = the 4 beats at toolBpm's 60)
            doc.audio.channels.assign (1, std::vector<float> (4, 0.0f));
        }
        NativeEffectEditor& editor;
        audioedit::Document doc;
        audioedit::Envelope env;
        std::vector<grossbeat::EditPoint> original;
        audioedit::NoiseProfile noise;
        audioedit::Audio emptyClip;

        void load (const std::vector<grossbeat::EditPoint>& points)
        {
            original = points;
            env.points.clear();
            for (const auto& p : points)
                env.points.push_back ({ p.x / grossbeat::barBeats, (double) p.y, p.tension });
        }
        const audioedit::Document& toolDocument() const override { return doc; }
        audioedit::Range toolSelection() const override { return { 0, 4 }; }
        void toolApply (const juce::String&, audioedit::Document, audioedit::Range) override {}
        void toolPreview (const audioedit::Audio&) override {}
        void toolStopPreview() override {}
        double toolBpm() const override { return 60.0; }
        audioedit::NoiseProfile& toolNoiseProfile() override { return noise; }
        const audioedit::Audio& toolClipboard() const override { return emptyClip; }
        audioedit::Envelope* toolEnvelope() override { return &env; }
        double toolEnvelopeUnity() const override { return 1.0; }
        void toolEnvelopeChanged (bool, const juce::String&) override
        {
            if (! detached)
                editor.gbWritePoints (gbPointsOf (env, original));
        }
        bool detached = false; // (the editor is closing: nothing is written any more)
        juce::Component& toolComponent() override { return editor; }
    };
    std::unique_ptr<GbToolHost> gbToolHost;
    juce::Component::SafePointer<juce::DocumentWindow> gbToolWindow;

    // an envelope 0…1 back as the slot's points (at most maxEditPoints; the first at 0, the last at the bar's end)
    static std::vector<grossbeat::EditPoint> gbPointsOf (audioedit::Envelope env, const std::vector<grossbeat::EditPoint>& original)
    {
        env.sort();
        for (double threshold = 0.002; (int) env.points.size() > grossbeat::maxEditPoints && threshold < 1.0; threshold *= 2.0)
            audioedit::decimatePoints (env, threshold);
        if (! env.points.empty() && env.points.back().x < 1.0 - 1.0e-9)
            env.points.push_back (audioedit::EnvPoint { 1.0, env.at (1.0), 0.0f });
        if (! env.points.empty() && env.points.front().x > 1.0e-9)
            env.points.insert (env.points.begin(), audioedit::EnvPoint { 0.0, env.at (0.0), 0.0f });
        std::vector<grossbeat::EditPoint> out;
        for (const auto& p : env.points)
        {
            grossbeat::EditPoint e;
            e.x = juce::jlimit (0.0, grossbeat::barBeats, p.x * grossbeat::barBeats);
            e.y = (float) juce::jlimit (0.0, 1.0, p.y);
            e.tension = p.tension;
            e.type = 0; // (a new point: FL's "Single curve")
            for (const auto& o : original)
                if (std::abs (o.x - e.x) < 1.0e-6)
                {
                    e.type = o.type;
                    break;
                }
            out.push_back (e);
        }
        if (out.size() < 2) // (an envelope keeps two points)
            out = { { 0.0, out.empty() ? 1.0f : out[0].y, 0.0f, 2 }, { grossbeat::barBeats, out.empty() ? 1.0f : out[0].y, 0.0f, 2 } };
        return out;
    }

    void gbOpenTool (audiotools::Tool tool)
    {
        if (gbToolWindow != nullptr)
            delete gbToolWindow.getComponent(); // (one tool window at a time: its Cancel puts the slot back first)
        if (gbToolHost == nullptr)
            gbToolHost = std::make_unique<GbToolHost> (*this);
        gbToolHost->load (grossbeat::editPoints (gbState().slots[(std::size_t) gbSlotIndex()]));
        gbToolWindow = openToolWindow (audiotools::makeToolPanel (*gbToolHost, tool), tr (audiotools::toolTitle (tool)), "grossbeat-tool");
    }

    void gbCloseTool() // (the editor closes: the tool window that works on it goes with it, the slot as it is now)
    {
        if (gbToolHost != nullptr)
            gbToolHost->detached = true;
        if (gbToolWindow != nullptr)
            delete gbToolWindow.getComponent();
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

    // Round 190lx (DP-4, FL manual "Right-click the slot button and select Move slot left / Move slot right"): the slot
    // trades places with its neighbour in the same row of 36; the selection goes with it
    void gbMoveSlot (int kindIndex, int slot, int delta)
    {
        const auto to = slot + delta;
        if (to < 0 || to >= grossbeat::slotsPerKind || slot < 0 || slot >= grossbeat::slotsPerKind)
            return;
        auto state = gbState();
        std::swap (state.slots[(std::size_t) (kindIndex * grossbeat::slotsPerKind + slot)], state.slots[(std::size_t) (kindIndex * grossbeat::slotsPerKind + to)]);
        gbWriteState (state);
        gbSelect (kindIndex, to);
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
        if (gbFocusOnClick && getWantsKeyboardFocus()) // round 190mk: Options › Focus on click (any click takes the keys)
            grabKeyboardFocus();
        if (isGbItem (item, "gbjog")) // round 190oa (DP-8): the scratch dial
        {
            if (gbState().options[7] == 0 || ! e.mods.isLeftButtonDown())
                return false;
            gbScratching = true;
            gbScratchFromY = e.position.y;
            gbScratchFrom = gbScratchValue();
            dragItem = index;
            repaintGbDial();
            return true;
        }
        if (item.type == Item::radio && item.param != nullptr) // FL: a click in a slot grid shows that slot's envelope
        {
            gbSelected.clear(); // (round 190mf: another slot's points)
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
                if (e.mods.isPopupMenu()) // right-click: FL's slot menu (round 190lx, DP-4)
                    gbSlotMenu (gbGridCell (item, slot));
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

    // FL's Options button over the slot grids (DFM OptionsBtn, SynthResModule.OptionsMenu), in FL's order: Trigger slots
    // from MIDI notes | Click reduction › Off / Low (drums) / High (melodic), Smoothing attack compensation, Remove DC
    // offset, HQ resampling | View scratching clock. Each item sets one of the plugin's option bytes (round 190ez, REA of
    // Gross Beat_x64.dll: CenterMenuClick 0x845640 writes +0x16c DC (byte 4), +0x16d compensation (5), +0x16e HQ (6) and
    // the Click reduction radio index at +0x170 (8); ViewScratchMenuClick 0x8459b0 writes +0x16f (7)). FL's last item,
    // About (its splash), is left out. Below a separator, LYNX's own extra: the MIDI input port the trigger notes come
    // from (FL 2026 has no port setting for Gross Beat; owner decision D1, live FL check 190ew).
    void gbPluginOptionsMenu (juce::Rectangle<float> at)
    {
        const auto* fx = current();
        if (fx == nullptr)
            return;
        const auto st = gbState();
        auto option = [this] (int byte, std::uint8_t value) {
            auto next = gbState();
            next.options[(std::size_t) byte] = value;
            gbWriteState (next);
        };
        auto toggle = [&st, option] (juce::PopupMenu& m, const juce::String& text, int byte) {
            const auto on = st.options[(std::size_t) byte] != 0;
            m.addItem (text, true, on, [option, byte, on] { option (byte, on ? 0 : 1); });
        };
        juce::PopupMenu menu;
        menu.addItem (tr ("Trigger slots from MIDI notes"), true, st.midiTrigger, [this, on = st.midiTrigger] {
            auto next = gbState();
            next.midiTrigger = ! on;
            gbWriteState (next);
        });
        menu.addSeparator();
        juce::PopupMenu click;
        const auto level = std::min<int> (st.options[8], 2);
        static const char* const levels[3] { TR_NOOP ("Off"), TR_NOOP ("Low (drums)"), TR_NOOP ("High (melodic)") };
        for (int i = 0; i < 3; ++i)
        {
            click.addItem (tr (levels[i]), true, level == i, [option, i] { option (8, (std::uint8_t) i); });
        }
        menu.addSubMenu (tr ("Click reduction"), click);
        toggle (menu, tr ("Smoothing attack compensation"), 5);
        toggle (menu, tr ("Remove DC offset"), 4);
        toggle (menu, tr ("HQ resampling"), 6);
        menu.addSeparator();
        toggle (menu, tr ("View scratching clock"), 7);
        menu.addSeparator();
        menu.addSubMenu (tr ("MIDI input port"), midiPortMenu());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (at.toNearestInt())));
    }

    juce::String gbHint (const LayoutItem& item, juce::Point<float> p) const
    {
        if (isGbItem (item, "gbjog")) // round 190oa (DP-8): FL's hint, with LYNX's gestures
            return tr ("Time offset (scratching)") + "  " + tr ("[LMB] drag up / down to scratch (released, it springs back)  [Wheel] step");
        if (isGbItem (item, "gbopts"))
            return tr ("Options: trigger slots from MIDI notes (time slots: keys C5 to B7, volume slots: C2 to B4), click reduction, attack compensation, DC offset, HQ resampling, scratching clock; MIDI input port");
        if (isGbItem (item, "gbtools"))
        {
            const auto part = gbToolAt (item, p);
            if (part == 0)
                return tr ("Options: copy, paste, flip, reset or rename the slot; LYNX SHARE's factory slots");
            if (part >= 1 && part <= 7)
                return tr ("Snap: {0} (Alt while editing: the other way)", { tr (gbSnapName (part - 1)) });
            if (part == 8 || part == 9)
                return tr ("Envelope: {0}", { part == 8 ? tr ("time travel") : tr ("volume") });
            if (part == 10) // (FL's hints, capture 190lx GB4)
                return tr ("Step editing (hold shift for pulse mode)");
            if (part == 11)
                return tr ("Slide remaining points");
            if (part == 12)
                return tr ("Freeze editing");
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
            g.setColour (fx.enabled ? colour::textPrimary : colour::textDisabled);
            for (int i = 0; i < 3; ++i)
                g.fillRect (juce::Rectangle<float> (b.getWidth() * 0.5f, 1.6f * s).withCentre ({ b.getCentreX(), b.getCentreY() + (float) (i - 1) * 4.0f * s }));
            return;
        }
        if (id == "gbjog") // a dial with the beat positions (FL's scratch indicator)
        {
            if (gbState().options[7] == 0) // Options › View scratching clock off (round 190ez)
                return;
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
            // round 190oa (DP-8; REA 190oa R5, FL's TScratchWheel paint): turned back from no offset, an arc from the top
            // over raw / 65536 of the turn (white while the dial is held), the indicator at its end
            if (const auto turned = gbScratchValue(); turned > 0.0f)
            {
                const auto turn = 1.0f - turned, end = -juce::MathConstants<float>::twoPi * turn;
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, radius * 0.62f, radius * 0.62f, 0.0f, 0.0f, end, true);
                g.setColour (gbScratching ? juce::Colours::white : (fx.enabled ? accent : colour::textSecondary));
                g.strokePath (arc, juce::PathStrokeType (juce::jmax (1.5f, 2.0f * s)));
                g.fillEllipse (juce::Rectangle<float> (5.0f * s, 5.0f * s).withCentre ({ c.x + std::sin (end) * radius * 0.62f, c.y - std::cos (end) * radius * 0.62f }));
            }
            // round 190lx (DP-9): FL's read-position ring (1 px, ~11 px across) while playing, counter-clockwise with the delay
            if (const auto deg = gbDialAngle (gbLive); deg >= 0.0f)
            {
                const auto a = -juce::degreesToRadians (deg);
                g.setColour (fx.enabled ? accent : colour::textSecondary);
                g.drawEllipse (juce::Rectangle<float> (11.0f * s, 11.0f * s).withCentre ({ c.x + std::sin (a) * radius * 0.85f, c.y - std::cos (a) * radius * 0.85f }),
                               juce::jmax (1.0f, s));
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
            // round 190mf: Step editing (a stepped line with a pen dot), Slide remaining points (an arrow to the right),
            // Freeze editing (a six-armed star): FL's glyphs, drawn by LYNX
            for (int part = 10; part <= 12; ++part)
            {
                const auto lit = part == 10 ? gbStepEdit : part == 11 ? gbSlide : gbFrozen;
                cell (part, lit, {});
                const auto c = gbToolCell (item, part).getCentre();
                const auto u = 4.0f * s;
                g.setColour (lit ? colour::textPrimary : colour::textSecondary);
                juce::Path p;
                if (part == 10)
                {
                    p.startNewSubPath (c.x - u * 1.3f, c.y + u);
                    p.lineTo (c.x - u * 0.3f, c.y + u);
                    p.lineTo (c.x - u * 0.3f, c.y - u * 0.1f);
                    p.lineTo (c.x + u * 0.7f, c.y - u * 0.1f);
                    p.lineTo (c.x + u * 0.7f, c.y - u * 1.0f);
                    g.strokePath (p, juce::PathStrokeType (juce::jmax (1.0f, 1.4f * s)));
                    g.fillEllipse (juce::Rectangle<float> (u * 0.7f, u * 0.7f).withCentre ({ c.x + u * 1.2f, c.y - u * 1.0f }));
                }
                else if (part == 11)
                {
                    g.drawLine (c.x - u * 1.2f, c.y, c.x + u * 0.4f, c.y, juce::jmax (1.2f, 1.8f * s));
                    p.addTriangle (c.x + u * 1.3f, c.y, c.x + u * 0.3f, c.y - u * 0.8f, c.x + u * 0.3f, c.y + u * 0.8f);
                    g.fillPath (p);
                }
                else
                    for (int k = 0; k < 3; ++k)
                    {
                        const auto a = juce::MathConstants<float>::pi * (float) k / 3.0f + juce::MathConstants<float>::halfPi;
                        g.drawLine (c.x - std::cos (a) * u * 1.2f, c.y - std::sin (a) * u * 1.2f, c.x + std::cos (a) * u * 1.2f, c.y + std::sin (a) * u * 1.2f,
                                    juce::jmax (1.0f, 1.3f * s));
                    }
            }
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
                if (! g.clipRegionIntersects (cell.getSmallestIntegerContainer().expanded (2))) // (round 190ih: a playing cell repaints alone)
                    continue;
                const auto& slot = gbState().slots[(std::size_t) (k * grossbeat::slotsPerKind + i)];
                const auto isSel = i == selected, hot = gbHoverCell == k * grossbeat::slotsPerKind + i;
                g.setColour (isSel ? colour.withAlpha (0.22f) : hot ? colour::bg3 : (i / 4 + i) % 2 == 0 ? colour::bg1 : colour::bg2);
                g.fillRoundedRectangle (cell, radius::sm * s);
                if (gbLive.playing && i == gbLive.slot[(std::size_t) k]) // round 190ih: FL's "filled = playing", from the left
                {
                    g.setColour (colour.withAlpha (0.45f));
                    g.fillRoundedRectangle (cell.withWidth (cell.getWidth() * gbLive.position[(std::size_t) k]), radius::sm * s);
                }
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
                if (isSel || (gbLive.playing && i == gbLive.queued[(std::size_t) k])) // (FL: "rectangle = queued for playback")
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
        // round 190mk (DP-7, capture 190mk A1.6): View › "Background gradient" (FL: lighter at the top, ~6 % up and down
        // from the flat ground) and "Invert grid" off (FL: the ground darker, the fine lines lighter than it)
        if (gbBackgroundGradient)
        {
            g.setGradientFill (juce::ColourGradient (colour::bg0.brighter (0.12f), r.getX(), r.getY(), colour::bg0.darker (0.22f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, radius::md * s);
        }
        if (! gbInvertGrid)
        {
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.fillRoundedRectangle (r, radius::md * s);
        }
        if (gbFrozen) // (FL: Freeze editing turns the ground slightly bluer, (52,75,91) -> (45,78,97))
        {
            g.setColour (colour::info.withAlpha (0.05f));
            g.fillRoundedRectangle (r, radius::md * s);
        }
        const auto inner = gbArea (item);
        const auto step = grossbeat::snapStep (gbSnap > 0 ? gbSnap : 3);
        g.setColour (colour::gridBeat.withAlpha (gbInvertGrid ? 0.45f : 0.7f));
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
        // round 190lx (DP-5, FL manual "The single dark 'Safety Line' cuts across the first 4 beats"; capture 190lx GB5
        // fl-gb-env-basic1.png: a 1 px line ~10 / 255 darker than the grid, from "now" at the bar's start to 4 beats back
        // at its end, the slope of a pause): above it the audio is buffered even in the song's first bar. Time slots only.
        if (gbEditKind == 0)
        {
            g.setColour (juce::Colours::black.withAlpha (0.32f)); // (LYNX's darker ground: the same visible step as FL's)
            g.drawLine (juce::Line<float> (gbPos (inner, 0.0, 1.0f), gbPos (inner, grossbeat::barBeats, 1.0f - (float) (grossbeat::barBeats / 8.0))), 1.0f);
        }

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

        const auto pts = gbFrozen ? std::vector<grossbeat::EditPoint>() : grossbeat::editPoints (shown); // (Freeze editing: no points)
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
            if (gbSelected.count (i) != 0) // (FL: a 9 px ring, a dark gap and a 3 px dot)
            {
                g.setColour (hot ? colour::textPrimary : kindColour);
                g.drawEllipse (juce::Rectangle<float> (8.0f * s, 8.0f * s).withCentre (pt), juce::jmax (1.0f, 1.2f * s));
                g.fillEllipse (juce::Rectangle<float> (3.0f * s, 3.0f * s).withCentre (pt));
                continue;
            }
            g.setColour (hot ? colour::textPrimary : kindColour);
            g.fillEllipse (juce::Rectangle<float> ((hot ? 8.0f : 6.0f) * s, (hot ? 8.0f : 6.0f) * s).withCentre (pt));
        }
        if (gbBoxing) // (round 190mk: the Ctrl+drag selection box)
        {
            const auto box = juce::Rectangle<float> (gbBoxFrom, gbBoxTo);
            g.setColour (kindColour.withAlpha (0.12f));
            g.fillRect (box);
            g.setColour (kindColour.withAlpha (0.8f));
            g.drawRect (box, 1.0f);
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

        // round 190ih: the playback locator with its short fading trail (FL, raw/c24); round 190iy: unless the GPU overlay
        // shows it
        if (gbLive.playing && ! gbLocatorOnGpu())
        {
            gbPaintPhase = gbPhaseOf (gbLive); // (round 190jd: the phase of what it draws)
            gbPaintLocator (g, inner, std::floor (gbLocatorX (item, gbLive)));
        }
    }

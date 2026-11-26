    // LYNX --feel190qetest <report.txt> [<shot prefix> [project.(flp|lynx)]] (round 190qe, TOP-NEXT R1 "Feel 1"): off screen
    // through the real components:
    //  - the edge scroll (core/EditorFeel.h): a clip dragged past the Playlist's right edge runs the view at FL's measured
    //    speed in the Pattern layout (≈ 223 px/s 20 px out), flat over time, the clip kept at the edge, stopped by the
    //    release; never before bar 1; a selection box grows into what comes in; the Clip-launch layout's Live-like zone;
    //    the Piano roll's notes (sideways and up), its velocity lane stroke;
    //  - the view stack ("Zoom to the selection" again = back, "Previous view"), the zoom glide (150 ms, ease-out), the
    //    zoom keys' anchor (the selection's centre stays), "Restore selection";
    //  - FL's "Keep labels on screen": a clip whose start is scrolled off has its label at the left edge (on) or none
    //    (off), in the three layouts.
    // With a shot prefix: the Playlist with Keep labels on and off (FL's verify_190qc shots 06 / 07 are the reference), on
    // the project when one is given; @150 with LYNX_SHOT_SCALE=1.5 LYNX_SHOT_SUFFIX=@150. Included in the app class of
    // Main.cpp. Returns true when every check passes; `summary` gets "OK" or the first failure (for --selftest).
    bool runFeel190qeTest (const juce::String& reportPath, const juce::String& prefix, const juce::String& projectPath, juce::String* summary = nullptr)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (lynx::ui::showingForShots(), true);
        using namespace lynx;
        const juce::ScopedValueSetter<bool> held (edgescroll::buttonHeldForTests, true);
        const auto motionBefore = ui::motion::enabled();
        const auto keepBefore = keeplabels::on;
        const auto layoutBefore = layoutstyle::current();
        ui::motion::enabled() = false;
        juce::String text, firstFailure;
        bool allOk = true;
        const auto check = [&] (const juce::String& what, bool ok) {
            text << (ok ? "ok   " : "FAIL ") << what << "\n";
            if (! ok && firstFailure.isEmpty())
                firstFailure = what;
            allOk = allOk && ok;
        };
        // a press, a drag (from the press) and a release, as JUCE delivers them to the component
        const auto event = [] (juce::Component& target, juce::Point<int> p, juce::Point<int> down, juce::ModifierKeys mods, bool dragged) {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, down.toFloat(), now, 1, dragged);
        };
        const auto left = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
        const auto px = [] (double v) { return juce::String (v, 1); };

        // 64 bars; tracks 1-4: one-bar pattern clips every other bar; track 6: a 32-bar clip ("Long lead") from bar 3
        const auto bar = (std::int64_t) 4 * ppq;
        auto song = Song::makeDefault();
        song.mode = PlayMode::song;
        song.patterns.resize (3);
        song.patterns[0].name = "Beat";
        song.patterns[1].name = "Long lead";
        song.patterns[2].name = "Notes";
        for (int k = 0; k < 16; ++k)
        {
            Note n;
            n.channel = 0;
            n.start = k * ppq;
            n.length = ppq / 2;
            n.key = 60 + (k * 5) % 12;
            song.patterns[2].notes.push_back (n);
            song.patterns[0].notes.push_back (n);
        }
        song.playlist.clear();
        for (int t = 1; t <= 4; ++t)
            for (int b = 0; b < 64; b += 2)
            {
                Clip c;
                c.pattern = 0;
                c.track = t;
                c.start = b * bar;
                c.length = bar;
                song.playlist.push_back (c);
            }
        {
            Clip c;
            c.pattern = 1;
            c.track = 6;
            c.start = 3 * bar;
            c.length = 32 * bar;
            song.playlist.push_back (c);
        }
        AudioEngine offline (false);
        offline.setSong (song);
        MainComponent ui (offline, false);
        ui.setSize (1920, 1080);
        offline.dispatchPendingMessages();
        ui.setLayoutStyle (layoutstyle::Style::pattern);
        ui.selectTabForSnapshot (2);
        offline.dispatchPendingMessages();
        auto& pl = ui.getPlaylistForSnapshot();
        const auto ppt = [&pl] { return pl.viewNowForTests().pixelsPerUnit; };
        const auto clipOf = [&offline] (int track, std::int64_t start) {
            const auto& list = offline.getSong().playlist;
            for (int i = 0; i < (int) list.size(); ++i)
                if (list[(std::size_t) i].track == track && list[(std::size_t) i].start == start)
                    return i;
            return -1;
        };
        // runs the view's frames for `ms` at 60 Hz from `t0`, returns the end time
        const auto frames = [] (auto& view, double t0, double ms) {
            for (double t = t0; t <= t0 + ms + 0.01; t += 1000.0 / 60.0)
                view.feelFrameForTests (t);
            return t0 + ms;
        };

        // ---- A. the Playlist, Pattern layout: FL's law ------------------------------------------------------------
        {
            pl.setViewForSnapshot (64.0, 0, 0);
            const auto grid = pl.gridAreaForSnapshot();
            const auto index = clipOf (2, 4 * bar);
            const auto box = pl.clipBoundsForSnapshot (index);
            const auto press = juce::Point<int> (box.getCentreX(), box.getBottom() - 4);
            pl.mouseDown (event (pl, press, press, left, false));
            const auto out = juce::Point<int> (grid.getRight() - 1 + 20, press.y); // 20 px past the right edge
            pl.mouseDrag (event (pl, out.withX (out.x - 200), press, left, true));
            pl.mouseDrag (event (pl, out, press, left, true));
            const auto s0 = pl.viewNowForTests().scroll;
            auto t = frames (pl, 0.0, 1000.0);
            const auto s1 = pl.viewNowForTests().scroll;
            t = frames (pl, t + 1000.0 / 60.0, 1000.0);
            const auto s2 = pl.viewNowForTests().scroll;
            const auto first = (s1 - s0) * ppt(), second = (s2 - s1) * ppt();
            check ("Playlist (Pattern): 20 px past the right edge runs " + px (first) + " px in the first second (FL: 223)", std::abs (first - 223.0) <= 6.0);
            check ("Playlist (Pattern): the same speed in the next second (" + px (second) + " px; FL: no acceleration over time)", std::abs (second - first) <= 6.0);
            // the dragged clip stays at the edge: its grabbed point at the view's right edge (within a snap step)
            const auto& moved = offline.getSong().playlist[(std::size_t) index];
            const auto grabbedX = pl.clipBoundsForSnapshot (index).getX() + (press.x - box.getX());
            check ("Playlist (Pattern): the dragged clip moved with the view and stays at the edge (its grip at " + juce::String (grabbedX) + ", edge "
                       + juce::String (grid.getRight() - 1) + ", clip at bar " + juce::String ((double) moved.start / (double) bar + 1.0, 2) + ")",
                   std::abs (grabbedX - (grid.getRight() - 1)) <= (int) std::ceil (16.0) + 1 && moved.start > 4 * bar);
            pl.mouseUp (event (pl, out, press, {}, true));
            const auto s3 = pl.viewNowForTests().scroll;
            frames (pl, t + 100.0, 500.0);
            check ("Playlist (Pattern): the release stops it", pl.viewNowForTests().scroll == s3 && ! pl.edgeScrollRunning());
            offline.undo();
            offline.dispatchPendingMessages();

            // 3 px inside the edge: still (FL); 1 px past the left edge at bar 1: still (FL stops at the song's start)
            pl.setViewForSnapshot (64.0, 0, 0);
            const auto box2 = pl.clipBoundsForSnapshot (clipOf (1, 2 * bar));
            const auto press2 = juce::Point<int> (box2.getCentreX(), box2.getBottom() - 4);
            pl.mouseDown (event (pl, press2, press2, left, false));
            pl.mouseDrag (event (pl, { grid.getRight() - 4, press2.y }, press2, left, true));
            frames (pl, 10000.0, 500.0);
            check ("Playlist (Pattern): 3 px inside the edge does not scroll (FL)", pl.viewNowForTests().scroll == 0.0);
            pl.mouseDrag (event (pl, { grid.getX() - 30, press2.y }, press2, left, true));
            frames (pl, 11000.0, 500.0);
            check ("Playlist (Pattern): past the left edge at bar 1 the view stays (FL stops at the song's start)", pl.viewNowForTests().scroll == 0.0);
            pl.mouseUp (event (pl, { grid.getX() - 30, press2.y }, press2, {}, true));
            offline.undo();
            offline.dispatchPendingMessages();

            // a selection box (Select tool) dragged past the right edge selects what comes into view
            pl.setViewForSnapshot (64.0, 0, 0);
            pl.selectClipsForSnapshot ({});
            pl.setToolForSnapshot (EditTool::lasso);
            const auto boxFrom = juce::Point<int> (grid.getX() + 96, pl.trackRowForSnapshot (1).getY() + 3); // (bar 2.5: empty)
            pl.mouseDown (event (pl, boxFrom, boxFrom, left, false));
            const auto boxTo = juce::Point<int> (grid.getRight() - 1 + 40, pl.trackRowForSnapshot (2).getBottom() - 3);
            pl.mouseDrag (event (pl, boxTo.withX (grid.getCentreX()), boxFrom, left, true));
            pl.mouseDrag (event (pl, boxTo, boxFrom, left, true));
            frames (pl, 20000.0, 1500.0);
            const auto visibleBars = (double) grid.getWidth() / 64.0;
            const auto farClip = clipOf (2, (std::int64_t) std::ceil (visibleBars / 2.0 + 1.0) * 2 * bar);
            const auto sel = pl.clipSelectionForSnapshot();
            const auto first0 = clipOf (1, 2 * bar);
            check ("Playlist (Pattern): a selection box past the edge selects the clips that came into view (" + juce::String ((int) sel.size())
                       + " clips) and keeps its corner at bar 2.5",
                   farClip >= 0 && std::find (sel.begin(), sel.end(), farClip) != sel.end() && std::find (sel.begin(), sel.end(), first0) != sel.end());
            pl.mouseUp (event (pl, boxTo, boxFrom, {}, true));
            pl.setToolForSnapshot (EditTool::pencil);

            // "Restore selection": the box's selection, then a click on an empty spot clears it, then Restore brings it back
            const auto before = pl.clipSelectionForSnapshot();
            pl.setViewForSnapshot (64.0, 0, 0);
            const auto empty = juce::Point<int> (grid.getX() + 100, pl.trackRowForSnapshot (9).getCentreY());
            pl.setToolForSnapshot (EditTool::lasso);
            pl.mouseDown (event (pl, empty, empty, left, false));
            pl.mouseUp (event (pl, empty, empty, {}, false));
            const auto cleared = pl.clipSelectionForSnapshot().empty();
            pl.runAction ("playlist.restoreSelection");
            check ("Playlist: a click cleared the selection (" + juce::String (cleared ? "yes" : "no") + ") and \"Restore selection\" brings back its "
                       + juce::String ((int) before.size()) + " clips",
                   cleared && ! before.empty() && pl.clipSelectionForSnapshot() == before);
            pl.setToolForSnapshot (EditTool::pencil);
        }

        // ---- B. the view stack, the zoom keys' anchor and the glide (Playlist) --------------------------------------
        // (round 190ro, D-2: the LYNX zoom keys are an extra, "editing.zoomKeysLynx", off as FL in the Pattern layout: on here,
        // FL's way checked after this block)
        {
            const juce::ScopedValueSetter<bool> lynxKeys (zoomanchor::lynxKeys(), true);
            pl.setViewForSnapshot (32.0, 8, 0);
            const auto start = pl.viewNowForTests();
            const auto one = clipOf (6, 3 * bar);
            pl.selectClipsForSnapshot ({ one });
            pl.runAction ("playlist.zoomSelection");
            const auto zoomed = pl.viewNowForTests();
            check ("Playlist: \"Zoom to the selection\" shows the clip (" + juce::String (zoomed.pixelsPerUnit * (double) bar, 1) + " px a bar)",
                   zoomed.pixelsPerUnit > start.pixelsPerUnit && pl.viewStackDepth() == 1);
            pl.runAction ("playlist.zoomSelection");
            check ("Playlist: the same key again goes back to the view before", viewstack::same (pl.viewNowForTests(), start) && pl.viewStackDepth() == 0);
            pl.runAction ("playlist.zoomSelection");
            pl.setScrollTickForSnapshot (pl.viewNowForTests().scroll + (double) bar); // (moved by hand: the key zooms again)
            pl.runAction ("playlist.previousView");
            check ("Playlist: \"Previous view\" goes back whatever the view is now", viewstack::same (pl.viewNowForTests(), start));

            // UX-A2: stopped, the zoom keys keep the selection's centre in place
            pl.setViewForSnapshot (32.0, 0, 0);
            const auto bounds = pl.clipBoundsForSnapshot (one);
            const auto centre = bounds.getX() + bounds.getWidth() * 0.5;
            const auto tickAt = [&pl] (double x) { const auto v = pl.viewNowForTests(); return v.scroll + (x - pl.gridAreaForSnapshot().getX()) / v.pixelsPerUnit; };
            const auto anchorTick = tickAt (centre);
            pl.runAction ("playlist.zoomIn");
            const auto v = pl.viewNowForTests();
            const auto after = pl.gridAreaForSnapshot().getX() + (anchorTick - v.scroll) * v.pixelsPerUnit;
            check ("Playlist: PageUp zooms around the selection's centre (it moved " + px (after - centre) + " px)", std::abs (after - centre) < 1.0);

            // UX-A3: with Animations on the zoom glides over 150 ms, ease-out, and lands on the key's view
            ui::motion::enabled() = true;
            const juce::ScopedValueSetter<bool> glides (glide::inTests, true);
            pl.setViewForSnapshot (32.0, 0, 0);
            pl.selectClipsForSnapshot ({});
            const auto from = pl.viewNowForTests();
            pl.runAction ("playlist.zoomIn");
            const auto running = pl.viewGlideRunning();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            pl.feelFrameForTests (t0 + 40.0);
            const auto mid = pl.viewNowForTests().pixelsPerUnit;
            pl.feelFrameForTests (t0 + 80.0);
            const auto later = pl.viewNowForTests().pixelsPerUnit;
            pl.feelFrameForTests (t0 + 200.0);
            const auto landed = pl.viewNowForTests().pixelsPerUnit;
            check ("Playlist: the zoom key glides (" + juce::String (from.pixelsPerUnit * (double) bar, 2) + " -> " + juce::String (mid * (double) bar, 2) + " -> "
                       + juce::String (later * (double) bar, 2) + " -> " + juce::String (landed * (double) bar, 2) + " px a bar)",
                   running && mid > from.pixelsPerUnit && later > mid && landed > later && std::abs (landed - from.pixelsPerUnit * 1.25) < 1.0e-9
                       && ! pl.viewGlideRunning());
            ui::motion::enabled() = false;
            pl.runAction ("playlist.zoomOut");
            check ("Playlist: with Animations off the zoom key jumps", ! pl.viewGlideRunning() && std::abs (pl.viewNowForTests().pixelsPerUnit - from.pixelsPerUnit) < 1.0e-9);
        }
        // round 190ro (D-2, FL 2026 checked live 11-26): the extra off, as FL in the Pattern layout — the same key again keeps the
        // view, and PageUp with the pointer away from the Playlist keeps the view's left edge
        {
            const juce::ScopedValueSetter<bool> lynxKeys (zoomanchor::lynxKeys(), false);
            pl.setViewForSnapshot (32.0, 8, 0);
            const auto one = clipOf (6, 3 * bar);
            pl.selectClipsForSnapshot ({ one });
            pl.runAction ("playlist.zoomSelection");
            const auto zoomed = pl.viewNowForTests();
            pl.runAction ("playlist.zoomSelection");
            check ("Playlist, FL's zoom keys: Shift+5 again keeps the view (FL 2026)", viewstack::same (pl.viewNowForTests(), zoomed) && pl.viewStackDepth() == 1);
            pl.runAction ("playlist.previousView");
            pl.setViewForSnapshot (32.0, 4, 0);
            const auto before = pl.viewNowForTests();
            pl.runAction ("playlist.zoomIn");
            check ("Playlist, FL's zoom keys: PageUp with the pointer elsewhere keeps the left edge (scroll " + juce::String (before.scroll, 1) + " -> "
                       + juce::String (pl.viewNowForTests().scroll, 1) + ")",
                   std::abs (pl.viewNowForTests().scroll - before.scroll) < 1.0e-6 && pl.viewNowForTests().pixelsPerUnit > before.pixelsPerUnit);
            check ("FL's zoom key anchor: the pointer over the view, else the left edge",
                   zoomanchor::flKeyAnchorPx (250.0, 1000.0) == 250.0 && zoomanchor::flKeyAnchorPx (std::nullopt, 1000.0) == 0.0
                       && zoomanchor::flKeyAnchorPx (-3.0, 1000.0) == 0.0);
            pl.selectClipsForSnapshot ({});
        }

        // ---- C. FL's "Keep labels on screen", three layouts -----------------------------------------------------------
        const auto labelStrip = [&pl, &offline, bar] {
            // the left 120 px of the long clip's row with its start 2 bars left of the view
            pl.setViewForSnapshot (48.0, 5, 0);
            offline.dispatchPendingMessages();
            const auto grid = pl.gridAreaForSnapshot();
            const auto row = pl.trackRowForSnapshot (6);
            juce::ignoreUnused (bar);
            return pl.createComponentSnapshot ({ grid.getX(), row.getY(), 120, row.getHeight() }, true, 1.0f);
        };
        const auto differs = [] (const juce::Image& a, const juce::Image& b) {
            if (a.getBounds() != b.getBounds())
                return true;
            int n = 0;
            for (int y = 0; y < a.getHeight(); ++y)
                for (int x = 0; x < a.getWidth(); ++x)
                    n += a.getPixelAt (x, y) != b.getPixelAt (x, y) ? 1 : 0;
            return n > 20;
        };
        for (const auto layout : { layoutstyle::Style::pattern, layoutstyle::Style::clipLaunch, layoutstyle::Style::simple })
        {
            ui.setLayoutStyle (layout);
            if (layout == layoutstyle::Style::clipLaunch)
            {
                ui.showPanelsForSnapshot ({ workspace::Panel::playlist });
                ui.setClipGridView (PlaylistArea::GridView::song);
            }
            offline.dispatchPendingMessages();
            const juce::String name = layout == layoutstyle::Style::pattern ? "Pattern" : layout == layoutstyle::Style::clipLaunch ? "Clip-launch" : "Simple";
            keeplabels::on = true;
            const auto on = labelStrip();
            keeplabels::on = false;
            pl.repaint();
            const auto off = labelStrip();
            keeplabels::on = true;
            check ("Keep labels on screen (" + name + "): the label of a clip scrolled off shows at the left edge, and not with the option off", differs (on, off));
        }
        ui.setLayoutStyle (layoutstyle::Style::pattern);
        ui.selectTabForSnapshot (2);
        offline.dispatchPendingMessages();

        // ---- D. the Clip-launch layout: the Live-like zone (10 px inside the edge already runs; the Pattern law does not)
        {
            ui.setLayoutStyle (layoutstyle::Style::clipLaunch);
            ui.showPanelsForSnapshot ({ workspace::Panel::playlist });
            ui.setClipGridView (PlaylistArea::GridView::song);
            offline.dispatchPendingMessages();
            auto& p2 = ui.getPlaylistForSnapshot();
            p2.setViewForSnapshot (64.0, 0, 0);
            const auto grid = p2.gridAreaForSnapshot();
            const auto index = clipOf (2, 4 * bar);
            const auto box = p2.clipBoundsForSnapshot (index);
            const auto press = juce::Point<int> (box.getCentreX(), box.getBottom() - 4);
            p2.mouseDown (event (p2, press, press, left, false));
            p2.mouseDrag (event (p2, { grid.getRight() - 11, press.y }, press, left, true));
            frames (p2, 30000.0, 1000.0);
            const auto ran = p2.viewNowForTests().scroll * p2.viewNowForTests().pixelsPerUnit;
            check ("Playlist (Clip-launch): 10 px inside the edge runs (Live-like zone): " + px (ran) + " px in 1 s, between 0.5 and 4 screens",
                   ran >= 0.5 * grid.getWidth() - 2.0 && ran <= 4.0 * grid.getWidth() + 2.0);
            p2.mouseUp (event (p2, { grid.getRight() - 11, press.y }, press, {}, true));
            offline.undo();
            offline.dispatchPendingMessages();
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            ui.selectTabForSnapshot (2);
            offline.dispatchPendingMessages();
        }

        // ---- E. the Piano roll (Pattern layout): notes sideways and up, the velocity lane ---------------------------
        {
            offline.editSong ([] (Song& s) { s.currentPattern = 2; }, false);
            offline.dispatchPendingMessages();
            ui.selectTabForSnapshot (1);
            offline.dispatchPendingMessages();
            auto& roll = ui.getPianoRollForSnapshot();
            roll.resized();
            const auto grid = roll.gridAreaForSnapshot();
            const auto note = offline.getSong().patterns[2].notes[1];
            const auto at = roll.pointForSnapshot (note.key, note.start);
            roll.mouseDown (event (roll, at, at, left, false));
            roll.mouseDrag (event (roll, { grid.getRight() - 1 + 20, at.y }, at, left, true));
            const auto s0 = roll.viewNowForTests().scroll;
            frames (roll, 40000.0, 1000.0);
            const auto ran = (roll.viewNowForTests().scroll - s0) * roll.viewNowForTests().pixelsPerUnit;
            check ("Piano roll (Pattern): a note 20 px past the right edge runs " + px (ran) + " px in 1 s (FL: 183 at 16 px, 223 at 20 px)", std::abs (ran - 223.0) <= 6.0);
            roll.mouseUp (event (roll, { grid.getRight() - 1 + 20, at.y }, at, {}, true));
            offline.undo();
            offline.dispatchPendingMessages();
            // up: a note dragged 37 px above the grid (FL: 396 px/s)
            const auto y0 = roll.viewNowForTests().scrollY;
            roll.mouseDown (event (roll, at, at, left, false));
            roll.mouseDrag (event (roll, { at.x, grid.getY() - 37 }, at, left, true));
            frames (roll, 50000.0, 250.0);
            const auto up = y0 - roll.viewNowForTests().scrollY;
            check ("Piano roll (Pattern): a note 37 px above the grid runs up " + px (up * 4.0) + " px/s (FL: 396)", y0 < 1.0 || std::abs (up * 4.0 - 396.0) <= 30.0);
            roll.mouseUp (event (roll, { at.x, grid.getY() - 37 }, at, {}, true));
            offline.undo();
            offline.dispatchPendingMessages();
            // the velocity lane: a stroke 16 px past the right edge (FL: 176 px/s)
            const auto lane = roll.laneAreaForSnapshot();
            if (! lane.isEmpty())
            {
                const auto from = juce::Point<int> (grid.getX() + 40, lane.getCentreY());
                const auto l0 = roll.viewNowForTests().scroll;
                roll.mouseDown (event (roll, from, from, left, false));
                roll.mouseDrag (event (roll, { grid.getRight() - 1 + 16, lane.getCentreY() }, from, left, true));
                frames (roll, 60000.0, 1000.0);
                const auto laneRan = (roll.viewNowForTests().scroll - l0) * roll.viewNowForTests().pixelsPerUnit;
                check ("Piano roll (Pattern): a velocity lane stroke 16 px past the edge runs " + px (laneRan) + " px in 1 s (FL: 176)", std::abs (laneRan - 186.7) <= 8.0);
                roll.mouseUp (event (roll, { grid.getRight() - 1 + 16, lane.getCentreY() }, from, {}, true));
                offline.undo();
                offline.dispatchPendingMessages();
            }
            else
                check ("Piano roll: the velocity lane is shown", false);
            ui.selectTabForSnapshot (2);
            offline.dispatchPendingMessages();
        }

        // ---- shots: Keep labels on screen on / off (the reference: FL 2026, verify_190qc shots 06 / 07) ---------------
        if (prefix.isNotEmpty())
        {
            if (projectPath.isNotEmpty())
            {
                juce::String error;
                const juce::File project (projectPath);
                const auto ok = project.hasFileExtension ("flp") ? offline.importFlpFile (project, error) : [&] {
                    if (auto s = ProjectFile::load (project, error))
                    {
                        offline.setSong (std::move (*s));
                        return true;
                    }
                    return false;
                }();
                check ("shots: the project " + project.getFileName() + " loads " + error, ok);
                offline.dispatchPendingMessages();
            }
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            ui.selectTabForSnapshot (2);
            offline.dispatchPendingMessages();
            auto& p3 = ui.getPlaylistForSnapshot();
            const auto firstBar = juce::SystemStats::getEnvironmentVariable ("LYNX_FEEL_BAR", projectPath.isNotEmpty() ? "41" : "5").getIntValue();
            const auto perBar = juce::SystemStats::getEnvironmentVariable ("LYNX_FEEL_PPB", "58").getDoubleValue();
            const auto firstTrack = juce::SystemStats::getEnvironmentVariable ("LYNX_FEEL_TRACK", "0").getIntValue();
            for (const auto on : { true, false })
            {
                keeplabels::on = on;
                p3.setViewForSnapshot (perBar, firstBar, firstTrack);
                p3.repaint();
                offline.dispatchPendingMessages();
                p3.resized();
                savePng (shot::snapshot (p3), prefix + (on ? "-keep-labels-on" : "-keep-labels-off") + ".png");
                const auto grid = p3.gridAreaForSnapshot();
                savePng (p3.createComponentSnapshot (juce::Rectangle<int> (grid.getX() - 8, grid.getY(), 420, 400).getIntersection (p3.getLocalBounds()), true,
                                                     2.0f * shot::scale()),
                         prefix + (on ? "-keep-labels-on-zoom" : "-keep-labels-off-zoom") + ".png");
            }
            keeplabels::on = true;
            // the Playlist menu's View submenu (FL 2026: Grid / Content / Time / Track headers, verify_190qc shot 05)
            {
                const auto menu = ui.getPanelMenuForSnapshot (workspace::Panel::playlist);
                for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                    if (it.getItem().text == tr ("View") && it.getItem().subMenu != nullptr)
                    {
                        auto hasKeep = false;
                        for (juce::PopupMenu::MenuItemIterator v (*it.getItem().subMenu); v.next();)
                            hasKeep = hasKeep || (v.getItem().text == tr ("Keep labels on screen") && v.getItem().isTicked);
                        check ("Playlist menu › View › Content › Keep labels on screen, ticked", hasKeep);
                        saveMenus ({ tr ("View") }, { *it.getItem().subMenu }, prefix + "-view-menu.png");
                    }
            }
            text << "shots: " << prefix << "-keep-labels-on / -off (+ -zoom), first bar " << firstBar << ", " << perBar << " px a bar\n";
        }

        keeplabels::on = keepBefore;
        ui::motion::enabled() = motionBefore;
        layoutstyle::setCurrent (layoutBefore);
        text << "result=" << (allOk ? "OK" : "FAIL") << "\n";
        if (reportPath.isNotEmpty())
            juce::File (reportPath).replaceWithText (text);
        if (summary != nullptr)
            *summary = allOk ? juce::String ("OK") : "ERROR " + firstFailure;
        return allOk;
    }

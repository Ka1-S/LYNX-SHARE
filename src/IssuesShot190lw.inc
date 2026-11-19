    // --issueshot190lw <prefix> (round 190lw, issues #205, #206, #207), offscreen through the real components with synthetic
    // mouse events: the Piano roll's row bands of the selected notes (and while a note is pressed and dragged), the mixer
    // slot's effect search (the double-click on an empty slot), and the automation points — a rubber band selection with
    // the Select tool, the group move, Delete, Ctrl+C / Ctrl+V, the pen (Ctrl+drag, thinned). <prefix>-checks.txt lists what
    // was checked. Included in the app class of Main.cpp. @150: --shot-scale 1.5 --shot-suffix @150.
    bool runIssuesShot190lw (const juce::String& prefix)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const char* name, bool ok, const juce::String& detail = {}) {
            report.add (juce::String (name) + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        using namespace lynx;
        ui::motion::enabled() = false;
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
        const juce::ModifierKeys ctrlLeft (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier);
        const juce::ModifierKeys altLeft (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
        const auto pixel = [] (const juce::Image& im, juce::Point<int> p) {
            const auto s = shot::scale();
            return im.getPixelAt ((int) std::lround ((float) p.x * s), (int) std::lround ((float) p.y * s));
        };

        // ================= #205: the Piano roll's row bands =================
        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            song.currentPattern = 0;
            auto& p = song.patterns.front();
            p.notes.clear();
            // a melody, then two chords: C minor (C Eb G) and Ab major (Ab C Eb), as the issue's picture
            const auto add = [&p] (int key, std::int64_t step, std::int64_t steps) {
                Note n;
                n.channel = 4;
                n.start = step * ticksPerStep;
                n.length = steps * ticksPerStep;
                n.key = key;
                p.notes.push_back (n);
            };
            int step = 0;
            for (const auto key : { 67, 70, 72, 74, 75, 74, 72, 70 })
                add (key, step++, 1);
            for (const auto key : { 60, 63, 67 })
                add (key, 8, 8);
            for (const auto key : { 56, 60, 63 })
                add (key, 16, 8);
            p.sortNotes();
            engine.setSong (std::move (song));
            engine.setSelectedChannel (4);
            engine.dispatchPendingMessages();

            PianoRollComponent roll (engine);
            roll.setBounds (0, 0, 1100, 620);
            roll.setVisible (true);
            roll.scrollToNotes (true);
            engine.dispatchPendingMessages();
            const auto& notes = [&]() -> const std::vector<Note>& { return engine.getSong().patterns.front().notes; };
            const auto indexOf = [&] (int key, std::int64_t step) {
                for (int i = 0; i < (int) notes().size(); ++i)
                    if (notes()[(std::size_t) i].key == key && notes()[(std::size_t) i].start == step * ticksPerStep)
                        return i;
                return -1;
            };
            const auto shoot = [&] (const juce::String& name) {
                const auto image = shot::snapshot (roll);
                savePng (image, prefix + name);
                return image;
            };
            // an empty place of a row (right of every note, inside the pattern's view)
            const auto rowSpot = [&] (int key) { return roll.pointForTests (29 * ticksPerStep + ticksPerStep / 2, key); };

            const auto plain = shoot ("-205-01-no-selection.png");
            roll.selectMidiToolResult ({ indexOf (60, 8), indexOf (63, 8), indexOf (67, 8) });
            engine.dispatchPendingMessages();
            const auto banded = shoot ("-205-02-chord-selected.png");
            // the selected keys' rows are lighter across the grid (also far right of the notes); the others are as before
            for (const auto key : { 60, 63, 67 })
            {
                const auto before = pixel (plain, rowSpot (key)), after = pixel (banded, rowSpot (key));
                check (("205_band_row_" + juce::String (key)).toRawUTF8(), after.getBrightness() > before.getBrightness() + 0.02f,
                       before.toDisplayString (false) + " -> " + after.toDisplayString (false) + " at " + rowSpot (key).toString());
            }
            for (const auto key : { 62, 65, 56 })
                check (("205_no_band_row_" + juce::String (key)).toRawUTF8(), pixel (plain, rowSpot (key)) == pixel (banded, rowSpot (key)),
                       pixel (banded, rowSpot (key)).toDisplayString (false));

            // a press on an unselected note keeps its row lit, and the band follows the note while it is dragged
            roll.selectMidiToolResult ({});
            engine.dispatchPendingMessages();
            const auto ev = [&] (juce::Point<int> at, juce::ModifierKeys mods, juce::Point<int> down, bool dragged) {
                return juce::MouseEvent (source, at.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &roll, &roll, now, down.toFloat(), now, 1, dragged);
            };
            const auto grab = roll.pointForTests (16 * ticksPerStep + ticksPerStep * 3, 56); // the middle of the Ab
            roll.mouseMove (ev (grab, {}, grab, false));
            const auto hovered = shoot ("-205-03-hover.png");
            roll.mouseDown (ev (grab, left, grab, false));
            engine.dispatchPendingMessages();
            const auto pressed = shoot ("-205-04-pressed.png");
            check ("205_hover_band_kept_on_press", pixel (pressed, rowSpot (56)).getBrightness() > pixel (plain, rowSpot (56)).getBrightness() + 0.02f,
                   pixel (hovered, rowSpot (56)).toDisplayString (false) + " / " + pixel (pressed, rowSpot (56)).toDisplayString (false));
            const auto to = roll.pointForTests (18 * ticksPerStep + ticksPerStep * 3, 58);
            roll.mouseDrag (ev ((grab + to) / 2, left, grab, true));
            roll.mouseDrag (ev (to, left, grab, true));
            engine.dispatchPendingMessages();
            const auto dragging = shoot ("-205-05-dragging.png");
            check ("205_band_follows_the_drag", pixel (dragging, rowSpot (58)).getBrightness() > pixel (plain, rowSpot (58)).getBrightness() + 0.02f
                                                   && pixel (dragging, rowSpot (56)) == pixel (plain, rowSpot (56)),
                   pixel (dragging, rowSpot (58)).toDisplayString (false));
            roll.mouseUp (ev (to, {}, grab, true));
            engine.dispatchPendingMessages();
        }

        // ================= #206: the mixer slot's effect search =================
        {
            AudioEngine engine (false);
            engine.setSong (Song::makeDefault());
            engine.dispatchPendingMessages();
            EffectsPanel panel (engine);
            panel.setSize (300, 700);
            panel.setTrack (1);
            engine.dispatchPendingMessages();
            auto search = panel.makeSlotSearch (2);
            search->setVisible (true);
            engine.dispatchPendingMessages();
            savePng (shot::snapshot (*search), prefix + "-206-01-slot-search.png");
            juce::TextEditor* box = nullptr;
            int visibleLists = 0;
            for (auto* child : search->getChildren())
            {
                if (auto* t = dynamic_cast<juce::TextEditor*> (child))
                    box = t;
                if (dynamic_cast<juce::ListBox*> (child) != nullptr && child->isVisible())
                    ++visibleLists;
            }
            check ("206_search_box_and_one_list", box != nullptr && visibleLists == 1, juce::String (visibleLists));
            if (box != nullptr)
            {
                box->setText ("limit", true);
                static_cast<juce::Component*> (box)->handleCommandMessage (0x10003001); // (JUCE posts the change: TextEditorDefs::textChangeMessageId, delivered now)
                engine.dispatchPendingMessages();
                savePng (shot::snapshot (*search), prefix + "-206-02-typed.png");
                search->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                engine.dispatchPendingMessages();
            }
            const auto& fx = engine.getSong().mixer[1].effects;
            check ("206_effect_lands_in_the_slot", fx.size() == 1 && juce::String (effectInfo (fx[0].kind).name).containsIgnoreCase ("limit"),
                   fx.empty() ? juce::String() : juce::String (effectInfo (fx[0].kind).name));
        }

        // ================= #207: automation points =================
        {
            AudioEngine engine (false);
            MainComponent ui (engine, false);
            ui.setSize (1600, 900);
            ui.setLayoutStyle (layoutstyle::Style::pattern, false);
            const auto bar = (std::int64_t) 4 * ppq;
            {
                auto song = Song::makeDefault();
                song.playlist.clear();
                song.playlistTracks.resize (8);
                song.playlistTracks[1].height = 120;
                song.playlistTracks[3].height = 120;
                Automation a;
                a.name = "Master Volume";
                a.target = { AutomationTargetKind::mixerVolume, 0, 0, 0 };
                for (int k = 0; k <= 8; ++k)
                    a.points.push_back ({ (std::int64_t) k * bar / 2, k % 2 == 0 ? 0.25f : 0.75f, 0.0f });
                song.automations.push_back (a);
                Automation b = a;
                b.name = "Filter cutoff";
                b.points = { { 0, 0.5f, 0.0f }, { 4 * bar, 0.5f, 0.0f } };
                song.automations.push_back (b);
                Clip c;
                c.kind = ClipKind::automation;
                c.automation = 0;
                c.track = 1;
                c.start = 0;
                c.length = 4 * bar;
                song.playlist.push_back (c);
                c.automation = 1;
                c.track = 3;
                song.playlist.push_back (c);
                engine.setSong (std::move (song));
            }
            engine.dispatchPendingMessages();
            ui.selectTabForSnapshot (2);
            engine.dispatchPendingMessages();
            auto& pl = ui.getPlaylistForSnapshot();
            pl.setSize (juce::jmax (pl.getWidth(), 1100), juce::jmax (pl.getHeight(), 640));
            pl.setViewForSnapshot (200.0, 0, 0);
            engine.dispatchPendingMessages();
            const auto ev = [&] (juce::Point<int> at, juce::ModifierKeys mods, juce::Point<int> down, bool dragged) {
                return juce::MouseEvent (source, at.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pl, &pl, now, down.toFloat(), now, 1, dragged);
            };
            const auto shootPl = [&] (const juce::String& name) {
                engine.dispatchPendingMessages();
                const auto bottom = pl.trackRowForSnapshot (5).getY();
                savePng (shot::snapshot (pl, juce::Rectangle<int> (0, 0, juce::jmin (pl.getWidth(), 1100), juce::jmin (pl.getHeight(), bottom + 8))), prefix + name);
            };
            const auto points = [&] (int automation) { return engine.getSong().automations[(std::size_t) automation].points; };
            // a drag in steps (each a mouse event), with a picture in the middle
            const auto dragPath = [&] (std::vector<juce::Point<int>> path, juce::ModifierKeys mods, std::function<void()> mid = nullptr) {
                pl.mouseMove (ev (path.front(), mods.withoutMouseButtons(), path.front(), false));
                pl.mouseDown (ev (path.front(), mods, path.front(), false));
                for (std::size_t k = 1; k < path.size(); ++k)
                {
                    pl.mouseDrag (ev (path[k], mods, path.front(), true));
                    if (k == path.size() / 2 && mid)
                        mid();
                }
                pl.mouseUp (ev (path.back(), mods.withoutMouseButtons(), path.front(), path.size() > 1));
                engine.dispatchPendingMessages();
            };
            shootPl ("-207-01-clips.png");

            // the Select tool's rubber band over points 2..5 of the first clip
            pl.setToolForSnapshot (EditTool::lasso);
            const auto p2 = pl.curvePointForTests (0, 2), p5 = pl.curvePointForTests (0, 5);
            const juce::Point<int> from (p2.x - 6, pl.curveSpotXYForTests (0, 0, 0.95f).y), to (p5.x + 6, pl.curveSpotXYForTests (0, 0, 0.05f).y);
            dragPath ({ from, (from + to) / 2, to }, left, [&] { shootPl ("-207-02-band.png"); });
            check ("207_band_selects_points", pl.selectedPointsForTests() == std::vector<int> ({ 2, 3, 4, 5 }),
                   juce::String ((int) pl.selectedPointsForTests().size()));
            shootPl ("-207-03-selected.png");

            // a drag on a selected point moves the group (right by a quarter bar, keeping its shape)
            const auto before = points (0);
            const auto grab = pl.curvePointForTests (0, 3);
            const auto quarter = (int) std::lround (pl.xOfTickForSnapshot ((double) bar / 4) - pl.xOfTickForSnapshot (0.0));
            dragPath ({ grab, grab + juce::Point<int> (quarter / 2, 0), grab + juce::Point<int> (quarter, 0) }, left);
            {
                const auto after = points (0);
                bool moved = after.size() == before.size();
                for (int i = 2; moved && i <= 5; ++i)
                    moved = after[(std::size_t) i].tick == before[(std::size_t) i].tick + bar / 4
                            && std::abs (after[(std::size_t) i].value - before[(std::size_t) i].value) < 0.02f;
                check ("207_group_move", moved && after[1].tick == before[1].tick && after[6].tick == before[6].tick);
            }
            shootPl ("-207-04-moved.png");

            // Ctrl+C, then Ctrl+V with the mouse on the second clip's curve: pasted there, selected
            pl.handleKey (juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0));
            const auto target = pl.curveSpotXYForTests (1, bar * 2, 0.5f);
            pl.mouseMove (ev (target, {}, target, false));
            pl.handleKey (juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0));
            engine.dispatchPendingMessages();
            check ("207_paste_on_other_curve", points (1).size() == 6 && pl.selectedPointsForTests().size() == 4, juce::String ((int) points (1).size()));
            shootPl ("-207-05-pasted.png");

            // Delete deletes the selected (pasted) points
            pl.handleKey (juce::KeyPress (juce::KeyPress::deleteKey));
            engine.dispatchPendingMessages();
            check ("207_delete_points", points (1).size() == 2 && pl.selectedPointsForTests().empty(), juce::String ((int) points (1).size()));

            // the pen: Alt+drag on the second clip draws a wave freehand, thinned on release (FL leaves Alt free there)
            pl.setToolForSnapshot (EditTool::pencil);
            std::vector<juce::Point<int>> stroke;
            for (int k = 0; k <= 120; ++k)
            {
                const auto tick = bar / 2 + (std::int64_t) k * (3 * bar / 2) / 120;
                stroke.push_back (pl.curveSpotXYForTests (1, tick, 0.5f + 0.35f * std::sin ((float) k / 12.0f)));
            }
            check ("207_pen_cursor_over_curve", pl.penCursorForTests (stroke.front()),
                   stroke.front().toString() + " row " + pl.trackRowForSnapshot (3).toString() + " p2 " + p2.toString());
            std::size_t raw = 0;
            dragPath (stroke, altLeft, [&] { raw = points (1).size(); shootPl ("-207-06-pen-drawing.png"); });
            const auto thinned = points (1).size();
            check ("207_pen_draws_and_thins", raw > 30 && thinned < raw && thinned >= 8, juce::String ((int) raw) + " -> " + juce::String ((int) thinned));
            shootPl ("-207-07-pen-thinned.png");
            // Ctrl+click keeps its meaning: the clip into the selection (no pen on Ctrl over a clip: FL's rubber band)
            const auto spot = pl.curveSpotXYForTests (1, bar * 3, 0.2f);
            dragPath ({ spot }, ctrlLeft);
            check ("207_ctrl_click_selects_clip", pl.clipSelectionForTests() == std::vector<int> ({ 1 }), juce::String ((int) pl.clipSelectionForTests().size()));

            // the Clip-launch layout's automation lane (Live): a drag in its background selects the points in that time
            // range; Ctrl+drag is the pen there; Delete deletes the selection
            pl.setToolForSnapshot (EditTool::pencil);
            ui.setLayoutStyle (layoutstyle::Style::clipLaunch, false);
            engine.editSong ([bar] (Song& s) {
                Automation a;
                a.name = "Reverb send";
                a.target = { AutomationTargetKind::mixerVolume, 2, 0, 0 };
                for (int k = 0; k <= 8; ++k)
                    a.points.push_back ({ (std::int64_t) k * bar / 2, k % 2 == 0 ? 0.3f : 0.6f, 0.0f });
                s.automations.push_back (a);
                s.playlistTracks[5].lanes.push_back ({ (int) s.automations.size() - 1, 110 });
            });
            autolanes::setShown (true);
            pl.setViewForSnapshot (200.0, 0, 0);
            engine.dispatchPendingMessages();
            const auto laneA = (int) engine.getSong().automations.size() - 1;
            const auto body = pl.laneBodyForTests (5, 0);
            const auto laneXY = [&] (std::int64_t tick, float value) {
                return juce::Point<int> ((int) std::lround (pl.xOfTickForSnapshot ((double) tick)), (int) std::lround (body.getBottom() - value * body.getHeight()));
            };
            const auto shootLane = [&] (const juce::String& name) {
                engine.dispatchPendingMessages();
                savePng (shot::snapshot (pl, juce::Rectangle<int> (0, 0, juce::jmin (pl.getWidth(), 1100), juce::jmin (pl.getHeight(), (int) body.getBottom() + 40))), prefix + name);
            };
            check ("207_live_lane_shown", body.getHeight() > 20.0f, body.toString());
            const auto f0 = laneXY (bar * 2 / 5, 0.95f), f1 = laneXY (bar * 13 / 5, 0.95f);
            dragPath ({ f0, (f0 + f1) / 2, f1 }, left, [&] { shootLane ("-207-08-live-lane-band.png"); });
            check ("207_live_lane_background_drag_selects_time_range", pl.selectedPointsForTests() == std::vector<int> ({ 1, 2, 3, 4, 5 }),
                   juce::String ((int) pl.selectedPointsForTests().size()));
            shootLane ("-207-09-live-lane-selected.png");
            pl.handleKey (juce::KeyPress (juce::KeyPress::deleteKey));
            engine.dispatchPendingMessages();
            check ("207_live_lane_delete", points (laneA).size() == 4, juce::String ((int) points (laneA).size()));
            std::vector<juce::Point<int>> laneStroke;
            for (int k = 0; k <= 80; ++k)
                laneStroke.push_back (laneXY (bar / 2 + (std::int64_t) k * bar / 40, 0.5f + 0.3f * std::sin ((float) k / 8.0f)));
            dragPath (laneStroke, ctrlLeft);
            check ("207_live_lane_ctrl_pen", points (laneA).size() > 8, juce::String ((int) points (laneA).size()));
            shootLane ("-207-10-live-lane-pen.png");
            autolanes::setShown (false);
            ui.setLayoutStyle (layoutstyle::Style::pattern, false);
        }

        juce::File (prefix + "-checks.txt").replaceWithText (report.joinIntoString ("\n") + "\n" + (allOk ? "ALL OK" : "SOME FAILED") + "\n");
        return allOk;
    }

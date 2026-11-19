    // --issueshot190mq <prefix> (round 190mq, issues #211, #212, #213), offscreen through the real components with
    // synthetic mouse and key events: Ctrl+V into the part in view (Piano roll and Playlist, per layout), the Piano roll's
    // rows with a scale in the issue's colours, and the ruler's double click that clears the time selection.
    // <prefix>-checks.txt lists what was checked. Included in the app class of Main.cpp. @150: --shot-scale 1.5
    // --shot-suffix @150.
    bool runIssuesShot190mq (const juce::String& prefix)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const juce::String& name, bool ok, const juce::String& detail = {}) {
            report.add (name + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        using namespace lynx;
        ui::motion::enabled() = false;
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
        const juce::ModifierKeys ctrlLeft (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier);
        const auto pixel = [] (const juce::Image& im, juce::Point<int> p) {
            const auto s = shot::scale();
            return im.getPixelAt ((int) std::lround ((float) p.x * s), (int) std::lround ((float) p.y * s));
        };
        const auto bar = (std::int64_t) 4 * ppq;
        const auto beat = (std::int64_t) ppq;

        // a song whose pattern 0 has two notes on channel 0: bar 1 beat 3 and bar 2 beat 1 (the clipboard's source)
        const auto noteSong = [&] {
            auto song = Song::makeDefault();
            song.currentPattern = 0;
            auto& p = song.patterns.front();
            p.notes.clear();
            for (const auto& [start, key] : { std::pair<std::int64_t, int> { 2 * beat, 72 }, { bar, 67 } })
            {
                Note n;
                n.channel = 0;
                n.start = start;
                n.length = beat;
                n.key = key;
                p.notes.push_back (n);
            }
            // (a note of another channel at bar 41 makes the pattern long enough to scroll far right)
            Note far;
            far.channel = 1;
            far.start = 40 * bar;
            far.length = beat;
            far.key = 48;
            p.notes.push_back (far);
            p.sortNotes();
            return song;
        };

        // ================= #211: Ctrl+V in the Piano roll =================
        for (const auto style : { layoutstyle::Style::pattern, layoutstyle::Style::clipLaunch, layoutstyle::Style::simple })
        {
            layoutstyle::setCurrent (style);
            const juce::String tag = layoutstyle::id (style);
            AudioEngine engine (false);
            engine.setSong (noteSong());
            engine.setSelectedChannel (0);
            engine.dispatchPendingMessages();
            PianoRollComponent roll (engine);
            roll.setBounds (0, 0, 1100, 520);
            roll.setVisible (true);
            roll.setViewForTests (0.0, 14, 70);
            engine.editSong ([] (Song&) {}); // (the editor's change listener learns the pattern first, as in use)
            engine.dispatchPendingMessages();
            roll.selectAll();
            roll.handleKey (juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0));
            const auto notes = [&]() -> const std::vector<Note>& { return engine.getSong().patterns.front().notes; };
            const auto pasted = [&] {
                std::vector<std::int64_t> starts;
                for (auto i : roll.selectionForTests())
                    starts.push_back (notes()[(std::size_t) i].start);
                std::sort (starts.begin(), starts.end());
                return starts;
            };
            const auto paste = [&] {
                roll.handleKey (juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0));
                engine.dispatchPendingMessages();
                return pasted();
            };
            const auto show = [] (const std::vector<std::int64_t>& v) {
                juce::String s;
                for (auto t : v)
                    s << (s.isEmpty() ? "" : ",") << juce::String ((double) t / (double) ppq, 2) << "q";
                return s;
            };

            if (style == layoutstyle::Style::pattern)
            {
                using V = std::vector<std::int64_t>;
                const auto grid = roll.gridForSnapshot();
                // FL rule 3: the view from bar 1 (nearest bar line: bar 1) would paste the notes onto themselves, so one
                // grid step later
                auto got = paste();
                check ("211_pr_pattern_in_place_moves_a_step", got == V ({ 2 * beat + grid, bar + grid }), show (got) + " grid " + juce::String (grid));
                // FL rule 2: scrolled to bar 9 (the left edge on its line): bar 9 beat 3 and bar 10 beat 1
                roll.setViewForTests ((double) (8 * bar), 14, 70);
                savePng (shot::snapshot (roll), prefix + "-211-pr-01-before-paste.png");
                got = paste();
                savePng (shot::snapshot (roll), prefix + "-211-pr-02-pasted-bar9.png");
                check ("211_pr_pattern_view_bar9", got == V ({ 8 * bar + 2 * beat, 9 * bar }), show (got));
                // the left edge a beat into bar 13 (under half a bar): bar 13 beat 3, in view
                roll.setViewForTests ((double) (12 * bar + beat), 14, 70);
                got = paste();
                check ("211_pr_pattern_view_bar13_beat2", got == V ({ 12 * bar + 2 * beat, 13 * bar }), show (got));
                // the left edge past half of bar 17 (FL's f23: 2.03 beats in): the next bar line, bar 18
                roll.setViewForTests ((double) (16 * bar + 2 * beat + beat / 8), 14, 70);
                got = paste();
                savePng (shot::snapshot (roll), prefix + "-211-pr-03-pasted-next-bar.png");
                check ("211_pr_pattern_view_past_half_next_bar", got == V ({ 17 * bar + 2 * beat, 18 * bar }), show (got));
                // FL rule 1: a click on the ruler in view (bar 18 beat 2) is the anchor: the first note lands on it
                const auto ev = [&] (juce::Point<int> p, juce::ModifierKeys mods, int clicks) {
                    return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &roll, &roll, now, p.toFloat(), now, clicks, false);
                };
                const auto rulerAt = [&] (std::int64_t tick) { return juce::Point<int> (roll.pointForTests (tick, 60).x + 1, roll.rulerYForTests()); };
                const auto click = [&] (std::int64_t tick, int clicks) {
                    roll.mouseDown (ev (rulerAt (tick), left, clicks));
                    roll.mouseUp (ev (rulerAt (tick), {}, clicks));
                    engine.dispatchPendingMessages();
                };
                click (17 * bar + beat, 1);
                got = paste();
                savePng (shot::snapshot (roll), prefix + "-211-pr-04-pasted-at-ruler-click.png");
                check ("211_pr_pattern_at_ruler_click", got == V ({ 17 * bar + beat, 17 * bar + beat + (bar - 2 * beat) }), show (got));
                // the anchor scrolled out of view: the view rule again (bar 25 beat 3)
                roll.setViewForTests ((double) (24 * bar), 14, 70);
                got = paste();
                check ("211_pr_pattern_anchor_out_of_view", got == V ({ 24 * bar + 2 * beat, 25 * bar }), show (got));
                // the ruler's double click drops the anchor: back at bar 18, the view rule (bar 18 beat 3... next line)
                roll.setViewForTests ((double) (16 * bar + 2 * beat + beat / 8), 14, 70);
                click (17 * bar + beat, 1);
                click (17 * bar + beat, 2);
                got = paste();
                check ("211_pr_pattern_double_click_drops_anchor", got == V ({ 17 * bar + 2 * beat, 18 * bar }), show (got));
                // a time selection in view: its start
                roll.setTimeSelectionForTests (17 * bar + 3 * beat, 18 * bar);
                got = paste();
                check ("211_pr_pattern_at_time_selection", got == V ({ 17 * bar + 3 * beat, 17 * bar + 3 * beat + (bar - 2 * beat) }), show (got));
            }
            else
            {
                // Live (insert marker) / BandLab (playhead): the song position, here bar 5 beat 2 of the pattern
                roll.setViewForTests ((double) (3 * bar), 14, 70);
                engine.editSong ([] (Song& s) { s.mode = PlayMode::pattern; });
                engine.seek ((double) (4 * bar + beat));
                engine.getTransport().positionTicks.store ((double) (4 * bar + beat)); // (offline: no audio thread applies the seek)
                roll.frameStepForSnapshot();
                engine.dispatchPendingMessages();
                const auto got = paste();
                check ("211_pr_" + tag + "_at_position", got == std::vector<std::int64_t> ({ 4 * bar + beat, 4 * bar + beat + (bar - 2 * beat) }),
                       show (got) + " playhead " + juce::String (roll.playheadForSnapshot() / ppq, 2) + "q");
                savePng (shot::snapshot (roll), prefix + "-211-pr-" + tag + ".png");
            }
        }
        layoutstyle::setCurrent (layoutstyle::Style::pattern);

        // ================= #211: Ctrl+V in the Playlist =================
        for (const auto style : { layoutstyle::Style::pattern, layoutstyle::Style::clipLaunch, layoutstyle::Style::simple })
        {
            layoutstyle::setCurrent (style);
            const juce::String tag = layoutstyle::id (style);
            AudioEngine engine (false);
            {
                auto song = noteSong();
                song.playlist.clear();
                Clip c;
                c.kind = ClipKind::pattern;
                c.pattern = 0;
                c.track = 1;
                c.start = 2 * beat; // bar 1 beat 3
                c.length = bar;
                song.playlist.push_back (c);
                c.track = 2;
                c.start = bar; // bar 2
                song.playlist.push_back (c);
                engine.setSong (std::move (song));
            }
            engine.dispatchPendingMessages();
            PlaylistComponent pl (engine);
            pl.setBounds (0, 0, 1100, 420);
            pl.setVisible (true);
            pl.setViewForSnapshot (80.0, 0, 0);
            engine.editSong ([] (Song&) {}); // (the editor's change listener learns the song first, as in use)
            engine.dispatchPendingMessages();
            pl.handleKey (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0));
            pl.handleKey (juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0));
            // (Ctrl+A also makes a time selection from bar 1, as in FL (capture p01): no anchor, FL's f04)
            // a clip at bar 41 (after the copy) makes the song long enough to scroll far right
            engine.editSong ([bar] (Song& s) {
                Clip far;
                far.kind = ClipKind::pattern;
                far.pattern = 0;
                far.track = 6;
                far.start = 40 * bar;
                far.length = bar;
                s.playlist.push_back (far);
            });
            engine.dispatchPendingMessages();
            const auto pasted = [&] {
                std::vector<std::pair<std::int64_t, int>> out;
                for (auto i : pl.clipSelectionForTests())
                    out.push_back ({ engine.getSong().playlist[(std::size_t) i].start, engine.getSong().playlist[(std::size_t) i].track });
                std::sort (out.begin(), out.end());
                return out;
            };
            const auto paste = [&] {
                pl.handleKey (juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0));
                engine.dispatchPendingMessages();
                return pasted();
            };
            const auto show = [] (const std::vector<std::pair<std::int64_t, int>>& v) {
                juce::String s;
                for (auto [t, track] : v)
                    s << (s.isEmpty() ? "" : ",") << juce::String ((double) t / (double) ppq, 2) << "q@" << track;
                return s;
            };
            using Got = std::vector<std::pair<std::int64_t, int>>;
            if (style == layoutstyle::Style::pattern)
            {
                const auto grid = pl.gridForSnapshot();
                // in view from bar 1: onto themselves, so one grid step later (FL rule 3)
                auto got = paste();
                check ("211_pl_pattern_in_place_moves_a_step", got == Got ({ { 2 * beat + grid, 1 }, { bar + grid, 2 } }), show (got) + " grid " + juce::String (grid));
                // scrolled to bar 9: bar 9 beat 3 / bar 10, same tracks
                pl.setScrollTickForSnapshot ((double) (8 * bar));
                engine.dispatchPendingMessages();
                savePng (shot::snapshot (pl), prefix + "-211-pl-01-before-paste.png");
                got = paste();
                savePng (shot::snapshot (pl), prefix + "-211-pl-02-pasted-bar9.png");
                check ("211_pl_pattern_view_bar9", got == Got ({ { 8 * bar + 2 * beat, 1 }, { 9 * bar, 2 } }), show (got));
                // past half of bar 16: bar 17 (FL's p09: left edge 15.875 -> bar 16)
                pl.setScrollTickForSnapshot ((double) (15 * bar + 3 * beat));
                got = paste();
                check ("211_pl_pattern_past_half_next_bar", got == Got ({ { 16 * bar + 2 * beat, 1 }, { 17 * bar, 2 } }), show (got));
                // a click on the ruler in view: the anchor
                const auto y = PlaylistComponent::rulerTop + PlaylistComponent::timelineHeight / 2;
                const auto ev = [&] (std::int64_t tick, juce::ModifierKeys mods, int clicks) {
                    const juce::Point<float> p ((float) pl.xOfTickForSnapshot ((double) tick) + 1.0f, (float) y);
                    return juce::MouseEvent (source, p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pl, &pl, now, p, now, clicks, false);
                };
                const auto click = [&] (std::int64_t tick, int clicks) {
                    pl.mouseDown (ev (tick, left, clicks));
                    pl.mouseUp (ev (tick, {}, clicks));
                    engine.dispatchPendingMessages();
                };
                click (20 * bar, 1);
                got = paste();
                savePng (shot::snapshot (pl), prefix + "-211-pl-03-pasted-at-ruler-click.png");
                check ("211_pl_pattern_at_ruler_click", got == Got ({ { 20 * bar, 1 }, { 20 * bar + (bar - 2 * beat), 2 } }), show (got));
                // the double click drops it (and deselects): the view rule
                click (20 * bar, 1);
                click (20 * bar, 2);
                check ("213_pl_double_click_deselects_clips", pl.clipSelectionForTests().empty());
                got = paste();
                check ("211_pl_pattern_double_click_drops_anchor", got == Got ({ { 16 * bar + 2 * beat, 1 }, { 17 * bar, 2 } }), show (got));
            }
            else
            {
                pl.setScrollTickForSnapshot ((double) (3 * bar));
                pl.runAction ("playlist.deselect"); // (Ctrl+A's time selection would be Live's insert point)
                engine.seek ((double) (4 * bar)); // (on a bar line: the Playlist snaps the position to its grid)
                engine.getTransport().positionTicks.store ((double) (4 * bar)); // (offline: no audio thread applies the seek)
                pl.frameStepForSnapshot();
                engine.dispatchPendingMessages();
                const auto got = paste();
                check ("211_pl_" + tag + "_at_position", got == Got ({ { 4 * bar, 1 }, { 4 * bar + (bar - 2 * beat), 2 } }), show (got));
                savePng (shot::snapshot (pl), prefix + "-211-pl-" + tag + ".png");
            }
        }
        layoutstyle::setCurrent (layoutstyle::Style::pattern);

        // ================= #212: the rows with a scale, in the issue's colours =================
        {
            const auto saved = theme::currentPalette();
            // the issue's pictures: rows #191e26 / #13171e = the palette derived from the background #12161d
            theme::setPalette (themes::derivePalette (0xffe5383b, 0xff12161d));
            AudioEngine engine (false);
            engine.setSong (noteSong());
            engine.setSelectedChannel (0);
            engine.dispatchPendingMessages();
            PianoRollComponent roll (engine);
            roll.setBounds (0, 0, 520, 330);
            roll.setVisible (true);
            roll.setViewForTests (16.0 * bar, 8, 102);
            engine.dispatchPendingMessages();
            const auto spot = [&] (int key) { return roll.pointForTests (16 * bar + beat + ticksPerStep / 2, key); }; // (between step lines)
            const auto grid = PianoRollComponent::gridHighlights;
            PianoRollComponent::gridHighlights = true;
            roll.setScaleForSnapshot (0, 0);
            const auto none = shot::snapshot (roll);
            savePng (none, prefix + "-212-01-no-scale.png");
            roll.setScaleForSnapshot (0, 2); // C natural minor, as the issue's picture
            engine.dispatchPendingMessages();
            const auto minor = shot::snapshot (roll);
            savePng (minor, prefix + "-212-02-c-minor.png");
            // in the scale: D# (black key) and D (white key) the same light row; out of it: C# (black) and E (white) the same dark row
            check ("212_scale_rows_ignore_key_colour", pixel (minor, spot (99)) == pixel (minor, spot (98)) && pixel (minor, spot (97)) == pixel (minor, spot (100))
                                                         && pixel (minor, spot (98)) != pixel (minor, spot (100)),
                   pixel (minor, spot (99)).toDisplayString (false) + "/" + pixel (minor, spot (98)).toDisplayString (false) + " vs "
                       + pixel (minor, spot (97)).toDisplayString (false) + "/" + pixel (minor, spot (100)).toDisplayString (false));
            PianoRollComponent::gridHighlights = false;
            roll.repaint();
            const auto off = shot::snapshot (roll);
            savePng (off, prefix + "-212-03-c-minor-highlights-off.png");
            check ("212_highlights_off_rows_flat", pixel (off, spot (99)) == pixel (off, spot (100)), pixel (off, spot (99)).toDisplayString (false));
            PianoRollComponent::gridHighlights = true;
            // FL's Scale highlighting › Default (no scale, no black-key rows) and back to Black notes ("No scale")
            const auto menu = roll.getScaleHighlightMenuForSnapshot();
            std::function<void()> pickDefault, pickBlack;
            bool defaultTicked = false;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            {
                const auto& item = it.getItem();
                if (item.text == tr ("Default"))
                    pickDefault = item.action;
                if (item.text == tr (notetools::scales()[0].name))
                    pickBlack = item.action;
            }
            check ("212_menu_has_default_and_black_notes", pickDefault != nullptr && pickBlack != nullptr);
            if (pickDefault)
                pickDefault();
            engine.dispatchPendingMessages();
            const auto after = roll.getScaleHighlightMenuForSnapshot(); // (kept alive while the iterator walks it)
            for (juce::PopupMenu::MenuItemIterator it (after); it.next();)
                if (it.getItem().text == tr ("Default"))
                    defaultTicked = it.getItem().isTicked;
            const auto plain = shot::snapshot (roll);
            savePng (plain, prefix + "-212-04-default-no-rows.png");
            check ("212_default_no_black_key_rows", defaultTicked && pixel (plain, spot (97)) == pixel (plain, spot (98)) && roll.scaleIndexForTests() == 0,
                   pixel (plain, spot (97)).toDisplayString (false) + "/" + pixel (plain, spot (98)).toDisplayString (false));
            saveMenus (juce::StringArray { tr ("Scale highlighting") }, std::vector<juce::PopupMenu> { roll.getScaleHighlightMenuForSnapshot() },
                       prefix + "-212-05-scale-menu.png");
            // a scale chosen after Default shades by the scale as before
            roll.setScaleForSnapshot (0, 2);
            check ("212_scale_after_default", pixel (shot::snapshot (roll), spot (99)) == pixel (minor, spot (99))
                                                  && pixel (shot::snapshot (roll), spot (100)) == pixel (minor, spot (100)));
            if (pickBlack)
                pickBlack();
            check ("212_black_notes_back", PianoRollComponent::blackKeyRows && pixel (shot::snapshot (roll), spot (97)) == pixel (none, spot (97)));
            PianoRollComponent::gridHighlights = grid;
            PianoRollComponent::blackKeyRows = true;
            theme::setPalette (saved);
        }

        // ================= #213: a double click on the ruler clears the time selection =================
        {
            AudioEngine engine (false);
            engine.setSong (noteSong());
            engine.setSelectedChannel (0);
            engine.dispatchPendingMessages();
            PianoRollComponent roll (engine);
            roll.setBounds (0, 0, 1100, 420);
            roll.setVisible (true);
            roll.setViewForTests (0.0, 14, 70);
            engine.dispatchPendingMessages();
            const auto y = roll.rulerYForTests();
            const auto at = [&] (std::int64_t tick) { return juce::Point<int> (roll.pointForTests (tick, 60).x, y); };
            const auto ev = [&] (juce::Point<int> p, juce::ModifierKeys mods, juce::Point<int> down, bool dragged, int clicks) {
                return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &roll, &roll, now, down.toFloat(), now, clicks, dragged);
            };
            const auto click = [&] (juce::Point<int> p, juce::ModifierKeys mods, int clicks) {
                roll.mouseDown (ev (p, mods, p, false, clicks));
                roll.mouseUp (ev (p, mods.withoutMouseButtons(), p, false, clicks));
                engine.dispatchPendingMessages();
            };
            // Ctrl+drag on the ruler: bar 2 to bar 3
            {
                const auto a = at (bar + beat / 2), b = at (2 * bar + beat / 2);
                roll.mouseDown (ev (a, ctrlLeft, a, false, 1));
                roll.mouseDrag (ev ((a + b) / 2, ctrlLeft, a, true, 1));
                roll.mouseDrag (ev (b, ctrlLeft, a, true, 1));
                roll.mouseUp (ev (b, {}, a, true, 1));
                engine.dispatchPendingMessages();
            }
            const auto made = roll.timeSelectionForTests();
            check ("213_pr_ctrl_drag_makes_selection", made.first >= 0 && made.second > made.first,
                   juce::String (made.first) + ".." + juce::String (made.second));
            savePng (shot::snapshot (roll), prefix + "-213-pr-01-selection.png");
            // a single click on the ruler moves the song position and keeps the selection (FL 2026, capture f06)
            click (at (5 * bar), left, 1);
            check ("213_pr_single_click_keeps_selection", roll.timeSelectionForTests() == made);
            // the double click: the selection goes, and the notes are deselected (FL 2026, capture f30 / f31)
            roll.selectAll();
            click (at (5 * bar), left, 1);
            click (at (5 * bar), left, 2);
            check ("213_pr_double_click_clears", roll.timeSelectionForTests().first < 0);
            check ("213_pr_double_click_deselects_notes", roll.selectionForTests().empty());
            savePng (shot::snapshot (roll), prefix + "-213-pr-02-cleared.png");
            // and a Ctrl+click in place does too (the Playlist's rule)
            roll.setTimeSelectionForTests (bar, 2 * bar);
            click (at (6 * bar), ctrlLeft, 1);
            check ("213_pr_ctrl_click_clears", roll.timeSelectionForTests().first < 0);
            // Ctrl+D (Deselect) clears it as well
            roll.setTimeSelectionForTests (bar, 2 * bar);
            roll.handleKey (juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0));
            check ("213_pr_ctrl_d_clears", roll.timeSelectionForTests().first < 0);
        }
        {
            AudioEngine engine (false);
            engine.setSong (noteSong());
            engine.dispatchPendingMessages();
            PlaylistComponent pl (engine);
            pl.setBounds (0, 0, 1100, 420);
            pl.setVisible (true);
            pl.setViewForSnapshot (80.0, 0, 0);
            engine.dispatchPendingMessages();
            const auto y = PlaylistComponent::rulerTop + PlaylistComponent::timelineHeight / 2;
            const auto at = [&] (std::int64_t tick) { return juce::Point<int> ((int) pl.xOfTickForSnapshot ((double) tick), y); };
            const auto ev = [&] (juce::Point<int> p, juce::ModifierKeys mods, int clicks) {
                return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pl, &pl, now, p.toFloat(), now, clicks, false);
            };
            const auto click = [&] (juce::Point<int> p, juce::ModifierKeys mods, int clicks) {
                pl.mouseDown (ev (p, mods, clicks));
                pl.mouseUp (ev (p, mods.withoutMouseButtons(), clicks));
                engine.dispatchPendingMessages();
            };
            pl.setTimeSelectionForSnapshot (1, 3);
            engine.dispatchPendingMessages();
            check ("213_pl_selection_made", engine.getSong().hasLoop());
            savePng (shot::snapshot (pl), prefix + "-213-pl-01-selection.png");
            click (at (6 * bar), left, 1);
            check ("213_pl_single_click_keeps_selection", engine.getSong().hasLoop());
            click (at (6 * bar), left, 1);
            click (at (6 * bar), left, 2);
            check ("213_pl_double_click_clears", ! engine.getSong().hasLoop());
            savePng (shot::snapshot (pl), prefix + "-213-pl-02-cleared.png");
            pl.setTimeSelectionForSnapshot (1, 3);
            // Ctrl+D: the key map's "playlist.deselect" (the main window routes the key to it)
            const auto* deselect = keys::findAction ("playlist.deselect");
            check ("213_pl_ctrl_d_is_deselect", deselect != nullptr && ! deselect->defaults.empty()
                                                    && deselect->defaults.front() == keys::Chord { (int) 'D', keys::ctrl });
            pl.runAction ("playlist.deselect");
            engine.dispatchPendingMessages();
            check ("213_pl_ctrl_d_clears", ! engine.getSong().hasLoop());
        }

        report.add (juce::String ("result=") + (allOk ? "OK" : "FAIL"));
        juce::File (prefix + "-checks.txt").replaceWithText (report.joinIntoString ("\n") + "\n");
        return allOk;
    }

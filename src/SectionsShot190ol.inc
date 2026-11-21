    // --sections190olshot <prefix> (round 190ol, research190oe P1 and P6): the Sections lane and the Simple layout's time
    // signature changes through the real main window, offscreen (--shot-scale 1.5 --shot-suffix @150 for @150):
    //  - Pattern layout: the lane off (FL's Playlist unchanged, the default) and on (Playlist menu › View › Sections lane),
    //    a drag of "Chorus" before "Verse" (the picture mid-drag: the ghost and the line where it lands) and the result;
    //  - Simple layout: no lane until the title bar's "Sections" button adds the first section, then four sections (GarageBand's
    //    Arrangement track), a 7/8 change from bar 9 on the ruler (P6) and the top bar's signature box;
    //  - Clip-launch: no lane;
    //  - the lane's paint cost with 50 sections against the lane off (research190oe: <= +0.2 ms a Playlist paint).
    // prefix-checks.txt lists what was checked. Included in the app class of Main.cpp.
    bool runSectionsShot190ol (const juce::String& prefix)
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
        sections::patternLaneOn() = false; // (a fresh profile)
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        AudioEngine engine (false);
        MainComponent ui (engine, false);
        ui.setSize (1600, 900);
        ui.setLayoutStyle (layoutstyle::Style::pattern, false);
        const auto bar = (std::int64_t) 4 * ppq;
        // a song of four parts: drums all through, bass from the verse on, a lead in the chorus, a pad in the outro
        const auto makeSong = [bar] {
            auto song = Song::makeDefault();
            song.playlist.clear();
            song.patterns.resize (4);
            const char* names[] = { "Drums", "Bass", "Lead", "Pad" };
            for (int i = 0; i < 4; ++i)
            {
                song.patterns[(std::size_t) i].name = names[i];
                song.patterns[(std::size_t) i].lengthTicks = bar;
                for (int step : { 0, 4, 8, 12 })
                    song.patterns[(std::size_t) i].toggleStep (0, step + (i % 2));
            }
            song.playlistTracks.resize (8);
            const auto put = [&song, bar] (int pattern, int track, int fromBar, int bars) {
                for (int b = 0; b < bars; ++b)
                {
                    Clip c;
                    c.pattern = pattern;
                    c.track = track;
                    c.start = (fromBar + b) * bar;
                    c.length = bar;
                    song.playlist.push_back (c);
                }
            };
            put (0, 0, 0, 24);
            put (1, 1, 4, 20);
            put (2, 2, 12, 8);
            put (3, 3, 20, 4);
            song.sections() = { { 0, 4 * bar, "Intro", 0, 1 }, { 4 * bar, 8 * bar, "Verse", 0, 2 }, { 12 * bar, 8 * bar, "Chorus", 0, 3 },
                                { 20 * bar, 4 * bar, "Outro", 0, 4 } };
            song.addMarker ({ 12 * bar, "Drop", 0, 4 });
            return song;
        };
        engine.setSong (makeSong());
        engine.dispatchPendingMessages();
        ui.selectTabForSnapshot (2);
        engine.dispatchPendingMessages();
        auto& pl = ui.getPlaylistForSnapshot();
        pl.setSize (juce::jmax (pl.getWidth(), 1200), juce::jmax (pl.getHeight(), 420));
        pl.setViewForSnapshot (40.0, 0, 0);
        engine.dispatchPendingMessages();
        const auto out = [&prefix] (const juce::String& name) { return prefix + "-" + name + ".png"; };
        const auto snap = [&] (const juce::String& name, int height = 300) {
            pl.refreshViewOptions();
            engine.dispatchPendingMessages();
            savePng (shot::snapshot (pl, juce::Rectangle<int> (0, 0, juce::jmin (pl.getWidth(), 1200), juce::jmin (pl.getHeight(), height))), out (name));
        };
        auto* lane = pl.getSectionsLaneForSnapshot();
        check ("lane_exists", lane != nullptr);
        if (lane == nullptr)
            return false;

        // --- Pattern: off by default (FL's Playlist unchanged), on through the extra -------------------------------------
        check ("pattern_off_by_default", ! pl.sectionsLaneShown() && ! lane->isVisible() && pl.tracksTop() == PlaylistComponent::headerHeight + pl.videoSectionHeight());
        snap ("pattern-lane-off");
        workflowextras::setExtra ("playlist.sectionsLane", true);
        pl.refreshViewOptions();
        check ("pattern_on_through_the_extra", pl.sectionsLaneShown() && lane->isVisible() && lane->getHeight() == PlaylistComponent::sectionsLaneFlHeight
                                                   && lane->getY() == PlaylistComponent::headerHeight,
               juce::String (lane->getHeight()));
        check ("extra_saved", workflowextras::uiOptionsText().contains ("sectionsLane=1"));
        snap ("pattern-lane-on");

        // drag "Chorus" (index 2) before "Verse" (index 1) on the lane: the picture mid-drag, then the drop
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const auto event = [&] (juce::Component& c, juce::Point<int> p, juce::ModifierKeys mods, juce::Point<int> down, int clicks, bool dragged) {
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, down.toFloat(), now, clicks, dragged);
        };
        const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
        const auto xOf = [&] (std::int64_t tick) { return (int) std::lround (pl.xOfTickForSnapshot ((double) tick)); };
        const auto dragLane = [&] (std::int64_t fromTick, std::int64_t toTick, juce::ModifierKeys mods, const juce::String& midShot) {
            const juce::Point<int> from (xOf (fromTick), lane->getHeight() / 2), to (xOf (toTick), lane->getHeight() / 2);
            lane->mouseDown (event (*lane, from, left.withFlags (mods.getRawFlags()), from, 1, false));
            lane->mouseDrag (event (*lane, (from + to) / 2, left.withFlags (mods.getRawFlags()), from, 1, true));
            lane->mouseDrag (event (*lane, to, left.withFlags (mods.getRawFlags()), from, 1, true));
            if (midShot.isNotEmpty())
                savePng (shot::snapshot (pl, juce::Rectangle<int> (0, 0, juce::jmin (pl.getWidth(), 1200), 300)), out (midShot));
            lane->mouseUp (event (*lane, to, mods, from, 1, true));
            engine.dispatchPendingMessages();
        };
        const auto order = [&engine] {
            juce::StringArray names;
            for (const auto& s : engine.getSong().sections())
                names.add (juce::String::fromUTF8 (s.name.c_str()));
            return names.joinIntoString (",");
        };
        dragLane (14 * bar, 4 * bar + bar / 4, {}, "pattern-drag-chorus");
        check ("drag_moves_chorus_before_verse", order() == "Intro,Chorus,Verse,Outro", order());
        {
            // the lead's eight bars went with it to bar 5, the bass under the chorus too; nothing else moved
            const auto& song = engine.getSong();
            const auto leadAt = std::count_if (song.playlist.begin(), song.playlist.end(), [bar] (const Clip& c) { return c.pattern == 2 && c.start >= 4 * bar && c.start < 12 * bar; });
            check ("drag_carries_the_clips", leadAt == 8 && song.songLength() == 24 * bar, juce::String ((int) leadAt));
            check ("drag_carries_the_marker", song.markers().size() == 1 && song.markers()[0].tick == 4 * bar);
        }
        snap ("pattern-after-move");
        engine.undo();
        engine.dispatchPendingMessages();
        check ("undo_brings_the_order_back", order() == "Intro,Verse,Chorus,Outro", order());
        // Ctrl+drag (Option on macOS): a copy of the intro after the outro
       #if JUCE_MAC
        const juce::ModifierKeys copyMods (juce::ModifierKeys::altModifier);
       #else
        const juce::ModifierKeys copyMods (juce::ModifierKeys::ctrlModifier);
       #endif
        dragLane (2 * bar, 24 * bar - bar / 4, copyMods, "pattern-copy-drag");
        check ("ctrl_drag_copies", order() == "Intro,Verse,Chorus,Outro,Intro" && engine.getSong().songLength() == 28 * bar, order());
        engine.undo();
        engine.dispatchPendingMessages();
        // the menus
        {
            juce::StringArray items;
            const auto sectionMenu = lane->sectionMenu (1);
            for (juce::PopupMenu::MenuItemIterator it (sectionMenu); it.next();)
                items.add (it.getItem().text.upToFirstOccurrenceOf ("\t", false, false));
            check ("section_menu", items.contains (tr ("Duplicate")) && items.contains (tr ("Delete and close the gap")) && items.contains (tr ("Loop this section"))
                                       && items.contains (tr ("Select its clips")),
                   items.joinIntoString ("|"));
            juce::StringArray laneItems;
            const auto laneMenu = lane->laneMenu();
            for (juce::PopupMenu::MenuItemIterator it (laneMenu); it.next();)
                laneItems.add (it.getItem().text);
            check ("lane_menu", laneItems.contains (tr ("Sections from markers")) && laneItems.contains (tr ("Create scenes from sections")), laneItems.joinIntoString ("|"));
        }

        // --- the paint cost: 50 sections against the lane off --------------------------------------------------------------
        {
            auto many = makeSong();
            many.sections().clear();
            for (int i = 0; i < 50; ++i)
                many.sections().push_back ({ i * bar / 2, bar / 2, "S" + std::to_string (i + 1), 0, (std::uint64_t) i + 1 });
            engine.setSong (std::move (many));
            engine.dispatchPendingMessages();
            pl.setViewForSnapshot (40.0, 0, 0);
            const auto time = [&] (bool on) {
                workflowextras::setExtra ("playlist.sectionsLane", on);
                pl.refreshViewOptions();
                juce::Image image (juce::Image::ARGB, pl.getWidth(), pl.getHeight(), true);
                double best = 1.0e9;
                for (int round = 0; round < 5; ++round)
                {
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    for (int k = 0; k < 20; ++k)
                    {
                        juce::Graphics g (image);
                        pl.paintEntireComponent (g, true);
                    }
                    best = juce::jmin (best, (juce::Time::getMillisecondCounterHiRes() - t0) / 20.0);
                }
                return best;
            };
            const auto off = time (false), on = time (true), off2 = time (false), on2 = time (true);
            const auto extra = juce::jmin (on, on2) - juce::jmin (off, off2);
            // the lane's own picture made again (a scroll or zoom step: no cached picture to reuse)
            {
                juce::Image image (juce::Image::RGB, lane->getWidth(), lane->getHeight(), false);
                double best = 1.0e9;
                for (int round = 0; round < 5; ++round)
                {
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    for (int k = 0; k < 20; ++k)
                    {
                        juce::Graphics g (image);
                        lane->paintLane (g);
                    }
                    best = juce::jmin (best, (juce::Time::getMillisecondCounterHiRes() - t0) / 20.0);
                }
                check ("lane_50_sections_uncached_ms", best <= 2.0, juce::String (best, 3) + " ms");
            }
            check ("paint_50_sections_within_0.2ms", extra <= 0.2, juce::String (extra, 3) + " ms (off " + juce::String (juce::jmin (off, off2), 3) + ", on "
                                                                         + juce::String (juce::jmin (on, on2), 3) + ")");
            snap ("pattern-50-sections");
            workflowextras::setExtra ("playlist.sectionsLane", false);
            engine.setSong (makeSong());
            engine.dispatchPendingMessages();
        }

        // --- Clip-launch: no lane -------------------------------------------------------------------------------------------
        workflowextras::setExtra ("playlist.sectionsLane", true);
        ui.setLayoutStyle (layoutstyle::Style::clipLaunch, false);
        engine.dispatchPendingMessages();
        check ("clip_launch_no_lane", ! pl.sectionsLaneShown());
        workflowextras::setExtra ("playlist.sectionsLane", false);

        // --- Simple: the lane comes with the first section ------------------------------------------------------------------
        {
            auto song = makeSong();
            song.sections().clear();
            song.markers().clear();
            engine.setSong (std::move (song));
        }
        ui.setLayoutStyle (layoutstyle::Style::simple, false);
        engine.dispatchPendingMessages();
        pl.setViewForSnapshot (34.0, 0, 0);
        check ("simple_hidden_without_sections", ! pl.sectionsLaneShown());
        auto* button = dynamic_cast<juce::Button*> (findById (ui, "simple.sections"));
        check ("simple_sections_button", button != nullptr);
        if (button != nullptr && button->onClick)
            button->onClick(); // (triggerClick posts a message; the click itself, as the button runs it)
        engine.dispatchPendingMessages();
        engine.dispatchPendingMessages();
        check ("simple_first_section", engine.getSong().sections().size() == 1 && engine.getSong().sections()[0].length == 8 * bar && pl.sectionsLaneShown()
                                           && lane->getHeight() == PlaylistComponent::sectionsLaneSimpleHeight,
               juce::String ((int) engine.getSong().sections().size()));
        // "+" three times: Verse, Chorus, Bridge at the end of the last one
        for (int i = 0; i < 3; ++i)
        {
            const auto plus = juce::Point<int> (pl.headerWidthForSnapshot() - 20, lane->getHeight() / 2);
            lane->mouseDown (event (*lane, plus, left, plus, 1, false));
            lane->mouseUp (event (*lane, plus, {}, plus, 1, false));
            engine.dispatchPendingMessages();
        }
        check ("simple_plus_adds_eight_bars", engine.getSong().sections().size() == 4 && engine.getSong().sections()[3].start == 24 * bar
                                                  && engine.getSong().sections()[1].name == sectionPresetName (1).toStdString(),
               order());
        // P6: 7/8 from bar 9 (the ruler's "Change time signature here..." writes it; the menu item is checked below)
        engine.editSong ([bar] (Song& s) { signatureedit::set (s, 8 * bar, 7, 8); });
        engine.dispatchPendingMessages();
        {
            juce::StringArray items;
            const auto rulerMenu = pl.rulerMenu (xOf (8 * bar) + 4);
            for (juce::PopupMenu::MenuItemIterator it (rulerMenu); it.next();)
                items.add (it.getItem().text);
            check ("simple_ruler_menu", items[0] == tr ("Change time signature here...") && items.contains (tr ("Delete this time signature change")),
                   items.joinIntoString ("|").substring (0, 200));
        }
        check ("simple_signature_box", findTextIn (ui, "4 / 4"));
        lane->select (1);
        savePng (shot::snapshot (pl, juce::Rectangle<int> (0, 0, juce::jmin (pl.getWidth(), 1200), juce::jmin (pl.getHeight(), 360))), out ("simple-sections"));
        savePng (shot::snapshot (ui, juce::Rectangle<int> (0, 0, ui.getWidth(), juce::jmin (ui.getHeight(), 420))), out ("simple-window"));
        // the button hides and shows it again (view only)
        if (button != nullptr && button->onClick)
            button->onClick(); // (triggerClick posts a message; the click itself, as the button runs it)
        engine.dispatchPendingMessages();
        check ("simple_button_hides", ! pl.sectionsLaneShown() && engine.getSong().sections().size() == 4);
        if (button != nullptr && button->onClick)
            button->onClick(); // (triggerClick posts a message; the click itself, as the button runs it)
        engine.dispatchPendingMessages();
        check ("simple_button_shows", pl.sectionsLaneShown());

        ui.setLayoutStyle (layoutstyle::Style::pattern, false);
        juce::File (prefix + "-checks.txt").replaceWithText (report.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

    static juce::Component* findById (juce::Component& root, const juce::String& id)
    {
        if (root.getComponentID() == id)
            return &root;
        for (auto* c : root.getChildren())
            if (auto* found = findById (*c, id))
                return found;
        return nullptr;
    }

    // a SignatureBox (or any component) whose paint shows `text`: looked up by its text() through a hint-free route —
    // the boxes of the top bar are found by type
    static bool findTextIn (juce::Component& root, const juce::String& text)
    {
        if (auto* box = dynamic_cast<lynx::toolbar::SignatureBox*> (&root); box != nullptr && box->isVisible() && box->text() == text)
            return true;
        for (auto* c : root.getChildren())
            if (findTextIn (*c, text))
                return true;
        return false;
    }

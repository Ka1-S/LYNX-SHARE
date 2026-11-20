    // --keys190nu <folder> (round chord2_190nu, discussion #105 part 2): key regions on the Piano roll ruler (FL's Key/Scale
    // markers), the Chord stamp's Scale degree chord and the Chord helper's pinned relationships, drawn offscreen at 1x and
    // at 150 % ("@150"):
    //  1. esiowry's example: bars 1-2 C Ionian, bar 3 C natural minor (a key region over a time selection), the rows of
    //     each region shaded for its key; the Add key marker menu, a key marker's menu, the Stamp menu with the degree chord.
    //  2. The Scale degree chord: the ghost over D in the C Ionian region (ii · Dm), over A♭ in the C minor one (VI · A♭),
    //     then clicks writing I, ii, IV in the first region and ♭VI, i in the second.
    //  3. The Chord helper with pinned relationships (the parallel minor's ♭VII pinned: first in the list), in the three
    //     layouts' looks.
    // <folder>/checks.txt lists what was checked.
    bool runKeysShot190nu (const juce::File& folder)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        folder.createDirectory();
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const char* name, bool ok, const juce::String& detail = {}) {
            report.add (juce::String (name) + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        const auto writeImage = [&] (const juce::Image& image, const juce::File& f, const juce::String& name) {
            f.deleteFile();
            bool ok = false;
            if (auto out = f.createOutputStream())
                ok = juce::PNGImageFormat().writeImageToStream (image, *out);
            check (name.toRawUTF8(), ok && image.getWidth() > 0);
        };
        const auto shoot = [&] (juce::Component& c, const juce::String& name) {
            writeImage (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f), folder.getChildFile (name + ".png"), name);
            writeImage (c.createComponentSnapshot (c.getLocalBounds(), true, 1.5f), folder.getChildFile (name + "@150.png"), name + "@150");
        };
        const auto subMenu = [] (const juce::PopupMenu& m, const juce::String& text) {
            for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
                if (it.getItem().text.upToFirstOccurrenceOf ("\t", false, false) == text && it.getItem().subMenu != nullptr)
                    return *it.getItem().subMenu;
            return juce::PopupMenu();
        };
        const auto menuText = [] (const juce::PopupMenu& m) {
            juce::StringArray items;
            for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
                items.add (it.getItem().isSeparator ? juce::String ("-") : it.getItem().text.upToFirstOccurrenceOf ("\t", false, false));
            return items;
        };
        const auto bar = 4 * (std::int64_t) ppq;
        const auto keysAt = [] (const Song& s, std::int64_t tick) {
            std::vector<int> keys;
            for (const auto& n : s.patterns.front().notes)
                if (n.start == tick)
                    keys.push_back (n.key);
            std::sort (keys.begin(), keys.end());
            return keys;
        };

        // --- 1 and 2: key regions and the Scale degree chord ------------------------------------------------------------
        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            song.currentPattern = 0;
            song.patterns.front().notes.clear();
            song.patterns.front().lengthTicks = 4 * bar;
            engine.setSong (std::move (song));
            engine.setSelectedChannel (0);
            engine.dispatchPendingMessages();
            PianoRollComponent roll (engine);
            roll.setBounds (0, 0, 1100, 520);
            roll.setVisible (true);
            engine.sendChangeMessage();
            engine.dispatchPendingMessages();
            // the regions: bars 1-2 C Ionian, bar 3 C natural minor (esiowry, 11-20), made as the menu items do
            const keyregions::State ionian { keyregions::State::key, { 0, keyregions::majorScale() } };
            const keyregions::State aeolian { keyregions::State::key, { 0, keyregions::minorScale() } };
            roll.addKeyMarker (0, ionian);
            roll.setTimeSelection (2 * bar, 3 * bar);
            check ("menu_add_key_marker", menuText (roll.getMarkersMenuForSnapshot()).contains (tr ("Add key marker")));
            check ("menu_key_region_needs_time_selection", menuText (roll.getMarkersMenuForSnapshot()).contains (tr ("Key region for the time selection")));
            roll.addKeyRegion (2 * bar, 3 * bar, aeolian);
            roll.setTimeSelection (-1, -1);
            const auto& p = engine.getSong().patterns.front();
            check ("regions_c_ionian_c_minor_c_ionian",
                   keyregions::stateAt (p, bar).value == ionian.value && keyregions::stateAt (p, 2 * bar).value == aeolian.value
                       && keyregions::stateAt (p, 3 * bar).value == ionian.value);
            check ("marker_names", p.markers.size() == 3 && p.markers[0].name == "C Major (Ionian)" && p.markers[1].name == "C Minor Natural (Aeolian)"
                                       && p.markers[2].name == "C Major (Ionian)");
            roll.scrollToNotes (true);
            shoot (roll, "keys-01-regions");
            // the paint cost of the regions: instructions per whole frame of the Piano roll (median of 15) with the three
            // key markers and with the same view after they are taken out (the rows' single fill path)
            {
                const lynx::platform::ThreadInstructions counter;
                const auto frameInstructions = [&roll, &counter] {
                    std::vector<long long> v;
                    juce::Image image (juce::Image::ARGB, roll.getWidth(), roll.getHeight(), true);
                    for (int f = 0; f < 18; ++f)
                    {
                        juce::Graphics g (image);
                        const auto i0 = counter.now();
                        roll.paintEntireComponent (g, true);
                        if (f >= 3)
                            v.push_back (counter.now() - i0);
                    }
                    std::sort (v.begin(), v.end());
                    return v[v.size() / 2];
                };
                const auto withRegions = frameInstructions();
                const auto markers = engine.getSong().patterns.front().markers;
                engine.editSong ([] (Song& s) { s.patterns.front().markers.clear(); });
                engine.dispatchPendingMessages();
                const auto without = frameInstructions();
                engine.editSong ([markers] (Song& s) { s.patterns.front().markers = markers; });
                engine.dispatchPendingMessages();
                check ("paint_instructions_with_regions", counter.available() ? withRegions > 0 : true,
                       juce::String (withRegions) + " vs " + juce::String (without) + " without key markers ("
                           + juce::String (without > 0 ? 100.0 * (double) (withRegions - without) / (double) without : 0.0, 2) + " %)");
            }
            // the menus (FL's Add key marker ▸ Root note | Scale; a key marker's menu; the Stamp menu)
            saveMenus (juce::StringArray { "Add key marker" }, std::vector<juce::PopupMenu> { subMenu (roll.getMarkersMenuForSnapshot(), tr ("Add key marker")) },
                       folder.getChildFile ("keys-02-add-key-marker-menu.png").getFullPathName());
            saveMenus (juce::StringArray { "Marker" }, std::vector<juce::PopupMenu> { roll.getPatternMarkerMenuForSnapshot (1) },
                       folder.getChildFile ("keys-03-marker-menu.png").getFullPathName());
            saveMenus (juce::StringArray { "Key/scale" }, std::vector<juce::PopupMenu> { subMenu (roll.getPatternMarkerMenuForSnapshot (1), tr ("Key/scale")) },
                       folder.getChildFile ("keys-03b-marker-key-menu.png").getFullPathName());
            const auto stamp = roll.getStampMenuForSnapshot();
            check ("stamp_menu_has_degree_chord", menuText (stamp).contains (tr ("Scale degree chord")) && menuText (stamp).contains (tr ("Scale degree chord options")));
            saveMenus (juce::StringArray { "Stamp" }, std::vector<juce::PopupMenu> { stamp }, folder.getChildFile ("keys-04-stamp-menu.png").getFullPathName());
            saveMenus (juce::StringArray { "Options" }, std::vector<juce::PopupMenu> { subMenu (stamp, tr ("Scale degree chord options")) },
                       folder.getChildFile ("keys-04b-degree-options.png").getFullPathName());

            // the Scale degree chord: the ghost and the clicks
            roll.setToolForSnapshot (EditTool::pencil);
            roll.setDegreeStamp (true);
            PianoRollComponent::degreeSize = keyregions::DegreeSize::triad;
            PianoRollComponent::degreeInversion = 0;
            PianoRollComponent::degreeHear = false; // (offscreen: nothing to hear)
            roll.hoverDegreeForSnapshot (roll.pointForSnapshot (62, 0));
            check ("ghost_d_is_ii_dm", roll.degreeGhostKeys() == std::vector<int> { 62, 65, 69 } && roll.degreeGhostLabel().startsWith ("ii"),
                   roll.degreeGhostLabel());
            shoot (roll, "keys-05-ghost-ii");
            roll.hoverDegreeForSnapshot (roll.pointForSnapshot (68, 2 * bar));
            check ("ghost_ab_in_c_minor_is_VI", roll.degreeGhostKeys() == std::vector<int> { 68, 72, 75 } && roll.degreeGhostLabel().startsWith ("VI") && roll.degreeGhostLabel().contains ("Ab"),
                   roll.degreeGhostLabel());
            shoot (roll, "keys-06-ghost-bVI");
            const auto click = [&roll, &engine] (int key, std::int64_t tick) {
                const auto at = roll.pointForSnapshot (key, tick);
                roll.pressForSnapshot (at);
                roll.releaseForSnapshot (at);
                engine.dispatchPendingMessages();
            };
            click (60, 0);       // I
            click (62, bar / 2); // ii
            click (65, bar);     // IV
            PianoRollComponent::degreeSize = keyregions::DegreeSize::seventh;
            click (67, bar + bar / 2); // V7
            PianoRollComponent::degreeSize = keyregions::DegreeSize::triad;
            click (68, 2 * bar);          // ♭VI (C minor's VI)
            click (60, 2 * bar + bar / 2); // i
            const auto& s = engine.getSong();
            check ("click_c_writes_c_major", keysAt (s, 0) == std::vector<int> { 60, 64, 67 });
            check ("click_d_writes_d_minor", keysAt (s, bar / 2) == std::vector<int> { 62, 65, 69 });
            check ("click_f_writes_f_major", keysAt (s, bar) == std::vector<int> { 65, 69, 72 });
            check ("click_g_7th_writes_g7", keysAt (s, bar + bar / 2) == std::vector<int> { 67, 71, 74, 77 });
            check ("click_ab_in_minor_region_writes_ab", keysAt (s, 2 * bar) == std::vector<int> { 68, 72, 75 });
            check ("click_c_in_minor_region_writes_c_minor", keysAt (s, 2 * bar + bar / 2) == std::vector<int> { 60, 63, 67 });
            PianoRollComponent::degreeHear = true;
            roll.hoverDegreeForSnapshot (roll.pointForSnapshot (64, 3 * bar));
            const auto armed = roll.degreeHearRunning();
            roll.hoverDegreeForSnapshot ({ -10, -10 });
            check ("hear_timer_only_while_resting_on_a_ghost", armed && ! roll.degreeHearRunning());
            PianoRollComponent::degreeHear = false;
            roll.selectMidiToolResult ({});
            shoot (roll, "keys-07-inserted");
            // the plain stamps are as before (a Major stamp on a key)
            roll.setDegreeStamp (false);
            check ("plain_note_without_stamp", roll.stampKeys (61, 0) == std::vector<int> { 61 });
        }

        // --- 3. the Chord helper's pinned relationships ---------------------------------------------------------------
        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            song.currentPattern = 0;
            auto& p = song.patterns.front();
            p.notes.clear();
            const std::vector<std::vector<int>> progression { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 }, { 60, 64, 67 } }; // Am F G C
            for (std::size_t c = 0; c < progression.size(); ++c)
                for (const auto key : progression[c])
                {
                    Note n;
                    n.start = (std::int64_t) c * bar;
                    n.length = bar;
                    n.key = key;
                    p.notes.push_back (n);
                }
            engine.setSong (std::move (song));
            engine.setSelectedChannel (0);
            engine.dispatchPendingMessages();
            std::vector<int> last;
            for (int i = 0; i < (int) engine.getSong().patterns.front().notes.size(); ++i)
                if (engine.getSong().patterns.front().notes[(std::size_t) i].start == 3 * bar)
                    last.push_back (i);
            const auto target = [&engine, last] {
                ChordHelperPanel::Target t;
                t.pattern = 0;
                t.channel = 0;
                t.selected = last;
                return t;
            };
            const auto saved = ChordHelperPanel::favourites;
            ChordHelperPanel::favourites.clear();
            ChordHelperPanel::pinnedFirst = true;
            for (const auto look : { ChordHelperPanel::Look::fl, ChordHelperPanel::Look::live, ChordHelperPanel::Look::simple })
            {
                const auto tag = look == ChordHelperPanel::Look::fl ? juce::String ("pattern") : look == ChordHelperPanel::Look::live ? juce::String ("cliplaunch") : juce::String ("simple");
                ChordHelperPanel panel (engine, target, [] (const std::vector<int>&) {}, look);
                panel.setVisible (true);
                if (look == ChordHelperPanel::Look::fl)
                {
                    // pin ♭VII (B♭, rank 2 of the parallel minor) and the dominant key (G major)
                    int flat7 = -1;
                    for (int i = 0; i < (int) panel.borrowedChords().size(); ++i)
                        if (panel.borrowedChords()[(std::size_t) i].chord.root == 10)
                            flat7 = i;
                    check ("bb_listed", flat7 == 1, juce::String (flat7));
                    panel.togglePinChord (flat7);
                    check ("pinned_bb_first", panel.borrowedChords().front().chord.root == 10 && panel.isPinnedChord (0) && panel.borrowedChords().front().rank == 2);
                    int dominant = -1;
                    for (int i = 0; i < (int) panel.relatedKeys().size(); ++i)
                        if (panel.relatedKeys()[(std::size_t) i].relation == modalhelp::Relation::dominant)
                            dominant = i;
                    panel.togglePinKey (dominant);
                    check ("pinned_key_first", panel.relatedKeys().front().relation == modalhelp::Relation::dominant && panel.isPinnedKey (0));
                    check ("chosen_key_stays", panel.selectedKeyRow() >= 0
                                                   && panel.relatedKeys()[(std::size_t) panel.selectedKeyRow()].scale.mode == modalhelp::Mode::aeolian);
                    check ("favourites_saved_as_ids", ChordHelperPanel::favourites.size() == 2, juce::String (modalhelp::favouritesText (ChordHelperPanel::favourites)));
                    check ("no_timer_while_idle", ! panel.previewRunning());
                }
                shoot (panel, "fav-" + tag + "-01-pinned");
                if (look == ChordHelperPanel::Look::fl)
                {
                    panel.setPinnedFirst (false); // (only highlighted, in rank order)
                    check ("highlight_only_keeps_rank_order", panel.borrowedChords().front().rank == 1 && panel.isPinnedChord (1));
                    shoot (panel, "fav-pattern-02-highlight-only");
                    panel.setPinnedFirst (true);
                }
            }
            // a pin holds in another key: G major's ♭VII (F) is pinned too
            {
                auto& notes = engine.getSong().patterns.front().notes;
                juce::ignoreUnused (notes);
                engine.editSong ([] (Song& s) {
                    for (auto& n : s.patterns.front().notes)
                        n.key += 7;
                });
                engine.dispatchPendingMessages();
                ChordHelperPanel panel (engine, target, [] (const std::vector<int>&) {}, ChordHelperPanel::Look::fl);
                check ("pin_holds_in_g", ! panel.borrowedChords().empty() && panel.borrowedChords().front().chord.root == 5 && panel.isPinnedChord (0));
                shoot (panel, "fav-pattern-03-same-pin-in-g");
            }
            ChordHelperPanel::favourites = saved;
        }

        folder.getChildFile ("checks.txt").replaceWithText (report.joinIntoString ("\n") + "\n");
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        return allOk;
    }

    // --details190lrshot <prefix> <project.flp> [set.als] (round 190lr, details pass 3): the Simple dock's Drum Machine
    // (BandLab's step grid, DS-39), Live's scene follow actions (DL-14: the striped Scene Launch button, the menu, the
    // .lynx round trip), Live's metronome "Enable Only While Recording" (DL-47) and Tap menu (DL-49), FL's F3 search step
    // (DF-97). <prefix>-checks.txt lists the checks. Included in the app class of Main.cpp.
    bool runDetailsShot190lr (const juce::String& prefix, const juce::String& project, const juce::String& set)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (lynx::ui::showingForShots(), true);
        juce::String checks;
        bool allOk = true;
        const auto check = [&checks, &allOk] (const juce::String& what, bool ok) {
            checks << (ok ? "ok   " : "FAIL ") << what << "\n";
            allOk = allOk && ok;
        };
        const auto line = [&checks] (const juce::String& what) { checks << "     " << what << "\n"; };
        const auto write = [&prefix] (juce::Component& c, const juce::String& name) { savePng (lynx::shot::snapshot (c), prefix + "-" + name + ".png"); };
        const auto menuShot = [&prefix] (const juce::PopupMenu& menu, const juce::String& title, const juce::String& name) {
            saveMenus (juce::StringArray { title }, std::vector<juce::PopupMenu> { menu }, prefix + "-" + name + ".png");
        };
        const auto texts = [] (const juce::PopupMenu& menu) {
            juce::StringArray all;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                all.add (it.getItem().text);
            return all;
        };
        using namespace lynx;
        juce::ignoreUnused (project);
        {
            // ---- Simple layout: a Drums track and its Drum Machine tab
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (1600, 900);
            ui.selectTabForSnapshot (2);
            ui.setLayoutStyle (layoutstyle::Style::simple);
            offline.dispatchPendingMessages();
            auto* simple = ui.getSimpleWorkflowForSnapshot();
            check ("Simple workflow", simple != nullptr);
            if (simple != nullptr)
            {
                simple->addDrumsForSnapshot ({}, true);
                offline.dispatchPendingMessages();
                simple->hideChooser();
                int drums = -1;
                for (int t = 0; t < (int) offline.getSong().playlistTracks.size() && drums < 0; ++t)
                    if (simpletracks::kindOf (offline.getSong(), t) == simpletracks::Kind::drums)
                        drums = t;
                check ("a Drums track from a starter", drums >= 0);
                ui.getPlaylistForSnapshot().selectTrack (drums);
                offline.dispatchPendingMessages();
                auto* dock = simple->dockForSnapshot();
                check ("the dock shows the Drums track", dock != nullptr && dock->shownTrack() == drums);
                if (dock != nullptr && drums >= 0)
                {
                    check ("a Drums track opens on its Drum Machine tab", dock->hasStepsTabForCheck() && dock->getTab() == SimpleDock::Tab::steps);
                    const auto rows = dock->stepsRowsForCheck();
                    line ("kit rows " + juce::String (rows) + ", steps " + juce::String (dock->stepsCountForCheck()));
                    check ("one row per kit sound, 16 steps a bar", rows == (int) simplesteps::kitRows (offline.getSong(), drums).size() && rows > 0
                                                                       && dock->stepsCountForCheck() % 16 == 0);
                    write (*dock, "simple-drum-machine");
                    // a click turns a step on (the default velocity) and off again; one undo step each
                    const auto pattern = dock->stepsPatternForCheck();
                    const auto kick = simplesteps::kitRows (offline.getSong(), drums)[0];
                    const auto had = offline.getSong().patterns[(std::size_t) pattern].hasStep (kick, 3);
                    dock->stepsClickForCheck (0, 3);
                    offline.dispatchPendingMessages();
                    check ("a click turns the bottom row's step 4 over", offline.getSong().patterns[(std::size_t) pattern].hasStep (kick, 3) != had);
                    offline.undo();
                    offline.dispatchPendingMessages();
                    check ("undo puts it back", offline.getSong().patterns[(std::size_t) pattern].hasStep (kick, 3) == had);
                    // Length: 2 bars → 32 steps; the menu lists BandLab's choices
                    const auto lengths = texts (dock->stepsLengthMenuForSnapshot());
                    line ("Length menu: " + lengths.joinIntoString (" | "));
                    check ("Length: 1 bar / 2 bars / 4 bars", lengths.size() == 3 && lengths[0] == lynx::tr ("1 bar"));
                    menuShot (dock->stepsLengthMenuForSnapshot(), "Length", "simple-drum-length-menu");
                    dock->stepsLengthForCheck (2);
                    offline.dispatchPendingMessages();
                    check ("Length 2 bars: 32 steps", dock->stepsCountForCheck() == 32);
                    dock->stepsSwingForCheck (0.6f);
                    offline.dispatchPendingMessages();
                    check ("Swing is the Channel Rack's swing", std::abs (offline.getSong().swing - 0.6f) < 1.0e-4f);
                    // the playing step (the song plays it) lights its column
                    dock->stepsPlayingStepForSnapshot (5);
                    write (*dock, "simple-drum-machine-2bars-playing");
                    savePng (dock->createComponentSnapshot (dock->getLocalBounds().withHeight (juce::jmin (dock->getHeight(), 140)), true, 2.0f),
                             prefix + "-simple-drum-machine@2x.png");
                    dock->stepsPlayingStepForSnapshot (-1);
                    // pattern B: made empty, as long as A; Add B puts it on a bar of the track
                    dock->stepsLetterForCheck (1);
                    offline.dispatchPendingMessages();
                    const auto b = dock->stepsPatternForCheck();
                    check ("pattern B is a new empty pattern named after the track",
                           b >= 0 && b != pattern && offline.getSong().patterns[(std::size_t) b].notes.empty()
                               && juce::String (offline.getSong().patterns[(std::size_t) b].name).endsWith (" B"));
                    const auto clips = offline.getSong().playlist.size();
                    dock->stepsAddClipForCheck();
                    offline.dispatchPendingMessages();
                    check ("Add B: a clip of B on the track", offline.getSong().playlist.size() == clips + 1
                                                               && offline.getSong().playlist.back().pattern == b && offline.getSong().playlist.back().track == drums);
                    write (*dock, "simple-drum-machine-pattern-b");
                    // ▶ plays the pattern on its own (Pattern mode), ■ gives the song back
                    dock->stepsLetterForCheck (0);
                    dock->stepsPreviewForCheck();
                    check ("▶: the pattern plays on its own", dock->stepsPreviewingForCheck() && offline.getSong().mode == PlayMode::pattern
                                                                && offline.getSong().currentPattern == pattern);
                    dock->stepsPreviewForCheck();
                    check ("■: the song mode again", ! dock->stepsPreviewingForCheck() && offline.getSong().mode == PlayMode::song);
                    // the row keys: A plays the bottom row while the tab shows
                    check ("key A plays the kick row", dock->stepsChannelForKey ('A') == kick && dock->stepsChannelForKey ('Z') == -1);
                    write (ui, "simple-drum-machine-window");
                }
            }
        }
        {
            // ---- Clip-launch layout: a scene follow action (striped Scene Launch button, the menu, the .lynx round trip),
            // Live's metronome menu and Tap menu
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (1600, 900);
            ui.setVisible (true);
            ui.selectTabForSnapshot (2);
            ui.setClipLaunchWorkflow (true);
            offline.dispatchPendingMessages();
            if (set.isNotEmpty())
                ui.openFile (juce::File (set));
            offline.dispatchPendingMessages();
            ui.setClipGridView (PlaylistArea::GridView::grid);
            offline.dispatchPendingMessages();
            ui.resized();
            offline.editSong ([] (Song& s) {
                auto& g = launcher::ensureGrid (s);
                while (g.scenes.size() < 4)
                    g.scenes.emplace_back();
                g.scenes[1].follow.enabled = true;
                g.scenes[1].follow.a = FollowKind::next;
                g.scenes[1].follow.ticks = 4 * (std::int64_t) s.barTicks();
            });
            offline.dispatchPendingMessages();
            auto* gridView = ui.getClipGridForSnapshot();
            check ("grid", gridView != nullptr && offline.getSong().clipGrid.has_value());
            if (gridView != nullptr)
            {
                refreshClipGrid (*gridView);
                const auto cell = clipGridCellForSnapshot (*gridView, 2, 0, 1);
                check ("scene 2's cell", ! cell.isEmpty());
                savePng (gridView->createComponentSnapshot (cell.expanded (0, cell.getHeight()), true, 4.0f), prefix + "-scene-follow-striped@4x.png");
                write (*gridView, "grid-scene-follow");
                const auto items = texts (clipGridSceneMenu (*gridView, 1));
                check ("the scene menu has Follow Action", items.contains (lynx::tr ("Follow Action")));
                menuShot (clipGridSceneMenu (*gridView, 1), "Scene", "grid-scene-menu-follow");
                juce::String error;
                const auto back = ProjectFile::fromJson (ProjectFile::toJson (offline.getSong()), error);
                check (".lynx keeps the scene follow action", back && back->clipGrid && back->clipGrid->scenes.size() >= 2
                                                                  && back->clipGrid->scenes[1].follow == offline.getSong().clipGrid->scenes[1].follow
                                                                  && back->clipGrid->scenes[0].follow == ClipFollow {});
            }
            const auto metronome = texts (ui.metronomeMenuForSnapshot());
            line ("metronome menu: " + metronome.joinIntoString (" | "));
            check ("Clip-launch metronome menu: Enable Only While Recording", metronome.contains (lynx::tr ("Enable Only While Recording")));
            menuShot (ui.metronomeMenuForSnapshot(), "Metronome", "live-metronome-menu");
            const auto tap = texts (ui.liveTapMenuForSnapshot());
            check ("Tap menu: Start Playback with Tap Tempo", tap.contains (lynx::tr ("Start Playback with Tap Tempo")));
            menuShot (ui.liveTapMenuForSnapshot(), "Tap", "live-tap-menu");
        }
        juce::File (prefix + "-checks.txt").replaceWithText (checks + (allOk ? "result=OK\n" : "result=FAIL\n"));
        return allOk;
    }

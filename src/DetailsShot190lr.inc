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
        if (project.isNotEmpty())
        {
            // ---- Pattern layout, the FL demo: the Mixer's View › Waveforms and FL's polarity / swap icons
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (1920, 1080);
            ui.openFile (juce::File (project));
            offline.dispatchPendingMessages();
            ui.selectTabForSnapshot (3);
            offline.dispatchPendingMessages();
            auto& strips = ui.getMixerForSnapshot().getStripsForSnapshot();
            const auto inv = strips.invertIconForSnapshot (1);
            check ("the polarity and swap icons beside the stereo knob (Wide layout)", ! inv.isEmpty() && ! strips.swapIconForSnapshot (1).isEmpty());
            offline.editSong ([] (Song& s) {
                if (s.mixer.size() > 2)
                    s.mixer[2].invertPhase = s.mixer[2].swapChannels = true;
            });
            offline.dispatchPendingMessages();
            const auto icons = juce::Rectangle<int> (strips.invertIconForSnapshot (1).getX() - 4, inv.getY() - 6, 120, 34);
            savePng (strips.createComponentSnapshot (icons.getIntersection (strips.getLocalBounds()), true, 3.0f), prefix + "-mixer-phase-swap@3x.png");
            write (strips, "mixer-phase-swap");
            strips.setMeterWaveforms (true);
            offline.getTransport().playing.store (true);
            double ms = 1000.0;
            for (int f = 0; f < 90; ++f)
            {
                for (int k = 0; k < maxMixerTracks; ++k)
                {
                    const auto wobble = 0.5f + 0.5f * std::sin ((float) f * 0.37f + (float) k);
                    offline.getMeters().accumulate (k, (k == 0 ? 1.0f : 0.25f + 0.12f * (float) (k % 5)) * wobble, (k == 0 ? 0.9f : 0.22f + 0.1f * (float) (k % 5)) * wobble);
                }
                strips.frameForSnapshot (ms += 1000.0 / 60.0);
            }
            offline.getTransport().playing.store (false);
            check ("Waveforms: on (FL's View menu)", MixerStrips::meterWaveforms);
            write (strips, "mixer-waveforms");
            const auto wells = strips.meterAreaForSnapshot (0).getUnion (strips.meterAreaForSnapshot (4)).expanded (4);
            savePng (strips.createComponentSnapshot (wells.getIntersection (strips.getLocalBounds()), true, 3.0f), prefix + "-mixer-waveforms@3x.png");
            strips.setMeterWaveforms (false);
            // the Browser's location history (FL's Backspace / Shift+Backspace; Live's Ctrl+[ / Ctrl+])
            auto& browser = ui.getBrowserForSnapshot();
            const auto a = juce::File::getSpecialLocation (juce::File::tempDirectory), b = a.getParentDirectory();
            browser.noteLocation (a);
            browser.noteLocation (b);
            check ("history: two places, at the last", browser.historySizeForCheck() == 2 && browser.historyAtForCheck() == 1);
            browser.walkHistory (-1);
            check ("Backspace: back to the first", browser.historyAtForCheck() == 0);
            browser.walkHistory (1);
            check ("Shift+Backspace: forward again", browser.historyAtForCheck() == 1);
        }
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
                    // BandLab's V tool: a softer step is drawn darker
                    for (int st = 0; st < 32; ++st)
                        if (offline.getSong().patterns[(std::size_t) pattern].hasStep (kick, st))
                        {
                            dock->stepsVelocityForCheck (0, st, 0.3f);
                            offline.dispatchPendingMessages();
                            bool soft = false;
                            for (const auto& n : offline.getSong().patterns[(std::size_t) pattern].notes)
                                soft = soft || (n.channel == kick && n.start == (std::int64_t) st * ticksPerStep && std::abs (n.velocity - 0.3f) < 1.0e-4f);
                            check ("V tool: the step's velocity 0.3", soft);
                            break;
                        }
                    dock->stepsVelocityToolForSnapshot (true);
                    // the playing step (the song plays it) lights its column
                    dock->stepsPlayingStepForSnapshot (4);
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
                    dock->stepsAddClipForCheck(); // (BandLab: an empty pattern is not put on the track)
                    check ("Add on an empty pattern: nothing added", offline.getSong().playlist.back().pattern != b);
                    dock->stepsClickForCheck (0, 0);
                    dock->stepsClickForCheck (0, 8);
                    offline.dispatchPendingMessages();
                    const auto clips = offline.getSong().playlist.size();
                    dock->stepsAddClipForCheck();
                    offline.dispatchPendingMessages();
                    check ("Add B: a clip of B on the track at the playhead (what it covers is overwritten)",
                           offline.getSong().playlist.size() > 0 && offline.getSong().playlist.back().pattern == b
                               && offline.getSong().playlist.back().track == drums && offline.getSong().playlist.back().start == 0);
                    juce::ignoreUnused (clips);
                    write (*dock, "simple-drum-machine-pattern-b");
                    // ▶ plays the pattern on its own (Pattern mode), ■ gives the song back
                    dock->stepsLetterForCheck (0);
                    dock->stepsPreviewForCheck();
                    check ("play: the pattern plays on its own", dock->stepsPreviewingForCheck() && offline.getSong().mode == PlayMode::pattern
                                                                && offline.getSong().currentPattern == pattern);
                    dock->stepsPreviewForCheck();
                    check ("stop: the song mode again", ! dock->stepsPreviewingForCheck() && offline.getSong().mode == PlayMode::song);
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
            if (gridView != nullptr && set.isNotEmpty())
            {
                const auto& song = offline.getSong();
                // Live's hover: a clip's launch ▶, a track's Stop ■, a scene's ▶ grow by 2 px
                int ct = -1, cs = -1;
                for (int t = 0; t < 8 && ct < 0; ++t)
                    for (int r = 0; r < 16 && ct < 0; ++r)
                        if (launcher::clipAt (*song.clipGrid, t, r) != nullptr)
                            ct = t, cs = r;
                check ("a clip in the set", ct >= 0);
                const auto zoom = [&] (juce::Rectangle<int> r, const juce::String& name) {
                    savePng (gridView->createComponentSnapshot (r.getIntersection (gridView->getLocalBounds()), true, 4.0f), prefix + "-" + name + "@4x.png");
                };
                if (ct >= 0)
                {
                    const auto cell = clipGridCellForSnapshot (*gridView, 0, ct, cs);
                    zoom (cell.withWidth (60), "hover-none-clip");
                    clipGridHoverForSnapshot (*gridView, 1, ct, cs);
                    zoom (cell.withWidth (60), "hover-clip-launch");
                    clipGridHoverForSnapshot (*gridView, 2, ct, -1);
                    zoom (clipGridCellForSnapshot (*gridView, 1, ct, 0).withWidth (60), "hover-track-stop");
                    clipGridHoverForSnapshot (*gridView, 3, -1, 0);
                    zoom (clipGridCellForSnapshot (*gridView, 2, 0, 0).withWidth (60), "hover-scene-launch");
                    clipGridHoverForSnapshot (*gridView, 0, -1, -1);
                }
                // a rectangle selection: Shift+click from slot (0, 0) to (2, 2) selects 9 slots
                clipGridSelectRangeForSnapshot (*gridView, 0, 0, 2, 2);
                check ("a rectangle selection of 3 x 3 slots", clipGridSelectedCountForSnapshot (*gridView) == 9);
                write (*gridView, "grid-range-selection");
                // several tracks selected (Shift+click on titles): the mixer strips selected together
                clipGridSelectTracksForSnapshot (*gridView, { 0, 1, 2 });
                check ("three tracks selected together", clipGridSelectedTracks (*gridView).size() == 3);
                // the Session mixer: RMS under the peak, the automated volume's LED
                if (auto* mixer = clipGridMixer (*gridView))
                {
                    for (int i = 0; i < 6; ++i)
                        mixer->setMeterForSnapshot (i, 0.5f + 0.08f * (float) i, 0.45f + 0.08f * (float) i, 0.7f, 0.65f, 0.8f);
                    write (*mixer, "session-mixer-rms");
                }
                // the Device View: a meter after each device (the first track with an insert)
                if (auto* detail = ui.getDetailViewForSnapshot())
                {
                    detail->showSelection (ct >= 0 ? ct : 0, cs >= 0 ? cs : 0);
                    detail->setPage (DetailView::Page::devices);
                    offline.dispatchPendingMessages();
                    detail->deviceLevelsForSnapshot (0.6f, 0.5f);
                    write (*detail, "device-view-meters");
                    line ("device cards: " + juce::String (detail->cardCountForCheck()));
                }
                // a Drum Rack's pads: a sounding pad's ▶ in Live's orange
                for (int c = 0; c < (int) song.channels.size(); ++c)
                    if (rack::isDrumRack (song.channels[(std::size_t) c]))
                    {
                        auto view = RackWindows::makeChannelView (offline, c, true);
                        view->setSize (std::max (420, view->preferredWidth (210)), 232);
                        const auto* r = rack::rackOf (song.channels[(std::size_t) c].patcher);
                        int lit = 0;
                        for (int key = rack::drumFirstPad; key < rack::drumFirstPad + 16 && lit < 2; ++key)
                            if (rack::padChain (*r, key) >= 0)
                                view->padLitForSnapshot (key), ++lit;
                        check ("a Drum Rack with pads to light", lit > 0);
                        write (*view, "drumrack-pads-lit");
                        break;
                    }
                // Delete clears every slot of the rectangle (one undo step)
                clipGridSelectRangeForSnapshot (*gridView, 0, 0, 1, 1);
                clipGridKeyForSnapshot (*gridView, juce::KeyPress (juce::KeyPress::deleteKey));
                offline.dispatchPendingMessages();
                bool cleared = true;
                for (int t = 0; t <= 1; ++t)
                    for (int r = 0; r <= 1; ++r)
                        cleared = cleared && launcher::clipAt (*offline.getSong().clipGrid, t, r) == nullptr;
                check ("Delete clears the 2 x 2 rectangle", cleared);
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

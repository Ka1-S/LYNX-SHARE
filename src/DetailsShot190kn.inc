    // --details190knshot <prefix> <project.flp> [set.als] (round 190kn, the details190if DETAILS pass): FL's output panel
    // (stereo bars, level at the pointer) and CPU panel (running graph, menu), the hint bar's copy menu, the Mixer's mute
    // switch modifiers (locked state, Unmute All) and the level under the pointer on a strip meter; BandLab's chain preset
    // ‹ › and bypass in the Simple dock, the greyed Undo / Redo; with the Live set, the clip's Rename / colour and Cancel
    // Scene Launch. <prefix>-checks.txt lists the checks. Included in the app class of Main.cpp.
    bool runDetailsShot190kn (const juce::String& prefix, const juce::String& project, const juce::String& set)
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
        const auto size = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("LYNX_SNAPSHOT_SIZE", "1920x1080"), "x", "");
        using namespace lynx;
        {
            // ---- Pattern layout, the FL demo: the toolbar's output and CPU panels, the hint bar, the Mixer
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (juce::jlimit (800, 3840, size[0].getIntValue()), juce::jlimit (450, 2160, size[1].getIntValue()));
            if (project.isNotEmpty())
                ui.openFile (juce::File (project));
            offline.dispatchPendingMessages();
            ui.selectTabForSnapshot (2);
            offline.dispatchPendingMessages();
            // (a wide window, so the toolbar has room for every panel: at 1920 with Thief's title the output panel is
            // one of the groups the toolbar drops first)
            const auto normalSize = ui.getBounds();
            ui.setSize (2560, normalSize.getHeight());
            offline.dispatchPendingMessages();
            const auto toolbar = ui.getToolbarBoundsForSnapshot();
            auto& out = ui.getOutputForSnapshot();
            check ("toolbar: the output and CPU panels shown", out.isVisible() && ui.getPerfMeterForSnapshot().isVisible());
            auto& cpu = ui.getPerfMeterForSnapshot();
            const auto right = juce::Rectangle<int> (juce::jmin (cpu.getX(), out.getX()) - 8, 0,
                                                     juce::jmax (cpu.getRight(), out.getRight()) + 8 - (juce::jmin (cpu.getX(), out.getX()) - 8), toolbar.getHeight());
            // a CPU history as FL's (a flat run with a spike near the end), 60 updates
            for (int k = 0; k < 60; ++k)
                cpu.setValues (k == 52 ? 0.62 : k == 53 ? 0.35 : 0.12 + 0.02 * std::sin ((double) k * 0.7), 910ll * 1024 * 1024, 9);
            check ("CPU panel: the running graph has its points (" + juce::String (cpu.historyCount()) + ")", cpu.historyCount() == 60 && ! cpu.graphArea().isEmpty());
            out.setSidesForShot (0.72f, 0.48f, false, false);
            saveArea (ui, right, prefix + "-tb-meters-playing.png");
            out.setSidesForShot (1.0f, 0.55f, true, false);
            saveArea (ui, right, prefix + "-tb-meters-clipleft.png");
            out.setSidesForShot (0.0f, 0.0f, false, false);
            saveArea (ui, right, prefix + "-tb-meters-silent.png");
            // the level at the pointer on the bars (FL: hovering a peak meter shows the value at the pointer position)
            const auto bars = out.barsArea();
            const auto at = juce::Point<int> ((int) (bars.getX() + bars.getWidth() * 0.75f), (int) bars.getCentreY());
            const auto hint = out.hintAt (at);
            line ("output bars hint: " + hint.text + " | " + hint.value);
            const auto expected = toolbar::OutputMonitor::levelAt (((float) at.x + 0.5f - bars.getX()) / bars.getWidth());
            check ("output bars: the value is the level at the pointer (" + hint.value + ", expected " + juce::String (expected, 1) + " dB)",
                   hint.value.contains (" dB") && std::abs (hint.value.getFloatValue() - expected) < 0.06f);
            check ("output bars: left above right, a 1 px gap", out.barArea (0).getBottom() < out.barArea (1).getY());
            ui.setHintForSnapshot (hint);
            saveArea (ui, toolbar, prefix + "-tb-hint-bars.png");
            const auto cpuHint = cpu.hintAt ({ 4, 4 });
            line ("CPU hint: " + cpuHint.text);
            check ("CPU panel hint names the double-click", cpuHint.text.contains (lynx::tr ("[Double-click] plugin performance monitor  [RMB] refresh rate")));
            menuShot (cpu.makeMenu(), "CPU", "tb-cpu-menu");
            int rows = 0;
            for (juce::PopupMenu::MenuItemIterator it (cpu.makeMenu()); it.next();)
                rows += it.getItem().isSeparator || it.getItem().isSectionHeader ? 0 : 1;
            // (rollup step 8: round 190ld added FL's Misc rows View voice count, View FPS and the thread count: --details190ldshot)
            check ("CPU menu: Slow, Medium, Fast, the monitor and round 190ld's three Misc rows (" + juce::String (rows) + " items)", rows == 7);
            // the hint bar's right-click menu copies the value shown
            ui.setHintForSnapshot ({ "Master volume", "-3.2 dB", false, false });
            const auto copy = ui.getHintBarForSnapshot().copyMenu();
            juce::String first;
            for (juce::PopupMenu::MenuItemIterator it (copy); it.next();)
                if (first.isEmpty())
                    first = it.getItem().text;
            check ("hint bar menu: \"" + first + "\"", first.contains ("-3.2 dB"));
            {
                // the right-click as the hint bar builds it: FL's toolbars menu (FL 2026 opens it there), then LYNX's copy items
                auto whole = ui.getToolbarMenuForSnapshot();
                whole.addSeparator();
                for (juce::PopupMenu::MenuItemIterator it (copy); it.next();)
                    whole.addItem (it.getItem());
                menuShot (whole, "Hint bar", "hint-copy-menu");
            }

            // the Mixer: a locked track (2), a soloed one (1), the hint at a mute switch and at a strip meter
            ui.setBounds (normalSize);
            ui.selectTabForSnapshot (3);
            offline.dispatchPendingMessages();
            auto& strips = ui.getMixerForSnapshot().getStripsForSnapshot();
            offline.editSong ([] (Song& s) {
                if (s.mixer.size() > 4)
                {
                    s.mixer[2].lockState = true;
                    s.mixer[3].muted = true;
                    s.mixer[4].solo = true;
                }
            });
            offline.dispatchPendingMessages();
            const auto audible = offline.getSong().soloAudible();
            check ("Mixer: a locked track stays audible under a solo elsewhere", audible.size() > 4 && audible[2] && audible[4]);
            ui.getMixerForSnapshot().resized();
            write (ui.getMixerForSnapshot(), "mixer-locked");
            const auto mute = strips.muteAreaForSnapshot (2);
            saveArea (strips, mute.withSizeKeepingCentre (120, 60), prefix + "-mixer-mute-locked.png");
            const auto muteHint = strips.hintAt (mute.getCentre());
            line ("mute hint: " + muteHint.text + " | " + muteHint.value);
            check ("Mixer mute hint lists FL's modifiers and the lock", muteHint.text.contains ("Shift+click") && muteHint.value == lynx::tr ("State locked"));
            const auto meter = strips.meterAreaForSnapshot (4);
            const auto meterHint = strips.hintAt ({ meter.getX() + 2, meter.getBottom() - meter.getHeight() / 2 });
            line ("meter hint: " + meterHint.text + " | " + meterHint.value);
            check ("Mixer meter: the level under the pointer (about -27 dB at half height: " + meterHint.value + ")",
                   meterHint.value.contains (" dB") && std::abs (meterHint.value.getFloatValue() + 27.0f) < 1.0f);
            ui.setHintForSnapshot (meterHint);
            saveArea (ui, ui.getToolbarBoundsForSnapshot(), prefix + "-mixer-hint-meter.png");
            {
                MixerTrack locked;
                locked.lockState = true;
                check ("the lock is saved with the project (and absent when off)",
                       ProjectFile::mixerTrackFromVar (ProjectFile::mixerTrackToVar (locked)).lockState
                           && ! ProjectFile::mixerTrackFromVar (ProjectFile::mixerTrackToVar (MixerTrack {})).lockState);
            }
            offline.editSong ([] (Song& s) { mixerops::unmuteAll (s); });
            offline.dispatchPendingMessages();
            check ("Unmute All: the solo cleared, the locked track's state kept", ! offline.getSong().mixer[4].solo && ! offline.getSong().mixer[3].muted);
        }
        {
            // ---- Simple layout: BandLab's ‹ preset › and the chain bypass; Undo / Redo greyed
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
                simple->hideChooser();
                offline.editSong ([] (Song& s) {
                    simpletracks::Request q;
                    q.kind = simpletracks::Kind::voice;
                    q.name = "Lead vocal";
                    simpletracks::addTrack (s, q);
                });
                offline.dispatchPendingMessages();
                simple->hideChooser();
                int voice = -1;
                for (int t = 0; t < (int) offline.getSong().playlistTracks.size() && voice < 0; ++t)
                    if (const auto kind = simpletracks::kindOf (offline.getSong(), t); kind && *kind == simpletracks::Kind::audio)
                        voice = t;
                ui.getPlaylistForSnapshot().selectTrack (voice);
                offline.dispatchPendingMessages();
                auto* dock = simple->dockForSnapshot();
                check ("the dock on the voice track's effects", dock != nullptr && voice >= 0);
                if (dock != nullptr)
                {
                    dock->setTab (SimpleDock::Tab::effects);
                    const auto insert = dock->insertForCheck();
                    if (const auto* preset = chains::find ("Clear Voice"))
                        offline.editSongStructural ([insert, preset] (Song& s) { chains::apply (s, insert, *preset); });
                    offline.dispatchPendingMessages();
                    dock->refresh();
                    const auto shots = [&] (const juce::String& name) {
                        offline.dispatchPendingMessages();
                        dock->refresh();
                        write (*dock, "simple-dock-" + name);
                        savePng (dock->createComponentSnapshot ({ 0, 0, juce::jmin (dock->getWidth(), 260), dock->getHeight() }, true, 2.0f),
                                 prefix + "-simple-side-" + name + "@2x.png");
                    };
                    shots ("preset");
                    const auto name = [&] { return juce::String (offline.getSong().mixer[(std::size_t) insert].chainPreset); };
                    const auto before = name();
                    dock->stepPresetForCheck (1);
                    offline.dispatchPendingMessages();
                    const auto after = name();
                    line ("preset > : " + before + " -> " + after);
                    check ("> steps to the next preset in the menu's order", after == juce::String (chains::stepped (before.toStdString(), 1)->name));
                    dock->stepPresetForCheck (-1);
                    offline.dispatchPendingMessages();
                    check ("< steps back", name() == before);
                    dock->bypassChainForCheck();
                    offline.dispatchPendingMessages();
                    dock->refresh();
                    check ("bypass: every effect of the chain off, the button lit",
                           ! mixerops::anyEffectEnabled (offline.getSong(), insert) && dock->bypassLitForCheck());
                    shots ("bypassed");
                    dock->bypassChainForCheck();
                    offline.dispatchPendingMessages();
                    dock->refresh();
                    check ("bypass again: the chain back on", mixerops::anyEffectEnabled (offline.getSong(), insert) && ! dock->bypassLitForCheck());
                    // the top bar: Undo lit (edits made), Redo greyed
                    ui.transportTickForSnapshot();
                    saveArea (ui, ui.getToolbarBoundsForSnapshot(), prefix + "-simple-topbar.png");
                    auto* undo = ui.toolbarPartForSnapshot ("undo");
                    auto* redo = ui.toolbarPartForSnapshot ("redo");
                    check ("Simple top bar: Undo enabled, Redo greyed", undo != nullptr && redo != nullptr && undo->isEnabled() && ! redo->isEnabled());
                }
            }
        }
        if (set.isNotEmpty())
        {
            // ---- Clip-launch layout, the Live set: a clip's Rename / Change color, Cancel Scene Launch
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (juce::jlimit (800, 3840, size[0].getIntValue()), juce::jlimit (450, 2160, size[1].getIntValue()));
            ui.setVisible (true);
            ui.selectTabForSnapshot (2);
            ui.setClipLaunchWorkflow (true);
            offline.dispatchPendingMessages();
            ui.openFile (juce::File (set));
            offline.dispatchPendingMessages();
            ui.setClipGridView (PlaylistArea::GridView::grid);
            offline.dispatchPendingMessages();
            ui.resized();
            auto* gridView = ui.getClipGridForSnapshot();
            const auto& song = offline.getSong();
            check ("grid", gridView != nullptr && song.clipGrid.has_value());
            if (gridView != nullptr && song.clipGrid.has_value())
            {
                auto& grid = *gridView;
                int t0 = -1, s0 = -1;
                for (int t = 0; t < 8 && t0 < 0; ++t)
                    for (int r = 0; r < 32 && t0 < 0; ++r)
                        if (launcher::clipAt (*song.clipGrid, t, r) != nullptr)
                        {
                            t0 = t;
                            s0 = r;
                        }
                check ("a clip in the set", t0 >= 0);
                if (t0 >= 0)
                {
                    const auto menu = clipGridSlotMenu (grid, t0, s0);
                    bool rename = false, colour = false;
                    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                    {
                        rename = rename || it.getItem().text.startsWith (lynx::tr ("Rename"));
                        colour = colour || (it.getItem().text == lynx::tr ("Change color") && it.getItem().subMenu != nullptr);
                    }
                    check ("clip menu: Rename and Change color (Live's clip menu)", rename && colour);
                    menuShot (menu, "Clip", "grid-clip-menu");
                    offline.editSong ([t0, s0] (Song& s) {
                        auto* slot = launcher::settingsAt (*s.clipGrid, t0, s0);
                        slot->name = "Renamed clip";
                        slot->colour = 0xffe5383b;
                    });
                    offline.dispatchPendingMessages();
                    refreshClipGrid (grid);
                    grid.repaint();
                    saveArea (grid, clipGridCellForSnapshot (grid, 0, t0, s0).expanded (40, 10), prefix + "-grid-clip-renamed.png");
                }
                // Live's count-in in the control bar's position field (DL-44): 2 bars of 4/4, a beat and an eighth in
                {
                    const auto bar = (double) song.barTicks();
                    offline.getTransport().countIn.store (2.0 * bar - 1.5 * (double) lynx::ppq);
                    ui.transportTickForSnapshot();
                    auto* position = ui.toolbarPartForSnapshot ("position");
                    if (position != nullptr)
                    {
                        position->repaint();
                        saveArea (ui, position->getBounds().expanded (60, 4), prefix + "-live-countin.png");
                        saveArea (ui, ui.getToolbarBoundsForSnapshot(), prefix + "-live-countin-bar.png");
                    }
                    const auto shown = position != nullptr ? dynamic_cast<toolbar::TimeDisplay*> (position) : nullptr;
                    const auto text = shown != nullptr ? shown->textForSnapshot() : juce::String();
                    line ("count-in position: " + text);
                    check ("Live count-in: the position field counts -2. 2. 3 (" + text + ")", text == "-2 : 2 : 3");
                    offline.getTransport().countIn.store (0.0);
                    ui.transportTickForSnapshot();
                }
                // a scene launched and waiting: Cancel Scene Launch is enabled and ends the wait
                const auto scene = juce::jmax (0, s0);
                launchClipGridSceneForSnapshot (grid, scene);
                const auto queued = clipGridSceneLaunchQueued (grid);
                const auto sceneMenu = clipGridSceneMenu (grid, scene);
                bool enabled = false;
                for (juce::PopupMenu::MenuItemIterator it (sceneMenu); it.next();)
                    if (it.getItem().text == lynx::tr ("Cancel Scene Launch"))
                        enabled = it.getItem().isEnabled;
                check ("scene menu: Cancel Scene Launch enabled while the scene waits", queued && enabled);
                menuShot (sceneMenu, "Scene", "grid-scene-menu");
                cancelClipGridSceneLaunch (grid);
                check ("Cancel Scene Launch: the wait ends", ! clipGridSceneLaunchQueued (grid));
                write (grid, "grid-after-cancel");
            }
        }
        juce::File (prefix + "-checks.txt").replaceWithText (checks + "result=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

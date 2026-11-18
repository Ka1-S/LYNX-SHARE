    // --details190ldshot <prefix> <project.flp> [set.als] (round 190ld, details pass 2): FL's CPU panel Misc (View FPS),
    // the MIDI light's colours and Sync beat, the Count-in / Wait for input menus, the FX slot lock, the Playlist track
    // lock hint, the FL tool names; BandLab's Cycle switch, the Fx pill with the chain preset, the record dock's monitor
    // switch; with the Live set, the track status row's Arrangement miniature and monitor icon. <prefix>-checks.txt lists
    // the checks. Included in the app class of Main.cpp.
    bool runDetailsShot190ld (const juce::String& prefix, const juce::String& project, const juce::String& set)
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
        const auto size = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("LYNX_SNAPSHOT_SIZE", "1920x1080"), "x", "");
        using namespace lynx;
        {
            // ---- Pattern layout, the FL demo
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (juce::jlimit (800, 3840, size[0].getIntValue()), juce::jlimit (450, 2160, size[1].getIntValue()));
            if (project.isNotEmpty())
                ui.openFile (juce::File (project));
            offline.dispatchPendingMessages();
            ui.selectTabForSnapshot (2);
            offline.dispatchPendingMessages();
            const auto normalSize = ui.getBounds();
            ui.setSize (2560, normalSize.getHeight());
            offline.dispatchPendingMessages();
            const auto toolbar = ui.getToolbarBoundsForSnapshot();
            auto& cpu = ui.getPerfMeterForSnapshot();
            // FL's CPU panel Misc: View voice count / View FPS and the info lines
            const auto cpuMenu = texts (cpu.makeMenu());
            line ("CPU menu: " + cpuMenu.joinIntoString (" | "));
            check ("CPU menu: Misc with View voice count and View FPS", cpuMenu.contains (lynx::tr ("View voice count")) && cpuMenu.contains (lynx::tr ("View FPS")));
            check ("CPU menu: the thread count", cpuMenu.joinIntoString ("|").contains (lynx::tr ("Threads: {0}", { "" }).trimEnd()));
            menuShot (cpu.makeMenu(), "CPU", "tb-cpu-menu");
            for (int k = 0; k < 40; ++k)
                cpu.setValues (0.12, 910ll * 1024 * 1024, 9);
            const auto area = cpu.getBounds().expanded (6, 0).withY (0).withHeight (toolbar.getHeight());
            saveArea (ui, area, prefix + "-tb-cpu-voices.png");
            toolbar::PerformanceMeter::secondView = toolbar::PerformanceMeter::framesPerSecond;
            cpu.setFramesPerSecond (60);
            cpu.setValues (0.12, 910ll * 1024 * 1024, 9);
            saveArea (ui, area, prefix + "-tb-cpu-fps.png");
            toolbar::PerformanceMeter::secondView = toolbar::PerformanceMeter::voiceCount;
            cpu.setValues (0.12, 910ll * 1024 * 1024, 9);

            // the MIDI light: FL's colours (orange = a project link, blue = a global link, green = nothing linked) and the
            // Sync light's beat / bar blinks
            {
                toolbar::MidiActivityLight light;
                light.setSize (22, 22);
                light.update (0, { "Keys" });
                light.update (1, { "Keys" });
                check ("MIDI light: unlinked input = green", light.isLit() && light.litKind() == toolbar::MidiActivityLight::Kind::unhandled);
                savePng (light.createComponentSnapshot (light.getLocalBounds(), true, 4.0f), prefix + "-midi-unhandled@4x.png");
                light.update (2, { "Keys" }, 1, 0);
                check ("MIDI light: a project link took it = orange", light.litKind() == toolbar::MidiActivityLight::Kind::project);
                savePng (light.createComponentSnapshot (light.getLocalBounds(), true, 4.0f), prefix + "-midi-project@4x.png");
                light.update (3, { "Keys" }, 1, 1);
                check ("MIDI light: a global link took it = blue", light.litKind() == toolbar::MidiActivityLight::Kind::global);
                savePng (light.createComponentSnapshot (light.getLocalBounds(), true, 4.0f), prefix + "-midi-global@4x.png");
                for (int k = 0; k < 8; ++k)
                    light.update (3, { "Keys" }, 1, 1);
                check ("MIDI light: off again", ! light.isLit());
                light.beat (true);
                check ("Sync light: blue on a bar's first beat", light.isLit() && light.litKind() == toolbar::MidiActivityLight::Kind::bar);
                savePng (light.createComponentSnapshot (light.getLocalBounds(), true, 4.0f), prefix + "-midi-sync-bar@4x.png");
                for (int k = 0; k < 4; ++k)
                    light.update (3, { "Keys" }, 1, 1);
                light.beat (false);
                check ("Sync light: a blink on the other beats", light.isLit() && light.litKind() == toolbar::MidiActivityLight::Kind::beat);
            }

            // FL's Count-in and Wait for input right-click menus
            {
                const auto countIn = texts (ui.countInMenuForSnapshot());
                check ("Count-in right-click: the bar lengths (" + countIn.joinIntoString (", ") + ")", countIn.size() == 4 && countIn.contains (lynx::tr ("2 bars")));
                menuShot (ui.countInMenuForSnapshot(), "Count-in", "countin-menu");
                const auto wait = texts (ui.waitInputMenuForSnapshot());
                check ("Wait for input right-click: FL's two options", wait.contains (lynx::tr ("Only when recording")) && wait.contains (lynx::tr ("Respect recording filter")));
                menuShot (ui.waitInputMenuForSnapshot(), "Wait for input", "wait-menu");
            }

            // the FX slot lock (Shift+click on the slot LED): a padlock in the LED's place; the slot solo leaves it on
            ui.setBounds (normalSize);
            ui.selectTabForSnapshot (3);
            offline.dispatchPendingMessages();
            int fxTrack = -1;
            for (int t = 1; t < (int) offline.getSong().mixer.size() && fxTrack < 0; ++t)
                if (offline.getSong().mixer[(std::size_t) t].effects.size() >= 2)
                    fxTrack = t;
            check ("a mixer track with two effects or more", fxTrack > 0);
            if (fxTrack > 0)
            {
                offline.editSong ([fxTrack] (Song& s) {
                    mixerops::toggleSlotLock (s, fxTrack, 1);
                    mixerops::soloSlot (s, fxTrack, 0);
                });
                offline.dispatchPendingMessages();
                const auto& fx = offline.getSong().mixer[(std::size_t) fxTrack].effects;
                check ("slot solo: the locked slot 2 stays on, slot 1 on", fx[0].enabled && fx[1].enabled && fx[1].lockState);
                {
                    MixerTrack withLock, without;
                    withLock.effects.resize (1);
                    without.effects.resize (1);
                    withLock.effects[0].lockState = true;
                    const auto a = ProjectFile::mixerTrackFromVar (ProjectFile::mixerTrackToVar (withLock));
                    const auto b = ProjectFile::mixerTrackFromVar (ProjectFile::mixerTrackToVar (without));
                    check ("the slot lock is saved with the project (and absent when off)",
                           a.effects.size() == 1 && a.effects[0].lockState && b.effects.size() == 1 && ! b.effects[0].lockState);
                }
                ui.getMixerForSnapshot().selectTrack (fxTrack);
                ui.getMixerForSnapshot().resized();
                offline.dispatchPendingMessages();
                write (ui.getMixerForSnapshot(), "mixer-slot-locked");
            }

            // the Playlist: a locked track's hint; the tool names
            ui.selectTabForSnapshot (2);
            offline.dispatchPendingMessages();
            offline.editSong ([] (Song& s) {
                if (s.playlistTracks.size() < 3)
                    s.playlistTracks.resize (3);
                s.playlistTracks[1].soloLock = true;
            });
            offline.dispatchPendingMessages();
            {
                Song copy = offline.getSong();
                noteedit::soloTrack (copy, 0, (int) copy.playlistTracks.size());
                check ("Playlist: a solo leaves the locked track 2 unmuted", ! copy.playlistTracks[1].muted && copy.playlistTracks[2].muted);
                juce::String error;
                const auto back = ProjectFile::fromJson (ProjectFile::toJson (offline.getSong(), false), error);
                check ("Playlist: the track lock is saved with the project", back.has_value() && back->playlistTracks.size() > 1 && back->playlistTracks[1].soloLock
                                                                         && ! back->playlistTracks[0].soloLock);
            }
            const auto tools = juce::StringArray { lynx::tr (editTools()[0].name), lynx::tr (editTools()[1].name), lynx::tr (editTools()[3].name),
                                                   lynx::tr (editTools()[5].name), lynx::tr (editTools()[6].name), lynx::tr (editTools()[8].name) };
            line ("tools: " + tools.joinIntoString (", "));
            check ("FL's tool names (owner E1)", juce::String (editTools()[0].name) == "Draw" && juce::String (editTools()[5].name) == "Slice"
                                                     && juce::String (editTools()[8].name) == "Playback");
            write (ui.getPlaylistForSnapshot(), "playlist");
        }
        {
            // ---- Simple layout: BandLab's Cycle switch, the Fx pill with the chain preset, the record dock's monitor switch
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
                // Cycle: off → on makes a one-bar range at the song position and loops it; again: the range stays, off
                ui.transportTickForSnapshot();
                saveArea (ui, ui.getToolbarBoundsForSnapshot(), prefix + "-simple-topbar.png");
                ui.toggleCycleForSnapshot();
                offline.dispatchPendingMessages();
                ui.transportTickForSnapshot();
                const auto& song = offline.getSong();
                check ("Cycle on: a one-bar loop range at the start, looping", song.loopPlays() && song.loopStart == 0 && song.loopEnd == song.barTicks());
                auto* cycle = ui.toolbarPartForSnapshot ("cycle");
                check ("Cycle switch lit and in the capsule", cycle != nullptr && cycle->isVisible() && static_cast<juce::Button*> (cycle)->getToggleState());
                saveArea (ui, ui.getToolbarBoundsForSnapshot(), prefix + "-simple-topbar-cycle.png");
                if (cycle != nullptr)
                    savePng (ui.createComponentSnapshot (cycle->getBounds().expanded (140, 6), true, 2.0f), prefix + "-simple-cycle@2x.png");
                ui.toggleCycleForSnapshot();
                offline.dispatchPendingMessages();
                ui.transportTickForSnapshot();
                check ("Cycle off: the range kept, not looping", ! offline.getSong().loopPlays() && offline.getSong().hasLoop()
                                                                    && ! static_cast<juce::Button*> (cycle)->getToggleState());

                // BandLab's metronome popover from the ▾ beside the metronome (DS-1)
                {
                    auto* chevron = ui.toolbarPartForSnapshot ("metronomeMenu");
                    check ("the metronome capsule has BandLab's ▾", chevron != nullptr && chevron->isVisible());
                    auto popover = ui.makeMetronomePopoverForSnapshot();
                    popover->setVisible (true);
                    popover->resized();
                    write (*popover, "simple-metronome-popover");
                    savePng (popover->createComponentSnapshot (popover->getLocalBounds(), true, 2.0f), prefix + "-simple-metronome-popover@2x.png");
                }

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
                check ("the dock on the voice track", dock != nullptr && voice >= 0);
                if (dock != nullptr && voice >= 0)
                {
                    dock->setTab (SimpleDock::Tab::effects);
                    const auto insert = dock->insertForCheck();
                    if (const auto* preset = chains::find ("Clear Voice"))
                        offline.editSongStructural ([insert, preset] (Song& s) { chains::apply (s, insert, *preset); });
                    offline.dispatchPendingMessages();
                    auto& playlist = ui.getPlaylistForSnapshot();
                    playlist.repaint();
                    const auto pill = playlist.trackFxForSnapshot (voice);
                    line ("Fx pill: " + playlist.simpleFxPresetForSnapshot (voice) + " " + pill.toString());
                    check ("Fx pill names the chain preset and grows to fit it", playlist.simpleFxPresetForSnapshot (voice) == "Clear Voice" && pill.getWidth() > 50);
                    write (playlist, "simple-playlist-fxpill");
                    savePng (playlist.createComponentSnapshot (pill.expanded (220, 30).getIntersection (playlist.getLocalBounds()), true, 2.0f),
                             prefix + "-simple-fxpill@2x.png");
                    // the record dock: the input level with BandLab's monitor switch
                    offline.editSong ([insert] (Song& s) {
                        if (insert > 0 && insert < (int) s.mixer.size())
                            s.mixer[(std::size_t) insert].inputChannel = 0;
                    });
                    offline.setInsertArmed (insert, true);
                    offline.dispatchPendingMessages();
                    dock->inputLevelForSnapshot (0.6f);
                    dock->refresh();
                    write (*dock, "simple-dock-monitor");
                    check ("record dock: the input is heard (When armed): the switch lit", offline.getSong().mixer[(std::size_t) insert].monitor != InputMonitor::off);
                }
            }
        }
        if (set.isNotEmpty())
        {
            // ---- Clip-launch layout, the Live set: the status row's Arrangement miniature and monitor icon
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
            if (gridView != nullptr)
            {
                int withClips = 0;
                for (int t = 0; t < 8; ++t)
                    withClips += std::any_of (song.playlist.begin(), song.playlist.end(), [t] (const Clip& c) { return c.track == t && c.start == 0; }) ? 1 : 0;
                line ("tracks with an Arrangement clip at the start: " + juce::String (withClips));
                refreshClipGrid (*gridView);
                write (*gridView, "grid-status");
                // a MIDI track locked to a controller (Live: Monitor In): the keyboard icon; an audio track monitoring On: the
                // microphone; an armed track on Auto keeps its miniature (Live 12.4, raw/L09)
                int midiTrack = -1, audioTrack = -1;
                for (int t = 0; t < 16; ++t)
                {
                    if (midiTrack < 0 && offline.gridTrackInput (t) == AudioEngine::GridInput::midi && detailchain::forTrack (song, t).channel >= 0)
                        midiTrack = t;
                    if (audioTrack < 0 && detailchain::forTrack (song, t).insert > 0 && detailchain::forTrack (song, t).channel < 0)
                        audioTrack = t;
                }
                line ("monitor icons on tracks " + juce::String (midiTrack) + " (MIDI) and " + juce::String (audioTrack) + " (audio)");
                offline.editSong ([midiTrack, audioTrack] (Song& s) {
                    if (midiTrack >= 0)
                        s.channels[(std::size_t) detailchain::forTrack (s, midiTrack).channel].receiveFrom = "Keys";
                    if (audioTrack >= 0)
                        s.mixer[(std::size_t) detailchain::forTrack (s, audioTrack).insert].monitor = InputMonitor::on;
                });
                offline.dispatchPendingMessages();
                refreshClipGrid (*gridView);
                write (*gridView, "grid-status-armed");
                offline.getTransport().positionTicks.store ((double) song.barTicks() * 4.0);
                refreshClipGrid (*gridView);
                write (*gridView, "grid-status-bar5");
                offline.getTransport().positionTicks.store (0.0);
            }
        }
        juce::File (prefix + "-checks.txt").replaceWithText (checks + "result=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

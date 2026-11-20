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
                    check ("the metronome capsule has BandLab's chevron segment", chevron != nullptr && chevron->isVisible());
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
                // Live's clip and Main track menus in Live's order (the first clip of the set; scene 1)
                for (int t = 0, done = 0; t < 8 && ! done; ++t)
                    for (int r = 0; r < 32 && ! done; ++r)
                        if (launcher::clipAt (*song.clipGrid, t, r) != nullptr)
                        {
                            auto items = texts (clipGridSlotMenu (*gridView, t, r));
                            items.removeEmptyStrings(); // (the separators)
                            line ("clip menu: " + items.joinIntoString (" | "));
                            check ("clip menu starts with Live's Cut / Copy / Duplicate / Delete, then Rename",
                                   items.size() > 5 && items[0].startsWith (lynx::tr ("Cut")) && items[3].startsWith (lynx::tr ("Delete"))
                                       && items[4].startsWith (lynx::tr ("Rename")));
                            menuShot (clipGridSlotMenu (*gridView, t, r), "Clip", "grid-clip-menu");
                            done = 1;
                        }
                {
                    const auto items = texts (clipGridSceneMenu (*gridView, 0));
                    line ("scene menu: " + items.joinIntoString (" | "));
                    check ("scene menu in Live's order (Duplicate first, Cancel Scene Launch, the two prefs)",
                           items.size() > 6 && items[0].startsWith (lynx::tr ("Duplicate")) && items.contains (lynx::tr ("Cancel Scene Launch"))
                               && items.contains (lynx::tr ("Trigger Recording on Launch (Pref)")));
                    menuShot (clipGridSceneMenu (*gridView, 0), "Scene", "grid-scene-menu");
                }
                // Live's count-in: the position field filled #03c3d5 with dark digits (2 bars of 4/4, a beat and an eighth in)
                {
                    offline.getTransport().countIn.store (2.0 * (double) song.barTicks() - 1.5 * (double) lynx::ppq);
                    ui.transportTickForSnapshot();
                    if (auto* position = ui.toolbarPartForSnapshot ("position"))
                    {
                        position->repaint();
                        saveArea (ui, position->getBounds().expanded (60, 4), prefix + "-live-countin.png");
                    }
                    offline.getTransport().countIn.store (0.0);
                    ui.transportTickForSnapshot();
                }
                // the song at bar 5: the line in the middle of the miniature, the clips scrolled under it
                offline.getTransport().positionTicks.store ((double) song.barTicks() * 4.0);
                refreshClipGrid (*gridView);
                write (*gridView, "grid-status-bar5");
                offline.getTransport().positionTicks.store (0.0);
                refreshClipGrid (*gridView);
                // a MIDI track locked to a controller (Live: Monitor In): the keyboard icon; an audio track monitoring On: the
                // microphone; an armed track on Auto keeps its miniature (Live 12.4, raw/L09)
                int midiTrack = -1, audioTrack = -1;
                for (int t = 0; t < 16; ++t)
                {
                    // (a track whose channel no other column shows: the imported set gives several columns channel 0)
                    if (const auto ch = detailchain::forTrack (song, t).channel;
                        midiTrack < 0 && offline.gridTrackInput (t) == AudioEngine::GridInput::midi && ch >= 0)
                    {
                        auto shared = false;
                        for (int u = 0; u < 16 && ! shared; ++u)
                            shared = u != t && detailchain::forTrack (song, u).channel == ch;
                        if (! shared)
                            midiTrack = t;
                    }
                    if (audioTrack < 0 && detailchain::forTrack (song, t).insert > 0 && detailchain::forTrack (song, t).channel < 0)
                        audioTrack = t;
                }
                line ("monitor icons on tracks " + juce::String (midiTrack) + " (MIDI) and " + juce::String (audioTrack) + " (audio)");
                {
                    juce::String chains;
                    for (int t = 0; t < 8; ++t)
                        chains << t << ":" << detailchain::forTrack (song, t).channel << (offline.gridTrackInput (t) == AudioEngine::GridInput::midi ? "m " : "a ");
                    line ("track chains (track:channel kind): " + chains);
                }
                offline.editSong ([midiTrack, audioTrack] (Song& s) {
                    if (midiTrack >= 0)
                        s.channels[(std::size_t) detailchain::forTrack (s, midiTrack).channel].receiveFrom = "Keys";
                    if (audioTrack >= 0)
                        s.mixer[(std::size_t) detailchain::forTrack (s, audioTrack).insert].monitor = InputMonitor::on;
                });
                offline.dispatchPendingMessages();
                refreshClipGrid (*gridView);
                write (*gridView, "grid-status-armed");

            }
        }
        {
            // ---- round 190ld plugins: FL-native plugin details (Fruity Delay 3, Fruity Vocoder, Patcher, Edison,
            // Parametric EQ 2, Fruity Limiter), each editor at FL's size (scale 1)
            AudioEngine offline (false);
            const auto loadSlot = [&offline] (const EffectSlot& fx) {
                Song song;
                song.mixer.resize (2);
                song.mixer[0].name = "Master";
                song.mixer[1].name = "Insert 1";
                song.mixer[1].effects.push_back (fx);
                offline.setSong (std::move (song));
                offline.dispatchPendingMessages();
            };
            const auto paramNamed = [] (EffectKind kind, const char* name) {
                const auto& params = effectInfo (kind).params;
                for (int i = 0; i < (int) params.size(); ++i)
                    if (std::strcmp (params[(std::size_t) i].name, name) == 0)
                        return i;
                return -1;
            };

            // (1) Fruity Delay 3, TEMPO SYNC on: step dots and no value arc; the Time knob's Set ▸ menu; the Feedback
            // ring at 90 / 97 / 100 %
            {
                auto fx = makeEffectSlot (EffectKind::fruityDelay3);
                const auto sync = paramNamed (EffectKind::fruityDelay3, "Tempo sync"), feedback = paramNamed (EffectKind::fruityDelay3, "Feedback");
                check ("Delay 3: Tempo sync and Feedback parameters", sync >= 0 && feedback >= 0);
                if (sync >= 0 && feedback >= 0)
                {
                    fx.params[(std::size_t) sync] = 1.0f;
                    loadSlot (fx);
                    auto editor = makeNativeEffectEditor (offline, 1, 0, 1.0f, 0);
                    write (*editor, "plug-delay3-sync");
                    const auto menu = delay3TimeMenuForSnapshot (*editor);
                    const auto items = texts (menu);
                    int setValues = 0;
                    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                        if (it.getItem().text == lynx::tr ("Set") && it.getItem().subMenu != nullptr)
                            setValues = it.getItem().subMenu->getNumItems();
                    line ("Delay 3 Time menu: " + items.joinIntoString (" | "));
                    check ("Delay 3: the synced Time menu has Set with 14 values", setValues == 14);
                    menuShot (menu, "Time", "plug-delay3-set-menu");
                    for (const auto& [value, name] : std::initializer_list<std::pair<float, const char*>> { { 0.90f, "plug-delay3-fb-090" }, { 0.97f, "plug-delay3-fb-097" }, { 1.0f, "plug-delay3-fb-100" } })
                    {
                        auto f = fx;
                        f.params[(std::size_t) feedback] = value;
                        loadSlot (f);
                        auto e = makeNativeEffectEditor (offline, 1, 0, 1.0f, 0);
                        write (*e, name);
                    }
                }
            }

            // (2) Fruity Vocoder: the band levels of a test signal (modulator 440 Hz on L, carrier 110 Hz on R), as the
            // effect publishes them while an editor shows them
            {
                const auto slot = makeEffectSlot (EffectKind::vocoder);
                auto voc = makeEffect (EffectKind::vocoder);
                voc->prepare (48000.0);
                voc->setMetersShown (true);
                std::vector<float> l (512), r (512);
                for (int b = 0; b < 40; ++b)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        const auto t = (float) (b * 512 + i) / 48000.0f;
                        l[(std::size_t) i] = 0.5f * std::sin (6.2831853f * 440.0f * t) + 0.2f * std::sin (6.2831853f * 1900.0f * t);
                        r[(std::size_t) i] = 0.5f * std::sin (6.2831853f * 110.0f * t);
                    }
                    voc->process (l.data(), r.data(), 512, slot.params, 120.0);
                }
                std::array<std::uint32_t, 8> packed {};
                std::uint32_t lit = 0;
                for (int o = 0; o < 8; ++o)
                    lit |= packed[(std::size_t) o] = (std::uint32_t) voc->controlOutput (o);
                check ("Vocoder: the test signal lights some bands", lit != 0u);
                loadSlot (slot);
                auto editor = makeNativeEffectEditor (offline, 1, 0, 1.0f, 0);
                check ("Vocoder: the editor takes the band levels", setVocoderBandsForSnapshot (*editor, packed));
                write (*editor, "plug-vocoder-bands");
            }

            // (3) Patcher: Fruity Filter → Maximus, View › Activity + Latency + Performance after a test signal; its View menu
            {
                auto fx = makeEffectSlot (EffectKind::patcher);
                PatchNode a, b;
                a.effect = makeEffectSlot (EffectKind::fruityFilter);
                a.x = 220.0f;
                a.y = 120.0f;
                b.effect = makeEffectSlot (EffectKind::maximus);
                b.x = 440.0f;
                b.y = 200.0f;
                const auto na = patcher::addNode (fx.patcher, a), nb = patcher::addNode (fx.patcher, b);
                for (const auto& [from, to] : std::initializer_list<std::pair<int, int>> { { patchFromHost, na }, { na, nb }, { nb, patchToHost } })
                {
                    PatchWire w;
                    w.from = from;
                    w.to = to;
                    fx.patcher.wires.push_back (w);
                }
                loadSlot (fx);
                auto editor = PatcherWindows::makeSlotEditor (offline, 1, 0);
                editor->setSize (900, 560);
                const auto ran = PatcherWindows::runViewsForSnapshot (*editor, true, true, true, 0.5);
                check ("Patcher: a running graph measured with the test signal", ran);
                write (*editor, "plug-patcher-views");
                const auto view = PatcherWindows::viewMenuForSnapshot (*editor);
                line ("Patcher View menu: " + texts (view).joinIntoString (" | "));
                check ("Patcher: View menu has Activity, Latency and Performance",
                       texts (view).contains (lynx::tr ("Activity")) && texts (view).contains (lynx::tr ("Latency")) && texts (view).contains (lynx::tr ("Performance")));
                menuShot (view, "View", "plug-patcher-view-menu");
            }

            // (4) Edison: a short generated sample; the hint bar's read-out over the wave
            {
                audioedit::Document d;
                d.title = "Tone";
                d.audio.sampleRate = 44100.0;
                const auto frames = (std::size_t) 44100;
                d.audio.channels.assign (2, std::vector<float> (frames, 0.0f));
                for (std::size_t i = 0; i < frames; ++i)
                {
                    const auto env = (float) std::exp (-3.0 * (double) i / 44100.0);
                    d.audio.channels[0][i] = d.audio.channels[1][i] = 0.6f * env * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * (double) i / 44100.0);
                }
                auto editor = makeAudioEditorForSnapshot (offline, d, false);
                editor->setSize (1120, 600);
                const auto hint = audioEditorHoverHintForSnapshot (*editor, 0.4f, 0.12f);
                line ("Edison hover hint: " + hint);
                check ("Edison: the hover hint shows the level in dB", hint.contains ("dB"));
                write (*editor, "plug-edison-hover");
            }

            // (5) Parametric EQ 2: the display menu with FL's Monitor input / output
            {
                juce::PopupMenu menu;
                peqview::addDisplayItems (menu, {});
                const auto items = texts (menu);
                line ("PEQ2 display menu: " + items.joinIntoString (" | "));
                check ("PEQ2: display menu has Monitor input (pre) / output (post)",
                       items.contains (lynx::tr ("Input (pre EQ)")) && items.contains (lynx::tr ("Output (post EQ)")));
                menuShot (menu, "Display", "plug-peq2-menu");
            }

            // (6) Fruity Limiter: the scrolling graph with a synthetic history, paused by a click
            {
                loadSlot (makeEffectSlot (EffectKind::fruityLimiter));
                auto editor = makeNativeEffectEditor (offline, 1, 0, 1.0f, 0);
                check ("Limiter: the graph paused", pauseLimiterForSnapshot (*editor, 240));
                write (*editor, "plug-limiter-paused");
            }
        }
        juce::File (prefix + "-checks.txt").replaceWithText (checks + "result=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

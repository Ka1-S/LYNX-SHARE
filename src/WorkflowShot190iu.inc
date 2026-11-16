    // --workflowshot190iu <prefix> (round 190iu, issues #186 and #191): the wheel zones, temporary tools, MIDI files
    // dropped on the Playlist / the Clip-launch arrangement / the clip grid, automation clips moved into lanes, Track-only
    // mode and the picker's clean-up, driven through the real components offscreen; prefix-checks.txt. Included in the app
    // class of Main.cpp (it uses savePng / saveMenus / lookAndFeel).
    bool runWorkflowShot190iu (const juce::String& prefix)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const char* name, bool ok, const juce::String& detail = {}) {
            report.add (juce::String (name) + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        using namespace lynx;

        // a .mid file as drum / instrument plugins hand one over: two tracks ("Bass" and an unnamed drum track)
        const auto midiFile = juce::File (prefix + "-groove.mid");
        {
            juce::MidiFile file;
            file.setTicksPerQuarterNote (960);
            juce::MidiMessageSequence bass, drums;
            bass.addEvent (juce::MidiMessage::textMetaEvent (3, "Bass"), 0);
            for (int beat = 0; beat < 8; beat += 2)
            {
                bass.addEvent (juce::MidiMessage::noteOn (1, 36 + (beat % 4 == 0 ? 0 : 7), (juce::uint8) 100), beat * 960);
                bass.addEvent (juce::MidiMessage::noteOff (1, 36 + (beat % 4 == 0 ? 0 : 7)), beat * 960 + 1800);
            }
            for (int step = 0; step < 32; ++step)
            {
                const auto key = step % 8 == 0 ? 36 : step % 8 == 4 ? 38 : 42;
                drums.addEvent (juce::MidiMessage::noteOn (10, key, (juce::uint8) 90), step * 240);
                drums.addEvent (juce::MidiMessage::noteOff (10, key), step * 240 + 120);
            }
            bass.updateMatchedPairs();
            drums.updateMatchedPairs();
            file.addTrack (bass);
            file.addTrack (drums);
            midiFile.deleteFile();
            if (auto out = midiFile.createOutputStream())
                file.writeTo (*out, 1);
        }
        check ("midi_file_written", midiFile.existsAsFile(), midiFile.getFullPathName());

        ui::motion::enabled() = false; // (the wheel's glide lands at once)
        AudioEngine engine (false);
        MainComponent ui (engine, false);
        ui.setSize (1600, 900);
        {
            auto song = Song::makeDefault();
            song.playlist.clear();
            // Kick on track 1, a "Lead" instrument track on track 3, two takes and two recorded envelopes
            auto& p = song.patterns.front();
            p.name = "Beat";
            for (int step : { 0, 4, 8, 12 })
                p.toggleStep (0, step);
            Clip beat;
            beat.kind = ClipKind::pattern;
            beat.pattern = 0;
            beat.track = 0;
            beat.length = 4 * ppq;
            for (int bar = 0; bar < 4; ++bar)
            {
                beat.start = (std::int64_t) bar * 4 * ppq;
                song.playlist.push_back (beat);
            }
            song.playlistTracks.resize (10);
            const auto lead = trackmodes::addInstrumentTrack (song, mididrop::newChannel ("Lead"), 2);
            for (const char* take : { "Vocal take 1", "Vocal take 2" })
            {
                ChannelDef c;
                c.name = take;
                c.instrument = InstrumentKind::audioClip;
                c.samplePath = std::string ("/shots/") + take + ".wav";
                song.channels.push_back (c);
            }
            Clip t1;
            t1.kind = ClipKind::audio;
            t1.channel = (int) song.channels.size() - 2;
            t1.track = 4;
            t1.start = 4 * ppq;
            t1.length = 8 * ppq;
            song.playlist.push_back (t1);
            Automation env;
            env.name = "Lead - Volume";
            env.target = { AutomationTargetKind::channelVolume, lead, 0, 0 };
            env.points = { { 0, 0.2f, 0.0f }, { 4 * ppq, 0.9f, 0.0f }, { 8 * ppq, 0.4f, 0.0f } };
            song.automations.push_back (env);
            Automation spare;
            spare.name = "Cutoff (unused take)";
            spare.target = { AutomationTargetKind::channelPan, 0, 0, 0 };
            song.automations.push_back (spare);
            Clip a;
            a.kind = ClipKind::automation;
            a.automation = 0;
            a.track = 6;
            a.start = 0;
            a.length = 8 * ppq;
            song.playlist.push_back (a);
            engine.setSong (std::move (song));
        }
        engine.dispatchPendingMessages();
        ui.selectTabForSnapshot (2);
        engine.dispatchPendingMessages();
        auto& pl = ui.getPlaylistForSnapshot();
        pl.setSize (juce::jmax (pl.getWidth(), 1100), juce::jmax (pl.getHeight(), 640));
        pl.setViewForSnapshot (120.0, 1, 0);
        const auto& song = [&]() -> const Song& { return engine.getSong(); };
        const auto snap = [&] (const juce::String& name, int rows) {
            const auto bottom = pl.trackRowForSnapshot (juce::jmin (rows, pl.numTracksForSnapshot() - 1)).getY();
            savePng (shot::snapshot (pl, juce::Rectangle<int> (0, 0, juce::jmin (pl.getWidth(), 1000), juce::jmin (pl.getHeight(), bottom + 12))), prefix + name);
        };
        snap ("-start.png", 9);

        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const auto wheelAt = [&] (juce::Point<int> p, juce::ModifierKeys mods, float dy) {
            juce::MouseWheelDetails w;
            w.deltaX = 0.0f;
            w.deltaY = dy;
            w.isReversed = false;
            w.isSmooth = false;
            w.isInertial = false;
            pl.mouseWheelMove (juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pl, &pl, now, p.toFloat(), now, 0, false), w);
        };
        const int ctrl = juce::ModifierKeys::commandModifier;
        const auto header = juce::Point<int> (pl.headerWidthForSnapshot() / 2, pl.trackRowForSnapshot (2).getCentreY());
        const auto grid = juce::Point<int> (pl.headerWidthForSnapshot() + 300, pl.trackRowForSnapshot (2).getCentreY());
        const auto zoomNow = [&] { return pl.xOfTickForSnapshot (16.0 * ppq) - pl.xOfTickForSnapshot (0.0); };
        const auto rowY = [&] { return pl.trackRowForSnapshot (3).getY(); };

        // ---- 1. #186 wheel zones: off = FL (plain wheel scrolls everywhere, Ctrl zooms)
        {
            const auto z0 = zoomNow();
            wheelAt (grid, {}, -0.5f);
            check ("off_plain_wheel_on_arrangement_keeps_zoom", std::abs (zoomNow() - z0) < 0.5, juce::String (z0) + " " + juce::String (zoomNow()));
            wheelAt (grid, ctrl, 0.5f);
            check ("off_ctrl_wheel_zooms", zoomNow() > z0 + 1.0, juce::String (zoomNow()));
            pl.setViewForSnapshot (120.0, 1, 0);
        }
        workflowextras::setExtra ("playlist.wheelZones", true);
        {
            const auto z0 = zoomNow();
            const auto y0 = rowY();
            wheelAt (grid, {}, 0.5f);
            const auto z1 = zoomNow();
            check ("on_plain_wheel_on_arrangement_zooms", z1 > z0 + 1.0 && rowY() == y0, juce::String (z0) + " -> " + juce::String (z1));
            snap ("-wheel-zoomed.png", 9);
            wheelAt (header, {}, -0.5f);
            const auto y1 = rowY();
            check ("on_plain_wheel_on_headers_scrolls", y1 < y0 && std::abs (zoomNow() - z1) < 0.5, juce::String (y0) + " -> " + juce::String (y1));
            wheelAt (grid, ctrl, 0.5f);
            check ("on_ctrl_wheel_on_arrangement_scrolls", std::abs (zoomNow() - z1) < 0.5 && rowY() >= y1, juce::String (rowY()));
            wheelAt (header, ctrl, 0.5f);
            check ("on_ctrl_wheel_on_headers_zooms", zoomNow() > z1 + 1.0, juce::String (zoomNow()));
            pl.setViewForSnapshot (120.0, 1, 0);
        }
        workflowextras::setExtra ("playlist.wheelZones", false);
        check ("wheel_zones_off_again", ! wheelzones::enabled());

        // ---- 2. #191.1 temporary tools: off by default (FL's keys only select)
        auto& hold = workflowextras::tempTools();
        const auto toolKey = [&] (int code, const char* action) {
            const auto before = pl.currentTool();
            pl.runKeyAction (action);
            hold.toolKey (temptool::Editor::playlist, code, before, pl.currentTool(), [&pl] { return pl.currentTool(); },
                          [&pl] (EditTool t) { pl.setToolForSnapshot (t); });
        };
        pl.setToolForSnapshot (EditTool::pencil);
        toolKey ('C', "playlist.tool.knife");
        hold.releaseNow (juce::Time::getMillisecondCounter() + 2000);
        check ("temp_tools_off_tool_stays", pl.currentTool() == EditTool::knife);
        workflowextras::setExtra ("editing.tempTools", true);
        pl.setToolForSnapshot (EditTool::pencil);
        toolKey ('C', "playlist.tool.knife");
        check ("held_c_is_the_knife", pl.currentTool() == EditTool::knife && hold.holding());
        {
            const auto area = ui.getToolStripAreaForSnapshot (true);
            if (! area.isEmpty())
                savePng (shot::snapshot (ui, area.expanded (4)), prefix + "-temptool-held.png");
        }
        hold.releaseNow (juce::Time::getMillisecondCounter() + 2000);
        check ("released_back_to_pencil", pl.currentTool() == EditTool::pencil && ! hold.holding());
        toolKey ('C', "playlist.tool.knife");
        hold.releaseNow (juce::Time::getMillisecondCounter()); // (a tap)
        check ("tap_keeps_the_knife", pl.currentTool() == EditTool::knife);
        pl.setToolForSnapshot (EditTool::pencil);
        workflowextras::setExtra ("editing.tempTools", false);

        // ---- 3. #191.2 MIDI files: the Pattern layout takes them only with the extra on
        const juce::StringArray files { midiFile.getFullPathName() };
        const auto dropX = (int) pl.xOfTickForSnapshot (8.0 * ppq) + 2, dropY = pl.trackRowForSnapshot (7).getCentreY();
        check ("pattern_layout_refuses_mid_by_default", ! pl.isInterestedInFileDrag (files));
        workflowextras::setExtra ("playlist.midiDrop", true);
        check ("pattern_layout_takes_mid_with_the_extra", pl.isInterestedInFileDrag (files));
        {
            const auto patterns = song().patterns.size(), channels = song().channels.size(), clips = song().playlist.size();
            pl.filesDropped (files, dropX, dropY);
            engine.dispatchPendingMessages();
            const auto& last = song().playlist.back();
            check ("mid_drop_makes_a_pattern_clip", song().patterns.size() == patterns + 1 && song().playlist.size() == clips + 1
                                                         && last.kind == ClipKind::pattern && last.track == 7 && last.start == 8 * ppq,
                   juce::String (last.track) + " " + juce::String (last.start));
            check ("mid_drop_two_new_channels", song().channels.size() == channels + 2
                                                    && song().channels[channels].name == "Bass",
                   juce::String ((int) song().channels.size()));
            check ("mid_drop_pattern_named_after_file", song().patterns.back().name == "workflow-groove" || song().patterns.back().name.find ("groove") != std::string::npos,
                   juce::String::fromUTF8 (song().patterns.back().name.c_str()));
            snap ("-mid-dropped.png", 9);
            // on the Lead instrument track: the notes go to its channel
            const auto leadTrack = 2;
            const auto leadChannel = song().playlistTracks[leadTrack].channel;
            const auto before = song().channels.size();
            pl.filesDropped (files, (int) pl.xOfTickForSnapshot (16.0 * ppq) + 2, pl.trackRowForSnapshot (leadTrack).getCentreY());
            engine.dispatchPendingMessages();
            const auto& p = song().patterns.back();
            check ("mid_drop_on_instrument_track_uses_its_channel", song().channels.size() == before && ! p.notes.empty()
                                                                         && std::all_of (p.notes.begin(), p.notes.end(), [leadChannel] (const Note& n) { return n.channel == leadChannel; }));
            engine.undo();
            engine.dispatchPendingMessages();
            check ("mid_drop_is_one_undo_step", song().channels.size() == before);
        }
        workflowextras::setExtra ("playlist.midiDrop", false);

        // ---- 4. #191.6 the picker's takes and envelopes: the Playlist menu (the Picker panel submenu holds the clean-up)
        {
            const auto menu = ui.getPanelMenuForSnapshot (workspace::Panel::playlist);
            saveMenus (juce::StringArray { "Playlist" }, std::vector<juce::PopupMenu> { menu }, prefix + "-playlist-menu.png");
            const juce::PopupMenu* pickerMenu = nullptr;
            juce::StringArray texts;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            {
                texts.add (it.getItem().text);
                if (it.getItem().text == tr ("Picker panel"))
                    pickerMenu = it.getItem().subMenu.get();
            }
            check ("menu_has_the_switches", texts.contains (tr ("Wheel: scroll over the track headers, zoom over the arrangement"))
                                                && texts.contains (tr ("Hold a tool key for a temporary tool"))
                                                && texts.contains (tr ("MIDI files dropped on the Playlist become pattern clips")));
            if (pickerMenu != nullptr)
                saveMenus (juce::StringArray { "Picker panel" }, std::vector<juce::PopupMenu> { *pickerMenu }, prefix + "-picker-menu.png");
            bool purge = false, unused = false;
            if (pickerMenu != nullptr)
                for (juce::PopupMenu::MenuItemIterator it (*pickerMenu); it.next();)
                {
                    purge = purge || it.getItem().text.startsWith (tr ("Purge unused audio clips"));
                    unused = unused || it.getItem().text.startsWith (tr ("Delete unused automation clips ({0})", { "1" }));
                }
            check ("picker_menu_clean_up_items", purge && unused);
            const auto automations = song().automations.size(), channels = song().channels.size();
            engine.editSong ([] (Song& s) { takecleanup::deleteUnusedAutomations (s); });
            engine.editSong ([] (Song& s) { pltracks::purgeUnusedAudioClips (s); });
            check ("unused_envelope_and_take_deleted", song().automations.size() == automations - 1 && song().channels.size() == channels - 1,
                   juce::String ((int) song().automations.size()) + " " + juce::String ((int) song().channels.size()));
        }

        // ---- 5. #191.3 automation clips into their tracks' lanes (shown while the lanes are on)
        autolanes::setShown (true);
        pl.repaint();
        snap ("-lanes-before.png", 9);
        check ("lanes_movable_clip", laneview::movableClips (song()).size() == 1);
        saveMenus (juce::StringArray { "Playlist" }, std::vector<juce::PopupMenu> { ui.getPanelMenuForSnapshot (workspace::Panel::playlist) },
                   prefix + "-playlist-menu-lanes.png");
        workflowextras::runAction ("playlist.clipsToLanes");
        engine.dispatchPendingMessages();
        check ("lanes_clip_moved_under_lead", song().playlistTracks[2].lanes.size() == 1
                                                  && std::none_of (song().playlist.begin(), song().playlist.end(), [] (const Clip& c) { return c.kind == ClipKind::automation; }));
        snap ("-lanes-after.png", 9);
        engine.undo();
        engine.dispatchPendingMessages();
        check ("lanes_undo", song().playlistTracks[2].lanes.empty());
        autolanes::setShown (false);

        // ---- 6. #191.5 Track-only mode: the rack hidden, every new instrument on its own track
        ui.showDesktopForSnapshot (false);
        trackonly::setEnabled (true);
        if (workflowextras::onChanged)
            workflowextras::onChanged();
        check ("track_only_refuses_the_rack", workflowextras::refusesChannelRack());
        {
            const auto channels = song().channels.size();
            engine.editSong ([] (Song& s) { s.channels.push_back (mididrop::newChannel ("Strings")); });
            engine.dispatchPendingMessages();
            check ("track_only_new_instrument_gets_a_track", ! trackmodes::instrumentTracksOf (song(), (int) channels).empty());
        }
        savePng (shot::snapshot (ui, ui.getLocalBounds()), prefix + "-track-only.png");
        trackonly::setEnabled (false);

        // ---- 7. the Clip-launch layout: MIDI files on its arrangement and its grid (Live: always)
        ui.setLayoutStyle (layoutstyle::Style::clipLaunch, false);
        engine.dispatchPendingMessages();
        check ("clip_launch_takes_mid", pl.isInterestedInFileDrag (files));
        {
            const auto free = trackmodes::firstFreeTrack (song());
            pl.filesDropped (files, (int) pl.xOfTickForSnapshot (0.0) + 2, pl.trackRowForSnapshot (free).getCentreY());
            engine.dispatchPendingMessages();
            check ("clip_launch_arrangement_drop_makes_instrument_tracks",
                   free >= 0 && free < (int) song().playlistTracks.size() && song().playlistTracks[(std::size_t) free].mode == PlaylistTrackMode::instrument,
                   juce::String (free));
            if (auto* gridView = ui.getClipGridForSnapshot())
            {
                if (auto* target = dynamic_cast<juce::FileDragAndDropTarget*> (gridView))
                    check ("grid_takes_mid", target->isInterestedInFileDrag (files));
                const auto ok = workflowextras::dropMidiOnSlot (engine, files, free, 1);
                engine.dispatchPendingMessages();
                check ("grid_slot_drop", ok && song().clipGrid.has_value() && launcher::clipAt (*song().clipGrid, free, 1) != nullptr);
                gridView->setSize (juce::jmax (gridView->getWidth(), 900), juce::jmax (gridView->getHeight(), 420));
                savePng (shot::snapshot (*gridView, gridView->getLocalBounds()), prefix + "-grid-mid.png");
            }
            snap ("-cliplaunch-mid.png", 9);
        }
        ui.setLayoutStyle (layoutstyle::Style::pattern, false);

        // ---- 8. Settings › LYNX extras with the new switches
        {
            auto page = makeSettingsPanel (engine, SettingsPage::extras);
            page->setSize (juce::jmax (page->getWidth(), 760), 1600);
            juce::Component frame;
            frame.setSize (page->getWidth(), page->getHeight());
            frame.setOpaque (true);
            frame.addAndMakeVisible (*page);
            savePng (shot::paint (frame, theme::panel), prefix + "-settings-extras.png");
        }

        midiFile.deleteFile();
        juce::File (prefix + "-checks.txt").replaceWithText (report.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

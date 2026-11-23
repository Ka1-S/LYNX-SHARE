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

        // ---- 3b. the same .mid dropped on the Channel Rack (PR #262, FL 2026 as seen in round 190qj): on a channel's row
        // FL's "Import MIDI data" window for that channel opens (Shift: at once, the window's last choices) and the notes go
        // into it; below the channels FL's full window (new channels). While dragging, FL outlines the target channel's
        // name button, or draws a line under the last channel for a new one.
        {
            auto& rack = ui.getRackForSnapshot();
            auto& ws = ui.workspaceForTest();
            const auto pumpUi = [&engine] {
#if JUCE_LINUX || JUCE_WINDOWS
                for (int i = 0; i < 256 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i) {}
#endif
                engine.dispatchPendingMessages();
            };
            const auto modals = [] { return juce::ModalComponentManager::getInstance()->getNumModalComponents(); };
            const auto leadChannel = song().playlistTracks[2].channel;
            const auto pattern = song().currentPattern;
            const auto noteCount = [&] (int channel) {
                const auto& notes = song().patterns[(std::size_t) pattern].notes;
                return (int) std::count_if (notes.begin(), notes.end(), [channel] (const Note& n) { return n.channel == channel; });
            };
            ui.showPanelsForSnapshot ({ workspace::Panel::channelRack });
            pumpUi();
            const auto rowCentre = rack.rowTopForSnapshot (leadChannel) + ChannelRackComponent::rowHeight / 2;
            const auto shotArea = juce::Rectangle<int> (0, 0, juce::jmin (rack.getWidth(), 1000), rack.getHeight());
            rack.fileDragEnter (files, 10, rowCentre);
            check ("rack_mid_drag_marks_the_channel", rack.fileDropRowForSnapshot() == leadChannel, juce::String (rack.fileDropRowForSnapshot()));
            savePng (shot::snapshot (rack, shotArea), prefix + "-mid-drag-over-rack.png");
            savePng (rack.createComponentSnapshot (shotArea, true, 1.5f), prefix + "-mid-drag-over-rack@150.png");
            const auto belowY = rack.rowTopForSnapshot ((int) song().channels.size() - 1) + 3 * ChannelRackComponent::rowHeight;
            rack.fileDragMove (files, 10, belowY);
            check ("rack_mid_drag_below_marks_a_new_channel", rack.fileDropRowForSnapshot() == ChannelRackComponent::newChannelDrop);
            savePng (shot::snapshot (rack, shotArea), prefix + "-mid-drag-below-rack.png");
            rack.fileDragExit (files);
            check ("rack_mid_drag_mark_goes_away", rack.fileDropRowForSnapshot() == ChannelRackComponent::noDrop);

            check ("rack_takes_mid", rack.isInterestedInFileDrag (files));
            const auto before = noteCount (leadChannel);
            const auto othersBefore = (int) song().patterns[(std::size_t) pattern].notes.size() - before;
            const auto panelsBefore = ws.isPanelVisible (workspace::Panel::pianoRoll);
            // a plain drop: FL's window first, nothing imported until Accept
            rack.filesDropped (files, 10, rowCentre);
            pumpUi();
            auto* window = juce::Component::getCurrentlyModalComponent();
            check ("rack_drop_opens_the_import_window", modals() > 0 && window != nullptr && noteCount (leadChannel) == before);
            if (window != nullptr)
            {
                savePng (window->createComponentSnapshot (window->getLocalBounds(), true, 1.0f), prefix + "-mid-drop-window.png");
                window->exitModalState (1); // (Accept)
            }
            pumpUi();
            // (the file's 4 bass and 32 drum notes, both tracks merged into the channel it was dropped on)
            check ("rack_drop_puts_the_notes_in_that_channel", noteCount (leadChannel) == 36, juce::String (noteCount (leadChannel)));
            check ("rack_drop_keeps_the_other_channels", (int) song().patterns[(std::size_t) pattern].notes.size() - noteCount (leadChannel) == othersBefore);
            check ("rack_drop_selects_the_channel", engine.getSelectedChannel() == leadChannel, juce::String (engine.getSelectedChannel()));
            check ("rack_drop_leaves_the_panels_as_fl", ws.isPanelVisible (workspace::Panel::pianoRoll) == panelsBefore);
            engine.undo();
            pumpUi();
            check ("rack_drop_is_one_undo_step", noteCount (leadChannel) == before, juce::String (noteCount (leadChannel)));
            // Cancel: nothing changes
            rack.filesDropped (files, 10, rowCentre);
            pumpUi();
            if (auto* w = juce::Component::getCurrentlyModalComponent())
                w->exitModalState (0);
            pumpUi();
            check ("rack_drop_cancelled_changes_nothing", noteCount (leadChannel) == before && modals() == 0);
            // Shift held (FL): at once, without the window
            rack.shiftDropForTests = true;
            rack.filesDropped (files, 10, rowCentre);
            pumpUi();
            rack.shiftDropForTests = false;
            check ("rack_shift_drop_imports_without_the_window", modals() == 0 && noteCount (leadChannel) == 36, juce::String (noteCount (leadChannel)));
            engine.undo();
            pumpUi();
            // below the channels: FL's full window (new channels); Cancel leaves the rack as it was
            const auto channels = song().channels.size();
            rack.filesDropped (files, 10, belowY);
            pumpUi();
            check ("rack_drop_below_opens_the_full_window", modals() > 0);
            if (auto* w = juce::Component::getCurrentlyModalComponent())
                w->exitModalState (0);
            pumpUi();
            check ("rack_drop_below_cancelled_adds_nothing", song().channels.size() == channels && modals() == 0);
            ui.selectTabForSnapshot (2); // (the Playlist again, as the rest of this shot expects it)
            pumpUi();
        }

        // ---- 3c. PR #266 (FL 2026's Save as window for a new project, round 190qj): the name is filled in as "Project_N"
        {
            auto panel = makeNewProjectPanelAsShown();
            panel->setSize (700, 540);
            std::function<juce::String (juce::Component&)> filled = [&filled] (juce::Component& c) -> juce::String {
                if (auto* e = dynamic_cast<juce::TextEditor*> (&c); e != nullptr && e->getText().startsWith ("Project_"))
                    return e->getText();
                for (auto* child : c.getChildren())
                    if (const auto t = filled (*child); t.isNotEmpty())
                        return t;
                return {};
            };
            const auto name = filled (*panel);
            check ("new_project_name_is_filled_in_as_fl", name.matchesWildcard ("Project_*", false) && name.substring (8).getIntValue() >= 1, name);
            savePng (panel->createComponentSnapshot (panel->getLocalBounds(), true, 1.0f), prefix + "-new-project-name.png");
            savePng (panel->createComponentSnapshot (panel->getLocalBounds(), true, 1.5f), prefix + "-new-project-name@150.png");
        }

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
        // (issue #246, round 190oq: the rack is hidden when the mode comes on and opens again on request, as FL keeps an
        // instrument track's channel in its Channel rack)
        check ("track_only_hides_the_rack_when_it_comes_on", ! ui.workspaceForTest().isPanelVisible (workspace::Panel::channelRack));
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

        // ---- 7b. FL's Playlist: with a track selected, the other tracks step back (a dark veil over their rows)
        {
            const auto bar = song().barTicks();
            pl.setViewForSnapshot (60.0, 1, 0);
            engine.dispatchPendingMessages();
            int selected = -1, other = -1;
            for (int t = 0; t < pl.numTracksForSnapshot(); ++t)
            {
                const auto row = pl.trackRowForSnapshot (t);
                if (row.getHeight() < 8 || row.getBottom() > pl.getHeight())
                    continue;
                const auto empty = std::none_of (song().playlist.begin(), song().playlist.end(), [t] (const Clip& c) { return c.track == t; })
                                   && (t >= (int) song().playlistTracks.size() || song().playlistTracks[(std::size_t) t].lanes.empty());
                if (! empty)
                    continue;
                if (selected < 0)
                    selected = t;
                else if ((t % 2) == (selected % 2)) // (the lanes alternate by row: same shade before the veil)
                {
                    other = t;
                    break;
                }
            }
            check ("playlist_dim_has_two_empty_tracks", selected >= 0 && other >= 0, juce::String (selected) + " " + juce::String (other));
            if (selected >= 0 && other >= 0)
            {
                pl.selectTrack (selected);
                engine.dispatchPendingMessages();
                const auto image = shot::snapshot (pl);
                const auto x = juce::jlimit (0, image.getWidth() - 1, (int) pl.xOfTickForSnapshot (5.0 * bar) + 8);
                const auto lit = image.getPixelAt (x, pl.trackRowForSnapshot (selected).getCentreY()).getBrightness();
                const auto dark = image.getPixelAt (x, pl.trackRowForSnapshot (other).getCentreY()).getBrightness();
                check ("playlist_selected_track_leaves_the_others_darker", lit > dark + 0.006f, juce::String (lit, 4) + " / " + juce::String (dark, 4));
                savePng (image, prefix + "-playlist-selected-track.png");
            }
        }


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

        // ---- 9. FL's Playlist: bars in groups of 4, every other group a shade apart (with the lanes' row shades)
        {
            const auto bar = song().barTicks();
            engine.editSong ([bar] (Song& s) { // a clip far out: the end-of-song veil starts past the bars compared
                Clip far;
                far.kind = ClipKind::pattern;
                far.pattern = 0;
                far.track = 0;
                far.start = 40 * 4 * bar;
                far.length = 4 * bar;
                s.playlist.push_back (far);
            });
            pl.setViewForSnapshot (60.0, 1, 0); // (60 px a bar: bars 1 to 8 fit the view)
            engine.dispatchPendingMessages();
            const auto image = shot::snapshot (pl);
            int track = -1; // (an empty lane: its own shade, no clips)
            for (int t = 0; t < pl.numTracksForSnapshot(); ++t)
            {
                const auto row = pl.trackRowForSnapshot (t);
                if (row.getCentreY() >= image.getHeight() - 4 || row.getHeight() < 8)
                    continue;
                const auto empty = std::none_of (song().playlist.begin(), song().playlist.end(), [t] (const Clip& c) { return c.track == t; })
                                   && (t >= (int) song().playlistTracks.size() || song().playlistTracks[(std::size_t) t].lanes.empty());
                if (empty)
                {
                    track = t;
                    break;
                }
            }
            check ("playlist_four_bar_shading_finds_an_empty_lane", track >= 0, juce::String (track));
            if (track >= 0)
            {
                const auto y = pl.trackRowForSnapshot (track).getCentreY();
                const auto at = [&] (double bars) { return juce::jlimit (0, image.getWidth() - 1, (int) std::lround (pl.xOfTickForSnapshot (bars * bar) + 8.0)); };
                const auto first = image.getPixelAt (at (1.4), y).getBrightness();  // bars 1-4 (the untinted group)
                const auto second = image.getPixelAt (at (5.4), y).getBrightness(); // bars 5-8 (the tinted one)
                check ("playlist_four_bar_groups_are_a_shade_apart", std::abs (first - second) > 0.003f,
                       juce::String (first, 4) + " / " + juce::String (second, 4));
            }
            savePng (image, prefix + "-playlist-four-bar-groups.png");
            engine.undo();
            engine.dispatchPendingMessages();
        }

        midiFile.deleteFile();
        juce::File (prefix + "-checks.txt").replaceWithText (report.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

    // LYNX --trackclips190neshot <prefix> (round 190ne, 0.1.31: track clips, core/TrackClips.h) off-screen through the real
    // components: the Pattern layout with two instrument tracks whose clips own their notes, a bus track and FL's
    // tracks as they are; the Picker's "Track clips" group open and folded, the headers' marks, the track and clip menus;
    // the pencil, Ctrl+B / Ctrl+Alt+B, Cut / Paste and Delete through the Playlist's own handlers, with Undo; the same
    // instrument track with the switch off (FL's own: every pencil click places one pattern), for the FL | LYNX pair.
    // <prefix>-checks.txt lists the checks. @150: LYNX_SHOT_SCALE=1.5 LYNX_SHOT_SUFFIX=@150.
    bool runTrackClipsShot190ne (const juce::String& prefix)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (lynx::ui::showingForShots(), true);
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const char* name, bool ok, const juce::String& detail = {}) {
            report.add (juce::String (name) + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        const auto finish = [&] {
            juce::File (prefix + "-checks.txt").replaceWithText (report.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
            return allOk;
        };
        using namespace lynx;
        ui::motion::enabled() = false;
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        const auto bar = (std::int64_t) 4 * ppq;

        // the wireframe's song (/mnt/e/lynx-work/study190nd/tracks/wireframe-track-clips.svg), as LYNX data
        const auto makeSong = [bar] (bool ownClips) {
            auto s = Song::makeDefault();
            s.playlist.clear();
            s.patterns.clear();
            s.channels.resize (1);
            s.channels[0].name = "Kick";
            for (const auto& [name, colour] : { std::pair<const char*, std::uint32_t> { "Lead", 0xFFE5383Bu }, { "Bass", 0xFF6E9BD8u } })
            {
                ChannelDef c;
                c.name = name;
                c.colour = colour;
                s.channels.push_back (c);
            }
            const auto note = [] (int channel, int key, std::int64_t start, std::int64_t length) {
                Note n;
                n.channel = channel;
                n.key = key;
                n.start = start;
                n.length = length;
                return n;
            };
            auto& drums = s.patterns.emplace_back();
            drums.name = "Drums";
            drums.colour = 0xFFD9A62Bu;
            for (int k = 0; k < 8; ++k)
                drums.notes.push_back (note (0, 60, k * ppq / 2, ppq / 4));
            s.patterns.emplace_back().name = "Chords";
            s.patterns.back().colour = 0xFF6EA8E0u;
            s.patterns.emplace_back().name = "Hook vox chop";
            s.patterns.back().colour = 0xFF9B6FD8u;
            s.playlistTracks.resize (6);
            s.playlistTracks[0].name = "Drums";
            for (int k = 0; k < 5; ++k)
            {
                Clip c;
                c.pattern = 0;
                c.track = 0;
                c.start = k * 2 * bar;
                c.length = 2 * bar;
                s.playlist.push_back (c);
            }
            trackmodes::makeInstrumentTrack (s, 1, 1);
            trackmodes::makeInstrumentTrack (s, 2, 2);
            const auto place = [&s] (int pattern, int track, std::int64_t start, std::int64_t length) {
                Clip c;
                c.pattern = pattern;
                c.track = track;
                c.start = start;
                c.length = length;
                s.playlist.push_back (c);
            };
            if (ownClips)
            {
                trackclips::setOwnClips (s, 1, true);
                trackclips::setOwnClips (s, 2, true);
                const auto lead1 = trackclips::newClip (s, 1, 2 * bar), lead2 = trackclips::newClip (s, 1, 2 * bar), lead3 = trackclips::newClip (s, 1, 2 * bar);
                const auto bass1 = trackclips::newClip (s, 2, 2 * bar), bass2 = trackclips::newClip (s, 2, 4 * bar);
                for (const auto& [p, notes] : { std::pair<int, std::vector<int>> { lead1, { 67, 64, 69, 65 } }, { lead2, { 72, 71, 67, 65 } }, { lead3, { 69, 67, 72, 72 } } })
                    for (int k = 0; k < 4; ++k)
                        s.patterns[(std::size_t) p].notes.push_back (note (1, notes[(std::size_t) k], k * 2 * ppq, 3 * ppq / 2));
                for (int k = 0; k < 8; ++k)
                    s.patterns[(std::size_t) bass1].notes.push_back (note (2, k % 2 ? 43 : 36, k * ppq, ppq / 2));
                for (int k = 0; k < 16; ++k)
                    s.patterns[(std::size_t) bass2].notes.push_back (note (2, 36 + (k * 5) % 12, k * ppq, ppq / 2));
                place (lead1, 1, 0, 2 * bar);
                place (lead1, 1, 2 * bar, 2 * bar);
                place (lead2, 1, 4 * bar, 2 * bar);
                place (lead3, 1, 8 * bar, 2 * bar);
                place (bass1, 2, 0, 2 * bar);
                place (bass1, 2, 2 * bar, 2 * bar);
                place (bass1, 2, 4 * bar, 2 * bar);
                place (bass2, 2, 6 * bar, 4 * bar);
            }
            else
            {
                // FL's instrument track: one pattern named after the channel on every clip (FL 2026 shot 02)
                const auto p = trackmodes::instrumentTrackPattern (s, 1);
                for (int k = 0; k < 4; ++k)
                    s.patterns[(std::size_t) p].notes.push_back (note (1, 64 + k * 2, k * 2 * ppq, 3 * ppq / 2));
                for (int k = 0; k < 3; ++k)
                    place (p, 1, k * 2 * bar, 2 * bar);
            }
            // the bus: an audio track linked to insert 10, which the instruments' inserts send into
            s.playlistTracks[3].name = "Synth bus";
            if ((int) s.mixer.size() <= 10)
                s.mixer.resize (11);
            trackmodes::linkToMixerTrack (s, 3, 10);
            for (const auto ch : { 1, 2 })
                if (const auto insert = s.channels[(std::size_t) ch].mixerTrack; insert > 0 && insert != 10)
                    s.setRoute (insert, 10, true);
            s.currentPattern = 0;
            return s;
        };

        AudioEngine engine (false);
        engine.setSong (makeSong (true));
        engine.dispatchPendingMessages();
        MainComponent ui (engine, false);
        ui.setSize (1920, 1080);
        engine.dispatchPendingMessages();
        ui.selectTabForSnapshot (2);
        engine.dispatchPendingMessages();
        auto& pl = ui.getPlaylistForSnapshot();
        auto& picker = ui.getPickerForSnapshot();
        pl.setViewForSnapshot (110.0, 0, 0);
        const auto& song = [&engine]() -> const Song& { return engine.getSong(); }();
        const auto write = [&prefix] (juce::Component& c, const juce::String& name) {
            c.resized();
            savePng (shot::snapshot (c), prefix + "-" + name + ".png");
        };
        const auto zoom = [&prefix] (juce::Component& c, juce::Rectangle<int> r, float k, const juce::String& name) {
            savePng (c.createComponentSnapshot (r.getIntersection (c.getLocalBounds()), true, k * shot::scale()), prefix + "-" + name + ".png");
        };
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto event = [&source] (juce::Component& c, juce::Point<int> p, juce::ModifierKeys mods) {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, p.toFloat(), now, 1, false);
        };
        const auto click = [&] (juce::Component& c, juce::Point<int> p, juce::ModifierKeys mods = {}) {
            const juce::ModifierKeys down = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
            c.mouseDown (event (c, p, down));
            c.mouseUp (event (c, p, mods));
            engine.dispatchPendingMessages();
        };
        const auto tracks = [&song] (int track) {
            int n = 0;
            for (const auto& c : song.playlist)
                n += c.track == track && c.kind == ClipKind::pattern ? 1 : 0;
            return n;
        };
        const auto pickerOnly = [&picker] { return picker.getLocalBounds().withHeight (juce::jmin (picker.getHeight(), 420)); };

        check ("song_has_track_clips", trackclips::count (song) == 5 && trackclips::owns (song, 1) && trackclips::owns (song, 2) && ! trackclips::owns (song, 0),
               juce::String (trackclips::count (song)));
        check ("bus_track", trackclips::isBusTrack (song, 3), juce::String (trackclips::busSources (song, 3)));
        int listed = 0;
        for (int p = 0; p < (int) song.patterns.size(); ++p)
            listed += trackclips::listedInSelector (song, p) ? 1 : 0;
        check ("selector_lists_the_ordinary_patterns", listed == 3, juce::String (listed));

        // ---- the Picker: the group folded (FL's list, then one line), open, and the "shown" switch
        engine.dispatchPendingMessages();
        write (ui, "01-pattern-layout");
        zoom (picker, pickerOnly(), 2.0f, "02-picker-group-folded@2x");
        const auto groupRow = picker.groupRowForSnapshot();
        check ("picker_group_after_fl_list", groupRow == 3 && picker.listedRowsForSnapshot() == 4, juce::String (groupRow));
        if (groupRow >= 0)
        {
            const auto rowY = picker.rowYForSnapshot (groupRow);
            click (picker, { 30, rowY });
            check ("picker_group_unfolds", picker.trackClipsOpenNow() && picker.listedRowsForSnapshot() == 9, juce::String (picker.listedRowsForSnapshot()));
            pl.setViewForSnapshot (110.0, 0, 0);
            write (ui, "03-pattern-layout-group-open");
            zoom (picker, pickerOnly(), 2.0f, "04-picker-group-open@2x");
            click (picker, picker.groupSwitchForSnapshot (groupRow).getCentre());
            check ("shown_switch_on", trackclips::shownInSelector());
            zoom (picker, pickerOnly(), 2.0f, "05-picker-shown-on@2x");
            click (picker, picker.groupSwitchForSnapshot (groupRow).getCentre());
            check ("shown_switch_off", ! trackclips::shownInSelector());
        }
        // the headers: the own-clips mark on Lead and Bass, the bus mark on Synth bus
        zoom (pl, juce::Rectangle<int> (0, pl.trackRowForSnapshot (0).getY(), pl.headerWidthForSnapshot() + 4 * 110, pl.trackRowForSnapshot (4).getBottom() - pl.trackRowForSnapshot (0).getY()),
              2.0f, "06-headers-and-clips@2x");
        saveMenus (juce::StringArray { "Track" }, std::vector<juce::PopupMenu> { pl.getTrackMenuForSnapshot (1) }, prefix + "-07-track-menu.png");
        saveMenus (juce::StringArray { "Bus track" }, std::vector<juce::PopupMenu> { pl.getTrackMenuForSnapshot (3) }, prefix + "-08-bus-track-menu.png");
        saveMenus (juce::StringArray { "Clip" }, std::vector<juce::PopupMenu> { pl.getClipMenuForSnapshot (5) }, prefix + "-09-clip-menu.png");

        // ---- the pencil on an own-clips track: a NEW empty clip (bar 13), named after the track
        pl.setToolForSnapshot (EditTool::pencil);
        const auto patternsBefore = song.patterns.size();
        const auto leadRow = pl.trackRowForSnapshot (1);
        click (pl, { (int) pl.xOfTickForSnapshot ((double) (12 * bar + ppq / 4)), leadRow.getCentreY() });
        check ("pencil_draws_a_new_clip", song.patterns.size() == patternsBefore + 1 && tracks (1) == 5 && song.patterns.back().trackClip
                                              && song.patterns.back().notes.empty() && song.currentPattern == (int) song.patterns.size() - 1,
               juce::String::fromUTF8 (song.patterns.back().name.c_str()));
        check ("new_clip_name", song.patterns.back().name == "Lead \xC2\xB7 clip 4");
        int drawn = -1;
        for (int i = 0; i < (int) song.playlist.size(); ++i)
            if (song.playlist[(std::size_t) i].pattern == (int) song.patterns.size() - 1)
                drawn = i;
        // Ctrl+B: a unique copy; Ctrl+Alt+B: a linked one
        pl.selectClipsForSnapshot ({ drawn });
        pl.handleKey (juce::KeyPress ('b', juce::ModifierKeys::commandModifier, 0));
        engine.dispatchPendingMessages();
        check ("ctrl_b_copy_is_unique", song.patterns.size() == patternsBefore + 2 && trackclips::uses (song).back() == 1
                                            && pl.clipSelectionForTests().size() == 1
                                            && song.playlist[(std::size_t) pl.clipSelectionForTests().front()].pattern == (int) song.patterns.size() - 1);
        pl.handleKey (juce::KeyPress ('b', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0));
        engine.dispatchPendingMessages();
        const auto linkedPattern = pl.clipSelectionForTests().empty() ? -1 : song.playlist[(std::size_t) pl.clipSelectionForTests().front()].pattern;
        check ("ctrl_alt_b_copy_is_linked", song.patterns.size() == patternsBefore + 2 && trackclips::uses (song)[(std::size_t) linkedPattern] == 2);
        write (ui, "10-drawn-unique-linked");
        // Delete both clips of the linked pattern: it goes with them, in one undo step
        {
            std::vector<int> both;
            for (int i = 0; i < (int) song.playlist.size(); ++i)
                if (song.playlist[(std::size_t) i].pattern == linkedPattern)
                    both.push_back (i);
            pl.selectClipsForSnapshot (both);
            pl.handleKey (juce::KeyPress (juce::KeyPress::deleteKey));
            engine.dispatchPendingMessages();
            check ("delete_last_clips_deletes_the_track_clip", song.patterns.size() == patternsBefore + 1, juce::String ((int) song.patterns.size()));
            engine.undo();
            engine.dispatchPendingMessages();
            check ("undo_brings_clips_and_pattern_back", song.patterns.size() == patternsBefore + 2 && trackclips::uses (song)[(std::size_t) linkedPattern] == 2);
        }
        // Cut the only clip of "Lead · clip 3" (bar 9) and paste it: a copy of its own comes back
        {
            int lead3 = -1;
            for (int i = 0; i < (int) song.playlist.size(); ++i)
                if (song.playlist[(std::size_t) i].track == 1 && song.playlist[(std::size_t) i].start == 8 * bar)
                    lead3 = i;
            const auto notes = lead3 >= 0 ? song.patterns[(std::size_t) song.playlist[(std::size_t) lead3].pattern].notes : std::vector<Note> {};
            pl.selectClipsForSnapshot ({ lead3 });
            pl.handleKey (juce::KeyPress ('x', juce::ModifierKeys::commandModifier, 0));
            engine.dispatchPendingMessages();
            const auto afterCut = song.patterns.size();
            check ("cut_deletes_the_track_clip", lead3 >= 0 && afterCut == patternsBefore + 1, juce::String ((int) afterCut));
            pl.handleKey (juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0));
            engine.dispatchPendingMessages();
            check ("paste_brings_a_copy", song.patterns.size() == afterCut + 1 && song.patterns.back().trackClip && song.patterns.back().notes == notes
                                              && trackclips::uses (song).back() == 1);
        }
        write (ui, "11-after-cut-paste");

        // ---- the switch off on Lead: its clips are ordinary patterns again (the list shows them), one undo step
        engine.editSong ([] (Song& s) { trackclips::setOwnClips (s, 1, false); });
        engine.dispatchPendingMessages();
        check ("switch_off_releases_the_clips", ! trackclips::owns (song, 1) && trackclips::count (song) == 2, juce::String (trackclips::count (song)));
        engine.undo();
        engine.dispatchPendingMessages();
        check ("undo_of_the_switch", trackclips::owns (song, 1) && trackclips::count (song) > 2);

        // ---- FL's own instrument track (the switch off, the Pattern layout's default): the pencil places the same pattern
        {
            AudioEngine flEngine (false);
            flEngine.setSong (makeSong (false));
            flEngine.dispatchPendingMessages();
            MainComponent flUi (flEngine, false);
            flUi.setSize (1920, 1080);
            flEngine.dispatchPendingMessages();
            flUi.selectTabForSnapshot (2);
            flEngine.dispatchPendingMessages();
            auto& flPl = flUi.getPlaylistForSnapshot();
            flPl.setViewForSnapshot (110.0, 0, 0);
            flPl.setToolForSnapshot (EditTool::pencil);
            const auto& flSong = flEngine.getSong();
            const auto before = flSong.patterns.size();
            const auto row = flPl.trackRowForSnapshot (1);
            const auto down = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
            const auto at = juce::Point<int> ((int) flPl.xOfTickForSnapshot ((double) (8 * bar + ppq / 4)), row.getCentreY());
            flPl.mouseDown (event (flPl, at, down));
            flPl.mouseUp (event (flPl, at, {}));
            flEngine.dispatchPendingMessages();
            int onLead = 0;
            std::set<int> leadPatterns;
            for (const auto& c : flSong.playlist)
                if (c.track == 1)
                {
                    ++onLead;
                    leadPatterns.insert (c.pattern);
                }
            check ("fl_track_pencil_places_the_same_pattern", flSong.patterns.size() == before && onLead == 4 && leadPatterns.size() == 1
                                                               && trackclips::count (flSong) == 0 && ! trackclips::owns (flSong, 1));
            write (flUi, "12-fl-instrument-track-switch-off");
            zoom (flUi.getPickerForSnapshot(), flUi.getPickerForSnapshot().getLocalBounds().withHeight (juce::jmin (flUi.getPickerForSnapshot().getHeight(), 420)), 2.0f,
                  "13-fl-picker-unchanged@2x");
        }

        // ---- Clip-launch: a new instrument track there owns its clips (the layout's default)
        {
            Song s = makeSong (false); // (made in the Pattern layout: FL's own instrument tracks)
            layoutstyle::setCurrent (layoutstyle::Style::clipLaunch);
            s.channels.emplace_back().name = "Pad";
            const auto made = trackmodes::makeInstrumentTrack (s, 4, (int) s.channels.size() - 1);
            check ("cliplaunch_new_track_owns_clips", made && trackclips::owns (s, 4) && ! trackclips::owns (s, 1),
                   juce::String (made ? "made" : "not made") + " mixer " + juce::String ((int) s.mixer.size()) + " own " + juce::String ((int) s.playlistTracks[4].ownClips)
                       + " mode " + juce::String ((int) s.playlistTracks[4].mode));
        }
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        return finish();
    }

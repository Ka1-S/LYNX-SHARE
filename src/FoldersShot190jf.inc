    // LYNX --foldersshot190jf <prefix> <thief.flp> (round 190jf, slice B17): the Playlist's folders off-screen, with checks
    // in prefix-checks.txt — FL's groups as imported (for the FL | LYNX pair), a collapsed group (thin rows), FL's View ›
    // "Hide collapsed grouped tracks", a nested folder (open / collapsed), an Alt+drag into a group (the drop line), FL's
    // Alt+click group mute and Alt+right-click group solo through the real mouse handlers, the track menu with the
    // nesting items, and the same project in the Clip-launch layout.
    bool runFoldersShot190jf (const juce::String& prefix, const juce::File& flp)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
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
        AudioEngine engine (false);
        MainComponent ui (engine, false);
        ui.setSize (1600, 900);
        juce::String error;
        const auto loaded = engine.importFlpFile (flp, error);
        check ("project_imported", loaded, error);
        if (! loaded)
            return finish();
        engine.dispatchPendingMessages();
        ui.selectTabForSnapshot (2);
        engine.dispatchPendingMessages();
        auto& pl = ui.getPlaylistForSnapshot();
        pl.setSize (juce::jmax (pl.getWidth(), 1500), juce::jmax (pl.getHeight(), 860));
        pl.setViewForSnapshot (16.0, 0, 0);
        const auto write = [&prefix, &pl] (const char* name) {
            pl.resized();
            const auto file = shot::file (juce::File (prefix + "-" + name + ".png"));
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (shot::snapshot (pl), out);
        };
        const auto& song = [&engine]() -> const Song& { return engine.getSong(); }();
        const auto edit = [&engine, &pl] (std::function<void (Song&)> f) {
            engine.editSong (std::move (f));
            engine.dispatchPendingMessages();
            pl.headerOptionsChanged();
        };
        const auto rowsHeight = [&pl] {
            int h = 0;
            for (int t = 0; t < pl.numTracksForSnapshot(); ++t)
                h += pl.trackRowForSnapshot (t).getHeight();
            return h;
        };
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        auto& mouse = static_cast<juce::Component&> (pl); // (the Playlist's own mouse handlers, as a click would reach them)
        const auto event = [&source, &pl] (juce::Point<int> p, juce::ModifierKeys mods, juce::Point<int> downAt) {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pl, &pl, now, downAt.toFloat(), now, 1, false);
        };

        // the groups of the imported project (FL's "Group with above track")
        std::vector<int> headers;
        for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
            if (noteedit::isGroupHeader (song, t))
                headers.push_back (t);
        check ("fl_groups_imported", headers.size() >= 2, juce::String ((int) headers.size()) + " groups");
        if (headers.size() < 2)
            return finish();
        const auto h0 = headers.front();
        const auto members = noteedit::groupMembers (song, h0);
        check ("first_group_has_two_members", members.size() >= 2, juce::String ((int) members.size()));
        if (members.size() < 2)
            return finish();
        write ("01-groups");
        {
            // the view of FL's reference shot (design190ia fl-playlist.png): from the CUTLAYER2 row, in the MELODICS group
            int cut = -1;
            for (int t = 0; t < (int) song.playlistTracks.size() && cut < 0; ++t)
                if (juce::String (song.playlistTracks[(std::size_t) t].name).startsWithIgnoreCase ("CUTL"))
                    cut = t;
            if (cut > 0)
            {
                pl.setScrollYForSnapshot (juce::jmax (0, pl.trackRowForSnapshot (cut).getY() - pl.trackRowForSnapshot (0).getY()));
                write ("01b-groups-fl-view");
                pl.setScrollYForSnapshot (0);
            }
        }
        {
            // the perf project: the same song with every group collapsed (--paintbench / --uiperf, Hide on and off)
            auto collapsed = song;
            pltracks::setAllGroupsCollapsed (collapsed, true);
            juce::String saveError;
            check ("collapsed_copy_saved", ProjectFile::save (collapsed, juce::File (prefix + "-collapsed.lynx"), saveError), saveError);
        }

        // a collapsed group: its members are 7 px rows of clip bars (FL)
        const auto open = rowsHeight();
        edit ([h0] (Song& s) { noteedit::setGroupCollapsed (s, h0, true); });
        check ("collapsed_members_are_thin_rows", pl.trackRowForSnapshot (members.front()).getHeight() == 7,
               juce::String (pl.trackRowForSnapshot (members.front()).getHeight()));
        write ("02-collapsed");
        // FL's View › Hide collapsed grouped tracks: no rows at all
        PlaylistComponent::hideCollapsedGroups = true;
        pl.headerOptionsChanged();
        check ("hidden_members_have_no_row", pl.trackRowForSnapshot (members.front()).getHeight() == 0 && rowsHeight() < open,
               juce::String (rowsHeight()) + " < " + juce::String (open));
        write ("03-hide-collapsed");
        PlaylistComponent::hideCollapsedGroups = false;
        edit ([h0] (Song& s) { noteedit::setGroupCollapsed (s, h0, false); });

        // a group in the group (LYNX): the second member nested under the first
        const auto m1 = members[0], m2 = members[1];
        edit ([m2] (Song& s) { noteedit::nestTrack (s, m2); });
        check ("nested_under_the_member_above", noteedit::folderDepth (song, m2) == 2 && noteedit::isGroupHeader (song, m1)
                                                    && noteedit::folderParent (song, m2) == m1);
        write ("04-nested");
        saveMenus (juce::StringArray { "Track" }, std::vector<juce::PopupMenu> { pl.getTrackMenuForSnapshot (m2) }, prefix + "-05-track-menu.png");
        edit ([m1] (Song& s) { noteedit::setGroupCollapsed (s, m1, true); });
        check ("nested_collapse_folds_only_its_folder", noteedit::isFoldedAway (song, m2) && ! noteedit::isFoldedAway (song, m1));
        write ("06-nested-collapsed");
        edit ([m1] (Song& s) { noteedit::setGroupCollapsed (s, m1, false); });

        // Alt+drag: the first ungrouped track below the last group into that group, through the mouse handlers
        const auto hL = headers.back();
        int loose = -1;
        for (int t = noteedit::folderEnd (song, hL) + 1; t < (int) song.playlistTracks.size(); ++t)
            if (noteedit::folderDepth (song, t) == 0 && ! noteedit::isGroupHeader (song, t))
            {
                loose = t;
                break;
            }
        check ("a_loose_track", loose > 0, juce::String (loose));
        if (loose > 0)
        {
            pl.setScrollYForSnapshot (0);
            pl.setScrollYForSnapshot (juce::jmax (0, pl.trackRowForSnapshot (hL).getY() - pl.trackRowForSnapshot (0).getY()));
            const auto name = song.playlistTracks[(std::size_t) loose].name;
            const auto row = [&pl] (int t) { return pl.trackRowForSnapshot (t); };
            const auto from = juce::Point<int> (60, row (loose).getCentreY());
            const auto lastOfGroup = noteedit::folderEnd (song, hL);
            const auto to = juce::Point<int> (60, row (lastOfGroup).getY() + 2); // the upper half of the group's last row
            const auto alt = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
            mouse.mouseDown (event (from, alt, from));
            mouse.mouseDrag (event (to, alt, from));
            write ("07-alt-drag");
            mouse.mouseUp (event (to, juce::ModifierKeys (juce::ModifierKeys::altModifier), from));
            engine.dispatchPendingMessages();
            int moved = -1;
            for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
                if (song.playlistTracks[(std::size_t) t].name == name)
                    moved = t;
            check ("alt_drag_moved_it_into_the_group", moved > hL && moved <= noteedit::folderEnd (song, hL) && noteedit::folderDepth (song, moved) >= 1,
                   juce::String (loose) + " -> " + juce::String (moved));
            write ("08-after-alt-drag");
            pl.setScrollYForSnapshot (0);
        }

        // FL's Alt+click on a member's LED: the whole group mutes; Alt+right-click: the group plays alone (a member that is
        // no sub-folder's header: its group is the first group, the nested folder included)
        {
            const auto member = members.back();
            const auto group = noteedit::groupMuteSet (song, member);
            check ("member_group_is_the_first_group", ! group.empty() && group.front() == h0, juce::String ((int) group.size()));
            const auto led = pl.trackLedForSnapshot (member).getCentre();
            const auto altDown = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
            const auto altUp = juce::ModifierKeys (juce::ModifierKeys::altModifier);
            mouse.mouseDown (event (led, altDown, led));
            mouse.mouseUp (event (led, altUp, led));
            engine.dispatchPendingMessages();
            auto all = true;
            for (const auto t : group)
                all = all && song.isTrackMuted (t);
            check ("alt_click_mutes_the_group", all && ! song.isTrackMuted (headers[1]));
            write ("09-group-muted");
            mouse.mouseDown (event (led, altDown, led));
            mouse.mouseUp (event (led, altUp, led));
            engine.dispatchPendingMessages();
            auto none = true;
            for (const auto t : group)
                none = none && ! song.isTrackMuted (t);
            check ("alt_click_again_unmutes_it", none);
            const auto right = juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier | juce::ModifierKeys::altModifier);
            mouse.mouseDown (event (led, right, led));
            mouse.mouseUp (event (led, altUp, led));
            engine.dispatchPendingMessages();
            auto solo = song.isTrackMuted (headers[1]);
            for (const auto t : group)
                solo = solo && ! song.isTrackMuted (t);
            check ("alt_right_click_solos_the_group", solo);
            write ("10-group-solo");
            mouse.mouseDown (event (led, right, led));
            mouse.mouseUp (event (led, altUp, led));
            engine.dispatchPendingMessages();
            check ("alt_right_click_again_unsolos", ! song.isTrackMuted (headers[1]));
        }

        // the .lynx round trip keeps the nesting
        {
            const auto file = juce::File (prefix + "-nested.lynx");
            juce::String saveError;
            const auto saved = ProjectFile::save (song, file, saveError);
            juce::String loadError;
            const auto back = saved ? ProjectFile::load (file, loadError) : std::optional<Song>();
            check ("lynx_round_trip_keeps_nesting", back.has_value() && noteedit::folderDepth (*back, m2) == 2,
                   saveError + loadError);
            file.deleteFile();
        }

        // the Clip-launch layout (Live's group tracks): the same folders
        ui.setClipLaunchWorkflow (true);
        engine.dispatchPendingMessages();
        ui.selectTabForSnapshot (2);
        pl.setSize (juce::jmax (pl.getWidth(), 1500), juce::jmax (pl.getHeight(), 860));
        pl.setViewForSnapshot (16.0, 0, 0);
        write ("11-cliplaunch-nested");
        return finish();
    }

    // LYNX --dawprojcheck190jf <out-prefix> <project> (round 190jf, slice B22): the project (any LYNX can open) through the
    // app's File › Export › DAWproject path and back through File › Open: the counts that must survive, the export's notes
    // and the import's report in prefix-checks.txt; the .dawproject stays as prefix.dawproject.
    bool runDawProjectCheck190jf (const juce::String& prefix, const juce::File& input)
    {
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
        struct Counts
        {
            int tracks = 0, groups = 0, nested = 0, clips = 0, audioClips = 0, notes = 0, automations = 0, points = 0, markers = 0;
            double bpm = 0.0;
        };
        const auto countsOf = [] (const Song& s) {
            Counts c;
            for (int t = 0; t < (int) s.playlistTracks.size(); ++t)
            {
                const auto used = std::any_of (s.playlist.begin(), s.playlist.end(), [t] (const Clip& k) { return k.track == t && k.gridSlot < 0; });
                if (used || ! s.playlistTracks[(std::size_t) t].name.empty())
                    ++c.tracks;
                c.groups += noteedit::isGroupHeader (s, t) ? 1 : 0;
                c.nested += noteedit::folderDepth (s, t) > 1 ? 1 : 0;
            }
            for (const auto& k : s.playlist)
                if (k.gridSlot < 0)
                {
                    ++c.clips;
                    c.audioClips += k.kind == ClipKind::audio ? 1 : 0;
                }
            // (the notes of the patterns placed on the Playlist: DAWproject has no pool of unplaced patterns)
            for (int p = 0; p < (int) s.patterns.size(); ++p)
                if (std::any_of (s.playlist.begin(), s.playlist.end(), [p] (const Clip& k) { return k.kind == ClipKind::pattern && k.pattern == p; }))
                    c.notes += (int) s.patterns[(std::size_t) p].notes.size();
            c.automations = (int) s.automations.size();
            for (const auto& a : s.automations)
                c.points += (int) a.points.size();
            c.markers = (int) s.markers().size();
            c.bpm = s.bpm;
            return c;
        };
        const auto describe = [] (const Counts& c) {
            return "tracks " + juce::String (c.tracks) + ", groups " + juce::String (c.groups) + ", nested " + juce::String (c.nested) + ", clips "
                   + juce::String (c.clips) + " (audio " + juce::String (c.audioClips) + "), notes " + juce::String (c.notes) + ", automations "
                   + juce::String (c.automations) + " (" + juce::String (c.points) + " points), markers " + juce::String (c.markers) + ", bpm "
                   + juce::String (c.bpm, 3);
        };

        AudioEngine engine (false);
        juce::String error;
        const auto loaded = input.hasFileExtension ("lynx") ? [&] {
            auto song = ProjectFile::load (input, error);
            if (song)
                engine.setSong (std::move (*song));
            return song.has_value();
        }()
                                                            : engine.importFlpFile (input, error);
        check ("project_opened", loaded, error);
        if (! loaded)
            return finish();
        engine.dispatchPendingMessages();
        const auto before = countsOf (engine.getSong());
        report.add ("before: " + describe (before));

        const auto target = juce::File (prefix + ".dawproject");
        juce::StringArray notes;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto written = writeDawProjectFile (engine.getSong(), &engine.getPluginHost(), target, notes, error);
        check ("exported", written, error + " " + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 0) + " ms, "
                                         + juce::String (target.getSize() / 1024) + " KB");
        for (const auto& n : notes)
            report.add ("export note: " + n);
        if (! written)
            return finish();

        AudioEngine back (false);
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        const auto reopened = back.importFlpFile (target, error);
        check ("reopened", reopened, error + " " + juce::String (juce::Time::getMillisecondCounterHiRes() - t1, 0) + " ms");
        if (! reopened)
            return finish();
        back.dispatchPendingMessages();
        const auto after = countsOf (back.getSong());
        report.add ("after:  " + describe (after));
        check ("same_tracks", after.tracks >= before.tracks, juce::String (before.tracks) + " -> " + juce::String (after.tracks));
        check ("same_groups", after.groups == before.groups && after.nested == before.nested);
        check ("same_notes", after.notes == before.notes, juce::String (before.notes) + " -> " + juce::String (after.notes));
        check ("same_audio_clips", after.audioClips == before.audioClips, juce::String (before.audioClips) + " -> " + juce::String (after.audioClips));
        check ("same_tempo", std::abs (after.bpm - before.bpm) < 0.001);
        check ("same_markers", after.markers >= before.markers, juce::String (before.markers) + " -> " + juce::String (after.markers));
        {
            // LYNX's own devices keep every parameter (the project file's state inside the DAWproject file): the effects
            // (kind + parameters) of every insert and the native instruments (kind + model parameters), as multisets
            const auto effects = [] (const Song& s) {
                std::multiset<std::string> out;
                for (const auto& m : s.mixer)
                    for (const auto& fx : m.effects)
                        if (fx.kind != EffectKind::plugin)
                        {
                            std::string key = std::to_string ((int) fx.kind);
                            for (const auto v : fx.params)
                                key += " " + std::to_string (v);
                            out.insert (key);
                        }
                return out;
            };
            const auto instruments = [] (const Song& s) {
                std::multiset<std::string> out;
                for (const auto& c : s.channels)
                    if (c.instrument != InstrumentKind::audioClip && c.plugin.foreign.format == ForeignPlugin::Format::none)
                    {
                        std::string key = std::to_string ((int) c.instrument);
                        for (const auto v : c.modelParams)
                            key += " " + std::to_string (v);
                        out.insert (key);
                    }
                return out;
            };
            const auto e0 = effects (engine.getSong()), e1 = effects (back.getSong());
            const auto i0 = instruments (engine.getSong()), i1 = instruments (back.getSong());
            const auto diff = [&report] (const char* what, const std::multiset<std::string>& a, const std::multiset<std::string>& b) {
                std::vector<std::string> gone, extra;
                std::set_difference (a.begin(), a.end(), b.begin(), b.end(), std::back_inserter (gone));
                std::set_difference (b.begin(), b.end(), a.begin(), a.end(), std::back_inserter (extra));
                for (std::size_t k = 0; k < gone.size() && k < 8; ++k)
                    report.add (juce::String (what) + " only before: " + juce::String (gone[k]).substring (0, 160));
                for (std::size_t k = 0; k < extra.size() && k < 8; ++k)
                    report.add (juce::String (what) + " only after: " + juce::String (extra[k]).substring (0, 160));
            };
            // (information, not a gate: the export leaves out the inserts no track plays through, with their effects, and
            // a plug-in that is not VST2 / VST3 / CLAP; both are in its notes)
            report.add ("info native effects identical: " + juce::String (e0 == e1 ? "yes" : "no") + " " + juce::String ((int) e0.size()) + " -> " + juce::String ((int) e1.size()));
            diff ("effect", e0, e1);
            report.add ("info native instruments identical: " + juce::String (i0 == i1 ? "yes" : "no") + " " + juce::String ((int) i0.size()) + " -> " + juce::String ((int) i1.size()));
            diff ("instrument", i0, i1);
        }
        {
            const auto& r = back.getImportReport();
            const auto list = [&report] (const char* what, const std::map<std::string, int>& m) {
                for (const auto& [name, count] : m)
                    report.add (juce::String ("import report: ") + what + " " + juce::String::fromUTF8 (name.c_str()) + " x" + juce::String (count));
            };
            list ("converted", r.converted);
            list ("approximated", r.approximated);
            list ("unsupported", r.unsupported);
            list ("not reproduced", r.notReproduced); // (round qafix190kq)
            list ("dropped effect", r.droppedEffects);
            list ("partial effect", r.partialEffects);
            report.add ("import report: missing samples " + juce::String (r.missingSamples));
        }
        return finish();
    }


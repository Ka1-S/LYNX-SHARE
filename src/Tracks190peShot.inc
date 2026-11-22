    // LYNX --tracks190peshot <prefix> (round 190pe, 0.1.32: research190oe P20 Track versions, P11 Import tracks from another
    // project) off-screen through the real components: the Pattern layout's Playlist with a track that has versions (the
    // header's version chip), the track menu, the versions menu, the other version shown, the Clip-launch arrangement's
    // headers, File › Import, the "Import tracks from another project" window and the Playlist after the import; the undo
    // steps. <prefix>-checks.txt lists the checks. Included in the app class of Main.cpp (savePng / saveMenus).
    // @150: LYNX_SHOT_SCALE=1.5 LYNX_SHOT_SUFFIX=@150.
    bool runTracks190peShot (const juce::String& prefix)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (lynx::ui::showingForShots(), true);
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const char* name, bool ok, const juce::String& detail = {}) {
            report.add (juce::String (name) + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        using namespace lynx;
        namespace tv = trackversions;
        ui::motion::enabled() = false;
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        const auto bar = (std::int64_t) 4 * ppq;

        // four tracks: Drums, Bass, Vox (an audio clip through its own insert, an EQ on it), Keys; 125 BPM
        const auto makeSong = [bar] (bool other) {
            auto s = Song::makeDefault();
            s.bpm = other ? 98.0 : 125.0;
            s.playlist.clear();
            s.patterns.clear();
            s.channels.clear();
            if (s.mixer.size() < 8)
                s.mixer.resize (8);
            const std::pair<const char*, std::uint32_t> named[] { { other ? "Rhodes" : "Kick", 0xFFD9A62Bu }, { other ? "Sub" : "Bass", 0xFF6E9BD8u },
                                                                  { other ? "Strings" : "Keys", 0xFF5FB36Bu } };
            for (const auto& [name, colour] : named)
            {
                ChannelDef c;
                c.name = name;
                c.colour = colour;
                c.mixerTrack = (int) s.channels.size() + 1;
                s.channels.push_back (c);
            }
            ChannelDef vox;
            vox.name = other ? "Choir" : "Vox";
            vox.instrument = InstrumentKind::audioClip;
            vox.samplePath = other ? "choir.wav" : "vox.wav";
            vox.mixerTrack = 4;
            s.channels.push_back (vox);
            const char* inserts[] { "Master", named[0].first, named[1].first, named[2].first, other ? "Choir bus" : "Vox bus" };
            for (int t = 1; t < 5; ++t)
                s.mixer[(std::size_t) t].name = inserts[t];
            s.mixer[4].effects.emplace_back().kind = EffectKind::eq;
            s.mixer[4].effects.emplace_back().kind = EffectKind::reverb;
            const char* trackNames[] { named[0].first, named[1].first, other ? "Choir" : "Vox", named[2].first };
            s.playlistTracks.resize (4);
            for (int t = 0; t < 4; ++t)
                s.playlistTracks[(std::size_t) t].name = trackNames[t];
            for (int c = 0; c < 3; ++c)
            {
                auto& p = s.patterns.emplace_back();
                p.name = s.channels[(std::size_t) c].name;
                p.colour = s.channels[(std::size_t) c].colour;
                for (int k = 0; k < 8; ++k)
                {
                    Note n;
                    n.channel = c;
                    n.key = c == 0 ? 60 : 48 + 12 * (c % 2) + k % 3;
                    n.start = k * ppq / 2;
                    n.length = ppq / 4;
                    p.notes.push_back (n);
                }
                const int track = c < 2 ? c : 3;
                for (int k = 0; k < 4; ++k)
                {
                    Clip clip;
                    clip.pattern = c;
                    clip.track = track;
                    clip.start = k * 2 * bar;
                    clip.length = 2 * bar;
                    s.playlist.push_back (clip);
                }
            }
            for (int k = 0; k < 2; ++k)
            {
                Clip a;
                a.kind = ClipKind::audio;
                a.channel = 3;
                a.track = 2;
                a.start = k * 4 * bar;
                a.length = 3 * bar;
                s.playlist.push_back (a);
            }
            return s;
        };

        AudioEngine engine (false);
        engine.setSong (makeSong (false));
        engine.dispatchPendingMessages();
        MainComponent ui (engine, false);
        ui.setSize (1920, 1080);
        engine.dispatchPendingMessages();
        ui.selectTabForSnapshot (2);
        engine.dispatchPendingMessages();
        auto& pl = ui.getPlaylistForSnapshot();
        pl.setViewForSnapshot (110.0, 0, 0);
        const auto write = [&prefix] (juce::Component& c, const juce::String& name) {
            c.resized();
            savePng (shot::snapshot (c), prefix + "-" + name + ".png");
        };
        const auto zoom = [&prefix] (juce::Component& c, juce::Rectangle<int> r, float k, const juce::String& name) {
            savePng (c.createComponentSnapshot (r.getIntersection (c.getLocalBounds()), true, k * shot::scale()), prefix + "-" + name + ".png");
        };
        const auto headers = [&pl] { return juce::Rectangle<int> (0, pl.trackRowForSnapshot (0).getY(), pl.headerWidthForSnapshot() + 6 * 110, 6 * 60); };
        const auto has = [] (const juce::PopupMenu& m, const juce::String& text) {
            for (juce::PopupMenu::MenuItemIterator it (m, true); it.next();)
                if (it.getItem().text.startsWith (text))
                    return true;
            return false;
        };
        const auto subMenu = [] (const juce::PopupMenu& m, const juce::String& text) -> juce::PopupMenu {
            for (juce::PopupMenu::MenuItemIterator it (m, true); it.next();)
                if (it.getItem().text == text && it.getItem().subMenu != nullptr)
                    return *it.getItem().subMenu;
            return {};
        };
        const auto voxClips = [&engine] {
            std::vector<Clip> out;
            for (const auto& c : engine.getSong().playlist)
                if (c.track == 2)
                    out.push_back (c);
            std::sort (out.begin(), out.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
            return out;
        };

        // ---- P20: the Vox track gets a second version (a duplicate), then a third half its length, with the EQ changed
        write (ui, "01-pattern-before");
        check ("no_chip_without_versions", pl.trackVersionChip (2).isEmpty());
        const auto patternMenus = tv::patternMenus();
        tv::patternMenus() = false;
        check ("pattern_extra_off_track_menu_unchanged", ! has (pl.getTrackMenuForSnapshot (2), tr ("Track versions")));
        tv::patternMenus() = true;
        const auto trackMenu = pl.getTrackMenuForSnapshot (2);
        check ("pattern_track_menu_versions", has (trackMenu, tr ("Track versions")));
        saveMenus (juce::StringArray { "Track 3 (Vox)" }, std::vector<juce::PopupMenu> { trackMenu }, prefix + "-02-pattern-track-menu.png");
        saveMenus (juce::StringArray { tr ("Track versions") }, std::vector<juce::PopupMenu> { pl.buildTrackVersionMenu (2) }, prefix + "-03-versions-menu-none.png");
        const auto takeA = voxClips();
        engine.editSongStructural ([] (Song& s) { tv::add (s, 2, tv::Start::duplicate, tr ("Version {0}", { "2" }).toStdString(), tr ("Version {0}", { "1" }).toStdString()); });
        engine.editSongStructural ([] (Song& s) { tv::rename (s, 2, 1, "Take B"); tv::rename (s, 2, 0, "Take A"); });
        engine.editSong ([] (Song& s) {
            for (auto& c : s.playlist)
                if (c.track == 2)
                    c.length /= 2;
            if (! s.mixer[4].effects.empty())
                s.mixer[4].effects[0].enabled = false;
        });
        engine.dispatchPendingMessages();
        pl.headerOptionsChanged();
        const auto takeB = voxClips();
        check ("chip_shown", ! pl.trackVersionChip (2).isEmpty(), pl.trackVersionChip (2).toString());
        write (ui, "04-pattern-version-b");
        zoom (pl, headers(), 1.5f, "04b-pattern-version-b-headers@1.5x");
        const auto versionsMenu = pl.buildTrackVersionMenu (2);
        check ("versions_menu_lists", has (versionsMenu, "Take A") && has (versionsMenu, "Take B") && has (versionsMenu, tr ("Duplicate version")));
        saveMenus (juce::StringArray { tr ("Track versions") }, std::vector<juce::PopupMenu> { versionsMenu }, prefix + "-05-versions-menu.png");
        // the switch: one undo step
        engine.editSongStructural ([] (Song& s) { tv::switchTo (s, 2, 0); });
        engine.dispatchPendingMessages();
        check ("switch_back_to_a_exact", voxClips() == takeA && engine.getSong().mixer[4].effects[0].enabled);
        write (ui, "06-pattern-version-a");
        engine.undo();
        engine.dispatchPendingMessages();
        check ("undo_of_switch", voxClips() == takeB && tv::active (engine.getSong(), 2) == 1 && ! engine.getSong().mixer[4].effects[0].enabled);
        // promote: bars 3-4 of Take A into Take B (a time selection first)
        pl.setTimeSelectionForSnapshot (2, 4);
        engine.dispatchPendingMessages();
        saveMenus (juce::StringArray { tr ("Track versions") }, std::vector<juce::PopupMenu> { pl.buildTrackVersionMenu (2) }, prefix + "-07-versions-menu-selection.png");
        engine.editSong ([] (Song& s) { tv::promote (s, 2, 0, s.loopStart, s.loopEnd); });
        engine.dispatchPendingMessages();
        write (ui, "08-pattern-promoted");
        {
            juce::String why; // (the paint gate's project with versions)
            check ("saved_versions_project", ProjectFile::save (engine.getSong(), juce::File (prefix + "-versions.lynx"), why), why);
        }

        // ---- the Clip-launch layout's arrangement: the same chip on Live's headers
        {
            ui.setClipLaunchWorkflow (true);
            ui.showPanelsForSnapshot ({ workspace::Panel::browser, workspace::Panel::playlist });
            ui.setClipGridView (PlaylistArea::GridView::song);
            engine.dispatchPendingMessages();
            write (ui, "09-cliplaunch-arrangement-versions");
            check ("cliplaunch_track_menu_versions", has (ui.getPlaylistForSnapshot().getTrackMenuForSnapshot (2), tr ("Track versions")));
            ui.setClipLaunchWorkflow (false);
            engine.dispatchPendingMessages();
        }
        // ---- Simple: no versions there
        {
            ui.setLayoutStyle (layoutstyle::Style::simple);
            engine.dispatchPendingMessages();
            check ("simple_no_versions", ! has (ui.getPlaylistForSnapshot().getTrackMenuForSnapshot (2), tr ("Track versions"))
                                             && ui.getPlaylistForSnapshot().trackVersionChip (2).isEmpty());
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            engine.dispatchPendingMessages();
        }

        // ---- P11: File › Import › Tracks from another project...
        {
            const auto names = ui.getMenuNamesForSnapshot();
            juce::PopupMenu file;
            for (int m = 0; m < names.size(); ++m)
                if (names[m] == tr ("File") || names[m].removeCharacters ("&") == tr ("File"))
                    file = ui.getMenuForSnapshot (m);
            const auto imports = subMenu (file, tr ("Import"));
            check ("file_import_menu_item", has (imports, tr ("Tracks from another project...")) && has (imports, tr ("MIDI file...")));
            saveMenus (juce::StringArray { tr ("Import") }, std::vector<juce::PopupMenu> { imports }, prefix + "-10-file-import-menu.png");
        }
        auto other = makeSong (true);
        tv::add (other, 3, tv::Start::duplicate, "Strings B", "Strings A");
        std::unique_ptr<juce::AlertWindow> window (buildTrackImportWindow ("Other song.flp", other, engine.getSong().loopStart, { 1, 2 }));
        window->setVisible (false);
        write (*window, "11-import-window");
        const auto choice = trackImportChoiceOf (*window);
        check ("window_choice", (choice.tracks == std::vector<int> { 1, 2 }) && choice.options.withInserts && choice.options.withAutomation && choice.options.at == 0);
        const auto before = engine.getSong();
        trackimport::Result result;
        auto picked = choice;
        picked.tracks.push_back (3); // (Strings, with its two versions)
        engine.editSongStructural ([&] (Song& s) { result = trackimport::importTracks (s, other, picked.tracks, picked.options); });
        engine.dispatchPendingMessages();
        pl.headerOptionsChanged();
        check ("import_three_tracks", result.ok && result.tracks.size() == 3, juce::String ((int) result.tracks.size()));
        check ("import_inserts", result.inserts.size() == 3 && engine.getSong().mixer[(std::size_t) result.inserts.back()].effects.size() == 2,
               juce::String ((int) result.inserts.size()));
        check ("import_versions_came", tv::count (engine.getSong(), result.tracks.back()) == 2);
        check ("import_old_tracks_unchanged", std::equal (before.channels.begin(), before.channels.end(), engine.getSong().channels.begin())
                                                  && std::equal (before.playlistTracks.begin(), before.playlistTracks.end(), engine.getSong().playlistTracks.begin()));
        report.add ("report: " + trackImportReport (result, "Other song.flp", {}).replaceCharacter ('\n', '|'));
        pl.setViewForSnapshot (110.0, 0, 0);
        write (ui, "12-pattern-after-import");
        ui.selectTabForSnapshot (3);
        engine.dispatchPendingMessages();
        write (ui, "13-mixer-after-import");
        ui.selectTabForSnapshot (2);
        engine.dispatchPendingMessages();
        engine.undo();
        engine.dispatchPendingMessages();
        // (editSongStructural first adds the working time so far to the song: the undo step holds that, the rest is as before)
        const auto withoutWorkTime = [] (Song s) {
            s.info.workSeconds = 0.0;
            return ProjectFile::toJson (s, false);
        };
        check ("undo_removes_the_import", withoutWorkTime (engine.getSong()) == withoutWorkTime (before));

        // ---- .lynx round trip of the versions and the copy for 0.1.30 (one line)
        {
            juce::String why;
            const auto back = ProjectFile::fromJson (ProjectFile::toJson (engine.getSong()), why);
            check ("lynx_round_trip", back && back->playlistTracks.size() > 2 && back->playlistTracks[2].versions == engine.getSong().playlistTracks[2].versions
                                          && back->playlistTracks[2].activeVersion == 1, why);
            const auto copy = olderlynx::forRelease (engine.getSong(), "0.1.30");
            const auto lines = std::count_if (copy.lost.begin(), copy.lost.end(), [] (const olderlynx::Loss& l) { return juce::String (l.text).contains ("track version"); });
            check ("older_copy_line", copy.ok && lines == 1 && ! copy.json.contains ("playlistTrackVersions"));
        }

        tv::patternMenus() = patternMenus;
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        juce::File (prefix + "-checks.txt").replaceWithText (report.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

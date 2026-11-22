    // LYNX --trackstates190opshot <prefix> (round 190op, 0.1.31: research190oe P4 Hide / Deactivate tracks, P7 Capture
    // automation) off-screen through the real components, in the three layouts: the Playlist before and with two tracks
    // hidden, "Show hidden tracks" (dimmed rows, a crossed eye), the track menu, the Playlist menu, a deactivated channel
    // in the Channel rack and its menu, a deactivated insert in the Mixer and its menu, the record menu with Capture
    // automation; Clip-launch: the Session grid with a hidden column and a deactivated track, the track title menu (Live's
    // Freeze Track group); Simple: the track menu. <prefix>-checks.txt lists the checks. Included in the app class of
    // Main.cpp (savePng / saveMenus). @150: LYNX_SHOT_SCALE=1.5 LYNX_SHOT_SUFFIX=@150.
    bool runTrackStatesShot190op (const juce::String& prefix)
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
        ui::motion::enabled() = false;
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        const auto bar = (std::int64_t) 4 * ppq;
        const auto patternMenus = trackstates::patternMenus();
        const auto showHidden = PlaylistComponent::showHiddenTracks;

        // six tracks: Drums, Lead, Bass, an empty track, Pad, FX; Bass and the empty track get hidden
        const auto makeSong = [bar] {
            auto s = Song::makeDefault();
            s.playlist.clear();
            s.patterns.clear();
            s.channels.resize (1);
            s.channels[0].name = "Kick";
            s.channels[0].mixerTrack = 1;
            const std::pair<const char*, std::uint32_t> named[] { { "Lead", 0xFFE5383Bu }, { "Bass", 0xFF6E9BD8u }, { "Pad", 0xFF5FB36Bu } };
            for (const auto& [name, colour] : named)
            {
                ChannelDef c;
                c.name = name;
                c.colour = colour;
                c.mixerTrack = (int) s.channels.size() + 1;
                s.channels.push_back (c);
            }
            if (s.mixer.size() < 8)
                s.mixer.resize (8);
            const char* inserts[] { "Master", "Kick", "Lead", "Bass", "Pad", "FX" };
            for (int t = 1; t < 6; ++t)
                s.mixer[(std::size_t) t].name = inserts[t];
            s.mixer[3].effects.emplace_back().kind = EffectKind::reverb;
            const char* trackNames[] { "Drums", "Lead", "Bass", "", "Pad", "FX" };
            s.playlistTracks.resize (6);
            for (int t = 0; t < 6; ++t)
                s.playlistTracks[(std::size_t) t].name = trackNames[t];
            for (int c = 0; c < 4; ++c)
            {
                auto& p = s.patterns.emplace_back();
                p.name = s.channels[(std::size_t) c].name;
                p.colour = c == 0 ? 0xFFD9A62Bu : s.channels[(std::size_t) c].colour;
                for (int k = 0; k < 8; ++k)
                {
                    Note n;
                    n.channel = c;
                    n.key = c == 0 ? 60 : 48 + 12 * (c % 2) + k % 3;
                    n.start = k * ppq / 2;
                    n.length = ppq / 4;
                    p.notes.push_back (n);
                }
                const int track = c < 3 ? c : 4;
                for (int k = 0; k < 4; ++k)
                {
                    Clip clip;
                    clip.pattern = c;
                    clip.track = track;
                    clip.start = k * 2 * bar + (c == 3 ? 4 * bar : 0);
                    clip.length = 2 * bar;
                    s.playlist.push_back (clip);
                }
            }
            Clip fx;
            fx.pattern = 1;
            fx.track = 5;
            fx.start = 6 * bar;
            fx.length = bar;
            s.playlist.push_back (fx);
            return s;
        };

        AudioEngine engine (false);
        engine.setSong (makeSong());
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
        const auto headers = [&pl] { return juce::Rectangle<int> (0, pl.trackRowForSnapshot (0).getY(), pl.headerWidthForSnapshot() + 6 * 110, 7 * 60); };
        const auto has = [] (const juce::PopupMenu& m, const juce::String& text) {
            for (juce::PopupMenu::MenuItemIterator it (m, true); it.next();)
                if (it.getItem().text.startsWith (text))
                    return true;
            return false;
        };

        // ---- Pattern layout, the extra off: FL's menus as they are (nothing added), then on
        trackstates::patternMenus() = false;
        check ("pattern_extra_off_track_menu_unchanged", ! has (pl.getTrackMenuForSnapshot (2), tr ("Hide track")));
        check ("pattern_extra_off_channel_menu_unchanged", ! has (ui.getRackForSnapshot().getChannelMenuForSnapshot (2), tr ("Deactivate")));
        write (ui, "01-pattern-before");
        trackstates::patternMenus() = true;
        const auto trackMenu = pl.getTrackMenuForSnapshot (2);
        check ("pattern_track_menu_hide", has (trackMenu, tr ("Hide track")));
        saveMenus (juce::StringArray { "Track 3 (Bass)" }, std::vector<juce::PopupMenu> { trackMenu }, prefix + "-02-pattern-track-menu.png");

        // hide Bass and the empty track (the menu's own edit: one undo step)
        engine.editSong ([] (Song& s) { trackstates::setHidden (s, { 2, 3 }, true); });
        engine.dispatchPendingMessages();
        PlaylistComponent::showHiddenTracks = false;
        pl.headerOptionsChanged();
        const auto rowsHidden = pl.trackRowForSnapshot (4).getY() - pl.trackRowForSnapshot (1).getBottom();
        check ("hidden_rows_take_no_space", rowsHidden <= 1, juce::String (rowsHidden));
        write (ui, "03-pattern-two-hidden");
        zoom (pl, headers(), 1.5f, "03b-pattern-two-hidden-headers@1.5x");
        const auto plMenu = ui.getPanelMenuForSnapshot (workspace::Panel::playlist);
        check ("playlist_menu_show_hidden", has (plMenu, tr ("Show hidden tracks ({0})", { "2" })) && has (plMenu, tr ("Hide unused tracks")));
        saveMenus (juce::StringArray { "Playlist" }, std::vector<juce::PopupMenu> { plMenu }, prefix + "-04-playlist-menu.png");
        PlaylistComponent::showHiddenTracks = true;
        pl.headerOptionsChanged();
        check ("show_hidden_gives_the_rows_back", pl.trackRowForSnapshot (2).getHeight() > 10, juce::String (pl.trackRowForSnapshot (2).getHeight()));
        write (ui, "05-pattern-hidden-shown");
        zoom (pl, headers(), 1.5f, "05b-pattern-hidden-shown-headers@1.5x");

        // ---- Deactivate: Bass (channel 2) and its insert 3 (a reverb on it)
        engine.setDeactivated ({ 2 }, { 3 }, true);
        engine.dispatchPendingMessages();
        check ("deactivated_flags", engine.channelDeactivated (2) && engine.insertDeactivated (3) && ! engine.channelDeactivated (1));
        write (ui, "06-pattern-deactivated-track");
        zoom (pl, headers(), 1.5f, "06b-pattern-deactivated-headers@1.5x");
        ui.selectTabForSnapshot (0);
        engine.dispatchPendingMessages();
        write (ui, "07-rack-deactivated");
        const auto channelMenu = ui.getRackForSnapshot().getChannelMenuForSnapshot (2);
        check ("channel_menu_reactivate", has (channelMenu, tr ("Reactivate")));
        saveMenus (juce::StringArray { "Bass" }, std::vector<juce::PopupMenu> { channelMenu }, prefix + "-08-channel-menu.png");
        ui.selectTabForSnapshot (3);
        engine.dispatchPendingMessages();
        write (ui, "09-mixer-deactivated");
        auto& strips = ui.getMixerForSnapshot().getStripsForSnapshot();
        const auto mixerMenu = strips.buildMenu (3);
        check ("mixer_menu_reactivate", has (mixerMenu, tr ("Reactivate")));
        saveMenus (juce::StringArray { "Insert 3 (Bass)" }, std::vector<juce::PopupMenu> { mixerMenu }, prefix + "-10-mixer-track-menu.png");
        saveMenus (juce::StringArray { "Insert 2 (Lead)" }, std::vector<juce::PopupMenu> { strips.buildMenu (2) }, prefix + "-10b-mixer-track-menu-active.png");

        // ---- the record menu: Capture automation under Capture MIDI
        const auto recordMenu = ui.getFullRecordMenuForSnapshot();
        check ("record_menu_capture_automation", has (recordMenu, tr ("Capture automation: the moves just made without recording become automation")));
        saveMenus (juce::StringArray { "Recording" }, std::vector<juce::PopupMenu> { recordMenu }, prefix + "-11-record-menu.png");
        ui.selectTabForSnapshot (2);
        engine.dispatchPendingMessages();

        // ---- Clip-launch: the Session grid (a hidden column, a deactivated track) and the track title menu
        {
            PlaylistComponent::showHiddenTracks = false;
            ui.setClipLaunchWorkflow (true);
            ui.showPanelsForSnapshot ({ workspace::Panel::browser, workspace::Panel::playlist });
            ui.setClipGridView (PlaylistArea::GridView::grid);
            engine.editSong ([] (Song& s) {
                if (! s.clipGrid)
                    s.clipGrid.emplace();
                for (int t = 0; t < 6; ++t)
                {
                    Clip c;
                    c.pattern = t < 3 ? t : 3;
                    c.track = t;
                    c.length = 2 * 4 * ppq;
                    s.clipGrid->clips.push_back (c);
                }
            });
            engine.dispatchPendingMessages();
            write (ui, "12-cliplaunch-grid-hidden-and-deactivated");
            if (auto* grid = ui.getClipGridForSnapshot())
            {
                const auto menu = clipGridHeaderMenu (*grid, 2);
                check ("cliplaunch_header_menu", has (menu, tr ("Reactivate Track")) && has (menu, tr ("Hide Track")));
                saveMenus (juce::StringArray { "Bass" }, std::vector<juce::PopupMenu> { menu }, prefix + "-13-cliplaunch-track-menu.png");
                const auto other = clipGridHeaderMenu (*grid, 1);
                check ("cliplaunch_header_menu_active", has (other, tr ("Deactivate Track")));
                saveMenus (juce::StringArray { "Lead" }, std::vector<juce::PopupMenu> { other }, prefix + "-13b-cliplaunch-track-menu-active.png");
            }
            else
                check ("cliplaunch_grid_shown", false);
            PlaylistComponent::showHiddenTracks = true;
            ui.resized();
            engine.dispatchPendingMessages();
            if (auto* grid = ui.getClipGridForSnapshot()) // (round 190ou: the Session mixer's columns, as the grid's timer sets them)
                syncClipGridMixerForSnapshot (*grid);
            if (auto* grid = ui.getClipGridForSnapshot()) // (round 190ou: the deactivated track's strip is dimmed)
                if (auto* mixer = clipGridMixer (*grid))
                {
                    const auto image = mixer->createComponentSnapshot (mixer->getLocalBounds(), true, 1.0f);
                    const auto mean = [&image, mixer] (int column) { // (the strip's mean brightness)
                        double sum = 0.0;
                        int n = 0;
                        for (int y = 0; y < image.getHeight(); y += 2)
                            for (int x = mixer->stripXForSnapshot (column) + 2; x < mixer->stripXForSnapshot (column) + mixer->stripWidthForSnapshot (column) - 2; x += 2, ++n)
                                sum += image.getPixelAt (x, y).getBrightness();
                        return (float) (sum / juce::jmax (1, n));
                    };
                    const auto lead = mean (1), bass = mean (2);
                    check ("cliplaunch_session_strip_dimmed", bass < lead * 0.8f, juce::String (bass, 3) + " / " + juce::String (lead, 3));
                }
            write (ui, "14-cliplaunch-grid-hidden-shown");
            ui.setClipGridView (PlaylistArea::GridView::song);
            engine.dispatchPendingMessages();
            write (ui, "15-cliplaunch-arrangement");
            saveMenus (juce::StringArray { "Track 2 (Lead)" }, std::vector<juce::PopupMenu> { ui.getPlaylistForSnapshot().getTrackMenuForSnapshot (1) },
                       prefix + "-16-cliplaunch-arrangement-track-menu.png");
            ui.setClipLaunchWorkflow (false);
            engine.dispatchPendingMessages();
        }

        // ---- Simple: the track menu (Hide / Deactivate), the Playlist with the hidden tracks shown dimmed
        {
            ui.setLayoutStyle (layoutstyle::Style::simple);
            engine.dispatchPendingMessages();
            write (ui, "17-simple-hidden-shown");
            const auto menu = ui.getPlaylistForSnapshot().getTrackMenuForSnapshot (1);
            check ("simple_track_menu", has (menu, tr ("Hide track")) && has (menu, tr ("Deactivate track")));
            saveMenus (juce::StringArray { "Track" }, std::vector<juce::PopupMenu> { menu }, prefix + "-18-simple-track-menu.png");
            PlaylistComponent::showHiddenTracks = false;
            ui.resized();
            engine.dispatchPendingMessages();
            write (ui, "19-simple-two-hidden");
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            engine.dispatchPendingMessages();
        }

        // ---- undo: the grid's clips, then Reactivate (one step), then Show the tracks again (one step)
        engine.undo();
        engine.undo();
        check ("undo_reactivates", ! engine.getSong().channels[2].deactivated && ! engine.getSong().mixer[3].deactivated);
        engine.undo();
        check ("undo_unhides", trackstates::hiddenCount (engine.getSong()) == 0);

        trackstates::patternMenus() = patternMenus;
        PlaylistComponent::showHiddenTracks = showHidden;
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        juce::File (prefix + "-checks.txt").replaceWithText (report.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

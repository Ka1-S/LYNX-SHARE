    // --issueshot190jz <prefix> (round 190jz, issues #192, #194, #195): the docked Browser and the maximize area in the three
    // layouts (left, closed, right, panels that follow the free area), the audio clip fade handles as the mouse shows them
    // (FL 2026: small triangles in the waveform's colour on hover, white when hovered, the tension circle), the fade hints,
    // the tension menu and Settings › LYNX extras with the W switch. <prefix>-checks.txt lists what was checked. Included in
    // the app class of Main.cpp (it uses savePng / saveMenus / writeWaveDemo / lookAndFeel). @150: LYNX_SHOT_SCALE=1.5 and
    // LYNX_SHOT_SUFFIX=@150.
    bool runIssuesShot190jz (const juce::String& prefix)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const char* name, bool ok, const juce::String& detail = {}) {
            report.add (juce::String (name) + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        using namespace lynx;
        using workspace::Panel;
        const auto text = [] (juce::Rectangle<int> r) { return r.toString(); };

        // a loop of the demo kind as the audio clip's file
        const auto wav = juce::File (prefix + "-loop.wav");
        writeWaveDemo (wav, 120.0);

        ui::motion::enabled() = false; // (panels show at once)
        AudioEngine engine (false);
        MainComponent ui (engine, false);
        ui.setSize (1920, 1040);
        {
            auto song = Song::makeDefault();
            song.bpm = 120.0;
            ChannelDef audio;
            audio.instrument = InstrumentKind::audioClip;
            audio.name = "Main Vox";
            audio.samplePath = wav.getFullPathName().toStdString();
            audio.colour = 0xff3a9a4c;
            song.channels.push_back (audio);
            const auto ch = (int) song.channels.size() - 1;
            song.playlistTracks.resize (12);
            Clip a;
            a.kind = ClipKind::audio;
            a.channel = ch;
            a.track = 2;
            a.start = 4 * ppq;
            a.length = 8 * ppq;
            song.playlist.push_back (a); // (the fades are set below, step by step)
            Clip b = a;
            b.track = 4;
            b.start = 4 * ppq;
            b.length = 3 * ppq;
            song.playlist.push_back (b);
            song.playlistTracks[2].height = 84; // FL's capture: tall tracks (Alt+wheel)
            song.playlistTracks[4].height = 84;
            engine.setSong (std::move (song));
        }
        engine.dispatchPendingMessages();
        auto& ws = ui.workspaceForTest();
        const auto frameOf = [&ws] (Panel p) { return ws.frameBounds (ws.getFrame (p)); };
        const auto browserRect = [&] { return frameOf (Panel::browser); };
        const auto shoot = [&] (const juce::String& name) {
            engine.dispatchPendingMessages();
            savePng (shot::snapshot (ui), prefix + name);
        };

        // ---- #192: the docked Browser and the maximize area (FL 2026 shots fl-192-*)
        ws.resetLayout();
        ws.show (Panel::playlist);
        ws.toggleMaximised (Panel::playlist);
        {
            const auto b = browserRect(), p = frameOf (Panel::playlist);
            check ("browser_docked_left", ws.getBrowserDock() == workspace::Dock::left && ws.isBrowserDockedPanel (Panel::browser) && b.getX() == 0
                       && b.getHeight() == ws.getHeight(), text (b));
            check ("maximized_playlist_beside_browser", p.getX() == b.getRight() && p.getRight() == ws.getWidth() && p.getHeight() == ws.getHeight(),
                   text (p));
        }
        shoot ("-192-browser-left-playlist-max.png");
        ui.runActionForSnapshot ("panel.browser"); // Alt+F8: the Browser has the focus? then closed (FL's F-key rule)
        if (ws.isPanelVisible (Panel::browser))
            ui.runActionForSnapshot ("panel.browser");
        check ("alt_f8_closes_browser", ! ws.isPanelVisible (Panel::browser));
        check ("maximized_playlist_whole_desktop", frameOf (Panel::playlist) == ws.getLocalBounds(), text (frameOf (Panel::playlist)));
        shoot ("-192-browser-closed-playlist-max.png");
        ui.runActionForSnapshot ("panel.browser");
        check ("alt_f8_shows_browser_docked", ws.isBrowserDockedPanel (Panel::browser) && frameOf (Panel::playlist).getX() == browserRect().getRight());
        ws.dockBrowser (workspace::Dock::right);
        {
            const auto b = browserRect(), p = frameOf (Panel::playlist);
            check ("browser_docked_right", b.getRight() == ws.getWidth() && b.getY() == 0 && b.getHeight() == ws.getHeight(), text (b));
            check ("maximized_playlist_left_of_browser", p.getX() == 0 && p.getRight() == b.getX(), text (p));
        }
        shoot ("-192-browser-right-playlist-max.png");
        ws.dockBrowser (workspace::Dock::left);
        check ("browser_docked_left_again", browserRect().getX() == 0 && frameOf (Panel::playlist).getX() == browserRect().getRight());
        // restored windows follow the free area (FL: Alt+F8 moves them by the Browser's width; the one at the right edge keeps it)
        ws.toggleMaximised (Panel::playlist);
        ws.show (Panel::mixer);
        ws.toggleMaximised (Panel::mixer);
        ws.show (Panel::playlist);
        shoot ("-192-restored-windows-browser-shown.png");
        const auto mixerShown = frameOf (Panel::mixer), playlistShown = frameOf (Panel::playlist);
        const auto bw = browserRect().getWidth();
        ws.hide (Panel::browser);
        shoot ("-192-restored-windows-browser-closed.png");
        check ("maximized_mixer_follows", frameOf (Panel::mixer) == ws.getLocalBounds() && mixerShown.getX() == bw, text (mixerShown));
        check ("restored_playlist_follows", frameOf (Panel::playlist).getX() == playlistShown.getX() - bw && frameOf (Panel::playlist).getRight() == playlistShown.getRight(),
               text (playlistShown) + " -> " + text (frameOf (Panel::playlist)));
        ws.show (Panel::browser);
        check ("restored_playlist_back", frameOf (Panel::playlist) == playlistShown, text (frameOf (Panel::playlist)));
        ws.hide (Panel::mixer);
        // the Clip-launch layout (Live's browser is a side panel): the docked Browser and the maximized arrangement beside it
        ui.setLayoutStyle (layoutstyle::Style::clipLaunch);
        engine.dispatchPendingMessages();
        ws.show (Panel::browser);
        if (! ws.isMaximised (Panel::playlist))
            ws.toggleMaximised (Panel::playlist);
        check ("clip_launch_side_panel", ws.isBrowserDockedPanel (Panel::browser) && frameOf (Panel::playlist).getX() == browserRect().getRight(),
               text (frameOf (Panel::playlist)));
        shoot ("-192-cliplaunch-browser-left.png");
        // the Simple layout: one maximized window; the Browser opened beside it, closed it fills the window again
        ui.setLayoutStyle (layoutstyle::Style::simple);
        engine.dispatchPendingMessages();
        // (what the Simple layout's first start does, MainComponent::setLayoutStyle; this window keeps no layouts)
        for (const auto p : { Panel::channelRack, Panel::pianoRoll, Panel::mixer, Panel::browser })
            ws.hide (p);
        ws.show (Panel::playlist);
        if (! ws.isMaximised (Panel::playlist))
            ws.toggleMaximised (Panel::playlist);
        check ("simple_playlist_whole_window", ! ws.isPanelVisible (Panel::browser) && frameOf (Panel::playlist) == ws.getLocalBounds(),
               text (frameOf (Panel::playlist)));
        shoot ("-192-simple.png");
        ws.show (Panel::browser);
        check ("simple_browser_beside", frameOf (Panel::playlist).getX() == browserRect().getRight());
        shoot ("-192-simple-browser.png");
        ws.hide (Panel::browser);
        ui.setLayoutStyle (layoutstyle::Style::pattern);
        engine.dispatchPendingMessages();

        // ---- #194: the fade handles (FL 2026 shots fl-194-*)
        ui.selectTabForSnapshot (2);
        engine.dispatchPendingMessages();
        auto& pl = ui.getPlaylistForSnapshot();
        pl.setViewForSnapshot (240.0, 1, 0);
        engine.dispatchPendingMessages();
        pl.createComponentSnapshot (pl.getLocalBounds()); // (starts the waveform's analysis)
        ui::WaveformCache::getInstance()->waitUntilIdle (8000);
        const auto clipArea = [&] (int index, int pad) { return pl.clipBoundsForSnapshot (index).expanded (pad, pad).getIntersection (pl.getLocalBounds()); };
        const auto shootClip = [&] (int index, const juce::String& name) {
            pl.repaint();
            engine.dispatchPendingMessages();
            savePng (shot::snapshot (pl, clipArea (index, 14)), prefix + name);
        };
        const auto setClip = [&] (int index, std::function<void (Clip&)> change) {
            engine.editSong ([index, change] (Song& s) { change (s.playlist[(std::size_t) index]); });
            engine.dispatchPendingMessages();
        };
        // 1. idle, no fades: nothing drawn; 2. the clip hovered: the small triangles at the body's top corners
        pl.hoverFadeForSnapshot (-1, -1);
        shootClip (0, "-194-idle-clip.png");
        pl.hoverFadeForSnapshot (0, 0);
        shootClip (0, "-194-hover-clip.png");
        pl.hoverFadeForSnapshot (0, 1);
        shootClip (0, "-194-hover-fade-handle.png");
        const auto in0 = pl.fadeHandlePointForSnapshot (0, 0);
        {
            const auto h = pl.hintAt (in0.toInt() + juce::Point<int> (2, 2));
            check ("hover_hint_fade_in_auto", h.text.startsWith (tr ("Fade in: {0}", { "0'00.000''" })) && h.text.contains (tr ("(auto)"))
                       && h.text.contains (tr ("Hold Alt to snap")), h.text);
        }
        // 3. a fade-in of one beat (0.5 s at 120 BPM), dragged by hand: FL's S curve, the circle at its middle, manual
        setClip (0, [] (Clip& c) {
            c.fadeIn = ppq;
            clipops::setManualFade (c, 0, true);
        });
        {
            const auto h = pl.fadeDragHintForSnapshot (0, 1);
            check ("drag_hint_fade_in", h.text.startsWith (tr ("Fade in: {0}", { "0'00.500''" })), h.text);
            const auto hover = pl.hintAt (pl.fadeHandlePointForSnapshot (0, 0).toInt() + juce::Point<int> (2, 2));
            check ("hover_hint_manual_has_no_auto", ! hover.text.contains (tr ("(auto)")), hover.text);
        }
        pl.hoverFadeForSnapshot (0, 1);
        shootClip (0, "-194-drag-fade-in.png");
        pl.hoverFadeForSnapshot (-1, -1);
        shootClip (0, "-194-fade-in-idle.png");
        pl.hoverFadeForSnapshot (0, 3);
        shootClip (0, "-194-hover-tension.png");
        {
            const auto circle = pl.fadeTensionPointForSnapshot (0, 0);
            const auto r = pl.clipBoundsForSnapshot (0);
            check ("tension_circle_mid_fade", std::abs (circle.x - ((float) r.getX() + (float) (pl.xOfTickForSnapshot (ppq) - pl.xOfTickForSnapshot (0.0)) * 0.5f)) < 1.0f,
                   juce::String (circle.x) + "," + juce::String (circle.y));
            const auto h = pl.hintAt (circle.toInt());
            check ("hover_hint_tension_0", h.text == tr ("Tension: {0}%", { "0" }), h.text);
        }
        // 4. the tension at 15 % (FL's drag of 26 px up)
        setClip (0, [] (Clip& c) { c.fadeInTension = clipops::fadeTensionFromHandle (0.15); });
        {
            const auto h = pl.fadeDragHintForSnapshot (0, 3);
            check ("drag_hint_tension_15", h.text == tr ("Tension: {0}%", { "15" }), h.text);
        }
        pl.hoverFadeForSnapshot (0, 3);
        shootClip (0, "-194-drag-tension.png");
        // 5. a fade-out on the short clip, dragged by hand (FL: "Fade out: 0'00.112''")
        setClip (1, [] (Clip& c) {
            c.fadeOut = ppq / 2;
            clipops::setManualFade (c, 1, true);
        });
        {
            const auto h = pl.fadeDragHintForSnapshot (1, 2);
            check ("drag_hint_fade_out", h.text.startsWith (tr ("Fade out: {0}", { "0'00.250''" })), h.text);
        }
        pl.hoverFadeForSnapshot (1, 2);
        shootClip (1, "-194-drag-fade-out.png");
        pl.hoverFadeForSnapshot (-1, -1);
        saveMenus (juce::StringArray { "Fade in tension", "Fade out tension" },
                   std::vector<juce::PopupMenu> { pl.fadeTensionMenu (0, 0), pl.fadeTensionMenu (1, 1) }, prefix + "-194-tension-menu.png");
        // the whole Playlist with the faded clips, idle
        savePng (shot::snapshot (pl), prefix + "-194-playlist.png");

        // ---- #195: Home and the W extra
        {
            auto& km = ui.getKeyMapForSnapshot();
            check ("home_in_playlist", km.find ({ keys::home, keys::none }, keys::Context::playlist, false) == "playlist.toStart");
            check ("home_in_piano_roll", km.find ({ keys::home, keys::none }, keys::Context::pianoRoll, false) == "pianoRoll.toStart");
            check ("home_not_in_mixer", km.find ({ keys::home, keys::none }, keys::Context::mixer, false).empty());
            engine.seek (16.0 * ppq);
            engine.dispatchPendingMessages();
            ui.runActionForSnapshot ("playlist.toStart");
            check ("home_marks_the_start", EditorGlobals::get().seekMark == 0, juce::String (EditorGlobals::get().seekMark));
            check ("w_extra_off", ! transportedit::wGoesToStart());
            workflowextras::setExtra ("transport.wToStart", true);
            check ("w_extra_on", transportedit::wGoesToStart() && workflowextras::uiOptionsText().contains ("wToStart=1"));
            SettingsHooks hooks;
            auto page = makeExtrasPage (engine, hooks, false);
            page->setSize (760, 1500);
            savePng (shot::snapshot (*page), prefix + "-195-extras.png");
            workflowextras::setExtra ("transport.wToStart", false);
        }

        juce::File (prefix + "-checks.txt").replaceWithText (report.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

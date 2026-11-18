    // --qarest190lcshot <folder> [Live set] (rollup 0.1.30 step 7, round 190lc: the owner's two QA remainders), offscreen:
    // - QA-8: the View menu of the Pattern, Clip-launch and Simple layouts drawn as JUCE lays it out, on a backdrop as tall
    //   as a 1920 x 1080 screen's work area (48 px taskbar) at the --shot-scale asked for (1 = 100 %, 1.5 = 150 %). In the
    //   Clip-launch and Simple layouts the rarely used items sit in "Windows and tools", and the menu must stay within
    //   JUCE's own split limit (the work area less 24 px, PopupMenu's maxMenuH), so it opens in one column; the Pattern
    //   layout keeps FL's full menu (every one of its items is still in the other two menus or their submenu).
    // - QA-2: imported Live take lanes have no row in the Simple layout's track list, as in the clip grid; the Pattern
    //   Playlist and the Clip-launch Arrangement keep them under their track. With a Live set (e.g. Patience) its lanes,
    //   else a small song with a comp header and two imported lanes.
    // <folder>/checks.txt lists what was checked. Included in the app class of Main.cpp.
    bool runQaRest190lcShot (const juce::File& folder, const juce::String& setPath)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        folder.createDirectory();
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const juce::String& name, bool ok, const juce::String& detail = {}) {
            report.add (name + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        const auto writeImage = [&folder, &check] (const juce::Image& image, const juce::String& name) {
            const auto f = shot::file (folder.getChildFile (name + ".png"));
            f.deleteFile();
            bool ok = false;
            if (auto out = f.createOutputStream())
                ok = juce::PNGImageFormat().writeImageToStream (image, *out);
            check ("wrote " + name, ok && image.getWidth() > 0);
        };
        const auto plain = [] (juce::String t) { return t.upToFirstOccurrenceOf ("\t", false, false).removeCharacters ("&").trim(); };

        AudioEngine offline (false);
        MainComponent ui (offline, false);
        ui.setSize (1600, 900);
        offline.dispatchPendingMessages();

        // ---- QA-8: the View menus
        const auto scale = juce::jmax (1.0f, shot::scale());
        const auto workArea = (int) std::floor ((1080.0f - 48.0f) / scale); // logical px
        const auto limit = workArea - 24;                                     // JUCE: a taller menu is split into columns
        const auto scaleName = juce::String (juce::roundToInt (scale * 100.0f)) + "%";
        juce::StringArray patternItems;
        const std::pair<layoutstyle::Style, const char*> layouts[] { { layoutstyle::Style::pattern, "pattern" },
                                                                     { layoutstyle::Style::clipLaunch, "cliplaunch" },
                                                                     { layoutstyle::Style::simple, "simple" } };
        for (const auto& [style, name] : layouts)
        {
            ui.setLayoutStyle (style);
            offline.dispatchPendingMessages();
            const auto menu = ui.getMenuForSnapshot (4);
            juce::StringArray top, sub;
            const juce::PopupMenu* windowsAndTools = nullptr;
            int rows = 0;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            {
                const auto& item = it.getItem();
                ++rows;
                if (item.isSeparator)
                    continue;
                top.add (plain (item.text));
                if (plain (item.text) == plain (tr ("Windows and tools")) && item.subMenu != nullptr)
                    windowsAndTools = item.subMenu.get();
            }
            if (windowsAndTools != nullptr)
                for (juce::PopupMenu::MenuItemIterator it (*windowsAndTools); it.next();)
                    if (! it.getItem().isSeparator)
                        sub.add (plain (it.getItem().text));
            const auto image = columnMenuImage (menu);
            const auto height = juce::roundToInt ((float) image.getHeight() / shot::scale());
            const auto detail = juce::String (rows) + " rows, " + juce::String (height) + " px of " + juce::String (limit) + " at " + scaleName;
            if (style == layoutstyle::Style::pattern)
            {
                patternItems = top;
                check ("pattern_view_menu_is_fl_full", windowsAndTools == nullptr && top.contains (plain (tr ("Tempo tapper")))
                                                           && top.contains (plain (tr ("Detached panels (own window)"))), detail);
            }
            else
            {
                check (juce::String (name) + "_view_menu_fits_one_column", height <= limit, detail);
                check (juce::String (name) + "_windows_and_tools_submenu",
                       windowsAndTools != nullptr && sub.contains (plain (tr ("Tempo tapper"))) && ! top.contains (plain (tr ("Tempo tapper")))
                           && sub.contains (plain (tr ("Close all plugin windows"))),
                       juce::String (sub.size()) + " items: " + sub.joinIntoString (" | "));
                juce::StringArray missing;
                for (const auto& t : patternItems)
                    if (! top.contains (t) && ! sub.contains (t))
                        missing.add (t);
                check (juce::String (name) + "_keeps_every_pattern_item", missing.isEmpty(), missing.joinIntoString (" | "));
            }
            report.add (juce::String (name) + " top: " + top.joinIntoString (" | "));
            writeImage (image, juce::String ("viewmenu-") + name);
            if (windowsAndTools != nullptr)
                writeImage (columnMenuImage (*windowsAndTools), juce::String ("viewmenu-") + name + "-windows-and-tools");
            // the menu on a backdrop of the screen's work area: the dashed line is JUCE's split limit
            {
                const auto w = juce::roundToInt ((float) image.getWidth() / shot::scale());
                const auto canvasHeight = juce::jmax (workArea, height + 40);
                auto framed = shot::canvas (w + 220, canvasHeight);
                {
                    juce::Graphics g (framed);
                    shot::logical (g);
                    g.fillAll (theme::colour::bg0);
                    g.drawImage (image, juce::Rectangle<float> (10.0f, 24.0f, (float) w, (float) height));
                    g.setColour (theme::colour::bg3);
                    g.fillRect (0, workArea, w + 220, juce::jmax (0, canvasHeight - workArea));
                    g.setColour (height + 24 <= workArea ? juce::Colours::limegreen : juce::Colours::red);
                    const float dash[] { 6.0f, 4.0f };
                    g.drawDashedLine (juce::Line<float> (0.0f, (float) (limit + 24), (float) (w + 220), (float) (limit + 24)), dash, 2, 1.5f);
                    g.setColour (theme::colour::textPrimary);
                    g.setFont (juce::FontOptions (13.0f));
                    g.drawMultiLineText ("1920 x 1080 at " + scaleName + "\nwork area " + juce::String (workArea) + " px\nmenu " + juce::String (height)
                                             + " px\nJUCE splits above " + juce::String (limit) + " px",
                                         w + 24, 40, 190);
                }
                writeImage (framed, juce::String ("viewmenu-") + name + "-on-screen");
            }
        }

        // ---- QA-2: imported take lanes in the Simple layout
        if (setPath.isNotEmpty())
        {
            ui.openFile (juce::File (setPath));
            offline.dispatchPendingMessages();
        }
        else
        {
            offline.editSong ([] (Song& s) {
                s.playlistTracks.resize (juce::jmax ((std::size_t) 6, s.playlistTracks.size()));
                s.playlistTracks[0].name = "Vocal";
                s.playlistTracks[0].compTrack = true;
                for (int t : { 1, 2 })
                {
                    s.playlistTracks[(std::size_t) t].name = "Vocal " + std::to_string (t);
                    s.playlistTracks[(std::size_t) t].grouped = true;
                    s.playlistTracks[(std::size_t) t].muted = true;
                    s.playlistTracks[(std::size_t) t].takeLane = true;
                }
                s.playlistTracks[3].name = "Drums";
            });
            offline.dispatchPendingMessages();
        }
        ui.setSize (1600, 900);
        const auto lanes = [&offline] {
            std::vector<int> out;
            const auto& song = offline.getSong();
            for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
                if (song.playlistTracks[(std::size_t) t].takeLane && song.playlistTracks[(std::size_t) t].grouped)
                    out.push_back (t);
            return out;
        }();
        check ("take_lanes_in_project", ! lanes.empty(), juce::String ((int) lanes.size()) + " lanes");
        for (const auto& [style, name] : layouts)
        {
            ui.setLayoutStyle (style);
            offline.dispatchPendingMessages();
            ui.selectTabForSnapshot (2); // the Playlist panel, maximised
            auto& playlist = ui.getPlaylistForSnapshot();
            int hidden = 0, folded = 0;
            for (const auto t : lanes)
            {
                hidden += playlist.trackRowForSnapshot (t).getHeight() <= 0 ? 1 : 0;
                folded += noteedit::isFoldedAway (offline.getSong(), t) || arrange::isFolded (offline.getSong(), t) ? 1 : 0;
            }
            const auto detail = juce::String (hidden) + " of " + juce::String ((int) lanes.size()) + " lanes without a row (" + juce::String (folded) + " in folded groups)";
            if (style == layoutstyle::Style::simple)
                check ("simple_hides_take_lanes", hidden == (int) lanes.size(), detail);
            else
                check (juce::String (name) + "_keeps_take_lanes", hidden == folded, detail);
            writeImage (shot::snapshot (ui), juce::String ("takelanes-") + name);
        }
        ui.setLayoutStyle (layoutstyle::Style::pattern);
        offline.dispatchPendingMessages();

        shot::file (folder.getChildFile ("checks.txt")).replaceWithText (report.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        return allOk;
    }

    // --issuesshot190ng <prefix> (round 190ng, issues #215, #217, #219, #220, #222), offscreen through the real components:
    // Esc closes the focused panel (FL 2026; a selection is cleared first; the switch keeps them open), FL's Backspace snap
    // key from the key map, the icon picker's search box and the "Rename, color and icon" window, the big track icons of
    // tall Playlist rows, and the Export window's file name wildcards. <prefix>-checks.txt lists the checks; pictures @100
    // and @150. Included in the app class of Main.cpp.
    bool runIssuesShot190ng (const juce::String& prefix)
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
        const auto write = [&prefix] (juce::Component& c, juce::Rectangle<int> area, const juce::String& name) {
            area = area.getIntersection (c.getLocalBounds());
            if (area.isEmpty())
                return;
            savePng (c.createComponentSnapshot (area, true, 1.0f), prefix + "-" + name + ".png");
            savePng (c.createComponentSnapshot (area, true, 1.5f), prefix + "-" + name + "@150.png");
        };
        using namespace lynx;
        using workspace::Panel;

        // ================= #217: Esc closes the focused window =================
        {
            AudioEngine offline (false);
            {
                auto song = Song::makeDefault();
                Clip c;
                c.kind = ClipKind::pattern;
                c.pattern = 0;
                c.track = 0;
                c.start = 0;
                c.length = 4 * ppq * 4;
                song.playlist.push_back (c);
                offline.setSong (song);
            }
            MainComponent ui (offline, false);
            ui.setSize (1600, 900);
            offline.dispatchPendingMessages();
            auto& ws = ui.workspaceForTest();
            const auto esc = juce::KeyPress (juce::KeyPress::escapeKey);
            check ("FL 2026's default: on", escclose::enabled());
            for (const auto panel : { Panel::channelRack, Panel::mixer, Panel::pianoRoll })
            {
                ui.showPanelsForSnapshot ({ panel });
                ws.show (panel);
                ws.activate (panel);
                const auto was = ws.isPanelVisible (panel);
                ui.keyPressed (esc);
                check ("Esc closes the focused " + juce::String (panel == Panel::channelRack ? "Channel rack" : panel == Panel::mixer ? "Mixer" : "Piano roll"),
                       was && ! ws.isPanelVisible (panel));
            }
            // the Playlist with a selection: the first Esc clears it (LYNX, issue #218), the next closes the window (FL)
            ws.show (Panel::playlist);
            ws.activate (Panel::playlist);
            auto& pl = ui.getPlaylistForSnapshot();
            pl.handleKey (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0));
            const auto selected = ! pl.getSelectedClips().empty();
            ui.keyPressed (esc);
            check ("Playlist with clips selected: the first Esc clears the selection, the Playlist stays",
                   selected && pl.getSelectedClips().empty() && ws.isPanelVisible (Panel::playlist));
            ui.keyPressed (esc);
            check ("the next Esc closes the Playlist (FL)", ! ws.isPanelVisible (Panel::playlist));
            // the switch off: Esc leaves windows open
            workflowextras::setExtra ("general.escCloses", false);
            ws.show (Panel::mixer);
            ws.activate (Panel::mixer);
            ui.keyPressed (esc);
            check ("switch off: Esc leaves the Mixer open", ws.isPanelVisible (Panel::mixer));
            workflowextras::setExtra ("general.escCloses", true);
            check ("the switch is saved with the UI options", workflowextras::uiOptionsText().contains ("escClosesWindows=1"));

            // Settings: LYNX extras and General list the switch
            for (const auto general : { false, true })
            {
                auto page = general ? makeGeneralPage (offline, {}, false) : makeExtrasPage (offline, {}, false);
                page->setSize (680, 3600);
                juce::Component* found = nullptr;
                std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
                    if (auto* b = dynamic_cast<juce::ToggleButton*> (&c); b != nullptr && b->getButtonText() == tr ("Esc closes the focused window"))
                        found = b;
                    for (auto* child : c.getChildren())
                        walk (*child);
                };
                walk (*page);
                check (juce::String (general ? "Settings › General" : "Settings › LYNX extras") + " has \"Esc closes the focused window\", ticked",
                       found != nullptr && dynamic_cast<juce::ToggleButton*> (found)->getToggleState());
                if (found != nullptr)
                {
                    const auto r = page->getLocalArea (found, found->getLocalBounds());
                    write (*page, juce::Rectangle<int> (0, r.getY() - 40, page->getWidth(), 120), general ? "esc-general" : "esc-extras");
                }
            }

            // ================= #219: FL's snap key, in the key map =================
            const auto snapOn = [] { return EditorGlobals::get().getGridOption() != grid::noneOption; };
            ws.show (Panel::playlist);
            ws.activate (Panel::playlist);
            const auto before = snapOn();
            ui.keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey));
            check ("Backspace in the Playlist turns the main snap off / on (FL)", snapOn() != before);
            ui.keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey));
            check ("again: back", snapOn() == before);
            ui.runActionForSnapshot ("snap.toggle");
            check ("\"Snap on/off, from any window\" (for a key of one's own, e.g. Alt+S) does the same", snapOn() != before);
            ui.runActionForSnapshot ("snap.toggle");
            line ("snap keys: " + juce::String (ui.getKeyMapForSnapshot().isDefault ("playlist.snapToggle") ? "Backspace (FL)" : "changed"));
        }

        // ================= #220: the icon picker's search box, the Rename, color and icon window =================
        {
            ui::TrackIconPicker picker (5, 10, nullptr);
            picker.setSize (picker.getWidth(), picker.getHeight());
            const auto all = picker.shownCount();
            picker.setQuery ("gu");
            check ("search \"gu\": the guitars only", picker.shownCount() == 2);
            write (picker, picker.getLocalBounds(), "icons-search-gu");
            picker.setQuery ("hat");
            check ("search \"hat\": Hi-hat", picker.shownCount() == 1 && picker.shownFlIndex (0) == 239);
            check ("Enter picks the one shown", picker.pickFirstShown() && picker.pickedFlIndex() == 239);
            picker.setQuery ("");
            check ("cleared: every icon again", picker.shownCount() == all);
            write (picker, picker.getLocalBounds(), "icons-all");

            auto dialog = makeNameAndColourDialog (tr ("Rename track"), "Kick", lynxPalette()[1], 5);
            auto* icons = iconPickerOfNameDialog (*dialog);
            check ("Rename, color and icon: the icon picker is in the window", icons != nullptr && icons->pickedFlIndex() == 5);
            if (icons != nullptr)
            {
                icons->setQuery ("sn");
                icons->pickFirstShown();
                check ("a pick in the window is what OK applies (Snare)", icons->pickedFlIndex() == 4);
                icons->setQuery ("");
                icons->pick (5);
            }
            dialog->setOpaque (true);
            juce::Component frame;
            frame.setSize (dialog->getWidth(), dialog->getHeight());
            frame.setOpaque (true);
            frame.addAndMakeVisible (*dialog);
            dialog->setTopLeftPosition (0, 0);
            write (frame, frame.getLocalBounds(), "rename-color-icon");
            frame.removeChildComponent (dialog.get());
            auto patternDialog = makeNameAndColourDialog (tr ("Rename pattern"), "Pattern 1", lynxPalette()[6]);
            check ("patterns keep the name and colour window (FL: no icon)", iconPickerOfNameDialog (*patternDialog) == nullptr);
        }

        // ================= #222: track icons grow with the track =================
        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            const std::pair<const char*, int> rows[] = { { "Kick", 5 }, { "Vocals", 20 }, { "Bass", 14 }, { "FX", 93 } };
            song.playlistTracks.resize (6);
            for (int i = 0; i < 4; ++i)
            {
                song.playlistTracks[(std::size_t) i].name = rows[i].first;
                song.playlistTracks[(std::size_t) i].icon = rows[i].second;
                song.playlistTracks[(std::size_t) i].colour = lynxPalette()[(std::size_t) (1 + 2 * i)];
            }
            engine.setSong (song);
            engine.dispatchPendingMessages();
            PlaylistComponent pl (engine);
            pl.setBounds (0, 0, 900, 700);
            pl.setVisible (true);
            pl.setViewForSnapshot (160.0, 0, 0);
            const int heights[] = { 56, 92, 116, 160 };
            for (int i = 0; i < 4; ++i)
                pl.setTrackHeightsForTest ({ i }, heights[i]);
            engine.dispatchPendingMessages();
            line ("row heights: " + juce::String (pl.trackRowForSnapshot (0).getHeight()) + ", " + juce::String (pl.trackRowForSnapshot (1).getHeight()) + ", "
                  + juce::String (pl.trackRowForSnapshot (2).getHeight()) + ", " + juce::String (pl.trackRowForSnapshot (3).getHeight()));
            const auto img = pl.createComponentSnapshot (pl.getLocalBounds(), true, 1.0f);
            // the big icon: pixels of the plate's shade in the middle of a tall header, none in a short one
            const auto shadeCount = [&] (int t) {
                const auto row = pl.trackRowForSnapshot (t);
                const auto plate = img.getPixelAt (8, row.getY() + 4);
                const auto shadeCol = trackiconscale::shade (plate);
                int n = 0;
                for (int y = row.getY() + row.getHeight() / 4; y < row.getBottom() - row.getHeight() / 4; ++y)
                    for (int x = 20; x < pl.headerWidthForSnapshot() - 30; ++x)
                    {
                        const auto p = img.getPixelAt (x, y);
                        n += std::abs (p.getRed() - shadeCol.getRed()) + std::abs (p.getGreen() - shadeCol.getGreen()) + std::abs (p.getBlue() - shadeCol.getBlue()) < 12 ? 1 : 0;
                    }
                return n;
            };
            const int counts[] = { shadeCount (0), shadeCount (1), shadeCount (2), shadeCount (3) };
            line ("big icon pixels per row: " + juce::String (counts[0]) + " " + juce::String (counts[1]) + " " + juce::String (counts[2]) + " " + juce::String (counts[3]));
            check ("a 56 px row: no big icon (FL)", counts[0] < 10);
            check ("92 / 116 / 160 px rows: the big icon, larger with the row", counts[1] > 40 && counts[2] > counts[1] && counts[3] > counts[2]);
            write (pl, juce::Rectangle<int> (0, 0, pl.headerWidthForSnapshot() + 220, pl.trackRowForSnapshot (3).getBottom() + 4), "tall-track-icons");
        }

        // ================= #215: the export file name =================
        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            song.bpm = 128.0;
            engine.setSong (song);
            juce::String preview, resolved;
            const auto when = juce::Time (2026, 10, 19, 21, 35, 7, 0, true); // (19 November 2026, 21:35:07)
            auto panel = makeExportPanelWithName (engine, "MySong", "$project_$date_$bpm", when, preview, resolved);
            line ("preview: " + preview);
            check ("the name the template gives", resolved == "MySong_2026-11-19_128");
            check ("the window shows it with the extension", preview.contains ("MySong_2026-11-19_128.wav"));
            write (*panel, panel->getLocalBounds(), "export-name");
            auto stems = makeExportPanelWithName (engine, "MySong", "$project_$track", when, preview, resolved, 1);
            line ("stems preview: " + preview);
            check ("stems: $track names each stem file", preview.contains ("MySong_Kick.wav"));
            auto plain = makeExportPanelWithName (engine, "MySong", "", when, preview, resolved);
            check ("empty template: the project's name, as before", resolved == "MySong");
            auto unsafe = makeExportPanelWithName (engine, "My:Song?", "$project/$format", when, preview, resolved);
            check ("unsafe characters become \"_\"", resolved == "My_Song__WAV");
        }

        juce::File (prefix + "-checks.txt").replaceWithText (checks + "result=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

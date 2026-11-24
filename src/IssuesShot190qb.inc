    // --issuesshot190qb <prefix> (round 190qb, issue follow-ups): #206 — a middle-click on an empty Mixer insert slot and
    // on the "+ Add" of a linked Playlist track's insert effects column opens the effect search ("Find an effect") for
    // that slot, with no plugin menu first; #215 — a "/" in the export file name template puts the file in folders
    // inside the export folder. Offscreen through the real components with synthetic mouse events (run under Xvfb on
    // Linux: the search opens in a tool window). <prefix>-checks.txt lists what was checked. Included in the app class
    // of Main.cpp.
    bool runIssuesShot190qb (const juce::String& prefix)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const char* name, bool ok, const juce::String& detail = {}) {
            report.add (juce::String (name) + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        using namespace lynx;
        ui::motion::enabled() = false;
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const juce::ModifierKeys middle (juce::ModifierKeys::middleButtonModifier);
        const auto closeSearch = [] {
            if (auto* w = findToolWindow ("slotsearch"))
                delete w;
        };
        // types `text` into the search window's box and presses Enter there (the first effect that matches lands)
        const auto pickInSearch = [] (AudioEngine& engine, const juce::String& text) {
            auto* w = findToolWindow ("slotsearch");
            if (w == nullptr)
                return false;
            juce::TextEditor* box = nullptr;
            std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
                for (auto* child : c.getChildren())
                {
                    if (auto* t = dynamic_cast<juce::TextEditor*> (child); t != nullptr && box == nullptr)
                        box = t;
                    find (*child);
                }
            };
            find (*w);
            if (box == nullptr || box->getParentComponent() == nullptr)
                return false;
            box->setText (text, true);
            static_cast<juce::Component*> (box)->handleCommandMessage (0x10003001); // (JUCE posts the change; delivered now)
            engine.dispatchPendingMessages();
            box->getParentComponent()->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
            engine.dispatchPendingMessages();
            return true;
        };
        const auto hasLimiter = [] (const std::vector<EffectSlot>& fx) {
            for (const auto& slot : fx)
                if (juce::String (effectInfo (slot.kind).name).containsIgnoreCase ("limit"))
                    return true;
            return false;
        };

        // ================= #206: a middle-click on an empty Mixer slot =================
        {
            AudioEngine engine (false);
            engine.setSong (Song::makeDefault());
            engine.dispatchPendingMessages();
            EffectsPanel panel (engine);
            panel.setSize (300, 700);
            panel.setTrack (1);
            engine.dispatchPendingMessages();
            closeSearch();
            const auto at = panel.slotBoundsForCheck (2).getCentre();
            check ("206_hint_names_middle_click", panel.hintAt (at).text.contains ("middle-click"), panel.hintAt (at).text);
            const auto e = juce::MouseEvent (source, at.toFloat(), middle, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &panel, &panel, now, at.toFloat(), now, 1, false);
            panel.mouseDown (e);
            panel.mouseUp (e.withNewPosition (at.toFloat()));
            engine.dispatchPendingMessages();
            check ("206_mixer_middle_click_opens_the_search", findToolWindow ("slotsearch") != nullptr);
            check ("206_mixer_search_fills_that_slot", pickInSearch (engine, "limit") && hasLimiter (engine.getSong().mixer[1].effects));
            closeSearch();
            // a middle-click on a filled slot changes nothing here (FL's slot menu / the window, as before)
            const auto filled = engine.getSong().mixer[1].effects.size();
            check ("206_mixer_one_effect", filled == 1, juce::String ((int) filled));
        }

        // ================= #206: a middle-click on "+ Add" in a linked track's insert effects column =================
        {
            AudioEngine engine (false);
            MainComponent ui (engine, false);
            ui.setSize (1600, 900);
            {
                auto song = Song::makeDefault();
                song.playlist.clear();
                ChannelDef vox;
                vox.name = "Lead vocal";
                vox.instrument = InstrumentKind::audioClip;
                vox.samplePath = "/shots/Lead vocal.wav";
                vox.mixerTrack = 2;
                song.channels.push_back (vox);
                Clip c;
                c.kind = ClipKind::audio;
                c.track = 0;
                c.channel = (int) song.channels.size() - 1;
                c.start = 0;
                c.length = 4 * 4 * ppq;
                song.playlist.push_back (c);
                song.playlistTracks.resize (3);
                song.playlistTracks[0].height = 70;
                engine.setSong (std::move (song));
            }
            engine.dispatchPendingMessages();
            ui.selectTabForSnapshot (2);
            engine.dispatchPendingMessages();
            auto& pl = ui.getPlaylistForSnapshot();
            pl.setSize (juce::jmax (pl.getWidth(), 1000), juce::jmax (pl.getHeight(), 640));
            PlaylistComponent::answerLinkConfirmation = [] (const autolink::Report&) { return true; };
            pl.runAction ("playlist.linkTracks");
            PlaylistComponent::answerLinkConfirmation = nullptr;
            engine.dispatchPendingMessages();
            const auto wasShown = PlaylistComponent::showTrackInserts;
            if (! wasShown)
                pl.runAction ("playlist.trackInserts");
            engine.dispatchPendingMessages();
            const auto insert = engine.getSong().playlistTracks.empty() ? -1 : engine.getSong().playlistTracks[0].mixerTrack;
            const auto list = pl.insertsListForSnapshot (0);
            check ("206_linked_track_has_the_column", insert > 0 && ! list.isEmpty(), juce::String (insert));
            if (insert > 0 && ! list.isEmpty())
            {
                const auto effectsBefore = engine.getSong().mixer[(std::size_t) insert].effects.size();
                // the "+ Add" row comes after the effects (13 px rows)
                const juce::Point<int> add (list.getX() + 40, list.getY() + (int) effectsBefore * 13 + 6);
                check ("206_hint_names_double_and_middle_click", pl.hintAt (add).text.contains ("[MMB] or double-click"), pl.hintAt (add).text);
                closeSearch();
                const auto e = juce::MouseEvent (source, add.toFloat(), middle, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pl, &pl, now, add.toFloat(), now, 1, false);
                pl.mouseDown (e);
                pl.mouseUp (e.withNewPosition (add.toFloat()));
                engine.dispatchPendingMessages();
                check ("206_track_add_middle_click_opens_the_search", findToolWindow ("slotsearch") != nullptr);
                check ("206_track_search_fills_the_tracks_insert",
                       pickInSearch (engine, "limit") && engine.getSong().mixer[(std::size_t) insert].effects.size() == effectsBefore + 1
                           && hasLimiter (engine.getSong().mixer[(std::size_t) insert].effects),
                       juce::String ((int) engine.getSong().mixer[(std::size_t) insert].effects.size()));
                closeSearch();
                // a middle-click on an effect's row does not open the search
                const juce::Point<int> row0 (list.getX() + 40, list.getY() + 6);
                const auto e2 = juce::MouseEvent (source, row0.toFloat(), middle, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pl, &pl, now, row0.toFloat(), now, 1, false);
                pl.mouseDown (e2);
                pl.mouseUp (e2.withNewPosition (row0.toFloat()));
                engine.dispatchPendingMessages();
                check ("206_middle_click_on_an_effect_row_no_search", findToolWindow ("slotsearch") == nullptr);
                closeSearch();
            }
            if (! wasShown)
                pl.runAction ("playlist.trackInserts");
        }

        // ================= #215: folders in the export file name =================
        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            song.bpm = 130.0;
            engine.setSong (song);
            juce::String preview, resolved;
            const auto when = juce::Time (2026, 10, 20, 17, 20, 0, 0, true); // (20 November 2026, the issue's comment)
            auto panel = makeExportPanelWithName (engine, "LYNX SHARE", "/BOUNCE/$project_D$day-M$month-Y$year_$bpm-BPM_$samplerateHz-$bitdepth", when,
                                                  preview, resolved);
            report.add ("preview: " + preview);
            check ("215_folder_in_the_name", resolved.startsWith ("BOUNCE/LYNX SHARE_D20-M11-Y2026_130-BPM_") && resolved.endsWith ("Hz-24bit"), resolved);
            // (round 190qp: the line shows the whole path now, with the system's separator)
            check ("215_preview_shows_the_folder", preview.replaceCharacter ('\\', '/').contains ("BOUNCE/LYNX SHARE_D20-M11-Y2026_130-BPM_"), preview);
            savePng (panel->createComponentSnapshot (panel->getLocalBounds(), true, 1.0f), prefix + "-215-folder-name.png");
            savePng (panel->createComponentSnapshot (panel->getLocalBounds(), true, 1.5f), prefix + "-215-folder-name@150.png");
            auto nested = makeExportPanelWithName (engine, "AC/DC", "Bounces/$year/$project", when, preview, resolved);
            check ("215_nested_folders_and_a_slash_in_a_value", resolved == "Bounces/2026/AC_DC", resolved);
            auto dots = makeExportPanelWithName (engine, "Song", "../../$project", when, preview, resolved);
            check ("215_never_out_of_the_export_folder", resolved == "Song", resolved);
        }

        juce::File (prefix + "-checks.txt").replaceWithText (report.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

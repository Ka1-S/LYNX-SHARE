    // --simplegb190keshot <folder> (round 190ke, owner decisions D2 / D4 / D5 of coverage190jw): the Simple layout's
    // GarageBand / BandLab ideas offscreen for the reference | LYNX pairs — the top bar with the tuner's button (closed and
    // open), the tuner panel (sharp, flat, in tune, waiting), the Finish dock's "Fade out the end" and the fade's clip on
    // the Master track, the dock's Controls tab (a synth, a voice and a drum track; Edit mapping; the remap menu) and the
    // Browser's "LYNX content" with the tags filtering it. <folder>/checks.txt lists what was checked (ok / FAIL).
    // Pictures at the --shot-scale asked for (100 %, @2x). Included in the app class of Main.cpp.
    bool runSimpleGbShot190ke (const juce::File& folder)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        folder.createDirectory();
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const juce::String& name, bool ok, const juce::String& detail = {}) {
            report.add ((ok ? "ok    " : "FAIL  ") + name + (detail.isEmpty() ? juce::String() : "  (" + detail + ")"));
            std::fprintf (stderr, "simplegb190keshot: %s %s\n", ok ? "ok  " : "FAIL", name.toRawUTF8());
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

        AudioEngine offline (false);
        MainComponent ui (offline, false);
        ui.setSize (1600, 900);

        // ---- the song: a beat from LYNX content (8 bars), a synth with an echo, a voice track on input 1
        {
            Song s;
            s.mixer = Song::defaultMixer();
            s.bpm = 100.0;
            const factorycontent::Item* beat = nullptr;
            for (const auto& it : factorycontent::items())
                if (beat == nullptr && it.kind == factorycontent::Kind::beat && it.genre == "hip hop")
                    beat = &it;
            if (beat != nullptr)
                factorycontent::addBeat (s, *beat, 0, 8);
            simpletracks::Request synth;
            synth.kind = simpletracks::Kind::instrument;
            const auto& sounds = soundlib::builtIn();
            for (const auto& sd : sounds)
                if (! synth.channel && sd.name == "Supersaw")
                    synth.channel = soundlib::channelFor (sd);
            if (! synth.channel)
                synth.channel = soundlib::channelFor (sounds.front());
            synth.name = "Supersaw";
            synth.emptyBars = 8;
            const auto st = simpletracks::addTrack (s, synth);
            if (st.ok() && st.insert > 0)
                s.mixer[(std::size_t) st.insert].effects.push_back (goals::make (goals::echo));
            simpletracks::Request voice;
            voice.kind = simpletracks::Kind::guitar;
            voice.input = 0;
            simpletracks::addTrack (s, voice);
            offline.setSong (std::move (s));
        }
        offline.dispatchPendingMessages();
        ui.setLayoutStyle (layoutstyle::Style::simple);
        offline.dispatchPendingMessages();
        auto* simple = ui.getSimpleWorkflowForSnapshot();
        check ("the Simple workflow is up", simple != nullptr);
        if (simple == nullptr)
            return false;
        simple->hideChooser();
        ui.workspaceForTest().hide (workspace::Panel::channelRack); // (the new Drums track opened the rack: the timeline in view)
        auto& playlist = ui.getPlaylistForSnapshot();
        playlist.setViewForSnapshot (64.0, 0, 0);
        offline.dispatchPendingMessages();

        // ---- 1. the top bar's tuner button (GarageBand's Tuner button in its control bar)
        auto* tunerButton = dynamic_cast<juce::Button*> (simple->tunerComponent());
        check ("the tuner's button sits in the Simple top bar", tunerButton != nullptr && tunerButton->isVisible() && tunerButton->getParentComponent() == &ui,
               tunerButton != nullptr ? tunerButton->getBounds().toString() : juce::String());
        const auto barArea = juce::Rectangle<int> (0, 0, ui.getWidth(), 64);
        writeImage (shot::snapshot (ui, barArea), "topbar");
        if (tunerButton != nullptr)
        {
            simpletunerui::setOpen (*tunerButton, true);
            writeImage (shot::snapshot (ui, tunerButton->getBounds().expanded (60, 10).getIntersection (ui.getLocalBounds())), "topbar-tuner-open");
            simpletunerui::setOpen (*tunerButton, false);
        }

        // ---- 2. the tuner panel: GarageBand's display (Keynote, needle, cents, reference)
        {
            struct Case { const char* name; int note; double cents, hz; const char* source; };
            const Case cases[] = { { "tuner-sharp", 63, 20.0, 440.0, "Guitar  \xc2\xb7  Input 1" }, { "tuner-flat", 45, -12.0, 440.0, "Guitar  \xc2\xb7  Input 1" },
                                   { "tuner-intune", 64, 1.0, 440.0, "Guitar  \xc2\xb7  Input 1" }, { "tuner-ref442", 57, -3.0, 442.0, "Voice  \xc2\xb7  Input 1" },
                                   { "tuner-waiting", -1, 0.0, 440.0, "Input 1" } };
            for (const auto& c : cases)
            {
                auto panel = simpletunerui::makePanelForSnapshot (offline, c.note, c.cents, c.hz, juce::String::fromUTF8 (c.source));
                writeImage (shot::paint (*panel, theme::colour::bg1), c.name);
            }
            auto panel = simpletunerui::makePanel (offline, [] { return 2; });
            panel->setSize (simpletunerui::panelWidth, simpletunerui::panelHeight);
            const auto a2 = simpletunerui::feedForCheck (*panel, 110.0, 1.0);
            check ("the tuner reads a guitar's A string (110 Hz) as A2, 0 cents", a2.valid && a2.note == 45 && std::abs (a2.cents) < 2.0,
                   juce::String (a2.note) + " " + juce::String (a2.cents, 2));
            const auto sharpE = simpletunerui::feedForCheck (*panel, 329.63 * std::pow (2.0, 20.0 / 1200.0), 1.0);
            check ("an E4 20 cents sharp reads E4 +20", sharpE.valid && sharpE.note == 64 && std::abs (sharpE.cents - 20.0) < 2.0,
                   juce::String (sharpE.note) + " " + juce::String (sharpE.cents, 2));
        }

        // ---- 3. the song fade-out (GarageBand's Mix › Fade Out): the Finish dock's switch, the clip on the Master track
        simple->showFinish();
        offline.dispatchPendingMessages();
        if (auto* finish = dynamic_cast<FinishDock*> (simple->finishDockForSnapshot()))
        {
            finish->setFadeForCheck (true, 10.0);
            offline.dispatchPendingMessages();
            const auto& song = offline.getSong();
            const auto clip = songfade::findClip (song);
            check ("the fade's clip is on a track of its own, under the Guitar track", clip >= 0 && song.playlist[(std::size_t) clip].track == 3,
                   clip >= 0 ? juce::String (song.playlist[(std::size_t) clip].track) : juce::String());
            check ("Fade out the end: one automation clip on the Master's volume, ending with the music", clip >= 0
                       && song.playlist[(std::size_t) clip].end() == songfade::musicEnd (song)
                       && song.automations[(std::size_t) song.playlist[(std::size_t) clip].automation].target.kind == AutomationTargetKind::mixerVolume);
            check ("the fade lasts 10 s", std::abs (songfade::seconds (song) - 10.0) < 0.01);
            playlist.setViewForSnapshot (40.0, 0, 0);
            offline.dispatchPendingMessages();
            writeImage (shot::snapshot (ui), "fade-window");
            writeImage (shot::paint (*finish, theme::colour::bg2), "fade-finish-dock");
            writeImage (shot::snapshot (playlist), "fade-playlist");
            finish->setFadeForCheck (true, 5.0);
            check ("a 5 s fade reuses the same clip", songfade::findClip (offline.getSong()) == clip && std::abs (songfade::seconds (offline.getSong()) - 5.0) < 0.01);
            finish->setFadeForCheck (true, 10.0);
        }
        else
            check ("the Finish dock opens", false);
        simple->hideFinish();
        offline.dispatchPendingMessages();

        // ---- 4. the Controls tab (GarageBand's Smart Controls): a synth, a guitar, the drums
        playlist.setViewForSnapshot (64.0, 0, 0);
        if (auto* dock = simple->dockForSnapshot())
        {
            const auto& song = offline.getSong();
            int synthTrack = -1, guitarTrack = -1, drumsTrack = -1;
            for (int t = 0; t < 8; ++t)
                if (const auto k = simpletracks::kindOf (song, t))
                {
                    if (*k == simpletracks::Kind::instrument && synthTrack < 0)
                        synthTrack = t;
                    if ((*k == simpletracks::Kind::audio || *k == simpletracks::Kind::guitar || *k == simpletracks::Kind::voice) && guitarTrack < 0)
                        guitarTrack = t;
                    if (*k == simpletracks::Kind::drums && drumsTrack < 0)
                        drumsTrack = t;
                }
            const auto shootDock = [&] (int track, const juce::String& name) {
                playlist.selectTrack (track);
                simple->trackSelected (track);
                dock->showTrack (track);
                dock->setTab (SimpleDock::Tab::controls);
                offline.dispatchPendingMessages();
                writeImage (shot::paint (*dock, theme::colour::bg1), name);
            };
            shootDock (synthTrack, "controls-synth");
            check ("the Controls tab shows on a synth track", dock->hasControlsTabForCheck() && dock->getTab() == SimpleDock::Tab::controls);
            check ("its knobs: Cutoff, Resonance, Attack, Release, the echo's knob, Reverb, Pan, Volume",
                   dock->controlsNameForCheck (0) == tr ("Cutoff") && dock->controlsNameForCheck (3) == tr ("Release")
                       && dock->controlsNameForCheck (5) == tr ("Reverb") && dock->controlsNameForCheck (7) == tr ("Volume"),
                   dock->controlsNameForCheck (0) + " / " + dock->controlsNameForCheck (4) + " / " + dock->controlsNameForCheck (7));
            const auto channel = detailchain::forTrack (song, synthTrack).channel;
            dock->controlsTurnForCheck (0, 0.3f);
            offline.dispatchPendingMessages();
            check ("turning the Cutoff knob moves the synth's own cutoff",
                   channel >= 0 && std::abs (synthparam::get (offline.getSong().channels[(std::size_t) channel].synth, synthparam::cutoff) - 0.3f) < 1.0e-3f);
            writeImage (shot::snapshot (ui), "controls-window");
            dock->controlsEditingForSnapshot (true);
            writeImage (shot::paint (*dock, theme::colour::bg1), "controls-edit");
            saveMenus (juce::StringArray { "Knob 8" }, std::vector<juce::PopupMenu> { dock->controlsMenuForSnapshot (7) }, folder.getChildFile ("controls-menu.png").getFullPathName());
            dock->controlsEditingForSnapshot (false);
            shootDock (guitarTrack, "controls-guitar");
            check ("a guitar track: the chain and the track's controls", dock->hasControlsTabForCheck() && dock->controlsNameForCheck (0) == tr ("Reverb"),
                   dock->controlsNameForCheck (0));
            shootDock (drumsTrack, "controls-drums");
        }
        else
            check ("the dock opens", false);

        // ---- 5. LYNX content in the Browser, with its tags (GarageBand's Loop Browser keywords)
        {
            auto& browser = ui.getBrowserForSnapshot();
            BrowserComponent::tagStore() = {};
            browser.setSize (300, 860);
            browser.setTab (0);
            browser.showFactoryContentForSnapshot ({ "lynx-content", "lynx-content/Beats", "lynx-content/Beats/Trap", "lynx-content/Sounds" });
            const auto rows = browser.rowLabelsForCheck();
            check ("the Simple layout's Browser lists LYNX content", rows.contains (tr ("LYNX content")) && rows.contains (tr ("Beats")));
            writeImage (shot::paint (browser, theme::colour::bg2), "browser-content");
            browser.setTagsPanelShown (true);
            browser.setPickedTags ({ "Trap" });
            writeImage (shot::paint (browser, theme::colour::bg2), "browser-tags-trap");
            const auto results = browser.resultLabelsForCheck();
            check ("the tag Trap lists LYNX's trap beats and sounds", results.size() > 0 && results.joinIntoString ("|").contains ("Trap beat"),
                   juce::String (results.size()) + " rows");
            browser.setTagMatch (browsertags::Match::all);
            browser.setPickedTags ({ "Dark", "Pad" });
            writeImage (shot::paint (browser, theme::colour::bg2), "browser-tags-dark-pad");
            browser.setPickedTags ({});
            browser.setTagsPanelShown (false);
            // the Pattern (FL) layout keeps its Browser as it was
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.dispatchPendingMessages();
            check ("the Pattern layout's Browser has no LYNX content", ! browser.rowLabelsForCheck().contains (tr ("LYNX content")));
            check ("the tuner's button is not in the Pattern layout's toolbar", tunerButton == nullptr || ! tunerButton->isShowing());
        }

        folder.getChildFile ("checks.txt").replaceWithText (report.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        return allOk;
    }

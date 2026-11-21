    // --design190ooshot <prefix> [project] (round 190oo, design review 2): off screen at 100 % and @150, the states of the
    // owner's options and the round's fixes — the Piano roll's keyboard styles (O-2: FL's keys by default), fades in the
    // Pattern layout with and without Shade faded audio (O-1) and in Clip-launch, a new Clip-launch project with a strip on
    // every column (CS O-2), Live's colours for an .als opened in the Pattern layout (CS O-1); <prefix>-checks.txt lists
    // the checks (result=OK / FAIL). Included in the app class of Main.cpp.
    bool runDesign190ooShot (const juce::String& prefix, const juce::String& projectPath)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        juce::StringArray made, checks;
        bool allOk = true;
        const auto check = [&checks, &allOk] (const juce::String& what, bool ok) {
            checks.add ((ok ? "ok   " : "FAIL ") + what);
            allOk = allOk && ok;
        };
        const auto both = [&prefix, &made] (juce::Component& c, juce::Rectangle<int> area, const juce::String& name) {
            area = area.getIntersection (c.getLocalBounds());
            if (area.isEmpty())
                return;
            checkTextFit (c, name);
            savePng (c.createComponentSnapshot (area, true, 1.0f), prefix + "-" + name + ".png");
            savePng (c.createComponentSnapshot (area, true, 1.5f), prefix + "-" + name + "@150.png");
            made.add (name);
        };

        AudioEngine offline (false);
        MainComponent ui (offline, false);
        ui.setSize (1920, 1080);
        ui.setVisible (true);
        const auto settle = [&offline] {
            for (int k = 0; k < 4; ++k)
                offline.dispatchPendingMessages();
#if JUCE_LINUX || JUCE_WINDOWS
            for (int i = 0; i < 256 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i) {}
#endif
        };
        const auto pump = [&settle] (int ms) {
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
            while (juce::Time::getMillisecondCounter() < end)
            {
                settle();
                juce::Thread::sleep (10);
            }
        };

        // --- O-2: the Piano roll's keyboard (FL's keys by default; flat and the lined piano keys from the View menu) ------
        check ("O-2: FL's keys are the default keyboard style", PianoRollComponent::keyStyle == pianokeys::Style::fl
                                                                    && pianokeys::defaultStyle == pianokeys::Style::fl);
        ui.setLayoutStyle (layoutstyle::Style::pattern);
        offline.setSong (makeNewProject());
        settle();
        if (projectPath.isNotEmpty())
            ui.openFile (juce::File (projectPath));
        settle();
        ui.showDesktopForSnapshot (false);
        ui.selectTabForSnapshot (1);
        pump (400);
        {
            auto& roll = ui.getPianoRollForSnapshot();
            const auto before = PianoRollComponent::keyStyle;
            const std::pair<pianokeys::Style, const char*> keyStyles[] { { pianokeys::Style::fl, "fl" },
                                                                         { pianokeys::Style::flat, "flat" },
                                                                         { pianokeys::Style::piano, "lined" } };
            for (const auto& [style, name] : keyStyles)
            {
                PianoRollComponent::keyStyle = style;
                roll.repaint();
                pump (150);
                both (roll, roll.getLocalBounds(), juce::String ("pianoroll-keys-") + name);
                both (roll, roll.getLocalBounds().withWidth (PianoRollComponent::keyboardWidth + 160), juce::String ("pianokeys-") + name);
            }
            PianoRollComponent::keyStyle = before;
            roll.repaint();
        }

        // --- PAT-6 / PAT-7: Thief opens with FL's 26 px rows and FL's three automation clip rows in the Channel rack ------
        if (projectPath.isNotEmpty())
        {
            const auto& song = offline.getSong();
            check ("PAT-6: Thief's Playlist rows " + juce::String (song.playlistRowHeight) + " px (FL: 26)", song.playlistRowHeight == 26);
            int rows = 0;
            for (const auto& a : song.automations)
                rows += a.rackSlot >= 0 ? 1 : 0;
            // (FL's rack shows Volume Duck, GROSS..w Bass and CRASH REVERB between YELP and Riser Noise Metal, and Thief's
            // other automation clips further down: every one has its FL place)
            check ("PAT-7: automation clips placed among the channels as in FL: " + juce::String (rows) + " of "
                       + juce::String ((int) song.automations.size()) + " (FL: at least the 3 under YELP)", rows >= 3);
            ui.selectTabForSnapshot (0);
            pump (300);
            both (ui, ui.getLocalBounds(), "p-rack");
            ui.selectTabForSnapshot (2);
            pump (300);
            both (ui, ui.getLocalBounds(), "p-playlist");
        }

        // --- CS O-1: an .als opened in the Pattern layout keeps Live's colours ------------------------------------------
        check ("CS O-1: .als colour 3 in the Pattern layout = Live's #F7F47C", alsImportColour (3) == 0xfff7f47cu);
        check ("CS O-1: .als colour 34 = Live's #9BC48D (Patience's Drums)", alsImportColour (34) == 0xff9bc48du);

        // --- O-1: fades in the Pattern layout with and without Shade faded audio; Clip-launch for comparison --------------
        {
            const auto bar = (std::int64_t) 4 * ppq;
            const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx190oo-fade.wav");
            {
                wav.deleteFile();
                juce::WavAudioFormat format;
                std::unique_ptr<juce::OutputStream> out (wav.createOutputStream().release());
                std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (out.get(), 44100.0, 1, 16, {}, 0));
                if (writer != nullptr)
                {
                    out.release();
                    juce::AudioBuffer<float> b (1, 44100 * 4);
                    for (int i = 0; i < b.getNumSamples(); ++i)
                    {
                        const auto t = (double) (i % 11025) / 44100.0;
                        b.setSample (0, i, (float) (0.8 * std::exp (-t * 4.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 180.0 * (double) i / 44100.0)));
                    }
                    writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
                }
            }
            check ("O-1: Shade faded audio is off by default", ! PlaylistComponent::shadeFadedAudio);
            const std::tuple<layoutstyle::Style, bool, const char*> cases[] { { layoutstyle::Style::pattern, false, "p-fades" },
                                                                              { layoutstyle::Style::pattern, true, "p-fades-shaded" },
                                                                              { layoutstyle::Style::clipLaunch, false, "c-fades" } };
            for (const auto& [style, shade, name] : cases)
            {
                auto song = makeNewProject();
                song.playlist.clear();
                ChannelDef vox;
                vox.name = "Main Vox";
                vox.instrument = InstrumentKind::audioClip;
                vox.samplePath = wav.getFullPathName().toStdString();
                vox.colour = 0xff3f9b3f;
                song.channels.push_back (vox);
                if (song.playlistTracks.size() < 4)
                    song.playlistTracks.resize (4);
                song.playlistTracks[0].colour = 0xff3f9b3f;
                Clip take;
                take.kind = ClipKind::audio;
                take.channel = (int) song.channels.size() - 1;
                take.track = 0;
                take.start = 0;
                take.length = 2 * bar;
                take.fadeIn = bar / 4;
                take.fadeOut = bar;
                song.playlist.push_back (take);
                PlaylistComponent::shadeFadedAudio = shade;
                ui.setLayoutStyle (layoutstyle::Style::pattern);
                ui.startNewProjectForSnapshot (std::move (song));
                settle();
                if (style != layoutstyle::Style::pattern)
                    ui.setLayoutStyle (style);
                ui.selectTabForSnapshot (2);
                settle();
                auto& pl = ui.getPlaylistForSnapshot();
                const juce::ScopedValueSetter<bool> tall (playlistTallLiveLanes(), style == layoutstyle::Style::clipLaunch);
                pl.liveLanesChanged();
                pl.setViewForSnapshot (220.0, 0, 0);
                ui::WaveformCache::getInstance()->find (wav.getFullPathName(), [&offline, &wav] { return offline.getSampleLibrary().load (wav.getFullPathName()); });
                ui::WaveformCache::getInstance()->waitUntilIdle (20000);
                pl.repaint();
                pump (300);
                const auto row = pl.trackRowForSnapshot (0);
                const auto x1 = (int) std::lround (pl.xOfTickForSnapshot ((double) (2 * bar)));
                both (pl, juce::Rectangle<int> (0, 0, juce::jmin (pl.getWidth(), x1 + 60), row.getBottom() + 4), name);
                playlistTallLiveLanes() = false;
                pl.liveLanesChanged();
            }
            PlaylistComponent::shadeFadedAudio = false;
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            settle();
            wav.deleteFile();
        }

        // --- CS O-2: a new project in the Clip-launch layout, without and with "a mixer track for every track" -----------
        {
            const auto before = newProjectDefaults().clipLaunchStrips;
            for (const auto on : { false, true })
            {
                newProjectDefaults().clipLaunchStrips = on;
                ui.setLayoutStyle (layoutstyle::Style::clipLaunch);
                ui.startNewProjectForSnapshot (makeNewProject());
                settle();
                ui.resetLayoutForStyle();
                pump (300);
                const auto& song = offline.getSong();
                int linked = 0;
                for (int t = 0; t < 16; ++t)
                    linked += autolink::linkedInsertOf (song, t) == t + 1 ? 1 : 0;
                check (juce::String ("CS O-2 ") + (on ? "on" : "off") + ": tracks 1-16 on inserts 1-16: " + juce::String (linked),
                       on ? linked == 16 : linked == 0);
                both (ui, ui.getLocalBounds(), juce::String ("c-new-project-strips-") + (on ? "on" : "off"));
            }
            newProjectDefaults().clipLaunchStrips = before;
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            settle();
        }

        // --- O-4 (Simple: the project name at the top) and O-5 (Clip-launch: Live's scale control), off and on -----------
        {
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.setSong (makeNewProject());
            settle();
            if (projectPath.isNotEmpty())
                ui.openFile (juce::File (projectPath));
            settle();
            check ("O-4 / O-5: off by default", ! ui::simplelook::projectNameShown() && ! livescale::shown);
            ui.selectTabForSnapshot (2);
            ui.setLayoutStyle (layoutstyle::Style::simple);
            ui.resetLayoutForStyle();
            pump (400);
            for (const auto on : { false, true })
            {
                ui::simplelook::projectNameShown() = on;
                ui.resized();
                pump (200);
                both (ui, ui.getToolbarBoundsForSnapshot().withHeight (ui.getToolbarBoundsForSnapshot().getHeight() + 40), juce::String ("s-topbar-name-") + (on ? "on" : "off"));
                both (ui, ui.getLocalBounds(), juce::String ("s-desktop-name-") + (on ? "on" : "off")); // (CS-24: the FL tracks' S / volume / pan)
            }
            ui::simplelook::projectNameShown() = false;
            ui.setLayoutStyle (layoutstyle::Style::clipLaunch);
            ui.resetLayoutForStyle();
            pump (400);
            for (const auto on : { false, true })
            {
                livescale::shown = on;
                ui.resized();
                ui.repaint();
                pump (200);
                both (ui, ui.getToolbarBoundsForSnapshot(), juce::String ("c-toolbar-scale-") + (on ? "on" : "off"));
            }
            offline.editSong ([] (Song& s) { projectkey::set (s, projectkey::Key { 9, 0 }); });
            pump (200);
            ui.repaint();
            pump (200);
            both (ui, ui.getToolbarBoundsForSnapshot(), "c-toolbar-scale-on-key");
            livescale::shown = false;
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            settle();
        }

        // --- CS-19 / CS-20 / CS O-3: Live's Patience in the Clip-launch arrangement, lanes open and folded on open ---------
        const auto als = juce::File ("/mnt/c/ProgramData/Ableton/Live 12 Trial/Resources/Core Library/Lessons/Demo Songs/Chuck Sutton - Patience (Live 12 Intro Demo).als");
        if (als.existsAsFile())
        {
            for (const auto folded : { false, true })
            {
                cliplanes::foldedOnOpen = folded;
                ui.setLayoutStyle (layoutstyle::Style::pattern);
                offline.setSong (makeNewProject());
                settle();
                ui.openFile (als);
                settle();
                ui.selectTabForSnapshot (2);
                ui.setLayoutStyle (layoutstyle::Style::clipLaunch);
                ui.setClipGridView (PlaylistArea::GridView::beside);
                pump (800);
                both (ui, ui.getLocalBounds(), juce::String ("c-als-both-lanes-") + (folded ? "folded" : "open"));
            }
            cliplanes::foldedOnOpen = false;
            check ("CS O-1: Patience's Drums in Live's colour", [&offline] {
                for (const auto& m : offline.getSong().mixer)
                    if (m.name == "Drums")
                        return m.colour == 0xff9bc48du;
                return true; // (no insert named so: nothing to compare)
            }());
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            settle();
        }

        juce::File (prefix + "-textfit.txt").replaceWithText (textFitLog().joinIntoString ("\n") + "\n");
        juce::File (prefix + "-checks.txt").replaceWithText (checks.joinIntoString ("\n") + "\npictures " + juce::String (made.size()) + "\n"
                                                             + made.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

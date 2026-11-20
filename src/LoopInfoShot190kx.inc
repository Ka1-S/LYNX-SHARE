    // --loopinfo190kxshot <folder> (round 190kx, loop analysis): LYNX's own synthetic loops (tests/LoopCorpus190kx.h)
    // written as WAVs with names that say nothing of their tempo or key, then drawn offscreen:
    //  - the Simple layout's Browser rows with the bars, tempo and key found (BandLab's loop rows);
    //  - the Pattern layout's Browser on the same folder (FL's rows: nothing added);
    //  - the Sample browser's Loop Browser with its Tempo / Key columns filled by the analysis;
    //  - an audio clip's menu with the LYNX extras (Follow the project key, Loop key with "Detect from the audio", Fit to
    //    the project tempo).
    // <folder>/checks.txt lists what was checked, with the paint times. Included in the app class of Main.cpp.
    bool runLoopInfoShot190kx (const juce::File& folder)
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
        // the user's own loops: names without a tempo or a key
        const auto library = folder.getChildFile ("library").getChildFile ("My Loops");
        library.createDirectory();
        struct Made
        {
            const char* name;
            loopcorpus::Item item;
        };
        const Made made[] { { "Groove 1", loopcorpus::drums ("house", 124.0, 4, 11) },
                            { "Boom bap idea", loopcorpus::drums ("boombap", 90.0, 2, 12) },
                            { "Late night keys", loopcorpus::tonal ("chords", 2, true, 96.0, 4, 13) },
                            { "Bass idea", loopcorpus::tonal ("bass", 9, true, 120.0, 4, 14) },
                            { "Synth arp", loopcorpus::tonal ("arp", 4, true, 128.0, 4, 15) },
                            { "Band jam", loopcorpus::mixed ("rock", 7, false, 110.0, 4, 16) },
                            { "Kick hit", loopcorpus::oneShot (false, 0, 17) } };
        juce::WavAudioFormat wav;
        for (const auto& m : made)
        {
            const auto f = library.getChildFile (juce::String (m.name) + ".wav");
            f.deleteFile();
            const auto& a = *m.item.audio;
            juce::AudioBuffer<float> buffer (a.isStereo() ? 2 : 1, (int) a.frames());
            buffer.copyFrom (0, 0, a.leftData(), (int) a.frames());
            if (a.isStereo())
                buffer.copyFrom (1, 0, a.rightData(), (int) a.frames());
            if (auto out = std::unique_ptr<juce::OutputStream> (f.createOutputStream()))
                if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (
                        wav.createWriterFor (out.get(), a.sampleRate, (unsigned int) buffer.getNumChannels(), 16, {}, 0)))
                {
                    out.release();
                    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
                }
        }
        PluginHost::settingsFolder().createDirectory();
        PluginHost::settingsFolder().getChildFile ("browser-folders.txt").replaceWithText (library.getParentDirectory().getFullPathName());
        BrowserComponent::readPrefs = false;
        auto savePngOf = [&] (juce::Component& c, const juce::String& name) {
            const auto image = shot::paint (c, theme::colour::bg2);
            const auto f = shot::file (folder.getChildFile (name));
            f.deleteFile();
            juce::PNGImageFormat png;
            bool ok = false;
            if (auto out = f.createOutputStream())
                ok = png.writeImageToStream (image, *out);
            check (name, ok && image.getWidth() > 0);
        };
        const auto paintMs = [] (juce::Component& c) {
            juce::Image image (juce::Image::ARGB, c.getWidth(), c.getHeight(), true);
            double best = 1.0e9;
            for (int round = 0; round < 3; ++round)
            {
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                for (int k = 0; k < 40; ++k)
                {
                    juce::Graphics g (image);
                    c.paintEntireComponent (g, false);
                }
                best = juce::jmin (best, (juce::Time::getMillisecondCounterHiRes() - t0) / 40.0);
            }
            return best;
        };
        const auto before = layoutstyle::current();
        AudioEngine engine (false);
        engine.editSong ([] (Song& s) { s.bpm = 120.0; });

        // 1. the Pattern layout (FL's Browser): the folder's rows as before, nothing analysed
        {
            layoutstyle::setCurrent (layoutstyle::Style::pattern);
            BrowserComponent browser (engine, BrowserComponent::Role::main);
            browser.setSize (360, 520);
            check ("pattern_located", browser.locateFile (library.getChildFile ("Groove 1.wav")));
            browser.waitLoopInfoForSnapshot (1000);
            check ("pattern_rows_unchanged", browser.loopRowTextsForCheck().isEmpty(), browser.loopRowTextsForCheck().joinIntoString ("|"));
            savePngOf (browser, "kx-02-pattern-rows-unchanged.png");
            check ("pattern_paint_ms", true, juce::String (paintMs (browser), 3));
        }

        // 2. the Simple layout: the rows show the bars, tempo and key found (BandLab's loop rows)
        {
            layoutstyle::setCurrent (layoutstyle::Style::simple);
            BrowserComponent browser (engine, BrowserComponent::Role::main);
            browser.setSize (360, 640);
            check ("simple_located", browser.locateFile (library.getChildFile ("Groove 1.wav")));
            // (the rows before the answers: the same paint as before this round; then with the texts, the cost they add)
            const auto bareMs = paintMs (browser);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            check ("simple_analysed", browser.waitLoopInfoForSnapshot (60000));
            const auto analysedMs = juce::Time::getMillisecondCounterHiRes() - t0;
            const auto texts = browser.loopRowTextsForCheck();
            report.add ("simple_rows " + texts.joinIntoString (" | "));
            check ("simple_analysis_ms", true, juce::String (analysedMs, 0) + " ms for " + juce::String ((int) std::size (made)) + " files");
            const auto has = [&texts] (const juce::String& row) { return texts.contains (row); };
            check ("groove_124", has (juce::String::fromUTF8 ("Groove 1: 4 bars \xc2\xb7 124 BPM")));
            check ("boombap_90", has (juce::String::fromUTF8 ("Boom bap idea: 2 bars \xc2\xb7 90 BPM")));
            check ("keys_d_minor", texts.joinIntoString ("|").contains (juce::String ("Late night keys: ")) && texts.joinIntoString ("|").contains ("D minor"));
            check ("kick_hit_nothing", ! texts.joinIntoString ("|").contains ("Kick hit"));
            savePngOf (browser, "kx-01-simple-rows.png");
            check ("simple_paint_ms", true, "rows without texts " + juce::String (bareMs, 3) + " with texts " + juce::String (paintMs (browser), 3));
            // idle: the worker waits on its condition (no polling): the process' CPU over 2 s with the Browser shown
            {
                const auto c0 = std::clock();
                juce::Thread::sleep (2000);
                const auto cpuMs = 1000.0 * (double) (std::clock() - c0) / CLOCKS_PER_SEC;
                check ("idle_cpu_ms_in_2s", cpuMs < 100.0, juce::String (cpuMs, 1) + " ms (worker queued " + juce::String ((int) browser.loopInfoQueuedForCheck()) + ")");
            }
            // the switch of the drop (on by default, as BandLab)
            check ("drop_detects_default_on", loopanalysis::dropDetects().load());
        }

        // 3. the Sample browser's Loop Browser: Tempo and Key columns filled where the names say nothing
        {
            layoutstyle::setCurrent (layoutstyle::Style::pattern);
            BrowserComponent browser (engine, BrowserComponent::Role::samples);
            browser.setSize (340, 620);
            browser.setLoopsShown (true);
            browser.loadLoopsForSnapshot();
            browser.waitLoopInfoForSnapshot (60000);
            const auto rows = browser.resultLabelsForCheck();
            report.add ("loop_browser_rows " + rows.joinIntoString (" | "));
            savePngOf (browser, "kx-03-loop-browser-columns.png");
            // Tempo 110 - 130 now finds the user's loops by their detected tempo
            browser.setLoopTempoChoice (4);
            check ("tempo_filter_finds_detected", browser.numLoopsShownForSnapshot() >= 3, juce::String (browser.numLoopsShownForSnapshot()));
            savePngOf (browser, "kx-04-loop-browser-tempo-110-130.png");
            browser.setLoopTempoChoice (0);
            browser.setLoopsShown (false);
        }

        // 4. an audio clip's menu: LYNX extras with the key items and the one-click fit (every layout)
        {
            layoutstyle::setCurrent (layoutstyle::Style::pattern);
            auto song = Song::makeDefault();
            song.bpm = 120.0;
            ChannelDef keys;
            keys.name = "Late night keys";
            keys.instrument = InstrumentKind::audioClip;
            keys.samplePath = library.getChildFile ("Late night keys.wav").getFullPathName().toStdString();
            song.channels.push_back (keys);
            Clip c;
            c.kind = ClipKind::audio;
            c.channel = (int) song.channels.size() - 1;
            c.track = 2;
            c.start = 0;
            c.length = 16 * ppq;
            c.followKey = true; // (the menu then says how far it moved)
            song.playlist.push_back (c);
            projectkey::set (song, keyfollow::projectKeyFor ({ 4, true })); // E minor
            keyfollow::setLoopKey (song, c.channel, keyfollow::LoopKey { 2, true }, 2); // D minor, detected
            engine.setSong (std::move (song));
            engine.dispatchPendingMessages();
            PlaylistComponent playlist (engine);
            playlist.setSize (900, 500);
            const auto index = (int) engine.getSong().playlist.size() - 1;
            const auto menu = playlist.getClipMenuForSnapshot (index);
            bool follow = false, fit = false, loopKey = false;
            for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            {
                const auto& t = it.getItem().text;
                follow = follow || t.startsWith (tr ("Follow the project key"));
                fit = fit || t.startsWith (tr ("Fit to the project tempo ({0}, from {1} BPM)").upToFirstOccurrenceOf ("(", false, false));
                loopKey = loopKey || t.startsWith (tr ("Loop key"));
            }
            check ("menu_follow_key", follow);
            check ("menu_loop_key", loopKey);
            check ("menu_fit_project_tempo", fit);
            savePng (columnMenuImage (menu), folder.getChildFile ("kx-05-clip-menu-lynx-extras.png").getFullPathName());
        }
        layoutstyle::setCurrent (before);
        folder.getChildFile ("checks.txt").replaceWithText (report.joinIntoString ("\n") + "\n");
        return allOk;
    }

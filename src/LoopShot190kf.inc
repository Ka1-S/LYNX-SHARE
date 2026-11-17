    // --loopshot190kf <folder> (round gaps190kf, item 3): the Sample browser window's Loop Browser drawn offscreen for the
    // GarageBand | LYNX pairs — the Instrument keyword buttons, picks narrowing the list (dimmed buttons), the Genre and
    // Descriptors grids, Scale, Loop Types and the Favorites button, and the Scale / Loop Types menus. A small library of
    // loop-named files is made in <folder>/library. <folder>/checks.txt lists what was checked. Included in the app
    // class of Main.cpp.
    bool runLoopShot190kf (const juce::File& folder)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        folder.createDirectory();
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const char* name, bool ok, const juce::String& detail = {}) {
            report.add (juce::String (name) + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        // a loop library as sample packs name their files (folders by instrument, tempo and key in the names)
        const auto library = folder.getChildFile ("library").getChildFile ("Loop Packs");
        const char* audioNames[] { "Drums/Trap Beat 140 bpm.wav",      "Drums/House Groove 124 bpm.wav",  "Drums/Kicks/Kick Punchy.wav",
                                   "Drums/Kicks/Kick Soft.wav",        "Drums/Hats/HiHat Loop 128.wav",   "Drums/Percussion/Conga Loop 100.wav",
                                   "Keys/Dark Piano 90 Am.wav",        "Keys/Piano Happy 120 Cmaj.wav",   "Keys/Rhodes Chill 85 Dm.wav",
                                   "Keys/Organ Gospel 96 F.wav",       "Synths/Dark Pad Loop 70 Dm.wav",  "Synths/Pluck Arp 128 Em.wav",
                                   "Synths/Lead Intense 150 Gm.wav",   "Bass/808 Trap 140 C#m.wav",       "Bass/Synth Bass Reese 174 Fm.wav",
                                   "Bass/Elec Bass Funk 110 E.wav",    "Guitars/Acoustic Guitar Strum 100 G.wav", "Guitars/Elec Guitar Distorted Riff 120 Em.wav",
                                   "Strings/Cinematic Strings 80 Cm.wav", "Vocals/Female Vocal Chop 120 Am.wav", "FX/Riser Long 128.wav",
                                   "FX/Impact Boom.wav",               "Lo-Fi/LoFi Keys Relaxed 80 Bbmaj.wav", "Lo-Fi/LoFi Drum Loop 80 bpm.wav" };
        for (const auto* name : audioNames)
        {
            const auto f = library.getChildFile (name);
            f.getParentDirectory().createDirectory();
            if (! f.existsAsFile())
                writeWaveDemo (f, 120.0);
        }
        // MIDI loops (a header and an empty track: the Loop Browser lists them by type)
        for (const auto* name : { "MIDI/Piano Chords 90 Am.mid", "MIDI/Trap Hats 140 bpm.mid", "MIDI/Bass Line 120 Em.mid" })
        {
            const auto f = library.getChildFile (name);
            f.getParentDirectory().createDirectory();
            const unsigned char midi[] { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k', 0, 0, 0, 4, 0, 0xff, 0x2f, 0 };
            f.replaceWithData (midi, sizeof (midi));
        }
        PluginHost::settingsFolder().createDirectory();
        PluginHost::settingsFolder().getChildFile ("browser-folders.txt").replaceWithText (library.getFullPathName());
        BrowserComponent::readPrefs = false;
        BrowserComponent::tagStore() = {};

        AudioEngine engine (false);
        BrowserComponent browser (engine, BrowserComponent::Role::samples);
        // a favourite (GarageBand's Favorites check box; LYNX's star) and a user tag that names a keyword
        browser.setStarred (library.getChildFile ("Keys/Dark Piano 90 Am.wav"), true);
        browser.setStarred (library.getChildFile ("Synths/Dark Pad Loop 70 Dm.wav"), true);
        BrowserComponent::tagStore().add ({ library.getChildFile ("Vocals/Female Vocal Chop 120 Am.wav").getFullPathName().toStdString() }, "Pop");
        auto save = [&] (juce::Component& c, const juce::String& name) {
            const auto image = shot::paint (c, theme::colour::bg2);
            const auto f = shot::file (folder.getChildFile (name));
            f.deleteFile();
            juce::PNGImageFormat png;
            bool ok = false;
            if (auto out = f.createOutputStream())
                ok = png.writeImageToStream (image, *out);
            check (name.toRawUTF8(), ok && image.getWidth() > 0);
        };
        const auto total = (int) (std::size (audioNames) + 3);

        // the Sample browser window's size (340 x 620, its default) and a taller one
        browser.setSize (340, 620);
        browser.setLoopsShown (true);
        browser.loadLoopsForSnapshot();
        check ("loops_listed", browser.numLoopsShownForSnapshot() == total, juce::String (browser.numLoopsShownForSnapshot()) + " of " + juce::String (total));
        save (browser, "loops-01-instrument.png");
        browser.setSize (340, 820);
        save (browser, "loops-01b-instrument-tall.png");
        browser.setSize (340, 620);

        // keyword buttons: Piano, then Piano + Dark (all of them); the dimmed ones have no loop left
        auto f = browser.loopFilter();
        f.keywords = { "piano" };
        browser.setLoopFilter (f);
        check ("piano", browser.numLoopsShownForSnapshot() == 6, juce::String (browser.numLoopsShownForSnapshot()));
        f.keywords = { "piano", "dark" };
        browser.setLoopFilter (f);
        check ("piano_and_dark", browser.numLoopsShownForSnapshot() == 1, juce::String (browser.numLoopsShownForSnapshot()));
        check ("kick_dimmed", browser.loopKeywordCounts().at ("kick") == 0);
        save (browser, "loops-02-piano-dark.png");
        f.keywords.clear();

        // Genre: Trap (the 808, the beat, the MIDI hats)
        browser.setLoopCategories (1 << (int) loopkeys::Category::genre);
        f.keywords = { "trap" };
        browser.setLoopFilter (f);
        check ("trap", browser.numLoopsShownForSnapshot() == 3, juce::String (browser.numLoopsShownForSnapshot()));
        save (browser, "loops-03-genre-trap.png");
        // a user tag that names a keyword gives it: Pop
        f.keywords = { "pop" };
        browser.setLoopFilter (f);
        check ("tag_pop", browser.numLoopsShownForSnapshot() == 1, juce::String (browser.numLoopsShownForSnapshot()));
        f.keywords.clear();

        // Descriptors with Scale: Minor
        browser.setLoopCategories (1 << (int) loopkeys::Category::descriptor);
        f.scale = loopkeys::Scale::minor;
        browser.setLoopFilter (f);
        check ("minor", browser.numLoopsShownForSnapshot() == 12, juce::String (browser.numLoopsShownForSnapshot()));
        save (browser, "loops-04-descriptors-minor.png");
        f.scale = loopkeys::Scale::neither;
        browser.setLoopFilter (f);
        check ("neither", browser.numLoopsShownForSnapshot() == 10, juce::String (browser.numLoopsShownForSnapshot()));
        f.scale = loopkeys::Scale::any;

        // Loop Types: MIDI only
        browser.setLoopCategories (1);
        f.types = loopkeys::midi;
        browser.setLoopFilter (f);
        check ("midi_only", browser.numLoopsShownForSnapshot() == 3, juce::String (browser.numLoopsShownForSnapshot()));
        save (browser, "loops-05-midi-loops.png");
        f.types = loopkeys::allTypes;

        // Tempo: 110 - 130
        browser.setLoopFilter (f);
        browser.setLoopTempoChoice (4);
        check ("tempo_110_130", browser.numLoopsShownForSnapshot() == 8, juce::String (browser.numLoopsShownForSnapshot()));
        save (browser, "loops-06-tempo-110-130.png");
        browser.setLoopTempoChoice (0);

        // Favorites
        f.favouritesOnly = true;
        browser.setLoopFilter (f);
        check ("favourites", browser.numLoopsShownForSnapshot() == 2, juce::String (browser.numLoopsShownForSnapshot()));
        save (browser, "loops-07-favorites.png");
        f.favouritesOnly = false;
        browser.setLoopFilter (f);

        // the keyword layout: Kick swapped onto All Drums' place (GarageBand: drag one button onto another)
        auto layout = browser.loopLayout();
        layout.swap (loopkeys::Category::instrument, 0, 16);
        browser.setLoopLayout (layout);
        check ("layout_swapped", browser.loopLayout().order[0][0] == "kick");
        save (browser, "loops-08-layout-swapped.png");
        browser.setLoopLayout (loopkeys::Layout::defaults());

        // rollup 0.1.30 step 6 (the owner's yes to gaps190kf's question): in the Simple layout, under Show All, LYNX's own
        // beats and sounds (round 190ke) are listed too; Loop Types Pattern gives the beats only, Genre Trap the trap ones
        {
            const auto before = layoutstyle::current();
            layoutstyle::setCurrent (layoutstyle::Style::simple);
            browser.setLoopFilter (f);
            browser.loadLoopsForSnapshot();
            int beats = 0, sounds = 0;
            for (const auto& item : factorycontent::items())
            {
                beats += item.kind == factorycontent::Kind::beat ? 1 : 0;
                sounds += item.kind == factorycontent::Kind::sound ? 1 : 0;
            }
            check ("simple_lists_lynx_content", browser.numLoopsShownForSnapshot() == total + beats + sounds,
                   juce::String (browser.numLoopsShownForSnapshot()) + " of " + juce::String (total + beats + sounds));
            auto g = f;
            g.types = loopkeys::pattern;
            browser.setLoopFilter (g);
            check ("simple_pattern_loops_are_lynx_beats", browser.numLoopsShownForSnapshot() == beats, juce::String (browser.numLoopsShownForSnapshot()));
            save (browser, "loops-10-simple-lynx-beats.png");
            g.types = loopkeys::allTypes;
            g.keywords = { "trap" };
            browser.setLoopFilter (g);
            check ("simple_trap", browser.numLoopsShownForSnapshot() > 3, juce::String (browser.numLoopsShownForSnapshot()));
            save (browser, "loops-11-simple-trap.png");
            browser.setLoopFilter (f);
            layoutstyle::setCurrent (before);
            browser.loadLoopsForSnapshot();
            check ("pattern_layout_without_lynx_content", browser.numLoopsShownForSnapshot() == total, juce::String (browser.numLoopsShownForSnapshot()));
        }

        // the menus
        saveMenus ({ "Scale", "Loop Types" }, { browser.buildLoopScaleMenu(), browser.buildLoopTypesMenu() }, folder.getChildFile ("loops-09-menus.png").getFullPathName());

        // perf: ms per full paint of the window with the Loop Browser shown and hidden (40 paints, best of 3)
        const auto paintMs = [&browser] {
            juce::Image image (juce::Image::ARGB, browser.getWidth(), browser.getHeight(), true);
            double best = 1.0e9;
            for (int round = 0; round < 3; ++round)
            {
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                for (int k = 0; k < 40; ++k)
                {
                    juce::Graphics g (image);
                    browser.paintEntireComponent (g, false);
                }
                best = juce::jmin (best, (juce::Time::getMillisecondCounterHiRes() - t0) / 40.0);
            }
            return best;
        };
        const auto shownMs = paintMs();
        browser.setLoopsShown (false);
        check ("hidden_again", ! browser.isLoopsShown());
        const auto hiddenMs = paintMs();
        check ("paint_ms", true, "shown " + juce::String (shownMs, 3) + " hidden " + juce::String (hiddenMs, 3));
        BrowserComponent::tagStore() = {};
        folder.getChildFile ("checks.txt").replaceWithText (report.joinIntoString ("\n") + "\n");
        return allOk;
    }

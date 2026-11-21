    // --design190ojshot <prefix> [project] (round 190oj, design review of every view): off screen at 100 % and @150, per
    // layout (p = Pattern, c = Clip-launch, s = Simple): the start desktop with the project, the Channel rack, Piano roll,
    // Playlist and Mixer each on the whole workspace, the Browser shown, the desktop with the extra panels, the toolbar, the
    // menu bar's menus, and an empty new project (the empty states). <prefix>-textfit.txt lists the labels and buttons whose
    // text does not fit (checkTextFit); run it with --lang <id> for the other languages. Included in the app class of
    // Main.cpp.
    bool runDesign190ojShot (const juce::String& prefix, const juce::String& projectPath)
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

        // round 190oj (CS-4/5/6): Live's marks on a colour read on every fill — Thief's dark FL colours take the light ink,
        // Live's pastels keep their own colour × 0.24
        for (const auto fill : { 0xff7d140du, 0xff202f61u, 0xff258ed0u, 0xff9bc48du, 0xffd2e498u, 0xff1b2c66u, 0xffcc9927u })
        {
            const auto ink = theme::liveMarkOn (juce::Colour (fill), 0.24f, 4.5f, theme::colour::textPrimary);
            const auto ratio = theme::contrast (ink, juce::Colour (fill));
            check ("liveMarkOn #" + juce::String::toHexString ((int) (fill & 0xffffff)).paddedLeft ('0', 6) + ": ink #"
                       + juce::String::toHexString ((int) (ink.getARGB() & 0xffffff)).paddedLeft ('0', 6) + " at " + juce::String (ratio, 2) + ":1",
                   ratio >= 4.5f || ink == theme::colour::textPrimary);
        }

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
        // the layout's own start look (as the real window after a switch, and View › Reset layout)
        const auto startLook = [&ui] (layoutstyle::Style style) {
            if (style == layoutstyle::Style::pattern)
                ui.showDesktopForSnapshot (false);
            else
                ui.resetLayoutForStyle();
        };
        const auto pump = [&settle] (int ms) {
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
            while (juce::Time::getMillisecondCounter() < end)
            {
                settle();
                juce::Thread::sleep (10);
            }
        };

        const std::pair<layoutstyle::Style, const char*> styles[] { { layoutstyle::Style::pattern, "p" },
                                                                    { layoutstyle::Style::clipLaunch, "c" },
                                                                    { layoutstyle::Style::simple, "s" } };
        for (const auto& [style, tag] : styles)
        {
            const juce::String t (tag);
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.setSong (makeNewProject());
            settle();
            if (projectPath.isNotEmpty())
                ui.openFile (juce::File (projectPath));
            settle();
            ui.waitForBrowserForSnapshot();
            pump (1500); // (the clips' waveforms load in the background)
            if (style != layoutstyle::Style::pattern)
            {
                ui.selectTabForSnapshot (2); // (the Playlist on the whole workspace, as the layout switch does in the real window)
                ui.setLayoutStyle (style);
            }
            startLook (style);
            // round 190oj: View › Reset layout keeps the layout's own panels (no Channel rack over the clip grid / tracks)
            if (style != layoutstyle::Style::pattern)
                check (t + ": the start look / Reset layout shows no Channel rack",
                       ! ui.workspaceForTest().isPanelVisible (workspace::Panel::channelRack));
            ui.setMetersForSnapshot (0.23, (std::int64_t) 412 << 20, 6);
            pump (300);
            both (ui, ui.getLocalBounds(), t + "-desktop");
            both (ui, ui.getToolbarBoundsForSnapshot(), t + "-toolbar");

            const char* tabs[] { "rack", "pianoroll", "playlist", "mixer" };
            for (int tab = 0; tab < 4; ++tab)
            {
                ui.selectTabForSnapshot (tab);
                pump (200);
                both (ui, ui.getLocalBounds(), t + "-" + tabs[tab]);
            }
            ui.showPanelsForSnapshot ({ workspace::Panel::browser });
            pump (200);
            both (ui, ui.getLocalBounds(), t + "-browser");
            ui.showDesktopForSnapshot (true);
            pump (300);
            both (ui, ui.getLocalBounds(), t + "-extra");

            {
                const auto names = ui.getMenuNamesForSnapshot();
                std::vector<juce::PopupMenu> menus;
                for (int m = 0; m < names.size(); ++m)
                    menus.push_back (ui.getMenuForSnapshot (m));
                saveMenus (names, menus, prefix + "-" + t + "-menus.png");
                const juce::ScopedValueSetter<float> scaled (shot::scaleOverride(), 1.5f);
                saveMenus (names, menus, prefix + "-" + t + "-menus@150.png");
            }

            // the empty states: a new project in this layout (File › New's path)
            ui.getPlaylistForSnapshot().setScrollTickForSnapshot (38.0 * 4 * ppq); // (as after a long song)
            ui.startNewProjectForSnapshot (makeNewProject());
            settle();
            check (t + ": File › New starts the Playlist at bar 1 (scroll " + juce::String (ui.getPlaylistForSnapshot().scrollTicksForSnapshot()) + " ticks)",
                   ui.getPlaylistForSnapshot().scrollTicksForSnapshot() == 0.0);
            if (style == layoutstyle::Style::pattern)
                check ("p: the toolbar's pattern box shows \"" + ui.patternBoxForTest().getText() + "\" in full (" + juce::String (ui.patternBoxForTest().getWidth()) + " px)",
                       ui.patternBoxForTest().getWidth() >= 90
                           && juce::GlyphArrangement::getStringWidth (ui.getLookAndFeel().getComboBoxFont (ui.patternBoxForTest()), ui.patternBoxForTest().getText())
                                  <= (float) ui.patternBoxForTest().getWidth() - 26.0f);
            startLook (style);
            pump (300);
            both (ui, ui.getLocalBounds(), t + "-empty");
            ui.selectTabForSnapshot (0);
            pump (200);
            both (ui, ui.getLocalBounds(), t + "-empty-rack");
            ui.selectTabForSnapshot (3);
            pump (200);
            both (ui, ui.getLocalBounds(), t + "-empty-mixer");
        }
        ui.setLayoutStyle (layoutstyle::Style::pattern);
        settle();

        // fades in the three layouts (issue #230's question): one audio clip in a track colour, a quarter-bar fade in and a
        // one-bar fade out, the Playlist at 220 px a bar — FL draws the curve and the fade preview, Live shades the faded part
        {
            const auto bar = (std::int64_t) 4 * ppq;
            const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx190oj-fade.wav");
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
            for (const auto& [style, tag] : styles)
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
                ui.setLayoutStyle (layoutstyle::Style::pattern);
                ui.startNewProjectForSnapshot (std::move (song));
                settle();
                if (style != layoutstyle::Style::pattern)
                    ui.setLayoutStyle (style);
                ui.selectTabForSnapshot (2);
                settle();
                auto& pl = ui.getPlaylistForSnapshot();
                // (Live's collapsed 19 px lanes draw no fades: the Clip-launch picture with View › Tall Arrangement lanes)
                const juce::ScopedValueSetter<bool> tall (playlistTallLiveLanes(), style == layoutstyle::Style::clipLaunch);
                pl.liveLanesChanged();
                pl.setViewForSnapshot (220.0, 0, 0);
                ui::WaveformCache::getInstance()->find (wav.getFullPathName(), [&offline, &wav] { return offline.getSampleLibrary().load (wav.getFullPathName()); });
                ui::WaveformCache::getInstance()->waitUntilIdle (20000);
                pl.repaint();
                pump (300);
                const auto row = pl.trackRowForSnapshot (0);
                const auto x1 = (int) std::lround (pl.xOfTickForSnapshot ((double) (2 * bar)));
                both (pl, juce::Rectangle<int> (0, 0, juce::jmin (pl.getWidth(), x1 + 60), row.getBottom() + 4), juce::String (tag) + "-fades");
                playlistTallLiveLanes() = false;
                pl.liveLanesChanged();
            }
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            settle();
            wav.deleteFile();
        }

        juce::File (prefix + "-textfit.txt").replaceWithText (textFitLog().joinIntoString ("\n") + "\n");
        juce::File (prefix + "-checks.txt").replaceWithText (checks.joinIntoString ("\n") + "\npictures " + juce::String (made.size()) + "\n"
                                                             + made.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

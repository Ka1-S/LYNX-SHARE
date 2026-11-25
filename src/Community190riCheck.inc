    // Round 190ri (0.1.32 quality, community batch): the real components, off screen. Included in the app class of Main.cpp;
    // --community190ritest <report> [shot prefix] and --selftest "community190ri".
    //   1 Mixer › Disk recording › Render armed / selected: the window (WAV, the recordings folder, FL's name, no file dialog),
    //     the rendered file on the first free Playlist track and as what the tools paint; a pattern render off the Playlist;
    //     an .mp3 placed too
    //   2 Transpose... / Transpose selected... in their menus
    //   3 the Piano roll's scale box in narrow windows (FL look and docked)
    //   4 Alt + a tool's letter with the typing keyboard on (FL's Slice on Alt+C)
    //   5 Clone names through the actions
    //   6 Make unique as sample: the worker's render, cancel, one undo step, an imported "C:\..." data folder
    //   9 F12 closes the Search palette
    juce::String community190riCheck (juce::String& log, const juce::String& shots = {})
    {
        using namespace lynx;
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        juce::String result = "OK";
        const auto check = [&] (const juce::String& what, bool ok, const juce::String& detail = {}) {
            log << (ok ? "ok   " : "FAIL ") << what << (detail.isNotEmpty() ? "  [" + detail + "]" : juce::String()) << "\n";
            if (! ok && result == "OK")
                result = "ERROR " + what + " " + detail;
        };
        const auto styleWas = layoutstyle::current();
        const juce::ScopeGuard styleBack { [styleWas] { layoutstyle::setCurrent (styleWas); } };
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        const auto folder = juce::File::createTempFile ("community190ri");
        folder.createDirectory();
        const juce::ScopeGuard folderGone { [folder] { folder.deleteRecursively(); } };
        const auto savedWav = [&folder] (const juce::String& name, double seconds) {
            const auto f = folder.getChildFile (name);
            f.getParentDirectory().createDirectory();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.get(), 44100.0, 2, 16, {}, 0));
            stream.release();
            juce::AudioBuffer<float> b (2, (int) (seconds * 44100.0));
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (c, i, 0.4f * std::sin ((float) i * (c == 0 ? 0.05f : 0.06f)));
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            return f;
        };

        // the menu's items by text (sub-menus walked)
        std::function<const juce::PopupMenu::Item* (const juce::PopupMenu&, const juce::String&, std::deque<juce::PopupMenu::Item>&)> find;
        find = [&find] (const juce::PopupMenu& menu, const juce::String& text, std::deque<juce::PopupMenu::Item>& keep) -> const juce::PopupMenu::Item* {
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            {
                const auto& item = it.getItem();
                if (item.text.upToFirstOccurrenceOf ("\t", false, false) == text)
                {
                    keep.push_back (item);
                    return &keep.back();
                }
                if (item.subMenu != nullptr)
                    if (const auto* found = find (*item.subMenu, text, keep))
                        return found;
            }
            return nullptr;
        };

        {
            AudioEngine offline (false);
            MainComponent main (offline, false);
            main.setSize (1600, 900);
            main.setVisible (true);
            main.setLayoutStyle (layoutstyle::Style::pattern);
            offline.editSong ([] (Song& s) {
                s = Song::makeDefault();
                s.mode = PlayMode::song;
                s.patterns.resize (1);
                s.patterns[0].name = "Pattern 1";
                s.patterns[0].notes = { { 0, ppq, 0, 60 }, { ppq, ppq, 1, 62 } };
                s.channels[0].mixerTrack = 5;
                Clip c;
                c.pattern = 0, c.track = 0, c.start = 0, c.length = ppq * 4;
                s.playlist = { c };
                c.track = 3;
                s.playlist.push_back (c);
            });
            offline.dispatchPendingMessages();
            auto& playlist = main.getPlaylistForSnapshot();

            // 1. the render
            check ("render window", exportTracksWindowCheck (offline, folder.getChildFile ("rec"), shots) == "OK", exportTracksWindowCheck (offline, folder.getChildFile ("rec"), {}));
            std::vector<int> asked;
            std::function<void (const std::vector<juce::File>&, const exportopts::Options&)> done;
            const juce::ScopedValueSetter<std::function<void (std::vector<int>, std::function<void (const std::vector<juce::File>&, const exportopts::Options&)>)>> hook (
                mixeroptions::renderTracks, [&asked, &done] (std::vector<int> tracks, std::function<void (const std::vector<juce::File>&, const exportopts::Options&)> d) {
                    asked = std::move (tracks);
                    done = std::move (d);
                });
            main.selectTabForSnapshot (3);
            main.getMixerForSnapshot().selectTrack (5);
            check ("render selected ran", main.runActionForSnapshot ("mixer.renderSelected") && asked == std::vector<int> { 5 } && done != nullptr);
            if (done)
            {
                const auto wav = savedWav ("one_Insert 5.wav", 2.0);
                exportopts::Options o;
                o.range = exportopts::Range::song;
                done ({ wav }, o);
                const auto& s = offline.getSong();
                const auto& c = s.playlist.back();
                check ("song render: the clip on the first free track", s.playlist.size() == 3 && c.kind == ClipKind::audio && c.track == 1 && c.start == 0,
                       juce::String (c.track));
                check ("its channel: named after the file, to the Master, the sample", s.channels.back().name == "one_Insert 5" && s.channels.back().mixerTrack == 0
                                                                                           && s.channels.back().samplePath == wav.getFullPathName().toStdString());
                check ("the Playlist's tools paint it", playlist.brush.kind == ClipKind::audio && playlist.brush.index == (int) s.channels.size() - 1);
                offline.undo();
                check ("one undo step", offline.getSong().playlist.size() == 2);
                offline.redo();
                o.range = exportopts::Range::pattern;
                done ({ savedWav ("two_Insert 5.wav", 1.0) }, o);
                check ("pattern render: only the channel (the Picker's)", offline.getSong().playlist.size() == 3 && offline.getSong().channels.back().name == "two_Insert 5"
                                                                             && playlist.brush.index == (int) offline.getSong().channels.size() - 1);
                // an .mp3 (the formats JUCE reads by itself do not include it: nothing was placed)
                const auto mp3 = folder.getChildFile ("three_Insert 5.mp3");
                double seconds = 0.0;
                float peak = 0.0f;
                const auto error = exportAudioNow (offline, (int) exportopts::Format::mp3, mp3, seconds, peak);
                o.range = exportopts::Range::song;
                done ({ mp3 }, o);
                check ("an .mp3 is placed too", error.isEmpty() && offline.getSong().playlist.size() == 4 && offline.getSong().channels.back().name == "three_Insert 5"
                                                   && offline.getSong().playlist.back().track == 2, error);
            }
            offline.setInsertArmed (5, true);
            check ("render armed ran", main.runActionForSnapshot ("mixer.renderArmed") && asked == std::vector<int> { 5 });
            offline.setInsertArmed (5, false);

            // 2. transpose
            std::deque<juce::PopupMenu::Item> kept;
            const auto rackMenu = main.getPanelMenuForSnapshot (workspace::Panel::channelRack);
            const auto* selected = find (rackMenu, tr ("Transpose selected..."), kept);
            check ("Channel rack menu: Transpose selected...", selected != nullptr && selected->isEnabled);
            check ("Pattern menu: Transpose...", find (rackMenu, tr ("Transpose..."), kept) != nullptr);
            if (shots.isNotEmpty())
            {
                saveMenus (juce::StringArray { tr ("Channel Rack") }, std::vector<juce::PopupMenu> { rackMenu }, shots + "-2-rack-menu.png");
                auto box = makeTransposeDialog();
                savePng (shot::snapshot (*box), shots + "-2-transpose.png");
            }

            // 4. Alt + tool letters
            main.selectTabForSnapshot (2); // (the Playlist, focused)
            const juce::KeyPress altC ('c', juce::ModifierKeys::altModifier, 0), altD ('d', juce::ModifierKeys::altModifier, 0);
            const auto patternsBefore = offline.getSong().patterns.size();
            main.setTypingPianoForTest (true);
            main.runActionForSnapshot ("playlist.tool.brush");
            main.keyPressed (altC);
            check ("Alt+C picks the Slice tool (typing keyboard on)", playlist.currentTool() == EditTool::knife);
            main.keyPressed (altD);
            check ("Alt+D picks the Delete tool", playlist.currentTool() == EditTool::eraser);
            main.setTypingPianoForTest (false);
            main.runActionForSnapshot ("playlist.tool.brush");
            main.keyPressed (altC);
            check ("Alt+C does nothing in the Playlist with it off (FL)", playlist.currentTool() == EditTool::brush);
            check ("Alt+C clones no pattern", offline.getSong().patterns.size() == patternsBefore);
            main.runActionForSnapshot ("playlist.tool.pencil");

            // 5. clone names
            for (int i = 0; i < 3; ++i)
                main.runActionForSnapshot ("pattern.clone");
            {
                const auto& p = offline.getSong().patterns;
                check ("Pattern 1 cloned three times", p.size() == 4 && p[1].name == "Pattern 2" && p[2].name == "Pattern 3" && p[3].name == "Pattern 4",
                       p.size() == 4 ? juce::String (p[1].name + " | " + p[2].name + " | " + p[3].name) : juce::String ((int) p.size()));
            }
            offline.editSong ([] (Song& s) { s.patterns[3].name = "Lead"; });
            for (int i = 0; i < 2; ++i)
                main.runActionForSnapshot ("pattern.clone");
            check ("Lead cloned twice", offline.getSong().patterns.back().name == "Lead #3", juce::String (offline.getSong().patterns.back().name));

            // 9. F12 and the Search palette
            main.runActionForSnapshot ("help.searchActions");
            const auto palette = [&main] { return main.findChildWithID ("palette") != nullptr; };
            check ("the Search palette opens", palette());
            main.runActionForSnapshot ("view.closeWindows");
            check ("Close all windows closes it", ! palette());
        }

        // 3. the scale box in narrow windows
        {
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            PianoRollComponent roll (offline);
            roll.setVisible (true);
            for (const auto width : { 1500, 1000, 840, 800, 760 })
            {
                roll.setBounds (0, 0, width, 500);
                roll.resized();
                const auto b = roll.scaleBoundsForSnapshot();
                check ("scale box in a " + juce::String (width) + " px Piano roll", roll.scaleBoxForTests().isVisible() && b.getWidth() >= pianoroll::compactChordBoxWidth && b.getRight() <= width,
                       b.toString());
                if (shots.isNotEmpty())
                    savePng (shot::snapshot (roll), shots + "-3-roll-" + juce::String (width) + ".png");
            }
            roll.setScaleForSnapshot (2, 1);
            if (shots.isNotEmpty())
                saveMenus (juce::StringArray { tr ("Scale") }, std::vector<juce::PopupMenu> { roll.getScaleHighlightMenuForSnapshot() }, shots + "-3-scale-menu.png");
            layoutstyle::setCurrent (layoutstyle::Style::simple);
            roll.setDocked (true);
            roll.setBounds (0, 0, 760, 300);
            roll.resized();
            check ("docked and narrow: the one box stands in for the two", roll.scaleBoxForTests().isVisible());
            roll.setBounds (0, 0, 1180, 300);
            roll.resized();
            check ("docked and wide: the two boxes", ! roll.scaleBoxForTests().isVisible());
            roll.setDocked (false);
            layoutstyle::setCurrent (layoutstyle::Style::pattern);
        }

        // 10. issue #272: a slide note of no length is FL's pill (7 px at any zoom, centred on its start)
        {
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            offline.editSong ([] (Song& song) {
                auto& p = song.patterns[0];
                p.notes = { { 0, 4 * ppq, 0, 48 } };
                for (const auto [start, key, none] : { std::tuple { (std::int64_t) ppq, 60, true }, std::tuple { (std::int64_t) (ppq * 5 / 2), 55, false } })
                {
                    Note n;
                    n.start = start, n.key = key, n.channel = 0, n.slide = true, n.length = ticksPerStep;
                    if (none)
                        stepslide::make (n);
                    p.notes.push_back (n);
                }
                p.sortNotes();
            });
            PianoRollComponent roll (offline);
            roll.setVisible (true);
            roll.setBounds (0, 0, 1000, 500);
            for (const auto zoom : { 0.1, 0.4 })
            {
                roll.setViewForTests (0.0, 18, 52);
                roll.setPixelsPerTickForTests (zoom);
                const auto& n = offline.getSong().patterns[0].notes;
                const auto at = std::find_if (n.begin(), n.end(), [] (const Note& x) { return stepslide::is (x); });
                const auto r = at != n.end() ? roll.noteRectForTests (*at) : juce::Rectangle<float>();
                check ("#272 the pill at " + juce::String (zoom) + " px per tick", juce::approximatelyEqual (r.getWidth(), 7.0f), r.toString());
                if (shots.isNotEmpty())
                    for (const auto scale : { 1.0f, 1.5f })
                        savePng (roll.createComponentSnapshot (roll.getLocalBounds(), true, scale), shots + "-10-slides-" + juce::String (zoom) + (scale > 1.0f ? "@150" : "") + ".png");
            }
            const auto folderFile = folder.getChildFile ("slides.lynx");
            juce::String error;
            const auto saved = ProjectFile::save (offline.getSong(), folderFile, error);
            const auto back = ProjectFile::load (folderFile, error);
            check ("#272 kept in a .lynx", saved && back && std::any_of (back->patterns[0].notes.begin(), back->patterns[0].notes.end(), [] (const Note& x) { return stepslide::is (x); }), error);
        }

        // 6. / 7. Make unique as sample
        {
            AudioEngine offline (false);
            const auto wav = savedWav ("src/loop.wav", 2.0);
            offline.setSong (Song::makeDefault());
            offline.editSong ([&wav] (Song& s) {
                s.bpm = 120.0;
                ChannelDef c;
                c.name = "loop";
                c.instrument = InstrumentKind::audioClip;
                c.samplePath = wav.getFullPathName().toStdString();
                s.channels.push_back (c);
                s.playlist.clear();
                for (int i = 0; i < 12; ++i)
                {
                    Clip clip;
                    clip.kind = ClipKind::audio, clip.channel = (int) s.channels.size() - 1;
                    clip.track = i / 6, clip.start = (i % 6) * ppq * 4, clip.length = ppq * 2, clip.offset = i == 11 ? ppq : 0;
                    s.playlist.push_back (clip);
                }
                s.dataFolder = "C:\\Users\\nobody190ri\\Data\\Demo songs";
            });
            std::vector<int> clips;
            for (int i = 0; i < 12; ++i)
                clips.push_back (i);
            const auto parts = [&wav] { return wav.getParentDirectory().findChildFiles (juce::File::findFiles, false, "loop - Part_*.wav").size(); };
            auto plan = uniquesamples::makePlan (offline.getSong(), 44100.0, clips);
            int steps = 0;
            check ("cancelled: false", ! uniquesamples::render (plan, [&steps] (double) { return ++steps < 5; }));
            check ("cancelled: no file left", parts() == 0, juce::String (parts()));
            plan = uniquesamples::makePlan (offline.getSong(), 44100.0, clips);
            const auto before = offline.getSong().channels.size();
            // (a thread, as the window runs it)
            std::atomic<bool> ok { false };
            std::thread worker ([&] { ok = uniquesamples::render (plan, [] (double) { return true; }); });
            worker.join();
            check ("one file per clip, also for clips that play the same (FL)", ok && parts() == 12, juce::String (parts()));
            offline.editSong ([&plan] (Song& s) { uniquesamples::apply (s, plan); });
            const auto& s = offline.getSong();
            check ("a channel and a clip each", s.channels.size() == before + 12 && s.playlist[0].channel == (int) before && s.playlist[11].offset == 0
                                                    && s.channels.back().name == "loop - Part_12", juce::String (s.channels.back().name));
            offline.undo();
            check ("one undo step", offline.getSong().channels.size() == before && offline.getSong().playlist[11].offset == ppq);
            check ("beside the sample, no folder named after the Windows path", plan.parts.front().folder == wav.getParentDirectory()
                                                                                      && ! juce::File::getCurrentWorkingDirectory().getChildFile ("C:\\Users\\nobody190ri\\Data\\Demo songs").exists());
            const auto data = folder.getChildFile ("data");
            data.createDirectory();
            offline.editSong ([&data] (Song& song) { song.dataFolder = data.getFullPathName().toStdString(); });
            check ("a data folder that exists is used", uniquesamples::folderFor (offline.getSong(), wav) == data.getChildFile ("Samples"));
        }
        return result;
    }

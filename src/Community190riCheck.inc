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
    //  13 FL's marker menu on a marker's tag (shot "-13-marker-menu")
    //  16 the track header menu on the selected tracks (Group, Move down as a block, Random color per track)
    //  14 #247: a letter typed just after a text field lost the keyboard goes back into it
    //  15 #296 / #301: a cut hint (or a cut name in its value) shows whole in the bar's bubble and comes back on the bar
    //  17 #299: a knob's value bubbles close when the knob is hidden or no longer under the pointer
    //  19 #305: the empty workspace's line on the default backdrop and over a bright and a dark workspace image, in the three
    //     layouts and beside a docked Browser (shots "-19-<layout>-<default|bright|dark>[-browser]")
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
                if (item.text.upToFirstOccurrenceOf ("\t", false, false).removeCharacters (juce::String ("&") + juce::String::charToString ((juce::juce_wchar) 1)) == text) // (a menu letter's mark left out)
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
            // (FL's in-place box: a signed whole number; anything else does nothing)
            check ("Transpose box: signed whole numbers", transposeSemitones ("-12") == -12 && transposeSemitones (" +7 ") == 7 && transposeSemitones ("5") == 5
                                                             && transposeSemitones ("") == 0 && transposeSemitones ("1.5") == 0 && transposeSemitones ("x") == 0
                                                             && transposeSemitones ("99999") == 1000);
            if (shots.isNotEmpty())
            {
                saveMenus (juce::StringArray { tr ("Channel Rack") }, std::vector<juce::PopupMenu> { rackMenu }, shots + "-2-rack-menu.png");
                auto box = makeTransposeBoxForSnapshot();
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

        // 11: the DAWproject export as the window runs it (the file on a worker), and its failures' messages
        {
            AudioEngine offline (false);
            offline.editSong ([] (Song& s) {
                s = Song::makeDefault();
                s.patterns.resize (1);
                s.patterns[0].notes = { { 0, ppq, 0, 60 } };
                Clip c;
                c.pattern = 0, c.length = 4 * ppq;
                s.playlist = { c };
                Marker m;
                m.tick = 4 * ppq, m.action = Marker::markerSkip, m.name = "skip";
                s.addMarker (m);
            });
            juce::StringArray notes;
            juce::String error;
            const auto out = folder.getChildFile ("worker.dawproject");
            check ("#11 DAWproject written on a worker", writeDawProjectOnWorkerForTests (offline, out, notes, error) && out.getSize() > 0, error);
            const auto bad = folder.getChildFile ("no such folder").getChildFile ("x.dawproject");
            notes.clear();
            check ("#11 a file that cannot be written says why", ! writeDawProjectOnWorkerForTests (offline, bad, notes, error) && error.isNotEmpty(), error);
        }

        // 14: the text field's letters (issue #247)
        {
            juce::Component window;
            window.setBounds (100, 100, 300, 80);
            juce::TextEditor field;
            field.setBounds (10, 10, 200, 24);
            window.addAndMakeVisible (field);
            window.addToDesktop (juce::ComponentPeer::windowIsTemporary);
            window.setVisible (true);
            field.setText ("rend", false);
            field.moveCaretToEnd();
            const auto now = juce::Time::getMillisecondCounterHiRes();
            ui::keyfocus::noteTextFieldLeftForTests (&field, now);
            check ("#247 a letter just after the field lost the keys goes into it", ui::keyfocus::typeIntoFieldJustLeft (juce::KeyPress ('e', {}, 'e')) && field.getText() == "rende",
                   field.getText());
            ui::keyfocus::noteTextFieldLeftForTests (&field, now - 1000.0);
            check ("#247 ... not after the grace time", ! ui::keyfocus::typeIntoFieldJustLeft (juce::KeyPress ('r', {}, 'r')));
            ui::keyfocus::noteTextFieldLeftForTests (&field, juce::Time::getMillisecondCounterHiRes());
            check ("#247 ... not with Ctrl", ! ui::keyfocus::typeIntoFieldJustLeft (juce::KeyPress ('r', juce::ModifierKeys::ctrlModifier, 'r')) && field.getText() == "rende");
            ui::keyfocus::noteTextFieldLeftForTests (nullptr, 0.0);
            window.removeFromDesktop();
        }

        // 15: the hint bar (issues #296, #301)
        {
            ui::HintBar bar;
            bar.setBounds (0, 0, 220, 30);
            juce::Image image (juce::Image::ARGB, 220, 30, true);
            const juce::String text = "Channel button: [LMB] select  [RMB] menu  [Wheel] scroll the rack, the patterns get a colour in the colour of the channel that plays them";
            bar.setHint ({ text, {}, false, false });
            {
                juce::Graphics g (image);
                bar.paint (g);
            }
            check ("#296 a cut hint shows whole in the bar's bubble", ui::quickTipOf (bar).contains ("in the colour of the channel that plays them"));
            bar.setHint ({ "Tempo", "140", true, true });
            check ("#296 ... and comes back on the bar for a while", bar.hintToKeep().text == text && bar.hintToKeep (0.0).text == "Tempo");
            {
                juce::Graphics g (image);
                bar.paint (g);
            }
            check ("#296 a hint that fits has no bubble", ui::quickTipOf (bar).isEmpty());
            bar.setHint ({ "Track", "BWB WAVE SOMETHING VERY LONG AND SHORTENED (22)", false, true });
            {
                juce::Graphics g (image);
                bar.paint (g);
            }
            check ("#301 a name cut in the value shows whole in the bubble", ui::quickTipOf (bar).contains ("BWB WAVE SOMETHING VERY LONG AND SHORTENED (22)"), ui::quickTipOf (bar));
            if (shots.isNotEmpty())
            {
                const auto tip = ui::quickTipOf (bar);
                const auto size = ui::quickTipSize (tip);
                juce::Image bubble (juce::Image::ARGB, size.getWidth(), size.getHeight(), true);
                juce::Graphics g (bubble);
                ui::paintQuickTip (g, size.toFloat(), tip);
                juce::PNGImageFormat png;
                if (auto out = juce::File (shots + "-15-hint-bubble.png").createOutputStream())
                    png.writeImageToStream (bubble, *out);
            }
        }

        // 17: value bubbles (issue #299)
        {
            juce::Component page;
            page.setBounds (0, 0, 300, 200);
            juce::Slider knob (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
            knob.setBounds (10, 10, 40, 40);
            page.addAndMakeVisible (knob);
            theme::LookAndFeel look;
            knob.setLookAndFeel (&look);
            knob.setPopupDisplayEnabled (true, false, &page);
            const auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent down (source, { 20.0f, 20.0f }, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &knob, &knob, now,
                                         { 20.0f, 20.0f }, now, 1, false);
            knob.mouseDown (down);
            const auto made = knob.getCurrentPopupDisplay() != nullptr;
            check ("#299 the knob's bubble is closed once the knob is not on screen", made && theme::checkSliderPopupsNow() == 1 && knob.getCurrentPopupDisplay() == nullptr);
            knob.mouseUp (down);
            knob.setLookAndFeel (nullptr);
            check ("#299 a point outside the knob is not on it", ! theme::pointOnVisiblePart (knob, { -100, -100 }));
            // LYNX's own value pill (the knob contract's, the hover read-out's): it goes when its control moves or is hidden
            juce::Component window, control;
            window.setBounds (100, 100, 300, 200);
            control.setBounds (20, 60, 40, 40);
            window.addAndMakeVisible (control);
            window.addToDesktop (juce::ComponentPeer::windowIsTemporary);
            window.setVisible (true);
            ui::flashValueTip (control, control.getLocalBounds(), "12%");
            const auto shown = ui::valueTipShown();
            const auto keptInPlace = ! ui::checkValueTipNow();
            control.setTopLeftPosition (20, 10); // (the page scrolled)
            check ("#299 the value pill shows, stays while its control stays, goes when the control moves", shown && keptInPlace && ui::checkValueTipNow() && ! ui::valueTipShown());
            ui::flashValueTip (control, control.getLocalBounds(), "13%");
            control.setVisible (false); // (the page changed)
            check ("#299 ... and when the control is hidden", ui::checkValueTipNow() && ! ui::valueTipShown());
            ui::hideValueTip();
            window.removeFromDesktop();
        }

        // 19: the empty workspace's line over a workspace image (issue #305)
        if (shots.isNotEmpty())
        {
            const auto themeWas = themeui::current();
            const juce::ScopeGuard themeBack { [themeWas] { themeui::apply (themeWas, false); } };
            const auto picture = [&folder] (const juce::String& name, bool bright) {
                juce::Image image (juce::Image::RGB, 640, 360, true);
                juce::Graphics g (image);
                g.setGradientFill (bright ? juce::ColourGradient (juce::Colour (0xfff6f0d8), 0.0f, 0.0f, juce::Colour (0xff9fd3ff), 640.0f, 360.0f, false)
                                          : juce::ColourGradient (juce::Colour (0xff10131c), 0.0f, 0.0f, juce::Colour (0xff3a1f4a), 640.0f, 360.0f, false));
                g.fillAll();
                juce::Random r (7);
                for (int i = 0; i < 400; ++i) // (a busy picture, as a photo)
                {
                    g.setColour (juce::Colour ((juce::uint8) r.nextInt (256), (juce::uint8) r.nextInt (256), (juce::uint8) r.nextInt (256), 0.35f));
                    g.fillEllipse ((float) r.nextInt (640), (float) r.nextInt (360), 6.0f + (float) r.nextInt (40), 6.0f + (float) r.nextInt (40));
                }
                const auto file = folder.getChildFile (name);
                juce::PNGImageFormat png;
                if (auto out = file.createOutputStream())
                    png.writeImageToStream (image, *out);
                return file;
            };
            const auto bright = picture ("bright.png", true), dark = picture ("dark.png", false);
            const juce::File none; // (the default backdrop, no image: yannik's case)
            for (const auto style : { layoutstyle::Style::pattern, layoutstyle::Style::clipLaunch, layoutstyle::Style::simple })
                for (const auto* which : { &none, &bright, &dark })
                    for (const auto withBrowser : { false, true })
                    {
                        auto spec = themeWas;
                        spec.image = which == &none ? std::string() : which->getFullPathName().toStdString();
                        themeui::apply (spec, false);
                        AudioEngine offline (false);
                        MainComponent main (offline, false);
                        main.setSize (1400, 800);
                        main.setVisible (true);
                        main.setLayoutStyle (style);
                        offline.dispatchPendingMessages();
                        for (int p = 0; p < workspace::panelCount; ++p)
                            main.hidePanelForCheck ((workspace::Panel) p);
                        if (withBrowser)
                            main.runActionForSnapshot ("panel.browser");
                        const auto name = juce::String (style == layoutstyle::Style::pattern ? "pattern" : style == layoutstyle::Style::clipLaunch ? "cliplaunch" : "simple")
                                          + (which == &none ? "-default" : which == &bright ? "-bright" : "-dark") + (withBrowser ? "-browser" : "");
                        savePng (shot::snapshot (main), shots + "-19-" + name + ".png");
                    }
            layoutstyle::setCurrent (layoutstyle::Style::pattern);
        }

        // 13 and 16: the Playlist's marker menu and the track menu on a selection
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
                s.playlistTracks.resize (12);
                for (int i = 0; i < 12; ++i)
                    s.playlistTracks[(std::size_t) i].name = "Track " + std::to_string (i + 1);
                for (const auto [bar, action, name] : { std::tuple { 3, Marker::markerSkip, "B: skip" }, { 7, Marker::markerSkip, "C: skip" }, { 11, Marker::none, "Verse" } })
                {
                    Marker m;
                    m.tick = (std::int64_t) (bar - 1) * 4 * ppq, m.action = action, m.name = name;
                    s.addMarker (m);
                }
                for (int b = 0; b < 12; ++b)
                {
                    Clip c;
                    c.pattern = 0, c.track = b % 3, c.start = (std::int64_t) b * 4 * ppq, c.length = 4 * ppq;
                    s.playlist.push_back (c);
                }
            });
            offline.dispatchPendingMessages();
            auto& playlist = main.getPlaylistForSnapshot();
            std::deque<juce::PopupMenu::Item> kept;
            const auto markerMenu = playlist.markerMenuForSnapshot (0); // ("B: skip")
            for (const auto& item : { tr ("Add marker..."), tr ("Add time signature..."), tr ("Place loop"), tr ("Delete"), tr ("Rename..."),
                                      tr ("Move content left"), tr ("Move content right"), tr ("Marker skip") })
                check ("marker menu: " + item, find (markerMenu, item, kept) != nullptr);
            check ("marker menu: the Action list", find (markerMenu, tr ("Marker pause"), kept) != nullptr && find (markerMenu, tr ("Stop recording"), kept) != nullptr);
            if (const auto* right = find (markerMenu, tr ("Move content right"), kept); right != nullptr && right->action)
            {
                right->action();
                const auto& m = offline.getSong().markers();
                check ("Move content right: C's section before B's", m.size() == 3 && m[0].name == "C: skip" && m[0].tick == 8 * ppq && m[1].name == "B: skip"
                                                                       && m[1].tick == 24 * ppq, juce::String (m[0].name) + " " + juce::String (m[0].tick));
                offline.undo();
                check ("Move content: one undo step", offline.getSong().markers()[0].name == "B: skip");
            }
            if (shots.isNotEmpty())
                saveMenus (juce::StringArray { tr ("Marker") }, std::vector<juce::PopupMenu> { markerMenu }, shots + "-13-marker-menu.png");

            playlist.selectTracksForSnapshot ({ 6, 7, 8 });
            const auto trackMenu = playlist.trackMenuForSnapshot (6);
            if (const auto* group = find (trackMenu, tr ("Group with above track"), kept); group != nullptr && group->action)
            {
                group->action();
                const auto& t = offline.getSong().playlistTracks;
                check ("Group with above track: every selected track", t.size() > 8 && t[6].grouped && t[7].grouped && t[8].grouped && ! t[9].grouped);
                offline.undo();
                check ("Group: one undo step", ! offline.getSong().playlistTracks[6].grouped && ! offline.getSong().playlistTracks[8].grouped);
            }
            else
                check ("track menu: Group with above track", false);
            if (shots.isNotEmpty())
                saveMenus (juce::StringArray { tr ("Track") }, std::vector<juce::PopupMenu> { trackMenu }, shots + "-16-track-menu.png");
        }
        return result;
    }

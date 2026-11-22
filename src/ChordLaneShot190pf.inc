    // --chordlane190pfshot <prefix> (round 190pf, research190oe P13 and P12): the Chord lane and Smart Tempo through the real
    // main window, offscreen (--shot-scale 1.5 --shot-suffix @150 for @150):
    //  - Pattern layout: the lane off (the default: FL's Playlist unchanged), then on under the Sections lane with the chords
    //    detected from a four-chord song's notes, a chord dragged (mid-drag), the block and lane menus' items;
    //  - Clip-launch and Simple layouts: the lane's looks;
    //  - the Piano roll's rows lit by the chord under the song position (C, then F);
    //  - Smart Tempo: the preview of a free recording that speeds up from 100 to 120 BPM (smooth 0 % and 50 %) and the
    //    Playlist after "Follow" (a tempo marker per bar);
    //  - the lane's paint cost with 64 chords against the lane off.
    // prefix-checks.txt lists what was checked. Included in the app class of Main.cpp.
    bool runChordLaneShot190pf (const juce::String& prefix)
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
        chordlane::laneOn() = false; // (a fresh profile)
        chordlane::pianoRollHighlight() = false;
        sections::patternLaneOn() = false;
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        AudioEngine engine (false);
        MainComponent ui (engine, false);
        ui.setSize (1600, 900);
        ui.setLayoutStyle (layoutstyle::Style::pattern, false);
        const auto bar = (std::int64_t) 4 * ppq;
        // a song: a piano part C | Am | F | G7 (a bar each, the bass under it) twice, drums all through, two sections
        const auto makeSong = [bar] {
            auto song = Song::makeDefault();
            song.playlist.clear();
            song.patterns.resize (2);
            song.patterns[0].name = "Drums";
            song.patterns[0].lengthTicks = bar;
            for (int step : { 0, 4, 8, 12 })
                song.patterns[0].toggleStep (0, step);
            auto& keys = song.patterns[1];
            keys.name = "Piano";
            keys.notes.clear();
            keys.lengthTicks = 4 * bar;
            ChannelDef piano;
            piano.name = "Piano";
            piano.instrument = InstrumentKind::synth;
            piano.mixerTrack = song.claimFreeInsert();
            song.channels.push_back (piano);
            const auto ch = (int) song.channels.size() - 1;
            const std::vector<std::vector<int>> chords { { 48, 60, 64, 67 }, { 45, 57, 60, 64 }, { 41, 60, 65, 69 }, { 43, 59, 62, 65, 67 } };
            for (int b = 0; b < 4; ++b)
                for (const auto k : chords[(std::size_t) b])
                {
                    Note n;
                    n.start = b * bar;
                    n.length = bar - 48;
                    n.key = k;
                    n.channel = ch;
                    keys.notes.push_back (n);
                }
            keys.sortNotes();
            song.playlistTracks.resize (8);
            for (int b = 0; b < 16; ++b)
            {
                Clip c;
                c.pattern = 0;
                c.track = 0;
                c.start = b * bar;
                c.length = bar;
                song.playlist.push_back (c);
            }
            for (int r = 0; r < 2; ++r)
            {
                Clip c;
                c.pattern = 1;
                c.track = 1;
                c.start = (4 + 4 * r) * bar;
                c.length = 4 * bar;
                song.playlist.push_back (c);
            }
            song.sections() = { { 0, 4 * bar, "Intro", 0, 1 }, { 4 * bar, 8 * bar, "Verse", 0, 2 }, { 12 * bar, 4 * bar, "Outro", 0, 3 } };
            projectkey::set (song, projectkey::Key { 0, keyregions::majorScale() });
            return song;
        };
        engine.setSong (makeSong());
        engine.dispatchPendingMessages();
        ui.selectTabForSnapshot (2);
        engine.dispatchPendingMessages();
        auto& pl = ui.getPlaylistForSnapshot();
        pl.setSize (juce::jmax (pl.getWidth(), 1200), juce::jmax (pl.getHeight(), 420));
        pl.setViewForSnapshot (56.0, 0, 0);
        engine.dispatchPendingMessages();
        const auto out = [&prefix] (const juce::String& name) { return prefix + "-" + name + ".png"; };
        const auto snap = [&] (const juce::String& name, int height = 300) {
            pl.refreshViewOptions();
            engine.dispatchPendingMessages();
            savePng (shot::snapshot (pl, juce::Rectangle<int> (0, 0, juce::jmin (pl.getWidth(), 1200), juce::jmin (pl.getHeight(), height))), out (name));
        };
        auto* lane = pl.getChordLaneForSnapshot();
        check ("lane_exists", lane != nullptr);
        if (lane == nullptr)
            return false;
        const auto names = [&engine] {
            juce::StringArray list;
            for (const auto& c : engine.getSong().chords())
                list.add (juce::String::fromUTF8 (chordlane::name (c).c_str()));
            return list.joinIntoString (",");
        };

        // --- Pattern: off by default, on through the extra ---------------------------------------------------------------
        check ("pattern_off_by_default", ! pl.chordLaneShown() && ! lane->isVisible() && pl.tracksTop() == PlaylistComponent::headerHeight + pl.videoSectionHeight());
        snap ("pattern-lane-off");
        workflowextras::setExtra ("playlist.sectionsLane", true);
        workflowextras::setExtra ("playlist.chordLane", true);
        pl.refreshViewOptions();
        check ("pattern_on_under_the_sections", pl.chordLaneShown() && lane->isVisible() && lane->getHeight() == PlaylistComponent::chordLaneFlHeight
                                                    && lane->getY() == PlaylistComponent::headerHeight + PlaylistComponent::sectionsLaneFlHeight,
               juce::String (lane->getY()));
        check ("extra_saved", workflowextras::uiOptionsText().contains ("chordLane=1"));
        snap ("pattern-lane-empty");
        // detect the chords of the piano part (bars 5-12)
        engine.editSong ([bar] (Song& s) {
            s.loopStart = 4 * bar;
            s.loopEnd = 12 * bar;
        }, false);
        lane->detectFromNotes (chordlane::Resolution::bar);
        engine.dispatchPendingMessages();
        check ("detect_from_notes", names() == "C,Am,F,G7,C,Am,F,G7", names());
        engine.editSong ([] (Song& s) { s.loopStart = s.loopEnd = 0; }, false);
        lane->select (1);
        snap ("pattern-lanes-on");

        // drag "F" (bar 7) to bar 15: mid-drag, then the drop
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const auto event = [&] (juce::Component& c, juce::Point<int> p, juce::ModifierKeys mods, juce::Point<int> down, int clicks, bool dragged) {
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, down.toFloat(), now, clicks, dragged);
        };
        const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
        const auto xOf = [&] (std::int64_t tick) { return (int) std::lround (pl.xOfTickForSnapshot ((double) tick)); };
        {
            const juce::Point<int> from (xOf (6 * bar + bar / 4), lane->getHeight() / 2), to (xOf (14 * bar + bar / 4), lane->getHeight() / 2);
            lane->mouseDown (event (*lane, from, left, from, 1, false));
            lane->mouseDrag (event (*lane, (from + to) / 2, left, from, 1, true));
            lane->mouseDrag (event (*lane, to, left, from, 1, true));
            savePng (shot::snapshot (pl, juce::Rectangle<int> (0, 0, juce::jmin (pl.getWidth(), 1200), 300)), out ("pattern-chord-drag"));
            lane->mouseUp (event (*lane, to, {}, from, 1, true));
            engine.dispatchPendingMessages();
        }
        check ("drag_moves_the_chord", names() == "C,Am,G7,C,Am,F,G7,F" && engine.getSong().chords()[7].start == 14 * bar, names());
        engine.undo();
        engine.dispatchPendingMessages();
        check ("undo_brings_it_back", names() == "C,Am,F,G7,C,Am,F,G7", names());
        // the menus
        {
            juce::StringArray items, sub;
            const auto chordMenu = lane->chordMenu (2);
            for (juce::PopupMenu::MenuItemIterator it (chordMenu, true); it.next();)
                items.add (it.getItem().text.replace ("\t", " "));
            check ("chord_menu_degrees", items.contains ("I C") && items.contains ("ii Dm") && items.contains ("V G") && items.contains (tr ("Duplicate"))
                                             && items.contains (tr ("Loop this chord")) && items.contains ("V7 G7"),
                   items.joinIntoString ("|").substring (0, 300));
            const auto laneMenu = lane->laneMenu (0);
            for (juce::PopupMenu::MenuItemIterator it (laneMenu); it.next();)
                sub.add (it.getItem().text);
            check ("lane_menu", sub.contains (tr ("Detect chords from the notes")) && sub.contains (tr ("Chords from the key regions"))
                                    && sub.contains (tr ("Export chords as MIDI")) && sub.contains (tr ("Channels that follow the chords"))
                                    && sub.contains (tr ("Use these chords on the Chord pad")),
                   sub.joinIntoString ("|"));
            // a double-click on an empty place: the degree chords (the picker) — pick "IV" for bar 16
            lane->addChordAt (15 * bar, chordlane::degreeChords ({ 0, keyregions::majorScale() })[3].chord);
            check ("add_chord_on_the_bar", names().endsWith (",G7,F") && engine.getSong().chords().back().start == 15 * bar
                                               && engine.getSong().chords().back().length == bar, names());
            engine.undo();
        }
        // the channel follows; the Chord pad takes the chords; MIDI export
        engine.editSong ([] (Song& s) { s.channels.back().followChords = chordlane::followChordTones; });
        check ("follow_saved_on_the_channel", engine.getSong().channels.back().followChords == 1);
        engine.undo();
        {
            const auto patterns = engine.getSong().patterns.size();
            engine.editSong ([] (Song& s) { chordlane::exportAsPattern (s, -1, "Chords", "Chords"); });
            check ("export_chords_as_midi", engine.getSong().patterns.size() == patterns + 1 && engine.getSong().patterns.back().notes.size() == 34,
                   juce::String ((int) engine.getSong().patterns.back().notes.size()));
            engine.undo();
            engine.editSong ([] (Song& s) { s.keyboardChords = chordlane::padSet (s.chords()); });
            check ("chord_pad_set", engine.getSong().keyboardChords.size() == 4);
            engine.undo();
        }
        engine.dispatchPendingMessages();

        // --- the Piano roll's rows: the chord under the song position ------------------------------------------------------
        {
            engine.setSelectedChannel ((int) engine.getSong().channels.size() - 1);
            engine.editSong ([] (Song& s) { s.currentPattern = 1; }, false);
            engine.dispatchPendingMessages();
            PianoRollComponent roll (engine);
            roll.setBounds (0, 0, 1100, 520);
            roll.setVisible (true);
            engine.sendChangeMessage();
            engine.dispatchPendingMessages();
            roll.scrollToNotes (true);
            workflowextras::setExtra ("pianoRoll.chordHighlight", true);
            roll.setChordHighlightForShot (chordlane::highlightAt (engine.getSong(), 4 * bar));
            check ("roll_highlight_c", roll.chordHighlightForShot().root == 0);
            savePng (shot::snapshot (roll), out ("pianoroll-chord-c"));
            roll.setChordHighlightForShot (chordlane::highlightAt (engine.getSong(), 6 * bar));
            check ("roll_highlight_f", roll.chordHighlightForShot().root == 5);
            savePng (shot::snapshot (roll), out ("pianoroll-chord-f"));
            workflowextras::setExtra ("pianoRoll.chordHighlight", false);
        }

        // --- the paint cost: 64 chords against the lane off ----------------------------------------------------------------
        {
            auto many = makeSong();
            for (int i = 0; i < 64; ++i)
            {
                auto c = chordlane::make (i % 12, i % chordlane::shapeCount);
                c.start = i * bar / 4;
                c.length = bar / 4;
                many.chords().push_back (c);
            }
            chordlane::normalize (many.chords());
            engine.setSong (std::move (many));
            engine.dispatchPendingMessages();
            pl.setViewForSnapshot (56.0, 0, 0);
            workflowextras::setExtra ("playlist.sectionsLane", false);
            const auto time = [&] (bool on) {
                workflowextras::setExtra ("playlist.chordLane", on);
                pl.refreshViewOptions();
                juce::Image image (juce::Image::ARGB, pl.getWidth(), pl.getHeight(), true);
                double best = 1.0e9;
                for (int round = 0; round < 5; ++round)
                {
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    for (int k = 0; k < 20; ++k)
                    {
                        juce::Graphics g (image);
                        pl.paintEntireComponent (g, true);
                    }
                    best = juce::jmin (best, (juce::Time::getMillisecondCounterHiRes() - t0) / 20.0);
                }
                return best;
            };
            const auto off = time (false), on = time (true), off2 = time (false), on2 = time (true);
            const auto extra = juce::jmin (on, on2) - juce::jmin (off, off2);
            {
                juce::Image image (juce::Image::RGB, lane->getWidth(), lane->getHeight(), false);
                double best = 1.0e9;
                for (int round = 0; round < 5; ++round)
                {
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    for (int k = 0; k < 20; ++k)
                    {
                        juce::Graphics g (image);
                        lane->paintLane (g);
                    }
                    best = juce::jmin (best, (juce::Time::getMillisecondCounterHiRes() - t0) / 20.0);
                }
                check ("lane_64_chords_uncached_ms", best <= 2.0, juce::String (best, 3) + " ms");
            }
            check ("paint_64_chords_within_0.2ms", extra <= 0.2, juce::String (extra, 3) + " ms (off " + juce::String (juce::jmin (off, off2), 3) + ", on "
                                                                     + juce::String (juce::jmin (on, on2), 3) + ")");
            snap ("pattern-64-chords");
            engine.setSong (makeSong());
            engine.dispatchPendingMessages();
            engine.editSong ([bar] (Song& s) {
                auto found = chordlane::detect (s, chordlane::songNotes (s, 0, 16 * bar), 0, 16 * bar);
                chordlane::write (s, 0, 16 * bar, found);
            });
            workflowextras::setExtra ("playlist.sectionsLane", true);
        }

        // --- Clip-launch and Simple looks ----------------------------------------------------------------------------------
        ui.setLayoutStyle (layoutstyle::Style::clipLaunch, false);
        engine.dispatchPendingMessages();
        check ("clip_launch_lane", pl.chordLaneShown() && lane->getHeight() == PlaylistComponent::chordLaneFlHeight);
        pl.setViewForSnapshot (56.0, 0, 0);
        lane->select (2);
        snap ("cliplaunch-lane");
        ui.setLayoutStyle (layoutstyle::Style::simple, false);
        engine.dispatchPendingMessages();
        pl.setViewForSnapshot (44.0, 0, 0);
        check ("simple_lane", pl.chordLaneShown() && lane->getHeight() == PlaylistComponent::chordLaneSimpleHeight);
        {
            juce::StringArray items;
            const auto rulerMenu = pl.rulerMenu (xOf (2 * bar));
            for (juce::PopupMenu::MenuItemIterator it (rulerMenu); it.next();)
                items.add (it.getItem().text);
            check ("simple_ruler_menu_has_the_lane", items.contains (tr ("Chord lane")), items.joinIntoString ("|").substring (0, 200));
        }
        snap ("simple-lane", 360);
        savePng (shot::snapshot (ui, juce::Rectangle<int> (0, 0, ui.getWidth(), juce::jmin (ui.getHeight(), 460))), out ("simple-window"));
        ui.setLayoutStyle (layoutstyle::Style::pattern, false);
        engine.dispatchPendingMessages();

        // --- Smart Tempo: a free recording that speeds up from 100 to 120 BPM ---------------------------------------------
        // (the clip's waveform strips are made off the message thread: a paint asks for them, then they are waited for)
        const auto settleWaveforms = [&] {
            pl.refreshViewOptions();
            engine.dispatchPendingMessages();
            ui::WaveformCache::getInstance()->waitUntilIdle (60000);
            pl.createComponentSnapshot (pl.getLocalBounds(), true, 1.0f);
            ui::WaveformCache::getInstance()->waitUntilIdle (60000);
            engine.dispatchPendingMessages();
        };
        {
            const double rate = 44100.0;
            std::vector<double> truth;
            const int beats = 16 * 4 + 1;
            std::vector<double> at;
            double t = 0.3;
            for (int n = 0; n < beats; ++n)
            {
                at.push_back (t);
                if (n % 4 == 0)
                    truth.push_back (t);
                t += 60.0 / (100.0 + 20.0 * (double) n / (double) (beats - 1));
            }
            juce::AudioBuffer<float> buffer (1, (int) ((t + 1.0) * rate));
            buffer.clear();
            for (int n = 0; n < beats; ++n)
            {
                const auto start = (int) std::lround (at[(std::size_t) n] * rate);
                const auto down = n % 4 == 0;
                for (int i = 0; i < (int) (0.04 * rate) && start + i < buffer.getNumSamples(); ++i)
                    buffer.addSample (0, start + i, (float) ((down ? 0.9 : 0.5) * std::exp (-(double) i / (0.008 * rate))
                                                             * std::sin (2.0 * juce::MathConstants<double>::pi * (down ? 1500.0 : 1000.0) * i / rate)));
            }
            const juce::File wav (prefix + "-accelerando.wav");
            wav.deleteFile();
            {
                juce::WavAudioFormat format;
                std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (new juce::FileOutputStream (wav), rate, 1, 24, {}, 0));
                if (writer != nullptr)
                    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
            }
            auto song = Song::makeDefault();
            song.playlist.clear();
            song.bpm = 110.0;
            song.playlistTracks.resize (8);
            ChannelDef audio;
            audio.name = "Take 1";
            audio.instrument = InstrumentKind::audioClip;
            audio.samplePath = wav.getFullPathName().toStdString();
            audio.mixerTrack = song.claimFreeInsert();
            song.channels.push_back (audio);
            Clip clip;
            clip.kind = ClipKind::audio;
            clip.channel = (int) song.channels.size() - 1;
            clip.track = 0;
            clip.start = 2 * bar;
            clip.length = (std::int64_t) std::llround ((double) buffer.getNumSamples() / rate * 110.0 / 60.0 * ppq);
            song.playlist.push_back (clip);
            engine.setSong (std::move (song));
            engine.dispatchPendingMessages();
            workflowextras::setExtra ("playlist.chordLane", false);
            workflowextras::setExtra ("playlist.sectionsLane", false);
            pl.setViewForSnapshot (40.0, 0, 0);
            double sampleRate = 0.0;
            auto mono = smartTempoAudio (engine, 0, sampleRate); // (loads the sample: the clip then shows its waveform)
            for (int k = 0; k < 40; ++k)
            {
                engine.dispatchPendingMessages();
                juce::Thread::sleep (25);
            }
            {
                juce::StringArray items;
                const auto clipMenu = pl.getClipMenuForSnapshot (0);
                for (juce::PopupMenu::MenuItemIterator it (clipMenu); it.next();)
                    items.add (it.getItem().text);
                check ("clip_menu_smart_tempo", items.contains (tr ("Make the project tempo follow this clip...")), items.joinIntoString ("|").substring (0, 200));
            }
            check ("smarttempo_reads_the_clip", ! mono.empty() && sampleRate == rate, juce::String ((int) mono.size()));
            SmartTempoWindow window (engine, 0, std::move (mono), sampleRate, nullptr);
            const auto& plan = window.currentPlan();
            check ("smarttempo_bars", plan.ok && plan.rawBpm.size() == 16 && plan.maxErrorMs < 0.5, juce::String ((int) plan.rawBpm.size()));
            double worst = 0.0;
            for (std::size_t k = 0; k + 1 < truth.size() && k < plan.rawBpm.size(); ++k)
                worst = juce::jmax (worst, std::abs (plan.rawBpm[k] - 240.0 / (truth[k + 1] - truth[k])));
            check ("smarttempo_within_half_a_bpm", worst < 0.5, juce::String (worst, 3) + " BPM");
            savePng (shot::snapshot (window), out ("smarttempo-dialog"));
            window.setSmoothForShot (50.0);
            check ("smarttempo_smooth_drifts", window.currentPlan().maxErrorMs > 0.5, juce::String (window.currentPlan().maxErrorMs, 1) + " ms");
            savePng (shot::snapshot (window), out ("smarttempo-dialog-smooth"));
            window.setSmoothForShot (0.0);
            const auto json = ProjectFile::toJson (engine.getSong());
            check ("smarttempo_follow", window.apply());
            engine.dispatchPendingMessages();
            check ("smarttempo_markers", tempomarkers::list (engine.getSong()).size() == 16 && engine.getSong().playlist[0].start < 2 * bar,
                   juce::String ((int) tempomarkers::list (engine.getSong()).size()));
            settleWaveforms();
            snap ("smarttempo-after");
            engine.undo();
            engine.dispatchPendingMessages();
            check ("smarttempo_one_undo_step", ProjectFile::toJson (engine.getSong()) == json);
            settleWaveforms();
            snap ("smarttempo-before"); // (the song as before Follow, after the undo)
            wav.deleteFile();
        }

        workflowextras::setExtra ("playlist.chordLane", false);
        workflowextras::setExtra ("playlist.sectionsLane", false);
        juce::File (prefix + "-checks.txt").replaceWithText (report.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

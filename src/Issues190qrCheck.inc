    // Round 190qr (issues #274-#281, yannikthegenius, 2026-11-23): the fixes and extras through the real components, off
    // screen. Included in the app class of Main.cpp; --selftest "issues190qr".
    //   #281 a right-click anywhere on a marker's tag gives its menu with FL's Delete (it was only 6 px around its line);
    //        Time markers > Delete removes the markers (all of them without a time selection)
    //   #279 the Channel rack shows one bar of the time signature (3/4: 12 steps, 7/8: 14) and alternates colours per beat
    //   #277 the Playlist and the Piano roll zoom in to FL's 8 px a tick; the deepest zoom draws the samples themselves
    //   #276 the LYNX extra "Zoom around the song position": the wheel keeps the playhead where it is on screen
    //   #274 the LYNX extra "The clip gain handle stays in view"
    juce::String issues190qrCheck (juce::String& log)
    {
        using namespace lynx;
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        const auto motionBefore = ui::motion::enabled();
        ui::motion::enabled() = false;
        juce::String result = "OK";
        const auto check = [&] (const juce::String& what, bool ok) {
            log << (ok ? "ok   " : "FAIL ") << what << "\n";
            if (! ok && result == "OK")
                result = "ERROR " + what;
        };
        const auto mouseOn = [] (juce::Component& target, juce::Point<int> p, juce::ModifierKeys mods) {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, p.toFloat(), now, 1, false);
        };
        const auto bar = (std::int64_t) 4 * ppq;

        // an 8-bar song: a long-named marker at bar 3, a 6-bar audio clip (a 440 Hz sine, 48 kHz) from bar 2 on track 2
        const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-issues190qr-sine.wav");
        {
            wav.deleteFile();
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (new juce::FileOutputStream (wav), 48000.0, 1, 24, {}, 0));
            juce::AudioBuffer<float> buffer (1, 48000 * 12);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, i, 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / 48000.0f));
            if (writer != nullptr)
                writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        }
        auto song = Song::makeDefault();
        song.mode = PlayMode::song;
        song.bpm = 120.0;
        {
            Marker m;
            m.tick = 2 * bar;
            m.name = "A long marker name";
            song.addMarker (m);
            ChannelDef audio;
            audio.name = "Sine";
            audio.instrument = InstrumentKind::audioClip;
            audio.samplePath = wav.getFullPathName().toStdString();
            song.channels.push_back (audio);
            Clip c;
            c.kind = ClipKind::audio;
            c.channel = (int) song.channels.size() - 1;
            c.track = 1;
            c.start = bar;
            c.length = 6 * bar;
            song.playlist.push_back (c);
        }
        AudioEngine offline (false);
        offline.setSong (song);
        MainComponent mainUi (offline, false);
        mainUi.setSize (1920, 1080);
        offline.dispatchPendingMessages();
        auto& pl = mainUi.getPlaylistForSnapshot();

        // ---- #281
        {
            pl.setViewForSnapshot (96.0, 0, 0);
            const auto markerX = (int) std::lround (pl.xOfTickForSnapshot ((double) (2 * bar)));
            const auto items = [] (const juce::PopupMenu& menu, bool deep) {
                juce::StringArray out;
                for (juce::PopupMenu::MenuItemIterator it (menu, deep); it.next();)
                    out.add (it.getItem().text);
                return out;
            };
            const auto onName = items (pl.rulerMenu (markerX + 40, pl.markerTagYForSnapshot()), false);
            check ("#281 a right-click on the marker's name, 40 px from its line, gives its menu with Delete and Rename",
                   onName.contains (tr ("Delete")) && onName.contains (tr ("Rename marker...")));
            const auto delIndex = onName.indexOf (tr ("Delete")), renameIndex = onName.indexOf (tr ("Rename marker..."));
            check ("#281 FL's order: Delete, then Rename...", delIndex >= 0 && delIndex < renameIndex);
            const auto away = items (pl.rulerMenu (markerX + 400, pl.markerTagYForSnapshot()), false);
            check ("#281 away from the tag: no marker items", ! away.contains (tr ("Rename marker...")));
            juce::PopupMenu menu = pl.rulerMenu (markerX + 400, pl.markerTagYForSnapshot());
            bool ran = false;
            for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
                if (it.getItem().text == tr ("Delete") && it.getItem().action != nullptr && it.getItem().isEnabled)
                {
                    it.getItem().action(); // Time markers › Delete (no time selection: every marker)
                    ran = true;
                    break;
                }
            offline.dispatchPendingMessages();
            check ("#281 Time markers > Delete removes the markers", ran && offline.getSong().markers().empty());
        }

        // ---- #279
        {
            auto& rack = mainUi.getRackForSnapshot();
            const auto meter = [&] (int beats, int unit) {
                offline.editSong ([beats, unit] (Song& s) {
                    s.beatsPerBar = beats;
                    s.beatUnit = unit;
                    if (! s.patterns.empty())
                        s.activePattern().lengthTicks = 0;
                });
                offline.dispatchPendingMessages();
            };
            const auto empty = [&] {
                offline.editSong ([] (Song& s) {
                    if (! s.patterns.empty())
                    {
                        s.activePattern().notes.clear();
                        s.activePattern().autoNotes.clear();
                    }
                });
                offline.dispatchPendingMessages();
            };
            empty();
            meter (3, 4);
            check ("#279 3/4: 12 steps (" + juce::String (rack.numStepsForSnapshot()) + ")", rack.numStepsForSnapshot() == 12);
            check ("#279 3/4: groups of 4 alternate", rack.stepOffForSnapshot (3) == rack.stepOffForSnapshot (0) && rack.stepOffForSnapshot (4) != rack.stepOffForSnapshot (0)
                                                         && rack.stepOffForSnapshot (8) == rack.stepOffForSnapshot (0));
            meter (7, 8);
            check ("#279 7/8: 14 steps (" + juce::String (rack.numStepsForSnapshot()) + ")", rack.numStepsForSnapshot() == 14);
            check ("#279 7/8: groups of 2 alternate", rack.stepOffForSnapshot (1) == rack.stepOffForSnapshot (0) && rack.stepOffForSnapshot (2) != rack.stepOffForSnapshot (0)
                                                         && rack.stepOffForSnapshot (4) == rack.stepOffForSnapshot (0));
            meter (6, 4);
            check ("#279 6/4: 24 steps", rack.numStepsForSnapshot() == 24);
            meter (4, 4);
            check ("#279 4/4: 16 steps, groups of 4", rack.numStepsForSnapshot() == 16 && rack.stepOffForSnapshot (4) != rack.stepOffForSnapshot (3));
            // the rack paints at every meter (strips for x/4, step by step for x/8)
            for (const auto [b, u] : { std::pair { 7, 8 }, { 3, 4 }, { 4, 4 } })
            {
                meter (b, u);
                rack.setSize (1200, 400);
                const auto image = rack.createComponentSnapshot (rack.getLocalBounds());
                check ("#279 the rack paints at " + juce::String (b) + "/" + juce::String (u), image.isValid());
            }
        }

        // ---- #277
        {
            pl.setViewForSnapshot (96.0, 0, 0);
            const auto at = juce::Point<int> (pl.headerWidthForSnapshot() + 300, pl.getHeight() / 2);
            juce::MouseWheelDetails up {};
            up.deltaY = 1.0f;
            const auto ctrl = juce::ModifierKeys (juce::ModifierKeys::ctrlModifier);
            for (int i = 0; i < 80; ++i)
                pl.mouseWheelMove (mouseOn (pl, at, ctrl), up);
            const auto deepest = pl.viewForSnapshot().pixelsPerTick;
            check ("#277 the Playlist zooms in to FL's 8 px a tick (" + juce::String (deepest, 4) + ")", std::abs (deepest - maxPixelsPerTick) < 1.0e-9);
            auto& roll = mainUi.getPianoRollForSnapshot();
            roll.setSize (1400, 700);
            for (int i = 0; i < 80; ++i)
                roll.mouseWheelMove (mouseOn (roll, { 400, 300 }, ctrl), up);
            const auto rollDeepest = roll.pixelsPerTickForTests();
            check ("#277 the Piano roll zooms in to FL's 8 px a tick (" + juce::String (rollDeepest, 4) + ")", std::abs (rollDeepest - maxPixelsPerTick) < 1.0e-9);

            // the audio clip at the deepest zoom: it paints, and the samples themselves are drawn (a 440 Hz sine at
            // 120 BPM / 48 kHz is 50 frames a tick: 6.25 frames a pixel, 109 pixels a cycle)
            pl.setScrollTickForSnapshot ((double) (2 * bar));
            const auto image = pl.createComponentSnapshot (pl.getLocalBounds());
            check ("#277 the Playlist paints at the deepest zoom", image.isValid());

            const auto data = offline.getSampleLibrary().load (wav.getFullPathName());
            check ("#277 the sine loads", data != nullptr && data->frames() == 48000u * 12u);
            if (data != nullptr)
            {
                const auto summary = std::make_shared<wave::Summary> (wave::analyse (data->leftData(), nullptr, data->frames(), data->sampleRate));
                auto style = ui::currentWaveStyle (juce::Colours::white);
                style.stereo = false;
                const juce::Rectangle<float> body (0.0f, 0.0f, 400.0f, 100.0f);
                juce::Path envelope, samples;
                ui::appendWaveformPath (envelope, 1.0f, body, body, *summary, 1000.0, 6.25, style);
                style.samples = data;
                ui::appendWaveformPath (samples, 1.0f, body, body, *summary, 1000.0, 6.25, style);
                // the summary's outline is mirrored (as high above the centre as below it everywhere); the samples' line
                // is not: from frame 1000 (a sixth into a cycle) the next 31 frames are all on the positive side, so the
                // first 4 px of the line stay above the centre (y 50)
                juce::Path firstPixels;
                ui::appendWaveformPath (firstPixels, 1.0f, body, body.withWidth (4.0f), *summary, 1000.0, 6.25, style);
                const auto fq = firstPixels.getBounds();
                check ("#277 the deepest zoom draws the samples, not the mirrored outline (" + fq.toString() + ")",
                       ! samples.isEmpty() && ! firstPixels.isEmpty() && fq.getBottom() < 50.0f);
                check ("#277 the summary's outline stays mirrored", std::abs (envelope.getBounds().getCentreY() - 50.0f) < 1.0f);
                // at 16 frames a pixel and more the samples are not looked at (the same path as without them)
                juce::Path wide, wideWithSamples;
                style.samples = nullptr;
                ui::appendWaveformPath (wide, 1.0f, body, body, *summary, 1000.0, 64.0, style);
                style.samples = data;
                ui::appendWaveformPath (wideWithSamples, 1.0f, body, body, *summary, 1000.0, 64.0, style);
                check ("#277 at 64 frames a pixel the drawing is the summary's, as before", wide == wideWithSamples);
            }
        }

        // ---- #276
        {
            const auto before = zoomanchor::atSongPosition();
            workflowextras::setExtra ("editing.zoomAtSongPosition", true);
            pl.setViewForSnapshot (96.0, 0, 0);
            offline.getTransport().positionTicks.store ((double) (3 * bar)); // (what the audio thread does with a seek)
            const auto playX = pl.xOfTickForSnapshot ((double) (3 * bar));
            juce::MouseWheelDetails up {};
            up.deltaY = 1.0f;
            pl.mouseWheelMove (mouseOn (pl, { pl.headerWidthForSnapshot() + 900, pl.getHeight() / 2 }, juce::ModifierKeys (juce::ModifierKeys::ctrlModifier)), up);
            const auto playXAfter = pl.xOfTickForSnapshot ((double) (3 * bar));
            check ("#276 on: the wheel zooms around the song position (x " + juce::String (playX, 1) + " -> " + juce::String (playXAfter, 1) + ")",
                   pl.viewForSnapshot().pixelsPerTick > 96.0 / (double) bar && std::abs (playXAfter - playX) < 1.0);
            workflowextras::setExtra ("editing.zoomAtSongPosition", false);
            pl.setViewForSnapshot (96.0, 0, 0);
            pl.mouseWheelMove (mouseOn (pl, { pl.headerWidthForSnapshot() + 900, pl.getHeight() / 2 }, juce::ModifierKeys (juce::ModifierKeys::ctrlModifier)), up);
            check ("#276 off (FL): around the mouse, the song position moves", std::abs (pl.xOfTickForSnapshot ((double) (3 * bar)) - playX) > 5.0);
            workflowextras::setExtra ("editing.zoomAtSongPosition", before);
        }

        // ---- #274
        {
            const auto before = cliphandle::gainFollowsView();
            int clip = -1;
            for (int i = 0; i < (int) offline.getSong().playlist.size(); ++i)
                if (offline.getSong().playlist[(std::size_t) i].kind == ClipKind::audio)
                    clip = i;
            // 1600 px a bar, scrolled so that the view ends 1.5 bars into the clip (bars 2-7): its middle (bar 5) is far
            // to the right, out of view
            pl.setViewForSnapshot (1600.0, 0, 0);
            const auto width = pl.getWidth() - pl.headerWidthForSnapshot();
            pl.setScrollTickForSnapshot ((double) bar + 1.5 * (double) bar - (double) width / (1600.0 / (double) bar));
            workflowextras::setExtra ("playlist.gainHandleFollowsView", false);
            const auto fl = pl.gainHandleForSnapshot (clip);
            workflowextras::setExtra ("playlist.gainHandleFollowsView", true);
            const auto follows = pl.gainHandleForSnapshot (clip);
            check ("#274 off (FL): the handle is in the clip's middle, out of view (" + fl.toString() + ")", ! fl.isEmpty() && fl.getX() > (float) pl.getWidth());
            check ("#274 on: the handle is in the middle of the part that shows (" + follows.toString() + ")",
                   ! follows.isEmpty() && follows.getRight() < (float) pl.getWidth() && follows.getX() > (float) pl.headerWidthForSnapshot());
            pl.setViewForSnapshot (96.0, 0, 0); // (the whole clip in view: FL's place either way)
            check ("#274 on, the middle in view: FL's place", pl.gainHandleForSnapshot (clip) == [&] {
                workflowextras::setExtra ("playlist.gainHandleFollowsView", false);
                const auto r = pl.gainHandleForSnapshot (clip);
                workflowextras::setExtra ("playlist.gainHandleFollowsView", true);
                return r;
            }());
            workflowextras::setExtra ("playlist.gainHandleFollowsView", before);
        }

        ui::motion::enabled() = motionBefore;
        wav.deleteFile();
        return result;
    }

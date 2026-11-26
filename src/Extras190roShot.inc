    // --extras190roshot <prefix> (round 190ro, LYNX extras review 2, the owner's decisions of 11-26): off screen, at 100 % and
    // @150, and the checks behind the round's three default changes. <prefix>-checks.txt lists them (result=OK / FAIL).
    // An empty prefix writes no picture (--selftest item "extras190ro"). Included in the app class of Main.cpp.
    //   ON-1  "The clip gain handle stays in view" on: with the clip's middle out of view the half circle sits in the middle
    //         of the part that shows; a press on it drags the gain, a press on the bottom edge beside it moves the clip, as
    //         FL does there (FL shows no handle at all in that view); switched off it is FL's place
    //   OFF-1 the Mixer's peak values and correlation bars off: the faders and meters at FL's height again
    bool runExtras190roShot (const juce::String& prefix, juce::String* selftestResult = nullptr)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        const auto motionBefore = ui::motion::enabled();
        ui::motion::enabled() = false;
        juce::StringArray checks;
        bool allOk = true;
        const auto check = [&checks, &allOk] (const juce::String& what, bool ok) {
            checks.add ((ok ? "ok   " : "FAIL ") + what);
            allOk = allOk && ok;
        };
        const auto both = [&prefix] (juce::Component& c, juce::Rectangle<int> area, const juce::String& name) {
            if (prefix.isEmpty())
                return;
            area = area.getIntersection (c.getLocalBounds());
            if (area.isEmpty())
                return;
            savePng (c.createComponentSnapshot (area, true, 1.0f), prefix + "-" + name + ".png");
            savePng (c.createComponentSnapshot (area, true, 1.5f), prefix + "-" + name + "@150.png");
        };
        AudioEngine engine (false);
        const auto pump = [&engine] (int ms) {
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
            while (juce::Time::getMillisecondCounter() < end)
            {
                engine.dispatchPendingMessages();
#if JUCE_LINUX || JUCE_WINDOWS
                for (int i = 0; i < 64 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i) {}
#endif
                juce::Thread::sleep (5);
            }
        };
        // a mouse event on `target` at `p` whose button went down at `down`
        const auto event = [] (juce::Component& target, juce::Point<float> p, juce::Point<float> down, juce::ModifierKeys mods) {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, down, now, 1, false);
        };
        const auto bar = (std::int64_t) 4 * ppq;

        // --- the defaults of a new profile ----------------------------------------------------------------------------------
        check ("new profile: the gain handle stays in view (on), the Mixer's peak values and correlation bars (off)",
               cliphandle::gainFollowsView() && ! MixerStrips::showPeakValues && ! MixerStrips::showCorrelation
                   && extras::defaultOn ("playlist.gainHandleFollowsView") && ! extras::defaultOn ("mixer.peakValues") && ! extras::defaultOn ("mixer.correlation"));
        check ("FL-exact turns the three off", ! extras::flExactOn ("playlist.gainHandleFollowsView") && ! extras::flExactOn ("mixer.peakValues")
                                                   && ! extras::flExactOn ("mixer.correlation"));

        // --- ON-1: the gain handle, a press on it and a press beside it -----------------------------------------------------
        {
            const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-extras190ro-take.wav");
            {
                wav.deleteFile();
                juce::WavAudioFormat format;
                std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (new juce::FileOutputStream (wav), 48000.0, 1, 24, {}, 0));
                juce::AudioBuffer<float> buffer (1, 48000 * 12);
                for (int i = 0; i < buffer.getNumSamples(); ++i) // (a plucked 220 Hz note every half second: a waveform to see)
                {
                    const auto t = (float) (i % 24000) / 48000.0f;
                    buffer.setSample (0, i, 0.6f * std::exp (-5.0f * t) * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) i / 48000.0f));
                }
                if (writer != nullptr)
                    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
            }
            auto song = Song::makeDefault();
            song.mode = PlayMode::song;
            song.bpm = 120.0;
            ChannelDef take;
            take.name = "Guitar take";
            take.instrument = InstrumentKind::audioClip;
            take.samplePath = wav.getFullPathName().toStdString();
            take.colour = 0xff4f8fc0;
            song.channels.push_back (take);
            Clip c;
            c.kind = ClipKind::audio;
            c.channel = (int) song.channels.size() - 1;
            c.track = 1;
            c.start = bar;
            c.length = 6 * bar; // bars 2-7: at 1600 px a bar its middle (bar 5) is far out of view
            song.playlist.push_back (c);
            const auto clipIndex = (int) song.playlist.size() - 1;
            engine.setSong (song);
            MainComponent mainUi (engine, false);
            mainUi.setSize (1920, 1080);
            engine.dispatchPendingMessages();
            auto& pl = mainUi.getPlaylistForSnapshot();
            ui::WaveformCache::getInstance()->find (wav.getFullPathName(), [&engine, &wav] { return engine.getSampleLibrary().load (wav.getFullPathName()); });
            ui::WaveformCache::getInstance()->waitUntilIdle (20000);
            const auto width = pl.getWidth() - pl.headerWidthForSnapshot();
            // the view: 1600 px a bar, scrolled so that it ends 1.5 bars into the clip
            const auto view = [&] {
                pl.setViewForSnapshot (1600.0, 0, 0);
                pl.setScrollTickForSnapshot ((double) bar + 1.5 * (double) bar - (double) width / (1600.0 / (double) bar));
                pl.repaint();
                pump (150);
            };
            const auto clipNow = [&engine, clipIndex] { return engine.getSong().playlist[(std::size_t) clipIndex]; };
            const auto restore = [&engine, &song] {
                engine.setSong (song);
                engine.dispatchPendingMessages();
            };
            const auto left = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
            const auto press = [&] (juce::Point<float> at, juce::Point<float> by) {
                pl.mouseDown (event (pl, at, at, left));
                for (int k = 1; k <= 8; ++k)
                    pl.mouseDrag (event (pl, at + by * ((float) k / 8.0f), at, left));
                pl.mouseUp (event (pl, at + by, at, juce::ModifierKeys()));
                engine.dispatchPendingMessages();
                pump (80);
            };
            const auto rows = [&pl] { return juce::Rectangle<int> (0, 0, pl.getWidth(), pl.trackRowForSnapshot (2).getBottom() + 6); };

            view();
            const auto handle = pl.gainHandleForSnapshot (clipIndex);
            const auto clipBox = pl.clipBoundsForSnapshot (clipIndex).toFloat();
            check ("ON-1 on: the half circle sits in the middle of the clip's visible part (" + handle.toString() + ", clip " + clipBox.toString() + ")",
                   ! handle.isEmpty() && handle.getRight() < (float) pl.getWidth() && handle.getX() > (float) pl.headerWidthForSnapshot()
                       && std::abs (handle.getCentreX() - (juce::jmax (clipBox.getX(), (float) pl.headerWidthForSnapshot()) + (float) pl.getWidth()) * 0.5f) < 2.0f);
            both (pl, rows(), "on1-handle-in-view");

            // a press on the half circle and a drag up: the clip's gain changes, the clip stays where it is
            const auto before = clipNow();
            press (handle.getCentre(), { 0.0f, -24.0f });
            check ("ON-1 on: a press on the half circle drags the gain (gain " + juce::String (before.gain, 3) + " -> " + juce::String (clipNow().gain, 3) + ")",
                   clipNow().gain != before.gain && clipNow().start == before.start && clipNow().track == before.track);
            restore();
            view();

            // a press on the bottom edge 4 px beside the half circle's hit area, dragged right: the clip moves, the gain stays
            // (FL 2026 in this view has no handle at all, and the same press moves the clip)
            const auto beside = juce::Point<float> (handle.getRight() + 2.0f + 4.0f, handle.getCentreY());
            checks.add ("     press beside the half circle at " + beside.toString() + " (half circle " + handle.toString() + ")");
            both (pl, rows(), "on1-before-press-beside");
            press (beside, { 240.0f, 0.0f });
            check ("ON-1 on: a press on the bottom edge beside it moves the clip, as FL (start " + juce::String (before.start) + " -> "
                       + juce::String (clipNow().start) + ", gain " + juce::String (clipNow().gain, 3) + ")",
                   clipNow().start > before.start && clipNow().gain == before.gain);
            both (pl, rows(), "on1-after-press-beside");
            restore();
            view();

            // the other side of the handle too
            const auto otherSide = juce::Point<float> (handle.getX() - 2.0f - 4.0f, handle.getCentreY());
            press (otherSide, { 240.0f, 0.0f });
            check ("ON-1 on: and on its other side", clipNow().start > before.start && clipNow().gain == before.gain);
            restore();
            view();

            // switched off (FL-exact): the handle is in the clip's middle, out of view, and a press where it was moves the clip
            workflowextras::setExtra ("playlist.gainHandleFollowsView", false);
            view();
            const auto fl = pl.gainHandleForSnapshot (clipIndex);
            check ("ON-1 off (FL): the half circle is in the clip's middle, out of view (" + fl.toString() + ")", ! fl.isEmpty() && fl.getX() > (float) pl.getWidth());
            both (pl, rows(), "on1-off-fl");
            press (handle.getCentre(), { 240.0f, 0.0f });
            check ("ON-1 off (FL): a press where the LYNX handle was moves the clip", clipNow().start > before.start && clipNow().gain == before.gain);
            restore();
            workflowextras::setExtra ("playlist.gainHandleFollowsView", true);

            // the clip's middle in view: FL's place, on or off
            pl.setViewForSnapshot (96.0, 0, 0);
            pump (100);
            const auto onPlace = pl.gainHandleForSnapshot (clipIndex);
            workflowextras::setExtra ("playlist.gainHandleFollowsView", false);
            const auto offPlace = pl.gainHandleForSnapshot (clipIndex);
            workflowextras::setExtra ("playlist.gainHandleFollowsView", true);
            check ("ON-1: with the clip's middle in view the half circle is where FL has it, on or off", onPlace == offPlace && ! onPlace.isEmpty());
            both (pl, rows(), "on1-middle-in-view");
            wav.deleteFile();
        }

        // --- OFF-1: the Mixer's faders and meters at FL's height ------------------------------------------------------------
        {
            engine.setSong (Song::makeDefault());
            engine.dispatchPendingMessages();
            juce::Component holder;
            holder.setSize (1400, 560);
            MixerComponent mixer (engine);
            mixer.setBounds (holder.getLocalBounds());
            holder.addAndMakeVisible (mixer);
            pump (200);
            auto& strips = mixer.getStripsForSnapshot();
            const auto fader = strips.faderAreaForSnapshot (1);
            check ("OFF-1: no peak value box and no correlation bar by default",
                   strips.peakValueAreaForSnapshot (1).isEmpty() && strips.correlationAreaForSnapshot (1).isEmpty() && ! engine.getMeters().correlationWanted.load());
            both (holder, holder.getLocalBounds(), "off1-mixer-default");
            strips.setShowPeakValues (true);
            strips.setShowCorrelation (true);
            pump (100);
            const auto shorter = strips.faderAreaForSnapshot (1);
            check ("OFF-1: switched on, the fader is " + juce::String (fader.getHeight() - shorter.getHeight()) + " px shorter (19 = 13 + 6)",
                   fader.getHeight() - shorter.getHeight() == MixerStrips::peakRowHeight + MixerStrips::correlationRowHeight);
            both (holder, holder.getLocalBounds(), "off1-mixer-switched-on");
            strips.setShowPeakValues (false);
            strips.setShowCorrelation (false);
            pump (100);
            check ("OFF-1: switched off again, FL's height", strips.faderAreaForSnapshot (1) == fader);
        }

        ui::motion::enabled() = motionBefore;
        if (prefix.isNotEmpty())
            juce::File (prefix + "-checks.txt").replaceWithText (checks.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        if (selftestResult != nullptr)
        {
            *selftestResult = "OK";
            for (const auto& line : checks)
                if (line.startsWith ("FAIL"))
                {
                    *selftestResult = "ERROR " + line.substring (5);
                    break;
                }
        }
        return allOk;
    }

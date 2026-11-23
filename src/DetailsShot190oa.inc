    // --details190oashot <prefix> <project.flp> (round 190oa, details pass 11): the audit's last open rows — FL's main
    // (monitor) volume, polyphony as voices being mixed, what starts a Wait for input, and the plugin-editor rows.
    // <prefix>-checks.txt lists the checks. Included in the app class of Main.cpp.
    bool runDetailsShot190oa (const juce::String& prefix, const juce::String& project)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (lynx::ui::showingForShots(), true);
        juce::String checks;
        bool allOk = true;
        const auto check = [&checks, &allOk] (const juce::String& what, bool ok) {
            checks << (ok ? "ok   " : "FAIL ") << what << "\n";
            allOk = allOk && ok;
        };
        const auto line = [&checks] (const juce::String& what) { checks << "     " << what << "\n"; };
        const auto write = [&prefix] (juce::Component& c, juce::Rectangle<int> area, const juce::String& name) {
            area = area.getIntersection (c.getLocalBounds());
            if (area.isEmpty())
                return;
            savePng (c.createComponentSnapshot (area, true, 1.0f), prefix + "-" + name + ".png");
            savePng (c.createComponentSnapshot (area, true, 1.5f), prefix + "-" + name + "@150.png");
        };
        using namespace lynx;
        if (project.isEmpty())
            return false;
        flsettings::reset();
        AudioEngine offline (false);
        MainComponent ui (offline, false);
        ui.setSize (1920, 1080);
        ui.openFile (juce::File (project));
        offline.dispatchPendingMessages();

        // a stand-in audio device: the engine's own callback, block by block (as --hostplay190ll)
        struct Device final : juce::AudioIODevice
        {
            Device() : juce::AudioIODevice ("details190oa", "test") {}
            juce::StringArray getOutputChannelNames() override { return { "L", "R" }; }
            juce::StringArray getInputChannelNames() override { return {}; }
            juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
            juce::Array<int> getAvailableBufferSizes() override { return { 512 }; }
            int getDefaultBufferSize() override { return 512; }
            juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
            void close() override {}
            bool isOpen() override { return true; }
            void start (juce::AudioIODeviceCallback*) override {}
            void stop() override {}
            bool isPlaying() override { return true; }
            juce::String getLastError() override { return {}; }
            int getCurrentBufferSizeSamples() override { return 512; }
            double getCurrentSampleRate() override { return 48000.0; }
            int getCurrentBitDepth() override { return 32; }
            juce::BigInteger getActiveOutputChannels() const override
            {
                juce::BigInteger b;
                b.setRange (0, 2, true);
                return b;
            }
            juce::BigInteger getActiveInputChannels() const override { return {}; }
            int getOutputLatencyInSamples() override { return 0; }
            int getInputLatencyInSamples() override { return 0; }
        } device;
        offline.deviceCallback().audioDeviceAboutToStart (&device);
        // the audio checks run on an engine made as the device tests make theirs (--countinrectest): its own callback
        // driven block by block (the offline engine above renders no device blocks)
        auto liveEngine = std::make_unique<AudioEngine>();
        liveEngine->getDeviceManager().closeAudioDevice();
        liveEngine->setSong (offline.getSong());
        liveEngine->deviceCallback().audioDeviceAboutToStart (&device);
        for (int i = 0; i < 100; ++i) // (the rack is published asynchronously; 2 s, as --hostplay190ll waits)
        {
            liveEngine->dispatchPendingMessages();
            juce::Thread::sleep (20);
        }
        auto& live = *liveEngine;
        std::vector<float> outL (512), outR (512);
        // runs `blocks` device blocks of the live engine; the output's peak
        const auto run = [&] (int blocks) {
            float* outs[] = { outL.data(), outR.data() };
            juce::AudioIODeviceCallbackContext context;
            float peak = 0.0f;
            for (int b = 0; b < blocks; ++b)
            {
                live.deviceCallback().audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, context);
                for (int i = 0; i < 512; ++i)
                    peak = std::max ({ peak, std::abs (outL[(std::size_t) i]), std::abs (outR[(std::size_t) i]) });
            }
            return peak;
        };

        // ---- FL's main (monitor) volume (DF-21; REA 190oa R2): on what the device plays, never in an export
        {
            const auto masterBefore = offline.getSong().mixer.front().volume;
            // a held note of a built-in synth (a steady tone, the transport stopped): its level at 0 dB, then at 50 %
            int tone = -1;
            for (int c = 0; c < (int) live.getSong().channels.size() && tone < 0; ++c)
                if (const auto k = live.getSong().channels[(std::size_t) c].instrument; k == InstrumentKind::threeosc || k == InstrumentKind::synth)
                    tone = c;
            live.stop();
            live.setMainVolume (unityFader);
            if (tone >= 0)
                live.sendLiveNote (tone, 57, 0.8f, true);
            run (60); // (past the attack)
            const auto unity = run (40);
            live.setMainVolume (0.5f);
            run (4); // (the gain glides over one block)
            const auto half = run (40);
            if (tone >= 0)
                live.sendLiveNote (tone, 57, 0.0f, false);
            for (int k = 0; k < 400 && live.voicesSounding() > 0; ++k)
                run (1);
            live.setMainVolume (unityFader);
            const auto want = faderGain (0.5f);
            line ("device peaks: main volume 0 dB " + juce::String (unity, 4) + ", at 50 % " + juce::String (half, 4) + " (gain "
                  + juce::String (want, 4) + ")");
            check ("main volume 50 %: the device output is that much lower (FL's (11^v - 1) / 10)",
                   unity > 0.01f && std::abs (half / unity - want) < 0.05f);
            check ("main volume leaves the Master insert's fader alone", offline.getSong().mixer.front().volume == masterBefore);
            // an export: the same with any main volume
            auto song = offline.getSong();
            song.loopStart = 0;
            song.loopEnd = 4 * ppq;
            const auto render = [&song] (float mainVolume) {
                auto s = song;
                s.mainVolume = mainVolume;
                exportopts::Options o;
                o.range = exportopts::Range::selection;
                o.tail = exportopts::TailMode::cut;
                o.wavBits = 32;
                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx190oa-mainvol.wav");
                juce::String error;
                ExportJob job (s, o, file);
                if (! job.run ([] (double) { return true; }, error))
                    return -1.0;
                double peak = 0.0;
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr)
                {
                    juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
                    reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        peak = juce::jmax (peak, (double) buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
                }
                file.deleteFile();
                return peak;
            };
            const auto exportUnity = render (unityFader), exportLow = render (0.2f);
            line ("export peaks: main volume 0 dB " + juce::String (exportUnity, 5) + ", at 20 % " + juce::String (exportLow, 5));
            check ("an export ignores the main volume (FL: \"not reflected in renders\")", exportUnity > 0.01 && std::abs (exportUnity - exportLow) < 1.0e-6);
            if (auto* knob = dynamic_cast<toolbar::MasterKnob*> (ui.toolbarPartForSnapshot ("masterVolume")))
            {
                write (ui, knob->getBounds().expanded (120, 6).withY (0).withHeight (juce::jmax (40, knob->getBottom() + 6)), "toolbar-main-volume");
                check ("the toolbar knob shows the main volume, not the Master fader (FL look)", knob->monitor());
                const auto hint = knob->hintAt ({});
                line ("knob hint: " + hint.text + " | " + hint.value);
                check ("the main volume knob is not automatable (FL)", ! hint.automatable);
            }
            offline.setMainVolume (unityFader);
            offline.markSaved();
        }

        // ---- polyphony = voices being mixed (DF-29; REA 190oa R1): a released note counts while its tail sounds
        {
            live.stop();
            const auto& song = live.getSong();
            int channel = -1;
            for (int c = 0; c < (int) song.channels.size() && channel < 0; ++c) // (a built-in synth: Thief's drums are audio clips)
                if (const auto k = song.channels[(std::size_t) c].instrument; k == InstrumentKind::threeosc || k == InstrumentKind::synth || k == InstrumentKind::sampler)
                    channel = c;
            for (int k = 0; k < 600 && live.voicesSounding() > 0; ++k) // (the song's tails die away first)
                run (1);
            const auto idle = live.voicesSounding();
            if (channel >= 0)
            {
                live.sendLiveNote (channel, 60, 0.8f, true);
                live.sendLiveNote (channel, 64, 0.8f, true);
                run (2);
                const auto held = live.voicesSounding();
                live.sendLiveNote (channel, 60, 0.0f, false);
                live.sendLiveNote (channel, 64, 0.0f, false);
                run (1);
                const auto released = live.voicesSounding();
                int blocks = 0;
                while (live.voicesSounding() > 0 && blocks < 2000)
                {
                    run (1);
                    ++blocks;
                }
                line ("polyphony: idle " + juce::String (idle) + ", two keys held " + juce::String (held) + ", just released " + juce::String (released)
                      + ", back to 0 after " + juce::String (blocks) + " blocks of 512 (" + juce::String (blocks * 512.0 / 48.0, 0) + " ms) on "
                      + juce::String::fromUTF8 (song.channels[(std::size_t) channel].name.c_str()));
                check ("polyphony: two held keys = 2 voices", idle == 0 && held == 2);
                check ("polyphony: the released voices count while they still sound, then drop to 0 (FL)", released == 2 && blocks < 2000);
            }
            else
                check ("polyphony: a sample channel to play", false);
        }

        // ---- what starts a Wait for input (DF-10; REA 190oa R4, FL's FUN_011c9ab0)
        {
            offline.stop();
            auto options = offline.getRecordOptions();
            const auto saved = options;
            options.waitForInput = true;
            options.waitOnlyWhenRecording = false;
            options.waitRespectFilter = false;
            offline.setRecordOptions (options);
            const AutomationTarget fader { AutomationTargetKind::mixerVolume, 1 };
            offline.startPlayback();
            const auto waiting = offline.isWaitingForInput() && ! offline.getTransport().playing.load();
            offline.noteTweaked (fader);
            check ("Wait for input: a moved control starts playback (FL's automation input)", waiting && offline.getTransport().playing.load());
            offline.stop();
            offline.startPlayback();
            offline.inputArrived (AudioEngine::WaitInput::tapper);
            check ("Wait for input: a tempo tapper tap with Sync starts it (FL)", offline.getTransport().playing.load());
            offline.stop();
            // Respect recording filter while recording: only the kinds it records (Audio in the filter: any)
            options.waitRespectFilter = true;
            options.recordAutomation = false;
            options.recordAudio = false;
            options.recordNotes = true;
            offline.setRecordOptions (options);
            offline.setRecording (true);
            offline.startPlayback();
            offline.noteTweaked (fader);
            const auto blocked = offline.isWaitingForInput() && ! offline.getTransport().playing.load();
            offline.inputArrived (AudioEngine::WaitInput::notes);
            check ("Respect recording filter: no Automation in the filter = a moved control waits on; a note starts it",
                   blocked && offline.getTransport().playing.load());
            offline.stop();
            options.recordAudio = true;
            offline.setRecordOptions (options);
            offline.startPlayback();
            offline.noteTweaked (fader);
            check ("Respect recording filter with Audio in the filter: any input starts it (FL)", offline.getTransport().playing.load());
            offline.stop();
            offline.setRecording (false);
            offline.setRecordOptions (saved);
        }
        // ---- Live's Re-Enable Automation (DL-58, capture 190oa L1): in the Clip-launch layout a moved automated control
        //      stays overridden across Play until Re-Enable; Pattern keeps FL's rule (Play hands it back)
        if (! offline.getSong().automations.empty())
        {
            const auto target = offline.getSong().automations.front().target;
            offline.stop();
            offline.setLiveAutomationOverride (true);
            offline.noteTweaked (target);
            const auto lit = offline.automationOverridden();
            offline.play();
            run (2);
            offline.dispatchPendingMessages();
            offline.timerCallbackForTests();
            const auto keptAcrossPlay = offline.automationOverridden();
            offline.reEnableAutomation();
            const auto cleared = ! offline.automationOverridden();
            offline.stop();
            offline.setLiveAutomationOverride (false);
            line ("override: lit " + juce::String ((int) lit) + ", after Play " + juce::String ((int) keptAcrossPlay) + ", after Re-Enable cleared " + juce::String ((int) cleared));
            check ("Live's Re-Enable Automation: lit by a moved automated control, kept across Play, cleared by Re-Enable", lit && keptAcrossPlay && cleared);
        }
        offline.deviceCallback().audioDeviceStopped();
        live.deviceCallback().audioDeviceStopped();

        // ---- FL's number display: digits typed while the button is held (DF-15, capture 190oa A7)
        if (auto* tempo = ui.toolbarPartForSnapshot ("tempo"))
        {
            const auto before = offline.getSong().bpm;
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const juce::Point<float> at { 6.0f, (float) tempo->getHeight() * 0.5f };
            const juce::MouseEvent press (source, at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, tempo, tempo,
                                          now, at, now, 1, false);
            tempo->mouseDown (press);
            for (const auto c : { '1', '2', '3' })
                tempo->keyPressed (juce::KeyPress (c, {}, (juce::juce_wchar) c));
            const auto held = offline.getSong().bpm;
            tempo->mouseUp (press);
            const auto typedAfterRelease = tempo->keyPressed (juce::KeyPress ('9', {}, (juce::juce_wchar) '9'));
            line ("tempo " + juce::String (before, 3) + " -> held + \"123\" = " + juce::String (held, 3));
            check ("tempo display: \"123\" typed with the button held = 123 BPM (FL)", std::abs (held - 123.0) < 1.0e-9);
            check ("tempo display: after the release the keys are not its (FL: they play the typing keyboard)", ! typedAfterRelease);
            offline.undo();
            offline.dispatchPendingMessages();
        }

        // ---- the hint bar's FL status icons (DF-45, capture 190oa A3)
        {
            auto& bar = ui.getHintBarForSnapshot();
            const auto barBefore = bar.getBounds();
            bar.setSize (juce::jmax (760, bar.getWidth()), juce::jmax (40, bar.getHeight()));
            ui::Hint waiting { tr ("Waiting for input: a played note or a moved control starts it ([Space] cancels)"), {}, false, false };
            waiting.mark = ui::Hint::Mark::warning;
            bar.setHint (waiting, false);
            write (bar, bar.getLocalBounds(), "hint-waiting");
            ui::Hint note { tr ("Note on: {0} / velocity: {1}", { "C5", "100" }), {}, false, false };
            note.mark = ui::Hint::Mark::note;
            bar.setHint (note, false);
            write (bar, bar.getLocalBounds(), "hint-note");
            check ("hint bar keeps the mark it is given", bar.getHint().mark == ui::Hint::Mark::note);
            bar.setBounds (barBefore);
        }

        // ---- the Mixer track EQ's FL gestures (DF-86, capture 190oa A5)
        {
            ui.selectTabForSnapshot (3);
            offline.dispatchPendingMessages();
            auto& mixer = ui.getMixerForSnapshot();
            mixer.selectTrack (1);
            offline.dispatchPendingMessages();
            auto& panel = mixer.getEffectsForSnapshot();
            const auto eqOf = [&offline] { return offline.getSong().mixer[1].eq; };
            const auto before = eqOf();
            const auto orange = panel.eqGestureForSnapshot (1, 30, 0);
            const auto afterDrag = eqOf();
            panel.eqGestureForSnapshot (1, 0, 3);
            const auto afterWheel = eqOf();
            line ("EQ band 2: level " + juce::String (before.gainDb[1], 1) + " -> right-drag " + juce::String (afterDrag.gainDb[1], 1) + " dB (freq "
                  + juce::String (before.hz[1], 0) + " -> " + juce::String (afterDrag.hz[1], 0) + " Hz, width " + juce::String (before.width[1], 3) + " -> "
                  + juce::String (afterDrag.width[1], 3) + ") -> 3 wheel notches " + juce::String (afterWheel.gainDb[1], 1) + " dB");
            check ("EQ right-drag: the band's level alone (FL), the curve orange while it drags",
                   orange && afterDrag.gainDb[1] > before.gainDb[1] + 1.0f && afterDrag.hz[1] == before.hz[1] && afterDrag.width[1] == before.width[1]);
            check ("EQ wheel: the level, 0.8 dB a notch (FL)", std::abs (afterWheel.gainDb[1] - juce::jmin (18.0f, afterDrag.gainDb[1] + 2.4f)) < 0.051f
                                                                  && afterWheel.width[1] == afterDrag.width[1]);
            write (mixer, mixer.getLocalBounds(), "mixer-eq");
            offline.editSong ([before] (Song& s) { s.mixer[1].eq = before; });
            ui.selectTabForSnapshot (0);
        }

        // ---- windows keep their place with the project (DF-130; REA 190oa R3, FL's wrapper records)
        {
            windowplaces::all().clear();
            windowplaces::all()["ch:42"] = { { 300, 200, 640, 420 }, true };
            windowplaces::all()["fx:1:0"] = { { 900, 150, 500, 380 }, false };
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx190oa-places.lynx");
            juce::String error;
            ProjectFile::WindowLayout layout;
            layout.places = windowplaces::toVar();
            const auto saved = ProjectFile::save (offline.getSong(), file, error, layout);
            ProjectFile::WindowLayout back;
            const auto loaded = ProjectFile::load (file, error, nullptr, &back);
            windowplaces::fromVar (back.places);
            const auto& places = windowplaces::all();
            check ("window places saved with the project and read back (FL keeps them in the .flp)",
                   saved && loaded.has_value() && places.size() == 2 && places.count ("ch:42") && places.at ("ch:42").bounds == juce::Rectangle<int> (300, 200, 640, 420)
                       && places.at ("ch:42").open && ! places.at ("fx:1:0").open);
            file.deleteFile();
            windowplaces::all().clear();
        }

        // ---- the plugin editors this round touched (DP-24/25 Limiter, DP-27 Maximus, DP-8 Gross Beat, DP-44 Love
        //      Philter, DP-76 dB Meter), on insert 1's free slots; DF-120 / DF-109 the Browser's Current project
        {
            const auto track = 1;
            const auto base = (int) offline.getSong().mixer[(std::size_t) track].effects.size();
            offline.editSong ([track] (Song& s) {
                auto& fx = s.mixer[(std::size_t) track].effects;
                for (const auto k : { EffectKind::fruityLimiter, EffectKind::maximus, EffectKind::grossBeat, EffectKind::lovePhilter, EffectKind::pitchCorrect,
                                      EffectKind::vocoder, EffectKind::waveCandy })
                    fx.push_back (makeEffectSlot (k));
                auto& meter = fx.back();
                meter.flOrigin.plugin = "Fruity dB Meter";
                meter.params[(std::size_t) visualfx::wavecandy::mode] = visualfx::wavecandy::modeValue (visualfx::wavecandy::Mode::peakMeter);
                fx[fx.size() - 3].params[(std::size_t) grossbeat::scratchParam] = 0.3f; // (the dial turned back, as while scratching)
            });
            offline.dispatchPendingMessages();
            const char* names[] { "limiter", "maximus", "grossbeat", "lovephilter", "pitcher", "vocoder" };
            for (int k = 0; k < 6; ++k)
                if (auto editor = makeNativeEffectEditor (offline, track, base + k))
                {
                    editor->setSize (juce::jmax (200, editor->getWidth()), juce::jmax (150, editor->getHeight()));
                    write (*editor, editor->getLocalBounds(), juce::String ("editor-") + names[k]);
                }
            if (auto meter = makeVisualEditor (offline, track, base + 6))
            {
                meter->setSize (juce::jmax (240, meter->getWidth()), juce::jmax (320, meter->getHeight()));
                write (*meter, meter->getLocalBounds(), "editor-dbmeter");
            }
            check ("Gross Beat's 9th parameter: the scratch offset, 0 = none (old projects)", makeEffectSlot (EffectKind::grossBeat).params[(std::size_t) grossbeat::scratchParam] == 0.0f);
            // the Browser: Current project › Effects, insert 1's Fruity Limiter opened on its parameters (Browse parameters)
            auto& browser = ui.getBrowserForSnapshot();
            browser.setSize (360, 900);
            const auto revealed = browser.revealProjectParameters (true, track, base);
            write (browser, browser.getLocalBounds(), "browser-parameters");
            check ("Browse parameters: the Browser opens Current project › Effects on the plugin's parameters (FL)", revealed);
            offline.editSong ([track, base] (Song& s) { s.mixer[(std::size_t) track].effects.resize ((std::size_t) base); });
        }

        // ---- Live's control bar with Automation Arm / Re-Enable Automation (DL-58), Clip-launch layout
        {
            ui.setLayoutStyle (layoutstyle::Style::clipLaunch);
            offline.dispatchPendingMessages();
            ui.resized();
            if (auto* arm = ui.toolbarPartForSnapshot ("automationArm"))
            {
                check ("Live's control bar shows Automation Arm and Re-Enable Automation", arm->isVisible()
                                                                                             && ui.toolbarPartForSnapshot ("reEnableAutomation")->isVisible());
                write (ui, ui.getToolbarBoundsForSnapshot(), "live-control-bar");
                const auto r = arm->getBounds().getUnion (ui.toolbarPartForSnapshot ("reEnableAutomation")->getBounds()).expanded (40, 4);
                write (ui, r, "live-auto-arm");
            }
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.dispatchPendingMessages();
        }

        juce::File (prefix + "-checks.txt").replaceWithText (checks + "result=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

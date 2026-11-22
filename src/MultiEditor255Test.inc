    // LYNX --multieditor255test report.txt <plugin>[::Name] ... (round 190ou, issue #255: "2 or more plugin editors open and
    // used: the audio goes silent, then LYNX crashes"). The plugins go into one song (instruments: a channel each, into
    // insert 1; effects: insert 1's slots), every editor is opened, and for `LYNX_ME_SECONDS` (default 45):
    //  - an audio thread runs the engine's device callback in real time (512 frames at 48 kHz) and records each
    //    callback's duration and output level;
    //  - a MIDI thread plays a keyboard into the engine (injectMidiInput: notes, a sustain pedal, pitch bend), the
    //    selected channel moving from one instrument to the next every 2 s;
    //  - the message thread "uses" the editors: on X11, XTest mouse moves and drags over every editor window (plugins in
    //    LYNX's own process), and parameter changes from the UI (setValueNotifyingHost) on every plugin, 20 times a second.
    // The audio must keep running: no callback over 250 ms and no second-long stretch without sound while the keyboard
    // plays (after the first 3 s). LYNX_ME_SEPARATE=1: every plugin in a separate process; LYNX_ME_DEVICE=1: the real
    // audio device instead of the timed thread (Linux: PipeWire when its daemon answers). Included in the app class.
    void runMultiEditor255Test (const juce::String& reportPath, const juce::StringArray& pluginArgs)
    {
        struct Device final : juce::AudioIODevice
        {
            Device() : juce::AudioIODevice ("multieditor255", "test") {}
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
        };

        struct Test final : juce::Timer
        {
            struct Unit
            {
                juce::PluginDescription type;
                int channel = -1, slot = -1; // an instrument's channel / an effect's slot on insert 1
                juce::AudioPluginInstance* instance = nullptr;
                std::shared_ptr<void> owner;
            };
            juce::String reportPath, report;
            std::unique_ptr<lynx::AudioEngine> engine;
            std::vector<Unit> units;
            Device device;
            std::unique_ptr<juce::DocumentWindow> mainWindow;
            std::function<void (bool)> done;
            int phase = 0, seconds = 45;
            juce::uint32 phaseStart = 0;
            juce::Random random { 255 };

            // the audio thread
            std::thread audio, midi;
            std::atomic<bool> running { false }, playing { false };
            std::atomic<int> callbacks { 0 }, selectedFrom { -1 };
            std::atomic<double> worstMs { 0.0 };
            std::atomic<float> secondPeak { 0.0f };
            std::atomic<std::int64_t> lastCallbackMs { 0 };
            int silentSeconds = 0, longestSilence = 0, secondsHeard = 0, secondsChecked = 0;
            double worstGapMs = 0.0;

            // XTest (X11, plugins in this process)
            void* display = nullptr;
            int (*fakeMotion) (void*, int, int, int, unsigned long) = nullptr;
            int (*fakeButton) (void*, unsigned int, int, unsigned long) = nullptr;
            int (*flush) (void*) = nullptr;
            int (*closeDisplay) (void*) = nullptr;
            int xMoves = 0, paramChanges = 0;
            std::map<std::pair<const void*, int>, float> firstValues;
            // the message thread's pulse: a watchdog thread notes a stall over 1.5 s in <report>.stall (pid, when, how
            // long) while it lasts, so a debugger can be attached from outside (LYNX_ME_PTRACE=1 allows it)
            std::atomic<std::int64_t> lastUiMs { 0 };
            double worstUiGapMs = 0.0;
            std::thread watchdog;
            std::atomic<bool> watching { false };
            int uiStalls = 0;
            std::atomic<int> stallsSeen { 0 };

            void startWatchdog()
            {
                lastUiMs = nowMs();
                watching = true;
                watchdog = std::thread ([this] {
                    auto inStall = false;
                    while (watching.load())
                    {
                        juce::Thread::sleep (100);
                        const auto gap = nowMs() - lastUiMs.load();
                        if (gap > 1500 && ! inStall)
                        {
                            inStall = true;
                            ++stallsSeen;
                            juce::File (reportPath + ".stall").appendText ("stall pid=" + juce::String (processId()) + " callbacks="
                                                                          + juce::String (callbacks.load()) + "\n");
                        }
                        else if (gap < 200)
                            inStall = false;
                    }
                });
            }

            void step (const juce::String& line)
            {
                report << line << "\n";
                juce::File (reportPath).replaceWithText (report); // (each step: a crash leaves the last one)
            }

            static std::int64_t nowMs() { return (std::int64_t) juce::Time::getMillisecondCounterHiRes(); }
            static int processId() { return lynx::windows::processId(); } // (ui/LynxWindows: Windows has no getpid here; rollup 0.1.31 step 6)

            // LYNX_ME_DEVICE=1: the real audio device (on Linux PipeWire when its daemon answers, as the app picks it);
            // a second callback on the device marks the device thread's blocks, the engine's scope gives the level
            struct Monitor final : juce::AudioIODeviceCallback
            {
                Test& test;
                double lastMs = 0.0;
                explicit Monitor (Test& t) : test (t) {}
                void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int outs, int n,
                                                       const juce::AudioIODeviceCallbackContext&) override
                {
                    for (int c = 0; c < outs; ++c)
                        if (outputs[c] != nullptr)
                            juce::FloatVectorOperations::clear (outputs[c], n);
                    const auto now = juce::Time::getMillisecondCounterHiRes();
                    // (the device's period is n samples: what is over it is the time the engine's callback took too long)
                    if (lastMs > 0.0)
                        if (const auto late = now - lastMs; late > test.worstMs.load())
                            test.worstMs = late;
                    lastMs = now;
                    ++test.callbacks;
                    test.lastCallbackMs = nowMs();
                }
                void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
                void audioDeviceStopped() override {}
            };
            std::unique_ptr<Monitor> monitor;
            bool realDevice = false;

            void startAudio()
            {
                if (juce::SystemStats::getEnvironmentVariable ("LYNX_ME_DEVICE", {}) == "1")
                {
                    realDevice = true;
                    engine->openAudioDevice();
                    monitor = std::make_unique<Monitor> (*this);
                    engine->getDeviceManager().addAudioCallback (monitor.get());
                    auto* d = engine->getDeviceManager().getCurrentAudioDevice();
                    step ("device=" + engine->getDeviceManager().getCurrentAudioDeviceType() + " / " + (d != nullptr ? d->getName() : juce::String ("none"))
                          + (d != nullptr ? " rate=" + juce::String (d->getCurrentSampleRate()) + " buffer=" + juce::String (d->getCurrentBufferSizeSamples()) : juce::String()));
                    return;
                }
                engine->deviceCallback().audioDeviceAboutToStart (&device);
                running = true;
                audio = std::thread ([this] {
                    std::vector<float> l (512), r (512);
                    float* outs[] = { l.data(), r.data() };
                    juce::AudioIODeviceCallbackContext context;
                    auto next = juce::Time::getMillisecondCounterHiRes();
                    while (running.load())
                    {
                        const auto before = juce::Time::getMillisecondCounterHiRes();
                        engine->deviceCallback().audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, context);
                        const auto took = juce::Time::getMillisecondCounterHiRes() - before;
                        if (took > worstMs.load())
                            worstMs = took;
                        auto peak = 0.0f;
                        for (int i = 0; i < 512; ++i)
                            peak = juce::jmax (peak, std::abs (l[(std::size_t) i]), std::abs (r[(std::size_t) i]));
                        if (peak > secondPeak.load())
                            secondPeak = peak;
                        ++callbacks;
                        lastCallbackMs = nowMs();
                        next += 512.0 / 48.0; // (ms)
                        const auto wait = next - juce::Time::getMillisecondCounterHiRes();
                        if (wait > 0.5)
                            juce::Thread::sleep ((int) wait);
                        else if (wait < -200.0)
                            next = juce::Time::getMillisecondCounterHiRes(); // (fell behind: no catch-up burst)
                    }
                });
            }

            void startMidi()
            {
                playing = true;
                midi = std::thread ([this] {
                    juce::Random r (55);
                    int k = 0;
                    while (playing.load())
                    {
                        const auto note = 48 + r.nextInt (24);
                        engine->injectMidiInput (juce::MidiMessage::noteOn (1, note, (juce::uint8) (70 + r.nextInt (50))));
                        if (k % 8 == 0)
                            engine->injectMidiInput (juce::MidiMessage::controllerEvent (1, 64, (k / 8) % 2 == 0 ? 127 : 0));
                        engine->injectMidiInput (juce::MidiMessage::pitchWheel (1, 8192 + r.nextInt (2000) - 1000));
                        juce::Thread::sleep (90);
                        engine->injectMidiInput (juce::MidiMessage::noteOff (1, note));
                        juce::Thread::sleep (30);
                        ++k;
                    }
                    for (int n = 0; n < 128; ++n)
                        engine->injectMidiInput (juce::MidiMessage::noteOff (1, n));
                    engine->injectMidiInput (juce::MidiMessage::controllerEvent (1, 64, 0));
                });
            }

            void stopThreads()
            {
                playing = false;
                if (midi.joinable())
                    midi.join();
                running = false;
                if (audio.joinable())
                    audio.join();
                if (monitor != nullptr)
                    engine->getDeviceManager().removeAudioCallback (monitor.get());
            }

            // the level of the device's output (real device: the engine's scope of its last 2048 samples)
            void sampleScope()
            {
                if (! realDevice)
                    return;
                std::array<float, 2048> scope {};
                engine->copyScope (scope.data(), (int) scope.size());
                auto peak = 0.0f;
                for (const auto v : scope)
                    peak = juce::jmax (peak, std::abs (v));
                if (peak > secondPeak.load())
                    secondPeak = peak;
            }

            void openXTest()
            {
               #if JUCE_LINUX
                if (juce::SystemStats::getEnvironmentVariable ("DISPLAY", {}).isEmpty())
                    return;
                auto* x11 = dlopen ("libX11.so.6", RTLD_NOW);
                auto* xtst = dlopen ("libXtst.so.6", RTLD_NOW);
                if (x11 == nullptr || xtst == nullptr)
                    return;
                auto open = (void* (*) (const char*)) dlsym (x11, "XOpenDisplay");
                closeDisplay = (int (*) (void*)) dlsym (x11, "XCloseDisplay");
                flush = (int (*) (void*)) dlsym (x11, "XFlush");
                fakeMotion = (int (*) (void*, int, int, int, unsigned long)) dlsym (xtst, "XTestFakeMotionEvent");
                fakeButton = (int (*) (void*, unsigned int, int, unsigned long)) dlsym (xtst, "XTestFakeButtonEvent");
                display = open != nullptr && fakeMotion != nullptr && fakeButton != nullptr && flush != nullptr ? open (nullptr) : nullptr;
               #endif
            }

            // a move, sometimes a press-drag-release, over the editor of plugin `u` (its window in this process)
            void touchEditor (Unit& u)
            {
                if (display == nullptr || u.instance == nullptr)
                    return;
                auto* editor = u.instance->getActiveEditor();
                if (editor == nullptr || ! editor->isShowing())
                    return;
                const auto area = editor->getScreenBounds().reduced (10);
                if (area.isEmpty())
                    return;
                const auto scale = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                                       ? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale : 1.0;
                const auto x = (int) ((area.getX() + random.nextInt (area.getWidth())) * scale);
                const auto y = (int) ((area.getY() + random.nextInt (area.getHeight())) * scale);
                fakeMotion (display, -1, x, y, 0);
                if (random.nextInt (4) == 0)
                {
                    fakeButton (display, 1, 1, 0);
                    fakeMotion (display, -1, x + random.nextInt (21) - 10, y + random.nextInt (41) - 20, 0);
                    fakeButton (display, 1, 0, 0);
                }
                flush (display);
                ++xMoves;
            }

            void changeParameters (Unit& u)
            {
                if (u.instance == nullptr)
                    return;
                const auto& params = u.instance->getParameters();
                if (params.isEmpty())
                    return;
                // (around each parameter's first value, as a hand turning a knob back and forth: the patch stays audible)
                const auto index = random.nextInt (params.size());
                auto* p = params[index];
                const auto key = std::make_pair ((const void*) u.instance, index);
                if (firstValues.find (key) == firstValues.end())
                    firstValues[key] = p->getValue();
                p->beginChangeGesture();
                p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, firstValues[key] + (random.nextFloat() - 0.5f) * 0.06f));
                p->endChangeGesture();
                ++paramChanges;
            }

            juce::AudioPluginInstance* instanceOf (Unit& u)
            {
                if (u.channel >= 0)
                {
                    auto instrument = engine->getChannelInstrument (u.channel);
                    if (auto* p = dynamic_cast<lynx::PluginInstrument*> (instrument.get()))
                    {
                        u.owner = instrument;
                        return &p->getInstance();
                    }
                    return nullptr;
                }
                auto effect = engine->getEffect (1, u.slot);
                if (auto* p = dynamic_cast<lynx::PluginEffect*> (effect.get()))
                {
                    u.owner = effect;
                    return &p->getInstance();
                }
                return nullptr;
            }

            void timerCallback() override
            {
                const auto elapsed = (int) (juce::Time::getMillisecondCounter() - phaseStart);
                {
                    const auto now = nowMs();
                    if (phase == 1)
                        worstUiGapMs = juce::jmax (worstUiGapMs, (double) (now - lastUiMs.load()));
                    lastUiMs = now;
                }
                if (phase == 0) // the plugins loaded: open every editor, start the keyboard
                {
                    auto all = true;
                    for (auto& u : units)
                        if (u.instance == nullptr)
                            all = (u.instance = instanceOf (u)) != nullptr && all;
                    if (! all && elapsed < 30000)
                        return;
                    for (auto& u : units)
                    {
                        step ("loaded " + u.type.name + "=" + juce::String ((int) (u.instance != nullptr)));
                        if (u.instance == nullptr)
                            continue;
                        const auto channel = u.channel, slot = u.slot;
                        lynx::PluginWindows::open (u.owner, *u.instance, u.instance->getName(),
                                                   { engine.get(), [channel, slot] (lynx::Song& song) -> lynx::PluginRef* {
                                                        if (channel >= 0)
                                                            return channel < (int) song.channels.size() ? &song.channels[(std::size_t) channel].plugin : nullptr;
                                                        return song.mixer.size() > 1 && slot < (int) song.mixer[1].effects.size() ? &song.mixer[1].effects[(std::size_t) slot].plugin : nullptr;
                                                    }, channel >= 0 });
                        u.owner.reset();
                        step ("window " + u.type.name + "=" + juce::String ((int) lynx::PluginWindows::isOpen (*u.instance)));
                    }
                    startMidi();
                    startWatchdog();
                    phase = 1;
                    phaseStart = juce::Time::getMillisecondCounter();
                    secondPeak = 0.0f;
                    return;
                }
                sampleScope();
                if (phase == 1)
                {
                    // use the editors
                    for (auto& u : units)
                    {
                        touchEditor (u);
                        changeParameters (u);
                    }
                    // the keyboard plays the next instrument every 2 s (FL: the selected channel takes the MIDI keyboard)
                    std::vector<int> channels;
                    for (const auto& u : units)
                        if (u.channel >= 0)
                            channels.push_back (u.channel);
                    if (! channels.empty())
                        if (const auto pick = channels[(std::size_t) ((elapsed / 2000) % (int) channels.size())]; pick != selectedFrom.load())
                        {
                            engine->setSelectedChannel (pick);
                            selectedFrom = pick;
                        }
                    // the audio thread alive, and a sound every second (after the first 3 s)
                    const auto gap = (double) (nowMs() - lastCallbackMs.load());
                    worstGapMs = juce::jmax (worstGapMs, gap);
                    if (elapsed / 1000 > secondsChecked)
                    {
                        secondsChecked = elapsed / 1000;
                        const auto peak = secondPeak.exchange (0.0f);
                        if (secondsChecked > 3)
                        {
                            const auto heard = peak > 1.0e-3f;
                            secondsHeard += heard ? 1 : 0;
                            silentSeconds = heard ? 0 : silentSeconds + 1;
                            if (! heard)
                            {
                                juce::String names;
                                for (auto& u : units)
                                    names << " " << u.type.name.removeCharacters (" ") << (u.instance == nullptr ? "=gone" : "=ok");
                                step ("silent second t=" + juce::String (secondsChecked) + " selected=" + juce::String (selectedFrom.load())
                                      + " offline_renders=" + juce::String (lynx::AudioEngine::offlineRendersRunning().load()) + names);
                            }
                            longestSilence = juce::jmax (longestSilence, silentSeconds);
                        }
                        if (secondsChecked % 5 == 0)
                            step ("t=" + juce::String (secondsChecked) + " s callbacks=" + juce::String (callbacks.load()) + " worst_callback_ms="
                                  + juce::String (worstMs.load(), 1) + " worst_gap_ms=" + juce::String (worstGapMs, 1) + " peak=" + juce::String (peak, 4)
                                  + " x_moves=" + juce::String (xMoves) + " param_changes=" + juce::String (paramChanges) + " worst_ui_gap_ms="
                                  + juce::String (worstUiGapMs, 0) + " ui_stalls=" + juce::String (stallsSeen.load()));
                    }
                    if (elapsed < seconds * 1000)
                        return;
                    stopTimer();
                    stopThreads();
                    const auto checked = juce::jmax (0, secondsChecked - 3);
                    watching = false;
                    if (watchdog.joinable())
                        watchdog.join();
                    const auto ok = worstMs.load() < 250.0 && worstGapMs < 500.0 && longestSilence <= 1 && secondsHeard >= checked - 1
                                    && worstUiGapMs < 1500.0;
                    step ("seconds_heard=" + juce::String (secondsHeard) + "/" + juce::String (checked) + " longest_silence_s=" + juce::String (longestSilence)
                          + " worst_callback_ms=" + juce::String (worstMs.load(), 1) + " worst_gap_ms=" + juce::String (worstGapMs, 1)
                          + " callbacks=" + juce::String (callbacks.load()) + " worst_ui_gap_ms=" + juce::String (worstUiGapMs, 0) + " ui_stalls="
                          + juce::String (stallsSeen.load()) + " editor_crashes=" + juce::String (lynx::PluginWindows::editorCrashCount()));
                    step (ok ? "result=OK" : "result=FAILED");
                    lynx::PluginWindows::closeAll();
                    if (display != nullptr && closeDisplay != nullptr)
                        closeDisplay (display);
                    done (ok);
                }
            }
        };

        auto* test = new Test();
        test->reportPath = reportPath;
        if (const auto s = juce::SystemStats::getEnvironmentVariable ("LYNX_ME_SECONDS", {}).getIntValue(); s > 0)
            test->seconds = s;
        test->done = [this, test] (bool ok) {
            setApplicationReturnValue (ok ? 0 : 1);
            juce::MessageManager::callAsync ([this, test] {
                if (! test->realDevice)
                    test->engine->deviceCallback().audioDeviceStopped();
                lynx::setMainWindowForOwnership (nullptr);
                test->mainWindow = nullptr;
                test->engine = nullptr;
                delete test;
                quit();
            });
        };
        test->engine = std::make_unique<lynx::AudioEngine> (true, -1, false, true); // (the app's engine; its device stays closed)
        test->mainWindow = std::make_unique<juce::DocumentWindow> ("LYNX test main window", juce::Colours::darkgrey, 0);
        test->mainWindow->setBounds (20, 20, 300, 200);
        test->mainWindow->setVisible (true);
        lynx::setMainWindowForOwnership (test->mainWindow.get());

        auto& host = test->engine->getPluginHost();
        const auto separate = juce::SystemStats::getEnvironmentVariable ("LYNX_ME_SEPARATE", {}) == "1";
        for (const auto& arg : pluginArgs)
        {
            const auto path = arg.upToFirstOccurrenceOf ("::", false, false);
            const auto name = arg.fromFirstOccurrenceOf ("::", false, false);
            host.addPluginFile (juce::File (path));
            std::optional<juce::PluginDescription> chosen;
            for (const auto& type : host.getKnownPlugins().getTypes())
                if ((juce::File (type.fileOrIdentifier).getFileName() == juce::File (path).getFileName() || type.fileOrIdentifier == path)
                    && (name.isEmpty() || type.name == name))
                {
                    chosen = type;
                    break;
                }
            if (! chosen)
            {
                test->step ("no plugin in " + arg);
                continue;
            }
            Test::Unit u;
            u.type = *chosen;
            lynx::PluginRef ref;
            ref.separateProcess = separate;
            ref.id = chosen->createIdentifierString().toStdString();
            ref.name = chosen->name.toStdString();
            test->engine->editSong ([&] (lynx::Song& song) {
                while (song.mixer.size() < 2)
                    song.mixer.emplace_back();
                if (chosen->isInstrument)
                {
                    lynx::ChannelDef c;
                    c.name = ref.name;
                    c.instrument = lynx::InstrumentKind::plugin;
                    c.plugin = ref;
                    c.mixerTrack = 1;
                    u.channel = (int) song.channels.size();
                    song.channels.push_back (c);
                }
                else
                {
                    lynx::EffectSlot slot;
                    slot.kind = lynx::EffectKind::plugin;
                    slot.plugin = ref;
                    u.slot = (int) song.mixer[1].effects.size();
                    song.mixer[1].effects.push_back (slot);
                }
            });
            test->step ("plugin " + chosen->name + " instrument=" + juce::String ((int) chosen->isInstrument) + " separate=" + juce::String ((int) separate));
            test->units.push_back (u);
        }
        // (the default song's own channels: muted, so the sound heard is the plugins')
        test->engine->editSong ([] (lynx::Song& song) {
            for (auto& c : song.channels)
                if (c.instrument != lynx::InstrumentKind::plugin)
                    c.muted = true;
        });
       #if JUCE_LINUX
        if (juce::SystemStats::getEnvironmentVariable ("LYNX_ME_PTRACE", {}) == "1")
            prctl (PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0); // (a debugger may attach to take the stacks of a stall)
       #endif
        test->openXTest();
        test->step ("xtest=" + juce::String ((int) (test->display != nullptr)) + " seconds=" + juce::String (test->seconds));
        test->startAudio();
        test->phaseStart = juce::Time::getMillisecondCounter();
        test->startTimerHz (20);
    }

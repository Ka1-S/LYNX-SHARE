    // --hostplaytest190ll report.txt "<LYNX Test Synth.vst3>" [--buffer N] (round 190ll, issue #199: Vital crackled when
    // its own preset played from the Piano roll, not when played in its own window). The engine's own device callback
    // (a stand-in device at 48 kHz, driven block by block as --renderaheadtest) plays a pattern of 16th-note chords on
    // the VST3 test synth (tools/testplugin/lynx_test_synth_vst3.cpp, its LYNX_TEST_SYNTH_LOG) in the ways a user
    // plays: the channel selected (real-time), not selected (render-ahead), its editor opened and closed while it
    // plays, the channel selected and unselected while it plays, and a knob moved in its editor on every block. What
    // the plugin got is read back from its log: every block's song position must follow the one before by exactly the
    // samples it had (a synth that keeps its LFOs / effects in step with the host's position — Vital, every JUCE synth
    // that reads the play head — jumps where it does not: clicks and fizz), each block in time order (never one from
    // the past after one from the future), and no block shorter than a note's own offset asks for. No window, no device.
    void runHostPlayTest190ll (const juce::File& report, const juce::File& pluginFile, int block)
    {
        struct StandIn final : juce::AudioIODevice
        {
            explicit StandIn (int b) : juce::AudioIODevice ("hostplay", "test"), block (b) {}
            const int block;
            juce::StringArray getOutputChannelNames() override { return { "L", "R" }; }
            juce::StringArray getInputChannelNames() override { return {}; }
            juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
            juce::Array<int> getAvailableBufferSizes() override { return { block }; }
            int getDefaultBufferSize() override { return block; }
            juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
            void close() override {}
            bool isOpen() override { return true; }
            void start (juce::AudioIODeviceCallback*) override {}
            void stop() override {}
            bool isPlaying() override { return true; }
            juce::String getLastError() override { return {}; }
            int getCurrentBufferSizeSamples() override { return block; }
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
        enum class Play { selected, ahead, editorToggles, selectionToggles, knob, songMode, fixedBuffers, fixedMaximum };
        struct Scenario
        {
            const char* name;
            Play play;
            bool renderAhead;
        };
        static const Scenario scenarios[] = {
            { "channel selected, render ahead off", Play::selected, false },
            { "channel selected, render ahead on", Play::selected, true },
            { "channel not selected (rendered ahead)", Play::ahead, true },
            { "editor opened / closed while playing", Play::editorToggles, true },
            { "channel selected / unselected while playing", Play::selectionToggles, true },
            { "a knob moved in the editor every block", Play::knob, true },
            { "Song mode, the pattern clip looping", Play::songMode, true },
            { "the wrapper's \"Use fixed size buffers\" on", Play::fixedBuffers, true },
            { "fixed size buffers, \"Process maximum size buffers\"", Play::fixedMaximum, true },
        };
        constexpr double bpm = 140.0, rate = 48000.0;
        const auto logBase = report.getSiblingFile ("hostplay190ll-synth-" + juce::String (juce::Time::currentTimeMillis()));
       #if JUCE_WINDOWS
        _putenv_s ("LYNX_TEST_SYNTH_LOG", logBase.getFullPathName().toRawUTF8());
       #else
        setenv ("LYNX_TEST_SYNTH_LOG", logBase.getFullPathName().toRawUTF8(), 1);
       #endif
        auto text = std::make_shared<juce::String>();
        auto step = [report, text] (const juce::String& line) { *text << line << "\n"; report.replaceWithText (*text); };
        step ("host play test (issue #199): blocks of " + juce::String (block) + " at 48 kHz, " + juce::String (bpm) + " BPM, the VST3 test synth");
        auto ok = std::make_shared<bool> (true);
        auto run = std::make_shared<std::function<void (std::size_t)>>();
        *run = [this, step, ok, run, block, pluginFile, logBase, report] (std::size_t index) {
            if (index >= std::size (scenarios))
            {
                lynx::renderAheadOption().store (true);
                step (juce::String ("result=") + (*ok ? "OK" : "FAIL"));
                if (*ok)
                    for (const auto& f : logBase.getParentDirectory().findChildFiles (juce::File::findFiles, false, logBase.getFileName() + "*"))
                        f.deleteFile();
                else
                    step ("synth logs: " + logBase.getFullPathName() + ".*");
                setApplicationReturnValue (*ok ? 0 : 1);
                engine.reset();
                quit();
                return;
            }
            const auto& sc = scenarios[index];
            // (probing another synth: LYNX_HOSTPLAY_ONLY = words of the scenarios to run, e.g. "selected,maximum")
            if (const auto only = juce::SystemStats::getEnvironmentVariable ("LYNX_HOSTPLAY_ONLY", {}); only.isNotEmpty())
            {
                bool wanted = false;
                for (const auto& w : juce::StringArray::fromTokens (only, ",", {}))
                    wanted = wanted || juce::String (sc.name).containsIgnoreCase (w.trim());
                if (! wanted)
                {
                    (*run) (index + 1);
                    return;
                }
            }
            lynx::renderAheadOption().store (sc.renderAhead);
            engine = std::make_unique<lynx::AudioEngine>();
            engine->getDeviceManager().closeAudioDevice(); // the engine's callback is driven by hand below
            engine->setRenderAheadAlways (true);           // (a light project: the app would leave it real-time)
            auto& host = engine->getPluginHost();
            host.addPluginFile (pluginFile);
            std::optional<juce::PluginDescription> synth;
            for (const auto& type : host.getKnownPlugins().getTypes()) // (this file's: the list keeps earlier scans)
                if (type.pluginFormatName == "VST3" && type.isInstrument && ! synth
                    && juce::File (type.fileOrIdentifier).getFileName() == pluginFile.getFileName())
                    synth = type;
            if (! synth)
            {
                step ("FAIL: no VST3 instrument in " + pluginFile.getFullPathName());
                *ok = false;
                (*run) (std::size (scenarios));
                return;
            }
            auto song = lynx::Song::makeDefault();
            song.bpm = bpm;
            auto& c = song.channels[0];
            c.instrument = lynx::InstrumentKind::plugin;
            c.plugin.id = synth->createIdentifierString().toStdString();
            c.plugin.name = synth->name.toStdString();
            c.plugin.fixedBuffers = sc.play == Play::fixedBuffers || sc.play == Play::fixedMaximum;
            c.plugin.maxSizeBuffers = sc.play == Play::fixedMaximum;
            for (std::size_t k = 1; k < song.channels.size(); ++k)
                song.channels[k].muted = true;
            auto& notes = song.patterns[0].notes;
            notes.clear();
            const auto sixteenth = (std::int64_t) lynx::ppq / 4;
            // two bars of 16th-note chords (overlapping legato notes, velocities and fine pitches that vary: what a
            // Piano roll part sends a synth)
            for (int k = 0; k < 32; ++k)
                for (const auto key : { 48 + (k % 5), 55 + (k % 3), 60 + (k % 7) })
                {
                    lynx::Note n { k * sixteenth, (k % 4 == 0 ? 3 : 1) * sixteenth + 7, 0, key, 0.5f + 0.015f * (float) (k % 30) };
                    n.finePitch = (float) (k % 3) * 10.0f - 10.0f;
                    notes.push_back (n);
                }
            song.patterns[0].sortNotes();
            song.patterns[0].lengthTicks = 32 * sixteenth;
            if (sc.play == Play::songMode)
            {
                song.mode = lynx::PlayMode::song;
                lynx::Clip clip;
                clip.pattern = 0;
                clip.start = 0;
                clip.length = 4 * 32 * sixteenth;
                song.playlist.push_back (clip);
            }
            else
                song.mode = lynx::PlayMode::pattern;
            engine->setSong (std::move (song));
            engine->setSelectedChannel (sc.play == Play::ahead || sc.play == Play::songMode ? 1 : 0);
            auto device = std::make_shared<StandIn> (block);
            engine->deviceCallback().audioDeviceAboutToStart (device.get());
            juce::Timer::callAfterDelay (1500, [this, step, ok, run, block, index, device, logBase] {
                if (engine == nullptr)
                    return;
                const auto& sc = scenarios[index];
                // the lines already in the logs belong to the scenarios before
                std::map<juce::String, int> skip;
                for (const auto& f : logBase.getParentDirectory().findChildFiles (juce::File::findFiles, false, logBase.getFileName() + ".*"))
                {
                    juce::StringArray lines;
                    f.readLines (lines);
                    skip[f.getFullPathName()] = lines.size();
                }
                auto& cb = engine->deviceCallback();
                std::vector<float> l ((std::size_t) block), r ((std::size_t) block);
                float* outs[] = { l.data(), r.data() };
                juce::AudioIODeviceCallbackContext context;
                engine->stop();
                engine->seek (0.0);
                cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, block, context);
                engine->play();
                const auto blocks = (int) (6.0 * rate / block);
                // (probing another synth: LYNX_HOSTPLAY_WAV = a prefix: each scenario's output as <prefix>-<n>.wav;
                // LYNX_HOSTPLAY_ZERO = a word: the plugin's parameters whose names hold it are set to 0, e.g. a
                // synth's phase randomisation, so two runs can be compared sample by sample)
                const auto wavPrefix = juce::SystemStats::getEnvironmentVariable ("LYNX_HOSTPLAY_WAV", {});
                juce::AudioBuffer<float> recorded (2, wavPrefix.isNotEmpty() ? blocks * block : 1);
                if (const auto zero = juce::SystemStats::getEnvironmentVariable ("LYNX_HOSTPLAY_ZERO", {}); zero.isNotEmpty())
                    if (const auto held0 = engine->getChannelInstrument (0))
                        if (auto* hosted = dynamic_cast<lynx::PluginInstrument*> (held0.get()))
                        {
                            juce::StringArray names;
                            const auto& params = hosted->getInstance().getParameters();
                            for (int pi = 0; pi < params.size(); ++pi)
                                if (params[pi]->getName (100).containsIgnoreCase (zero))
                                {
                                    hosted->setParameterFromUi (pi, 0.0f);
                                    names.add (params[pi]->getName (100));
                                }
                            step ("     zeroed: " + names.joinIntoString (", "));
                        }
                // (LYNX_HOSTPLAY_SET = "name=value;name=value": parameters by their exact names, 0..1; LYNX_HOSTPLAY_LIST=1
                // lists every parameter with its value in the report)
                if (const auto held1 = engine->getChannelInstrument (0))
                    if (auto* hosted = dynamic_cast<lynx::PluginInstrument*> (held1.get()))
                    {
                        const auto& params = hosted->getInstance().getParameters();
                        for (const auto& pair : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("LYNX_HOSTPLAY_SET", {}), ";", {}))
                            for (int pi = 0; pi < params.size(); ++pi)
                                if (params[pi]->getName (100) == pair.upToFirstOccurrenceOf ("=", false, false).trim())
                                    hosted->setParameterFromUi (pi, pair.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
                        if (juce::SystemStats::getEnvironmentVariable ("LYNX_HOSTPLAY_LIST", {}) == "1")
                            for (int pi = 0; pi < params.size(); ++pi)
                                step ("     param " + juce::String (pi) + " " + params[pi]->getName (100) + " = " + juce::String (params[pi]->getValue(), 4)
                                      + " (" + params[pi]->getCurrentValueAsText() + ")");
                    }
                const auto perSecond = (int) (rate / block);
                int key = 0;
                const auto held = engine->getChannelInstrument (0);
                auto* plugin = dynamic_cast<lynx::PluginInstrument*> (held.get());
                for (int k = 0; k < blocks; ++k)
                {
                    if (sc.play == Play::editorToggles && k % (perSecond / 2) == 0)
                    {
                        const auto open = (k / (perSecond / 2)) % 2 == 0;
                        engine->setEditorOpen (&key, [] (lynx::Song& s) { return &s.channels[0].plugin; }, -1, open);
                        engine->dispatchPendingMessages();
                    }
                    if (sc.play == Play::selectionToggles && k % (perSecond / 3) == 0)
                    {
                        engine->setSelectedChannel ((k / (perSecond / 3)) % 2 == 0 ? 0 : 1);
                        engine->dispatchPendingMessages();
                    }
                    if (sc.play == Play::knob && plugin != nullptr && plugin->getInstance().getParameters().size() > 0)
                        plugin->setParameterFromUi (0, (float) (k % 100) / 100.0f);
                    cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, block, context);
                    if (wavPrefix.isNotEmpty())
                        for (int ch = 0; ch < 2; ++ch)
                            recorded.copyFrom (ch, k * block, outs[ch], block);
                    if (k % perSecond < perSecond / 2) // (about the device's pace for half of each second)
                        std::this_thread::sleep_for (std::chrono::microseconds ((int) (block * 1.0e6 / rate * 0.6)));
                }
                engine->stop();
                cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, block, context);
                if (wavPrefix.isNotEmpty())
                {
                    const juce::File wav (wavPrefix + "-" + juce::String ((int) index) + ".wav");
                    wav.deleteFile();
                    if (auto out = std::unique_ptr<juce::OutputStream> (wav.createOutputStream()))
                        if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (juce::WavAudioFormat().createWriterFor (out.get(), rate, 2, 32, {}, 0)))
                        {
                            out.release();
                            writer->writeFromAudioSampleBuffer (recorded, 0, recorded.getNumSamples());
                        }
                }
                if (sc.play == Play::editorToggles)
                    engine->setEditorOpen (&key, {}, -1, false);

                // what the synth got: P<instance> <ppq> <samples> <playing> <rt|off> <voices>
                struct Block { double ppq; int samples; bool playing; };
                std::map<juce::String, std::vector<Block>> perInstance;
                for (const auto& f : logBase.getParentDirectory().findChildFiles (juce::File::findFiles, false, logBase.getFileName() + ".*"))
                {
                    juce::StringArray lines;
                    f.readLines (lines);
                    for (int i = skip[f.getFullPathName()]; i < lines.size(); ++i)
                    {
                        const auto p = juce::StringArray::fromTokens (lines[i], " ", {});
                        if (p.size() >= 6 && p[0].startsWith ("P"))
                            perInstance[p[0]].push_back ({ p[1].getDoubleValue(), p[2].getIntValue(), p[3] == "1" });
                    }
                }
                const auto patternQuarters = 32.0 / 4.0;
                const auto samplesPerQuarter = 60.0 * rate / bpm;
                int jumps = 0, backwards = 0, total = 0, smallest = 1 << 30, under16 = 0;
                double worst = 0.0;
                juce::String firstJump;
                for (const auto& [name, list] : perInstance)
                    for (std::size_t i = 0; i < list.size(); ++i)
                    {
                        const auto& b = list[i];
                        if (! b.playing || b.samples <= 0)
                            continue;
                        ++total;
                        smallest = std::min (smallest, b.samples);
                        under16 += b.samples < 16 ? 1 : 0;
                        if (i == 0 || ! list[i - 1].playing || list[i - 1].ppq < 0.0 || b.ppq < 0.0)
                            continue;
                        const auto& a = list[i - 1];
                        auto d = b.ppq - (a.ppq + a.samples / samplesPerQuarter);
                        // (the pattern's own loop: Pattern mode reports the position inside the pattern, as FL)
                        const auto loop = sc.play == Play::songMode ? 4.0 * patternQuarters : patternQuarters;
                        if (d < -loop / 2.0)
                            d += loop;
                        const auto err = d * samplesPerQuarter;
                        if (std::abs (err) > 0.5)
                        {
                            ++jumps;
                            backwards += err < 0.0 ? 1 : 0;
                            if (firstJump.isEmpty())
                                firstJump = name + " block " + juce::String ((int) i) + ": " + juce::String (err, 1) + " samples (" + juce::String (a.ppq, 6) + " + "
                                            + juce::String (a.samples) + " -> " + juce::String (b.ppq, 6) + ")";
                        }
                        worst = std::max (worst, std::abs (err));
                    }
                const auto good = (total > blocks / 2 || wavPrefix.isNotEmpty()) && jumps == 0;
                *ok = *ok && good;
                step (juce::String (good ? "ok   " : "FAIL ") + sc.name + ": " + juce::String (total) + " plugin blocks while playing ("
                      + juce::String ((int) perInstance.size()) + " instance(s)), smallest " + juce::String (smallest) + " samples, " + juce::String (under16)
                      + " under 16; position jumps " + juce::String (jumps) + " (" + juce::String (backwards) + " backwards), worst " + juce::String (worst, 1)
                      + " samples" + (firstJump.isEmpty() ? juce::String() : "; first: " + firstJump));
                (*run) (index + 1);
            });
        };
        (*run) (0);
    }

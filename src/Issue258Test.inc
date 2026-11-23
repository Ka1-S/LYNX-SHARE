    // Round 190pv (issue #258, "Transistor bass duplicated instrument automation doesn't work"): the reporter's steps
    // through the app's own engine (AudioEngine: edits published as the UI publishes them, the device callback driven by
    // hand, the worker pool, render-ahead on or off). A channel plays a pattern; it is cloned (Channel rack › Clone: the
    // clone right below, selected); a new pattern plays the clone; an automation clip on one of the clone's parameters
    // ramps over the pattern; the Transistor Bass page's follow timer (ui/ChannelSettings.cpp) runs between the blocks.
    // The clone's sound must follow its clip: the clip ramping up and ramping down must render differently, and so must
    // the original under the same steps (the reference). Included in the app class (--issue258test, --selftest).
    struct Issue258Case
    {
        juce::String name;
        lynx::ChannelDef channel;
        int key = 60, param = 0;
    };

    // the cases this PC can make: Transistor Bass (FL's "Empty" preset, program A6 = key 81 shown, its sequence gated)
    // when FL Studio is here, and LYNX Synth's cutoff always
    static std::vector<Issue258Case> issue258Cases()
    {
        std::vector<Issue258Case> out;
        const auto& fl = lynx::flStudioAccess();
        if (! fl.roots.data.empty())
        {
            const auto empty = juce::File (juce::String::fromUTF8 (fl.roots.data.c_str()))
                                   .getChildFile ("Patches/Plugin presets/Generators/Transistor Bass/Empty.fst");
            if (auto tb = lynx::natives::channelFromPresetFile (lynx::InstrumentKind::transistorbass, empty.getFullPathName().toStdString(), fl))
            {
                lynx::transistorBassSelectProgram (tb->pluginState, 81);
                for (int i = 0; i < lynx::tbseq::steps; ++i)
                    lynx::modelparams::setRawValue (*tb, lynx::tbseq::firstStep + 5 * i + 2, 1.0f);
                out.push_back ({ "Transistor Bass cutoff", *tb, 81, 2 });
            }
        }
        lynx::ChannelDef synth;
        synth.name = "Synth";
        synth.instrument = lynx::InstrumentKind::synth;
        out.push_back ({ "LYNX Synth cutoff", synth, 60, lynx::synthparam::target (lynx::synthparam::cutoff) });
        return out;
    }

    // one run of the steps: the clip on the clone (or, `onOriginal`, the same steps with the original playing the new
    // pattern and automated), ramping `from` → `to`; the output of the new pattern's two bars (left channel)
    static std::vector<float> issue258Run (const Issue258Case& c, bool renderAhead, bool onOriginal, float from, float to)
    {
        struct Device final : juce::AudioIODevice
        {
            Device() : juce::AudioIODevice ("issue258", "test") {}
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
        constexpr std::int64_t bar = 4 * lynx::ppq;
        const auto wasAhead = lynx::renderAheadOption().load();
        lynx::renderAheadOption().store (renderAhead);
        Device device;
        std::vector<float> out;
        {
            lynx::AudioEngine e (true, -1, false, true); // (the live engine's graph and workers; no device opened: driven by hand)
            e.setRenderAheadAlways (renderAhead);
            auto song = lynx::Song::makeDefault();
            for (auto& p : song.patterns)
                p.notes.clear();
            song.playlist.clear();
            song.mode = lynx::PlayMode::song;
            auto channel = c.channel;
            channel.mixerTrack = 1;
            song.channels.push_back (channel);
            const auto original = (int) song.channels.size() - 1;
            if (song.patterns.empty())
                song.patterns.emplace_back();
            song.patterns[0].notes.push_back ({ 0, 2 * bar, original, c.key, 0.78f });
            song.playlist.push_back ({ 0, 0, 0, 2 * bar, 0 }); // (pattern 1 at the start, track 1)
            e.setSong (song);
            e.setSelectedChannel (original);
            auto& cb = e.deviceCallback();
            cb.audioDeviceAboutToStart (&device);
            std::vector<float> l (512), r (512);
            float* outs[] = { l.data(), r.data() };
            juce::AudioIODeviceCallbackContext context;
            // the Transistor Bass page of the channel the clip drives, open: its 10 Hz timer follows the program the
            // notes chose (followTransistorBassProgram)
            int followed = original;
            auto pageTimer = [&e, &followed] {
                const auto& s = e.getSong();
                if (followed < 0 || followed >= (int) s.channels.size() || s.channels[(std::size_t) followed].instrument != lynx::InstrumentKind::transistorbass)
                    return;
                const auto instrument = e.getChannelInstrument (followed);
                const auto prog = lynx::transistorBassProgram (s.channels[(std::size_t) followed].pluginState);
                const auto selected = instrument != nullptr ? instrument->selectedProgram() : -1;
                if (selected < 0 || ! prog || prog->program == selected)
                    return;
                auto next = s.channels[(std::size_t) followed].pluginState;
                if (lynx::transistorBassSelectProgram (next, selected))
                    e.editSong ([followed, next] (lynx::Song& x) { x.channels[(std::size_t) followed].pluginState = next; }, false);
            };
            auto playBlocks = [&] (int blocks, std::vector<float>* into) {
                for (int k = 0; k < blocks; ++k)
                {
                    cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, context);
                    if (into != nullptr)
                        into->insert (into->end(), l.begin(), l.end());
                    if (k % 9 == 8) // (about 10 times a second)
                        pageTimer();
                    if (renderAhead && k % 2 == 0)
                        std::this_thread::sleep_for (std::chrono::microseconds (3000)); // (the render-ahead thread gets ahead)
                }
            };
            // 1. the original plays its pattern for a bar
            e.stop();
            e.seek (0.0);
            cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, context);
            e.play();
            playBlocks (50, nullptr);
            e.stop();
            cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, context);
            // 2. Clone (the Channel rack's cloneSelected: the clone right below, selected)
            e.editSong ([original] (lynx::Song& s) { lynx::cloneChannel (s, original); });
            const auto clone = original + 1;
            e.setSelectedChannel (clone);
            const auto player = onOriginal ? original : clone;
            followed = player;
            // 3. a new pattern whose note plays the clone (or the original), the first pattern moved later
            e.editSong ([player, c, bar] (lynx::Song& s) {
                s.patterns.emplace_back();
                s.patterns.back().notes.push_back ({ 0, 2 * bar, player, c.key, 0.78f });
                s.currentPattern = (int) s.patterns.size() - 1;
                for (auto& clip : s.playlist)
                    if (clip.kind == lynx::ClipKind::pattern && clip.pattern == 0)
                        clip.start = 4 * bar;
                lynx::Clip p;
                p.kind = lynx::ClipKind::pattern;
                p.pattern = (int) s.patterns.size() - 1;
                p.track = 1;
                p.start = 0;
                p.length = 2 * bar;
                s.playlist.push_back (p);
            });
            // (the note dragged up onto its key in the Piano roll: each key it passes is previewed on its channel)
            for (int k = c.key - 4; k <= c.key; ++k)
            {
                e.sendLiveNote (player, k, 0.78f, true);
                playBlocks (3, nullptr);
                e.sendLiveNote (player, k, 0.0f, false);
                playBlocks (1, nullptr);
            }
            playBlocks (10, nullptr);
            // 4. right-click › Create automation clip on the knob (addAutomationClip), its points then drawn
            e.editSong ([player, c, bar, from, to] (lynx::Song& s) {
                lynx::Automation a;
                a.name = "issue 258";
                a.target = { lynx::AutomationTargetKind::channelParam, player, 0, c.param };
                a.points = { { 0, from, 0.0f }, { 2 * bar, from, 0.0f } };
                s.automations.push_back (a);
                lynx::Clip clip;
                clip.kind = lynx::ClipKind::automation;
                clip.automation = (int) s.automations.size() - 1;
                clip.track = 2;
                clip.start = 0;
                clip.length = 2 * bar;
                s.playlist.push_back (clip);
            });
            e.editSong ([from, to, bar] (lynx::Song& s) {
                s.automations.back().points = { { 0, from, 0.0f }, { bar, 0.5f * (from + to), 0.0f }, { 2 * bar, to, 0.0f } };
            });
            // 5. played from the start
            e.seek (0.0);
            cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, context);
            e.play();
            playBlocks ((int) (2 * 4 * 60.0 / e.getSong().bpm * 48000.0 / 512.0), &out);
            e.stop();
            cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, context);
            cb.audioDeviceStopped();
        }
        lynx::renderAheadOption().store (wasAhead);
        return out;
    }

    // "OK" or what failed; each case's levels go to `log`
    static juce::String issue258Check (juce::String& log)
    {
        juce::String result = "OK";
        auto peak = [] (const std::vector<float>& x) {
            float m = 0.0f;
            for (const auto v : x)
                m = std::max (m, std::abs (v));
            return m;
        };
        auto diff = [] (const std::vector<float>& a, const std::vector<float>& b) {
            float m = 0.0f;
            for (std::size_t i = 0; i < std::min (a.size(), b.size()); ++i)
                m = std::max (m, std::abs (a[i] - b[i]));
            return m;
        };
        for (const auto& c : issue258Cases())
            for (const auto ahead : { false, true })
            {
                const auto cloneUp = issue258Run (c, ahead, false, 0.0f, 1.0f), cloneDown = issue258Run (c, ahead, false, 1.0f, 0.0f);
                const auto origUp = issue258Run (c, ahead, true, 0.0f, 1.0f), origDown = issue258Run (c, ahead, true, 1.0f, 0.0f);
                const auto cloneMoves = diff (cloneUp, cloneDown), origMoves = diff (origUp, origDown);
                log << c.name << (ahead ? ", render ahead" : ", real time") << ": peak " << juce::String (peak (cloneUp), 4)
                    << ", clone up vs down " << juce::String (cloneMoves, 5) << ", original up vs down " << juce::String (origMoves, 5) << "\n";
                if (peak (cloneUp) < 1.0e-3f || peak (origUp) < 1.0e-3f)
                    result = "ERROR " + c.name + ": silent";
                else if (origMoves < 1.0e-3f * peak (origUp))
                    result = "ERROR " + c.name + ": the original's automation does nothing";
                else if (cloneMoves < 1.0e-3f * peak (cloneUp))
                    result = "ERROR " + c.name + (ahead ? " (render ahead)" : "") + ": the clone's automation does nothing";
            }
        return result;
    }

    void runIssue258Test (const juce::String& reportPath)
    {
        juce::String log;
        const auto result = issue258Check (log);
        juce::File (reportPath).replaceWithText (log + "issue258=" + result + "\n");
        setApplicationReturnValue (result == "OK" ? 0 : 1);
        quit();
    }

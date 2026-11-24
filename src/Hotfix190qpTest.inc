    // Round 190qp (0.1.31.1 hotfix: issue #253 "Still broken in 0.1.31", Discord "volume automations are still broken"):
    // an imported project's automation through the app's own engine (the live AudioEngine on a stand-in device, driven by
    // hand, render-ahead off and on). For each automated mixer volume, send level and effect parameter the window of two
    // bars where its automation moves the most is played twice: as imported, and with that automation's clips muted. The
    // sound must differ (the automation is heard), and the value the controls show (AutomationDisplay, what the mixer's
    // faders, send knobs and the plugin windows draw) must follow the automation. Included in the app class
    // (--autolive190qp, --selftest).
    struct AutoLiveDevice final : juce::AudioIODevice
    {
        AutoLiveDevice() : juce::AudioIODevice ("autolive190qp", "test") {}
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

    struct AutoLiveRun
    {
        std::vector<float> out;        // left channel
        std::vector<float> shown;      // the shown value of the target, every 4 blocks
        std::vector<float> expected;   // the automation's value at the same positions (an AutomationDisplay of the song)
        std::vector<float> meter;      // the peak of the insert the target acts on (a send: its destination), every 4 blocks
        float sourcePeak = 0.0f;       // a send: the peak of its source insert over the window (silent: nothing to hear)
    };

    // How a run is driven: `ahead` = render-ahead; `clipLaunch` = the Clip-launch layout's override rule (Live's
    // Re-Enable Automation); `touchFirst` = the user moved the control by hand once while stopped, before Play (FL: the
    // automation takes over again at Play)
    // `lanes`: 0 = the song as imported; 1 = a flat automation lane of the target added under its track (the Playlist
    // track's lane chooser / Create automation lane, Clip-launch's envelope); 2 = the clips moved into their tracks' lanes
    // (Playlist › "Move automation clips into their tracks' lanes")
    struct AutoLiveMode
    {
        bool ahead = false, clipLaunch = false, touchFirst = false;
        int lanes = 0;
    };

    static AutoLiveRun autoLivePlay (const lynx::Song& song, const lynx::AutomationTarget& target, double fromTick, double ticks,
                                     AutoLiveMode mode)
    {
        const auto wasAhead = lynx::renderAheadOption().load();
        lynx::renderAheadOption().store (mode.ahead);
        AutoLiveDevice device;
        AutoLiveRun run;
        {
            lynx::AudioEngine e (true, -1, false, true);
            e.setRenderAheadAlways (mode.ahead);
            auto played = song;
            if (mode.lanes == 1)
                lynx::autolanes::addLane (played, lynx::autolanes::trackFor (played, target), target, "lane", played.targetValue (target));
            else if (mode.lanes == 2)
                lynx::laneview::clipsToLanes (played);
            e.setSong (played);
            e.setLiveAutomationOverride (mode.clipLaunch);
            auto& cb = e.deviceCallback();
            cb.audioDeviceAboutToStart (&device);
            std::vector<float> l (512), r (512);
            float* outs[] = { l.data(), r.data() };
            juce::AudioIODeviceCallbackContext context;
            // a pre-roll of one bar (half a second of wall time first): the disk streams of the audio clips and the
            // render-ahead thread are under way when the measured window starts, as when a song plays into it
            std::this_thread::sleep_for (std::chrono::milliseconds (500));
            const auto preRoll = std::min (fromTick, 4.0 * (double) lynx::ppq);
            e.stop();
            e.seek (fromTick - preRoll);
            cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, context);
            if (mode.touchFirst)
            {
                e.noteTweaked (target);
                e.timerCallbackForTests();
                cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, context);
            }
            e.play();
            {
                const auto preBlocks = (int) std::ceil (preRoll / (double) lynx::ppq * 60.0 / song.bpm * 48000.0 / 512.0);
                auto at = std::chrono::steady_clock::now();
                for (int k = 0; k < preBlocks; ++k)
                {
                    cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, context);
                    if (k % 4 == 3)
                        e.timerCallbackForTests();
                    at += std::chrono::microseconds (2667);
                    std::this_thread::sleep_until (at);
                }
            }
            lynx::AutomationDisplay reference;
            // (a send: its destination; a channel's volume: the channel's insert)
            const auto channelTarget = target.kind == lynx::AutomationTargetKind::channelVolume;
            const auto meterTrack = target.kind == lynx::AutomationTargetKind::mixerSend ? target.slot
                                    : channelTarget ? (target.index >= 0 && target.index < (int) song.channels.size() ? song.channels[(std::size_t) target.index].mixerTrack : -1)
                                                    : target.index;
            float peak = 0.0f;
            auto next = std::chrono::steady_clock::now();
            const auto blocks = (int) std::ceil (ticks / (double) lynx::ppq * 60.0 / song.bpm * 48000.0 / 512.0);
            for (int k = 0; k < blocks; ++k)
            {
                cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, context);
                run.out.insert (run.out.end(), l.begin(), l.end());
                if (meterTrack >= 0)
                    peak = std::max ({ peak, e.getMeters().takePeak (meterTrack, 0), e.getMeters().takePeak (meterTrack, 1) });
                if (target.kind == lynx::AutomationTargetKind::mixerSend && target.index >= 0)
                    run.sourcePeak = std::max ({ run.sourcePeak, e.getMeters().takePeak (target.index, 0), e.getMeters().takePeak (target.index, 1) });
                if (k % 4 == 3)
                {
                    run.meter.push_back (std::exchange (peak, 0.0f));
                    e.timerCallbackForTests(); // (the engine's periodic step: released targets, publishing)
                    const auto& shown = e.getAutomationDisplay();
                    const auto v = shown.valueOf (target);
                    run.shown.push_back (v ? *v : std::numeric_limits<float>::quiet_NaN());
                    const auto tick = fromTick + (double) (k + 1) * 512.0 / 48000.0 * song.bpm / 60.0 * (double) lynx::ppq;
                    reference.update (played, 1, tick, true);
                    const auto x = reference.valueOf (target);
                    run.expected.push_back (x ? *x : std::numeric_limits<float>::quiet_NaN());
                }
                // (at a quarter of the device's pace: the disk streams and the render-ahead thread keep up, as on a device)
                next += std::chrono::microseconds (2667);
                std::this_thread::sleep_until (next);
            }
            e.stop();
            cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, context);
            cb.audioDeviceStopped();
        }
        lynx::renderAheadOption().store (wasAhead);
        return run;
    }

    // the automated targets worth playing (mixer volume, send level, effect parameter, channel volume) and, for each, the
    // two-bar window where its automation moves the most (sampled every beat with an AutomationDisplay of the song)
    struct AutoLiveCase
    {
        lynx::AutomationTarget target;
        int automation = -1;
        double from = 0.0, range = 0.0;
    };

    static std::vector<AutoLiveCase> autoLiveCases (const lynx::Song& song, int perKind)
    {
        using K = lynx::AutomationTargetKind;
        std::vector<AutoLiveCase> found;
        lynx::AutomationDisplay d;
        double end = 0.0;
        for (const auto& c : song.playlist)
            if (c.kind == lynx::ClipKind::automation)
                end = std::max (end, (double) (c.start + c.length));
        const auto bar = 4.0 * (double) lynx::ppq, beat = (double) lynx::ppq;
        std::map<std::tuple<int, int, int, int>, std::vector<float>> curves;
        for (double tick = 0.0; tick < end; tick += beat)
        {
            d.update (song, 1, tick, true);
            for (const auto& t : d.automatedTargets())
            {
                if (t.kind != K::mixerVolume && t.kind != K::mixerSend && t.kind != K::effectParam && t.kind != K::channelVolume)
                    continue;
                auto& curve = curves[{ (int) t.kind, t.index, t.slot, t.param }];
                const auto v = d.valueOf (t);
                curve.push_back (v ? *v : std::numeric_limits<float>::quiet_NaN());
            }
        }
        for (const auto& [key, curve] : curves)
        {
            AutoLiveCase c;
            c.target = { (K) std::get<0> (key), std::get<1> (key), std::get<2> (key), std::get<3> (key) };
            const auto span = (std::size_t) (2.0 * bar / beat);
            for (std::size_t i = 0; i + span <= curve.size(); ++i)
            {
                float lo = 2.0f, hi = -1.0f;
                for (std::size_t j = i; j < i + span; ++j)
                    if (! std::isnan (curve[j]))
                        lo = std::min (lo, curve[j]), hi = std::max (hi, curve[j]);
                if (hi - lo > c.range)
                    c.range = hi - lo, c.from = (double) i * beat;
            }
            for (int a = 0; a < (int) song.automations.size(); ++a)
                if (song.automations[(std::size_t) a].target == c.target)
                    c.automation = a;
            if (c.range > 0.15)
                found.push_back (c);
        }
        // the widest-moving ones of each kind
        std::stable_sort (found.begin(), found.end(), [] (const AutoLiveCase& a, const AutoLiveCase& b) { return a.range > b.range; });
        std::vector<AutoLiveCase> out;
        std::map<int, int> taken;
        for (const auto& c : found)
            if (taken[(int) c.target.kind]++ < perKind)
                out.push_back (c);
        return out;
    }

    static juce::String autoLiveName (const lynx::Song& song, const lynx::AutomationTarget& t)
    {
        using K = lynx::AutomationTargetKind;
        const auto track = [&song] (int i) {
            return i >= 0 && i < (int) song.mixer.size() ? juce::String (i) + " " + juce::String::fromUTF8 (song.mixer[(std::size_t) i].name.c_str()) : juce::String (i);
        };
        switch (t.kind)
        {
            case K::mixerVolume: return "volume of insert " + track (t.index);
            case K::mixerSend: return "send " + track (t.index) + " -> " + track (t.slot);
            case K::effectParam: return "effect param " + juce::String (t.param) + " of slot " + juce::String (t.slot + 1) + " on insert " + track (t.index);
            case K::channelVolume: return "volume of channel " + juce::String (t.index);
            default: return "target";
        }
    }

    // "OK" or what failed; the per-case numbers go to `log`
    static juce::String autoLiveCheck (const lynx::Song& song, juce::String& log, int perKind, bool allModes)
    {
        auto rms = [] (const std::vector<float>& x) {
            double s = 0.0;
            for (const auto v : x)
                s += (double) v * v;
            return x.empty() ? 0.0 : std::sqrt (s / (double) x.size());
        };
        auto rmsDiff = [] (const std::vector<float>& a, const std::vector<float>& b) {
            double s = 0.0;
            const auto n = std::min (a.size(), b.size());
            for (std::size_t i = 0; i < n; ++i)
                s += (double) (a[i] - b[i]) * (a[i] - b[i]);
            return n == 0 ? 0.0 : std::sqrt (s / (double) n);
        };
        juce::String result = "OK";
        // (what the import made of every automation clip, for the report)
        for (int a = 0; a < (int) song.automations.size(); ++a)
        {
            const auto& x = song.automations[(std::size_t) a];
            int clips = 0;
            for (const auto& c : song.playlist)
                clips += c.kind == lynx::ClipKind::automation && c.automation == a ? 1 : 0;
            log << "  automation " << a << " \"" << juce::String::fromUTF8 (x.name.c_str()) << "\": kind " << (int) x.target.kind << " index "
                << x.target.index << " slot " << x.target.slot << " param " << x.target.param << ", links " << (int) x.links.size() << ", clips "
                << clips << (x.muted ? ", muted" : "") << "\n";
        }
        const auto cases = autoLiveCases (song, perKind);
        if (cases.empty())
            return "ERROR no automated mixer volume, send or effect parameter that moves";
        const auto bar = 4.0 * (double) lynx::ppq;
        std::vector<AutoLiveMode> modes { { false, false, false, 0 }, { true, false, false, 0 } };
        if (allModes)
            modes.insert (modes.end(), { { false, true, false, 0 }, { false, false, true, 0 }, { false, true, true, 0 }, { false, false, false, 1 },
                                         { false, true, false, 1 }, { false, false, false, 2 }, { true, true, false, 2 } });
        for (const auto& c : cases)
        {
            auto without = song;
            // (every clip that drives the target: its own automations' and those linked to it among other controls)
            const auto drives = [&c] (const lynx::Automation& a) {
                return a.target == c.target
                       || std::any_of (a.links.begin(), a.links.end(), [&c] (const lynx::Automation::Link& l) { return l.target == c.target; });
            };
            for (auto& clip : without.playlist)
                if (clip.kind == lynx::ClipKind::automation && clip.automation >= 0 && clip.automation < (int) without.automations.size()
                    && drives (without.automations[(std::size_t) clip.automation]))
                    clip.muted = true;
            for (const auto& m : modes)
            {
                const auto with = autoLivePlay (song, c.target, c.from, 2.0 * bar, m);
                const auto off = autoLivePlay (without, c.target, c.from, 2.0 * bar, m);
                const auto level = rms (with.meter), moved = rmsDiff (with.meter, off.meter), master = rmsDiff (with.out, off.out);
                int follows = 0, compared = 0;
                for (std::size_t i = 0; i < with.shown.size() && i < with.expected.size(); ++i)
                    if (! std::isnan (with.expected[i]))
                    {
                        ++compared;
                        follows += ! std::isnan (with.shown[i]) && std::abs (with.shown[i] - with.expected[i]) < 0.02f ? 1 : 0;
                    }
                const auto modeName = juce::String (m.ahead ? "render ahead" : "real time") + (m.clipLaunch ? ", Clip-launch" : "")
                                      + (m.touchFirst ? ", touched before Play" : "")
                                      + (m.lanes == 1 ? ", a flat lane added" : m.lanes == 2 ? ", clips moved into lanes" : "");
                const auto sourceSilent = c.target.kind == lynx::AutomationTargetKind::mixerSend && with.sourcePeak <= 1.0e-3f && off.sourcePeak <= 1.0e-3f;
                if (level <= 1.0e-3 || sourceSilent)
                    log << (sourceSilent ? "  (the send's source is silent in this window: not judged) " : "  (the insert is silent in this window: not judged) ");
                log << autoLiveName (song, c.target) << " (automation " << c.automation << ", bar " << juce::String (c.from / bar + 1.0, 1)
                    << ", range " << juce::String (c.range, 3) << ", " << modeName << "): level " << juce::String (level, 5)
                    << ", with vs without " << juce::String (moved, 5) << " (Master " << juce::String (master, 5) << "), shown follows " << follows << " / " << compared << "\n";
                // Live's override (Clip-launch, a control moved by hand): neither heard nor shown until Re-Enable Automation
                const auto overridden = m.clipLaunch && m.touchFirst;
                if (! overridden && ! sourceSilent && level > 1.0e-3 && moved < 1.0e-2 * level && result == "OK")
                    result = "ERROR " + autoLiveName (song, c.target) + " (" + modeName + "): its automation is not heard";
                // (80 %: the display reads the playhead the engine estimates from the wall clock, and this loop runs at four
                // times the device's pace, so right at a clip's step the shown value may be a block early or late)
                if (! overridden && compared > 0 && follows * 10 < compared * 8 && result == "OK")
                    result = "ERROR " + autoLiveName (song, c.target) + " (" + modeName + "): the control does not show its automation";
                if (overridden && ((! sourceSilent && level > 1.0e-3 && moved > 1.0e-2 * level) || follows > 0) && result == "OK")
                    result = "ERROR " + autoLiveName (song, c.target) + " (" + modeName + "): overridden by hand, yet its automation still plays or shows";
            }
        }
        return result;
    }

    // An FL project made here (no FL Studio needed): a LYNX Synth holding a chord into insert 1, a Fruity Filter there,
    // insert 1 sent to insert 2 (a bus) besides the Master; automation clips on insert 1's volume (split into two clips,
    // as the reporter's), on the send 1 -> 2 and on the filter's cutoff; written as FL writes it and imported back.
    static lynx::Song autoLiveFlSong()
    {
        using namespace lynx;
        auto song = Song::makeDefault();
        for (auto& p : song.patterns)
            p.notes.clear(), p.events.clear();
        song.playlist.clear();
        song.automations.clear();
        song.mode = PlayMode::song;
        constexpr std::int64_t bar = 4 * ppq;
        ChannelDef synth;
        synth.name = "Pad";
        synth.instrument = InstrumentKind::synth;
        synth.mixerTrack = 1;
        song.channels.clear();
        song.channels.push_back (synth);
        while (song.mixer.size() < 4)
            song.mixer.emplace_back();
        song.mixer[1].name = "PLUCK 2";
        song.mixer[2].name = "MUSIC BUS";
        song.mixer[1].effects.clear();
        song.mixer[1].effects.push_back (makeEffectSlot (EffectKind::fruityFilter));
        song.setRoute (1, 0, true, 1.0f);
        song.setRoute (1, 2, true, 1.0f);
        song.setRoute (2, 0, true, 1.0f);
        if (song.patterns.empty())
            song.patterns.emplace_back();
        for (const auto key : { 48, 55, 60, 64 })
            song.patterns[0].notes.push_back ({ 0, 8 * bar, 0, key, 0.8f });
        song.playlist.push_back ({ 0, 0, 0, 8 * bar, 0 });
        const auto addClips = [&song, bar] (const char* name, AutomationTarget target, std::vector<std::pair<float, float>> ramps) {
            for (std::size_t i = 0; i < ramps.size(); ++i)
            {
                Automation a;
                a.name = name;
                a.target = target;
                a.points = { { 0, ramps[i].first, 0.0f }, { 4 * bar, ramps[i].second, 0.0f } };
                song.automations.push_back (a);
                Clip c;
                c.kind = ClipKind::automation;
                c.automation = (int) song.automations.size() - 1;
                c.track = 2 + (int) song.automations.size();
                c.start = (std::int64_t) i * 4 * bar;
                c.length = 4 * bar;
                song.playlist.push_back (c);
            }
        };
        addClips ("PLUCK 2 - Volume", { AutomationTargetKind::mixerVolume, 1 }, { { 0.8f, 0.1f }, { 0.1f, 0.8f } });
        addClips ("PLUCK 2 - To MUSIC BUS", { AutomationTargetKind::mixerSend, 1, 2 }, { { 0.0f, 1.0f }, { 1.0f, 0.0f } });
        addClips ("Filter - Cutoff frequency", { AutomationTargetKind::effectParam, 1, 0, 0 }, { { 1.0f, 0.15f }, { 0.15f, 1.0f } });
        flpexport::Options options;
        flpexport::Report report;
        return importFlp (flp::parseProject (flpexport::exportFlp (song, options, report)));
    }

    void runAutoLive190qpTest (const juce::String& projectPath, const juce::String& reportPath, int perKind, bool allModes)
    {
        juce::String log, error;
        juce::String result;
        {
            lynx::AudioEngine loader (false);
            const juce::File input (projectPath == "synthetic" ? juce::String() : projectPath);
            auto ok = false;
            if (projectPath == "synthetic")
                loader.setSong (autoLiveFlSong()), ok = true;
            else if (input.hasFileExtension ("flp;als"))
                ok = loader.importFlpFile (input, error);
            else if (auto song = lynx::ProjectFile::load (input, error))
                loader.setSong (std::move (*song)), ok = true;
            result = ok ? autoLiveCheck (loader.getSong(), log, perKind, allModes) : "ERROR could not open: " + error;
        }
        juce::File (reportPath).replaceWithText (log + "autolive=" + result + "\n");
        setApplicationReturnValue (result == "OK" ? 0 : 1);
        quit();
    }

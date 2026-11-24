    // --pickerpaint190qi <report> (round 190qi): the paint gate of the plugin lists and the Browser — the Plugin picker
    // (effects, instruments), the Quick-add palette, the Plugin manager and the Browser's tree drawn offscreen with
    // 120 scanned plugin builds (no files), FRAMES whole repaints each after 3 warm-up ones; per view the median of the
    // instructions the message thread retires per frame (Linux), its CPU ms and the first frame's pixel hash. Only
    // functions older than the round, so the same file builds on the base for the before / after numbers. Included in
    // the app class of Main.cpp.
    void runPickerPaint190qi (const juce::File& reportFile)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        ui::motion::enabled() = false;
        const auto frames = juce::jmax (5, juce::SystemStats::getEnvironmentVariable ("LYNX_PAINTBENCH_FRAMES", "30").getIntValue());
        AudioEngine engine (false, -1, false);
        engine.setSong (Song::makeDefault());
        {
            auto& list = engine.getPluginHost().getKnownPlugins();
            int uid = 5000;
            const char* kinds[] { "Comp", "EQ", "Verb", "Delay", "Synth", "Sampler" };
            for (int i = 0; i < 48; ++i)
                for (const auto* f : { "VST", "VST3", "CLAP" })
                {
                    if ((i % 3 == 1 && juce::String (f) == "CLAP") || (i % 3 == 2 && juce::String (f) != "VST3"))
                        continue;
                    juce::PluginDescription d;
                    d.name = d.descriptiveName = juce::String (kinds[i % 6]) + " " + juce::String (i);
                    d.manufacturerName = "Vendor " + juce::String (i % 11);
                    d.pluginFormatName = f;
                    d.isInstrument = i % 6 >= 4;
                    d.category = d.isInstrument ? "Instrument|Synth" : "Fx|Dynamics";
                    d.fileOrIdentifier = "/plugins/" + juce::String (f) + "/" + d.name;
                    d.uniqueId = d.deprecatedUid = ++uid;
                    d.numOutputChannels = 2;
                    list.addType (d);
                }
        }
        const platform::ThreadInstructions instructions;
        juce::String text;
        auto measure = [&] (juce::Component& c, const juce::String& name) {
            juce::Image image (juce::Image::ARGB, juce::jmax (1, c.getWidth()), juce::jmax (1, c.getHeight()), true);
            std::vector<double> ms;
            std::vector<long long> instr;
            juce::String hash;
            for (int f = -3; f < frames; ++f)
            {
                engine.dispatchPendingMessages();
                c.repaint();
                const auto t0 = threadCpuSeconds();
                const auto i0 = instructions.now();
                {
                    juce::Graphics g (image);
                    g.fillAll (juce::Colours::black);
                    c.paintEntireComponent (g, true);
                }
                if (const auto i1 = instructions.now(); f >= 0 && i0 >= 0 && i1 >= i0)
                    instr.push_back (i1 - i0);
                if (f >= 0)
                    ms.push_back (1000.0 * (threadCpuSeconds() - t0));
                if (f == 0)
                {
                    const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
                    std::uint64_t h = 1469598103934665603ull;
                    for (int y = 0; y < data.height; ++y)
                        for (int x = 0; x < data.width; ++x)
                            h = (h ^ data.getPixelColour (x, y).getARGB()) * 1099511628211ull;
                    hash = juce::String::toHexString ((juce::int64) h);
                }
            }
            std::sort (ms.begin(), ms.end());
            text << name << ": median " << juce::String (ms[ms.size() / 2], 3) << " ms";
            if (instr.size() == ms.size())
            {
                std::sort (instr.begin(), instr.end());
                text << ", instructions median " << juce::String ((juce::int64) instr[instr.size() / 2]);
            }
            text << ", pixels " << hash << "\n";
        };
        for (const auto instruments : { false, true })
        {
            showPluginPicker (engine, instruments, [] (PluginRef) {}, instruments ? std::function<void (ChannelDef)> ([] (ChannelDef) {}) : nullptr);
            if (auto* window = findToolWindow ("pluginpicker"))
            {
                auto* picker = window->getContentComponent();
                picker->setSize (760, 520);
                measure (*picker, instruments ? "picker instruments" : "picker effects");
                delete window;
            }
        }
        {
            QuickAddTargets targets;
            targets.addInstrument = [] (ChannelDef) {};
            targets.addEffect = [] (EffectSlot) {};
            auto palette = makeQuickAddPalette (engine, std::move (targets));
            palette->setSize (720, 460);
            measure (*palette, "quick-add palette");
        }
        {
            auto manager = makePluginManagerPanel (engine, true);
            manager->setSize (1040, 600);
            measure (*manager, "plugin manager");
        }
        {
            const auto library = reportFile.getParentDirectory().getChildFile ("pickerpaint-library");
            for (int i = 0; i < 24; ++i)
            {
                const auto f = library.getChildFile ("Drums").getChildFile ("Hit " + juce::String (i) + ".wav");
                f.getParentDirectory().createDirectory();
                if (! f.existsAsFile())
                    writeWaveDemo (f, 120.0);
            }
            PluginHost::settingsFolder().createDirectory();
            PluginHost::settingsFolder().getChildFile ("browser-folders.txt").replaceWithText (library.getFullPathName());
            BrowserComponent::readPrefs = false;
            BrowserComponent browser (engine);
            browser.setSize (260, 720);
            browser.setTab (BrowserComponent::allTab);
            browser.locateFile (library.getChildFile ("Drums").getChildFile ("Hit 3.wav"));
            measure (browser, "browser tree");
        }
        text << "result=OK\n";
        reportFile.replaceWithText (text);
    }

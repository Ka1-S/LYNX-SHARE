    // --lynxmodelshot190ni <prefix> (round 190ni): LYNX Model off screen. Its channel window for each preset of its bank
    // (01-marimba ... 08), the window's strike point dragged to the end of the bar (09), the Browser's plugin database at
    // Generators › FL engines (10; FL's category for it: Synth special); checks (prefix-checks.txt): the .lynx round trip of a LYNX Model channel, the copy
    // for LYNX 0.1.28, the generator list's entry, the bank and the strike point's mapping; the window's paint cost.
    // @150: LYNX_SHOT_SCALE=1.5 LYNX_SHOT_SUFFIX=@150. Included in the app class of Main.cpp.
    bool runLynxModelShot190ni (const juce::String& prefix)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        juce::File (prefix).getParentDirectory().createDirectory();
        juce::String checks;
        bool allOk = true;
        const auto check = [&checks, &allOk] (const juce::String& what, bool ok, const juce::String& detail = {}) {
            checks << (ok ? "ok   " : "FAIL ") << what << (detail.isEmpty() ? juce::String() : "  (" + detail + ")") << "\n";
            allOk = allOk && ok;
        };
        const auto savePage = [&prefix] (juce::Component& c, const juce::String& name) {
            const auto image = shot::snapshot (c);
            const auto f = shot::file (juce::File (prefix + "-" + name + ".png"));
            f.deleteFile();
            juce::PNGImageFormat png;
            bool ok = false;
            if (auto out = f.createOutputStream())
                ok = png.writeImageToStream (image, *out);
            return ok && image.getWidth() > 0;
        };

        AudioEngine offline (false);
        const auto& bank = banks::bankOf (InstrumentKind::lynxModel);
        check ("bank: 8 presets, the Marimba first", bank.size() == 8 && bank[0].name == "Marimba", juce::String ((int) bank.size()));
        offline.editSong ([] (Song& s) {
            if (s.channels.empty())
                s.channels.resize (1);
            ChannelDef c;
            c.instrument = InstrumentKind::lynxModel;
            c.mixerTrack = s.channels[0].mixerTrack;
            c.modelParams = lynxModelDefaults();
            c.name = "LYNX Model";
            s.channels[0] = c;
        }, false);
        offline.dispatchPendingMessages();
        auto page = makeLynxModelEditor (offline, [] (Song& s) -> ChannelDef* { return s.channels.empty() ? nullptr : &s.channels[0]; }, 0);
        check ("window", page != nullptr && page->getWidth() >= 700 && page->getHeight() >= 300,
               page != nullptr ? juce::String (page->getWidth()) + " x " + juce::String (page->getHeight()) : juce::String());
        if (page == nullptr)
        {
            juce::File (prefix + "-checks.txt").replaceWithText (checks);
            return false;
        }
        const char* files[] = { "01-marimba", "02-vibraphone", "03-xylophone", "04-glockenspiel", "05-tubular-bells", "06-kalimba",
                                "07-vibraphone-motor", "08-marimba-soft" };
        for (int b = 0; b < (int) bank.size() && b < 8; ++b)
        {
            offline.editSong ([&bank, b] (Song& s) {
                if (auto loaded = banks::withPreset (s.channels[0], b))
                    s.channels[0] = *loaded;
            }, false);
            offline.dispatchPendingMessages();
            page->repaint();
            check (juce::String ("shot ") + files[b], savePage (*page, files[b]));
        }
        // the strike point: a click maps back to the same position; dragged to the bar's end
        offline.editSong ([] (Song& s) {
            if (auto loaded = banks::withPreset (s.channels[0], 0))
                s.channels[0] = *loaded;
        }, false);
        offline.dispatchPendingMessages();
        const auto x0 = lynxmodelui::strikeX (*page, 0.25), x1 = lynxmodelui::strikeX (*page, 1.0);
        check ("strike point maps back", std::abs (lynxmodelui::positionAtX (*page, x0) - 0.25) < 0.01 && x1 > x0,
               juce::String (x0) + " / " + juce::String (x1));
        offline.editSong ([] (Song& s) { s.channels[0].modelParams[(std::size_t) lm::p::position] = 1.0f; }, false);
        offline.dispatchPendingMessages();
        page->repaint();
        check ("shot 09-strike-at-end", savePage (*page, "09-strike-at-end"));

        // the paint cost: 60 whole frames off screen, the best of 3 runs
        double best = 1.0e9;
        for (int run = 0; run < 3; ++run)
        {
            juce::Image image (juce::Image::ARGB, page->getWidth(), page->getHeight(), true);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int f = 0; f < 60; ++f)
            {
                juce::Graphics g (image);
                page->paintEntireComponent (g, false);
            }
            best = std::min (best, (juce::Time::getMillisecondCounterHiRes() - t0) / 60.0);
        }
        checks << "paint " << juce::String (best, 3) << " ms per frame (" << page->getWidth() << " x " << page->getHeight() << ")\n";

        // .lynx: the channel and its knobs come back exactly
        {
            auto song = Song::makeDefault();
            ChannelDef c;
            c.instrument = InstrumentKind::lynxModel;
            c.name = "Kalimba";
            c.modelParams = lynxModelPreset (lm::kalimba);
            c.modelParams[(std::size_t) lm::p::hardness] = 0.71f;
            song.channels.push_back (c);
            juce::String error;
            const auto back = ProjectFile::fromJson (ProjectFile::toJson (song), error);
            const bool same = back && back->channels.size() == song.channels.size() && back->channels.back().instrument == InstrumentKind::lynxModel
                              && back->channels.back().modelParams == c.modelParams;
            check (".lynx round trip", same, error);
            // the copy for LYNX 0.1.28: an empty Sampler, reported
            const auto copy = olderlynx::forRelease (song, "0.1.28");
            bool reported = false;
            for (const auto& l : copy.lost)
                reported = reported || juce::String (l.text).startsWith ("LYNX Model is not in this release");
            check ("copy for 0.1.28 reports LYNX Model", copy.ok && reported && ! copy.json.contains ("\"lynxmodel\""));
        }
        // the generator list (the Channel rack's +, the Browser's plugin database, the quick-add palette)
        {
            bool listed = false;
            for (const auto& e : generators::entries (offline))
                if (e.kind == InstrumentKind::lynxModel && e.label == "LYNX Model" && e.make)
                    if (const auto c = e.make())
                        listed = c->instrument == InstrumentKind::lynxModel && (int) c->modelParams.size() == lm::stateSize;
            check ("generator list entry", listed);
            check ("plugin database folder", plugincatalog::flCategory ("LYNX Model", true) == "Synth special");
        }
        // the Browser's plugin database at Generators › Synth special
        {
            BrowserComponent::readPrefs = false;
            BrowserComponent browser (offline);
            browser.setSize (260, 640);
            browser.showPluginDatabase ("FL engines", 1); // (LYNX's own engines sit there, as Tetra, Helix and Braid)
            offline.dispatchPendingMessages();
            const auto labels = browser.rowLabelsForCheck();
            check ("browser row", labels.contains ("LYNX Model"), labels.joinIntoString (", ").substring (0, 300));
            const auto image = shot::paint (browser, theme::colour::bg2);
            const auto f = shot::file (juce::File (prefix + "-10-browser.png"));
            f.deleteFile();
            juce::PNGImageFormat png;
            bool ok = false;
            if (auto out = f.createOutputStream())
                ok = png.writeImageToStream (image, *out);
            check ("shot 10-browser", ok);
        }
        checks << "result=" << (allOk ? "OK" : "FAIL") << "\n";
        juce::File (prefix + "-checks.txt").replaceWithText (checks);
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        return allOk;
    }

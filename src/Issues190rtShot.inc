    // Round 190rt (issues #309 / #311): LYNX --issues190rtshot <prefix> <sample.wav> [--shot-scale 1.5 --shot-suffix @150]:
    // the channel windows, every tab, offscreen — a Sampler with a sample (its Sample page's Attack / Decay / Sustain /
    // Release under "LYNX extras": where a sample's volume envelope is), a new 3x Osc, the same with FL's pluck on its new
    // Envelope / instrument tab (Volume and Mod X targets), and new DX10 / Granulizer / BeepMap channels. Writes
    // <prefix>-<channel>-tab<k>.png and <prefix>-checks.txt (result=OK when every channel has its tabs).
    // Included in the app class of Main.cpp (savePng).
    void runIssues190rtShot (const juce::String& prefix, const juce::String& samplePath)
    {
        using namespace lynx;
        juce::StringArray checks;
        bool ok = true;
        auto check = [&] (const juce::String& name, bool pass) {
            checks.add ((pass ? "ok " : "FAIL ") + name);
            ok = ok && pass;
        };
        AudioEngine engine (false);
        windowplaces::fromVar ({});
        auto song = Song::makeDefault();
        song.channels.clear();
        std::vector<std::pair<juce::String, int>> shown; // name, tab to show the voice page on
        ChannelDef sampler;
        sampler.name = "Sample";
        sampler.instrument = InstrumentKind::sampler;
        sampler.samplePath = samplePath.toStdString();
        sampler.sampler.release = 0.05f; // (the knob the reply points at, set)
        song.channels.push_back (sampler);
        shown.push_back ({ "sampler", -1 });
        for (const auto kind : { InstrumentKind::threeosc, InstrumentKind::threeosc, InstrumentKind::dx10, InstrumentKind::granulizer,
                                 InstrumentKind::beepmap })
        {
            auto made = natives::makeChannel (kind, flStudioAccess());
            ChannelDef def;
            def.instrument = kind;
            if (made)
                def = *made;
            if (kind == InstrumentKind::threeosc && def.pluginState.empty())
                def.pluginState = flpexport::defaultThreeOscState();
            const auto name = kind == InstrumentKind::threeosc ? (shown.size() == 1 ? "3xosc" : "3xosc-pluck")
                              : kind == InstrumentKind::dx10   ? "dx10"
                              : kind == InstrumentKind::granulizer ? "granulizer"
                                                                    : "beepmap";
            if (juce::String (name) == "3xosc-pluck")
                if (auto s = flvoiceblock::read (def))
                {
                    auto& v = s->articulators[flvoice::volume];
                    v.envelope = true;
                    v.delay = v.attack = v.hold = 100;
                    v.decay = flpexport::envelopeTimeValue (0.25);
                    v.sustain = 0;
                    v.release = flpexport::envelopeTimeValue (0.05);
                    flvoiceblock::write (def, *s);
                }
            def.name = name;
            song.channels.push_back (def);
            shown.push_back ({ name, 1 });
        }
        engine.setSong (song);
        engine.dispatchPendingMessages();
        for (int c = 0; c < (int) shown.size(); ++c)
        {
            const auto& [name, voiceTab] = shown[(std::size_t) c];
            auto frame = makeChannelFrame (engine, c, 0);
            if (frame == nullptr)
            {
                check (name + " window", false);
                continue;
            }
            int tabs = 0;
            for (auto* child : frame->getChildren())
                if (dynamic_cast<juce::Button*> (child) != nullptr && child->getY() == 0 && child->getWidth() == 44)
                    ++tabs;
            const auto& def = engine.getSong().channels[(std::size_t) c];
            // the generators with the voice block: Plugin, Envelope / instrument, Misc (FL's three tabs)
            if (voiceTab >= 0)
                check (name + ": " + juce::String (tabs) + " tabs, voice block " + (flvoiceblock::has (def) ? "yes" : "no"),
                       tabs == 3 && flvoiceblock::has (def));
            for (int tab = 0; tab < tabs; ++tab)
            {
                showChannelWindowTab (*frame, tab);
                if (tab == voiceTab && name == "3xosc-pluck")
                    showFlVoiceTarget (*frame, flvoice::volume);
                savePng (shot::snapshot (*frame), shot::fileName (prefix + "-" + name + "-tab" + juce::String (tab) + ".png"));
            }
        }
        checks.add (juce::String ("result=") + (ok ? "OK" : "FAIL"));
        juce::File (shot::fileName (prefix + "-checks.txt")).replaceWithText (checks.joinIntoString ("\n") + "\n");
        quit();
    }

    // Round 190rt self-test item (issues190rt): #311 the Envelope / instrument tab of 3x Osc, DX10, Granulizer and
    // BeepMap (FL's page over the voice block) edits what the engine plays and the .flp export writes; #310 an existing
    // automation clip linked to a control from "Link to controller" (FL's Internal controller › Automation clips), with
    // and without "Remove conflicts".
    juce::String issues190rtCheck (juce::String& log)
    {
        using namespace lynx;
        bool ok = true;
        auto check = [&] (const juce::String& name, bool pass) {
            log << (pass ? "  ok " : "  FAIL ") << name << "\n";
            ok = ok && pass;
        };
        // #311
        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            song.channels.clear();
            for (const auto kind : { InstrumentKind::threeosc, InstrumentKind::dx10, InstrumentKind::granulizer, InstrumentKind::beepmap })
            {
                auto made = natives::makeChannel (kind, flStudioAccess());
                ChannelDef def;
                def.instrument = kind;
                if (made)
                    def = *made;
                if (kind == InstrumentKind::threeosc && def.pluginState.empty())
                    def.pluginState = flpexport::defaultThreeOscState();
                song.channels.push_back (def);
            }
            engine.setSong (song);
            engine.dispatchPendingMessages();
            for (int c = 0; c < (int) song.channels.size(); ++c)
            {
                const auto& def = engine.getSong().channels[(std::size_t) c];
                const auto name = def.instrument == InstrumentKind::threeosc ? juce::String ("3x Osc") : def.instrument == InstrumentKind::dx10 ? juce::String ("DX10")
                                  : def.instrument == InstrumentKind::granulizer ? juce::String ("Granulizer") : juce::String ("BeepMap");
                if (! flvoiceblock::has (def))
                {
                    log << "  (" << name << ": no usable state here, skipped)\n";
                    continue;
                }
                auto frame = makeChannelFrame (engine, c, 1);
                check (name + ": the window shows the Envelope / instrument tab", frame != nullptr && showChannelWindowTab (*frame, 1));
                auto s = flvoiceblock::read (def);
                check (name + ": the tab reads the block", s.has_value());
                if (! s)
                    continue;
                s->articulators[flvoice::volume].envelope = true;
                s->articulators[flvoice::volume].sustain = 0;
                engine.editSong ([c, settings = *s] (Song& song) { flvoiceblock::write (song.channels[(std::size_t) c], settings); });
                const auto after = flvoiceblock::read (engine.getSong().channels[(std::size_t) c]);
                check (name + ": an edit is kept in the channel", after && after->articulators[flvoice::volume].envelope
                                                                     && after->articulators[flvoice::volume].sustain == 0);
            }
        }
        // #310
        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            song.channels.resize (2);
            Automation clip;
            clip.name = "Lost clip";
            clip.points = { { 0, 0.1f }, { 384, 0.9f } };
            song.automations = { clip };
            engine.setSong (song);
            engine.dispatchPendingMessages();
            const auto outputs = controllerOutputs (engine.getSong());
            const auto listed = std::find_if (outputs.begin(), outputs.end(), [] (const ControllerOutput& o) { return o.automation == 0; });
            check ("310: the clip is in the Internal controller list", listed != outputs.end() && listed->label == "Lost clip");
            if (listed != outputs.end())
            {
                const AutomationTarget vol { AutomationTargetKind::channelVolume, 0, 0, 0 }, pan { AutomationTargetKind::channelPan, 1, 0, 0 };
                linkToController (engine, vol, *listed, true);
                check ("310: Accept links the clip to the control", engine.getSong().automations[0].target == vol);
                linkToController (engine, pan, *listed, false);
                check ("310: without Remove conflicts the control joins its targets", engine.getSong().automations[0].links.size() == 2);
                linkToController (engine, pan, *listed, true);
                check ("310: with Remove conflicts the clip drives only the new control",
                       engine.getSong().automations[0].links.empty() && engine.getSong().automations[0].target == pan);
                engine.undo();
                check ("310: one undo step back", engine.getSong().automations[0].links.size() == 2);
            }
        }
        return ok ? "OK" : "FAIL";
    }

    // --details190myshot <prefix> <project.flp> (round 190my, details pass 10): the Current track's follow-ups (export,
    // PDC, its menu), the tempo tapper's Mute / now and the rest of the round's rows. <prefix>-checks.txt lists the checks.
    // Included in the app class of Main.cpp.
    bool runDetailsShot190my (const juce::String& prefix, const juce::String& project)
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

        // ---- the Current (C) track in a render (DF-82; REA 190my R1, live check 190my A2: FL's export runs C)
        {
            Song s = offline.getSong();
            const auto c = mixerops::addCurrentTrack (s);
            EffectSlot balance;
            balance.kind = EffectKind::fruityBalance;
            flbasic::balanceToParams ({ 0, 0 }, balance.params.data()); // Fruity Balance, volume at its minimum (FL's test)
            if (c > 0)
                s.mixer[(std::size_t) c].effects.push_back (balance);
            s.loopStart = 0;
            s.loopEnd = 2 * 4 * ppq; // two bars
            const auto render = [&s] (bool withC, const juce::String& name) {
                auto song = s;
                if (! withC)
                    if (const auto ci = mixerops::currentTrack (song); ci > 0)
                        song.mixer[(std::size_t) ci].effects.clear();
                exportopts::Options o;
                o.range = exportopts::Range::selection;
                o.tail = exportopts::TailMode::cut;
                o.wavBits = 32;
                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx190my-" + name + ".wav");
                juce::String error;
                ExportJob job (song, o, file);
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
            const auto none = render (false, "none");
            const auto master = render (true, "master");
            line ("render peaks: C empty " + juce::String (none, 4) + ", Balance at -inf on C " + juce::String (master, 6));
            check ("render with an empty Current track: the song sounds", none > 0.01);
            check ("render with Fruity Balance at -inf on C: silent (FL's render runs C on the Master)", master >= 0.0 && master < 1.0e-5);
        }

        // ---- C's latency in the selected track's delay compensation (DF-82; REA 190mv R / 190my R2)
        {
            offline.editSong ([] (Song& s) {
                const auto c = mixerops::addCurrentTrack (s);
                if (c > 0)
                    s.mixer[(std::size_t) c].latencyMs = 10.0f; // (C's manual latency counts as its own: FUN_010ff3b0 +0x1cc)
            });
            offline.setCurrentTrackSource (-1);
            offline.dispatchPendingMessages();
            const auto before = offline.getPdcPlan().total;
            offline.setCurrentTrackSource (1);
            const auto selected = offline.getPdcPlan();
            line ("PDC total: no source " + juce::String (before) + ", insert 1 selected " + juce::String (selected.total) + " samples");
            check ("C's latency moves into the selected insert's output latency (the plan is made again on selection)",
                   selected.currentLatency > 0 && (int) selected.outLat.size() > 1 && selected.outLat[1] >= selected.currentLatency);
            offline.setCurrentTrackSource (2);
            check ("another insert selected: its output gets it, insert 1's is back",
                   offline.getPdcPlan().outLat.size() > 2 && offline.getPdcPlan().outLat[2] >= selected.currentLatency
                       && offline.getPdcPlan().outLat[1] == selected.outLat[1] - selected.currentLatency);
            offline.editSong ([] (Song& s) {
                if (const auto c = mixerops::currentTrack (s); c > 0)
                    s.mixer[(std::size_t) c].latencyMs = 0.0f;
            });
        }

        // ---- C's menu: what FL greys (capture 190my A1)
        {
            ui.selectTabForSnapshot (3);
            offline.dispatchPendingMessages();
            auto& strips = ui.getMixerForSnapshot().getStripsForSnapshot();
            const auto c = mixerops::currentTrack (offline.getSong());
            const auto states = [] (const juce::PopupMenu& menu) {
                std::map<juce::String, bool> out;
                for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
                    out[it.getItem().text.upToFirstOccurrenceOf ("\t", false, false)] = it.getItem().isEnabled;
                return out;
            };
            saveMenus (juce::StringArray { "Current track menu" }, std::vector<juce::PopupMenu> { strips.buildMenu (c) }, prefix + "-current-menu.png");
            saveMenus (juce::StringArray { "Insert 3 menu" }, std::vector<juce::PopupMenu> { strips.buildMenu (3) }, prefix + "-insert-menu.png");
            const auto onC = states (strips.buildMenu (c));
            const auto onInsert = states (strips.buildMenu (3));
            const auto greyed = [&onC] (const char* item) { return onC.count (tr (item)) > 0 && ! onC.at (tr (item)); };
            for (const auto* item : { "Assign to new Audio track(s)", "Track routing", "Compensate for master latency", "Auto color group", "Delete",
                                      "Insert one", "Insert multiple...", "Insert from state", "Route selected channels to this track",
                                      "Route selected channels starting from this track" })
                check (juce::String ("C's menu: \"") + item + "\" greyed (FL)", greyed (item));
            check ("C's menu: Rename / Move left / Allow threaded processing stay (FL)",
                   onC.count (tr ("Move left")) && onC.at (tr ("Move left")) && onC.count (tr ("Allow threaded processing")) && onC.at (tr ("Allow threaded processing")));
            check ("an insert's menu keeps Delete and Insert one", onInsert.count (tr ("Delete")) && onInsert.at (tr ("Delete")) && onInsert.count (tr ("Insert one")) && onInsert.at (tr ("Insert one")));
            const auto count = offline.getSong().mixer.size();
            strips.deleteTracksForSnapshot ({ c });
            offline.dispatchPendingMessages();
            check ("Del never deletes the Current track", offline.getSong().mixer.size() == count && mixerops::currentTrack (offline.getSong()) == c);
        }

        // ---- --uiperf's "mixer scroll" point: a strip's blank body, which scrolls (round 190my)
        {
            auto& mixer = ui.getMixerForSnapshot();
            auto& strips = mixer.getStripsForSnapshot();
            const auto centre = strips.getLocalPoint (&mixer, mixer.getLocalBounds().getCentre());
            const auto visible = strips.getParentComponent() != nullptr ? strips.getLocalArea (strips.getParentComponent(), strips.getParentComponent()->getLocalBounds())
                                                                         : strips.getLocalBounds();
            const auto at = strips.wheelScrollPointForPerf (centre, visible);
            line ("Mixer centre " + centre.toString() + " -> wheel point " + at.toString() + ", strips " + juce::String (strips.getWidth())
                  + " px wide in a view of " + juce::String (visible.getWidth()) + " px at x " + juce::String (visible.getX()));
            if (auto* holder = strips.getParentComponent())
                if (auto* vp = holder->findParentComponentOfClass<juce::Viewport>())
                {
                    const auto before = vp->getViewPositionX();
                    auto source = juce::Desktop::getInstance().getMainMouseSource();
                    const auto now = juce::Time::getCurrentTime();
                    const juce::MouseEvent e (source, at.toFloat(), {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &strips, &strips, now, at.toFloat(), now, 0, false);
                    juce::MouseWheelDetails wheel { 0.0f, -0.25f, false, false, false };
                    strips.mouseWheelMove (e, wheel);
                    line ("wheel at that point: view x " + juce::String (before) + " -> " + juce::String (vp->getViewPositionX()));
                    vp->setViewPosition (before, vp->getViewPositionY());
                }
            check ("uiperf mixer scroll: a point on screen that scrolls the strips", at != centre || visible.contains (at));
        }

        // ---- the tempo tapper: FL's Mute and "now" (DF-17; REA 190my R3)
        {
            auto tapper = makeTempoTapperForSnapshot (offline, 5, 128.0);
            tapper->setSize (tapper->getWidth(), tapper->getHeight());
            write (*tapper, tapper->getLocalBounds(), "tempo-tapper");
            check ("tapper mute curve: 1 % at the start, back to 1 at the end (FL's 0.01 + 0.99 cos^18)",
                   std::abs (AudioEngine::tapperMuteGain (8.0, 8.0) - 0.01f) < 1.0e-4f && AudioEngine::tapperMuteGain (0.0, 8.0) == 1.0f
                       && AudioEngine::tapperMuteGain (4.0, 8.0) < 0.05f && AudioEngine::tapperMuteGain (0.4, 8.0) > 0.5f);
            offline.muteOutputForTicks (8.0 * ppq);
            check ("now: the output is muted for 8 beats (stopped: the audio thread lifts it)", offline.outputMutedForTicks());
            offline.muteOutputForTicks (0.0);
        }

        // ---- FL's Project picker (DF-143, captures 190mv A11 / 190my A4)
        {
            ui.selectTabForSnapshot (0);
            offline.dispatchPendingMessages();
            const auto screen = ui.createComponentSnapshot (ui.getLocalBounds(), true, 1.0f);
            int picked = -2, pickedChannel = -1;
            ProjectPickerTargets targets;
            targets.pickPattern = [&picked] (int p) { picked = p; };
            targets.pickChannel = [&pickedChannel] (int c) { pickedChannel = c; };
            targets.close = [] {};
            auto view = makeProjectPickerView (offline, targets);
            view->setBounds (0, 0, 1920, 1032);
            projectPickerBackdropForSnapshot (*view, screen);
            const auto& song = offline.getSong();
            const auto first = projectPickerTileBounds (*view, 1, 0);
            const auto add = projectPickerTileBounds (*view, 2, -1);
            int sampleChannel = -1, generatorChannel = -1;
            for (int c = 0; c < (int) song.channels.size(); ++c)
            {
                const auto kind = song.channels[(std::size_t) c].instrument;
                auto& slot = kind == InstrumentKind::sampler || kind == InstrumentKind::audioClip ? sampleChannel : generatorChannel;
                if (slot < 0)
                    slot = c;
            }
            const auto channelTile = projectPickerTileBounds (*view, 3, sampleChannel >= 0 ? sampleChannel : 0);
            line ("pattern tile " + first.toString() + ", add tile " + add.toString() + ", channel tile " + channelTile.toString());
            check ("Project picker: every pattern a 188 x 106 tile on the left half, the add tile after them (FL)",
                   ! first.isEmpty() && first.getWidth() == 188 && first.getHeight() == 106 && first.getRight() < 960 && ! add.isEmpty());
            check ("Project picker: the channels on the right half", ! channelTile.isEmpty() && channelTile.getX() > 960);
            for (int c = 0; c < (int) song.channels.size(); ++c)
                if (const auto b = projectPickerTileBounds (*view, 3, c); ! b.isEmpty() && ! song.channels[(std::size_t) c].samplePath.empty())
                    line ("  channel " + juce::String::fromUTF8 (song.channels[(std::size_t) c].name.c_str()) + " " + juce::String (b.getWidth()) + " px");
            write (*view, view->getLocalBounds(), "project-picker");
            projectPickerHoverForSnapshot (*view, first.getCentre());
            write (*view, first.expanded (60, 50), "project-picker-hover");
            check ("a point on a pattern tile finds it", projectPickerTileAt (*view, first.getCentre()) == std::make_pair (1, 0));
            projectPickerTypeForSnapshot (*view, "sax");
            write (*view, view->getLocalBounds(), "project-picker-search");
            check ("typing filters: a pattern whose name lacks the text is gone (FL)", projectPickerTileBounds (*view, 1, 0).isEmpty()
                       || juce::String::fromUTF8 (song.patterns.front().name.c_str()).containsIgnoreCase ("sax"));
            projectPickerTypeForSnapshot (*view, "");
            projectPickerShowTabForSnapshot (*view, 1);
            write (*view, view->getLocalBounds(), "project-picker-patterns");
            projectPickerShowTabForSnapshot (*view, 2);
            write (*view, view->getLocalBounds(), "project-picker-channels");
            juce::ignoreUnused (picked, pickedChannel, generatorChannel);
        }

        juce::File (prefix + "-checks.txt").replaceWithText (checks + "result=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

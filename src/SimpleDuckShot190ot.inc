    // --simpleduck190otshot <folder> (round 190ot, research190oe P5): the Simple layout's "Duck under..." and shared Echo
    // send through the real main window, off screen, at 100 % and 150 %:
    //  - the track dock's Effects tab before (Duck under: None, Echo 0) and after (the pad ducks under the drums, Echo and
    //    Reverb sends), the window, the picker's menu before / after;
    //  - the same project in the Pattern layout's Mixer (an ordinary Fruity Limiter with FL's level-0 sidechain route);
    //  - .lynx round trip, the copies for 0.1.30 and 0.1.28 (nothing lost: ordinary slot, route and sends), the .flp export
    //    (written to <folder>/duck-export.flp for the FL check) and the dock's paint cost.
    // <folder>/checks.txt lists what was checked. Included in the app class of Main.cpp.
    bool runSimpleDuckShot190ot (const juce::String& folderPath)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (lynx::ui::showingForShots(), true);
        lynx::ui::motion::enabled() = false;
        const juce::File folder (folderPath);
        folder.createDirectory();
        juce::String checks;
        bool allOk = true;
        const auto check = [&checks, &allOk] (const juce::String& what, bool ok) {
            checks << (ok ? "ok   " : "FAIL ") << what << "\n";
            allOk = allOk && ok;
        };
        const auto note = [&checks] (const juce::String& text) { checks << "     " << text << "\n"; };
        using namespace lynx;
        namespace st = simpletracks;
        const auto save = [&folder] (juce::Component& c, juce::Rectangle<int> area, const juce::String& name) {
            for (const auto scale : { 1.0f, 1.5f })
                savePng (c.createComponentSnapshot (area, true, scale), folder.getChildFile (name + (scale > 1.0f ? "@150.png" : ".png")).getFullPathName());
        };

        AudioEngine offline (false);
        MainComponent ui (offline, false);
        ui.setSize (1600, 900);
        ui.selectTabForSnapshot (2);
        ui.setLayoutStyle (layoutstyle::Style::simple);
        offline.dispatchPendingMessages();
        auto* simple = ui.getSimpleWorkflowForSnapshot();
        if (simple == nullptr)
        {
            folder.getChildFile ("checks.txt").replaceWithText ("FAIL no Simple workflow\n");
            return false;
        }
        simple->hideChooser();
        // three tracks: a drum kit (four on the floor), a bass, a pad holding chords
        offline.editSong ([] (Song& s) {
            s.bpm = 124.0;
            st::Request q;
            q.kind = st::Kind::drums;
            q.name = "Drums";
            for (auto kind : { InstrumentKind::kick, InstrumentKind::snare, InstrumentKind::hat })
            {
                ChannelDef c;
                c.instrument = kind;
                c.name = kind == InstrumentKind::kick ? "Kick" : kind == InstrumentKind::snare ? "Snare" : "Hat";
                q.kit.push_back (c);
            }
            Pattern beat;
            for (int step = 0; step < 16; step += 4)
                beat.notes.push_back (Note { (std::int64_t) step * ticksPerStep, ticksPerStep, 0, 60 });
            for (int step = 2; step < 16; step += 4)
                beat.notes.push_back (Note { (std::int64_t) step * ticksPerStep, ticksPerStep, 2, 60 });
            q.pattern = beat;
            q.fillBars = 4;
            st::addTrack (s, q);
            for (const auto* name : { "Bass", "Pad" })
            {
                q = {};
                q.kind = st::Kind::instrument;
                q.name = name;
                ChannelDef synth;
                synth.instrument = InstrumentKind::synth;
                q.channel = synth;
                Pattern notes;
                for (int i = 0; i < 4; ++i)
                    if (std::string (name) == "Pad")
                        for (int k : { 60, 64, 67 })
                            notes.notes.push_back (Note { (std::int64_t) i * 4 * ppq, 4 * ppq - ppq / 4, 0, k });
                    else
                        notes.notes.push_back (Note { (std::int64_t) i * 4 * ppq, 2 * ppq, 0, 36 });
                q.pattern = notes;
                st::addTrack (s, q);
            }
        });
        offline.dispatchPendingMessages();
        simple->hideChooser();
        int drumsTrack = -1, bassTrack = -1, padTrack = -1;
        {
            const auto& song = offline.getSong();
            for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
                if (const auto kind = st::kindOf (song, t))
                {
                    if (*kind == st::Kind::drums)
                        drumsTrack = t;
                    else if (*kind == st::Kind::instrument)
                        (bassTrack < 0 ? bassTrack : padTrack) = t;
                }
        }
        check ("three Simple tracks (drums, bass, pad)", drumsTrack >= 0 && bassTrack >= 0 && padTrack >= 0);
        const auto drums = st::insertOf (offline.getSong(), drumsTrack), pad = st::insertOf (offline.getSong(), padTrack);
        const auto mixerBefore = offline.getSong().mixer;
        ui.getPlaylistForSnapshot().selectTrack (padTrack);
        offline.dispatchPendingMessages();
        auto* dock = simple->dockForSnapshot();
        if (dock == nullptr)
        {
            folder.getChildFile ("checks.txt").replaceWithText (checks + "FAIL no dock\n");
            return false;
        }
        dock->setTab (SimpleDock::Tab::effects);
        offline.dispatchPendingMessages();
        check ("the dock shows the pad's effects", dock->insertForCheck() == pad && dock->getTab() == SimpleDock::Tab::effects);
        check ("Duck under: None at first", dock->duckButtonTextForCheck().startsWith (tr ("None")));
        save (*dock, dock->getLocalBounds(), "dock-before");
        const auto dockPaint = [&dock] {
            juce::Image image (juce::Image::ARGB, dock->getWidth(), dock->getHeight(), true);
            for (int i = 0; i < 20; ++i) // (warm-up)
            {
                juce::Graphics g (image);
                dock->paintEntireComponent (g, true);
            }
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 300; ++i)
            {
                juce::Graphics g (image);
                dock->paintEntireComponent (g, true);
            }
            return (juce::Time::getMillisecondCounterHiRes() - t0) / 300.0;
        };
        note ("dock paint, no duck / no sends: " + juce::String (dockPaint(), 3) + " ms a frame (" + juce::String (dock->getWidth()) + " x " + juce::String (dock->getHeight()) + ")");
        const auto texts = [] (const juce::PopupMenu& menu) {
            juce::StringArray out;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                out.add (it.getItem().text + (it.getItem().isTicked ? " [x]" : "") + (it.getItem().isEnabled ? "" : " (off)"));
            return out;
        };
        {
            const auto items = texts (dock->duckMenuForCheck());
            note ("picker: " + items.joinIntoString (" | "));
            check ("the picker lists None and the two other tracks, styles off without a duck",
                   (items.contains (tr ("None") + " [x]") || items.contains (tr ("None") + " [x] (off)")) && items.contains ("Drums") && items.contains ("Bass") && ! items.contains ("Pad")
                       && items.contains (trc ("@duck style: Pumping") + " (off)"));
            saveMenus (juce::StringArray { tr ("Duck under") }, std::vector<juce::PopupMenu> { dock->duckMenuForCheck() },
                       folder.getChildFile ("menu-duck-before.png").getFullPathName());
        }
        // Echo at 0: no insert
        dock->setEchoForCheck (0.0f);
        offline.dispatchPendingMessages();
        check ("an Echo send at 0 makes no insert", st::sharedEcho (offline.getSong()) == -1 && offline.getSong().mixer == mixerBefore);

        // Duck under the drums, Echo and Reverb sends
        dock->duckUnderForCheck (drums);
        offline.dispatchPendingMessages();
        dock->setEchoForCheck (0.35f);
        offline.dispatchPendingMessages();
        dock->setReverbForCheck (0.25f);
        offline.dispatchPendingMessages();
        const auto song = offline.getSong(); // (a copy: undo below swaps the engine's song)
        const auto slot = st::duckSlot (song, pad);
        check ("Duck under Drums: a Fruity Limiter keyed by the drums' insert, at Pumping",
               slot >= 0 && song.mixer[(std::size_t) pad].effects[(std::size_t) slot].sidechain == drums
                   && std::abs (song.mixer[(std::size_t) pad].effects[(std::size_t) slot].goalAmount - st::duckAmount (st::DuckStyle::pumping)) < 1.0e-6f);
        check ("FL's sidechain route: drums -> pad at level 0, the drums still on the Master",
               song.routes (drums, pad) && song.routeLevel (drums, pad) == 0.0f && song.mixer[(std::size_t) drums].toMaster && mixerops::soleSidechainSender (song, pad) == drums);
        const auto echo = st::sharedEcho (song);
        check ("the Echo send made the shared Echo insert", echo > 0 && std::abs (st::echoSend (song, pad) - 0.35f) < 1.0e-6f && std::abs (st::reverbSend (song, pad) - 0.25f) < 1.0e-6f);
        check ("the button names the trigger", dock->duckButtonTextForCheck().startsWith ("Drums"));
        check ("one card: Duck (a one-knob card)", dock->cardCountForCheck() == 1 && dock->oneKnobCardsForCheck() == 1);
        save (*dock, dock->getLocalBounds(), "dock-duck");
        save (ui, ui.getLocalBounds(), "window-duck");
        {
            const auto items = texts (dock->duckMenuForCheck());
            note ("picker: " + items.joinIntoString (" | "));
            check ("the picker ticks Drums and Pumping", items.contains ("Drums [x]") && items.contains (trc ("@duck style: Pumping") + " [x]"));
            saveMenus (juce::StringArray { tr ("Duck under") }, std::vector<juce::PopupMenu> { dock->duckMenuForCheck() },
                       folder.getChildFile ("menu-duck-after.png").getFullPathName());
        }
        // undo takes the duck back in one step
        {
            const auto withDuck = song.mixer;
            offline.undo();
            offline.dispatchPendingMessages();
            const auto gone = st::duckSlot (offline.getSong(), pad) < 0 || offline.getSong().mixer != withDuck;
            offline.redo();
            offline.dispatchPendingMessages();
            check ("undo / redo of the sends and the duck", gone && offline.getSong().mixer == withDuck);
        }

        // the dock's paint cost with the duck card and the three knobs (the whole dock, 200 frames at 100 %)
        note ("dock paint, the Duck card and the sends: " + juce::String (dockPaint(), 3) + " ms a frame");

        // the Pattern layout: the Mixer shows an ordinary sidechain route and slot
        ui.setLayoutStyle (layoutstyle::Style::pattern);
        offline.dispatchPendingMessages();
        ui.selectTabForSnapshot (3);
        auto& mixer = ui.getMixerForSnapshot();
        mixer.selectTrack (pad);
        mixer.resized();
        offline.dispatchPendingMessages();
        check ("Pattern layout: the same slot and route (nothing Simple-only)", st::duckSlot (offline.getSong(), pad) == slot && offline.getSong().routes (drums, pad));
        save (mixer, mixer.getLocalBounds(), "pattern-mixer");
        ui.setLayoutStyle (layoutstyle::Style::simple);
        offline.dispatchPendingMessages();

        // the mixer as it plays: names, routes and sends, every slot's kind, side input, goal and parameters (the file keeps
        // 4 decimals)
        // (`effectsOf`: the inserts whose effects are compared, empty = every insert; a copy for 0.1.28 also moves the
        // Reverb's Atrium to its 0.1.27 law, which is not this round's)
        const auto samePlay = [] (const std::vector<MixerTrack>& x, const std::vector<MixerTrack>& y, std::vector<int> effectsOf = {}) {
            const auto near = [] (float a, float b) { return std::abs (a - b) <= 1.0e-4f; };
            if (x.size() != y.size())
                return false;
            for (std::size_t t = 0; t < x.size(); ++t)
            {
                const auto& a = x[t];
                const auto& b = y[t];
                if (a.name != b.name || a.toMaster != b.toMaster || a.sends.size() != b.sends.size() || a.effects.size() != b.effects.size())
                    return false;
                for (std::size_t i = 0; i < a.sends.size(); ++i)
                    if (a.sends[i].target != b.sends[i].target || ! near (a.sends[i].level, b.sends[i].level) || a.sends[i].pre != b.sends[i].pre)
                        return false;
                if (! effectsOf.empty() && std::find (effectsOf.begin(), effectsOf.end(), (int) t) == effectsOf.end())
                    continue;
                for (std::size_t i = 0; i < a.effects.size(); ++i)
                {
                    const auto& e = a.effects[i];
                    const auto& f = b.effects[i];
                    if (e.kind != f.kind || e.sidechain != f.sidechain || e.enabled != f.enabled || ! near (e.mix, f.mix) || e.goal != f.goal)
                        return false;
                    for (std::size_t p = 0; p < e.params.size(); ++p)
                        if (! near (e.params[p], f.params[p]))
                            return false;
                }
            }
            return true;
        };
        // .lynx round trip
        const auto json = ProjectFile::toJson (offline.getSong());
        {
            juce::String why;
            const auto back = ProjectFile::fromJson (json, why);
            check (".lynx keeps the duck (its goal and Amount), its route and the sends",
                   back && samePlay (back->mixer, offline.getSong().mixer) && st::duckTrigger (*back, pad) == drums
                       && std::abs (back->mixer[(std::size_t) pad].effects[(std::size_t) slot].goalAmount - st::duckAmount (st::DuckStyle::pumping)) < 1.0e-4f);
        }
        // copies for older releases: an ordinary Fruity Limiter with its side input, the same route and sends
        for (const auto* release : { "0.1.30", "0.1.28" })
        {
            const auto copy = olderlynx::forRelease (offline.getSong(), release);
            juce::String why;
            const auto back = copy.ok ? ProjectFile::fromJson (copy.json, why) : std::optional<Song> {};
            // (the Echo's Echoform: its kind and the parameters the bus set; a copy for 0.1.28 cuts Echoform's hidden
            // revision parameters, heard only with an echo voicing, round 190is's own step)
            const auto echoKept = [&] {
                if (! back || (int) back->mixer.size() <= echo || back->mixer[(std::size_t) echo].effects.size() != 1)
                    return false;
                const auto& a = back->mixer[(std::size_t) echo].effects.front();
                const auto& b = offline.getSong().mixer[(std::size_t) echo].effects.front();
                namespace ef = echosuite::echoform;
                for (const int param : { ef::mix, ef::syncL, ef::noteL, ef::syncR, ef::noteR, ef::feedback, ef::lowCut, ef::highCut })
                    if (std::abs (a.params[(std::size_t) param] - b.params[(std::size_t) param]) > 1.0e-4f)
                        return false;
                return a.kind == b.kind;
            };
            const auto same = back && samePlay (back->mixer, offline.getSong().mixer, { drums, pad }) && echoKept();
            juce::StringArray lines;
            for (const auto& l : copy.lost)
                lines.add (tr (l.text, { l.detail }) + (l.sound ? " (sound)" : ""));
            note (juce::String ("copy for ") + release + ": " + lines.joinIntoString (" / "));
            const auto duckLines = std::count_if (copy.lost.begin(), copy.lost.end(), [] (const olderlynx::Loss& l) { return juce::String (l.text).contains ("Duck under"); });
            check (juce::String ("Save a copy for ") + release + ": the slot, its side input, the route and the sends unchanged; one view line for the duck",
                   copy.ok && same && duckLines == 1);
        }
        // .flp export (FL's own Fruity Limiter, side input on the drums' place, level-0 route), written for the FL check
        {
            flpexport::Options options;
            flpexport::Report report;
            const auto bytes = flpexport::exportFlp (offline.getSong(), options, report);
            const auto file = folder.getChildFile ("duck-export.flp");
            file.replaceWithData (bytes.data(), bytes.size());
            const auto project = flp::parseProject (bytes);
            int sideIndex = -1;
            bool level0 = false, echoRoute = false;
            for (const auto& m : project.mixer)
            {
                if (m.index == pad)
                    for (const auto& s : m.slots)
                        if (s.plugin == "Fruity Limiter")
                            if (const auto state = fldyn::parseLimiter (s.state))
                                sideIndex = state->sidechain;
                if (m.index == drums)
                    level0 = std::find (m.routes.begin(), m.routes.end(), pad) != m.routes.end();
                if (m.index == pad)
                    echoRoute = std::find (m.routes.begin(), m.routes.end(), echo) != m.routes.end();
            }
            for (const auto& e : report.exact)
                if (juce::String (e).contains ("Duck"))
                    note ("flp: " + juce::String (e));
            for (const auto& e : report.dropped)
                note ("flp dropped: " + juce::String (e));
            for (const auto& e : report.substituted)
                note ("flp substituted: " + juce::String (e));
            check (".flp: Fruity Limiter keyed by input " + juce::String (sideIndex) + " (the drums), drums -> pad route, pad -> Echo route",
                   sideIndex == mixerops::sidechainIndexOf (offline.getSong(), pad, drums) && sideIndex > 0 && level0 && echoRoute && file.getSize() > 0);
        }
        // the FL check's projects (rendered by FL itself, FL64 /R /Ewav): two Simple audio tracks playing real files (a 60 Hz
        // kick burst at -6 dBFS on every beat, a 330 Hz pad at -12 dBFS; tests/SimpleDuck190otTests.inc's signals), the kick
        // only keying the pad (not on the Master): duck-off.flp and duck-on.flp (Duck under the kick at 50 %). The files are
        // named by LYNX_190OT_WINDIR (FL's own path to <folder>/fl), else by their path here
        {
            const auto dir = folder.getChildFile ("fl");
            dir.createDirectory();
            const auto winDir = juce::SystemStats::getEnvironmentVariable ("LYNX_190OT_WINDIR", dir.getFullPathName());
            const auto sep = winDir.containsChar ('\\') ? juce::String ("\\") : juce::String ("/");
            for (const auto* name : { "kick", "pad" })
            {
                const double rate = 48000.0;
                juce::AudioBuffer<float> b (1, (int) (rate * 4.5));
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto t = i / rate;
                    const auto inBeat = std::fmod (t, 0.5);
                    b.setSample (0, i, juce::String (name) == "kick"
                                           ? (inBeat < 0.08 ? (float) (0.5 * std::exp (-inBeat * 20.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * inBeat)) : 0.0f)
                                           : (float) (0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t)));
                }
                const auto file = dir.getChildFile (juce::String (name) + ".wav");
                file.deleteFile();
                if (auto stream = std::make_unique<juce::FileOutputStream> (file); stream->openedOk())
                    if (std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (stream.get(), rate, 1, 24, {}, 0)); w != nullptr)
                    {
                        stream.release();
                        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
                    }
            }
            for (const auto duck : { false, true })
            {
                Song probe;
                probe.bpm = 120.0;
                probe.mode = PlayMode::song;
                probe.mixer.resize (6);
                probe.mixer[0].name = "Master";
                for (int i = 1; i < 6; ++i)
                    probe.mixer[(std::size_t) i].name = "Insert " + std::to_string (i);
                int kickInsert = -1, padInsert = -1;
                for (const auto* name : { "Kick", "Pad" })
                {
                    st::Request q;
                    q.kind = st::Kind::audio;
                    q.name = name;
                    q.audioFile = (winDir + sep + juce::String (name).toLowerCase() + ".wav").toStdString();
                    q.audioLength = 8 * ppq;
                    (kickInsert < 0 ? kickInsert : padInsert) = st::addTrack (probe, q).insert;
                }
                probe.mixer[(std::size_t) kickInsert].toMaster = false;
                if (duck)
                    st::setDuck (probe, padInsert, kickInsert, 0.5f);
                flpexport::Options options;
                flpexport::Report report;
                const auto bytes = flpexport::exportFlp (probe, options, report);
                dir.getChildFile (duck ? "duck-on.flp" : "duck-off.flp").replaceWithData (bytes.data(), bytes.size());
            }
            note ("FL check projects: " + dir.getFullPathName() + " (duck-off.flp, duck-on.flp; files named " + winDir + ")");
        }
        checks << (allOk ? "result=OK\n" : "result=FAIL\n");
        folder.getChildFile ("checks.txt").replaceWithText (checks);
        return allOk;
    }

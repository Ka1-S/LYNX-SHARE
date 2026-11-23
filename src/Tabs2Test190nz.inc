    // --tabs2test190nz <a|b> <folder> (round 190nz, issue #232 stage 2): two LYNX processes on one display, the way two
    // windows opened with File > Open in new window run. A copies, B pastes, each kind in turn, through the real
    // components and the system clipboard (X11 on Linux: A serves its selection while it waits): notes (and a copy big
    // enough for the side file), Playlist clips with what they play, Channel Rack steps, channels, a mixer track state,
    // automation points. Then the windows' registry: B lists A, brings it to the front, sees that A plays through the
    // device B wants (a stand-in: no audio device is opened by either process) and asks for it; A lets it go. The two
    // processes meet through marker files in <folder>; every wait is bounded. B writes pictures (its Window menu, the
    // audio device bar, the menus with Copy / Paste) at the shot scale (LYNX_SHOT_SCALE=1.5 LYNX_SHOT_SUFFIX=@150 for
    // @150). Needs LYNX_TEST_MODE=1 LYNX_SYSTEM_CLIPBOARD=1 LYNX_WINDOWS=1 and the same XDG_CONFIG_HOME for both.
    // Round 190pb (flaky under load in rollup 0.1.31 step 5): no step rests on time any more. The markers carry the
    // run's key (A's and B's process ids), so a marker left in <folder> by an earlier run never lets a wait through (the
    // step-5 runner reused its folder: B pasted before A had copied, A copied the next kind before B had pasted); the
    // clipboard is read until A's text arrives (an X11 selection transfer of a loaded process can miss JUCE's ~100 ms
    // read); the bar, the hand-over and the device are waited for as conditions; the setup both windows want is written
    // to the settings folder the processes really use (LYNX_PROFILE_DIR moves it away from XDG_CONFIG_HOME, so the
    // runner's audio-device.xml was never read and the device bar never came). Every wait stays bounded.
    bool runTabs2Test190nz (const juce::String& role, const juce::File& dir)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (lynx::ui::showingForShots(), true);
        using namespace lynx;
        const auto isA = role == "a";
        juce::String checks;
        bool allOk = true;
        const auto log = shot::file (dir.getChildFile (role + "-checks.txt"));
        const auto check = [&] (const juce::String& what, bool ok) {
            checks << (ok ? "ok   " : "FAIL ") << what << "\n";
            allOk = allOk && ok;
            log.replaceWithText (checks);
        };
        const auto line = [&] (const juce::String& what) {
            checks << "     " << what << "\n";
            log.replaceWithText (checks);
        };
        const auto pump = [] (int ms) {
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
            while (juce::Time::getMillisecondCounter() < end)
#if JUCE_LINUX || JUCE_WINDOWS // (rollup 0.1.31 step 4: macOS has no such JUCE function; the route runs on Linux / Windows)
                if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
#endif
                    juce::Thread::sleep (5);
        };
        // (round 190pb) the run's key, "<A's pid> <B's pid>", once the two have met: every marker after that holds it
        juce::String runKey;
        const auto mark = [&dir, &runKey] (const juce::String& name) { dir.getChildFile (name).replaceWithText (runKey); };
        const auto until = [&pump] (const std::function<bool()>& condition, int seconds) {
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) seconds * 1000u;
            while (! condition())
            {
                if (juce::Time::getMillisecondCounter() > end)
                    return condition();
                pump (20);
            }
            return true;
        };
        const auto waitFor = [&] (const juce::String& name, int seconds = 120) {
            return until ([&] { const auto f = dir.getChildFile (name); return f.existsAsFile() && f.loadFileAsString() == runKey; }, seconds);
        };
        {
            // this role's markers of an earlier run go (the other process clears its own; the key keeps them apart anyway)
            juce::StringArray own { "ready", "activated", "audio-released", "finished" };
            for (const auto* step : { "notes", "big", "clips", "steps", "channels", "mixer", "points" })
                own.add ((isA ? "copied-" : "done-") + juce::String (step));
            for (const auto& name : own)
                dir.getChildFile (role + "-" + name).deleteFile();
        }
        {
            // (round 190pb) the stand-in setup both windows want, in the settings folder this process reads (a test profile's
            // when LYNX_PROFILE_DIR is set): written when it is missing, never over another device
            const auto setup = platform::settingsFile ("audio-device.xml");
            const juce::String wantedSetup ("<?xml version=\"1.0\" encoding=\"UTF-8\"?><DEVICESETUP deviceType=\"ASIO\" audioOutputDeviceName=\"Test interface\" "
                                            "audioInputDeviceName=\"\" audioDeviceRate=\"48000.0\" audioDeviceBufferSize=\"256\"/>");
            if (! setup.existsAsFile())
            {
                setup.getParentDirectory().createDirectory();
                setup.replaceWithText (wantedSetup);
            }
            const auto device = windowaudio::deviceOf (juce::XmlDocument::parse (setup).get());
            check (role + ": the setup both windows want is ASIO / Test interface (" + setup.getFullPathName() + ")", device.type == "ASIO" && device.name == "Test interface");
        }
        const auto write = [&dir] (juce::Component& c, juce::Rectangle<int> area, const juce::String& name) {
            area = area.getIntersection (c.getLocalBounds());
            if (! area.isEmpty())
                writePng (shot::snapshot (c, area), dir.getChildFile ("232-" + name + ".png").getFullPathName());
        };
        const auto findItem = [] (const juce::PopupMenu& menu, const juce::String& text) -> const juce::PopupMenu::Item* {
            for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
                if (it.getItem().text.upToFirstOccurrenceOf ("\t", false, false) == text)
                    return &it.getItem();
            return nullptr;
        };
        const auto run = [&findItem] (const juce::PopupMenu& menu, const juce::String& text) {
            const auto* item = findItem (menu, text);
            if (item == nullptr || ! item->isEnabled || item->action == nullptr)
                return false;
            item->action();
            return true;
        };
        const auto ctrl = [] (char letter) { return juce::KeyPress (letter, juce::ModifierKeys::commandModifier, 0); };

        // A's project ("Album - track 1"): Kick, Clap, a synth "Lead", an audio clip channel "Vox" (a WAV in the folder),
        // a pattern "Beat" (Kick and Lead), an automation clip on Lead's volume, mixer insert 3 "Vocal bus"
        const auto wav = dir.getChildFile ("vox take.wav");
        Song song;
        if (isA)
        {
            {
                wav.deleteFile();
                juce::WavAudioFormat format;
                std::unique_ptr<juce::OutputStream> out (wav.createOutputStream().release());
                if (std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (out.get(), 44100.0, 1, 16, {}, 0)); writer != nullptr)
                {
                    out.release();
                    juce::AudioBuffer<float> b (1, 22050);
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        b.setSample (0, i, 0.5f * std::sin ((float) i * 0.05f));
                    writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
                }
            }
            song = Song::makeDefault();
            song.channels.resize (2); // Kick, Clap
            ChannelDef lead;
            lead.name = "Lead";
            lead.instrument = InstrumentKind::synth;
            lead.synth.cutoff = 1234.5f;
            lead.mixerTrack = 3;
            song.channels.push_back (lead);
            ChannelDef vox;
            vox.name = "Vox";
            vox.instrument = InstrumentKind::audioClip;
            vox.samplePath = wav.getFullPathName().toStdString();
            song.channels.push_back (vox);
            song.patterns.clear();
            Pattern beat;
            beat.name = "Beat";
            for (int i = 0; i < 4; ++i)
                beat.notes.push_back ({ i * 96, 24, 0, 60, 0.8f });
            beat.notes.push_back ({ 0, 192, 2, 64, 0.61f });
            beat.notes.push_back ({ 192, 192, 2, 67, 0.72f });
            beat.sortNotes();
            song.patterns.push_back (beat);
            Pattern big; // (a copy larger than the clipboard text keeps inline: it goes through the side file)
            big.name = "Many notes";
            for (int i = 0; i < 6000; ++i)
                big.notes.push_back ({ (std::int64_t) i * 12, 12, 1, 40 + i % 19, 0.5f });
            song.patterns.push_back (big);
            Automation volume;
            volume.name = "Lead volume";
            volume.target = { AutomationTargetKind::channelVolume, 2, 0, 0 };
            volume.points = { { 0, 0.2f, 0.0f, 0 }, { 384, 0.9f, 0.3f, 0 } };
            song.automations.push_back (volume);
            song.playlist.clear();
            Clip p;
            p.pattern = 0;
            p.track = 0;
            p.start = 0;
            p.length = 768;
            song.playlist.push_back (p);
            Clip a;
            a.kind = ClipKind::audio;
            a.channel = 3;
            a.track = 1;
            a.start = 384;
            a.length = 384;
            a.gain = 0.5f;
            song.playlist.push_back (a);
            Clip au;
            au.kind = ClipKind::automation;
            au.automation = 0;
            au.track = 2;
            au.length = 384;
            song.playlist.push_back (au);
            song.mixer[3].name = "Vocal bus";
            song.mixer[3].colour = 0xff3080c0;
            song.mixer[3].volume = 0.6f;
            song.mixer[3].pan = -0.25f;
            EffectSlot limiter;
            limiter.kind = EffectKind::limiter;
            limiter.params = { 0.9f, 0.1f, 0.4f };
            song.mixer[3].effects = { limiter, limiter };
        }
        else
            song = Song::makeDefault(); // B's project ("Album - track 2"): Kick, Clap, Hat, Snare, Synth

        AudioEngine engine (false);
        engine.setSong (song);
        engine.setSelectedChannel (isA ? 2 : 4);
        engine.dispatchPendingMessages();
        MainComponent ui (engine, false);
        ui.setSize (1600, 900);
        ui.setProjectNameForSnapshot (isA ? "Album - track 1" : "Album - track 2");
        PianoRollComponent roll (engine);
        roll.setBounds (0, 0, 1100, 520);
        roll.setVisible (true);
        PlaylistComponent playlist (engine);
        playlist.setBounds (0, 0, 1400, 600);
        ChannelRackComponent rack (engine);
        rack.setBounds (0, 0, 700, 500);
        MixerComponent mixer (engine);
        mixer.setBounds (0, 0, 1400, 600);
        engine.editSong ([] (Song&) {});
        engine.dispatchPendingMessages();

        // the windows' registry, as a real window starts it (MainComponent does when it is the program's window)
        windows::Entry me;
        me.project = isA ? "Album - track 1" : "Album - track 2";
        const auto started = windows::start (me, [&ui, &mark, isA] (const juce::String& verb, int from, const juce::String&) {
            if (verb == "activate" && isA)
                mark ("a-activated");
            if (verb == "audio-released" && ! isA)
                mark ("b-audio-released");
            ui.windowMessage (verb, from);
        });
        check (role + ": the window's entry and inbox (" + windows::mine().inbox + ")", started);
        if (isA)
            windows::update ([] (windows::Entry& e) { // (stands for a device A plays through: none is opened in a test)
                e.deviceType = "ASIO";
                e.deviceName = "Test interface";
            });

        const auto steps = juce::StringArray { "notes", "big", "clips", "steps", "channels", "mixer", "points" };
        // (round 190pb) the handshake: A's "a-ready" names A; B answers with "b-ready" naming both, the run's key. A
        // marker of a process that is gone (an earlier run) is not a live window of the registry: never taken.
        const auto myPid = juce::String (windows::mine().pid);
        const auto liveWindow = [] (int pid) {
            const auto o = windows::others();
            return pid > 0 && std::any_of (o.begin(), o.end(), [pid] (const windows::Entry& e) { return e.pid == pid; });
        };
        if (isA)
        {
            runKey = myPid;
            mark ("a-ready");
            check ("A: B started", until ([&] {
                       const auto f = dir.getChildFile ("b-ready");
                       const auto parts = juce::StringArray::fromTokens (f.existsAsFile() ? f.loadFileAsString() : juce::String(), " ", "");
                       if (parts.size() != 2 || parts[0] != myPid || ! liveWindow (parts[1].getIntValue()))
                           return false;
                       runKey = parts.joinIntoString (" ");
                       return true;
                   }, 120));
            line ("run " + runKey);
            for (const auto& step : steps)
            {
                if (step == "notes")
                {
                    engine.setSelectedChannel (2);
                    engine.editSong ([] (Song& s) { s.currentPattern = 0; });
                    engine.dispatchPendingMessages();
                    roll.selectAll();
                    roll.handleKey (ctrl ('c'));
                }
                else if (step == "big")
                {
                    engine.setSelectedChannel (1);
                    engine.editSong ([] (Song& s) { s.currentPattern = 1; });
                    engine.dispatchPendingMessages();
                    roll.selectAll();
                    roll.handleKey (ctrl ('c'));
                    const auto text = juce::SystemClipboard::getTextFromClipboard();
                    check ("A: 6,000 notes go through the side file (the clipboard holds its path, " + juce::String (text.length()) + " characters)",
                           text.contains ("\nfile ") && text.length() < 1000);
                }
                else if (step == "clips")
                {
                    playlist.selectForSnapshot ({ 0, 1, 2 });
                    playlist.handleKey (ctrl ('c'));
                }
                else if (step == "steps")
                {
                    engine.editSong ([] (Song& s) { s.currentPattern = 0; });
                    engine.setChannelSelection ({ 0, 1 }, 0);
                    engine.dispatchPendingMessages();
                    rack.copySelected();
                }
                else if (step == "channels")
                {
                    engine.setChannelSelection ({ 2, 3 }, 2);
                    check ("A: channel menu > Copy 2 channels", run (rack.getChannelMenuForSnapshot (2), tr ("Copy {0} channels", { "2" })));
                }
                else if (step == "mixer")
                {
                    const auto menu = mixer.getStripsForSnapshot().buildMenu (3);
                    check ("A: mixer track menu > File > Copy mixer track state", run (menu, tr ("Copy mixer track state")));
                }
                else if (step == "points")
                {
                    crossclipboard::Payload copy; // (the Playlist's point selection needs the mouse on a curve: the payload as it sends it)
                    copy.kind = crossclipboard::Kind::points;
                    copy.points.points = { { 0, 0.25f, 0.0f, 0 }, { 96, 0.75f, -0.5f, 2 } };
                    copy.points.span = 96;
                    windowclip::publish (copy);
                }
                pump (50);
                mark ("a-copied-" + step);
                check ("A: B pasted " + step, waitFor ("b-done-" + step));
            }
            check ("A: B asked this window to come to the front (Window menu)", waitFor ("a-activated"));
            // B asks for the audio device: this window lets it go (no device is open here; the entry says none now)
            check ("A: B is done", waitFor ("b-finished", 180));
            check ("A: let the audio device go (B asked; the entry names no device now)", until ([&ui] {
                       return ui.windowAudioForSnapshot() != nullptr && ui.windowAudioForSnapshot()->gaveAway() && windows::mine().deviceName.isEmpty();
                   }, 30));
        }
        else
        {
            check ("B: A started", until ([&] {
                       const auto f = dir.getChildFile ("a-ready");
                       const auto pid = f.existsAsFile() ? f.loadFileAsString().trim() : juce::String();
                       if (! liveWindow (pid.getIntValue()))
                           return false;
                       runKey = pid + " " + myPid;
                       return true;
                   }, 120));
            line ("run " + runKey);
            mark ("b-ready");
            const auto pasteChecks = [&] (const juce::String& step) {
                const auto& s = engine.getSong();
                if (step == "notes")
                {
                    engine.setSelectedChannel (4);
                    engine.dispatchPendingMessages();
                    juce::String text; // (round 190pb: read until A's selection arrives; one read of a loaded process can miss it)
                    const auto arrived = until ([&text] {
                        text = juce::SystemClipboard::getTextFromClipboard();
                        return text.startsWith (crossclipboard::magic) && text.contains ("\nnotes ");
                    }, 60);
                    check ("B: the system clipboard (the other process's X11 selection) holds A's notes", arrived);
                    const auto before = (int) s.patterns[0].notes.size();
                    roll.handleKey (ctrl ('v'));
                    engine.dispatchPendingMessages();
                    std::vector<int> keys;
                    for (const auto& n : engine.getSong().patterns[0].notes)
                        if (n.channel == 4)
                            keys.push_back (n.key);
                    std::sort (keys.begin(), keys.end());
                    check ("B: Ctrl+V in the Piano roll pastes A's two Lead notes on Synth (keys 64 and 67, velocities kept)",
                           (int) engine.getSong().patterns[0].notes.size() == before + 2 && keys == std::vector<int> { 64, 67 });
                    bool velocity = false;
                    for (const auto& n : engine.getSong().patterns[0].notes)
                        velocity = velocity || (n.channel == 4 && n.key == 64 && std::abs (n.velocity - 0.61f) < 0.001f);
                    check ("B: ... with their velocity", velocity);
                }
                else if (step == "big")
                {
                    engine.setSelectedChannel (2);
                    engine.dispatchPendingMessages();
                    const auto before = (int) s.patterns[0].notes.size();
                    roll.handleKey (ctrl ('v'));
                    engine.dispatchPendingMessages();
                    line ("pasted " + juce::String ((int) engine.getSong().patterns[0].notes.size() - before) + " notes");
                    check ("B: 6,000 notes pasted from the side file", (int) engine.getSong().patterns[0].notes.size() == before + 6000);
                }
                else if (step == "clips")
                {
                    const auto channels = s.channels.size(), clips = s.playlist.size(), patterns = s.patterns.size();
                    playlist.handleKey (ctrl ('v'));
                    engine.dispatchPendingMessages();
                    const auto& after = engine.getSong();
                    check ("B: Ctrl+V in the Playlist pastes the three clips", after.playlist.size() == clips + 3);
                    check ("B: Kick is this project's own Kick; Lead and Vox come in as new channels",
                           after.channels.size() == channels + 2 && after.channels[channels].name == "Lead" && after.channels[channels + 1].name == "Vox");
                    if (after.channels.size() == channels + 2)
                    {
                        const auto lead = (int) channels, vox = (int) channels + 1;
                        check ("B: Lead keeps its settings (filter cutoff) and its insert number", std::abs (after.channels[(std::size_t) lead].synth.cutoff - 1234.5f) < 0.01f
                                                                                                      && after.channels[(std::size_t) lead].mixerTrack == 3);
                        check ("B: Vox plays A's sample file (its path)", juce::String (after.channels[(std::size_t) vox].samplePath) == wav.getFullPathName());
                        check ("B: the pattern Beat came along, its notes on Kick (0) and Lead", after.patterns.size() == patterns + 1 && after.patterns.back().name == "Beat"
                                                                                                    && std::count_if (after.patterns.back().notes.begin(), after.patterns.back().notes.end(), [] (const Note& n) { return n.channel == 0; }) == 4
                                                                                                    && std::count_if (after.patterns.back().notes.begin(), after.patterns.back().notes.end(), [lead] (const Note& n) { return n.channel == lead; }) == 2);
                        // (the pasted clips are sorted into the Playlist: found by kind among the new ones)
                        const Clip* autoClip = nullptr;
                        const Clip* audioClip = nullptr;
                        for (std::size_t i = 0; i < after.playlist.size(); ++i)
                            if (after.playlist[i].kind == ClipKind::automation)
                                autoClip = &after.playlist[i];
                            else if (after.playlist[i].kind == ClipKind::audio && after.playlist[i].channel == vox)
                                audioClip = &after.playlist[i];
                        check ("B: the automation clip drives the pasted Lead's volume", autoClip != nullptr
                                                                                           && after.automations[(std::size_t) autoClip->automation].target.index == lead
                                                                                           && after.automations[(std::size_t) autoClip->automation].points.size() == 2);
                        check ("B: the audio clip keeps its gain", audioClip != nullptr && std::abs (audioClip->gain - 0.5f) < 0.001f);
                    }
                    // a second paste: nothing new but the clips
                    const auto channels2 = engine.getSong().channels.size(), patterns2 = engine.getSong().patterns.size();
                    playlist.handleKey (ctrl ('v'));
                    engine.dispatchPendingMessages();
                    check ("B: pasted again, the same channels and pattern are used", engine.getSong().channels.size() == channels2
                                                                                        && engine.getSong().patterns.size() == patterns2 && engine.getSong().playlist.size() == clips + 6);
                    engine.undo();
                    engine.dispatchPendingMessages();
                    check ("B: Undo takes the second paste back in one step", engine.getSong().playlist.size() == clips + 3);
                }
                else if (step == "steps")
                {
                    engine.editSong ([] (Song& x) { x.currentPattern = 0; });
                    engine.setChannelSelection ({ 2, 3 }, 2); // Hat, Snare get Kick's and Clap's steps
                    engine.dispatchPendingMessages();
                    rack.pasteSelected();
                    engine.dispatchPendingMessages();
                    const auto& p = engine.getSong().patterns[0];
                    std::vector<std::int64_t> hat;
                    bool snareEmpty = true;
                    for (const auto& n : p.notes)
                    {
                        if (n.channel == 2)
                            hat.push_back (n.start);
                        if (n.channel == 3)
                            snareEmpty = false;
                    }
                    std::sort (hat.begin(), hat.end());
                    // A's pattern Beat: Kick on 0, 96, 192, 288; Clap has no notes there (its slot clears Snare)
                    check ("B: Channel Rack Ctrl+V: Hat has A's Kick steps, Snare A's Clap steps (none)",
                           hat == std::vector<std::int64_t> { 0, 96, 192, 288 } && snareEmpty);
                }
                else if (step == "channels")
                {
                    engine.setChannelSelection ({ 0 }, 0);
                    engine.dispatchPendingMessages();
                    const auto count = s.channels.size();
                    const auto menu = rack.getChannelMenuForSnapshot (0);
                    writeMenu (menu, tr ("Channel"), dir.getChildFile ("232-channel-menu.png"));
                    check ("B: channel menu > Paste channels (enabled: A copied two)", run (menu, tr ("Paste channels")));
                    engine.dispatchPendingMessages();
                    const auto& after = engine.getSong();
                    check ("B: Lead and Vox pasted below Kick (rows 1 and 2), the rest moved down",
                           after.channels.size() == count + 2 && after.channels[1].name == "Lead" && after.channels[2].name == "Vox" && after.channels[3].name == "Clap");
                    check ("B: ... with their settings and sample", after.channels.size() > 2 && std::abs (after.channels[1].synth.cutoff - 1234.5f) < 0.01f
                                                                       && juce::String (after.channels[2].samplePath) == wav.getFullPathName());
                }
                else if (step == "mixer")
                {
                    const auto menu = mixer.getStripsForSnapshot().buildMenu (5);
                    if (const auto* file = findItem (menu, tr ("File")); file != nullptr && file->subMenu != nullptr)
                        writeMenu (*file->subMenu, tr ("File"), dir.getChildFile ("232-mixer-file-menu.png"));
                    check ("B: mixer track 5 menu > File > Paste mixer track state", run (menu, tr ("Paste mixer track state")));
                    engine.dispatchPendingMessages();
                    const auto& t = engine.getSong().mixer[5];
                    check ("B: insert 5 is A's Vocal bus: name, colour, level, pan, its two effects",
                           t.name == "Vocal bus" && t.colour == 0xff3080c0u && std::abs (t.volume - 0.6f) < 0.001f && std::abs (t.pan + 0.25f) < 0.001f
                               && t.effects.size() == 2u && t.effects[0].kind == EffectKind::limiter);
                }
                else if (step == "points")
                {
                    const auto in = windowclip::incoming (crossclipboard::Kind::points);
                    check ("B: A's automation points (ticks, values, tension, curve mode)", in && in->points.points.size() == 2u && in->points.span == 96
                                                                                               && in->points.points[1].mode == 2 && std::abs (in->points.points[1].tension + 0.5f) < 1.0e-4f);
                    check ("B: not a kind it is not asked for (a Piano roll paste ignores them)", ! windowclip::incoming (crossclipboard::Kind::notes).has_value());
                }
            };
            for (const auto& step : steps)
            {
                check ("B: A copied " + step, waitFor ("a-copied-" + step));
                pasteChecks (step);
                mark ("b-done-" + step);
            }

            // the Window menu: A listed, a click brings it to the front
            const auto others = windows::others();
            check ("B: the registry lists A (its project, the device it plays through)",
                   others.size() == 1u && others.front().project == "Album - track 1" && others.front().deviceName == "Test interface");
            ui.windowMessage ("changed", 0);
            check ("B: WINDOW in the menu bar, before HELP", ui.getMenuNamesForSnapshot().indexOf (tr ("Window")) == 7
                                                              && ui.getMenuNamesForSnapshot().indexOf (tr ("Help")) == 8);
            const auto menu = ui.windowMenuForSnapshot();
            writeMenu (menu, tr ("Window"), dir.getChildFile ("232-window-menu.png"));
            check ("B: Window menu > Album - track 1 brings A to the front",
                   run (menu, juce::String ("Album - track 1") + "   (" + tr ("plays through {0}", { "Test interface" }) + ")") && waitFor ("a-activated"));

            // the audio device: B wants A's (its saved setup), A plays through it: the bar, then the hand-over
            if (auto* link = ui.windowAudioForSnapshot())
            {
                check ("B: the bar names the device and A's project", until ([&] {
                           link->refresh();
                           return ui.windowAudioBarForSnapshot().isVisible() && link->holder() == others.front().pid;
                       }, 60));
                write (ui, ui.getLocalBounds().withHeight (ui.windowAudioBarForSnapshot().getBottom() + 60), "audio-bar");
                write (ui.windowAudioBarForSnapshot(), ui.windowAudioBarForSnapshot().getLocalBounds(), "audio-bar-only");
                const auto menuWithBar = ui.windowMenuForSnapshot();
                check ("B: the Window menu offers Make this window the one that plays", findItem (menuWithBar, tr ("Make this window the one that plays")) != nullptr);
                link->playHere();
                check ("B: A let the device go (audio-released)", waitFor ("b-audio-released"));
                check ("B: no other window has the device now: the bar is gone", until ([&ui] { return ! ui.windowAudioBarForSnapshot().isVisible(); }, 60));
                check ("B: and opens it (in a test: logged, no device is opened)", until ([] {
                           return platform::settingsFile ("test-mode.log").loadFileAsString().contains ("audio device: would open ASIO: Test interface");
                       }, 60));
            }
            else
                check ("B: the audio device link", false);
            mark ("b-finished");
        }
        pump (200);
        windows::stop();
        line ("done");
        return allOk;
    }

    // one popup menu, under `name`, as the menu routes draw them
    static void writeMenu (const juce::PopupMenu& menu, const juce::String& name, const juce::File& file)
    {
        saveMenus (juce::StringArray { name }, std::vector<juce::PopupMenu> { menu }, file.getFullPathName());
    }

    // --demo190plcheck <prefix> (round 190pl): the --demo operations tutorial batch 2 added (docs/DEMO_MODE.md), run against
    // a real main window on the desktop (a private X server): ui.export (the Export window by its IDs, its Export... button
    // writing the script's file without a file dialog), file.export, file.save (Save as, Save, Save new version) and the
    // Versions window's rows, midi.input through the virtual "LYNX demo keyboard" and its MIDI settings (listed in Options ›
    // MIDI; off = ignored), plugin.scan and the Plugin manager's IDs, ui.drag (a Playlist clip dragged through the window to
    // another track), ui.click "peer", ui.controlmenu on an insert effect's knob (its own menu: Create automation clip),
    // mixer.route (a sidechain, a send level) and the Mixer's route-arrow cells. <prefix>-checks.txt lists the checks; run it
    // with --lang pt-BR too. Files go to <prefix>-files/. Included in the app class of Main.cpp.
    bool runDemoCheck190pl (const juce::String& prefix)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        juce::String checks;
        bool allOk = true;
        const auto check = [&checks, &allOk, &prefix] (const juce::String& what, bool ok) {
            checks << (ok ? "ok   " : "FAIL ") << what << "\n";
            allOk = allOk && ok;
            juce::File (prefix + "-checks.txt").replaceWithText (checks);
        };
        const juce::File files (prefix + "-files");
        files.deleteRecursively();
        files.createDirectory();

        demo::setActive (true);
        AudioEngine offline (false);
        MainComponent ui (offline, false);
        ui.setSize (1600, 960);
        ui.addToDesktop (juce::ComponentPeer::windowAppearsOnTaskbar);
        ui.setTopLeftPosition (0, 0);
        ui.setVisible (true);
        const auto settle = [&offline] {
            for (int round = 0; round < 6; ++round)
            {
                for (int k = 0; k < 4; ++k)
                    offline.dispatchPendingMessages();
#if JUCE_LINUX || JUCE_WINDOWS
                for (int i = 0; i < 256 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i) {}
#endif
                juce::Thread::sleep (20);
            }
        };
        const auto settleFor = [&settle] (int ms) {
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
            while (juce::Time::getMillisecondCounter() < end)
                settle();
        };
        const auto op = [&ui] (const char* name, juce::var args) -> juce::var {
            try
            {
                return ui.handleDemoOp (name, args);
            }
            catch (const std::exception& e)
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("error", juce::String (e.what()));
                return o;
            }
        };
        const auto json = [] (const juce::String& text) { return juce::JSON::parse (text); };
        const auto ok = [] (const juce::var& r) { return ! r.hasProperty ("error"); };
        const auto why = [] (const juce::var& r) { return r.hasProperty ("error") ? " (" + r["error"].toString() + ")" : juce::String(); };
        const auto quoted = [] (const juce::File& f) { return juce::JSON::toString (f.getFullPathName()); };
        const auto found = [&op, &json] (const juce::String& target) { return (bool) op ("ui.find", json ("{\"target\":\"" + target + "\"}"))["found"]; };
        const auto rectOf = [] (const juce::var& r) {
            const auto& s = r["control"]["screen"];
            return juce::Rectangle<int> ((int) s["x"], (int) s["y"], (int) s["width"], (int) s["height"]);
        };

        op ("ui.welcome", json (R"({"choice":"fl"})"));
        offline.setSong (makeNewProject());
        settle();
        {
            const auto ops = demo::operations();
            bool all = true;
            for (const auto* name : { "ui.export", "file.export", "file.save", "midi.input", "plugin.scan", "ui.drag", "ui.controlmenu", "mixer.route" })
                all = all && ops.contains (name);
            check ("the batch 2 operations are listed", all);
        }
        // a few notes on channel 1, so the renders hold sound
        offline.editSong ([] (Song& s) {
            if (! s.patterns.empty())
                for (int k = 0; k < 4; ++k)
                {
                    Note n;
                    n.start = k * 4 * ticksPerStep;
                    n.length = ticksPerStep;
                    n.channel = 0;
                    n.key = 60;
                    s.patterns[0].notes.push_back (n);
                }
        });
        settle();

        // --- ui.export: the Export window, its IDs, Export... writes the script's file -------------------------------------
        const auto mp3 = files.getChildFile ("Demo song.mp3");
        auto r = op ("ui.export", json ("{\"format\":\"mp3\",\"path\":" + quoted (mp3) + "}"));
        settle();
        check ("ui.export opens the Export window (ID export.window)" + why (r), ok (r) && (bool) r["opened"] && found ("export.window"));
        auto f = op ("ui.find", json (R"({"target":"export.format.mp3"})"));
        check ("its MP3 format button is on", (bool) f["found"] && (bool) f["control"]["on"]);
        check ("and the WAV one off", ! (bool) op ("ui.find", json (R"({"target":"export.format.wav"})"))["control"]["on"]);
        check ("export.start and export.cancel found", found ("export.start") && found ("export.cancel"));
        r = op ("ui.click", json (R"({"target":"export.start"})"));
        for (int i = 0; i < 300 && ! (mp3.existsAsFile() && mp3.getSize() > 2000 && ! found ("export.window")); ++i)
            settle();
        settleFor (600);
        check ("Export... writes " + mp3.getFileName() + " without a file dialog (" + juce::String (mp3.getSize()) + " bytes)" + why (r),
               ok (r) && mp3.existsAsFile() && mp3.getSize() > 2000);
        check ("the window closed", ! found ("export.window"));
        check ("the target is used once", demo::takeExportTarget() == juce::File());
        r = op ("ui.export", json ("{\"format\":\"wav\",\"path\":" + quoted (files.getChildFile ("x.wav")) + ",\"run\":false}"));
        check ("ui.export run false only arms the file", ok (r) && ! found ("export.window") && demo::takeExportTarget() == files.getChildFile ("x.wav"));
        check ("ui.export refuses an unknown format", ! ok (op ("ui.export", json (R"({"format":"wma"})"))));

        // --- file.export ---------------------------------------------------------------------------------------------------------
        const auto wav = files.getChildFile ("now.wav");
        r = op ("file.export", json ("{\"format\":\"wav\",\"path\":" + quoted (wav) + "}"));
        check ("file.export writes a WAV here and now: " + juce::String ((juce::int64) r["bytes"]) + " bytes, " + juce::String ((double) r["seconds"], 2) + " s" + why (r),
               ok (r) && wav.existsAsFile() && (juce::int64) r["bytes"] > 1000 && (double) r["seconds"] > 0.5);
        check ("file.export refuses a relative path", ! ok (op ("file.export", json (R"({"format":"wav","path":"x.wav"})"))));

        // --- file.save + Versions ------------------------------------------------------------------------------------------------
        check ("file.save without a file or path is refused", ! ok (op ("file.save", json ("{}"))));
        const auto project = files.getChildFile ("Demo song.lynx");
        r = op ("file.save", json ("{\"path\":" + quoted (files.getChildFile ("Demo song")) + "}"));
        settle();
        check ("file.save path saves as " + project.getFileName() + why (r), ok (r) && project.existsAsFile() && r["file"].toString() == project.getFullPathName());
        offline.editSong ([] (Song& s) { s.bpm = 128.0; });
        r = op ("file.save", json ("{}"));
        settle();
        check ("file.save saves over it (not modified after)", ok (r) && ! (bool) r["modified"]);
        r = op ("file.save", json (R"({"newVersion":true})"));
        settle();
        check ("file.save newVersion writes Demo song_2: " + r["name"].toString(), ok (r) && files.getChildFile ("Demo song_2.lynx").existsAsFile());
        r = op ("file.save", json ("{\"path\":" + quoted (files.getChildFile ("armed.lynx")) + ",\"run\":false}"));
        check ("file.save run false only arms it", ok (r) && ! files.getChildFile ("armed.lynx").existsAsFile());
        op ("ui.click", json (R"({"target":"menu:File"})")); // (File › Save as..., the real menu: the armed file, no dialog)
        settle();
        r = op ("ui.click", json (R"({"target":"menu:Save as..."})"));
        settleFor (300);
        check ("then File › Save as... writes the armed file" + why (r), ok (r) && files.getChildFile ("armed.lynx").existsAsFile());
        juce::PopupMenu::dismissAllActiveMenus();
        r = op ("ui.show", json (R"({"panel":"versions"})"));
        settleFor (300);
        check ("ui.show versions opens the Versions window (ID versions)" + why (r), ok (r) && found ("versions") && found ("versions.newVersion"));
        f = op ("ui.find", json (R"({"target":"versions","cell":{"row":0,"column":0}})"));
        check ("its first row is a cell: " + rectOf (f).toString() + why (f), (bool) f["found"] && ! rectOf (f).isEmpty());
        check ("versions.open is off with no row chosen", ! (bool) op ("ui.find", json (R"({"target":"versions.open"})"))["control"]["enabled"]);
        r = op ("ui.click", json (R"({"target":"versions","cell":{"row":0,"column":0},"peer":true})"));
        settleFor (200);
        check ("a peer click on the row chooses it (Open as a copy on)" + why (r),
               ok (r) && (bool) op ("ui.find", json (R"({"target":"versions.open"})"))["control"]["enabled"]);
        check ("a row past the list is refused", ! ok (op ("ui.find", json (R"({"target":"versions","cell":{"row":99,"column":0}})"))));
        closeAllToolWindows();
        settle();

        // --- midi.input + Options › MIDI -----------------------------------------------------------------------------------------
        const auto activity = offline.getMidiActivity();
        r = op ("midi.input", json (R"({"events":[{"type":"note","note":60,"velocity":100,"at":0,"length":0.05},{"type":"cc","cc":1,"value":64,"at":0.02}]})"));
        settleFor (200);
        check ("midi.input plays the LYNX demo keyboard (" + juce::String ((int) r["messages"]) + " messages, activity " + juce::String ((int) activity) + " -> "
                   + juce::String ((int) offline.getMidiActivity()) + ")" + why (r),
               ok (r) && (int) r["messages"] == 3 && offline.getMidiActivity() >= activity + 3);
        {
            auto prefs = offline.getMidiPrefs();
            auto in = prefs.input (demo::virtualKeyboardName().toStdString());
            in.enabled = false;
            prefs.setInput (in);
            offline.setMidiPrefs (prefs, false);
            const auto before = offline.getMidiActivity();
            op ("midi.input", json (R"({"events":[{"type":"note","note":62,"at":0}]})"));
            settleFor (100);
            check ("turned off in its MIDI settings, the keyboard is ignored", offline.getMidiActivity() == before);
            in.enabled = true;
            prefs.setInput (in);
            offline.setMidiPrefs (prefs, false);
        }
        check ("midi.input refuses a note past 127", ! ok (op ("midi.input", json (R"({"events":[{"type":"note","note":128}]})"))));
        r = op ("ui.action", json (R"({"id":"view.midiSettings"})"));
        settleFor (400);
        {
            bool listed = false;
            for (int k = 0; k < 16 && ! listed; ++k)
            {
                const auto row = op ("ui.find", json ("{\"target\":\"midi.input." + juce::String (k) + ".enabled\"}"));
                listed = (bool) row["found"] && row["control"]["text"].toString() == demo::virtualKeyboardName() && (bool) row["control"]["on"];
            }
            check ("Options › MIDI lists the LYNX demo keyboard, on (midi.input.<n>.enabled)" + why (r), ok (r) && listed);
        }
        closeAllToolWindows();
        for (int i = juce::Desktop::getInstance().getNumComponents(); --i >= 0;) // (the settings window, if it is not a tool window)
            if (auto* top = juce::Desktop::getInstance().getComponent (i); top != nullptr && top != &ui)
                if (auto* w = dynamic_cast<juce::DialogWindow*> (top))
                    w->exitModalState (0);
        settle();

        // --- plugin.scan + the Plugin manager --------------------------------------------------------------------------------------
        const auto empty = files.getChildFile ("NoPlugins");
        empty.createDirectory();
        check ("plugin.scan refuses a missing folder", ! ok (op ("plugin.scan", json (R"({"folders":["/no/such/folder"]})"))));
        const auto finished = (int) op ("plugin.scan", json (R"({"status":true})"))["finished"];
        r = op ("plugin.scan", json ("{\"folders\":[" + quoted (empty) + "],\"add\":false}"));
        for (int i = 0; i < 300 && (int) op ("plugin.scan", json (R"({"status":true})"))["finished"] == finished; ++i)
            settle();
        check ("plugin.scan scans a folder in the background and finishes" + why (r),
               ok (r) && (int) op ("plugin.scan", json (R"({"status":true})"))["finished"] == finished + 1);
        r = op ("ui.show", json (R"({"panel":"pluginManager"})"));
        settleFor (300);
        check ("ui.show pluginManager opens it (IDs pluginManager, .scanNew, .folders, .search, .filter.effects)" + why (r),
               ok (r) && found ("pluginManager") && found ("pluginManager.scanNew") && found ("pluginManager.folders") && found ("pluginManager.search")
                   && found ("pluginManager.filter.effects"));
        r = op ("ui.type", json (R"({"target":"pluginManager.search","text":"xyz","interval":10})"));
        settleFor (300);
        {
            juce::TextEditor* box = nullptr;
            for (int i = 0; i < juce::Desktop::getInstance().getNumComponents() && box == nullptr; ++i)
                if (auto* top = juce::Desktop::getInstance().getComponent (i))
                    box = dynamic_cast<juce::TextEditor*> (lynx::onboardui::findShowing (*top, "pluginManager.search"));
            check ("ui.type types into the Plugin manager's search box" + why (r), ok (r) && box != nullptr && box->getText() == "xyz");
        }
        check ("ui.type refuses a control that is not a text field", ! ok (op ("ui.type", json (R"({"target":"pluginManager.scanNew","text":"a"})"))));
        closeAllToolWindows();
        settle();

        // --- ui.drag: a Playlist clip through the window onto the next track ------------------------------------------------------
        op ("ui.show", json (R"({"panel":"playlist"})"));
        settle();
        op ("ui.click", json (R"({"target":"playlist","cell":{"row":0,"column":0}})"));
        settle();
        const auto clipsBefore = offline.getSong().playlist.size();
        r = op ("ui.drag", json (R"({"from":{"target":"playlist","cell":{"row":0,"column":1}},"to":{"target":"playlist","cell":{"row":1,"column":9}},"seconds":0.3})"));
        settleFor (900);
        {
            const auto& pl = offline.getSong().playlist;
            const auto moved = std::any_of (pl.begin(), pl.end(), [] (const auto& c) { return c.track == 1; });
            check ("ui.drag moves the clip to track 2 through the window (" + juce::String ((int) clipsBefore) + " clip(s))" + why (r),
                   ok (r) && pl.size() == clipsBefore && moved);
        }
        check ("ui.drag refuses a missing end", ! ok (op ("ui.drag", json (R"({"from":{"target":"playlist"}})"))));

        // --- mixer.route + the route arrow's cell -------------------------------------------------------------------------------
        r = op ("mixer.route", json (R"({"from":1,"to":2,"kind":"sidechain"})"));
        {
            const auto& sends = offline.getSong().mixer[1].sends;
            const auto it = std::find_if (sends.begin(), sends.end(), [] (const MixerSend& s) { return s.target == 2; });
            check ("mixer.route sidechain: insert 1 -> 2 at level 0, still to the Master" + why (r), ok (r) && it != sends.end() && it->level == 0.0f && offline.getSong().mixer[1].toMaster);
        }
        r = op ("mixer.route", json (R"({"from":3,"to":2,"kind":"route","level":0.5})"));
        {
            const auto& sends = offline.getSong().mixer[3].sends;
            const auto it = std::find_if (sends.begin(), sends.end(), [] (const MixerSend& s) { return s.target == 2; });
            check ("mixer.route route with level 0.5" + why (r), ok (r) && it != sends.end() && std::abs (it->level - 0.5f) < 1.0e-6f);
        }
        check ("mixer.route refuses the Master as a source", ! ok (op ("mixer.route", json (R"({"from":0,"to":2})"))));
        check ("and a sidechain to the Master", ! ok (op ("mixer.route", json (R"({"from":1,"to":0,"kind":"sidechain"})"))));
        offline.undo();
        settle();
        {
            const auto& sends = offline.getSong().mixer[3].sends;
            check ("one undo step takes the route away", std::none_of (sends.begin(), sends.end(), [] (const MixerSend& s) { return s.target == 2; }));
        }
        op ("ui.show", json (R"({"panel":"mixer"})"));
        settle();
        f = op ("ui.find", json (R"({"target":"mixer.strips","cell":{"row":3,"column":2}})"));
        const auto arrow = rectOf (f);
        const auto fader = rectOf (op ("ui.find", json (R"({"target":"mixer.strips","cell":{"row":0,"column":2}})")));
        check ("the Mixer's route arrow under track 2 is cell row 3: " + arrow.toString(), (bool) f["found"] && ! arrow.isEmpty() && arrow.getY() > fader.getY());
        {
            const auto volumeBefore = offline.getSong().mixer[2].volume;
            const auto cap = rectOf (op ("ui.find", json (R"({"target":"mixer.strips","cell":{"row":4,"column":2}})")));
            check ("the fader's cap (row 4) sits on its rail: " + cap.toString(), ! cap.isEmpty() && fader.contains (cap.getCentre()));
            r = op ("ui.drag", json (R"({"from":{"target":"mixer.strips","cell":{"row":4,"column":2}},"to":{"target":"mixer.strips","cell":{"row":0,"column":2},"at":{"x":0.5,"y":0.95}},"seconds":0.3})"));
            settleFor (800);
            check ("ui.drag of the cap pulls the fader down (" + juce::String (volumeBefore, 3) + " -> " + juce::String (offline.getSong().mixer[2].volume, 3) + ")" + why (r),
                   ok (r) && offline.getSong().mixer[2].volume < volumeBefore - 0.1f);
            check ("the pan knob (row 5) and solo (row 6) are cells", (bool) op ("ui.find", json (R"({"target":"mixer.strips","cell":{"row":5,"column":2}})"))["found"]
                                                                       && (bool) op ("ui.find", json (R"({"target":"mixer.strips","cell":{"row":6,"column":2}})"))["found"]);
            check ("the insert slots are cells of mixer.slots", (bool) op ("ui.find", json (R"({"target":"mixer.slots","cell":{"row":0,"column":0}})"))["found"]);
        }

        // --- ui.editor insert + slot, param: targets, ui.controlmenu ------------------------------------------------------------
        offline.editSong ([] (Song& s) { s.mixer[1].effects.push_back (makeEffectSlot (EffectKind::facetEq)); });
        settle();
        r = op ("ui.editor", json (R"({"insert":1,"slot":0})"));
        settleFor (500);
        check ("ui.editor insert 1 slot 0 opens Facet EQ's window (ID editor)" + why (r), ok (r) && found ("editor") && found ("facetEq"));
        f = op ("ui.find", json (R"({"target":"facetEq","cell":{"row":110,"column":1000}})"));
        const auto above = rectOf (f);
        const auto zero = rectOf (op ("ui.find", json (R"({"target":"facetEq","cell":{"row":100,"column":1000}})")));
        const auto low = rectOf (op ("ui.find", json (R"({"target":"facetEq","cell":{"row":100,"column":100}})")));
        check ("Facet EQ's graph: +10 dB at 1 kHz sits above 0 dB, 100 Hz left of 1 kHz", (bool) f["found"] && above.getY() < zero.getY() && low.getX() < zero.getX());
        check ("its listen switch's segments are cells (EQ, source)", found ("facetEq") && (bool) op ("ui.find", json (R"({"target":"facetEq","cell":{"row":1,"column":1}})"))["found"]);
        const auto bandsBefore = [&offline] { int n = 0; for (int b = 0; b < 8; ++b) n += offline.getSong().mixer[1].effects[0].params[(std::size_t) facet::param (b, 0)] > 0.0f ? 1 : 0; return n; };
        const auto bands = bandsBefore();
        r = op ("ui.click", json (R"({"target":"facetEq","cell":{"row":106,"column":3000},"double":true,"peer":true})"));
        settleFor (400);
        check ("a double click on the graph adds a band (" + juce::String (bands) + " -> " + juce::String (bandsBefore()) + ")" + why (r), ok (r) && bandsBefore() == bands + 1);
        check ("ui.editor refuses an empty slot", ! ok (op ("ui.editor", json (R"({"insert":1,"slot":5})"))));
        op ("ui.action", json (R"({"id":"view.closeWindows"})"));
        settle();
        // a native effect whose editor draws its knobs (a modring::TargetSource): its knobs by parameter
        offline.editSong ([] (Song& s) { s.mixer[1].effects.push_back (makeEffectSlot (EffectKind::delay)); });
        settle();
        r = op ("ui.editor", json (R"({"insert":1,"slot":1})"));
        settleFor (500);
        juce::String knob;
        for (int p = 0; p < 64 && knob.isEmpty(); ++p)
            if (found ("param:insert.1.1." + juce::String (p)))
                knob = "param:insert.1.1." + juce::String (p);
        f = knob.isNotEmpty() ? op ("ui.find", json ("{\"target\":\"" + knob + "\"}")) : juce::var();
        check ("a knob of the editor found by its parameter: " + knob + " " + rectOf (f).toString(), knob.isNotEmpty() && ! rectOf (f).isEmpty() && rectOf (f).getWidth() < 200);
        check ("a param target in no open window is not found", ! found ("param:insert.5.0.0"));
        check ("a malformed param target is an error", ! ok (op ("ui.find", json (R"({"target":"param:nothing"})"))));
        if (knob.isNotEmpty())
        {
            const auto automations = offline.getSong().automations.size();
            r = op ("ui.controlmenu", json ("{\"insert\":1,\"slot\":1,\"parameter\":" + knob.fromLastOccurrenceOf (".", false, false) + "}"));
            settleFor (300);
            check ("ui.controlmenu opens the knob's own menu (Create automation clip in it)" + why (r), ok (r) && found ("menu:Create automation clip"));
            r = op ("ui.click", json (R"({"target":"menu:Create automation clip"})"));
            settleFor (300);
            check ("its Create automation clip makes the clip (" + juce::String ((int) automations) + " -> " + juce::String ((int) offline.getSong().automations.size()) + ")",
                   ok (r) && offline.getSong().automations.size() == automations + 1);
            juce::PopupMenu::dismissAllActiveMenus();
        }
        check ("ui.controlmenu refuses a parameter with no control showing", ! ok (op ("ui.controlmenu", json (R"({"channel":0,"parameter":"volume"})"))));
        op ("ui.action", json (R"({"id":"view.closeWindows"})"));
        settle();
        // a synth's designed page (its knobs painted by the page, demo::ParamSource): Cutoff and the others by parameter
        {
            int synth = -1;
            for (int c = 0; c < (int) offline.getSong().channels.size() && synth < 0; ++c)
                if (offline.getSong().channels[(std::size_t) c].instrument == InstrumentKind::synth)
                    synth = c;
            if (synth < 0)
            {
                offline.editSong ([] (Song& s) { ChannelDef d; d.name = "Synth"; d.instrument = InstrumentKind::synth; s.channels.push_back (d); });
                settle();
                synth = (int) offline.getSong().channels.size() - 1;
            }
            r = op ("ui.editor", json ("{\"channel\":" + juce::String (synth) + "}"));
            settleFor (500);
            juce::String synthKnob;
            int knobs = 0;
            for (int p = 0x2800; p < 0x2800 + 40; ++p) // (synthparam::base: the synth page's knobs)
                if (found ("param:channel." + juce::String (synth) + "." + juce::String (p)))
                {
                    ++knobs;
                    if (synthKnob.isEmpty())
                        synthKnob = "param:channel." + juce::String (synth) + "." + juce::String (p);
                }
            check ("the synth's page shows its knobs by parameter (" + juce::String (knobs) + " found)" + why (r), ok (r) && knobs >= 8);
            if (synthKnob.isNotEmpty())
            {
                const auto automations = offline.getSong().automations.size();
                r = op ("ui.controlmenu", json ("{\"target\":\"" + synthKnob + "\"}"));
                settleFor (300);
                check ("ui.controlmenu on a synth knob opens its menu" + why (r), ok (r) && found ("menu:Create automation clip"));
                op ("ui.click", json (R"({"target":"menu:Create automation clip"})"));
                settleFor (300);
                check ("and makes the automation clip", offline.getSong().automations.size() == automations + 1);
                juce::PopupMenu::dismissAllActiveMenus();
            }
        }
        op ("ui.action", json (R"({"id":"view.closeWindows"})"));
        settle();

        ui.removeFromDesktop();
        demo::setActive (false);
        checks << (allOk ? "ALL OK\n" : "FAILURES\n");
        juce::File (prefix + "-checks.txt").replaceWithText (checks);
        return allOk;
    }

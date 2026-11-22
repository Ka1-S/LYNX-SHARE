    // --demo190pgcheck <prefix> <project.lynx> (round 190pg): the --demo view operations the tutorial videos added
    // (docs/DEMO_MODE.md), run against a real main window on the desktop: the editors' cells (a Channel rack step toggles,
    // a Piano roll click draws a note, a Playlist click places the pattern, a Mixer track and the clip grid's slots and
    // scene buttons are found), menu items by their English text in any language (View › Layout style › Simple through
    // the menus themselves), ui.hover, ui.action and ui.open. <prefix>-checks.txt lists the checks; run it with --lang pt-BR
    // too. Included in the app class of Main.cpp.
    bool runDemoCheck190pg (const juce::String& prefix, const juce::File& project)
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
        // one operation; its result, or an "error" property
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
        const auto json = [] (const char* text) { return juce::JSON::parse (juce::String::fromUTF8 (text)); };
        const auto ok = [] (const juce::var& r) { return ! r.hasProperty ("error"); };
        const auto why = [] (const juce::var& r) { return r.hasProperty ("error") ? " (" + r["error"].toString() + ")" : juce::String(); };
        const auto rectOf = [] (const juce::var& r) {
            const auto& s = r["control"]["screen"];
            return juce::Rectangle<int> ((int) s["x"], (int) s["y"], (int) s["width"], (int) s["height"]);
        };
        const auto noteCount = [&offline] {
            std::size_t n = 0;
            for (const auto& p : offline.getSong().patterns)
                n += p.notes.size();
            return n;
        };

        op ("ui.welcome", json (R"({"choice":"fl"})"));
        offline.setSong (makeNewProject());
        settle();
        check ("the operations are listed", demo::operations().contains ("ui.hover") && demo::operations().contains ("ui.open") && demo::operations().contains ("ui.action"));

        // --- Channel rack: a step by row and column ---------------------------------------------------------------------------
        op ("ui.show", json (R"({"panel":"channelRack"})"));
        settle();
        auto found = op ("ui.find", json (R"({"target":"channelRack","cell":{"row":0,"column":2}})"));
        const auto step = rectOf (found);
        check ("Channel rack step (0, 2) found: " + step.toString(), (bool) found["found"] && ! step.isEmpty() && step.getWidth() < 40);
        const auto before = noteCount();
        auto clicked = op ("ui.click", json (R"({"target":"channelRack","cell":{"row":0,"column":2}})"));
        settle();
        check ("a click on the step adds a note (" + juce::String ((int) before) + " -> " + juce::String ((int) noteCount()) + ")", ok (clicked) && noteCount() == before + 1);
        op ("ui.click", json (R"({"target":"channelRack","cell":{"row":0,"column":2}})"));
        settle();
        check ("a second click removes it", noteCount() == before);
        check ("a step far outside the rack is refused", ! ok (op ("ui.find", json (R"({"target":"channelRack","cell":{"row":0,"column":4000}})"))));

        // --- Piano roll: a key and a step ------------------------------------------------------------------------------------------
        op ("ui.show", json (R"({"panel":"pianoRoll"})"));
        settle();
        found = op ("ui.find", json (R"({"target":"pianoRoll","cell":{"row":60,"column":1}})"));
        const auto key = rectOf (found);
        check ("Piano roll C5 step 1 found: " + key.toString() + why (found), (bool) found["found"] && ! key.isEmpty());
        found = op ("ui.find", json (R"({"target":"pianoRoll","cell":{"row":62,"column":1}})"));
        check ("D5 sits above C5", ! rectOf (found).isEmpty() && rectOf (found).getY() < key.getY());
        const auto beforeRoll = noteCount();
        clicked = op ("ui.click", json (R"({"target":"pianoRoll","cell":{"row":60,"column":1}})"));
        settle();
        check ("a click in the Piano roll draws a note", ok (clicked) && noteCount() == beforeRoll + 1);

        // --- Playlist: a track and a step -----------------------------------------------------------------------------------------
        op ("ui.show", json (R"({"panel":"playlist"})"));
        settle();
        found = op ("ui.find", json (R"({"target":"playlist","cell":{"row":0,"column":0}})"));
        check ("Playlist track 1 step 0 found: " + rectOf (found).toString() + why (found), (bool) found["found"] && ! rectOf (found).isEmpty());
        found = op ("ui.find", json (R"({"target":"playlist","cell":{"row":1,"column":16}})"));
        check ("track 2 bar 2 sits right of and below it", ! rectOf (found).isEmpty());
        const auto clipsBefore = offline.getSong().playlist.size();
        clicked = op ("ui.click", json (R"({"target":"playlist","cell":{"row":0,"column":0}})"));
        settle();
        check ("a click on the Playlist places the pattern (" + juce::String ((int) clipsBefore) + " -> " + juce::String ((int) offline.getSong().playlist.size()) + ")",
               ok (clicked) && offline.getSong().playlist.size() == clipsBefore + 1);

        // --- Mixer ---------------------------------------------------------------------------------------------------------------
        op ("ui.show", json (R"({"panel":"mixer"})"));
        settle();
        found = op ("ui.find", json (R"({"target":"mixer.strips","cell":{"row":0,"column":1}})"));
        check ("Mixer track 1's fader found: " + rectOf (found).toString(), (bool) found["found"] && ! rectOf (found).isEmpty());
        found = op ("ui.find", json (R"({"target":"mixer.strips","cell":{"row":1,"column":1}})"));
        check ("its mute switch found", (bool) found["found"] && ! rectOf (found).isEmpty());

        // --- menus by their English text (the real menus, in any language) ---------------------------------------------------
        auto r = op ("ui.find", json (R"({"target":"menu:View"})"));
        check ("the menu bar's View found: " + rectOf (r).toString() + " " + r["control"]["text"].toString(), (bool) r["found"] && ! rectOf (r).isEmpty());
        r = op ("ui.click", json (R"({"target":"menu:View"})"));
        settle();
        check ("a click on View opens it", ok (r));
        r = op ("ui.find", json (R"({"target":"menu:Layout style"})"));
        check ("its item Layout style found: " + rectOf (r).toString(), (bool) r["found"] && ! rectOf (r).isEmpty());
        r = op ("ui.hover", json (R"({"target":"menu:Layout style"})"));
        check ("ui.hover lights it", ok (r));
        r = op ("ui.click", json (R"({"target":"menu:Layout style"})"));
        settle();
        check ("a click on it opens its submenu", ok (r));
        r = op ("ui.find", json (R"({"target":"menu:Simple workflow"})")); // (the code's English; shown as "Simple layout")
        check ("the submenu's Simple layout found: " + rectOf (r).toString(), (bool) r["found"] && ! rectOf (r).isEmpty());
        r = op ("ui.click", json (R"({"target":"menu:Simple workflow"})"));
        settle();
        check ("a click on Simple switches the layout", ok (r) && op ("ui.window", {})["layout"].toString() == "simple");
        check ("no menu item is found once the menu closed", ! (bool) op ("ui.find", json (R"({"target":"menu:Layout style"})"))["found"]);
        check ("a missing menu item is an error", ! ok (op ("ui.click", json (R"({"target":"menu:No such item"})"))));
        // an item whose English text ends in "..." (its translation keeps the dots in the tables), two levels down
        op ("ui.click", json (R"({"target":"menu:File"})"));
        settle();
        op ("ui.click", json (R"({"target":"menu:Export"})"));
        settle();
        r = op ("ui.find", json (R"({"target":"menu:Wave file..."})"));
        check ("File › Export › Wave file... found by its English text: " + rectOf (r).toString(), (bool) r["found"] && ! rectOf (r).isEmpty());
        r = op ("ui.find", json (R"({"target":"menu:Ableton Live set (.als)..."})"));
        check ("and Ableton Live set (.als)...", (bool) r["found"]);
        juce::PopupMenu::dismissAllActiveMenus();
        settle();

        // --- the clip grid ---------------------------------------------------------------------------------------------------------
        op ("ui.layout", json (R"({"style":"clipLaunch"})"));
        settle();
        found = op ("ui.find", json (R"({"target":"clipGrid","cell":{"row":0,"column":0}})"));
        const auto slot = rectOf (found);
        check ("clip grid slot (scene 1, column 1) found: " + slot.toString(), (bool) found["found"] && ! slot.isEmpty());
        found = op ("ui.find", json (R"({"target":"clipGrid","cell":{"row":1,"column":-1}})"));
        check ("scene 2's launch button found, right of the slots and below scene 1", ! rectOf (found).isEmpty() && rectOf (found).getX() > slot.getRight() && rectOf (found).getY() > slot.getY());
        op ("ui.layout", json (R"({"style":"pattern"})"));
        settle();

        // --- ui.editor, ui.place ------------------------------------------------------------------------------------------------
        r = op ("ui.editor", json (R"({"channel":0})"));
        settle();
        check ("ui.editor opens channel 1's window (ID editor)" + why (r), ok (r) && (bool) op ("ui.find", json (R"({"target":"editor"})"))["found"]);
        r = op ("ui.place", json (R"({"target":"editor","x":40,"y":60})"));
        settle();
        check ("ui.place moves it" + why (r), ok (r) && (int) r["screen"]["x"] == 40 && (int) r["screen"]["y"] == 60);
        check ("ui.place refuses a control of the main window", ! ok (op ("ui.place", json (R"({"target":"toolbar.play","x":0,"y":0})"))));
        op ("ui.action", json (R"({"id":"view.closeWindows"})"));
        settle();

        // --- ui.action, ui.open --------------------------------------------------------------------------------------------------
        check ("ui.action refuses an unknown id", ! ok (op ("ui.action", json (R"({"id":"no.such.action"})"))));
        r = op ("ui.action", json (R"({"id":"view.typingPiano"})"));
        settle();
        check ("ui.action runs view.typingPiano", ok (r));
        op ("ui.action", json (R"({"id":"view.typingPiano"})")); // (back as it was)
        settle();
        check ("ui.open refuses a missing file", ! ok (op ("ui.open", json (R"({"file":"/no/such/file.lynx"})"))));
        if (project.existsAsFile())
        {
            const auto idBefore = offline.getSong().projectId;
            auto* args = new juce::DynamicObject();
            args->setProperty ("file", project.getFullPathName());
            r = op ("ui.open", juce::var (args));
            for (int i = 0; i < 100 && offline.getSong().projectId == idBefore; ++i)
                settle();
            check ("ui.open opens " + project.getFileName() + " (" + juce::String ((int) offline.getSong().channels.size()) + " channels)",
                   ok (r) && offline.getSong().projectId != idBefore);
        }
        else
            check ("the project to open exists: " + project.getFullPathName(), false);

        ui.removeFromDesktop();
        demo::setActive (false);
        checks << (allOk ? "ALL OK\n" : "FAILURES\n");
        juce::File (prefix + "-checks.txt").replaceWithText (checks);
        return allOk;
    }

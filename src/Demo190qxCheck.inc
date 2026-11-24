    // --demo190qxcheck <prefix> (round 190qx): the --demo targets tutorial batch 3 (options and settings) added
    // (docs/DEMO_MODE.md), run against a real main window on the desktop (a private X server): "text:" (a Settings tab, a
    // label, a search field by its hint, also once something is typed in it), "row:" (the control on a Settings label's
    // row), "settings" / "window:<id>" (a tool window), "panelmenu:<panel>" (a panel's Window options button and its menu),
    // the Playlist's automation-lane cells (row 2000 + t: the "+A" button, row 1000 + t: the lane), "devices" (the Clip-launch
    // Device View) with its window-button cells, and ui.click "peer" with dx / dy. <prefix>-checks.txt lists the checks;
    // run it with --lang pt-BR too. Included in the app class of Main.cpp.
    bool runDemoCheck190qx (const juce::String& prefix)
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
        const auto find = [&op, &json] (const juce::String& args) { return op ("ui.find", json (args)); };
        const auto found = [&find] (const juce::String& target) { return (bool) find ("{\"target\":" + juce::JSON::toString (target) + "}")["found"]; };

        op ("ui.welcome", json (R"({"choice":"fl"})"));
        offline.setSong (makeNewProject());
        settle();

        // --- Settings: the window, tabs, labels and rows by their English text ------------------------------------------
        op ("ui.action", json (R"({"id":"view.settings"})"));
        settleFor (400);
        check ("\"settings\" is the Settings window", found ("settings"));
        check ("\"window:settings\" too", found ("window:settings"));
        auto r = op ("ui.click", json (R"({"target":"text:Audio"})"));
        settleFor (300);
        check ("text:Audio clicks the Audio tab" + why (r), ok (r) && found ("text:Buffer size"));
        auto f = find (R"({"target":"row:Buffer size"})");
        check ("row:Buffer size is the combo box on that label's row", (bool) f["found"] && f["control"]["kind"] == "combo");
        auto label = find (R"({"target":"text:Buffer size"})");
        check ("and sits to the right of the label, on the same line",
               (bool) label["found"] && (int) f["control"]["screen"]["x"] > (int) label["control"]["screen"]["x"]
                   && std::abs ((int) f["control"]["screen"]["y"] - (int) label["control"]["screen"]["y"]) < 12);
        r = op ("ui.click", json (R"({"target":"text:General"})"));
        settleFor (300);
        check ("text:General switches the page" + why (r), ok (r) && found ("row:Text size") && ! found ("text:Buffer size"));
        r = op ("ui.click", json (R"({"target":"row:Text size"})"));
        settleFor (300);
        check ("a click on a row's combo box opens its menu (125 % listed)" + why (r), ok (r) && found ("menu:125 %"));
        juce::PopupMenu::dismissAllActiveMenus();
        settle();
        check ("an unknown text is not found", ! found ("text:No such setting anywhere"));
        r = op ("ui.type", json (R"({"target":"settings.find","text":"metronome","interval":10})"));
        settleFor (600);
        check ("the find box takes typing (ui.type)" + why (r), ok (r));
        op ("ui.action", json (R"({"id":"view.settings"})"));
        settleFor (300);
        check ("Settings closes again", ! found ("settings"));

        // --- a search field by its hint, typed into, and a list row below it (peer click with dy) -------------------------
        {
            const juce::String search = "text:Search an action or a key (e.g. \\\"Ctrl+L\\\")";
            op ("ui.click", json (R"({"target":"menu:Options"})"));
            settle();
            op ("ui.click", json (R"({"target":"menu:Manage shortcuts..."})"));
            settleFor (500);
            check ("Manage shortcuts: the search field is found by its hint", found (search.replace ("\\\"", "\"")));
            r = op ("ui.type", json ("{\"target\":\"" + search + "\",\"text\":\"metronome\",\"interval\":10}"));
            settleFor (500);
            check ("and still after typing into it" + why (r), ok (r) && found (search.replace ("\\\"", "\"")));
            check ("the Change button is disabled before a row is selected", ! (bool) find (R"({"target":"text:Change"})")["control"]["enabled"]);
            r = op ("ui.click", json ("{\"target\":\"" + search + "\",\"peer\":true,\"at\":{\"x\":0.3,\"y\":0.5},\"dy\":40}"));
            settleFor (400);
            check ("a peer click 40 px under the field selects the first row (Change enabled)" + why (r),
                   ok (r) && (bool) find (R"({"target":"text:Change"})")["control"]["enabled"]);
            op ("ui.action", json (R"({"id":"view.closeWindows"})"));
            settleFor (300);
        }

        // --- a panel's Window options button and the Playlist's automation lanes ------------------------------------------
        op ("ui.show", json (R"({"panel":"playlist"})"));
        settle();
        f = find (R"({"target":"panelmenu:playlist"})");
        check ("panelmenu:playlist is the Playlist's Window options button", (bool) f["found"] && f["control"]["kind"] == "button");
        r = op ("ui.click", json (R"({"target":"panelmenu:playlist"})"));
        settleFor (300);
        check ("its menu holds Automation lanes" + why (r), ok (r) && found ("menu:Automation lanes"));
        r = op ("ui.click", json (R"({"target":"menu:Automation lanes"})"));
        settleFor (300);
        check ("which turns the lanes on" + why (r), ok (r) && autolanes::shown());
        f = find (R"({"target":"playlist","cell":{"row":2000,"column":0}})");
        check ("cell row 2000 = track 1's +A button (left of its LED)", (bool) f["found"]);
        offline.editSong ([] (Song& s) { autolanes::addLane (s, 0, { AutomationTargetKind::tempo, 0, 0, 0 }, "Tempo", 0.5f); });
        settleFor (300);
        r = op ("ui.click", json (R"({"target":"playlist","cell":{"row":1000,"column":8},"at":{"x":0.5,"y":0.2}})"));
        settleFor (300);
        {
            const auto& song = offline.getSong();
            const auto a = song.playlistTracks.empty() || song.playlistTracks[0].lanes.empty() ? -1 : song.playlistTracks[0].lanes[0].automation;
            const auto points = a >= 0 && a < (int) song.automations.size() ? song.automations[(std::size_t) a].points.size() : 0;
            check ("a click on cell row 1000 adds a point to track 1's lane (" + juce::String ((int) points) + " points)" + why (r), ok (r) && points >= 1);
        }
        if (autolanes::shown())
            autolanes::setShown (false);

        // --- the Clip-launch layout's Device View ------------------------------------------------------------------------
        op ("ui.layout", json (R"({"style":"clipLaunch"})"));
        settleFor (500);
        op ("ui.action", json (R"({"id":"view.detailDevices"})"));
        settleFor (300);
        check ("\"devices\" is the Device View", found ("devices"));
        op ("ui.click", json (R"({"target":"clipGrid","cell":{"row":-2,"column":0}})"));
        settleFor (300);
        if (auto* detail = ui.getDetailViewForSnapshot(); detail != nullptr && detail->cardCountForCheck() > 0)
        {
            f = find (R"({"target":"devices","cell":{"row":0,"column":0}})");
            check ("cell row 0 = a device card's window button", (bool) f["found"] && (int) f["control"]["screen"]["width"] > 4);
        }
        else
            check ("(the first track shows no device card: the window-button cell is not checked)", true);
        op ("ui.layout", json (R"({"style":"pattern"})"));
        settle();

        check ("the batch 3 targets need no new operation (ui.find / ui.click)", demo::operations().contains ("ui.find"));
        ui.removeFromDesktop();
        demo::setActive (false);
        checks << (allOk ? "ALL OK\n" : "FAILURES\n");
        juce::File (prefix + "-checks.txt").replaceWithText (checks);
        return allOk;
    }

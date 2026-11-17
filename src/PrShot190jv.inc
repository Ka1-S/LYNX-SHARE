    // --prshot190jv <folder> (round 190jv, helper "pr", coverage pianoroll-0183): FL's Piano roll View › Scale
    // highlighting › Automatic drawn offscreen — the View menu with its Scale highlighting submenu, and the Piano roll
    // with the same A minor line under "No scale" (the black-key rows) and under Automatic (FL's rule: the rows out of the
    // guessed key dark, here A minor = A B C D E F G#), Automatic on two notes (no guess: no dark row), and Automatic
    // with three notes of a D major triad selected (only those count). Each Piano roll picture at 1x and at 150 %
    // ("@150"); the menus at the shot scale (run again with "--shot-scale 1.5 --shot-suffix @150" for them at 150 %).
    // Then FL's arpeggiator tool (prtools-0003): the Arpeggiate dialog and FL's Default pattern previewed on a chord.
    // <folder>/checks.txt lists what was checked. Included in the app class of Main.cpp.
    bool runPrShot190jv (const juce::File& folder)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        folder.createDirectory();
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const char* name, bool ok, const juce::String& detail = {}) {
            report.add (juce::String (name) + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        const auto writeImage = [&] (const juce::Image& image, const juce::File& f, const juce::String& name) {
            f.deleteFile();
            bool ok = false;
            if (auto out = f.createOutputStream())
                ok = juce::PNGImageFormat().writeImageToStream (image, *out);
            check (name.toRawUTF8(), ok && image.getWidth() > 0);
        };
        // 1x (or the asked shot scale) and, without an asked scale, 150 % beside it
        const auto shoot = [&] (juce::Component& c, const juce::String& name) {
            writeImage (shot::snapshot (c), shot::file (folder.getChildFile (name + ".png")), name);
            if (! shot::given())
                writeImage (c.createComponentSnapshot (c.getLocalBounds(), true, 1.5f), folder.getChildFile (name + "@150.png"), name + "@150");
        };

        // one channel (the Synth, 4) with an A minor line: A C E A B D E G# A over two bars
        AudioEngine engine (false);
        auto song = Song::makeDefault();
        song.currentPattern = 0;
        auto& p = song.patterns.front();
        p.notes.clear();
        std::int64_t start = 0;
        for (const auto key : { 57, 60, 64, 69, 59, 62, 64, 68, 69 })
        {
            Note n;
            n.channel = 4;
            n.start = start;
            n.length = 2 * ticksPerStep;
            n.key = key;
            p.notes.push_back (n);
            start += 2 * ticksPerStep;
        }
        engine.setSong (std::move (song));
        engine.setSelectedChannel (4);
        engine.dispatchPendingMessages();

        PianoRollComponent roll (engine);
        roll.setBounds (0, 0, 1100, 620);
        roll.setVisible (true);
        roll.scrollToNotes (true);
        engine.dispatchPendingMessages();

        // 1. the menus: View with its new submenu, and the submenu (FL's DFM order, then LYNX's scales)
        {
            const auto view = roll.getViewMenuForSnapshot();
            bool listed = false;
            for (juce::PopupMenu::MenuItemIterator it (view); it.next();)
                listed = listed || it.getItem().text == tr ("Scale highlighting");
            check ("view_menu_lists_scale_highlighting", listed);
            const auto sub = roll.getScaleHighlightMenuForSnapshot();
            juce::StringArray items;
            for (juce::PopupMenu::MenuItemIterator it (sub); it.next();)
                items.add (it.getItem().isSeparator ? juce::String ("-") : it.getItem().text.upToFirstOccurrenceOf ("\t", false, false));
            const auto automatic = items.indexOf (tr ("Automatic"));
            check ("submenu_automatic_after_no_scale", automatic > 0 && items[automatic - 1] == tr ("No scale"), items.joinIntoString (" | "));
            saveMenus (juce::StringArray { tr ("View"), tr ("Scale highlighting") }, std::vector<juce::PopupMenu> { view, sub },
                       shot::fileName (folder.getChildFile ("pr190jv-01-view-scale-highlighting.png").getFullPathName()));
        }

        // 2. "No scale": LYNX's black-key rows (FL's Black notes)
        roll.setScaleForSnapshot (0, 0);
        shoot (roll, "pr190jv-02-no-scale");

        // 3. Automatic: A minor guessed, its rows light and the others dark
        roll.setAutoScale (true);
        const auto guessed = roll.autoScaleKeyForTests();
        check ("automatic_guesses_a_minor", guessed == 21, juce::String (guessed));
        roll.repaint();
        shoot (roll, "pr190jv-03-automatic-a-minor");

        // 4. Automatic with three D major notes selected among them: only the selection counts
        {
            engine.editSong ([] (Song& s) {
                for (const auto key : { 62, 66, 69 })
                {
                    Note n;
                    n.channel = 4;
                    n.start = 20 * ticksPerStep;
                    n.length = 4 * ticksPerStep;
                    n.key = key;
                    s.patterns.front().notes.push_back (n);
                }
                s.patterns.front().sortNotes();
            });
            engine.dispatchPendingMessages();
            std::vector<int> triad;
            const auto& notes = engine.getSong().patterns.front().notes;
            for (int i = 0; i < (int) notes.size(); ++i)
                if (notes[(std::size_t) i].start == 20 * ticksPerStep)
                    triad.push_back (i);
            roll.selectMidiToolResult (triad);
            const auto selected = roll.autoScaleKeyForTests();
            check ("automatic_selection_d_major", triad.size() == 3 && selected == 2, juce::String (selected));
            roll.repaint();
            shoot (roll, "pr190jv-04-automatic-selection-d-major");
            roll.selectMidiToolResult ({});
            check ("automatic_back_to_all_notes", roll.autoScaleKeyForTests() >= 0);
        }

        // 5. Automatic on two notes: no guess, no dark row (FL: fewer than 3 notes)
        {
            engine.editSong ([] (Song& s) {
                auto& n = s.patterns.front().notes;
                n.resize (2);
            });
            engine.dispatchPendingMessages();
            check ("automatic_two_notes_no_guess", roll.autoScaleKeyForTests() == -1);
            roll.repaint();
            shoot (roll, "pr190jv-05-automatic-two-notes");
        }

        roll.setAutoScale (false);
        check ("automatic_off", ! roll.isAutoScaleForTests());

        // 6. coverage prtools-0003, FL's arpeggiator tool: the Arpeggiate dialog with its FL section (pattern None:
        //    LYNX's arpeggiator), then FL's Default pattern (the user's FL install, read only) previewed on a C E G
        //    chord of one bar: C E G C E G … every sixteenth (core/FlArpeggiator.h)
        {
            engine.editSong ([] (Song& s) {
                auto& n = s.patterns.front().notes;
                n.clear();
                for (const auto key : { 60, 64, 67 })
                {
                    Note c;
                    c.channel = 4;
                    c.start = 0;
                    c.length = 16 * ticksPerStep;
                    c.key = key;
                    n.push_back (c);
                }
                s.patterns.front().sortNotes();
            });
            engine.dispatchPendingMessages();
            roll.scrollToNotes (true);
            shoot (roll, "pr190jv-06-arp-chord");
            auto panel = makeNoteToolPanel (engine, NoteTool::arpeggiate, { 0, 1, 2 }, ticksPerStep);
            panel->setVisible (true);
            shoot (*panel, "pr190jv-07-arp-dialog");
            const auto chosen = noteToolChooseForSnapshot (*panel, tr ("Pattern"), "Default");
            check ("arp_fl_pattern_listed", chosen, chosen ? juce::String() : juce::String ("no FL install: Data/Patches/Scores/Arpeggiator"));
            if (chosen)
            {
                engine.dispatchPendingMessages();
                const auto& notes = engine.getSong().patterns.front().notes;
                auto order = true;
                for (int i = 0; i < (int) notes.size() && order; ++i)
                    order = notes[(std::size_t) i].start == i * ticksPerStep && notes[(std::size_t) i].key == (i % 3 == 0 ? 60 : i % 3 == 1 ? 64 : 67);
                check ("arp_default_c_e_g_sixteenths", notes.size() == 16 && order, juce::String ((int) notes.size()));
                shoot (*panel, "pr190jv-08-arp-dialog-default");
                roll.repaint();
                shoot (roll, "pr190jv-09-arp-default-result");
            }
            panel.reset(); // (not accepted: the chord comes back)
            engine.dispatchPendingMessages();
            check ("arp_cancel_restores", engine.getSong().patterns.front().notes.size() == 3);
        }
        folder.getChildFile (shot::fileName ("checks.txt")).replaceWithText (report.joinIntoString ("\n") + "\n");
        return allOk;
    }

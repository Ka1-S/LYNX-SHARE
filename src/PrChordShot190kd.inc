    // --prchord190kd <folder> (round 190kd): the FL | LYNX pairs of this round, drawn offscreen.
    //  1. FL's Chord progression tool with its strip of chord blocks on the A minor line of the FL session (A3 C4 E4 A4 B4
    //     D5 E5 G#4 A4, an eighth apart, a beat long): the dialog as it opens, then Analyze, a selected block, the block
    //     outlined red while learning from MIDI (a C/E played in), Names rel, the alternatives, and the menus (chord
    //     block, options for generation, key tab).
    //  2. The Piano roll with a chosen scale (A natural minor): FL's shading, the rows out of the scale dark.
    //  3. The Channel rack with its names left and centred, and its menu's Options › Channel name alignment.
    //  4. The arpeggiator's FL pattern list (headings per folder).
    // Each picture at 1x and at 150 % ("@150"); menus at the shot scale. <folder>/checks.txt lists what was checked.
    bool runPrChordShot190kd (const juce::File& folder)
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
        const auto shoot = [&] (juce::Component& c, const juce::String& name) {
            writeImage (shot::snapshot (c), shot::file (folder.getChildFile (name + ".png")), name);
            if (! shot::given())
                writeImage (c.createComponentSnapshot (c.getLocalBounds(), true, 1.5f), folder.getChildFile (name + "@150.png"), name + "@150");
        };
        const auto menuText = [] (const juce::PopupMenu& m) {
            juce::StringArray items;
            for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
                items.add (it.getItem().isSeparator ? juce::String ("-") : it.getItem().text.upToFirstOccurrenceOf ("\t", false, false));
            return items;
        };

        // the FL session's line on channel 4, each note a beat long, an eighth apart
        AudioEngine engine (false);
        auto song = Song::makeDefault();
        song.currentPattern = 0;
        auto& p = song.patterns.front();
        p.notes.clear();
        std::int64_t start = 0;
        for (const auto key : { 45, 48, 52, 57, 59, 62, 64, 56, 57 })
        {
            Note n;
            n.channel = 4;
            n.start = start;
            n.length = ppq;
            n.key = key + 12;
            p.notes.push_back (n);
            start += ppq / 2;
        }
        engine.setSong (std::move (song));
        engine.setSelectedChannel (4);
        engine.dispatchPendingMessages();
        const auto original = engine.getSong().patterns.front().notes;

        // --- 1. the chord tool ---------------------------------------------------------------------------
        {
            std::vector<int> all;
            for (int i = 0; i < (int) original.size(); ++i)
                all.push_back (i);
            auto panel = makeNoteToolPanel (engine, NoteTool::chordProgression, all, ticksPerStep);
            panel->setVisible (true);
            auto* state = noteToolChordStateForSnapshot (*panel);
            auto* view = noteToolChordViewForSnapshot (*panel);
            check ("chord_strip_present", state != nullptr && view != nullptr);
            if (state != nullptr && view != nullptr)
            {
                engine.dispatchPendingMessages();
                check ("strip_four_blocks", state->strip.blocks.size() == 4, juce::String ((int) state->strip.blocks.size()));
                check ("analyze_available_with_notes", state->analyzeAvailable);
                check ("default_mode_rework_keep", state->mode == chordstrip::GenMode::rework && state->nonChord == chordstrip::NonChord::keep);
                const auto kept = [&] {
                    const auto& now = engine.getSong().patterns.front().notes;
                    return std::all_of (original.begin(), original.end(), [&] (const Note& o) {
                        return std::any_of (now.begin(), now.end(), [&] (const Note& n) { return n.key == o.key && n.start == o.start && n.length == o.length; });
                    });
                };
                check ("keep_leaves_the_line", kept());
                shoot (*panel, "prchord-01-open");

                state->mode = chordstrip::GenMode::analyze;
                noteToolChordGenerateForSnapshot (*panel);
                engine.dispatchPendingMessages();
                juce::StringArray names;
                for (const auto& b : state->strip.blocks)
                    names.add (juce::String::fromUTF8 (chordstrip::blockName (state->strip, b, false).c_str()));
                check ("analyze_first_bar_a_chord_of_a", state->strip.blocks.front().chord.root == 9 || state->strip.blocks.front().chord.root == 4,
                       names.joinIntoString (" "));
                state->selected = { 0 };
                view->repaint();
                shoot (*panel, "prchord-02-analyze-selected");

                // Learn from MIDI input: the selected block outlined red; a C/E played in (E two octaves under the rest)
                // replaces it, and the selection stays (FL's "Auto learn next chord" is off)
                check ("learn_needs_one_selected", view->setLearning (true));
                shoot (*panel, "prchord-03-learning");
                const auto learned = view->learnKeysForSnapshot ({ 40, 60, 67, 72 });
                check ("learn_replaces_selected", learned && state->strip.blocks.front().chord.root == 0 && state->strip.blocks.front().chord.bass == 4);
                check ("learn_keeps_selection", state->selected == std::set<int> { 0 });
                shoot (*panel, "prchord-03b-learned");
                view->setLearning (false);

                state->relativeNames = true;
                view->repaint();
                shoot (*panel, "prchord-04-names-rel");
                state->relativeNames = false;

                view->showAlternativesForSnapshot (1);
                shoot (*panel, "prchord-05-alternatives");
                view->showAlternativesForSnapshot (-1);

                const auto chordMenu = view->chordMenuForSnapshot (0);
                const auto items = menuText (chordMenu);
                check ("chord_menu_fl_order", items.indexOf (tr ("Preview")) == 0 && items.indexOf (tr ("Learn from MIDI input")) > items.indexOf (tr ("Type in chord name..."))
                                                  && items.indexOf (tr ("Delete")) == items.size() - 1,
                       items.joinIntoString (" | "));
                const auto options = view->optionsMenuForSnapshot();
                check ("options_menu_keep_ticked", menuText (options).contains (tr ("Replace clashing")));
                saveMenus (juce::StringArray { "Chord", tr ("Options for generation"), tr ("Key") },
                           std::vector<juce::PopupMenu> { chordMenu, options, view->keyMenuForSnapshot() },
                           shot::fileName (folder.getChildFile ("prchord-06-menus.png").getFullPathName()));
            }
            panel.reset(); // (not accepted: the line comes back)
            engine.dispatchPendingMessages();
            check ("cancel_restores_line", engine.getSong().patterns.front().notes == original);
        }

        // --- 2. the Piano roll with a chosen scale: FL's rows out of the scale dark --------------------------
        {
            PianoRollComponent roll (engine);
            roll.setBounds (0, 0, 1100, 620);
            roll.setVisible (true);
            roll.scrollToNotes (true);
            engine.dispatchPendingMessages();
            int natural = 0;
            const auto& list = notetools::scales();
            for (int i = 0; i < (int) list.size(); ++i)
                if (juce::String (list[(std::size_t) i].name) == "Natural minor")
                    natural = i;
            check ("natural_minor_listed", natural > 0);
            roll.setScaleForSnapshot (0, 0);
            shoot (roll, "prchord-07-pr-no-scale");
            roll.setScaleForSnapshot (9, natural);
            roll.repaint();
            shoot (roll, "prchord-08-pr-a-minor");
            roll.setScaleForSnapshot (0, natural);
            roll.repaint();
            shoot (roll, "prchord-09-pr-c-minor");
            roll.setScaleForSnapshot (0, 0);
        }

        // --- 3. the Channel rack: names left / centred, and the menu --------------------------------------------
        {
            ChannelRackComponent rack (engine);
            rack.setBounds (0, 0, 640, 320);
            rack.setVisible (true);
            engine.dispatchPendingMessages();
            const auto was = ChannelRackComponent::nameAlignLeft;
            ChannelRackComponent::nameAlignLeft = false;
            rack.repaint();
            shoot (rack, "prchord-10-rack-center");
            ChannelRackComponent::nameAlignLeft = true;
            rack.repaint();
            shoot (rack, "prchord-11-rack-left");
            ChannelRackComponent::nameAlignLeft = was;

            MainComponent ui (engine, false);
            const auto menu = ui.getPanelMenuForSnapshot (workspace::Panel::channelRack);
            juce::PopupMenu align;
            bool found = false;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                if (it.getItem().text == tr ("Channel name alignment") && it.getItem().subMenu != nullptr)
                {
                    found = true;
                    align = *it.getItem().subMenu;
                }
            const auto alignItems = menuText (align);
            check ("rack_menu_name_alignment", found && alignItems == juce::StringArray { tr ("Left"), tr ("Center") }, alignItems.joinIntoString (" | "));
            saveMenus (juce::StringArray { "Channel Rack", tr ("Channel name alignment") }, std::vector<juce::PopupMenu> { menu, align },
                       shot::fileName (folder.getChildFile ("prchord-12-rack-menu.png").getFullPathName()));
        }

        // --- 4. the arpeggiator's pattern list ------------------------------------------------------------------
        {
            auto panel = makeNoteToolPanel (engine, NoteTool::arpeggiate, { 0, 1, 2 }, ticksPerStep);
            panel->setVisible (true);
            if (auto* box = noteToolLongestChoiceForSnapshot (*panel); box != nullptr && box->getNumItems() > 1)
            {
                check ("arp_list_has_headings", box->getRootMenu()->getNumItems() > box->getNumItems(), juce::String (box->getNumItems()));
                saveMenus (juce::StringArray { tr ("Pattern") }, std::vector<juce::PopupMenu> { *box->getRootMenu() },
                           shot::fileName (folder.getChildFile ("prchord-13-arp-patterns.png").getFullPathName()));
            }
            else
                check ("arp_list_has_headings", false, "no FL install: Data/Patches/Scores/Arpeggiator");
            panel.reset();
            engine.dispatchPendingMessages();
        }

        folder.getChildFile (shot::fileName ("checks.txt")).replaceWithText (report.joinIntoString ("\n") + "\n");
        return allOk;
    }

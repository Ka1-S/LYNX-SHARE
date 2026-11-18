    // --issuesshot190ll <prefix> (round 190ll, issues #200, #203): the Piano roll's scale choice in the three layouts —
    // the FL layout's scale box and the FL menu its click opens (Root note | Scale, two columns as FL's View › Scale
    // highlighting), docked in the Clip-launch layout (Live's Clip View Scale: root box | scale box) and in the Simple
    // layout (BandLab's Smart View: key box | scale box) — and a folder dropped on the Browser (the outline while it is
    // dragged over, the folder in the list afterwards). <prefix>-checks.txt lists what was checked. Run with
    // XDG_CONFIG_HOME / APPDATA pointing to a scratch folder (the Browser's folder list is written). @150:
    // LYNX_SHOT_SCALE=1.5 and LYNX_SHOT_SUFFIX=@150. Included in the app class of Main.cpp (savePng / saveMenus).
    bool runIssuesShot190ll (const juce::String& prefix)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const char* name, bool ok, const juce::String& detail = {}) {
            report.add (juce::String (name) + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        using namespace lynx;
        ui::motion::enabled() = false;

        // ---- #203: the Piano roll's scale choice --------------------------------------------------------------------
        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            song.currentPattern = 0;
            auto& p = song.patterns.front();
            p.notes.clear();
            std::int64_t start = 0;
            for (const auto key : { 62, 65, 69, 67, 64, 62, 60, 62 }) // a D minor line
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
            roll.setBounds (0, 0, 1180, 420);
            roll.setVisible (true);
            roll.scrollToNotes (true);
            engine.dispatchPendingMessages();

            // FL layout: one box, FL's two-column menu on a click
            layoutstyle::setCurrent (layoutstyle::Style::pattern);
            roll.setScaleForSnapshot (2, 2); // D Natural minor
            check ("fl_one_box", ! roll.scaleBoxesSplitForTests());
            check ("fl_box_text", roll.scaleTextForTests() == tr ("{0} {1}", { "D", tr ("Natural minor") }), roll.scaleTextForTests());
            const auto menu = roll.getScaleHighlightMenuForSnapshot();
            juce::StringArray items;
            int majors = 0, breaks = 0;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            {
                const auto& item = it.getItem();
                items.add (item.isSeparator ? juce::String ("-") : item.text.upToFirstOccurrenceOf ("\t", false, false));
                majors += item.text == tr ("Major") ? 1 : 0;
                breaks += item.shouldBreakAfter ? 1 : 0;
            }
            const auto rootHeader = items.indexOf (tr ("Root note")), scaleHeader = items.indexOf (tr ("Scale"));
            check ("fl_menu_roots_then_scales", rootHeader == 0 && scaleHeader == 13, items.joinIntoString (" | "));
            check ("fl_menu_each_scale_once", majors == 1, juce::String (majors));
            check ("fl_menu_two_columns", breaks == 1);
            check ("fl_menu_size", items.size() < 12 + (int) notetools::scales().size() + 8, juce::String (items.size()));
            savePng (shot::snapshot (roll), prefix + "-203-fl-pianoroll.png");
            saveMenus (juce::StringArray { tr ("Scale") }, std::vector<juce::PopupMenu> { menu }, prefix + "-203-fl-scale-menu.png");

            // the FL menu picks: a scale keeps the root, a root keeps the scale
            roll.setScaleForSnapshot (2, 1);
            check ("fl_scale_keeps_root", roll.scaleRootForTests() == 2 && roll.scaleIndexForTests() == 1);

            // docked, the Clip-launch layout (Live's Clip View Scale) and the Simple layout (BandLab's Smart View)
            for (const auto style : { layoutstyle::Style::clipLaunch, layoutstyle::Style::simple })
            {
                const juce::String name = style == layoutstyle::Style::clipLaunch ? "cliplaunch" : "simple";
                layoutstyle::setCurrent (style);
                roll.setDocked (true);
                roll.setBounds (0, 0, 1180, 300);
                roll.resized();
                check ((name + "_two_boxes").toRawUTF8(), roll.scaleBoxesSplitForTests());
                check ((name + "_scale_list_once").toRawUTF8(), roll.scaleNameItemsForTests() == (int) notetools::scales().size() + 1,
                       juce::String (roll.scaleNameItemsForTests()));
                roll.setScaleForSnapshot (0, 0);
                roll.pickScaleRootForTests (9); // A first, with no scale: kept for the scale picked next
                check ((name + "_root_waits").toRawUTF8(), roll.scaleIndexForTests() == 0);
                roll.pickScaleNameForTests (3); // No scale, Automatic, Major, Natural minor
                check ((name + "_a_minor").toRawUTF8(), roll.scaleRootForTests() == 9 && roll.scaleIndexForTests() == 2,
                       juce::String (roll.scaleRootForTests()) + " " + juce::String (roll.scaleIndexForTests()));
                roll.pickScaleRootForTests (2); // the root alone: D minor
                check ((name + "_root_keeps_scale").toRawUTF8(), roll.scaleRootForTests() == 2 && roll.scaleIndexForTests() == 2);
                check ((name + "_boxes_show_it").toRawUTF8(), roll.scaleRootTextForTests() == "D" && roll.scaleNameTextForTests() == tr ("Natural minor"),
                       roll.scaleRootTextForTests() + " | " + roll.scaleNameTextForTests());
                roll.setScaleForSnapshot (2, 2); // (the FL menu / a project's saved scale: the boxes follow)
                savePng (shot::snapshot (roll), prefix + "-203-" + name + "-pianoroll.png");
                roll.setDocked (false);
            }
            layoutstyle::setCurrent (layoutstyle::Style::pattern);
            roll.setBounds (0, 0, 1180, 420);
            roll.resized();
            check ("undocked_one_box_again", ! roll.scaleBoxesSplitForTests() && roll.scaleRootForTests() == 2 && roll.scaleIndexForTests() == 2);
        }

        // ---- #200: a folder dropped on the Browser joins its list ---------------------------------------------------
        {
            AudioEngine engine (false);
            const auto folder = juce::File (prefix + "-library").getChildFile ("My Drum Kit 190ll");
            folder.getChildFile ("Kicks").createDirectory();
            for (const auto* n : { "Kick 01.wav", "Kick 02.wav" })
                writeWaveDemo (folder.getChildFile ("Kicks").getChildFile (n), 120.0);
            writeWaveDemo (folder.getChildFile ("Snare.wav"), 120.0);
            BrowserComponent browser (engine);
            browser.setBounds (0, 0, 260, 600);
            browser.setVisible (true);
            engine.dispatchPendingMessages();
            check ("browser_wants_a_folder", browser.isInterestedInFileDrag ({ folder.getFullPathName() }));
            check ("browser_not_a_file_alone", ! browser.isInterestedInFileDrag ({ folder.getChildFile ("Snare.wav").getFullPathName() }));
            browser.fileDragEnter ({ folder.getFullPathName() }, 100, 200);
            savePng (shot::snapshot (browser), prefix + "-200-browser-drag-over.png");
            browser.filesDropped ({ folder.getFullPathName() }, 100, 200);
            engine.dispatchPendingMessages();
            savePng (shot::snapshot (browser), prefix + "-200-browser-after-drop.png");
            const auto added = browser.addUserFolders ({ folder.getFullPathName() }); // (again: listed already)
            check ("browser_folder_once", added == 0);
            savePng (shot::snapshot (browser), prefix + "-200-browser-drop-again.png");
        }

        report.add (juce::String ("result=") + (allOk ? "OK" : "FAIL"));
        juce::File (prefix + "-checks.txt").replaceWithText (report.joinIntoString ("\n") + "\n");
        return allOk;
    }

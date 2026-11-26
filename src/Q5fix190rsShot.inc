    // --q5fix190rsshot <prefix> <project.flp> (round 190rs, 0.1.32 stage Q5 fixes): the screens the round changed, off
    // screen (run it again with --shot-scale 1.5 --shot-suffix @150 for the 150 % pictures). <prefix>-checks.txt lists the
    // checks and the hint texts read. Included in the app class of Main.cpp.
    //   G-04 the rack's route box hint names the insert   G-01 an unassigned Playlist track's header says where it is heard
    //   G-15 the Playlist track menu's Change color...      G-20 / G-21 / G-12 the Help menu in the Pattern and Clip-launch layouts
    //   G-11 the tour's question                            G-03 the How-to cards and the Mixer tip
    //   G-06 the instrument / audio track marks             D-3 a channel added in the Pattern layout reads "---"
    //   G-30 the Clip-launch track header menu              G-17 Live's drop-area text      D-4 the F1 sheet's Ctrl+N row
    //   G-16 the Recording window's check box
    bool runQ5fix190rsShot (const juce::String& prefix, const juce::String& project)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        juce::String checks;
        bool allOk = true;
        const auto check = [&checks, &allOk, &prefix] (const juce::String& what, bool ok) {
            checks << (ok ? "ok   " : "FAIL ") << what << "\n";
            allOk = allOk && ok;
            juce::File (prefix + "-checks.txt").replaceWithText (checks);
        };
        const auto line = [&checks, &prefix] (const juce::String& what) {
            checks << "     " << what << "\n";
            juce::File (prefix + "-checks.txt").replaceWithText (checks);
        };
        const auto write = [&prefix] (juce::Component& c, const juce::String& name) { savePng (lynx::shot::snapshot (c), prefix + "-" + name + ".png"); };
        const auto area = [&prefix] (juce::Component& c, juce::Rectangle<int> r, const juce::String& name) {
            r = r.getIntersection (c.getLocalBounds());
            if (! r.isEmpty())
                saveArea (c, r, prefix + "-" + name + ".png");
        };
        const auto menuShot = [&prefix] (const juce::PopupMenu& menu, const juce::String& title, const juce::String& name) {
            saveMenus (juce::StringArray { title }, std::vector<juce::PopupMenu> { menu }, prefix + "-" + name + ".png");
        };
        const auto texts = [] (const juce::PopupMenu& menu) {
            juce::StringArray out;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                out.add (it.getItem().text);
            return out;
        };
        AudioEngine offline (false);
        MainComponent ui (offline, false);
        ui.setSize (1920, 1080);
        ui.setVisible (true);
        const auto settle = [&offline] {
            for (int k = 0; k < 4; ++k)
                offline.dispatchPendingMessages();
#if JUCE_LINUX || JUCE_WINDOWS
            for (int i = 0; i < 256 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i) {}
#endif
        };
        const auto toolbar = ui.getToolbarBoundsForSnapshot().withWidth (1100);
        const auto helpMenu = [&ui] {
            const auto names = ui.getMenuNamesForSnapshot();
            const auto help = names.indexOf (tr ("Help"));
            return help >= 0 ? ui.getMenuForSnapshot (help) : juce::PopupMenu();
        };

        // ---- Pattern layout, the FL demo -----------------------------------------------------------------------------------
        if (project.isNotEmpty())
            ui.openFile (juce::File (project));
        settle();
        ui.showDesktopForSnapshot (false);
        settle();
        const auto& song = offline.getSong();

        // G-04: the route box's hint (found by pointing along a row whose insert has a name of its own)
        {
            auto& rack = ui.getRackForSnapshot();
            ui::Hint found;
            for (int row = 0; row < (int) song.channels.size() && found.text.isEmpty(); ++row)
            {
                const auto t = song.channels[(std::size_t) row].mixerTrack;
                if (t <= 0 || t >= (int) song.mixer.size() || song.mixer[(std::size_t) t].name.empty())
                    continue;
                const auto y = rack.rowTopForSnapshot (row) + 8;
                for (int x = 0; x < juce::jmin (420, rack.getWidth()) && found.text.isEmpty(); x += 2)
                    if (const auto h = rack.hintAt ({ x, y }); h.text.contains (tr ("Target mixer track")))
                        found = h;
            }
            line ("rack route box hint: " + found.text + " | " + found.value);
            check ("G-04: the route box's value names the insert (\"Insert N · name\")", found.value.contains (juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))));
            check ("G-04: the gestures say the right button opens a menu", found.text.contains ("[RMB] menu"));
            ui.setHintForSnapshot (found);
            area (ui, toolbar, "rack-route-hint");
        }

        // G-01: an unassigned Playlist track's header (pointing down the headers)
        {
            auto& playlist = ui.getPlaylistForSnapshot();
            ui::Hint plays, lane;
            for (int y = 0; y < playlist.getHeight() && (plays.text.isEmpty() || lane.text.isEmpty()); y += 3)
            {
                const auto h = playlist.hintAt ({ 30, y });
                if (plays.text.isEmpty() && h.value.contains (juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 "))) && h.value.contains ("plays"))
                    plays = h;
                if (lane.text.isEmpty() && h.value.contains ("(a lane for clips"))
                    lane = h;
            }
            line ("Playlist header hint (clips): " + plays.value);
            line ("Playlist header hint (empty): " + lane.value);
            check ("G-01: an unassigned track with clips says where they are heard", plays.value.isNotEmpty());
            check ("G-01: an empty track says it is a lane for clips", lane.value.isNotEmpty());
            ui.setHintForSnapshot (plays);
            area (ui, toolbar, "pl-header-hint");
            ui.setHintForSnapshot (lane);
            area (ui, toolbar, "pl-header-hint-empty");
            PlaylistComponent::routingHints = false;
            const auto off = playlist.hintAt ({ 30, 0 });
            bool anyRoute = false;
            for (int y = 0; y < playlist.getHeight(); y += 3)
                anyRoute = anyRoute || playlist.hintAt ({ 30, y }).value.contains ("plays");
            check ("G-01: Routing hints off (FL-exact): the bare name", ! anyRoute);
            juce::ignoreUnused (off);
            PlaylistComponent::routingHints = true;

            // G-15: the track menu
            const auto menu = playlist.getTrackMenuForSnapshot (0);
            const auto items = texts (menu);
            check ("G-15: the track menu has Change color... and the Color palette under it",
                   items.contains (tr ("Change color...")) && items.contains (tr ("Color palette")));
            menuShot (menu, tr ("Playlist track"), "pl-track-menu");
        }

        // G-20 / G-21 / G-11: the Help menu in the Pattern layout
        {
            const auto menu = helpMenu();
            const auto items = texts (menu);
            check ("G-20 / G-21: Help has Tutorials, Manual, FAQ and What's new",
                   items.contains (tr ("Tutorials (lynx-share.com/learn)...")) && items.contains (tr ("Manual (wiki)..."))
                       && items.contains (tr ("FAQ and troubleshooting...")) && items.contains (tr ("What's new in this version...")));
            check ("G-11: the tour's item says it switches (Pattern layout)", items.contains (tr ("First-song tour (Simple layout)")));
            check ("G-12: Search actions shows ? in the Pattern layout", items.joinIntoString ("\n").contains (tr ("Search actions...") + "\t?"));
            menuShot (menu, tr ("Help"), "help-pattern");
        }

        // G-11: the question before the tour switches the layout
        {
            ui.startFirstSongTourAnywhere();
            settle();
            auto* box = juce::ModalComponentManager::getInstance()->getModalComponent (0);
            check ("G-11: a question first, the layout stays", box != nullptr && ui.getLayoutStyle() == layoutstyle::Style::pattern);
            if (box != nullptr)
            {
                write (*box, "tour-ask");
                box->exitModalState (0);
            }
            settle();
        }

        // G-03: the How-to cards and the Mixer's tip
        {
            auto rackCard = howto::makeCard (howto::Window::channelRack);
            auto mixerCard = howto::makeCard (howto::Window::mixer);
            check ("G-03: the cards have their fifth step", howto::stepCount (howto::Window::channelRack) == 5 && howto::stepCount (howto::Window::mixer) == 5);
            write (*rackCard, "howto-rack");
            write (*mixerCard, "howto-mixer");
            onboardui::TipCard tip (onboarding::Tip::mixerRouting);
            if (tip.getWidth() <= 0 || tip.getHeight() <= 0)
                tip.setSize (onboardui::TipCard::width, 170);
            write (tip, "tip-mixer");
        }

        // D-4: the F1 sheet in the Pattern layout (Ctrl+N = Save new version)
        {
            check ("D-4: Ctrl+N is Save new version in the Pattern layout",
                   ui.getKeyMapForSnapshot().find ({ 'N', keys::ctrl }, keys::Context::global, false) == "file.saveNewVersion");
            auto sheet = MainComponent::makeHelpContent (&ui.getKeyMapForSnapshot());
            area (*sheet, sheet->getLocalBounds().withHeight (240).withWidth (420), "f1-sheet-pattern");
        }

        // G-16: the Recording window
        {
            auto dialog = makeRecordChoiceDialog();
            if (dialog != nullptr)
            {
                dialog->setVisible (true);
                write (*dialog, "record-dialog");
            }
        }

        // ---- a new project: D-3, G-06 ----------------------------------------------------------------------------------------
        ui.startNewProjectForSnapshot (makeNewProject());
        settle();
        offline.editSong ([] (Song& s) {
            trackmodes::assignToNewInstrumentTrack (s, 0); // the Kick on an instrument track
            trackmodes::addAudioTrack (s);                 // an audio track on a free insert
            ChannelDef c;                                  // a channel added in the Pattern layout: the Master
            c.name = "3x Osc";
            c.mixerTrack = newChannelInsert (s, layoutstyle::Style::pattern);
            s.channels.push_back (c);
        });
        settle();
        check ("D-3: the added channel plays into the Master", song.channels.back().mixerTrack == 0);
        ui.showDesktopForSnapshot (true);
        settle();
        write (ui, "desktop-linked");
        {
            auto& rack = ui.getRackForSnapshot();
            write (rack, "rack-linked");
            auto& playlist = ui.getPlaylistForSnapshot();
            area (playlist, playlist.getLocalBounds().withWidth (420).withHeight (260), "pl-linked");
            auto& mixer = ui.getMixerForSnapshot();
            area (mixer, mixer.getLocalBounds().withWidth (700).withHeight (200), "mixer-linked");
        }

        // ---- the Clip-launch layout: G-30, G-17, G-12 ---------------------------------------------------------------------
        ui.setSize (2560, 1080); // (room right of the last track: Live's drop-area text)
        ui.setLayoutStyle (layoutstyle::Style::clipLaunch);
        settle();
        if (auto* grid = ui.getClipGridForSnapshot())
        {
            const auto menu = clipGridHeaderMenu (*grid, 0);
            const auto items = texts (menu);
            line ("grid header menu: " + items.joinIntoString (" | "));
            check ("G-30: Live's items, in Live's order", items.indexOf (tr ("Duplicate")) >= 0 && items.indexOf (tr ("Duplicate")) < items.indexOf (tr ("Delete"))
                                                             && items.indexOf (tr ("Delete")) < items.indexOf (tr ("Insert Audio Track"))
                                                             && items.indexOf (tr ("Insert Audio Track")) < items.indexOf (tr ("Insert MIDI Track"))
                                                             && items.indexOf (tr ("Insert MIDI Track")) < items.indexOf (tr ("Insert Return Track"))
                                                             && items.indexOf (tr ("Insert Return Track")) < items.indexOf (tr ("Select Track Content"))
                                                             && items.indexOf (tr ("Select Track Content")) < items.indexOf (tr ("Assign Track Color to Clips")));
            menuShot (menu, tr ("Track"), "grid-header-menu");
            write (*grid, "grid");
        }
        write (ui, "cliplaunch");
        {
            const auto menu = helpMenu();
            const auto all = texts (menu).joinIntoString ("\n");
            check ("G-12: in Live's look Search actions shows Ctrl+Shift+Y", ! theme::livelook::active() || all.contains (tr ("Search actions...") + "\tCtrl+Shift+Y"));
            menuShot (menu, tr ("Help"), "help-cliplaunch");
        }
        check ("D-4: Ctrl+N is New project in the Clip-launch layout",
               ui.getKeyMapForSnapshot().find ({ 'N', keys::ctrl }, keys::Context::global, false) == "file.new");
        ui.setLayoutStyle (layoutstyle::Style::pattern);
        settle();
        line (allOk ? "result=OK" : "result=FAIL");
        return allOk;
    }

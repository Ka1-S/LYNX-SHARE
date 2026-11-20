    // --issuesshot190ns <prefix> (round 190ns, issues #231, #232, #233, #234, #235), offscreen through the real components:
    // Project info › Created of a project saved without a date (#231), File › Open in new window (#232), the welcome
    // window's update choice (#233), the F1 sheet's Manage shortcuts link (#234), and a linked audio track and its mixer
    // track recoloured from either side (#235). <prefix>-checks.txt lists the checks; pictures @100 and @150. Included in
    // the app class of Main.cpp.
    bool runIssuesShot190ns (const juce::String& prefix)
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
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-190ns-shots");
        folder.createDirectory();

        // ================= #231: Project info › Created =================
        {
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            // a .lynx saved by 0.1.30 from the start-up song: no "created" in it
            const auto file = folder.getChildFile ("Saved without a date.lynx");
            juce::String error;
            auto song = Song::makeDefault();
            song.info.created = 0.0;
            check ("a project with no date saves without one", ProjectFile::save (song, file, error) && ! file.loadFileAsString().contains ("\"created\""));
            auto loaded = ProjectFile::load (file, error);
            check ("and loads with none", loaded.has_value() && loaded->info.created <= 0.0);
            offline.setSong (std::move (*loaded));
            const auto born = platform::fileBirthSeconds (file);
            line ("file system birth time of the file: " + juce::String (born, 3) + " (0 = not kept)");
            offline.markCreatedFrom (file);
            if (born > 0.0)
                check ("opened: Created = the file's creation date", std::abs (offline.getSong().info.created - born) < 1.0e-6);
            else
                check ("opened: no birth time on this file system, Created stays empty", offline.getSong().info.created <= 0.0);
            // a project that has a date keeps it
            const auto kept = offline.getSong().info.created;
            offline.markCreatedFrom (folder.getChildFile ("missing.lynx"));
            offline.markCreatedNow();
            check ("a date already there is kept (markCreatedFrom / markCreatedNow)", kept <= 0.0 || offline.getSong().info.created == kept);
            check ("save marks a project that still has none", offline.getSong().info.created > 0.0);
            check ("a missing file has no birth time", platform::fileBirthSeconds (folder.getChildFile ("missing.lynx")) == 0.0);
            auto panel = makeSettingsPanel (offline, SettingsPage::info);
            panel->setSize (juce::jmax (panel->getWidth(), 760), juce::jmax (panel->getHeight(), 560));
            write (*panel, panel->getLocalBounds(), "231-project-info");
            bool shownDate = false;
            std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
                if (auto* l = dynamic_cast<juce::Label*> (&c); l != nullptr && l->getText().contains (tr ("Created: {0}").upToFirstOccurrenceOf ("{", false, false))
                                                                && ! l->getText().contains (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"))))
                    shownDate = true;
                for (auto* child : c.getChildren())
                    find (*child);
            };
            find (*panel);
            check ("Project info shows a date after Created:", shownDate);
        }

        // ================= #232: File › Open in new window =================
        {
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            MainComponent ui (offline, false);
            ui.setSize (1600, 900);
            const auto menu = ui.getMenuForSnapshot (0);
            bool found = false;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                if (it.getItem().text == tr ("Open in new window") && it.getItem().subMenu != nullptr)
                {
                    found = true;
                    juce::StringArray items;
                    for (juce::PopupMenu::MenuItemIterator sub (*it.getItem().subMenu); sub.next();)
                        items.add (sub.getItem().text);
                    line ("File › Open in new window: " + items.joinIntoString (" | "));
                    check ("the submenu has Open... and Start-up project", items.contains (tr ("Open...")) && items.contains (tr ("Start-up project")));
                }
            check ("File menu: Open in new window (submenu)", found);
            check ("tests start no second program (LYNX_TEST_MODE logs it)", ! platform::testMode() || ui.openInNewWindow ({}));
        }

        // ================= #233: the welcome window's update choice =================
        {
            AudioEngine offline (false);
            auto wizard = makeSetupWizardContent (offline, {}, {});
            wizard->setSize (680, 2400);
            bool toggle = false, note = false;
            std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
                if (auto* b = dynamic_cast<juce::ToggleButton*> (&c); b != nullptr && b->getButtonText() == tr ("Check for updates automatically"))
                    toggle = b->isVisible() == updater::offered();
                if (auto* l = dynamic_cast<juce::Label*> (&c); l != nullptr && l->getText().contains ("GitHub"))
                    note = true;
                for (auto* child : c.getChildren())
                    find (*child);
            };
            find (*wizard);
            check ("welcome window: Check for updates automatically (shown where updates are offered)", toggle);
            check ("welcome window: the note says what the check sends and that nothing else goes online", note);
            // the bottom of the wizard: the scan options and the new Updates section
            wizard->setSize (680, 1600);
            if (auto* viewport = [&]() -> juce::Viewport* {
                    for (auto* c : wizard->getChildren())
                        if (auto* v = dynamic_cast<juce::Viewport*> (c))
                            return v;
                    return nullptr;
                }())
            {
                if (auto* body = viewport->getViewedComponent())
                {
                    const auto h = body->getHeight();
                    write (*body, { 0, juce::jmax (0, h - 260), body->getWidth(), 260 }, "233-welcome-updates");
                    line ("welcome window body height: " + juce::String (h));
                }
            }
            write (*wizard, wizard->getLocalBounds(), "233-welcome-whole");
        }

        // ================= #234: the F1 sheet's Manage shortcuts link =================
        {
            int clicked = 0;
            auto sheet = MainComponent::makeHelpContent (nullptr, [&clicked] { ++clicked; });
            juce::HyperlinkButton* link = nullptr;
            for (auto* c : sheet->getChildren())
                if (auto* b = dynamic_cast<juce::HyperlinkButton*> (c))
                    link = b;
            check ("F1 sheet: a Manage shortcuts... link", link != nullptr && link->getButtonText() == tr ("Manage shortcuts..."));
            if (link != nullptr)
            {
                if (link->onClick)
                    link->onClick(); // (what a click runs; triggerClick only posts it)
                check ("clicking it opens the Shortcut manager (the sheet's callback)", clicked == 1);
                check ("the link sits in the free space at the bottom left", link->getX() < 400 && link->getBottom() <= sheet->getHeight() - 10);
            }
            write (*sheet, sheet->getLocalBounds(), "234-shortcuts-sheet");
            auto plain = MainComponent::makeHelpContent();
            bool none = true;
            for (auto* c : plain->getChildren())
                none = none && dynamic_cast<juce::HyperlinkButton*> (c) == nullptr;
            check ("no link without a callback", none);
        }

        // ================= #235: a linked audio track and its mixer track keep one colour =================
        {
            AudioEngine offline (false);
            {
                auto song = Song::makeDefault();
                song.playlistTracks.resize (6);
                song.playlistTracks[0].name = "Time Warp Playback";
                offline.setSong (song);
            }
            offline.editSong ([] (Song& s) { trackmodes::makeAudioTrack (s, 0); });
            const auto mixer = offline.getSong().playlistTracks[0].mixerTrack;
            check ("Track mode › Audio track links track 1 to a mixer track", mixer > 0);
            // the mixer track is recoloured purple (Mixer › Rename / color): the Playlist track follows (FUN_012be6f0)
            offline.editSong ([mixer] (Song& s) { s.mixer[(std::size_t) mixer].colour = 0xff7a4bd1; });
            check ("mixer track recoloured: the Playlist track takes its colour", offline.getSong().playlistTracks[0].colour == 0xff7a4bd1);
            check ("and keeps its name (the insert's name is the track's)", offline.getSong().playlistTracks[0].name == "Time Warp Playback");
            // the Playlist track recoloured (Rename / color): the mixer track follows (FUN_012e60f0 → FUN_012e6010)
            offline.editSong ([] (Song& s) { s.playlistTracks[0].colour = 0xff3fa66b; s.playlistTracks[0].name = "Warp"; });
            check ("Playlist track recoloured / renamed: the mixer track follows",
                   offline.getSong().mixer[(std::size_t) mixer].colour == 0xff3fa66b && offline.getSong().mixer[(std::size_t) mixer].name == "Warp");
            offline.editSong ([mixer] (Song& s) { s.mixer[(std::size_t) mixer].colour = 0xff7a4bd1; s.mixer[(std::size_t) mixer].name = "Time Warp Playback"; });
            // with LYNX's "Link Playlist tracks to mixer tracks" (the reporter's headers), shots of both sides
            MainComponent ui (offline, false);
            ui.setSize (1600, 900);
            ui.selectTabForSnapshot (2);
            offline.dispatchPendingMessages();
            write (ui, { 0, 0, 900, 420 }, "235-playlist");
            ui.selectTabForSnapshot (3);
            offline.dispatchPendingMessages();
            write (ui, { 0, 0, 900, 620 }, "235-mixer");
        }

        folder.deleteRecursively();
        juce::File (prefix + "-checks.txt").replaceWithText (checks);
        std::printf ("%s", checks.toRawUTF8());
        return allOk;
    }

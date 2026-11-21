    // --design190ojshot <prefix> [project] (round 190oj, design review of every view): off screen at 100 % and @150, per
    // layout (p = Pattern, c = Clip-launch, s = Simple): the start desktop with the project, the Channel rack, Piano roll,
    // Playlist and Mixer each on the whole workspace, the Browser shown, the desktop with the extra panels, the toolbar, the
    // menu bar's menus, and an empty new project (the empty states). <prefix>-textfit.txt lists the labels and buttons whose
    // text does not fit (checkTextFit); run it with --lang <id> for the other languages. Included in the app class of
    // Main.cpp.
    bool runDesign190ojShot (const juce::String& prefix, const juce::String& projectPath)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        juce::StringArray made;
        const auto both = [&prefix, &made] (juce::Component& c, juce::Rectangle<int> area, const juce::String& name) {
            area = area.getIntersection (c.getLocalBounds());
            if (area.isEmpty())
                return;
            checkTextFit (c, name);
            savePng (c.createComponentSnapshot (area, true, 1.0f), prefix + "-" + name + ".png");
            savePng (c.createComponentSnapshot (area, true, 1.5f), prefix + "-" + name + "@150.png");
            made.add (name);
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
        const auto pump = [&settle] (int ms) {
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
            while (juce::Time::getMillisecondCounter() < end)
            {
                settle();
                juce::Thread::sleep (10);
            }
        };

        const std::pair<layoutstyle::Style, const char*> styles[] { { layoutstyle::Style::pattern, "p" },
                                                                    { layoutstyle::Style::clipLaunch, "c" },
                                                                    { layoutstyle::Style::simple, "s" } };
        for (const auto& [style, tag] : styles)
        {
            const juce::String t (tag);
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.setSong (makeNewProject());
            settle();
            if (projectPath.isNotEmpty())
                ui.openFile (juce::File (projectPath));
            settle();
            ui.waitForBrowserForSnapshot();
            pump (1500); // (the clips' waveforms load in the background)
            if (style != layoutstyle::Style::pattern)
            {
                ui.selectTabForSnapshot (2); // (the Playlist on the whole workspace, as the layout switch does in the real window)
                ui.setLayoutStyle (style);
            }
            ui.showDesktopForSnapshot (false);
            ui.setMetersForSnapshot (0.23, (std::int64_t) 412 << 20, 6);
            pump (300);
            both (ui, ui.getLocalBounds(), t + "-desktop");
            both (ui, ui.getToolbarBoundsForSnapshot(), t + "-toolbar");

            const char* tabs[] { "rack", "pianoroll", "playlist", "mixer" };
            for (int tab = 0; tab < 4; ++tab)
            {
                ui.selectTabForSnapshot (tab);
                pump (200);
                both (ui, ui.getLocalBounds(), t + "-" + tabs[tab]);
            }
            ui.showPanelsForSnapshot ({ workspace::Panel::browser });
            pump (200);
            both (ui, ui.getLocalBounds(), t + "-browser");
            ui.showDesktopForSnapshot (true);
            pump (300);
            both (ui, ui.getLocalBounds(), t + "-extra");

            {
                const auto names = ui.getMenuNamesForSnapshot();
                std::vector<juce::PopupMenu> menus;
                for (int m = 0; m < names.size(); ++m)
                    menus.push_back (ui.getMenuForSnapshot (m));
                saveMenus (names, menus, prefix + "-" + t + "-menus.png");
                const juce::ScopedValueSetter<float> scaled (shot::scaleOverride(), 1.5f);
                saveMenus (names, menus, prefix + "-" + t + "-menus@150.png");
            }

            // the empty states: a new project in this layout
            offline.setSong (makeNewProject());
            settle();
            ui.showDesktopForSnapshot (false);
            pump (300);
            both (ui, ui.getLocalBounds(), t + "-empty");
            ui.selectTabForSnapshot (0);
            pump (200);
            both (ui, ui.getLocalBounds(), t + "-empty-rack");
            ui.selectTabForSnapshot (3);
            pump (200);
            both (ui, ui.getLocalBounds(), t + "-empty-mixer");
        }
        ui.setLayoutStyle (layoutstyle::Style::pattern);
        settle();

        juce::File (prefix + "-textfit.txt").replaceWithText (textFitLog().joinIntoString ("\n") + "\n");
        juce::File (prefix + "-checks.txt").replaceWithText ("pictures " + juce::String (made.size()) + "\n" + made.joinIntoString ("\n") + "\n");
        return true;
    }

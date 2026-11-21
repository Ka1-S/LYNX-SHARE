    // --rackauto190owshot <prefix> [project.flp] [probe.flp] (round 190ow, PAT-7 rest / details DE-123): off screen at 100 %
    // and @150, FL's automation clip rows of the Channel rack doing what FL's do — Thief as FL's rack shows it, then with
    // steps on "Volume Duck" (lit, the playing one flashing its selector), MIN / MAX turned on "GROSS..w Bass", "CRASH
    // REVERB" muted and "Volume Duck" selected; the round's probe project (the FL picture's twin) as it opens. Checks: the
    // rows sit where FL's channel order has them, the export writes FL's channel numbers back, the steps start the clip.
    // <prefix>-checks.txt lists the checks (result=OK / FAIL). Included in the app class of Main.cpp.
    bool runRackAuto190owShot (const juce::String& prefix, const juce::String& projectPath, const juce::String& probePath)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        juce::StringArray checks;
        bool allOk = true;
        const auto check = [&checks, &allOk] (const juce::String& what, bool ok) {
            checks.add ((ok ? "ok   " : "FAIL ") + what);
            allOk = allOk && ok;
        };
        const auto both = [&prefix] (juce::Component& c, juce::Rectangle<int> area, const juce::String& name) {
            area = area.getIntersection (c.getLocalBounds());
            if (area.isEmpty())
                return;
            checkTextFit (c, name);
            savePng (c.createComponentSnapshot (area, true, 1.0f), prefix + "-" + name + ".png");
            savePng (c.createComponentSnapshot (area, true, 1.5f), prefix + "-" + name + "@150.png");
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
        const auto find = [&offline] (const char* name) {
            const auto& song = offline.getSong();
            for (int a = 0; a < (int) song.automations.size(); ++a)
                if (juce::String::fromUTF8 (song.automations[(std::size_t) a].name.c_str()).startsWithIgnoreCase (name))
                    return a;
            return -1;
        };
        auto& rack = ui.getRackForSnapshot();
        // the rack's rows from its top to a little past the last automation row (the rack picture of FL's capture)
        const auto rackShot = [&] (const juce::String& name) {
            ui.selectTabForSnapshot (0);
            pump (300);
            rack.repaint();
            pump (150);
            both (rack, rack.getLocalBounds().withHeight (juce::jmin (rack.getHeight(), 30 * 40)), name);
            both (ui, ui.getLocalBounds(), name + "-desktop");
        };

        ui.setLayoutStyle (layoutstyle::Style::pattern);
        offline.setSong (makeNewProject());
        settle();
        if (projectPath.isNotEmpty())
        {
            ui.openFile (juce::File (projectPath));
            settle();
            ui.showDesktopForSnapshot (false);
            pump (400);
            const auto& song = offline.getSong();

            // the rows where FL's channel order has them: the export numbers every automation clip channel as the .flp did
            {
                juce::MemoryBlock data;
                juce::File (projectPath).loadFileAsData (data);
                const auto original = flp::parseProject ({ static_cast<const std::uint8_t*> (data.getData()), data.getSize() });
                std::map<std::string, int> flIndex;
                for (const auto& c : original.channels)
                    if (c.type == flp::ChannelType::automationClip && ! c.automation.empty())
                        flIndex[c.name] = c.index;
                flpexport::Options options;
                flpexport::Report report;
                const auto exported = flp::parseProject (flpexport::exportFlp (song, options, report));
                int same = 0, compared = 0;
                juce::String moved;
                for (const auto& c : exported.channels)
                    if (c.type == flp::ChannelType::automationClip)
                        if (const auto it = flIndex.find (c.name); it != flIndex.end())
                        {
                            ++compared;
                            if (it->second == c.index)
                                ++same;
                            else
                                moved << " " << juce::String::fromUTF8 (c.name.c_str()) << " " << it->second << "->" << c.index;
                        }
                check ("export in rack order: " + juce::String (same) + " of " + juce::String (compared)
                           + " automation clip channels keep FL's channel number" + moved,
                       compared > 0 && same == compared);
                check ("export keeps the channel count: " + juce::String ((int) exported.channels.size()) + " (FL's file: "
                           + juce::String ((int) original.channels.size()) + ")",
                       exported.channels.size() == original.channels.size());
            }
            rackShot ("thief-rack");

            const auto duck = find ("Volume Duck"), gross = find ("GROSS"), crash = find ("CRASH");
            check ("Thief's Volume Duck / GROSS..w Bass / CRASH REVERB rows found", duck >= 0 && gross >= 0 && crash >= 0);
            if (duck >= 0 && gross >= 0 && crash >= 0)
            {
                offline.editSong ([duck, gross, crash] (Song& s) {
                    auto& p = s.activePattern();
                    for (int step = 0; step < 16; step += 4)
                        p.toggleAutoStep (duck, step);
                    s.automations[(std::size_t) gross].rangeMin = 0.25f;
                    s.automations[(std::size_t) gross].rangeMax = 0.8f;
                    s.automations[(std::size_t) crash].muted = true;
                });
                settle();
                rack.clickAutomationSelectorForSnapshot (duck, false);
                pump (200);
                rackShot ("thief-rack-edited");
                const auto& edited = offline.getSong();
                check ("the steps sit on Volume Duck's row only: " + juce::String ((int) edited.activePattern().autoNotes.size()),
                       edited.activePattern().autoNotes.size() == 4 && edited.activePattern().hasAutoStep (duck, 8));
                check ("Volume Duck selected alone", rack.selectedAutomationRows() == std::vector<int> { duck });
                // the steps start the clip in Pattern mode: its value at the second step is the clip's start value
                auto pattern = edited;
                pattern.mode = PlayMode::pattern;
                const auto v = rackauto::triggeredValue (pattern, duck, 4.0 * ticksPerStep + 1.0);
                check ("a step starts Volume Duck (Pattern mode): " + juce::String (v.first, 3) + " from tick " + juce::String (v.second, 0),
                       ! std::isnan (v.first) && std::abs (v.second - 4.0 * ticksPerStep) < 0.5);
                check ("muted CRASH REVERB drives nothing", std::isnan (rackauto::triggeredValue (pattern, crash, 1.0).first));
            }
        }

        if (probePath.isNotEmpty()) // the probe project of FL's picture, as LYNX opens it
        {
            offline.setSong (makeNewProject());
            settle();
            ui.openFile (juce::File (probePath));
            settle();
            pump (400);
            const auto& song = offline.getSong();
            int steps = 0;
            for (const auto& p : song.patterns)
                steps += (int) p.autoNotes.size();
            check ("the probe opens with its automation rows' steps: " + juce::String (steps), steps > 0);
            rackShot ("probe-rack");
        }

        checks.add (juce::String ("result=") + (allOk ? "OK" : "FAIL"));
        juce::File (prefix + "-checks.txt").replaceWithText (checks.joinIntoString ("\n") + "\n");
        return allOk;
    }

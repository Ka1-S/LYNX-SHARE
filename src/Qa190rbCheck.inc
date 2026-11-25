    // Round 190rb (0.1.32 quality release: crashes and memory bugs, from the crash reports, the sanitizer build and the
    // UI monkey test): each fix checked through the real components, off screen. Included in the app class of Main.cpp;
    // LYNX --qa190rbtest report.txt; --selftest "qa190rb". Run it in the sanitizer build (scripts/build-sanitize.sh) too:
    // the old code read freed or out-of-range memory there, which ASan names.
    juce::String qa190rbCheck (juce::String& log)
    {
        using namespace lynx;
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        juce::String result = "OK";
        const auto check = [&] (const juce::String& what, bool ok) {
            log << (ok ? "ok   " : "FAIL ") << what << "\n";
            if (! ok && result == "OK")
                result = "ERROR " + what;
        };

        // ---- crash report 68413071 (0.1.31.1, Windows): BrowserComponent::FileTree::paint. Current project's Effects /
        //      Generators entries carry their plugin's picture on three extra rows (DF-109); when the project changed,
        //      the section was listed into a temporary node and its entries moved over, but the picture rows kept the
        //      temporary as their parent, and paint() walks a row's parents for the tree guides
        {
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            MainComponent mainUi (offline, false);
            mainUi.setSize (1600, 900);
            offline.dispatchPendingMessages();
            // a captured picture for a hosted plugin (written for the check, removed after)
            const std::string key = "lynx-qa190rb-check-plugin";
            const auto picture = pluginthumbs::fileFor (key, false);
            const auto hadPicture = picture.existsAsFile();
            if (! hadPicture)
            {
                picture.getParentDirectory().createDirectory();
                juce::Image image (juce::Image::ARGB, 120, 60, true);
                juce::Graphics (image).fillAll (juce::Colours::orange);
                juce::FileOutputStream out (picture);
                juce::PNGImageFormat().writeImageToStream (image, out);
            }
            offline.editSong ([&key] (Song& s) {
                EffectSlot fx;
                fx.kind = EffectKind::plugin;
                fx.plugin.id = key;
                fx.plugin.name = "QA plugin";
                if (s.mixer.size() > 2)
                    s.mixer[1].effects.push_back (fx);
            });
            offline.dispatchPendingMessages();
            auto& browser = mainUi.getBrowserForSnapshot();
            browser.setSize (360, 900);
            const auto revealed = browser.revealProjectParameters (true, 1, 0);
            const auto pictureRows = browser.pictureRowsForCheck();
            check ("Browser: Current project > Effects lists the plugin with its picture rows (" + juce::String (pictureRows) + ")", revealed && pictureRows >= 3);
            // the project changes (one more effect): the section's items change and are listed again
            offline.editSong ([] (Song& s) {
                if (s.mixer.size() > 2)
                    s.mixer[2].effects.push_back (makeEffectSlot (EffectKind::fruityLimiter));
            });
            offline.dispatchPendingMessages();
            browser.refreshProjectForCheck();
            const auto dangling = browser.danglingRowParentsForCheck();
            check ("Browser: after the project changed, every row's parent is a node of the tree (dangling: " + juce::String (dangling) + ")",
                   dangling == 0 && browser.pictureRowsForCheck() >= 3);
            // the paint that crashed (tree guides walk the parents)
            juce::Image image (juce::Image::ARGB, browser.getWidth(), browser.getHeight(), true);
            {
                juce::Graphics g (image);
                browser.paintEntireComponent (g, false);
            }
            check ("Browser: the list paints after the change", true);
            if (! hadPicture)
                picture.deleteFile();
        }

        // ---- crash report ef495e29 (0.1.31, Windows): NativeEffectEditor::paint -> pluginheader::describe. The help line
        //      under LYNX's own panels named the control under the mouse with effectInfo().params[p], but a state-backed
        //      control or a hidden parameter with a control has an index past the plain parameters
        {
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            offline.dispatchPendingMessages();
            int extended = 0, failed = 0, kinds = 0;
            auto kindsToCheck = plugui::ownLayoutKinds();
            kindsToCheck.push_back (EffectKind::lynxTune);
            for (const auto kind : kindsToCheck)
            {
                offline.editSong ([kind] (Song& s) {
                    if (s.mixer.size() > 1)
                        s.mixer[1].effects = { makeEffectSlot (kind) };
                });
                offline.dispatchPendingMessages();
                const auto report = helpLineSelfTest (offline, 1, 0, (int) plugui::layoutFor (kind).pages.size());
                for (const auto& line : juce::StringArray::fromLines (report))
                {
                    if (line.contains (": OK"))
                        ++extended;
                    if (line.contains ("FAIL"))
                    {
                        ++failed;
                        log << "     " << line << "\n";
                    }
                }
                ++kinds;
            }
            check ("Native editors: the help line under every control of " + juce::String (kinds) + " panels paints, " + juce::String (extended)
                       + " of them past the plain parameters, none failed",
                   failed == 0 && kinds > 0 && extended > 0);
        }
        // ---- crash report 00d9f624 (0.1.31.1, Linux): the Simple dock's Drum Machine page named the sound under the mouse
        //      with song.channels[rows[...]] while its rows still listed the sounds of the song before a change (opening
        //      a project, deleting sounds): the page reads its rows again first now
        {
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            MainComponent ui (offline, false);
            ui.setSize (1600, 900);
            ui.setLayoutStyle (layoutstyle::Style::simple);
            offline.dispatchPendingMessages();
            auto* simple = ui.getSimpleWorkflowForSnapshot();
            SimpleDock* dock = nullptr;
            int drums = -1;
            if (simple != nullptr)
            {
                simple->addDrumsForSnapshot ({}, true);
                offline.dispatchPendingMessages();
                simple->hideChooser();
                for (int t = 0; t < (int) offline.getSong().playlistTracks.size() && drums < 0; ++t)
                    if (simpletracks::kindOf (offline.getSong(), t) == simpletracks::Kind::drums)
                        drums = t;
                if (drums >= 0)
                    ui.getPlaylistForSnapshot().selectTrack (drums);
                offline.dispatchPendingMessages();
                dock = simple->dockForSnapshot();
            }
            const auto rowsBefore = dock != nullptr ? dock->stepsRowsForCheck() : 0;
            check ("Simple dock: the Drum Machine page lists the kit (" + juce::String (rowsBefore) + " rows)", dock != nullptr && rowsBefore > 0);
            if (dock != nullptr && rowsBefore > 0)
            {
                // the song loses its sounds; no message runs before the mouse moves over the top row's first step
                offline.editSong ([] (Song& s) {
                    std::vector<int> all;
                    for (int c = 0; c < (int) s.channels.size(); ++c)
                        all.push_back (c);
                    removeChannels (s, all);
                });
                dock->stepsHoverCellForCheck (rowsBefore - 1, 0);
                if (auto* page = dock->stepsComponentForSnapshot())
                {
                    juce::Image image (juce::Image::ARGB, juce::jmax (1, page->getWidth()), juce::jmax (1, page->getHeight()), true);
                    juce::Graphics g (image);
                    page->paintEntireComponent (g, false);
                }
                check ("Simple dock: after the sounds went, the page's rows are the song's (" + juce::String (dock->stepsRowsForCheck()) + ")",
                       dock->stepsRowsForCheck() == (int) simplesteps::kitRows (offline.getSong(), drums).size());
                offline.dispatchPendingMessages();
            }
        }
        return result;
    }

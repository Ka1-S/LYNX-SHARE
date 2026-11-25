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

        return result;
    }

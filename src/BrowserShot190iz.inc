    // --browsershot190iz <folder> (round 190iz): the Browser drawn offscreen for the reference | LYNX
    // pairs — FL's six icon tabs (each one selected), the Current project tree with FL's guides, the hover star, the
    // tags (the TAGS panel with picked tags and its filtered list, the row menu's Tags section), the preview strip with
    // a part played from the middle, and the Clip-launch layout's Live-style browser (Samples, Instruments,
    // Collections, the tag chips). <folder>/checks.txt lists what was checked. Included in the app class of Main.cpp.
    bool runBrowserShot190iz (const juce::File& folder)
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
        // a small sample library of the user's own (a folder the Browser lists), with drum and loop names
        const auto library = folder.getChildFile ("library").getChildFile ("My Samples");
        for (const auto* name : { "Drums/Kick Hard.wav", "Drums/Kick Soft.wav", "Drums/Snare Tight.wav", "Drums/Hat Closed.wav", "Drums/Clap Wide.wav",
                                  "Loops/Break 120.wav", "Loops/Pad Dark 90.wav", "Bass/808 Long.wav" })
        {
            const auto f = library.getChildFile (name);
            f.getParentDirectory().createDirectory();
            if (! f.existsAsFile())
                writeWaveDemo (f, 120.0);
        }
        PluginHost::settingsFolder().createDirectory();
        PluginHost::settingsFolder().getChildFile ("browser-folders.txt").replaceWithText (library.getFullPathName());
        BrowserComponent::readPrefs = false;

        AudioEngine engine (false);
        // tags (FL's "Add a tag": comma-separated) and a starred file
        auto& tags = BrowserComponent::tagStore();
        tags = {};
        const auto path = [&library] (const char* name) { return library.getChildFile (name).getFullPathName().toStdString(); };
        tags.add ({ path ("Drums/Kick Hard.wav"), path ("Drums/Kick Soft.wav"), path ("Bass/808 Long.wav") }, "Kick,808");
        tags.add ({ path ("Drums/Kick Hard.wav"), path ("Drums/Snare Tight.wav") }, "Punchy");
        tags.add ({ path ("Loops/Pad Dark 90.wav") }, "Dark,Pad");
        check ("tags_made", tags.all().size() == 5 && tags.has (path ("Drums/Kick Hard.wav"), "808"));

        BrowserComponent browser (engine);
        browser.setStarred (library.getChildFile ("Drums").getChildFile ("Snare Tight.wav"), true);
        auto save = [&] (juce::Component& c, const juce::String& name) {
            const auto image = shot::paint (c, theme::colour::bg2);
            const auto f = shot::file (folder.getChildFile (name));
            f.deleteFile();
            juce::PNGImageFormat png;
            bool ok = false;
            if (auto out = f.createOutputStream())
                ok = png.writeImageToStream (image, *out);
            check (name.toRawUTF8(), ok && image.getWidth() > 0);
        };

        // 1. FL layout: the six icon tabs, each selected (FL's order), 260 px wide as LYNX's default Browser panel
        browser.setSize (260, 700);
        for (const auto t : browserplaces::tabOrder)
        {
            browser.setTab ((int) t);
            save (browser, "fl-tab-" + juce::String (browserplaces::tabName (t)).toLowerCase() + ".png");
        }
        check ("tab_order", browserplaces::slotOf (browserplaces::Tab::starred) == 5 && browserplaces::slotOf (browserplaces::Tab::plugins) == 2);
        // 2. PROJECT with its sections open (FL's "Current project" first, its tree metrics) and ALL with the library open
        browser.showProjectForSnapshot();
        save (browser, "fl-project-tree.png");
        browser.setSize (260, 900); // (tall enough for the library's rows under the places)
        browser.setTab (BrowserComponent::allTab);
        browser.locateFile (library.getChildFile ("Drums").getChildFile ("Kick Hard.wav"));
        browser.hoverRowForSnapshot ("Kick Soft");
        save (browser, "fl-all-hover-star.png");
        browser.setSize (260, 700);
        // 3. tags: the TAGS panel with Kick picked (Any), then Kick + Punchy with All; the row menu's Tags section
        browser.setTagsPanelShown (true);
        browser.setPickedTags ({ "Kick" });
        check ("tag_filter_any", browser.pickedTags().size() == 1);
        save (browser, "fl-tags-any.png");
        browser.setTagMatch (browsertags::Match::all);
        browser.setPickedTags ({ "Kick", "Punchy" });
        save (browser, "fl-tags-all.png");
        check ("tag_filter_all_one_item", tags.filter ({ "Kick", "Punchy" }, browsertags::Match::all).size() == 1);
        saveMenus ({ "Kick Hard.wav" }, { browser.buildFileMenu (library.getChildFile ("Drums").getChildFile ("Kick Hard.wav")) },
                   folder.getChildFile ("fl-file-menu-tags.png").getFullPathName());
        browser.setPickedTags ({});
        browser.setTagsPanelShown (false);
        // 4. the preview strip: a part played from 40 % (FL's Shift+Click / Live's click in the waveform)
        browser.showPreviewPlayedForSnapshot (library.getChildFile ("Loops").getChildFile ("Break 120.wav"), 0.4);
        juce::Thread::sleep (400);
        save (browser, "fl-preview-from-middle.png");

        // 5. the Clip-launch layout: Live's sidebar and list (330 px wide as Live's browser in the reference shot)
        browser.setSize (330, 740);
        browser.setLiveLayout (true);
        check ("live_layout", browser.isLiveLayout());
        auto entry = [] (browserplaces::Category c) {
            browserplaces::Entry e;
            e.category = c;
            return e;
        };
        browser.showLiveEntry (entry (browserplaces::Category::samples));
        save (browser, "live-samples.png");
        browser.showLiveEntry (entry (browserplaces::Category::instruments));
        save (browser, "live-instruments.png");
        browser.showLiveEntry (entry (browserplaces::Category::audioEffects));
        save (browser, "live-audio-effects.png");
        browserplaces::Entry favourites;
        favourites.group = browserplaces::Group::collections;
        favourites.category = browserplaces::Category::collection;
        favourites.collection = 0;
        browser.showLiveEntry (favourites);
        save (browser, "live-favorites.png");
        browser.showLiveEntry (entry (browserplaces::Category::samples));
        browser.setTagsPanelShown (true);
        browser.setTagMatch (browsertags::Match::any);
        browser.setPickedTags ({ "808" });
        save (browser, "live-tag-chips.png");
        browser.setPickedTags ({});
        browser.setTagsPanelShown (false);
        browser.setLiveLayout (false);
        check ("back_to_fl", ! browser.isLiveLayout());

        BrowserComponent::tagStore() = {};
        folder.getChildFile ("checks.txt").replaceWithText (report.joinIntoString ("\n") + "\n");
        return allOk;
    }

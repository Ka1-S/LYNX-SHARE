    // Round 190qi (TOP-NEXT R6, picker and Browser quality of life). Included in the app class of Main.cpp.
    //   picker190qiFakePlugins: plugin descriptions for the checks (no files: they are never loaded), some installed in
    //   several formats, as a scan lists them.
    //   picker190qiSelfTest (--selftest item picker190qi): the slot menu's Recent section and its pick, a plugin hidden
    //   from the menus' folders, one row per plugin's builds, a MIDI file's preview schedule.
    //   runPicker190qiShot (--picker190qishot <folder>): the Quick-add palette, the Plugin picker, the Plugin manager, the
    //   empty slot's menu and the Browser (MIDI preview, multi-selection, tag tree, Tab completion, file types, hidden
    //   folders) drawn offscreen at the shot scale, with <folder>/checks.txt.
    static void picker190qiFakePlugins (lynx::AudioEngine& engine, int extraCopies = 0)
    {
        auto& list = engine.getPluginHost().getKnownPlugins();
        int uid = 1000;
        auto add = [&] (const char* name, const char* vendor, const char* format, bool instrument, const char* category) {
            juce::PluginDescription d;
            d.name = d.descriptiveName = name;
            d.manufacturerName = vendor;
            d.pluginFormatName = format;
            d.isInstrument = instrument;
            d.category = category;
            d.fileOrIdentifier = juce::String ("/plugins/") + format + "/" + name + (juce::String (format) == "CLAP" ? ".clap" : juce::String (format) == "VST3" ? ".vst3" : ".so");
            d.uniqueId = d.deprecatedUid = ++uid;
            d.numInputChannels = instrument ? 0 : 2;
            d.numOutputChannels = 2;
            list.addType (d);
        };
        for (const auto* f : { "VST", "VST3", "CLAP" })
        {
            add ("Pro-Q 3", "FabFilter", f, false, "Fx|EQ");
            add ("OTT", "Xfer Records", f, false, "Fx|Dynamics");
            add ("Nova", "Tokyo Dawn Labs", f, false, "Fx|EQ");
        }
        for (const auto* f : { "VST", "VST3" })
        {
            add ("Serum", "Xfer Records", f, true, "Instrument|Synth");
            add ("Kontakt 7", "Native Instruments", f, true, "Instrument|Sampler");
            add ("ValhallaSupermassive", "Valhalla DSP", f, false, "Fx|Reverb");
        }
        for (const auto* f : { "VST3", "CLAP" })
            add ("Vital", "Vital Audio", f, true, "Instrument|Synth");
        add ("Decapitator", "Soundtoys", "VST3", false, "Fx|Distortion");
        add ("Surge XT", "Surge Synth Team", "CLAP", true, "Instrument|Synth");
        // --pickertime with 300+ plugins: more vendors' plugins, each in two or three formats
        for (int i = 0; i < extraCopies; ++i)
            for (const auto* f : { "VST", "VST3", "CLAP" })
                if (i % 3 != 0 || juce::String (f) != "CLAP")
                    add (("Plug " + juce::String (i)).toRawUTF8(), ("Vendor " + juce::String (i % 23)).toRawUTF8(), f, i % 4 == 0,
                         i % 4 == 0 ? "Instrument|Synth" : "Fx|Dynamics");
    }

    static juce::String picker190qiId (lynx::AudioEngine& engine, const juce::String& name, const juce::String& format)
    {
        for (const auto& t : engine.getPluginHost().getKnownPlugins().getTypes())
            if (t.name == name && t.pluginFormatName == format)
                return t.createIdentifierString();
        return {};
    }

    static juce::String picker190qiSelfTest (const juce::File& temp)
    {
        using namespace lynx;
        juce::StringArray errors;
        AudioEngine engine (false, -1, false); // (a plugin host that keeps no settings: nothing is written)
        engine.setSong (Song::makeDefault());
        picker190qiFakePlugins (engine);
        auto& host = engine.getPluginHost();

        // 1. Recent in the empty slot's menu: the effects picked last, newest first, and a pick of one adds it again
        const auto fx = quickpick::effectEntries (engine);
        std::string builtin;
        for (const auto& e : fx)
            if (e.folder == 0 && e.available && e.make)
            {
                builtin = e.key;
                break;
            }
        const auto proq = picker190qiId (engine, "Pro-Q 3", "VST3").toStdString();
        host.notePluginPicked (builtin);
        host.notePluginPicked (proq);
        juce::PopupMenu menu;
        std::vector<EffectSlot> added;
        const auto any = quickpick::addRecentItems (menu, engine, std::function<void (EffectSlot)> ([&added] (EffectSlot s) { added.push_back (s); }));
        std::vector<juce::PopupMenu::Item> items;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (! it.getItem().isSectionHeader && ! it.getItem().isSeparator)
                items.push_back (it.getItem());
        if (! any || items.size() != 2 || items[0].text != "Pro-Q 3")
            errors.add ("recent menu (" + juce::String ((int) items.size()) + " items, first \"" + (items.empty() ? juce::String() : items[0].text) + "\")");
        else
        {
            items[1].action(); // the built-in one: added, and now the newest
            if (added.size() != 1 || quickpick::effectKey (added[0]) != builtin || host.recentPlugins().front() != builtin)
                errors.add ("recent pick");
        }

        // 2. Hidden: a built-in effect hidden from the lists leaves the slot menu's folders, and comes back
        auto labelsOf = [&engine] {
            juce::PopupMenu m;
            quickpick::addFolderGroups (m, engine, std::function<void (EffectSlot)> ([] (EffectSlot) {}));
            juce::StringArray out;
            std::function<void (const juce::PopupMenu&)> walk = [&] (const juce::PopupMenu& pm) {
                for (juce::PopupMenu::MenuItemIterator it (pm); it.next();)
                {
                    out.add (it.getItem().text);
                    if (it.getItem().subMenu != nullptr)
                        walk (*it.getItem().subMenu);
                }
            };
            walk (m);
            return out;
        };
        const auto builtinName = quickpick::shownName (engine, builtin, [&] {
            for (const auto& e : fx)
                if (e.key == builtin)
                    return e.label;
            return juce::String();
        }());
        const auto before = labelsOf().contains (builtinName);
        host.setHidden (builtin, true);
        const auto hiddenGone = ! labelsOf().contains (builtinName);
        host.setHidden (builtin, false);
        if (! before || ! hiddenGone || ! labelsOf().contains (builtinName))
            errors.add ("hidden built-in in the slot menu (" + builtinName + ")");
        // ... and the recent list leaves a hidden plugin out
        host.setHidden (proq, true);
        juce::PopupMenu recentHidden;
        quickpick::addRecentItems (recentHidden, engine, std::function<void (EffectSlot)> ([] (EffectSlot) {}));
        for (juce::PopupMenu::MenuItemIterator it (recentHidden); it.next();)
            if (it.getItem().text == "Pro-Q 3")
                errors.add ("hidden plugin in Recent");
        host.setHidden (proq, false);

        // 3. One row per plugin: the scanned builds of Pro-Q 3 (VST, VST3, CLAP) are one row that loads the CLAP build
        std::vector<pluginpicks::Item> builds;
        std::vector<juce::String> ids;
        for (const auto& t : host.getKnownPlugins().getTypes())
        {
            builds.push_back ({ t.name.toStdString(), t.manufacturerName.toStdString(), t.pluginFormatName.toStdString(), t.isInstrument });
            ids.push_back (t.pluginFormatName);
        }
        const auto groups = pluginpicks::groupByFormat (builds);
        int proqRows = 0;
        for (const auto& g : groups)
            if (builds[(std::size_t) g.preferred()].name == "Pro-Q 3")
            {
                ++proqRows;
                if (g.members.size() != 3 || ids[(std::size_t) g.preferred()] != "CLAP")
                    errors.add ("Pro-Q 3 row");
            }
        if (proqRows != 1 || groups.size() != 9) // (Pro-Q 3, OTT, Nova, Serum, Kontakt 7, Valhalla, Vital, Decapitator, Surge XT)
            errors.add ("rows " + juce::String ((int) groups.size()));

        // 4. A MIDI file's preview: its notes from the first one, at the project's tempo
        {
            auto song = Song::makeDefault();
            song.bpm = 150.0;
            std::vector<Note> notes (2);
            notes[0].start = 960;
            notes[0].length = 480;
            notes[0].key = 60;
            notes[1].start = 1920;
            notes[1].length = 960;
            notes[1].key = 67;
            const auto bytes = MidiFileIo::writeNotes (song, notes, "Preview");
            const auto file = temp.getChildFile ("picker190qi-preview.mid");
            file.replaceWithData (bytes.getData(), bytes.getSize());
            juce::String error;
            const auto back = MidiFileIo::loadNotes (file, 0, error);
            const auto events = back.has_value() ? midipreview::schedule (*back, engine.getSong().bpm) : std::vector<midipreview::Event>();
            const auto beat = 60000.0 / engine.getSong().bpm; // (the project's tempo, not the file's 150)
            if (events.size() != 4 || std::abs (events[2].ms - beat) > 0.5 || std::abs (events.back().ms - 2.0 * beat) > 0.5)
                errors.add ("midi schedule (" + juce::String ((int) events.size()) + " events) " + error);
            file.deleteFile();
        }
        return errors.isEmpty() ? juce::String ("OK") : "ERROR " + errors.joinIntoString ("; ");
    }

    bool runPicker190qiShot (const juce::File& folder)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        ui::motion::enabled() = false;
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        folder.createDirectory();
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const juce::String& name, bool ok, const juce::String& detail = {}) {
            report.add (name + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        // every picture at 100 % and 150 % (name.png, name@150.png)
        const auto save = [&] (juce::Component& c, const juce::String& name) {
            for (const auto scale : { 1.0f, 1.5f })
            {
                const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
                const auto f = folder.getChildFile (name + (scale == 1.0f ? ".png" : "@150.png"));
                f.deleteFile();
                juce::PNGImageFormat png;
                bool ok = false;
                if (auto out = f.createOutputStream())
                    ok = png.writeImageToStream (image, *out);
                check ("shot_" + name + (scale == 1.0f ? "" : "@150"), ok && image.getWidth() > 0);
            }
        };
        const auto saveMenu = [&] (const juce::String& title, const juce::PopupMenu& m, const juce::String& name) {
            saveMenus ({ title }, { m }, folder.getChildFile (name + ".png").getFullPathName());
            shot::scaleOverride() = 1.5f;
            saveMenus ({ title }, { m }, folder.getChildFile (name + "@150.png").getFullPathName());
            shot::scaleOverride() = 0.0f;
        };
        const auto menuTexts = [] (const juce::PopupMenu& m) {
            juce::StringArray out;
            for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
                out.add (it.getItem().text);
            return out;
        };
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const auto click = [&] (juce::Component& c, juce::Point<int> at, juce::ModifierKeys mods) {
            return juce::MouseEvent (source, at.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, at.toFloat(), now, 1, false);
        };

        AudioEngine engine (false, -1, false);
        engine.setSong (Song::makeDefault());
        engine.setSelectedChannel (0);
        picker190qiFakePlugins (engine);
        auto& host = engine.getPluginHost();
        const auto id = [&engine] (const char* name, const char* format) { return picker190qiId (engine, name, format).toStdString(); };
        // picked last: Pro-Q 3 as VST3 (another format than its row's CLAP), Serum, a built-in effect, Vital
        std::string builtin;
        for (const auto& e : quickpick::effectEntries (engine))
            if (e.folder == 0 && e.available && e.make && e.key.find ("Limiter") != std::string::npos)
                builtin = e.key;
        host.notePluginPicked (id ("Vital", "CLAP"));
        host.notePluginPicked (builtin);
        host.notePluginPicked (id ("Serum", "VST3"));
        host.notePluginPicked (id ("Pro-Q 3", "VST3"));
        // hidden: Kontakt 7's VST2 build, Decapitator
        host.setHidden (id ("Kontakt 7", "VST"), true);
        host.setHidden (id ("Decapitator", "VST3"), true);
        check ("recent_count", host.recentPlugins().size() == 4u);

        // 1. the Quick-add palette (F8's list): Recent first in both lists; one entry per plugin
        {
            QuickAddTargets targets;
            targets.addInstrument = [] (ChannelDef) {};
            targets.addEffect = [] (EffectSlot) {};
            auto palette = makeQuickAddPalette (engine, std::move (targets));
            palette->setSize (720, 460);
            save (*palette, "quickadd-recent");
            QuickAddTargets slot;
            slot.effectsOnly = slot.effectsFirst = true;
            slot.effectsTitle = tr ("Effects: into slot {0}", { juce::String (3) });
            slot.addInstrument = [] (ChannelDef) {};
            slot.addEffect = [] (EffectSlot) {};
            auto search = makeQuickAddPalette (engine, std::move (slot));
            save (*search, "slot-search-recent");
            // the lists' rows (their ListBoxes: instruments, then effects)
            std::vector<juce::ListBox*> lists;
            std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
                for (auto* child : c.getChildren())
                {
                    if (auto* lb = dynamic_cast<juce::ListBox*> (child))
                        lists.push_back (lb);
                    find (*child);
                }
            };
            find (*palette);
            check ("quickadd_lists", lists.size() == 2u, juce::String ((int) lists.size()));
        }

        // 2. the Plugin picker (More plugins...): one row per plugin with its format chips; Recent; the row menu
        for (const auto instruments : { false, true })
        {
            showPluginPicker (engine, instruments, [] (PluginRef) {}, instruments ? std::function<void (ChannelDef)> ([] (ChannelDef) {}) : nullptr);
            auto* window = findToolWindow ("pluginpicker");
            check (instruments ? "picker_instruments_open" : "picker_effects_open", window != nullptr);
            if (window == nullptr)
                continue;
            auto* picker = window->getContentComponent();
            picker->setSize (760, 520);
            std::vector<juce::ListBox*> lists;
            for (auto* child : picker->getChildren())
                if (auto* lb = dynamic_cast<juce::ListBox*> (child))
                    lists.push_back (lb);
            const auto kind = juce::String (instruments ? "instruments" : "effects");
            if (lists.size() == 2)
            {
                // (the groups first, then the plugins)
                const auto rows = lists[1]->getModel()->getNumRows();
                check ("picker_" + kind + "_rows", rows == (instruments ? 4 + (int) vfx::allKinds.size() : 4), juce::String (rows)); // (Kontakt still listed: its VST3)
                save (*picker, "picker-" + kind + "-all");
                lists[0]->getModel()->listBoxItemClicked (2, click (*lists[0], { 20, 2 * 22 + 11 }, {})); // "Recent"
                const auto recent = lists[1]->getModel()->getNumRows();
                check ("picker_" + kind + "_recent", recent == (instruments ? 2 : 1), juce::String (recent)); // (Serum, Vital / Pro-Q 3)
                save (*picker, "picker-" + kind + "-recent");
            }
            delete window;
        }

        // 3. the Plugin manager: the Hidden column, then its filter
        {
            auto manager = makePluginManagerPanel (engine, true);
            manager->setSize (1040, 420);
            save (*manager, "manager-hidden-column");
            juce::Component* found = nullptr;
            std::function<void (juce::Component&)> byId = [&] (juce::Component& c) {
                for (auto* child : c.getChildren())
                {
                    if (child->getComponentID() == "pluginManager.filter.hidden")
                        found = child;
                    byId (*child);
                }
            };
            byId (*manager);
            if (auto* hiddenFilter = dynamic_cast<juce::Button*> (found))
            {
                hiddenFilter->triggerClick();
                engine.dispatchPendingMessages();
                save (*manager, "manager-hidden-filter");
                check ("manager_hidden_filter", true);
            }
            else
                check ("manager_hidden_filter", false);
        }

        // 4. the empty slot's menu: Recent above FL's categories (the same parts as EffectsPanel::showAddMenu)
        {
            juce::PopupMenu menu;
            const auto add = std::function<void (EffectSlot)> ([] (EffectSlot) {});
            quickpick::addRecentItems (menu, engine, add);
            quickpick::addFolderGroups (menu, engine, add);
            const auto texts = menuTexts (menu);
            check ("slot_menu_recent_first", texts.size() > 3 && texts[0] == tr ("Recent") && texts[1] == "Pro-Q 3", texts.joinIntoString (" | ").substring (0, 120));
            saveMenu ("Insert slot 3", menu, "slot-menu-recent");
        }

        // 5. the Browser: a library with MIDI files, a Backup folder and tags in a tree
        const auto library = folder.getChildFile ("library").getChildFile ("My Samples");
        for (const auto* name : { "Drums/Kick Hard.wav", "Drums/Kick Soft.wav", "Drums/Snare Tight.wav", "Backup/Kick Old.wav", "Bass/808 Long.wav" })
        {
            const auto f = library.getChildFile (name);
            f.getParentDirectory().createDirectory();
            if (! f.existsAsFile())
                writeWaveDemo (f, 120.0);
        }
        const auto midi = library.getChildFile ("MIDI").getChildFile ("Chords 120.mid");
        {
            midi.getParentDirectory().createDirectory();
            auto song = Song::makeDefault();
            std::vector<Note> notes;
            const int chords[][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 }, { 52, 55, 59 } };
            for (int c = 0; c < 4; ++c)
                for (const auto key : chords[c])
                {
                    Note n;
                    n.start = c * 4 * ppq;
                    n.length = 4 * ppq - 120;
                    n.key = key;
                    notes.push_back (n);
                }
            for (int i = 0; i < 16; ++i)
            {
                Note n;
                n.start = i * ppq + ppq / 2;
                n.length = ppq / 2;
                n.key = 69 + (i % 4) * 2;
                notes.push_back (n);
            }
            const auto bytes = MidiFileIo::writeNotes (song, notes, "Chords");
            midi.replaceWithData (bytes.getData(), bytes.getSize());
        }
        PluginHost::settingsFolder().createDirectory();
        PluginHost::settingsFolder().getChildFile ("browser-folders.txt").replaceWithText (library.getFullPathName());
        BrowserComponent::readPrefs = false;
        auto& tags = BrowserComponent::tagStore();
        tags = {};
        const auto path = [&library] (const char* name) { return library.getChildFile (name).getFullPathName().toStdString(); };
        {
            BrowserComponent browser (engine);
            browser.setSize (260, 720);
            browser.setTab (BrowserComponent::allTab);
            // a. the MIDI file previewed: its notes on the selected channel, the strip drawing them as it plays
            browser.locateFile (midi);
            browser.previewForSnapshot (midi);
            check ("midi_preview_plays", browser.midiPreviewPlaying() && browser.midiPreviewChannel() == 0,
                   juce::String (browser.midiPreviewChannel()));
            for (int i = 0; i < 40; ++i) // (~1.2 s: the timer sends the notes as their time comes)
            {
                engine.dispatchPendingMessages();
                juce::Thread::sleep (30);
            }
            save (browser, "browser-midi-preview");
            browser.stopPreview();
            check ("midi_preview_stops", ! browser.midiPreviewPlaying());
            // b. two files marked with Ctrl+click: right-click › "Add tag to 2 items..."
            const auto kickHard = library.getChildFile ("Drums").getChildFile ("Kick Hard.wav");
            const auto kickSoft = library.getChildFile ("Drums").getChildFile ("Kick Soft.wav");
            browser.locateFile (kickHard);
            browser.markForSnapshot ({ kickHard, kickSoft });
            save (browser, "browser-multi-select");
            const auto fileMenu = browser.buildFileMenu (kickHard);
            check ("batch_add_tag_item", menuTexts (fileMenu).contains (tr ("Add tag to {0} items...", { juce::String (2) })));
            saveMenu (kickHard.getFileName(), fileMenu, "browser-multi-select-menu");
            tags.add ({ path ("Drums/Kick Hard.wav"), path ("Drums/Kick Soft.wav") }, "Drums/Kick, 808");
            tags.add ({ path ("Drums/Snare Tight.wav") }, "Drums/Snare");
            tags.add ({ path ("Bass/808 Long.wav") }, "808");
            browser.tagsChangedForSnapshot();
            browser.markForSnapshot ({});
            // c. the tags panel as a tree, "Drums" picked (the items under it)
            browser.setTagsPanelShown (true);
            browser.setPickedTags ({ "Drums" });
            check ("tag_tree_parent_filter", tags.filter ({ "Drums" }, browsertags::Match::any).size() == 3u);
            save (browser, "browser-tag-tree");
            browser.setPickedTags ({});
            browser.setTagsPanelShown (false);
            // d. Tab completion: "#dr" shows "#Drums" greyed; Tab takes it, the next Tab "#Drums/Kick"
            browser.typeSearchForSnapshot ("#dr");
            check ("search_suggestion", browser.searchSuggestionForSnapshot() == "#Drums", browser.searchSuggestionForSnapshot());
            save (browser, "browser-search-completion");
            const auto took = browser.acceptSearchForSnapshot();
            check ("search_tab_takes_it", took && browser.searchTextForSnapshot() == "#Drums", browser.searchTextForSnapshot());
            browser.typeSearchForSnapshot ("#Drums");
            check ("search_suggestion_child", browser.searchSuggestionForSnapshot() == "#Drums/Kick", browser.searchSuggestionForSnapshot());
            browser.typeSearchForSnapshot ("bac");
            check ("search_suggestion_folder", browser.searchSuggestionForSnapshot() == "Backup", browser.searchSuggestionForSnapshot());
            browser.typeSearchForSnapshot ({});
            // e. file types: only audio (the MIDI folder stays, empty), and the window
            browser.locateFile (midi);
            const auto before = browser.listedLabelsForSnapshot();
            browser.setFileTypes ({ "wav", "flac", "ogg", "mp3", "aif", "aiff", "wv", "ncw", "speech" });
            browser.locateFile (kickHard);
            const auto audioOnly = browser.listedLabelsForSnapshot();
            check ("file_types_hide_midi", before.contains (midi.getFileName()) && ! audioOnly.contains (midi.getFileName()));
            save (browser, "browser-file-types-audio");
            auto panel = browser.makeFileTypesPanelForSnapshot();
            save (*panel, "browser-file-types-window");
            panel.reset();
            browser.setFileTypes ({});
            // f. hide folders named "Backup"
            browser.locateFile (kickHard);
            const auto withBackup = browser.listedLabelsForSnapshot();
            browser.setHiddenFolderNames ({ "Backup" });
            browser.locateFile (kickHard);
            const auto noBackup = browser.listedLabelsForSnapshot();
            check ("hidden_folder_name", withBackup.contains ("Backup") && ! noBackup.contains ("Backup"), noBackup.joinIntoString (","));
            save (browser, "browser-hidden-folder");
            browser.setHiddenFolderNames ({});
            juce::PopupMenu viewMenu;
            browser.addViewItemsForSnapshot (viewMenu);
            check ("view_menu_items", menuTexts (viewMenu).contains (tr ("File types...")) && menuTexts (viewMenu).contains (tr ("Hide folders named...")));
            saveMenu ("Browser options", viewMenu, "browser-view-menu");
        }
        tags = {};
        const auto selftest = picker190qiSelfTest (folder);
        check ("selftest_item", selftest == "OK", selftest);
        folder.getChildFile ("checks.txt").replaceWithText (report.joinIntoString ("\n") + "\n");
        return allOk;
    }

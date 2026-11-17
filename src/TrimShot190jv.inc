    // --trimshot190jv <folder> (round 190jv): FL's Mixer menu › "Trim unused tracks" drawn offscreen for the FL | LYNX
    // pairs — the mixer menu with the item, FL's two questions ("Remove unused tracks" with its two choices, then the
    // "This will delete N unused mixer track(s)" confirmation), and the mixer of a small project before and after each
    // choice (one undo step brings it back). Run it again with "--shot-scale 1.5 --shot-suffix @150" for the 150 %
    // pictures. <folder>/checks.txt lists what was checked. Included in the app class of Main.cpp.
    bool runTrimShot190jv (const juce::File& folder)
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
        const auto write = [&] (const juce::Image& image, const juce::String& name) {
            const auto f = shot::file (folder.getChildFile (name));
            f.deleteFile();
            bool ok = false;
            if (auto out = f.createOutputStream())
                ok = juce::PNGImageFormat().writeImageToStream (image, *out);
            check (name.toRawUTF8(), ok && image.getWidth() > 0);
        };
        const auto plain = [] (juce::String t) { return t.upToFirstOccurrenceOf ("\t", false, false).removeCharacters ("&"); };

        // a small project: Master + 8 inserts
        //  1 Kick, 2 Snare (channels)   3 colour + fader only (unlinked, empty)   4 "Reverb bus" fed by 2 (linked)
        //  5 "Old FX" with an effect (unlinked, non-empty)   6 default (unlinked)   7 "Vox" Playlist audio track (linked)
        //  8 default (unlinked)
        AudioEngine engine (false);
        engine.editSong ([] (Song& s) {
            s = Song::makeDefault();
            s.mixer = Song::defaultMixer (8);
            s.channels.resize (2);
            s.channels[0].mixerTrack = 1;
            s.channels[1].mixerTrack = 2;
            s.mixer[1].name = "Kick";
            s.mixer[2].name = "Snare";
            s.mixer[2].sends = { { 4, 0.6f } };
            s.mixer[3].colour = 0xff3a7bd5;
            s.mixer[3].volume = 0.55f;
            s.mixer[4].name = "Reverb bus";
            EffectSlot reverb;
            reverb.kind = EffectKind::reverb;
            s.mixer[4].effects.push_back (reverb);
            s.mixer[5].name = "Old FX";
            EffectSlot delay;
            delay.kind = EffectKind::delay;
            s.mixer[5].effects.push_back (delay);
            s.mixer[7].name = "Vox";
            s.playlistTracks.resize (std::max<std::size_t> (s.playlistTracks.size(), 1));
            s.playlistTracks[0].mode = PlaylistTrackMode::audio;
            s.playlistTracks[0].mixerTrack = 7;
            s.playlistTracks[0].name = "Vox";
        }, false);
        engine.dispatchPendingMessages();
        const auto before = engine.getSong();

        MixerComponent mixerView (engine);
        mixerView.setSize (1200, 640);
        mixerView.setVisible (true);
        auto& strips = mixerView.getStripsForSnapshot();
        strips.setSelection ({ 1 }, 1);
        const auto shootMixer = [&] (const juce::String& name) {
            engine.dispatchPendingMessages();
            mixerView.resized();
            write (shot::snapshot (mixerView), name);
        };

        // 1. the menu: FL's Options section has it after View / Detached and a separator; LYNX's Mixer section after
        //    Disk recording, Plugin delay compensation and Mixer layout
        const auto menu = strips.buildMenu (1);
        juce::StringArray items;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            items.add (it.getItem().isSeparator ? juce::String ("-") : plain (it.getItem().text));
        const auto at = items.indexOf (tr ("Trim unused tracks"));
        check ("menu_item", at > 0, juce::String (at));
        check ("menu_after_layout_and_separator", at > 1 && items[at - 1] == "-" && items[at - 2] == tr ("Mixer layout"),
               items[at - 2] + " | " + items[at - 1]);
        check ("menu_before_the_lynx_options", items[at + 1] == "-");
        saveMenus (juce::StringArray { "Mixer track" }, std::vector<juce::PopupMenu> { menu }, shot::fileName (folder.getChildFile ("trim-01-menu.png").getFullPathName()));

        // 2. FL's questions
        {
            FlMessage ask { tr ("Remove unused tracks"), tr ("Select the mixer track types to be removed."), { tr ("OK"), tr ("Cancel") }, false };
            ask.options = { tr ("Unlinked tracks only"), tr ("Unlinked and non-empty tracks") };
            ask.icon = juce::MessageBoxIconType::WarningIcon;
            auto box = makeFlMessageDialog (ask);
            write (shot::snapshot (*box), "trim-02-dialog-choices.png");
            const auto count = (int) mixerops::unusedTracks (engine.getSong(), mixerops::TrimUnused::unlinkedOnly).size();
            auto confirm = makeFlMessageDialog ({ tr ("Remove unused tracks"),
                                                  tr ("This will |delete| {0} unused mixer track(s). Are you sure?", { juce::String (count) }),
                                                  { tr ("Yes"), tr ("No") }, true });
            write (shot::snapshot (*confirm), "trim-03-dialog-confirm.png");
            bool listed = false;
            for (const auto& m : messages::all())
                listed = listed || juce::String (m.id) == "TrimUnusedMixerTracks";
            check ("manage_messages_entry", listed);
        }

        // 3. before / after each choice, as one undo step
        shootMixer ("trim-04-mixer-before.png");
        check ("unused_unlinked_only", (mixerops::unusedTracks (before, mixerops::TrimUnused::unlinkedOnly) == std::vector<int> { 3, 6, 8 }));
        check ("unused_and_non_empty", (mixerops::unusedTracks (before, mixerops::TrimUnused::unlinkedAndNonEmpty) == std::vector<int> { 3, 5, 6, 8 }));
        for (const auto mode : { mixerops::TrimUnused::unlinkedOnly, mixerops::TrimUnused::unlinkedAndNonEmpty })
        {
            const auto nonEmpty = mode == mixerops::TrimUnused::unlinkedAndNonEmpty;
            int removed = 0;
            engine.editSongStructural ([&removed, mode] (Song& s) { removed = mixerops::trimUnusedTracks (s, mode); });
            shootMixer (nonEmpty ? "trim-06-mixer-after-non-empty.png" : "trim-05-mixer-after-unlinked-only.png");
            const auto& s = engine.getSong();
            if (nonEmpty)
                check ("after_non_empty", removed == 4 && s.mixer.size() == 5 && s.mixer[3].name == "Reverb bus" && s.mixer[4].name == "Vox"
                                              && s.mixer[2].sends.size() == 1 && s.mixer[2].sends[0].target == 3 && s.playlistTracks[0].mixerTrack == 4);
            else
                check ("after_unlinked_only", removed == 3 && s.mixer.size() == 6 && s.mixer[3].name == "Reverb bus" && s.mixer[4].name == "Old FX"
                                                  && s.mixer[5].name == "Vox" && s.mixer[2].sends.size() == 1 && s.mixer[2].sends[0].target == 3
                                                  && s.playlistTracks[0].mixerTrack == 5);
            engine.undo(); // one step back to the whole mixer
            engine.dispatchPendingMessages();
            check (nonEmpty ? "undo_non_empty_one_step" : "undo_unlinked_only_one_step",
                   engine.getSong().mixer.size() == before.mixer.size() && engine.getSong().mixer[3].colour == before.mixer[3].colour
                       && engine.getSong().mixer[5].name == "Old FX" && engine.getSong().playlistTracks[0].mixerTrack == 7);
        }

        folder.getChildFile (shot::fileName ("checks.txt")).replaceWithText (report.joinIntoString ("\n") + "\n");
        return allOk;
    }

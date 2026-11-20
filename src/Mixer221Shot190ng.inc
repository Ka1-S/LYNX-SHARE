    // --mixer221shot190ng <prefix> (round 190ng, issue #221 "Can't DEL on empty mixer channel"), offscreen: with "Show only
    // used inserts" on, "+" brings back an empty insert and Del (its question's choice remembered) deletes it: the blank
    // insert that takes its number (FL keeps the count) must not come back into view, and the selection goes to a shown
    // insert. Also: FL's menu item and order, and where a remembered answer is undone (Settings › General).
    // <prefix>-checks.txt lists the checks; pictures @100 and @150. Included in the app class of Main.cpp.
    bool runMixer221Shot190ng (const juce::String& prefix)
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
        const auto keptUsedOnly = MixerStrips::showUsedOnly;
        const auto keptHidden = messages::hiddenList();
        MixerStrips::showUsedOnly = true;
        AudioEngine offline (false);
        {
            // the issue's mixer: inserts 8, 9, 10, 17, 18 in use (a channel routed to each), the rest empty
            auto song = Song::makeDefault();
            song.mixer = Song::defaultMixer (18);
            for (const auto t : { 8, 9, 10, 17, 18 })
            {
                ChannelDef c;
                c.name = "Ch " + std::to_string (t);
                c.mixerTrack = t;
                song.channels.push_back (c);
            }
            offline.setSong (song);
        }
        MainComponent ui (offline, false);
        ui.setSize (1600, 900);
        ui.selectTabForSnapshot (3);
        offline.dispatchPendingMessages();
        auto& mixer = ui.getMixerForSnapshot();
        auto& strips = mixer.getStripsForSnapshot();
        const auto shownCount = [&strips, &offline] {
            int n = 0;
            for (int t = 1; t < (int) offline.getSong().mixer.size(); ++t)
                n += strips.isHidden (t) ? 0 : 1;
            return n;
        };
        offline.editSong ([] (Song&) {}); // (the main window's first look at the song: FL's Auto select linked modules picks the
        offline.dispatchPendingMessages(); //  selected channel's insert once, as at start-up)
        strips.selectTrack (18);
        strips.layoutForTest();
        const auto before = shownCount();
        const auto count = (int) offline.getSong().mixer.size();
        // the issue's "+": a new empty insert at the end, kept in view (MixerStrips' Part::add, without the menu)
        offline.editSong ([] (Song& s) { auto& t = s.mixer.emplace_back(); t.name = "Insert " + std::to_string (s.mixer.size() - 1); });
        const auto added = (int) offline.getSong().mixer.size() - 1;
        strips.revealForTest (added);
        check ("\"+\": the new empty insert shows and is selected", ! strips.isHidden (added) && strips.getSelection() == std::vector<int> { added });
        check ("\"+\": one more insert shows", shownCount() == before + 1);
        write (mixer, mixer.getLocalBounds().withHeight (juce::jmin (mixer.getHeight(), 700)), "mixer-before-del");

        // Del with "Remember my choice" ticked earlier (FL's "Yes" remembered): no question, the delete runs at once
        messages::setShown ("DeleteMixerTrack", false);
        const auto countBefore = (int) offline.getSong().mixer.size();
        ui.runActionForSnapshot ("mixer.deleteTracks");
        const auto selNow = strips.getSelection();
        line ("selected right after Del: " + juce::String (selNow.empty() ? -1 : selNow.front()));
        offline.dispatchPendingMessages();
        strips.layoutForTest();
        const auto sel = strips.getSelection();
        line ("after Del: " + juce::String ((int) offline.getSong().mixer.size()) + " inserts (count kept as FL), selected "
              + juce::String (sel.empty() ? -1 : sel.front()) + ", shown " + juce::String (shownCount()) + " (before \"+\": " + juce::String (before) + ")");
        check ("Del: FL keeps the insert count (a blank one at the end)", (int) offline.getSong().mixer.size() == countBefore);
        check ("Del: the blank insert under the deleted number stays hidden (it looked as if Del did nothing)", strips.isHidden (added));
        check ("Del: as many inserts show as before \"+\"", shownCount() == before);
        check ("Del: the selection goes to the shown insert before it (18)", sel == std::vector<int> { 18 });
        write (mixer, mixer.getLocalBounds().withHeight (juce::jmin (mixer.getHeight(), 700)), "mixer-after-del");

        // two empty inserts added, the first deleted: the second (now under the first's number) stays shown and selected
        offline.editSong ([] (Song& s) { for (int i = 0; i < 2; ++i) { auto& t = s.mixer.emplace_back(); t.name = "Insert " + std::to_string (s.mixer.size() - 1); } });
        const auto a = (int) offline.getSong().mixer.size() - 2, b = a + 1;
        strips.revealForTest (b);
        strips.revealForTest (a);
        ui.runActionForSnapshot ("mixer.deleteTracks");
        offline.dispatchPendingMessages();
        strips.layoutForTest();
        check ("Del on the first of two added inserts: the second moves into its place and shows, selected",
               ! strips.isHidden (a) && strips.isHidden (b) && strips.getSelection() == std::vector<int> { a });

        // "Show only used inserts" off: FL's selection (the same number)
        MixerStrips::showUsedOnly = false;
        strips.layoutForTest();
        strips.selectTrack (9);
        ui.runActionForSnapshot ("mixer.deleteTracks");
        offline.dispatchPendingMessages();
        check ("all inserts shown: Del keeps the number selected (FL)", strips.getSelection() == std::vector<int> { 9 });
        MixerStrips::showUsedOnly = true;

        // the remembered choice: only Yes can be remembered; Settings › General › Manage messages brings the question back
        check ("Manage messages lists \"Delete mixer track\", unticked while remembered", [] {
            const auto menu = messages::manageMenu();
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                if (it.getItem().text == tr ("Delete mixer track"))
                    return ! it.getItem().isTicked;
            return false;
        }());
        messages::setShown ("DeleteMixerTrack", true);
        check ("ticked again (or Show all): the question is asked again", messages::shown ("DeleteMixerTrack"));

        // FL's menu: "Delete  Del" in the Layout group, after Move right, before Insert one (fl-mixer-track-menu.png)
        {
            const auto menu = strips.buildMenu (18);
            juce::StringArray items;
            std::function<void (const juce::PopupMenu&)> walk = [&] (const juce::PopupMenu& m) {
                for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
                    if (! it.getItem().isSeparator && ! it.getItem().isSectionHeader)
                        items.add (it.getItem().text);
            };
            walk (menu);
            const auto del = items.indexOf (tr ("Delete") + "\tDel");
            check ("the track menu has FL's \"Delete  Del\"", del >= 0);
            check ("FL's order: Move right, Delete, Insert one", del > 0 && items[del - 1].startsWith (tr ("Move right")) && items[del + 1] == tr ("Insert one"));
        }

        // Settings › General: Manage messages and the note under it
        {
            auto page = makeGeneralPage (offline, {}, false);
            page->setSize (680, 3600);
            juce::Component* button = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
                if (auto* b = dynamic_cast<juce::Button*> (&c); b != nullptr && b->getButtonText() == tr ("Manage messages"))
                    button = b;
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (*page);
            check ("Settings › General has Manage messages", button != nullptr);
            if (button != nullptr)
            {
                const auto area = page->getLocalArea (button, button->getLocalBounds()).withX (0).withWidth (page->getWidth()).expanded (0, 40);
                write (*page, area.withHeight (area.getHeight() + 30), "settings-manage-messages");
            }
        }

        messages::setHiddenList (keptHidden);
        MixerStrips::showUsedOnly = keptUsedOnly;
        juce::File (prefix + "-checks.txt").replaceWithText (checks + "result=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

    // --details190mpshot <prefix> <project.flp> (round 190mp, details pass 7): the tempo menu's Half speed, the Playlist
    // track menu's Auto name clips, the picker's Alt+right-hold preview, the editors' tool gestures (right-hold + wheel,
    // middle + right menu, the Zoom icon's quick-zoom menu), the Piano roll's Magic lasso, the rack's Restretch all and
    // the Mixer's FX slot menu in FL's order. <prefix>-checks.txt lists the checks. Included in the app class of Main.cpp.
    bool runDetailsShot190mp (const juce::String& prefix, const juce::String& project)
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
        const auto zoomShot = [&prefix] (juce::Component& c, juce::Rectangle<int> r, float k, const juce::String& name) {
            savePng (c.createComponentSnapshot (r.getIntersection (c.getLocalBounds()), true, k), prefix + "-" + name + ".png");
        };
        const auto texts = [] (const juce::PopupMenu& menu, bool recurse = false) {
            juce::StringArray items;
            for (juce::PopupMenu::MenuItemIterator it (menu, recurse); it.next();)
                items.add (it.getItem().isSectionHeader ? "[" + it.getItem().text + "]" : it.getItem().isSeparator ? juce::String ("-") : it.getItem().text);
            return items;
        };
        const auto find = [] (const juce::PopupMenu& menu, const juce::String& text) -> const juce::PopupMenu::Item* {
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                if (it.getItem().text.upToFirstOccurrenceOf ("\t", false, false) == text)
                    return &it.getItem();
            return nullptr;
        };
        const auto mouseOn = [] (juce::Component& target, juce::Point<int> p, juce::ModifierKeys mods) {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, p.toFloat(), now, 1, false);
        };
        using namespace lynx;

        if (project.isEmpty())
            return false;
        AudioEngine offline (false);
        MainComponent ui (offline, false);
        ui.setSize (1920, 1080);
        ui.openFile (juce::File (project));
        offline.dispatchPendingMessages();

        // ---- the tempo menu's Half speed (DF-16)
        {
            const auto menu = ui.tempoMenuForSnapshot();
            const auto items = texts (menu);
            const auto at160 = items.indexOf ("160 BPM"), atHalf = items.indexOf (tr ("Half speed"));
            line ("tempo menu: " + items.joinIntoString (" | "));
            check ("tempo menu: Half speed after 160 BPM, behind a line", at160 >= 0 && atHalf == at160 + 2 && items[at160 + 1] == "-");
            const auto tpsBefore = offline.getTransport().ticksPerSample();
            const auto shownBefore = offline.shownTempo();
            if (const auto* item = find (menu, tr ("Half speed")); item != nullptr && item->action)
                item->action();
            check ("Half speed on: the transport moves at half the rate, the tempo shown stays",
                   offline.isHalfSpeed() && std::abs (offline.getTransport().ticksPerSample() * 2.0 - tpsBefore) < 1.0e-9 && offline.shownTempo() == shownBefore
                       && offline.getSong().bpm == shownBefore);
            const auto ticked = ui.tempoMenuForSnapshot();
            const auto* on = find (ticked, tr ("Half speed"));
            check ("the menu ticks Half speed while it is on", on != nullptr && on->isTicked);
            saveMenus (juce::StringArray { tr ("Tempo") }, { ticked }, prefix + "-menu-tempo-halfspeed.png");
            offline.setHalfSpeed (false);
            check ("Half speed off: the rate as before", std::abs (offline.getTransport().ticksPerSample() - tpsBefore) < 1.0e-12);
        }

        // ---- the Playlist track menu's Auto name clips (DE-127)
        {
            auto& pl = ui.getPlaylistForSnapshot();
            const auto& song = offline.getSong();
            int track = -1, channel = -1;
            for (const auto& c : song.playlist)
                if (c.kind == ClipKind::audio && c.channel >= 0 && c.track >= 0 && c.track < (int) song.playlistTracks.size()
                    && ! song.playlistTracks[(std::size_t) c.track].name.empty()
                    && song.playlistTracks[(std::size_t) c.track].name != song.channels[(std::size_t) c.channel].name)
                {
                    track = c.track;
                    channel = c.channel;
                    break;
                }
            const auto menu = pl.trackMenuForSnapshot (juce::jmax (0, track));
            const auto items = texts (menu);
            const auto autoName = items.indexOf (theme::mnemonic (tr ("Auto name"), 'A')), clips = items.indexOf (tr ("Auto name clips"));
            line ("track menu: " + items.joinIntoString (" | ").substring (0, 260));
            check ("track menu: Auto name clips right after Auto name, before Reset", autoName >= 0 && clips == autoName + 1 && items[clips + 1] == tr ("Reset"));
            saveMenus (juce::StringArray { tr ("Playlist track") }, { menu }, prefix + "-menu-track.png");
            if (track >= 0)
            {
                const auto trackName = song.playlistTracks[(std::size_t) track].name;
                const auto before = song.channels[(std::size_t) channel].name;
                line ("track " + juce::String (track + 1) + " \"" + juce::String::fromUTF8 (trackName.c_str()) + "\": audio clip channel \""
                      + juce::String::fromUTF8 (before.c_str()) + "\"");
                pl.autoNameClipsForSnapshot (track);
                offline.dispatchPendingMessages();
                const auto after = offline.getSong().channels[(std::size_t) channel].name;
                line ("after Auto name clips: \"" + juce::String::fromUTF8 (after.c_str()) + "\"");
                check ("Auto name clips: the audio clip's channel takes the track's name (FL: Main Vox -> Thief Vox)",
                       after == trackName || after.rfind (trackName + " #", 0) == 0);
                offline.undo();
                offline.dispatchPendingMessages();
                check ("one undo step puts the name back", offline.getSong().channels[(std::size_t) channel].name == before);
            }
            else
                line ("no named track with an audio clip of another name");
        }

        // ---- the picker's Alt+right-hold preview (DE-134)
        {
            auto& picker = ui.getPickerForSnapshot();
            ui.showPanelsForSnapshot ({ workspace::Panel::playlist });
            offline.dispatchPendingMessages();
            picker.setView (0);
            const auto pattern = offline.getSong().patterns.size() > 1 ? 1 : 0;
            const auto y = picker.rowCentreForSnapshot (pattern);
            const auto modeBefore = offline.getSong().mode;
            line ("picker row of pattern " + juce::String (pattern + 1) + " at y " + juce::String (y) + ", picker " + picker.getBounds().toString());
            if (y > 0)
            {
                const auto mods = juce::ModifierKeys (juce::ModifierKeys::altModifier | juce::ModifierKeys::rightButtonModifier);
                picker.mouseDown (mouseOn (picker, { 30, y }, mods));
                offline.dispatchPendingMessages();
                const auto playingHeld = offline.getTransport().playing.load();
                check ("Alt+right-hold: the pattern plays in pattern mode while held", playingHeld && offline.getSong().mode == PlayMode::pattern
                                                                                          && offline.getSong().currentPattern == pattern);
                picker.mouseUp (mouseOn (picker, { 30, y }, juce::ModifierKeys (juce::ModifierKeys::altModifier)));
                offline.dispatchPendingMessages();
                check ("the release stops it and puts the mode back", ! offline.getTransport().playing.load() && offline.getSong().mode == modeBefore);
            }
        }

        // ---- the Piano roll's tool gestures (DE-61, DE-62, DE-74) and Magic lasso (DE-71)
        {
            auto& pr = ui.getPianoRollForSnapshot();
            ui.showPanelsForSnapshot ({ workspace::Panel::pianoRoll });
            offline.dispatchPendingMessages();
            // a channel with notes in a pattern
            for (int p = 0; p < (int) offline.getSong().patterns.size(); ++p)
                if (! offline.getSong().patterns[(std::size_t) p].notes.empty())
                {
                    const auto ch = offline.getSong().patterns[(std::size_t) p].notes.front().channel;
                    offline.editSong ([p] (Song& s) { s.currentPattern = p; }, false);
                    offline.setSelectedChannel (ch);
                    break;
                }
            offline.dispatchPendingMessages();
            pr.runAction ("pianoRoll.zoomAll"); // (the pattern's notes in view)
            pr.scrollToNotes();
            offline.dispatchPendingMessages();
            line ("piano roll " + pr.getBounds().toString());
            pr.setToolForSnapshot (EditTool::pencil);
            const auto& strip = pr.toolsForSnapshot();
            const auto list = strip.toolList();
            juce::MouseWheelDetails down {}, up {};
            down.deltaY = -0.5f;
            up.deltaY = 0.5f;
            const auto at = juce::Point<int> (pr.getWidth() / 2, pr.getHeight() / 2);
            const auto right = juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier);
            pr.mouseWheelMove (mouseOn (pr, at, right), down);
            const auto first = strip.getTool();
            pr.mouseWheelMove (mouseOn (pr, at, right), down);
            const auto second = strip.getTool();
            pr.mouseWheelMove (mouseOn (pr, at, right), up);
            pr.mouseWheelMove (mouseOn (pr, at, right), up);
            pr.mouseWheelMove (mouseOn (pr, at, right), up);
            line ("right-hold + wheel: " + juce::String (tr (editToolInfo (first).name)) + ", " + tr (editToolInfo (second).name) + ", back to "
                  + tr (editToolInfo (strip.getTool()).name));
            check ("right-hold + wheel down: the next tools to the right (Draw -> Paint -> the next)", list.size() > 2 && first == list[1] && second == list[2]);
            check ("wheel up goes back and stops at the first tool (no wrap)", strip.getTool() == list[0]);

            const auto mode = toolModeMenu (strip, nullptr, pr.mainMenu != nullptr ? pr.mainMenu() : juce::PopupMenu());
            const auto modeItems = texts (mode);
            line ("middle + right menu: " + modeItems.joinIntoString (" | ").substring (0, 300));
            const auto* draw = find (mode, tr (editToolInfo (list[0]).name));
            check ("middle + right: a Mode column of the tools (the one in use ticked), then the main menu",
                   modeItems[0] == "[" + tr ("Mode") + "]" && draw != nullptr && draw->isTicked && modeItems.size() > (int) list.size() + 3);
            saveMenus (juce::StringArray { tr ("Mode") }, { mode }, prefix + "-menu-pr-mode.png");

            const auto zoom = quickZoomMenu ("pianoRoll.", nullptr, PianoRollComponent::shortcutText, false);
            const auto zoomItems = texts (zoom);
            line ("Zoom icon menu (Piano roll): " + zoomItems.joinIntoString (" | "));
            const auto* stepZoom = find (zoom, tr ("Step sequencer mode zoom"));
            check ("Zoom icon menu: FL's 8 items in FL's order (Step sequencer mode zoom greyed)",
                   zoomItems.size() == 11 && zoomItems[0].startsWith (tr ("Zoom in")) && zoomItems[3].startsWith (tr ("Quick zoom 1"))
                       && zoomItems[7].startsWith (tr ("Zoom out far")) && stepZoom != nullptr && ! stepZoom->isEnabled);
            saveMenus (juce::StringArray { tr ("Zoom") }, { zoom }, prefix + "-menu-pr-zoom.png");
            const auto plZoom = quickZoomMenu ("playlist.", nullptr, PlaylistComponent::shortcutText, false);
            check ("Zoom icon menu (Playlist): Zoom on performance clips last", texts (plZoom)[10].startsWith (tr ("Zoom on performance clips")));
            saveMenus (juce::StringArray { tr ("Zoom") }, { plZoom }, prefix + "-menu-pl-zoom.png");

            // Magic lasso: the Select submenu's last item; on, a loop drawn around a note (no button) selects it
            const auto select = pr.selectMenuForSnapshot();
            const auto selectItems = texts (select);
            check ("Select submenu: Magic lasso last, after a line, off", selectItems.size() > 2 && selectItems[selectItems.size() - 1] == tr ("Magic lasso")
                                                                              && selectItems[selectItems.size() - 2] == "-" && ! EditorGlobals::get().pianoRollMagicLasso);
            EditorGlobals::get().pianoRollMagicLasso = true;
            pr.setSelectionForSnapshot ({});
            // (FL's rule: with nothing selected a loop has to hold two notes or more)
            const auto target = pr.firstVisibleNoteForSnapshot();
            const auto other = pr.firstVisibleNoteForSnapshot (target.index, target.bounds.getCentre());
            line ("lasso notes " + juce::String (target.index) + " at " + target.bounds.toString() + ", " + juce::String (other.index) + " at " + other.bounds.toString());
            check ("two notes of the channel on screen for the lasso", target.index >= 0 && other.index >= 0);
            if (target.index >= 0 && other.index >= 0)
            {
                const auto both = target.bounds.getUnion (other.bounds);
                const auto c = both.getCentre();
                const auto rx = 40.0 + 0.75 * both.getWidth(), ry = 30.0 + 0.75 * both.getHeight();
                for (int k = 0; k <= 46; ++k) // a loop around them, then a little past the start so the trail closes
                {
                    const auto a = juce::MathConstants<double>::twoPi * k / 40.0;
                    pr.mouseMove (mouseOn (pr, { c.x + juce::roundToInt (rx * std::cos (a)), c.y + juce::roundToInt (ry * std::sin (a)) }, {}));
                }
                const auto chosen = pr.selectionForTests();
                line ("selected after the loop: " + juce::String ((int) chosen.size()) + " note(s)");
                check ("Magic lasso: a loop around two notes selects them", std::find (chosen.begin(), chosen.end(), target.index) != chosen.end()
                                                                            && std::find (chosen.begin(), chosen.end(), other.index) != chosen.end());
                zoomShot (pr, both.expanded (juce::roundToInt (rx * 0.6) + 20, juce::roundToInt (ry * 0.6) + 20), 1.5f, "pr-magic-lasso@150");
            }
            EditorGlobals::get().pianoRollMagicLasso = false;
        }

        // ---- the rack's Restretch all (DE-38)
        {
            const auto menu = ui.rackMenuForSnapshot();
            const auto items = texts (menu);
            int unzip = -1;
            for (int i = 0; i < items.size() && unzip < 0; ++i)
                if (items[i].startsWith (tr ("Unzip all")))
                    unzip = i;
            const auto* restretch = find (menu, tr ("Restretch all"));
            line ("rack menu around it: " + items.joinIntoString (" | ").fromFirstOccurrenceOf (tr ("Zip selected"), true, false).substring (0, 200));
            check ("rack menu: Restretch all Ctrl+Alt+R after Unzip all, behind a line", restretch != nullptr && unzip >= 0 && items[unzip + 1] == "-"
                                                                                           && items[unzip + 2].startsWith (tr ("Restretch all")));
            saveMenus (juce::StringArray { tr ("Channel Rack") }, { menu }, prefix + "-menu-rack.png");
            const auto count = restretchableChannels (offline.getSong());
            line ("channels stretched ahead of time: " + juce::String (count));
            check ("Restretch all is greyed exactly when no channel is stretched ahead of time", restretch != nullptr && restretch->isEnabled == (count > 0));
            const auto bpm = offline.getSong().bpm;
            const auto done = offline.restretchAll();
            offline.dispatchPendingMessages();
            check ("Restretch all runs on those channels and changes nothing in the song", done == count && offline.getSong().bpm == bpm);
        }

        // ---- the Mixer's FX slot menu in FL's order (DF-77)
        {
            auto& mixer = ui.getMixerForSnapshot();
            ui.showPanelsForSnapshot ({ workspace::Panel::mixer });
            mixer.selectFirstTrackWithEffects();
            offline.dispatchPendingMessages();
            const auto t = mixer.getSelectedTrack();
            int slot = -1;
            if (t >= 0 && t < (int) offline.getSong().mixer.size())
                slot = offline.getSong().mixer[(std::size_t) t].effects.empty() ? -1 : 0;
            line ("slot menu of insert " + juce::String (t) + " slot " + juce::String (slot + 1));
            if (slot >= 0)
            {
                const auto menu = mixer.slotMenuForSnapshot (slot);
                const auto items = texts (menu);
                line ("slot menu: " + items.joinIntoString (" | ").substring (0, 600));
                const juce::StringArray fl { tr ("Replace"), tr ("Delete..."), tr ("Patcherize"), tr ("Patcherize all"), "[" + tr ("Presets") + "]", tr ("Presets"),
                                             tr ("Browse presets"), tr ("Save preset as..."), tr ("Add to plugin database (flag as favorite)"),
                                             tr ("Display preset name in preset selector"), "[" + tr ("Spare state") + "]", tr ("Store in spare state"),
                                             tr ("Flip with spare state"), "[" + tr ("Parameters") + "]", tr ("Link all parameters..."), tr ("Browse parameters"),
                                             "[" + tr ("CPU") + "]", tr ("Allow threaded processing"), tr ("Smart disable"), tr ("Move up"), tr ("Move down"),
                                             tr ("View editor"), tr ("Rename, color and icon..."), tr ("Change color..."), tr ("Random color"), tr ("Change icon..."),
                                             tr ("Scaling"), tr ("Help") };
                juce::StringArray got;
                for (const auto& i : items)
                    if (i != "-")
                        got.add (i.upToFirstOccurrenceOf ("\t", false, false));
                bool inOrder = got.size() >= fl.size();
                for (int i = 0; i < fl.size() && inOrder; ++i)
                    inOrder = got[i] == fl[i];
                check ("FX slot menu: FL's 28 items in FL's order first, LYNX's own after", inOrder);
                saveMenus (juce::StringArray { "FX slot " + juce::String (slot + 1) }, { menu }, prefix + "-menu-mixer-slot.png");
            }
        }

        juce::File (prefix + "-checks.txt").replaceWithText (checks + "result=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

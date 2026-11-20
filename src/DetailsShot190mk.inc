    // --details190mkshot <prefix> <project.flp> (round 190mk, details pass 6): Live's insert marker (Clip-launch, Live's
    // look), the Channel rack's zipped channels, Fruity Multiband's peak display with its SPEED slider, the rack's
    // Options menu items. <prefix>-checks.txt lists the checks. Included in the app class of Main.cpp.
    bool runDetailsShot190mk (const juce::String& prefix, const juce::String& project)
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
        const auto write = [&prefix] (juce::Component& c, const juce::String& name) { savePng (lynx::shot::snapshot (c), prefix + "-" + name + ".png"); };
        const auto zoomShot = [&prefix] (juce::Component& c, juce::Rectangle<int> r, float k, const juce::String& name) {
            savePng (c.createComponentSnapshot (r.getIntersection (c.getLocalBounds()), true, k), prefix + "-" + name + ".png");
        };
        using namespace lynx;

        if (project.isNotEmpty())
        {
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (1920, 1080);
            ui.openFile (juce::File (project));
            offline.dispatchPendingMessages();

            // ---- the Channel rack's zipped channels (DE-38): FL's capture zipped two neighbouring channels
            {
                auto& rack = ui.getRackForSnapshot();
                ui.showPanelsForSnapshot ({ workspace::Panel::channelRack });
                rack.updateSize();
                offline.dispatchPendingMessages();
                const auto menu = ui.rackMenuForSnapshot();
                juce::StringArray items;
                for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
                    if (! it.getItem().text.isEmpty())
                        items.add (it.getItem().text);
                check ("rack menu: Zip selected / Unzip all", items.joinIntoString ("|").contains (tr ("Zip selected")) && items.joinIntoString ("|").contains (tr ("Unzip all")));
                const auto before = rack.getHeight();
                write (rack, "rack-before-zip");
                offline.setChannelSelection ({ 1, 2 }, 1);
                rack.zipSelected();
                offline.dispatchPendingMessages();
                rack.updateSize();
                juce::Rectangle<int> strip;
                const auto zipped = rack.zipStripForSnapshot (1, strip) && offline.getSong().channels[1].zipped && offline.getSong().channels[2].zipped;
                line ("rack height " + juce::String (before) + " -> " + juce::String (rack.getHeight()) + ", strip " + strip.toString());
                check ("Zip selected: the two channels in one half-row strip", zipped && strip.getHeight() == ChannelRackComponent::rowHeight / 2
                                                                                  && rack.getHeight() == before - (2 * ChannelRackComponent::rowHeight - ChannelRackComponent::rowHeight / 2));
                write (rack, "rack-zipped");
                zoomShot (rack, juce::Rectangle<int> (0, 0, 560, strip.getBottom() + 3 * ChannelRackComponent::rowHeight), 2.0f, "rack-zipped@2x");
                zoomShot (rack, juce::Rectangle<int> (0, 0, rack.getWidth(), strip.getBottom() + 3 * ChannelRackComponent::rowHeight), 1.5f, "rack-zipped@150");
                rack.unzip (2);
                offline.dispatchPendingMessages();
                check ("a right-click on a bar unzips that channel", ! offline.getSong().channels[2].zipped && offline.getSong().channels[1].zipped);
                rack.unzipAll();
                offline.dispatchPendingMessages();
                rack.updateSize();
                check ("Unzip all: the rack as before", rack.getHeight() == before && ! offline.getSong().channels[1].zipped);

                // Channel button width (DE-39): FL's 400 % (80 -> 332 px there; 150 -> 622 px here), then back
                rack.setButtonWidth (400);
                offline.dispatchPendingMessages();
                line ("button width at 400 %: " + juce::String (ChannelRackComponent::nameWidth) + " px");
                check ("Channel button width 400 %: FL's proportion (x 4.15)", ChannelRackComponent::nameWidth == ChannelRackComponent::widthForPercent (400)
                                                                               && std::abs (ChannelRackComponent::nameWidth / 150.0 - 332.0 / 80.0) < 0.01);
                zoomShot (rack, juce::Rectangle<int> (0, 0, rack.getWidth(), juce::jmin (rack.getHeight(), 8 * ChannelRackComponent::rowHeight)), 1.0f, "rack-button-width-400");
                rack.setButtonWidth (0);
                line ("button width, size to fit: " + juce::String (ChannelRackComponent::nameWidth) + " px");
                rack.setButtonWidth (100);
                check ("Channel button width 100 %: LYNX's 150 px", ChannelRackComponent::nameWidth == 150);

                // the piano roll preview's mute button (DE-33): a muted channel with a preview keeps the crossed icon
                int previewRow = -1;
                for (int p = 0; p < (int) offline.getSong().patterns.size() && previewRow < 0; ++p)
                {
                    for (int c = 0; c < (int) offline.getSong().channels.size() && previewRow < 0; ++c)
                        if (showsNotePreview (offline.getSong().patterns[(std::size_t) p], c))
                            previewRow = c;
                    if (previewRow >= 0)
                        offline.editSong ([p] (Song& s) { s.currentPattern = p; }, false);
                }
                offline.dispatchPendingMessages();
                rack.updateSize();
                line ("preview row " + juce::String (previewRow));
                if (previewRow >= 0)
                {
                    offline.editSong ([previewRow] (Song& s) { s.channels[(std::size_t) previewRow].muted = true; });
                    offline.dispatchPendingMessages();
                    rack.updateSize();
                    const auto rowTop = rack.rowTopForSnapshot (previewRow);
                    zoomShot (rack, juce::Rectangle<int> (0, rowTop - 2, rack.getWidth(), ChannelRackComponent::rowHeight + 4), 2.0f, "rack-preview-muted@2x");
                    offline.editSong ([previewRow] (Song& s) { s.channels[(std::size_t) previewRow].muted = false; });
                }
                check ("Options menu: Show a mute button on the piano roll preview, Channel button width",
                       items.joinIntoString ("|").contains (tr ("Show a mute button on the piano roll preview")) && items.joinIntoString ("|").contains (tr ("Channel button width")));
            }

            // ---- an automation clip's Articulator tools (DE-115)
            {
                const auto& song = offline.getSong();
                int clipIndex = -1;
                for (int i = 0; i < (int) song.playlist.size() && clipIndex < 0; ++i)
                    if (song.playlist[(std::size_t) i].kind == ClipKind::automation && song.playlist[(std::size_t) i].automation >= 0)
                        clipIndex = i;
                if (clipIndex >= 0)
                {
                    const auto clip = song.playlist[(std::size_t) clipIndex];
                    const auto menu = articulator::menu (offline, clip.automation, clip.length, ui);
                    juce::StringArray items;
                    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                        if (! it.getItem().text.isEmpty())
                            items.add (it.getItem().text);
                    line ("Articulator tools: " + items.joinIntoString (" | "));
                    check ("automation clip: FL's Articulator tools", items.size() == 12 && items.contains (tr ("Smooth up abrupt changes")) && items.contains (tr ("Analyze audio file...")));
                    saveMenus (juce::StringArray { tr ("Articulator tools") }, { menu }, prefix + "-menu-articulator.png");
                    const auto before = offline.getSong().automations[(std::size_t) clip.automation].points;
                    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                        if (it.getItem().text == tr ("Flip vertically") && it.getItem().action)
                            it.getItem().action();
                    const auto& after = offline.getSong().automations[(std::size_t) clip.automation].points;
                    check ("Articulator › Flip vertically flips the clip's points", ! before.empty() && after.size() == before.size()
                                                                                   && std::abs (after[0].value - (1.0f - before[0].value)) < 1.0e-6f);
                    offline.undo();
                }
                else
                    line ("no automation clip in the project");
            }

            // ---- Live's insert marker (DL-70): Clip-launch layout, Live's look
            {
                ui.setLayoutStyle (layoutstyle::Style::clipLaunch);
                offline.dispatchPendingMessages();
                auto& pl = ui.getPlaylistForSnapshot();
                ui.showPanelsForSnapshot ({ workspace::Panel::playlist });
                offline.dispatchPendingMessages();
                juce::Rectangle<int> lineBox;
                juce::String hint;
                const auto tick = (std::int64_t) offline.getSong().barTicks() * 2 + (std::int64_t) ppq; // bar 3, beat 2
                const auto ok = pl.insertMarkForSnapshot (1, tick, lineBox, hint);
                line ("insert mark line " + lineBox.toString() + ", hint \"" + hint + "\"");
                check ("Live look: the insert marker, 1 px over the lane (17 px in Live's 19 px lane)", ok && lineBox.getWidth() == 1 && lineBox.getHeight() >= 15);
                check ("status bar text: Insert Mark 3.2.1 (Time: m:ss:ms)", hint.startsWith (tr ("Insert Mark {0} (Time: {1})", { "3.2.1", "" }).upToFirstOccurrenceOf ("(", false, false)));
                write (pl, "live-insert-marker");
                zoomShot (pl, lineBox.expanded (60, 24), 4.0f, "live-insert-marker@4x");
                zoomShot (pl, lineBox.expanded (200, 60), 1.5f, "live-insert-marker@150");
                ui.setLayoutStyle (layoutstyle::Style::pattern);
                offline.dispatchPendingMessages();
            }
        }
        {
            // ---- Gross Beat's mapping menu, the rest (DP-7): State / Points selection / Tools / Options / View
            AudioEngine offline (false);
            Song gb;
            gb.mixer.resize (2);
            gb.mixer[0].name = "Master";
            gb.mixer[1].name = "Insert 1";
            gb.mixer[1].effects.push_back (makeEffectSlot (EffectKind::grossBeat));
            offline.setSong (std::move (gb));
            offline.dispatchPendingMessages();
            auto editor = makeNativeEffectEditor (offline, 1, 0, 1.0f, 0);
            showGrossBeatEnvelopeForSnapshot (*editor, 0);
            grossBeatEditForSnapshot (*editor, 4); // (FL's Basic 4, as in the capture)
            juce::PopupMenu menu;
            grossBeatToolsForSnapshot (*editor, false, false, false, &menu);
            juce::StringArray items;
            for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
                if (! it.getItem().text.isEmpty())
                    items.add (it.getItem().text);
            line ("mapping menu: " + items.joinIntoString (" | "));
            check ("mapping menu: FL's Presets, state files, Prepare, point clipboard, Analyze, Options, View",
                   items.contains (tr ("Presets")) && items.contains (tr ("Open state file...")) && items.contains (tr ("Save state file..."))
                       && items.contains (tr ("Prepare for smooth editing")) && items.contains (tr ("Paste (insert)")) && items.contains (tr ("Paste (replace)"))
                       && items.contains (tr ("Duplicate")) && items.contains (tr ("Analyze audio file...")) && items.contains (tr ("Focus on click"))
                       && items.contains (tr ("Background gradient")) && items.contains (tr ("Invert grid")));
            saveMenus (juce::StringArray { "Gross Beat: mapping options" }, { menu }, prefix + "-menu-grossbeat-mapping.png");
            write (*editor, "plug-grossbeat-before");
            grossBeatEditForSnapshot (*editor, 2);
            write (*editor, "plug-grossbeat-paste-insert");
            grossBeatEditForSnapshot (*editor, 4);
            grossBeatEditForSnapshot (*editor, 1);
            write (*editor, "plug-grossbeat-prepare-smooth");
            grossBeatEditForSnapshot (*editor, 3);
            write (*editor, "plug-grossbeat-gradient-grid");
            grossBeatEditForSnapshot (*editor, 0);
            auto big = makeNativeEffectEditor (offline, 1, 0, 1.5f, 0);
            showGrossBeatEnvelopeForSnapshot (*big, 0);
            write (*big, "plug-grossbeat-prepare-smooth@150");
        }
        {
            // ---- Fruity Multiband Compressor's peak display and SPEED (DP-30)
            AudioEngine offline (false);
            Song song;
            song.mixer.resize (2);
            song.mixer[0].name = "Master";
            song.mixer[1].name = "Insert 1";
            song.mixer[1].effects.push_back (makeEffectSlot (EffectKind::fruityMultiband));
            offline.setSong (std::move (song));
            offline.dispatchPendingMessages();
            auto editor = makeNativeEffectEditor (offline, 1, 0, 1.0f, 0);
            multibandMetersForSnapshot (*editor);
            check ("Multiband: the scrolling peak display at FL's default SPEED", multibandHistoryForSnapshot (*editor, 0.5f));
            write (*editor, "plug-multiband-peaks");
            check ("Multiband: SPEED 0 shows the crossover graph only", ! multibandHistoryForSnapshot (*editor, 0.0f));
            write (*editor, "plug-multiband-speed0");
            auto big = makeNativeEffectEditor (offline, 1, 0, 1.5f, 0);
            multibandMetersForSnapshot (*big);
            multibandHistoryForSnapshot (*big, 0.5f);
            write (*big, "plug-multiband-peaks@150");
        }
        juce::File (prefix + "-checks.txt").replaceWithText (checks + (allOk ? "result=OK\n" : "result=FAIL\n"));
        return allOk;
    }

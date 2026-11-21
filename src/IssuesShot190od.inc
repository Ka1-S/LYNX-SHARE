    // --issuesshot190od <prefix> (round 190od, issues #236-#240 and #242), offscreen through the real components: the Piano
    // roll's scale menu opens again after it closed (#242), Settings › Theme's chosen dot ring and the chosen preset's
    // frame whole (#237), the Tracks dot (#238), automatic colours and icons by name (#239), a new project from a user
    // template starts its own clock (#240), the slide-note law in the project file (#236). <prefix>-checks.txt lists the
    // checks; pictures @100 and @150. Included in the app class of Main.cpp.
    bool runIssuesShot190od (const juce::String& prefix)
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
        const auto pump = [] {
#if JUCE_LINUX || JUCE_WINDOWS // (rollup 0.1.31 step 4: macOS has no such JUCE function; the route runs on Linux / Windows)
            for (int i = 0; i < 256 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i) {}
#endif
        };
        const auto findId = [] (juce::Component& root, const juce::String& id) -> juce::Component* {
            std::function<juce::Component* (juce::Component&)> find = [&] (juce::Component& c) -> juce::Component* {
                if (c.getComponentID() == id)
                    return &c;
                for (auto* child : c.getChildren())
                    if (auto* f = find (*child))
                        return f;
                return nullptr;
            };
            return find (root);
        };
        // how many pixels of `image` in column x / row y are near `colour`
        const auto near = [] (juce::Colour a, juce::Colour b) {
            return std::abs (a.getRed() - b.getRed()) < 24 && std::abs (a.getGreen() - b.getGreen()) < 24 && std::abs (a.getBlue() - b.getBlue()) < 24;
        };
        using namespace lynx;

        // ================= #242: the scale highlighting menu opens again =================
        {
            AudioEngine engine (false);
            engine.setSong (Song::makeDefault());
            PianoRollComponent roll (engine);
            roll.setBounds (0, 0, 1180, 420);
            roll.addToDesktop (0);
            roll.setVisible (true);
            pump();
            auto& box = roll.scaleBoxForTests();
            const auto open = [&box, &pump] {
                box.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); // (ComboBox: Return = a click, showPopupIfNotActive)
                pump();
                return box.isPopupActive() && juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0;
            };
            const auto close = [&box, &pump] {
                juce::PopupMenu::dismissAllActiveMenus();
                pump();
                return ! box.isPopupActive() && juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0;
            };
            check ("#242 the scale menu opens", open());
            check ("#242 closed without a choice, the box is no longer 'active'", close());
            check ("#242 it opens a second time", open());
            check ("#242 and closes again", close());
            check ("#242 and a third time", open());
            close();
            // rollup 0.1.31 step 4: chord2_190nu's stamp box is a ScaleBox too (its click opens the Stamp menu); the same fix
            // lets it open again
            {
                auto& stamp = roll.chordBoxForTests();
                const auto openStamp = [&stamp, &pump] {
                    stamp.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                    pump();
                    return stamp.isPopupActive() && juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0;
                };
                const auto closeStamp = [&stamp, &pump] {
                    juce::PopupMenu::dismissAllActiveMenus();
                    pump();
                    return ! stamp.isPopupActive() && juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0;
                };
                check ("#242 the chord stamp menu opens (190nu's stamp box)", openStamp());
                check ("#242 the stamp box is no longer 'active' after it closed", closeStamp());
                check ("#242 the stamp menu opens a second time", openStamp());
                check ("#242 and closes again", closeStamp());
                check ("#242 and a third time", openStamp());
                closeStamp();
                check ("#242 the scale menu still opens after the stamp menu", open());
                close();
            }
            // FL-exact (FL manual: "You must first select a Scale ... before Root notes are available"; REA FUN_00eadb90:
            // the root items are enabled only when a reference scale is chosen)
            roll.setScaleForSnapshot (0, 0);
            const auto rootsEnabled = [&roll] {
                int enabled = 0;
                const auto menu = roll.getScaleHighlightMenuForSnapshot();
                for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                    if (it.getItem().text == "C" || it.getItem().text == "D")
                        enabled += it.getItem().isEnabled ? 1 : 0;
                return enabled;
            };
            check ("#242 no scale: the roots wait for one (FL)", rootsEnabled() == 0);
            saveMenus (juce::StringArray { tr ("Scale highlighting") }, std::vector<juce::PopupMenu> { roll.getScaleHighlightMenuForSnapshot() },
                       prefix + "-242-scale-menu-no-scale.png");
            roll.setScaleForSnapshot (0, 1);
            check ("#242 a scale chosen: the roots are available", rootsEnabled() == 2);
            saveMenus (juce::StringArray { tr ("Scale highlighting") }, std::vector<juce::PopupMenu> { roll.getScaleHighlightMenuForSnapshot() },
                       prefix + "-242-scale-menu-major.png");
            roll.removeFromDesktop();
        }

        // ================= #237 / #238: Settings › Theme =================
        {
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            auto panel = makeSettingsPanel (offline, SettingsPage::theme);
            panel->setSize (1180, 1400);
            pump();
            auto* meters = dynamic_cast<juce::Button*> (findId (*panel, "theme.dot.meters"));
            auto* tracksDot = dynamic_cast<juce::Button*> (findId (*panel, "theme.dot.tracks"));
            auto* grid = findId (*panel, "theme.presets");
            check ("#238 the Tracks dot is on the page", tracksDot != nullptr && tracksDot->getButtonText() == tr ("Tracks"));
            if (meters != nullptr && meters->onClick)
                meters->onClick(); // (selects the dot: its ring)
            if (meters != nullptr)
            {
                const auto image = meters->createComponentSnapshot (meters->getLocalBounds(), true, 1.0f);
                int top = 0, ring = 0;
                for (int x = 0; x < image.getWidth(); ++x)
                {
                    top += near (image.getPixelAt (x, 0), theme::colour::accent) ? 1 : 0;
                    ring += near (image.getPixelAt (x, 2), theme::colour::accent) || near (image.getPixelAt (x, 3), theme::colour::accent) ? 1 : 0;
                }
                line ("#237 chosen dot: ring pixels in the top row " + juce::String (top) + ", rows 2-3 " + juce::String (ring));
                check ("#237 the chosen dot's ring is whole (nothing on the top edge, its top inside)", top == 0 && ring > 0);
            }
            if (grid != nullptr)
            {
                const auto image = grid->createComponentSnapshot (grid->getLocalBounds(), true, 1.0f);
                int left = 0, right = 0;
                for (int y = 0; y < image.getHeight(); ++y)
                {
                    left += near (image.getPixelAt (0, y), theme::colour::accent) ? 1 : 0;
                    for (int x = image.getWidth() - 6; x < image.getWidth(); ++x)
                        right += near (image.getPixelAt (x, y), theme::colour::borderStrong) || near (image.getPixelAt (x, y), theme::colour::accent) ? 1 : 0;
                }
                line ("#237 preset grid " + juce::String (image.getWidth()) + " px: accent frame pixels in its left column " + juce::String (left)
                      + ", frame pixels in its last six columns " + juce::String (right));
                check ("#237 the chosen preset's frame is whole on the left (LYNX, first column)", left > 40);
                check ("#237 the last column's frame is inside too", right > 40);
                write (*panel, grid->getBoundsInParent().getUnion (meters != nullptr ? meters->getBoundsInParent() : juce::Rectangle<int>()).withX (0).withWidth (1180)
                                   .expanded (0, 12),
                       "237-theme-page");
            }
            write (*panel, panel->getLocalBounds(), "237-theme-whole");
            // #238: the Tracks dot recolours what has no colour of its own (the project keeps "no colour")
            const auto before = themeui::current();
            auto spec = before;
            spec.dots[(std::size_t) themes::Dot::tracks] = 0xffd08a2a;
            themeui::apply (spec, false);
            check ("#238 Tracks dot: tracks without a colour draw in it", theme::colour::defaultTrack == juce::Colour (0xffd08a2a));
            {
                auto song = Song::makeDefault();
                song.playlistTracks.resize (8);
                song.playlistTracks[1].colour = 0xff3a6fd8; // (a track with its own colour keeps it)
                song.playlistTracks[1].name = "Own colour";
                offline.setSong (song);
                MainComponent ui (offline, false);
                ui.setSize (1600, 900);
                ui.selectTabForSnapshot (2);
                pump();
                write (ui, { 0, 0, 900, 420 }, "238-playlist-tracks-dot");
                check ("#238 the project still says 'no colour'", offline.getSong().playlistTracks[0].colour == 0u);
            }
            // FL's Theme › Options › "Audio and automation clips use note colors": new clips take a note colour
            check ("#239 Theme > Options: FL's 'Audio and automation clips use note colors'",
                   dynamic_cast<juce::ToggleButton*> (findId (*panel, "theme.clipsNoteColours")) != nullptr);
            spec.clipsUseNoteColours = true;
            themeui::apply (spec, false);
            {
                const auto channels = offline.getSong().channels.size();
                offline.editSong ([] (Song& s) {
                    ChannelDef c;
                    c.instrument = InstrumentKind::audioClip;
                    c.name = "Vox take";
                    s.channels.push_back (c);
                    s.automations.push_back ({});
                });
                const auto colour = offline.getSong().channels.back().colour;
                bool isNote = false;
                for (int i = 0; i < 16; ++i)
                    isNote = isNote || colour == (theme::noteGroupColour (i, 1.0f).getARGB() | 0xff000000u);
                check ("#239 with it on, a new audio clip channel and automation clip take note colours",
                       offline.getSong().channels.size() == channels + 1 && isNote && offline.getSong().automations.back().colour != 0u);
            }
            if (auto* box = findId (*panel, "theme.clipsNoteColours"))
                write (*panel, box->getBoundsInParent().withX (0).withWidth (1180).expanded (0, 40), "239-theme-options");
            themeui::apply (before, false);
            check ("#238 back to FL's grey", theme::colour::defaultTrack == juce::Colour (0xff485156));
            {
                MainComponent ui (offline, false);
                ui.setSize (1600, 900);
                ui.selectTabForSnapshot (2);
                pump();
                write (ui, { 0, 0, 900, 420 }, "238-playlist-fl-grey");
            }
            auto again = makeSettingsPanel (offline, SettingsPage::theme);
            again->setSize (1180, 1400);
            if (auto* d = dynamic_cast<juce::Button*> (findId (*again, "theme.dot.tracks")); d != nullptr && d->onClick)
                d->onClick();
            pump();
            if (auto* d = findId (*again, "theme.dot.tracks"))
                write (*again, d->getBoundsInParent().withX (0).withWidth (1180).expanded (0, 60), "238-theme-tracks-dot");
        }

        // ================= #239: automatic colours and icons by name =================
        {
            const auto saved = autolook::current();
            autolook::Rules rules;
            rules.on = true;
            rules.list.push_back ({ "kick", autolook::Match::contains, 0xff3a6fd8, trackicons::flIndexOf (trackicons::Icon::kick), autolook::everything });
            rules.list.push_back ({ "REC", autolook::Match::startsWith, 0, trackicons::flIndexOf (trackicons::Icon::microphone), autolook::everything });
            rules.list.push_back ({ "Bass", autolook::Match::is, 0xffc04040, -1, autolook::channels });
            autolook::current() = rules;
            AudioEngine offline (false);
            {
                auto song = Song::makeDefault();
                song.playlistTracks.resize (8);
                offline.setSong (song);
            }
            offline.editSong ([] (Song& s) { s.playlistTracks[0].name = "Kick 1"; });
            const auto& s0 = offline.getSong().playlistTracks[0];
            check ("#239 a track renamed 'Kick 1' takes the rule's colour and icon",
                   s0.colour == 0xff3a6fd8 && s0.icon == trackicons::flIndexOf (trackicons::Icon::kick));
            offline.editSong ([] (Song& s) { s.mixer[3].name = "REC vox"; });
            check ("#239 a mixer track named 'REC vox' gets the microphone, its colour stays",
                   offline.getSong().mixer[3].icon == trackicons::flIndexOf (trackicons::Icon::microphone) && offline.getSong().mixer[3].colour == 0u);
            offline.editSong ([] (Song& s) { s.playlistTracks[0].colour = 0xff20a050; }); // (the user's own colour afterwards)
            offline.editSong ([] (Song& s) { s.playlistTracks.insert (s.playlistTracks.begin(), PlaylistTrack {}); });
            check ("#239 a track inserted above shifts it without renaming it: its own colour stays",
                   offline.getSong().playlistTracks[1].colour == 0xff20a050);
            offline.editSong ([] (Song& s) {
                if (s.channels.size() > 1)
                    s.channels[1].name = "Bass";
            });
            check ("#239 a channel rule ('Bass', channels only, Name is)", offline.getSong().channels.size() > 1 && offline.getSong().channels[1].colour == 0xffc04040);
            offline.editSong ([] (Song& s) { s.playlistTracks[4].name = "Bass"; });
            check ("#239 ... and not a Playlist track named Bass", offline.getSong().playlistTracks[4].colour == 0u);
            autolook::current().on = false;
            offline.editSong ([] (Song& s) { s.playlistTracks[5].name = "Kick 2"; });
            check ("#239 switched off: nothing happens", offline.getSong().playlistTracks[5].colour == 0u);
            int applied = 0;
            offline.editSong ([&applied] (Song& s) { applied = autolook::applyAll (s); });
            check ("#239 Apply to this project: the named items that match", applied >= 1 && offline.getSong().playlistTracks[5].colour == 0xff3a6fd8);
            const auto text = autolook::toText (autolook::current());
            check ("#239 the rules' file round trip", autolook::fromText (text) == autolook::current());
            autolook::current().on = true;
            {
                MainComponent ui (offline, false);
                ui.setSize (1600, 900);
                ui.selectTabForSnapshot (2);
                pump();
                write (ui, { 0, 0, 900, 420 }, "239-playlist");
            }
            auto panel = makeSettingsPanel (offline, SettingsPage::extras);
            panel->setSize (900, 3600);
            pump();
            if (auto* on = findId (*panel, "autoLook.on"))
            {
                auto* editor = on->getParentComponent();
                check ("#239 Settings > LYNX extras: the rules editor shows the three rules",
                       dynamic_cast<AutoLookEditor*> (editor) != nullptr && dynamic_cast<AutoLookEditor*> (editor)->rowCount() == 3);
                write (*editor->getParentComponent(), editor->getBoundsInParent().expanded (0, 40), "239-extras-rules");
            }
            else
                check ("#239 Settings > LYNX extras: the rules editor", false);
            autolook::current() = saved;
        }

        // ================= #240: a new project from a user template starts its own clock =================
        {
            auto song = Song::makeDefault();
            song.info.title = "Template 190od";
            song.info.created = 1763660040.0; // (2025-11-20)
            song.info.workSeconds = 1693.0;   // (0:28:13, the reporter's picture)
            juce::String error;
            const auto name = juce::String ("Template 190od");
            check ("#240 save as template", saveUserTemplate (song, name, error));
            const auto file = userTemplatesFolder().getChildFile (name + ProjectFile::extension);
            const auto json = file.loadFileAsString();
            check ("#240 the template file keeps no date and no working time", json.isNotEmpty() && ! json.contains ("\"created\"") && ! json.contains ("\"workSeconds\""));
            // an older template (saved by 0.1.30, with both): a new project made from it drops them
            auto old = song;
            ProjectFile::save (old, file, error);
            TemplateChoice choice;
            choice.kind = TemplateChoice::Kind::user;
            choice.name = file.getFileName().toStdString();
            const auto made = makeFromTemplate (choice);
            check ("#240 a project from an old template: no template date, working time 0",
                   made.has_value() && made->info.created == 0.0 && made->info.workSeconds == 0.0 && made->info.title == "Template 190od");
            AudioEngine offline (false);
            if (made)
                offline.setSong (*made);
            offline.markCreatedNow(); // (what MainComponent::startNewProject does)
            const auto now = (double) juce::Time::currentTimeMillis() / 1000.0;
            check ("#240 the new project is dated now and counts from 0:00:00",
                   std::abs (offline.getSong().info.created - now) < 60.0 && offline.workSecondsNow() < 60.0);
            line ("#240 created " + juce::String (offline.getSong().info.created, 0) + ", working " + juce::String (offline.workSecondsNow(), 1) + " s");
            auto info = makeSettingsPanel (offline, SettingsPage::info);
            info->setSize (760, 560);
            write (*info, info->getLocalBounds(), "240-project-info");
            file.deleteFile();
        }

        // ================= #236: the slide-note law in the project file =================
        {
            const auto fresh = makeNewProject();
            check ("#236 new projects: slide notes move velocity, pan and Mod X / Y (FL)", fresh.slideNoteProps);
            juce::String error;
            const auto back = ProjectFile::fromJson (ProjectFile::toJson (fresh, false), error);
            check ("#236 saved and loaded", back.has_value() && back->slideNoteProps);
            auto old = fresh;
            old.slideNoteProps = false;
            const auto json = ProjectFile::toJson (old, false);
            const auto oldBack = ProjectFile::fromJson (json, error);
            check ("#236 a project saved before (no key) keeps the pitch-only slides", ! json.contains ("slideNoteProps") && oldBack.has_value() && ! oldBack->slideNoteProps);
        }

        juce::File (prefix + "-checks.txt").replaceWithText (checks);
        std::printf ("%s", checks.toRawUTF8());
        return allOk;
    }

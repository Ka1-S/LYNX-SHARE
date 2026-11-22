    // --issuesshot190oq <prefix> (round 190oq, issues #243-#252), offscreen through the real components: the search windows
    // open with the caret in their search box (#244), Settings' find bar (#247), deleted clips and notes fading (#250), the
    // start page (#252), an ARA plugin on a mixer insert (#243: the slot menu's switch, the projection, the project file),
    // and the round's other items (#245, #246, #249, #251). <prefix>-checks.txt lists the checks; pictures @100 and @150.
    // Included in the app class of Main.cpp.
    bool runIssuesShot190oq (const juce::String& prefix)
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
#if JUCE_LINUX || JUCE_WINDOWS
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
        const auto menuHas = [] (const juce::PopupMenu& menu, const juce::String& start, bool* ticked = nullptr) {
            for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
                if (it.getItem().text.startsWith (start))
                {
                    if (ticked != nullptr)
                        *ticked = it.getItem().isTicked;
                    return true;
                }
            return false;
        };
        using namespace lynx;

        // ================= #244: the search windows open with the caret in their search box =================
        {
            AudioEngine engine (false);
            engine.setSong (Song::makeDefault());
            // a main window to come back to (the tool windows stay above it)
            juce::DocumentWindow host ("LYNX 190oq", theme::colour::bg1, 0);
            host.setUsingNativeTitleBar (true);
            host.setBounds (40, 40, 900, 600);
            host.setVisible (true);
            host.toFront (true);
            pump();
            const auto focusedIn = [&pump] (juce::DocumentWindow* w) {
                pump();
                juce::Thread::sleep (60);
                pump();
                auto* target = w != nullptr ? focusOnOpenTarget (*w) : nullptr;
                return std::pair<bool, bool> { target != nullptr, target != nullptr && target->hasKeyboardFocus (false) };
            };
            {
                QuickAddTargets targets;
                targets.effectsOnly = true;
                targets.effectsFirst = true;
                targets.effectsTitle = tr ("Effects: into slot {0}", { juce::String (3) });
                targets.addInstrument = [] (ChannelDef) {};
                targets.addEffect = [] (EffectSlot) {};
                auto* window = openToolWindow (makeQuickAddPalette (engine, std::move (targets)), tr ("Find an effect"), "slotsearch");
                const auto [marked, focused] = focusedIn (window);
                check ("#244 \"Find an effect\": its search box is marked to take the focus", marked);
                check ("#244 \"Find an effect\": the search box has the keyboard focus when it opens", focused);
                if (window != nullptr)
                    write (*window, window->getLocalBounds(), "244-find-an-effect");
                closeAllToolWindows();
                pump();
            }
            {
                showShortcutManager ({});
                auto* window = findToolWindow ("shortcuts");
                const auto [marked, focused] = focusedIn (window);
                check ("#244 \"Manage shortcuts\": its search box is marked to take the focus", marked);
                check ("#244 \"Manage shortcuts\": the search box has the keyboard focus when it opens", focused);
                if (window != nullptr)
                    write (*window, window->getLocalBounds(), "244-manage-shortcuts");
                closeAllToolWindows();
                pump();
            }
            host.setVisible (false);
        }

        // ================= #247: Settings' find bar =================
        {
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            auto panel = makeSettingsPanel (offline, SettingsPage::audio);
            panel->setSize (760, 700);
            pump();
            auto st = settingsFindForSnapshot (*panel, "");
            check ("#247 the find bar is in the Settings window", st.field != nullptr && st.field->getComponentID() == "settings.find");
            check ("#247 the find box takes the focus when Settings opens", st.field != nullptr && focusOnOpenTarget (*panel) == st.field);
            write (*panel, panel->getLocalBounds(), "247-settings-find-empty");
            st = settingsFindForSnapshot (*panel, "crossfades");
            line ("#247 'crossfades': " + juce::String (st.matches) + " matches, first '" + st.text + "' on page " + juce::String (st.page));
            check ("#247 'crossfades' jumps to Settings › Project (FL's automatic crossfades)", st.matches >= 2 && st.page == (int) SettingsPage::project
                                                                                                  && st.text.containsIgnoreCase ("crossfade"));
            check ("#247 the match is outlined", st.ring != nullptr && st.target != nullptr && st.ring->getParentComponent() == st.target->getParentComponent());
            write (*panel, panel->getLocalBounds(), "247-settings-find-crossfades");
            const auto first = st.current;
            st = settingsFindForSnapshot (*panel, "crossfades", 1);
            check ("#247 Enter / the down arrow steps to the next match", st.matches > 0 && st.current == (first + 1) % st.matches);
            st = settingsFindForSnapshot (*panel, "crossfades", -1);
            check ("#247 Shift+Enter / the up arrow steps back (wrapping)", st.matches > 0 && st.current == (first + st.matches - 1) % st.matches);
            st = settingsFindForSnapshot (*panel, "buffer");
            line ("#247 'buffer': " + juce::String (st.matches) + " matches, '" + st.text + "' on page " + juce::String (st.page));
            check ("#247 'buffer' finds the Audio page's buffer setting", st.matches >= 1 && st.page == (int) SettingsPage::audio);
            st = settingsFindForSnapshot (*panel, "track-only");
            line ("#247 'track-only': " + juce::String (st.matches) + " matches, '" + st.text + "' on page " + juce::String (st.page));
            check ("#247 a LYNX extra by its caption (Settings › LYNX extras)", st.matches >= 1 && st.page == (int) SettingsPage::extras);
            write (*panel, panel->getLocalBounds(), "247-settings-find-extras");
            st = settingsFindForSnapshot (*panel, "zzqx nothing");
            check ("#247 no match: \"No match\", no outline", st.matches == 0 && st.ring == nullptr);
            write (*panel, panel->getLocalBounds(), "247-settings-find-nomatch");
            st = settingsFindForSnapshot (*panel, "");
            check ("#247 an empty box: no outline, the tabs as before", st.matches == 0 && st.ring == nullptr);
        }

        // ================= #250: deleted clips and notes fade out as FL's do (Animations "Make it pretty") =================
        {
            {
                juce::Component holder;
                ui::DeleteFade probe (holder, ui::DeleteFade::playlistMs); // (the numbers only)
                line ("#250 Playlist ghost: alpha 1 at 0 ms, " + juce::String (probe.alphaAt (200.0), 3) + " at 200 ms, gone after "
                      + juce::String (probe.lifetimeMs(), 0) + " ms (FL ~801)");
                check ("#250 FL's fade law: 0.01^(t/T), dropped under 0.025 (Playlist T 1000 ms: ~801 ms)",
                       std::abs (probe.alphaAt (0.0) - 1.0) < 1e-9 && std::abs (probe.alphaAt (500.0) - 0.1) < 1e-6
                           && std::abs (probe.lifetimeMs() - 801.0) < 1.0 && probe.alphaAt (802.0) == 0.0);
                const auto dark = ui::DeleteFade::ghostColour (juce::Colour (0xff202a40)), light = ui::DeleteFade::ghostColour (juce::Colour (0xfff0f4ff));
                line ("#250 ghost colours: dark clip -> " + dark.toDisplayString (false) + ", light -> " + light.toDisplayString (false));
                const auto lightness = [] (juce::Colour c) { return (std::max ({ c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() })
                                                                     + std::min ({ c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() })) * 0.5f; };
                check ("#250 FL's ghost colour: HSL lightness held in 0.55..0.80", std::abs (lightness (dark) - 0.55f) < 0.01f && std::abs (lightness (light) - 0.80f) < 0.01f);
            }
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            song.mode = PlayMode::song;
            for (int i = 0; i < 4; ++i)
            {
                Clip c;
                c.kind = ClipKind::pattern;
                c.pattern = 0;
                c.track = i % 2;
                c.start = (std::int64_t) i * 4 * ppq;
                c.length = 4 * ppq;
                song.playlist.push_back (c);
            }
            if (! song.patterns.empty())
                for (int k = 0; k < 12; ++k) // (keys over the whole range: some are in the roll's view)
                {
                    Note n;
                    n.key = 36 + k * 6;
                    n.start = (std::int64_t) k * ppq / 2;
                    n.length = ppq / 2;
                    song.patterns[0].notes.push_back (n);
                }
            engine.setSong (song);
            engine.dispatchPendingMessages();
            {
                PlaylistComponent pl (engine);
                pl.setBounds (0, 0, 1000, 420);
                pl.setVisible (true);
                engine.dispatchPendingMessages();
                const auto r = pl.clipBoundsForSnapshot (1);
                const auto before = pl.createComponentSnapshot (pl.getLocalBounds(), true, 1.0f);
                check ("#250 on by default (FL's Animations default: Make it pretty)", ui::DeleteFade::enabled());
                pl.deleteFadeForSnapshot().freezeForSnapshot (120.0);
                pl.selectClipsForSnapshot ({ 1 });
                pl.deleteSelectionForSnapshot();
                engine.dispatchPendingMessages();
                check ("#250 Delete: the clip leaves a ghost", pl.deleteFadeForSnapshot().count() == 1);
                write (pl, r.expanded (60, 30), "250-playlist-ghost-120ms");
                pl.deleteFadeForSnapshot().freezeForSnapshot (500.0);
                write (pl, r.expanded (60, 30), "250-playlist-ghost-500ms");
                const auto ghost = pl.createComponentSnapshot (pl.getLocalBounds(), true, 1.0f);
                const auto edge = juce::Point<int> (r.getX() + 2, r.getCentreY());
                line ("#250 the clip's left edge before " + before.getPixelAt (edge.x, edge.y).toDisplayString (false) + ", ghost at 500 ms "
                      + ghost.getPixelAt (edge.x, edge.y).toDisplayString (false));
                pl.deleteFadeForSnapshot().clear();
                check ("#250 the ghost goes", pl.deleteFadeForSnapshot().count() == 0);
                // Reduced effects / Animations off ("Keep it sober"): nothing
                skin::setReducedEffects (true);
                pl.selectClipsForSnapshot ({ 0 });
                pl.deleteSelectionForSnapshot();
                check ("#250 Reduced effects: no ghost", pl.deleteFadeForSnapshot().count() == 0);
                skin::setReducedEffects (false);
                ui::motion::enabled() = false;
                pl.selectClipsForSnapshot ({ 0 });
                pl.deleteSelectionForSnapshot();
                check ("#250 Animations off (FL's Keep it sober): no ghost", pl.deleteFadeForSnapshot().count() == 0);
                ui::motion::enabled() = true;
            }
            {
                PianoRollComponent roll (engine);
                roll.setBounds (0, 0, 1000, 460);
                roll.setVisible (true);
                engine.dispatchPendingMessages();
                roll.deleteFadeForSnapshot().freezeForSnapshot (100.0);
                const auto before = engine.getSong().activePattern().notes.size();
                std::vector<int> inView; // (the notes the roll shows: C6…C7 at this size)
                for (int i = 0; i < (int) engine.getSong().activePattern().notes.size(); ++i)
                    if (const auto k = engine.getSong().activePattern().notes[(std::size_t) i].key; k >= 72 && k <= 84)
                        inView.push_back (i);
                roll.setSelectionForSnapshot (inView);
                roll.deleteSelectionForSnapshot();
                engine.dispatchPendingMessages();
                line ("#250 Piano roll: notes " + juce::String ((int) before) + " -> " + juce::String ((int) engine.getSong().activePattern().notes.size())
                      + ", ghosts " + juce::String (roll.deleteFadeForSnapshot().count()) + ", fades on " + juce::String ((int) ui::DeleteFade::enabled())
                      + ", grid " + roll.gridAreaForSnapshot().toString() + ", selected channel " + juce::String (engine.getSelectedChannel()));
                check ("#250 Piano roll: the deleted notes leave ghosts (T 500 ms: ~401 ms)", roll.deleteFadeForSnapshot().count() >= 1
                                                                                                   && std::abs (roll.deleteFadeForSnapshot().lifetimeMs() - 400.5) < 1.0);
                write (roll, roll.getLocalBounds(), "250-pianoroll-ghost-100ms");
            }
        }

        // ================= #252: the start page (LYNX extra) =================
        {
            check ("#252 on by default (owner decision: FL shows its Welcome window at every start)", startpage::enabled());
            startpageui::Host host;
            const std::int64_t day = 24LL * 3600 * 1000;
            host.nowMs = juce::Time (2026, 10, 21, 18, 0, 0).toMilliseconds();
            const char* names[] = { "Night Drive", "Trap Idea 2", "Vocal Chops", "Lo-fi Sunday", "Remix (Old)", "House Groove 124", "Beat Tape 07" };
            const char* folders[] = { "/home/me/Music/LYNX/Night Drive", "/home/me/Music/LYNX/Beats", "/home/me/Music/LYNX/Vocals",
                                      "/home/me/Music/LYNX/Beats", "/media/usb/Old projects", "/home/me/Music/LYNX/House", "/home/me/Music/LYNX/Beats" };
            const int ages[] = { 0, 1, 2, 5, 40, 12, 90 };
            for (int i = 0; i < 7; ++i)
                host.recent.push_back ({ std::string (folders[i]) + "/" + names[i] + ".lynx", names[i], folders[i], host.nowMs - ages[i] * day - (i + 1) * 3700000LL, i != 4,
                                         (std::int64_t) (60 + 37 * i) * 1024 });
            for (const auto& e : startfrom::entries())
                host.templates.push_back (e.name);
            juce::String opened;
            host.open = [&opened] (const juce::String& path) { opened = path; };
            host.close = [] {};
            auto overlay = startpageui::makeOverlay (host);
            overlay->setSize (1280, 760);
            pump();
            write (*overlay, overlay->getLocalBounds(), "252-start-page");
            auto card = startpageui::makeCard (host);
            write (*card, card->getLocalBounds(), "252-start-page-card");
            check ("#252 every recent project listed", startpageui::shownRowsForSnapshot (*card) == 7);
            startpageui::typeForSnapshot (*card, "beats");
            check ("#252 typing filters (name and folder)", startpageui::shownRowsForSnapshot (*card) == 3);
            write (*card, card->getLocalBounds(), "252-start-page-search");
            startpageui::selectRowForSnapshot (*card, 1);
            check ("#252 Enter opens the selected project", startpageui::openSelectedForSnapshot (*card) && opened.endsWith ("Lo-fi Sunday.lynx"));
            {
                bool other = false;
                auto withOther = host;
                withOther.openDialog = [&other] { other = true; };
                auto card2 = startpageui::makeCard (withOther);
                startpageui::selectRowForSnapshot (*card2, -1); // (FL's first row: "Other projects...")
                check ("#252 FL's \"Other projects...\" row opens File › Open", startpageui::openSelectedForSnapshot (*card2) && other);
            }
            startpageui::typeForSnapshot (*card, "zzz");
            check ("#252 no match: an empty list with a line saying so", startpageui::shownRowsForSnapshot (*card) == 0);
            write (*card, card->getLocalBounds(), "252-start-page-nomatch");
            check ("#252 its search box exists (the caret goes there)", startpageui::searchBoxForSnapshot (*card) != nullptr);
            check ("#252 shown at start only when switched on, without a file, not over the first run's card",
                   startpage::showAtStart (true, false, false, false) && ! startpage::showAtStart (true, false, true, false));
        }

        // ================= #243: an ARA plugin on a mixer insert =================
        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            ChannelDef take;
            take.name = "Lead vocal";
            take.instrument = InstrumentKind::audioClip;
            take.samplePath = "vocal.wav";
            take.mixerTrack = 2;
            take.uid = 4243;
            song.channels.push_back (take);
            song.mixer[2].name = "Vocals";
            EffectSlot fx;
            fx.kind = EffectKind::plugin;
            fx.plugin.id = "VST3-Melodyne-190oq";
            fx.plugin.name = "Melodyne";
            song.mixer[2].effects.push_back (fx);
            engine.setSong (song);
            // a scanned ARA plugin (the menu reads the plugin list; no instance is made here)
            juce::PluginDescription d;
            d.name = "Melodyne";
            d.pluginFormatName = "VST3";
            d.fileOrIdentifier = "/plugins/Melodyne.vst3";
            d.uniqueId = 190;
            d.deprecatedUid = 190;
            d.isInstrument = false;
#if JUCE_PLUGINHOST_ARA
            d.hasARAExtension = true;
#endif
            engine.getPluginHost().getKnownPlugins().addType (d);
            const auto id = d.createIdentifierString().toStdString();
            engine.editSong ([&id] (Song& s) { s.mixer[2].effects[0].plugin.id = id; });
            MixerComponent mixer (engine);
            mixer.setBounds (0, 0, 1400, 640);
            mixer.selectTrack (2);
            pump();
            bool ticked = true;
            const auto menu = mixer.slotMenuForSnapshot (0);
            const auto offered = menuHas (menu, tr ("Edit the audio clips routed here (ARA)"), &ticked);
            check ("#243 the slot menu of an ARA plugin offers ARA on the insert", ! AraHost::available() || offered);
            check ("#243 off for a slot that was there before (old projects, FL imports)", ! ticked);
            saveMenus (juce::StringArray { "FX slot" }, std::vector<juce::PopupMenu> { menu }, prefix + "-243-slot-menu.png");
            aramenu::setSlotAra (engine, 2, 0, true);
            check ("#243 switched on: the routed audio clip channel plays through it", ara::insertLink (engine.getSong(), (int) engine.getSong().channels.size() - 1).pluginId == id);
            const auto menuOn = mixer.slotMenuForSnapshot (0);
            menuHas (menuOn, tr ("Edit the audio clips routed here (ARA)"), &ticked);
            check ("#243 the menu shows it ticked", ! AraHost::available() || ticked);
            juce::String error;
            const auto back = ProjectFile::fromJson (ProjectFile::toJson (engine.getSong(), false), error);
            check ("#243 the insert's ARA mode is saved and read back", back.has_value() && back->mixer[2].effects[0].ara);
            const auto json = ProjectFile::toJson (Song::makeDefault(), false);
            check ("#243 nothing new in a project without it", ! json.contains ("\"ara\""));
            const auto plugin = engine.getSong().mixer[2].effects[0].plugin;
            check ("#243 one ARA insert per mixer track: a second ARA plugin on insert 2 starts as a plain effect",
                   ! aramenu::araByDefault (engine, 2, plugin));
            engine.editSong ([] (Song& s) { s.channels.back().mixerTrack = 3; });
            check ("#243 a new ARA plugin on an insert that audio clips go to starts in ARA mode (REAPER, Logic, Studio One)",
                   ! AraHost::available() || aramenu::araByDefault (engine, 3, plugin));
            check ("#243 ... not on the Master, not on an insert without audio clips", ! aramenu::araByDefault (engine, 0, plugin) && ! aramenu::araByDefault (engine, 4, plugin));
            // the clip menu of a channel edited through its insert
            engine.editSong ([] (Song& s) { s.channels.back().mixerTrack = 2; });
            {
                Clip c;
                c.kind = ClipKind::audio;
                c.channel = (int) engine.getSong().channels.size() - 1;
                c.length = 4 * ppq;
                engine.editSong ([c] (Song& s) { s.playlist.push_back (c); });
                const auto clipMenu = aramenu::forClip (engine, (int) engine.getSong().playlist.size() - 1);
                check ("#243 the clip menu says it is edited on its mixer track's ARA insert", ! AraHost::available() || menuHas (clipMenu, tr ("Edited in {0} on mixer track {1} (ARA insert)", { "Melodyne", "2" })));
                saveMenus (juce::StringArray { "Edit in ARA plugin" }, std::vector<juce::PopupMenu> { clipMenu }, prefix + "-243-clip-menu.png");
            }
            // ProjectDowngrade (0.1.28 copies): the slot's ARA goes with a line
            line ("#243 ARA host available in this build: " + juce::String (AraHost::available() ? "yes" : "no"));
        }

        // ================= #245: FL's Shift+Up / Shift+Down move the selected clips one track =================
        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            ChannelDef take;
            take.instrument = InstrumentKind::audioClip;
            take.name = "Take";
            song.channels.push_back (take);
            Clip a;
            a.kind = ClipKind::audio;
            a.channel = (int) song.channels.size() - 1;
            a.track = 2;
            a.start = 4 * ppq;
            a.length = 4 * ppq;
            song.playlist.push_back (a);
            engine.setSong (song);
            PlaylistComponent pl (engine);
            pl.setBounds (0, 0, 900, 420);
            const auto clip = (int) engine.getSong().playlist.size() - 1;
            pl.selectClipsForSnapshot ({ clip });
            const auto* up = keys::findAction ("playlist.shiftUp");
            check ("#245 Shift+Up is the key of \"Shift the selected clips up (one track)\" (FL's EEShiftUAction)",
                   up != nullptr && ! up->defaults.empty() && up->defaults[0].key == keys::up && up->defaults[0].mods == keys::shift);
            pl.runAction ("playlist.shiftUp");
            engine.dispatchPendingMessages();
            check ("#245 an audio clip moves one track up", engine.getSong().playlist[(std::size_t) clip].track == 1);
            pl.runAction ("playlist.shiftDown");
            pl.runAction ("playlist.shiftDown");
            engine.dispatchPendingMessages();
            check ("#245 and down (Shift+Down)", engine.getSong().playlist[(std::size_t) clip].track == 3);
        }

        // ================= #246: the Channel rack opens in Track-only mode =================
        {
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            MainComponent ui (offline, false);
            ui.setSize (1600, 900);
            pump();
            const auto was = trackonly::enabled();
            workflowextras::setExtra ("layout.trackOnly", true);
            pump();
            const auto rackShown = [&ui] { return ui.workspaceForTest().isPanelVisible (workspace::Panel::channelRack); };
            check ("#246 Track-only mode hides the Channel rack when it comes on", ! rackShown());
            ui.runActionForSnapshot ("panel.channelRack");
            pump();
            check ("#246 ... and F6 opens it again (FL keeps an instrument track's channel in its rack)", rackShown());
            workflowextras::setExtra ("playlist.wheelZones", ! wheelzones::enabled()); // another switch: the rack stays
            workflowextras::setExtra ("playlist.wheelZones", ! wheelzones::enabled());
            pump();
            check ("#246 another switch does not close it again", rackShown());
            write (ui, ui.getLocalBounds(), "246-track-only-rack");
            workflowextras::setExtra ("layout.trackOnly", was);
        }

        // ================= #249: FL's Alt+M only mutes; the LYNX extra makes it toggle =================
        {
            Song song = Song::makeDefault();
            for (int i = 0; i < 3; ++i)
            {
                Clip c;
                c.kind = ClipKind::pattern;
                c.pattern = 0;
                c.start = (std::int64_t) i * 4 * ppq;
                c.length = 4 * ppq;
                c.muted = i == 2;
                song.playlist.push_back (c);
            }
            check ("#249 not all muted: Alt+M mutes", ! clipops::allClipsMuted (song, { 0, 2 }));
            check ("#249 all muted: the extra's Alt+M unmutes", clipops::allClipsMuted (song, { 2 }));
            check ("#249 nothing to act on: mutes (nothing)", ! clipops::allClipsMuted (song, { 7 }));
            check ("#249 off by default (FL: EEMuteAction sets the mute)", ! mutekeys::toggles());
            AudioEngine engine (false);
            engine.setSong (song);
            PlaylistComponent pl (engine);
            pl.setBounds (0, 0, 900, 420);
            pl.selectClipsForSnapshot ({ 0 });
            pl.runAction ("playlist.muteClips");
            pl.runAction ("playlist.muteClips");
            engine.dispatchPendingMessages();
            check ("#249 FL: Alt+M twice leaves the clip muted", engine.getSong().playlist[0].muted);
            mutekeys::setToggles (true);
            pl.runAction ("playlist.muteClips");
            engine.dispatchPendingMessages();
            check ("#249 the extra on: Alt+M again unmutes it", ! engine.getSong().playlist[0].muted);
            pl.runAction ("playlist.muteClips");
            engine.dispatchPendingMessages();
            check ("#249 ... and mutes it again", engine.getSong().playlist[0].muted);
            pl.selectClipsForSnapshot ({ 0, 1 });
            pl.runAction ("playlist.muteClips");
            engine.dispatchPendingMessages();
            check ("#249 a mixed selection: Alt+M mutes them all first", engine.getSong().playlist[0].muted && engine.getSong().playlist[1].muted);
            mutekeys::setToggles (false);
        }

        // ================= #251: FL's zipped loop package, Project data files, Live's Collect All =================
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-190oq-251");
            dir.deleteRecursively();
            const auto lib = dir.getChildFile ("Library");
            lib.createDirectory();
            const auto makeWav = [] (const juce::File& f) {
                f.getParentDirectory().createDirectory();
                juce::WavAudioFormat format;
                juce::AudioBuffer<float> b (1, 480);
                for (int i = 0; i < 480; ++i)
                    b.setSample (0, i, 0.25f * std::sin (0.05f * (float) i));
                std::unique_ptr<juce::OutputStream> out (f.createOutputStream().release());
                if (auto* w = format.createWriterFor (out.get(), 48000.0, 1, 16, {}, 0))
                {
                    out.release();
                    std::unique_ptr<juce::AudioFormatWriter> writer (w);
                    writer->writeFromAudioSampleBuffer (b, 0, 480);
                }
            };
            makeWav (lib.getChildFile ("Kick.wav"));
            makeWav (lib.getChildFile ("Other").getChildFile ("Kick.wav"));
            const auto projectDir = dir.getChildFile ("My Beat");
            makeWav (projectDir.getChildFile ("Audio").getChildFile ("Recorded").getChildFile ("Take 1.wav"));
            auto song = Song::makeDefault();
            for (const auto& f : { lib.getChildFile ("Kick.wav"), lib.getChildFile ("Other").getChildFile ("Kick.wav"),
                                   projectDir.getChildFile ("Audio").getChildFile ("Recorded").getChildFile ("Take 1.wav") })
            {
                ChannelDef c;
                c.instrument = InstrumentKind::sampler;
                c.name = f.getFileNameWithoutExtension().toStdString();
                c.samplePath = f.getFullPathName().toStdString();
                song.channels.push_back (c);
            }
            // the zipped loop package: flat, the project named after the zip, the two Kick.wav both kept
            ProjectPackage::Result r;
            juce::String error;
            const auto zip = dir.getChildFile ("Beat Package.zip");
            const auto zipped = ProjectPackage::exportZip (song, zip.getFileNameWithoutExtension(), zip, r, error);
            juce::StringArray names;
            {
                juce::ZipFile z (zip);
                for (int i = 0; i < z.getNumEntries(); ++i)
                    names.add (z.getEntry (i)->filename);
            }
            line ("#251 zip entries: " + names.joinIntoString (", "));
            check ("#251 Zipped loop package (FL): the project named after the zip, its files beside it, flat",
                   zipped && r.files == 3 && names.contains ("Beat Package.lynx") && names.contains ("Kick.wav") && names.contains ("Kick_2.wav")
                       && names.contains ("Take 1.wav"));
            const auto unpacked = ProjectPackage::extractZip (zip, dir.getChildFile ("Unpacked"), error);
            const auto reopened = unpacked.existsAsFile() ? ProjectFile::load (unpacked, error) : std::nullopt;
            bool allThere = reopened.has_value();
            if (reopened)
                for (int i = (int) reopened->channels.size() - 3; i < (int) reopened->channels.size(); ++i)
                    allThere = allThere && juce::File (juce::String::fromUTF8 (reopened->channels[(std::size_t) i].samplePath.c_str())).existsAsFile();
            check ("#251 the package opens with every sample found", allThere);
            // Project data files (FL): copies, flat, the project untouched
            const auto before = song;
            const auto dataDir = dir.getChildFile ("Data files");
            const auto copied = ProjectPackage::copyDataFiles (song, dataDir, r, error);
            check ("#251 Project data files (FL): every file copied flat, nothing re-pointed, no project file",
                   copied && r.files == 3 && dataDir.getChildFile ("Kick.wav").existsAsFile() && dataDir.getChildFile ("Kick_2.wav").existsAsFile()
                       && song.channels.back().samplePath == before.channels.back().samplePath && song.channels.size() == before.channels.size() && dataDir.findChildFiles (juce::File::findFiles, false, "*.lynx").isEmpty());
            // Collect all and save (Live): the files outside the project folder go to Samples/Imported, the recording stays
            std::map<std::string, std::string> relink;
            const auto collected = ProjectPackage::collectIntoProject (song, projectDir, relink, r, error);
            applyRelocations (song, relink);
            const auto imported = projectDir.getChildFile ("Samples").getChildFile ("Imported");
            check ("#251 Collect all (Live): the two outside files copied into Samples/Imported and used from there",
                   collected && r.files == 2 && imported.getChildFile ("Kick.wav").existsAsFile() && imported.getChildFile ("Kick_2.wav").existsAsFile()
                       && juce::File (juce::String::fromUTF8 (song.channels[song.channels.size() - 3].samplePath.c_str())).isAChildOf (projectDir));
            check ("#251 ... a file already inside the project folder stays where it is",
                   song.channels.back().samplePath == projectDir.getChildFile ("Audio").getChildFile ("Recorded").getChildFile ("Take 1.wav").getFullPathName().toStdString());
            dir.deleteRecursively();
            // the File menu and its Export submenu (FL's names)
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            MainComponent ui (offline, false);
            ui.setSize (1600, 900);
            const auto file = ui.getMenuForSnapshot (0);
            check ("#251 File: Collect all and save, Start page...", menuHas (file, tr ("Collect all and save")) && menuHas (file, tr ("Start page...")));
            juce::PopupMenu exportMenu;
            for (juce::PopupMenu::MenuItemIterator it (file); it.next();)
                if (it.getItem().text == tr ("Export") && it.getItem().subMenu != nullptr)
                    exportMenu = *it.getItem().subMenu;
            check ("#251 File › Export: FL's Zipped loop package... and Project data files...",
                   menuHas (exportMenu, tr ("Zipped loop package...")) && menuHas (exportMenu, tr ("Project data files...")));
            saveMenus (juce::StringArray { "File", "Export" }, std::vector<juce::PopupMenu> { file, exportMenu }, prefix + "-251-file-menu.png");
        }

        // ================= #253: FL's send-level automation (an FL demo that has it) =================
        {
            const juce::File demo ("/mnt/c/Program Files/Image-Line/FL Studio 2026/Data/Demo projects/Product demos/Synth_dfr - Overcome - Sytrus.flp");
            if (demo.existsAsFile())
            {
                juce::MemoryBlock bytes;
                demo.loadFileAsData (bytes);
                const auto* data = static_cast<const std::uint8_t*> (bytes.getData());
                const auto song = importFlp (flp::parseProject (std::vector<std::uint8_t> (data, data + bytes.getSize())));
                int sends = 0, none = 0;
                for (const auto& a : song.automations)
                {
                    sends += a.target.kind == AutomationTargetKind::mixerSend ? 1 : 0;
                    none += a.target.kind == AutomationTargetKind::none ? 1 : 0;
                }
                line ("#253 Synth_dfr - Overcome: " + juce::String (sends) + " send-level automations, " + juce::String (none) + " without a target");
                check ("#253 FL's send automation (two clips: insert 15 -> 3 / 2, insert 12 -> 4 / 5) imports as send-level automation", sends >= 2);
            }
            else
                line ("#253 the FL demo is not on this machine: skipped");
        }

        // @@190oq-more@@

        juce::File (prefix + "-checks.txt").replaceWithText (checks);
        std::printf ("%s", checks.toRawUTF8());
        return allOk;
    }

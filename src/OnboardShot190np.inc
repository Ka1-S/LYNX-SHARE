    // --onboard190npshot <prefix> (round 190np): the first-run onboarding off screen, 100 % and @150 — the welcome card (and
    // hovered, and squeezed into 1280x720), Skip / Esc, each of the five choices applied (layout, starter, Next-3 card),
    // the Next-3 items ticking, the empty-state hints (Pattern, Clip-launch, Simple), the two contextual tips and the
    // Help menu in the three layouts. <prefix>-checks.txt lists the checks. Run it with --lang pt-BR for the Portuguese
    // pictures. Included in the app class of Main.cpp.
    bool runOnboardShot190np (const juce::String& prefix)
    {
        using namespace lynx;
        using onboarding::Choice;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        juce::String checks;
        bool allOk = true;
        const auto check = [&checks, &allOk, &prefix] (const juce::String& what, bool ok) {
            checks << (ok ? "ok   " : "FAIL ") << what << "\n";
            allOk = allOk && ok;
            juce::File (prefix + "-checks.txt").replaceWithText (checks);
        };
        const auto line = [&checks, &prefix] (const juce::String& what) {
            checks << "     " << what << "\n";
            juce::File (prefix + "-checks.txt").replaceWithText (checks);
        };
        const auto write = [&prefix] (juce::Component& c, juce::Rectangle<int> area, const juce::String& name) {
            area = area.getIntersection (c.getLocalBounds());
            if (area.isEmpty())
                return;
            savePng (c.createComponentSnapshot (area, true, 1.0f), prefix + "-" + name + ".png");
            savePng (c.createComponentSnapshot (area, true, 1.5f), prefix + "-" + name + "@150.png");
        };

        AudioEngine offline (false);
        MainComponent ui (offline, false);
        ui.setSize (1920, 1080);
        ui.setVisible (true);
        ui.setOnboardingForSnapshot (true);
        const auto settle = [&offline] {
            for (int k = 0; k < 4; ++k)
                offline.dispatchPendingMessages();
#if JUCE_LINUX || JUCE_WINDOWS // (rollup 0.1.31 step 2: on Windows the cards' callAsync never ran: 29 checks failed there)
            for (int i = 0; i < 256 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i) {} // (clicks and the cards' async callbacks)
#endif
        };
        const auto fresh = [&] (layoutstyle::Style style) {
            ui.hideGettingStarted();
            ui.closeTip();
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.setSong (makeNewProject());
            settle();
            if (style == layoutstyle::Style::pattern)
                ui.showDesktopForSnapshot (false);
            else
            {
                ui.selectTabForSnapshot (2); // (the Playlist on the whole workspace, as the layout switch does in the real window)
                ui.setLayoutStyle (style);
            }
            settle();
        };
        const auto whole = [&] (const juce::String& name) {
            settle();
            write (ui, ui.getLocalBounds(), name);
        };
        const auto hintKind = [&ui] {
            auto* h = ui.emptyHintForSnapshot();
            return h != nullptr && h->isVisible() && h->getParentComponent() != nullptr ? h->getKind() : onboarding::EmptyHint::none;
        };

        // --- the empty Playlist (Pattern layout) --------------------------------------------------------------------------
        fresh (layoutstyle::Style::pattern);
        check ("an empty Playlist shows its hint (Pattern layout)", hintKind() == onboarding::EmptyHint::playlist);
        whole ("empty-playlist");
        if (auto* hint = ui.emptyHintForSnapshot())
        {
            write (*hint, hint->getLocalBounds(), "empty-playlist-card");
            check ("the hint's body passes clicks to the Playlist; its buttons take them",
                   ! hint->hitTest (hint->getWidth() / 2, 30) && hint->getNumChildComponents() == 3);
        }

        // --- the welcome card ---------------------------------------------------------------------------------------------
        ui.showWelcomeCard (false);
        settle();
        auto* overlay = ui.setupOverlayForTest();
        check ("Help › Welcome... shows the card over the window", overlay != nullptr);
        if (overlay == nullptr)
            return false;
        whole ("welcome");
        if (auto* card = overlay->findChildWithID ("welcome.card"))
        {
            write (*card, card->getLocalBounds(), "welcome-card");
            int tiles = 0;
            for (int i = 0; i < onboarding::choiceCount; ++i)
                tiles += card->findChildWithID (juce::String ("welcome.choice.") + onboarding::choiceId (onboarding::choiceAt (i))) != nullptr ? 1 : 0;
            check ("five choices, each found by its component ID", tiles == 5);
            check ("Skip, ×, More setup, Language and Test are on the card",
                   card->findChildWithID ("welcome.skip") != nullptr && card->findChildWithID ("welcome.close") != nullptr
                       && card->findChildWithID ("welcome.more") != nullptr && card->findChildWithID ("welcome.language") != nullptr
                       && card->findChildWithID ("welcome.test") != nullptr);
            line ("card " + card->getBounds().toString());
            // issue #233 (rollup 0.1.31 step 2): round 190ns's Updates choice on the card's bottom row, ticked by default
            {
                auto* updates = dynamic_cast<juce::ToggleButton*> (card->findChildWithID ("welcome.updates"));
                const auto* more = card->findChildWithID ("welcome.more");
                check ("#233: \"Check for updates automatically\" on the card's bottom row, ticked by default",
                       ! updater::offered()
                           || (updates != nullptr && updates->isVisible() && updates->getToggleState() && more != nullptr
                               && updates->getY() >= more->getBottom() && card->getLocalBounds().contains (updates->getBounds())));
                if (updates != nullptr)
                    write (*card, updates->getBounds().getUnion (more != nullptr ? more->getBounds() : updates->getBounds()).expanded (8), "welcome-updates-row");
            }
            onboardui::setWelcomeHoverForSnapshot (*overlay, 0);
            write (*card, card->getLocalBounds(), "welcome-card-hover");
            onboardui::setWelcomeHoverForSnapshot (*overlay, -1);
        }
        // a small window: the tiles squeeze, nothing is cut
        ui.setSize (1280, 720);
        settle();
        whole ("welcome-1280x720");
        if (auto* card = overlay->findChildWithID ("welcome.card"))
        {
            const auto* skip = card->findChildWithID ("welcome.skip");
            check ("at 1280x720 the card fits the window and its Skip button is inside it",
                   ui.getLocalBounds().contains (card->getBounds()) && skip != nullptr && card->getLocalBounds().contains (skip->getBounds()));
            const auto* updates = card->findChildWithID ("welcome.updates");
            check ("#233: at 1280x720 the Updates toggle is inside the card",
                   ! updater::offered() || (updates != nullptr && card->getLocalBounds().contains (updates->getBounds())));
        }
        ui.setSize (1920, 1080);
        settle();

        // --- Esc = Skip: nothing changes ----------------------------------------------------------------------------------
        {
            const auto before = offline.getSong().channels.size();
            overlay->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            settle();
            check ("Esc closes the card", ui.setupOverlayForTest() == nullptr);
            check ("Skip keeps the Pattern layout and the project as it was",
                   ui.getLayoutStyle() == layoutstyle::Style::pattern && offline.getSong().channels.size() == before && ui.next3ForSnapshot() == nullptr);
            check ("Skip is remembered as the answer", ui.onboardingStateForCheck().choice == "skip" && ! ui.onboardingStateForCheck().pending);
            check ("#233: Esc counts as answered with updates on (the toggle as shown: ticked)", ui.welcomeUpdatesForCheck());
        }
        // #233: an untick is what Skip applies; ticked again for the rest of the route
        if (updater::offered())
            for (const bool on : { false, true })
            {
                ui.showWelcomeCard (false);
                settle();
                if (auto* o = ui.setupOverlayForTest())
                    if (auto* card = o->findChildWithID ("welcome.card"))
                    {
                        if (auto* t = dynamic_cast<juce::ToggleButton*> (card->findChildWithID ("welcome.updates")))
                            t->setToggleState (on, juce::dontSendNotification);
                        if (auto* b = dynamic_cast<juce::Button*> (card->findChildWithID ("welcome.skip")))
                            b->triggerClick();
                    }
                settle();
                check (juce::String ("#233: Skip with the Updates toggle ") + (on ? "ticked applies on" : "unticked applies off"),
                       ui.setupOverlayForTest() == nullptr && ui.welcomeUpdatesForCheck() == on);
            }
        // the Skip button and × do the same
        for (const auto* id : { "welcome.skip", "welcome.close" })
        {
            ui.showWelcomeCard (false);
            settle();
            if (auto* o = ui.setupOverlayForTest())
                if (auto* b = dynamic_cast<juce::Button*> (o->findChildWithID ("welcome.card") != nullptr ? o->findChildWithID ("welcome.card")->findChildWithID (id) : nullptr))
                    b->triggerClick();
            settle();
            check (juce::String (id) + " closes the card and keeps the Pattern layout", ui.setupOverlayForTest() == nullptr && ui.getLayoutStyle() == layoutstyle::Style::pattern);
        }

        // --- each choice ---------------------------------------------------------------------------------------------------
        struct Expect
        {
            Choice choice;
            layoutstyle::Style style;
            const char* name;
        };
        for (const auto& e : { Expect { Choice::mobile, layoutstyle::Style::simple, "mobile" }, Expect { Choice::fl, layoutstyle::Style::pattern, "fl" },
                               Expect { Choice::live, layoutstyle::Style::clipLaunch, "live" }, Expect { Choice::timeline, layoutstyle::Style::simple, "timeline" },
                               Expect { Choice::newcomer, layoutstyle::Style::simple, "new" } })
        {
            fresh (layoutstyle::Style::pattern);
            if (e.style != layoutstyle::Style::pattern)
                ui.selectTabForSnapshot (2);
            ui.showWelcomeCard (false);
            settle();
            // a click on the tile (its own click, as a user's)
            if (auto* o = ui.setupOverlayForTest())
                if (auto* card = o->findChildWithID ("welcome.card"))
                    if (auto* tile = dynamic_cast<juce::Button*> (card->findChildWithID (juce::String ("welcome.choice.") + onboarding::choiceId (e.choice))))
                        tile->triggerClick();
            settle();
            const auto& song = offline.getSong();
            check (juce::String (e.name) + ": one click closes the card and opens the " + layoutstyle::id (e.style) + " layout",
                   ui.setupOverlayForTest() == nullptr && ui.getLayoutStyle() == e.style);
            check (juce::String (e.name) + ": remembered", ui.onboardingStateForCheck().choice == onboarding::choiceId (e.choice));
            auto* simple = ui.getSimpleWorkflowForSnapshot();
            switch (e.choice)
            {
                case Choice::mobile:
                {
                    bool drums = false;
                    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
                        drums = drums || simpletracks::kindOf (song, t) == simpletracks::Kind::drums;
                    check ("mobile: the starter beat (a Drums track with notes)", drums && onboarding::anyPatternHasNotes (song));
                    settle();
                    if (auto* dm = ui.findChildWithID ("simple.dock.drumMachine") != nullptr ? ui.findChildWithID ("simple.dock.drumMachine")
                                                                                              : onboardui::findShowing (ui, "simple.dock.drumMachine"))
                        write (*dm, dm->getLocalBounds(), "drum-machine");
                    break;
                }
                case Choice::timeline:
                    check ("timeline: the New-track card", simple != nullptr && simple->isChooserShowing());
                    check ("timeline: no Mixer over the New-track card yet", ! ui.mixerShownForCheck());
                    break;
                case Choice::newcomer:
                    check ("new: the first-song tour runs", simple != nullptr && simple->tourRunning());
                    check ("new: no Next-3 card (the tour is the guide)", ui.next3ForSnapshot() == nullptr);
                    break;
                case Choice::fl:
                case Choice::live:
                    check (juce::String (e.name) + ": the project is not changed", song.channels.size() == makeNewProject().channels.size());
                    break;
                default:
                    break;
            }
            if (e.choice != Choice::newcomer)
                check (juce::String (e.name) + ": the Next-3 card shows its three items",
                       ui.next3ForSnapshot() != nullptr && ui.next3ForSnapshot()->getParentComponent() != nullptr && ui.next3ForSnapshot()->getChoice() == e.choice);
            whole (juce::String ("choice-") + e.name);
            if (auto* n = ui.next3ForSnapshot())
                write (*n, n->getLocalBounds(), juce::String ("next3-") + e.name);
            if (e.choice == Choice::live)
                check ("live: the empty clip grid shows its hint", hintKind() == onboarding::EmptyHint::clipGrid);
            if (e.choice == Choice::timeline && simple != nullptr)
            {
                // the first track (the chooser's Instrument tile does the same): the Mixer comes up, the card closes
                simple->doTourStep (tour::instrument);
                settle();
                check ("timeline: the first track brings up the Mixer", ui.mixerShownForCheck() && ! simple->isChooserShowing());
                check ("timeline: the Next-3 card ticks the instrument track and the Mixer",
                       onboarding::ticked (ui.onboardingStateForCheck(), onboarding::Item::instrumentTrack));
                whole ("choice-timeline-first-track");
                if (auto* n = ui.next3ForSnapshot())
                    write (*n, n->getLocalBounds(), "next3-timeline-first-track");
            }
            if (e.choice == Choice::mobile)
            {
                // the items tick from what the song and the view show
                ui.onboardingContextForCheck().played = true;
                ui.updateOnboarding();
                settle();
                check ("mobile: Play ticks item 1", onboarding::ticked (ui.onboardingStateForCheck(), onboarding::Item::play));
                if (auto* n = ui.next3ForSnapshot())
                    write (*n, n->getLocalBounds(), "next3-mobile-ticked");
                // a recorded voice take finishes the list: "Nice, that's the basics"
                offline.editSong ([] (Song& s) {
                    simpletracks::Request q;
                    q.kind = simpletracks::Kind::voice;
                    q.input = 0;
                    if (const auto r = simpletracks::addTrack (s, q); r.ok())
                    {
                        Clip c;
                        c.kind = ClipKind::audio;
                        c.track = r.track;
                        c.length = 4 * ppq;
                        s.playlist.push_back (c);
                    }
                });
                settle();
                line ("ticked " + juce::String ((int) ui.onboardingStateForCheck().ticked) + ", recorded track " + juce::String ((int) onboarding::hasRecordedTrack (offline.getSong())));
                check ("mobile: a voice take ticks the loop and voice items, the card says it's done",
                       onboarding::ticked (ui.onboardingStateForCheck(), onboarding::Item::recordVoice) && ui.next3ForSnapshot() != nullptr
                           && ui.next3ForSnapshot()->findChildWithID ("next3.tutorials") != nullptr
                           && ui.next3ForSnapshot()->findChildWithID ("next3.tutorials")->isVisible());
                if (auto* n = ui.next3ForSnapshot())
                    write (*n, n->getLocalBounds(), "next3-mobile-done");
                // × hides it; Help › Getting started brings it back
                if (auto* n = ui.next3ForSnapshot())
                    if (auto* close = dynamic_cast<juce::Button*> (n->findChildWithID ("next3.close")))
                        close->triggerClick();
                settle();
                check ("× hides the Next-3 card", ui.next3ForSnapshot() == nullptr && ui.onboardingStateForCheck().next3Hidden);
                ui.showGettingStarted();
                settle();
                check ("Help › Getting started shows it again", ui.next3ForSnapshot() != nullptr && ! ui.onboardingStateForCheck().next3Hidden);
            }
        }

        // --- the empty Simple timeline (New-track card closed) and the empty clip grid ---------------------------------------
        fresh (layoutstyle::Style::simple);
        if (auto* simple = ui.getSimpleWorkflowForSnapshot())
        {
            simple->hideChooser();
            if (auto* tour = simple->tourForSnapshot())
                tour->hideWelcome();
            ui.updateOnboarding();
            settle();
            check ("Simple: an empty song with its New-track card closed shows the hint", hintKind() == onboarding::EmptyHint::simple);
            whole ("empty-simple");
            if (auto* hint = ui.emptyHintForSnapshot())
                write (*hint, hint->getLocalBounds(), "empty-simple-card");
            simple->showChooser();
            ui.updateOnboarding();
            settle();
            check ("Simple: no second hint while the New-track card shows", hintKind() == onboarding::EmptyHint::none);
        }
        fresh (layoutstyle::Style::clipLaunch);
        check ("Clip-launch: the empty grid shows its hint", hintKind() == onboarding::EmptyHint::clipGrid);
        whole ("empty-cliplaunch");
        if (auto* hint = ui.emptyHintForSnapshot())
            write (*hint, hint->getLocalBounds(), "empty-cliplaunch-card");

        // --- the Help menu in the three layouts -----------------------------------------------------------------------------
        for (const auto style : { layoutstyle::Style::pattern, layoutstyle::Style::clipLaunch, layoutstyle::Style::simple })
        {
            line (juce::String ("help menu: ") + layoutstyle::id (style));
            fresh (style);
            const auto names = ui.getMenuNamesForSnapshot();
            const auto help = names.indexOf (tr ("Help"));
            line ("menus: " + names.joinIntoString (", ") + " -> " + juce::String (help));
            juce::StringArray items;
            const auto helpMenu = help >= 0 ? ui.getMenuForSnapshot (help) : juce::PopupMenu(); // (the iterator keeps a reference)
            for (juce::PopupMenu::MenuItemIterator it (helpMenu); it.next();)
                items.add (it.getItem().text);
            check (juce::String ("Help has Welcome..., the first-song tour and Getting started in the ") + layoutstyle::id (style) + " layout",
                   items.contains (tr ("Welcome...")) && items.contains (tr ("Getting started"))
                       && (items.contains (tr ("First-song tour"))
                           || items.joinIntoString ("\n").contains (tr ("Continue the first-song tour (step {0} of {1})", { "1", juce::String (tour::stepCount) }).upToFirstOccurrenceOf ("(", false, false))));
            if (help >= 0)
                saveMenus (juce::StringArray { tr ("Help") }, std::vector<juce::PopupMenu> { helpMenu },
                           prefix + "-help-" + layoutstyle::id (style) + ".png");
        }
        // Help › First-song tour from the Pattern layout: the Simple layout, the tour
        line ("tour from Pattern");
        fresh (layoutstyle::Style::pattern);
        ui.startFirstSongTourAnywhere();
        settle();
        check ("Help › First-song tour from the Pattern layout opens the Simple layout and starts the tour",
               ui.getLayoutStyle() == layoutstyle::Style::simple && ui.getSimpleWorkflowForSnapshot() != nullptr && ui.getSimpleWorkflowForSnapshot()->tourRunning());
        if (auto* simple = ui.getSimpleWorkflowForSnapshot())
            if (auto* tour = simple->tourForSnapshot())
                tour->pause();

        // --- the tips: Play in Song mode on an empty Playlist; once a session ----------------------------------------------
        line ("tips");
        fresh (layoutstyle::Style::pattern);
        offline.editSong ([] (Song& s) { s.mode = PlayMode::song; });
        settle();
        ui.runActionForSnapshot ("transport.playStop");
        settle();
        check ("Play in Song mode with an empty Playlist: the tip", ui.tipForSnapshot() != nullptr && ui.tipForSnapshot()->getTip() == onboarding::Tip::songModeEmpty);
        whole ("tip-songmode");
        if (auto* tip = ui.tipForSnapshot())
            write (*tip, tip->getLocalBounds(), "tip-songmode-card");
        if (auto* tip = ui.tipForSnapshot())
            if (auto* b = dynamic_cast<juce::Button*> (tip->findChildWithID ("tip.action")))
                b->triggerClick();
        settle();
        check ("its Switch to Pattern: Pattern mode, the tip closes", offline.getSong().mode == PlayMode::pattern && ui.tipForSnapshot() == nullptr);
        ui.stopForCheck();
        offline.editSong ([] (Song& s) { s.mode = PlayMode::song; });
        ui.runActionForSnapshot ("transport.playStop");
        settle();
        check ("a second signal in the same session: no second tip", ui.tipForSnapshot() == nullptr);
        ui.stopForCheck();

        // the Record tip, in a session of its own (a second window: its own once-a-session gate)
        {
            MainComponent second (offline, false);
            second.setSize (1920, 1080);
            second.setVisible (true);
            second.setOnboardingForSnapshot (true);
            second.showDesktopForSnapshot (false);
            auto o = offline.getRecordOptions();
            const auto saved = o;
            o.recordAudio = true;
            o.recordNotes = false;
            o.askWhatToRecord = false;
            o.startsPlayback = false;
            offline.setRecordOptions (o);
            second.runActionForSnapshot ("transport.record");
            settle();
            check ("Record (audio) with no input: the tip", second.tipForSnapshot() != nullptr && second.tipForSnapshot()->getTip() == onboarding::Tip::recordNoInput);
            settle();
            write (second, second.getLocalBounds(), "tip-record");
            if (auto* tip = second.tipForSnapshot())
                write (*tip, tip->getLocalBounds(), "tip-record-card");
            offline.setRecording (false);
            offline.setRecordOptions (saved);
            second.closeTip();
        }

        // --- the hints off (Help › Show tips and hints) ----------------------------------------------------------------------
        fresh (layoutstyle::Style::pattern);
        ui.setOnboardingForSnapshot (false);
        check ("hints off: no empty-state hint", ui.emptyHintForSnapshot() == nullptr);
        line (allOk ? "result=OK" : "result=FAILED");
        juce::File (prefix + "-checks.txt").replaceWithText (checks);
        return allOk;
    }

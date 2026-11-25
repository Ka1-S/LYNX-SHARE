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

        // ---- crash report ef495e29 (0.1.31, Windows): NativeEffectEditor::paint -> pluginheader::describe. The help line
        //      under LYNX's own panels named the control under the mouse with effectInfo().params[p], but a state-backed
        //      control or a hidden parameter with a control has an index past the plain parameters
        {
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            offline.dispatchPendingMessages();
            int extended = 0, failed = 0, kinds = 0;
            auto kindsToCheck = plugui::ownLayoutKinds();
            kindsToCheck.push_back (EffectKind::lynxTune);
            for (const auto kind : kindsToCheck)
            {
                offline.editSong ([kind] (Song& s) {
                    if (s.mixer.size() > 1)
                        s.mixer[1].effects = { makeEffectSlot (kind) };
                });
                offline.dispatchPendingMessages();
                const auto report = helpLineSelfTest (offline, 1, 0, (int) plugui::layoutFor (kind).pages.size());
                for (const auto& line : juce::StringArray::fromLines (report))
                {
                    if (line.contains (": OK"))
                        ++extended;
                    if (line.contains ("FAIL"))
                    {
                        ++failed;
                        log << "     " << line << "\n";
                    }
                }
                ++kinds;
            }
            check ("Native editors: the help line under every control of " + juce::String (kinds) + " panels paints, " + juce::String (extended)
                       + " of them past the plain parameters, none failed",
                   failed == 0 && kinds > 0 && extended > 0);
        }
        // ---- crash report 00d9f624 (0.1.31.1, Linux): the Simple dock's Drum Machine page named the sound under the mouse
        //      with song.channels[rows[...]] while its rows still listed the sounds of the song before a change (opening
        //      a project, deleting sounds): the page reads its rows again first now
        {
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            MainComponent ui (offline, false);
            ui.setSize (1600, 900);
            ui.setLayoutStyle (layoutstyle::Style::simple);
            offline.dispatchPendingMessages();
            auto* simple = ui.getSimpleWorkflowForSnapshot();
            SimpleDock* dock = nullptr;
            int drums = -1;
            if (simple != nullptr)
            {
                simple->addDrumsForSnapshot ({}, true);
                offline.dispatchPendingMessages();
                simple->hideChooser();
                for (int t = 0; t < (int) offline.getSong().playlistTracks.size() && drums < 0; ++t)
                    if (simpletracks::kindOf (offline.getSong(), t) == simpletracks::Kind::drums)
                        drums = t;
                if (drums >= 0)
                    ui.getPlaylistForSnapshot().selectTrack (drums);
                offline.dispatchPendingMessages();
                dock = simple->dockForSnapshot();
            }
            const auto rowsBefore = dock != nullptr ? dock->stepsRowsForCheck() : 0;
            check ("Simple dock: the Drum Machine page lists the kit (" + juce::String (rowsBefore) + " rows)", dock != nullptr && rowsBefore > 0);
            if (dock != nullptr && rowsBefore > 0)
            {
                // the song loses its sounds; no message runs before the mouse moves over the top row's first step
                offline.editSong ([] (Song& s) {
                    std::vector<int> all;
                    for (int c = 0; c < (int) s.channels.size(); ++c)
                        all.push_back (c);
                    removeChannels (s, all);
                });
                dock->stepsHoverCellForCheck (rowsBefore - 1, 0);
                if (auto* page = dock->stepsComponentForSnapshot())
                {
                    juce::Image image (juce::Image::ARGB, juce::jmax (1, page->getWidth()), juce::jmax (1, page->getHeight()), true);
                    juce::Graphics g (image);
                    page->paintEntireComponent (g, false);
                }
                check ("Simple dock: after the sounds went, the page's rows are the song's (" + juce::String (dock->stepsRowsForCheck()) + ")",
                       dock->stepsRowsForCheck() == (int) simplesteps::kitRows (offline.getSong(), drums).size());
                offline.dispatchPendingMessages();
            }
            // (the layout style is the app's setting: back to the Pattern layout for the checks that follow)
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.dispatchPendingMessages();
        }
        // ---- crash reports 52879e1b / a49ceeeb (0.1.29, Windows): DetailView::DeviceChain::paintBody named an effect
        //      card's knobs with effectInfo(kind).params[knob] while the card was a step behind the song (the slot's
        //      effect just replaced by a hosted plugin, which has no plain parameters)
        {
            const juce::ScopedValueSetter<bool> detailShown (DetailView::shownSetting, true), devices (DetailView::devicesPage, true);
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (1600, 1000);
            ui.setVisible (true);
            ui.setClipLaunchWorkflow (true);
            ui.showPanelsForSnapshot ({ workspace::Panel::playlist });
            ui.setClipGridView (PlaylistArea::GridView::song);
            offline.editSong ([] (Song& s) {
                s.channels.resize (1);
                s.channels[0].name = "Keys";
                s.channels[0].instrument = InstrumentKind::synth;
                s.channels[0].mixerTrack = 2;
                if (s.mixer.size() < 4)
                    s.mixer.resize (4);
                s.mixer[2].name = "Keys";
                s.mixer[2].effects = { makeEffectSlot (EffectKind::filter) };
                s.playlistTracks.resize (std::max<std::size_t> (s.playlistTracks.size(), 2));
                s.playlistTracks[0].name = "Keys";
                s.playlistTracks[0].mode = PlaylistTrackMode::instrument;
                s.playlistTracks[0].channel = 0;
            }, false);
            offline.dispatchPendingMessages();
            ui.resized();
            auto* detail = ui.getDetailViewForSnapshot();
            check ("Device View: the chain shows the instrument and its effect", detail != nullptr && (detail->showSelection (0, 0), offline.dispatchPendingMessages(), detail->cardCountForCheck() >= 2));
            if (detail != nullptr)
            {
                offline.editSong ([] (Song& s) {
                    EffectSlot fx;
                    fx.kind = EffectKind::plugin;
                    fx.plugin.id = "lynx-qa190rb-check-plugin";
                    fx.plugin.name = "QA plugin";
                    s.mixer[2].effects = { fx };
                });
                // painted before any message lays the cards out again
                juce::Image image (juce::Image::ARGB, juce::jmax (1, detail->getWidth()), juce::jmax (1, detail->getHeight()), true);
                {
                    juce::Graphics g (image);
                    detail->paintEntireComponent (g, false);
                }
                offline.dispatchPendingMessages();
                check ("Device View: an effect card paints after its slot became a plugin", detail->cardCountForCheck() >= 2);
                // (UI monkey seed 104, ASan: the chain's context panel named a channel the song no longer has)
                offline.editSong ([] (Song& s) { removeChannels (s, { 0 }); });
                {
                    juce::Graphics g (image);
                    detail->paintEntireComponent (g, false);
                }
                offline.dispatchPendingMessages();
                check ("Device View: the chain paints right after its channel was deleted", offline.getSong().channels.empty());
            }
            // (the layout style is the app's setting: back to the Pattern layout for the checks that follow)
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.dispatchPendingMessages();
        }
        // ---- UI monkey test, seed 101 (sanitizer build): heap-use-after-free in the Device View. A custom card (a Rack)
        //      asked for a new layout from inside its own resize; the layout replaced that very card (its channel's kind
        //      had changed) and deleted it under its running code. Replaced cards are deleted on the next message now
        {
            struct QaCard final : public DetailCard
            {
                QaCard*& last;
                bool& deletedInside;
                bool inside = false;
                QaCard (QaCard*& l, bool& d) : last (l), deletedInside (d) { last = this; }
                ~QaCard() override
                {
                    deletedInside = deletedInside || inside;
                    if (last == this)
                        last = nullptr;
                }
                int preferredWidth (int) const override { return 140; }
                void poke()
                {
                    inside = true;
                    if (onPreferredWidthChanged)
                        onPreferredWidthChanged(); // (as a Rack card does when its rack view resizes)
                    inside = false;
                }
            };
            QaCard* lastCard = nullptr;
            bool deletedInside = false;
            auto savedFactory = DetailView::instrumentCardFactory;
            {
                const juce::ScopedValueSetter<bool> detailShown (DetailView::shownSetting, true), devices (DetailView::devicesPage, true);
                AudioEngine offline (false);
                MainComponent ui (offline, false);
                ui.setSize (1600, 1000);
                ui.setVisible (true);
                ui.setClipLaunchWorkflow (true);
                ui.showPanelsForSnapshot ({ workspace::Panel::playlist });
                ui.setClipGridView (PlaylistArea::GridView::song);
                offline.editSong ([] (Song& s) {
                    s.channels.resize (1);
                    s.channels[0].name = "Keys";
                    s.channels[0].instrument = InstrumentKind::synth;
                    s.channels[0].mixerTrack = 2;
                    if (s.mixer.size() < 4)
                        s.mixer.resize (4);
                    s.playlistTracks.resize (std::max<std::size_t> (s.playlistTracks.size(), 2));
                    s.playlistTracks[0].mode = PlaylistTrackMode::instrument;
                    s.playlistTracks[0].channel = 0;
                }, false);
                offline.dispatchPendingMessages();
                ui.resized();
                // (set here: the window installs the racks' factories as it is made; a kind change makes the card)
                savedFactory = DetailView::instrumentCardFactory;
                DetailView::instrumentCardFactory = [&lastCard, &deletedInside] (AudioEngine&, int, const ChannelDef&) {
                    return std::make_unique<QaCard> (lastCard, deletedInside);
                };
                offline.editSong ([] (Song& s) { s.channels[0].instrument = InstrumentKind::kick; }, false);
                offline.dispatchPendingMessages();
                auto* detail = ui.getDetailViewForSnapshot();
                if (detail != nullptr)
                {
                    detail->showSelection (0, 0);
                    offline.dispatchPendingMessages();
                    detail->resized();
                }
                auto* card = lastCard;
                check ("Device View: a custom instrument card is made", detail != nullptr && card != nullptr);
                if (card != nullptr)
                {
                    // the channel's kind changes, then the card asks for a layout from its own code
                    offline.editSong ([] (Song& s) { s.channels[0].instrument = InstrumentKind::sampler; }, false);
                    card->poke();
                    check ("Device View: a card replaced while it asked for the layout is not deleted under its own code", ! deletedInside);
                }
                // (the layout style is the app's setting: back to the Pattern layout for the checks that follow)
                ui.setLayoutStyle (layoutstyle::Style::pattern);
                offline.dispatchPendingMessages();
            }
            DetailView::instrumentCardFactory = savedFactory;
        }

        // ---- UI monkey test, seed 107 (sanitizer build): heap-use-after-free in Settings' find bar. Stepping to a match
        //      on another page switched the tab; the page shown gets the look and feel again and its sliders make their
        //      +/- buttons anew, so a match that was such a button was gone. The search runs again on the page shown
        {
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            auto panel = makeSettingsPanel (offline, SettingsPage::audio, false);
            panel->setSize (900, 700);
            int steps = 0, found = 0;
            for (char c = 'a'; c <= 'z'; ++c)
            {
                const auto first = settingsFindForSnapshot (*panel, juce::String::charToString ((juce::juce_wchar) c));
                found += first.matches;
                for (int k = 1; k <= juce::jmin (24, first.matches); ++k, ++steps)
                    settingsFindForSnapshot (*panel, juce::String::charToString ((juce::juce_wchar) c), k);
            }
            check ("Settings: stepping through the matches of a..z over every page (" + juce::String (steps) + " steps, " + juce::String (found)
                       + " matches) keeps every match it shows alive",
                   steps > 0);
        }

        // ---- UI monkey test, seed 113 (sanitizer build): heap-buffer-overflow in the Session mixer's paint. Its send rows
        //      are laid out from the song; a row's insert the song no longer has (inserts went, another project) was
        //      read as mixer[insert].liveReturn before the next layout
        {
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (1600, 1000);
            ui.setVisible (true);
            ui.setClipLaunchWorkflow (true);
            ui.setClipGridView (PlaylistArea::GridView::grid);
            const auto inserts = offline.getSong().mixer.size();
            offline.editSong ([inserts] (Song& s) {
                s.mixer.resize (inserts + 2);
                s.mixer[inserts].name = "A Return";
                s.mixer[inserts].liveReturn = true;
                s.mixer[inserts + 1].name = "B Return";
                s.mixer[inserts + 1].liveReturn = true;
            }, false);
            offline.dispatchPendingMessages();
            ui.resized();
            auto* grid = ui.getClipGridForSnapshot();
            auto* mixer = grid != nullptr ? clipGridMixer (*grid) : nullptr;
            check ("Session mixer: shown with two returns", mixer != nullptr && mixer->getHeight() > 0);
            if (mixer != nullptr)
            {
                offline.editSong ([inserts] (Song& s) { s.mixer.resize (inserts); }); // (the returns go; no message runs before the paint)
                juce::Image image (juce::Image::ARGB, juce::jmax (1, mixer->getWidth()), juce::jmax (1, mixer->getHeight()), true);
                {
                    juce::Graphics g (image);
                    mixer->paintEntireComponent (g, false);
                }
                offline.dispatchPendingMessages();
                check ("Session mixer: paints right after its return inserts went", offline.getSong().mixer.size() == inserts);
            }
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.dispatchPendingMessages();
        }

        // ---- UI monkey test, seed 15 (release build): the Tracker painted its cells from a layout built before an undo,
        //      whose notes indexed past the pattern's (TrackerComponent::paint -> paintCell -> tracker::fxActive). Cells
        //      of a layout a step behind the song show nothing now, and a key or a click builds the layout again first
        {
            auto song = Song::makeDefault();
            song.currentPattern = 0;
            if (! song.patterns.empty() && ! song.channels.empty())
                for (int k = 0; k < 16; ++k)
                {
                    Note n;
                    n.channel = 0;
                    n.key = 60 + (k % 12);
                    n.start = (std::int64_t) k * (ppq / 4);
                    n.length = ppq / 8;
                    n.velocity = 0.8f;
                    song.patterns[0].notes.push_back (n);
                }
            AudioEngine offline (false);
            offline.setSong (song);
            offline.dispatchPendingMessages();
            TrackerComponent tracker (offline);
            tracker.setSize (1280, 760);
            offline.dispatchPendingMessages();
            const auto before = tracker.layoutForCheck().placed.size();
            // undo-like: the pattern loses its notes; the tracker paints and takes a key before the change message
            offline.editSong ([] (Song& s) {
                if (! s.patterns.empty())
                    s.patterns[0].notes = {}; // (its storage released: an old index reads nothing that is there)
            });
            juce::Image image (juce::Image::ARGB, 1280, 760, true);
            {
                juce::Graphics g (image);
                tracker.paintEntireComponent (g, false);
            }
            tracker.keyPressed (juce::KeyPress (juce::KeyPress::downKey));
            const auto afterKey = tracker.layoutForCheck().placed.size();
            offline.dispatchPendingMessages();
            check ("Tracker: paints while its layout is a step behind the song, and a key builds it again first (" + juce::String ((int) before)
                       + " notes, then " + juce::String ((int) afterKey) + ")",
                   before >= 16 && afterKey == 0);
        }

        // ---- UI monkey test, seed 59 (release build): "double free or corruption" after an undo during a Piano roll drag.
        //      A drag of notes holds their indices from the press; the song changed under it and the move wrote past the
        //      pattern's notes. Such a drag ends now
        {
            auto song = Song::makeDefault();
            song.currentPattern = 0;
            if (! song.patterns.empty() && ! song.channels.empty())
            {
                song.patterns[0].notes.clear();
                for (int k = 0; k < 8; ++k)
                {
                    Note n;
                    n.channel = 0;
                    n.key = 60 + k;
                    n.start = (std::int64_t) k * ppq;
                    n.length = ppq / 2;
                    song.patterns[0].notes.push_back (n);
                }
            }
            AudioEngine offline (false);
            offline.setSong (song);
            offline.setSelectedChannel (0);
            MainComponent ui (offline, false);
            ui.setSize (1600, 1000);
            ui.showPanelsForSnapshot ({ workspace::Panel::pianoRoll });
            offline.dispatchPendingMessages();
            ui.resized();
            auto& pr = ui.getPianoRollForSnapshot();
            pr.runAction ("pianoRoll.zoomAll"); // (the pattern's notes in view)
            pr.scrollToNotes();
            offline.dispatchPendingMessages();
            const auto target = pr.firstVisibleNoteForSnapshot();
            check ("Piano roll: a note of the channel on screen (" + juce::String (target.index) + ")", target.index >= 0);
            if (target.index >= 0)
            {
                const auto at = target.bounds.getCentre();
                pr.pressForSnapshot (at);
                // the song changes under the drag (Ctrl+Z while the button is down): the pattern's notes are gone
                offline.editSong ([] (Song& s) {
                    if (! s.patterns.empty())
                        s.patterns[0].notes = {};
                });
                pr.dragForSnapshot (at + juce::Point<int> (60, -20));
                pr.dragForSnapshot (at + juce::Point<int> (120, -40));
                pr.releaseForSnapshot (at + juce::Point<int> (120, -40));
                offline.dispatchPendingMessages();
                check ("Piano roll: a note drag whose notes went with the song's change ends, nothing written",
                       ! offline.getSong().patterns.empty() && offline.getSong().patterns[0].notes.empty());
            }
        }

       #if JUCE_LINUX
        // ---- crash reports 0a4071b9 / 83a7f664 (0.1.31.1, Linux): JUCE's event loop gathers the descriptor callbacks that
        //      are ready and calls them in turn; when one of them unregistered another (the X11 events closing a plugin's
        //      last VST3 editor take JUCE's VST3 run loop with it), the gathered copy still ran on its freed owner. JUCE
        //      fix 52 (cmake/LynxPatchJuce.cmake): a callback no longer registered is skipped
        {
            int first[2] = { -1, -1 }, second[2] = { -1, -1 };
            if (::pipe (first) == 0 && ::pipe (second) == 0)
            {
                bool secondGone = false, calledAfterUnregister = false, firstCalled = false;
                juce::LinuxEventLoop::registerFdCallback (first[0], [&] (int fd) {
                    char c;
                    juce::ignoreUnused (::read (fd, &c, 1));
                    firstCalled = true;
                    if (! secondGone)
                    {
                        juce::LinuxEventLoop::unregisterFdCallback (second[0]); // (as a run loop's destructor does)
                        secondGone = true;
                    }
                });
                juce::LinuxEventLoop::registerFdCallback (second[0], [&] (int fd) {
                    char c;
                    juce::ignoreUnused (::read (fd, &c, 1));
                    calledAfterUnregister = calledAfterUnregister || secondGone;
                });
                juce::ignoreUnused (::write (first[1], "x", 1), ::write (second[1], "x", 1));
                for (int i = 0; i < 20 && ! firstCalled; ++i)
                    juce::detail::dispatchNextMessageOnSystemQueue (true);
                check ("Linux event loop: a descriptor callback unregistered by an earlier one of the same round is not called",
                       firstCalled && ! calledAfterUnregister);
                juce::LinuxEventLoop::unregisterFdCallback (first[0]);
                if (! secondGone)
                    juce::LinuxEventLoop::unregisterFdCallback (second[0]);
            }
            for (const auto fd : { first[0], first[1], second[0], second[1] })
                if (fd >= 0)
                    ::close (fd);
        }
       #endif
        return result;
    }

    // --discord190qushot <folder> [lanes.flp] (round 190qu, Discord #general 11-24 after 0.1.31.1): through the real main
    // window, off screen, at 100 % and 150 % (the keyboard also at 125 %):
    //  1. the Piano roll keyboard at four row heights: no light seam down the black keys (sidthenugget's screenshot);
    //  2. the Clip-launch layout: an Arrangement clip clicked shows its notes in the Clip View (kainproctor);
    //  3. an FL project's automation clips moved into lanes (Pattern and Clip-launch layouts), then turned back into clips;
    //  4. the Simple layout: the drop strip and the chooser's start row on an empty project only;
    //  5. FL's Split by channel (the live FL 2026 case) and "Convert project to Track-only" on the same project;
    //  6. the pencil on a Track-only instrument track draws a new empty clip each time.
    // <folder>/checks.txt lists what was checked. Included in the app class of Main.cpp.
    bool runDiscordShot190qu (const juce::String& folderPath, const juce::String& lanesProject)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (lynx::ui::showingForShots(), true);
        lynx::ui::motion::enabled() = false;
        const juce::File folder (folderPath);
        folder.createDirectory();
        juce::String checks;
        bool allOk = true;
        const auto check = [&checks, &allOk] (const juce::String& what, bool ok) {
            checks << (ok ? "ok   " : "FAIL ") << what << "\n";
            allOk = allOk && ok;
        };
        const auto note = [&checks] (const juce::String& text) { checks << "     " << text << "\n"; };
        using namespace lynx;
        const auto save = [&folder] (juce::Component& c, juce::Rectangle<int> area, const juce::String& name) {
            for (const auto scale : { 1.0f, 1.5f })
                savePng (c.createComponentSnapshot (area, true, scale), folder.getChildFile (name + (scale > 1.0f ? "@150.png" : ".png")).getFullPathName());
        };
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto mouse = [&source] (juce::Component& c, juce::Point<int> p, juce::ModifierKeys mods, int clicks) {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, p.toFloat(), now, clicks, false);
        };
        const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);

        AudioEngine offline (false);
        MainComponent ui (offline, false);
        ui.setSize (1600, 900);
        ui.setVisible (true); // (shown, as in the window: the Clip View docks its editor only while it is on screen)
        ui.setLayoutStyle (layoutstyle::Style::pattern);
        offline.dispatchPendingMessages();

        // --- 1. the keyboard ----------------------------------------------------------------------------------------
        {
            offline.editSong ([] (Song& s) {
                s.patterns.resize (std::max<std::size_t> (1, s.patterns.size()));
                s.currentPattern = 0;
                s.patterns[0].notes = { { 0, ppq, 0, 81 }, { ppq, ppq, 0, 83 }, { 2 * ppq, ppq, 0, 70 } }; // (A5, B5, A#4)
            });
            ui.selectTabForSnapshot (1);
            offline.dispatchPendingMessages();
            auto& roll = ui.getPianoRollForSnapshot();
            // (the black keys' own names off for the measurement: their text is not a seam)
            const auto names = notenames::everyKey.exchange (false);
            int seams = 0, rows = 0;
            for (const auto rowPixels : { 8, 12, 16, 24 })
            {
                roll.setViewForTests (0.0, rowPixels, 78);
                const auto grid = roll.gridAreaForSnapshot();
                const juce::Rectangle<int> keys (0, grid.getY(), PianoRollComponent::keyboardWidth + 40, juce::jmin (grid.getHeight(), 260));
                for (const auto scale : { 1.0f, 1.25f, 1.5f })
                {
                    const auto image = roll.createComponentSnapshot (keys, true, scale);
                    savePng (image, folder.getChildFile ("1-keys-row" + juce::String (rowPixels) + (scale == 1.0f ? juce::String() : "@" + juce::String (juce::roundToInt (scale * 100))) + ".png")
                                        .getFullPathName());
                    // down each black key's middle row: its shade only grows lighter towards the tip (a seam: a pixel lighter
                    // than both neighbours, the white key showing through)
                    const juce::Image::BitmapData px (image, juce::Image::BitmapData::readOnly);
                    const auto blackPx = (int) std::floor ((float) PianoRollComponent::blackKeyWidth * scale) - 1;
                    for (int y = 1; y < image.getHeight() - 1; ++y)
                    {
                        const auto first = px.getPixelColour (1, y);
                        if (first.getPerceivedBrightness() > 0.35f) // (a white key, or the C label)
                            continue;
                        ++rows;
                        for (int x = 2; x < blackPx - 1; ++x)
                        {
                            const auto b = px.getPixelColour (x, y).getPerceivedBrightness();
                            if (b > px.getPixelColour (x - 1, y).getPerceivedBrightness() + 0.03f && b > px.getPixelColour (x + 1, y).getPerceivedBrightness() + 0.03f)
                            {
                                ++seams;
                                break;
                            }
                        }
                    }
                }
            }
            notenames::everyKey.store (names);
            note ("black key rows scanned at 100 / 125 / 150 %, row heights 8 / 12 / 16 / 24: " + juce::String (rows) + ", rows with a seam: " + juce::String (seams));
            check ("1 the black keys have no light seam at any scale or row height", rows > 50 && seams == 0);
            save (roll, roll.getLocalBounds().withHeight (juce::jmin (roll.getHeight(), 520)), "1-pianoroll");
        }

        // a song with two pattern clips and an audio-less automation clip
        const auto twoClips = [] {
            auto s = Song::makeDefault();
            s.playlist.clear();
            s.patterns.resize (2);
            s.patterns[0].name = "Lead";
            s.patterns[0].notes = { { 0, ppq, 0, 72 }, { ppq, ppq, 0, 76 }, { 2 * ppq, 2 * ppq, 0, 79 }, { 4 * ppq, 4 * ppq, 0, 74 } };
            s.patterns[1].name = "Bass";
            s.patterns[1].notes = { { 0, 2 * ppq, 1, 48 }, { 2 * ppq, 2 * ppq, 1, 43 } };
            for (int k = 0; k < 4; ++k)
            {
                Clip c;
                c.kind = ClipKind::pattern;
                c.pattern = k % 2;
                c.track = k % 2;
                c.start = (std::int64_t) (k / 2) * 8 * ppq;
                c.length = 8 * ppq;
                s.playlist.push_back (c);
            }
            s.mode = PlayMode::song;
            return s;
        };

        // --- 2. Clip-launch: an Arrangement clip in the Clip View -----------------------------------------------------
        {
            ui.startNewProjectForSnapshot (twoClips());
            ui.setLayoutStyle (layoutstyle::Style::clipLaunch);
            offline.dispatchPendingMessages();
            ui.showDesktopForSnapshot (false); // (the layout's own panels: section 1 left the Piano roll maximised)
            offline.dispatchPendingMessages();
            ui.setClipGridView (PlaylistArea::GridView::song);
            offline.dispatchPendingMessages();
            auto* detail = ui.getDetailViewForSnapshot();
            check ("2 the Clip-launch layout has its detail view", detail != nullptr);
            if (detail != nullptr)
            {
                detail->setPage (DetailView::Page::clip);
                offline.dispatchPendingMessages();
                check ("2 before: no clip shown (nothing selected)", ! detail->clipEditorDockedForCheck());
                save (ui, ui.getLocalBounds(), "2-cliplaunch-before");
                auto& pl = ui.getPlaylistForSnapshot();
                pl.resized();
                const auto at = pl.clipBoundsForSnapshot (2).getCentre();
                pl.mouseDown (mouse (pl, at, left, 1));
                pl.mouseUp (mouse (pl, at, {}, 1));
                offline.dispatchPendingMessages();
                note ("clicked clip 2 at " + at.toString() + ": the Clip View shows Arrangement clip " + juce::String (detail->arrangementClipForCheck()));
                check ("2 a click on an Arrangement clip shows it in the Clip View with its notes docked",
                       detail->arrangementClipForCheck() >= 0 && detail->clipEditorDockedForCheck());
                save (ui, ui.getLocalBounds(), "2-cliplaunch-arrangement-clip");
                detail->setClipTab (true);
                offline.dispatchPendingMessages();
                save (ui, ui.getLocalBounds(), "2-cliplaunch-arrangement-envelopes");
                detail->setClipTab (false);
                pl.selectClipsForSnapshot ({ 1 });
                offline.dispatchPendingMessages();
                check ("2 selecting one Arrangement clip shows it", detail->arrangementClipForCheck() >= 0 && detail->clipEditorDockedForCheck());
                detail->showSelection (0, 0); // (a click on the grid: the grid's slot again)
                offline.dispatchPendingMessages();
                check ("2 the grid's slot again after a grid click", detail->arrangementClipForCheck() == -1);
            }
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.dispatchPendingMessages();
        }

        // --- 3. automation clips into lanes and back --------------------------------------------------------------------
        if (lanesProject.isNotEmpty() && juce::File (lanesProject).existsAsFile())
        {
            juce::String error;
            const auto ok = offline.importFlpFile (juce::File (lanesProject), error);
            check ("3 the FL project opens: " + juce::File (lanesProject).getFileName(), ok);
            offline.editSong ([] (Song& s) { autolink::setOn (s, true); });
            offline.dispatchPendingMessages();
            autolanes::setShown (true);
            auto& pl = ui.getPlaylistForSnapshot();
            ui.selectTabForSnapshot (2);
            pl.resized();
            pl.zoomAllForSnapshot();
            offline.dispatchPendingMessages();
            const auto before = offline.getSong();
            const auto movable = (int) laneview::movableClips (before).size();
            int clips = 0;
            for (const auto& c : before.playlist)
                clips += c.kind == ClipKind::automation ? 1 : 0;
            note (juce::String (clips) + " automation clips, " + juce::String (movable) + " movable, " + juce::String ((int) before.automations.size()) + " automations");
            save (pl, pl.getLocalBounds(), "3-lanes-pattern-before");
            check ("3 the Playlist menu lists the move", ui.runActionForSnapshot ("playlist.clipsToLanes"));
            offline.dispatchPendingMessages();
            const auto after = offline.getSong();
            int lanes = 0, past = 0;
            for (const auto& t : after.playlistTracks)
                for (const auto& l : t.lanes)
                {
                    ++lanes;
                    past += l.automation >= RackState::maxAutomations ? 1 : 0;
                }
            note (juce::String (lanes) + " lanes after the move, " + juce::String (past) + " past the engine's 1024 slots");
            check ("3 the clips went into lanes the engine plays", movable > 0 && lanes > 0 && past == 0 && laneview::movableClips (after).empty());
            save (pl, pl.getLocalBounds(), "3-lanes-pattern-after");
            ui.setLayoutStyle (layoutstyle::Style::clipLaunch);
            offline.dispatchPendingMessages();
            pl.resized();
            save (ui, ui.getLocalBounds(), "3-lanes-cliplaunch-after");
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.dispatchPendingMessages();
            // the menu items and the way back
            juce::StringArray items;
            workflowextras::addPlaylistMenuItems ([&items] (const char*, const juce::String& text, bool enabled, bool) { items.add (text + (enabled ? "" : " (off)")); });
            note ("Playlist menu: " + items.joinIntoString (" | "));
            check ("3 the menu offers the way back", items.joinIntoString ("|").contains (tr ("Turn automation lanes back into clips ({0})", { juce::String (lanes) })));
            check ("3 lanes back into clips", ui.runActionForSnapshot ("playlist.lanesToClips"));
            offline.dispatchPendingMessages();
            check ("3 no lane left, a clip per lane", laneview::laneCount (offline.getSong()) == 0);
            save (pl, pl.getLocalBounds(), "3-lanes-pattern-back");
            offline.undo();
            offline.undo();
            offline.dispatchPendingMessages();
            check ("3 undo takes both steps back", offline.getSong().playlist == before.playlist && offline.getSong().automations == before.automations);
            autolanes::setShown (false);
        }
        else
            note ("3 skipped: no FL project given");

        // --- 4. Simple: the drop strip and the chooser ------------------------------------------------------------------
        {
            auto empty = Song::makeDefault();
            empty.playlist.clear();
            empty.playlistTracks.clear();
            empty.arrangements.clear();
            ui.startNewProjectForSnapshot (empty);
            ui.setLayoutStyle (layoutstyle::Style::simple);
            offline.dispatchPendingMessages();
            auto* simple = ui.getSimpleWorkflowForSnapshot();
            check ("4 the Simple workflow", simple != nullptr);
            if (simple != nullptr)
            {
                auto& pl = ui.getPlaylistForSnapshot();
                simple->hideChooser();
                pl.resized();
                save (ui, ui.getLocalBounds(), "4-simple-empty");
                auto* chooser = simple->chooserForSnapshot();
                simple->showChooser();
                offline.dispatchPendingMessages();
                const auto startRowShown = [chooser] {
                    bool any = false;
                    for (auto* c : chooser->getChildren())
                        if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->isVisible()
                                                                          && (b->getButtonText() == tr ("Start from a demo song or a template...") || b->getButtonText() == tr ("First-song tour")))
                            any = true;
                    return any;
                };
                check ("4 empty project: the chooser offers a demo song or the tour", startRowShown());
                save (ui, ui.getLocalBounds(), "4-simple-chooser-empty");
                simple->hideChooser();
                offline.editSong ([] (Song& s) {
                    simpletracks::Request q;
                    q.kind = simpletracks::Kind::instrument;
                    q.name = "Keys";
                    ChannelDef synth;
                    synth.instrument = InstrumentKind::synth;
                    q.channel = synth;
                    Pattern notes;
                    notes.notes = { { 0, 2 * ppq, 0, 60 }, { 2 * ppq, 2 * ppq, 0, 64 } };
                    q.pattern = notes;
                    simpletracks::addTrack (s, q);
                });
                offline.dispatchPendingMessages();
                simple->hideChooser();
                pl.resized();
                pl.repaint();
                save (ui, ui.getLocalBounds(), "4-simple-with-track");
                check ("4 with a track: no drop strip, no +", ! simpletracks::dropStripAlways && simpletracks::anyTrack (offline.getSong()));
                simple->showChooser();
                offline.dispatchPendingMessages();
                check ("4 with a track: the chooser only adds tracks", ! startRowShown());
                save (ui, ui.getLocalBounds(), "4-simple-chooser-with-track");
                simple->hideChooser();
                check ("4 the switch", workflowextras::setExtra ("simple.dropStrip", true) && simpletracks::dropStripAlways);
                pl.repaint();
                save (ui, ui.getLocalBounds(), "4-simple-with-track-strip-on");
                workflowextras::setExtra ("simple.dropStrip", false);
            }
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.dispatchPendingMessages();
        }

        // --- 5. Split by channel (FL 2026's live case) and Convert to Track-only ------------------------------------------
        {
            auto s = Song::makeDefault();
            s.playlist.clear();
            s.patterns.assign (1, Pattern {});
            s.patterns[0].name = "Pattern 1";
            const auto kick = 0, hat = std::min<int> (2, (int) s.channels.size() - 1);
            for (const auto step : { 0, 4 })
                s.patterns[0].notes.push_back ({ (std::int64_t) step * ticksPerStep, ticksPerStep, kick, 60 });
            for (const auto step : { 2, 6 })
                s.patterns[0].notes.push_back ({ (std::int64_t) step * ticksPerStep, ticksPerStep, hat, 60 });
            for (const auto bar : { 0, 4 })
            {
                Clip c;
                c.kind = ClipKind::pattern;
                c.pattern = 0;
                c.track = 1;
                c.start = (std::int64_t) bar * s.barTicks();
                c.length = s.barTicks();
                s.playlist.push_back (c);
            }
            ui.startNewProjectForSnapshot (s);
            offline.dispatchPendingMessages();
            ui.selectTabForSnapshot (2);
            auto& pl = ui.getPlaylistForSnapshot();
            pl.resized();
            pl.setViewForSnapshot (120.0, 0, 0);
            save (pl, pl.getLocalBounds().withHeight (360), "5-split-before");
            int created = 0;
            offline.editSong ([&created] (Song& song) { created = splitPatternByChannel (song, 0); });
            offline.dispatchPendingMessages();
            const auto& song = offline.getSong();
            note ("patterns after the split: " + juce::String (song.patterns[0].name) + " | " + juce::String (song.patterns.size() > 1 ? song.patterns[1].name : std::string()));
            check ("5 FL's split: the pattern keeps the Kick and its name, the HiHat a new pattern, nothing placed",
                   created == 1 && song.patterns[0].name == song.channels[(std::size_t) kick].name && song.patterns.size() == 2
                       && song.patterns[1].name == song.channels[(std::size_t) hat].name && song.playlist.size() == 2);
            save (pl, pl.getLocalBounds().withHeight (360), "5-split-after");
            offline.undo();
            offline.dispatchPendingMessages();
            trackonly::ConvertReport report;
            offline.editSong ([&report] (Song& song2) { report = trackonly::convertProject (song2); });
            trackonly::setEnabled (true);
            offline.dispatchPendingMessages();
            pl.resized();
            note ("convert: " + juce::String (report.clipsMoved) + " clips moved, " + juce::String (report.patternsSplit) + " split, " + juce::String (report.tracksMade) + " tracks");
            check ("5 Convert to Track-only: each part on its instrument's track", report.patternsSplit == 1 && report.clipsMoved == 4);
            save (pl, pl.getLocalBounds().withHeight (420), "5-trackonly-converted");

            // --- 6. the pencil on a Track-only instrument track: a new empty clip each click -----------------------------
            const auto kickTracks = trackmodes::instrumentTracksOf (offline.getSong(), kick);
            if (! kickTracks.empty())
            {
                pl.setToolForSnapshot (EditTool::pencil);
                const auto patternsBefore = offline.getSong().patterns.size();
                const auto row = pl.trackRowForSnapshot (kickTracks.front());
                for (const auto bar : { 6, 8 }) // (empty bars of the Kick's track, well inside the view)
                {
                    const juce::Point<int> at ((int) std::lround (pl.xOfTickForSnapshot ((double) bar * offline.getSong().barTicks() + ppq / 2)), row.getCentreY());
                    pl.mouseDown (mouse (pl, at, left, 1));
                    pl.mouseUp (mouse (pl, at, {}, 1));
                    offline.dispatchPendingMessages();
                }
                const auto& after = offline.getSong();
                int empties = 0;
                for (std::size_t p = patternsBefore; p < after.patterns.size(); ++p)
                    empties += after.patterns[p].notes.empty() ? 1 : 0;
                note ("pencil clicks on the Kick's track made " + juce::String ((int) (after.patterns.size() - patternsBefore)) + " patterns, " + juce::String (empties) + " empty");
                check ("6 Track-only: two pencil clicks, two new empty clips", after.patterns.size() == patternsBefore + 2 && empties == 2);
                save (pl, pl.getLocalBounds().withHeight (420), "6-trackonly-pencil");
            }
            trackonly::setEnabled (false);
        }

        checks << (allOk ? "result=OK\n" : "result=FAILED\n");
        folder.getChildFile ("checks.txt").replaceWithText (checks);
        return allOk;
    }

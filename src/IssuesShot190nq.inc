    // --issuesshot190nq <prefix> (round 190nq, issues #193, #225, #226, #227, #228, #229, #230), offscreen through the real
    // components: a narrow Channel rack's title bar, the Mixer's Left / Right, the action search ("?"), FL's "Show fade
    // previews", the ruler click with every tool (LYNX extra), an automation clip whose insert was deleted, and (with
    // LYNX_193_ALS=<set.als>) issue #193's Live set as LYNX opens it. <prefix>-checks.txt lists the checks; pictures @100
    // and @150. Included in the app class of Main.cpp.
    bool runIssuesShot190nq (const juce::String& prefix)
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
        using workspace::Panel;
        ui::motion::enabled() = false;
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        const auto bar = (std::int64_t) 4 * ppq;

        // a test sound: 4 s of a decaying 220 Hz tone, pulsed every half second (a waveform with shape)
        const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx190nq-fade.wav");
        {
            wav.deleteFile();
            juce::WavAudioFormat format;
            std::unique_ptr<juce::OutputStream> out (wav.createOutputStream().release());
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (out.get(), 44100.0, 1, 16, {}, 0));
            if (writer != nullptr)
            {
                out.release();
                juce::AudioBuffer<float> b (1, 44100 * 4);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto t = (double) (i % 22050) / 44100.0;
                    b.setSample (0, i, (float) (0.8 * std::exp (-t * 6.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) i / 44100.0)));
                }
                writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
        }

        AudioEngine engine (false);
        {
            auto song = Song::makeDefault();
            song.playlist.clear();
            song.mixer.resize (std::max<std::size_t> (song.mixer.size(), 12)); // (std::max: juce::jmax<std::size_t> instantiates juce::dsp::SIMDRegister on macOS arm64)
            song.mixer[1].name = "BEAT BUS";
            song.mixer[2].name = "VERB";
            song.mixer[1].sends.push_back ({ 2, 0.5f });
            ChannelDef vox;
            vox.name = "Vocal";
            vox.instrument = InstrumentKind::audioClip;
            vox.samplePath = wav.getFullPathName().toStdString();
            song.channels.push_back (vox);
            song.playlistTracks.resize (12);
            Clip take;
            take.kind = ClipKind::audio;
            take.channel = (int) song.channels.size() - 1;
            take.track = 0;
            take.start = 0;
            take.length = 2 * bar; // (the 4 s sound at 120 BPM)
            take.fadeIn = bar / 4;
            take.fadeOut = bar;
            song.playlist.push_back (take);
            Automation send;
            send.name = "BEAT BUS \xe2\x86\x92 VERB - Send level";
            send.target = { AutomationTargetKind::mixerSend, 1, 2, 0 };
            send.points = { { 0, 0.1f, 0.0f }, { 2 * bar, 0.9f, 0.0f }, { 4 * bar, 0.3f, 0.0f } };
            song.automations.push_back (send);
            Clip a;
            a.kind = ClipKind::automation;
            a.automation = 0;
            a.track = 2;
            a.start = 0;
            a.length = 4 * bar;
            song.playlist.push_back (a);
            engine.setSong (std::move (song));
        }
        MainComponent ui (engine, false);
        ui.setSize (1600, 900);
        engine.dispatchPendingMessages();
        auto& ws = ui.workspaceForTest();

        // ================= #229: a narrow Channel rack =================
        {
            ui.showPanelsForSnapshot ({ Panel::channelRack });
            ws.show (Panel::channelRack);
            auto& frame = ws.getFrame (Panel::channelRack);
            for (const auto width : { 760, 560, 470, 330 })
            {
                frame.setSize (width, 300);
                engine.dispatchPendingMessages();
                // the buttons at the right of the title bar, and every visible title tool's right edge
                int buttonsLeft = width;
                for (auto* c : frame.getChildren())
                    if (auto* b = dynamic_cast<juce::Button*> (c); b != nullptr && b->isVisible() && c->getY() < Workspace::titleHeight && c->getX() > width / 2)
                        buttonsLeft = juce::jmin (buttonsLeft, c->getX());
                int toolsRight = 0, hidden = 0, toolCount = 0;
                juce::Component* tools = nullptr;
                for (auto* c : frame.getChildren())
                    if (c->getNumChildComponents() >= 8 && c->getY() <= 2) // (the rack's title tools)
                        tools = c;
                if (tools != nullptr)
                    for (auto* t : tools->getChildren())
                    {
                        ++toolCount;
                        if (t->isVisible())
                            toolsRight = juce::jmax (toolsRight, tools->getX() + t->getRight());
                        else
                            ++hidden;
                    }
                check ("Channel rack " + juce::String (width) + " px wide: no title tool under the buttons (tools end at " + juce::String (toolsRight)
                           + ", buttons start at " + juce::String (buttonsLeft) + ", " + juce::String (hidden) + " of " + juce::String (toolCount) + " hidden)",
                       tools != nullptr && toolsRight <= buttonsLeft);
                write (frame, { 0, 0, width, Workspace::titleHeight + 40 }, "229-rack-" + juce::String (width));
            }
            frame.setSize (760, 300);
        }

        // ================= #225: Left / Right in the Mixer =================
        {
            ui.showPanelsForSnapshot ({ Panel::mixer });
            ws.show (Panel::mixer);
            ws.activate (Panel::mixer);
            auto& mixer = ui.getMixerForSnapshot();
            mixer.selectTrack (3);
            const auto key = [&] (int code, juce::ModifierKeys mods = {}) { ui.keyPressed (juce::KeyPress (code, mods, 0)); engine.dispatchPendingMessages(); };
            key (juce::KeyPress::rightKey);
            check ("Mixer: Right selects the next insert (3 -> 4)", mixer.getSelectedTrack() == 4);
            key (juce::KeyPress::leftKey);
            key (juce::KeyPress::leftKey);
            check ("Left, Left -> 2", mixer.getSelectedTrack() == 2);
            key (juce::KeyPress::rightKey, juce::ModifierKeys::ctrlModifier);
            check ("Ctrl+Right -> 3 (FL)", mixer.getSelectedTrack() == 3);
            key (juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier);
            check ("Shift+Right -> 4, one track selected (FL)", mixer.getSelectedTrack() == 4 && mixer.getStripsForSnapshot().getSelection().size() == 1);
            const auto names = [&] { juce::String s; for (int t = 1; t < 6; ++t) s << engine.getSong().mixer[(std::size_t) t].name << ","; return s; };
            const auto before = names();
            key (juce::KeyPress::rightKey);
            check ("selecting moves nothing", names() == before && mixer.getSelectedTrack() == 5);
            mixer.selectTrack (0);
            key (juce::KeyPress::leftKey);
            check ("Left on the Master stays there", mixer.getSelectedTrack() == 0);
            key (juce::KeyPress::rightKey, juce::ModifierKeys::altModifier); // (Alt still moves: from the Master nothing happens)
            mixer.selectTrack (4);
            write (ws.getFrame (Panel::mixer), { 0, 0, juce::jmin (1000, ws.getFrame (Panel::mixer).getWidth()), 240 }, "225-mixer-4");
            key (juce::KeyPress::rightKey);
            write (ws.getFrame (Panel::mixer), { 0, 0, juce::jmin (1000, ws.getFrame (Panel::mixer).getWidth()), 240 }, "225-mixer-5");
        }

        // ================= #228: the action search =================
        {
            ui.showPanelsForSnapshot ({ Panel::playlist });
            ws.activate (Panel::playlist);
            ui.keyPressed (juce::KeyPress ('/', juce::ModifierKeys::shiftModifier, '?'));
            engine.dispatchPendingMessages();
            auto* search = ui.actionSearchForSnapshot();
            check ("? opens the action search (Pattern layout)", search != nullptr && search->isVisible());
            if (search != nullptr)
            {
                const auto all = search->matchCount();
                line ("actions listed: " + juce::String (all));
                // (round 190on: the command palette lists the menus, extras, settings and recent projects too)
                check ("every action of the key map is listed", all >= (int) keys::actions().size());
                write (ui, ui.getLocalBounds(), "228-search-all");
                search->setQuery ("metro");
                line ("\"metro\": " + juce::String (search->matchCount()) + " actions, first " + juce::String (search->selectedId()));
                check ("\"metro\" finds the metronome actions", search->matchCount() >= 1 && juce::String (search->selectedId()).containsIgnoreCase ("metronome"));
                write (ui, ui.getLocalBounds(), "228-search-metro");
                search->setQuery ("next mixer");
                check ("\"next mixer\": Select the next mixer track first", search->selectedId() == "mixer.selectNext");
                auto& mixer = ui.getMixerForSnapshot();
                mixer.selectTrack (6);
                const auto ran = search->runSelected();
                engine.dispatchPendingMessages();
                // (the box goes away on the next message loop turn: it is hidden at once)
                check ("Enter runs it (insert 6 -> 7) and closes the search", ran && mixer.getSelectedTrack() == 7
                                                                                 && (ui.actionSearchForSnapshot() == nullptr || ! ui.actionSearchForSnapshot()->isVisible()));
            }
        }

        // ================= #230: FL's "Show fade previews" =================
        {
            ui.showPanelsForSnapshot ({ Panel::playlist });
            ui.selectTabForSnapshot (2);
            engine.dispatchPendingMessages();
            auto& pl = ui.getPlaylistForSnapshot();
            pl.setSize (juce::jmax (pl.getWidth(), 1100), juce::jmax (pl.getHeight(), 500));
            pl.setViewForSnapshot (220.0, 0, 0);
            ui::WaveformCache::getInstance()->find (wav.getFullPathName(), [&engine, &wav] { return engine.getSampleLibrary().load (wav.getFullPathName()); });
            ui::WaveformCache::getInstance()->waitUntilIdle (20000);
            pl.repaint();
            engine.dispatchPendingMessages();
            const auto row = pl.trackRowForSnapshot (0);
            const auto x0 = (int) std::lround (pl.xOfTickForSnapshot (0.0)), x1 = (int) std::lround (pl.xOfTickForSnapshot ((double) (2 * bar)));
            const auto area = juce::Rectangle<int> (0, 0, juce::jmin (pl.getWidth(), x1 + 60), row.getBottom() + 4);
            check ("FL's default: Show fade previews on", PlaylistComponent::showFadePreviews);
            const auto on = pl.createComponentSnapshot (area, true, 1.0f);
            write (pl, area, "230-fade-preview-on");
            PlaylistComponent::showFadePreviews = false;
            pl.repaint();
            const auto off = pl.createComponentSnapshot (area, true, 1.0f);
            write (pl, area, "230-fade-preview-off");
            PlaylistComponent::showFadePreviews = true;
            pl.repaint();
            // the pictures differ inside the fades only (the middle of the clip is the same)
            const auto differs = [&] (int from, int to) {
                int n = 0;
                for (int x = juce::jmax (0, from); x < juce::jmin (area.getWidth(), to); ++x)
                    for (int y = row.getY() + 2; y < row.getBottom() - 2; ++y)
                        n += on.getPixelAt (x, y) != off.getPixelAt (x, y) ? 1 : 0;
                return n;
            };
            const auto inEnd = (int) std::lround (pl.xOfTickForSnapshot ((double) (bar / 4))), outStart = (int) std::lround (pl.xOfTickForSnapshot ((double) bar));
            const auto inFade = differs (x0 + 2, inEnd - 2), middle = differs (inEnd + 3, outStart - 3), outFade = differs (outStart + 2, x1 - 2);
            line ("pixels that change with the option: fade in " + juce::String (inFade) + ", middle " + juce::String (middle) + ", fade out " + juce::String (outFade));
            check ("the waveform follows the fades (it changes under the fades, nowhere else)", inFade > 20 && outFade > 50 && middle == 0);
            // the View menu has FL's item
            auto menu = ui.getPanelMenuForSnapshot (Panel::playlist);
            bool listed = false;
            std::function<void (const juce::PopupMenu&)> walk = [&] (const juce::PopupMenu& m) {
                for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
                {
                    listed = listed || (it.getItem().text == tr ("Show fade previews") && it.getItem().isTicked);
                    if (it.getItem().subMenu != nullptr)
                        walk (*it.getItem().subMenu);
                }
            };
            walk (menu);
            check ("Playlist menu › View › Show fade previews, ticked", listed);

            // ================= #227: a click on the ruler with the Select tool =================
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const auto rulerY = PlaylistComponent::rulerTop + PlaylistComponent::timelineHeight / 2;
            const auto click = [&] (int x) {
                const juce::Point<float> p ((float) x, (float) rulerY);
                const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
                pl.mouseDown (juce::MouseEvent (source, p, left, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pl, &pl, now, p, now, 1, false));
                pl.mouseUp (juce::MouseEvent (source, p, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pl, &pl, now, p, now, 1, false));
                engine.dispatchPendingMessages();
                return (std::int64_t) std::llround (pl.playheadTickForSnapshot());
            };
            pl.setToolForSnapshot (EditTool::pencil);
            const auto x2 = (int) std::lround (pl.xOfTickForSnapshot ((double) (2 * bar)));
            const auto x3 = (int) std::lround (pl.xOfTickForSnapshot ((double) (3 * bar)));
            const auto drawTool = click (x2);
            line ("Draw tool: ruler click at bar 3 -> tick " + juce::String (drawTool));
            pl.setToolForSnapshot (EditTool::lasso);
            const auto selectOff = click (x3);
            check ("Select tool, extra off (FL): the song position stays", selectOff == drawTool && drawTool > 0);
            workflowextras::setExtra ("playlist.rulerSeeksAnyTool", true);
            const auto selectOn = click (x3);
            check ("Select tool, extra on: the song position moves to the click", selectOn > drawTool);
            workflowextras::setExtra ("playlist.rulerSeeksAnyTool", false);
            pl.setToolForSnapshot (EditTool::pencil);
            check ("the extra is saved with the UI options", workflowextras::uiOptionsText().contains ("rulerSeeksAnyTool=0"));
            auto page = makeExtrasPage (engine, {}, false);
            page->setSize (680, 4200);
            juce::Component* found = nullptr;
            std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
                if (auto* b = dynamic_cast<juce::ToggleButton*> (&c); b != nullptr && b->getButtonText() == tr ("A click on the ruler moves the song position with every tool"))
                    found = b;
                for (auto* child : c.getChildren())
                    find (*child);
            };
            find (*page);
            check ("Settings › LYNX extras lists it, off", found != nullptr && ! dynamic_cast<juce::ToggleButton*> (found)->getToggleState());
            if (found != nullptr)
            {
                const auto r = page->getLocalArea (found, found->getLocalBounds());
                write (*page, juce::Rectangle<int> (0, r.getY() - 40, page->getWidth(), 120), "227-extras");
            }

            // ================= #226: the automation of a deleted insert =================
            const auto clipArea = juce::Rectangle<int> (0, pl.trackRowForSnapshot (2).getY() - 2, juce::jmin (pl.getWidth(), x1 + 60), pl.trackRowForSnapshot (2).getHeight() + 4);
            write (pl, clipArea, "226-before");
            engine.editSongStructural ([] (Song& s) { mixerops::deleteTrack (s, 2); });
            engine.dispatchPendingMessages();
            pl.repaint();
            const auto& a = engine.getSong().automations.front();
            check ("deleting VERB: the send's clip controls nothing and says so (\"" + juce::String::fromUTF8 (a.name.c_str()) + "\")",
                   a.target.kind == AutomationTargetKind::none && juce::String::fromUTF8 (a.name.c_str()).endsWith ("(target deleted)"));
            write (pl, clipArea, "226-after");
            engine.undo();
            engine.dispatchPendingMessages();
            check ("undo brings the track and the clip's target and name back",
                   engine.getSong().mixer[2].name == "VERB" && engine.getSong().automations.front().target.kind == AutomationTargetKind::mixerSend
                       && ! juce::String::fromUTF8 (engine.getSong().automations.front().name.c_str()).contains ("deleted"));
        }

        // ================= #218 follow-up: Enter with the Browser focused =================
        {
            ui.showPanelsForSnapshot ({ Panel::playlist, Panel::browser });
            ws.show (Panel::browser);
            auto& browser = ui.getBrowserForSnapshot();
            browser.waitForListing (5000);
            // a click inside the Browser (its tree) focuses the Browser panel, as FL's window
            auto& frame = ws.getFrame (Panel::browser);
            juce::Component* inside = &browser;
            while (inside->getNumChildComponents() > 0 && inside->getChildComponent (0)->isVisible())
                inside = inside->getChildComponent (0);
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const auto p = juce::Point<float> (6.0f, 6.0f);
            ws.activate (Panel::playlist);
            frame.mouseDown (juce::MouseEvent (source, p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, inside, inside, now, p, now, 1, false));
            check ("a click in the docked Browser makes it the focused panel", ws.getFocused() == Panel::browser);
            line ("Browser selection: " + browser.getSelectedFile().getFullPathName() + " (row " + juce::String (browser.selectedRowForSnapshot()) + ")");
            const auto maxBefore = ws.isMaximised (Panel::playlist);
            const auto rowsBefore = browser.rowCountForSnapshot();
            browser.selectRowIndexForSnapshot (0); // "Current project", a folder
            const auto open = browser.rowCountForSnapshot();
            ui.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
            engine.dispatchPendingMessages();
            const auto afterFirst = browser.rowCountForSnapshot();
            ui.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
            engine.dispatchPendingMessages();
            const auto afterSecond = browser.rowCountForSnapshot();
            line ("Browser rows: " + juce::String (rowsBefore) + ", selected folder: " + juce::String (open) + ", Enter: " + juce::String (afterFirst) + ", Enter: " + juce::String (afterSecond));
            check ("Enter in the Browser opens / closes the selected folder (FL 2026)", afterFirst != open && afterSecond == open);
            check ("Enter in the Browser leaves the Playlist as it was (not maximised / restored)", ws.isMaximised (Panel::playlist) == maxBefore);
            ws.activate (Panel::playlist);
            ui.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
            check ("Enter with the Playlist focused still maximises / restores it (FL)", ws.isMaximised (Panel::playlist) != maxBefore);
            ui.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
            write (ws.getFrame (Panel::browser), ws.getFrame (Panel::browser).getLocalBounds().withHeight (300), "218-browser");
        }

        // ================= #222 follow-up: the big track icon with the insert effects column =================
        {
            AudioEngine e2 (false);
            {
                auto song = Song::makeDefault();
                song.linkTracksToMixer = true;
                song.playlistTracks.resize (6);
                for (int t = 0; t < 3; ++t)
                {
                    auto& tr = song.playlistTracks[(std::size_t) t];
                    tr.mode = PlaylistTrackMode::audio;
                    tr.mixerTrack = t + 1;
                    tr.icon = 5; // FL's kick icon
                    tr.height = 112;
                    tr.name = "Drums " + std::to_string (t + 1);
                }
                e2.setSong (std::move (song));
            }
            MainComponent u2 (e2, false);
            u2.setSize (1600, 900);
            u2.showPanelsForSnapshot ({ Panel::playlist });
            u2.selectTabForSnapshot (2);
            e2.dispatchPendingMessages();
            auto& pl = u2.getPlaylistForSnapshot();
            pl.setSize (juce::jmax (pl.getWidth(), 1100), juce::jmax (pl.getHeight(), 500));
            for (const auto inserts : { false, true })
            {
                if (PlaylistComponent::showTrackInserts != inserts)
                    u2.runActionForSnapshot ("playlist.trackInserts");
                e2.dispatchPendingMessages();
                const auto header = pl.headerWidthForSnapshot();
                write (pl, { 0, pl.trackRowForSnapshot (0).getY(), header + 4, pl.trackRowForSnapshot (1).getBottom() - pl.trackRowForSnapshot (0).getY() },
                       inserts ? "222-icon-inserts" : "222-icon-plain");
                line ("header width " + juce::String (header) + (inserts ? " with" : " without") + " the insert effects column");
            }
            u2.runActionForSnapshot ("playlist.trackInserts");
        }

        // ================= Discord 11-18: the Missing files window of a project that is not from FL =================
        for (const auto fromFl : { true, false })
        {
            MissingFilesInfo info;
            info.projectName = fromFl ? "Imported.flp" : "try_lynx_its_cool";
            info.fromFlStudio = fromFl;
            info.plugins = { MissingPlugin { "TR5 Saturator X", { "Master" }, "Nothing: the slot keeps the plugin's settings until it is installed", {} } };
            auto panel = makeMissingFilesPanel (info, 1);
            panel->setSize (760, 420);
            juce::String text;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
                if (auto* l = dynamic_cast<juce::Label*> (&c))
                    text << l->getText() << "\n";
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (*panel);
            const auto says = text.contains ("as in FL Studio");
            check (fromFl ? "Missing files, an FL project: \"will not sound as in FL Studio\" (as before)" : "Missing files, a LYNX project: no \"as in FL Studio\"",
                   fromFl ? says : ! says && text.contains ("when the project was saved"));
            write (*panel, panel->getLocalBounds().withHeight (140), fromFl ? "d1120-missing-flp" : "d1120-missing-lynx");
        }

        // ================= #193: the reporter's Live set =================
        // (and with LYNX_PROJECT_190NQ=<file>: any other project the same way, Discord 11-18's .lynx, tag "d1120")
        for (const auto& [als, tag] : { std::pair { juce::File (juce::SystemStats::getEnvironmentVariable ("LYNX_193_ALS", {})), juce::String ("193") },
                                        std::pair { juce::File (juce::SystemStats::getEnvironmentVariable ("LYNX_PROJECT_190NQ", {})), juce::String ("d1120") } })
        {
            if (! als.existsAsFile())
                continue;
            AudioEngine liveEngine (false);
            MainComponent live (liveEngine, false);
            live.setSize (1920, 1080);
            live.openFile (als);
            liveEngine.dispatchPendingMessages();
            ui::WaveformCache::getInstance()->waitUntilIdle (60000);
            const auto& s = liveEngine.getSong();
            line (tag + ": tempo " + juce::String (s.bpm, 2) + ", " + juce::String ((int) s.playlistTracks.size()) + " Playlist tracks, " + juce::String ((int) s.playlist.size())
                  + " clips, " + juce::String ((int) s.automations.size()) + " automations, " + juce::String ((int) s.videoTracks.size()) + " video tracks");
            for (const auto& v : s.videoTracks)
                for (const auto& c : v.clips)
                    line (tag + " video clip: start " + juce::String (c.start, 3) + " s, file offset " + juce::String (c.offset, 3) + " s, length " + juce::String (c.length, 3)
                          + " s, file " + juce::String (c.duration, 3) + " s: " + juce::File (juce::String::fromUTF8 (c.path.c_str())).getFileName());
            for (const auto& a : s.automations)
                line (tag + " automation: " + juce::String::fromUTF8 (a.name.c_str()) + " (" + juce::String ((int) a.points.size()) + " points)");
            const auto& report = liveEngine.getImportReport();
            for (const auto* group : { &report.notReproduced, &report.approximated, &report.unsupported, &report.droppedEffects, &report.partialEffects })
                for (const auto& [what, count] : *group)
                    line (tag + " report: " + juce::String::fromUTF8 (what.c_str()) + " x" + juce::String (count));
            if (tag == "193")
                check ("193: the set opens with its video track", ! s.videoTracks.empty());
            line (tag + ": missing samples " + juce::String (report.missingSamples));
            live.showPanelsForSnapshot ({ Panel::playlist });
            live.selectTabForSnapshot (2);
            liveEngine.dispatchPendingMessages();
            savePng (live.createComponentSnapshot (live.getLocalBounds(), true, 1.0f), prefix + "-" + tag + "-set.png");
            auto& pl = live.getPlaylistForSnapshot();
            // (rollup 0.1.31 step 3: the settle step waits for the video lanes' thumbnails too, as for the waveforms: under load
            // they were still arriving when the scroll's pictures were taken, 547-12 484 px apart; after both, a few px)
            const auto settle = [&live, &liveEngine] {
                ui::WaveformCache::getInstance()->waitUntilIdle (60000);
                live.createComponentSnapshot (live.getLocalBounds(), true, 1.0f); // (a paint asks for the strips it shows)
                VideoLanesView::waitForThumbnails (60000);
                liveEngine.dispatchPendingMessages();
            };
            settle();
            const auto cache = pl.cacheSelfTest (settle, juce::File (prefix).getParentDirectory());
            // (the video lanes' thumbnails arrive in the background, so a picture taken while they come in differs there; the
            // scroll is what #193 is about: the old shift area moved the pinned lanes, ~190 000 px off on this set)
            int scrollOff = -1;
            for (const auto& l : juce::StringArray::fromLines (cache))
                if (l.startsWith ("scroll down"))
                    scrollOff = l.contains (": match") ? 0 : l.fromFirstOccurrenceOf ("MISMATCH, ", false, false).getIntValue();
            check (tag + ": after a scroll down the cached picture matches an exact paint (under 100 px apart; " + juce::String (scrollOff) + ")",
                   scrollOff < 0 || scrollOff < 100);
            for (const auto& l : juce::StringArray::fromLines (cache))
                if (l.contains ("scroll") || l.contains ("MISMATCH") || l.contains ("result="))
                    line (tag + " cachetest: " + l);
        }

        wav.deleteFile();
        checks << "result=" << (allOk ? "OK" : "FAIL") << "\n";
        juce::File (prefix + "-checks.txt").replaceWithText (checks);
        return allOk;
    }

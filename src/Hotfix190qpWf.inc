    // Round 190qp (0.1.31.1 hotfix, Discord on 0.1.31): in Track-only mode (Pattern layout) and in the Simple layout a
    // clip drawn on an instrument track is that instrument's, and a double-click on it opens that instrument's notes even
    // while it is empty (the Piano roll; Simple: the dock's Editor tab on that clip), not the Channel rack. The Pattern
    // layout with Track-only mode off keeps FL's rule (an empty pattern opens the Channel rack). Also #215's follow-up:
    // Export... with a folder in the file name writes without the file dialog (ui/ExportDialog.cpp
    // exportFolderTemplateCheck). Through the real MainComponent and Playlist with synthetic mouse events (Xvfb on Linux).
    // Included in the app class (--selftest hotfix190qp_wf, --hotfix190qpwfshot <prefix>).
    juce::String hotfix190qpWfCheck (const juce::String& shotPrefix = {})
    {
        using namespace lynx;
        using workspace::Panel;
        juce::StringArray failed;
        const auto expect = [&failed] (const juce::String& what, bool ok, const juce::String& detail = {}) {
            if (! ok)
                failed.add (what + (detail.isEmpty() ? juce::String() : " (" + detail + ")"));
        };
        const auto wasTrackOnly = trackonly::enabled();
        const auto wasStyle = layoutstyle::current();
        ui::motion::enabled() = false;

        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            song.playlist.clear();
            for (auto& p : song.patterns)
                p.notes.clear();
            ChannelDef synth;
            synth.name = "Lead";
            synth.instrument = InstrumentKind::synth;
            const auto channel = trackmodes::addInstrumentTrack (song, synth);
            const auto track = channel >= 0 ? trackmodes::instrumentTracksOf (song, channel).front() : -1;
            expect ("instrument track made", track >= 0);
            if (track < 0)
                return "ERROR " + failed.joinIntoString ("; ");
            song.mode = PlayMode::song;
            engine.setSong (song);
            engine.dispatchPendingMessages();
            MainComponent ui (engine, false);
            ui.setSize (1600, 1000);
            ui.setVisible (true); // (off the desktop; the dock's Editor docks only into a shown tree)
            engine.dispatchPendingMessages();
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            trackonly::setEnabled (true);
            ui.setLayoutStyle (layoutstyle::Style::pattern); // (Track-only mode hides the rack as the layout comes up)
            engine.dispatchPendingMessages();
            auto& pl = ui.getPlaylistForSnapshot();
            pl.setSize (juce::jmax (pl.getWidth(), 1100), juce::jmax (pl.getHeight(), 600));
            pl.setViewForSnapshot (100.0, 0, 0);
            pl.setToolForSnapshot (EditTool::pencil);
            engine.dispatchPendingMessages();
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
            const auto event = [&] (juce::Point<int> p, juce::ModifierKeys mods, int clicks) {
                return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pl, &pl, now, p.toFloat(), now, clicks, false);
            };
            const auto click = [&] (juce::Point<int> p) {
                pl.mouseMove (event (p, {}, 1));
                pl.mouseDown (event (p, left, 1));
                pl.mouseUp (event (p, {}, 1));
                engine.dispatchPendingMessages();
            };
            const auto doubleClick = [&] (juce::Point<int> p) {
                pl.mouseDown (event (p, left, 2));
                pl.mouseUp (event (p, {}, 2));
                pl.mouseDoubleClick (event (p, left, 2));
                engine.dispatchPendingMessages();
            };
            const auto at = [&pl] (int t, double bar) {
                return juce::Point<int> (pl.headerWidthForSnapshot() + (int) std::lround (bar * 100.0) + 20, pl.trackRowForSnapshot (t).getCentreY());
            };
            const auto lastClipOn = [&engine] (int t) {
                const auto& list = engine.getSong().playlist;
                for (int i = (int) list.size(); --i >= 0;)
                    if (list[(std::size_t) i].track == t)
                        return i;
                return -1;
            };

            // ---- Pattern layout, Track-only mode on: the pencil draws the instrument's (empty) pattern
            ui.hidePanelForCheck (Panel::channelRack);
            ui.hidePanelForCheck (Panel::pianoRoll);
            click (at (track, 2.0));
            const auto drawn = lastClipOn (track);
            expect ("track-only: the pencil drew a clip", drawn >= 0);
            if (drawn >= 0)
            {
                const auto& c = engine.getSong().playlist[(std::size_t) drawn];
                const auto& p = engine.getSong().patterns[(std::size_t) c.pattern];
                expect ("track-only: the clip is the instrument's empty pattern", c.kind == ClipKind::pattern && p.notes.empty() && p.name == "Lead",
                        juce::String::fromUTF8 (p.name.c_str()));
                engine.setSelectedChannel (0);
                doubleClick (at (track, 2.0));
                expect ("track-only: a double-click opens the Piano roll", ui.panelShownForCheck (Panel::pianoRoll));
                expect ("track-only: not the Channel rack", ! ui.panelShownForCheck (Panel::channelRack));
                expect ("track-only: on the track's instrument", engine.getSelectedChannel() == channel, juce::String (engine.getSelectedChannel()));

                // ---- Track-only mode off: FL's rule (an empty pattern opens the Channel rack)
                trackonly::setEnabled (false);
                ui.hidePanelForCheck (Panel::pianoRoll);
                doubleClick (at (track, 2.0));
                expect ("pattern layout: FL's rule kept (the Channel rack)", ui.panelShownForCheck (Panel::channelRack) && ! ui.panelShownForCheck (Panel::pianoRoll));
                ui.hidePanelForCheck (Panel::channelRack);
            }

            // ---- Simple layout: an instrument track of its own
            auto simple = Song::makeDefault();
            simple.playlist.clear();
            simpletracks::Request q;
            q.kind = simpletracks::Kind::instrument;
            ChannelDef keys;
            keys.instrument = InstrumentKind::synth;
            q.channel = keys;
            const auto made = simpletracks::addTrack (simple, q);
            expect ("simple track made", made.ok());
            if (made.ok())
            {
                simple.mode = PlayMode::song;
                ui.startNewProjectForSnapshot (simple);
                engine.dispatchPendingMessages();
                ui.setLayoutStyle (layoutstyle::Style::simple);
                engine.dispatchPendingMessages();
                pl.setSize (juce::jmax (pl.getWidth(), 1100), juce::jmax (pl.getHeight(), 600));
                pl.setViewForSnapshot (100.0, 0, 0);
                pl.setToolForSnapshot (EditTool::pencil);
                engine.dispatchPendingMessages();
                const auto st = made.track;
                const auto simpleChannel = engine.getSong().playlistTracks[(std::size_t) st].channel;
                pl.mouseMove (event (at (st, 5.0), {}, 1));
                const auto hint = pl.hintAt (at (st, 5.0));
                expect ("simple: the hint says how to make a MIDI clip", hint.text.contains (tr ("MIDI clip: [LMB] click or drag draws one on this track  [2xLMB] on a clip: edit its notes below")), hint.text);
                const auto clipsBefore = engine.getSong().playlist.size();
                click (at (st, 5.0));
                const auto sd = lastClipOn (st);
                expect ("simple: the pencil drew a clip", engine.getSong().playlist.size() == clipsBefore + 1 && sd >= 0);
                if (sd >= 0)
                {
                    const auto pattern = engine.getSong().playlist[(std::size_t) sd].pattern;
                    expect ("simple: the clip is empty", engine.getSong().patterns[(std::size_t) pattern].notes.empty());
                    expect ("simple: it is the instrument's", trackonly::pianoRollChannelOf (engine.getSong(), sd) == simpleChannel);
                    engine.setSelectedChannel (0);
                    engine.editSong ([] (Song& s) { s.currentPattern = 0; }, false);
                    doubleClick (at (st, 5.0));
                    auto* dock = ui.getSimpleWorkflowForSnapshot() != nullptr ? ui.getSimpleWorkflowForSnapshot()->dockForSnapshot() : nullptr;
                    expect ("simple: a double-click opens the Editor tab", dock != nullptr && dock->getTab() == SimpleDock::Tab::editor);
                    expect ("simple: of that track", dock != nullptr && dock->shownTrack() == st);
                    engine.dispatchPendingMessages();
                    ui.resized();
                    expect ("simple: the Piano roll is docked there", dock != nullptr && dock->isEditorDocked());
                    expect ("simple: on that clip", engine.getSong().currentPattern == pattern && simpletracks::editorPattern (engine.getSong(), st) == pattern,
                            juce::String (engine.getSong().currentPattern) + " / " + juce::String (pattern));
                    expect ("simple: on the track's instrument", engine.getSelectedChannel() == simpleChannel);
                    expect ("simple: not the Channel rack", ! ui.panelShownForCheck (Panel::channelRack));
                    if (shotPrefix.isNotEmpty())
                    {
                        savePng (ui.createComponentSnapshot (ui.getLocalBounds(), true, 1.0f), shotPrefix + "-simple-clip-editor.png");
                        savePng (ui.createComponentSnapshot (ui.getLocalBounds(), true, 1.5f), shotPrefix + "-simple-clip-editor@150.png");
                    }
                }
            }
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            engine.dispatchPendingMessages();
        }
        trackonly::setEnabled (wasTrackOnly);
        layoutstyle::setCurrent (wasStyle);

        // #215 follow-up: Export... with a folder in the name
        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            engine.setSong (song);
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-hotfix190qp-export");
            const auto r = exportFolderTemplateCheck (engine, folder, shotPrefix);
            expect ("export: " + r, r == "OK");
            folder.deleteRecursively();
        }
        return failed.isEmpty() ? juce::String ("OK") : "ERROR " + failed.joinIntoString ("; ");
    }

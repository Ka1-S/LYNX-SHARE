    // --undogesture190jv <report.txt> (round 190jv, queue item 73): one undo step per mouse gesture. Each editor gets a
    // drag of 12 moves (a Playlist clip, a Piano roll note, a Channel rack step paint, an Event editor draw), driven two
    // ways: (a) by calling the component's mouse handlers (as the test routes do) and (b) through the window peer, as the
    // platform layer does, without its global button flags (as any input source but the OS's own main mouse). The report has the undo steps each drag added
    // and whether the drag changed the song; result=OK when every drag changed the song in exactly one step. Included in
    // the app class of Main.cpp.
    bool runUndoGesture190jv (const juce::File& reportFile)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const juce::String& name, int steps, bool changed) {
            const auto ok = steps == 1 && changed;
            report.add (name + " steps=" + juce::String (steps) + " changed=" + (changed ? "yes" : "no") + " " + (ok ? "OK" : "FAIL"));
            allOk = allOk && ok;
        };

        // one pattern with a note, a clip of it on track 0, a channel rack step row
        const auto makeSong = [] {
            auto song = Song::makeDefault();
            song.currentPattern = 0;
            auto& p = song.patterns.front();
            p.notes.clear();
            Note n;
            n.channel = 4; // (the Synth)
            n.start = 0;
            n.length = 4 * ticksPerStep;
            n.key = 60;
            p.notes.push_back (n);
            song.playlist.clear();
            Clip c;
            c.pattern = 0;
            c.track = 0;
            c.start = 0;
            c.length = 4 * ppq * 4; // (one bar of 4/4)
            song.playlist.push_back (c);
            return song;
        };

        // (a) the component's own handlers; (b) the window peer, as a touch screen (touch index 0)
        enum class Drive { handlers, peerMouse, touch };
        const auto drag = [] (juce::Component& target, juce::Point<int> from, juce::Point<int> step, int moves, Drive how,
                              juce::ModifierKeys button = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)) {
            const auto now = juce::Time::getCurrentTime();
            if (how == Drive::handlers)
            {
                const auto source = juce::Desktop::getInstance().getMainMouseSource();
                const auto event = [&] (juce::Point<int> p, juce::ModifierKeys mods) {
                    return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, from.toFloat(), now, 1,
                                             p != from);
                };
                const juce::Component::SafePointer<juce::Component> alive (&target);
                target.mouseDown (event (from, button));
                for (int k = 1; k <= moves && alive != nullptr; ++k)
                    target.mouseDrag (event (from + step * k, button));
                if (alive != nullptr)
                    target.mouseUp (event (from + step * moves, juce::ModifierKeys()));
                return;
            }
            auto* peer = target.getPeer();
            if (peer == nullptr)
                return;
            auto t = juce::Time::currentTimeMillis();
            const auto type = how == Drive::touch ? juce::MouseInputSource::InputSourceType::touch : juce::MouseInputSource::InputSourceType::mouse;
            const auto at = [&] (juce::Point<int> p) { return peer->globalToLocal (target.localPointToGlobal (p.toFloat())); };
            const auto send = [&] (juce::Point<int> p, juce::ModifierKeys mods) {
                peer->handleMouseEvent (type, at (p), mods, juce::MouseInputSource::defaultPressure,
                                        juce::MouseInputSource::defaultOrientation, t += 16, {}, 0);
            };
            send (from, juce::ModifierKeys());
            send (from, button);
            for (int k = 1; k <= moves; ++k)
                send (from + step * k, button);
            send (from + step * moves, juce::ModifierKeys());
        };

        for (const auto how : { Drive::handlers, Drive::peerMouse }) // (JUCE's Linux window takes no touch input: Drive::touch is for Windows runs)
        {
            const juce::String tag = how == Drive::handlers ? "handlers" : how == Drive::touch ? "touch" : "peer-mouse";
            const auto desk = how != Drive::handlers;

            { // Playlist: drag the clip 12 × 20 px to the right
                AudioEngine engine (false);
                engine.setSong (makeSong());
                PlaylistComponent playlist (engine);
                playlist.setBounds (0, 0, 1400, 420);
                playlist.setVisible (true);
                if (desk)
                    playlist.addToDesktop (0);
                playlist.setViewForSnapshot (160.0, 0, 0);
                playlist.setToolForSnapshot (EditTool::pencil);
                engine.dispatchPendingMessages();
                const auto gridLeft = (int) playlist.laneBodyForSnapshot (0, 0).getX(); // (the grid's left edge)
                const auto from = juce::Point<int> (gridLeft + 40, playlist.trackRowForSnapshot (0).getCentreY());
                const auto before = engine.getHistoryPosition();
                const auto startBefore = engine.getSong().playlist.empty() ? -1 : engine.getSong().playlist.front().start;
                drag (playlist, from, { 20, 0 }, 12, how);
                engine.dispatchPendingMessages();
                const auto& clips = engine.getSong().playlist;
                const auto moved = clips.size() == 1 && clips.front().start != startBefore;
                check ("playlist-clip-move-" + tag, engine.getHistoryPosition() - before, moved);
                if (desk)
                    playlist.removeFromDesktop();
            }
            { // Piano roll: drag the note 12 × 12 px to the right
                AudioEngine engine (false);
                engine.setSong (makeSong());
                engine.setSelectedChannel (4);
                PianoRollComponent roll (engine);
                roll.setBounds (0, 0, 1200, 700);
                roll.setVisible (true);
                if (desk)
                    roll.addToDesktop (0);
                roll.setToolForSnapshot (EditTool::pencil);
                engine.dispatchPendingMessages();
                const auto from = roll.pointForTests (ticksPerStep, 60);
                const auto before = engine.getHistoryPosition();
                drag (roll, from, { 12, 0 }, 12, how);
                engine.dispatchPendingMessages();
                const auto& notes = engine.getSong().patterns.front().notes;
                const auto moved = notes.size() == 1 && notes.front().start != 0;
                check ("pianoroll-note-move-" + tag, engine.getHistoryPosition() - before, moved);
                if (desk)
                    roll.removeFromDesktop();
            }
            { // Channel rack: paint steps along the Synth's row (12 steps)
                AudioEngine engine (false);
                auto song = makeSong();
                song.patterns.front().notes.clear();
                engine.setSong (std::move (song));
                ChannelRackComponent rack (engine);
                rack.setBounds (0, 0, 900, 400);
                rack.setVisible (true);
                if (desk)
                    rack.addToDesktop (0);
                engine.dispatchPendingMessages();
                const auto first = rack.stepBoundsForTests (4, 0), second = rack.stepBoundsForTests (4, 1);
                const auto from = first.getCentre();
                const auto before = engine.getHistoryPosition();
                drag (rack, from, { second.getX() - first.getX(), 0 }, 11, how);
                engine.dispatchPendingMessages();
                const auto painted = std::count_if (engine.getSong().patterns.front().notes.begin(), engine.getSong().patterns.front().notes.end(),
                                                    [] (const Note& n) { return n.channel == 4; });
                check ("channelrack-step-paint-" + tag + " (" + juce::String ((int) painted) + " steps)", engine.getHistoryPosition() - before, painted > 1);
                if (desk)
                    rack.removeFromDesktop();
            }
            { // Event editor: draw across the Synth's volume lane
                AudioEngine engine (false);
                engine.setSong (makeSong());
                AutomationTarget target;
                target.kind = AutomationTargetKind::channelVolume;
                target.index = 4;
                auto editor = makeEventEditor (engine, target);
                editor->setBounds (0, 0, 900, 360);
                editor->setVisible (true);
                if (desk)
                    editor->addToDesktop (0);
                engine.dispatchPendingMessages();
                // the plot: the largest child under the editor's middle
                auto* plot = editor->getComponentAt (editor->getLocalBounds().getCentre());
                if (plot == nullptr)
                    plot = editor.get();
                const auto from = plot->getLocalBounds().getCentre() - juce::Point<int> (plot->getWidth() / 3, 0);
                const auto before = engine.getHistoryPosition();
                const auto eventsBefore = engine.getSong().patterns.front().events.size();
                drag (*plot, from, { plot->getWidth() / 20, -3 }, 12, how);
                engine.dispatchPendingMessages();
                const auto changed = engine.getSong().patterns.front().events.size() != eventsBefore
                                     || (! engine.getSong().patterns.front().events.empty() && ! engine.getSong().patterns.front().events.front().events.empty());
                check ("eventeditor-draw-" + tag, engine.getHistoryPosition() - before, changed);
                if (desk)
                    editor->removeFromDesktop();
            }
        }

        report.add (juce::String ("result=") + (allOk ? "OK" : "FAIL"));
        reportFile.getParentDirectory().createDirectory();
        reportFile.replaceWithText (report.joinIntoString ("\n") + "\n");
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        return allOk;
    }

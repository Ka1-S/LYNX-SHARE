    // --q1a190rwtest <report.txt> (round 190rw, 0.1.32 stage Q1 part A; also in --selftest): the song replaced under a held
    // drag. In 0.1.31.2 an undo pressed while a Playlist clip was still held (the Draw tool's new clip) wrote past the
    // playlist's end on the next mouse move ("double free or corruption"). Now (1) undo / redo / history jumps wait while
    // a real mouse gesture is held (AudioEngine::isEditGestureHeld), and (2) every editor's drag checks, on each move and at
    // the release, that what it holds is still there (by uid, or by value for notes / rows): if not it ends with no
    // write (ui/DragSongWatch.h). Each editor gets a drag with a song swap in the middle — an undo (the handlers carry no
    // real button, so the engine lets it through, as a guest's edit or Remote Control would) and an outside edit that
    // deletes what the drag holds — and one outside edit that leaves it alone (the drag goes on). Run it under ASan too.
    // Included in the app class of Main.cpp.
    bool runQ1a190rwTest (const juce::File& reportFile)
    {
        using namespace lynx;
        auto* previousLook = &juce::LookAndFeel::getDefaultLookAndFeel(); // (put back at the end: --selftest goes on)
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const juce::String& name, bool ok, const juce::String& detail = {}) {
            report.add (name + (detail.isNotEmpty() ? " (" + detail + ")" : juce::String()) + " " + (ok ? "OK" : "FAIL"));
            allOk = allOk && ok;
        };

        const auto makeSong = [] {
            auto song = Song::makeDefault();
            song.currentPattern = 0;
            auto& p = song.patterns.front();
            p.notes.clear();
            Note n;
            n.channel = 4;
            n.start = 0;
            n.length = 4 * ticksPerStep;
            n.key = 60;
            p.notes.push_back (n);
            song.playlist.clear();
            for (int t = 0; t < 3; ++t)
            {
                Clip c;
                c.pattern = 0;
                c.track = t;
                c.start = 0;
                c.length = 4 * ppq * 4;
                song.playlist.push_back (c);
            }
            uids::normalize (song);
            return song;
        };

        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        struct Gesture
        {
            juce::Component& target;
            juce::Point<int> from;
            juce::MouseInputSource source;
            juce::MouseEvent at (juce::Point<int> p, juce::ModifierKeys mods) const
            {
                const auto now = juce::Time::getCurrentTime();
                return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, from.toFloat(), now, 1,
                                         p != from);
            }
            void down() { target.mouseDown (at (from, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))); }
            void move (juce::Point<int> step, int k)
            {
                for (int i = 1; i <= k; ++i)
                    target.mouseDrag (at (from + step * i, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)));
            }
            void up (juce::Point<int> p) { target.mouseUp (at (p, juce::ModifierKeys())); }
        };

        // (1) the engine: undo and redo wait while a real mouse gesture is held
        {
            AudioEngine engine (false);
            engine.setSong (makeSong());
            engine.editSong ([] (Song& s) { s.bpm = 99.0; });
            const auto position = engine.getHistoryPosition();
            const auto saved = juce::ModifierKeys::currentModifiers;
            juce::ModifierKeys::currentModifiers = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
            engine.beginUndoGesture();
            const auto held = engine.isEditGestureHeld();
            engine.undo();
            engine.jumpToHistory (0);
            const auto refused = engine.getHistoryPosition() == position && engine.getSong().bpm == 99.0;
            engine.endUndoGesture();
            juce::ModifierKeys::currentModifiers = juce::ModifierKeys();
            engine.undo();
            const auto afterRelease = engine.getHistoryPosition() == position - 1 && engine.getSong().bpm != 99.0;
            juce::ModifierKeys::currentModifiers = saved;
            check ("engine: undo / history jump refused under a held drag, done after the release", held && refused && afterRelease);
        }

        // (2) Playlist
        for (int swap = 0; swap < 3; ++swap) // 0 undo of the drag's own new clip, 1 a guest deletes the dragged clip, 2 a guest adds a clip elsewhere
        {
            AudioEngine engine (false);
            engine.setSong (makeSong());
            PlaylistComponent playlist (engine);
            playlist.setBounds (0, 0, 1400, 420);
            playlist.setVisible (true);
            playlist.setViewForSnapshot (160.0, 0, 0);
            playlist.setToolForSnapshot (EditTool::pencil);
            engine.dispatchPendingMessages();
            const auto gridLeft = (int) playlist.laneBodyForSnapshot (0, 0).getX();
            // swap 0: the Draw tool on empty space (track 4) makes a clip and holds it; else the clip on track 1 is grabbed
            const auto track = swap == 0 ? 4 : 1;
            const auto x = swap == 0 ? gridLeft + 700 : gridLeft + 40;
            Gesture g { playlist, { x, playlist.trackRowForSnapshot (track).getCentreY() }, source };
            const auto before = engine.getSong().playlist;
            g.down();
            g.move ({ 15, 0 }, 3);
            const auto held = engine.getSong().playlist;
            if (swap == 0)
                engine.undo(); // (the new clip goes)
            else if (swap == 1)
                engine.editSong ([] (Song& s) {
                    s.playlist.erase (std::remove_if (s.playlist.begin(), s.playlist.end(), [] (const Clip& c) { return c.track == 1; }), s.playlist.end());
                });
            else
                engine.editSong ([] (Song& s) {
                    Clip c;
                    c.pattern = 0;
                    c.track = 7;
                    c.start = 4 * ppq * 4 * 6;
                    c.length = 4 * ppq * 4;
                    s.playlist.insert (s.playlist.begin(), c); // (first: every index after it moves)
                });
            const auto afterSwap = engine.getSong().playlist;
            g.move ({ 15, 0 }, 8);
            g.up (g.from + juce::Point<int> (120, 0));
            engine.dispatchPendingMessages();
            const auto& now = engine.getSong().playlist;
            if (swap < 2)
            {
                // the drag ended at the swap: the playlist is what the swap left
                const auto same = now.size() == afterSwap.size()
                                  && std::equal (now.begin(), now.end(), afterSwap.begin(), [] (const Clip& a, const Clip& b) {
                                         return a.uid == b.uid && a.start == b.start && a.track == b.track && a.length == b.length;
                                     });
                check (juce::String ("playlist: ") + (swap == 0 ? "undo of the held new clip" : "a guest deletes the held clip") + ", the drag ends with no write",
                       same, juce::String ((int) before.size()) + " -> " + juce::String ((int) held.size()) + " -> " + juce::String ((int) now.size()) + " clips");
            }
            else
            {
                // the drag went on with the same clip (found by uid): it moved, the other clips didn't
                std::uint64_t grabbed = 0;
                for (const auto& c : before)
                    if (c.track == 1)
                        grabbed = c.uid;
                const auto it = std::find_if (now.begin(), now.end(), [grabbed] (const Clip& c) { return c.uid == grabbed; });
                const auto others = std::all_of (now.begin(), now.end(), [grabbed] (const Clip& c) { return c.uid == grabbed || c.start == 0 || c.track == 7; });
                check ("playlist: a guest adds a clip elsewhere, the drag goes on with its clip", it != now.end() && it->start > 0 && it->track == 1 && others
                                                                                                      && now.size() == before.size() + 1);
            }
        }

        // (3) Piano roll
        for (int swap = 0; swap < 2; ++swap) // 0 undo of the drag's own new note, 1 a guest clears the pattern
        {
            AudioEngine engine (false);
            engine.setSong (makeSong());
            engine.setSelectedChannel (4);
            PianoRollComponent roll (engine);
            roll.setBounds (0, 0, 1200, 700);
            roll.setVisible (true);
            roll.setToolForSnapshot (EditTool::pencil);
            engine.dispatchPendingMessages();
            // swap 0: a new note on empty grid (key 64, step 8), held; swap 1: the existing note
            Gesture g { roll, swap == 0 ? roll.pointForTests (8 * ticksPerStep + ticksPerStep / 2, 64) : roll.pointForTests (ticksPerStep, 60), source };
            g.down();
            g.move ({ 12, 0 }, 3);
            if (swap == 0)
                engine.undo();
            else
                engine.editSong ([] (Song& s) { s.patterns.front().notes.clear(); });
            const auto afterSwap = engine.getSong().patterns.front().notes;
            g.move ({ 12, -4 }, 8);
            g.up (g.from + juce::Point<int> (96, -32));
            engine.dispatchPendingMessages();
            const auto& notes = engine.getSong().patterns.front().notes;
            const auto same = notes.size() == afterSwap.size()
                              && std::equal (notes.begin(), notes.end(), afterSwap.begin(), [] (const Note& a, const Note& b) {
                                     return a.start == b.start && a.key == b.key && a.length == b.length && a.channel == b.channel;
                                 });
            check (juce::String ("piano roll: ") + (swap == 0 ? "undo of the held new note" : "a guest clears the pattern") + ", the drag ends with no write", same,
                   juce::String ((int) notes.size()) + " notes");
        }

        // (4) Channel rack: a step paint; a guest deletes the painted channel in the middle
        {
            AudioEngine engine (false);
            auto song = makeSong();
            song.patterns.front().notes.clear();
            engine.setSong (std::move (song));
            ChannelRackComponent rack (engine);
            rack.setBounds (0, 0, 900, 400);
            rack.setVisible (true);
            engine.dispatchPendingMessages();
            const auto first = rack.stepBoundsForTests (4, 0), second = rack.stepBoundsForTests (4, 1);
            Gesture g { rack, first.getCentre(), source };
            const auto channels = engine.getSong().channels.size();
            g.down();
            g.move ({ second.getX() - first.getX(), 0 }, 3);
            engine.editSong ([] (Song& s) { removeChannels (s, { 4 }); });
            const auto afterSwap = engine.getSong().patterns.front().notes;
            g.move ({ second.getX() - first.getX(), 0 }, 8);
            g.up (g.from);
            engine.dispatchPendingMessages();
            const auto& notes = engine.getSong().patterns.front().notes;
            check ("channel rack: a guest deletes the painted channel, the paint ends with no write (no steps on the channel below)",
                   notes == afterSwap && engine.getSong().channels.size() == channels - 1, juce::String ((int) notes.size()) + " notes");
        }
        // (4b) Channel rack: an undo under the paint (the same rows): the paint goes on
        {
            AudioEngine engine (false);
            auto song = makeSong();
            song.patterns.front().notes.clear();
            engine.setSong (std::move (song));
            engine.editSong ([] (Song& s) { s.bpm = 98.0; });
            ChannelRackComponent rack (engine);
            rack.setBounds (0, 0, 900, 400);
            rack.setVisible (true);
            engine.dispatchPendingMessages();
            const auto first = rack.stepBoundsForTests (4, 0), second = rack.stepBoundsForTests (4, 1);
            Gesture g { rack, first.getCentre(), source };
            g.down();
            g.move ({ second.getX() - first.getX(), 0 }, 3);
            engine.undo(); // (the paint's own step: the rows are the same)
            g.move ({ second.getX() - first.getX(), 0 }, 8);
            g.up (g.from);
            engine.dispatchPendingMessages();
            check ("channel rack: an undo that keeps the rows, the paint goes on", ! engine.getSong().patterns.front().notes.empty());
        }

        // (5) Event editor: a stroke; the song replaced in the middle: the release puts no old events back
        for (int swap = 0; swap < 2; ++swap)
        {
            AudioEngine engine (false);
            engine.setSong (makeSong());
            AutomationTarget target;
            target.kind = AutomationTargetKind::channelVolume;
            target.index = 4;
            auto editor = makeEventEditor (engine, target);
            editor->setBounds (0, 0, 900, 360);
            editor->setVisible (true);
            engine.dispatchPendingMessages();
            auto* plot = editor->getComponentAt (editor->getLocalBounds().getCentre());
            if (plot == nullptr)
                plot = editor.get();
            Gesture g { *plot, plot->getLocalBounds().getCentre() - juce::Point<int> (plot->getWidth() / 3, 0), source };
            g.down();
            g.move ({ plot->getWidth() / 20, -3 }, 3);
            auto replaced = makeSong();
            replaced.bpm = 77.0; // (a guest's song: no events)
            if (swap == 0)
                engine.setSong (replaced);
            else
                engine.editSong ([] (Song& s) { s.patterns.front().events.clear(); });
            g.move ({ plot->getWidth() / 20, -3 }, 6);
            g.up (g.from + juce::Point<int> (plot->getWidth() / 20 * 6, -18));
            engine.dispatchPendingMessages();
            const auto& p = engine.getSong().patterns.front();
            const auto none = std::all_of (p.events.begin(), p.events.end(), [] (const PatternEvents& e) { return e.events.empty(); });
            check (juce::String ("event editor: ") + (swap == 0 ? "the song replaced" : "a guest clears the events") + " under a stroke, no old events written back", none);
        }

        // (6) Automation editor: a point held; a guest deletes the clip's points in the middle
        {
            AudioEngine engine (false);
            auto song = makeSong();
            Automation a;
            a.name = "Test";
            a.points = { { 0, 0.2f }, { 4 * ppq, 0.8f }, { 8 * ppq, 0.4f } };
            song.automations.push_back (a);
            uids::normalize (song);
            engine.setSong (std::move (song));
            auto editor = makeAutomationEditor (engine, 0);
            editor->setBounds (0, 0, 900, 400);
            editor->setVisible (true);
            engine.dispatchPendingMessages();
            Gesture g { *editor, editor->getLocalBounds().getCentre(), source };
            g.down(); // (on the curve: a new point, held)
            g.move ({ 10, -5 }, 3);
            engine.editSong ([] (Song& s) { s.automations.front().points.resize (1); });
            const auto afterSwap = engine.getSong().automations.front().points;
            g.move ({ 10, -5 }, 6);
            g.up (g.from + juce::Point<int> (60, -30));
            engine.dispatchPendingMessages();
            const auto& points = engine.getSong().automations.front().points;
            const auto same = points.size() == afterSwap.size() && points.front().tick == afterSwap.front().tick && points.front().value == afterSwap.front().value;
            check ("automation editor: a guest deletes the held point's clip points, the drag ends with no write", same,
                   juce::String ((int) points.size()) + " points");
        }

        // (7) Clip-launch grid and the Playlist <-> grid drags: a drop checks the dragged clip's uid
        {
            auto song = makeSong();
            uids::normalize (song);
            const auto description = playlistClipDragDescription (1, song.playlist[1].uid);
            auto changed = song;
            changed.playlist.erase (changed.playlist.begin());
            uids::normalize (changed);
            check ("playlist -> grid drag: the same clip holds, a removed / moved one does not",
                   playlistClipDragHolds (song, description) && ! playlistClipDragHolds (changed, description)
                       && playlistClipDragHolds (song, playlistClipDragDescription (1)));
            int t = -1, s = -1;
            check ("grid drag descriptions with and without a uid parse",
                   clipGridDrag (juce::var ("lynx-grid-clip|2|3"), t, s) && t == 2 && s == 3 && clipGridDrag (juce::var ("lynx-grid-clip|1|0|1f"), t, s) && t == 1 && s == 0
                       && ! clipGridDragHolds (song, juce::var ("lynx-grid-clip|1|0|1f")));
        }

        // (9) Piano roll keys that ignored extra modifiers and shadowed other bindings: Ctrl+Alt+L (the Clip-launch detail
        // view), Ctrl+Alt+C (Consolidate), Ctrl+Shift+U (the Clip-launch editor's quantize settings) are no longer legato /
        // copy / chop; Ctrl+L, Ctrl+C, Ctrl+U still are. Ctrl+X with nothing selected cuts every note of the channel (FL)
        {
            AudioEngine engine (false);
            engine.setSong (makeSong());
            engine.setSelectedChannel (4);
            PianoRollComponent roll (engine);
            roll.setBounds (0, 0, 1200, 700);
            engine.dispatchPendingMessages();
            const auto ctrl = juce::ModifierKeys::commandModifier, alt = juce::ModifierKeys::altModifier, shift = juce::ModifierKeys::shiftModifier;
            const auto shadowed = ! roll.handleKey (juce::KeyPress ('L', juce::ModifierKeys (ctrl | alt), 0))
                                  && ! roll.handleKey (juce::KeyPress ('C', juce::ModifierKeys (ctrl | alt), 0))
                                  && ! roll.handleKey (juce::KeyPress ('U', juce::ModifierKeys (ctrl | shift), 0));
            const auto own = roll.handleKey (juce::KeyPress ('C', juce::ModifierKeys (ctrl), 0)) && roll.handleKey (juce::KeyPress ('U', juce::ModifierKeys (ctrl), 0));
            check ("piano roll: Ctrl+Alt+L / Ctrl+Alt+C / Ctrl+Shift+U reach their own actions; Ctrl+C / Ctrl+U stay the editor's", shadowed && own);
            const auto before = engine.getSong().patterns.front().notes.size();
            roll.handleKey (juce::KeyPress ('X', juce::ModifierKeys (ctrl), 0));
            const auto after = engine.getSong().patterns.front().notes.size();
            roll.handleKey (juce::KeyPress ('V', juce::ModifierKeys (ctrl), 0));
            check ("piano roll: Ctrl+X with nothing selected cuts the channel's notes, Ctrl+V brings them back (FL)",
                   before > 0 && after == 0 && engine.getSong().patterns.front().notes.size() == before);
        }

        // (10) Linux: X11 gives Shift+1 as '!': the Shift+digit zoom keys of the Playlist and the Piano roll (and Shift+/)
        // did nothing there. The chord is the key again (Windows sends the key itself, unchanged)
        {
            const auto shift = juce::ModifierKeys (juce::ModifierKeys::shiftModifier);
            const auto c1 = chordFromKeyPress (juce::KeyPress ('!', shift, '!')), c4 = chordFromKeyPress (juce::KeyPress ('$', shift, '$'));
            const auto c0 = chordFromKeyPress (juce::KeyPress (')', shift, ')')), cq = chordFromKeyPress (juce::KeyPress ('?', shift, '?'));
            const auto win = chordFromKeyPress (juce::KeyPress ('1', shift, '!'));
            const auto plain = chordFromKeyPress (juce::KeyPress ('!', juce::ModifierKeys(), '!')); // (no Shift: left alone)
            check ("keys: Shift+1 / Shift+4 / Shift+0 / Shift+/ as Linux sends them are the Shift+digit / Shift+/ chords",
                   c1.key == '1' && c4.key == '4' && c0.key == '0' && cq.key == '/' && win.key == '1' && c1.mods == keys::shift && plain.key == '!');
            check ("keys: the key map finds playlist.zoom1 for Linux's Shift+1",
                   keys::KeyMap().find (c1, keys::Context::playlist, false) == "playlist.zoom1");
        }

        // (8) several files dropped at once from the file manager: one undo step (they were one per file)
        {
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-q1a190rw-drop");
            folder.createDirectory();
            juce::StringArray files;
            for (int k = 0; k < 3; ++k)
            {
                const auto wav = folder.getChildFile ("hit" + juce::String (k) + ".wav");
                wav.deleteFile();
                juce::WavAudioFormat format;
                if (std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (new juce::FileOutputStream (wav), 44100.0, 1, 16, {}, 0)); writer != nullptr)
                {
                    juce::AudioBuffer<float> buffer (1, 4410);
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (0, i, 0.3f * std::sin ((float) i * 0.05f * (float) (k + 1)));
                    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
                }
                files.add (wav.getFullPathName());
            }
            {
                AudioEngine engine (false);
                engine.setSong (makeSong());
                PlaylistComponent playlist (engine);
                playlist.setBounds (0, 0, 1400, 420);
                playlist.setVisible (true);
                playlist.setViewForSnapshot (160.0, 0, 0);
                engine.dispatchPendingMessages();
                const auto before = engine.getHistoryPosition();
                const auto clips = engine.getSong().playlist.size();
                const auto at = juce::Point<int> ((int) playlist.laneBodyForSnapshot (5, 0).getX() + 300, playlist.trackRowForSnapshot (5).getCentreY());
                playlist.filesDropped (files, at.x, at.y);
                engine.dispatchPendingMessages();
                check ("playlist: three files dropped at once are one undo step", engine.getHistoryPosition() - before == 1
                                                                                     && engine.getSong().playlist.size() == clips + 3,
                       juce::String (engine.getHistoryPosition() - before) + " steps, " + juce::String ((int) (engine.getSong().playlist.size() - clips)) + " clips");
            }
            {
                AudioEngine engine (false);
                engine.setSong (makeSong());
                ChannelRackComponent rack (engine);
                rack.setBounds (0, 0, 900, 600);
                rack.setVisible (true);
                engine.dispatchPendingMessages();
                const auto before = engine.getHistoryPosition();
                const auto channels = engine.getSong().channels.size();
                rack.filesDropped (files, 400, 590); // (below the channels: new channels)
                engine.dispatchPendingMessages();
                check ("channel rack: three files dropped at once are one undo step", engine.getHistoryPosition() - before == 1
                                                                                         && engine.getSong().channels.size() == channels + 3,
                       juce::String (engine.getHistoryPosition() - before) + " steps, " + juce::String ((int) (engine.getSong().channels.size() - channels)) + " channels");
            }
            folder.deleteRecursively();
        }

        report.add (juce::String ("result=") + (allOk ? "OK" : "FAIL"));
        reportFile.getParentDirectory().createDirectory();
        reportFile.replaceWithText (report.joinIntoString ("\n") + "\n");
        juce::LookAndFeel::setDefaultLookAndFeel (previousLook);
        return allOk;
    }

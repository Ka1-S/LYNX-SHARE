    // Round 190re (0.1.32 regression sweep). LYNX --sweep190re <folder> [project]... [--fast]: every project opened through
    // the main window's File › Open path (off screen, no input), then, per project:
    //  open      the song has content; nothing fails to open
    //  layouts   Pattern -> Clip-launch -> Simple -> Track-only -> Pattern: each layout draws (100 % and 150 %), every
    //            visible panel lies inside the window, and switching the layout leaves the song as it was
    //  panels    each of the five panels shown alone, maximised and back, drawn at 100 % / 150 %; the song untouched
    //  reload    saved as .lynx, opened again: the same song; saved again: the same bytes
    //  undo      ten edit kinds (note, clip move, clip delete, fader, effect slot, pattern order, tempo, channel name,
    //            track mute, automation point) one undo step each: undone all = the opened song, redone all = the edited one
    // <folder>/checks.txt (ok / FAIL lines, result=OK); pictures only for the first project (or all without --fast).
    // Included in the app class of Main.cpp.
    // the song as text without the project's clock, which runs on its own (checked by itself, below)
    static juce::String sweep190reText (lynx::Song song)
    {
        song.info.workSeconds = 0.0;
        return lynx::ProjectFile::toJson (song, false);
    }

    static juce::String sweep190reFirstDiff (const juce::String& a, const juce::String& b)
    {
        if (a == b)
            return {};
        juce::StringArray la, lb;
        la.addLines (a);
        lb.addLines (b);
        // (a line inserted or removed puts a comma on the line before it: compare without the line-end commas)
        for (auto* lines : { &la, &lb })
            for (auto& line : *lines)
                line = line.trimEnd().trimCharactersAtEnd (",");
        if (la == lb)
            return {};
        // (a line inserted or removed puts a comma on the line before it: that line is not the difference)
        const auto same = [] (const juce::String& x, const juce::String& y) { return x.trimEnd().trimCharactersAtEnd (",") == y.trimEnd().trimCharactersAtEnd (","); };
        for (int k = 0; k < juce::jmax (la.size(), lb.size()); ++k)
            if (! same (la[k], lb[k]))
                return "line " + juce::String (k + 1) + " of " + juce::String (la.size()) + "/" + juce::String (lb.size()) + ": [" + la[k].trim().substring (0, 160)
                       + "] -> [" + lb[k].trim().substring (0, 160) + "]";
        return "(same lines, other line ends)";
    }

    // distinct colours in a picture, sampled on a grid (a blank or one-colour picture has 1-2)
    static int sweep190reColours (const juce::Image& image)
    {
        std::set<juce::uint32> seen;
        if (! image.isValid())
            return 0;
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < image.getHeight(); y += 7)
            for (int x = 0; x < image.getWidth(); x += 7)
            {
                seen.insert (data.getPixelColour (x, y).getARGB());
                if (seen.size() > 64)
                    return (int) seen.size();
            }
        return (int) seen.size();
    }

    bool runSweep190re (const juce::String& folderPath, const juce::StringArray& projects, bool fast)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        ui::motion::enabled() = false;
        const juce::File folder (folderPath);
        folder.createDirectory();
        juce::String checks;
        int fails = 0, oks = 0;
        const auto check = [&] (const juce::String& what, bool ok) {
            checks << (ok ? "ok   " : "FAIL ") << what << "\n";
            (ok ? oks : fails) += 1;
            folder.getChildFile ("checks.txt").replaceWithText (checks); // (kept as it goes: a crash leaves the steps before it)
        };
        const auto note = [&] (const juce::String& text) {
            checks << "     " << text << "\n";
            folder.getChildFile ("checks.txt").replaceWithText (checks);
        };
        int dumps = 0;
        const auto dump = [&] (const juce::String& what, const juce::String& a, const juce::String& b) {
            if (++dumps > 40)
                return;
            const auto base = "diff" + juce::String (dumps) + "-" + juce::File::createLegalFileName (what).replaceCharacter (' ', '_');
            folder.getChildFile (base + "-expected.json").replaceWithText (a);
            folder.getChildFile (base + "-got.json").replaceWithText (b);
            checks << "     (texts: " << base << "-expected.json / -got.json)\n";
        };
        const auto wasTrackOnly = trackonly::enabled();

        int index = 0;
        for (const auto& path : projects)
        {
            const juce::File file (path);
            const auto tag = juce::String (++index) + " " + file.getFileName();
            const auto pictures = ! fast || index == 1;
            const auto shotName = "p" + juce::String (index);
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (1600, 900);
            ui.setVisible (true);
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.dispatchPendingMessages();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            crashguard::note (("sweep190re open " + file.getFileName()).toStdString());
            ui.openFile (file);
            for (int k = 0; k < 5; ++k)
                offline.dispatchPendingMessages();
            const auto& song = offline.getSong();
            const auto opened = ! song.channels.empty() || ! song.playlist.empty() || ! song.playlistTracks.empty();
            note (tag + ": " + juce::String (song.channels.size()) + " channels, " + juce::String (song.patterns.size()) + " patterns, "
                  + juce::String (song.playlist.size()) + " clips, " + juce::String (song.mixer.size()) + " inserts, opened in "
                  + juce::String (juce::roundToInt (juce::Time::getMillisecondCounterHiRes() - t0)) + " ms");
            check (tag + " open: the project has content", opened);
            if (! opened)
                continue;
            const auto s0 = sweep190reText (offline.getSong());

            // --- layouts ---------------------------------------------------------------------------------------------
            struct L { const char* name; layoutstyle::Style style; bool trackOnly; };
            const L layouts[] { { "pattern", layoutstyle::Style::pattern, false }, { "cliplaunch", layoutstyle::Style::clipLaunch, false },
                                { "simple", layoutstyle::Style::simple, false }, { "trackonly", layoutstyle::Style::pattern, true },
                                { "pattern-again", layoutstyle::Style::pattern, false } };
            for (const auto& l : layouts)
            {
                trackonly::setEnabled (l.trackOnly);
                ui.setLayoutStyle (l.style);
                offline.dispatchPendingMessages();
                ui.resized();
                offline.dispatchPendingMessages();
                int colours = 0;
                for (const auto scale : { 1.0f, 1.5f })
                {
                    const auto image = ui.createComponentSnapshot (ui.getLocalBounds(), true, scale);
                    colours = juce::jmax (colours, sweep190reColours (image));
                    if (pictures)
                        savePng (image, folder.getChildFile (shotName + "-layout-" + l.name + (scale > 1.0f ? "@150.png" : ".png")).getFullPathName());
                }
                check (tag + " layout " + l.name + ": drawn (" + juce::String (colours) + " colours)", colours > 8);
                juce::StringArray outside;
                for (auto* child : ui.getChildren())
                    if (child->isVisible() && child->getWidth() > 0 && ! ui.getLocalBounds().contains (child->getBounds()))
                        outside.add (child->getName().isEmpty() ? juce::String (typeid (*child).name()) : child->getName());
                if (! outside.isEmpty())
                    note ("children past the window: " + outside.joinIntoString (", "));
                check (tag + " layout " + l.name + ": every shown part inside the window", outside.isEmpty());
                const auto now = sweep190reText (offline.getSong());
                const auto diff = sweep190reFirstDiff (s0, now);
                if (diff.isNotEmpty())
                {
                    note ("song changed by the layout switch: " + diff);
                    dump (tag + " layout " + l.name, s0, now);
                }
                check (tag + " layout " + l.name + ": the song is unchanged", diff.isEmpty());
                check (tag + " layout " + l.name + ": no undo step from a layout switch", ! offline.canUndo());
            }
            trackonly::setEnabled (false);

            // --- panels ----------------------------------------------------------------------------------------------
            for (int p = 0; p < workspace::panelCount; ++p)
            {
                const auto panel = (workspace::Panel) p;
                ui.showPanelsForSnapshot ({ panel });
                offline.dispatchPendingMessages();
                const auto image = ui.createComponentSnapshot (ui.getLocalBounds(), true, 1.0f);
                if (pictures)
                {
                    savePng (image, folder.getChildFile (shotName + "-panel-" + workspace::panelId (panel) + ".png").getFullPathName());
                    savePng (ui.createComponentSnapshot (ui.getLocalBounds(), true, 1.5f),
                             folder.getChildFile (shotName + "-panel-" + workspace::panelId (panel) + "@150.png").getFullPathName());
                }
                check (tag + " panel " + workspace::panelId (panel) + ": shown and drawn",
                       ui.panelShownForCheck (panel) && sweep190reColours (image) > 8);
            }
            ui.showDesktopForSnapshot (false);
            offline.dispatchPendingMessages();
            {
                const auto diff = sweep190reFirstDiff (s0, sweep190reText (offline.getSong()));
                if (diff.isNotEmpty())
                    note ("song changed by showing panels: " + diff);
                check (tag + " panels: the song is unchanged", diff.isEmpty());
            }

            // --- save / reload ---------------------------------------------------------------------------------------
            {
                const auto a = folder.getChildFile (shotName + "-a.lynx"), b = folder.getChildFile (shotName + "-b.lynx");
                juce::String error;
                const auto savedA = ProjectFile::save (offline.getSong(), a, error, ui.currentWindowLayout());
                check (tag + " save .lynx" + (error.isEmpty() ? juce::String() : ": " + error), savedA);
                ui.openFile (a);
                for (int k = 0; k < 5; ++k)
                    offline.dispatchPendingMessages();
                const auto now = sweep190reText (offline.getSong());
                const auto diff = sweep190reFirstDiff (s0, now);
                if (diff.isNotEmpty())
                {
                    note ("song after save + reopen: " + diff);
                    dump (tag + " reload", s0, now);
                }
                check (tag + " reload: the same song", diff.isEmpty());
                const auto savedB = ProjectFile::save (offline.getSong(), b, error, ui.currentWindowLayout());
                juce::MemoryBlock ma, mb;
                a.loadFileAsData (ma);
                b.loadFileAsData (mb);
                const auto same = savedB && ma == mb;
                if (! same)
                {
                    juce::String ta, tb;
                    ta = a.loadFileAsString();
                    tb = b.loadFileAsString();
                    note ("second save: " + sweep190reFirstDiff (ta, tb));
                }
                check (tag + " save again: the same bytes", same);
                if (fast)
                {
                    a.deleteFile();
                    b.deleteFile();
                }
            }

            // --- undo / redo -----------------------------------------------------------------------------------------
            {
                const auto before = sweep190reText (offline.getSong());
                struct E { const char* name; std::function<bool (const Song&)> can; std::function<void()> run; };
                std::vector<E> edits {
                    { "add a note", [] (const Song& s) { return ! s.patterns.empty() && ! s.channels.empty(); },
                      [&offline] { offline.editSong ([] (Song& s) { s.patterns[0].notes.push_back ({ 3 * ppq, ppq, 0, 67 }); }); } },
                    { "move a clip", [] (const Song& s) { return ! s.playlist.empty(); },
                      [&offline] { offline.editSong ([] (Song& s) { s.playlist[0].start += 4 * ppq; }); } },
                    { "fader", [] (const Song& s) { return s.mixer.size() > 1; },
                      [&offline] { offline.editLevels ([] (Song& s) { s.mixer[1].volume = s.mixer[1].volume * 0.5f + 0.01f; }); offline.flushLevels(); } },
                    { "effect slot", [] (const Song& s) { return s.mixer.size() > 1 && s.mixer[1].effects.size() < 8; },
                      [&offline] { offline.editSongStructural ([] (Song& s) { s.mixer[1].effects.push_back (makeEffectSlot (EffectKind::delay)); }); } },
                    { "pattern order", [] (const Song& s) { return s.patterns.size() > 1; },
                      [&offline] { offline.editSongPatternOrder ([] (Song& s) { std::swap (s.patterns[0].name, s.patterns[1].name); s.patterns[0].name += " *"; }); } },
                    { "tempo", [] (const Song&) { return true; }, [&offline] { offline.editSong ([] (Song& s) { s.bpm += 1.0; }); } },
                    { "channel name", [] (const Song& s) { return ! s.channels.empty(); },
                      [&offline] { offline.editSong ([] (Song& s) { s.channels[0].name += " 2"; }); } },
                    { "track mute", [] (const Song& s) { return ! s.playlistTracks.empty(); },
                      [&offline] { offline.editSong ([] (Song& s) { s.playlistTracks[0].muted = ! s.playlistTracks[0].muted; }); } },
                    { "automation point", [] (const Song& s) { return ! s.automations.empty(); },
                      [&offline] { offline.editSong ([] (Song& s) { s.automations[0].points.push_back ({ 64 * ppq, 0.5f, 0.0f }); }); } },
                    { "delete a clip", [] (const Song& s) { return s.playlist.size() > 1; },
                      [&offline] { offline.editSong ([] (Song& s) { s.playlist.pop_back(); }); } },
                };
                std::vector<juce::String> states { before };
                juce::Thread::sleep (1100); // (so the structural edit's stamp adds working time: the clock check below)
                juce::StringArray done;
                for (auto& e : edits)
                {
                    if (! e.can (offline.getSong()))
                        continue;
                    e.run();
                    offline.dispatchPendingMessages();
                    juce::Thread::sleep (2);
                    states.push_back (sweep190reText (offline.getSong()));
                    done.add (e.name);
                    check (tag + " edit " + e.name + ": changes the song and can be undone",
                           states.back() != states[states.size() - 2] && offline.canUndo());
                }
                note ("edits: " + done.joinIntoString (", "));
                const auto workBefore = offline.workSecondsNow();
                for (int k = (int) states.size() - 1; k > 0; --k)
                {
                    offline.undo();
                    offline.dispatchPendingMessages();
                    const auto now = sweep190reText (offline.getSong());
                    const auto diff = sweep190reFirstDiff (states[(std::size_t) k - 1], now);
                    if (diff.isNotEmpty())
                    {
                        note ("undo of " + done[k - 1] + ": " + diff);
                        dump (tag + " undo " + done[k - 1], states[(std::size_t) k - 1], now);
                    }
                    check (tag + " undo of " + done[k - 1] + ": back to the song before it", diff.isEmpty());
                }
                check (tag + " undo: nothing left to undo after the edits", ! offline.canUndo());
                const auto workAfter = offline.workSecondsNow();
                note ("working time before the undos " + juce::String (workBefore, 2) + " s, after " + juce::String (workAfter, 2) + " s");
                check (tag + " undo: the project's working time does not go back", workAfter + 1.0e-3 >= workBefore);
                for (int k = 1; k < (int) states.size(); ++k)
                {
                    offline.redo();
                    offline.dispatchPendingMessages();
                    const auto now = sweep190reText (offline.getSong());
                    const auto diff = sweep190reFirstDiff (states[(std::size_t) k], now);
                    if (diff.isNotEmpty())
                    {
                        note ("redo of " + done[k - 1] + ": " + diff);
                        dump (tag + " redo " + done[k - 1], states[(std::size_t) k], now);
                    }
                    check (tag + " redo of " + done[k - 1] + ": the edited song again", diff.isEmpty());
                }
            }
        }
        trackonly::setEnabled (wasTrackOnly);
        checks << "checks=" << (oks + fails) << " ok=" << oks << " fail=" << fails << "\nresult=" << (fails == 0 && oks > 0 ? "OK" : "FAIL") << "\n";
        folder.getChildFile ("checks.txt").replaceWithText (checks);
        return fails == 0 && oks > 0;
    }

    // Discord 11-25 (sidthenugget): in Track-only mode, a clip dragged onto another instrument's track. Real drags on the
    // main window's Playlist, in Track-only, Simple and the Pattern layout (FL: the clip keeps playing its own channel).
    // Errors are added to `errors`, what happened to `log`.
    void sweep190reTrackMove (juce::StringArray& errors, juce::StringArray& log)
    {
        using namespace lynx;
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        const auto wasTrackOnly = trackonly::enabled();
        const auto wasStyle = layoutstyle::current();
        struct Mode { const char* name; layoutstyle::Style style; bool trackOnly, follows; };
        for (const Mode mode : { Mode { "Track-only", layoutstyle::Style::pattern, true, true }, Mode { "Simple", layoutstyle::Style::simple, false, true },
                                 Mode { "Pattern", layoutstyle::Style::pattern, false, false } })
        {
            trackonly::setEnabled (mode.trackOnly);
            layoutstyle::setCurrent (layoutstyle::Style::pattern); // (process-wide; the window before this one left its own)
            AudioEngine e (false);
            MainComponent m (e, false);
            m.setSize (1600, 900);
            m.setVisible (true);
            if (mode.style == layoutstyle::Style::pattern && ! mode.trackOnly) // (FL's way must be back after a real switch)
                m.setLayoutStyle (layoutstyle::Style::simple);
            m.setLayoutStyle (mode.style);
            auto song = Song::makeDefault();
            song.channels.resize (2);
            for (int c = 0; c < 2; ++c)
            {
                song.channels[(std::size_t) c].name = c == 0 ? "Keys" : "Bass";
                song.channels[(std::size_t) c].instrument = InstrumentKind::synth;
            }
            song.playlistTracks.resize (3);
            trackmodes::makeInstrumentTrack (song, 0, 0);
            trackmodes::makeInstrumentTrack (song, 1, 1);
            song.patterns.assign (1, Pattern {});
            song.patterns[0].name = "Riff";
            song.patterns[0].notes = { { 0, ppq, 0, 60 }, { ppq, ppq, 0, 64 } };
            song.playlist.clear();
            for (const auto start : { 0, 8 }) // (two clips of one pattern)
            {
                Clip c;
                c.pattern = 0;
                c.track = 0;
                c.start = start * 4 * ppq;
                c.length = 4 * 4 * ppq;
                song.playlist.push_back (c);
            }
            song.mode = PlayMode::song;
            e.setSong (std::move (song));
            e.dispatchPendingMessages();
            auto& pl = m.getPlaylistForSnapshot();
            m.showDesktopForSnapshot (false);
            pl.setViewForSnapshot (70.0, 0, 0);
            e.dispatchPendingMessages();
            const auto channelsOf = [&e] (int clip) {
                std::set<int> out;
                for (const auto& n : e.getSong().patterns[(std::size_t) e.getSong().playlist[(std::size_t) clip].pattern].notes)
                    out.insert (n.channel);
                return out;
            };
            const auto drag = [&pl, &e] (int clip, juce::Point<int> by) {
                const auto source = juce::Desktop::getInstance().getMainMouseSource();
                const auto now = juce::Time::getCurrentTime();
                const auto from = pl.clipBoundsForSnapshot (clip).getCentre();
                const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
                const auto at = [&] (juce::Point<int> p, juce::ModifierKeys mods) {
                    return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pl, &pl, now, from.toFloat(), now, 1, p != from);
                };
                pl.mouseDown (at (from, left));
                for (int k = 1; k <= 6; ++k)
                    pl.mouseDrag (at (from + by * k / 6, left));
                pl.mouseUp (at (from + by, {}));
                e.dispatchPendingMessages();
            };
            const auto say = [&] (const juce::String& what, bool ok) {
                log.add (juce::String (ok ? "ok   " : "FAIL ") + mode.name + ": " + what);
                if (! ok)
                    errors.add (juce::String (mode.name) + ": " + what);
            };
            const auto rows = pl.trackRowForSnapshot (1).getCentreY() - pl.trackRowForSnapshot (0).getCentreY();
            const auto before = sweep190reText (e.getSong());
            drag (0, { 0, rows });
            int moved = -1, stayed = -1; // (the drop sorts the clips)
            for (int i = 0; i < (int) e.getSong().playlist.size(); ++i)
                (e.getSong().playlist[(std::size_t) i].track == 1 ? moved : stayed) = i;
            say ("the drag moved one clip to the Bass track", moved >= 0 && stayed >= 0);
            if (moved < 0 || stayed < 0)
                continue;
            const auto want = std::set<int> { mode.follows ? 1 : 0 };
            say ("the moved clip plays " + juce::String (mode.follows ? "the Bass track's instrument" : "its own channel (FL)"), channelsOf (moved) == want);
            say ("the other clip of that pattern still plays Keys", channelsOf (stayed) == std::set<int> { 0 });
            e.undo();
            e.dispatchPendingMessages();
            say ("one undo puts the song back", sweep190reFirstDiff (before, sweep190reText (e.getSong())).isEmpty());
        }
        layoutstyle::setCurrent (wasStyle);
        trackonly::setEnabled (wasTrackOnly);
    }

    // The self-test's `sweep190re` item: the fast part of the sweep. "OK" or what failed.
    //  1. undo / redo keep the project's working time (it went back to the undone step's count);
    //  2. "Nothing to play in Song mode yet" goes when it stops being true (a clip placed, a project opened, another layout);
    //  3. --sweep190re on a small generated project (File › Open path, four layouts, five panels, save / reload, ten undo kinds);
    //  4. a clip dragged onto another instrument's track (Track-only and Simple: that instrument; Pattern: FL's way).
    juce::String sweep190reSelfTest()
    {
        using namespace lynx;
        juce::StringArray errors;
        const auto wasStyle = layoutstyle::current();
        { // 1
            AudioEngine e (false);
            e.capturePluginStates(); // (what a save does first: the working time so far goes into the song; the clock starts)
            e.editSong ([] (Song& s) { s.bpm = 128.0; });
            juce::Thread::sleep (1100);
            e.capturePluginStates();
            e.editSong ([] (Song& s) { s.bpm = 129.0; });
            e.capturePluginStates();
            const auto before = e.getSong().info.workSeconds;
            e.undo();
            const auto afterUndo = e.getSong().info.workSeconds;
            e.undo();
            e.redo();
            const auto afterRedo = e.getSong().info.workSeconds;
            if (before < 1.0 || afterUndo < before || afterRedo < before)
                errors.add ("working time " + juce::String (before, 2) + " s went to " + juce::String (afterUndo, 2) + " / " + juce::String (afterRedo, 2) + " s");
        }
        { // 2
            const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
            const auto emptySongMode = [] {
                auto s = Song::makeDefault();
                s.playlist.clear();
                s.arrangements.clear();
                s.mode = PlayMode::song;
                return s;
            };
            struct Case { const char* name; int how; };
            for (const Case c : { Case { "a clip placed", 0 }, Case { "a project opened", 1 }, Case { "the Simple layout", 2 }, Case { "Pattern mode", 3 } })
            {
                AudioEngine e (false);
                MainComponent m (e, false);
                m.setSize (1200, 800);
                m.setLayoutStyle (layoutstyle::Style::pattern);
                m.setOnboardingForSnapshot (true);
                e.setSong (emptySongMode());
                e.dispatchPendingMessages();
                if (! m.maybeShowTip (onboarding::Tip::songModeEmpty, &m) || m.tipForSnapshot() == nullptr)
                {
                    errors.add (juce::String ("tip not shown (") + c.name + ")");
                    continue;
                }
                if (c.how == 0)
                    e.editSong ([] (Song& s) { s.playlist.push_back ({ 0, 0, 0, 4 * ppq, 0 }); });
                else if (c.how == 1)
                {
                    auto full = Song::makeDefault();
                    full.playlist.push_back ({ 0, 0, 0, 4 * ppq, 0 });
                    full.mode = PlayMode::song;
                    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sweep190re-tip.lynx");
                    juce::String error;
                    ProjectFile::save (full, file, error);
                    m.openFile (file);
                    file.deleteFile();
                }
                else if (c.how == 2)
                    m.setLayoutStyle (layoutstyle::Style::simple);
                else
                    e.editSong ([] (Song& s) { s.mode = PlayMode::pattern; });
                for (int k = 0; k < 3; ++k)
                    e.dispatchPendingMessages();
                if (m.tipForSnapshot() != nullptr)
                    errors.add (juce::String ("the Song-mode tip stayed after ") + c.name);
                m.setLayoutStyle (layoutstyle::Style::pattern);
            }
        }
        { // 3
            auto song = Song::makeDefault();
            song.playlist.push_back ({ 0, 0, 0, 4 * ppq, 0 });
            song.playlist.push_back ({ 0, 1, 4 * ppq, 4 * ppq, 0 });
            song.mode = PlayMode::song;
            if (song.mixer.size() < 3)
                song.mixer.resize (3);
            Automation a;
            a.name = "Volume";
            a.target = { AutomationTargetKind::mixerVolume, 1, 0, 0 };
            a.points = { { 0, 0.2f, 0.0f }, { 4 * ppq, 0.8f, 0.0f } };
            song.automations.push_back (a);
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sweep190re-selftest");
            folder.deleteRecursively();
            folder.createDirectory();
            const auto file = folder.getChildFile ("sweep.lynx");
            juce::String error;
            ProjectFile::save (song, file, error);
            const auto ok = runSweep190re (folder.getChildFile ("out").getFullPathName(), { file.getFullPathName() }, true);
            if (! ok)
            {
                juce::StringArray lines;
                lines.addLines (folder.getChildFile ("out").getChildFile ("checks.txt").loadFileAsString());
                for (const auto& l : lines)
                    if (l.startsWith ("FAIL"))
                        errors.add ("route: " + l.substring (5).trim());
                if (errors.isEmpty())
                    errors.add ("route failed");
            }
            folder.deleteRecursively();
        }
        { // 4
            juce::StringArray log;
            sweep190reTrackMove (errors, log);
        }
        layoutstyle::setCurrent (wasStyle);
        return errors.isEmpty() ? juce::String ("OK") : "ERROR " + errors.joinIntoString ("; ");
    }

    // LYNX --tracker190nwshot <prefix> [module] (round 190nw, 0.1.31: the built-in tracker, stage 1) off-screen through the
    // real components: the tracker tool window's editor on the wireframe's pattern (study190nd/tracker/wireframe-tracker),
    // Edit on with a block, playing, decimal values, the Pan column; keys typed through its own keyPressed (notes, OFF,
    // values, Insert / Backspace, copy / paste, transpose) and checked against the pattern; the View, Pattern and Clip-launch
    // clip menus with their new items; the Simple dock's Tracker tab; with [module], that module imported and shown, its
    // report listed. The paint bench: a 64-track x 512-row pattern at 1920x1080 (full frame, and the rows a playhead step
    // repaints). <prefix>-checks.txt lists the checks. @150: LYNX_SHOT_SCALE=1.5 LYNX_SHOT_SUFFIX=@150.
    bool runTrackerShot190nw (const juce::String& prefix, const juce::String& modulePath)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (lynx::ui::showingForShots(), true);
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const char* name, bool ok, const juce::String& detail = {}) {
            report.add (juce::String (name) + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        const auto line = [&report] (const juce::String& text) { report.add ("# " + text); };
        const auto finish = [&] {
            juce::File (prefix + "-checks.txt").replaceWithText (report.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
            return allOk;
        };
        using namespace lynx;
        ui::motion::enabled() = false;
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        const auto rt = (std::int64_t) ticksPerStep;
        const auto write = [&prefix] (juce::Component& c, const juce::String& name) {
            c.resized();
            savePng (shot::snapshot (c), prefix + "-" + name + ".png");
        };
        const auto note = [] (int channel, int key, std::int64_t start, std::int64_t length, float velocity = 0.78f) {
            Note n;
            n.channel = channel;
            n.key = key;
            n.start = start;
            n.length = length;
            n.velocity = velocity;
            return n;
        };

        // ---- the wireframe's song: Kick, Snare, Bass, Lead + Lead 2 (a group), Pad (chords); "03 Verse" of 2 bars
        auto song = Song::makeDefault();
        song.channels.clear();
        song.patterns.clear();
        song.playlist.clear();
        for (const auto& [name, colour] : { std::pair<const char*, std::uint32_t> { "Kick", 0xFFE5383Bu }, { "Snare", 0xFFF5A742u }, { "Bass - 3x Osc", 0xFF6EA8FEu },
                                            { "Lead", 0xFF3DDC84u }, { "Lead 2", 0xFF3DDC84u }, { "Pad", 0xFF9D7CF0u } })
        {
            ChannelDef c;
            c.name = name;
            c.colour = colour;
            song.channels.push_back (c);
        }
        uids::normalize (song);
        song.tracker.groups.push_back ({ "Lead", { song.channels[3].uid, song.channels[4].uid } });
        for (const auto* name : { "Intro", "Break", "Verse", "Fill", "Chorus" })
            song.patterns.emplace_back().name = name;
        auto& verse = song.patterns[2];
        verse.lengthTicks = 2 * song.barTicks();
        for (int r = 0; r < 32; r += 8)
            verse.notes.push_back (note (0, 48, r * rt, rt, 100.0f / 127.0f));
        for (int r = 4; r < 32; r += 8)
            verse.notes.push_back (note (1, 50, r * rt, rt, 90.0f / 127.0f));
        verse.notes.push_back (note (1, 50, 14 * rt + 0x30, rt / 2, 0x30 / 127.0f));
        verse.notes.push_back (note (2, 36, 0, 3 * rt, 112.0f / 127.0f));
        verse.notes.push_back (note (2, 36, 4 * rt, 4 * rt, 96.0f / 127.0f));
        auto slide = note (2, 39, 8 * rt, 3 * rt, 112.0f / 127.0f);
        slide.slide = true;
        verse.notes.push_back (slide);
        verse.notes.push_back (note (2, 31, 12 * rt, 4 * rt, 112.0f / 127.0f));
        auto chance = note (2, 36, 24 * rt, 8 * rt, 112.0f / 127.0f);
        chance.chance = 0.5f;
        verse.notes.push_back (chance);
        verse.notes.push_back (note (3, 55, 0, 2 * rt, 0x60 / 127.0f));
        verse.notes.push_back (note (4, 58, 2 * rt, 2 * rt, 0x58 / 127.0f));
        auto porta = note (3, 60, 4 * rt, 2 * rt, 0x60 / 127.0f);
        porta.porta = true;
        verse.notes.push_back (porta);
        verse.notes.push_back (note (4, 63, 6 * rt, 2 * rt, 0x58 / 127.0f));
        auto rel = note (3, 55, 16 * rt, 4 * rt, 0x60 / 127.0f);
        rel.release = 0x40 / 127.0f + 0.25f;
        verse.notes.push_back (rel);
        for (const auto& [a, b] : { std::pair<int, int> { 60, 63 }, { 58, 62 } })
        {
            const auto start = a == 60 ? 0 : 16 * rt;
            verse.notes.push_back (note (5, a, start, 16 * rt - 2 * rt, 0x48 / 127.0f));
            verse.notes.push_back (note (5, b, start, 16 * rt - 2 * rt, 0x48 / 127.0f));
        }
        verse.sortNotes();
        song.currentPattern = 2;
        song.patterns[0].lengthTicks = song.patterns[4].lengthTicks = 4 * song.barTicks();
        for (const auto& [pattern, bars] : { std::pair<int, int> { 0, 4 }, { 2, 8 }, { 2, 8 }, { 4, 8 }, { 2, 8 } })
        {
            Clip c;
            c.pattern = pattern;
            c.start = song.playlist.empty() ? 0 : song.playlist.back().end();
            c.length = bars * song.barTicks() / 4;
            song.playlist.push_back (c);
        }

        AudioEngine engine (false);
        engine.setSong (song);
        engine.setSelectedChannel (2);
        engine.dispatchPendingMessages();

        // ---- 1. the tool window's editor
        TrackerComponent tracker (engine);
        tracker.setSize (1280, 760);
        engine.dispatchPendingMessages();
        const auto& layout = tracker.layoutForCheck();
        check ("tracks: Kick, Snare, Bass, the Lead group, Pad", layout.tracks.size() == 5 && layout.tracks[3].group && layout.tracks[3].channels.size() == 2,
               juce::String ((int) layout.tracks.size()));
        check ("the Pad's chord has two note columns", layout.tracks.size() == 5 && layout.tracks[4].noteColumns == 2);
        check ("32 rows at 4 rows a beat", layout.rows == 32 && layout.rowTicks == 240);
        tracker::Cursor c;
        c.track = 2;
        c.row = 8;
        c.field = tracker::Field::fxLetter;
        tracker.setCursorForCheck (c);
        write (tracker, "01-tracker-window");
        tracker.setEditForCheck (true);
        tracker::Cursor from, to;
        from.row = 4;
        to.track = 1;
        to.row = 11;
        tracker.setSelectionForCheck (from, to);
        write (tracker, "02-edit-and-block");
        tracker.setCursorForCheck (c);
        tracker.setPlayRowForSnapshot (9);
        write (tracker, "03-playing");
        tracker.setPlayRowForSnapshot (-1);
        TrackerComponent::hex = false;
        tracker.resized();
        write (tracker, "04-decimal");
        TrackerComponent::hex = true;
        engine.editSong ([] (Song& s) { s.tracker.showPan = true; });
        engine.dispatchPendingMessages();
        tracker.resized();
        write (tracker, "05-pan-column");
        engine.editSong ([] (Song& s) { s.tracker.showPan = false; });
        engine.dispatchPendingMessages();

        // ---- 2. keys through the editor's own handler, against the pattern (the Piano roll's data)
        {
            const auto notes = [&engine]() -> const std::vector<Note>& { return engine.getSong().patterns[2].notes; };
            const auto find = [&] (int channel, std::int64_t start) -> const Note* {
                for (const auto& n : notes())
                    if (n.channel == channel && n.start == start)
                        return &n;
                return nullptr;
            };
            const auto press = [&tracker, &engine] (int code, juce::ModifierKeys mods = {}) {
                tracker.keyForCheck (juce::KeyPress (code, mods, 0));
                engine.dispatchPendingMessages();
            };
            const auto before = notes().size();
            tracker.setEditForCheck (true);
            TrackerComponent::octave = 4;
            TrackerComponent::editStep = 1;
            tracker::Cursor k;
            k.track = 0;
            k.row = 2;
            tracker.setCursorForCheck (k);
            press ('z');                         // C4 on the Kick's row 2
            const auto* typed = find (0, 2 * rt);
            check ("Z types C4 (MIDI 48) at the cursor", typed != nullptr && typed->key == 48 && notes().size() == before + 1);
            check ("the cursor moves on by the edit step", tracker.cursorForCheck().row == 3);
            press ('1');                         // OFF on row 3: the new note ends there
            typed = find (0, 2 * rt);
            check ("1 = OFF ends the note above", typed != nullptr && typed->length == rt);
            k.row = 2;
            k.field = tracker::Field::vol;
            tracker.setCursorForCheck (k);
            press ('4');
            press ('0');
            typed = find (0, 2 * rt);
            check ("4 0 in Vol: velocity 40 hex", typed != nullptr && std::lround (typed->velocity * 127.0f) == 0x40);
            engine.undo();
            engine.dispatchPendingMessages();
            typed = find (0, 2 * rt);
            check ("one undo step for the two digits", typed != nullptr && std::lround (typed->velocity * 127.0f) != 0x40);
            engine.redo();
            engine.dispatchPendingMessages();
            k.field = tracker::Field::note;
            k.row = 2;
            tracker.setCursorForCheck (k);
            press (juce::KeyPress::upKey, juce::ModifierKeys::ctrlModifier); // transpose the note at the cursor up
            typed = find (0, 2 * rt);
            check ("Ctrl+Up transposes a semitone", typed != nullptr && typed->key == 49);
            press (juce::KeyPress::insertKey);  // Insert on row 2 of the Kick track: its notes from row 2 move down
            check ("Insert pushes the track's notes down a row", find (0, 3 * rt) != nullptr && find (0, 2 * rt) == nullptr && find (0, 9 * rt) != nullptr);
            k.row = 3;
            tracker.setCursorForCheck (k);
            press (juce::KeyPress::backspaceKey); // Backspace pulls them back
            check ("Backspace pulls them back up", find (0, 2 * rt) != nullptr && find (0, 8 * rt) != nullptr && tracker.cursorForCheck().row == 2);
            // copy the Kick's first 8 rows and paste them at row 16 of the Snare
            tracker::Cursor a, b;
            a.row = 0;
            b.row = 7;
            tracker.setSelectionForCheck (a, b);
            press ('c', juce::ModifierKeys::ctrlModifier);
            k.track = 1;
            k.row = 16;
            tracker.setCursorForCheck (k);
            press ('v', juce::ModifierKeys::ctrlModifier);
            check ("Ctrl+C / Ctrl+V: the Kick's notes on the Snare at row 16", find (1, 16 * rt) != nullptr && find (1, 16 * rt)->key == 48);
            // Del clears; the Piano roll sees the same notes (the tracker is a view)
            k.track = 0;
            k.row = 2;
            tracker.setCursorForCheck (k);
            press (juce::KeyPress::deleteKey);
            check ("Del removes the note at the cursor", find (0, 2 * rt) == nullptr);
            line ("pattern notes after the key script: " + juce::String ((int) notes().size()));
            tracker.setEditForCheck (false);
            // with Edit off a typing key is not the tracker's (the main window's typing piano plays it)
            k.row = 5;
            tracker.setCursorForCheck (k);
            check ("Edit off: Z is left to the typing piano", ! tracker.keyForCheck (juce::KeyPress ('z')));
            engine.setSong (song); // (the wireframe again for the next pictures)
            engine.setSelectedChannel (2);
            engine.dispatchPendingMessages();
        }

        // ---- 3. the menus: View › Tracker, Pattern › Edit in tracker (Pattern layout), a clip's "Edit in tracker"
        {
            MainComponent ui (engine, false);
            ui.setSize (1920, 1080);
            engine.dispatchPendingMessages();
            const auto names = ui.getMenuNamesForSnapshot();
            const auto texts = [] (const juce::PopupMenu& m) {
                juce::StringArray out;
                for (juce::PopupMenu::MenuItemIterator it (m, true); it.next();)
                    out.add (it.getItem().text);
                return out;
            };
            for (int m = 0; m < names.size(); ++m)
            {
                const auto menu = ui.getMenuForSnapshot (m);
                const auto items = texts (menu);
                const auto has = items.joinIntoString ("|").contains (tr ("Tracker")) || items.joinIntoString ("|").contains (tr ("Edit in tracker"));
                if (has)
                {
                    saveMenus (juce::StringArray { names[m] }, std::vector<juce::PopupMenu> { menu }, prefix + "-06-menu-" + names[m].toLowerCase() + ".png");
                    line ("menu " + names[m] + " has the tracker");
                }
            }
            const auto menuNamed = [&] (const juce::String& name) { return names.indexOf (name) >= 0 ? ui.getMenuForSnapshot (names.indexOf (name)) : juce::PopupMenu(); };
            check ("View has Tracker", texts (menuNamed (tr ("View"))).joinIntoString ("|").contains (tr ("Tracker")));
            check ("Patterns has Edit in tracker", texts (menuNamed (tr ("Patterns"))).joinIntoString ("|").contains (tr ("Edit in tracker")));
            // round 190ob: the Channel rack's channel menu, "Edit in tracker" right after FL's Piano roll (with its key)
            {
                const auto channelMenu = ui.getRackForSnapshot().getChannelMenuForSnapshot (2);
                const auto items = texts (channelMenu);
                int pianoAt = -1, trackerAt = -1;
                for (int k = 0; k < items.size(); ++k)
                {
                    if (pianoAt < 0 && items[k].startsWith (tr ("Piano Roll")))
                        pianoAt = k;
                    if (trackerAt < 0 && items[k].startsWith (tr ("Edit in tracker")))
                        trackerAt = k;
                }
                check ("190ob: the channel menu has Edit in tracker after Piano Roll", pianoAt >= 0 && trackerAt == pianoAt + 1, items.joinIntoString (" | "));
                check ("190ob: Edit in tracker shows its key (Shift+F7)", trackerAt >= 0 && items[trackerAt].contains ("Shift+F7"), trackerAt >= 0 ? items[trackerAt] : juce::String());
                saveMenus (juce::StringArray { "Channel" }, std::vector<juce::PopupMenu> { channelMenu }, prefix + "-12-channel-menu.png");
            }
            // Clip-launch: a pattern clip in the grid
            ui.setLayoutStyle (layoutstyle::Style::clipLaunch, false);
            engine.dispatchPendingMessages();
            ui.setClipGridView (PlaylistArea::GridView::grid);
            engine.dispatchPendingMessages();
            if (auto* grid = ui.getClipGridForSnapshot())
            {
                engine.editSong ([] (Song& s) {
                    if (! s.clipGrid)
                        s.clipGrid.emplace();
                    Clip clip;
                    clip.pattern = 2;
                    clip.length = s.patterns[2].effectiveLength (s.meter());
                    launcher::setClip (s, 0, 0, clip);
                });
                engine.dispatchPendingMessages();
                const auto menu = clipGridSlotMenu (*grid, 0, 0);
                check ("a clip of the grid has Edit in tracker", texts (menu).contains (tr ("Edit in tracker")), texts (menu).joinIntoString (" | "));
                saveMenus (juce::StringArray { "Clip" }, std::vector<juce::PopupMenu> { menu }, prefix + "-07-clip-launch-clip-menu.png");
            }
            else
                check ("the clip grid", false);
            // Simple: an Instrument track with the Verse's bass line, its Tracker tab
            ui.setLayoutStyle (layoutstyle::Style::simple, false);
            engine.dispatchPendingMessages();
            if (auto* simple = ui.getSimpleWorkflowForSnapshot())
            {
                simpletracks::Request r;
                r.kind = simpletracks::Kind::instrument;
                r.name = "Bass";
                r.channel = ChannelDef {};
                Pattern bass;
                for (const auto& n : song.patterns[2].notes)
                    if (n.channel == 2)
                        bass.notes.push_back (n);
                r.pattern = bass;
                int track = -1;
                engine.editSong ([&r, &track] (Song& s) { track = simpletracks::addTrack (s, r).track; });
                engine.dispatchPendingMessages();
                if (auto* dock = simple->dockForSnapshot(); dock != nullptr && track >= 0)
                {
                    ui.getPlaylistForSnapshot().selectTrack (track);
                    simple->trackSelected (track);
                    dock->showTrack (track);
                    dock->setTab (SimpleDock::Tab::tracker);
                    engine.dispatchPendingMessages();
                    dock->resized();
                    auto* t = dynamic_cast<TrackerComponent*> (dock->trackerForSnapshot());
                    check ("the Simple dock's Tracker tab shows the track's pattern", t != nullptr && t->layoutForCheck().tracks.size() == 1
                                                                                          && ! t->layoutForCheck().placed.empty());
                    savePng (shot::paint (*dock, theme::colour::bg1), prefix + "-08-simple-tracker-tab.png");
                    savePng (shot::snapshot (ui), prefix + "-09-simple-layout.png");
                }
                else
                    check ("the Simple dock", false);
            }
            ui.setLayoutStyle (layoutstyle::Style::pattern, false);
            engine.dispatchPendingMessages();
        }

        // ---- 4. a module, imported through the engine and shown
        if (modulePath.isNotEmpty())
        {
            AudioEngine moduleEngine (false);
            juce::String error;
            const auto ok = moduleEngine.importFlpFile (juce::File (modulePath), error);
            check ("the module imports", ok, error);
            if (ok)
            {
                moduleEngine.dispatchPendingMessages();
                const auto& s = moduleEngine.getSong();
                line ("module: " + juce::String::fromUTF8 (s.info.comments.c_str()) + ", " + juce::String ((int) s.channels.size()) + " channels, "
                      + juce::String ((int) s.patterns.size()) + " patterns, " + juce::String ((int) s.playlist.size()) + " clips, " + juce::String (s.bpm, 2) + " BPM, "
                      + juce::String (s.tracker.rowsPerBeat) + " rows a beat");
                const auto& r = moduleEngine.getImportReport();
                for (const auto* list : { &r.converted, &r.approximated, &r.notReproduced })
                    for (const auto& [what, n] : *list)
                        line ("report: " + juce::String::fromUTF8 (what.c_str()) + " x" + juce::String (n));
                TrackerComponent mt (moduleEngine);
                mt.setSize (1280, 760);
                moduleEngine.dispatchPendingMessages();
                if (! s.playlist.empty())
                    mt.showPattern (s.playlist.front().pattern);
                write (mt, "10-module-" + juce::File (modulePath).getFileNameWithoutExtension());
                // round 190om: the first note with a pitch curve (Bxx), its row at the top
                for (int p = 0; p < (int) s.patterns.size(); ++p)
                {
                    const auto& notes = s.patterns[(std::size_t) p].notes;
                    const auto curved = std::find_if (notes.begin(), notes.end(), [] (const Note& n) { return n.curves.has(); });
                    if (curved == notes.end())
                        continue;
                    mt.showChannel (curved->channel);
                    mt.showPattern (p);
                    const auto rowTicks = (std::int64_t) ppq / std::max (1, s.tracker.rowsPerBeat);
                    mt.scrollToRowForCheck (std::max<std::int64_t> (0, curved->start / rowTicks - 2));
                    check ("a note with a pitch curve shows Bxx", true);
                    write (mt, "13-module-bend-" + juce::File (modulePath).getFileNameWithoutExtension());
                    break;
                }
            }
        }

        // ---- 5. the paint bench: 64 tracks x 512 rows at 1920x1080 (16 rows a beat), full frames and a playhead step
        {
            auto big = Song::makeDefault();
            big.channels.clear();
            big.patterns.clear();
            for (int ch = 0; ch < 64; ++ch)
                big.channels.emplace_back().name = "Ch " + std::to_string (ch + 1);
            big.tracker.rowsPerBeat = 16;
            auto& p = big.patterns.emplace_back();
            p.name = "Bench";
            const auto row = (std::int64_t) ppq / 16;
            p.lengthTicks = 512 * row;
            for (int ch = 0; ch < 64; ++ch)
                for (int r = 0; r < 512; r += 1 + ch % 4)
                    p.notes.push_back (note (ch, 36 + (r + ch) % 48, r * row, row * (1 + ch % 4)));
            p.sortNotes();
            big.currentPattern = 0;
            AudioEngine benchEngine (false);
            benchEngine.setSong (big);
            benchEngine.dispatchPendingMessages();
            TrackerComponent bench (benchEngine);
            bench.setSize (1920, 1080);
            const auto built = std::chrono::steady_clock::now();
            bench.showPattern (0);
            const auto buildMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - built).count();
            const auto ms = bench.paintBenchMs (20);
            const auto rowsMs = bench.paintRowsBenchMs (100);
            line ("paint bench: a playhead step (two rows) " + juce::String (rowsMs, 3) + " ms");
            line ("paint bench 64 x 512 at 1920x1080: " + juce::String (ms, 3) + " ms a full frame, layout rebuild " + juce::String (buildMs, 3) + " ms ("
                  + juce::String ((int) p.notes.size()) + " notes)");
            check ("the layout rebuild of a 10k-note pattern under 20 ms", buildMs < 20.0, juce::String (buildMs, 2));
            // round 190ob: every kind of frame, median wall time and instructions, and the frame's pixels
            for (const auto& [kind, name] : { std::pair<TrackerComponent::BenchFrame, const char*> { TrackerComponent::BenchFrame::same, "full frame, same" },
                                              { TrackerComponent::BenchFrame::scroll, "full frame, scrolled a row" },
                                              { TrackerComponent::BenchFrame::uncached, "full frame, nothing cached" },
                                              { TrackerComponent::BenchFrame::edit, "full frame, after an edit in one track" },
                                              { TrackerComponent::BenchFrame::playhead, "playhead step (two rows)" } })
            {
                const auto r = bench.paintBench (kind, 30);
                line ("bench190ob " + juce::String (name) + ": median " + juce::String (r.ms, 3) + " ms, instructions median "
                      + juce::String (r.instructions) + ", pixels " + juce::String::toHexString ((juce::int64) r.pixels));
            }
            write (bench, "11-bench-64x512");
            // the cached frames are the uncached frames, pixel for pixel: scrolled, the cursor, a block, the playhead, decimal
            // Round 190pb (owner's call, rollup 0.1.31 step 5): on Windows within 1 level per channel. There the chunks are
            // Direct2D bitmaps and the straight frame is drawn by Direct2D into another bitmap; the header
            // picture's track separators come out 1 level apart between the two (Direct2D's own antialiasing)
            // (66 pixels at 1 level in step 5, s5/win/trdump9), which the software renderer of Linux / macOS never does:
            // there the check stays exact. Any pixel more than 1 level off still fails on Windows.
            {
               #if JUCE_WINDOWS
                constexpr int tolerance = 1;
               #else
                constexpr int tolerance = 0;
               #endif
                bool same = true;
                juce::String where;
                int nearPixels = 0, nearLevel = 0;
                const auto compare = [&] (const juce::String& state) {
                    const auto a = bench.frameForCheck (true), b = bench.frameForCheck (true), c = bench.frameForCheck (false);
                    int largestA = 0, anyA = 0, largestB = 0, anyB = 0;
                    const auto overA = TrackerComponent::framesDifferForCheck (a, c, tolerance, largestA, anyA);
                    const auto overB = TrackerComponent::framesDifferForCheck (b, c, tolerance, largestB, anyB);
                    if (overA > 0 || overB > 0)
                    {
                        same = false;
                        where << state << " (" << juce::jmax (overA, overB) << " px, up to " << juce::jmax (largestA, largestB) << " levels) ";
                    }
                    nearPixels = juce::jmax (nearPixels, juce::jmax (anyA, anyB));
                    nearLevel = juce::jmax (nearLevel, juce::jmax (largestA, largestB));
                };
                compare ("top");
                bench.scrollToRowForCheck (37);
                compare ("row37");
                tracker::Cursor cur;
                cur.track = 2;
                cur.row = 45;
                cur.field = tracker::Field::vol;
                bench.setCursorForCheck (cur);
                compare ("cursor");
                tracker::Cursor a0, a1;
                a0.track = 1;
                a0.row = 40;
                a1.track = 3;
                a1.row = 52;
                bench.setSelectionForCheck (a0, a1);
                compare ("block");
                bench.setPlayRowForSnapshot (50);
                compare ("playhead");
                bench.setPlayRowForSnapshot (-1);
                TrackerComponent::hex = false;
                bench.resized();
                compare ("decimal");
                TrackerComponent::hex = true;
                bench.resized();
                bench.scrollToRowForCheck (512);
                compare ("end");
                line ("bench190ob pixel tolerance " + juce::String (tolerance) + " level(s); pixels apart at all: up to " + juce::String (nearPixels)
                      + " a frame, by up to " + juce::String (nearLevel) + " level(s)");
                check (tolerance == 0 ? "bench190ob: the cached frames equal the uncached ones pixel for pixel"
                                      : "bench190ob: the cached frames equal the uncached ones within 1 level (Windows: Direct2D antialiasing)",
                       same, where);
                line ("bench190ob chunks cached: " + juce::String ((int) bench.cachedChunksForCheck()));
            }
        }
        return finish();
    }

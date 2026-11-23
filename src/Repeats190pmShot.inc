    // --repeats190pmshot <folder> (round 190pm, research190oe P19 and P23), offscreen through the real main window:
    //  - P19, Note Repeats and Conditions: the Piano roll's Ratchets / Ratchet velocity / Condition lanes and the notes'
    //    marks, its "Repeats and conditions (LYNX)" menu, the Note inspector's rows; .lynx round trip; the copies for
    //    0.1.30 / 0.1.28 (ratchets as notes, one line each); the Piano roll's paint time with and without them;
    //  - P23, Touch mode: the Simple layout off / on (Large controls), the suggestion card, the value pop-up; synthetic
    //    fingers on the Piano roll and the Playlist (a tap draws a snapped note, a long press opens the note's / clip's
    //    menu and deletes nothing, two fingers pan and pinch and edit nothing; a mouse is not held back; outside the
    //    Simple layout a second finger is ignored).
    // <folder>/checks.txt lists the checks; pictures at 100 % and @150. Included in the app class of Main.cpp.
    bool runRepeatsShot190pm (const juce::String& folderPath)
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
        const auto save = [&folder] (juce::Component& c, juce::Rectangle<int> area, const juce::String& name) {
            area = area.getIntersection (c.getLocalBounds());
            if (area.isEmpty())
                return;
            for (const auto scale : { 1.0f, 1.5f })
                savePng (c.createComponentSnapshot (area, true, scale), folder.getChildFile (name + (scale > 1.0f ? "@150.png" : ".png")).getFullPathName());
        };
        using namespace lynx;
        using workspace::Panel;
        namespace st = simpletracks;
        namespace tm = ui::touchmode;
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        const auto bar = (std::int64_t) 4 * ppq;

        AudioEngine offline (false);
        MainComponent ui (offline, false);
        ui.setSize (1600, 900);

        // ================= P19 =================
        const auto repeatNotes = [] {
            std::vector<Note> notes;
            const auto add = [&notes] (std::int64_t start, std::int64_t length, int key, float velocity) -> Note& {
                Note n;
                n.start = start;
                n.length = length;
                n.key = key;
                n.velocity = velocity;
                notes.push_back (n);
                return notes.back();
            };
            add (0, ppq, 60, 0.8f).ratchets = 4;                               // a 1/16 roll
            auto& fade = add (ppq, ppq, 62, 0.9f);                              // three strikes fading
            fade.ratchets = 3;
            fade.ratchetVelocity = -0.6f;
            add (2 * ppq, ppq / 2, 64, 0.7f).condition = notecondition::firstPass;
            auto& cyc = add (2 * ppq + ppq / 2, ppq / 2, 65, 0.7f);              // passes 2 and 4 of 4
            cyc.cycle = 4;
            cyc.cycleMask = 0b1010;
            add (3 * ppq, ppq, 67, 0.75f).condition = notecondition::fill;
            add (3 * ppq, ppq, 60, 0.6f);
            auto& every = add (3 * ppq + ppq / 2, ppq / 2, 72, 0.6f);           // 1:2 with 8 strikes
            every.cycle = 2;
            every.cycleMask = 1;
            every.ratchets = 8;
            return notes;
        };
        offline.editSong ([&] (Song& s) {
            s.bpm = 120.0;
            if (s.channels.empty())
                s.channels.resize (1);
            if (s.patterns.empty())
                s.patterns.resize (1);
            s.patterns[0].notes = repeatNotes();
            s.patterns[0].sortNotes();
            s.currentPattern = 0;
        });
        offline.dispatchPendingMessages();
        ui.showPanelsForSnapshot ({ Panel::pianoRoll });
        offline.setSelectedChannel (0);
        offline.dispatchPendingMessages();
        auto& pr = ui.getPianoRollForSnapshot();
        pr.setSize (1200, 640);
        pr.resized();
        pr.scrollToNotes();
        offline.dispatchPendingMessages();
        const auto paintMs = [] (juce::Component& c) {
            juce::Image image (juce::Image::ARGB, c.getWidth(), c.getHeight(), true);
            std::vector<double> t;
            for (int i = 0; i < 25; ++i)
            {
                juce::Graphics g (image);
                const auto start = juce::Time::getMillisecondCounterHiRes();
                c.paintEntireComponent (g, false);
                t.push_back (juce::Time::getMillisecondCounterHiRes() - start);
            }
            std::sort (t.begin(), t.end());
            return t[t.size() / 2];
        };
        for (const auto& [lane, name] : { std::pair { PianoRollComponent::laneRatchets, "ratchets" }, { PianoRollComponent::laneRatchetVelocity, "ratchetvel" },
                                          { PianoRollComponent::laneCondition, "condition" }, { PianoRollComponent::laneChance, "chance" },
                                          { PianoRollComponent::laneVelocity, "velocity" } })
        {
            pr.setLaneForSnapshot (lane);
            offline.dispatchPendingMessages();
            save (pr, pr.getLocalBounds(), juce::String ("p19-pianoroll-") + name);
        }
        const auto withMarks = paintMs (pr);
        {
            // the same notes without the new fields: the paint cost of the marks
            const auto saved = offline.getSong().patterns[0].notes;
            offline.editSong ([] (Song& s) {
                for (auto& n : s.patterns[0].notes)
                    n.ratchets = 0, n.ratchetVelocity = 0.0f, n.condition = 0, n.cycle = 0, n.cycleMask = 0;
            });
            offline.dispatchPendingMessages();
            const auto plain = paintMs (pr);
            note ("Piano roll paint (1200 x 640, velocity lane): " + juce::String (plain, 3) + " ms plain, " + juce::String (withMarks, 3) + " ms with the marks");
            offline.editSong ([saved] (Song& s) { s.patterns[0].notes = saved; });
            offline.dispatchPendingMessages();
        }
        {
            const auto& notes = offline.getSong().patterns[0].notes;
            std::vector<std::string> laneNames;
            juce::PopupMenu menu = pr.repeatsMenu();
            juce::StringArray items;
            for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
                items.add (it.getItem().text);
            note ("repeats menu: " + items.joinIntoString (" | "));
            check ("the Repeats and conditions menu: ratchets, the five conditions, cycles of 2 to 8, Fill, reset, the lanes",
                   items.contains (tr ("{0} strikes", { "4" })) && items.contains (tr ("First pass")) && items.contains (tr ("Not fill"))
                       && items.contains ("2+4:4") && items.contains ("8:8") && items.contains (tr ("Fill (the notes with a Fill condition play)"))
                       && items.contains (tr ("Reset ratchets and conditions")) && items.contains (tr ("Show the condition lane")));
            saveMenus (juce::StringArray { tr ("Repeats and conditions (LYNX)") }, std::vector<juce::PopupMenu> { menu },
                       folder.getChildFile ("p19-menu-repeats.png").getFullPathName());
            // the Note inspector of the 2+4:4 note
            int cycNote = -1;
            for (int i = 0; i < (int) notes.size(); ++i)
                if (notes[(std::size_t) i].cycle == 4)
                    cycNote = i;
            pr.setSelectionForSnapshot ({ cycNote });
            if (auto inspector = pr.makeNoteInspectorForSnapshot (true))
            {
                inspector->setBounds (inspector->getLocalBounds());
                save (*inspector, inspector->getLocalBounds(), "p19-inspector");
                check ("the Note inspector has the Ratchets, Ratchet vel., Condition and Cycle passes rows (14 rows)", inspector->getHeight() >= 34 + 15 * 26);
            }
            pr.setSelectionForSnapshot ({});
            // the hint bar text while a lane is set
            pr.setLaneForSnapshot (PianoRollComponent::laneCondition);
            check ("the Condition lane names its presets", PianoRollComponent::conditionText (notes[(std::size_t) cycNote]) == "2+4:4");
            pr.setLaneForSnapshot (PianoRollComponent::laneVelocity);
        }
        // .lynx and the copies for the older releases
        {
            const auto song = offline.getSong();
            juce::String why;
            const auto back = ProjectFile::fromJson (ProjectFile::toJson (song), why);
            check (".lynx keeps the ratchets, their bend, the conditions and the cycles", back && back->patterns[0].notes == song.patterns[0].notes);
            auto plainSong = song;
            for (auto& n : plainSong.patterns[0].notes)
                n.ratchets = 0, n.ratchetVelocity = 0.0f, n.condition = 0, n.cycle = 0, n.cycleMask = 0;
            {
                const auto parsed = juce::JSON::parse (ProjectFile::toJson (plainSong));
                int longest = 0;
                if (auto* patterns = parsed["patterns"].getArray())
                    for (const auto& p : *patterns)
                        if (auto* ns = p["notes"].getArray())
                            for (const auto& n : *ns)
                                longest = juce::jmax (longest, n.size());
                check ("a project without them writes no new note fields (" + juce::String (longest) + " at most, 21 is FL's repeat)", longest <= 21);
            }
            for (const auto* release : { "0.1.30", "0.1.28" })
            {
                const auto copy = olderlynx::forRelease (song, release);
                const auto old = copy.ok ? ProjectFile::fromJson (copy.json, why) : std::optional<Song> {};
                juce::StringArray lines;
                for (const auto& l : copy.lost)
                    if (juce::String (l.text).contains ("ratchets") || juce::String (l.text).contains ("Condition"))
                        lines.add (tr (l.text, { l.detail }) + (l.sound ? " (sound)" : ""));
                note (juce::String ("copy for ") + release + ": " + lines.joinIntoString (" / "));
                // 7 notes: the 4, 3 and 8 strikes become notes of their own: 4 + 3 + 8 + 4 others = 19
                int strikes62 = 0;
                bool bent = true;
                if (old)
                    for (const auto& n : old->patterns[0].notes)
                        if (n.key == 62)
                        {
                            const auto expected = 0.9f * (1.0f - 0.6f * (float) strikes62 / 2.0f);
                            bent = bent && std::abs (n.velocity - expected) < 0.002f && n.length == ppq / 3 + (strikes62 == 2 ? 0 : 0);
                            ++strikes62;
                        }
                check (juce::String ("Save a copy for ") + release + ": ratchets as their strikes (19 notes, the bend kept), one view line and one sound line",
                       old && old->patterns[0].notes.size() == 19 && strikes62 == 3 && bent && lines.size() == 2
                           && std::none_of (old->patterns[0].notes.begin(), old->patterns[0].notes.end(), [] (const Note& n) { return n.hasRatchets() || n.hasCondition(); }));
                const auto none = olderlynx::forRelease (plainSong, release);
                check (juce::String ("no ratchet / condition line for a project without them (") + release + ")",
                       std::none_of (none.lost.begin(), none.lost.end(), [] (const olderlynx::Loss& l) {
                           return juce::String (l.text).contains ("ratchets") || juce::String (l.text).contains ("Condition");
                       }));
            }
        }

        // ================= P23 =================
        const auto mouseOn = [] (juce::Component& target, juce::Point<int> p, juce::ModifierKeys mods, int clicks = 1) {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, p.toFloat(), now, clicks, false);
        };
        const auto dragOn = [] (juce::Component& target, juce::Point<int> from, juce::Point<int> p) {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p.toFloat(), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     &target, &target, now, from.toFloat(), now, 1, true);
        };
        const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
        tm::forceTouchForTests() = true;
        tm::testClockMs() = 1000.0;
        const auto finger = [] (int id) { tm::Deferral::fingerForTests() = id; };

        ui.selectTabForSnapshot (2);
        ui.setLayoutStyle (layoutstyle::Style::simple);
        offline.dispatchPendingMessages();
        auto* simple = ui.getSimpleWorkflowForSnapshot();
        check ("the Simple layout", simple != nullptr);
        if (simple != nullptr)
        {
            simple->hideChooser();
            offline.editSong ([bar] (Song& s) {
                st::Request q;
                q.kind = st::Kind::instrument;
                q.name = "Keys";
                ChannelDef synth;
                synth.instrument = InstrumentKind::synth;
                q.channel = synth;
                Pattern chords;
                for (int i = 0; i < 4; ++i)
                    for (int k : { 60, 64, 67 })
                        chords.notes.push_back (Note { (std::int64_t) i * bar, bar - ppq / 4, 0, k });
                q.pattern = chords;
                q.fillBars = 4;
                st::addTrack (s, q);
            });
            offline.dispatchPendingMessages();
            simple->hideChooser();
            check ("Touch mode is off by default, and so are Large controls", ! tm::enabled() && ! ui::simplelook::largeControls());
            save (ui, ui.getLocalBounds(), "p23-simple-off");
            ui.setTouchMode (true);
            offline.dispatchPendingMessages();
            check ("Touch mode on: Large controls on with it", tm::enabled() && ui::simplelook::largeControls() && tm::simpleLayout());
            ui.getPlaylistForSnapshot().selectTrack ((int) offline.getSong().playlistTracks.size() - 1);
            offline.dispatchPendingMessages();
            save (ui, ui.getLocalBounds(), "p23-simple-touch");

            // the suggestion and the value pop-up
            {
                bool answered = false, turnedOn = false;
                auto card = tm::makeSuggestion ([&] (bool on) { answered = true; turnedOn = on; });
                save (*card, card->getLocalBounds(), "p23-suggestion");
                check ("the suggestion card is 380 x 150 with its two buttons", card->getWidth() == 380 && card->getHeight() == 150 && card->getNumChildComponents() == 4);
                auto popup = tm::makeValuePopupForSnapshot (0.62f, [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
                save (*popup, popup->getLocalBounds(), "p23-valuepopup");
                check ("the value pop-up's buttons are 48 px (touch size)", popup->getNumChildComponents() == 2 && popup->getChildComponent (0)->getWidth() == 48
                                                                                && popup->getChildComponent (0)->getHeight() == 48);
                (void) answered;
                (void) turnedOn;
            }

            // --- synthetic fingers on the Playlist (Simple): a long press on a clip opens its menu, deletes nothing
            auto& pl = ui.getPlaylistForSnapshot();
            pl.setSize (1400, 600);
            pl.resized();
            offline.dispatchPendingMessages();
            const auto clipsBefore = offline.getSong().playlist.size();
            {
                // the first clip's middle
                const auto bounds = pl.clipBoundsForSnapshot (0);
                const juce::Point<int> onClip = bounds.isEmpty() ? juce::Point<int> { -1, -1 } : bounds.getCentre();
                pl.lastTouchActionForSnapshot.clear();
                finger (0);
                pl.mouseDown (mouseOn (pl, onClip, left));
                tm::testClockMs() += 300.0;
                pl.touchInputForTests().tickForTests();
                const auto heldAt300 = pl.lastTouchActionForSnapshot.isEmpty();
                tm::testClockMs() += 300.0;
                pl.touchInputForTests().tickForTests();
                pl.mouseUp (mouseOn (pl, onClip, {}));
                tm::testClockMs() += 100.0;
                check ("no long press before 500 ms", heldAt300);
                check ("a long press on a clip opens its menu (the right click's FL delete stays on the mouse): " + pl.lastTouchActionForSnapshot,
                       ! bounds.isEmpty() && pl.lastTouchActionForSnapshot == "clip menu 0");
                check ("... and deletes no clip", offline.getSong().playlist.size() == clipsBefore);
                note ("long press found a clip at " + onClip.toString());
            }
            {
                const auto view = pl.viewForSnapshot();
                finger (0);
                pl.mouseDown (mouseOn (pl, { 500, 200 }, left));
                finger (1);
                pl.mouseDown (mouseOn (pl, { 700, 200 }, left));
                pl.mouseDrag (dragOn (pl, { 700, 200 }, { 900, 200 })); // apart from 200 to 400 px: zoom x2, centre +100 px
                pl.mouseUp (mouseOn (pl, { 900, 200 }, {}));
                finger (0);
                pl.mouseUp (mouseOn (pl, { 500, 200 }, {}));
                const auto after = pl.viewForSnapshot();
                note ("Playlist two fingers: " + juce::String (view.pixelsPerTick * ppq, 3) + " -> " + juce::String (after.pixelsPerTick * ppq, 3) + " px a beat");
                check ("two fingers on the Playlist: a pinch zooms (x2) and edits nothing", std::abs (after.pixelsPerTick / view.pixelsPerTick - 2.0) < 0.05
                                                                                               && offline.getSong().playlist.size() == clipsBefore);
                finger (0);
                pl.mouseDown (mouseOn (pl, { 600, 200 }, left));
                finger (1);
                pl.mouseDown (mouseOn (pl, { 800, 200 }, left));
                const auto pan0 = pl.viewForSnapshot();
                pl.mouseDrag (dragOn (pl, { 800, 200 }, { 760, 200 }));
                finger (0);
                pl.mouseDrag (dragOn (pl, { 600, 200 }, { 560, 200 })); // both 40 px left: the view goes right by 40 px
                pl.mouseUp (mouseOn (pl, { 560, 200 }, {}));
                finger (1);
                pl.mouseUp (mouseOn (pl, { 760, 200 }, {}));
                const auto pan1 = pl.viewForSnapshot();
                check ("two fingers moved together pan the Playlist (40 px)", std::abs ((pan1.scrollTicks - pan0.scrollTicks) * pan1.pixelsPerTick - 40.0) < 2.0);
                pl.restoreViewForSnapshot (view);
            }

            // --- the Piano roll: a tap draws a note on the grid's snap, a long press on it opens its inspector
            ui.showPanelsForSnapshot ({ Panel::pianoRoll });
            offline.dispatchPendingMessages();
            auto& roll = ui.getPianoRollForSnapshot();
            roll.setSize (1200, 640);
            roll.resized();
            roll.scrollToNotes();
            offline.dispatchPendingMessages();
            const auto pattern = offline.getSong().currentPattern;
            const auto notesBefore = offline.getSong().patterns[(std::size_t) pattern].notes;
            juce::Point<int> tapAt { -1, -1 };
            for (int y = 120; y < 500 && tapAt.x < 0; y += 9)
                for (int x = 300; x < 1100 && tapAt.x < 0; x += 23)
                {
                    finger (0);
                    roll.mouseDown (mouseOn (roll, { x, y }, left));
                    const auto held = offline.getSong().patterns[(std::size_t) pattern].notes == notesBefore;
                    roll.mouseUp (mouseOn (roll, { x, y }, {}));
                    tm::testClockMs() += 400.0;
                    if (offline.getSong().patterns[(std::size_t) pattern].notes.size() == notesBefore.size() + 1)
                    {
                        tapAt = { x, y };
                        check ("a finger's press is held back (nothing drawn before the finger lifts or moves)", held);
                    }
                    else if (offline.getSong().patterns[(std::size_t) pattern].notes != notesBefore)
                        offline.editSong ([pattern, notesBefore] (Song& s) { s.patterns[(std::size_t) pattern].notes = notesBefore; });
                }
            const auto now = offline.getSong().patterns[(std::size_t) pattern].notes;
            std::optional<Note> added;
            for (const auto& n : now)
                if (std::find (notesBefore.begin(), notesBefore.end(), n) == notesBefore.end())
                    added = n;
            if (added)
                note ("the tap at " + tapAt.toString() + " drew key " + juce::String (added->key) + " at tick " + juce::String ((juce::int64) added->start)
                      + ", length " + juce::String ((juce::int64) added->length));
            // the same press by mouse (no Touch mode involved) draws on the editor's snap: the tap must draw the same note
            std::optional<Note> byMouse;
            if (added)
            {
                const auto withTap = offline.getSong().patterns[(std::size_t) pattern].notes;
                offline.editSong ([pattern, notesBefore] (Song& s) { s.patterns[(std::size_t) pattern].notes = notesBefore; });
                offline.dispatchPendingMessages();
                tm::forceTouchForTests() = false;
                roll.mouseDown (mouseOn (roll, tapAt, left));
                roll.mouseUp (mouseOn (roll, tapAt, {}));
                tm::forceTouchForTests() = true;
                for (const auto& n : offline.getSong().patterns[(std::size_t) pattern].notes)
                    if (std::find (notesBefore.begin(), notesBefore.end(), n) == notesBefore.end())
                        byMouse = n;
                if (byMouse)
                    note ("the mouse at the same place drew tick " + juce::String ((juce::int64) byMouse->start));
                offline.editSong ([pattern, withTap] (Song& s) { s.patterns[(std::size_t) pattern].notes = withTap; });
                offline.dispatchPendingMessages();
            }
            check ("a tap draws one note, on the editor's snap (where the mouse draws it)",
                   tapAt.x >= 0 && added && byMouse && byMouse->start == added->start && byMouse->key == added->key);
            if (tapAt.x >= 0)
            {
                const auto count = now.size();
                roll.lastTouchActionForSnapshot.clear();
                finger (0);
                roll.mouseDown (mouseOn (roll, tapAt + juce::Point<int> (2, 0), left));
                tm::testClockMs() += 600.0;
                roll.touchInputForTests().tickForTests();
                roll.mouseUp (mouseOn (roll, tapAt + juce::Point<int> (2, 0), {}));
                tm::testClockMs() += 400.0;
                check ("a long press on the note opens its inspector and deletes nothing (" + roll.lastTouchActionForSnapshot + ")",
                       roll.lastTouchActionForSnapshot.startsWith ("note inspector") && offline.getSong().patterns[(std::size_t) pattern].notes.size() == count);
                // two fingers: zoom, no edit
                const auto ppt = roll.pixelsPerTickForTests();
                const auto notesNow = offline.getSong().patterns[(std::size_t) pattern].notes;
                finger (0);
                roll.mouseDown (mouseOn (roll, { 400, 300 }, left));
                finger (1);
                roll.mouseDown (mouseOn (roll, { 600, 300 }, left));
                roll.mouseDrag (dragOn (roll, { 600, 300 }, { 500, 300 })); // 200 -> 100 px apart: zoom out x0.5
                roll.mouseUp (mouseOn (roll, { 500, 300 }, {}));
                finger (0);
                roll.mouseUp (mouseOn (roll, { 400, 300 }, {}));
                check ("two fingers on the Piano roll: a pinch zooms (x0.5) and edits nothing",
                       std::abs (roll.pixelsPerTickForTests() / ppt - 0.5) < 0.05 && offline.getSong().patterns[(std::size_t) pattern].notes == notesNow);
                // a mouse is never held back: a real click draws at once
                tm::forceTouchForTests() = false;
                tm::Deferral::fingerForTests() = -1;
                const auto before = offline.getSong().patterns[(std::size_t) pattern].notes.size();
                roll.mouseDown (mouseOn (roll, { tapAt.x + 300, tapAt.y }, left));
                const auto drawnAtOnce = offline.getSong().patterns[(std::size_t) pattern].notes.size() == before + 1;
                roll.mouseUp (mouseOn (roll, { tapAt.x + 300, tapAt.y }, {}));
                check ("the mouse is not held back (a click draws on the press)", drawnAtOnce);
                tm::forceTouchForTests() = true;
            }
            // outside the Simple layout: a second finger is ignored (no pan / pinch there)
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.dispatchPendingMessages();
            {
                const auto ppt = roll.pixelsPerTickForTests();
                finger (0);
                roll.mouseDown (mouseOn (roll, { 400, 300 }, left));
                finger (1);
                roll.mouseDown (mouseOn (roll, { 600, 300 }, left));
                roll.mouseDrag (dragOn (roll, { 600, 300 }, { 500, 300 }));
                roll.mouseUp (mouseOn (roll, { 500, 300 }, {}));
                finger (0);
                roll.mouseUp (mouseOn (roll, { 400, 300 }, {}));
                check ("Pattern layout: no pinch (Touch mode's two-finger gestures are the Simple layout's)", roll.pixelsPerTickForTests() == ppt && ! tm::simpleLayout());
                offline.undo(); // (the first finger was a press: its note goes)
                offline.dispatchPendingMessages();
            }
            ui.setLayoutStyle (layoutstyle::Style::simple);
            offline.dispatchPendingMessages();
            ui.setTouchMode (false);
            check ("Touch mode off: Large controls back off (Touch mode had switched them on)", ! tm::enabled() && ! ui::simplelook::largeControls());
        }
        tm::forceTouchForTests() = false;
        tm::testClockMs() = -1.0;
        tm::Deferral::fingerForTests() = -1;
        tm::enabled() = false;
        tm::asked() = false;

        checks << (allOk ? "ALL OK\n" : "SOME FAILED\n");
        folder.getChildFile ("checks.txt").replaceWithText (checks);
        return allOk;
    }

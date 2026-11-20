    // --details190mvshot <prefix> <project.flp> (round 190mv, details pass 9): FL's "Undo knob tweaks" option and the
    // rest of the round's rows (see the sections below). <prefix>-checks.txt lists the checks.
    // Included in the app class of Main.cpp.
    bool runDetailsShot190mv (const juce::String& prefix, const juce::String& project)
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
        const auto zoomShot = [&prefix, &checks] (juce::Component& c, juce::Rectangle<int> r, float k, const juce::String& name) {
            const auto area = r.getIntersection (c.getLocalBounds());
            if (area.isEmpty())
            {
                checks << "     (no picture: " << name << " is off the view)\n";
                return;
            }
            savePng (c.createComponentSnapshot (area, true, k), prefix + "-" + name + ".png");
        };
        using namespace lynx;
        if (project.isEmpty())
            return false;
        flsettings::reset();
        AudioEngine offline (false);

        // ---- Settings › General › Undo history: "Undo knob tweaks" (DF-134), on as in FL, after Maximum undo levels
        {
            auto page = makeGeneralPage (offline, {}, false);
            page->setSize (680, 3600);
            std::map<juce::String, juce::Component*> found;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
                if (auto* t = dynamic_cast<juce::Button*> (&c))
                    found[t->getButtonText()] = t;
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (*page);
            auto* knobBox = dynamic_cast<juce::Button*> (found.count (tr ("Undo knob tweaks")) ? found[tr ("Undo knob tweaks")] : nullptr);
            auto* alternate = found.count (tr ("Alternate undo mode (classic undo, instead of undo/redo on the same shortcut)"))
                                  ? found[tr ("Alternate undo mode (classic undo, instead of undo/redo on the same shortcut)")] : nullptr;
            check ("General: \"Undo knob tweaks\", on as in FL", knobBox != nullptr && knobBox->getToggleState());
            check ("General: \"Undo knob tweaks\" sits above Alternate undo mode (FL's order)",
                   knobBox != nullptr && alternate != nullptr && knobBox->getY() < alternate->getY());
            if (knobBox != nullptr)
                zoomShot (*page, juce::Rectangle<int> (0, knobBox->getY() - 40, page->getWidth(), 150), 1.0f, "settings-undo-knob");
        }

        // ---- the engine: with the option off a tweak's step is left out of the history (both orders), on keeps it
        {
            AudioEngine e (false);
            e.setSong (offline.getSong());
            const auto steps = [&e] { return e.getHistoryPosition(); };
            const auto tweakVolume = [&e] (float v) { e.editLevels ([v] (Song& s) { if (! s.mixer.empty()) s.mixer.front().volume = v; }); };
            const auto start = steps();
            tweakVolume (0.5f);
            e.controlTweakedForUndo();
            check ("\"Undo knob tweaks\" on (FL's default): a knob tweak is an undo step", steps() == start + 1);
            flsettings::options().undoKnobTweaks = false;
            juce::Thread::sleep (80);
            const auto before = steps();
            tweakVolume (0.4f); // the edit first, then the tweak report (a wheel step)
            e.controlTweakedForUndo();
            check ("off: the step the tweak's edit saved is taken back", steps() == before);
            check ("off: the project still counts as changed", e.isModified());
            juce::Thread::sleep (80);
            e.controlTweakedForUndo(); // the report first, then the edit (a drag's first move)
            tweakVolume (0.3f);
            check ("off: an edit right after the tweak report saves no step", steps() == before);
            juce::Thread::sleep (80);
            e.editSong ([] (Song& s) { s.bpm = s.bpm + 1.0; });
            check ("off: other edits are still undo steps", steps() == before + 1);
            e.undo();
            check ("off: undo takes back the other edit (the tweak's value stays as in that step)", steps() == before);
            flsettings::options().undoKnobTweaks = true;
        }

        const auto write = [&prefix] (juce::Component& c, juce::Rectangle<int> area, const juce::String& name) {
            area = area.getIntersection (c.getLocalBounds());
            if (area.isEmpty())
                return;
            savePng (c.createComponentSnapshot (area, true, 1.0f), prefix + "-" + name + ".png");
            savePng (c.createComponentSnapshot (area, true, 1.5f), prefix + "-" + name + "@150.png");
        };
        MainComponent ui (offline, false);
        ui.setSize (1920, 1080);
        ui.openFile (juce::File (project));
        offline.dispatchPendingMessages();

        // ---- the Piano roll's Draw tool: Shift adds in stretch mode (DE-69, capture 190mv A3)
        {
            auto& pr = ui.getPianoRollForSnapshot();
            ui.showPanelsForSnapshot ({ workspace::Panel::pianoRoll });
            offline.dispatchPendingMessages();
            int pattern = -1;
            for (int p = 0; p < (int) offline.getSong().patterns.size() && pattern < 0; ++p)
                if (! offline.getSong().patterns[(std::size_t) p].notes.empty())
                {
                    pattern = p;
                    const auto ch = offline.getSong().patterns[(std::size_t) p].notes.front().channel;
                    offline.editSong ([p] (Song& s) { s.currentPattern = p; }, false);
                    offline.setSelectedChannel (ch);
                }
            offline.dispatchPendingMessages();
            pr.runAction ("pianoRoll.zoomAll");
            pr.scrollToNotes();
            pr.setToolForSnapshot (EditTool::pencil);
            offline.dispatchPendingMessages();
            const auto notes = [&offline, pattern] { return offline.getSong().patterns[(std::size_t) juce::jmax (0, pattern)].notes; };
            const auto before = notes();
            const auto grid = pr.gridTicksForSnapshot();
            // a key in view with nothing in the first four steps
            int key = -1;
            for (int d = 1; d < 24 && key < 0 && ! before.empty(); ++d)
                for (const auto k : { before.front().key + d, before.front().key - d })
                {
                    const auto clear = std::none_of (before.begin(), before.end(), [k, grid] (const Note& n) { return n.key == k && n.start < 6 * grid; });
                    if (key < 0 && k > 1 && k < 126 && clear && pr.getLocalBounds().reduced (0, 40).contains (pr.pointForTests (grid / 2, k)))
                        key = k;
                }
            check ("Piano roll: a free key in view for the Draw tests", key >= 0);
            if (key >= 0)
            {
                const juce::ModifierKeys shiftLeft (juce::ModifierKeys::shiftModifier | juce::ModifierKeys::leftButtonModifier);
                const auto at = pr.pointForTests (grid / 2, key);
                pr.pressForSnapshot (at, shiftLeft);
                pr.dragForSnapshot (pr.pointForTests (3 * grid + grid / 2, key), shiftLeft);
                zoomShot (pr, juce::Rectangle<int> (at.x - 40, at.y - 40, 360, 80), 2.0f, "pr-draw-shift-stretch");
                pr.releaseForSnapshot (pr.pointForTests (3 * grid + grid / 2, key));
                const auto after = notes();
                const auto added = std::find_if (after.begin(), after.end(), [key, grid] (const Note& n) { return n.key == key && n.start < 6 * grid; });
                check ("Draw + Shift on the empty grid: a note is added (not a box selection)", after.size() == before.size() + 1 && added != after.end());
                if (added != after.end())
                {
                    line ("added note start " + juce::String (added->start) + " length " + juce::String (added->length) + " (grid " + juce::String (grid) + ")");
                    check ("Draw + Shift: the start stays where it was pressed (snapped)", added->start <= grid);
                    check ("Draw + Shift: the drag stretches its end (FL's stretch mode)", added->start + added->length >= 3 * grid && added->length >= 2 * grid);
                }
                offline.editSong ([pattern, before] (Song& s) { s.patterns[(std::size_t) pattern].notes = before; }, false);
                // plain Draw: the drag moves the new note (FL), its length stays
                pr.pressForSnapshot (at);
                pr.dragForSnapshot (pr.pointForTests (3 * grid + grid / 2, key));
                pr.releaseForSnapshot (pr.pointForTests (3 * grid + grid / 2, key));
                const auto moved = notes();
                const auto plain = std::find_if (moved.begin(), moved.end(), [key, grid] (const Note& n) { return n.key == key && n.start < 6 * grid; });
                check ("plain Draw: the drag moves the new note", plain != moved.end() && plain->start >= 2 * grid);
                offline.editSong ([pattern, before] (Song& s) { s.patterns[(std::size_t) pattern].notes = before; }, false);
            }
        }

        // ---- the Browser's Auto-hide: a floating Browser rolls up to its title strip, a docked one never (DF-107)
        {
            auto& ws = ui.workspaceForTest();
            ui.showPanelsForSnapshot ({ workspace::Panel::browser });
            for (const auto other : { workspace::Panel::playlist, workspace::Panel::channelRack, workspace::Panel::pianoRoll, workspace::Panel::mixer })
                ws.hide (other); // (the floating Browser stays under the other panels: alone, its roll shows)
            ws.dockBrowser (workspace::Dock::none);
            ws.show (workspace::Panel::browser);
            offline.dispatchPendingMessages();
            auto& frame = ws.getFrame (workspace::Panel::browser);
            const auto fullHeight = frame.getHeight();
            ws.setBrowserAutoHide (true);
            ws.rollBrowserForSnapshot (true);
            check ("Auto-hide, floating Browser: rolled up to its title strip", ws.isBrowserRolledUp() && frame.getHeight() == Workspace::titleHeight);
            write (ws, frame.getBounds().expanded (40, 60).withHeight (fullHeight + 120), "browser-autohide-rolled");
            ws.rollBrowserForSnapshot (false);
            check ("Auto-hide, the mouse on its title: out again at full height", frame.getHeight() == fullHeight);
            write (ws, frame.getBounds().expanded (40, 60), "browser-autohide-out");
            ws.dockBrowser (workspace::Dock::left);
            ws.rollBrowserForSnapshot (true);
            check ("Auto-hide, docked Browser: never hides (FL 2026)", ! ws.isBrowserRolledUp() && frame.getHeight() > Workspace::titleHeight);
            ws.rollBrowserForSnapshot (false);
            ws.setBrowserAutoHide (false);
            offline.dispatchPendingMessages();
        }

        // ---- the Mixer's Current (C) track (DF-82, captures 190mr A4 / 190mv A1)
        {
            ui.selectTabForSnapshot (3);
            offline.dispatchPendingMessages();
            auto& strips = ui.getMixerForSnapshot().getStripsForSnapshot();
            const auto cx = strips.currentStripXForSnapshot();
            check ("Mixer: the Current strip is the leftmost, left of the Master", cx < strips.stripLeft (0));
            check ("Mixer: the song has no Current track before its first click", mixerops::currentTrack (offline.getSong()) < 0);
            strips.selectTrack (1);
            check ("selecting insert 1 makes it the Current track's source", offline.getCurrentTrackSource() == 1);
            offline.getTransport().playing.store (true);
            double ms = 1000.0;
            for (int f = 0; f < 40; ++f)
            {
                for (int k = 0; k < 4; ++k)
                    offline.getMeters().accumulate (k, 0.55f + 0.05f * (float) k, 0.5f + 0.05f * (float) k);
                strips.frameForSnapshot (ms += 1000.0 / 60.0);
            }
            const auto area = juce::Rectangle<int> (0, 0, strips.stripLeft (0) + strips.widthOf (0) + 2 * strips.widthOf (1), strips.getHeight());
            write (strips, area, "mixer-current");
            const auto c = strips.selectCurrentForSnapshot();
            check ("a click on the Current strip makes the Current track (the last mixer track) and selects it",
                   c > 0 && c == (int) offline.getSong().mixer.size() - 1 && offline.getSong().mixer[(std::size_t) c].current);
            check ("the Current track routes nowhere (FL: CanRoute is false for it)", c > 0 && ! offline.getSong().mixer[(std::size_t) c].toMaster
                                                                                         && offline.getSong().mixer[(std::size_t) c].sends.empty());
            check ("the Current track itself selected: it takes the Master's output (FL, REA 190mv)", offline.getCurrentTrackSource() == 0);
            for (int f = 0; f < 40; ++f)
            {
                for (int k = 0; k < (int) offline.getSong().mixer.size(); ++k)
                    offline.getMeters().accumulate (k, 0.55f, 0.5f);
                strips.frameForSnapshot (ms += 1000.0 / 60.0);
            }
            offline.getTransport().playing.store (false);
            write (strips, area, "mixer-current-selected");
            write (ui.getMixerForSnapshot(), ui.getMixerForSnapshot().getLocalBounds(), "mixer-current-panel");
            // inserts added after it: it moves back to the end
            const auto inserts = (int) offline.getSong().mixer.size();
            offline.editSong ([] (Song& s) { mixerops::increaseInsertsTo (s, mixerops::insertCount (s) + 2); });
            check ("two inserts added: the Current track is still the last one", mixerops::currentTrack (offline.getSong()) == inserts + 1);
            strips.selectTrack (0);
            check ("the Master selected: the Current track takes the Master's output", offline.getCurrentTrackSource() == 0);
        }

        // ---- the toolbar: FL's audio editor button (DF-35), the output panel's Stereo scope (DF-24), the Master pitch in Hz
        // (DF-22) and the value menu's "Set..." knob (DF-146)
        {
            auto& button = ui.getAudioEditorButtonForSnapshot();
            check ("toolbar: FL's audio editor button is shown with the file buttons", button.isShowing() || button.isVisible());
            if (auto* parent = button.getParentComponent())
                write (*parent, button.getBounds().expanded (4 * button.getWidth(), button.getHeight() + 6), "toolbar-audio-editor");
            const auto before = offline.getSong().mixer.front().effects.size();
            ui.openMasterAudioEditorForSnapshot (false);
            offline.dispatchPendingMessages();
            const auto& master = offline.getSong().mixer.front().effects;
            check ("its click puts an audio editor (Edison) into the Master's first empty slot (FL)",
                   master.size() == before + 1 && std::any_of (master.begin(), master.end(), [] (const EffectSlot& e) { return e.kind == EffectKind::edison; }));
            ui.openMasterAudioEditorForSnapshot (false);
            offline.dispatchPendingMessages();
            check ("a second click opens that one (no second editor)", offline.getSong().mixer.front().effects.size() == before + 1);
            ui.openMasterAudioEditorForSnapshot (true);
            offline.dispatchPendingMessages();
            check ("the right button: a new audio editor", offline.getSong().mixer.front().effects.size() == before + 2);
            closeAllToolWindows();
            offline.editSong ([before] (Song& s) { s.mixer.front().effects.resize (before); }, false);

            toolbar::OutputMonitor out (offline); // (FL's output panel's size: the toolbar may not show its own here)
            out.setSize (116, 38);
            toolbar::OutputMonitor::mode = toolbar::OutputMonitor::oscilloscope;
            toolbar::OutputMonitor::stereoScope = false;
            out.setScopeSidesForShot (0.0f, 1.0f);
            write (out, out.getLocalBounds(), "scope-single");
            toolbar::OutputMonitor::stereoScope = true;
            out.setScopeSidesForShot (0.0f, 1.0f);
            write (out, out.getLocalBounds(), "scope-stereo");
            check ("output panel: Stereo draws the left and right sides as two lines", toolbar::OutputMonitor::stereoScope);
            toolbar::OutputMonitor::stereoScope = false;

            check ("Master pitch typed in Hz: A = 440 is 0 cents, 432 Hz = -31.8 cents",
                   std::abs (masterpitch::centsOfHz (432.0) + 31.77) < 0.01 && std::abs (masterpitch::hzOfCents (0.0) - 440.0) < 1.0e-9);

            auto spec = ui::panSpec ("HI TOM - Panning");
            double value = 0.0;
            auto knob = ui::makeSetKnob (spec, value, [&value] (double v) { value = v; });
            check ("Set...: FL's 162 px wide Knob window content", knob != nullptr && knob->getWidth() == 162);
            if (knob != nullptr)
                write (*knob, knob->getLocalBounds(), "set-knob");
        }

        // ---- Live's phase nudge in the Clip-launch control bar (DL-51, capture 190mv B2)
        {
            ui.setLayoutStyle (layoutstyle::Style::clipLaunch);
            offline.dispatchPendingMessages();
            auto* down = dynamic_cast<toolbar::PhaseNudge*> (ui.toolbarPartForSnapshot ("nudgeDown"));
            auto* up = dynamic_cast<toolbar::PhaseNudge*> (ui.toolbarPartForSnapshot ("nudgeUp"));
            auto* tempo = ui.toolbarPartForSnapshot ("tempo");
            check ("Clip-launch: the two phase-nudge buttons right of the tempo box (Live)",
                   down != nullptr && up != nullptr && tempo != nullptr && down->isVisible() && up->isVisible()
                       && down->getX() > tempo->getRight() && up->getX() > down->getRight());
            if (down != nullptr && tempo != nullptr)
            {
                const auto area = tempo->getBounds().getUnion (up->getBounds()).expanded (60, 8);
                write (ui, area, "live-phase-nudge");
                const auto bpm = offline.getSong().bpm;
                auto source = juce::Desktop::getInstance().getMainMouseSource();
                const auto now = juce::Time::getCurrentTime();
                const juce::MouseEvent press (source, {}, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                              down, down, now, {}, now, 1, false);
                down->mouseDown (press);
                check ("held: the song runs slower (Phase Nudge Down)", offline.getSong().bpm < bpm);
                write (ui, area, "live-phase-nudge-held");
                down->mouseUp (press);
                check ("let go: the tempo is back", std::abs (offline.getSong().bpm - bpm) < 1.0e-9);
            }
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.dispatchPendingMessages();
        }

        juce::File (prefix + "-checks.txt").replaceWithText (checks + "result=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

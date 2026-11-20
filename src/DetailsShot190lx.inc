    // --details190lxshot <prefix> <project.flp> [set.als] (round 190lx, details pass 4): Live's slot-block clipboard
    // (DL-29), Live's Punch-In / Loop / Punch-Out switches (DL-61), the Simple Loops dock (DS-42), FL's graph editor Ctrl
    // scaling (DE-31) and the round's other rows. <prefix>-checks.txt lists the checks. Included in the app class of Main.cpp.
    bool runDetailsShot190lx (const juce::String& prefix, const juce::String& project, const juce::String& set)
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
        const auto write = [&prefix] (juce::Component& c, const juce::String& name) { savePng (lynx::shot::snapshot (c), prefix + "-" + name + ".png"); };
        const auto zoomShot = [&prefix] (juce::Component& c, juce::Rectangle<int> r, float k, const juce::String& name) {
            savePng (c.createComponentSnapshot (r.getIntersection (c.getLocalBounds()), true, k), prefix + "-" + name + ".png");
        };
        using namespace lynx;
        const auto mouseOn = [] (juce::Component& target, juce::Point<int> p, juce::Point<int> down, juce::ModifierKeys mods, bool dragged) {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, down.toFloat(), now, 1, dragged);
        };

        if (project.isNotEmpty())
        {
            // ---- Pattern layout, the FL demo: the Channel rack's graph editor, Ctrl scales every column (DE-31)
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (1920, 1080);
            ui.openFile (juce::File (project));
            offline.dispatchPendingMessages();
            auto& rack = ui.getRackForSnapshot();
            const auto& song = offline.getSong();
            // the first channel with steps on 1, 5, 9 and 13 in the current pattern (put there if the demo has none)
            const int channel = song.channels.empty() ? -1 : 0;
            check ("a channel", channel >= 0);
            if (channel >= 0)
                offline.editSong ([channel] (Song& s) {
                    for (int st : { 0, 4, 8, 12 })
                        if (notesInStep (s.activePattern(), channel, st).empty())
                        {
                            Note n;
                            n.start = (std::int64_t) st * ticksPerStep;
                            n.channel = channel;
                            s.activePattern().notes.push_back (n);
                        }
                });
            if (channel >= 0)
            {
                offline.setSelectedChannel (channel);
                rack.setSize (900, 600);
                rack.setGraphLevelForSnapshot ((int) notetools::Level::velocity);
                offline.dispatchPendingMessages();
                std::vector<int> stepsOn;
                for (int st = 0; st < 16; ++st)
                    if (! notesInStep (song.activePattern(), channel, st).empty())
                        stepsOn.push_back (st);
                // give the steps different velocities first (a plain drag sets one column)
                for (std::size_t k = 0; k < stepsOn.size(); ++k)
                    offline.editSong ([channel, st = stepsOn[k], v = 0.3f + 0.2f * (float) (k % 3)] (Song& s) {
                        for (auto i : notesInStep (s.activePattern(), channel, st))
                            s.activePattern().notes[(std::size_t) i].velocity = v;
                    });
                auto velocities = [&] {
                    std::vector<float> v;
                    for (auto st : stepsOn)
                        v.push_back (offline.getSong().activePattern().notes[(std::size_t) notesInStep (offline.getSong().activePattern(), channel, st).front()].velocity);
                    return v;
                };
                const auto before = velocities();
                const auto pressed = stepsOn.front();
                const auto from = rack.laneSpotForSnapshot (pressed, before.front());
                const auto to = rack.laneSpotForSnapshot (pressed, before.front() * 0.5f);
                const auto ctrl = juce::ModifierKeys (juce::ModifierKeys::commandModifier | juce::ModifierKeys::leftButtonModifier);
                rack.mouseDown (mouseOn (rack, from, from, ctrl, false));
                rack.mouseDrag (mouseOn (rack, to, from, ctrl, true));
                rack.mouseUp (mouseOn (rack, to, from, juce::ModifierKeys (juce::ModifierKeys::commandModifier), true));
                offline.dispatchPendingMessages();
                const auto after = velocities();
                bool halved = after.size() == before.size();
                for (std::size_t k = 0; k < after.size() && halved; ++k)
                    halved = std::abs (after[k] - before[k] * 0.5f) < 0.03f;
                juce::String vs;
                for (std::size_t k = 0; k < after.size(); ++k)
                    vs << juce::String (before[k], 2) << "->" << juce::String (after[k], 2) << " ";
                line ("graph Ctrl+drag velocities: " + vs);
                check ("graph editor: Ctrl+drag on one column scales every column (FL: the others follow)", halved);
                write (rack, "rack-graph-ctrl-scaled");
                // FL's Shift tab (DE-30): a drag to the middle starts the step's note half a step later
                rack.setGraphLevelForSnapshot (notetools::graphShift);
                offline.dispatchPendingMessages();
                const auto at = rack.laneSpotForSnapshot (pressed, 0.5f);
                const auto left = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
                rack.mouseDown (mouseOn (rack, at, at, left, false));
                rack.mouseUp (mouseOn (rack, at, at, {}, false));
                offline.dispatchPendingMessages();
                const auto notes = notesInStep (offline.getSong().activePattern(), channel, pressed);
                const auto offset = notes.empty() ? -1 : (int) (offline.getSong().activePattern().notes[(std::size_t) notes.front()].start - (std::int64_t) pressed * ticksPerStep);
                line ("Shift lane at 0.5: offset " + juce::String (offset) + " of " + juce::String ((int) ticksPerStep) + " ticks");
                check ("graph editor Shift tab: the note starts later in its step", offset > ticksPerStep / 3 && offset < ticksPerStep * 2 / 3);
                write (rack, "rack-graph-shift");
                rack.setGraphLevelForSnapshot (notetools::graphNote);
                offline.dispatchPendingMessages();
                write (rack, "rack-graph-note");
            }
            // FL's Alternate meter scale (DF-61): the same levels on the mixer meters, off and on
            ui.selectTabForSnapshot (3);
            offline.dispatchPendingMessages();
            auto& strips = ui.getMixerForSnapshot().getStripsForSnapshot();
            const auto feed = [&] {
                offline.getTransport().playing.store (true);
                double ms = 1000.0;
                for (int f = 0; f < 30; ++f)
                {
                    for (int k = 0; k < 8; ++k)
                        offline.getMeters().accumulate (k, 0.5f + 0.06f * (float) k, 0.45f + 0.06f * (float) k);
                    strips.frameForSnapshot (ms += 1000.0 / 60.0);
                }
                offline.getTransport().playing.store (false);
            };
            const auto wells = strips.meterAreaForSnapshot (0).getUnion (strips.meterAreaForSnapshot (5)).expanded (6);
            feed();
            zoomShot (strips, wells, 3.0f, "mixer-meters-normal@3x");
            meterscale::alternate() = true;
            strips.repaint();
            feed();
            check ("Alternate meter scale: 0 dB at 85 % of the meter (FL's law)", std::abs (meterscale::positionOfDb (0.0f, meterscale::alternate()) - 0.847f) < 0.01f);
            zoomShot (strips, wells, 3.0f, "mixer-meters-alternate@3x");
            meterscale::alternate() = false;
        }
        {
            // ---- Simple layout: the Loops dock on the right narrows the timeline and the track dock (DS-42)
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (1920, 1000);
            ui.selectTabForSnapshot (2);
            ui.setLayoutStyle (layoutstyle::Style::simple);
            offline.dispatchPendingMessages();
            auto* simple = ui.getSimpleWorkflowForSnapshot();
            check ("Simple workflow", simple != nullptr);
            if (simple != nullptr)
            {
                simple->addDrumsForSnapshot ({}, true);
                offline.dispatchPendingMessages();
                simple->hideChooser();
                offline.dispatchPendingMessages();
                auto* dock = simple->dockForSnapshot();
                auto* loops = ui.showLoopsDockForSnapshot();
                offline.dispatchPendingMessages();
                ui.resized();
                check ("the Loops dock shows", loops != nullptr && ui.isLoopsDockShowing());
                if (loops != nullptr && dock != nullptr)
                {
                    const auto lb = ui.getLocalArea (loops, loops->getLocalBounds());
                    const auto db = ui.getLocalArea (dock, dock->getLocalBounds());
                    const auto pb = ui.getLocalArea (&ui.getPlaylistForSnapshot(), ui.getPlaylistForSnapshot().getLocalBounds());
                    line ("Loops dock " + lb.toString() + ", track dock " + db.toString() + ", timeline " + pb.toString());
                    check ("the Loops dock is BandLab's 401 px wide", lb.getWidth() == BrowserComponent::loopsDockWidth);
                    check ("the track dock ends where the Loops dock starts (narrowed too)", db.getRight() <= lb.getX() && db.getWidth() > 0);
                    check ("the timeline ends where the Loops dock starts", pb.getRight() <= lb.getX());
                    check ("the dock reaches down past the track dock's top", lb.getBottom() >= db.getBottom() && lb.getY() < db.getY());
                    auto* b = BrowserComponent::browserOfLoopsDock (loops);
                    check ("its Browser shows the Loop Browser", b != nullptr && b->isLoopsShown());
                    write (ui, "simple-loops-dock");
                }
                // leaving the Simple layout takes it away
                ui.setLayoutStyle (layoutstyle::Style::pattern);
                offline.dispatchPendingMessages();
                check ("the Loops dock is the Simple layout's own", ! ui.isLoopsDockShowing());
            }
        }
        {
            // ---- Clip-launch layout: Live's Punch-In / Loop / Punch-Out switches (DL-61), the slot-block clipboard (DL-29)
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (1920, 1000);
            ui.setVisible (true);
            ui.selectTabForSnapshot (2);
            ui.setClipLaunchWorkflow (true);
            offline.dispatchPendingMessages();
            if (set.isNotEmpty())
                ui.openFile (juce::File (set));
            offline.dispatchPendingMessages();
            ui.resized();
            auto* punchIn = dynamic_cast<juce::Button*> (ui.toolbarPartForSnapshot ("punch"));
            auto* loopSwitch = dynamic_cast<juce::Button*> (ui.toolbarPartForSnapshot ("liveLoop"));
            auto* punchOut = dynamic_cast<juce::Button*> (ui.toolbarPartForSnapshot ("punchOut"));
            check ("Live's three switches", punchIn != nullptr && loopSwitch != nullptr && punchOut != nullptr);
            if (punchIn != nullptr && loopSwitch != nullptr && punchOut != nullptr)
            {
                check ("in Live's order: Punch-In, Loop, Punch-Out, all shown", punchIn->isVisible() && loopSwitch->isVisible() && punchOut->isVisible()
                                                                                 && punchIn->getRight() < loopSwitch->getX() && loopSwitch->getRight() < punchOut->getX());
                line ("switches " + punchIn->getBounds().toString() + " " + loopSwitch->getBounds().toString() + " " + punchOut->getBounds().toString());
                auto opts = offline.getRecordOptions();
                opts.punchLoopRegion = false;
                offline.setRecordOptions (opts);
                ui.runActionForSnapshot ("record.punchIn");
                auto o = offline.getRecordOptions();
                check ("Punch-In alone: punch on, the in end only", o.punchLoopRegion && o.punchIn && ! o.punchOut && punchIn->getToggleState()
                                                                    && ! punchOut->getToggleState());
                ui.runActionForSnapshot ("record.punchOut");
                o = offline.getRecordOptions();
                check ("then Punch-Out: both ends", o.punchLoopRegion && o.punchIn && o.punchOut && punchOut->getToggleState());
                ui.runActionForSnapshot ("record.punchIn");
                o = offline.getRecordOptions();
                check ("Punch-In off again: the out end alone", o.punchLoopRegion && ! o.punchIn && o.punchOut && ! punchIn->getToggleState());
                ui.runActionForSnapshot ("record.punchOut");
                check ("both off: no punch", ! offline.getRecordOptions().punchLoopRegion);
                // the Loop switch: a loop region (a bar at the position without one), then its looping on / off
                const auto hadLoop = offline.getSong().hasLoop();
                if (offline.getSong().loopPlays()) // (start from a region that does not loop)
                    offline.editSong ([] (Song& s) { s.loopOff = true; }, false);
                loopSwitch->onClick();
                offline.dispatchPendingMessages();
                check ("Loop switch: the region loops", offline.getSong().loopPlays());
                line (juce::String ("loop region before: ") + (hadLoop ? "yes" : "no"));
                ui.updateLookFieldsForSnapshot();
                const auto bar = punchIn->getBounds().getUnion (punchOut->getBounds()).expanded (6, 4);
                ui.runActionForSnapshot ("record.punchIn");
                ui.updateLookFieldsForSnapshot();
                zoomShot (ui, bar, 3.0f, "live-punch-in-loop@3x");
                ui.runActionForSnapshot ("record.punchOut");
                ui.updateLookFieldsForSnapshot();
                zoomShot (ui, bar, 3.0f, "live-punch-in-loop-out@3x");
                write (ui, "live-control-bar");
                ui.runActionForSnapshot ("record.punchIn");
                ui.runActionForSnapshot ("record.punchOut");
                loopSwitch->onClick();
                offline.dispatchPendingMessages();
                ui.updateLookFieldsForSnapshot();
                check ("Loop switch again: the region stays, not looped", ! offline.getSong().loopPlays() && offline.getSong().hasLoop());
                zoomShot (ui, bar, 3.0f, "live-punch-off@3x");
            }
            ui.setClipGridView (PlaylistArea::GridView::grid);
            offline.dispatchPendingMessages();
            ui.resized();
            auto* gridView = ui.getClipGridForSnapshot();
            line ("grid clips: " + juce::String (offline.getSong().clipGrid ? (int) offline.getSong().clipGrid->clips.size() : -1));
            if (gridView != nullptr && offline.getSong().clipGrid.has_value())
            {
                refreshClipGrid (*gridView);
                // Live's automation LEDs on knobs (DL-59): the set's automated sends / pans / device knobs
                {
                    int sends = 0, pans = 0, devices = 0;
                    for (const auto& t : offline.getAutomationDisplay().automatedTargets())
                        sends += t.kind == AutomationTargetKind::mixerSend ? 1 : 0, pans += t.kind == AutomationTargetKind::mixerPan ? 1 : 0,
                            devices += t.kind == AutomationTargetKind::effectParam || t.kind == AutomationTargetKind::channelParam ? 1 : 0;
                    for (const auto& t : offline.getAutomationDisplay().automatedTargets())
                        if (t.kind == AutomationTargetKind::mixerSend)
                            line ("send automated: insert " + juce::String (t.index) + " -> " + juce::String (t.slot));
                    line ("automated in the set: sends " + juce::String (sends) + ", pans " + juce::String (pans) + ", device knobs " + juce::String (devices));
                    if (auto* mixer = clipGridMixer (*gridView))
                        write (*mixer, "session-mixer-knob-leds");
                }
                const auto& grid = *offline.getSong().clipGrid;
                // a 2 x 2 block with at least two clips, and an empty 2 x 2 area under the last scene to paste into
                int t0 = -1, s0 = -1;
                for (int s = 0; s < 24 && t0 < 0; ++s)
                    for (int t = 0; t + 1 < 16 && t0 < 0; ++t)
                    {
                        int n = 0;
                        for (int dt = 0; dt < 2; ++dt)
                            for (int ds = 0; ds < 2; ++ds)
                                n += launcher::clipAt (grid, t + dt, s + ds) != nullptr ? 1 : 0;
                        if (n >= 2)
                            t0 = t, s0 = s;
                    }
                check ("a 2 x 2 block with clips", t0 >= 0);
                if (t0 >= 0)
                {
                    int lastScene = 0;
                    for (const auto& c : grid.clips)
                        if (c.length > 0)
                            lastScene = juce::jmax (lastScene, (int) grid.slots[(std::size_t) c.gridSlot].scene);
                    int count = 0;
                    for (int dt = 0; dt < 2; ++dt)
                        for (int ds = 0; ds < 2; ++ds)
                            count += launcher::clipAt (grid, t0 + dt, s0 + ds) != nullptr ? 1 : 0;
                    clipGridSelectRangeForSnapshot (*gridView, t0, s0, t0 + 1, s0 + 1);
                    clipGridKeyForSnapshot (*gridView, juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0));
                    const auto at = lastScene + 2;
                    clipGridSelectRangeForSnapshot (*gridView, t0, at, t0, at);
                    clipGridKeyForSnapshot (*gridView, juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0));
                    offline.dispatchPendingMessages();
                    const auto& g2 = *offline.getSong().clipGrid;
                    int pasted = 0;
                    bool same = true;
                    for (int dt = 0; dt < 2; ++dt)
                        for (int ds = 0; ds < 2; ++ds)
                        {
                            const auto* a = launcher::clipAt (g2, t0 + dt, s0 + ds);
                            const auto* b = launcher::clipAt (g2, t0 + dt, at + ds);
                            pasted += b != nullptr ? 1 : 0;
                            same = same && (a == nullptr) == (b == nullptr) && (a == nullptr || (a->pattern == b->pattern && a->channel == b->channel));
                        }
                    check ("Ctrl+C on a 2 x 2 selection, Ctrl+V on a slot: the block lands with its top-left there", pasted == count && same);
                    check ("the pasted block is selected", clipGridSelectedCountForSnapshot (*gridView) == 4);
                    write (*gridView, "grid-block-pasted");
                    offline.undo();
                    offline.dispatchPendingMessages();
                    check ("one undo takes the whole paste back", launcher::clipAt (*offline.getSong().clipGrid, t0, at) == nullptr
                                                                      && launcher::clipAt (*offline.getSong().clipGrid, t0 + 1, at + 1) == nullptr);
                    // Ctrl+D on the block: its copy right under it
                    clipGridSelectRangeForSnapshot (*gridView, t0, s0, t0 + 1, s0 + 1);
                    const auto under = launcher::clipAt (*offline.getSong().clipGrid, t0, s0 + 2) != nullptr;
                    clipGridKeyForSnapshot (*gridView, juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0));
                    offline.dispatchPendingMessages();
                    int dup = 0;
                    for (int dt = 0; dt < 2; ++dt)
                        for (int ds = 0; ds < 2; ++ds)
                            dup += launcher::clipAt (*offline.getSong().clipGrid, t0 + dt, s0 + 2 + ds) != nullptr ? 1 : 0;
                    line (juce::String ("slot under the block taken before: ") + (under ? "yes" : "no"));
                    check ("Ctrl+D on the block: the copies under it", dup >= count);
                    offline.undo();
                    // Ctrl+X: the block is cut (one undo step)
                    clipGridSelectRangeForSnapshot (*gridView, t0, s0, t0 + 1, s0 + 1);
                    clipGridKeyForSnapshot (*gridView, juce::KeyPress ('x', juce::ModifierKeys::commandModifier, 0));
                    offline.dispatchPendingMessages();
                    bool cut = true;
                    for (int dt = 0; dt < 2; ++dt)
                        for (int ds = 0; ds < 2; ++ds)
                            cut = cut && launcher::clipAt (*offline.getSong().clipGrid, t0 + dt, s0 + ds) == nullptr;
                    check ("Ctrl+X cuts the block", cut);
                    offline.undo();
                }
            }
        }
        {
            // ---- FL plugins: Transient Processor's lower trace (DP-31)
            AudioEngine offline (false);
            Song song;
            song.mixer.resize (2);
            song.mixer[0].name = "Master";
            song.mixer[1].name = "Insert 1";
            song.mixer[1].effects.push_back (makeEffectSlot (EffectKind::transientProcessor));
            offline.setSong (std::move (song));
            offline.dispatchPendingMessages();
            auto editor = makeNativeEffectEditor (offline, 1, 0, 1.0f, 0);
            check ("Transient Processor: the trace takes a history", transientTraceForSnapshot (*editor, false));
            write (*editor, "plug-transient-trace");
            auto hovered = makeNativeEffectEditor (offline, 1, 0, 1.0f, 0);
            transientTraceForSnapshot (*hovered, true);
            write (*hovered, "plug-transient-trace-hover");
            // Gross Beat's time envelope with FL's Safety Line (DP-5)
            Song gb;
            gb.mixer.resize (2);
            gb.mixer[0].name = "Master";
            gb.mixer[1].name = "Insert 1";
            gb.mixer[1].effects.push_back (makeEffectSlot (EffectKind::grossBeat));
            offline.setSong (std::move (gb));
            offline.dispatchPendingMessages();
            auto gbEditor = makeNativeEffectEditor (offline, 1, 0, 1.0f, 0);
            check ("Gross Beat editor on its time envelope", showGrossBeatEnvelopeForSnapshot (*gbEditor, 0));
            write (*gbEditor, "plug-grossbeat-safety-line");
        }
        juce::File (prefix + "-checks.txt").replaceWithText (checks + (allOk ? "result=OK\n" : "result=FAIL\n"));
        return allOk;
    }

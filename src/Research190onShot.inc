    // --research190onshot <prefix> (round 190on: research190oe P2, P8, P10), offscreen through the real components: the
    // command palette (Ctrl+Shift+Y, the toolbar's magnifier, its matching and running, the open time) in the Pattern,
    // Simple and Clip-launch layouts; Tab to transient on a drum loop in the Playlist and the audio editor; the Piano
    // roll's stacked controller lanes (four lanes, an edit in the third, the paint time with one and with four).
    // <prefix>-checks.txt lists the checks; pictures @100 and @150. Included in the app class of Main.cpp.
    bool runResearchShot190on (const juce::String& prefix)
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

        // a drum loop: 8 sharp hits, 0.25 s apart from 0.05 s (2.1 s, 44.1 kHz mono)
        constexpr double firstHit = 0.05, hitPeriod = 0.25;
        const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx190on-drums.wav");
        {
            wav.deleteFile();
            juce::WavAudioFormat format;
            std::unique_ptr<juce::OutputStream> out (wav.createOutputStream().release());
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (out.get(), 44100.0, 1, 24, {}, 0));
            if (writer != nullptr)
            {
                out.release();
                juce::AudioBuffer<float> b (1, (int) (2.1 * 44100.0));
                b.clear();
                std::uint32_t seed = 12345;
                for (int h = 0; h < 8; ++h)
                {
                    const auto at = (int) std::llround ((firstHit + h * hitPeriod) * 44100.0);
                    for (int i = 0; i < (int) (0.12 * 44100.0) && at + i < b.getNumSamples(); ++i)
                    {
                        seed = seed * 1664525u + 1013904223u;
                        const auto noise = (float) ((double) (seed >> 8) / (double) (1u << 24) * 2.0 - 1.0);
                        b.setSample (0, at + i, 0.8f * noise * (float) std::exp (-(double) i / (0.02 * 44100.0)));
                    }
                }
                writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
        }

        AudioEngine engine (false);
        const auto bar = (std::int64_t) 4 * ppq;
        int drumChannel = -1;
        {
            auto song = Song::makeDefault();
            song.bpm = 120.0;
            song.playlist.clear();
            ChannelDef drums;
            drums.name = "Drum loop";
            drums.instrument = InstrumentKind::audioClip;
            drums.samplePath = wav.getFullPathName().toStdString();
            song.channels.push_back (drums);
            drumChannel = (int) song.channels.size() - 1;
            song.playlistTracks.resize (std::max<std::size_t> (song.playlistTracks.size(), 8));
            Clip take;
            take.kind = ClipKind::audio;
            take.channel = drumChannel;
            take.track = 0;
            take.start = bar;
            take.length = bar; // (2 s at 120 BPM)
            song.playlist.push_back (take);
            // a pattern with a few notes for the Piano roll (channel 0)
            if (! song.patterns.empty())
            {
                auto& p = song.patterns[0];
                p.notes.clear();
                const int keys[] = { 60, 64, 67, 72, 65, 69, 62, 71 };
                for (int i = 0; i < 8; ++i)
                {
                    Note n;
                    n.channel = 0;
                    n.key = keys[i];
                    n.start = i * ppq / 2;
                    n.length = ppq / 2;
                    n.velocity = 0.5f + 0.05f * (float) i;
                    p.notes.push_back (n);
                }
                song.currentPattern = 0;
            }
            engine.setSong (std::move (song));
        }
        engine.getSampleLibrary().load (wav.getFullPathName());
        MainComponent ui (engine, false);
        ui.setSize (1600, 900);
        engine.dispatchPendingMessages();
        auto& ws = ui.workspaceForTest();

        // ================= P2: the command palette =================
        const auto palette = [&] { return ui.actionSearchForSnapshot(); };
        const auto closePalette = [&] {
            ui.closeActionSearchForSnapshot();
            engine.dispatchPendingMessages();
        };
        // what building each main menu costs (the palette walks them all)
        {
            const auto names = ui.getMenuNamesForSnapshot();
            juce::String times;
            for (int i = 0; i < names.size(); ++i)
            {
                if (names[i] == tr ("Add")) // (its generator tree reads every preset folder: the palette leaves it out)
                    continue;
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                const auto menu = ui.getMenuForSnapshot (i);
                times << names[i] << " " << juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 1) << " ms, ";
            }
            line ("menu builds: " + times);
        }
        for (const auto style : { layoutstyle::Style::pattern, layoutstyle::Style::simple, layoutstyle::Style::clipLaunch })
        {
            const auto name = style == layoutstyle::Style::pattern ? juce::String ("pattern") : style == layoutstyle::Style::simple ? juce::String ("simple") : juce::String ("cliplaunch");
            ui.setLayoutStyle (style);
            engine.dispatchPendingMessages();
            ui.resized();
            auto& magnifier = ui.paletteButtonForSnapshot();
            line (name + ": magnifier at " + magnifier.getBounds().toString());
            check (name + ": the magnifier is in the toolbar", magnifier.isVisible() && magnifier.getY() < ui.getToolbarBoundsForSnapshot().getBottom());
            write (ui, ui.getToolbarBoundsForSnapshot().withWidth (juce::jmin (900, ui.getWidth())), "p2-toolbar-" + name);
            // Ctrl+Shift+Y opens it
            ui.keyPressed (juce::KeyPress ('Y', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0));
            engine.dispatchPendingMessages();
            check (name + ": Ctrl+Shift+Y opens the palette", palette() != nullptr && palette()->isVisible());
            if (palette() == nullptr)
                continue;
            const auto all = palette()->matchCount();
            line (name + ": " + juce::String (all) + " items (" + juce::String ((int) keys::actions().size()) + " actions), opened in "
                  + juce::String (ui.paletteOpenMsForSnapshot(), 2) + " ms");
            check (name + ": every action plus the menus, extras, settings", all > (int) keys::actions().size() + 100);
            write (ui, ui.getLocalBounds(), "p2-palette-" + name);
            palette()->setQuery ("metro");
            write (ui, ui.getLocalBounds(), "p2-palette-metro-" + name);
            closePalette();
            // the magnifier opens it too
            if (auto* b = dynamic_cast<juce::Button*> (&magnifier); b != nullptr && b->onClick)
                b->onClick();
            engine.dispatchPendingMessages();
            check (name + ": the magnifier opens it", palette() != nullptr);
            closePalette();
        }
        ui.setLayoutStyle (layoutstyle::Style::pattern);
        engine.dispatchPendingMessages();
        {
            // open time with the index built (median of 7 opens), and what it finds
            std::vector<double> times;
            for (int i = 0; i < 7; ++i)
            {
                ui.runActionForSnapshot ("view.commandPalette");
                times.push_back (ui.paletteOpenMsForSnapshot());
                if (i < 6)
                    closePalette();
            }
            std::sort (times.begin(), times.end());
            line ("open time, median of 7: " + juce::String (times[3], 2) + " ms (min " + juce::String (times.front(), 2) + ", max " + juce::String (times.back(), 2) + ")");
            check ("opens in 30 ms or less (median)", times[3] <= 30.0);
            auto* p = palette();
            if (p != nullptr)
            {
                p->setQuery ("metronome on/off");
                line ("\"metronome on/off\": first " + juce::String (p->selectedId()));
                check ("\"metronome on/off\" finds the metronome key first", p->selectedId() == "transport.metronome");
                const auto was = engine.getTransport().metronome.load();
                const auto ran = p->runSelected();
                engine.dispatchPendingMessages();
                check ("Enter runs it: the metronome toggles, as Ctrl+M does", ran && engine.getTransport().metronome.load() != was);
                ui.runActionForSnapshot ("transport.metronome"); // (back)
                closePalette();
                ui.runActionForSnapshot ("view.commandPalette");
                p = palette();
            }
            if (p != nullptr)
            {
                p->setQuery ("song bars");
                const auto id = p->selectedId();
                line ("\"song bars\": first " + juce::String (id));
                check ("a LYNX extra's switch is listed (\"song bars\")", juce::String (id).contains ("songBarsRuler"));
                p->setQuery ("export mp3");
                line ("\"export mp3\": first " + juce::String (p->selectedId()) + ", " + juce::String (p->matchCount()) + " items");
                check ("a menu item is found by its words (\"export mp3\")", p->matchCount() >= 1);
                write (ui, ui.getLocalBounds(), "p2-palette-export");
                p->setQuery ("settings: audio");
                line ("\"settings: audio\": first " + juce::String (p->selectedId()));
                check ("a Settings page is listed (\"settings: audio\")", juce::String (p->selectedId()) == "settings:Audio");
                p->setQuery ("nxt trnsnt");
                line ("\"nxt trnsnt\" (loose): " + juce::String (p->matchCount()) + " items, first " + juce::String (p->selectedId()));
                check ("loose matching (\"nxt trnsnt\" finds the transient actions)", juce::String (p->selectedId()).containsIgnoreCase ("transient"));
                write (ui, ui.getLocalBounds(), "p2-palette-loose");
                closePalette();
            }
            // "?" keeps opening it
            ws.activate (Panel::playlist);
            ui.keyPressed (juce::KeyPress ('/', juce::ModifierKeys::shiftModifier, '?'));
            engine.dispatchPendingMessages();
            check ("\"?\" still opens it", palette() != nullptr);
            closePalette();
        }

        // ================= P8: Tab to transient =================
        {
            ui.showPanelsForSnapshot ({ Panel::playlist });
            ws.activate (Panel::playlist);
            engine.dispatchPendingMessages();
            auto& pl = ui.getPlaylistForSnapshot();
            pl.setSize (juce::jmax (pl.getWidth(), 1200), juce::jmax (pl.getHeight(), 500));
            pl.setViewForSnapshot (400.0, 0, 0);
            pl.setPlayheadForSnapshot ((double) bar);
            engine.dispatchPendingMessages();
            const auto ticksPerSecond = engine.getSong().bpm / 60.0 * ppq;
            int visited = 0;
            double worstMs = 0.0;
            for (int k = 0; k < 8; ++k)
            {
                pl.runAction ("playlist.nextTransient");
                engine.dispatchPendingMessages();
                const auto at = (pl.playheadTickForSnapshot() - (double) bar) / ticksPerSecond;
                const auto errMs = std::abs (at - (firstHit + k * hitPeriod)) * 1000.0;
                worstMs = std::max (worstMs, errMs);
                visited += errMs <= 1.0 ? 1 : 0;
            }
            line ("Playlist: 8 presses, worst error " + juce::String (worstMs, 3) + " ms");
            check ("Playlist: 8 presses visit the 8 hits in order (within 1 ms)", visited == 8);
            pl.runAction ("playlist.prevTransient");
            engine.dispatchPendingMessages();
            check ("Playlist: back one hit", std::abs ((pl.playheadTickForSnapshot() - (double) bar) / ticksPerSecond - (firstHit + 6 * hitPeriod)) <= 0.001);
            pl.setPlayheadForSnapshot ((double) bar);
            pl.runAction ("playlist.nextTransient");
            pl.runAction ("playlist.extendToNextTransient");
            pl.runAction ("playlist.extendToNextTransient");
            pl.runAction ("playlist.extendToNextTransient");
            engine.dispatchPendingMessages();
            const auto& song = engine.getSong();
            const auto selFrom = (double) (song.loopStart - bar) / ticksPerSecond, selTo = (double) (song.loopEnd - bar) / ticksPerSecond;
            line ("Playlist: Shift x3 -> time selection " + juce::String (selFrom, 4) + " .. " + juce::String (selTo, 4) + " s of the clip");
            check ("Playlist: Shift extends the time selection hit by hit", song.hasLoop() && std::abs (selFrom - firstHit) <= 0.001 && std::abs (selTo - (firstHit + 3 * hitPeriod)) <= 0.001);
            pl.repaint();
            engine.dispatchPendingMessages();
            write (pl, { 0, 0, juce::jmin (pl.getWidth(), 1200), 200 }, "p8-playlist-selection");
            engine.editSong ([] (Song& s) { s.loopStart = s.loopEnd = 0; }, false);

            // the audio editor (the same editor as the Clip-launch Clip View)
            auto panel = makeAudioEditorPanel (engine, drumChannel, 0, false);
            check ("audio editor panel", panel != nullptr);
            if (panel != nullptr)
            {
                panel->setSize (1000, 380);
                engine.dispatchPendingMessages();
                int hits = 0;
                double worst = 0.0;
                std::int64_t cursor = 0, s0 = 0, s1 = 0;
                for (int k = 0; k < 8; ++k)
                    if (audioEditorTransientForSnapshot (*panel, 1, false, cursor, s0, s1))
                    {
                        const auto err = std::abs ((double) cursor / 44100.0 - (firstHit + k * hitPeriod)) * 1000.0;
                        worst = std::max (worst, err);
                        hits += err <= 1.0 ? 1 : 0;
                    }
                line ("audio editor: 8 presses, worst error " + juce::String (worst, 3) + " ms");
                check ("audio editor: 8 presses visit the 8 hits (within 1 ms)", hits == 8);
                audioEditorTransientForSnapshot (*panel, -1, false, cursor, s0, s1);
                audioEditorTransientForSnapshot (*panel, -1, false, cursor, s0, s1);
                audioEditorTransientForSnapshot (*panel, 1, true, cursor, s0, s1);
                audioEditorTransientForSnapshot (*panel, 1, true, cursor, s0, s1);
                line ("audio editor: selection " + juce::String ((double) s0 / 44100.0, 4) + " .. " + juce::String ((double) s1 / 44100.0, 4) + " s");
                check ("audio editor: Shift extends the selection hit by hit",
                       std::abs ((double) s0 / 44100.0 - (firstHit + 5 * hitPeriod)) <= 0.001 && std::abs ((double) s1 / 44100.0 - (firstHit + 7 * hitPeriod)) <= 0.001);
                write (*panel, panel->getLocalBounds(), "p8-audioeditor-selection");
            }
        }

        // ================= P10: stacked controller lanes =================
        {
            ui.showPanelsForSnapshot ({ Panel::pianoRoll });
            engine.setSelectedChannel (0);
            engine.dispatchPendingMessages();
            auto& pr = ui.getPianoRollForSnapshot();
            pr.setSize (1200, 640);
            pr.resized();
            pr.scrollToNotes();
            engine.dispatchPendingMessages();
            check ("Pattern layout: no \"+\" while the extra is off (FL's single lane)", ! PianoRollComponent::stackedLanesExtra && pr.stackedLaneCount() == 0);
            write (pr, pr.getLocalBounds(), "p10-pianoroll-off");
            const auto paintMs = [&pr] {
                juce::Image image (juce::Image::ARGB, pr.getWidth(), pr.getHeight(), true);
                std::vector<double> t;
                for (int i = 0; i < 15; ++i)
                {
                    juce::Graphics g (image);
                    const auto start = juce::Time::getMillisecondCounterHiRes();
                    pr.paintEntireComponent (g, false);
                    t.push_back (juce::Time::getMillisecondCounterHiRes() - start);
                }
                std::sort (t.begin(), t.end());
                return t[t.size() / 2];
            };
            const auto oneLane = paintMs();
            PianoRollComponent::stackedLanesExtra = true;
            pr.stackedLanesChanged();
            pr.addStackedLaneForSnapshot (PianoRollComponent::lanePan);
            pr.addStackedLaneForSnapshot (PianoRollComponent::laneFinePitch);
            pr.addStackedLaneForSnapshot (PianoRollComponent::laneModX);
            pr.moveEventPaneBar (pr.getHeight() / 2); // (room for four lanes)
            engine.dispatchPendingMessages();
            const auto lanes = pr.laneAreasForTests();
            juce::String ids;
            for (const auto& [area, id] : lanes)
                ids << id << "@" << area.getY() << "+" << area.getHeight() << " ";
            line ("lanes (id@y+h): " + ids);
            check ("four lanes on screen (three added + the bottom one)", lanes.size() == 4 && std::all_of (lanes.begin(), lanes.end(), [] (const auto& l) { return l.first.getHeight() >= 24; }));
            write (pr, pr.getLocalBounds(), "p10-pianoroll-4lanes");
            const auto fourLanes = paintMs();
            line ("paint (whole Piano roll, median of 15): one lane " + juce::String (oneLane, 2) + " ms, four lanes " + juce::String (fourLanes, 2) + " ms");
            check ("paint time with four lanes within +1 ms of one lane", fourLanes - oneLane <= 1.0);
            // an edit in lane 3 (index 2: Pan) changes only pan
            if (lanes.size() == 4)
            {
                const auto before = engine.getSong().patterns[0].notes;
                const auto area = lanes[2].first;
                pr.dragInLaneForSnapshot (2, area.getX() + 2, area.getRight() - 2, 0.9f, 0.9f);
                engine.dispatchPendingMessages();
                const auto after = engine.getSong().patterns[0].notes;
                bool panChanged = false, othersSame = before.size() == after.size();
                for (std::size_t i = 0; i < before.size() && i < after.size(); ++i)
                {
                    panChanged = panChanged || std::abs (after[i].pan - before[i].pan) > 0.1f;
                    othersSame = othersSame && after[i].velocity == before[i].velocity && after[i].modX == before[i].modX && after[i].finePitch == before[i].finePitch
                                 && after[i].modY == before[i].modY && after[i].release == before[i].release;
                }
                line ("lane 3 is id " + juce::String (lanes[2].second) + " (Pan = " + juce::String (PianoRollComponent::lanePan) + ")");
                check ("an edit in lane 3 changes only its target (Pan)", lanes[2].second == PianoRollComponent::lanePan && panChanged && othersSame);
                write (pr, pr.getLocalBounds(), "p10-pianoroll-4lanes-edited");
            }
            // the set is kept, and goes away with the switch
            const auto saved = PianoRollComponent::stackedLanesSaved;
            line ("saved: " + juce::String (saved));
            PianoRollComponent::stackedLanesExtra = false;
            pr.stackedLanesChanged();
            check ("switch off: FL's single lane again", pr.stackedLaneCount() == 0);
            PianoRollComponent::stackedLanesExtra = true;
            pr.stackedLanesChanged();
            check ("switch on: the saved lanes come back", pr.stackedLaneCount() == 3);
            PianoRollComponent::stackedLanesExtra = false;
            pr.stackedLanesChanged();
            PianoRollComponent::stackedLanesSaved.clear();
            pr.stackedLanesChanged();
            // the Clip-launch layout offers them with the extra off (Live 12's Velocity and Chance lanes, stacked)
            ui.setLayoutStyle (layoutstyle::Style::clipLaunch);
            ui.showPanelsForSnapshot ({ Panel::pianoRoll });
            engine.dispatchPendingMessages();
            pr.setSize (1200, 640);
            pr.stackedLanesChanged();
            check ("Clip-launch layout: stacked lanes offered with the extra off", pr.stackingEnabled());
            pr.addStackedLaneForSnapshot (PianoRollComponent::laneChance);
            pr.moveEventPaneBar (pr.getHeight() / 2);
            engine.dispatchPendingMessages();
            check ("Clip-launch layout: Velocity with Chance stacked", pr.stackedLaneCount() == 1 && pr.laneAreasForTests().size() == 2);
            write (pr, pr.getLocalBounds(), "p10-pianoroll-cliplaunch-chance");
            pr.removeStackedLaneForSnapshot (0);
            PianoRollComponent::stackedLanesSaved.clear();
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            engine.dispatchPendingMessages();
        }

        juce::File (prefix + "-checks.txt").replaceWithText (checks + (allOk ? "result=OK\n" : "result=FAIL\n"));
        wav.deleteFile();
        return allOk;
    }

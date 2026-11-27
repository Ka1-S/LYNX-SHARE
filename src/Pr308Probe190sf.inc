    // Round 190sf (review of PR #308, "Continuous scrolling"): LYNX --pr308probe <prefix> <project.flp> [--shot-scale 1.5
    // --shot-suffix @150]. Offscreen, the Playlist's live frame step driven by hand (the transport's position set per frame,
    // no audio clock): where the playhead sits in the grid and where the view starts, frame by frame, in the three layouts,
    // with Continuous scrolling on and off; a loop back to the start, a loop to a later bar, a zoom change and a window
    // resize while playing, and a start in Pattern mode after the view was scrolled. Writes <prefix>-checks.txt (result=OK
    // when every check passes), <prefix>-frames.csv and a shot of the playhead at a third in each layout.
    // Included in the app class of Main.cpp (savePng).
    void runPr308Probe (const juce::String& prefix, const juce::String& project)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        juce::String checks, csv ("layout,scenario,frame,tick,scrollTicks,pixelsPerTick,gridX,gridW,playheadX,ratio,scrollFracPx\n");
        bool allOk = true;
        const auto check = [&] (const juce::String& what, bool ok) { checks << (ok ? "ok   " : "FAIL ") << what << "\n"; allOk = allOk && ok; };
        const auto line = [&checks] (const juce::String& what) { checks << "     " << what << "\n"; };
        AudioEngine offline (false);
        MainComponent main (offline, false);
        main.setSize (1689, 950);
        if (project.isNotEmpty())
            main.openFile (juce::File (project));
        offline.dispatchPendingMessages();
        main.selectTabForSnapshot (2);
        auto& playlist = main.getPlaylistForSnapshot();
        offline.dispatchPendingMessages();
        auto& globals = EditorGlobals::get();
        globals.setFollowing (false, true, false);
        const auto bar = (double) offline.getSong().barTicks();
        auto& transport = offline.getTransport();
        offline.editSong ([] (Song& s) { s.mode = PlayMode::song; }, false);
        offline.dispatchPendingMessages();

        struct Frame { double tick, scroll, ppt, ratio, frac; int gx, gw; double px; };
        const auto frame = [&] (const juce::String& layout, const juce::String& scenario, int n, double tick) {
            transport.positionTicks.store (tick);
            playlist.frameStepForSnapshot();
            tick = offline.songPlayheadTicks(); // (what the Playlist shows: past the song's end it wraps)
            const auto g = playlist.gridAreaForSnapshot();
            const auto v = playlist.viewForSnapshot();
            const auto px = playlist.xOfTickForSnapshot (tick);
            Frame f { tick, v.scrollTicks, v.pixelsPerTick, (px - g.getX()) / juce::jmax (1, g.getWidth()),
                      std::abs (v.scrollTicks * v.pixelsPerTick - std::round (v.scrollTicks * v.pixelsPerTick)), g.getX(), g.getWidth(), px };
            csv << layout << "," << scenario << "," << n << "," << tick << "," << f.scroll << "," << f.ppt << "," << f.gx << "," << f.gw << ","
                << px << "," << f.ratio << "," << f.frac << "\n";
            return f;
        };
        const auto third = [] (const Frame& f) { return std::abs (f.ratio - 1.0 / 3.0) * f.gw <= 1.01; }; // (within a pixel)

        const std::pair<layoutstyle::Style, const char*> layouts[] { { layoutstyle::Style::pattern, "pattern" },
                                                                     { layoutstyle::Style::clipLaunch, "cliplaunch" },
                                                                     { layoutstyle::Style::simple, "simple" } };
        for (const auto& [style, name] : layouts)
        {
            const juce::String L (name);
            main.setSize (1689, 950);
            main.setLayoutStyle (style, false);
            main.selectTabForSnapshot (2);
            offline.dispatchPendingMessages();
            playlist.setViewForSnapshot (80.0, 0, 0);
            transport.playing.store (false);
            transport.positionTicks.store (0.0);
            playlist.frameStepForSnapshot();

            // 1. Continuous on: from bar 1 for three views' widths, 3 px a frame
            globals.setContinuousScroll (true);
            transport.playing.store (true);
            auto f = frame (L, "start", 0, 0.0);
            const auto viewTicks = f.gw / f.ppt;
            int before = 0, held = 0, frames = 0, fractional = 0, backwards = 0, scrolls = 0, fullPx = 0;
            double lastScroll = f.scroll, worst = 0.0;
            bool shot = false;
            for (double t = 0.0; t < 3.0 * viewTicks; t += 3.0 / f.ppt, ++frames)
            {
                const auto ff = frame (L, "run", frames, t);
                if (t * ff.ppt < ff.gw / 3.0 - 1.0)
                    before += ff.scroll == 0.0 ? 1 : 0;
                else if (t * ff.ppt > ff.gw / 3.0 + 1.0)
                {
                    held += third (ff) ? 1 : 0;
                    worst = juce::jmax (worst, std::abs (ff.ratio - 1.0 / 3.0) * ff.gw);
                }
                fractional += ff.frac > 1e-6 ? 1 : 0;
                backwards += ff.scroll < lastScroll ? 1 : 0;
                if (ff.scroll != lastScroll)
                {
                    ++scrolls;
                    const auto dx = (ff.scroll - lastScroll) * ff.ppt;
                    fullPx += std::abs (dx - std::round (dx)) < 1e-6 ? 1 : 0;
                }
                lastScroll = ff.scroll;
                if (! shot && t > 1.5 * viewTicks)
                {
                    savePng (shot::snapshot (playlist), prefix + "-" + L + "-continuous-third.png");
                    shot = true;
                }
            }
            const auto beforeFrames = (int) std::ceil ((f.gw / 3.0 - 1.0) / 3.0);
            line (L + ": grid " + juce::String (f.gw) + " px, " + juce::String (frames) + " frames, " + juce::String (scrolls) + " scroll steps, worst "
                  + juce::String (worst, 2) + " px from a third");
            check (L + ": the view stays at the start until the playhead reaches a third", before >= beforeFrames - 1);
            check (L + ": then the playhead stays at a third of the grid (within 1 px)", held > 0 && worst <= 1.01);
            check (L + ": the view starts on whole pixels (" + juce::String (fractional) + " fractional)", fractional == 0);
            check (L + ": every scroll step is whole pixels", fullPx == scrolls);
            check (L + ": the view never moves back while playing on", backwards == 0);

            // 2. a loop back to the start (the song's loop / a jump to bar 1): the view goes back with it
            f = frame (L, "loop-start", 0, 0.0);
            check (L + ": a loop back to the start shows the start", f.scroll == 0.0 && std::abs (f.ratio) < 1e-9);
            // ... and a loop to bar 17 (past a third): the playhead lands at a third at once
            f = frame (L, "loop-bar17", 0, 16.0 * bar);
            check (L + ": a jump to bar 17 puts the playhead at a third", third (f));

            // 3. a zoom change while playing (Ctrl+wheel, zoom keys): the next frame holds a third at the new zoom
            f = frame (L, "zoom-before", 0, 24.0 * bar);
            playlist.setViewForSnapshot (160.0, (int) (f.scroll / bar), 0);
            f = frame (L, "zoom-after", 0, 24.0 * bar + 3.0 / f.ppt);
            check (L + ": after a zoom in the playhead is at a third again", third (f));
            playlist.setViewForSnapshot (40.0, (int) (f.scroll / bar), 0);
            f = frame (L, "zoom-out", 0, 24.0 * bar + 9.0 / f.ppt); // (the playhead moves: the Follow step runs on a moving frame)
            check (L + ": after a zoom out too", third (f));
            playlist.setViewForSnapshot (80.0, (int) (f.scroll / bar), 0);

            // 4. a window resize while playing
            main.setSize (1200, 800);
            offline.dispatchPendingMessages();
            f = frame (L, "resize", 0, 30.0 * bar);
            check (L + ": after a resize the playhead is at a third of the new grid (" + juce::String (f.gw) + " px)", third (f));
            main.setSize (1689, 950);
            offline.dispatchPendingMessages();
            f = frame (L, "resize-back", 0, 30.0 * bar + 3.0 / f.ppt);
            check (L + ": and back", third (f));

            // 5. stopped there, the view scrolled by hand, Pattern mode played: the view stays where it is
            transport.playing.store (false);
            playlist.frameStepForSnapshot();
            playlist.setViewForSnapshot (80.0, 40, 0);
            playlist.frameStepForSnapshot();
            const auto handScroll = playlist.viewForSnapshot().scrollTicks;
            offline.editSong ([] (Song& s) { s.mode = PlayMode::pattern; }, false);
            offline.dispatchPendingMessages();
            transport.positionTicks.store (0.0);
            transport.playing.store (true);
            for (int k = 0; k < 5; ++k)
            {
                transport.positionTicks.store (k * 3.0 / f.ppt);
                playlist.frameStepForSnapshot();
            }
            const auto afterPattern = playlist.viewForSnapshot().scrollTicks;
            line (L + ": Pattern mode played: view start bar " + juce::String (handScroll / bar + 1.0, 2) + " -> " + juce::String (afterPattern / bar + 1.0, 2));
            check (L + ": playing in Pattern mode leaves the Playlist's view where it was (as with the option off)", afterPattern == handScroll);
            transport.playing.store (false);
            playlist.frameStepForSnapshot();
            offline.editSong ([] (Song& s) { s.mode = PlayMode::song; }, false);
            offline.dispatchPendingMessages();

            // 6. the option off: FL's page jump as before (the view moves only by pages)
            globals.setContinuousScroll (false);
            playlist.setViewForSnapshot (80.0, 0, 0);
            transport.playing.store (true);
            f = frame (L, "off-start", 0, 0.0);
            int moves = 0;
            lastScroll = f.scroll;
            for (double t = 0.0; t < 3.0 * viewTicks; t += 3.0 / f.ppt)
            {
                const auto ff = frame (L, "off-run", 0, t);
                moves += ff.scroll != lastScroll ? 1 : 0;
                lastScroll = ff.scroll;
            }
            line (L + ": option off: " + juce::String (moves) + " page jumps over three views");
            check (L + ": option off pages (2 to 4 jumps over three views)", moves >= 2 && moves <= 4);
            transport.playing.store (false);
            playlist.frameStepForSnapshot();
        }
        main.setLayoutStyle (layoutstyle::Style::pattern, false);
        checks << "result=" << (allOk ? "OK" : "FAIL") << "\n";
        juce::File (prefix + "-checks.txt").replaceWithText (checks);
        juce::File (prefix + "-frames.csv").replaceWithText (csv);
    }

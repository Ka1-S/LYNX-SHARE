    // LYNX --hwheel254test <report.txt> (round 190ou, issue #254): the horizontal wheel (a tilt wheel, a mouse's second
    // wheel) through the real components, off screen, as JUCE delivers it (Windows' WM_MOUSEHWHEEL: deltaX = -/+60/256 a
    // notch, right / left; X11's buttons 6 / 7 come the same way with the JUCE patch fix 49). FL 2026 (checked in this
    // round on Thief): Playlist and Piano roll 1.5 beats a notch whatever the zoom, the Mixer one insert a notch, the
    // Browser takes it as the vertical wheel (right = up). Shift+wheel, a trackpad's swipe and the vertical wheel keep
    // what they did. The Playlist is checked in the three layouts. Included in the app class of Main.cpp.
    bool runHWheel254Test (const juce::String& reportPath)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (lynx::ui::showingForShots(), true);
        using namespace lynx;
        const auto motionBefore = ui::motion::enabled();
        ui::motion::enabled() = false; // (the wheel jumps: no glide to wait for)
        juce::String text;
        bool allOk = true;
        const auto check = [&text, &allOk] (const juce::String& what, bool ok) {
            text << (ok ? "ok   " : "FAIL ") << what << "\n";
            allOk = allOk && ok;
        };
        const auto mouseOn = [] (juce::Component& target, juce::Point<int> p, juce::ModifierKeys mods) {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, p.toFloat(), now, 1, false);
        };
        constexpr float notch = 60.0f / 256.0f;
        const auto tilt = [notch] (int notches) { // (> 0: to the right)
            juce::MouseWheelDetails w {};
            w.deltaX = notches > 0 ? -notch : notch;
            return w;
        };
        const auto none = juce::ModifierKeys();
        const auto shiftKey = juce::ModifierKeys (juce::ModifierKeys::shiftModifier);
        const auto near = [] (double a, double b) { return std::abs (a - b) < 1.0e-6; };

        // a song long enough to scroll: 64 bars of a 1-bar pattern on track 1, a 16-bar pattern of notes, 40 inserts
        auto song = Song::makeDefault();
        song.mode = PlayMode::song;
        auto& p = song.patterns[0];
        for (int k = 0; k < 64; ++k)
        {
            Note n;
            n.channel = 4;
            n.start = k * ppq;
            n.length = ppq / 2;
            n.key = 60 + k % 12;
            p.notes.push_back (n);
        }
        song.playlist.clear();
        for (int bar = 0; bar < 64; ++bar)
            song.playlist.push_back ({ 0, 0, bar * 16 * ppq, 16 * ppq, 0 }); // (the 16-beat pattern, 64 times: 256 bars)
        if (song.mixer.size() < 40)
            song.mixer.resize (40);
        AudioEngine offline (false);
        offline.setSong (song);
        MainComponent ui (offline, false);
        ui.setSize (1920, 1080);
        offline.dispatchPendingMessages();

        // ---- the Playlist, in the Pattern, Clip-launch (its Arrangement) and Simple layouts
        const auto playlistChecks = [&] (const juce::String& layout) {
            auto& pl = ui.getPlaylistForSnapshot();
            const auto at = juce::Point<int> (pl.getWidth() * 3 / 5, pl.getHeight() / 2);
            const auto barTicks = (double) offline.getSong().barTicks();
            text << "     " << layout << ": Playlist " << pl.getBounds().toString() << "\n";
            for (const auto pixelsPerBar : { 16.0, 384.0 })
            {
                pl.setViewForSnapshot (pixelsPerBar, 8, 0);
                const auto start = pl.scrollTicksForSnapshot();
                const auto ppt = pixelsPerBar / barTicks;
                pl.mouseWheelMove (mouseOn (pl, at, none), tilt (1));
                pl.mouseWheelMove (mouseOn (pl, at, none), tilt (1));
                const auto right = pl.scrollTicksForSnapshot() - start;
                pl.mouseWheelMove (mouseOn (pl, at, none), tilt (-1));
                const auto back = pl.scrollTicksForSnapshot() - start;
                // (the view lands on whole pixels: within a pixel of 1.5 beats a notch)
                const auto beats = [&] (double ticks) { return juce::String (ticks / ppq, 3); };
                check (layout + ", " + juce::String (pixelsPerBar, 0) + " px a bar: two notches right = 3 beats later, one left = 1.5 beats back ("
                           + beats (right) + ", " + beats (back) + ")",
                       std::abs (right - 3.0 * ppq) <= 1.0 / ppt + 1.0e-6 && std::abs (back - 1.5 * ppq) <= 1.0 / ppt + 1.0e-6);
                // Shift+wheel over the clips (FL's nudge: over an empty part nothing moves) and a trackpad's sideways swipe
                // (80 x 7 px a whole unit): as before
                pl.setViewForSnapshot (pixelsPerBar, 8, 0);
                juce::MouseWheelDetails shiftDown {};
                shiftDown.deltaY = -notch;
                pl.mouseWheelMove (mouseOn (pl, at, shiftKey), shiftDown);
                const auto shifted = (pl.scrollTicksForSnapshot() - start) * ppt;
                pl.setViewForSnapshot (pixelsPerBar, 8, 0);
                juce::MouseWheelDetails swipe {};
                swipe.deltaX = -0.1f;
                swipe.isSmooth = true;
                pl.mouseWheelMove (mouseOn (pl, at, none), swipe);
                const auto swiped = (pl.scrollTicksForSnapshot() - start) * ppt;
                check (layout + ", " + juce::String (pixelsPerBar, 0) + " px a bar: Shift+wheel does not scroll (FL's nudge), a trackpad swipe 56 px, as before ("
                           + juce::String (shifted, 1) + ", " + juce::String (swiped, 1) + ")",
                       std::abs (shifted) < 1.0e-6 && std::abs (swiped - 56.0) <= 1.0);
            }
        };
        ui.showPanelsForSnapshot ({ workspace::Panel::playlist });
        offline.dispatchPendingMessages();
        playlistChecks ("Pattern layout");
        ui.setClipLaunchWorkflow (true);
        ui.showPanelsForSnapshot ({ workspace::Panel::playlist });
        ui.setClipGridView (PlaylistArea::GridView::song);
        offline.dispatchPendingMessages();
        playlistChecks ("Clip-launch layout");
        ui.setLayoutStyle (layoutstyle::Style::simple);
        offline.dispatchPendingMessages();
        playlistChecks ("Simple layout");
        ui.setLayoutStyle (layoutstyle::Style::pattern);
        offline.dispatchPendingMessages();

        // ---- the Piano roll: 1.5 beats a notch at two zooms
        {
            ui.showPanelsForSnapshot ({ workspace::Panel::pianoRoll });
            offline.setSelectedChannel (4);
            offline.dispatchPendingMessages();
            auto& pr = ui.getPianoRollForSnapshot();
            const auto at = juce::Point<int> (pr.getWidth() * 3 / 5, pr.getHeight() / 3);
            text << "     Piano roll " << pr.getBounds().toString() << "\n";
            for (const auto* zoom : { "pianoRoll.zoom2", "pianoRoll.zoom1" })
            {
                pr.runAction (zoom);
                pr.setViewForTests (4.0 * ppq, 16, 66);
                const auto start = pr.scrollTicksForTests();
                const auto ppt = pr.pixelsPerTickForTests();
                pr.mouseWheelMove (mouseOn (pr, at, none), tilt (1));
                pr.mouseWheelMove (mouseOn (pr, at, none), tilt (1));
                const auto right = pr.scrollTicksForTests() - start;
                pr.mouseWheelMove (mouseOn (pr, at, none), tilt (-1));
                const auto back = pr.scrollTicksForTests() - start;
                check (juce::String ("Piano roll, ") + juce::String (ppt * 4 * ppq, 0) + " px a bar: two notches right = 3 beats, one left = 1.5 beats back ("
                           + juce::String (right / ppq, 3) + ", " + juce::String (back / ppq, 3) + ")",
                       near (right, 3.0 * ppq) && near (back, 1.5 * ppq));
                // Shift+wheel (80 px a notch sideways) and a trackpad's swipe (60 x 7 px a whole unit): as before
                pr.setViewForTests (4.0 * ppq, 16, 66);
                juce::MouseWheelDetails shiftDown {};
                shiftDown.deltaY = -notch;
                pr.mouseWheelMove (mouseOn (pr, at, shiftKey), shiftDown);
                const auto shifted = (pr.scrollTicksForTests() - start) * ppt;
                pr.setViewForTests (4.0 * ppq, 16, 66);
                juce::MouseWheelDetails swipe {};
                swipe.deltaX = -0.1f;
                swipe.isSmooth = true;
                pr.mouseWheelMove (mouseOn (pr, at, none), swipe);
                const auto swiped = (pr.scrollTicksForTests() - start) * ppt;
                check ("Piano roll: Shift+wheel 80 px, a trackpad swipe 42 px, as before (" + juce::String (shifted, 1) + ", " + juce::String (swiped, 1) + ")",
                       std::abs (shifted - 80.0) <= 0.5 && std::abs (swiped - 42.0) <= 0.5);
            }
        }

        // ---- the Mixer: one insert a notch, stopped at the first one
        {
            ui.showPanelsForSnapshot ({ workspace::Panel::mixer });
            offline.dispatchPendingMessages();
            auto& strips = ui.getMixerForSnapshot().getStripsForSnapshot();
            auto* view = strips.findParentComponentOfClass<juce::Viewport>();
            const auto width = strips.widthOf (5);
            if (view == nullptr)
                check ("Mixer: its viewport", false);
            else
            {
                view->setViewPosition (0, 0);
                const auto at = juce::Point<int> (view->getViewWidth() / 2, strips.getHeight() - 20);
                for (int k = 0; k < 3; ++k)
                    strips.mouseWheelMove (mouseOn (strips, at + juce::Point<int> (view->getViewPositionX(), 0), none), tilt (1));
                const auto right = view->getViewPositionX();
                for (int k = 0; k < 5; ++k)
                    strips.mouseWheelMove (mouseOn (strips, at + juce::Point<int> (view->getViewPositionX(), 0), none), tilt (-1));
                const auto back = view->getViewPositionX();
                check ("Mixer: three notches right = three inserts (" + juce::String (right) + " px, an insert " + juce::String (width)
                           + " px), five left stop at the first (" + juce::String (back) + ")",
                       right == 3 * width && back == 0);
            }
        }

        // ---- the Browser: its lists take the horizontal wheel as the vertical one (right = up), as FL's
        {
            auto& browser = ui.getBrowserForSnapshot();
            check ("Browser: its folder list takes the horizontal wheel", dynamic_cast<ui::TiltViewport*> (browser.treeViewForTests()) != nullptr);
            ui::TiltViewport list;
            juce::Component tall;
            tall.setSize (300, 3000);
            list.setViewedComponent (&tall, false);
            list.setScrollBarsShown (true, false);
            list.setSize (300, 400);
            list.setViewPosition (0, 1000);
            list.mouseWheelMove (mouseOn (list, { 100, 100 }, none), tilt (1));
            const auto up = list.getViewPositionY();
            list.mouseWheelMove (mouseOn (list, { 100, 100 }, none), tilt (-1));
            list.mouseWheelMove (mouseOn (list, { 100, 100 }, none), tilt (-1));
            const auto down = list.getViewPositionY();
            juce::MouseWheelDetails wheelUp {};
            wheelUp.deltaY = notch;
            list.setViewPosition (0, 1000);
            list.mouseWheelMove (mouseOn (list, { 100, 100 }, none), wheelUp);
            const auto vertical = list.getViewPositionY();
            check ("Browser list: a notch right scrolls up as the wheel turned up (" + juce::String (up) + " / " + juce::String (vertical)
                       + "), two left scroll down past the start (" + juce::String (down) + ")",
                   up < 1000 && up == vertical && down > 1000);
        }

       #if JUCE_LINUX
        // ---- X11 end to end (the JUCE patch fix 49): XTest presses buttons 7 and 6 over a window of ours; JUCE must
        // deliver them as the horizontal wheel (7 = right = deltaX < 0, 6 = left = deltaX > 0). Needs an X server.
        if (juce::SystemStats::getEnvironmentVariable ("DISPLAY", {}).isNotEmpty())
        {
            struct Catcher final : juce::Component
            {
                float x = 0.0f, y = 0.0f;
                int events = 0;
                void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { x += w.deltaX; y += w.deltaY; ++events; }
                void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
            };
            const auto pumpFor = [] (int ms, const std::function<bool()>& until) {
                const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
                while (juce::Time::getMillisecondCounter() < end && ! (until && until()))
                {
                    for (int i = 0; i < 256 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i) {}
                    juce::Thread::sleep (10);
                }
            };
            auto* x11 = dlopen ("libX11.so.6", RTLD_NOW);
            auto* xtst = dlopen ("libXtst.so.6", RTLD_NOW);
            using OpenDisplay = void* (*) (const char*);
            using CloseDisplay = int (*) (void*);
            using Flush = int (*) (void*);
            using FakeMotion = int (*) (void*, int, int, int, unsigned long);
            using FakeButton = int (*) (void*, unsigned int, int, unsigned long);
            auto open = x11 != nullptr ? (OpenDisplay) dlsym (x11, "XOpenDisplay") : nullptr;
            auto close = x11 != nullptr ? (CloseDisplay) dlsym (x11, "XCloseDisplay") : nullptr;
            auto flush = x11 != nullptr ? (Flush) dlsym (x11, "XFlush") : nullptr;
            auto motion = xtst != nullptr ? (FakeMotion) dlsym (xtst, "XTestFakeMotionEvent") : nullptr;
            auto button = xtst != nullptr ? (FakeButton) dlsym (xtst, "XTestFakeButtonEvent") : nullptr;
            auto* display = open != nullptr ? open (nullptr) : nullptr;
            if (display == nullptr || motion == nullptr || button == nullptr || flush == nullptr)
                text << "     X11: no XTest here (" << (xtst == nullptr ? "no libXtst" : "no display") << "): the end-to-end check is skipped\n";
            else
            {
                const auto scale = juce::Desktop::getInstance().getGlobalScaleFactor();
                juce::Desktop::getInstance().setGlobalScaleFactor (1.0f);
                Catcher catcher;
                catcher.setBounds (200, 200, 400, 300);
                catcher.addToDesktop (0);
                catcher.setVisible (true);
                catcher.toFront (false);
                pumpFor (500, {});
                const auto press = [&] (unsigned int b) {
                    motion (display, -1, 400, 350, 0);
                    button (display, b, 1, 0);
                    button (display, b, 0, 0);
                    flush (display);
                };
                press (7);
                pumpFor (2000, [&catcher] { return catcher.events >= 1; });
                const auto right = catcher.x, rightY = catcher.y;
                const auto first = catcher.events;
                press (6);
                press (6);
                pumpFor (2000, [&catcher, first] { return catcher.events >= first + 2; });
                const auto left = catcher.x - right;
                check ("X11: button 7 is the horizontal wheel to the right, 6 to the left (deltaX " + juce::String (right, 3) + ", then "
                           + juce::String (left, 3) + " for two, deltaY " + juce::String (rightY, 3) + ", " + juce::String (catcher.events) + " events)",
                       first == 1 && right < 0.0f && rightY == 0.0f && left > 0.0f && catcher.events == 3);
                catcher.removeFromDesktop();
                juce::Desktop::getInstance().setGlobalScaleFactor (scale);
                close (display);
            }
        }
       #endif

        ui::motion::enabled() = motionBefore;
        text << "result=" << (allOk ? "OK" : "FAILED") << "\n";
        juce::File (reportPath).replaceWithText (text);
        return allOk;
    }

    // --rack190ncshot <prefix> (round 190nc, 0.1.31): the Clip-launch layout's Live look — a rack in the Device View with
    // its chain's devices inline (DL-95), Live's Macro Map overlay (Map on, a parameter picked, the Map button, the Macro
    // Mappings list in the Browser's place), and the Session mixer's Solo / Cue controls (DL-110: one output pair, then a
    // device given four outputs). <prefix>-checks.txt lists the checks; -perf.txt the Device View's paint times. Included
    // in the app class of Main.cpp (savePng / saveMenus). @150: LYNX_SHOT_SCALE=1.5 and LYNX_SHOT_SUFFIX=@150.
    bool runRackShot190nc (const juce::String& prefix)
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
        const auto mouseOn = [] (juce::Component& target, juce::Point<int> p, juce::ModifierKeys mods, int clicks = 1) {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, p.toFloat(), now, clicks, false);
        };
        std::function<lynx::RackView* (juce::Component&)> findRack = [&findRack] (juce::Component& c) -> lynx::RackView* {
            if (auto* r = dynamic_cast<lynx::RackView*> (&c))
                return r;
            for (auto* child : c.getChildren())
                if (auto* r = findRack (*child))
                    return r;
            return nullptr;
        };
        using namespace lynx;

        AudioEngine offline (false);
        MainComponent ui (offline, false);
        ui.setSize (1920, 1080);
        ui.setVisible (true);
        DetailView::shownSetting = DetailView::devicesPage = true;
        ui.setClipLaunchWorkflow (true);
        ui.showPanelsForSnapshot ({ workspace::Panel::browser, workspace::Panel::playlist });
        ui.setClipGridView (PlaylistArea::GridView::grid);
        // a "Main Bus" track: a synth on insert 2 whose chain is an Audio Effect Rack holding a compressor, an EQ and a
        // distortion (Live's demo: Glue Compressor, EQ Eight, Roar), three macros mapped as Live's demo maps its four
        offline.editSong ([] (Song& s) {
            if (s.channels.empty())
                s.channels.resize (1);
            s.channels[0].name = "Main Bus";
            s.channels[0].instrument = InstrumentKind::synth;
            s.channels[0].mixerTrack = 2;
            if (s.mixer.size() < 5)
                s.mixer.resize (5);
            s.mixer[2].name = "Main Bus";
            s.mixer[3].name = "Keys";
            auto slot = rack::makeRackSlot (RackDoc::Kind::audioEffect);
            auto& doc = slot.patcher;
            rack::rackOf (doc)->shownMacros = 4;
            rack::rackOf (doc)->showChains = true;
            rack::rackOf (doc)->showDevices = true;
            rack::rackOf (doc)->chains[0].name = "Bus Glue";
            int nodes[3] {};
            int k = 0;
            for (auto kind : { EffectKind::compressor, EffectKind::eq, EffectKind::distortion })
            {
                PatchNode n;
                n.effect = makeEffectSlot (kind);
                nodes[k++] = rack::addDevice (doc, 0, n);
            }
            RackMapping a;
            a.node = nodes[0], a.param = 0;
            rack::map (doc, 0, a);
            RackMapping b;
            b.node = nodes[0], b.param = 1;
            rack::map (doc, 1, b);
            RackMapping c;
            c.node = nodes[1], c.param = 0;
            rack::map (doc, 2, c);
            s.mixer[2].effects = { slot };
            while (s.patterns.size() < 1)
                s.patterns.emplace_back();
            Note note;
            note.channel = 0;
            note.key = 60;
            s.patterns[0].notes = { note };
            s.patterns[0].lengthTicks = 4 * ppq;
            s.playlistTracks.resize (std::max<std::size_t> (s.playlistTracks.size(), 3));
            s.playlistTracks[0].name = "Main Bus";
            Clip clip;
            clip.pattern = 0;
            clip.length = 4 * ppq;
            launcher::setClip (s, 0, 0, clip);
        }, false);
        offline.dispatchPendingMessages();
        ui.resized();
        auto* detail = ui.getDetailViewForSnapshot();
        check ("the Device View exists in the Clip-launch layout", detail != nullptr);
        if (detail == nullptr)
        {
            juce::File (prefix + "-checks.txt").replaceWithText (checks);
            return false;
        }
        detail->showSelection (0, 0);
        detail->setPage (DetailView::Page::devices);
        offline.dispatchPendingMessages();
        detail->resized();
        auto* rackView = findRack (*detail);
        check ("Live's look is on (Clip-launch)", theme::livelook::active());
        check ("the rack's card shows its view", rackView != nullptr);
        if (rackView == nullptr)
        {
            juce::File (prefix + "-checks.txt").replaceWithText (checks);
            return false;
        }

        // ---- DL-95 (1): the chain's devices inline
        check ("the chain's three devices are shown inline", rackView->showsDevicesInline() && rackView->inlineDeviceCountForCheck() == 3);
        const auto d0 = rackView->inlineDeviceBoundsForCheck (0), d1 = rackView->inlineDeviceBoundsForCheck (1);
        check ("devices 2 px apart (Live: 379..381)", d1.getX() - d0.getRight() == 2);
        check ("each device as tall as the rack (its title at the rack's top)", d0.getY() == 0 && d0.getBottom() == rackView->getHeight());
        const auto bar = rackView->titleBarWidth();
        check ("the rack's title bar spans its own part only", bar > 0 && bar < d0.getX() + 1 && bar < rackView->getWidth());
        check ("the title strip over the rack's own part is the Device View's (no hit there)", ! rackView->hitTest (bar / 2, 5) && rackView->hitTest (d0.getX() + 40, 5));
        check ("the first device shows knobs", ! rackView->inlineKnobBoundsForCheck (0, 0).isEmpty());
        line ("rack " + rackView->getBounds().toString() + ", title bar " + juce::String (bar) + ", devices " + d0.toString() + " " + d1.toString() + " "
              + rackView->inlineDeviceBoundsForCheck (2).toString());
        write (ui, "01-device-view-inline");
        write (*detail, "01b-detail");
        const auto widthBefore = rackView->getWidth();
        rackView->toggleInlineFoldForCheck (1);
        offline.dispatchPendingMessages();
        detail->resized();
        check ("a device folds to its 22 px title (the rack narrows)", rackView->inlineDeviceBoundsForCheck (1).getWidth() == 22 && rackView->getWidth() < widthBefore);
        write (*detail, "02-detail-folded");
        rackView->toggleInlineFoldForCheck (1);
        offline.dispatchPendingMessages();
        detail->resized();

        // ---- DL-95 (2): Live's Macro Map overlay
        rackView->setMapMode (true);
        offline.dispatchPendingMessages();
        auto* list = rackView->mappingsListForCheck();
        auto& browser = ui.getBrowserForSnapshot();
        check ("Map on: the Macro Mappings list takes the Browser's place", list != nullptr && list->isVisible() && list->getParentComponent() == &browser
                                                                           && list->getBounds() == browser.getLocalBounds());
        write (ui, "03-map-on");
        write (*detail, "03b-detail-map-on");
        if (list != nullptr)
            write (*list, "03c-macro-mappings");
        // a click on the EQ's second knob picks it (nothing changes), then macro 4's Map button maps it
        const auto& song0 = offline.getSong();
        const auto before = song0.mixer[2].effects[0].patcher.nodes;
        const auto knob = rackView->inlineKnobBoundsForCheck (1, 1);
        rackView->mouseDown (mouseOn (*rackView, knob.getCentre(), {}));
        rackView->mouseUp (mouseOn (*rackView, knob.getCentre(), {}));
        offline.dispatchPendingMessages();
        check ("Map mode: a click picks the parameter, its value unchanged", offline.getSong().mixer[2].effects[0].patcher.nodes == before);
        write (*detail, "04-detail-picked");
        const auto mapAt = rackView->mapButtonBoundsForCheck (3);
        rackView->mouseDown (mouseOn (*rackView, mapAt.getCentre(), {}));
        rackView->mouseUp (mouseOn (*rackView, mapAt.getCentre(), {}));
        offline.dispatchPendingMessages();
        {
            const auto* r = rack::rackOf (offline.getSong().mixer[2].effects[0].patcher);
            const auto eq = r != nullptr ? rack::devices (*r, 0)[1] : -1;
            RackMapping x;
            x.node = eq, x.param = 1;
            check ("then macro 4's Map button maps it to macro 4", r != nullptr && rack::macroOf (*r, x) == 3);
        }
        write (ui, "05-mapped");
        write (*detail, "05b-detail-mapped");
        if (list != nullptr)
            write (*list, "05c-macro-mappings");
        rackView->setMapMode (false);
        check ("Map off: the list goes", rackView->mappingsListForCheck() == nullptr);

        // the rack's own window keeps LYNX's devices pane (Live's look only in the Device View)
        {
            auto window = RackWindows::makeSlotView (offline, 2, 0, true);
            window->setSize (std::max (720, window->preferredWidth (230)), 230);
            check ("the rack's window keeps its devices pane (no inline devices)", ! window->showsDevicesInline() && window->titleBarWidth() == 0);
            write (*window, "06-rack-window");
        }

        // ---- the Device View's paint cost with the inline rack (ms per paint of the whole Detail View, 60 paints)
        {
            juce::Image img (juce::Image::ARGB, detail->getWidth(), detail->getHeight(), true);
            const auto paints = [&] (int n) {
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                for (int i = 0; i < n; ++i)
                {
                    juce::Graphics g (img);
                    detail->paintEntireComponent (g, true);
                }
                return (juce::Time::getMillisecondCounterHiRes() - t0) / n;
            };
            paints (5);
            const auto inlineMs = paints (60);
            offline.editSong ([] (Song& s) { rack::rackOf (s.mixer[2].effects[0].patcher)->showDevices = false; }, false);
            offline.dispatchPendingMessages();
            detail->resized();
            paints (5);
            const auto hiddenMs = paints (60);
            rackView->setMapMode (true);
            offline.editSong ([] (Song& s) { rack::rackOf (s.mixer[2].effects[0].patcher)->showDevices = true; }, false);
            offline.dispatchPendingMessages();
            detail->resized();
            paints (5);
            const auto mapMs = paints (60);
            rackView->setMapMode (false);
            juce::File (prefix + "-perf.txt").replaceWithText ("Detail View paint, ms per paint (60 paints, software renderer):\n  rack with 3 inline devices: "
                                                             + juce::String (inlineMs, 3) + "\n  the same rack, devices hidden: " + juce::String (hiddenMs, 3)
                                                             + "\n  inline devices in Map mode: " + juce::String (mapMs, 3) + "\n");
            line ("paint ms: inline " + juce::String (inlineMs, 3) + ", devices hidden " + juce::String (hiddenMs, 3) + ", map mode " + juce::String (mapMs, 3));
        }

        // ---- DL-110: the Session mixer's Solo / Cue
        offline.dispatchPendingMessages();
        auto* grid = ui.getClipGridForSnapshot();
        auto* mixer = grid != nullptr ? clipGridMixer (*grid) : nullptr;
        check ("the Session mixer is shown", mixer != nullptr && mixer->isVisible() && mixer->getHeight() > 0);
        if (mixer != nullptr)
        {
            constexpr int mainCol = -2;
            using P = SessionMixerComponent::Part;
            offline.setOutputChannelCountForCheck (2); // (the owner's device: one output pair)
            mixer->repaint();
            check ("the Main strip shows Cue Out, Main Out, the Solo/Cue switch and the Cue Volume knob",
                   ! mixer->partBounds (mainCol, P::cueOut).isEmpty() && ! mixer->partBounds (mainCol, P::mainOut).isEmpty()
                       && ! mixer->partBounds (mainCol, P::soloCue).isEmpty() && ! mixer->partBounds (mainCol, P::cueVolume).isEmpty());
            line ("main parts: cue out " + mixer->partBounds (mainCol, P::cueOut).toString() + ", main out " + mixer->partBounds (mainCol, P::mainOut).toString()
                  + ", switch " + mixer->partBounds (mainCol, P::soloCue).toString() + ", knob " + mixer->partBounds (mainCol, P::cueVolume).toString());
            const auto sw = mixer->partBounds (mainCol, P::soloCue).getCentre();
            mixer->mouseDown (mouseOn (*mixer, sw, {}));
            mixer->mouseUp (mouseOn (*mixer, sw, {}));
            offline.dispatchPendingMessages();
            check ("one output pair (Cue Out = Main Out): the switch stays on Solo (190mv B1)", ! sessionmixer::cueOf (offline.getSong()).cueMode);
            write (*mixer, "07-mixer-one-pair");
            const auto main = juce::Rectangle<int> (mixer->partBounds (mainCol, P::cueOut).getX() - 8, 0, 100, mixer->getHeight());
            savePng (mixer->createComponentSnapshot (main.getIntersection (mixer->getLocalBounds()), true, 3.0f), prefix + "-07b-main-strip-one-pair@3x.png");
            // a device with four outputs: Cue Out 3/4, the switch to Cue, the first track cued
            offline.setOutputChannelCountForCheck (4);
            offline.setCueOutput (2, OutputMode::stereo);
            mixer->mouseDown (mouseOn (*mixer, sw, {}));
            mixer->mouseUp (mouseOn (*mixer, sw, {}));
            offline.dispatchPendingMessages();
            check ("Cue Out 3/4 on a 4-output device: the switch goes to Cue", sessionmixer::cueOf (offline.getSong()).cueMode
                                                                               && sessionmixer::cueActive (offline.getSong(), 4));
            const auto solo = mixer->partBounds (0, P::solo).getCentre();
            const auto soloBefore = offline.getSong().mixer[2].solo;
            mixer->mouseDown (mouseOn (*mixer, solo, {}));
            mixer->mouseUp (mouseOn (*mixer, solo, {}));
            offline.dispatchPendingMessages();
            check ("in Cue mode the track's Solo switch cues it (no solo)", offline.getSong().mixer[2].cue && offline.getSong().mixer[2].solo == soloBefore);
            line ("track 1 strip insert " + juce::String (2) + ", tip: " + mixer->tipForSnapshot ({ 0, P::solo, 0 }));
            mixer->repaint();
            write (*mixer, "08-mixer-cue-mode");
            savePng (mixer->createComponentSnapshot (main.getIntersection (mixer->getLocalBounds()), true, 3.0f), prefix + "-08b-main-strip-cue@3x.png");
            const auto trackStrip = juce::Rectangle<int> (mixer->partBounds (0, P::solo).getX() - 40, mixer->partBounds (0, P::solo).getY() - 120, 110, 160);
            savePng (mixer->createComponentSnapshot (trackStrip.getIntersection (mixer->getLocalBounds()), true, 3.0f), prefix + "-08c-track-cue@3x.png");
            saveMenus (juce::StringArray { tr ("Cue Out") }, std::vector<juce::PopupMenu> { mixer->deviceOutputMenu (true) }, prefix + "-09-cue-out-menu.png");
            // the Cue Volume: a drag is one undo step; the project keeps every Cue setting
            offline.setCueVolume (0.6f, true);
            const auto json = ProjectFile::toJson (offline.getSong());
            juce::String error;
            const auto back = ProjectFile::fromJson (json, error);
            check ("the project keeps the Cue settings and the Cue switch", back.has_value() && back->clipGrid && back->clipGrid->cue == offline.getSong().clipGrid->cue
                                                                            && back->mixer.size() > 2 && back->mixer[2].cue);
            check ("a project saved before keeps Live's defaults", ClipGrid {}.cue == SessionCue {});
            // Cue Out moved back onto the Main Out: Cue mode stops being in effect (the switch shows Solo, the S buttons return)
            offline.setCueOutput (outputDefault, OutputMode::stereo);
            check ("Cue Out back on Main Out: no Cue mode in effect", ! sessionmixer::cueActive (offline.getSong(), 4));
            offline.setOutputChannelCountForCheck (-1);
        }
        juce::File (prefix + "-checks.txt").replaceWithText (checks + (allOk ? "result=OK\n" : "result=FAIL\n"));
        return allOk;
    }

    // Hotfix 0.1.31.1 (round 190qp, UI items). LYNX --hotfixui190qp <prefix> [--plugin <file>]...: <prefix>-checks.txt
    // (result=OK). Included in the app class of Main.cpp (savePng). Off screen except the plugin windows of part B.
    // (A) Clip-launch layout (kainproctor, Discord): a track chosen in the Arrangement — its header clicked, a spot of its
    //     lane clicked — is the selected track of the clip grid too, and the Device View shows its devices (Live 12: one
    //     selected track for Session and Arrangement; the Device View shows the selected track's chain).
    bool runHotfixUiAPart (juce::StringArray& checks)
    {
        using namespace lynx;
        auto ok = true;
        const auto check = [&checks, &ok] (const juce::String& what, bool pass) {
            checks.add ((pass ? "ok   " : "FAIL ") + what);
            ok = ok && pass;
        };
        const auto mouseOn = [] (juce::Component& target, juce::Point<int> p, juce::ModifierKeys mods) {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, p.toFloat(), now, 1, false);
        };
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        AudioEngine offline (false);
        MainComponent ui (offline, false);
        ui.setSize (1600, 1000);
        ui.setVisible (true);
        DetailView::shownSetting = DetailView::devicesPage = true;
        ui.setClipLaunchWorkflow (true);
        ui.showPanelsForSnapshot ({ workspace::Panel::playlist });
        ui.setClipGridView (PlaylistArea::GridView::song);
        offline.editSong ([] (Song& s) {
            s.channels.resize (2);
            for (int c = 0; c < 2; ++c)
            {
                s.channels[(std::size_t) c].name = c == 0 ? "Keys" : "Bass";
                s.channels[(std::size_t) c].instrument = InstrumentKind::synth;
                s.channels[(std::size_t) c].mixerTrack = 2 + c;
            }
            if (s.mixer.size() < 5)
                s.mixer.resize (5);
            s.mixer[2].name = "Keys";
            s.mixer[3].name = "Bass";
            s.playlistTracks.resize (std::max<std::size_t> (s.playlistTracks.size(), 4));
            for (int t = 0; t < 2; ++t)
            {
                auto& pt = s.playlistTracks[(std::size_t) t];
                pt.name = t == 0 ? "Keys" : "Bass";
                pt.mode = PlaylistTrackMode::instrument;
                pt.channel = t;
            }
        }, false);
        offline.dispatchPendingMessages();
        ui.resized();
        auto* detail = ui.getDetailViewForSnapshot();
        auto* grid = ui.getClipGridForSnapshot();
        check ("the Clip-launch layout has its Device View and clip grid", detail != nullptr && grid != nullptr);
        if (detail == nullptr || grid == nullptr)
            return false;
        auto& playlist = ui.getPlaylistForSnapshot();
        check ("the Clip-launch Arrangement (Live's look)", playlist.hasLiveStyle());
        // grid on track 0 first
        selectClipGridSlotForSnapshot (*grid, 0, 0);
        detail->showSelection (0, 0);
        check ("start: the Device View shows track 1's insert (2)", detail->chainInsertForCheck() == 2);

        // 1. the header of track 2 clicked (no drag): the Device View shows insert 3, the grid selects track 2
        {
            const auto row = playlist.trackRowForSnapshot (1);
            const juce::Point<int> p { juce::jmax (8, playlist.headerWidthForSnapshot() / 3), row.getCentreY() };
            playlist.mouseDown (mouseOn (playlist, p, juce::ModifierKeys::leftButtonModifier));
            playlist.mouseUp (mouseOn (playlist, p, juce::ModifierKeys()));
            offline.dispatchPendingMessages();
            checks.add ("     header click at " + p.toString() + ": insert shown " + juce::String (detail->chainInsertForCheck()) + ", grid track "
                        + juce::String (clipGridSelection (*grid).first));
            check ("Arrangement header click: the Device View shows that track's devices (insert 3)", detail->chainInsertForCheck() == 3);
            check ("Arrangement header click: the clip grid's selected track follows", clipGridSelection (*grid).first == 1);
        }
        // 2. a spot of track 1's lane clicked: back to insert 2
        {
            const auto row = playlist.trackRowForSnapshot (0);
            const juce::Point<int> p { playlist.headerWidthForSnapshot() + 300, row.getCentreY() };
            playlist.mouseDown (mouseOn (playlist, p, juce::ModifierKeys::leftButtonModifier));
            playlist.mouseUp (mouseOn (playlist, p, juce::ModifierKeys()));
            offline.dispatchPendingMessages();
            check ("Arrangement lane click: the Device View shows that track's devices (insert 2)", detail->chainInsertForCheck() == 2);
            check ("Arrangement lane click: the clip grid's selected track follows", clipGridSelection (*grid).first == 0);
        }
        // 4. item 4b (kainproctor): the Device View's effect card follows a change made in the plugin's window and the
        //    automation while the song plays
        offline.editSong ([] (Song& s) {
            auto fx = makeEffectSlot (EffectKind::filter);
            fx.params[0] = 0.2f;
            s.mixer[2].effects = { fx };
        });
        offline.dispatchPendingMessages();
        detail->showSelection (0, 0);
        offline.dispatchPendingMessages();
        check ("Device View: the effect card shows the slot's value (0.2)", std::abs (detail->knobValueForCheck (1, 0) - 0.2) < 1.0e-4);
        offline.editSong ([] (Song& s) { s.mixer[2].effects[0].params[0] = 0.7f; }); // (as the plugin window's knob does)
        offline.dispatchPendingMessages();
        check ("Device View: a change in the plugin's window shows on the card (0.7)", std::abs (detail->knobValueForCheck (1, 0) - 0.7) < 1.0e-4);
        offline.editSong ([] (Song& s) {
            Automation a;
            a.name = "Filter cutoff";
            a.target = { AutomationTargetKind::effectParam, 2, 0, 0 };
            a.points = { { 0, 0.1f, 0.0f }, { 4 * 4 * ppq, 0.9f, 0.0f } };
            s.automations.push_back (a);
            Clip c;
            c.kind = ClipKind::automation;
            c.automation = (int) s.automations.size() - 1;
            c.track = 3;
            c.start = 0;
            c.length = 4 * 4 * ppq;
            s.playlist.push_back (c);
            s.mode = PlayMode::song;
        });
        offline.dispatchPendingMessages();
        offline.seek (2.0 * 4 * ppq);
        offline.play();
        juce::Thread::sleep (30); // (the display refreshes at most every few ms)
        const auto shownNow = offline.getAutomationDisplay().shown ({ AutomationTargetKind::effectParam, 2, 0, 0 }, -1.0f);
        const auto onCard = detail->knobValueForCheck (1, 0);
        checks.add ("     playing at bar 3: automation shows " + juce::String (shownNow, 3) + ", the card " + juce::String (onCard, 3));
        check ("Device View: while the song plays, the card shows the automation's value (as the plugin window)",
               shownNow > 0.0f && std::abs (onCard - (double) shownNow) < 1.0e-4 && std::abs (onCard - 0.7) > 0.05);
        offline.stop();
        offline.dispatchPendingMessages();
        juce::Thread::sleep (30);
        offline.getAutomationDisplay();
        check ("Device View: stopped, the card shows the Song's value again", std::abs (detail->knobValueForCheck (1, 0) - 0.7) < 1.0e-4);

        // 3. the Pattern layout (FL's Playlist, no Device View) is untouched: a header click there leaves the grid as it is
        const auto gridBefore = clipGridSelection (*grid).first;
        ui.setClipLaunchWorkflow (false);
        offline.dispatchPendingMessages();
        playlist.chooseArrangementTrack (1);
        offline.dispatchPendingMessages();
        check ("Pattern layout: choosing a track there changes nothing of the Clip-launch selection", clipGridSelection (*grid).first == gridBefore);
        return ok;
    }

    // (B) "Some plugin interfaces refuse to open after a few clicks" (sidthenugget, Discord, 0.1.31): the real main window
    //     on the desktop (docked editors, as users have them; then detached), a native channel window, a native effect
    //     window, a hosted instrument and a hosted effect (--plugin files), each opened and closed again and again the way
    //     users do it (the channel button / slot click, the window's close button, a tab switch and the header gear in
    //     between). Every open must show a window; every close must take it away.
    struct HotfixUi190qpDriver final : juce::Timer
    {
        std::function<bool()> step;
        void timerCallback() override
        {
            if (! step())
                stopTimer();
        }
    };
    std::unique_ptr<HotfixUi190qpDriver> hotfixUi190qp;

    // the shown window whose name contains `token` (on the desktop, or docked inside the main window). A function of its
    // own: the timer's steps call it after runHotfixUi190qp has returned (a recursive std::function captured by
    // reference there dangled: the route crashed on Windows)
    static juce::Component* hotfixUiFindIn (juce::Component& c, const juce::String& token)
    {
        if (c.getName().contains (token) && c.isShowing() && c.getWidth() > 20 && c.getHeight() > 20)
            return &c;
        for (auto* child : c.getChildren())
            if (auto* found = hotfixUiFindIn (*child, token))
                return found;
        return nullptr;
    }

    void runHotfixUi190qp (const juce::String& prefix, const juce::StringArray& pluginFiles)
    {
        using namespace lynx;
        struct State
        {
            juce::StringArray checks;
            bool ok = true;
            std::unique_ptr<AudioEngine> engine;
            std::unique_ptr<juce::DocumentWindow> window;
            MainComponent* ui = nullptr;
            std::vector<std::function<void()>> steps;
            std::size_t next = 0;
            juce::uint32 wakeAt = 0;
            int failures = 0;
            void check (const juce::String& what, bool pass)
            {
                checks.add ((pass ? "ok   " : "FAIL ") + what);
                ok = ok && pass;
                failures += pass ? 0 : 1;
            }
            void pause (int ms) { wakeAt = juce::Time::getMillisecondCounter() + (juce::uint32) ms; }
        };
        auto st = std::make_shared<State>();
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        windowplaces::fromVar ({});

        // (A) first, off screen
        st->ok = runHotfixUiAPart (st->checks) && st->ok;

        // the shown window whose name contains `token` (on the desktop, or docked inside the main window)
        auto shownWindow = [] (const juce::String& token) -> juce::Component* {
            auto& d = juce::Desktop::getInstance();
            for (int i = d.getNumComponents(); --i >= 0;)
                if (auto* c = d.getComponent (i); c != nullptr && c->isShowing())
                    if (auto* found = hotfixUiFindIn (*c, token))
                        return found;
            return nullptr;
        };
        // where the window of `token` is: a docked one (a native child window of the main window) in the editor dock's
        // coordinates, a window of its own on the screen; and how much of it the user can see (docked: inside the dock)
        auto windowOf = [shownWindow] (const juce::String& token) -> juce::Component* {
            auto* w = shownWindow (token);
            while (w != nullptr && w->getParentComponent() != nullptr && w->getParentComponent()->getName().contains (token))
                w = w->getParentComponent();
            return w;
        };
        auto placeOf = [st, windowOf] (const juce::String& token, float& seen) -> juce::Rectangle<int> {
            seen = 0.0f;
            auto* w = windowOf (token);
            if (w == nullptr || st->ui == nullptr)
                return {};
            auto& dock = st->ui->workspaceForTest();
            juce::Rectangle<int> r;
            if (nativechild::isAttached (*w))
            {
                r = nativechild::boundsIn (*w, dock);
                seen = (float) r.getIntersection (dock.getLocalBounds()).getWidth() * (float) r.getIntersection (dock.getLocalBounds()).getHeight();
            }
            else if (w->isOnDesktop())
            {
                r = w->getScreenBounds();
                seen = (float) r.getWidth() * (float) r.getHeight();
            }
            else
            {
                r = dock.getLocalArea (w, w->getLocalBounds());
                seen = (float) r.getIntersection (dock.getLocalBounds()).getWidth() * (float) r.getIntersection (dock.getLocalBounds()).getHeight();
            }
            seen /= juce::jmax (1.0f, (float) r.getWidth() * (float) r.getHeight());
            return r;
        };
        auto names = [] {
            juce::StringArray out;
            std::function<void (juce::Component&, int)> walk = [&out, &walk] (juce::Component& c, int depth) {
                if (c.getName().isNotEmpty() && c.isVisible() && depth < 6)
                    out.add (c.getName() + (c.isShowing() ? "" : " (hidden)") + " " + c.getBounds().toString());
                if (depth < 6)
                    for (auto* child : c.getChildren())
                        walk (*child, depth + 1);
            };
            auto& d = juce::Desktop::getInstance();
            for (int i = 0; i < d.getNumComponents(); ++i)
                walk (*d.getComponent (i), 0);
            return out.joinIntoString (" | ");
        };

        st->steps.push_back ([st, pluginFiles] {
            st->engine = std::make_unique<AudioEngine> (false);
            st->window = std::make_unique<juce::DocumentWindow> ("LYNX hotfix 190qp", juce::Colours::darkgrey, 0);
            st->ui = new MainComponent (*st->engine, false);
            st->window->setUsingNativeTitleBar (true);
            st->window->setContentOwned (st->ui, false);
            st->window->setBounds (20, 20, 1700, 1000);
            st->window->setVisible (true);
            setMainWindowForOwnership (st->window.get());
            st->checks.add ("     docking=" + juce::String ((int) nativechild::dockingEnabled()));
            std::vector<PluginRef> refs (2);
            auto& host = st->engine->getPluginHost();
            for (const auto& file : pluginFiles)
            {
                host.addPluginFile (juce::File (file));
                for (const auto& t : host.getKnownPlugins().getTypes())
                    if (juce::File (t.fileOrIdentifier).getFileName() == juce::File (file).getFileName())
                    {
                        auto& r = refs[t.isInstrument ? 0 : 1];
                        if (r.id.empty())
                            r.id = t.createIdentifierString().toStdString(), r.name = t.name.toStdString();
                    }
            }
            st->checks.add ("     hosted instrument: " + juce::String (refs[0].name) + ", hosted effect: " + juce::String (refs[1].name));
            st->engine->editSong ([refs] (Song& s) {
                while (s.mixer.size() < 5)
                    s.mixer.emplace_back();
                s.mixer[2].name = "Ins191";
                s.mixer[3].name = "Rev191";
                s.mixer[2].effects = { makeEffectSlot (EffectKind::fruityCompressor) };
                s.channels.clear();
                ChannelDef synth;
                synth.name = "Zed191";
                synth.instrument = InstrumentKind::synth;
                synth.mixerTrack = 1;
                s.channels.push_back (synth);
                if (! refs[0].id.empty())
                {
                    ChannelDef p;
                    p.name = "Nek191";
                    p.instrument = InstrumentKind::plugin;
                    p.plugin = refs[0];
                    p.mixerTrack = 4;
                    s.channels.push_back (p);
                }
                if (! refs[1].id.empty())
                {
                    auto slot = makeEffectSlot (EffectKind::plugin);
                    slot.plugin = refs[1];
                    s.mixer[3].effects = { slot };
                }
            });
            st->ui->showPanelsForSnapshot ({ workspace::Panel::channelRack, workspace::Panel::mixer, workspace::Panel::playlist });
            st->engine->renderOffline (0.05, 48000.0);
            st->pause (2000);
        });

        // one target opened and closed `rounds` times; `open` / `close` act as the user does, `token` names its window
        struct Target
        {
            juce::String what, token;
            std::function<void()> open, close, extra;
            std::function<bool()> available;
        };
        auto addRounds = [st, shownWindow, names, placeOf] (std::shared_ptr<Target> t, const juce::String& mode, int rounds) {
            auto first = std::make_shared<juce::Point<int>>();
            for (int r = 0; r < rounds; ++r)
            {
                st->steps.push_back ([st, t] {
                    if (t->available())
                        t->open();
                    st->pause (450);
                });
                st->steps.push_back ([st, t, shownWindow, names, placeOf, first, r, mode] {
                    if (! t->available())
                        return;
                    auto* w = shownWindow (t->token);
                    st->check (mode + " " + t->what + ": open #" + juce::String (r + 1) + " shows its window", w != nullptr);
                    if (w == nullptr)
                        st->checks.add ("     shown: " + names());
                    float seen = 0.0f;
                    const auto at = placeOf (t->token, seen);
                    if (r == 0)
                        *first = at.getPosition();
                    st->checks.add ("     at " + at.toString() + ", seen " + juce::String (seen * 100.0f, 0) + " %");
                    // closed without being moved, it opens where it was, in full view (0.1.31: a docked window came back
                    // shifted by the main window's place on the screen each time, until it left the window)
                    st->check (mode + " " + t->what + ": open #" + juce::String (r + 1) + " where it was and in view",
                               w != nullptr && at.getPosition().getDistanceFrom (*first) <= 2 && seen > 0.99f);
                    if (w != nullptr && r % 2 == 1 && t->extra)
                        t->extra(); // (a tab switch and the header gear, as a user does between opening and closing)
                    st->pause (250);
                });
                st->steps.push_back ([st, t, r] {
                    if (! t->available())
                        return;
                    t->close();
                    juce::ignoreUnused (r);
                    st->pause (450);
                });
                st->steps.push_back ([st, t, shownWindow, r, mode] {
                    if (! t->available())
                        return;
                    st->check (mode + " " + t->what + ": close #" + juce::String (r + 1) + " takes the window away", shownWindow (t->token) == nullptr);
                });
            }
        };

        auto channelOf = [st] (const char* name) {
            const auto& ch = st->engine->getSong().channels;
            for (int c = 0; c < (int) ch.size(); ++c)
                if (ch[(std::size_t) c].name == name)
                    return c;
            return -1;
        };
        // the window's own close button: a DocumentWindow's ✕, or a docked frame's
        auto closeButtonOf = [shownWindow] (const juce::String& token) {
            auto* w = shownWindow (token);
            for (auto* c = w; c != nullptr; c = c->getParentComponent())
                if (auto* dw = dynamic_cast<juce::DocumentWindow*> (c))
                {
                    dw->closeButtonPressed();
                    return true;
                }
            if (w != nullptr)
                for (auto* child : w->getChildren())
                    if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->getName() == "close")
                    {
                        b->triggerClick();
                        return true;
                    }
            return false;
        };
        auto pressGear = [shownWindow] (const juce::String& token) {
            if (auto* w = shownWindow (token))
                pressChannelHeaderGear (*w);
        };

        for (const auto* mode : { "docked", "detached" })
        {
            const juce::String m (mode);
            st->steps.push_back ([st, m] {
                setEditorsDetached (m == "detached");
                st->pause (200);
            });
            auto synth = std::make_shared<Target>();
            synth->what = "native channel window (Synth)";
            synth->token = "Zed191";
            synth->available = [] { return true; };
            synth->open = [st, channelOf] { st->ui->getRackForSnapshot().toggleEditor (channelOf ("Zed191"), false); };
            int closes = 0;
            synth->close = [st, channelOf, closeButtonOf, closes]() mutable {
                if (++closes % 2 == 0 && closeButtonOf ("Zed191"))
                    return;
                st->ui->getRackForSnapshot().toggleEditor (channelOf ("Zed191"), false);
            };
            synth->extra = [shownWindow, pressGear] {
                if (auto* w = shownWindow ("Zed191"))
                {
                    showChannelWindowTab (*w, -1);
                    showChannelWindowTab (*w, 0);
                }
                pressGear ("Zed191");
                pressGear ("Zed191");
            };
            addRounds (synth, m, 6);

            auto fx = std::make_shared<Target>();
            fx->what = "native effect window (Fruity Compressor, insert 2)";
            fx->token = "Ins191";
            fx->available = [] { return true; };
            fx->open = [st] { st->ui->getMixerForSnapshot().openSlotEditor (2, 0); };
            fx->close = [closeButtonOf] { closeButtonOf ("Ins191"); };
            addRounds (fx, m, 6);

            auto inst = std::make_shared<Target>();
            inst->what = "hosted instrument window (channel)";
            inst->token = "Nek191";
            inst->available = [st, channelOf] { return channelOf ("Nek191") >= 0 && st->engine->getChannelInstrument (channelOf ("Nek191")) != nullptr; };
            inst->open = [st, channelOf] { st->ui->getRackForSnapshot().toggleEditor (channelOf ("Nek191"), false); };
            int instCloses = 0;
            inst->close = [st, channelOf, closeButtonOf, instCloses]() mutable {
                if (++instCloses % 2 == 0 && closeButtonOf ("Nek191"))
                    return;
                st->ui->getRackForSnapshot().toggleEditor (channelOf ("Nek191"), false);
            };
            inst->extra = [st, channelOf, pressGear] {
                if (auto* p = dynamic_cast<PluginInstrument*> (st->engine->getChannelInstrument (channelOf ("Nek191")).get()))
                {
                    PluginWindows::showWindowPage (p->getInstance(), 1);
                    PluginWindows::showWindowPage (p->getInstance(), 0);
                }
                pressGear ("Nek191");
                pressGear ("Nek191");
            };
            addRounds (inst, m, 6);

            auto hfx = std::make_shared<Target>();
            hfx->what = "hosted effect window (insert 3)";
            hfx->token = "MVerb"; // (a hosted plugin's window has the plugin's name)
            hfx->available = [st] { return dynamic_cast<PluginEffect*> (st->engine->getEffect (3, 0).get()) != nullptr; };
            hfx->open = [st] { st->ui->getMixerForSnapshot().openSlotEditor (3, 0); };
            hfx->close = [closeButtonOf] { closeButtonOf ("MVerb"); };
            hfx->extra = [st] {
                if (auto* p = dynamic_cast<PluginEffect*> (st->engine->getEffect (3, 0).get()))
                {
                    PluginWindows::showWindowPage (p->getInstance(), 1);
                    PluginWindows::showWindowPage (p->getInstance(), 0);
                }
            };
            addRounds (hfx, m, 6);
        }

        auto finish = [this, st, prefix] {
            PluginWindows::closeAll();
            closeChannelSettings();
            setEditorsDetached (false);
            windowplaces::fromVar ({});
            setMainWindowForOwnership (nullptr);
            st->window = nullptr;
            st->engine = nullptr;
            juce::File (prefix + "-checks.txt").replaceWithText (st->checks.joinIntoString ("\n") + "\nfailures=" + juce::String (st->failures)
                                                                 + "\nresult=" + (st->ok ? "OK" : "FAIL") + "\n");
            setApplicationReturnValue (st->ok ? 0 : 1);
            quit();
        };
        hotfixUi190qp = std::make_unique<HotfixUi190qpDriver>();
        hotfixUi190qp->step = [st, finish] {
            if (juce::Time::getMillisecondCounter() < st->wakeAt)
                return true;
            if (st->next >= st->steps.size())
            {
                finish();
                return false;
            }
            st->steps[st->next++]();
            return true;
        };
        hotfixUi190qp->startTimer (50);
    }

    // --selftest hotfixplaces190qp: a docked window's place is kept in the dock's coordinates with its "docked" mark and
    // read back as such; a place of 0.1.31 (six entries, a screen place) reads as a window of its own's
    static juce::String hotfixPlaces190qpCheck()
    {
        using namespace lynx;
        const auto keep = windowplaces::toVar();
        juce::String result = "OK";
        windowplaces::fromVar (juce::Array<juce::var> { juce::Array<juce::var> { "fx:3:0", 120, 80, 640, 420, true, "docked" },
                                                        juce::Array<juce::var> { "ch:77", 300, 200, 500, 400, false } });
        const auto& all = windowplaces::all();
        const auto docked = all.find ("fx:3:0"), own = all.find ("ch:77");
        if (docked == all.end() || ! docked->second.docked || docked->second.bounds != juce::Rectangle<int> (120, 80, 640, 420) || ! docked->second.open)
            result = "ERROR the docked place did not read back";
        else if (own == all.end() || own->second.docked || own->second.bounds != juce::Rectangle<int> (300, 200, 500, 400))
            result = "ERROR a six-entry place did not read as a window of its own's";
        else
        {
            const auto again = windowplaces::toVar();
            const auto* list = again.getArray();
            int sevens = 0, sixes = 0;
            if (list != nullptr)
                for (const auto& e : *list)
                    sevens += e.size() == 7 && e[6].toString() == "docked" ? 1 : 0, sixes += e.size() == 6 ? 1 : 0;
            if (sevens != 1 || sixes != 1)
                result = "ERROR written back: " + juce::String (sevens) + " docked / " + juce::String (sixes) + " own places";
        }
        windowplaces::fromVar (keep);
        return result;
    }

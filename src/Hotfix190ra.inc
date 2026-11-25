    // Hotfix 0.1.31.2 (round 190ra). Included in the app class of Main.cpp.
    // LYNX --hotfix190ra <prefix> [--part name]...: <prefix>-checks.txt (result=OK). A shown main window (its own native
    // window, as users have it: on Windows the start page is then a native overlay window of its own), driven through the
    // window's peer the way the system delivers input (ComponentPeer::handleMouseWheel / handleMouseEvent), so every
    // global mouse listener and every paint runs as for a user. Parts (all by default):
    //   startpage  10.net (Discord, 0.1.31.1, Windows 11): the start page crashed when its left list ("Start a new
    //              project") was scrolled with the wheel or its scroll bar. Every list of the page (the templates, the
    //              recent projects) is scrolled to the end and back with the wheel and with its scroll bar, as shown by
    //              File › Start page... and with many rows, and painted after each step.
    struct Hotfix190raDriver final : juce::Timer
    {
        std::function<bool()> step;
        void timerCallback() override
        {
            if (! step())
                stopTimer();
        }
    };
    std::unique_ptr<Hotfix190raDriver> hotfix190ra;

    struct Hotfix190raState
    {
        juce::StringArray checks;
        bool ok = true;
        int failures = 0;
        std::unique_ptr<lynx::AudioEngine> engine;
        std::unique_ptr<juce::DocumentWindow> window;
        lynx::MainComponent* ui = nullptr;
        std::vector<std::function<void()>> steps;
        std::size_t next = 0;
        juce::uint32 wakeAt = 0;
        void check (const juce::String& what, bool pass)
        {
            checks.add ((pass ? "ok   " : "FAIL ") + what);
            ok = ok && pass;
            failures += pass ? 0 : 1;
        }
        void note (const juce::String& what) { checks.add ("     " + what); }
        void pause (int ms) { wakeAt = juce::Time::getMillisecondCounter() + (juce::uint32) ms; }
    };

    static juce::Component* hotfix190raFindId (juce::Component& c, const juce::String& id)
    {
        if (c.getComponentID() == id)
            return &c;
        for (auto* child : c.getChildren())
            if (auto* found = hotfix190raFindId (*child, id))
                return found;
        return nullptr;
    }
    // the component with this ID in any window of the desktop (the start page may be a native overlay window of its own)
    static juce::Component* hotfix190raFindShown (const juce::String& id)
    {
        auto& d = juce::Desktop::getInstance();
        for (int i = d.getNumComponents(); --i >= 0;)
            if (auto* c = d.getComponent (i))
                if (auto* found = hotfix190raFindId (*c, id); found != nullptr && found->isShowing())
                    return found;
        return nullptr;
    }

    // input through the peer of `target`'s window at a point of `target` (as the system delivers it)
    static bool hotfix190raWheel (juce::Component& target, juce::Point<int> local, float deltaY)
    {
        auto* peer = target.getPeer();
        if (peer == nullptr)
            return false;
        const auto at = peer->getComponent().getLocalPoint (&target, local.toFloat());
        juce::MouseWheelDetails wheel;
        wheel.deltaX = 0.0f;
        wheel.deltaY = deltaY;
        wheel.isReversed = false;
        wheel.isSmooth = false;
        wheel.isInertial = false;
        peer->handleMouseWheel (juce::MouseInputSource::InputSourceType::mouse, at, juce::Time::currentTimeMillis(), wheel);
        return true;
    }
    static bool hotfix190raMouse (juce::Component& target, juce::Point<int> local, bool down)
    {
        auto* peer = target.getPeer();
        if (peer == nullptr)
            return false;
        const auto at = peer->getComponent().getLocalPoint (&target, local.toFloat());
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, at,
                                down ? juce::ModifierKeys::currentModifiers.withFlags (juce::ModifierKeys::leftButtonModifier)
                                     : juce::ModifierKeys::currentModifiers.withoutMouseButtons(),
                                juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, juce::Time::currentTimeMillis());
        return true;
    }

    // (startpage) the steps that scroll one list of the page shown with component ID `pageId`: the wheel down to the end
    // and up again over its middle, then its scroll bar's thumb dragged to the bottom and back, each step painted
    static void hotfix190raScrollList (std::shared_ptr<Hotfix190raState> st, const juce::String& pageId, const juce::String& listId,
                                       const juce::String& what)
    {
        auto viewOf = [pageId, listId] () -> juce::ListBox* {
            auto* page = hotfix190raFindShown (pageId);
            return page != nullptr ? dynamic_cast<juce::ListBox*> (hotfix190raFindId (*page, listId)) : nullptr;
        };
        auto paintPage = [pageId] {
            if (auto* page = hotfix190raFindShown (pageId))
            {
                // the page's own paint (what the window paints when the list moved), and the window's real repaint
                juce::Image image (juce::Image::ARGB, juce::jmax (1, page->getWidth()), juce::jmax (1, page->getHeight()), true);
                juce::Graphics g (image);
                page->paintEntireComponent (g, true);
                if (auto* peer = page->getPeer())
                    peer->performAnyPendingRepaintsNow();
            }
        };
        auto start = std::make_shared<int> (0);
        st->steps.push_back ([st, viewOf, what, start] {
            auto* list = viewOf();
            st->check (what + ": the list is shown", list != nullptr);
            if (list == nullptr)
                return;
            const auto rows = list->getListBoxModel() != nullptr ? list->getListBoxModel()->getNumRows() : 0;
            st->note (what + ": " + juce::String (rows) + " rows, " + juce::String (list->getRowHeight()) + " px each, " + juce::String (list->getHeight())
                      + " px shown, scroll bar " + (list->getVerticalScrollBar().isVisible() ? "shown" : "hidden"));
            *start = list->getViewport()->getViewPositionY();
        });
        // the wheel: down 40 notches, then up 40
        for (int n = 0; n < 80; ++n)
            st->steps.push_back ([st, viewOf, paintPage, n] {
                if (auto* list = viewOf())
                {
                    hotfix190raWheel (*list, list->getLocalBounds().getCentre(), n < 40 ? -0.25f : 0.25f);
                    paintPage();
                }
                st->pause (n % 10 == 0 ? 30 : 0);
            });
        st->steps.push_back ([st, viewOf, what, start] {
            auto* list = viewOf();
            st->check (what + ": the wheel scrolled it and the page is still there", list != nullptr);
            if (list != nullptr)
                st->check (what + ": back at the top after the wheel went down and up", list->getViewport()->getViewPositionY() == *start);
        });
        // the scroll bar: its thumb dragged to the bottom in steps, and back up
        for (int pass = 0; pass < 2; ++pass)
            st->steps.push_back ([st, viewOf, paintPage, what, pass] {
                auto* list = viewOf();
                if (list == nullptr)
                    return;
                auto& bar = list->getVerticalScrollBar();
                if (! bar.isVisible())
                    return;
                const auto track = bar.getLocalBounds();
                const auto thumbY = [&bar] {
                    const auto range = bar.getRangeLimit().getLength() <= 0.0 ? 1.0 : bar.getRangeLimit().getLength();
                    return juce::roundToInt ((bar.getCurrentRangeStart() + bar.getCurrentRangeSize() * 0.5) / range * bar.getHeight());
                };
                auto at = juce::Point<int> (track.getCentreX(), thumbY());
                hotfix190raMouse (bar, at, true);
                for (int s = 1; s <= 24; ++s)
                {
                    at.y = juce::jlimit (0, bar.getHeight() + 40, pass == 0 ? at.y + bar.getHeight() / 16 : at.y - bar.getHeight() / 16);
                    hotfix190raMouse (bar, at, true);
                    paintPage();
                }
                hotfix190raMouse (bar, at, false);
                paintPage();
                st->note (what + ": scroll bar " + (pass == 0 ? "down" : "up") + " -> view at " + juce::String (list->getViewport()->getViewPositionY()));
                st->pause (30);
            });
        // and every position the bar can take, set directly (the bar's own arrow / track clicks end there too)
        st->steps.push_back ([st, viewOf, paintPage, what] {
            auto* list = viewOf();
            if (list == nullptr)
                return;
            auto& bar = list->getVerticalScrollBar();
            const auto limit = bar.getRangeLimit();
            const auto size = bar.getCurrentRangeSize();
            const auto rowH = juce::jmax (1, list->getRowHeight());
            int steps = 0;
            for (auto y = limit.getStart(); y <= limit.getEnd() - size + rowH; y += rowH / 3.0, ++steps)
            {
                bar.setCurrentRangeStart (y);
                paintPage();
            }
            bar.setCurrentRangeStart (limit.getEnd()); // (past the end: clamped)
            paintPage();
            bar.setCurrentRangeStart (0.0);
            paintPage();
            st->check (what + ": every scroll position painted (" + juce::String (steps) + ")", viewOf() != nullptr);
        });
    }

    void runHotfix190ra (const juce::String& prefix, const juce::StringArray& parts)
    {
        using namespace lynx;
        auto st = std::make_shared<Hotfix190raState>();
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const auto wants = [parts] (const char* name) { return parts.isEmpty() || parts.contains (name); };

        st->steps.push_back ([st] {
            st->engine = std::make_unique<AudioEngine> (false);
            st->window = std::make_unique<juce::DocumentWindow> ("LYNX hotfix 190ra", juce::Colours::darkgrey, 0);
            st->ui = new MainComponent (*st->engine, false);
            st->window->setUsingNativeTitleBar (true);
            st->window->setContentOwned (st->ui, false);
            st->window->setBounds (20, 20, 1600, 960);
            st->window->setVisible (true);
            setMainWindowForOwnership (st->window.get());
            st->note ("docking=" + juce::String ((int) nativechild::dockingEnabled()));
            st->ui->closeSetupWizardForSnapshot();
            st->pause (1500);
        });

        if (wants ("startpage"))
        {
            // 1. as File › Start page... shows it (this profile's recent projects and templates)
            st->steps.push_back ([st] {
                st->ui->closeSetupWizardForSnapshot();
                st->ui->showStartPage();
                st->pause (600);
            });
            st->steps.push_back ([st] {
                auto* page = hotfix190raFindShown ("startpage.card");
                st->check ("File › Start page... shows the page", page != nullptr);
                if (page != nullptr)
                    st->note ("page on its own native window: " + juce::String ((int) (page->getPeer() != nullptr && page->getPeer() != st->window->getPeer())));
            });
            hotfix190raScrollList (st, "startpage.card", "startpage.templates", "start page (File menu): Start a new project");
            hotfix190raScrollList (st, "startpage.card", "startpage.recent", "start page (File menu): Recent projects");

            // 2. many rows in both lists (a user with many templates and recent projects): a page of its own over the
            //    main window, scrolled the same way
            st->steps.push_back ([this, st] {
                st->ui->closeSetupWizardForSnapshot();
                startpageui::Host host;
                for (int i = 0; i < 37; ++i)
                {
                    host.templates.push_back ("Template " + juce::String (i + 1));
                    if (i % 4 != 3) // (fewer lines than names: the paint must not read past them)
                        host.templateDetails.push_back ("Your template");
                }
                for (int i = 0; i < 53; ++i)
                {
                    startpage::Recent r;
                    r.name = "Project " + std::to_string (i + 1);
                    r.path = "/nowhere/Project " + std::to_string (i + 1) + ".lynx";
                    r.folder = "/nowhere";
                    r.exists = i % 3 != 0;
                    r.modifiedMs = juce::Time::currentTimeMillis() - (std::int64_t) i * 3600000;
                    r.bytes = 1000 * (i + 1);
                    host.recent.push_back (std::move (r));
                }
                auto overlay = startpageui::makeOverlay (std::move (host));
                overlay->setComponentID ("startpage.overlay.many");
                st->ui->addAndMakeVisible (*overlay);
                overlay->setBounds (st->ui->getLocalBounds());
                manyRowsPage190ra = std::move (overlay);
                st->pause (400);
            });
            hotfix190raScrollList (st, "startpage.overlay.many", "startpage.templates", "start page with 38 rows: Start a new project");
            hotfix190raScrollList (st, "startpage.overlay.many", "startpage.recent", "start page with 54 rows: Recent projects");
            // a search that leaves a few rows, scrolled again (the list shrinks under a scrolled view)
            st->steps.push_back ([st] {
                if (auto* page = hotfix190raFindShown ("startpage.overlay.many"))
                {
                    if (auto* list = dynamic_cast<juce::ListBox*> (hotfix190raFindId (*page, "startpage.recent")))
                        list->getVerticalScrollBar().setCurrentRangeStart (1.0e6);
                    startpageui::typeForSnapshot (*page, "Project 5");
                    st->check ("a search while scrolled to the end leaves the matches (Project 5, 50-53)", startpageui::shownRowsForSnapshot (*page) == 5);
                }
            });
            hotfix190raScrollList (st, "startpage.overlay.many", "startpage.recent", "start page, searched: Recent projects");
            st->steps.push_back ([this, st] {
                if (manyRowsPage190ra != nullptr)
                    st->ui->removeChildComponent (manyRowsPage190ra.get());
                manyRowsPage190ra = nullptr;
            });
        }

        auto finish = [this, st, prefix] {
            manyRowsPage190ra = nullptr;
            setMainWindowForOwnership (nullptr);
            st->window = nullptr;
            st->engine = nullptr;
            juce::File (prefix + "-checks.txt").replaceWithText (st->checks.joinIntoString ("\n") + "\nfailures=" + juce::String (st->failures)
                                                                 + "\nresult=" + (st->ok ? "OK" : "FAIL") + "\n");
            setApplicationReturnValue (st->ok ? 0 : 1);
            quit();
        };
        hotfix190ra = std::make_unique<Hotfix190raDriver>();
        hotfix190ra->step = [st, finish, prefix] {
            if (juce::Time::getMillisecondCounter() < st->wakeAt)
                return true;
            if (st->next >= st->steps.size())
            {
                finish();
                return false;
            }
            // (the step about to run and what was checked so far: a crash leaves them behind)
            juce::File (prefix + "-progress.txt").replaceWithText ("step " + juce::String ((int) st->next) + "\n" + st->checks.joinIntoString ("\n"));
            st->steps[st->next++]();
            return true;
        };
        hotfix190ra->startTimer (20);
    }
    std::unique_ptr<juce::Component> manyRowsPage190ra;

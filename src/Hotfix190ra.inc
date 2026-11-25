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

    static juce::Component* hotfixUiFindIn190ra (const juce::String& token)
    {
        auto& d = juce::Desktop::getInstance();
        for (int i = d.getNumComponents(); --i >= 0;)
            if (auto* c = d.getComponent (i); c != nullptr && c->isShowing())
                if (auto* found = hotfixUiFindIn (*c, token))
                    return found;
        return nullptr;
    }

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
    static bool hotfix190raMouse (juce::Component& target, juce::Point<int> local, bool down, int button = juce::ModifierKeys::leftButtonModifier)
    {
        auto* peer = target.getPeer();
        if (peer == nullptr)
            return false;
        const auto at = peer->getComponent().getLocalPoint (&target, local.toFloat());
        const auto mods = down ? juce::ModifierKeys::currentModifiers.withoutMouseButtons().withFlags (button)
                               : juce::ModifierKeys::currentModifiers.withoutMouseButtons();
        juce::ModifierKeys::currentModifiers = mods; // (as the platform layer keeps it)
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, at, mods, juce::MouseInputSource::defaultPressure,
                                juce::MouseInputSource::defaultOrientation, juce::Time::currentTimeMillis());
        return true;
    }
    // every item text of a menu and its submenus (submenu names too), "  " per level
    static void hotfix190raMenuTexts (const juce::PopupMenu& menu, juce::StringArray& out, int depth = 0)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            const auto& item = it.getItem();
            out.add (juce::String::repeatedString ("  ", depth) + item.text);
            if (item.subMenu != nullptr)
                hotfix190raMenuTexts (*item.subMenu, out, depth + 1);
        }
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
                    bar.handleUpdateNowIfNeeded(); // (a scroll bar tells its viewport asynchronously)
                    paintPage();
                }
                hotfix190raMouse (bar, at, false);
                bar.handleUpdateNowIfNeeded();
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
                bar.handleUpdateNowIfNeeded();
                paintPage();
            }
            bar.setCurrentRangeStart (limit.getEnd()); // (past the end: clamped)
            paintPage();
            bar.setCurrentRangeStart (0.0);
            paintPage();
            st->check (what + ": every scroll position painted (" + juce::String (steps) + ")", viewOf() != nullptr);
        });
    }

    // The root cause of 10.net's crash, measured: a ListBox scrolled to its end and painted at a display scale that is not a
    // whole number (Windows' 125 % / 150 %, LYNX's own zoom) asks its model for rows past getNumRows() — the row just
    // under the last one, a sliver of which rounds into the list's clip. The start page's template list read its name
    // there (host.templates[row - 1]) past the end of the vector. Returns how many such paints a 38-row list (50 px rows,
    // 447 px shown, as the page's) got over every scroll position at each scale; `out` lists them per scale.
    static int rowsPastTheEnd190ra (juce::String& out)
    {
        struct Model final : juce::ListBoxModel
        {
            int rows = 38, past = 0;
            int getNumRows() override { return rows; }
            void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
            {
                past += row >= rows ? 1 : 0;
                g.setColour (juce::Colours::grey);
                g.fillRect (0, 0, w, h);
            }
        };
        int total = 0;
        for (const float scale : { 1.0f, 1.1f, 1.25f, 1.5f, 1.75f, 2.0f })
        {
            Model model;
            juce::ListBox list ("rows", &model);
            list.setRowHeight (50);
            list.setBounds (0, 0, 428, 447);
            list.updateContent();
            // (JUCE's software renderer, as Linux / macOS draw and as LYNX's own CPU rendering draws on Windows)
            juce::Image image (juce::Image::ARGB, juce::roundToInt (440 * scale), juce::roundToInt (460 * scale), true, juce::SoftwareImageType());
            auto& bar = list.getVerticalScrollBar();
            for (double y = 0.0; y <= bar.getRangeLimit().getEnd(); y += 1.0)
            {
                bar.setCurrentRangeStart (y);
                bar.handleUpdateNowIfNeeded();
                juce::Graphics g (image);
                g.addTransform (juce::AffineTransform::scale (scale));
                list.paintEntireComponent (g, true);
            }
            out << juce::String (scale, 2) << ": " << model.past << "  ";
            total += model.past;
            list.setModel (nullptr);
        }
        return total;
    }

    // --selftest startpagescroll190ra (10.net's crash): a start page with 40 templates (8 without a detail line) and 60
    // recent projects, off screen; every list in it scrolled through every position by its scroll bar and back by the
    // wheel, the whole page painted after each step, also with a search that shrinks the recent list while it is scrolled
    // to its end. "OK" or "ERROR ...".
    static juce::String startPageScroll190raCheck()
    {
        using namespace lynx;
        startpageui::Host host;
        for (int i = 0; i < 40; ++i)
        {
            host.templates.push_back ("Template " + juce::String (i + 1));
            if (i < 32)
                host.templateDetails.push_back ("Your template");
        }
        for (int i = 0; i < 60; ++i)
        {
            startpage::Recent r;
            r.name = "Song " + std::to_string (i + 1);
            r.path = "/nowhere/Song " + std::to_string (i + 1) + ".lynx";
            r.folder = "/nowhere";
            r.exists = i % 4 != 0;
            r.modifiedMs = 1764000000000 - (std::int64_t) i * 3600000;
            r.bytes = 4096 * (i + 1);
            host.recent.push_back (std::move (r));
        }
        host.nowMs = 1764000000000;
        auto card = startpageui::makeCard (std::move (host));
        juce::Image image (juce::Image::ARGB, card->getWidth() * 2, card->getHeight() * 2, true);
        int lists = 0, steps = 0;
        // (painted as a display at 100 %, 125 % and 150 % draws it: at a scale that is not a whole number JUCE asks a list
        // for the row under its last one, rowsPastTheEnd190ra; 0.1.31.1's template list read past its names there)
        float scale = 1.0f;
        const auto paintCard = [&] {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (scale));
            card->paintEntireComponent (g, true);
        };
        const auto scrollAll = [&] {
            for (const auto* id : { "startpage.templates", "startpage.recent" })
            {
                auto* list = dynamic_cast<juce::ListBox*> (hotfix190raFindId (*card, id));
                if (list == nullptr)
                    return juce::String ("ERROR no list ") + id;
                ++lists;
                auto& bar = list->getVerticalScrollBar();
                const auto end = bar.getRangeLimit().getEnd();
                for (const auto s : { 1.0f, 1.25f, 1.5f })
                {
                    scale = s;
                    for (double y = 0.0; y <= end + list->getRowHeight(); y += 3.0, ++steps)
                    {
                        bar.setCurrentRangeStart (y);
                        bar.handleUpdateNowIfNeeded();
                        paintCard();
                    }
                }
                scale = 1.25f;
                const auto mouse = juce::Desktop::getInstance().getMainMouseSource();
                const auto now = juce::Time::getCurrentTime();
                const auto at = list->getLocalBounds().getCentre().toFloat();
                juce::MouseWheelDetails wheel { 0.0f, 0.5f, false, false, false };
                for (int n = 0; n < 200; ++n, ++steps)
                {
                    list->mouseWheelMove (juce::MouseEvent (mouse, at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, list, list, now, at, now, 0, false), wheel);
                    bar.handleUpdateNowIfNeeded();
                    paintCard();
                }
                if (list->getViewport()->getViewPositionY() != 0)
                    return juce::String ("ERROR the wheel left ") + id + " at " + juce::String (list->getViewport()->getViewPositionY());
            }
            return juce::String();
        };
        if (const auto error = scrollAll(); error.isNotEmpty())
            return error;
        if (auto* recent = dynamic_cast<juce::ListBox*> (hotfix190raFindId (*card, "startpage.recent")))
            recent->getVerticalScrollBar().setCurrentRangeStart (1.0e9);
        startpageui::typeForSnapshot (*card, "Song 1");
        if (const auto error = scrollAll(); error.isNotEmpty())
            return error;
        juce::String past;
        const auto pastPaints = rowsPastTheEnd190ra (past);
        return "OK (" + juce::String (lists) + " lists, " + juce::String (steps) + " painted steps; JUCE asked a plain list for "
               + juce::String (pastPaints) + " rows past its end by scale: " + past.trim() + ")";
    }

    void runHotfix190ra (const juce::String& prefix, const juce::StringArray& parts, const juce::StringArray& pluginFiles = {})
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
            // HOTFIX190RA_SCALE=<percent>: LYNX's interface zoom for the run (a display at 125 % / 150 % as 10.net's)
            if (const auto zoom = juce::SystemStats::getEnvironmentVariable ("HOTFIX190RA_SCALE", {}).getIntValue(); zoom >= 50 && zoom <= 300)
            {
                juce::Desktop::getInstance().setGlobalScaleFactor ((float) zoom / 100.0f);
                st->note ("interface zoom " + juce::String (zoom) + " %");
            }
            st->ui->closeSetupWizardForSnapshot();
            st->pause (1500);
        });

        if (wants ("pastend"))
        {
            st->steps.push_back ([st] {
                juce::String past;
                const auto n = rowsPastTheEnd190ra (past);
                st->note ("rows past the end a plain list was asked to paint off screen (software renderer), by scale: " + past + "(total " + juce::String (n) + ")");
                st->check ("the start page with many rows, scrolled and painted at every position", startPageScroll190raCheck().startsWith ("OK"));
            });
            // the same list on the shown window (Windows: Direct2D unless LYNX's CPU rendering is chosen), at LYNX's zoom
            // 100 / 125 / 150 % (the window's scale as a display at that scale gives it)
            struct PastModel final : juce::ListBoxModel
            {
                int past = 0;
                int getNumRows() override { return 38; }
                void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
                {
                    past += row >= 38 ? 1 : 0;
                    g.setColour (row % 2 == 0 ? juce::Colours::grey : juce::Colours::darkgrey);
                    g.fillRect (0, 0, w, h);
                }
            };
            auto model = std::make_shared<PastModel>();
            auto list = std::make_shared<std::unique_ptr<juce::ListBox>>();
            for (const float scale : { 1.0f, 1.25f, 1.5f })
                st->steps.push_back ([st, model, list, scale] {
                    juce::Desktop::getInstance().setGlobalScaleFactor (scale);
                    if (*list == nullptr)
                    {
                        *list = std::make_unique<juce::ListBox> ("past190ra", model.get());
                        (*list)->setRowHeight (50);
                        st->ui->addAndMakeVisible (**list);
                        (*list)->setBounds (40, 120, 428, 447);
                        (*list)->toFront (false);
                        (*list)->updateContent();
                    }
                    model->past = 0;
                    auto& bar = (*list)->getVerticalScrollBar();
                    for (double y = 0.0; y <= bar.getRangeLimit().getEnd(); y += 1.0)
                    {
                        bar.setCurrentRangeStart (y);
                        bar.handleUpdateNowIfNeeded();
                        (*list)->repaint();
                        if (auto* peer = (*list)->getPeer())
                            peer->performAnyPendingRepaintsNow();
                    }
                    st->note ("on the shown window at " + juce::String (scale, 2) + ": rows past the end painted " + juce::String (model->past));
                });
            st->steps.push_back ([st, list] {
                juce::Desktop::getInstance().setGlobalScaleFactor (1.0f);
                if (*list != nullptr)
                {
                    (*list)->setModel (nullptr);
                    st->ui->removeChildComponent (list->get());
                    list->reset();
                }
            });
        }

        if (wants ("startpage"))
        {
            // 1. as File › Start page... shows it (this profile's recent projects and templates; 30 templates of the user's
            //    own, so that its left list scrolls as 10.net's did: the start page then lists them, unread)
            st->steps.push_back ([st] {
                const auto folder = userTemplatesFolder();
                if (folder.createDirectory())
                    for (int i = 1; i <= 30; ++i)
                        if (const auto f = folder.getChildFile ("Template190ra " + juce::String (i).paddedLeft ('0', 2) + ProjectFile::extension); ! f.existsAsFile())
                            f.create();
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
                    st->check ("a search while scrolled to the end leaves the matches (\"Project\" and \"5\": 9 projects)", startpageui::shownRowsForSnapshot (*page) == 9);
                }
            });
            hotfix190raScrollList (st, "startpage.overlay.many", "startpage.recent", "start page, searched: Recent projects");
            st->steps.push_back ([this, st] {
                if (manyRowsPage190ra != nullptr)
                    st->ui->removeChildComponent (manyRowsPage190ra.get());
                manyRowsPage190ra = nullptr;
            });
        }

        if (wants ("typingmenu"))
        {
            // nivram77661 (#270, 0.1.31.1): right-clicking the typing keyboard showed no Root note / Octave / Layout menu.
            // The toolbar button's right-click at a wide and a narrow window (1280-1440 px: the record panel is compact and
            // the button goes, round 170e3), and the menus that keep the typing piano there (Options, View)
            for (const int width : { 1900, 1366 })
            {
                st->steps.push_back ([st, width] {
                    st->window->setSize (width, 900);
                    st->ui->resized();
                    st->pause (300);
                });
                st->steps.push_back ([st, width] {
                    auto& button = st->ui->typingPianoButtonForTest();
                    st->note ("window " + juce::String (width) + " px: the typing keyboard button is " + (button.isShowing() ? "shown" : "hidden"));
                    if (! button.isShowing())
                        return;
                    const auto centre = button.getLocalBounds().getCentre();
                    hotfix190raMouse (button, centre, true, juce::ModifierKeys::rightButtonModifier);
                    hotfix190raMouse (button, centre, false);
                    st->pause (400);
                });
                st->steps.push_back ([st, width] {
                    auto& button = st->ui->typingPianoButtonForTest();
                    if (! button.isShowing())
                        return;
                    const auto open = juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0;
                    // (a menu closes at once while another program is in front: only checked when LYNX is)
                    if (juce::Process::isForegroundProcess())
                        st->check ("window " + juce::String (width) + " px: a right-click on the typing keyboard button opens its menu", open);
                    else
                        st->note ("window " + juce::String (width) + " px: LYNX is not the foreground program, the menu's opening is not checked (open "
                                  + juce::String ((int) open) + ")");
                    juce::PopupMenu::dismissAllActiveMenus();
                    st->pause (300);
                });
            }
            st->steps.push_back ([st] {
                const auto names = st->ui->getMenuNamesForSnapshot();
                for (int i = 0; i < names.size(); ++i)
                {
                    juce::StringArray texts;
                    hotfix190raMenuTexts (st->ui->getMenuForSnapshot (i), texts);
                    const auto toggle = texts.indexOf ("Typing keyboard to piano", true) >= 0
                                        || texts.joinIntoString ("|").contains ("Typing keyboard to piano");
                    if (! toggle)
                        continue;
                    const auto all = texts.joinIntoString ("|");
                    const auto settings = all.contains ("Root note") && all.contains ("Octave") && all.contains ("Layout") && all.contains ("Minor Natural");
                    st->check ("the " + names[i] + " menu: next to \"Typing keyboard to piano\", its Root note / Octave / Layout", settings);
                }
            });
        }

        if (wants ("focus"))
        {
            // #244 (yannikthegenius, Linux COSMIC / Wayland): "Manage shortcuts" opened without the caret in its search box
            // ("Find an effect" did). Both windows opened as the menus / the mixer open them; the search box must have the
            // keyboard focus once the window is up (with a window manager on the display: it activates new windows itself)
            struct Opener
            {
                const char* what;
                const char* key;
                std::function<void (lynx::MainComponent&)> open;
            };
            const std::vector<Opener> openers {
                { "Manage shortcuts", "shortcuts", [] (lynx::MainComponent& m) { m.showShortcutManagerForTest(); } },
                { "Quick-add palette (Find an effect's panel)", "quickadd", [] (lynx::MainComponent& m) { m.openQuickAddForTest(); } },
            };
            for (const auto& o : openers)
            {
                st->steps.push_back ([st, o] {
                    st->window->toFront (true);
                    // as from a menu: the menu goes, then its item runs
                    juce::MessageManager::callAsync ([ui = juce::Component::SafePointer<lynx::MainComponent> (st->ui), o] {
                        if (ui != nullptr)
                            o.open (*ui);
                    });
                    st->pause (1500);
                });
                st->steps.push_back ([st, o] {
                    auto* window = findToolWindow (o.key);
                    st->check (juce::String (o.what) + ": its window opened", window != nullptr);
                    if (window == nullptr)
                        return;
                    auto* target = focusOnOpenTarget (*window);
                    auto* focused = juce::Component::getCurrentlyFocusedComponent();
                    st->note (juce::String (o.what) + ": window active " + juce::String ((int) window->isActiveWindow()) + ", peer focused "
                              + juce::String ((int) (window->getPeer() != nullptr && window->getPeer()->isFocused())) + ", focus on "
                              + (focused == nullptr ? juce::String ("nothing") : focused->getName() + " [" + juce::String (typeid (*focused).name()) + "]"));
                    st->check (juce::String (o.what) + ": the caret is in its search box", target != nullptr && focused == target);
                    // the window manager of #244 (COSMIC / Wayland, through XWayland) activates the window only after these
                    // requests: nothing inside had the focus, then the window's activation arrives (JUCE: handleFocusGain
                    // with no last focused child). Played here on the window's own peer.
                    juce::Component::unfocusAllComponents();
                    if (auto* peer = window->getPeer())
                    {
                        peer->handleFocusLoss();
                        peer->handleFocusGain();
                    }
                    st->pause (400);
                });
                st->steps.push_back ([st, o] {
                    auto* window = findToolWindow (o.key);
                    if (window == nullptr)
                        return;
                    auto* target = focusOnOpenTarget (*window);
                    auto* focused = juce::Component::getCurrentlyFocusedComponent();
                    st->note (juce::String (o.what) + ", activated late: focus on "
                              + (focused == nullptr ? juce::String ("nothing") : focused->getName() + " [" + juce::String (typeid (*focused).name()) + "]"));
                    st->check (juce::String (o.what) + ": activated late by the window manager, the caret goes to its search box", target != nullptr && focused == target);
                    closeToolWindowOf (*window->getContentComponent());
                    st->pause (500);
                });
            }
        }

        if (wants ("keys") && ! pluginFiles.isEmpty())
        {
            // 10.net (Discord, 0.1.31.1, Windows 11): over a docked hosted VSTi / CLAP the typing keyboard played nothing,
            // whatever its keyboard button said; detached it played. The plugin's own view takes the focus and gets Z
            // (a typing keyboard key) posted, as a real key arrives, with the window's keyboard button both ways
            // (hostKeysFirst = "Allow the plugin to handle keystrokes before the host does" unticked), docked and detached.
            struct Counts
            {
                int first = 0, left = 0, shortcuts = 0, played = 0;
            };
            auto counts = std::make_shared<Counts>();
            auto channel = std::make_shared<int> (-1);
            st->steps.push_back ([st, pluginFiles, counts, channel] {
                auto& host = st->engine->getPluginHost();
                PluginRef ref;
                for (const auto& file : pluginFiles)
                {
                    host.addPluginFile (juce::File (file));
                    for (const auto& t : host.getKnownPlugins().getTypes())
                        if (t.isInstrument && ref.id.empty() && juce::File (t.fileOrIdentifier).getFileName() == juce::File (file).getFileName())
                            ref.id = t.createIdentifierString().toStdString(), ref.name = t.name.toStdString();
                }
                st->note ("hosted instrument: " + juce::String (ref.name));
                if (ref.id.empty())
                    return;
                st->engine->editSong ([ref, channel] (Song& s) {
                    ChannelDef p;
                    p.name = "Kys190";
                    p.instrument = InstrumentKind::plugin;
                    p.plugin = ref;
                    s.channels.push_back (p);
                    *channel = (int) s.channels.size() - 1;
                });
                st->engine->renderOffline (0.05, 48000.0);
                st->ui->setTypingPianoForTest (true);
                // the app's own handlers, counted
                setPluginViewPlainKeyHandler ([] (juce::Component& w) { return PluginWindows::letsHostProcessKeys (w); },
                                              [ui = juce::Component::SafePointer<MainComponent> (st->ui), counts] (const juce::KeyPress& k) {
                                                  ++counts->first;
                                                  return ui != nullptr && ui->keyFromPluginView (k, true);
                                              },
                                              [ui = juce::Component::SafePointer<MainComponent> (st->ui), counts] (const juce::KeyPress& k) {
                                                  ++counts->left;
                                                  const auto used = ui != nullptr && ui->keyFromPluginView (k, false);
                                                  counts->played += used ? 1 : 0;
                                                  return used;
                                              });
                // (and the shortcuts' path, what an attached window's unused keys reached before)
                setSecondaryWindowKeyHandler ([ui = juce::Component::SafePointer<MainComponent> (st->ui), counts] (const juce::KeyPress& k) {
                    ++counts->shortcuts;
                    const auto used = ui != nullptr && ui->keyFromSecondaryWindow (k);
                    counts->played += used ? 1 : 0;
                    return used;
                });
                st->pause (1000);
            });
            for (const auto* mode : { "docked", "detached" })
                for (const bool hostFirst : { true, false })
                {
                    const juce::String m (mode);
                    st->steps.push_back ([st, channel, m, hostFirst] {
                        if (*channel < 0)
                            return;
                        st->engine->editSong ([channel, m, hostFirst] (Song& s) {
                            auto& ref = s.channels[(std::size_t) *channel].plugin;
                            ref.detached = m == "detached";
                            ref.hostKeysFirst = hostFirst;
                        });
                        st->ui->getRackForSnapshot().toggleEditor (*channel, false);
                        st->pause (1500);
                    });
                    st->steps.push_back ([this, st, channel, counts, m, hostFirst] {
                        if (*channel < 0)
                            return;
                        auto* w = hotfixUiFindIn190ra ("Kys190");
                        st->check (m + ": the hosted instrument's window shows", w != nullptr);
                        if (w == nullptr)
                            return;
                        while (w->getParentComponent() != nullptr && w->getWindowHandle() == w->getParentComponent()->getWindowHandle())
                            w = w->getParentComponent(); // (up to the component that owns its native window)
                        *counts = {};
                        juce::Process::makeForegroundProcess(); // (the system gives the focus only to the active program's windows)
                        w->toFront (true);
                        // a text field of the main window still holds JUCE's focus (the Browser's search after a search, a
                        // name box), as it can when the user clicks into the plugin
                        if (textField190ra == nullptr)
                        {
                            textField190ra = std::make_unique<juce::TextEditor> ("focus190ra");
                            st->ui->addAndMakeVisible (*textField190ra);
                            textField190ra->setBounds (4, 4, 60, 20);
                        }
                        textField190ra->grabKeyboardFocus();
                        const auto posted = postKeyToPluginViewForTest (*w, 'Z');
                        st->note (m + (hostFirst ? ", keyboard button off (LYNX first)" : ", keyboard button on (the plugin first)") + ": key posted "
                                  + juce::String ((int) posted) + ", focus seen in " + focusedPluginViewForTest());
                        st->pause (600);
                    });
                    st->steps.push_back ([st, channel, counts, m, hostFirst] {
                        if (*channel < 0)
                            return;
                        st->note (m + ": LYNX first took " + juce::String (counts->first) + ", came back from the plugin " + juce::String (counts->left)
                                  + ", reached the shortcuts " + juce::String (counts->shortcuts) + ", used by LYNX " + juce::String (counts->played));
                       #if JUCE_WINDOWS
                        if (hostFirst)
                            st->check (m + ", keyboard button off: Z plays the typing keyboard before the plugin", counts->first >= 1);
                        else // (Surge XT leaves Z unused: it comes back and plays, as from a detached window)
                            st->check (m + ", keyboard button on: Z the plugin leaves plays the typing keyboard, a text field of the main window focused",
                                       counts->played >= 1);
                       #endif
                        st->ui->getRackForSnapshot().toggleEditor (*channel, false); // closed again
                        st->pause (800);
                    });
                }
            st->steps.push_back ([this, st] {
                setSecondaryWindowKeyHandler ([ui = juce::Component::SafePointer<MainComponent> (st->ui)] (const juce::KeyPress& k) { return ui != nullptr && ui->keyFromSecondaryWindow (k); });
                setPluginViewPlainKeyHandler ([] (juce::Component& w) { return PluginWindows::letsHostProcessKeys (w); },
                                              [ui = juce::Component::SafePointer<MainComponent> (st->ui)] (const juce::KeyPress& k) { return ui != nullptr && ui->keyFromPluginView (k, true); },
                                              [ui = juce::Component::SafePointer<MainComponent> (st->ui)] (const juce::KeyPress& k) { return ui != nullptr && ui->keyFromPluginView (k, false); });
                PluginWindows::closeAll();
                textField190ra = nullptr;
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
    std::unique_ptr<juce::TextEditor> textField190ra;

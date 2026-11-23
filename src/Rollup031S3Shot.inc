    // --rollup031s3shot <prefix> (rollup 0.1.31 step 3, round 190ny): the texts of the step drawn off screen at 100 % and
    // @150 through the real code — View › Layout style (English: "… layout"), the live session's Start dialog and, with a
    // relay (LYNX_RELAY_URL, a local server/app/lynx_relay.py), its invite code with Copy; then offline mode (190nx):
    // File › Online session with its note, the live session's and Help › Check for updates' messages. Run it with
    // --lang pt-BR for the Portuguese pictures. <prefix>-checks.txt lists the checks. Included in the app class of Main.cpp.
    bool runRollup031S3Shot (const juce::String& prefix)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        juce::String checks;
        bool allOk = true;
        const auto check = [&checks, &allOk, &prefix] (const juce::String& what, bool ok) {
            checks << (ok ? "ok   " : "FAIL ") << what << "\n";
            allOk = allOk && ok;
            juce::File (prefix + "-checks.txt").replaceWithText (checks);
        };
        const auto line = [&checks, &prefix] (const juce::String& what) {
            checks << "     " << what << "\n";
            juce::File (prefix + "-checks.txt").replaceWithText (checks);
        };
        const auto write = [&prefix] (juce::Component& c, const juce::String& name) {
            savePng (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f), prefix + "-" + name + ".png");
            savePng (c.createComponentSnapshot (c.getLocalBounds(), true, 1.5f), prefix + "-" + name + "@150.png");
        };
        // a menu as JUCE draws it (saveMenus), at 100 % and @150 (the shot scale)
        const auto writeMenu = [&prefix] (const juce::String& title, const juce::PopupMenu& menu, const juce::String& name) {
            saveMenus (juce::StringArray { title }, std::vector<juce::PopupMenu> { menu }, prefix + "-" + name + ".png");
            const juce::ScopedValueSetter<float> scaled (shot::scaleOverride(), 1.5f);
            saveMenus (juce::StringArray { title }, std::vector<juce::PopupMenu> { menu }, prefix + "-" + name + "@150.png");
        };
        const auto itemsOf = [] (const juce::PopupMenu& m) {
            juce::StringArray items;
            for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
                if (! it.getItem().isSeparator)
                    items.add (it.getItem().text.upToFirstOccurrenceOf ("\t", false, false));
            return items;
        };

        AudioEngine engine (false);
        MainComponent ui (engine, false);
        ui.setSize (1920, 1080);
        ui.setVisible (true);
        const auto settle = [&engine] {
            for (int k = 0; k < 4; ++k)
                engine.dispatchPendingMessages();
#if JUCE_LINUX || JUCE_WINDOWS
            for (int i = 0; i < 256 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i) {}
#endif
        };
        const auto pump = [&settle] (int ms) {
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
            while (juce::Time::getMillisecondCounter() < end)
            {
                settle();
                juce::Thread::sleep (10);
            }
        };
        // the modal window a command opened (an AlertWindow): drawn, then dismissed with `result`
        const auto modal = [&] (const juce::String& name, const juce::String& title, int result) -> juce::String {
            settle();
            auto* w = dynamic_cast<juce::AlertWindow*> (juce::Component::getCurrentlyModalComponent (0));
            check (name + ": the window \"" + title + "\" is up", w != nullptr && w->getName() == title);
            if (w == nullptr)
                return {};
            juce::StringArray buttons;
            for (int i = 0; i < w->getNumButtons(); ++i)
                if (auto* b = w->getButton (i))
                    buttons.add (b->getButtonText());
            write (*w, name);
            line (name + ": buttons " + buttons.joinIntoString (" | "));
            w->exitModalState (result);
            settle();
            return buttons.joinIntoString ("|");
        };

        // --- View › Layout style -------------------------------------------------------------------------------------------
        {
            const auto names = ui.getMenuNamesForSnapshot();
            juce::PopupMenu styles;
            bool found = false;
            for (int m = 0; m < names.size() && ! found; ++m)
            {
                const auto top = ui.getMenuForSnapshot (m); // (kept: the iterator refers into it)
                for (juce::PopupMenu::MenuItemIterator it (top); it.next();)
                    if (it.getItem().subMenu != nullptr && it.getItem().text == tr ("Layout style"))
                    {
                        styles = *it.getItem().subMenu;
                        found = true;
                        break;
                    }
            }
            check ("View has Layout style", found);
            const auto items = itemsOf (styles);
            line ("Layout style: " + items.joinIntoString (" | "));
            check ("Layout style names the three layouts as the current language shows them",
                   items.size() >= 3 && items[0] == tr ("Pattern workflow (default)") && items[1] == tr ("Clip-launch workflow") && items[2] == tr ("Simple workflow"));
            if (i18n::current() == "en")
                check ("English: Pattern layout (default) / Clip-launch layout / Simple layout",
                       items.size() >= 3 && items[0] == "Pattern layout (default)" && items[1] == "Clip-launch layout" && items[2] == "Simple layout");
            writeMenu (tr ("Layout style"), styles, "layout-style-menu");
        }

        auto ctx = ui.getLiveContextForSnapshot();

        // --- the live session's Start dialog (and, with a relay, the invite code's Copy) ----------------------------------
        offline::set (false);
        if (! offline::enabled())
        {
            liveui::start (ctx);
            const auto relay = juce::SystemStats::getEnvironmentVariable ("LYNX_RELAY_URL", {});
            const auto buttons = modal ("live-start", tr ("Start live session"), relay.isNotEmpty() ? 1 : 0);
            check ("Start live session: its button is the verb (" + trc ("@action button: Start") + ")", buttons.startsWith (trc ("@action button: Start") + "|"));
            if (relay.isNotEmpty())
            {
                // the real start through the local relay; the invite window opens once the session is live
                juce::Component* invite = nullptr;
                for (int i = 0; i < 300 && invite == nullptr; ++i)
                {
                    pump (50);
                    if (auto* w = dynamic_cast<juce::AlertWindow*> (juce::Component::getCurrentlyModalComponent (0)); w != nullptr && w->getName() == tr ("Invite code"))
                        invite = w;
                }
                check ("with the relay the session starts and shows its invite code", invite != nullptr);
                if (invite != nullptr)
                {
                    const auto inviteButtons = modal ("live-invite", tr ("Invite code"), 0);
                    check ("Invite code: Copy is the verb (" + tr ("Copy") + ")", inviteButtons.startsWith (tr ("Copy") + "|"));
                }
                if (ctx.live.isActive())
                {
                    ctx.live.leave();
                    pump (500);
                }
            }
            else
                line ("no LYNX_RELAY_URL: the invite code's window (Copy) is not drawn");
        }

        // --- offline mode (round 190nx) -----------------------------------------------------------------------------------
        offline::set (true);
        {
            juce::PopupMenu online;
            liveui::addMenuItems (online, ctx);
            const auto items = itemsOf (online);
            line ("Online session (offline): " + items.joinIntoString (" | "));
            check ("offline: the Online session menu says why live sessions are off", items.contains (tr ("Live sessions are off: offline mode")));
            writeMenu (tr ("Online session"), online, "offline-online-menu");

            liveui::start (ctx);
            modal ("offline-live-start", tr ("Start live session"), 0);
            liveui::join (ctx);
            modal ("offline-live-join", tr ("Join live session"), 0);
            if (updater::offered())
            {
                updater::checkNow();
                modal ("offline-check-updates", tr ("Check for updates"), 0);
            }
            else
                line ("this build offers no updates (Store): Check for updates is not drawn");
        }
        offline::set (false);
        settle();

        checks << "result=" << (allOk ? "OK" : "FAIL") << "\n";
        juce::File (prefix + "-checks.txt").replaceWithText (checks);
        return allOk;
    }

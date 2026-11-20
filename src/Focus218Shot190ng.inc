    // --focus218shot190ng <prefix> (round 190ng, issue #218 "Stuck inside Pattern toolbar icon"): the main window on the
    // desktop (Xvfb or a real display), clicks sent through its window (peer): the toolbar's Pattern and snap boxes never
    // keep the PC keyboard, any drop-down or button of the main window hands it back (ui/KeyFocus.h), Esc gives it back,
    // dialogs keep their own keyboard navigation. <prefix>-checks.txt lists the checks; pictures @100 and @150.
    // Included in the app class of Main.cpp.
    bool runFocus218Shot190ng (const juce::String& prefix)
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
        using namespace lynx;
        AudioEngine offline (false);
        MainComponent ui (offline, false);
        ui.setSize (1600, 900);
        ui.setVisible (true);
        ui.addToDesktop (juce::ComponentPeer::windowAppearsOnTaskbar);
        ui.toFront (true);
        offline.dispatchPendingMessages();
        auto* peer = ui.getPeer();
        check ("the main window is on the desktop", peer != nullptr && ui.isShowing());
        if (peer == nullptr)
        {
            juce::File (prefix + "-checks.txt").replaceWithText (checks + "result=FAIL\n");
            return false;
        }
        auto t = juce::Time::currentTimeMillis();
        const auto click = [&] (juce::Component& c, juce::Point<int> p) { // press + release through the window, as a real click
            const auto at = peer->globalToLocal (c.localPointToGlobal (p.toFloat()));
            peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, at, {}, 1.0f, 0.0f, t += 40, {}, 0);
            peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                    1.0f, 0.0f, t += 40, {}, 0);
            peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, at, {}, 1.0f, 0.0f, t += 60, {}, 0);
        };
        const auto focused = [] { return juce::Component::getCurrentlyFocusedComponent(); };
        const auto name = [] (juce::Component* c) {
            return c == nullptr ? juce::String ("nothing") : juce::String (typeid (*c).name()) + " '" + c->getName() + "'";
        };

        ui.grabKeyboardFocus();
        const auto displayFocus = ui.hasKeyboardFocus (false);
        line ("display focus: " + juce::String (displayFocus ? "yes" : "no (the window manager gives none: focus checks below use JUCE's own state)"));
        auto& patternBox = ui.patternBoxForTest();
        auto& gridBox = ui.gridBoxForTest();
        check ("the Pattern box doesn't want the keyboard", ! patternBox.getWantsKeyboardFocus() && ! patternBox.getMouseClickGrabsKeyboardFocus());
        check ("the snap box doesn't want the keyboard", ! gridBox.getWantsKeyboardFocus() && ! gridBox.getMouseClickGrabsKeyboardFocus());

        // the issue's steps: a click on the Pattern box (its list opens), a pick, then Enter
        if (patternBox.isShowing() && displayFocus)
        {
            click (patternBox, patternBox.getLocalBounds().getCentre());
            line ("after the click: focus on " + name (focused()) + ", list open: " + juce::String (patternBox.isPopupActive() ? "yes" : "no"));
            juce::PopupMenu::dismissAllActiveMenus();
            patternBox.hidePopup();
            check ("after the click and the list: the Pattern box doesn't hold the keyboard", ! patternBox.hasKeyboardFocus (true));
            check ("after the click: the main window keeps the keyboard", ui.hasKeyboardFocus (true) && focused() != &patternBox);
            ui.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
            check ("Enter afterwards doesn't open the Pattern list again", ! patternBox.isPopupActive());
            juce::PopupMenu::dismissAllActiveMenus();
            patternBox.grabKeyboardFocus();
            check ("even asked directly, the Pattern box doesn't take the keyboard", ! patternBox.hasKeyboardFocus (true));
            gridBox.grabKeyboardFocus();
            check ("nor does the snap box", ! gridBox.hasKeyboardFocus (true));
        }
        else
            line ("Pattern box clicks skipped (not showing in this layout or no display focus)");

        // any other drop-down or button of the main window that gets the keyboard (as the box did) hands it back
        juce::ComboBox box ("190ng box");
        box.addItem ("A", 1);
        box.addItem ("B", 2);
        juce::TextButton button ("190ng button");
        juce::TextEditor text ("190ng text");
        for (auto* c : std::initializer_list<juce::Component*> { &box, &button, &text })
        {
            ui.addAndMakeVisible (c);
            c->setBounds (10, 200, 120, 24);
        }
        check ("rules: a drop-down of the main window gives the keyboard back", ui::keyfocus::shouldGiveBack (&box, ui));
        check ("rules: a button of the main window gives the keyboard back", ui::keyfocus::shouldGiveBack (&button, ui));
        check ("rules: a text field keeps it", ! ui::keyfocus::shouldGiveBack (&text, ui));
        if (displayFocus)
        {
            box.grabKeyboardFocus();
            const auto took = box.hasKeyboardFocus (false);
            ui::keyfocus::giveBackNow (ui);
            check ("a focused drop-down: the main window takes the keyboard back", took && ui.hasKeyboardFocus (false));
            button.grabKeyboardFocus();
            ui::keyfocus::giveBackNow (ui);
            check ("a focused button: the main window takes the keyboard back", ui.hasKeyboardFocus (false));
            box.grabKeyboardFocus();
            const auto used = ui.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            check ("Esc while a drop-down holds the keyboard gives it back (and is used up)", used && ui.hasKeyboardFocus (false));
            text.grabKeyboardFocus();
            ui::keyfocus::giveBackNow (ui);
            check ("a text field keeps the keyboard (typing)", text.hasKeyboardFocus (false));
            ui.grabKeyboardFocus();
        }
        for (auto* c : std::initializer_list<juce::Component*> { &box, &button, &text })
            ui.removeChildComponent (c);

        // dialogs and other windows keep their own keyboard navigation
        {
            juce::DocumentWindow dialog ("190ng dialog", juce::Colours::black, 0);
            juce::ComboBox inDialog ("in dialog");
            inDialog.setSize (120, 24);
            dialog.setContentNonOwned (&inDialog, true);
            check ("rules: a drop-down in a dialog keeps the keyboard", ! ui::keyfocus::shouldGiveBack (&inDialog, ui));
            dialog.getProperties().set ("lynxPanel", true);
            check ("rules: a drop-down in a panel window (detached Mixer...) gives it back", ui::keyfocus::shouldGiveBack (&inDialog, ui));
            juce::ComboBox typed ("typed");
            typed.setEditableText (true);
            ui.addChildComponent (typed);
            check ("rules: a drop-down one types into keeps the keyboard", ! ui::keyfocus::shouldGiveBack (&typed, ui));
            ui.removeChildComponent (&typed);
        }

        // pictures: the toolbar around the Pattern box after the click (FL draws no focus frame there)
        if (patternBox.isShowing())
        {
            const auto area = ui.getLocalArea (&patternBox, patternBox.getLocalBounds()).expanded (180, 14).getIntersection (ui.getLocalBounds());
            savePng (ui.createComponentSnapshot (area, true, 1.0f), prefix + "-toolbar-pattern-after-click.png");
            savePng (ui.createComponentSnapshot (area, true, 1.5f), prefix + "-toolbar-pattern-after-click@150.png");
        }
        ui.removeFromDesktop();
        juce::File (prefix + "-checks.txt").replaceWithText (checks + "result=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

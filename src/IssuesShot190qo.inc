    // --issuesshot190qo <prefix> (round 190qo, issues #239, #215, #251, #270), offscreen through the real components:
    // Settings › LYNX extras › Automatic colors and icons adds a second, third... rule (the page used to lay itself out
    // before the new row, so "Add rule" ended under the section's bottom edge after the first rule), moves rules up and
    // down, removes one; the two new LYNX extras on the page; the typing keyboard's layouts. <prefix>-checks.txt lists
    // the checks; pictures @100 and @150. Included in the app class of Main.cpp.
    bool runIssuesShot190qo (const juce::String& prefix)
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
        const auto pump = [] {
#if JUCE_LINUX || JUCE_WINDOWS
            for (int i = 0; i < 256 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i) {}
#endif
        };
        const auto findId = [] (juce::Component& root, const juce::String& id) -> juce::Component* {
            std::function<juce::Component* (juce::Component&)> find = [&] (juce::Component& c) -> juce::Component* {
                if (c.getComponentID() == id)
                    return &c;
                for (auto* child : c.getChildren())
                    if (auto* f = find (*child))
                        return f;
                return nullptr;
            };
            return find (root);
        };
        // scrolls the page's viewport so `c` sits 40 px under its top (the shots take the panel's own pixels)
        const auto reveal = [&pump] (juce::Component& c) {
            if (auto* viewport = c.findParentComponentOfClass<juce::Viewport>())
                if (auto* content = viewport->getViewedComponent())
                    viewport->setViewPosition (0, juce::jmax (0, content->getLocalPoint (&c, juce::Point<int>()).y - 40));
            pump();
        };
        using namespace lynx;

        // ================= #239: several rules =================
        {
            const auto saved = autolook::current();
            autolook::current() = {};
            AudioEngine offline (false);
            auto panel = makeSettingsPanel (offline, SettingsPage::extras);
            panel->setSize (1000, 700); // (a window's height: the page scrolls, as in the reporter's picture)
            pump();
            auto* add = dynamic_cast<juce::Button*> (findId (*panel, "autoLook.add"));
            auto* editor = add != nullptr ? dynamic_cast<AutoLookEditor*> (add->getParentComponent()) : nullptr;
            check ("#239 the rules editor and its Add rule button", editor != nullptr);
            if (editor != nullptr)
            {
                // the editor shows all it holds: Add rule inside its bounds, its bounds as tall as it asks for
                const auto whole = [editor, add] {
                    return editor->getHeight() >= editor->heightFor (editor->getWidth()) && add->getBottom() <= editor->getHeight()
                           && add->isVisible() && add->getWidth() > 0;
                };
                check ("#239 no rule: Add rule shown", editor->rowCount() == 0 && whole());
                const juce::StringArray names { "BEAT", "VOX", "REC" };
                for (int i = 0; i < names.size(); ++i)
                {
                    add->onClick();
                    pump();
                    autolook::current().list.back().text = names[i].toStdString();
                    editor->refresh();
                    pump();
                    check ("#239 rule " + juce::String (i + 1) + " added: " + juce::String (editor->rowCount()) + " rows, Add rule still inside ("
                               + juce::String (add->getBottom()) + " <= " + juce::String (editor->getHeight()) + ")",
                           editor->rowCount() == i + 1 && whole());
                }
                // (the panel's own pixels: the page behind the rows has no background of its own)
                reveal (*editor);
                write (*panel, panel->getLocalArea (editor->getParentComponent(), editor->getBoundsInParent().expanded (0, 30)), "239-three-rules");
                // up / down: the first matching rule wins, so the order is the user's
                auto* up = dynamic_cast<juce::Button*> (findId (*editor, "autoLook.up2"));
                check ("#239 the first row can't move up, the last can't move down",
                       findId (*editor, "autoLook.up0") != nullptr && ! findId (*editor, "autoLook.up0")->isEnabled()
                           && findId (*editor, "autoLook.down2") != nullptr && ! findId (*editor, "autoLook.down2")->isEnabled());
                if (up != nullptr)
                {
                    up->onClick();
                    pump();
                }
                const auto order = [] {
                    juce::StringArray o;
                    for (const auto& r : autolook::current().list)
                        o.add (juce::String::fromUTF8 (r.text.c_str()));
                    return o.joinIntoString (",");
                };
                check ("#239 REC moved up: " + order(), order() == "BEAT,REC,VOX");
                if (auto* down = dynamic_cast<juce::Button*> (findId (*editor, "autoLook.down0")))
                {
                    down->onClick();
                    pump();
                }
                check ("#239 BEAT moved down: " + order(), order() == "REC,BEAT,VOX");
                // remove the middle one: two rows, the editor shorter, Add rule still inside
                const auto tall = editor->getHeight();
                if (auto* remove = dynamic_cast<juce::Button*> (findId (*editor, "autoLook.remove1")))
                {
                    remove->onClick();
                    pump();
                }
                check ("#239 BEAT removed: " + order() + ", height " + juce::String (tall) + " -> " + juce::String (editor->getHeight()),
                       order() == "REC,VOX" && editor->rowCount() == 2 && editor->getHeight() < tall && whole());
                // a fourth rule after the removal, then the note at a narrow width (three lines in some languages)
                add->onClick();
                pump();
                check ("#239 adding after a removal", editor->rowCount() == 3 && whole());
                panel->setSize (620, 700);
                pump();
                check ("#239 narrow window: the note and the rows still fit", whole() && editor->heightFor (560) >= editor->heightFor (900));
                reveal (*editor);
                write (*panel, panel->getLocalArea (editor->getParentComponent(), editor->getBoundsInParent().expanded (0, 30)), "239-narrow");
                panel->setSize (1000, 700);
                pump();
                // the new extras' switches (#251, #215) sit on the same page
                check ("#251 / #215 switches on the page", findId (*panel, "file.alwaysCollect") != nullptr && findId (*panel, "export.noDialog") != nullptr);
                if (auto* collect = findId (*panel, "file.alwaysCollect"))
                {
                    reveal (*collect);
                    write (*panel, panel->getLocalArea (collect->getParentComponent(), collect->getBoundsInParent().withX (0).withWidth (1000).withHeight (190).translated (0, -40)),
                           "251-215-extras");
                }
            }
            autolook::current() = saved;
        }

        // ================= #270: the typing keyboard's layouts =================
        {
            using namespace keys;
            check ("#270 LYNX's chord rule gives FL's own chord layouts back: " + juce::String (typingpiano::chordRuleCheck()), typingpiano::chordRuleCheck() == "OK");
            juce::StringArray lynxNames;
            for (int i = 0; i < typingpiano::layoutCount(); ++i)
                if (typingpiano::layoutSource (i) == typingpiano::Source::lynx)
                    lynxNames.add (typingpiano::layoutName (i));
            line ("#270 LYNX layouts: " + lynxNames.joinIntoString (", "));
            check ("#270 Minor Melodic + Chords and Chordmap", lynxNames.contains ("Minor Melodic + Chords") && lynxNames.contains ("Minor Melodic Chordmap"));
        }

        // ================= #272: slide notes in the Piano roll (FL's ◢ mark) =================
        // LYNX_SHOT_FLP=<an .flp with slide notes> (round 190qo: pr/flp/slides272.flp, the project of the FL capture)
        if (const auto flp = juce::SystemStats::getEnvironmentVariable ("LYNX_SHOT_FLP", {}); flp.isNotEmpty() && juce::File (flp).existsAsFile())
        {
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (1280, 900);
            ui.openFile (juce::File (flp));
            offline.dispatchPendingMessages();
            pump();
            ui.selectTabForSnapshot (1);
            offline.dispatchPendingMessages();
            pump();
            int slides = 0;
            for (const auto& n : offline.getSong().activePattern().notes)
                slides += n.slide ? 1 : 0;
            check ("#272 the .flp's slide notes imported: " + juce::String (slides), slides == 5);
            write (ui, ui.getLocalBounds(), "272-pianoroll-slides");
        }

        juce::File (prefix + "-checks.txt").replaceWithText (checks);
        std::printf ("%s", checks.toRawUTF8());
        return allOk;
    }

    // Round 190qx (the owner's "make the options easy", 11-24): --selftest item optionsux190qx and --optionsux190qxshot
    // <prefix>. Included in the app class of Main.cpp.
    //  - the LYNX extras page grouped by area (each area once, in extras::sectionOrder), FL-exact / Recommended in its fixed top
    //  - every Settings option indexed for the command palette, each found by the Settings search on its page
    //  - every switch and box of the Settings pages has a tooltip (a one-line explanation)
    //  - "Reset this page": what each page resets and keeps, and its Undo; no reset on Files / Project / Info
    //  - the window tips (once each, gated by "No more tips" and Show tips and hints), the How-to guides, the notices
    juce::String runOptionsux190qxSelftest (lynx::AudioEngine& engine, juce::StringArray* lines = nullptr)
    {
        using namespace lynx;
        juce::String result = "OK";
        const auto fail = [&result, lines] (const juce::String& what) {
            if (result == "OK")
                result = "ERROR " + what;
            if (lines != nullptr)
                lines->add ("FAIL " + what);
        };
        const auto note = [lines] (const juce::String& what) {
            if (lines != nullptr)
                lines->add ("ok   " + what);
        };
        // (only what shows: a component off screen has no peer, so "showing" is the visible chain up to `root`)
        const auto walkShown = [] (juce::Component& root, const std::function<void (juce::Component&)>& fn) {
            std::function<void (juce::Component&)> go = [&] (juce::Component& c) {
                if (&c != &root && ! c.isVisible())
                    return;
                fn (c);
                for (auto* child : c.getChildren())
                    go (*child);
            };
            go (root);
        };
        const auto walk = [] (juce::Component& root, const std::function<void (juce::Component&)>& fn) {
            std::function<void (juce::Component&)> go = [&] (juce::Component& c) {
                fn (c);
                for (auto* child : c.getChildren())
                    go (*child);
            };
            go (root);
        };
        GeneralOptions general;
        general.language = "en";
        SettingsHooks hooks;
        hooks.getGeneral = [&general] { return general; };
        hooks.setGeneral = [&general] (const GeneralOptions& o) { general = o; };
        bool tips = true;
        hooks.tipsShown = [&tips] { return tips; };
        hooks.showTips = [&tips] (bool on) { tips = on; };

        // 1. the extras page: grouped by area
        {
            for (const auto& item : extras::items)
                if (extras::sectionRank (item.section) >= (int) std::size (extras::sectionOrder))
                    fail ("extra " + juce::String (std::string (item.id)) + " has an area missing in sectionOrder");
            auto page = makeExtrasPage (engine, hooks, false);
            page->setSize (760, 700);
            juce::StringArray sections;
            walk (*page, [&sections] (juce::Component& c) {
                if (auto* t = dynamic_cast<settingsui::SectionTitle*> (&c))
                    sections.add (t->getText());
            });
            juce::StringArray unique (sections);
            unique.removeDuplicates (false);
            if (unique.size() != sections.size())
                fail ("the extras page shows an area twice: " + sections.joinIntoString (", "));
            else
                note ("extras page areas: " + sections.joinIntoString (", "));
            juce::Component* fl = nullptr;
            juce::Component* first = nullptr;
            walk (*page, [&] (juce::Component& c) {
                if (c.getComponentID() == "extras.flExact")
                    fl = &c;
                if (c.getComponentID() == "general.escCloses")
                    first = &c;
            });
            if (fl == nullptr || fl->getParentComponent() != page.get())
                fail ("FL-exact is not in the page's fixed top");
            if (first == nullptr || first->getParentComponent() == page.get())
                fail ("the switches do not scroll under the fixed top");
            int previews = 0;
            walk (*page, [&previews] (juce::Component& c) { previews += c.getComponentID().startsWith ("extras.preview.") ? 1 : 0; });
            if (previews != (int) std::size (extras::visualIds))
                fail ("previews " + juce::String (previews) + " of " + juce::String ((int) std::size (extras::visualIds)));
            for (const auto id : extras::visualIds)
                if (std::none_of (std::begin (extras::items), std::end (extras::items), [id] (const extras::Item& i) { return i.id == id; }))
                    fail ("visual id not an extra: " + juce::String (std::string (id)));
        }

        // 2. every Settings option in the palette's index, each found by the Settings search on its page
        {
            const auto& options = settingsOptions (engine, hooks);
            note ("Settings options indexed: " + juce::String ((int) options.size()) + " in " + juce::String (settingsOptionsBuildMs(), 1) + " ms");
            if (options.size() < 150)
                fail ("only " + juce::String ((int) options.size()) + " Settings options indexed");
            std::map<int, int> perPage;
            for (const auto& o : options)
                ++perPage[(int) o.page];
            for (const auto page : { SettingsPage::midi, SettingsPage::audio, SettingsPage::general, SettingsPage::files, SettingsPage::theme,
                                     SettingsPage::project, SettingsPage::info, SettingsPage::extras })
                if (perPage[(int) page] < 2)
                    fail ("no options indexed on " + settingsPageName (page));
            int notFound = 0;
            juce::String missed;
            auto panel = makeSettingsPanel (engine, SettingsPage::general, false, hooks);
            panel->setSize (720, 660);
            for (const auto& o : options)
            {
                settingsShowPageForSnapshot (*panel, o.page);
                const auto found = settingsFindForSnapshot (*panel, o.text);
                if (found.matches == 0)
                {
                    ++notFound;
                    if (missed.length() < 300)
                        missed << o.text << " (" << settingsPageName (o.page) << "); ";
                }
            }
            settingsFindForSnapshot (*panel, {});
            // (a few rows exist only in the real window: an open device's list, a running test)
            if (notFound > (int) options.size() / 20)
                fail (juce::String (notFound) + " indexed options not found by the Settings search: " + missed);
            else
                note (juce::String (notFound) + " indexed options not in a snapshot panel (device rows): " + missed);
        }

        // 3. every switch and box of the pages says what it does (a tooltip)
        {
            auto panel = makeSettingsPanel (engine, SettingsPage::general, false, hooks);
            panel->setSize (720, 660);
            int controls = 0;
            juce::String without;
            for (const auto page : { SettingsPage::midi, SettingsPage::audio, SettingsPage::general, SettingsPage::files, SettingsPage::theme,
                                     SettingsPage::project, SettingsPage::info, SettingsPage::link, SettingsPage::extras })
            {
                settingsShowPageForSnapshot (*panel, page);
                walkShown (*panel, [&] (juce::Component& c) {
                    auto* toggle = dynamic_cast<juce::ToggleButton*> (&c);
                    auto* combo = dynamic_cast<juce::ComboBox*> (&c);
                    if (toggle == nullptr && combo == nullptr)
                        return;
                    ++controls;
                    auto* tip = dynamic_cast<juce::SettableTooltipClient*> (&c);
                    if (tip != nullptr && tip->getTooltip().trim().isNotEmpty())
                        return;
                    // (a box beside a row caption that has the tooltip counts: the label is what the mouse reads first)
                    if (combo != nullptr)
                        if (auto* parent = c.getParentComponent())
                            for (auto* sibling : parent->getChildren())
                                if (auto* label = dynamic_cast<juce::Label*> (sibling); label != nullptr && std::abs (label->getY() - c.getY()) < 4
                                                                                        && label->getTooltip().isNotEmpty())
                                    return;
                    without << settingsPageName (page) << ": " << (toggle != nullptr ? toggle->getButtonText() : combo->getText()) << "; ";
                });
            }
            note ("controls checked for a tooltip: " + juce::String (controls));
            if (without.isNotEmpty())
                fail ("no tooltip: " + without.substring (0, 400));
        }

        // 4. Reset this page
        {
            auto panel = makeSettingsPanel (engine, SettingsPage::files, false, hooks);
            panel->setSize (720, 660);
            for (const auto page : { SettingsPage::files, SettingsPage::project, SettingsPage::info })
            {
                settingsShowPageForSnapshot (*panel, page);
                if (settingsCanResetForSnapshot (*panel))
                    fail ("Reset offered on " + settingsPageName (page));
            }
            // General: the language and layout stay, the rest back at the defaults; Undo brings it back
            general.boldText = 2;
            general.textSize = 130;
            general.layoutStyle = 1;
            general.language = "pt-BR";
            tips = false;
            flsettings::options().clickHold = true;
            settingsShowPageForSnapshot (*panel, SettingsPage::general);
            if (! settingsResetForSnapshot (*panel))
                fail ("General: Reset did not run");
            if (general.boldText != 0 || general.textSize != 100 || general.layoutStyle != 1 || general.language != "pt-BR" || ! tips
                || flsettings::options().clickHold.load())
                fail ("General: not reset as it should be");
            if (! settingsUndoResetForSnapshot (*panel) || general.boldText != 2 || general.textSize != 130 || tips || ! flsettings::options().clickHold.load())
                fail ("General: Undo did not bring it back");
            flsettings::options().clickHold = false;
            general = {};
            general.language = "en";
            tips = true;
            // the LYNX extras page = Recommended
            setLynxExtra (engine, hooks, "playlist.sectionsLane", true, false);
            settingsShowPageForSnapshot (*panel, SettingsPage::extras);
            if (! settingsResetForSnapshot (*panel) || lynxExtraOn (engine, hooks, "playlist.sectionsLane") != extras::defaultOn ("playlist.sectionsLane"))
                fail ("extras: Reset is not Recommended");
            if (! settingsUndoResetForSnapshot (*panel) || ! lynxExtraOn (engine, hooks, "playlist.sectionsLane"))
                fail ("extras: Undo");
            setLynxExtra (engine, hooks, "playlist.sectionsLane", extras::defaultOn ("playlist.sectionsLane"), false);
            // Audio: the FL options, the device kept
            flsettings::options().smartDisable = false;
            engine.getTransport().resetPluginsOnTransport = true;
            settingsShowPageForSnapshot (*panel, SettingsPage::audio);
            if (! settingsResetForSnapshot (*panel) || ! flsettings::options().smartDisable.load() || engine.getTransport().resetPluginsOnTransport.load())
                fail ("Audio: not reset");
            if (! settingsUndoResetForSnapshot (*panel) || flsettings::options().smartDisable.load())
                fail ("Audio: Undo");
            flsettings::options().smartDisable = true;
            engine.getTransport().resetPluginsOnTransport = false;
            // MIDI: the velocity curve back, the devices kept
            {
                auto p = engine.getMidiPrefs();
                const auto kept = p;
                p.velocity = prefs::VelocityCurve (1);
                p.pickup = true;
                engine.setMidiPrefs (p, false);
                settingsShowPageForSnapshot (*panel, SettingsPage::midi);
                if (! settingsResetForSnapshot (*panel) || engine.getMidiPrefs().pickup || engine.getMidiPrefs().inputs.size() != kept.inputs.size())
                    fail ("MIDI: not reset");
                if (! settingsUndoResetForSnapshot (*panel) || ! engine.getMidiPrefs().pickup)
                    fail ("MIDI: Undo");
                engine.setMidiPrefs (kept, false);
            }
            // Theme: LYNX's own
            {
                const auto kept = themeui::current();
                auto spec = kept;
                spec.accent = 0xff3388ff;
                themeui::apply (spec, false);
                settingsShowPageForSnapshot (*panel, SettingsPage::theme);
                if (! settingsResetForSnapshot (*panel) || ! (themeui::current() == themes::Spec {}))
                    fail ("Theme: not reset");
                if (! settingsUndoResetForSnapshot (*panel) || themeui::current().accent != 0xff3388ff)
                    fail ("Theme: Undo");
                themeui::apply (kept, false);
            }
            // asking first: the first click only asks
            settingsShowPageForSnapshot (*panel, SettingsPage::link);
            if (! settingsResetForSnapshot (*panel, false))
                fail ("Link: the first click does not ask");
            note ("Reset this page: General, Audio, MIDI, Theme, LYNX extras (+ Undo); none on Files, Project, Info");
        }

        // 5. the window tips
        {
            using namespace onboarding;
            State s;
            if (! TipGate::canShowWindowTip (Tip::mixerRouting, s, false) || TipGate::canShowWindowTip (Tip::mixerRouting, s, true)
                || TipGate::canShowWindowTip (Tip::songModeEmpty, s, false))
                fail ("window tip gate");
            TipGate::gotIt (s, Tip::mixerRouting);
            if (TipGate::canShowWindowTip (Tip::mixerRouting, s, false) || ! TipGate::canShowWindowTip (Tip::tracker, s, false))
                fail ("window tip: once each");
            s.tipsOff = true;
            if (TipGate::canShowWindowTip (Tip::tracker, s, false))
                fail ("window tip after No more tips");
            s.tipsOff = false;
            s.hintsOff = true;
            if (TipGate::canShowWindowTip (Tip::tracker, s, false))
                fail ("window tip with the hints off");
            State all;
            for (int t = 0; t < tipCount; ++t)
                all.tipsSeen |= 1u << (std::uint32_t) t;
            if (parse (serialize (all)).tipsSeen != all.tipsSeen)
                fail ("tipsSeen does not keep the window tips");
            juce::StringArray ids;
            for (int t = 0; t < tipCount; ++t)
            {
                ids.add (tipId ((Tip) t));
                if (juce::String (tipTitle ((Tip) t)).isEmpty() || juce::String (tipText ((Tip) t)).isEmpty() || juce::String (tipAction ((Tip) t)).isEmpty())
                    fail ("tip texts " + juce::String (tipId ((Tip) t)));
            }
            ids.removeDuplicates (false);
            if (ids.size() != tipCount)
                fail ("tip ids");
        }

        // 6. the How-to guides
        {
            juce::StringArray ids;
            for (int w = 0; w < howto::windowCount; ++w)
            {
                const auto win = (howto::Window) w;
                ids.add (howto::windowId (win));
                if (howto::stepCount (win) < 3 || howto::stepCount (win) > 5 || juce::String (howto::tutorialSlug (win)).isEmpty())
                    fail ("guide " + juce::String (howto::windowId (win)));
                auto card = howto::makeCard (win);
                if (card->getComponentID() != "howto." + juce::String (howto::windowId (win)) || card->getHeight() < 120)
                    fail ("guide card " + juce::String (howto::windowId (win)));
            }
            ids.removeDuplicates (false);
            if (ids.size() != howto::windowCount)
                fail ("guide ids");
            if (howto::tutorialUrl (howto::Window::mixer) != "https://lynx-share.com/learn/mixing-basics/" && i18n::current() == "en")
                fail ("tutorial url " + howto::tutorialUrl (howto::Window::mixer));
            juce::PopupMenu menu;
            howto::addMenuItem (menu, howto::Window::playlist, nullptr);
            bool listed = false;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                listed = listed || it.getItem().text == tr ("How to use this...");
            if (! listed)
                fail ("How to use this... item");
        }

        // 7. the notices
        {
            juce::Component window;
            window.setSize (1200, 800);
            ui::addNoticeHost (&window);
            ui::clearNotices();
            ui::pluginNotLoaded ("Serum");
            ui::pluginNotLoaded ("Serum"); // (the same notice again replaces it)
            if (ui::shownNoticeCount() != 1)
                fail ("notice replaced by its id: " + juce::String (ui::shownNoticeCount()));
            for (int i = 0; i < 4; ++i)
                ui::notify ({ ui::Notice::Kind::info, "Notice " + juce::String (i), {}, {}, {}, 0, {} });
            if (ui::shownNoticeCount() != 3)
                fail ("at most three notices: " + juce::String (ui::shownNoticeCount()));
            if (auto* c = ui::shownNotice (2); c == nullptr || c->getParentComponent() != &window || c->getRight() > window.getWidth() || c->getBottom() > window.getHeight())
                fail ("notice placed in the window's lower right");
            ui::clearNotices();
            ui::savedFile ("Saved a copy", juce::File::getSpecialLocation (juce::File::tempDirectory));
            const auto* card = ui::shownNotice (0);
            bool action = false;
            if (card != nullptr)
                for (auto* child : card->getChildren())
                    action = action || child->getComponentID() == "notice.action";
            if (! action)
                fail ("saved-file notice without Show in folder");
            ui::clearNotices();
            ui::removeNoticeHost (&window);
            if (ui::shownNoticeCount() != 0)
                fail ("notices left after clearNotices");
        }
        return result;
    }

    bool runOptionsux190qxShot (const juce::String& prefix)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        juce::StringArray checks;
        AudioEngine engine (false);
        const auto both = [&prefix] (juce::Component& c, juce::Rectangle<int> area, const juce::String& name) {
            area = area.getIntersection (c.getLocalBounds());
            if (area.isEmpty())
                return;
            savePng (c.createComponentSnapshot (area, true, 1.0f), prefix + "-" + name + ".png");
            savePng (c.createComponentSnapshot (area, true, 1.5f), prefix + "-" + name + "@150.png");
        };
        const auto selftest = runOptionsux190qxSelftest (engine, &checks);
        checks.add ("selftest item: " + selftest);
        GeneralOptions general;
        general.language = "en";
        SettingsHooks hooks;
        hooks.getGeneral = [&general] { return general; };
        hooks.setGeneral = [&general] (const GeneralOptions& o) { general = o; };
        hooks.tipsShown = [] { return true; };
        hooks.showTips = [] (bool) {};
        // the Settings pages (as the window opens them), and the extras page scrolled
        {
            auto panel = makeSettingsPanel (engine, SettingsPage::general, false, hooks);
            panel->setSize (720, 700);
            const std::pair<SettingsPage, const char*> pages[] = { { SettingsPage::midi, "midi" },     { SettingsPage::audio, "audio" },     { SettingsPage::general, "general" },
                                                                   { SettingsPage::files, "files" },   { SettingsPage::theme, "theme" },     { SettingsPage::project, "project" },
                                                                   { SettingsPage::info, "info" },     { SettingsPage::link, "link" },       { SettingsPage::extras, "extras" } };
            for (const auto& [page, name] : pages)
            {
                settingsShowPageForSnapshot (*panel, page);
                both (*panel, panel->getLocalBounds(), juce::String ("settings-") + name);
            }
            // the find bar outlining an option, as the palette shows it
            settingsShowPageForSnapshot (*panel, SettingsPage::general);
            const auto found = settingsFindForSnapshot (*panel, tr ("Show tips and hints"));
            checks.add (juce::String (found.matches > 0 ? "ok   " : "FAIL ") + "Settings search finds Show tips and hints (" + juce::String (found.matches) + ")");
            both (*panel, panel->getLocalBounds(), "settings-find-showtips");
            settingsFindForSnapshot (*panel, {});
            // the reset bar's three states (General)
            settingsShowPageForSnapshot (*panel, SettingsPage::general);
            auto* bar = settingsResetBarForSnapshot (*panel);
            const auto bottom = panel->getLocalBounds().removeFromBottom (36);
            both (*panel, bottom, "reset-1-button");
            settingsResetForSnapshot (*panel, false);
            both (*panel, bottom, "reset-2-confirm");
            if (bar != nullptr)
                for (auto* child : bar->getChildren())
                    if (child->getComponentID() == "settings.reset.confirm")
                        if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->onClick)
                            b->onClick();
            both (*panel, bottom, "reset-3-undo");
            settingsUndoResetForSnapshot (*panel);
            // the extras page, tall: every area and the pictures
            settingsShowPageForSnapshot (*panel, SettingsPage::extras);
            auto tall = makeExtrasPage (engine, hooks, false);
            tall->setSize (720, 5200);
            both (*tall, tall->getLocalBounds(), "extras-whole");
        }
        // the How-to cards, the tips, the notices
        for (int w = 0; w < howto::windowCount; ++w)
        {
            auto card = howto::makeCard ((howto::Window) w);
            both (*card, card->getLocalBounds(), juce::String ("howto-") + howto::windowId ((howto::Window) w));
        }
        for (int t = (int) onboarding::Tip::mixerRouting; t < onboarding::tipCount; ++t)
        {
            onboardui::TipCard card ((onboarding::Tip) t);
            card.setSize (onboardui::TipCard::width, card.idealHeight());
            both (card, card.getLocalBounds(), juce::String ("tip-") + onboarding::tipId ((onboarding::Tip) t));
        }
        {
            ui::openPluginManager = [] {};
            auto a = ui::makeNoticeCard ({ ui::Notice::Kind::problem, tr ("Couldn't open {0}", { "Serum" }),
                                           tr ("It isn't installed on this computer, or it hasn't been scanned yet. Its slot stays as it is in the project."),
                                           tr ("Plugin manager..."), [] {}, 0, {} });
            both (*a, a->getLocalBounds(), "notice-plugin");
            auto b = ui::makeNoticeCard ({ ui::Notice::Kind::done, tr ("Saved a copy for LYNX SHARE {0}", { "0.1.31" }), "Night Drive.lynx", tr ("Show in folder"), [] {}, 0, {} });
            both (*b, b->getLocalBounds(), "notice-savedcopy");
            auto c = ui::makeNoticeCard ({ ui::Notice::Kind::info, tr ("New: every window's menu has How to use this..., and Settings has Reset this page"),
                                           tr ("The ? palette finds any setting too: type a few words, Enter shows it in Settings."), tr ("How to use Settings"), [] {}, 0, {} });
            both (*c, c->getLocalBounds(), "notice-whatsnew");
            ui::openPluginManager = nullptr;
        }
        // the palette: a Settings option found by its words, and its open time
        {
            MainComponent window (engine, false);
            window.setSize (1600, 900);
            engine.dispatchPendingMessages();
            std::vector<double> times;
            for (int i = 0; i < 7; ++i)
            {
                window.runActionForSnapshot ("view.commandPalette");
                times.push_back (window.paletteOpenMsForSnapshot());
                if (i < 6)
                {
                    window.closeActionSearchForSnapshot();
                    engine.dispatchPendingMessages();
                }
            }
            std::sort (times.begin(), times.end());
            checks.add (juce::String (times[3] <= 30.0 ? "ok   " : "FAIL ") + "palette opens in " + juce::String (times[3], 2) + " ms (median of 7; first "
                        + juce::String (times.back(), 1) + " ms with the Settings index)");
            if (auto* p = window.actionSearchForSnapshot())
            {
                checks.add ("palette items: " + juce::String (p->itemCount()));
                p->setQuery ("bold text");
                const juce::String id (p->selectedId());
                checks.add (juce::String (id.startsWith ("setting:") ? "ok   " : "FAIL ") + "\"bold text\" finds the Settings option first: " + id);
                both (window, window.getLocalBounds(), "palette-boldtext");
                p->setQuery ("buffer");
                checks.add ("\"buffer\": first " + juce::String (p->selectedId()) + ", " + juce::String (p->matchCount()) + " items");
                p->setQuery ("settings: audio");
                checks.add (juce::String (juce::String (p->selectedId()) == "settings:Audio" ? "ok   " : "FAIL ") + "\"settings: audio\" still finds the page first: " + juce::String (p->selectedId()));
                p->setQuery ("how to use the mixer");
                checks.add ("\"how to use the mixer\": first " + juce::String (p->selectedId()));
                window.closeActionSearchForSnapshot();
                engine.dispatchPendingMessages();
            }
        }
        bool ok = selftest == "OK";
        for (const auto& line : checks)
            ok = ok && ! line.startsWith ("FAIL");
        checks.add (juce::String ("result=") + (ok ? "OK" : "FAIL"));
        juce::File (prefix + "-checks.txt").replaceWithText (checks.joinIntoString ("\n") + "\n");
        return ok;
    }

    // Round 190qg (Discord #feedback 11-23: "toolbar elements appear or disappear depending on the space"): the main
    // toolbar's walk and pictures. Included in the app class (--toolbarshot190qg, --selftest).
    //
    // The walk: every layout style (Pattern, Simple, Clip-launch), every toolbar preset (and the Grouped look), every icon
    // size, and the window widths a screen of 1024 ... 3840 px gives at every Interface size (80 ... 250 %, the window at
    // least its 800 px minimum): every item of the shown panels must be on the toolbar (inside it, covering nothing) or in
    // the overflow menu ("»"; "⋯" in Simple / Clip-launch), never lost (MainComponent::checkToolbarItemsForSnapshot).

    struct Toolbar190qgCase
    {
        lynx::layoutstyle::Style style;
        juce::String name;
        lynx::toolbarlayout::Layout layout;
    };

    static std::vector<Toolbar190qgCase> toolbar190qgCases (bool pictures)
    {
        namespace tl = lynx::toolbarlayout;
        using lynx::layoutstyle::Style;
        std::vector<Toolbar190qgCase> cases;
        const auto base = tl::Layout::makeDefault();
        for (int p = 0; p < tl::presetCount; ++p)
        {
            auto l = tl::applyPreset (base, (tl::Preset) p);
            const juce::String id[] { "default", "compact", "minimal", "mixing", "flclassic" };
            cases.push_back ({ Style::pattern, id[p], l });
            if ((tl::Preset) p == tl::Preset::standard || (tl::Preset) p == tl::Preset::flClassic)
            {
                l.grouped = true;
                cases.push_back ({ Style::pattern, id[p] + "-grouped", l });
            }
        }
        if (! pictures)
        {
            // every panel shown (Multilink, Key and Link too), two rows and one
            auto all = base;
            for (auto& e : all.entries)
                e.shown = true;
            cases.push_back ({ Style::pattern, "everything", all });
            all.oneRow = true;
            cases.push_back ({ Style::pattern, "everything-onerow", all });
            for (auto style : { Style::simple, Style::clipLaunch })
            {
                auto shown = all;
                shown.oneRow = false;
                cases.push_back ({ style, style == Style::simple ? "simple-everything" : "cliplaunch-everything", shown });
                cases.push_back ({ style, style == Style::simple ? "simple-default" : "cliplaunch-default", base });
            }
        }
        return cases;
    }

    // the logical window widths for screens of these widths at these Interface sizes
    static std::vector<int> toolbar190qgWidths()
    {
        std::vector<int> out;
        for (auto screen : { 1024, 1280, 1366, 1440, 1600, 1920, 2560, 3840 })
            for (auto scale : lynx::toolbarlayout::uiScales)
                if (const auto w = juce::roundToInt (screen * 100.0 / scale); w >= 800 && std::find (out.begin(), out.end(), w) == out.end())
                    out.push_back (w);
        std::sort (out.begin(), out.end());
        return out;
    }

    // "OK" or "ERROR <first problems>"; `log` gets a line per layout that failed and the totals
    juce::String toolbar190qgWalk (lynx::MainComponent& ui, lynx::AudioEngine& engine, juce::String& log)
    {
        namespace tl = lynx::toolbarlayout;
        int layouts = 0, failed = 0, withOverflow = 0;
        juce::String first;
        const auto before = ui.getToolbarLayoutForSnapshot();
        const auto widths = toolbar190qgWidths();
        for (const auto& c : toolbar190qgCases (false))
        {
            ui.setLayoutStyle (c.style, false);
            engine.dispatchPendingMessages();
            for (auto size : { tl::IconSize::small, tl::IconSize::medium, tl::IconSize::large })
            {
                auto layout = c.layout;
                layout.iconSize = size;
                ui.setToolbarLayoutForSnapshot (layout);
                for (auto w : widths)
                {
                    ui.setSize (w, 900);
                    ++layouts;
                    if (! ui.getToolbarOverflowForSnapshot().empty())
                        ++withOverflow;
                    if (const auto problems = ui.checkToolbarItemsForSnapshot(); problems.isNotEmpty())
                    {
                        ++failed;
                        const auto line = c.name + " " + tl::Layout::iconSizeId (size) + " " + juce::String (w) + " px: " + problems.replace ("\n", "; ");
                        log << line << "\n";
                        if (first.isEmpty())
                            first = line;
                    }
                }
            }
        }
        ui.setLayoutStyle (lynx::layoutstyle::Style::pattern, false);
        ui.setToolbarLayoutForSnapshot (before);
        engine.dispatchPendingMessages();
        log << "layouts=" << layouts << " failed=" << failed << " with_overflow=" << withOverflow << " widths=" << (int) widths.size() << "\n";
        return failed == 0 && layouts > 0 ? juce::String ("OK") : "ERROR " + juce::String (failed) + " of " + juce::String (layouts) + ": " + first.substring (0, 300);
    }

    // --selftest's item: the walk on a fresh main window
    juce::String toolbar190qgCheck (juce::String& log)
    {
        auto& lookBefore = juce::LookAndFeel::getDefaultLookAndFeel();
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        juce::String result;
        {
            lynx::AudioEngine offline (false);
            lynx::MainComponent ui (offline, false);
            ui.setSize (1920, 1080);
            result = toolbar190qgWalk (ui, offline, log);
        }
        juce::LookAndFeel::setDefaultLookAndFeel (&lookBefore);
        return result;
    }

    // LYNX --toolbarshot190qg <prefix> [project]: the walk's report (<prefix>-walk.txt), then the toolbar of every preset
    // (and the Grouped look) at 80 / 100 / 125 / 150 / 175 % on a 1280 px and a 1920 px screen (the picture at the screen's
    // pixels), the overflow menus, and the bounds of each item. Exit code 0 = the walk passed.
    bool runToolbarShot190qg (const juce::String& prefix, const juce::String& projectPath)
    {
        namespace tl = lynx::toolbarlayout;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        lynx::ui::motion::enabled() = false;
        lynx::AudioEngine offline (false);
        lynx::MainComponent ui (offline, false);
        ui.setSize (1920, 1080);
        if (projectPath.isNotEmpty())
        {
            ui.openFile (juce::File (projectPath));
            offline.dispatchPendingMessages();
        }
        ui.selectTabForSnapshot (2);
        juce::String report, walkLog;
        const auto walk = toolbar190qgWalk (ui, offline, walkLog);
        juce::File (prefix + "-walk.txt").replaceWithText ("walk=" + walk + "\n" + walkLog);

        const auto write = [&prefix] (const juce::Image& image, const juce::String& name) {
            const auto file = juce::File (prefix + "-" + name + ".png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (image, out);
        };
        const auto bar = [&ui] { return ui.getLocalBounds().withHeight (juce::jmin (ui.getHeight(), 96)); };
        for (const auto& c : toolbar190qgCases (true))
        {
            ui.setToolbarLayoutForSnapshot (c.layout);
            for (auto screen : { 1280, 1920 })
                for (auto scale : { 80, 100, 125, 150, 175 })
                {
                    const auto w = juce::roundToInt (screen * 100.0 / scale);
                    if (w < 800)
                        continue;
                    ui.setSize (w, 900);
                    lynx::shot::scaleOverride() = (float) scale / 100.0f;
                    const auto name = c.name + "-" + juce::String (screen) + "-" + juce::String (scale);
                    write (lynx::shot::snapshot (ui, bar()), name);
                    lynx::shot::scaleOverride() = 0.0f;
                    report << "== " << name << " (" << w << " x " << ui.getHeight() << " logical)\n";
                    juce::StringArray items;
                    for (int i = 0; i < tl::itemCount; ++i)
                        if (auto* comp = ui.toolbarPartForSnapshot (juce::String ("190qg:") + tl::itemId ((tl::Item) i)); comp != nullptr && comp->isVisible())
                            items.add (juce::String (tl::itemId ((tl::Item) i)) + " " + comp->getBounds().toString());
                    report << "  bar: " << items.joinIntoString ("; ") << "\n";
                    juce::StringArray over;
                    for (auto i : ui.getToolbarOverflowForSnapshot())
                        over.add (tl::itemId (i));
                    report << "  overflow: " << over.joinIntoString (", ") << "\n";
                    if (! ui.getToolbarOverflowForSnapshot().empty() && scale == 100)
                    {
                        lynx::shot::scaleOverride() = 1.0f;
                        saveMenus (juce::StringArray { "Overflow" }, std::vector<juce::PopupMenu> { ui.makeToolbarOverflowMenuForSnapshot() },
                                   prefix + "-" + name + "-menu.png");
                        lynx::shot::scaleOverride() = 1.5f;
                        saveMenus (juce::StringArray { "Overflow" }, std::vector<juce::PopupMenu> { ui.makeToolbarOverflowMenuForSnapshot() },
                                   prefix + "-" + name + "-menu@150.png");
                        lynx::shot::scaleOverride() = 0.0f;
                    }
                }
        }
        // the overflow button hovered (1280 px, 125 %: the default preset sends panels there)
        {
            ui.setToolbarLayoutForSnapshot (tl::Layout::makeDefault());
            ui.setSize (1024, 900);
            if (auto* b = dynamic_cast<juce::Button*> (&ui.overflowButtonForSnapshot()); b != nullptr && b->isVisible())
            {
                b->setState (juce::Button::buttonOver);
                const auto area = b->getBounds().expanded (60, 4).withY (0).withHeight (ui.getHeight() >= 96 ? 96 : ui.getHeight());
                lynx::shot::scaleOverride() = 3.0f;
                write (lynx::shot::snapshot (ui, area.getIntersection (ui.getLocalBounds())), "overflow-hover-z3");
                lynx::shot::scaleOverride() = 0.0f;
                b->setState (juce::Button::buttonNormal);
            }
        }
        // the Simple and Clip-launch bars at a narrow window with their "⋯" menus (what they left out is listed first)
        for (auto style : { lynx::layoutstyle::Style::simple, lynx::layoutstyle::Style::clipLaunch })
        {
            auto all = tl::Layout::makeDefault();
            for (auto& e : all.entries)
                e.shown = true;
            ui.setLayoutStyle (style, false);
            offline.dispatchPendingMessages();
            ui.setToolbarLayoutForSnapshot (all);
            ui.setSize (1024, 900);
            const auto n = juce::String (style == lynx::layoutstyle::Style::simple ? "simple" : "cliplaunch");
            write (lynx::shot::snapshot (ui, bar()), n + "-1024");
            saveMenus (juce::StringArray { "More" }, std::vector<juce::PopupMenu> { ui.makeToolbarMoreMenuForSnapshot() }, prefix + "-" + n + "-1024-more.png");
        }
        ui.setLayoutStyle (lynx::layoutstyle::Style::pattern, false);
        ui.setToolbarLayoutForSnapshot (tl::Layout::makeDefault());
        juce::File (prefix + "-report.txt").replaceWithText (report);
        return walk == "OK";
    }

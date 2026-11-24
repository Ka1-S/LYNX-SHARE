    // --polish190qwshot <prefix> [project] (round 190qw, design polish pass + the Soft look, queue item 67): the Soft look's
    // checks (off by default, saved in the theme and read back, the Channel rack's 26 px rows, the plugin header's 36 px),
    // then every main view of the three layouts (the --design190ojshot set, 100 % and @150) in the default look
    // (<prefix>-default-*) and in the Soft look (<prefix>-soft-*), and the Settings pages in the Soft look
    // (<prefix>-soft-settings-*). <prefix>-checks.txt ends with result=OK / FAIL. Included in the app class of Main.cpp.
    bool runPolish190qwShot (const juce::String& prefix, const juce::String& projectPath)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        juce::StringArray checks;
        bool allOk = true;
        const auto check = [&checks, &allOk] (const juce::String& what, bool ok) {
            checks.add ((ok ? "ok   " : "FAIL ") + what);
            allOk = allOk && ok;
        };

        const auto start = themeui::current();
        check ("the Soft look is off by default (a new theme, and the theme in use at start)", ! themes::Spec {}.soft && ! start.soft && ! theme::soft());
        check ("the default theme is the default look; a Soft one is not", themes::Spec {}.isDefaultLook() && ! [] { themes::Spec s; s.soft = true; return s.isDefaultLook(); }());
        {
            auto soft = start;
            soft.soft = true;
            const auto json = themeui::toJson (soft);
            const auto back = themeui::fromJson (json);
            check ("a Soft theme is saved as \"look\": \"soft\" and read back", json.contains ("\"look\": \"soft\"") && back.has_value() && back->soft);
            const auto plain = themeui::fromJson (themeui::toJson (start));
            check ("a theme saved without it (every older .lynxtheme) reads as the default look", plain.has_value() && ! plain->soft && ! themeui::toJson (start).contains ("look"));
        }
        check ("Settings > LYNX extras lists the Soft look once",
               std::count_if (std::begin (extras::items), std::end (extras::items), [] (const extras::Item& i) { return i.id == "theme.softLook"; }) == 1);

        {
            AudioEngine offline (false);
            ChannelRackComponent rack (offline);
            const auto before = ChannelRackComponent::rowHeight;
            auto soft = start;
            soft.soft = true;
            themeui::apply (soft, false);
            rack.sendLookAndFeelChange();
            const auto softRows = ChannelRackComponent::rowHeight;
            const auto softTile = theme::paletteStampNow();
            themeui::apply (start, false);
            rack.sendLookAndFeelChange();
            check ("Channel rack rows: " + juce::String (before) + " px, Soft " + juce::String (softRows) + " px, back to "
                       + juce::String (ChannelRackComponent::rowHeight) + " px",
                   before == 30 && softRows == 26 && ChannelRackComponent::rowHeight == 30);
            check ("switching the look renews the palette stamp (the cached steps and plates are drawn again)", theme::paletteStampNow() != softTile);
            check ("Soft: user colours muted, the default look leaves them", theme::softened (juce::Colour (0xffe03020)) == juce::Colour (0xffe03020));
            soft.soft = true;
            themeui::apply (soft, false);
            const auto muted = theme::softened (juce::Colour (0xffe03020));
            check ("Soft: #e03020 becomes #" + muted.toDisplayString (false) + " (saturation " + juce::String (muted.getSaturation(), 2) + ", brightness "
                       + juce::String (muted.getBrightness(), 2) + ")",
                   muted.getBrightness() <= 0.505f && muted.getSaturation() < juce::Colour (0xffe03020).getSaturation());
            themeui::apply (start, false);
        }

        const auto defaultOk = runDesign190ojShot (prefix + "-default", projectPath);
        check ("the default look's views (--design190ojshot set)", defaultOk);
        {
            auto soft = start;
            soft.soft = true;
            themeui::apply (soft, false);
        }
        const auto softOk = runDesign190ojShot (prefix + "-soft", projectPath);
        check ("the Soft look's views (--design190ojshot set)", softOk);
        runSettingsShot (prefix + "-soft");
        themeui::apply (start, false);

        juce::File (prefix + "-checks.txt").replaceWithText (checks.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }

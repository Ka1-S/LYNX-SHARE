    // Rollup 0.1.31 step 7 (PRs #248 / #256 by FKHR26): LYNX --headergearshot248 <prefix> [--shot-scale 1.5 --shot-suffix @150]
    // pictures the docked channel window of a Sampler channel with its header shown (the default) and hidden by the gear
    // in its title bar, and checks that the choice is kept for the window; <prefix>-checks.txt lists the checks (result=OK).
    // Included in the app class of Main.cpp (savePng).
    bool runHeaderGearShot (const juce::String& prefix)
    {
        using namespace lynx;
        juce::StringArray lines;
        bool ok = true;
        const auto check = [&] (const juce::String& name, bool pass) {
            lines.add ((pass ? "ok " : "FAIL ") + name);
            ok = ok && pass;
        };
        AudioEngine engine (false);
        windowplaces::fromVar ({});
        const auto write = [&] (juce::Component& c, const juce::String& name) {
            savePng (shot::snapshot (c), shot::fileName (prefix + "-" + name + ".png"));
        };
        {
            auto w = makeChannelWindowForShot (engine, 0, 0);
            check ("a channel window", w != nullptr);
            if (w != nullptr)
            {
                const auto before = w->getHeight();
                check ("header shown by default", channelWindowHeaderShown (*w) == std::optional<bool> (true));
                write (*w, "01-header-shown");
                check ("the gear in the title bar", pressChannelHeaderGear (*w));
                check ("header hidden by the gear", channelWindowHeaderShown (*w) == std::optional<bool> (false));
                check ("the window is shorter by the header", w->getHeight() < before);
                write (*w, "02-header-hidden");
            }
        }
        {
            auto w = makeChannelWindowForShot (engine, 0, 0);
            check ("opened again: still hidden (kept for the window)", w != nullptr && channelWindowHeaderShown (*w) == std::optional<bool> (false));
            if (w != nullptr)
            {
                write (*w, "03-opened-again");
                check ("the gear shows it again", pressChannelHeaderGear (*w) && channelWindowHeaderShown (*w) == std::optional<bool> (true));
            }
        }
        windowplaces::fromVar ({});
        lines.add (juce::String ("result=") + (ok ? "OK" : "FAIL"));
        juce::File (shot::fileName (prefix + "-checks.txt")).replaceWithText (lines.joinIntoString ("\n") + "\n");
        return ok;
    }

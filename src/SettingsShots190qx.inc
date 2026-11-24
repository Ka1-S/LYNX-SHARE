    // --settingsshots190qx <prefix> (round 190qx): every Settings page as the window opens it (720 x 700), at 100 % and @150,
    // the same framing in the round's build and in the base build (the before / after pairs). Included in the app class.
    void runSettingsShots190qx (const juce::String& prefix)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        lynx::AudioEngine engine (false);
        lynx::GeneralOptions general;
        general.language = "en";
        lynx::SettingsHooks hooks;
        hooks.getGeneral = [&general] { return general; };
        hooks.setGeneral = [&general] (const lynx::GeneralOptions& o) { general = o; };
        const std::pair<lynx::SettingsPage, const char*> pages[] = { { lynx::SettingsPage::midi, "midi" },       { lynx::SettingsPage::audio, "audio" },
                                                                     { lynx::SettingsPage::general, "general" }, { lynx::SettingsPage::files, "files" },
                                                                     { lynx::SettingsPage::theme, "theme" },     { lynx::SettingsPage::project, "project" },
                                                                     { lynx::SettingsPage::info, "info" },       { lynx::SettingsPage::link, "link" },
                                                                     { lynx::SettingsPage::extras, "extras" } };
        for (const auto& [page, name] : pages)
        {
            auto panel = lynx::makeSettingsPanel (engine, page, false, hooks);
            panel->setSize (720, 700);
            for (const float scale : { 1.0f, 1.5f })
                savePng (panel->createComponentSnapshot (panel->getLocalBounds(), true, scale),
                         prefix + "-" + name + (scale > 1.0f ? "@150" : "") + ".png");
        }
    }

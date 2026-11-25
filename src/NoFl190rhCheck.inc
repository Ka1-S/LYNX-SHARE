    // Round 190rh (QUEUE-0.1.33 item 78): the FL natives without FL Studio. Every engine that needs nothing is made with no
    // installation, saved as .lynx and opened again with the same state and the same data set (core/NativeData.h); a copy
    // for an older release says which channels it plays silent. Included in the app class (--selftest).
    static juce::String noFl190rhCheck (juce::String& log)
    {
        using namespace lynx;
        Song song = Song::makeDefault();
        song.channels.clear();
        int ownSets = 0;
        for (const auto& e : natives::instrumentEngines())
        {
            const auto c = natives::makeChannel (e.kind, {});
            if (c.has_value() != natives::available (e, {}))
                return juce::String ("ERROR ") + e.name + ": listed as available without FL but not made, or the reverse";
            if (! c)
            {
                log << e.name << " needs FL\n";
                continue;
            }
            ownSets += nativedata::isOwn (c->samplePath) ? 1 : 0;
            song.channels.push_back (*c);
        }
        for (const auto& e : natives::effectEngines())
            if (const auto fx = natives::makeSlot (e.kind, {}))
                song.mixer[1].effects.push_back (*fx);
        juce::String error;
        const auto back = ProjectFile::fromJson (ProjectFile::toJson (song), error);
        if (! back || back->channels.size() != song.channels.size() || back->mixer[1].effects.size() != song.mixer[1].effects.size())
            return "ERROR the project with every engine did not open again (" + error + ")";
        for (std::size_t i = 0; i < song.channels.size(); ++i)
        {
            const auto &a = song.channels[i], &b = back->channels[i];
            if (a.instrument != b.instrument || a.pluginState != b.pluginState || a.samplePath != b.samplePath || a.modelParams != b.modelParams)
                return "ERROR " + juce::String (a.name) + ": not the same after saving and opening again";
        }
        for (std::size_t i = 0; i < song.mixer[1].effects.size(); ++i)
            if (song.mixer[1].effects[i].kind != back->mixer[1].effects[i].kind || song.mixer[1].effects[i].state != back->mixer[1].effects[i].state)
                return "ERROR an effect is not the same after saving and opening again";
        int lines = 0;
        for (const auto& loss : olderlynx::forRelease (song, "0.1.31").lost)
            lines += juce::String (loss.text).contains ("own data set") ? 1 : 0;
        log << (int) song.channels.size() << " instruments, " << (int) song.mixer[1].effects.size() << " effects, " << ownSets << " own data sets\n";
        if (ownSets == 0 || lines != 1)
            return "ERROR a copy for 0.1.31 does not say which channels play LYNX's own data set";
        // a shared project: the reserved path is no file of the sender's
        auto shared = song;
        sessionpack::confine (shared, juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-nofl190rh"), {});
        for (std::size_t i = 0; i < song.channels.size(); ++i)
            if (nativedata::isOwn (song.channels[i].samplePath) && shared.channels[i].samplePath != song.channels[i].samplePath)
                return "ERROR a shared project lost a channel's data set";
        return "OK";
    }

    // Settings › Files › FL Studio folder (issue #291): the setting is used, LYNX_FL_INSTALL wins over it, a folder that is
    // gone is passed over and kept. Only on a profile of the test's own.
    static juce::String flFolder190rhCheck (juce::String& log)
    {
        using namespace lynx;
        if (juce::SystemStats::getEnvironmentVariable ("LYNX_PROFILE_DIR", {}).isEmpty())
        {
            log << "skipped (no LYNX_PROFILE_DIR)\n";
            return "OK";
        }
        if (juce::SystemStats::getEnvironmentVariable ("LYNX_NO_FL", {}).isNotEmpty())
        {
            log << "skipped (LYNX_NO_FL)\n";
            return "OK";
        }
        const auto before = flFolderSetting();
        const auto install = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("lynx-flfolder190rh", {}, false);
        install.getChildFile ("Data").createDirectory();
        install.getChildFile ("Plugins/Fruity").createDirectory();
        const auto done = [&] (const juce::String& result) {
            setFlFolderSetting (before.isNotEmpty() ? juce::File (before) : juce::File());
            install.deleteRecursively();
            return result;
        };
        if (! setFlFolderSetting (install) || flFolderSetting() != install.getFullPathName())
            return done ("ERROR the FL Studio folder was not saved");
        const auto found = findFlStudio();
        if (juce::File::isAbsolutePath (flFolderVariable()) && juce::File (flFolderVariable()).getChildFile ("Data").isDirectory())
        {
            log << "LYNX_FL_INSTALL is set: it must win\n";
            if (found.source != FlStudioInstall::Source::variable || found.installDir != juce::File (flFolderVariable()))
                return done ("ERROR the setting won over LYNX_FL_INSTALL");
        }
        else if (found.source != FlStudioInstall::Source::setting || found.installDir != install
                 || flStudioAccess().roots.factoryData != install.getFullPathName().toStdString() || nativedata::factoryData() != flStudioAccess().roots.factoryData)
            return done ("ERROR the FL Studio folder was not used without a restart");
        install.deleteRecursively();
        if (findFlStudio().installDir == install || flFolderSetting() != install.getFullPathName())
            return done ("ERROR a folder that is gone was used, or forgotten");
        return done ("OK");
    }

    // --nofl190rhshot <prefix>: Settings › Files with the real FL Studio folder row (the profile's setting, this computer's
    // disk), at 100 % and 150 %, once the folder has been read
    void runNoFl190rhShot (const juce::String& prefix)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        auto engine = std::make_shared<AudioEngine> (false);
        std::shared_ptr<juce::Component> panel (makeSettingsPanel (*engine, SettingsPage::files, true, {}).release());
        panel->setSize (720, 700);
        settingsShowPageForSnapshot (*panel, SettingsPage::files);
        savePng (panel->createComponentSnapshot (panel->getLocalBounds(), true, 1.0f), prefix + "-reading.png");
        juce::Timer::callAfterDelay (juce::SystemStats::getEnvironmentVariable ("LYNX_SHOT_WAIT_MS", "6000").getIntValue(), [engine, panel, prefix] {
            savePng (panel->createComponentSnapshot (panel->getLocalBounds(), true, 1.0f), prefix + ".png");
            savePng (panel->createComponentSnapshot (panel->getLocalBounds(), true, 1.5f), prefix + "@150.png");
            quit();
        });
    }

    // --nofl190rhlynx <in.flp> <release> <out.lynx>: the project as that release opens it, every generator's state LYNX's own
    // to write (no FL original kept beside it): what an older LYNX then exports as .flp is checked in FL
    bool runNoFl190rhLynx (const juce::String& input, const juce::String& release, const juce::String& output)
    {
        using namespace lynx;
        AudioEngine loader (false);
        juce::String error;
        if (! loader.importFlpFile (juce::File (input), error))
            return false;
        auto song = loader.getSong();
        for (auto& c : song.channels)
            c.flOrigin = {};
        const auto copy = olderlynx::forRelease (song, release);
        return copy.ok && juce::File (output).replaceWithText (copy.json, false, false, nullptr);
    }

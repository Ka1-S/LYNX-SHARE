    // Round 190rp (0.1.32 Q4, stability): the recovery box's choice of plugins (decision O-2), the audio device lost and
    // back (O-4), the crash-recovery round trip (FIX-LIST item 3) and the sample-0 start with latency (item 8 / TN2-1).
    // Included in the app class of Main.cpp; LYNX --recoverytest report.txt; --selftest "stab190rp".
    juce::String stab190rpCheck (juce::String& log)
    {
        using namespace lynx;
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        juce::String result = "OK";
        const auto check = [&] (const juce::String& what, bool ok) {
            log << (ok ? "ok   " : "FAIL ") << what << "\n";
            if (! ok && result == "OK")
                result = "ERROR " + what;
        };

        // the message loop's part (the box's button, its callback, the open that follows: async messages)
        const auto pump = [] (int ms, const std::function<bool()>& done) {
            const auto start = juce::Time::getMillisecondCounter();
            while (juce::Time::getMillisecondCounter() - start < (juce::uint32) ms)
            {
                for (int i = 0; i < 256 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i) {}
                if (done && done())
                    return;
                juce::Thread::sleep (5);
            }
        };

        // ---- the recovery box: the backup of a session that ended while a hosted plugin's window was open opens without
        //      third-party plugins by default (Return), the plugin kept off in the project; without a plugin named,
        //      Return opens it as before
        for (const bool named : { true, false })
        {
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            MainComponent mainUi (offline, false);
            mainUi.setSize (1600, 900);
            const auto folder = autosaveFolder();
            folder.createDirectory();
            auto song = Song::makeDefault();
            song.channels[0].instrument = InstrumentKind::plugin;
            song.channels[0].plugin.id = "VST3-LYNX Recovery Crash-190rp-0";
            song.channels[0].plugin.name = "Crashy Synth 190rp";
            song.channels[0].name = "Crashy Synth 190rp";
            const auto backup = folder.getChildFile ("recovered 2026-11-26 07-00-0" + juce::String (named ? 1 : 2) + ProjectFile::extension);
            juce::String error;
            const auto saved = ProjectFile::save (song, backup, error);
            check ("recovery box: backup written " + error, saved);
            mainUi.setRecoveryPlugin (named ? "Crashy Synth 190rp" : "");
            juce::StringArray buttons;
            const auto text = mainUi.recoveryBoxForTest (backup, buttons);
            offline.markSaved(); // (nothing to save: the box's choice opens at once, no "save changes?" first)
            mainUi.checkPreviousSessionForTest (juce::Time::getCurrentTime() - juce::RelativeTime::seconds (30));
            pump (500, [] { return juce::Component::getCurrentlyModalComponent() != nullptr; });
            auto* box = dynamic_cast<juce::AlertWindow*> (juce::Component::getCurrentlyModalComponent());
            check ("recovery box shown (" + juce::String (named ? "plugin named" : "no plugin") + ")", box != nullptr);
            if (box != nullptr)
            {
                check ("recovery box: 4 buttons, the default first: " + (box->getNumButtons() > 0 ? box->getButton (0)->getButtonText() : juce::String()),
                       box->getNumButtons() == 4 && box->getButton (0)->getButtonText() == buttons[0]
                           && buttons[0] == (named ? lynx::tr ("Open backup without third-party plugins") : lynx::tr ("Open backup")));
                check ("recovery box names the plugin", text.contains ("Crashy Synth 190rp") == named);
                static_cast<juce::Component*> (box)->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                pump (5000, [&offline] { return ! offline.getSong().channels.empty() && offline.getSong().channels[0].name == "Crashy Synth 190rp"; });
                pump (200, {});
                const auto uses = offline.hostedPluginUses();
                const auto off = std::any_of (uses.begin(), uses.end(), [] (const pluginhold::Use& u) { return u.name == "Crashy Synth 190rp" && u.off; });
                const auto opened = ! offline.getSong().channels.empty() && offline.getSong().channels[0].name == "Crashy Synth 190rp";
                check ("Return opened the backup " + juce::String (named ? "without third-party plugins (kept, off)" : "as before") + " (opened "
                           + juce::String (opened ? "yes" : "no") + ", plugins off " + juce::String (offline.pluginsOff() ? "yes" : "no") + ")",
                       opened && (named ? (offline.pluginsOff() && off) : ! offline.pluginsOff()));
            }
            while (auto* modal = juce::Component::getCurrentlyModalComponent())
                modal->exitModalState (0);
            pump (200, {});
            backup.deleteFile();
        }

        // ---- the audio device in use unplugged and plugged in again (stand-in devices)
        {
            const auto r = AudioEngine::deviceLostSelfTest();
            check ("device lost: " + r, r.startsWith ("OK"));
        }
        // ---- the crash-recovery round trip (children of this executable, ended on purpose)
        {
            juce::String detail;
            const auto r = recoveryRoundTripSelfTest (detail);
            log << detail;
            check ("recovery round trip: " + r, r.startsWith ("OK") || r.startsWith ("SKIPPED"));
        }
        // ---- a burst at tick 0 through latency starts at sample 0
        {
            const auto r = AudioEngine::sampleZeroSelfTest ([&log] (const juce::String& line) { log << "     " << line << "\n"; });
            check ("sample 0: " + r, r.startsWith ("OK"));
        }
        return result;
    }

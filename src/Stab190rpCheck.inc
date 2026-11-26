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

        // ---- BUGS 13 (the Linux UI monkey: "Freezing ..." cancelled, LYNX aborted): a freeze's thread cancelled while the
        //      message thread waits for it, as a progress window's Cancel does (stopThread, no messages handled), must
        //      end by itself; it used to wait for the message thread (its render job made / freed there) until JUCE
        //      killed it after 10 s (glibc then aborts). Three times: cancelled at once, after 30 ms and after 200 ms
        for (const int afterMs : { 0, 30, 200 })
        {
            struct FreezeWorker final : juce::Thread
            {
                FreezeWorker() : juce::Thread ("stab190rp freeze") {}
                void run() override
                {
                    auto song = Song::makeDefault();
                    for (int k = 0; k < 64; ++k)
                        song.patterns[0].notes.push_back ({ (std::int64_t) k * ppq / 4, ppq / 8, 4, 48 + k % 24, 0.8f });
                    song.patterns[0].sortNotes();
                    folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-stab190rp-freeze-" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
                    folder.createDirectory();
                    std::vector<StandIn> stems;
                    juce::String error;
                    sessionpack::renderFreeze (song, 4, 48000.0, folder, stems, [this] (double) { return ! threadShouldExit(); }, error);
                }
                juce::File folder;
            } worker;
            worker.startThread();
            if (afterMs > 0)
                juce::Thread::sleep (afterMs);
            worker.signalThreadShouldExit();
            const auto t0 = juce::Time::getMillisecondCounter();
            const auto ended = worker.waitForThreadToExit (8000); // (the message thread handles nothing meanwhile)
            const auto ms = juce::Time::getMillisecondCounter() - t0;
            if (! ended) // (the old code: let it finish now, so the check itself ends)
                pump (15000, [&worker] { return ! worker.isThreadRunning(); });
            pump (200, {});
            worker.folder.deleteRecursively();
            check ("a cancelled freeze ends while the message thread waits for it (cancelled after " + juce::String (afterMs) + " ms: "
                       + (ended ? "ended in " + juce::String ((int) ms) + " ms" : juce::String ("still waiting after 8 s")) + ")",
                   ended);
        }

        // ---- crash group G1: a device whose driver never returns from its open (engine/DeviceGuard.h, the test type):
        //      the open is given up after the time limit and the default device ("Answers") opens; a second open of it
        //      fails at once; "Try again" makes one more bounded attempt; the manager is deleted at once (a quit does not
        //      wait for the stuck driver)
        {
            deviceguard::setTimeoutForChecks (1500);
            const auto limit = deviceguard::timeoutMs();
            const auto before = deviceguard::hungCount();
            auto devices = std::make_unique<juce::AudioDeviceManager>();
            devices->addAudioDeviceType (deviceguard::wrap (deviceguard::makeHangTestType (true)));
            juce::XmlElement saved ("DEVICESETUP");
            saved.setAttribute ("deviceType", deviceguard::hangTestTypeName);
            saved.setAttribute ("audioOutputDeviceName", "Hangs on open");
            auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto error = devices->initialise (0, 2, &saved, true);
            const auto first = juce::Time::getMillisecondCounterHiRes() - t0;
            auto* now = devices->getCurrentAudioDevice();
            check ("device hang: the start gave up after " + juce::String (first / 1000.0, 1) + " s (limit " + juce::String (limit / 1000.0, 1)
                       + " s) and opened \"" + (now != nullptr ? now->getName() : juce::String ("none")) + "\" (" + error + ")",
                   first < limit + 3000.0 && now != nullptr && now->getName() == "Answers" && deviceguard::hungCount() == before + 1
                       && deviceguard::lastHung() == juce::String (deviceguard::hangTestTypeName) + ": Hangs on open");
            auto setup = devices->getAudioDeviceSetup();
            setup.outputDeviceName = "Hangs on open";
            t0 = juce::Time::getMillisecondCounterHiRes();
            const auto again = devices->setAudioDeviceSetup (setup, true);
            const auto second = juce::Time::getMillisecondCounterHiRes() - t0;
            check ("device hang: choosing it again fails at once (" + juce::String (second, 0) + " ms: " + again + ")", second < 1000.0 && again.isNotEmpty());
            deviceguard::allowAgain (deviceguard::hangTestTypeName, "Hangs on open");
            t0 = juce::Time::getMillisecondCounterHiRes();
            devices->setAudioDeviceSetup (setup, true);
            const auto third = juce::Time::getMillisecondCounterHiRes() - t0;
            check ("device hang: \"Try again\" waits once more, bounded (" + juce::String (third / 1000.0, 1) + " s)",
                   third >= limit * 0.8 && third < limit + 3000.0 && deviceguard::hungCount() == before + 2);
            t0 = juce::Time::getMillisecondCounterHiRes();
            devices.reset();
            const auto gone = juce::Time::getMillisecondCounterHiRes() - t0;
            check ("device hang: the device manager is deleted at once (" + juce::String (gone, 0) + " ms)", gone < 2000.0);
            deviceguard::setTimeoutForChecks (0);
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

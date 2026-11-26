// Round 190rp (0.1.32 Q4, stability): one test per defect found and fixed, and the new logic's rules.

// Killed (or a power cut) while an autosave wrote its file: the half-written temporary file ("..._temp<hex>.lynx", the
// newest file of the session) was the backup the next start offered, and it opened broken or not at all.
TEST (stab190rp_recovery_never_offers_a_half_written_save)
{
    using namespace lynx::safety;
    CHECK (isTemporarySaveName ("Song (autosaved on 2026-11-26 at 07h01)_temp1a2b3c4d"));
    CHECK (isTemporarySaveName ("recovered 2026-11-26 07-00-00_temp7"));
    // (File::replaceWithText's own hidden temporary inside the save's: what a kill leaves most often, 0 bytes)
    CHECK (isTemporarySaveName (".Song (autosaved on 2026-11-26 at 07h11)_temp19ebccc7_temp5a981c96"));
    CHECK (! isTemporarySaveName ("Song (autosaved on 2026-11-26 at 07h01)"));
    CHECK (! isTemporarySaveName ("Song (autosaved on 2026-11-26 at 07h01)_2"));
    CHECK (! isTemporarySaveName ("My_temp"));          // (no digits)
    CHECK (! isTemporarySaveName ("Song_temp123456789")); // (more than 8)
    CHECK (! isTemporarySaveName ("Song_tempo"));
    const std::vector<BackupFile> files { { "Beat (autosaved on 2026-11-26 at 07h00)", 100 },
                                          { "Beat (autosaved on 2026-11-26 at 07h02)_tempdeadbeef", 130 },
                                          { "recovered 2026-11-26 07-01-00_temp3f", 140 },
                                          { ".Beat (autosaved on 2026-11-26 at 07h03)_temp19ebccc7_temp5a981c96", 150 } };
    CHECK (crashRecoveryCandidate (files, 50) == "Beat (autosaved on 2026-11-26 at 07h00)");
    CHECK (crashRecoveryCandidate (files, 101).empty()); // (only half-written files since then: nothing to offer)
}

// The audio device in use went away (decision O-4): JUCE opens the default device (or none) in its place; LYNX must
// tell a loss from a switch, reopen the lost one when it is listed again, and keep a device chosen meanwhile.
TEST (stab190rp_device_loss_rules)
{
    using namespace lynx::deviceloss;
    std::set<std::string> listed { "Interface", "Speakers" };
    auto isListed = [&] (const Device& d) { return listed.count (d.name) > 0; };
    Watch w;
    CHECK (w.update ({ "ALSA", "Interface" }, isListed) == Event::none);
    // a switch in Settings › Audio (the old one still there): not a loss
    CHECK (w.update ({ "ALSA", "Speakers" }, isListed) == Event::none && w.current().name == "Speakers");
    CHECK (w.update ({ "ALSA", "Interface" }, isListed) == Event::none);
    // unplugged: JUCE opened the speakers instead
    listed.erase ("Interface");
    CHECK (w.update ({ "ALSA", "Speakers" }, isListed) == Event::lost);
    CHECK (w.lost().name == "Interface" && w.fallback().name == "Speakers");
    CHECK (w.update ({ "ALSA", "Speakers" }, isListed) == Event::none); // (still away)
    listed.insert ("Interface");
    CHECK (w.update ({ "ALSA", "Speakers" }, isListed) == Event::reopen);
    w.reopened ({ "ALSA", "Interface" });
    CHECK (w.lost().empty() && w.current().name == "Interface");
    // unplugged with nothing left, then another device chosen before it is back: that choice stays
    listed.erase ("Interface");
    CHECK (w.update ({}, isListed) == Event::lost && w.fallback().empty());
    CHECK (w.update ({ "ALSA", "Speakers" }, isListed) == Event::none && w.lost().empty());
    listed.insert ("Interface");
    CHECK (w.update ({ "ALSA", "Speakers" }, isListed) == Event::none);
    // a device that never opened (none at the start) is no loss
    Watch fresh;
    CHECK (fresh.update ({}, isListed) == Event::none);
}

// The recovery box (decision O-2 (a)): "Open backup without third-party plugins" always there, the default (Return)
// when the session ended while a plugin's window was open; the plugin's name comes from Main.cpp's uncleanCause.
TEST (stab190rp_recovery_box_offers_plugins_off)
{
    const auto main = i18ntest::readFile ("src/ui/MainComponent.cpp");
    const auto box = main.find ("void MainComponent::checkPreviousSession (juce::Time sessionStart)");
    const auto end = main.find ("juce::String MainComponent::recoveryBoxForTest", box);
    CHECK (box != std::string::npos && end != std::string::npos);
    const auto body = main.substr (box, end - box);
    CHECK (body.find ("w->addButton (tr (\"Open backup without third-party plugins\"), 3, juce::KeyPress (juce::KeyPress::returnKey));") != std::string::npos);
    CHECK (body.find ("w->addButton (tr (\"Open backup without third-party plugins\"), 3);") != std::string::npos);
    CHECK (body.find ("safe->openLynxProject (backup, withoutPlugins);") != std::string::npos);
    const auto app = i18ntest::readFile ("src/Main.cpp");
    CHECK (app.find ("mainWindow->getContent().setRecoveryPlugin (uncleanCause().editorPlugin);") != std::string::npos);
}

// Crash reports since 0.1.31.2, G1: one Windows machine hung at every start inside its ASIO driver's open (6 starts in a
// row, 0.1.30 to 0.1.31.2). The open is a session step now; a session that ended inside it makes the next start skip
// the saved device (and its prewarm), open the default one and save that, with the reason in the start-up notice.
TEST (stab190rp_a_device_that_hung_the_start_is_skipped_next_time)
{
    const auto engine = i18ntest::readFile ("src/engine/AudioEngine.cpp");
    const auto open = engine.find ("void AudioEngine::openAudioDevice()");
    const auto note = engine.find ("crashguard::note (std::string (openingDeviceStep)", open);
    const auto init = engine.find ("deviceManager.initialise (0, 2, saved.get(), true)", open);
    const auto done = engine.find ("crashguard::note (\"audio device opened\");", open);
    CHECK (open != std::string::npos && note > open && init > note && done > init);
    CHECK (engine.find ("const auto saved = skipSavedDevice ? std::unique_ptr<juce::XmlElement>()") != std::string::npos);
    const auto prewarm = engine.find ("void AudioEngine::prewarmAudioDevice()");
    CHECK (prewarm != std::string::npos && engine.find ("if (skipSavedDevice)", prewarm) < engine.find ("prewarmAsioDevice", prewarm));
    const auto app = i18ntest::readFile ("src/Main.cpp");
    CHECK (app.find ("if (uncleanDevice = uncleanDeviceOpen(); uncleanDevice.isNotEmpty())") != std::string::npos);
    CHECK (app.find ("lynx::AudioEngine::skipSavedDevice = true;") != std::string::npos);
}

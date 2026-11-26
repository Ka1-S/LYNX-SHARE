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

// One session marker per running LYNX (round 190rp, coordinator item 3): a crashed window's session is found while other
// LYNX windows run; an older LYNX's single marker is read as before.
TEST (stab190rp_session_markers_per_process)
{
    using namespace lynx::safety;
    SessionMarker a, b, legacy, broken;
    CHECK (parseSessionMarker (sessionMarkerText (1000, 41, 7), a) && a.startMs == 1000 && a.pid == 41 && a.processStart == 7 && ! a.legacy);
    CHECK (parseSessionMarker (sessionMarkerText (2000, 42, 0), b) && b.pid == 42);
    CHECK (parseSessionMarker ("1795700000000\n", legacy) && legacy.legacy && legacy.startMs == 1795700000000LL);
    CHECK (! parseSessionMarker ("nothing here", broken));
    auto alive = [] (std::uint32_t pid, std::uint64_t) { return pid == 41; }; // (41 runs, 42 crashed)
    CHECK (newestEndedSession ({ a, b }, alive) == 2000);
    CHECK (newestEndedSession ({ a }, alive) == -1);
    CHECK (newestEndedSession ({ a, b, legacy }, alive) == 1795700000000LL);
    const auto main = i18ntest::readFile ("src/ui/MainComponent.cpp");
    CHECK (main.find ("InterProcessLock> (\"LYNX-ui-session\")") == std::string::npos);
    CHECK (main.find ("\"session-running-\" + juce::String (crashguard::processId())") != std::string::npos);
}

// Every abort that is not a heap corruption gets the emergency save (round 190rp, coordinator item 2): glibc's other
// fatal aborts ("FATAL: exception not rethrown", after a thread was killed) were taken for heap corruptions.
TEST (stab190rp_only_the_allocators_abort_skips_the_save)
{
    const auto guard = i18ntest::readFile ("src/platform/CrashGuard.cpp");
    const auto at = guard.find ("bool heapCorruptionCrash (void* exceptionPointers) noexcept");
    CHECK (at != std::string::npos && guard.find ("return allocatorMessage (abortMessage());", at) != std::string::npos);
    CHECK (guard.find ("RaiseException (abortExceptionCode, EXCEPTION_NONCONTINUABLE, 0, nullptr)") != std::string::npos);
}

// Thief's frozen mix did not null against live (round 190rp, coordinator item 4): per channel, only the Harmless bass
// differed (+2.3 dB; -135 dB with its volume / pan neutral on both sides). Harmless and Harmor add their level / pan
// modulators to FL's channel level, so a stem rendered at unity and scaled after is another sound: their channel freeze
// renders the volume / pan into the stems (FreezeState::levels), which then play at unity.
TEST (stab190rp_a_harmless_channel_freezes_with_its_levels)
{
    auto song = freeze190aySong();
    auto& h = song.channels[0];
    h.instrument = lynx::InstrumentKind::harmless;
    lynx::banks::bankOf (lynx::InstrumentKind::harmless).front().apply (h);
    h.volume = 0.55f;
    h.pan = -0.35f;
    CHECK (lynx::freeze::levelsShapeSound (h) && ! lynx::freeze::levelsShapeSound (song.channels[1]));
    const auto end = song.songLength();

    // what sessionpack::renderFreeze does for it (the whole song with the channel's levels)
    auto frozen = song;
    Freeze190ayFiles files;
    {
        const auto tps = lynx::ticksPerSample (song.bpm, freeze190ayRate);
        const auto tail = lynx::standins::tailTicks (song);
        auto renderSong = lynx::standins::channelTimelineRender (song, 0, false, true);
        CHECK (renderSong.channels[0].volume == 0.55f && renderSong.channels[0].pan == -0.35f && renderSong.channels[0].mixerTrack == 0);
        lynx::freeze::lighten (renderSong, 0);
        std::int64_t music = 0;
        const auto exported = lynx::makeExportSong (renderSong, true, tail, music);
        files["h whole"] = freeze190ayCut (freeze190ayRender (exported, music + tail, {}, true), tps, 0, music + tail);
        lynx::StandIn s;
        s.path = "h whole";
        s.lengthTicks = song.songLength() + tail;
        lynx::freeze::apply (frozen, frozen.channels[0].uid, { s }, "test");
    }
    CHECK (frozen.channels[0].freeze.on() && frozen.channels[0].freeze.levels && frozen.channels[0].freeze.insert == 0);
    const auto played = lynx::standins::project (frozen, [] (const lynx::PluginRef&) { return false; });
    CHECK (played.channels[0].instrument == lynx::InstrumentKind::audioClip && played.channels[0].volume == 1.0f && played.channels[0].pan == 0.0f);
    CHECK (played.channels[0].mixerTrack == song.channels[0].mixerTrack); // (the insert stays live)
    auto provider = [&] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> {
        auto it = files.find (path);
        return it != files.end() ? it->second : nullptr;
    };
    auto soloLive = song, soloIce = played;
    for (std::size_t c = 1; c < soloLive.channels.size(); ++c)
        soloLive.channels[c].muted = true;
    for (std::size_t c = 1; c < soloIce.channels.size(); ++c)
        soloIce.channels[c].muted = true;
    const auto alone = freeze190ayCompare (freeze190ayRender (soloLive, end, {}, true), freeze190ayRender (soloIce, end, provider, true));
    std::printf ("  Harmless frozen with its levels, alone: peak %.4f, residual %.1f dB\n", alone.peak, alone.dB());
    CHECK (alone.peak > 0.01f && alone.dB() < -100.0);

    // (what freezeSource and the project file do with it: the --selftest check "stab190rp")
}

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

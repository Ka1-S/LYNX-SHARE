// Round 190rj (0.1.32 regression sweep 2): one test per defect found and fixed.

// "Nothing to play in Song mode yet" came up over the window when the first take was recorded into an empty Playlist
TEST (sweep190rj_no_empty_song_tip_while_recording)
{
    const auto main = i18ntest::readFile ("src/ui/MainComponent.cpp");
    const auto at = main.find ("maybeShowTip (onboarding::Tip::songModeEmpty, &songModeButton);");
    CHECK (at != std::string::npos);
    const auto line = main.rfind ("if (onboardingOn", at);
    CHECK (line != std::string::npos && main.substr (line, at - line).find ("! engine.isRecording()") != std::string::npos);
}

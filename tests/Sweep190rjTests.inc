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

// A guest of 0.1.31.x still joins (same protocol) but keeps his own tempo / project settings: the host is told who
// should update (round 190rj).
TEST (sweep190rj_session_old_guest_named_to_the_host)
{
    using namespace lynx;
    CHECK (live::keepsOwnSettings ("0.1.31.2", "0.1.32"));
    CHECK (live::keepsOwnSettings ("0.1.31", "0.1.32"));
    CHECK (live::keepsOwnSettings ("0.1.30", "0.1.32.1"));
    CHECK (! live::keepsOwnSettings ("0.1.32", "0.1.32"));
    CHECK (! live::keepsOwnSettings ("0.1.32.1", "0.1.32"));
    CHECK (! live::keepsOwnSettings ("0.2.0", "0.1.32"));
    CHECK (! live::keepsOwnSettings ("", "0.1.32"));
    CHECK (! live::keepsOwnSettings ("0.1.31.2", "0.1.31.2")); // (the same version: silent)
    const auto src = i18ntest::readFile ("src/engine/LiveSessions.cpp");
    const auto accept = src.find ("void LiveSessionManager::accept (int from)");
    const auto hint = src.find ("live::keepsOwnSettings (utf8 (peer.version), JUCE_APPLICATION_VERSION_STRING)", accept);
    CHECK (accept != std::string::npos && hint != std::string::npos && hint < src.find ("\n}\n", accept));
}

